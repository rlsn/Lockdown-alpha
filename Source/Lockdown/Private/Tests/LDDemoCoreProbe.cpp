#include "Tests/LDDemoEntryProbe.h"
#include "Core/LDLog.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Character/LDNativeMotionTypes.h"
#include "Components/CapsuleComponent.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDemoContentCatalog.h"
#include "Region/LDMapRegistry.h"
#include "Region/LDMapGeometry.h"
#include "World/LDSpawnPlacement.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/GameNetworkManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"

class FLDDemoCoreProbe : public TSharedFromThis<FLDDemoCoreProbe>
{
public:
    TWeakObjectPtr<ALDPlayerController> Controller;
    FString Output, Role, Error, CatalogDigest;
    double Deadline=0, StageAt=0, ArrivalReceivedAt=0;
    int32 Stage=0;
    bool Written=false, Passed=true, StopSampled=false, IdleVerified=false, DisconnectVerified=false;
    FLDCombatBodyRef OriginalBody;
    FLDMoveIdentity OldOrder;
    FVector StartPosition=FVector::ZeroVector;
    TMap<FGuid,FVector> StoppedBodies;
    TArray<TSharedPtr<FJsonValue>> Checks;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    uint64 AuditedMotionBatches=0, AuditedNativeRecords=0, AuditedRevertedTrials=0;
    bool CheckedMotionAckRefusals=false;
    // Normal Start opens only from provider wiring; the fixture must leave that gate exactly as it found it.
    bool bStartGateOpenBefore=false;
    FString StartGateBefore;

    bool AuditNativeMotion(ALDGameMode& Mode)
    {
        for (auto& Pair : Mode.Lives)
        {
            auto& Life=Pair.Value;
            if (Life.NativeMotion.IsEmpty()) { continue; }
            uint64 Expected=Life.MotionAckSequence;
            uint64 LastStep=Life.MotionAckStep;
            for (const auto& Batch : Life.NativeMotion)
            {
                if (!Batch || Batch->Step.Match!=Mode.GetMatchContext() || Batch->Body!=Life.MotionBody
                    || Batch->StreamId!=Life.MotionStreamId || Batch->PreviousSequence!=Expected
                    || Batch->Step.StepSerial<=LastStep)
                { Error=TEXT("Native audit found a stale stream, sequence gap or reused step."); Passed=false; return false; }
                FVector Foot=Batch->StartFootCm;
                double Time=Batch->Step.StartServerSeconds;
                for (const auto& Record : Batch->Records)
                {
                    if (Expected==MAX_uint64 || Record.Sequence!=++Expected
                        || !Record.FromFootCm.Equals(Foot,.000001) || Record.StartServerSeconds<Time
                        || Record.EndServerSeconds<Record.StartServerSeconds || Record.EndServerSeconds>Batch->Step.EndServerSeconds)
                    { Error=TEXT("Native audit found a discontinuous or backdated committed record."); Passed=false; return false; }
                    Foot=Record.ToFootCm;
                    Time=Record.EndServerSeconds;
                    ++AuditedNativeRecords;
                }
                if (Expected!=Batch->ThroughSequence || !Foot.Equals(Batch->EndFootCm,.000001))
                { Error=TEXT("Native audit did not reach the batch's actual final pose."); Passed=false; return false; }
                LastStep=Batch->Step.StepSerial;
                ++AuditedMotionBatches;
                AuditedRevertedTrials+=Batch->RevertedTrials;
            }
            if (!CheckedMotionAckRefusals)
            {
                const int32 Retained=Life.NativeMotion.Num();
                FString Refusal;
                auto WrongBody=Life.MotionBody;
                WrongBody.BodyIdentity=FGuid::NewGuid();
                Check(!Mode.AcknowledgeNativeMotion(Mode.GetMatchContext(),WrongBody,LastStep,Expected,Refusal)
                    && !Refusal.IsEmpty() && Life.NativeMotion.Num()==Retained,
                    TEXT("Wrong body acknowledgement cannot discard retained native evidence"));
                Refusal.Reset();
                Check(LastStep<MAX_uint64
                    && !Mode.AcknowledgeNativeMotion(Mode.GetMatchContext(),Life.MotionBody,LastStep+1,Expected,Refusal)
                    && !Refusal.IsEmpty() && Life.NativeMotion.Num()==Retained,
                    TEXT("Future acknowledgement cannot invent a completed native prefix"));
                CheckedMotionAckRefusals=true;
                if (!Passed) { return false; }
            }
            if (!Mode.AcknowledgeNativeMotion(Mode.GetMatchContext(),Life.MotionBody,LastStep,Expected,Error))
            { Passed=false; return false; }
        }
        return true;
    }

    bool Check(bool Value, const TCHAR* Name)
    {
        auto Row=MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("name"),Name);
        Row->SetBoolField(TEXT("passed"),Value);
        Checks.Add(MakeShared<FJsonValueObject>(Row));
        Passed&=Value;
        if (!Value && Error.IsEmpty()) { Error=Name; }
        return Value;
    }

    bool Save(const TSharedRef<FJsonObject>& Value, const FString& Name)
    {
        FString Json;
        const FString File=FPaths::Combine(Output,Name);
        return FJsonSerializer::Serialize(Value,TJsonWriterFactory<>::Create(&Json))
            && FFileHelper::SaveStringToFile(Json,*(File+TEXT(".tmp")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
            && IFileManager::Get().Move(*File,*(File+TEXT(".tmp")),true,true);
    }

    bool Mark(const FString& Name)
    {
        auto Value=MakeShared<FJsonObject>();
        Value->SetBoolField(TEXT("ready"),true);
        if (Save(Value,Name)) { return true; }
        Error=TEXT("Could not persist isolated native probe marker: ")+Name;
        Passed=false;
        return false;
    }

    bool AllMarkers(const TCHAR* Suffix) const
    {
        for (const TCHAR* Name : {TEXT("Host"),TEXT("Client1"),TEXT("Client2")})
        {
            if (!FPaths::FileExists(FPaths::Combine(Output,FString(Name)+Suffix))) { return false; }
        }
        return true;
    }

    bool Initialize(ALDGameMode& Mode, ULDMapRegistry& Registry)
    {
        bStartGateOpenBefore=Mode.CheckDemoStartInputs(StartGateBefore);
        Error.Reset();
        const double WorldSeconds=Mode.GetWorld()->GetTimeSeconds();
        Mode.MatchPhase=ELDMatchPhase::Starting;
        if (!Check(Mode.InitializeAuthorityCore(Error),TEXT("Fixture uses the real core initializer and compiled content"))) { return false; }
        // Every registered provider prepares this match exactly as a normal start would.
        FLDMatchStartContext Start;
        Start.Match=Mode.GetMatchContext();
        Start.StartedAtMs=Mode.MatchStartedAtMs;
        Start.OpenAtMs=Mode.CaptureReducer.GetOpenAtMs();
        for (const FGuid& Id : Mode.RosterOrder) { if (Mode.IsMemberPresent(Id)) { Start.Humans.Add(Id); } }
        bool bPrepared=true;
        for (int32 Index=0; bPrepared && Index<int32(ELDMatchProvider::Count); ++Index)
        {
            const FLDMatchProviderHooks* Hooks=LDMatchProviders::Find(ELDMatchProvider(Index));
            bPrepared=!Hooks || !Hooks->Prepare || Hooks->Prepare(Mode,Start,Error);
        }
        if (!Check(bPrepared,TEXT("Fixture prepares every registered provider like a normal start"))) { return false; }
        const FName ShopId(*Content->Get().World.ShopId);
        const auto* Shop=Registry.Resolve(ShopId,Registry.Generation);
        if (!Shop) { Error=TEXT("Shop geometry fixture has no approved anchor."); return false; }
        const auto& ShopRadius=Content->Get().Parameters.ShopInteractRadiusCm;
        const double Radius=double(ShopRadius.Numerator)/double(ShopRadius.Denominator);
        bool WithinReach=false;
        ELDWorldQueryFailure ShopFailure;
        FString ShopError;
        if (!Check(Registry.QueryShopReach(Registry.Revision(),Shop->Position+FVector(Radius-1,0,0),
            WithinReach,ShopFailure,ShopError) && WithinReach && ShopFailure==ELDWorldQueryFailure::None,
            TEXT("Registered shop reach accepts a legal in-range foot point"))) { if (!ShopError.IsEmpty()) { Error=ShopError; } return false; }
        if (!Check(Registry.QueryShopReach(Registry.Revision(),Shop->Position+FVector(Radius,0,0),
            WithinReach,ShopFailure,ShopError) && WithinReach,
            TEXT("Registered shop reach includes its exact approved radius"))) { if (!ShopError.IsEmpty()) { Error=ShopError; } return false; }
        if (!Check(Registry.QueryShopReach(Registry.Revision(),Shop->Position+FVector(Radius+1,0,0),
            WithinReach,ShopFailure,ShopError) && !WithinReach && ShopFailure==ELDWorldQueryFailure::None && ShopError.IsEmpty(),
            TEXT("Outside shop range is a successful false query, not a world failure"))) { if (!ShopError.IsEmpty()) { Error=ShopError; } return false; }
        if (!Check(!Registry.QueryShopReach(Registry.Revision(),Shop->Position+FVector(Radius,0,50),
            WithinReach,ShopFailure,ShopError) && !WithinReach && ShopFailure==ELDWorldQueryFailure::NotGrounded && !ShopError.IsEmpty(),
            TEXT("Invalid shop support is explicitly rejected rather than treated as out of range"))) { if (!ShopError.IsEmpty()) { Error=ShopError; } return false; }
        auto StaleShopWorld=Registry.Revision();
        ++StaleShopWorld.GeometryRevision;
        if (!Check(!Registry.QueryShopReach(StaleShopWorld,Shop->Position+FVector(Radius,0,0),
            WithinReach,ShopFailure,ShopError) && !WithinReach && ShopFailure==ELDWorldQueryFailure::StaleWorld && !ShopError.IsEmpty(),
            TEXT("Shop reach rejects a stale registered world before range evaluation"))) { if (!ShopError.IsEmpty()) { Error=ShopError; } return false; }
        bool Registered=Mode.ExecuteAuthorityPhase(*Mode.Combat,ELDCommitPhase::RespawnsAndWorld,
            [&](const FLDGameplayWritePermit& Permit)
        {
            const FGuid FirstId=Mode.RosterOrder[0];
            auto& FirstLife=Mode.Lives.FindChecked(FirstId);
            FirstLife.SpawnSequence=1;
            FirstLife.SpawnForLife=1;
            const FLDSpawnRequestKey FirstRequest{Mode.GetMatchContext(),FirstId,1,1};
            TSharedPtr<const FLDSpawnSelection> FirstSelection;
            FCollisionShape Capsule;
            if (!ULDMapRegistry::ContentCapsule(*Content,Capsule,Error)
                || !Registry.DrawSpawnAnchor(FirstRequest,Mode.SpawnRandom,Permit,FirstSelection,Error)) { return false; }
            const auto* FirstAnchor=Registry.Resolve(FirstSelection->AnchorId(),Registry.Generation);
            const auto* SpawnGeometry=Registry.GetGeometry();
            if (!FirstAnchor || !SpawnGeometry) { Error=TEXT("Spawn fixture anchor is missing."); return false; }
            TArray<TWeakObjectPtr<APawn>> Occupants;
            const double FloorGap=(UCharacterMovementComponent::MIN_FLOOR_DIST+UCharacterMovementComponent::MAX_FLOOR_DIST)*.5;
            ON_SCOPE_EXIT
            {
                for (const auto& Weak : Occupants)
                {
                    if (auto* Pawn=Weak.Get())
                    {
                        Pawn->SetActorEnableCollision(false);
                        Pawn->Destroy();
                    }
                }
            };
            for (const auto& Point : SpawnGeometry->SpawnOffsetsCm)
            {
                FVector Offset;
                if (!Point.ToVector(Offset,Error)) { return false; }
                auto* Pawn=Mode.GetWorld()->SpawnActor<APawn>();
                if (!Pawn) { Error=TEXT("Spawn occupancy fixture allocation failed."); return false; }
                Occupants.Add(Pawn);
                Pawn->SetActorHiddenInGame(true);
                auto* Shape=NewObject<UCapsuleComponent>(Pawn);
                Pawn->SetRootComponent(Shape);
                Pawn->AddInstanceComponent(Shape);
                Shape->SetCapsuleSize(Capsule.GetCapsuleRadius(),Capsule.GetCapsuleHalfHeight());
                Shape->SetWorldLocation(FirstAnchor->Position+Offset+FVector(0,0,Capsule.GetCapsuleHalfHeight()+FloorGap));
                Shape->SetCollisionObjectType(ECC_Pawn);
                Shape->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
                Shape->SetCollisionResponseToChannels(GetDefault<ALDCharacter>()->GetCapsuleComponent()->GetCollisionResponseToChannels());
                Shape->SetCanEverAffectNavigation(false);
                Shape->RegisterComponentWithWorld(Mode.GetWorld());
            }
            bool AllOccupied=true;
            for (const auto& Weak : Occupants)
            {
                const auto* Pawn=Weak.Get();
                AllOccupied&=Pawn && Mode.GetWorld()->OverlapAnyTestByObjectType(Pawn->GetActorLocation(),
                    FQuat::Identity,FCollisionObjectQueryParams(ECC_Pawn),Capsule,
                    FCollisionQueryParams(SCENE_QUERY_STAT(DemoSpawnOccupancyFixture)));
            }
            if (!Check(AllOccupied,TEXT("All seven fixture bodies have native query collision despite ignoring Pawn contacts"))) { return false; }
            TSharedPtr<const FLDSpawnReservation> Blocked;
            FString BlockedError;
            const int32 BlockedSeed=Mode.SpawnRandom.GetCurrentSeed();
            const bool ReservedBlocked=Registry.ReserveSpawnPlacement(FirstSelection,Capsule,Permit,Blocked,BlockedError);
            const bool CorrectlyBlocked=!ReservedBlocked
                && !Blocked.IsValid() && BlockedError.Contains(TEXT("SpawnBlocked"))
                && FirstLife.Life==0 && Mode.SpawnRandom.GetCurrentSeed()==BlockedSeed;
            if (!CorrectlyBlocked)
            { Error=FString::Printf(TEXT("Occupied spawn: reserved=%d life=%u seed=%d expectedSeed=%d error=%s"),
                ReservedBlocked,FirstLife.Life,Mode.SpawnRandom.GetCurrentSeed(),BlockedSeed,*BlockedError); }
            if (!Check(CorrectlyBlocked,
                TEXT("Seven physically occupied offsets fail without advancing life or redrawing"))) { return false; }
            for (const auto& Weak : Occupants)
            {
                if (auto* Pawn=Weak.Get()) { Pawn->SetActorEnableCollision(false); Pawn->Destroy(); }
            }
            Occupants.Reset();
            TSharedPtr<const FLDSpawnSelection> RetrySelection;
            TSharedPtr<const FLDSpawnReservation> Retry;
            if (!Check(Registry.DrawSpawnAnchor(FirstRequest,Mode.SpawnRandom,Permit,RetrySelection,Error)
                && RetrySelection==FirstSelection && Mode.SpawnRandom.GetCurrentSeed()==BlockedSeed
                && Registry.ReserveSpawnPlacement(RetrySelection,Capsule,Permit,Retry,Error)
                && Retry->OffsetIndex()==0 && FirstLife.Life==0,
                TEXT("Unblocked retry reuses the original anchor and starts at its centre"))) { return false; }
            if (!Registry.ReleaseSpawnReservation(Retry,Error)) { return false; }
            if (!Mode.SpawnInitialContenders(Permit,Error)) { return false; }
            const auto ShopDenied=Registry.QueryShopEligibility(Permit,Mode.Lives.FindChecked(FirstId).Body,
                ShopId,Registry.Revision());
            if (!Check(!ShopDenied.Succeeded && ShopDenied.CommitFailure==ELDCommitReject::WrongPhase && !ShopDenied.Error.IsEmpty(),
                TEXT("A valid spawned buyer cannot reuse a spawn permit for a shop transaction"))) { return false; }
            FString DuplicateError;
            if (!Check(!Mode.SpawnInitialContenders(Permit,DuplicateError) && !DuplicateError.IsEmpty(),
                TEXT("Repeated roster spawn cannot grant another life or resource reset"))) { return false; }
            for (const auto& Pair : Mode.Lives)
            {
                const auto& Life=Pair.Value;
                // Camp members spawn at their camps, not at the contender anchors.
                if (Life.Kind!=ELDVictimKind::Contender) { continue; }
                const auto* Anchor=Registry.Resolve(Life.SpawnAnchor,Registry.Generation);
                const auto* Body=Life.Actor.Get();
                FVector Offset;
                if (!Check(Anchor && SpawnGeometry && SpawnGeometry->SpawnOffsetsCm.IsValidIndex(Life.SpawnOffset)
                    && SpawnGeometry->SpawnOffsetsCm[Life.SpawnOffset].ToVector(Offset,Error)
                    && Body && FVector::DistSquared2D(Body->GetActorLocation(),Anchor->Position+Offset)<.000001,
                    TEXT("Actual initial body uses one of the seven exact offsets at its fixed selected anchor"))) { return false; }
                const int32 Seed=Mode.SpawnRandom.GetCurrentSeed();
                FLDSpawnRequestKey Request{Mode.GetMatchContext(),Pair.Key,1,Life.SpawnSequence};
                TSharedPtr<const FLDSpawnSelection> Replay;
                if (!Check(Registry.DrawSpawnAnchor(Request,Mode.SpawnRandom,Permit,Replay,Error)
                    && Replay->AnchorId()==Life.SpawnAnchor && Mode.SpawnRandom.GetCurrentSeed()==Seed,
                    TEXT("Replayed spawn selection does not consume another random draw"))) { return false; }
                ++Request.ProposedLife;
                TSharedPtr<const FLDSpawnSelection> Conflict;
                FString ConflictError;
                if (!Check(!Registry.DrawSpawnAnchor(Request,Mode.SpawnRandom,Permit,Conflict,ConflictError)
                    && !Conflict.IsValid() && !ConflictError.IsEmpty() && Mode.SpawnRandom.GetCurrentSeed()==Seed,
                    TEXT("Same spawn request with changed terms cannot redraw a safer anchor"))) { return false; }
            }
            return true;
        },Error);
        if (!Check(Registered,TEXT("Three real controllers receive one committed body each"))) { return false; }
        Mode.MatchPhase=ELDMatchPhase::Development;
        Mode.PublishLobby();
        Check(WorldSeconds==Mode.GetWorld()->GetTimeSeconds(),TEXT("Core initialization does not rewrite UWorld time"));
        return Passed;
    }

    bool Finish()
    {
        auto Report=MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"),Passed);
        Report->SetBoolField(TEXT("fixture_only"),true);
        Report->SetBoolField(TEXT("full_ct06"),false);
        Report->SetStringField(TEXT("scope"),TEXT("DEV-only three-owned-body initial random spawn/reservation, core/ground/CMC and native-prefix audit fixture. Not a playable match, death/respawn, final motion Fence, vision, combat effects or 3+3 AI acceptance."));
        Report->SetStringField(TEXT("role"),Role);
        Report->SetStringField(TEXT("catalog_sha256"),CatalogDigest);
        Report->SetStringField(TEXT("detail"),Error);
        Report->SetNumberField(TEXT("audited_motion_batches"),double(AuditedMotionBatches));
        Report->SetNumberField(TEXT("audited_native_records"),double(AuditedNativeRecords));
        Report->SetNumberField(TEXT("audited_reverted_trials"),double(AuditedRevertedTrials));
        Report->SetArrayField(TEXT("checks"),Checks);
        if (const auto* PC=Controller.Get())
        {
            Report->SetNumberField(TEXT("generation"),PC->OwnerState.Match);
            Report->SetStringField(TEXT("entity"),PC->OwnerState.BodyRef.Victim.EntityId.ToString(EGuidFormats::Digits));
            Report->SetStringField(TEXT("body"),PC->OwnerState.BodyRef.BodyIdentity.ToString(EGuidFormats::Digits));
            Report->SetNumberField(TEXT("life"),PC->OwnerState.Life);
            Report->SetStringField(TEXT("navigation_status"),PC->Navigation->Status);
            if (const auto* Body=Cast<ALDCharacter>(PC->GetPawn()))
            {
                const auto* Movement=CastChecked<ULDCharacterMovementComponent>(Body->GetCharacterMovement());
                Report->SetStringField(TEXT("foot"),Body->GetNavAgentLocation().ToString());
                Report->SetNumberField(TEXT("corrections"),Movement->CorrectionCount);
                Report->SetNumberField(TEXT("max_correction_cm"),Movement->MaxCorrectionCm);
            }
            if (const auto* Mode=PC->GetWorld()->GetAuthGameMode<ALDGameMode>())
            {
                Report->SetNumberField(TEXT("simulation_seconds"),Mode->GetAuthorityNowSeconds());
                TArray<TSharedPtr<FJsonValue>> Rows;
                int32 CampBodies=0;
                for (const auto& Pair : Mode->Lives)
                {
                    const auto* Body=Pair.Value.Actor.Get();
                    const auto* Movement=Body?Cast<ULDCharacterMovementComponent>(Body->GetCharacterMovement()):nullptr;
                    if (!Body || !Movement) { continue; }
                    // Owned projections bind only to contender bodies; camp members are counted as world population.
                    if (Pair.Value.Kind!=ELDVictimKind::Contender) { ++CampBodies; continue; }
                    auto Row=MakeShared<FJsonObject>();
                    Row->SetStringField(TEXT("entity"),Pair.Key.ToString(EGuidFormats::Digits));
                    Row->SetStringField(TEXT("body"),Pair.Value.Body.BodyIdentity.ToString(EGuidFormats::Digits));
                    Row->SetNumberField(TEXT("life"),Pair.Value.Life);
                    Row->SetStringField(TEXT("spawn_anchor"),Pair.Value.SpawnAnchor.ToString());
                    Row->SetNumberField(TEXT("spawn_offset"),Pair.Value.SpawnOffset);
                    Row->SetNumberField(TEXT("native_integrations"),double(Movement->AuthorityIntegrationCount));
                    Row->SetNumberField(TEXT("integrated_seconds"),Movement->AuthorityIntegratedSeconds);
                    Row->SetNumberField(TEXT("network_bookkeeping"),double(Movement->NetworkBookkeepingMoves));
                    Row->SetNumberField(TEXT("suppressed_network_simulations"),double(Movement->SuppressedNetworkSimulationMoves));
                    Row->SetNumberField(TEXT("rejected_authority_moves"),double(Movement->RejectedAuthorityMoves));
                    Rows.Add(MakeShared<FJsonValueObject>(Row));
                }
                Report->SetArrayField(TEXT("authority_bodies"),Rows);
                Report->SetNumberField(TEXT("camp_bodies"),CampBodies);
            }
        }
        Written=Save(Report,Role+TEXT(".json"));
        UE_LOG(LogLDTest,Display,TEXT("LD_DEMO_CORE_PROBE role=%s passed=%d detail=%s"),*Role,Passed&&Written,*Error);
        if (!Passed || !Written) { FPlatformMisc::RequestExitWithStatus(false,2); return false; }
        return true;
    }

    bool Tick()
    {
        if (auto* PC=Controller.Get())
        {
            if (auto* Mode=PC->GetWorld()->GetAuthGameMode<ALDGameMode>(); Mode && !AuditNativeMotion(*Mode))
            {
                if (Written) { FPlatformMisc::RequestExitWithStatus(false,2); return false; }
                return Finish();
            }
        }
        if (Written)
        {
            if (Role!=TEXT("Host") && FPaths::FileExists(FPaths::Combine(Output,TEXT("FinishClients"))))
            { FPlatformMisc::RequestExitWithStatus(false,0); return false; }
            if (Role==TEXT("Host"))
            {
                auto* PC=Controller.Get();
                auto* Mode=PC?PC->GetWorld()->GetAuthGameMode<ALDGameMode>():nullptr;
                auto* Driver=PC?PC->GetWorld()->GetNetDriver():nullptr;
                if (!DisconnectVerified && Mode && Driver && Driver->ClientConnections.IsEmpty()
                    && FPaths::FileExists(FPaths::Combine(Output,TEXT("FinishClients"))))
                {
                    int32 Left=0, Alive=0;
                    bool Retired=true;
                    for (const auto& Pair : Mode->Lives)
                    {
                        if (Pair.Value.Kind!=ELDVictimKind::Contender) { continue; }
                        if (Pair.Value.Phase==ELDLifePhase::Left)
                        {
                            ++Left;
                            Retired&=Pair.Value.RetiredThroughLife==1 && !Pair.Value.Body.IsValid() && !Pair.Value.Actor.IsValid();
                        }
                        else if (Pair.Value.Phase==ELDLifePhase::Alive) { ++Alive; }
                    }
                    auto Result=MakeShared<FJsonObject>();
                    const bool Clean=Left==2 && Alive==1 && Retired && Mode->IsGameplayWriteOpen(Mode->GetMatchContext());
                    Result->SetBoolField(TEXT("passed"),Clean);
                    Result->SetNumberField(TEXT("retired_clients"),Left);
                    Result->SetNumberField(TEXT("remaining_live_bodies"),Alive);
                    Result->SetStringField(TEXT("scope"),TEXT("Real client disconnect retires body/resource ownership even when native Pawn teardown precedes Logout; the Host remains active."));
                    if (!Save(Result,TEXT("Disconnects.json")) || !Clean)
                    {
                        UE_LOG(LogLDTest,Error,TEXT("LD_DEMO_CORE_DISCONNECT_FAILED"));
                        FPlatformMisc::RequestExitWithStatus(false,2);
                        return false;
                    }
                    DisconnectVerified=true;
                }
                if (FPaths::FileExists(FPaths::Combine(Output,TEXT("Finish"))))
                { FPlatformMisc::RequestExitWithStatus(false,0); return false; }
            }
            return true;
        }
        if (FPlatformTime::Seconds()>=Deadline)
        {
            Check(false,TEXT("Owned-body scenario completed within its bounded deadline"));
            Error=TEXT("Native core fixture timed out at stage ")+LexToString(Stage);
            return Finish();
        }
        auto* PC=Controller.Get();
        auto* World=PC?PC->GetWorld():nullptr;
        auto* Public=World?World->GetGameState<ALDGameState>():nullptr;
        auto* Registry=World?World->GetSubsystem<ULDMapRegistry>():nullptr;
        if (!PC || !World || !Public || !Registry || !Registry->Enabled()) { return true; }
        if (!Content && (!LDDemoEntry::LoadRuntimeContent(Content,Error)
            || !LDDemoEntry::VerifyRuntimeFiles(Error,&CatalogDigest))) { Passed=false; return Finish(); }
        auto* Mode=World->GetAuthGameMode<ALDGameMode>();
        if (Stage==0)
        {
            if (!PC->IsLocalController() || !PC->GetLocalPlayer() || !Public->DemoLobby.Generation) { return true; }
            if (!Mark(Role+TEXT("Joined.json"))) { return Finish(); }
            if (Role==TEXT("Host"))
            {
                if (!Mark(TEXT("HostReady.json"))) { return Finish(); }
                if (!AllMarkers(TEXT("Joined.json")) || Public->DemoLobby.Members.Num()!=3) { return true; }
                Check(Mode && World->GetNetMode()==NM_ListenServer && World->GetNetDriver()
                    && World->GetNetDriver()->ClientConnections.Num()==2,TEXT("Host has two real owning client connections"));
                if (!Passed || !Initialize(*Mode,*Registry)) { Passed=false; return Finish(); }
            }
            else
            {
                Check(World->GetNetMode()==NM_Client && !PC->HasAuthority() && PC->GetNetConnection(),
                    TEXT("Remote body is controlled by a real owning connection"));
            }
            Stage=1;
        }
        auto* Body=Cast<ALDCharacter>(PC->GetPawn());
        auto* Movement=Body?Cast<ULDCharacterMovementComponent>(Body->GetCharacterMovement()):nullptr;
        auto* Nav=PC->Navigation.Get();
        const auto* Goal=Registry->Resolve(TEXT("capture"),Registry->Generation);
        if (!Goal) { Error=TEXT("Approved capture ground is missing."); Passed=false; return Finish(); }
        if (Stage==1)
        {
            int64 DisplayNowMs=0;
            if (!Body || !Movement || !PC->OwnerState.BodyRef.IsValid() || !PC->OwnerState.bActive
                || Public->PublicMatch.Generation!=PC->OwnerState.Match || Public->PublicMatch.Phase!=ELDMatchPhase::Development
                || !Movement->CurrentFloor.IsWalkableFloor()
                || !PC->CanWorldCommand() || !PC->ReadDemoAuthorityTimeMs(DisplayNowMs)) { return true; }
            Check(DisplayNowMs==Public->DemoClock.NowMs && Public->DemoClock.Generation==PC->OwnerState.Match,
                TEXT("Owner display time reads the matching completed Core clock without local extrapolation"));
            Check(Body->GetController()==PC && PC->OwnerState.Life==1 && PC->OwnerState.LifeRevision==1,
                TEXT("Possession, body identity and logical first life agree"));
            Check(PC->OwnerState.Hp*100==Content->Get().Parameters.MaxHP.BaseP2
                && PC->OwnerState.Mp*100==Content->Get().Parameters.MaxMP.BaseP2,
                TEXT("Approved P2 resources are projected from the sole Combat body record"));
            Check(PC->OwnerState.ProtectedUntilMs==0,TEXT("Common initial-body path does not invent respawn protection"));
            Check(PC->CanWorldCommand(),TEXT("Owning connection can submit an ordinary ground intent"));
            Check(Nav->PredictedIdentity().Match==PC->OwnerState.Match && Nav->PredictedIdentity().Life==PC->OwnerState.Life,
                TEXT("Idle saved moves retain the current match and life for native acknowledgements"));
            OriginalBody=PC->OwnerState.BodyRef;
            if (!Passed) { return Finish(); }
            Nav->NavigateGround(Goal->Position,NAME_None);
            Stage=2;
            return true;
        }
        if (!Body || !Movement || PC->OwnerState.BodyRef!=OriginalBody)
        { Error=TEXT("Owned body disappeared or changed identity during movement."); Passed=false; return Finish(); }
        if (Role==TEXT("Host") && !StopSampled && AllMarkers(TEXT("Stopped.json")))
        {
            for (const auto& Pair : Mode->Lives)
            {
                if (const auto* Current=Pair.Value.Actor.Get()) { StoppedBodies.Add(Pair.Key,Current->GetActorLocation()); }
            }
            StopSampled=true;
            if (!Mark(TEXT("ReplayReady.json"))) { return Finish(); }
        }
        if (Stage==2)
        {
            if (Nav->HasMoveOrder()) { return true; }
            const FLDOrderReceipt& Refused=Nav->LatestReceipt();
            Check(Refused.Status==ELDOrderStatus::Rejected && Refused.Failure==ELDOrderFailure::InvalidGround,
                TEXT("Missing explicit target SurfaceHint is refused on the owning RPC"));
            if (!Passed) { return Finish(); }
            StartPosition=Body->GetActorLocation();
            Nav->NavigateGround(Goal->Position,Goal->Surface);
            OldOrder=Nav->LocalIdentity();
            Stage=3;
        }
        else if (Stage==3)
        {
            if (FVector::Dist2D(StartPosition,Body->GetActorLocation())<300) { return true; }
            Check(true,TEXT("The owned body actually moves through native CMC"));
            const FLDMoveIdentity Accepted=Nav->PredictedIdentity();
            Nav->NavigateGround(Goal->Position,Goal->Surface);
            Check(Nav->LocalIdentity()!=Accepted && Nav->PredictedIdentity()==Accepted,
                TEXT("A pending replacement keeps the accepted order identity for prediction and replay"));
            if (!Passed) { return Finish(); }
            StageAt=FPlatformTime::Seconds();
            Stage=31;
        }
        else if (Stage==31)
        {
            if (FPlatformTime::Seconds()-StageAt<.5) { return true; }
            Nav->Stop();
            Stage=4;
        }
        else if (Stage==4)
        {
            const FLDOrderReceipt& Stopped=Nav->LatestReceipt();
            if (Stopped.Identity!=Nav->LocalIdentity() || Stopped.Status!=ELDOrderStatus::Cancelled
                || Stopped.Failure!=ELDOrderFailure::Stopped) { return true; }
            StageAt=FPlatformTime::Seconds();
            Stage=5;
        }
        else if (Stage==5)
        {
            if (FPlatformTime::Seconds()-StageAt<.5) { return true; }
            if (!Mark(Role+TEXT("Stopped.json"))) { return Finish(); }
            Stage=6;
        }
        else if (Stage==6)
        {
            if (!FPaths::FileExists(FPaths::Combine(Output,TEXT("ReplayReady.json")))) { return true; }
            const auto SubmitGround=[Nav,Goal](const FLDMoveIdentity& Identity)
            {
                FLDSpatialOrderRequest Request;
                Request.Identity=Identity;
                Request.Kind=ELDSpatialOrderKind::MoveGround;
                Request.GroundCm=Goal->Position;
                Request.SurfaceHint=Goal->Surface;
                Nav->ServerSubmitOrder(Request);
            };
            SubmitGround(OldOrder);
            Nav->ServerHeartbeat(OldOrder,MAX_uint32);
            auto WrongLife=Nav->LocalIdentity();
            ++WrongLife.Order; ++WrongLife.Life;
            SubmitGround(WrongLife);
            auto WrongMatch=Nav->LocalIdentity();
            ++WrongMatch.Order; ++WrongMatch.Match;
            SubmitGround(WrongMatch);
            if (PC->HasAuthority())
            {
                const auto Count=Movement->AuthorityIntegrationCount;
                const FVector Position=Body->GetActorLocation();
                Movement->PerformMovement(.2f);
                Movement->MoveAutonomous(999.f,.2f,0,FVector(1800,0,0));
                Check(Movement->AuthorityIntegrationCount==Count && Body->GetActorLocation()==Position,
                    TEXT("Direct and remote-style native movement cannot integrate outside the issued step"));
            }
            StageAt=FPlatformTime::Seconds();
            Stage=7;
        }
        else if (Stage==7)
        {
            if (!PC->HasAuthority() && FPlatformTime::Seconds()-StageAt<1)
            { Body->AddMovementInput(FVector::ForwardVector,1); }
            if (FPlatformTime::Seconds()-StageAt<2) { return true; }
            if (!Mark(Role+TEXT("Replayed.json"))) { return Finish(); }
            Stage=8;
        }
        if (Role==TEXT("Host") && Stage>=5 && !IdleVerified)
        {
            if (StopSampled && AllMarkers(TEXT("Replayed.json")))
            {
                for (const auto& Pair : Mode->Lives)
                {
                    // Only the three owned contender bodies are under test; camp members have no owning controller.
                    if (Pair.Value.Kind!=ELDVictimKind::Contender) { continue; }
                    const auto* Current=Pair.Value.Actor.Get();
                    const auto* Position=StoppedBodies.Find(Pair.Key);
                    const auto* Owner=Current?Cast<ALDPlayerController>(Current->GetController()):nullptr;
                    Check(Current && Position && FVector::Dist(*Position,Current->GetActorLocation())<.5
                        && Owner && !Owner->Navigation->IsAuthorityActive(),
                        TEXT("Stale order/life/match, old heartbeat and raw client steering cannot restart stopped authority movement"));
                }
                IdleVerified=true;
                if (!Passed || !Mark(TEXT("Continue.json"))) { return Finish(); }
            }
        }
        if (Stage==8 && FPaths::FileExists(FPaths::Combine(Output,TEXT("Continue.json"))))
        {
            Nav->NavigateGround(Goal->Position,Goal->Surface);
            Stage=9;
        }
        else if (Stage==9 && !Nav->HasMoveOrder() && Nav->LatestReceipt().Status==ELDOrderStatus::Completed
            && Nav->LatestReceipt().Failure==ELDOrderFailure::None)
        {
            // The server completes the order with its own body inside the receipt's acceptance radius. The owning client's
            // predicted foot is only kept within the engine's uncorrected position error of the server's, so that error
            // bounds the client-side distance on top of the radius.
            const FLDOrderReceipt& Arrived=Nav->LatestReceipt();
            const double ClientErrorCm=FMath::Sqrt(GetDefault<AGameNetworkManager>()->MAXPOSITIONERRORSQUARED);
            // The reliable completion receipt can precede the final unreliable CMC correction under packet lag/loss.
            if (ArrivalReceivedAt==0) { ArrivalReceivedAt=FPlatformTime::Seconds(); }
            if (FVector::Dist2D(Body->GetNavAgentLocation(),Goal->Position)>Arrived.AcceptanceCm+ClientErrorCm
                && FPlatformTime::Seconds()-ArrivalReceivedAt<3) { return true; }
            Check(Arrived.Status==ELDOrderStatus::Completed && Arrived.AcceptanceCm>0
                && FVector::Dist2D(Body->GetNavAgentLocation(),Goal->Position)<=Arrived.AcceptanceCm+ClientErrorCm
                && Movement->CurrentFloor.IsWalkableFloor(),TEXT("The accepted complete route reaches its real grounded endpoint"));
            Check(PC->OwnerState.BodyRef==OriginalBody && PC->OwnerState.Life==1,
                TEXT("Navigation and corrections do not allocate a new body or life"));
            if (!Mark(Role+TEXT("Arrived.json"))) { return Finish(); }
            Stage=10;
        }
        if (Stage==10 && AllMarkers(TEXT("Arrived.json")))
        {
            if (Mode)
            {
                Check(AuditedMotionBatches>0 && AuditedNativeRecords>0,
                    TEXT("Explicit DEV audit consumed real native records without pretending to seal a gameplay Fence"));
                for (const auto& Pair : Mode->Lives)
                {
                    if (Pair.Value.Kind!=ELDVictimKind::Contender) { continue; }
                    const auto* Current=Pair.Value.Actor.Get();
                    const auto* Move=Current?Cast<ULDCharacterMovementComponent>(Current->GetCharacterMovement()):nullptr;
                    Check(Move && !Move->IsComponentTickEnabled() && Move->AuthorityIntegrationCount>0
                        && FMath::Abs(Move->AuthorityIntegratedSeconds-Mode->GetAuthorityNowSeconds())<.0001,
                        TEXT("Each body integrates the match interval once, never native tick plus driver"));
                    if (Current && !Current->IsLocallyControlled())
                    {
                        Check(Move && Move->NetworkBookkeepingMoves>0 && Move->SuppressedNetworkSimulationMoves>0,
                            TEXT("Real packed movement still runs native network bookkeeping without double simulation"));
                    }
                    const auto* Resource=Current?Mode->Combat->StateFor(Current):nullptr;
                    Check(Resource && Resource->BasicTiming.IntervalMs>Resource->BasicTiming.WindupMs
                        && Resource->BasicTiming.WindupMs==Content->Get().Parameters.BasicAttackWindupMs,
                        TEXT("Registered body consumes the shared approved basic-timing evaluator"));
                }
                FString Blocker;
                Check(Mode->CheckDemoStartInputs(Blocker)==bStartGateOpenBefore && Blocker==StartGateBefore,
                    TEXT("Mechanism fixture completion leaves the normal Start gate exactly as provider wiring set it"));
            }
            return Finish();
        }
        return true;
    }
};

void LDDemoTests::StartCoreProbe(ALDPlayerController& Controller)
{
    if (!FParse::Param(FCommandLine::Get(),TEXT("LDDemoCoreProbe")) || !Controller.IsLocalController()
        || UWorld::RemovePIEPrefix(Controller.GetWorld()->GetOutermost()->GetName())!=LDDemoEntry::WorldMap) { return; }
    auto Probe=MakeShared<FLDDemoCoreProbe>();
    int32 Timeout=120;
    FParse::Value(FCommandLine::Get(),TEXT("LDDemoCoreTimeout="),Timeout);
    if (!FParse::Value(FCommandLine::Get(),TEXT("LDDemoCoreOutput="),Probe->Output)
        || !FParse::Value(FCommandLine::Get(),TEXT("LDDemoCoreRole="),Probe->Role)
        || (Probe->Role!=TEXT("Host") && Probe->Role!=TEXT("Client1") && Probe->Role!=TEXT("Client2"))
        || Timeout<30 || Timeout>300
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Probe->Output),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(),TEXT("Artifacts"))))
    {
        UE_LOG(LogLDTest,Error,TEXT("LD_DEMO_CORE_PROBE Invalid isolated test arguments."));
        FPlatformMisc::RequestExitWithStatus(false,2);
        return;
    }
    Probe->Controller=&Controller;
    Probe->Deadline=FPlatformTime::Seconds()+Timeout;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float) { return Probe->Tick(); }),.1f);
}
#endif
