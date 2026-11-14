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
#include "Combat/LDTacticalRules.h"
#include "Development/LDDemoContentCatalog.h"
#include "Navigation/LDPathSteering.h"
#include "Region/LDMapRegistry.h"
#include "Region/LDMapGeometry.h"
#include "Vision/LDWorldViewRelay.h"
#include "World/LDSpawnPlacement.h"
#include "UI/LDChoicesPanel.h"
#include "UI/LDHUD.h"
#include "UI/LDUIRoot.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameNetworkManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/AutomationTest.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"

namespace LDDemoCoreProbeDetail
{
    bool CanStartArrival(int32 Slot, bool bHostArrived, bool bClient1Arrived)
    {
        return Slot==0 || (Slot==1 && bHostArrived) || (Slot==2 && bHostArrived && bClient1Arrived);
    }

    bool MatchesCurrentBasicTiming(const FLDBodyCombatState* Body, const FLDDemoContentSnapshot& Catalog)
    {
        if (!Body) { return false; }
        FLDBasicTimingInput Input;
        Input.BaseAttackSpeed=Catalog.Get().Parameters.AttackSpeed.Base;
        Input.CurrentAttackSpeed=Body->AttributeProjection.Final.AttackSpeed;
        Input.BaseWindupMs=Catalog.Get().Parameters.BasicAttackWindupMs;
        const auto Expected=LDTactical::EvaluateBasicTiming(Input,Catalog);
        return Expected.IsSuccess() && Body->BasicTiming.IntervalMs==Expected.Value->IntervalMs
            && Body->BasicTiming.WindupMs==Expected.Value->WindupMs;
    }
}

class FLDDemoCoreProbe : public TSharedFromThis<FLDDemoCoreProbe>
{
public:
    TWeakObjectPtr<ALDPlayerController> Controller;
    FString Output, Role, Error, CatalogDigest;
    double Deadline=0, StageAt=0, ArrivalReceivedAt=0;
    int64 FieldStartedAtMs=0;
    int32 Stage=0;
    bool Written=false, Passed=true, StopSampled=false, IdleVerified=false, DisconnectVerified=false;
    bool bEarlyRestReady=false, bReplayFieldReturn=false, bRestReadySent=false, bRestReadyProjected=false, bRestPresentationSeen=false;
    bool bReadyWindowObserved=false, bLeftFirstService=false, bChoicesPanelOpened=false, bServicePanelOpened=false, bServicePanelClosed=false;
    bool bNpcPurchaseSent=false, bNpcPurchaseApplied=false;
    uint64 NpcPurchaseRevision=0;
    int64 NpcPurchasePointsBefore=0;
    double NpcPurchaseSentAt=0;
    double RestObservationStartedAt=0;
    int32 SharedTrainingStage=0;
    uint32 ObservedRestLife=1;
    uint64 ObservedRestRevision=1;
    bool bSharedTrainingDone=false;
    bool bAuthoredTrainingDone=false;
    int32 AuthoredTrainingStage=0;
    int32 RestServiceStage=0;
    FLDCombatBodyRef OriginalBody;
    FLDMoveIdentity OldOrder, AcceptedOrder, RestServiceOrder;
    FVector StartPosition=FVector::ZeroVector;
    FVector ArrivalGoal=FVector::ZeroVector;
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

    bool ReviewSharedTraining(ALDPlayerController& PC)
        {
            const auto* Relay=PC.LocalView();
            const FLDObservedFrameRead Read=Relay?Relay->ReadObservedFrame():FLDObservedFrameRead();
            if(!Read.Frame||Read.Status!=ELDObservedViewStatus::Current){return true;}
            const auto* Public=PC.GetWorld()->GetGameState<ALDGameState>();
            auto* Registry=PC.GetWorld()->GetSubsystem<ULDMapRegistry>();
            const auto* Slot=Registry?Registry->Resolve(TEXT("training_dummy_0"),Registry->Generation):nullptr;
            const auto* Target=Read.Frame->Units.FindByPredicate([Public,Slot](const FLDObservedUnitView& Unit)
            {
                return Slot&&FVector::DistSquared2D(Unit.FeetCm,Slot->Position)<FMath::Square(300.)
                    &&Unit.Kind==ELDObservedUnitKind::Contender&&Unit.Presentation==0
                    &&!Public->DemoLobby.Members.ContainsByPredicate([&Unit](const FLDLobbyMember& Member)
                    {return Member.ContenderId==Unit.ContenderId;});
            });
            if(!Target){return true;}
            if(SharedTrainingStage==0)
            {
                if(!Mark(Role+TEXT("SharedTargetSeen.json"))){return Finish();}
                SharedTrainingStage=1;
            }
            if(!AllMarkers(TEXT("SharedTargetSeen.json"))){return true;}
            if(Role==TEXT("Client1")&&SharedTrainingStage==1)
            {
                const auto* Body=Cast<ALDCharacter>(PC.GetPawn());
                if(!Check(Body&&Body->PresentationKey!=Target->Presentation,TEXT("The remote attacker does not own the shared target"))){return Finish();}
                if(PC.HUDUI&&PC.HUDUI->ChoicesPanel){PC.HUDUI->ChoicesPanel->ClosePanel();}
                if(!PC.CanWorldCommand()){return true;}
                const auto* Anchor=Registry?Registry->Resolve(TEXT("training_dummy_0"),Registry->Generation):nullptr;
                if(!Check(Anchor!=nullptr,TEXT("The remote trainee approaches the shared target on registered rest ground"))){return Finish();}
                PC.Navigation->NavigateGround(FVector(Target->FeetCm)+FVector(0,-120,0),Anchor->Surface);
                SharedTrainingStage=10;
            }
            if(Role==TEXT("Client1")&&SharedTrainingStage==10)
            {
                const auto* Body=Cast<ALDCharacter>(PC.GetPawn());
                if(!Body||FVector::DistSquared2D(ALDCombatDirector::FootOf(*Body),Target->FeetCm)>FMath::Square(300.)){return true;}
                PC.Navigation->AttackUnit(Target->Handle,Target->ObservationEpoch);
                if(!Mark(TEXT("SharedAttackStarted.json"))){return Finish();}
                SharedTrainingStage=2;
            }
            const bool bStarted=FPaths::FileExists(FPaths::Combine(Output,TEXT("SharedAttackStarted.json")));
            if(Role==TEXT("Client1")&&SharedTrainingStage==2)
            {
                const auto& Receipt=PC.Navigation->LatestReceipt();
                if(Receipt.Kind==ELDSpatialOrderKind::AttackUnit&&Receipt.Status==ELDOrderStatus::Rejected)
                {
                    Error=FString::Printf(TEXT("Shared training attack rejected: failure=%u"),uint32(Receipt.Failure));
                    Check(false,TEXT("A non-owner training attack is admitted through the normal order receipt"));
                    return Finish();
                }
            }
            const FString HitFile=Role+TEXT("SharedHit.json");
            if(bStarted&&Target->Health.CurrentP2<Target->Health.MaxP2&&!FPaths::FileExists(FPaths::Combine(Output,HitFile)))
            {
                Check(Target->Health.CurrentP2>0,TEXT("The same real shared target publishes a non-owner training hit to this connection"));
                auto Hit=MakeShared<FJsonObject>();Hit->SetStringField(TEXT("target"),Target->ContenderId.ToString());
                Hit->SetNumberField(TEXT("hp_p2"),Target->Health.CurrentP2);Hit->SetNumberField(TEXT("max_hp_p2"),Target->Health.MaxP2);
                if(!Save(Hit,HitFile)){Check(false,TEXT("Shared hit evidence persists"));return Finish();}
                if(Role==TEXT("Client1")){PC.StopCombatAndOrders();}
            }
            if(!AllMarkers(TEXT("SharedHit.json"))){return true;}
            if(Role==TEXT("Host")&&SharedTrainingStage==1)
            {
                PC.SubmitTrainingReset();
                if(!Mark(TEXT("SharedResetSent.json"))){return Finish();}
                SharedTrainingStage=2;
            }
            if(FPaths::FileExists(FPaths::Combine(Output,TEXT("SharedResetSent.json")))
                &&Target->Health.CurrentP2==Target->Health.MaxP2)
            {
                const FString ResetFile=Role+TEXT("SharedResetSeen.json");
                if(!FPaths::FileExists(FPaths::Combine(Output,ResetFile)))
                {
                    Check(true,TEXT("Owner reset restores the same shared target on this owning connection"));
                    if(!Mark(ResetFile)){return Finish();}
                }
            }
            bSharedTrainingDone=AllMarkers(TEXT("SharedResetSeen.json"));
            return true;
        }
    bool ReviewAuthoredTraining(ALDPlayerController& PC)
    {
        auto* Registry=PC.GetWorld()->GetSubsystem<ULDMapRegistry>();
        const auto* Geometry=Registry?Registry->GetGeometry():nullptr;
        const auto* Placement=Geometry?Geometry->Anchors.FindByPredicate([](const FLDDemoWorldAnchor& A)
            {return A.Kind==ELDDemoWorldAnchorKind::TrainingDummy&&A.Id.StartsWith(TEXT("training_shared_"));}):nullptr;
        if(!Placement){bAuthoredTrainingDone=true;return true;}
        const auto* Anchor=Registry->Resolve(FName(*Placement->Id),Registry->Generation);
        const auto* Relay=PC.LocalView();
        const FLDObservedFrameRead Read=Relay?Relay->ReadObservedFrame():FLDObservedFrameRead();
        if(!Anchor||!Read.Frame||Read.Status!=ELDObservedViewStatus::Current){return true;}
        const auto* Public=PC.GetWorld()->GetGameState<ALDGameState>();
        const auto* Target=Read.Frame->Units.FindByPredicate([Public,Anchor](const FLDObservedUnitView& Unit)
        {
            return Unit.Kind==ELDObservedUnitKind::Contender
                &&FVector::DistSquared2D(Unit.FeetCm,Anchor->Position)<FMath::Square(300.)
                &&!Public->DemoLobby.Members.ContainsByPredicate([&Unit](const FLDLobbyMember& M){return M.ContenderId==Unit.ContenderId;});
        });
        if(!Target){return true;}
        if(AuthoredTrainingStage==0)
        {
            if(!Mark(Role+TEXT("AuthoredTargetSeen.json"))){return Finish();}
            AuthoredTrainingStage=1;
        }
        if(!AllMarkers(TEXT("AuthoredTargetSeen.json"))){return true;}
        if(Role==TEXT("Client1")&&AuthoredTrainingStage==1&&PC.CanWorldCommand())
        {
            PC.Navigation->NavigateGround(FVector(Target->FeetCm)+FVector(0,120,0),Anchor->Surface);
            AuthoredTrainingStage=2;
        }
        if(Role==TEXT("Client1")&&AuthoredTrainingStage==2)
        {
            const auto* Body=Cast<ALDCharacter>(PC.GetPawn());
            if(!Body||FVector::DistSquared2D(ALDCombatDirector::FootOf(*Body),Target->FeetCm)>FMath::Square(300.)){return true;}
            PC.Navigation->AttackUnit(Target->Handle,Target->ObservationEpoch);
            AuthoredTrainingStage=3;
        }
        const FString HitFile=Role+TEXT("AuthoredHit.json");
        if(Target->Health.CurrentP2<Target->Health.MaxP2&&!FPaths::FileExists(FPaths::Combine(Output,HitFile)))
        {
            Check(Target->Health.CurrentP2>0,TEXT("An added general target publishes real damage to this owning connection"));
            auto Hit=MakeShared<FJsonObject>();Hit->SetStringField(TEXT("anchor"),Placement->Id);
            Hit->SetStringField(TEXT("target"),Target->ContenderId.ToString());
            Hit->SetNumberField(TEXT("hp_p2"),Target->Health.CurrentP2);Hit->SetNumberField(TEXT("max_hp_p2"),Target->Health.MaxP2);
            if(!Save(Hit,HitFile)){Check(false,TEXT("Authored training hit evidence persists"));return Finish();}
        }
        if(AllMarkers(TEXT("AuthoredHit.json")))
        {
            if(Role==TEXT("Client1")){PC.StopCombatAndOrders();}
            bAuthoredTrainingDone=true;
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
        bool Registered=Mode.ExecuteAuthorityPhase(*Mode.Combat,ELDCommitPhase::RespawnsAndWorld,
            [&](const FLDGameplayWritePermit& Permit)
        {
            if (bReplayFieldReturn)
            {
                const FGuid ReplayId=Mode.RosterOrder[1];
                const FLDDemoRational& Radius=Content->Get().Parameters.RestReturnSearchRadiusCm;
                const FVector Foot(1312,-1600,4);
                if (Radius.Denominator<=0) { Error=TEXT("Return replay requires the approved search radius."); return false; }
                ALDGameMode::FLDPlacementTarget Target;
                Target.Kind=ALDGameMode::FLDPlacementTarget::EKind::Near;
                Target.FootCm=Foot;
                Target.Surface=TEXT("demo_ground");
                Target.RadiusCm=double(Radius.Numerator)/double(Radius.Denominator);
                ALDGameMode::FLDPlacedBody Placed;
                if (!Mode.PlaceContenderBody(ReplayId,1,Permit,Placed,Error,Target))
                {
                    Error=FString::Printf(TEXT("Return replay at %.3f,%.3f,%.3f: %s"),
                        Target.FootCm.X,Target.FootCm.Y,Target.FootCm.Z,*Error);
                    Check(false,TEXT("Captured field return position prepares without an authority fault"));
                    return false;
                }
                const bool bAtCapturedFoot=Placed.Reservation && Placed.Reservation->AnchorId().IsNone()
                    && Placed.Reservation->Ground().FootCm.Equals(Foot,.000001);
                Mode.ReleasePlacedBody(Placed);
                if (!bAtCapturedFoot || !Mode.WithdrawPhaseRequest(ReplayId,Permit,Error))
                {
                    if (Error.IsEmpty()) { Error=TEXT("The exact captured field foot could not be prepared."); }
                    Check(false,TEXT("Captured field return position prepares without an authority fault"));
                    return false;
                }
                Check(true,TEXT("Captured field return position prepares without an authority fault"));
            }
            const FGuid FinalEntryId=Mode.RosterOrder[1];
            for (int32 Slot=0; Slot<LDDemoWorld::FinalSpawns; ++Slot)
            {
                ALDGameMode::FLDPlacementTarget Target;
                Target.Kind=ALDGameMode::FLDPlacementTarget::EKind::Anchor;
                Target.Anchor=FName(*LDDemoWorld::FinalSpawnId(Slot));
                ALDGameMode::FLDPlacedBody Placed;
                if (!Mode.PlaceContenderBody(FinalEntryId,1,Permit,Placed,Error,Target))
                {
                    Error=FString::Printf(TEXT("Final entry %s: %s"),*Target.Anchor.ToString(),*Error);
                    auto Evidence=MakeShared<FJsonObject>();
                    Evidence->SetBoolField(TEXT("passed"),false);
                    Evidence->SetStringField(TEXT("detail"),Error);
                    if (!Save(Evidence,TEXT("FinalEntry.json"))) { Error=TEXT("Could not save final-entry failure evidence."); }
                    Check(false,TEXT("Every final entry prepares through the authoritative spawn path"));
                    return false;
                }
                Mode.ReleasePlacedBody(Placed);
                if (!Mode.WithdrawPhaseRequest(FinalEntryId,Permit,Error)) { return false; }
            }
            Check(true,TEXT("Every final entry prepares through the authoritative spawn path"));
            auto FinalEntryEvidence=MakeShared<FJsonObject>();
            FinalEntryEvidence->SetBoolField(TEXT("passed"),true);
            FinalEntryEvidence->SetNumberField(TEXT("entries"),LDDemoWorld::FinalSpawns);
            if (!Save(FinalEntryEvidence,TEXT("FinalEntry.json")))
            { Error=TEXT("Could not save final-entry evidence."); return false; }
            const FGuid FirstId=Mode.RosterOrder[0];
            auto& FirstLife=Mode.Lives.FindChecked(FirstId);
            FirstLife.SpawnSequence=1;
            FirstLife.SpawnForLife=1;
            const FLDSpawnRequestKey FirstRequest{Mode.GetMatchContext(),FirstId,1,1};
            // #198: the first life opens R0 in the shared rest space, at the fixed anchor of its roster slot.
            const FName FirstRestAnchor=Mode.RestAnchorFor(FirstId);
            if (!Check(!FirstRestAnchor.IsNone(),TEXT("The first contender has a rest anchor by roster slot"))) { return false; }
            TSharedPtr<const FLDSpawnSelection> FirstSelection;
            FCollisionShape Capsule;
            const int32 SelectSeed=Mode.SpawnRandom.GetCurrentSeed();
            if (!ULDMapRegistry::ContentCapsule(*Content,Capsule,Error)
                || !Registry.SelectSpawnAnchor(FirstRequest,FirstRestAnchor,Permit,FirstSelection,Error)) { return false; }
            if (!Check(FirstSelection->AnchorId()==FirstRestAnchor && Mode.SpawnRandom.GetCurrentSeed()==SelectSeed,
                TEXT("The fixed rest anchor is selected without a random draw"))) { return false; }
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
            if (!Check(AllOccupied,TEXT("All seven fixture bodies have native query collision with Pawn blocking responses"))) { return false; }
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
            if (!Check(Registry.SelectSpawnAnchor(FirstRequest,FirstRestAnchor,Permit,RetrySelection,Error)
                && RetrySelection==FirstSelection && Mode.SpawnRandom.GetCurrentSeed()==BlockedSeed
                && Registry.ReserveSpawnPlacement(RetrySelection,Capsule,Permit,Retry,Error)
                && Retry->OffsetIndex()==0 && FirstLife.Life==0,
                TEXT("Unblocked retry reuses the original anchor and starts at its centre"))) { return false; }
            if (!Registry.ReleaseSpawnReservation(Retry,Error)) { return false; }
            const auto* TrainingAnchor=Registry.Resolve(TEXT("training_dummy_1"),Registry.Generation);
            if(!Check(TrainingAnchor!=nullptr,TEXT("The occupied training-slot fixture uses an approved anchor"))){return false;}
            APawn* TrainingOccupant=Mode.GetWorld()->SpawnActor<APawn>();
            if(!TrainingOccupant){Error=TEXT("Training occupancy fixture allocation failed.");return false;}
            ON_SCOPE_EXIT {TrainingOccupant->SetActorEnableCollision(false);TrainingOccupant->Destroy();};
            auto* TrainingShape=NewObject<UCapsuleComponent>(TrainingOccupant);
            TrainingOccupant->SetRootComponent(TrainingShape);TrainingOccupant->AddInstanceComponent(TrainingShape);
            TrainingShape->SetCapsuleSize(Capsule.GetCapsuleRadius(),Capsule.GetCapsuleHalfHeight());
            TrainingShape->SetWorldLocation(TrainingAnchor->Position+FVector(0,0,Capsule.GetCapsuleHalfHeight()+FloorGap));
            TrainingShape->SetCollisionObjectType(ECC_Pawn);TrainingShape->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            TrainingShape->SetCollisionResponseToChannels(GetDefault<ALDCharacter>()->GetCapsuleComponent()->GetCollisionResponseToChannels());
            TrainingShape->SetCanEverAffectNavigation(false);TrainingShape->RegisterComponentWithWorld(Mode.GetWorld());
            // The real start: every first life stands in the shared rest space and the world enters the R0 rest.
            if (!Mode.SpawnInitialContenders(Permit,Error) || !Mode.EnterRestWorld(Permit,Error)) { return false; }
            Check(Mode.TrainingDummies.Num()==3&&Mode.MatchPhase!=ELDMatchPhase::Faulted,
                TEXT("A Pawn temporarily occupying a future training slot does not fault or duplicate committed lives"));
            FString DuplicateError;
            if (!Check(!Mode.SpawnInitialContenders(Permit,DuplicateError) && !DuplicateError.IsEmpty(),
                TEXT("Repeated roster spawn cannot grant another life or resource reset"))) { return false; }
            int32 RestContenders=0;
            for (const auto& Pair : Mode.Lives)
            {
                const auto& Life=Pair.Value;
                // Camp members spawn at their camps, not at the contender anchors; each owner's rest training dummy is a
                // contender-kind target that is checked against its owner below, not a roster body.
                if (Life.Kind!=ELDVictimKind::Contender || Mode.IsTrainingDummy(Pair.Key)) { continue; }
                ++RestContenders;
                const auto* Anchor=Registry.Resolve(Life.SpawnAnchor,Registry.Generation);
                const auto* Body=Life.Actor.Get();
                FVector Offset;
                if (!Check(Anchor && SpawnGeometry && SpawnGeometry->SpawnOffsetsCm.IsValidIndex(Life.SpawnOffset)
                    && SpawnGeometry->SpawnOffsetsCm[Life.SpawnOffset].ToVector(Offset,Error)
                    && Body && FVector::DistSquared2D(Body->GetActorLocation(),Anchor->Position+Offset)<.000001,
                    TEXT("Actual initial body uses one of the seven exact offsets at its fixed selected anchor"))) { return false; }
                if (!Check(Life.SpawnAnchor==Mode.RestAnchorFor(Pair.Key),
                    TEXT("Each initial body stands at the rest anchor of its roster slot"))) { return false; }
                if (!Check(Life.ProtectedUntilMs==0,TEXT("Common initial-body path does not invent respawn protection"))) { return false; }
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
            bool DummiesPerOwner=Mode.TrainingDummies.Num()==RestContenders
                && Mode.TrainingDummyIds.Num()==RestContenders+Mode.SharedTrainingDummies.Num();
            const int32 AuthoredCount=Registry.GetGeometry()->Anchors.FilterByPredicate([](const FLDDemoWorldAnchor& A)
                {return A.Kind==ELDDemoWorldAnchorKind::TrainingDummy&&A.Id.StartsWith(TEXT("training_shared_"));}).Num();
            if(!Check(Mode.SharedTrainingDummies.Num()==AuthoredCount,TEXT("Every additional authored training point spawns a real registered body"))){return false;}
            for (const auto& Pair : Mode.TrainingDummies) { DummiesPerOwner&=Mode.IsInRestSpace(Pair.Key); }
            if (!Check(RestContenders==3 && DummiesPerOwner,
                TEXT("Each of the three roster bodies stands in its rest space with exactly one training dummy"))) { return false; }
            const FGuid RetryOwner=Mode.RosterOrder[1];
            const uint32 RetryLife=Mode.Lives.FindChecked(RetryOwner).Life;
            Mode.RetireTrainingDummy(RetryOwner);
            if(!Mode.RetireLifeForPhase(RetryOwner,Permit,Error)){return false;}
            for(const auto& Point:SpawnGeometry->SpawnOffsetsCm)
            {
                FVector Offset;if(!Point.ToVector(Offset,Error)){return false;}
                auto* Pawn=Mode.GetWorld()->SpawnActor<APawn>();
                if(!Pawn){Error=TEXT("Training retry occupancy allocation failed.");return false;}
                Occupants.Add(Pawn);
                auto* Shape=NewObject<UCapsuleComponent>(Pawn);
                Pawn->SetRootComponent(Shape);Pawn->AddInstanceComponent(Shape);
                Shape->SetCapsuleSize(Capsule.GetCapsuleRadius(),Capsule.GetCapsuleHalfHeight());
                Shape->SetWorldLocation(TrainingAnchor->Position+Offset+FVector(0,0,Capsule.GetCapsuleHalfHeight()+FloorGap));
                Shape->SetCollisionObjectType(ECC_Pawn);Shape->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
                Shape->SetCollisionResponseToChannels(GetDefault<ALDCharacter>()->GetCapsuleComponent()->GetCollisionResponseToChannels());
                Shape->SetCanEverAffectNavigation(false);Shape->RegisterComponentWithWorld(Mode.GetWorld());
            }
            bool bRetryBlocked=false;
            if(!Check(Mode.PlaceRestLife(RetryOwner,Permit,bRetryBlocked,Error)&&bRetryBlocked
                &&Mode.Lives.FindChecked(RetryOwner).Life==RetryLife
                &&Mode.Lives.FindChecked(RetryOwner).Phase==ELDLifePhase::Dead
                &&Mode.PendingPhasePlacement.Contains(RetryOwner)&&!Mode.TrainingDummies.Contains(RetryOwner),
                TEXT("A pending rest retry blocked at every training offset commits neither player life nor target and does not fault")))
            {return false;}
            for(const auto& Weak:Occupants){if(auto* Pawn=Weak.Get()){Pawn->SetActorEnableCollision(false);Pawn->Destroy();}}
            Occupants.Reset();
            if(!Check(Mode.PlaceRestLife(RetryOwner,Permit,bRetryBlocked,Error)&&!bRetryBlocked
                &&Mode.Lives.FindChecked(RetryOwner).Life==RetryLife+1
                &&Mode.Lives.FindChecked(RetryOwner).Phase==ELDLifePhase::Alive
                &&!Mode.PendingPhasePlacement.Contains(RetryOwner)&&Mode.TrainingDummies.Contains(RetryOwner)
                &&Mode.TrainingDummies.Num()==3,
                TEXT("Unblocked pending rest retry commits one next life and one real shared target together")))
            {return false;}
            return true;
        },Error);
        if (!Check(Registered,TEXT("Three real controllers receive one committed body each"))) { return false; }
        // Like a normal start, the match opens in the rest R0; the movement audit runs on the first field life after it.
        Mode.MatchPhase=ELDMatchPhase::Rest;
        ++Mode.TimelineRevision;
        Mode.bPublicMatchDirty=true;
        Mode.bLobbyDirty=true;
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
        Report->SetStringField(TEXT("scope"),TEXT("DEV-only three-owned-body rest placement/retry, optional EarlyRestReady shared training observation/hit/reset, core/ground/CMC and native-prefix audit fixture. Not a full playable match, death/respawn, final motion Fence, general vision/combat or 3+3 AI acceptance."));
        Report->SetBoolField(TEXT("shared_training_exercised"),bSharedTrainingDone);
        Report->SetBoolField(TEXT("authored_training_exercised"),bAuthoredTrainingDone && AllMarkers(TEXT("AuthoredHit.json")));
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
            Report->SetNumberField(TEXT("rest_life_before_field"),ObservedRestLife);
            Report->SetStringField(TEXT("navigation_status"),PC->Navigation->Status);
            if (const auto* Body=Cast<ALDCharacter>(PC->GetPawn()))
            {
                const auto* Movement=CastChecked<ULDCharacterMovementComponent>(Body->GetCharacterMovement());
                Report->SetStringField(TEXT("foot"),Body->GetNavAgentLocation().ToString());
                if (Stage>=9)
                {
                    Report->SetStringField(TEXT("arrival_goal"),ArrivalGoal.ToString());
                    Report->SetNumberField(TEXT("arrival_distance_cm"),FVector::Dist2D(Body->GetNavAgentLocation(),ArrivalGoal));
                    Report->SetNumberField(TEXT("arrival_acceptance_cm"),PC->Navigation->LatestReceipt().AcceptanceCm);
                }
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
                            Retired&=Pair.Value.RetiredThroughLife==Pair.Value.Life && Pair.Value.Life>=2
                                && !Pair.Value.Body.IsValid() && !Pair.Value.Actor.IsValid();
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
            // Record the actual R0 life, including a placement-retry fixture, before its exact next field life.
            const FLDTimelineView& Line=Public->PublicMatch.Timeline;
            if(Public->PublicMatch.Phase==ELDMatchPhase::Rest&&Line.Stage==ELDMatchStage::Rest
                &&Line.Step==ELDRestStep::Build&&PC->OwnerState.bActive)
            {
                ObservedRestLife=PC->OwnerState.Life;
                ObservedRestRevision=PC->OwnerState.LifeRevision;
            }
            if (bEarlyRestReady && Public->PublicMatch.Phase==ELDMatchPhase::Rest
                && Line.Stage==ELDMatchStage::Rest && Line.Step==ELDRestStep::Build && Body && Movement && Nav
                && PC->OwnerState.bActive && PC->OwnerState.Match==Public->PublicMatch.Generation)
            {
                if (const ALDWorldViewRelay* Relay=PC->LocalView())
                {
                    const FLDObservedFrameRead Read=Relay->ReadObservedFrame();
                    if (Read.Status==ELDObservedViewStatus::Current && Read.Frame)
                    {
                        TSet<FGuid> Others, Dummies;
                        for (const FLDObservedUnitView& Unit : Read.Frame->Units)
                        {
                            if (Unit.Kind!=ELDObservedUnitKind::Contender || !Unit.ContenderId.IsValid()) { continue; }
                            const bool bRoster=Public->DemoLobby.Members.ContainsByPredicate([&Unit](const FLDLobbyMember& Member)
                                { return Member.ContenderId==Unit.ContenderId; });
                            if (bRoster && Unit.ContenderId!=PC->OwnerState.ContenderId) { Others.Add(Unit.ContenderId); }
                            else if (!bRoster) { Dummies.Add(Unit.ContenderId); }
                        }
                        if (!RestObservationStartedAt) { RestObservationStartedAt=FPlatformTime::Seconds(); }
                        if (FPlatformTime::Seconds()-RestObservationStartedAt>3. && !bRestPresentationSeen)
                        {
                            const int32 Added=Registry->GetGeometry()->Anchors.FilterByPredicate([](const FLDDemoWorldAnchor& A)
                                {return A.Kind==ELDDemoWorldAnchorKind::TrainingDummy&&A.Id.StartsWith(TEXT("training_shared_"));}).Num();
                            if(Others.Num()!=2 || Dummies.Num()!=Public->DemoLobby.Members.Num()+Added)
                            {Error=FString::Printf(TEXT("Rest view: others=%d dummies=%d expected=%d"),Others.Num(),Dummies.Num(),Public->DemoLobby.Members.Num()+Added);}
                            if (!Check(Others.Num()==2 && Dummies.Num()==Public->DemoLobby.Members.Num()+Added,
                                TEXT("Every owning connection observes all live shared training dummies and the other players")))
                            { return Finish(); }
                            bRestPresentationSeen=true;
                        }
                    }
                }
                if(bRestPresentationSeen&&!bSharedTrainingDone){return ReviewSharedTraining(*PC);}
                if(bRestPresentationSeen&&!bAuthoredTrainingDone){return ReviewAuthoredTraining(*PC);}
                if (Role==TEXT("Client1") && bRestPresentationSeen)
                {
                    if (RestServiceStage==0 && PC->CanWorldCommand() && Movement->IsMovingOnGround()
                        && Movement->CurrentFloor.IsWalkableFloor())
                    {
                        const auto* Service=Registry->Resolve(TEXT("rest_service_1"),Registry->Generation);
                        if (!Check(Service!=nullptr,TEXT("The first rest service is registered"))) { return Finish(); }
                        Nav->NavigateGround(Service->Position,Service->Surface);
                        RestServiceOrder=Nav->LocalIdentity();
                        RestServiceStage=1;
                    }
                    else if (RestServiceStage==1 && PC->OwnerState.NearbyRestService==ELDRestService::SkillMerchant)
                    {
                        ULDChoicesPanel* Panel=PC->HUDUI?PC->HUDUI->ChoicesPanel.Get():nullptr;
                        if (!Check(Panel && PC->RootUI,TEXT("The remote owner has the shared choices panel and root UI"))) { return Finish(); }
                        if (!bChoicesPanelOpened)
                        {
                            if (!Check(PC->RootUI->ShowMainPanel(ELDUIMainPanel::Choices),
                                TEXT("The remote owner opens the shared choices panel"))) { return Finish(); }
                            Panel->OpenPanel();
                            if (!Check(Panel->IsPanelOpen(),TEXT("The remote choices panel is visible before resolving its opening offer")))
                            {
                                return Finish();
                            }
                            bChoicesPanelOpened=true;
                            return true;
                        }
                        const FLDProgressionView* Growth=PC->ReadOwnProgression();
                        if (!Growth || !Growth->bValid) { return true; }
                        if (Panel->HasPendingRequest()) { return true; }
                        if (bNpcPurchaseSent)
                        {
                            if (Growth->BuildRevision==NpcPurchaseRevision
                                || Growth->UnspentPoints!=NpcPurchasePointsBefore-Content->Get().Parameters.NpcActivePoints)
                            {
                                if (FPlatformTime::Seconds()-NpcPurchaseSentAt<=10.) { return true; }
                                Check(false,TEXT("The non-host NPC purchase did not receive an applied owner projection"));
                                return Finish();
                            }
                            bNpcPurchaseApplied=Growth->BuildRevision==NpcPurchaseRevision+1
                                && Growth->bHasActiveOffer && Growth->QueuedActiveOpportunities==1
                                && Growth->UnspentPoints==NpcPurchasePointsBefore-Content->Get().Parameters.NpcActivePoints;
                            if (!Check(bNpcPurchaseApplied,TEXT("The non-host owner receives an applied NPC purchase with the authoritative point charge")))
                            {
                                return Finish();
                            }
                            const auto* Service=Registry->Resolve(TEXT("rest_service_2"),Registry->Generation);
                            if (!Check(Service!=nullptr,TEXT("The second rest service is registered"))) { return Finish(); }
                            Nav->NavigateGround(Service->Position,Service->Surface);
                            RestServiceOrder=Nav->LocalIdentity();
                            RestServiceStage=2;
                            return true;
                        }
                        if (Growth->bHasActiveOffer)
                        {
                            const int64 Price=Content->Get().Parameters.NpcActivePoints;
                            const bool bNeedSkip=Growth->UnspentPoints<Price
                                && Growth->ActiveOffer.SkipPoints>=Price-Growth->UnspentPoints;
                            if (bNeedSkip ? !Panel->SkipOffer() : !Panel->ChooseOffer(0)) { return true; }
                            return true;
                        }
                        if (Growth->QueuedActiveOpportunities>0) { return true; }
                        if (!bServicePanelOpened)
                        {
                            Panel->SetServiceContext(PC->OwnerState.NearbyRestService);
                            if (!Panel->OpenServicePanel() || !Panel->IsPanelOpen())
                            {
                                Check(false,TEXT("The remote owner opens the contextual NPC service panel"));
                                return Finish();
                            }
                            bServicePanelOpened=true;
                            return true;
                        }
                        if (!bNpcPurchaseSent)
                        {
                            const int64 Price=Content->Get().Parameters.NpcActivePoints;
                            if (Growth->UnspentPoints<Price)
                            {
                                Check(false,TEXT("The remote owner has enough opening points for the NPC purchase"));
                                return Finish();
                            }
                            NpcPurchaseRevision=Growth->BuildRevision;
                            NpcPurchasePointsBefore=Growth->UnspentPoints;
                            if (!Panel->BuyNpcOffer())
                            {
                                Check(false,TEXT("The remote owner submits the skill merchant purchase through the shared panel"));
                                return Finish();
                            }
                            NpcPurchaseSentAt=FPlatformTime::Seconds();
                            bNpcPurchaseSent=true;
                            return true;
                        }
                    }
                    else if (RestServiceStage==2)
                    {
                        bLeftFirstService |= PC->OwnerState.NearbyRestService==ELDRestService::None;
                        if (bLeftFirstService && PC->HUDUI && PC->HUDUI->ChoicesPanel)
                        {
                            bServicePanelClosed |= !PC->HUDUI->ChoicesPanel->IsPanelOpen();
                        }
                        if (bLeftFirstService && PC->OwnerState.NearbyRestService==ELDRestService::ComponentArtisan)
                        {
                            Check(bLeftFirstService && bServicePanelOpened && bServicePanelClosed
                                && bNpcPurchaseApplied && PC->OwnerState.NearbyRestService==ELDRestService::ComponentArtisan,
                                TEXT("The non-host owner completes an NPC purchase, then the service panel closes on leaving before the next service"));
                            if (!Mark(TEXT("RestServicesVisited.json"))) { return Finish(); }
                            RestServiceStage=3;
                        }
                    }
                    if ((RestServiceStage==1 || RestServiceStage==2)
                        && ((Nav->LatestReceipt().Identity==RestServiceOrder
                            && (Nav->LatestReceipt().Status==ELDOrderStatus::Rejected
                                || Nav->LatestReceipt().Status==ELDOrderStatus::Cancelled))
                            || Nav->LocalIdentity()!=RestServiceOrder))
                    {
                        Error=FString::Printf(TEXT("Rest service walk did not continue: %u"),uint32(Nav->LatestReceipt().Failure));
                        Check(false,TEXT("The remote owner can navigate to each rest service"));
                        return Finish();
                    }
                }
                if (!bRestReadySent && bRestPresentationSeen && PC->OwnerState.bCanRestReady
                    && FPaths::FileExists(FPaths::Combine(Output,TEXT("RestServicesVisited.json"))))
                {
                    bRestReadySent=true;
                    PC->SubmitRestReady(true);
                }
                bRestReadyProjected |= PC->OwnerState.bRestReady;
                if (Role==TEXT("Host") && Mode && !bReadyWindowObserved && Line.ReadyContenders.Num()==3)
                {
                    const int64 Now=Mode->GetAuthorityNowMs();
                    const int64 Remaining=Line.StepEndsAtMs-Now;
                    if (Remaining>0 && Remaining<=10000)
                    {
                        bReadyWindowObserved=true;
                        Check(Line.SignupOpensAtMs<=Now,TEXT("All three real owners shorten rest to the final ten-second window"));
                    }
                }
            }
            if (!Body || !Movement || !PC->OwnerState.BodyRef.IsValid() || !PC->OwnerState.bActive
                || Public->PublicMatch.Generation!=PC->OwnerState.Match || Public->PublicMatch.Phase!=ELDMatchPhase::Development
                || PC->OwnerState.Life<2 || !Movement->CurrentFloor.IsWalkableFloor()
                || !PC->CanWorldCommand() || !PC->ReadDemoAuthorityTimeMs(DisplayNowMs)) { return true; }
            if (bEarlyRestReady)
            {
                Check(bRestPresentationSeen,TEXT("This owning connection sees both other players and all real shared training dummies"));
                Check(bRestReadySent && bRestReadyProjected,TEXT("This owning connection submitted ready and received its owner projection"));
                if (Role==TEXT("Host")) { Check(bReadyWindowObserved,TEXT("The host observed the shortened final window")); }
            }
            Check(DisplayNowMs==Public->DemoClock.NowMs && Public->DemoClock.Generation==PC->OwnerState.Match,
                TEXT("Owner display time reads the matching completed Core clock without local extrapolation"));
            Check(Body->GetController()==PC && PC->OwnerState.Life==ObservedRestLife+1
                && PC->OwnerState.LifeRevision==ObservedRestRevision+2,
                TEXT("Possession, body identity and the first field life after the R0 rest life agree"));
            Check(PC->OwnerState.Hp*100==Content->Get().Parameters.MaxHP.BaseP2
                && PC->OwnerState.Mp*100==Content->Get().Parameters.MaxMP.BaseP2,
                TEXT("Approved P2 resources are projected from the sole Combat body record"));
            FieldStartedAtMs=Line.FieldStartedAtMs;
            Check(Line.Stage==ELDMatchStage::Field && Line.FieldSegment==0 && FieldStartedAtMs>0
                && PC->OwnerState.ProtectedUntilMs<=FieldStartedAtMs+Content->Get().Parameters.ProtectionMs,
                TEXT("The first field life carries at most the shared R0 return protection"));
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
            AcceptedOrder=Nav->PredictedIdentity();
            Nav->NavigateGround(Goal->Position,Goal->Surface);
            Check(Nav->LocalIdentity()!=AcceptedOrder && Nav->PredictedIdentity()==AcceptedOrder,
                TEXT("A pending replacement keeps the accepted order identity for prediction and replay"));
            if (!Passed) { return Finish(); }
            StageAt=FPlatformTime::Seconds();
            Stage=31;
        }
        else if (Stage==31)
        {
            if (FPlatformTime::Seconds()-StageAt<.5) { return true; }
            // Stop, then a new order before the stop receipt: the pre-stop order stays client-active but has the old epoch.
            Check(Nav->HasMoveOrder(),TEXT("The walk is still client-active when Stop is pressed"));
            Nav->Stop();
            Nav->NavigateGround(Goal->Position,Goal->Surface);
            Check(Nav->PredictedIdentity()==Nav->LocalIdentity() && Nav->LocalIdentity().Epoch>AcceptedOrder.Epoch,
                TEXT("An order sent after Stop never predicts or replays the cancelled order"));
            if (!Passed) { return Finish(); }
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
            // A passing body can hold another role's goal (#97), so endpoint checks take turns after the shared walk/stop.
            const int32 Slot=Role==TEXT("Host")?0:Role==TEXT("Client1")?1:2;
            const bool bHostArrived=FPaths::FileExists(FPaths::Combine(Output,TEXT("HostArrived.json")));
            const bool bClient1Arrived=FPaths::FileExists(FPaths::Combine(Output,TEXT("Client1Arrived.json")));
            if (!LDDemoCoreProbeDetail::CanStartArrival(Slot,bHostArrived,bClient1Arrived)) { return true; }
            // The separate endpoints keep already-arrived bodies from holding another role's spot.
            const double Angle=Slot*UE_TWO_PI/3;
            ArrivalGoal=Goal->Position+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)
                *4*Body->GetCapsuleComponent()->GetScaledCapsuleRadius();
            Nav->NavigateGround(ArrivalGoal,Goal->Surface);
            Stage=9;
        }
        else if (Stage==9 && !Nav->HasMoveOrder() && Nav->LatestReceipt().Status==ELDOrderStatus::Completed
            && Nav->LatestReceipt().Failure==ELDOrderFailure::None)
        {
            // For an unoccupied endpoint the server completes inside the receipt's acceptance radius. The owning client's
            // predicted foot can differ by up to the engine's uncorrected position error.
            const FLDOrderReceipt& Arrived=Nav->LatestReceipt();
            const double ClientErrorCm=FMath::Sqrt(GetDefault<AGameNetworkManager>()->MAXPOSITIONERRORSQUARED);
            // The reliable completion receipt can precede the final unreliable CMC correction under packet lag/loss.
            if (ArrivalReceivedAt==0) { ArrivalReceivedAt=FPlatformTime::Seconds(); }
            if (FVector::Dist2D(Body->GetNavAgentLocation(),ArrivalGoal)>Arrived.AcceptanceCm+ClientErrorCm
                && FPlatformTime::Seconds()-ArrivalReceivedAt<3) { return true; }
            Check(Arrived.Status==ELDOrderStatus::Completed && Arrived.AcceptanceCm>0
                && FVector::Dist2D(Body->GetNavAgentLocation(),ArrivalGoal)<=Arrived.AcceptanceCm+ClientErrorCm
                && Movement->CurrentFloor.IsWalkableFloor(),TEXT("The accepted complete route reaches its real grounded endpoint"));
            Check(PC->OwnerState.BodyRef==OriginalBody && PC->OwnerState.Life==OriginalBody.Victim.Life,
                TEXT("Navigation and corrections do not allocate a new body or life"));
            if (!Passed || !Mark(Role+TEXT("Arrived.json"))) { return Finish(); }
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
                    // The field body exists from the R0 return (field 1 start); it integrates exactly that interval once.
                    Check(Move && !Move->IsComponentTickEnabled() && Move->AuthorityIntegrationCount>0
                        && FMath::Abs(Move->AuthorityIntegratedSeconds-(Mode->GetAuthorityNowSeconds()-FieldStartedAtMs/1000.))<.0001,
                        TEXT("Each body integrates the match interval once, never native tick plus driver"));
                    if (Current && !Current->IsLocallyControlled())
                    {
                        Check(Move && Move->NetworkBookkeepingMoves>0 && Move->SuppressedNetworkSimulationMoves>0,
                            TEXT("Real packed movement still runs native network bookkeeping without double simulation"));
                    }
                    const auto* Resource=Current?Mode->Combat->StateFor(Current):nullptr;
                    Check(LDDemoCoreProbeDetail::MatchesCurrentBasicTiming(Resource,*Content),
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoCoreProbeArrivalTurnTest, "Lockdown.Rules.DemoCoreProbe.ArrivalTurn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoCoreProbeArrivalTurnTest::RunTest(const FString& Parameters)
{
    using LDDemoCoreProbeDetail::CanStartArrival;
    TestTrue(TEXT("Host can start the first endpoint"),CanStartArrival(0,false,false));
    TestFalse(TEXT("Client1 waits while the host can cross its goal"),CanStartArrival(1,false,false));
    TestTrue(TEXT("Client1 can start after the host arrives"),CanStartArrival(1,true,false));
    TestFalse(TEXT("Client2 waits until both earlier roles arrive"),CanStartArrival(2,true,false));
    TestFalse(TEXT("Client2 cannot start if the host marker is missing"),CanStartArrival(2,false,true));
    TestTrue(TEXT("Client2 can start after both earlier roles arrive"),CanStartArrival(2,true,true));
    TestFalse(TEXT("Unknown roles cannot start"),CanStartArrival(3,true,true));

    const FVector2D Goal(0,0), Foot(0,160);
    const LDPathSteering::FBodyDisc PassingBody{FVector2D(0,70),40};
    const double ClientErrorCm=FMath::Sqrt(GetDefault<AGameNetworkManager>()->MAXPOSITIONERRORSQUARED);
    TestTrue(TEXT("A passing body can legitimately complete a route beyond the strict endpoint check"),
        LDPathSteering::BlockedAtHeldGoal(Foot,40,Goal,{PassingBody})
        && FVector2D::Distance(Foot,Goal)>40+ClientErrorCm);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoCoreProbeGrowthTimingTest, "Lockdown.Rules.DemoCoreProbe.GrowthTiming",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoCoreProbeGrowthTimingTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    FString Error;
    if (!TestTrue(TEXT("Approved runtime content"), LDDemoEntry::LoadRuntimeContent(Catalog,Error) && Catalog)) { return false; }
    const auto Projection=LDAttributeProjection::Project(FLDAttributeBuildValues(3,{}),*Catalog);
    if (!TestTrue(TEXT("Level-three attribute projection"),Projection.IsSuccess())) { return false; }
    FLDBodyCombatState Body;
    Body.AttributeProjection=Projection.Value.GetValue();
    FLDBasicTimingInput Input;
    Input.BaseAttackSpeed=Catalog->Get().Parameters.AttackSpeed.Base;
    Input.CurrentAttackSpeed=Body.AttributeProjection.Final.AttackSpeed;
    Input.BaseWindupMs=Catalog->Get().Parameters.BasicAttackWindupMs;
    const auto Expected=LDTactical::EvaluateBasicTiming(Input,*Catalog);
    if (!TestTrue(TEXT("Level-three basic timing"),Expected.IsSuccess())) { return false; }
    Body.BasicTiming=Expected.Value.GetValue();
    TestTrue(TEXT("Level growth changes the base windup"),Body.BasicTiming.WindupMs!=Catalog->Get().Parameters.BasicAttackWindupMs);
    TestTrue(TEXT("Current projected timing is accepted"),LDDemoCoreProbeDetail::MatchesCurrentBasicTiming(&Body,*Catalog));
    ++Body.BasicTiming.WindupMs;
    TestFalse(TEXT("An incorrect current windup is rejected"),LDDemoCoreProbeDetail::MatchesCurrentBasicTiming(&Body,*Catalog));
    return true;
}

void LDDemoTests::StartCoreProbe(ALDPlayerController& Controller)
{
    if (!FParse::Param(FCommandLine::Get(),TEXT("LDDemoCoreProbe")) || !Controller.IsLocalController()
        || UWorld::RemovePIEPrefix(Controller.GetWorld()->GetOutermost()->GetName())!=LDDemoEntry::WorldMap) { return; }
    auto Probe=MakeShared<FLDDemoCoreProbe>();
    Probe->bEarlyRestReady=FParse::Param(FCommandLine::Get(),TEXT("LDDemoCoreReady"));
    Probe->bReplayFieldReturn=FParse::Param(FCommandLine::Get(),TEXT("LDDemoCoreReplayFieldReturn"));
    int32 Timeout=330;
    FParse::Value(FCommandLine::Get(),TEXT("LDDemoCoreTimeout="),Timeout);
    if (!FParse::Value(FCommandLine::Get(),TEXT("LDDemoCoreOutput="),Probe->Output)
        || !FParse::Value(FCommandLine::Get(),TEXT("LDDemoCoreRole="),Probe->Role)
        || (Probe->Role!=TEXT("Host") && Probe->Role!=TEXT("Client1") && Probe->Role!=TEXT("Client2"))
        || Timeout<30 || Timeout>600
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
