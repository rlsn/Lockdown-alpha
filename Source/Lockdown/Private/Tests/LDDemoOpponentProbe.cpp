#include "Tests/LDDemoOpponentProbe.h"
#include "Core/LDLog.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AI/LDOpponentController.h"
#include "Character/LDCharacter.h"
#include "Containers/Ticker.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDevelopmentDirector.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PK/LDPKDirector.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapRegistry.h"
#include "World/LDWorldQueryTypes.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "UnrealClient.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Vision/LDWorldViewRelay.h"
#include "Navigation/LDNavigationComponent.h"
#include "World/LDCaptureMarker.h"
#include "UI/LDHUD.h"
#include "UI/LDChoicesPanel.h"

// Three humans ready a full room with three opponent AI and then stay out of play, so every growth step,
// fight and the capture win below come from the AI's own requests through the shared domain entries.
enum class ELDOpponentProbeStage : uint8 { Join, Lobby, Starting, Match, Results, Done };
// Only a check the match actually exercised can pass. One the match gave no occasion for, such as respawning when no AI
// died, is reported apart as not exercised and never counted as passed.
enum class ELDOpponentProbeCheck : uint8 { Passed, Failed, NotExercised };

namespace LDDemoOpponentProbeDetail
{
constexpr uint8 OpponentCount = 3;
constexpr int32 HumanCount = 3;
constexpr int64 SampleMs = 1000;
// Bounds of this check, not game values. A loop at the approved decision rate sends 120 body requests in 30 s;
// fighting with every source and target change stays well below 40.
constexpr int64 RequestWindowMs = 30000;
constexpr uint32 MaxBodyRequestsPerWindow = 40;
constexpr int64 RespawnSlackMs = 2500;
constexpr int64 ActAfterRespawnMs = 10000;
constexpr double MinTravelCm = 1000.;

bool ReturnLifeRetired(ELDLifePhase Phase, bool bHasBody, bool bHasActor)
{
    return Phase == ELDLifePhase::Dead && !bHasBody && !bHasActor;
}

int32 CountKind(const FLDLobbyView& Lobby, ELDContenderKind Kind)
{
    int32 Count = 0;
    for (const FLDLobbyMember& Row : Lobby.Members) { Count += Row.Kind == Kind ? 1 : 0; }
    return Count;
}

TSet<FGuid> OpponentRows(const FLDLobbyView& Lobby)
{
    TSet<FGuid> Ids;
    for (const FLDLobbyMember& Row : Lobby.Members)
    {
        if (Row.Kind == ELDContenderKind::OpponentAI) { Ids.Add(Row.ContenderId); }
    }
    return Ids;
}

int32 OpponentSlot(const FLDLobbyView& Lobby, const FGuid& Contender)
{
    int32 Slot = 0;
    for (const FLDLobbyMember& Row : Lobby.Members)
    {
        if (Row.Kind != ELDContenderKind::OpponentAI) { continue; }
        if (Row.ContenderId == Contender) { return Slot; }
        ++Slot;
    }
    return INDEX_NONE;
}
}

// Authority truth about one AI, read only by this checker on the Host and never fed back to any policy.
struct FLDOpponentProbeTrack
{
    FGuid Contender;
    int32 Slot = INDEX_NONE;
    TWeakObjectPtr<ALDOpponentController> Controller;
    ELDLifePhase Phase = ELDLifePhase::Unspawned;
    uint32 Life = 0;
    int64 DeadSeenAtMs = -1;
    uint32 Deaths = 0;
    uint32 Respawns = 0;
    int64 LongestRespawnMs = 0;
    bool bRespawnsValid = true;
    int64 ActCheckDueMs = -1;
    // Body requests counted at the first sample that saw this AI dead. A dead body sends nothing, so any later request
    // belongs to the next life, including one sent in its very first frame.
    uint32 ActBase = 0;
    bool bQuietWhileDead = true;
    uint32 RespawnsChecked = 0;
    uint32 RespawnsActed = 0;
    double TravelCm = 0;
    FVector LastFeet = FVector::ZeroVector;
    bool bHasFeet = false;
    int64 MaxLevel = 0;
    int64 MaxXP = 0;
    int64 MaxConsumed = 0;
    // Ms since the match start (R0) at the first sample that saw a consumed choice; the D10 opening pick resolves in R0.
    int64 OpeningPickMs = -1;
    // #198: the field respawn wait fixed by the field progress T of the death being tracked (8/10/12/15 s by segment).
    int64 RespawnWaitMs = 0;
    // Development only: XP at the last sample before the circle opened, and sampled time spent dead or recovering
    // under the approved regeneration. A stuck AI shows neither.
    int64 DevXP = 0;
    int64 DevRecoverOrDeadMs = 0;
    TArray<TPair<int64, uint32>> Requests;
    uint32 MaxWindowRequests = 0;
    FLDOpponentRequestTally Tally;
};

class FLDDemoOpponentProbe : public TSharedFromThis<FLDDemoOpponentProbe>
{
public:
    TWeakObjectPtr<ALDPlayerController> Controller;
    FString Output, Role, Error;
    double Deadline = 0, LastCommandAt = -100, ReturnedAt = 0;
    ELDOpponentProbeStage Stage = ELDOpponentProbeStage::Join;
    bool Passed = true, Written = false, StartSent = false, ReturnSent = false;
    bool RosterKept = true, ReadsKept = true;
    // #134: every role presses the playtest mark once, a minute into the match, so the logs carry a mark to review.
    bool bPlaytestMarked = false;
    // #198: each AI's current life in the shared opening rest room, that life's foot at its first
    // sample, whether it has decided to train on a shared dummy (§6.4: the only reason to move there before the PK sign-up
    // window) and its distance to its own sign-up disk at the first sample inside the window (站圈报名: afterwards the only
    // walk is into that disk); the largest drift seen before training, the largest step away from the disk inside the
    // window, the AIs that stood in their disks at the lock, the AIs seen training or on a PK life in R0, and the ms since
    // the match start (R0) at which each AI's opening choice was first seen.
    struct FRest0Foot
    {
        uint32 Life = 0;
        FVector Foot = FVector::ZeroVector;
        bool bTrained = false;
        double WindowDiskCm = -1;
        bool bLocked = false;
    };
    TMap<FGuid, FRest0Foot> Rest0AIFeet;
    TSet<FGuid> Rest0PKEntrants;
    TSet<FGuid> Rest0Signed;
    TSet<FGuid> Rest0Trainers;
    TMap<FGuid, int64> Rest0Picks;
    double MaxRest0AIDriftCm = 0;
    double MaxRest0AIStrayCm = 0;
    int32 Rest0Samples = 0;
    uint32 FirstGeneration = 0;
    // Development: the three timed field segments before the flagpole opens at the final field.
    int64 DevelopmentMs = 0, NextSampleMs = 0, StartedAtMs = -1;
    int32 MaxSeenOpponents = 0;
    // Rendered Host only: up to three presentation screenshots of the running match (HUD review evidence, never a check).
    int32 Shots = 0;
    bool bRestChoicesShot = false;
    double MatchSeenAt = 0, LastShotAt = 0;
    int64 LastHostHPP2 = -1;
    TArray<TSharedPtr<FJsonValue>> Screenshots;
    void CaptureHost(const ALDPlayerController& Owner);
    // Rendered Host only, #112 review evidence (never a check): the opening active offer with its points, a
    // visible opponent's Q/W card, a kill notice naming assisters, the spray and guard HUD over the flagpole graffiti
    // and the results with their assist column. Each is taken at most once, one request at a time.
    TSet<FString> ReviewTaken;
    FString ReviewPending;
    double ReviewDueAt = 0, LastReviewAt = 0;
    uint64 ReviewKillSerial = 0;
    bool bPannedToFlagpole = false;
    void ReviewShots(ALDPlayerController& Owner, const ALDGameState& Public, bool bResults);
    TSet<FGuid> OpponentIds;
    TArray<FLDOpponentProbeTrack> Tracks;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TArray<TSharedPtr<FJsonValue>> Checks;
    TArray<TSharedPtr<FJsonValue>> NotExercised;
    TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();

    bool Check(bool Value, const TCHAR* Name)
    {
        Record(Value ? ELDOpponentProbeCheck::Passed : ELDOpponentProbeCheck::Failed, Name);
        return Value;
    }

    void Record(ELDOpponentProbeCheck Result, const TCHAR* Name)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("name"), Name);
        Row->SetBoolField(TEXT("passed"), Result == ELDOpponentProbeCheck::Passed);
        Row->SetStringField(TEXT("status"), Result == ELDOpponentProbeCheck::Passed ? TEXT("passed")
            : Result == ELDOpponentProbeCheck::Failed ? TEXT("failed") : TEXT("not_exercised"));
        Checks.Add(MakeShared<FJsonValueObject>(Row));
        if (Result == ELDOpponentProbeCheck::NotExercised)
        {
            NotExercised.Add(MakeShared<FJsonValueString>(Name));
            return;
        }
        if (Result == ELDOpponentProbeCheck::Failed)
        {
            Passed = false;
            if (Error.IsEmpty()) { Error = Name; }
        }
    }

    bool Save(const TSharedRef<FJsonObject>& Value, const FString& Name) const
    {
        FString Json;
        const FString File = FPaths::Combine(Output, Name);
        return FJsonSerializer::Serialize(Value, TJsonWriterFactory<>::Create(&Json))
            && FFileHelper::SaveStringToFile(Json, *(File + TEXT(".tmp")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
            && IFileManager::Get().Move(*File, *(File + TEXT(".tmp")), true, true);
    }

    bool Mark(const FString& Name)
    {
        auto Value = MakeShared<FJsonObject>();
        Value->SetBoolField(TEXT("ready"), true);
        if (Save(Value, Name)) { return true; }
        Error = TEXT("Could not persist opponent probe marker: ") + Name;
        Passed = false;
        return false;
    }

    bool AllMarkers(const TCHAR* Suffix) const
    {
        for (const TCHAR* Name : {TEXT("Host"), TEXT("Client1"), TEXT("Client2")})
        {
            if (!FPaths::FileExists(FPaths::Combine(Output, FString(Name) + Suffix))) { return false; }
        }
        return true;
    }

    // Session commands are idempotent per roster; a refused or raced one is sent again after a pause.
    bool Resend()
    {
        const double Now = FPlatformTime::Seconds();
        if (Now - LastCommandAt < 3.) { return false; }
        LastCommandAt = Now;
        return true;
    }

    bool Finish()
    {
        auto Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"), Passed);
        Report->SetStringField(TEXT("role"), Role);
        Report->SetStringField(TEXT("detail"), Error);
        Report->SetStringField(TEXT("scope"), TEXT("DEV-only full room: participating Listen Host, two owning clients and three opponent AI through the normal lobby; the humans stay out of play. Not hidden-state non-interference, field whitelists, a leaving human or network faults."));
        Summary->SetArrayField(TEXT("not_exercised"), NotExercised);
        Report->SetObjectField(TEXT("summary"), Summary);
        Report->SetArrayField(TEXT("checks"), Checks);
        Written = Save(Report, Role + TEXT(".json"));
        UE_LOG(LogLDTest, Display, TEXT("LD_DEMO_OPPONENT_PROBE role=%s passed=%d detail=%s"), *Role, Passed && Written, *Error);
        if (!Passed || !Written) { FPlatformMisc::RequestExitWithStatus(false, 2); return false; }
        return true;
    }

    bool Tick();
    bool BindOpponents(UWorld& World, ALDGameMode& Mode, const FLDLobbyView& Lobby);
    void Sample(UWorld& World, ALDGameMode& Mode);
    void ReadGrowth(ALDGameMode& Mode, FLDOpponentProbeTrack& Track) const;
    // #198 R01/R02/R14 fixtures (Host; PhaseFixtures below).
    TArray<TWeakObjectPtr<APawn>> Occupants;
    bool bGrowthArmed = false, bGrowthChecked = false;
    SIZE_T GrowthArmedBytes = 0;
    bool bPKBlocked = false, bPKWaitSeen = false, bPKStartedEarly = false, bPKChecked = false;
    int64 PKBlockDueMs = 0;
    bool bReturnBlocked = false, bReturnHeldSeen = false, bNobodyStood = true, bReturnChecked = false;
    int64 ReturnDueMs = 0, ReturnFrozenFieldMs = 0;
    FGuid ReturnTarget;
    bool PlaceOccupant(UWorld& World, const FCollisionShape& Capsule, const FVector& FootCm);
    void ClearOccupants();
    void PhaseFixtures(UWorld& World, ALDGameMode& Mode);
    void FinishPhaseFixtures();
    static void CountRequests(FLDOpponentProbeTrack& Track, int64 Now);
    void CloseHost(ALDGameMode& Mode, int64 EndedAtMs, const FLDResultView& Result);
};

bool FLDDemoOpponentProbe::BindOpponents(UWorld& World, ALDGameMode& Mode, const FLDLobbyView& Lobby)
{
    using namespace LDDemoOpponentProbeDetail;
    TSet<FGuid> Bound;
    bool ServerOnly = true, Bodies = true;
    for (TActorIterator<ALDOpponentController> It(&World); It; ++It)
    {
        ALDOpponentController* AI = *It;
        if (!IsValid(AI)) { continue; }
        const FGuid Id = Mode.ContenderId(AI);
        ServerOnly &= AI->HasAuthority() && !AI->GetNetConnection() && !AI->Player && Id.IsValid() && !Bound.Contains(Id);
        Bound.Add(Id);
        FLDContenderLifeRead Life;
        FString ReadError;
        Bodies &= Cast<ALDCharacter>(AI->GetPawn()) && Mode.ReadContenderLife(Mode.GetMatchContext(), Id, Life, ReadError)
            && Life.Phase == ELDLifePhase::Alive;
        FLDOpponentProbeTrack& Track = Tracks.AddDefaulted_GetRef();
        Track.Contender = Id;
        Track.Slot = OpponentSlot(Lobby, Id);
        Track.Controller = AI;
        Track.Phase = Life.Phase;
        Track.Life = Life.Life;
    }
    Check(ServerOnly && Bound.Num() == OpponentCount && Bound.Includes(OpponentIds) && OpponentIds.Includes(Bound),
        TEXT("Each AI slot is one server-only opponent controller bound to its own lobby contender"));
    Check(Bodies, TEXT("Each AI enters the match with a live body, like a human"));
    return Passed;
}

void FLDDemoOpponentProbe::ReadGrowth(ALDGameMode& Mode, FLDOpponentProbeTrack& Track) const
{
    if (Mode.Development)
    {
        const FLDProgressionView Progression = Mode.Development->ReadProgression(Track.Contender);
        if (Progression.bValid)
        {
            Track.MaxLevel = FMath::Max<int64>(Track.MaxLevel, Progression.Level);
            Track.MaxXP = FMath::Max<int64>(Track.MaxXP, Progression.CumulativeXP);
            Track.MaxConsumed = FMath::Max<int64>(Track.MaxConsumed, Progression.ActiveOpportunitiesConsumed);
        }
    }
}

void FLDDemoOpponentProbe::CountRequests(FLDOpponentProbeTrack& Track, int64 Now)
{
    using namespace LDDemoOpponentProbeDetail;
    const uint32 Body = Track.Tally.BodyRequests();
    Track.Requests.Add(TPair<int64, uint32>(Now, Body));
    while (Track.Requests.Num() > 1 && Track.Requests[0].Key < Now - RequestWindowMs) { Track.Requests.RemoveAt(0); }
    Track.MaxWindowRequests = FMath::Max(Track.MaxWindowRequests, Body - Track.Requests[0].Value);
}

// #198 R01/R02/R14 fixtures (issue #198 review: a normal unobstructed match does not prove them). R01: the life ledger is
// compacted to its exact size right before R1's rest entry, so adding the training dummies there grows it; every dummy
// must still carry its owner's slot. R02: every offset of every PK spawn is occupied from R1's betting step until the PK
// entry gives up (REST.EntryReadyTimeoutSec), so the PK is NoContest and its entrants go back to their rest spaces under
// new spawn identities without an authority fault. R14: at R2's return the last survivor in placement order loses its old
// foot and every free spot near it, and every normal spawn offset is occupied, for a moment past the boundary; nobody may
// stand, T and the field wait, and then everybody stands together with one protection.
bool FLDDemoOpponentProbe::PlaceOccupant(UWorld& World, const FCollisionShape& Capsule, const FVector& FootCm)
{
    const double Gap = (UCharacterMovementComponent::MIN_FLOOR_DIST + UCharacterMovementComponent::MAX_FLOOR_DIST) * .5;
    APawn* Pawn = World.SpawnActor<APawn>();
    if (!Pawn) { return false; }
    Occupants.Add(Pawn);
    Pawn->SetActorHiddenInGame(true);
    auto* Shape = NewObject<UCapsuleComponent>(Pawn);
    Pawn->SetRootComponent(Shape);
    Pawn->AddInstanceComponent(Shape);
    Shape->SetCapsuleSize(Capsule.GetCapsuleRadius(), Capsule.GetCapsuleHalfHeight());
    Shape->SetWorldLocation(FootCm + FVector(0, 0, Capsule.GetCapsuleHalfHeight() + Gap));
    Shape->SetCollisionObjectType(ECC_Pawn);
    Shape->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Shape->SetCollisionResponseToChannels(GetDefault<ALDCharacter>()->GetCapsuleComponent()->GetCollisionResponseToChannels());
    Shape->SetCanEverAffectNavigation(false);
    Shape->RegisterComponentWithWorld(&World);
    return true;
}

void FLDDemoOpponentProbe::ClearOccupants()
{
    for (const TWeakObjectPtr<APawn>& Weak : Occupants)
    {
        if (APawn* Pawn = Weak.Get())
        {
            Pawn->SetActorEnableCollision(false);
            Pawn->Destroy();
        }
    }
    Occupants.Reset();
}

void FLDDemoOpponentProbe::PhaseFixtures(UWorld& World, ALDGameMode& Mode)
{
    const FLDMatchTimeline& Line = Mode.GetTimeline();
    const int64 Now = Mode.GetAuthorityNowMs();
    auto* Registry = World.GetSubsystem<ULDMapRegistry>();
    const FLDDemoWorldGeometry* Geometry = Registry ? Registry->GetGeometry() : nullptr;
    FCollisionShape Capsule;
    FString QueryError;
    if (!Content || !Registry || !Geometry || !ULDMapRegistry::ContentCapsule(*Content, Capsule, QueryError)) { return; }
    const FLDDemoContentParameters& P = Content->Get().Parameters;
    const auto OccupyAnchor = [&](const FString& Id)
    {
        const FLDMapBinding* Anchor = Registry->Resolve(FName(*Id), Registry->Generation);
        for (const FLDDemoWorldPoint& Point : Geometry->SpawnOffsetsCm)
        {
            FVector Offset;
            if (Anchor && Point.ToVector(Offset, QueryError)) { PlaceOccupant(World, Capsule, Anchor->Position + Offset); }
        }
    };
    // R01: the dummies are added to the life ledger at R1's rest entry.
    if (!bGrowthChecked)
    {
        if (Mode.IsFieldRunning() && Line.GetFieldSegment() == 0 && Mode.GetFieldNowMs() >= P.FieldSegmentMs - 1000)
        {
            Mode.Lives.Compact();
            Mode.Lives.Shrink();
            GrowthArmedBytes = Mode.Lives.GetAllocatedSize();
            bGrowthArmed = true;
        }
        if (bGrowthArmed && Line.IsRest() && Line.GetRestIndex() == 1 && Line.GetStep() == ELDRestStep::Build
            && !Mode.TrainingDummies.IsEmpty())
        {
            bool bSlots = true;
            for (const auto& Pair : Mode.TrainingDummies)
            {
                const auto* Owner = Mode.Lives.Find(Pair.Key);
                const auto* Dummy = Mode.Lives.Find(Pair.Value.Entity);
                const ALDCharacter* Body = Pair.Value.Actor.Get();
                bSlots &= Owner && Dummy && Body && Dummy->PresentationSlot == Owner->PresentationSlot
                    && Body->PresentationKey == Owner->PresentationSlot && Dummy->Body == Pair.Value.Body
                    && Owner->Phase == ELDLifePhase::Alive && Mode.IsInRestSpace(Pair.Key);
            }
            Summary->SetNumberField(TEXT("r01_ledger_bytes_before"), double(GrowthArmedBytes));
            Summary->SetNumberField(TEXT("r01_ledger_bytes_after"), double(Mode.Lives.GetAllocatedSize()));
            Check(bSlots, TEXT("R01: every R1 training dummy keeps its owner's slot"));
            const bool bGrew = Mode.Lives.GetAllocatedSize() > GrowthArmedBytes;
            Record(!bGrew ? ELDOpponentProbeCheck::NotExercised
                : bSlots ? ELDOpponentProbeCheck::Passed : ELDOpponentProbeCheck::Failed,
                TEXT("R01: the life ledger grew while the R1 training dummies were added"));
            bGrowthChecked = true;
        }
    }
    // R02: R1's PK entry is blocked at every PK spawn until it gives up.
    if (!bPKChecked && Line.IsRest() && Line.GetRestIndex() == 1 && Mode.PKDirector)
    {
        const FLDPKView& PK = Mode.PKDirector->GetView();
        if (!bPKBlocked && Line.GetStep() == ELDRestStep::Betting && PK.Serial != 0)
        {
            for (const FString& Id : Content->Get().World.PKSpawnIds) { OccupyAnchor(Id); }
            PKBlockDueMs = Line.GetStepEndsAtMs() + P.RestPKReadyMs;
            bPKBlocked = true;
        }
        if (bPKBlocked && Line.GetStep() == ELDRestStep::PKReady)
        {
            bPKWaitSeen |= Mode.IsPKEntryWaiting() && Mode.PKLives.IsEmpty();
            bPKStartedEarly |= !Mode.PKLives.IsEmpty();
        }
        if (bPKBlocked && (Line.GetStep() == ELDRestStep::Settlement || Line.GetStep() == ELDRestStep::Payout))
        {
            ClearOccupants();
            bool bBack = true;
            for (const FLDPKEntrantView& Entrant : PK.Entrants)
            {
                bBack &= !Mode.IsContenderPresent(Entrant.Contender) || Mode.IsInRestSpace(Entrant.Contender)
                    || Mode.PendingPhasePlacement.Contains(Entrant.Contender);
            }
            Summary->SetNumberField(TEXT("r02_decided_at_ms"), double(PK.DecidedAtMs));
            Summary->SetNumberField(TEXT("r02_blocked_due_ms"), double(PKBlockDueMs));
            Check(bPKWaitSeen && !bPKStartedEarly && PK.Outcome == ELDPKOutcome::NoContest
                && PK.DecidedAtMs >= PKBlockDueMs + P.RestEntryReadyTimeoutMs && bBack,
                TEXT("R02: a PK entry blocked at every PK spawn waits, ends NoContest after the approved wait and returns its entrants to their rest spaces"));
            bPKChecked = true;
        }
    }
    // R14: R2's return with its last survivor and every normal spawn blocked for a moment.
    if (!bReturnChecked && Line.IsRest() && Line.GetRestIndex() == 2 && Line.GetStep() == ELDRestStep::Return)
    {
        if (!bReturnBlocked && Now >= Line.GetStepEndsAtMs() - 1000)
        {
            TArray<FGuid> Survivors;
            for (const FGuid& Id : Mode.GetRosterOrder())
            {
                const auto* Return = Mode.RestReturns.Find(Id);
                if (Mode.IsContenderPresent(Id) && Return && Return->bAliveAtRest) { Survivors.Add(Id); }
            }
            Survivors.Sort();
            const FLDDemoRational& Radius = P.RestReturnSearchRadiusCm;
            if (Survivors.Num() >= 2 && Radius.Denominator > 0)
            {
                ReturnTarget = Survivors.Last();
                const auto& Return = Mode.RestReturns.FindChecked(ReturnTarget);
                const double Reach = double(Radius.Numerator) / double(Radius.Denominator);
                TArray<FVector> Spots = {Return.FieldFootCm};
                for (int32 Ring = 1; Ring <= 3; ++Ring)
                {
                    for (int32 Direction = 0; Direction < 8; ++Direction)
                    {
                        const double Angle = Direction * PI / 4.;
                        Spots.Add(Return.FieldFootCm + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * (Reach * Ring / 3.));
                    }
                }
                for (const FVector& Spot : Spots)
                {
                    FLDLegalGround Ground;
                    ELDWorldQueryFailure Failure;
                    if (Registry->QueryGround(Registry->Revision(), Spot, Capsule, Return.FieldSurface, Ground, Failure, QueryError))
                    {
                        PlaceOccupant(World, Capsule, Ground.SurfaceCm);
                    }
                }
                for (const FString& Id : Content->Get().World.SpawnIds) { OccupyAnchor(Id); }
                ReturnDueMs = Line.GetStepEndsAtMs();
                ReturnFrozenFieldMs = Line.FieldMsAt(Now);
                bReturnBlocked = true;
            }
        }
        if (bReturnBlocked && Line.IsEntryHeld())
        {
            bReturnHeldSeen = true;
            for (const FGuid& Id : Mode.GetRosterOrder())
            {
                if (!Mode.IsContenderPresent(Id)) { continue; }
                const auto* Life = Mode.Lives.Find(Id);
                bNobodyStood &= Life && LDDemoOpponentProbeDetail::ReturnLifeRetired(
                    Life->Phase, Life->Body.IsValid(), Life->Actor.IsValid());
            }
            bNobodyStood &= Mode.bFieldReturnStarted && Mode.PreparedFieldReturn.IsEmpty()
                && !Mode.IsFieldRunning() && Line.FieldMsAt(Now) == ReturnFrozenFieldMs;
            if (Now >= ReturnDueMs + 1500) { ClearOccupants(); }
        }
    }
    if (bReturnBlocked && !bReturnChecked && Mode.IsFieldRunning() && Line.GetFieldSegment() == 2)
    {
        ClearOccupants();
        bool bTogether = true;
        int64 Protection = INDEX_NONE;
        for (const FGuid& Id : Mode.GetRosterOrder())
        {
            const auto* Life = Mode.Lives.Find(Id);
            if (!Mode.IsContenderPresent(Id)) { continue; }
            bTogether &= Life && Life->Phase == ELDLifePhase::Alive && (Protection == INDEX_NONE || Life->ProtectedUntilMs == Protection);
            if (Life) { Protection = Life->ProtectedUntilMs; }
        }
        const int64 FieldStartedAtMs = Mode.ReadPublicMatch().Timeline.FieldStartedAtMs;
        Summary->SetNumberField(TEXT("r14_return_due_ms"), double(ReturnDueMs));
        Summary->SetNumberField(TEXT("r14_field_started_at_ms"), double(FieldStartedAtMs));
        Summary->SetBoolField(TEXT("r14_held_seen"), bReturnHeldSeen);
        Summary->SetBoolField(TEXT("r14_nobody_stood_while_held"), bNobodyStood);
        Summary->SetBoolField(TEXT("r14_shared_protection"), bTogether);
        Check(bReturnHeldSeen && bNobodyStood && bTogether && FieldStartedAtMs > ReturnDueMs,
            TEXT("R14: a return with one survivor blocked stands nobody and holds T, then stands everybody together with one protection"));
        bReturnChecked = true;
    }
}

void FLDDemoOpponentProbe::FinishPhaseFixtures()
{
    ClearOccupants();
    if (!bGrowthChecked) { Check(false, TEXT("R01: the forced life-ledger growth at R1's rest entry was reached")); }
    if (!bPKChecked) { Check(false, TEXT("R02: the blocked R1 PK entry was reached")); }
    if (!bReturnChecked) { Check(false, TEXT("R14: the blocked R2 return was reached")); }
}

void FLDDemoOpponentProbe::Sample(UWorld& World, ALDGameMode& Mode)
{
    using namespace LDDemoOpponentProbeDetail;
    const int64 Now = Mode.GetAuthorityNowMs();
    if (Now < NextSampleMs) { return; }
    NextSampleMs = Now + SampleMs;
    const FLDPublicMatchView Public = Mode.ReadPublicMatch();
    if (StartedAtMs < 0) { StartedAtMs = Public.StartedAtMs; }
    const bool bDevelopment = Public.Phase == ELDMatchPhase::Development;
    // #198: a rest retires every field life without a death, ends every running respawn wait and stands each body in the
    // shared rest room; the field return places a new life. None is a death, a respawn or travel through the map.
    // The request window keeps counting through the rest, so a PK fight is held to the same bound and its requests are
    // never folded into the first field window after the rest.
    if (Public.Phase == ELDMatchPhase::Rest)
    {
        for (FLDOpponentProbeTrack& Track : Tracks)
        {
            const ALDOpponentController* AI = Track.Controller.Get();
            FLDContenderLifeRead Life;
            FString ReadError;
            if (!AI || !Mode.ReadContenderLife(Mode.GetMatchContext(), Track.Contender, Life, ReadError))
            {
                ReadsKept = false;
                continue;
            }
            Track.Phase = Life.Phase;
            Track.Life = Life.Life;
            Track.DeadSeenAtMs = -1;
            Track.ActCheckDueMs = -1;
            Track.bHasFeet = false;
            ReadGrowth(Mode, Track);
            Track.Tally = AI->GetRequestTally();
            CountRequests(Track, Now);
        }
        return;
    }
    // No backfill or replacement: the same three adapters for the same three contenders throughout.
    TSet<FGuid> Live;
    for (TActorIterator<ALDOpponentController> It(&World); It; ++It)
    {
        if (IsValid(*It)) { Live.Add(Mode.ContenderId(*It)); }
    }
    RosterKept &= Live.Num() == OpponentCount && Live.Includes(OpponentIds);
    for (FLDOpponentProbeTrack& Track : Tracks)
    {
        ALDOpponentController* AI = Track.Controller.Get();
        FLDContenderLifeRead Life;
        FString ReadError;
        if (!AI || !Mode.ReadContenderLife(Mode.GetMatchContext(), Track.Contender, Life, ReadError))
        {
            ReadsKept = false;
            continue;
        }
        Track.Tally = AI->GetRequestTally();
        const uint32 Body = Track.Tally.BodyRequests();
        if (Life.Phase == ELDLifePhase::Dead)
        {
            if (Track.DeadSeenAtMs < 0)
            {
                Track.DeadSeenAtMs = Now;
                Track.RespawnWaitMs = Mode.RespawnWaitAtFieldMs(Mode.GetFieldNowMs());
                ++Track.Deaths;
                Track.ActBase = Body;
            }
            Track.bQuietWhileDead &= Body == Track.ActBase;
        }
        if (Life.Phase == ELDLifePhase::Alive && Track.DeadSeenAtMs >= 0)
        {
            const int64 Waited = Now - Track.DeadSeenAtMs;
            Track.LongestRespawnMs = FMath::Max(Track.LongestRespawnMs, Waited);
            Track.bRespawnsValid &= Life.Life == Track.Life + 1 && Waited <= Track.RespawnWaitMs + RespawnSlackMs;
            ++Track.Respawns;
            Track.DeadSeenAtMs = -1;
            Track.ActCheckDueMs = Now + ActAfterRespawnMs;
            Track.bHasFeet = false;
        }
        if (Track.ActCheckDueMs >= 0 && (Body > Track.ActBase || Now >= Track.ActCheckDueMs))
        {
            ++Track.RespawnsChecked;
            Track.RespawnsActed += Body > Track.ActBase ? 1 : 0;
            Track.ActCheckDueMs = -1;
        }
        Track.Phase = Life.Phase;
        Track.Life = Life.Life;
        const APawn* Pawn = Life.Phase == ELDLifePhase::Alive ? AI->GetPawn() : nullptr;
        if (Pawn)
        {
            const FVector Feet = Pawn->GetActorLocation();
            if (Track.bHasFeet) { Track.TravelCm += FVector::Dist2D(Track.LastFeet, Feet); }
            Track.LastFeet = Feet;
            Track.bHasFeet = true;
        }
        else { Track.bHasFeet = false; }
        ReadGrowth(Mode, Track);
        if (Track.OpeningPickMs < 0 && Track.MaxConsumed >= 1) { Track.OpeningPickMs = Now - StartedAtMs; }
        if (bDevelopment)
        {
            Track.DevXP = Track.MaxXP;
            const bool bRecovering = Life.Phase == ELDLifePhase::Dead || AI->GetReportedReason() == ELDOpponentReason::Recover;
            Track.DevRecoverOrDeadMs += bRecovering ? SampleMs : 0;
        }
        CountRequests(Track, Now);
    }
}

void FLDDemoOpponentProbe::CaptureHost(const ALDPlayerController& Owner)
{
    // Only a rendered Host takes them: the opening HUD (own bars, pending active offer, minimap), then the first two
    // moments an AI stands in the Host's current sight, where cooldowns, floating numbers and falls can show.
    if (!FApp::CanEverRender() || Shots >= 3) { return; }
    const double Now = FPlatformTime::Seconds();
    if (MatchSeenAt == 0) { MatchSeenAt = Now; }
    // One screenshot request at a time with the #112 review shots.
    if (!ReviewPending.IsEmpty() || Now - LastReviewAt < .6) { return; }
    int32 Visible = 0;
    if (const ALDWorldViewRelay* Relay = Owner.LocalView())
    {
        // Only an opponent AI in the Host's current sight counts, and after the opening only while it acts (an attack,
        // cast or hit cue), so the shots catch fights rather than the other humans standing nearby.
        const FLDObservedFrameRead Read = Relay->ReadObservedFrame();
        if (Read.Frame && Read.Status == ELDObservedViewStatus::Current)
        {
            for (const FLDObservedUnitView& Unit : Read.Frame->Units)
            {
                Visible += Unit.Kind == ELDObservedUnitKind::Contender && OpponentIds.Contains(Unit.ContenderId)
                    && Unit.ActionCue != ELDObservedActionCue::None ? 1 : 0;
            }
        }
    }
    const bool bOpening = Shots == 0 && Now - MatchSeenAt >= 8.;
    // A hit on the Host itself also counts: its own damage number is on screen for about a second.
    const FLDCombatOwnerView& Combat = Owner.CombatView;
    const bool bHurt = Combat.bHasBody && LastHostHPP2 >= 0 && Combat.HPP2 < LastHostHPP2;
    LastHostHPP2 = Combat.bHasBody ? Combat.HPP2 : -1;
    const bool bSighted = Shots > 0 && (Visible > 0 || bHurt) && Now - LastShotAt >= 6.;
    if (!bOpening && !bSighted) { return; }
    const FString File = FString::Printf(TEXT("HostMatch%d.png"), ++Shots);
    LastShotAt = Now;
    FScreenshotRequest::RequestScreenshot(FPaths::Combine(Output, File), true, false);
    Screenshots.Add(MakeShared<FJsonValueString>(File));
}

void FLDDemoOpponentProbe::ReviewShots(ALDPlayerController& Owner, const ALDGameState& Public, bool bResults)
{
    if (!FApp::CanEverRender()) { return; }
    const double Now = FPlatformTime::Seconds();
    if (MatchSeenAt == 0) { MatchSeenAt = Now; }
    if (!ReviewPending.IsEmpty())
    {
        if (Now < ReviewDueAt) { return; }
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(Output, ReviewPending + TEXT(".png")), true, false);
        Screenshots.Add(MakeShared<FJsonValueString>(ReviewPending + TEXT(".png")));
        ReviewTaken.Add(ReviewPending);
        ReviewPending.Reset();
        LastReviewAt = Now;
        return;
    }
    if (Now - LastReviewAt < .6 || Now - LastShotAt < .6) { return; }
    const auto Queue = [&](const TCHAR* Name, double Delay)
    {
        if (ReviewTaken.Contains(Name)) { return false; }
        ReviewPending = Name;
        ReviewDueAt = Now + Delay;
        return true;
    };
    if (bResults) { Queue(TEXT("HostResults"), .5); return; }
    // The opening active offer with its points. The Host opens it and previews the first card only so a hit does not fold it
    // before the shot; it never confirms.
    if (Now - MatchSeenAt >= 2. && !ReviewTaken.Contains(TEXT("HostChoices")))
    {
        ULDChoicesPanel* Panel = Owner.HUDUI ? Owner.HUDUI->ChoicesPanel.Get() : nullptr;
        if (Panel)
        {
            if (!Panel->IsPanelOpen()) { Panel->OpenPanel(); }
            Panel->PreviewOffer(0);
        }
        if (Queue(TEXT("HostChoices"), .4)) { return; }
    }
    for (const FLDKillNotice& Notice : Public.KillFeed.Notices)
    {
        if (Notice.Serial <= ReviewKillSerial) { continue; }
        ReviewKillSerial = Notice.Serial;
        if (!Notice.Assisters.IsEmpty() && Queue(TEXT("HostKillAssist"), .2)) { return; }
    }
    const FLDPublicMatchView& View = Public.PublicMatch;
    const bool bSpray = View.CircleState == ELDCircleState::Solo || View.CircleState == ELDCircleState::Contested;
    const bool bGuard = View.GraffitiOwner.IsValid();
    if ((bSpray && !ReviewTaken.Contains(TEXT("HostSpray"))) || (bGuard && !ReviewTaken.Contains(TEXT("HostGuard"))))
    {
        // The idle Host's camera leaves its body for the flagpole so the zone ring and graffiti board are in view.
        if (!bPannedToFlagpole && Owner.Navigation)
        {
            for (TActorIterator<ALDCaptureMarker> It(Owner.GetWorld()); It; ++It)
            {
                Owner.Navigation->PanTo(It->GetActorLocation());
                bPannedToFlagpole = true;
                break;
            }
        }
        if (bGuard && Queue(TEXT("HostGuard"), 1.)) { return; }
        if (bSpray && Queue(TEXT("HostSpray"), 1.)) { return; }
    }
    if (!ReviewTaken.Contains(TEXT("HostOpponentCard")))
    {
        if (const ALDWorldViewRelay* Relay = Owner.LocalView())
        {
            const FLDObservedFrameRead Read = Relay->ReadObservedFrame();
            if (Read.Frame && Read.Status == ELDObservedViewStatus::Current)
            {
                for (const FLDObservedUnitView& Unit : Read.Frame->Units)
                {
                    if (Unit.Kind != ELDObservedUnitKind::Contender || !OpponentIds.Contains(Unit.ContenderId)
                        || (Unit.Actives.Abilities[0] == 0 && Unit.Actives.Abilities[1] == 0)) { continue; }
                    Owner.Inspect(Unit.Handle, Unit.ObservationEpoch);
                    Queue(TEXT("HostOpponentCard"), .4);
                    return;
                }
            }
        }
    }
}

void FLDDemoOpponentProbe::CloseHost(ALDGameMode& Mode, int64 EndedAtMs, const FLDResultView& Result)
{
    using namespace LDDemoOpponentProbeDetail;
    Check(RosterKept && ReadsKept && Tracks.Num() == OpponentCount, TEXT("No AI is added, replaced or lost during the match"));
    bool Moved = true, WinnerGrew = false, RespawnsOnTime = true, Acted = true, Bounded = true, QuietWhileDead = true;
    bool OpeningPicks = true, DevGrowth = true;
    uint32 Respawns = 0, RespawnsChecked = 0, DeathsSeen = 0, DashesLanded = 0, DormEntries = 0, AirdropClaims = 0, LeaderHunts = 0, BossAvoids = 0;
    uint32 PausedHolds = 0, PausedHoldsKept = 0, PausedHoldsBroken = 0, Challenges = 0, ChallengesKept = 0, ChallengesBroken = 0;
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (FLDOpponentProbeTrack& Track : Tracks)
    {
        if (const ALDOpponentController* AI = Track.Controller.Get()) { Track.Tally = AI->GetRequestTally(); }
        ReadGrowth(Mode, Track);
        // A death whose respawn time had not come when the match closed is not a late respawn.
        if (Track.DeadSeenAtMs >= 0 && EndedAtMs - Track.DeadSeenAtMs > Track.RespawnWaitMs + RespawnSlackMs) { Track.bRespawnsValid = false; }
        // A respawn shortly before the close settles against the final tally once it acted or its window had passed.
        if (Track.ActCheckDueMs >= 0)
        {
            const bool bActed = Track.Tally.BodyRequests() > Track.ActBase;
            if (bActed || EndedAtMs >= Track.ActCheckDueMs)
            {
                ++Track.RespawnsChecked;
                Track.RespawnsActed += bActed ? 1 : 0;
            }
            Track.ActCheckDueMs = -1;
        }
        const FLDOpponentRequestTally& T = Track.Tally;
        Moved &= Track.TravelCm >= MinTravelCm;
        // An AI that loses an even opening fight may spend the match recovering; the winner shows the full growth loop.
        if (Track.Contender == Result.Winner) { WinnerGrew = Track.MaxLevel >= 2 && Track.MaxConsumed >= 1; }
        DashesLanded += T.DashesLanded;
        DormEntries += T.DormEntries;
        AirdropClaims += T.AirdropClaims;
        LeaderHunts += T.LeaderHunts;
        BossAvoids += T.BossAvoids;
        RespawnsOnTime &= Track.bRespawnsValid;
        Respawns += Track.Respawns;
        RespawnsChecked += Track.RespawnsChecked;
        Acted &= Track.RespawnsActed == Track.RespawnsChecked;
        DeathsSeen += Track.Deaths;
        QuietWhileDead &= Track.bQuietWhileDead;
        // Refusals on receipt. Admission refusals are reported (orders_rejected), not bounded here: a body that navigation
        // finds standing on no registered support has every walk refused, a world fault the AI cannot loop on (#160, #163).
        Bounded &= Track.MaxWindowRequests <= MaxBodyRequestsPerWindow && T.OrdersRefused <= T.Orders / 10 + 3;
        const int64* OpeningAt = Rest0Picks.Find(Track.Contender);
        // The AI picks after its drawn delay into the R0 build step, never after the build step ends.
        OpeningPicks &= OpeningAt && *OpeningAt >= 1000 && *OpeningAt < Content->Get().Parameters.RestOpeningBuildMs;
        // An early rival fight at level one can leave a body waiting on regeneration for most of development; that is
        // the approved pacing, not a stall. Without XP, at least half of development must have been dead or recovering.
        DevGrowth &= Track.DevXP > 0 || 2 * Track.DevRecoverOrDeadMs >= DevelopmentMs;
        PausedHolds += T.PausedHolds;
        PausedHoldsKept += T.PausedHoldsKept;
        PausedHoldsBroken += T.PausedHolds - T.PausedHoldsKept - T.PausedHoldsReleased;
        Challenges += T.Challenges;
        ChallengesKept += T.ChallengesKept;
        ChallengesBroken += T.Challenges - T.ChallengesKept - T.ChallengesReleased;
        const FLDResultRow* Row = Result.Rows.FindByPredicate([&Track](const FLDResultRow& R) { return R.ContenderId == Track.Contender; });
        auto Value = MakeShared<FJsonObject>();
        Value->SetStringField(TEXT("contender"), Track.Contender.ToString(EGuidFormats::Digits));
        Value->SetNumberField(TEXT("slot"), Track.Slot);
        Value->SetBoolField(TEXT("winner"), Track.Contender == Result.Winner);
        Value->SetNumberField(TEXT("level"), double(Track.MaxLevel));
        Value->SetNumberField(TEXT("xp"), double(Track.MaxXP));
        Value->SetNumberField(TEXT("choices"), double(Track.MaxConsumed));
        Value->SetNumberField(TEXT("opening_pick_ms"), double(OpeningAt ? *OpeningAt : Track.OpeningPickMs));
        Value->SetNumberField(TEXT("dev_xp"), double(Track.DevXP));
        Value->SetNumberField(TEXT("dev_recover_or_dead_ms"), double(Track.DevRecoverOrDeadMs));
        Value->SetNumberField(TEXT("kills"), Row ? double(Row->Kills) : -1.);
        Value->SetNumberField(TEXT("deaths"), Track.Deaths);
        Value->SetNumberField(TEXT("respawns"), Track.Respawns);
        Value->SetBoolField(TEXT("quiet_while_dead"), Track.bQuietWhileDead);
        Value->SetNumberField(TEXT("longest_respawn_ms"), double(Track.LongestRespawnMs));
        Value->SetNumberField(TEXT("travel_cm"), FMath::RoundToDouble(Track.TravelCm));
        Value->SetNumberField(TEXT("orders"), T.Orders);
        Value->SetNumberField(TEXT("orders_refused"), T.OrdersRefused);
        Value->SetNumberField(TEXT("stops"), T.Stops);
        Value->SetNumberField(TEXT("actions"), T.Actions);
        Value->SetNumberField(TEXT("actions_refused"), T.ActionsRefused);
        Value->SetNumberField(TEXT("dashes"), T.Dashes);
        Value->SetNumberField(TEXT("dashes_landed"), T.DashesLanded);
        Value->SetNumberField(TEXT("dorm_entries"), T.DormEntries);
        Value->SetNumberField(TEXT("airdrop_claims"), T.AirdropClaims);
        Value->SetNumberField(TEXT("leader_hunts"), T.LeaderHunts);
        Value->SetNumberField(TEXT("boss_avoids"), T.BossAvoids);
        Value->SetNumberField(TEXT("build_requests"), T.Builds);
        Value->SetNumberField(TEXT("builds_refused"), T.BuildsRefused);
        Value->SetNumberField(TEXT("max_body_requests_30s"), Track.MaxWindowRequests);
        Value->SetNumberField(TEXT("paused_holds"), T.PausedHolds);
        Value->SetNumberField(TEXT("paused_holds_kept"), T.PausedHoldsKept);
        Value->SetNumberField(TEXT("paused_holds_released"), T.PausedHoldsReleased);
        Value->SetNumberField(TEXT("challenges"), T.Challenges);
        Value->SetNumberField(TEXT("challenges_kept"), T.ChallengesKept);
        Value->SetNumberField(TEXT("challenges_released"), T.ChallengesReleased);
        Value->SetNumberField(TEXT("holder_attacks"), T.HolderAttacks);
        Value->SetNumberField(TEXT("owner_attacks"), T.OwnerAttacks);
        Value->SetNumberField(TEXT("orders_rejected"), T.OrdersRejected);
        Rows.Add(MakeShared<FJsonValueObject>(Value));
    }
    Summary->SetArrayField(TEXT("opponents"), Rows);
    Check(Moved, TEXT("Every AI moved through the map on its own orders"));
    Check(WinnerGrew, TEXT("The winning AI levelled up and confirmed its active choices"));
    // Reported until multi-match runs show it holds (#117): a landed D19 dash.
    Record(DashesLanded > 0 ? ELDOpponentProbeCheck::Passed : ELDOpponentProbeCheck::NotExercised,
        TEXT("D19: at least one AI landed an innate dash"));
    // D21: an AI walks to a dorm only while recovering away from threats and the circle, which a match may never produce.
    Record(DormEntries ? ELDOpponentProbeCheck::Passed : ELDOpponentProbeCheck::NotExercised,
        TEXT("D21: an AI stepped into a dorm"));
    // D26: drops land at fixed public times inside the run and healthy AIs walk to them; a match where a human always
    // got there first only did not exercise it.
    Record(AirdropClaims ? ELDOpponentProbeCheck::Passed : ELDOpponentProbeCheck::NotExercised,
        TEXT("D26: an AI claimed a public airdrop"));
    // D27: a leader is named only when one contender is uniquely ahead at a refresh, and only strong healthy AIs hunt.
    Record(LeaderHunts ? ELDOpponentProbeCheck::Passed : ELDOpponentProbeCheck::NotExercised,
        TEXT("D27: an AI hunted a named leader"));
    // D26: the Boss patrols from its public appearance time; an AI that comes near it steps away.
    Record(BossAvoids ? ELDOpponentProbeCheck::Passed : ELDOpponentProbeCheck::NotExercised,
        TEXT("D26: an AI stepped away from the Boss"));
    // A late or missing respawn fails even when no respawn completed; a match where none was due only did not exercise it.
    Record(!RespawnsOnTime ? ELDOpponentProbeCheck::Failed
        : Respawns ? ELDOpponentProbeCheck::Passed : ELDOpponentProbeCheck::NotExercised,
        TEXT("Every AI death led to its next life within the approved respawn delay"));
    Record(!DeathsSeen ? ELDOpponentProbeCheck::NotExercised
        : QuietWhileDead ? ELDOpponentProbeCheck::Passed : ELDOpponentProbeCheck::Failed,
        TEXT("A dead AI sends no body request until its next life"));
    Record(!RespawnsChecked ? ELDOpponentProbeCheck::NotExercised
        : Acted ? ELDOpponentProbeCheck::Passed : ELDOpponentProbeCheck::Failed,
        TEXT("Every respawned AI sent a new body request within ten seconds"));
    Check(Bounded, TEXT("AI requests stay bounded: no request loop and few refusals"));
    Check(OpeningPicks, TEXT("Every AI chose its opening active in the opening rest R0's build step"));
    Check(DevGrowth, TEXT("Every AI gained experience before the circle opened, unless it spent at least half of development dead or recovering"));
    // D12: decisions are counted from each adapter's own frames; a released decision is a stop-loss exit.
    Record(!PausedHolds ? ELDOpponentProbeCheck::NotExercised
        : !PausedHoldsBroken && PausedHoldsKept ? ELDOpponentProbeCheck::Passed : ELDOpponentProbeCheck::Failed,
        TEXT("D12: an AI holding paused progress stays to fight in the circle, leaving only at its stop-loss line"));
    Record(!Challenges ? ELDOpponentProbeCheck::NotExercised
        : !ChallengesBroken && ChallengesKept ? ELDOpponentProbeCheck::Passed : ELDOpponentProbeCheck::Failed,
        TEXT("D12: an AI inside a contest against a rival's paused progress keeps fighting there instead of leaving to farm"));
    Check(!Mode.IsGameplayWriteOpen(Mode.GetMatchContext()) && Mode.ReadPublicMatch().ResultId == Result.ResultId,
        TEXT("The authority gate is closed by the same result it published"));
}

bool FLDDemoOpponentProbe::Tick()
{
    using namespace LDDemoOpponentProbeDetail;
    if (Written)
    {
        if (Role != TEXT("Host") && FPaths::FileExists(FPaths::Combine(Output, TEXT("FinishClients"))))
        { FPlatformMisc::RequestExitWithStatus(false, 0); return false; }
        if (Role == TEXT("Host") && FPaths::FileExists(FPaths::Combine(Output, TEXT("Finish"))))
        { FPlatformMisc::RequestExitWithStatus(false, 0); return false; }
        return true;
    }
    if (FPlatformTime::Seconds() >= Deadline)
    {
        Check(false, TEXT("Opponent scenario completed within its bounded deadline"));
        Error = FString::Printf(TEXT("Opponent probe timed out at stage %u."), uint8(Stage));
        return Finish();
    }
    auto* PC = Controller.Get();
    auto* World = PC ? PC->GetWorld() : nullptr;
    auto* Public = World ? World->GetGameState<ALDGameState>() : nullptr;
    auto* Mode = World ? World->GetAuthGameMode<ALDGameMode>() : nullptr;
    if (!PC || !World || !Public) { return true; }
    if (!Content)
    {
        if (!LDDemoEntry::LoadRuntimeContent(Content, Error) || !Content || Content->Get().Parameters.FieldSegmentMs <= 0
            || Content->Get().Parameters.RestOpeningBuildMs <= 0 || Content->Get().Parameters.RespawnField1Ms <= 0)
        {
            Check(false, TEXT("Approved content with positive field, opening rest and respawn durations loads"));
            return Finish();
        }
        DevelopmentMs = int64(FLDTimelineParameters::TimedSegments) * Content->Get().Parameters.FieldSegmentMs;
    }
    const bool bHost = Role == TEXT("Host");
    if (bHost && !Mode) { return true; }
    const FLDLobbyView& Lobby = Public->DemoLobby;
    const FLDPublicMatchView& View = Public->PublicMatch;
    switch (Stage)
    {
    case ELDOpponentProbeStage::Join:
        if (!PC->IsLocalController() || !Lobby.Generation) { return true; }
        Check(bHost ? World->GetNetMode() == NM_ListenServer
            : World->GetNetMode() == NM_Client && PC->GetNetConnection() && !PC->HasAuthority(),
            TEXT("Each role is a real Listen Host or owning client connection"));
        FirstGeneration = Lobby.Generation;
        if (!Mark(Role + TEXT("Joined.json"))) { return Finish(); }
        Stage = ELDOpponentProbeStage::Lobby;
        return true;
    case ELDOpponentProbeStage::Lobby:
    {
        if (!AllMarkers(TEXT("Joined.json")) || CountKind(Lobby, ELDContenderKind::Human) != HumanCount) { return true; }
        if (Lobby.AICount != OpponentCount)
        {
            // Only the Host selects; everyone readies for the final room, because a change unreadies all humans.
            if (bHost && Resend()) { PC->SetDemoAICount(OpponentCount); }
            return true;
        }
        const FLDLobbyMember* Self = Lobby.Members.FindByPredicate([PC](const FLDLobbyMember& Row)
        { return Row.ContenderId == PC->OwnerState.ContenderId; });
        if (!Self || !Self->Ready)
        {
            if (Resend()) { PC->SetDemoReady(true); }
            return true;
        }
        if (!bHost)
        {
            Stage = ELDOpponentProbeStage::Starting;
            return true;
        }
        const bool AllReady = !Lobby.Members.ContainsByPredicate([](const FLDLobbyMember& Row)
        { return Row.Kind == ELDContenderKind::Human && !Row.Ready; });
        if (!AllReady || StartSent) { return true; }
        OpponentIds = OpponentRows(Lobby);
        Check(OpponentIds.Num() == OpponentCount && !OpponentIds.Contains(FGuid()),
            TEXT("The Host selects three opponent AI, listed as three distinct AI rows"));
        if (!Check(Lobby.CanStart && Lobby.StartBlocker.IsEmpty(), TEXT("Every provider is wired: the ready full room may start normally")))
        {
            Error = TEXT("Normal start is still blocked: ") + Lobby.StartBlocker;
            return Finish();
        }
        if (!Passed) { return Finish(); }
        PC->StartDemoMatch();
        StartSent = true;
        Stage = ELDOpponentProbeStage::Starting;
        return true;
    }
    case ELDOpponentProbeStage::Starting:
    {
        if (bHost && Lobby.Phase == ELDMatchPhase::Lobby && Lobby.Generation != FirstGeneration)
        {
            Check(false, TEXT("The full room starts without aborting"));
            Error = TEXT("The start aborted: ") + PC->IntentFeedback;
            return Finish();
        }
        // #198: through the opening rest R0 an opponent AI moves in the shared room only to train on a shared dummy
        // once nothing is left to build (OPPONENT_AI.md §6.4) and, once the PK sign-up window opens, only to walk into its own
        // sign-up disk (站圈报名, user decision A), so every AI stands in its disk at the lock (sampled on the authority until
        // field 1); each picks its opening active in the R0 build step after its drawn delay. An AI that signed up fights on
        // a PK life in the arena during the PK step (approved R0 PK); that life is not a rest-space body, and the rest life
        // it returns to afterwards is measured from its own first sample.
        if (bHost && Mode && View.Generation == Lobby.Generation && Mode->IsRestPhase(Mode->GetMatchContext())
            && Mode->GetTimeline().GetRestIndex() == 0)
        {
            const FLDTimelineView Line = Mode->ReadPublicMatch().Timeline;
            const bool bBeforeWindow = Line.Step == ELDRestStep::Build && Mode->GetAuthorityNowMs() < Line.SignupOpensAtMs;
            const bool bInWindow = Line.Step == ELDRestStep::Build && !bBeforeWindow;
            if (!bRestChoicesShot && bBeforeWindow && FApp::CanEverRender()
                && Mode->GetAuthorityNowMs() - Mode->ReadPublicMatch().StartedAtMs >= 3000
                && PC->HUDUI && PC->HUDUI->ChoicesPanel && PC->HUDUI->ChoicesPanel->IsPanelOpen()
                && PC->CanWorldCommand() && PC->CombatView.bHasBody && PC->CombatView.AttackRangeCm > 0
                && !FScreenshotRequest::IsScreenshotRequested())
            {
                FScreenshotRequest::RequestScreenshot(FPaths::Combine(Output, TEXT("HostRestChoices.png")), true, false);
                Screenshots.Add(MakeShared<FJsonValueString>(TEXT("HostRestChoices.png")));
                bRestChoicesShot = true;
            }
            for (TActorIterator<ALDOpponentController> It(World); It; ++It)
            {
                const FGuid Id = Mode->ContenderId(*It);
                if (Mode->IsPKLife(Id)) { Rest0PKEntrants.Add(Id); }
                if (Mode->GetLockedEntrants().Contains(Id)) { Rest0Signed.Add(Id); }
                const bool bTraining = It->GetReportedReason() == ELDOpponentReason::Train;
                if (bTraining) { Rest0Trainers.Add(Id); }
                const APawn* Pawn = It->GetPawn();
                FLDContenderLifeRead Life;
                FString ReadError;
                FVector Disk;
                double DiskRadius = 0;
                if (Pawn && Mode->IsInRestSpace(Id) && Mode->ReadContenderLife(Mode->GetMatchContext(), Id, Life, ReadError)
                    && Life.Phase == ELDLifePhase::Alive)
                {
                    const FVector Foot = Pawn->GetActorLocation();
                    FRest0Foot& Seen = Rest0AIFeet.FindOrAdd(Id);
                    if (Seen.Life != Life.Life)
                    {
                        Seen.Life = Life.Life;
                        Seen.Foot = Foot;
                        Seen.bTrained = false;
                        Seen.WindowDiskCm = -1;
                        Seen.bLocked = false;
                    }
                    // The training order may outlive the decision that sent it, so a life that trained is not measured again.
                    Seen.bTrained |= bTraining;
                    if (bBeforeWindow)
                    {
                        if (!Seen.bTrained) { MaxRest0AIDriftCm = FMath::Max(MaxRest0AIDriftCm, FVector::Dist2D(Seen.Foot, Foot)); }
                    }
                    else if (!bInWindow)
                    {
                        // After the lock the body stands where the window left it (in its disk) and again moves only to
                        // train (the payout step trains too), measured from its first sample after the lock.
                        if (!Seen.bLocked)
                        {
                            Seen.bLocked = true;
                            Seen.Foot = Foot;
                            Seen.bTrained = bTraining;
                        }
                        else if (!Seen.bTrained) { MaxRest0AIDriftCm = FMath::Max(MaxRest0AIDriftCm, FVector::Dist2D(Seen.Foot, Foot)); }
                    }
                    else if (Mode->SignupDiskFor(Id, Disk, DiskRadius))
                    {
                        const double DiskCm = FVector::Dist2D(Foot, Disk);
                        if (Seen.WindowDiskCm < 0) { Seen.WindowDiskCm = DiskCm; }
                        MaxRest0AIStrayCm = FMath::Max(MaxRest0AIStrayCm, DiskCm - Seen.WindowDiskCm);
                    }
                    else { MaxRest0AIStrayCm = TNumericLimits<double>::Max(); }
                }
                const FLDProgressionView Growth = Mode->Development->ReadProgression(Id);
                if (Growth.ActiveOpportunitiesConsumed == 1 && !Rest0Picks.Contains(Id))
                {
                    Rest0Picks.Add(Id, Mode->GetAuthorityNowMs() - Mode->ReadPublicMatch().StartedAtMs);
                }
            }
            ++Rest0Samples;
            return true;
        }
        if (View.Phase != ELDMatchPhase::Development || View.Generation != Lobby.Generation || !Cast<ALDCharacter>(PC->GetPawn())
            || PC->OwnerState.LifePhase != ELDLifePhase::Alive) { return true; }
        if (OpponentIds.IsEmpty()) { OpponentIds = OpponentRows(Lobby); }
        Check(CountKind(Lobby, ELDContenderKind::Human) == HumanCount && OpponentIds.Num() == OpponentCount,
            TEXT("The room plays with three humans and three AI on the locked roster"));
        if (bHost)
        {
            Check(Rest0Samples > 0 && Rest0AIFeet.Num() == OpponentCount && MaxRest0AIDriftCm < 1. && MaxRest0AIStrayCm < 1.,
                TEXT("Before training an R0 AI stays at its initial foot, and in the sign-up window it does not stray farther from its disk than its first sample"));
            Check(Rest0Signed.Num() == OpponentCount && Rest0PKEntrants.Num() == OpponentCount,
                TEXT("Every opponent AI stood in its own sign-up disk at the R0 lock and entered the PK (站圈报名)"));
            Summary->SetNumberField(TEXT("rest0_samples"), Rest0Samples);
            Summary->SetNumberField(TEXT("rest0_ai_drift_cm"), MaxRest0AIDriftCm);
            Summary->SetNumberField(TEXT("rest0_ai_stray_cm"), MaxRest0AIStrayCm);
            Summary->SetNumberField(TEXT("rest0_signed"), Rest0Signed.Num());
            Summary->SetNumberField(TEXT("rest0_pk_entrants"), Rest0PKEntrants.Num());
            Summary->SetNumberField(TEXT("rest0_trainers"), Rest0Trainers.Num());
        }
        if (bHost && !BindOpponents(*World, *Mode, Lobby)) { return Finish(); }
        if (!Passed) { return Finish(); }
        Stage = ELDOpponentProbeStage::Match;
        return true;
    }
    case ELDOpponentProbeStage::Match:
    {
        int64 MarkNowMs = 0;
        if (!bPlaytestMarked && View.Phase == ELDMatchPhase::Development && PC->ReadDemoAuthorityTimeMs(MarkNowMs)
            && LDMatchTimeline::FieldNowFromView(View.Timeline, MarkNowMs) >= 60000)
        {
            PC->PlaytestMark();
            bPlaytestMarked = true;
        }
        // Rests are sampled too: their phase-boundary lives are neither deaths nor respawns.
        if (bHost && Mode) { PhaseFixtures(*World, *Mode); }
        if (bHost && View.Phase == ELDMatchPhase::Rest) { Sample(*World, *Mode); }
        if (bHost && (View.Phase == ELDMatchPhase::Development || View.Phase == ELDMatchPhase::CaptureOpen))
        { Sample(*World, *Mode); CaptureHost(*PC); ReviewShots(*PC, *Public, false); }
        if (!bHost)
        {
            // Ordinary vision decides what an owning client sees of the AI; this is evidence, not a requirement.
            int32 Seen = 0;
            if (const ALDWorldViewRelay* Relay = PC->LocalView())
            {
                const FLDObservedFrameRead Read = Relay->ReadObservedFrame();
                if (Read.Frame)
                {
                    for (const FLDObservedUnitView& Unit : Read.Frame->Units)
                    {
                        Seen += Unit.Kind == ELDObservedUnitKind::Contender && OpponentIds.Contains(Unit.ContenderId) ? 1 : 0;
                    }
                }
            }
            MaxSeenOpponents = FMath::Max(MaxSeenOpponents, Seen);
        }
        if (View.Phase != ELDMatchPhase::Results) { return true; }
        const FLDResultView& Result = Public->MatchResult;
        if (Result.Generation != View.Generation || Result.ResultId != View.ResultId) { return true; }
        const FLDResultRow* Winner = Result.Rows.FindByPredicate([&Result](const FLDResultRow& Row) { return Row.ContenderId == Result.Winner; });
        // The win is the guard of a graffiti that an opponent AI sprayed; the public end view still names him as its owner.
        Check(Result.Reason == ELDMatchEndReason::GuardSucceeded && Winner && Winner->Kind == ELDContenderKind::OpponentAI
            && OpponentIds.Contains(Result.Winner) && View.GraffitiOwner == Result.Winner && View.GuardDueAtMs == Result.EndedAtMs,
            TEXT("An opponent AI wins the match by spraying the flagpole and guarding its graffiti"));
        Check(Result.Rows.Num() == HumanCount + OpponentCount && !Result.Rows.ContainsByPredicate([](const FLDResultRow& Row)
            { return Row.FinalLevel < 1 || Row.bLeft; }), TEXT("The frozen result lists all six contenders with committed final levels"));
        Summary->SetStringField(TEXT("winner"), Result.Winner.ToString(EGuidFormats::Digits));
        Summary->SetNumberField(TEXT("ended_at_ms"), double(Result.EndedAtMs));
        Summary->SetNumberField(TEXT("duration_ms"), double(Result.DurationMs));
        if (bHost)
        {
            FinishPhaseFixtures();
            Summary->SetArrayField(TEXT("screenshots"), Screenshots);
            CloseHost(*Mode, Result.EndedAtMs, Result);
        }
        else { Summary->SetNumberField(TEXT("max_seen_opponents"), MaxSeenOpponents); }
        if (!Passed || !Mark(Role + TEXT("Results.json"))) { return Finish(); }
        Stage = ELDOpponentProbeStage::Results;
        return true;
    }
    case ELDOpponentProbeStage::Results:
    {
        // The rendered Host keeps the results page up until its review screenshot of it has been taken.
        if (bHost) { ReviewShots(*PC, *Public, true); }
        if (bHost && !ReturnSent && AllMarkers(TEXT("Results.json"))
            && (!FApp::CanEverRender() || (ReviewTaken.Contains(TEXT("HostResults")) && FPlatformTime::Seconds() - LastReviewAt >= 1.)))
        {
            PC->ReturnDemoToLobby();
            ReturnSent = true;
        }
        if (Lobby.Generation == FirstGeneration || Lobby.Phase != ELDMatchPhase::Lobby
            || View.Generation != Lobby.Generation || View.Phase != ELDMatchPhase::Lobby || PC->GetPawn()) { return true; }
        if (bHost)
        {
            // Each adapter ends itself on its first tick after the core retired its match.
            int32 Remaining = 0;
            for (TActorIterator<ALDOpponentController> It(World); It; ++It) { Remaining += IsValid(*It) ? 1 : 0; }
            if (!ReturnedAt) { ReturnedAt = FPlatformTime::Seconds(); }
            if (Remaining && FPlatformTime::Seconds() - ReturnedAt < 15.) { return true; }
            Check(!Remaining, TEXT("Every AI controller ends with its match"));
        }
        Check(CountKind(Lobby, ELDContenderKind::Human) == HumanCount && !Lobby.Members.ContainsByPredicate([](const FLDLobbyMember& Row)
            { return Row.Ready; }), TEXT("The same connections return to a new, unready Lobby generation"));
        Check(View.CircleState == ELDCircleState::NotOpen && !View.ResultId.IsValid(),
            TEXT("The new Lobby carries no capture state or result"));
        Summary->SetNumberField(TEXT("next_generation"), double(Lobby.Generation));
        Summary->SetNumberField(TEXT("next_ai_count"), Lobby.AICount);
        Stage = ELDOpponentProbeStage::Done;
        return Finish();
    }
    default:
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpponentProbeReturnHoldTest, "Lockdown.Rules.DemoOpponentProbe.ReturnHold",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoOpponentProbeReturnHoldTest::RunTest(const FString& Parameters)
{
    using LDDemoOpponentProbeDetail::ReturnLifeRetired;
    TestTrue(TEXT("A retired rest life has no standing field body"), ReturnLifeRetired(ELDLifePhase::Dead, false, false));
    TestFalse(TEXT("A live life is not a retired rest life"), ReturnLifeRetired(ELDLifePhase::Alive, false, false));
    TestFalse(TEXT("A retained body identity is not a retired rest life"), ReturnLifeRetired(ELDLifePhase::Dead, true, false));
    TestFalse(TEXT("A retained actor is not a retired rest life"), ReturnLifeRetired(ELDLifePhase::Dead, false, true));
    TestFalse(TEXT("An unspawned life is not a retired rest life"), ReturnLifeRetired(ELDLifePhase::Unspawned, false, false));
    TestFalse(TEXT("A departed life is not a retired rest life"), ReturnLifeRetired(ELDLifePhase::Left, false, false));
    return true;
}

void LDDemoTests::StartOpponentProbe(ALDPlayerController& Controller)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("LDDemoOpponentProbe")) || !Controller.IsLocalController()
        || Cast<ALDOpponentController>(&Controller)
        || UWorld::RemovePIEPrefix(Controller.GetWorld()->GetOutermost()->GetName()) != LDDemoEntry::WorldMap) { return; }
    auto Probe = MakeShared<FLDDemoOpponentProbe>();
    // #198: a full match runs R0, three timed field segments and the rests R1..R3 before the flagpole opens in the final
    // field. Without early readiness, each rest can run its whole approved PK: R0 180+5+8+3+60+3+12+5 = 276 s,
    // R1..R3 each 120+5+8+3+60+3+12+5 = 216 s, fields 3 x 180 s. The flagpole opens after at most
    // 276 + 3 x 216 + 540 = 1464 s; retain 505 s for the lobby, capture contest and results.
    int32 Timeout = 1980;
    FParse::Value(FCommandLine::Get(), TEXT("LDDemoOpponentTimeout="), Timeout);
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoOpponentOutput="), Probe->Output)
        || !FParse::Value(FCommandLine::Get(), TEXT("LDDemoOpponentRole="), Probe->Role)
        || (Probe->Role != TEXT("Host") && Probe->Role != TEXT("Client1") && Probe->Role != TEXT("Client2")
            && Probe->Role != TEXT("Telemetry"))
        || Timeout < 300 || Timeout > 2400
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Probe->Output),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts"))))
    {
        UE_LOG(LogLDTest, Error, TEXT("LD_DEMO_OPPONENT_PROBE Invalid isolated test arguments."));
        FPlatformMisc::RequestExitWithStatus(false, 2);
        return;
    }
    if (Probe->Role == TEXT("Telemetry"))
    {
        StartOpponentTelemetry(Controller, Probe->Output, Timeout);
        return;
    }
    Probe->Controller = &Controller;
    Probe->Deadline = FPlatformTime::Seconds() + Timeout;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float) { return Probe->Tick(); }), .1f);
}
#endif
