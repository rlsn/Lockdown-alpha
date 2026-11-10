#include "PK/LDPKDirector.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDevelopmentDirector.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Region/LDMapRegistry.h"
#include "Engine/World.h"

// #198 session D: the PK authority (approved outer parameters, issue #198 comment 6752601175 §2-§4/§6; user approval
// "批准整套方案，按票内参数实施"). See PK/LDPKDirector.h for the flow; every number comes from FLDPKParameters (content).

namespace
{
    FLDMatchProviderHooks MakePKProvider()
    {
        FLDMatchProviderHooks Hooks;
        Hooks.Prepare = [](ALDGameMode& Mode, const FLDMatchStartContext& Start, FString& Error)
        {
            if (Mode.PKDirector)
            {
                Error = TEXT("The previous match's PK director was not reset.");
                return false;
            }
            FActorSpawnParameters Spawn;
            Spawn.Owner = &Mode;
            ALDPKDirector* Director = Mode.GetWorld()->SpawnActor<ALDPKDirector>(Spawn);
            if (!Director)
            {
                Error = TEXT("The PK director could not be created.");
                return false;
            }
            Mode.PKDirector = Director;
            return Director->BeginMatch(Start, Error);
        };
        Hooks.MatchClosed = [](ALDGameMode& Mode, const FLDMatchClosed&)
        {
            if (ALDPKDirector* Director = Mode.PKDirector.Get()) { Director->EndMatch(); }
        };
        Hooks.MatchReset = [](ALDGameMode& Mode, const FLDMatchReset&, FString& Error)
        {
            if (ALDPKDirector* Director = Mode.PKDirector.Get())
            {
                Director->EndMatch();
                Director->Destroy();
            }
            Mode.PKDirector = nullptr;
            Error.Reset();
            return true;
        };
        Hooks.RestStep = [](ALDGameMode& Mode, const FLDRestStepNotice& Notice, const FLDGameplayWritePermit& Permit, FString& Error)
        {
            ALDPKDirector* Director = Mode.PKDirector.Get();
            if (!Director)
            {
                Error = TEXT("A rest step reached the PK provider without its director.");
                return false;
            }
            return Director->OnRestStep(Notice, Permit, Error);
        };
        return Hooks;
    }

    const LDMatchProviders::FRegistrar PKProviderRegistration(ELDMatchProvider::PK, &MakePKProvider);

    int64 TotalPoints(const FLDPKAwardView& Row) { return int64(Row.Participation) + Row.Winner + Row.Bet; }
}

ALDPKDirector::ALDPKDirector()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
}

ALDGameMode* ALDPKDirector::AuthorityMode() const
{
    ALDGameMode* Core = Mode.Get();
    return Core && HasAuthority() && Core->PKDirector == this ? Core : nullptr;
}

bool ALDPKDirector::CheckPermit(const FLDGameplayWritePermit& Permit, ELDCommitPhase Phase, FString& Error) const
{
    const ALDGameMode* Core = AuthorityMode();
    if (!Core || Core->ValidateWritePermit(Permit, Phase) != ELDCommitReject::None || Core->GetMatchContext() != Match)
    {
        Error = FString::Printf(TEXT("The PK needs the current permit (phase %u) of its match."), uint32(Phase));
        return false;
    }
    return true;
}

bool ALDPKDirector::BeginMatch(const FLDMatchStartContext& Start, FString& Error)
{
    Mode = Cast<ALDGameMode>(GetOwner());
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    auto* Registry = GetWorld() ? GetWorld()->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!Mode.IsValid() || !Start.Match.IsValid() || !LDDemoEntry::LoadRuntimeContent(Content, Error) || !Registry
        || !Registry->Ensure(Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("The PK needs its match, the approved content and the registered world."); }
        return false;
    }
    if (!FLDPKParameters::FromContent(Content->Get().Parameters, Parameters, Error)) { return false; }
    const auto& World = Content->Get().World;
    const FLDMapBinding* Capture = Registry->Resolve(FName(*World.CaptureId), Registry->Generation);
    const FLDMapBinding* Throw = Registry->Resolve(FName(*World.PKThrowId), Registry->Generation);
    if (!Capture || !Throw || World.PKSpawnIds.Num() != LDPKRules::SpawnAnchors)
    {
        Error = TEXT("The PK needs the registered capture anchor, the throw anchor and exactly six PK spawn anchors.");
        return false;
    }
    SpawnAnchors.Reset();
    for (const FString& Id : World.PKSpawnIds)
    {
        if (!Registry->Resolve(FName(*Id), Registry->Generation))
        {
            Error = FString::Printf(TEXT("PK spawn anchor %s is not registered."), *Id);
            return false;
        }
        SpawnAnchors.Add(FName(*Id));
    }
    Match = Start.Match;
    ArenaCentreCm = Capture->Position;
    ArenaSurface = Capture->Surface;
    ThrowAnchorCm = Throw->Position;
    // One private stream per match: every format, team and spawn draw is committed once from it.
    Random.Initialize(int32(GetTypeHash(FGuid::NewGuid())));
    Serial = 0;
    ResetRest();
    UE_LOG(LogLDMatch, Display, TEXT("LD_PK_BEGIN generation=%u centre=%s throw_anchor=%s"), Match.Generation,
        *ArenaCentreCm.ToCompactString(), *ThrowAnchorCm.ToCompactString());
    Error.Reset();
    return true;
}

void ALDPKDirector::EndMatch()
{
    ResetRest();
    Match = FLDMatchContext();
}

void ALDPKDirector::ResetRest()
{
    Entrants.Reset();
    Bettors.Reset();
    Drawn = FLDPKDraw();
    BetSides.Reset();
    Forfeited.Reset();
    Group.Invalidate();
    bArenaActive = bFightRunning = bDecided = bFinishRequested = false;
    FightStartedAtMs = 0;
    StartMaxHpP2.Reset();
    RingAccumulatorE7.Reset();
    RingSettledThroughMs = 0;
    NextRingTickAtMs = MAX_int64;
    Throws.Reset();
    ThrowReadyAtMs.Reset();
    PendingAwards.Reset();
    AwardIds.Reset();
    bAwardsPaid = false;
    View = FLDPKView();
    View.Generation = Match.Generation;
}

int32 ALDPKDirector::SeatOf(const FGuid& Contender) const
{
    return Entrants.IndexOfByKey(Contender);
}

uint8 ALDPKDirector::SideOf(const FGuid& Contender) const
{
    const int32 Seat = SeatOf(Contender);
    return bArenaActive && Seat != INDEX_NONE && Drawn.SideOfSeat.IsValidIndex(Seat) ? Drawn.SideOfSeat[Seat] : 0;
}

void ALDPKDirector::LivingTeammates(const FGuid& Contender, TArray<FGuid>& Out) const
{
    Out.Reset();
    const ALDGameMode* Core = AuthorityMode();
    const uint8 Side = SideOf(Contender);
    if (!Core || Side == 0) { return; }
    for (int32 Seat = 0; Seat < Entrants.Num(); ++Seat)
    {
        if (Entrants[Seat] != Contender && Drawn.SideOfSeat[Seat] == Side && Core->IsPKLife(Entrants[Seat])) { Out.Add(Entrants[Seat]); }
    }
}

bool ALDPKDirector::CanSpectate(const FGuid& Observer) const
{
    const ALDGameMode* Core = AuthorityMode();
    return Core && bArenaActive && Observer.IsValid() && !Core->IsPKLife(Observer);
}

void ALDPKDirector::AliveOfSeats(TArray<bool>& Out) const
{
    const ALDGameMode* Core = AuthorityMode();
    Out.SetNumZeroed(Entrants.Num());
    for (int32 Seat = 0; Seat < Entrants.Num(); ++Seat)
    {
        Out[Seat] = Core && !Forfeited[Seat] && Core->IsContenderPresent(Entrants[Seat]) && Core->IsPKLife(Entrants[Seat]);
    }
}

void ALDPKDirector::Publish()
{
    ALDGameMode* Core = AuthorityMode();
    View.Generation = Match.Generation;
    View.Entrants.Reset();
    if (View.Serial != 0)
    {
        TArray<bool> Alive;
        AliveOfSeats(Alive);
        for (int32 Seat = 0; Seat < Entrants.Num(); ++Seat)
        {
            FLDPKEntrantView& Row = View.Entrants.AddDefaulted_GetRef();
            Row.Contender = Entrants[Seat];
            Row.Side = Drawn.SideOfSeat.IsValidIndex(Seat) ? Drawn.SideOfSeat[Seat] : 0;
            Row.bAlive = Alive[Seat];
            Row.bForfeited = Forfeited[Seat];
        }
    }
    View.Bettors = Bettors;
    View.Bets.Reset();
    for (int32 Index = 0; Index < Bettors.Num(); ++Index)
    {
        if (BetSides[Index] != 0) { View.Bets.Add({Bettors[Index], BetSides[Index]}); }
    }
    View.ArenaCentreCm = ArenaCentreCm;
    View.ThrowAnchorCm = ThrowAnchorCm;
    View.FightStartedAtMs = FightStartedAtMs;
    View.Throws.Reset();
    for (const FPendingThrow& Throw : Throws) { View.Throws.Add({Throw.Serial, Throw.TargetCm, Throw.LandsAtMs}); }
    View.Awards = PendingAwards;
    if (Core) { Core->MarkPublicMatchDirty(); }
}

bool ALDPKDirector::OnRestStep(const FLDRestStepNotice& Notice, const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (!AuthorityMode() || Notice.Match != Match)
    {
        Error = TEXT("A rest step of another match reached the PK.");
        return false;
    }
    if (Notice.bBuildLocked) { return LockRoster(Notice, Error); }
    switch (Notice.Step)
    {
    case ELDRestStep::Build:
    case ELDRestStep::Return:
        // A new rest starts empty; the return countdown no longer shows the settled PK.
        if (View.Serial != 0 || !Entrants.IsEmpty())
        {
            ResetRest();
            Publish();
        }
        return true;
    case ELDRestStep::Draw: return DrawFormat(Error);
    case ELDRestStep::Betting:
        Publish();
        return true;
    case ELDRestStep::PKReady: return EnterArena(Permit, Error);
    case ELDRestStep::PK: return StartFight(Permit, Error);
    case ELDRestStep::Settlement: return Settle(Permit, Error);
    default: return true;
    }
}

bool ALDPKDirector::LockRoster(const FLDRestStepNotice& Notice, FString& Error)
{
    ALDGameMode* Core = AuthorityMode();
    ResetRest();
    if (Notice.Entrants.Num() < 2)
    {
        Publish();
        Error.Reset();
        return true;
    }
    if (Notice.Entrants.Num() > LDPKRules::SpawnAnchors || Serial == MAX_uint32)
    {
        Error = TEXT("A PK locks two to six entrants.");
        return false;
    }
    Entrants = Notice.Entrants;
    Entrants.Sort();
    Forfeited.Init(false, Entrants.Num());
    // The eligible spectators are the present contenders who did not enter, fixed at the lock.
    for (const FGuid& Id : Core->GetRosterOrder())
    {
        if (Core->IsContenderPresent(Id) && !Entrants.Contains(Id)) { Bettors.Add(Id); }
    }
    Bettors.Sort();
    BetSides.Init(0, Bettors.Num());
    ++Serial;
    Group = FGuid::NewGuid();
    View.Serial = Serial;
    View.RestIndex = Notice.RestIndex;
    View.Outcome = ELDPKOutcome::Pending;
    UE_LOG(LogLDMatch, Display, TEXT("LD_PK_LOCK generation=%u serial=%u rest=%u entrants=%d spectators=%d"), Match.Generation,
        Serial, Notice.RestIndex, Entrants.Num(), Bettors.Num());
    Publish();
    Error.Reset();
    return true;
}

bool ALDPKDirector::DrawFormat(FString& Error)
{
    if (View.Serial == 0) { Error.Reset(); return true; }
    if (!LDPKRules::Draw(Entrants.Num(), Parameters, Random, Drawn, Error)) { return false; }
    View.Format = Drawn.Format;
    View.SideCount = Drawn.SideCount;
    FString Sides;
    for (int32 Seat = 0; Seat < Entrants.Num(); ++Seat)
    {
        Sides += FString::Printf(TEXT("%s%s:%u@%d"), Seat ? TEXT(",") : TEXT(""), *LDLog::Id(Entrants[Seat]),
            uint32(Drawn.SideOfSeat[Seat]), Drawn.AnchorOfSeat[Seat]);
    }
    UE_LOG(LogLDMatch, Display, TEXT("LD_PK_DRAW serial=%u format=%s sides=%u seats=%s"), Serial, *LDLog::EnumName(Drawn.Format),
        uint32(Drawn.SideCount), *Sides);
    Publish();
    Error.Reset();
    return true;
}

bool ALDPKDirector::EnterArena(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (View.Serial == 0) { Error.Reset(); return true; }
    if (!CheckPermit(Permit, ELDCommitPhase::RespawnsAndWorld, Error)) { return false; }
    ALDGameMode* Core = AuthorityMode();
    TArray<ALDGameMode::FLDPKPlacement> Placements;
    for (int32 Seat = 0; Seat < Entrants.Num(); ++Seat)
    {
        // A contender that left after the lock forfeits: no re-draw, no AI stand-in.
        if (!Core->IsContenderPresent(Entrants[Seat]))
        {
            Forfeited[Seat] = true;
            continue;
        }
        if (!Drawn.AnchorOfSeat.IsValidIndex(Seat) || !SpawnAnchors.IsValidIndex(Drawn.AnchorOfSeat[Seat]))
        {
            Error = TEXT("A PK seat has no drawn spawn anchor.");
            return false;
        }
        Placements.Add({Entrants[Seat], SpawnAnchors[Drawn.AnchorOfSeat[Seat]], Group, Drawn.SideOfSeat[Seat]});
    }
    // The arena is active while its lives are placed: their effects and sight belong to the PK group from the start.
    bArenaActive = true;
    TArray<FGuid> Blocked;
    if (!Core->EnterPKLives(Placements, Permit, Blocked, Error)) { return false; }
    if (!Blocked.IsEmpty())
    {
        // A blocked approved anchor is an arena failure, reported and never replaced: nobody fights or is paid.
        UE_LOG(LogLDMatch, Error, TEXT("LD_PK_ARENA_BLOCKED serial=%u blocked=%d outcome=NoContest"), Serial, Blocked.Num());
        bArenaActive = false;
        bDecided = true;
        View.Outcome = ELDPKOutcome::NoContest;
        View.DecidedAtMs = Core->GetAuthorityNowMs();
        if (!Core->ReturnPKLivesToRest(Permit, Error)) { return false; }
    }
    Publish();
    Error.Reset();
    return true;
}

bool ALDPKDirector::StartFight(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (View.Serial == 0 || bDecided) { Error.Reset(); return true; }
    if (!CheckPermit(Permit, ELDCommitPhase::RespawnsAndWorld, Error)) { return false; }
    ALDGameMode* Core = AuthorityMode();
    FightStartedAtMs = Permit.GetBoundary().AtMs;
    bFightRunning = true;
    StartMaxHpP2.Init(0, Entrants.Num());
    RingAccumulatorE7.Init(0, Entrants.Num());
    RingSettledThroughMs = FightStartedAtMs;
    NextRingTickAtMs = FightStartedAtMs + LDPKRules::NextRingTickMs(Parameters, 0);
    // H is each entrant's max HP at the PK start; the build is locked, so it never changes during the fight.
    for (int32 Seat = 0; Seat < Entrants.Num(); ++Seat)
    {
        FLDCombatBodyRef Body;
        FVector Foot;
        int64 Hp = 0, MaxHp = 0;
        if (Core->ReadLivingBody(Entrants[Seat], Body, Foot) && Core->IsPKLife(Entrants[Seat])
            && Core->Combat && Core->Combat->ReadBodyHealth(Body, Hp, MaxHp))
        {
            StartMaxHpP2[Seat] = MaxHp;
        }
    }
    View.Outcome = ELDPKOutcome::Running;
    UE_LOG(LogLDMatch, Display, TEXT("LD_PK_START serial=%u at_ms=%lld"), Serial, FightStartedAtMs);
    Publish();
    Error.Reset();
    return true;
}

bool ALDPKDirector::JudgeAfterDeaths(const FLDGameplayWritePermit& Permit, FString& Error)
{
    ALDGameMode* Core = AuthorityMode();
    if (!Core || View.Serial == 0 || bFinishRequested || !Core->IsRestPhase(Match)
        || Core->GetTimeline().GetStep() != ELDRestStep::PK)
    {
        Error.Reset();
        return true;
    }
    if (!CheckPermit(Permit, ELDCommitPhase::DeathsAndRewards, Error)) { return false; }
    bool bChanged = false;
    for (int32 Seat = 0; Seat < Entrants.Num(); ++Seat)
    {
        if (!Forfeited[Seat] && !Core->IsContenderPresent(Entrants[Seat]))
        {
            Forfeited[Seat] = true;
            bChanged = true;
        }
    }
    if (!bDecided)
    {
        TArray<bool> Alive;
        AliveOfSeats(Alive);
        uint8 Winner = 0;
        const ELDPKOutcome Outcome = LDPKRules::Judge(Drawn.SideOfSeat, Alive, false, Winner);
        if (Outcome == ELDPKOutcome::None)
        {
            Error = TEXT("The PK sides could not be judged.");
            return false;
        }
        if (Outcome == ELDPKOutcome::Won || Outcome == ELDPKOutcome::Draw)
        {
            bDecided = true;
            View.Outcome = Outcome;
            View.WinnerSide = Winner;
            View.DecidedAtMs = Permit.GetBoundary().AtMs;
            UE_LOG(LogLDMatch, Display, TEXT("LD_PK_DECIDED serial=%u outcome=%s winner_side=%u at_ms=%lld"), Serial,
                *LDLog::EnumName(Outcome), uint32(Winner), View.DecidedAtMs);
        }
        bChanged = true;
    }
    if (bDecided)
    {
        // A decided PK (or one that never had a legal arena) ends at this boundary; the settlement follows in it.
        bFinishRequested = true;
        bFightRunning = false;
        Throws.Reset();
        if (!Core->FinishRestPK(Permit, Error)) { return false; }
    }
    if (bChanged) { Publish(); }
    Error.Reset();
    return true;
}

bool ALDPKDirector::Settle(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (View.Serial == 0) { Error.Reset(); return true; }
    if (!CheckPermit(Permit, ELDCommitPhase::RespawnsAndWorld, Error)) { return false; }
    ALDGameMode* Core = AuthorityMode();
    for (int32 Seat = 0; Seat < Entrants.Num(); ++Seat)
    {
        if (!Core->IsContenderPresent(Entrants[Seat])) { Forfeited[Seat] = true; }
    }
    if (!bDecided)
    {
        // The deadline: deaths up to the deadline were committed in this boundary's DeathsAndRewards; whoever is left
        // decides. Several sides alive is a draw, never a judgement by HP or damage.
        TArray<bool> Alive;
        AliveOfSeats(Alive);
        uint8 Winner = 0;
        const ELDPKOutcome Outcome = LDPKRules::Judge(Drawn.SideOfSeat, Alive, true, Winner);
        if (Outcome != ELDPKOutcome::Won && Outcome != ELDPKOutcome::Draw)
        {
            Error = TEXT("The PK deadline could not be judged.");
            return false;
        }
        bDecided = true;
        View.Outcome = Outcome;
        View.WinnerSide = Winner;
        View.DecidedAtMs = Permit.GetBoundary().AtMs;
        UE_LOG(LogLDMatch, Display, TEXT("LD_PK_DECIDED serial=%u outcome=%s winner_side=%u at_ms=%lld deadline=1"), Serial,
            *LDLog::EnumName(Outcome), uint32(Winner), View.DecidedAtMs);
    }
    // The result and its eligibility freeze before the PK lives leave the arena.
    bFightRunning = false;
    Throws.Reset();
    TArray<bool> Eligible;
    Eligible.SetNumZeroed(Entrants.Num());
    for (int32 Seat = 0; Seat < Entrants.Num(); ++Seat) { Eligible[Seat] = !Forfeited[Seat]; }
    TArray<bool> BettorPresent;
    BettorPresent.SetNumZeroed(Bettors.Num());
    for (int32 Index = 0; Index < Bettors.Num(); ++Index) { BettorPresent[Index] = Core->IsContenderPresent(Bettors[Index]); }
    TArray<FLDPKAward> Awards;
    if (!LDPKRules::ComputeAwards(Parameters, View.Outcome, View.WinnerSide, Serial, Drawn.SideOfSeat, Eligible, BetSides,
        BettorPresent, Awards, Error))
    {
        return false;
    }
    PendingAwards.Reset();
    AwardIds.Reset();
    for (const FLDPKAward& Award : Awards)
    {
        const FGuid Recipient = Award.Seat != INDEX_NONE ? Entrants[Award.Seat] : Bettors[Award.Bettor];
        FLDPKAwardView* Row = PendingAwards.FindByPredicate([&Recipient](const FLDPKAwardView& R) { return R.Contender == Recipient; });
        if (!Row)
        {
            Row = &PendingAwards.AddDefaulted_GetRef();
            Row->Contender = Recipient;
            AwardIds.Add(FGuid::NewGuid());
        }
        Row->Participation = uint8(FMath::Clamp<int64>(Row->Participation + Award.Participation, 0, MAX_uint8));
        Row->Winner = uint8(FMath::Clamp<int64>(Row->Winner + Award.Winner, 0, MAX_uint8));
        Row->Bet = uint8(FMath::Clamp<int64>(Row->Bet + Award.Bet, 0, MAX_uint8));
    }
    bAwardsPaid = PendingAwards.IsEmpty();
    bArenaActive = false;
    if (!Core->ReturnPKLivesToRest(Permit, Error)) { return false; }
    UE_LOG(LogLDMatch, Display, TEXT("LD_PK_SETTLED serial=%u outcome=%s winner_side=%u paid_rows=%d"), Serial,
        *LDLog::EnumName(View.Outcome), uint32(View.WinnerSide), PendingAwards.Num());
    Publish();
    Error.Reset();
    return true;
}

bool ALDPKDirector::ProcessTransactions(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (bAwardsPaid || PendingAwards.IsEmpty()) { Error.Reset(); return true; }
    if (!CheckPermit(Permit, ELDCommitPhase::Transactions, Error)) { return false; }
    ALDGameMode* Core = AuthorityMode();
    if (!Core->Development)
    {
        Error = TEXT("PK awards need the growth domain of this match.");
        return false;
    }
    // Paid once, all in this boundary; a recipient that left since the settlement keeps nothing.
    bAwardsPaid = true;
    for (int32 Index = 0; Index < PendingAwards.Num(); ++Index)
    {
        const FLDPKAwardView& Row = PendingAwards[Index];
        const int64 Points = TotalPoints(Row);
        if (Points <= 0 || !Core->IsContenderPresent(Row.Contender)) { continue; }
        const ELDGrowthReason Reason = Core->Development->GrantPKPoints(Row.Contender, Points, AwardIds[Index], Permit);
        if (Reason == ELDGrowthReason::None || Reason == ELDGrowthReason::PKAwardDuplicate) { continue; }
        Error = FString::Printf(TEXT("A PK award to %s was refused: %s."), *LDLog::Id(Row.Contender), LDGrowthWire::ReasonName(Reason));
        return false;
    }
    Error.Reset();
    return true;
}

ELDPKBetResult ALDPKDirector::SubmitBet(const FGuid& Contender, uint32 Generation, uint32 InSerial, uint8 Side)
{
    ALDGameMode* Core = AuthorityMode();
    if (!Core || !Contender.IsValid() || !Core->IsContenderPresent(Contender)) { return ELDPKBetResult::NotContender; }
    if (Generation != Match.Generation || Core->GetMatchContext() != Match) { return ELDPKBetResult::StaleMatch; }
    if (View.Serial == 0) { return ELDPKBetResult::NoPK; }
    if (InSerial != Serial) { return ELDPKBetResult::StaleMatch; }
    const int32 Index = Bettors.IndexOfByKey(Contender);
    if (Index == INDEX_NONE) { return ELDPKBetResult::NotSpectator; }
    if (!Core->IsRestPhase(Match) || Core->GetTimeline().GetStep() != ELDRestStep::Betting) { return ELDPKBetResult::WindowClosed; }
    if (Side == 0 || Side > Drawn.SideCount) { return ELDPKBetResult::UnknownSide; }
    if (BetSides[Index] != Side)
    {
        BetSides[Index] = Side;
        UE_LOG(LogLDMatch, Display, TEXT("LD_PK_BET serial=%u spectator=%s side=%u"), Serial, *LDLog::Id(Contender), uint32(Side));
        Publish();
    }
    return ELDPKBetResult::Placed;
}

ELDPKThrowResult ALDPKDirector::SubmitThrow(const FGuid& Contender, uint32 Generation, uint32 InSerial, const FVector& TargetCm,
    int64& OutReadyAtMs)
{
    OutReadyAtMs = 0;
    ALDGameMode* Core = AuthorityMode();
    if (!Core || !Contender.IsValid() || !Core->IsContenderPresent(Contender)) { return ELDPKThrowResult::NotContender; }
    if (Generation != Match.Generation || Core->GetMatchContext() != Match) { return ELDPKThrowResult::StaleMatch; }
    if (View.Serial == 0) { return ELDPKThrowResult::NoPK; }
    if (InSerial != Serial) { return ELDPKThrowResult::StaleMatch; }
    // Only the spectators of the lock throw; an eliminated entrant watches read-only.
    if (!Bettors.Contains(Contender)) { return ELDPKThrowResult::NotSpectator; }
    if (!bFightRunning || bDecided || !Core->IsRestPhase(Match) || Core->GetTimeline().GetStep() != ELDRestStep::PK)
    {
        return ELDPKThrowResult::NotRunning;
    }
    const int64 NowMs = Core->GetAuthorityNowMs();
    if (const int64* Ready = ThrowReadyAtMs.Find(Contender); Ready && NowMs < *Ready)
    {
        OutReadyAtMs = *Ready;
        return ELDPKThrowResult::CoolingDown;
    }
    if (TargetCm.ContainsNaN() || FVector::DistSquared2D(TargetCm, ArenaCentreCm) > FMath::Square(Parameters.ArenaRadiusCm))
    {
        return ELDPKThrowResult::OutsideArena;
    }
    if (FVector::DistSquared2D(TargetCm, ThrowAnchorCm) > FMath::Square(Parameters.ThrowRangeCm)) { return ELDPKThrowResult::OutOfRange; }
    // The landing point must be legal ground of the arena's surface (not inside a wall, tree or off the map).
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    FCollisionShape Capsule;
    FString Error;
    if (!Registry || !LDDemoEntry::LoadRuntimeContent(Content, Error) || !ULDMapRegistry::ContentCapsule(*Content, Capsule, Error))
    {
        UE_LOG(LogLDMatch, Error, TEXT("LD_PK_THROW_GROUND_UNAVAILABLE %s"), *Error);
        return ELDPKThrowResult::NotGround;
    }
    FLDLegalGround Ground;
    ELDWorldQueryFailure Failure;
    if (!Registry->QueryGround(Registry->Revision(), FVector(TargetCm.X, TargetCm.Y, ArenaCentreCm.Z), Capsule, ArenaSurface, Ground,
        Failure, Error))
    {
        return ELDPKThrowResult::NotGround;
    }
    if (NextThrowSerial == MAX_uint32 || NowMs > MAX_int64 - FMath::Max(Parameters.ThrowTelegraphMs, Parameters.ThrowCooldownMs))
    {
        return ELDPKThrowResult::NotRunning;
    }
    FPendingThrow& Throw = Throws.AddDefaulted_GetRef();
    Throw.Serial = NextThrowSerial++;
    Throw.Thrower = Contender;
    Throw.TargetCm = Ground.SurfaceCm;
    Throw.LandsAtMs = NowMs + Parameters.ThrowTelegraphMs;
    // The cooldown runs from the successful throw.
    OutReadyAtMs = NowMs + Parameters.ThrowCooldownMs;
    ThrowReadyAtMs.Add(Contender, OutReadyAtMs);
    UE_LOG(LogLDMatch, Display, TEXT("LD_PK_THROW serial=%u throw=%u spectator=%s target=%s lands_at_ms=%lld"), Serial, Throw.Serial,
        *LDLog::Id(Contender), *Throw.TargetCm.ToCompactString(), Throw.LandsAtMs);
    Publish();
    return ELDPKThrowResult::Thrown;
}

int64 ALDPKDirector::NextDueMs() const
{
    if (!bFightRunning) { return MAX_int64; }
    int64 Due = NextRingTickAtMs;
    for (const FPendingThrow& Throw : Throws) { Due = FMath::Min(Due, Throw.LandsAtMs); }
    return Due;
}

bool ALDPKDirector::AdvanceEffects(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (!bFightRunning) { Error.Reset(); return true; }
    if (!CheckPermit(Permit, ELDCommitPhase::Effects, Error)) { return false; }
    return LandThrows(Permit, Error) && BurnRing(Permit, Error);
}

bool ALDPKDirector::LandThrows(const FLDGameplayWritePermit& Permit, FString& Error)
{
    ALDGameMode* Core = AuthorityMode();
    const int64 NowMs = Permit.GetBoundary().AtMs;
    Throws.Sort([](const FPendingThrow& A, const FPendingThrow& B)
    { return A.LandsAtMs != B.LandsAtMs ? A.LandsAtMs < B.LandsAtMs : A.Serial < B.Serial; });
    bool bLanded = false;
    while (!Throws.IsEmpty() && Throws[0].LandsAtMs <= NowMs)
    {
        const FPendingThrow Throw = Throws[0];
        Throws.RemoveAt(0);
        bLanded = true;
        const FGuid EffectId = FGuid::NewGuid();
        int32 Hits = 0;
        // Any entrant at the landing is hit (by its actual position at the landing, in sight of the landing point).
        for (const FGuid& Id : Entrants)
        {
            FLDCombatBodyRef Body;
            FVector Foot;
            if (!Core->IsPKLife(Id) || !Core->ReadLivingBody(Id, Body, Foot)
                || FVector::DistSquared2D(Foot, Throw.TargetCm) > FMath::Square(Parameters.ThrowRadiusCm))
            {
                continue;
            }
            bool bVisible = false;
            if (!Core->Combat->HasAreaSight(Throw.TargetCm, Foot, bVisible, Error)) { return false; }
            if (!bVisible) { continue; }
            if (!Core->Combat->ApplyThrowHit(Body, EffectId, Parameters.ThrowSlowBp, Parameters.ThrowSlowMs, Parameters.ThrowExposeMs,
                Permit, Error))
            {
                return false;
            }
            ++Hits;
        }
        UE_LOG(LogLDMatch, Display, TEXT("LD_PK_THROW_LANDED serial=%u throw=%u hits=%d"), Serial, Throw.Serial, Hits);
    }
    if (bLanded) { Publish(); }
    Error.Reset();
    return true;
}

bool ALDPKDirector::BurnRing(const FLDGameplayWritePermit& Permit, FString& Error)
{
    ALDGameMode* Core = AuthorityMode();
    const int64 NowMs = Permit.GetBoundary().AtMs;
    if (NowMs <= RingSettledThroughMs) { Error.Reset(); return true; }
    const int64 From = RingSettledThroughMs - FightStartedAtMs;
    const int64 To = NowMs - FightStartedAtMs;
    // Outside time of this step, judged at the step's authoritative foot (boundary inside), accumulates per entrant.
    for (int32 Seat = 0; Seat < Entrants.Num(); ++Seat)
    {
        FLDCombatBodyRef Body;
        FVector Foot;
        if (!Core->IsPKLife(Entrants[Seat]) || !Core->ReadLivingBody(Entrants[Seat], Body, Foot)
            || LDPKRules::IsInsideRing(Parameters, ArenaCentreCm, Foot, To))
        {
            continue;
        }
        if (!LDPKRules::AccumulateRingDamage(Parameters, StartMaxHpP2[Seat], From, To, RingAccumulatorE7[Seat]))
        {
            Error = TEXT("The PK ring damage left its checked numeric domain.");
            return false;
        }
    }
    RingSettledThroughMs = NowMs;
    if (NowMs < NextRingTickAtMs) { Error.Reset(); return true; }
    NextRingTickAtMs = FightStartedAtMs + LDPKRules::NextRingTickMs(Parameters, To);
    // Every tick settles the accumulated whole P2 as environmental damage.
    for (int32 Seat = 0; Seat < Entrants.Num(); ++Seat)
    {
        const int64 Amount = LDPKRules::TakeRingDamageP2(RingAccumulatorE7[Seat]);
        FLDCombatBodyRef Body;
        FVector Foot;
        if (Amount <= 0 || !Core->IsPKLife(Entrants[Seat]) || !Core->ReadLivingBody(Entrants[Seat], Body, Foot)) { continue; }
        if (!Core->Combat->ApplyEnvironmentDamage(Body, Amount, Permit, Error)) { return false; }
    }
    Error.Reset();
    return true;
}
