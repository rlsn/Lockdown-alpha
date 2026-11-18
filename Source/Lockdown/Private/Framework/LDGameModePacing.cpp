#include "Framework/LDGameMode.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDevelopmentDirector.h"
#include "Development/LDGrowthDirector.h"
#include "Framework/LDPlayerController.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapRegistry.h"
#include "Vision/LDVisionDirector.h"
#include "World/LDCampDirector.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

// P6 field pacing (spec §10-§11): the match's daily modifiers, the mid-field objectives (spray mini flags and the arena),
// the death screen's spawn choice, delivery lockers, the second-hand dealer, vending machines and the rain and typhoon.
// The rules are pure (World/LDFieldPacing.h); this file feeds them the committed feet and pays their rewards through the
// growth composite. Every timer is field progress T, so a rest pauses them like the other field timers.

namespace LDGameModePacingDetail
{
uint64 SeedOf(const FGuid& Match, uint64 Salt)
{
    return LDAirdrop::Mix((uint64(Match.A) << 32 | Match.B) ^ LDAirdrop::Mix((uint64(Match.C) << 32 | Match.D) + Salt));
}

double Cm(const FLDDemoRational& Value) { return Value.Denominator > 0 ? double(Value.Numerator) / double(Value.Denominator) : 0.; }

// The next multiple of Interval strictly after NowT (field T); MAX_int64 for a bad interval.
int64 NextMultiple(int64 NowT, int64 Interval)
{
    return Interval > 0 ? (NowT / Interval + 1) * Interval : MAX_int64;
}
}

void ALDGameMode::ResetPacing()
{
    DailyModifiers.Reset();
    Objective = FLDObjectiveState();
    Lockers = FLDLockerState();
    Peddler = FLDPeddlerPlan();
    SpawnChoices.Reset();
    PendingVending.Reset();
    PacingControlled.Reset();
    PacingCheckedAtMs = 0;
    BountyDeaths.Reset();
}

bool ALDGameMode::InitializePacing(FString& Error)
{
    using namespace LDGameModePacingDetail;
    ResetPacing();
    const ULDMapRegistry* Registry = GetWorld() ? GetWorld()->GetSubsystem<ULDMapRegistry>() : nullptr;
    const FLDDemoWorldGeometry* Geometry = Registry ? Registry->GetGeometry() : nullptr;
    if (!DemoContent || !Registry || !Geometry || !MatchContext.LedgerMatch.IsValid())
    {
        Error = TEXT("Field pacing needs the approved content, the registered Demo world and the match identity.");
        return false;
    }
    const FLDDemoContentDefinitions& Content = DemoContent->Get();
    const auto& P = Content.Parameters;
#if WITH_DEV_AUTOMATION_TESTS
    if (!DevDailyPreset.IsEmpty()) { DailyModifiers = DevDailyPreset; }
    else
#endif
    LDPacing::DrawDailyModifiers(SeedOf(MatchContext.LedgerMatch, 0xDA11), P.Daily.Count, DailyModifiers);
    TArray<LDFieldPacing::FPoint> Flags, LockerPoints;
    TArray<FName> CampAnchors;
    for (const FLDDemoWorldAnchor& Anchor : Geometry->Anchors)
    {
        const FName Id(*Anchor.Id);
        if (Anchor.Kind == ELDDemoWorldAnchorKind::Camp) { CampAnchors.Add(Id); continue; }
        if (Anchor.Kind != ELDDemoWorldAnchorKind::MiniFlag && Anchor.Kind != ELDDemoWorldAnchorKind::Locker) { continue; }
        const FLDMapBinding* Binding = Registry->Resolve(Id, Registry->Generation);
        if (!Binding)
        {
            Error = FString::Printf(TEXT("Layout point '%s' is not bound to registered ground."), *Anchor.Id);
            return false;
        }
        (Anchor.Kind == ELDDemoWorldAnchorKind::MiniFlag ? Flags : LockerPoints).Add({Id, Binding->Position});
    }
    const FLDMapBinding* Capture = Registry->Resolve(FName(*Content.World.CaptureId), Registry->Generation);
    if (!Capture
        || !Objective.Begin(P, Flags, Capture->Position, SeedOf(MatchContext.LedgerMatch, 0x0B1E), Error)
        || !Lockers.Begin(P, LockerPoints, SeedOf(MatchContext.LedgerMatch, 0x10CC), Error)
        || !Peddler.Plan(P, CampAnchors, SeedOf(MatchContext.LedgerMatch, 0x9EDD), Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("Field pacing needs the flagpole anchor."); }
        return false;
    }
    FString Ids;
    for (const ELDDailyModifier Modifier : DailyModifiers) { Ids += FString(Ids.IsEmpty() ? TEXT("") : TEXT(",")) + LDPacing::DailyId(Modifier); }
    FString Stocked;
    for (const FLDLockerState::FLocker& Locker : Lockers.Lockers) { Stocked += (Stocked.IsEmpty() ? TEXT("") : TEXT(",")) + Locker.Point.Id.ToString(); }
    UE_LOG(LogLDWorld, Display, TEXT("LD_PACING_BEGIN generation=%u daily=%s lockers=%s peddler_t_ms=%lld peddler_camp=%s"),
        MatchContext.Generation, *Ids, *Stocked, Peddler.AppearT, *Peddler.CampAnchor.ToString());
    Error.Reset();
    return true;
}

int64 ALDGameMode::NextPacingDueMs() const
{
    using namespace LDGameModePacingDetail;
    if (!IsFieldRunning() || IsShowdown() || !DemoContent) { return MAX_int64; }
    const int64 T = GetFieldNowMs();
    const auto& D = DemoContent->Get().Parameters.Daily;
    int64 Due = FMath::Min(Objective.NextDueT(T), Lockers.NextDueT());
    if (!Peddler.bSpawned && Peddler.AppearT > T) { Due = FMath::Min(Due, Peddler.AppearT); }
    if (Peddler.bSpawned && !Peddler.bGone && Peddler.LeaveT > T) { Due = FMath::Min(Due, Peddler.LeaveT); }
    if (HasDaily(ELDDailyModifier::Rain)) { Due = FMath::Min(Due, NextMultiple(T, D.RainIntervalMs)); }
    if (HasDaily(ELDDailyModifier::Typhoon)) { Due = FMath::Min(Due, NextMultiple(T, D.TyphoonIntervalMs)); }
    return Due == MAX_int64 ? MAX_int64 : FieldDueAuthorityMs(Due);
}

void ALDGameMode::BuildPacingPresence(TArray<LDFieldPacing::FPresence>& Out) const
{
    Out.Reset();
    for (const FGuid& Id : RosterOrder)
    {
        FLDCombatBodyRef Body;
        FVector Foot;
        if (!IsMemberPresent(Id) || RestLives.Contains(Id) || !ReadLivingBody(Id, Body, Foot)) { continue; }
        LDFieldPacing::FPresence& Presence = Out.AddDefaulted_GetRef();
        Presence.Contender = Id;
        Presence.FootCm = Foot;
        Presence.bProtected = IsContenderProtected(Id);
        Presence.bControlled = PacingControlled.Contains(Id);
        Presence.bDamaged = Combat && Combat->TookDamageSince(Body, PacingCheckedAtMs);
    }
}

bool ALDGameMode::AdvanceFieldPacing(const FLDGameplayWritePermit& Permit, FString& Error)
{
    using namespace LDGameModePacingDetail;
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None || !DemoContent || !Combat || !Camps)
    {
        Error = TEXT("Field pacing advances only under the current RespawnsAndWorld permit with Combat and the camp owner.");
        return false;
    }
    if (!IsFieldRunning() || IsShowdown()) { Error.Reset(); return true; }
    const int64 T = Timeline.FieldMsAt(Permit.GetBoundary().AtMs);
    const auto& P = DemoContent->Get().Parameters;
    if (Objective.Advance(T))
    {
        bPublicMatchDirty = true;
        const FLDObjectiveView& View = Objective.View;
        UE_LOG(LogLDWorld, Display, TEXT("LD_OBJECTIVE serial=%u kind=%s phase=%u anchor=%s field_t_ms=%lld"), View.Serial,
            View.Kind == ELDObjectiveKind::Arena ? TEXT("arena") : TEXT("mini_flag"), uint32(View.Phase), *View.AnchorId.ToString(), T);
    }
    // The arena settles at its close: the winner is judged from the living feet now; its reward is paid in Transactions.
    FLDObjectiveView& View = Objective.View;
    if (View.Kind == ELDObjectiveKind::Arena && View.Phase == ELDObjectivePhase::Open && T >= View.ClosesAtT)
    {
        TArray<LDFieldPacing::FPresence> Living;
        BuildPacingPresence(Living);
        View.Winner = Objective.ArenaWinner(Living);
        View.Phase = View.Winner.IsValid() ? ELDObjectivePhase::Won : ELDObjectivePhase::Expired;
        bObjectiveRewardOwed = View.Winner.IsValid();
        bPublicMatchDirty = true;
        UE_LOG(LogLDWorld, Display, TEXT("LD_ARENA_CLOSE serial=%u winner=%s field_t_ms=%lld"), View.Serial, *LDLog::Id(View.Winner), T);
    }
    // Spec §11.3: the dealer comes out once beside a camp, and leaves unkilled.
    if (!Peddler.bSpawned && T >= Peddler.AppearT && T < Peddler.LeaveT)
    {
        bool bBlocked = false;
        if (!Camps->SpawnPeddler(Peddler.CampAnchor, Peddler.OffsetAngleRad, Cm(P.Explore.PeddlerOffsetCm), Permit, bBlocked, Error)) { return false; }
        Peddler.bSpawned = !bBlocked;
        if (Peddler.bSpawned) { bPublicMatchDirty = true; }
    }
    if (Peddler.bSpawned && !Peddler.bGone && T >= Peddler.LeaveT)
    {
        if (!Camps->RetirePeddler(Permit, Error)) { return false; }
        Peddler.bGone = true;
    }
    // Spec §11.1 rain: every DAILY.RainIntervalSec of field T, DAILY.RainPuddles puddles at random legal field points.
    if (HasDaily(ELDDailyModifier::Rain) && P.Daily.RainIntervalMs > 0 && T > 0 && T % P.Daily.RainIntervalMs == 0)
    {
        if (!Combat->SpawnDailyRain(SeedOf(MatchContext.LedgerMatch, 0x4A1 + uint64(T)), P.Daily.RainPuddles, Permit, Error)) { return false; }
    }
    // Spec §11.1 typhoon: every DAILY.TyphoonIntervalSec of field T, everyone and every ground state is blown the same way.
    if (HasDaily(ELDDailyModifier::Typhoon) && P.Daily.TyphoonIntervalMs > 0 && T > 0 && T % P.Daily.TyphoonIntervalMs == 0)
    {
        const double Angle = double(SeedOf(MatchContext.LedgerMatch, 0x7F0 + uint64(T)) % 3600) / 3600. * 2. * UE_DOUBLE_PI;
        if (!Combat->ApplyTyphoon(FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)), Cm(P.Daily.TyphoonPushCm), Permit, Error)) { return false; }
    }
    Error.Reset();
    return true;
}

bool ALDGameMode::PayPacingReward(const FGuid& Recipient, FName Event, uint64 Serial, int32 Vouchers, int32 Tickets,
    ELDFieldRewardKind Offer, const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (!Development)
    {
        Error = TEXT("A field pacing reward needs the development domain.");
        return false;
    }
    if (Vouchers > 0 || Tickets > 0)
    {
        if (const ELDGrowthReason Reason = Development->GrantVouchers(Recipient, Vouchers, Tickets, *Event.ToString(), Permit);
            Reason != ELDGrowthReason::None)
        {
            Error = FString::Printf(TEXT("The %s reward to %s was refused: %s."), *Event.ToString(), *LDLog::Id(Recipient),
                LDGrowthWire::ReasonName(Reason));
            return false;
        }
    }
    return Offer == ELDFieldRewardKind::None || PayFieldReward(Recipient, Event, Serial, 0, Offer, Permit, Error);
}

bool ALDGameMode::ClaimFieldPacing(const FLDGameplayWritePermit& Permit, FString& Error)
{
    using namespace LDGameModePacingDetail;
    if (ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None || !DemoContent || !Combat || !Development)
    {
        Error = TEXT("Field pacing claims need the current Transactions permit, Combat and the growth domain.");
        return false;
    }
    const int64 AtMs = Permit.GetBoundary().AtMs;
    ON_SCOPE_EXIT
    {
        PacingControlled.Reset();
        PacingCheckedAtMs = AtMs;
    };
    if (!IsFieldRunning() || IsShowdown())
    {
        for (const auto& Pair : PendingVending) { TellVending(Pair.Key, ELDVendingResult::Closed); }
        PendingVending.Reset();
        Error.Reset();
        return true;
    }
    const auto& P = DemoContent->Get().Parameters;
    const int64 T = Timeline.FieldMsAt(AtMs);
    TArray<LDFieldPacing::FPresence> Living;
    BuildPacingPresence(Living);
    // Mini flag spray.
    bool bChanged = false;
    if (Objective.StepFlag(T, Living, bChanged))
    {
        const FLDObjectiveView& View = Objective.View;
        UE_LOG(LogLDWorld, Display, TEXT("LD_MINI_FLAG_WON serial=%u anchor=%s winner=%s field_t_ms=%lld"), View.Serial,
            *View.AnchorId.ToString(), *LDLog::Id(View.Winner), T);
        if (!PayPacingReward(View.Winner, TEXT("mini_flag"), View.Serial, P.Vouchers.MiniFlag, 0, ELDFieldRewardKind::MiniFlagOffer,
            Permit, Error)) { return false; }
    }
    if (bChanged) { bPublicMatchDirty = true; }
    // The arena winner: 20 vouchers, a five-card offer and one more final mod option.
    if (bObjectiveRewardOwed)
    {
        bObjectiveRewardOwed = false;
        const FLDObjectiveView& View = Objective.View;
        if (!PayPacingReward(View.Winner, TEXT("arena"), View.Serial, P.Vouchers.Arena, 0, ELDFieldRewardKind::ArenaOffer, Permit, Error)
            || !Development->GrantExtraFinalModCard(View.Winner, Permit, Error)) { return false; }
    }
    // Lockers: sightings (sight from the foot within the vision radius), then channels and openings.
    if (Vision)
    {
        const ULDMapRegistry* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
        const double Radius = Cm(P.VisionRadiusCm) * (HasDaily(ELDDailyModifier::Blackout) ? double(10000 - P.Daily.BlackoutVisionCutBp) / 10000. : 1.);
        for (FLDLockerState::FLocker& Locker : Lockers.Lockers)
        {
            if (Locker.bOpened) { continue; }
            for (const LDFieldPacing::FPresence& Presence : Living)
            {
                if (Locker.SeenBy.Contains(Presence.Contender) || FVector::DistSquared2D(Presence.FootCm, Locker.Point.PositionCm) > Radius * Radius)
                { continue; }
                if (Registry && Vision->TraceSight(Presence.FootCm, Locker.Point.PositionCm, Registry->Revision()) == ELDSightResult::Visible)
                {
                    Locker.SeenBy.Add(Presence.Contender);
                    if (ALDPlayerController* Controller = FindContenderController(Presence.Contender)) { SyncOwner(Controller); }
                }
            }
        }
    }
    TArray<TPair<int32, FGuid>> Opened;
    bool bLockerChanged = false;
    Lockers.Step(T, Living, Opened, bLockerChanged);
    for (const TPair<int32, FGuid>& Open : Opened)
    {
        const FLDLockerState::FLocker& Locker = Lockers.Lockers[Open.Key];
        // Spec §11.3: half the lockers hold a card offer, half the voucher prize (a private per-locker draw).
        const bool bCards = int64(SeedOf(MatchContext.LedgerMatch, 0x10C0 + uint64(Open.Key)) % 10000) < P.Explore.LockerCardChanceBp;
        UE_LOG(LogLDWorld, Display, TEXT("LD_LOCKER_OPEN locker=%s contender=%s prize=%s field_t_ms=%lld"), *Locker.Point.Id.ToString(),
            *LDLog::Id(Open.Value), bCards ? TEXT("cards") : TEXT("vouchers"), T);
        if (!PayPacingReward(Open.Value, TEXT("locker"), uint64(Open.Key) + 1, bCards ? 0 : P.Vouchers.Locker, 0,
            bCards ? ELDFieldRewardKind::LockerOffer : ELDFieldRewardKind::None, Permit, Error)) { return false; }
        for (const FGuid& Seen : Locker.SeenBy)
        {
            if (ALDPlayerController* Controller = FindContenderController(Seen)) { SyncOwner(Controller); }
        }
    }
    // Vending purchases queued since the last boundary.
    TArray<FGuid> Buyers;
    PendingVending.GetKeys(Buyers);
    Buyers.Sort();
    for (const FGuid& Buyer : Buyers)
    {
        const FLDPendingVending Purchase = PendingVending.FindAndRemoveChecked(Buyer);
        FLDCombatBodyRef Body;
        FVector Foot;
        const FLDProgressionState* Progression = Development->FindProgression(Buyer);
        const int32 Price = Purchase.Item == ELDVendingItem::Drink ? P.Explore.VendingDrinkPrice : P.Explore.VendingBuffPrice;
        const ELDVendingResult Refusal = !ReadLivingBody(Buyer, Body, Foot) || Body.Victim.Life != Purchase.Life ? ELDVendingResult::NotAlive
            : NearbyVending(Buyer).IsNone() ? ELDVendingResult::NotAtMachine
            : !Progression || Progression->Vouchers < Price ? ELDVendingResult::NotEnoughVouchers : ELDVendingResult::None;
        if (Refusal != ELDVendingResult::None)
        {
            UE_LOG(LogLDWorld, Display, TEXT("LD_VENDING_REFUSED contender=%s item=%u result=%u"), *LDLog::Id(Buyer), uint32(Purchase.Item),
                uint32(Refusal));
            TellVending(Buyer, Refusal);
            continue;
        }
        if (const ELDGrowthReason Reason = Development->SpendVouchers(Buyer, Price, TEXT("vending"), Permit); Reason != ELDGrowthReason::None)
        {
            Error = FString::Printf(TEXT("The vending payment of %s was refused: %s."), *LDLog::Id(Buyer), LDGrowthWire::ReasonName(Reason));
            return false;
        }
        bool bApplied = false;
        if (!Combat->ApplyVending(Body, Purchase.Item, Purchase.Element, Permit, bApplied, Error)) { return false; }
        UE_LOG(LogLDWorld, Display, TEXT("LD_VENDING contender=%s item=%u element=%u price=%d applied=%d"), *LDLog::Id(Buyer),
            uint32(Purchase.Item), uint32(Purchase.Element), Price, bApplied ? 1 : 0);
        if (ALDPlayerController* Controller = FindContenderController(Buyer)) { SyncOwner(Controller); }
        TellVending(Buyer, ELDVendingResult::Delivered);
    }
    Error.Reset();
    return true;
}

void ALDGameMode::TellVending(const FGuid& Buyer, ELDVendingResult Result)
{
    if (ALDPlayerController* Controller = FindContenderController(Buyer)) { Controller->ClientVending(Result); }
}

void ALDGameMode::NoteContenderDamage(const FGuid& Attacker, const FGuid& Victim, int64 DamageP2)
{
    if (Objective.View.Kind != ELDObjectiveKind::Arena || Objective.View.Phase != ELDObjectivePhase::Open || !IsFieldRunning()) { return; }
    FLDCombatBodyRef AttackerBody, VictimBody;
    FVector AttackerFoot, VictimFoot;
    // Spec §10.3: only damage dealt while both stand inside the circle counts.
    if (Attacker == Victim || !ReadLivingBody(Attacker, AttackerBody, AttackerFoot) || !ReadLivingBody(Victim, VictimBody, VictimFoot)
        || !Objective.IsInside(AttackerFoot) || !Objective.IsInside(VictimFoot)) { return; }
    Objective.NoteArenaDamage(Attacker, DamageP2, GetFieldNowMs());
}

void ALDGameMode::OpenSpawnChoice(const FGuid& Contender, uint32 Life, int64 AtMs)
{
    using namespace LDGameModePacingDetail;
    if (!DemoContent) { return; }
    const auto& Content = DemoContent->Get();
    TArray<FName> Anchors;
    for (const FString& Id : Content.World.SpawnIds) { Anchors.Add(FName(*Id)); }
    FLDSpawnChoice& Choice = SpawnChoices.Add(Contender);
    Choice.Life = Life;
    Choice.Chosen = INDEX_NONE;
    LDFieldPacing::DrawCandidates(SeedOf(MatchContext.LedgerMatch, 0x5BA7), uint64(Contender.A ^ Contender.D) * 1315423911ull + Life, Anchors,
        Content.Parameters.SpawnChoiceCount, Choice.Candidates);
    Choice.DeadlineMs = AtMs + Content.Parameters.SpawnChoiceMs;
    // Spec §13: an opponent AI chooses at once.
    for (const auto& Pair : Contenders)
    {
        if (Pair.Value.State.ContenderId == Contender && Pair.Value.Kind != ELDContenderKind::Human) { ChooseSpawnForAI(Contender); }
    }
    UE_LOG(LogLDMatch, Display, TEXT("LD_SPAWN_CHOICE_OPEN contender=%s life=%u candidates=%s,%s"), *LDLog::Id(Contender), Life,
        Choice.Candidates.IsValidIndex(0) ? *Choice.Candidates[0].ToString() : TEXT("-"),
        Choice.Candidates.IsValidIndex(1) ? *Choice.Candidates[1].ToString() : TEXT("-"));
}

ELDSpawnChoiceResult ALDGameMode::ChooseSpawnForAI(const FGuid& Contender)
{
    FLDSpawnChoice* Choice = SpawnChoices.Find(Contender);
    const ULDMapRegistry* Registry = GetWorld() ? GetWorld()->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!Choice || !Registry || Choice->Candidates.IsEmpty()) { return ELDSpawnChoiceResult::NoChoice; }
    TArray<FVector> Positions, Goals;
    for (const FName& Id : Choice->Candidates)
    {
        const FLDMapBinding* Binding = Registry->Resolve(Id, Registry->Generation);
        Positions.Add(Binding ? Binding->Position : FVector(MAX_flt));
    }
    // The nearest public goal: an announced or open objective, else the flagpole.
    if (Objective.View.Phase == ELDObjectivePhase::Announced || Objective.View.Phase == ELDObjectivePhase::Open)
    {
        Goals.Add(Objective.View.CentreCm);
    }
    else if (DemoContent)
    {
        if (const FLDMapBinding* Capture = Registry->Resolve(FName(*DemoContent->Get().World.CaptureId), Registry->Generation)) { Goals.Add(Capture->Position); }
    }
    Choice->Chosen = FMath::Max(0, LDFieldPacing::NearestCandidate(Positions, Goals));
    return ELDSpawnChoiceResult::Chosen;
}

ELDSpawnChoiceResult ALDGameMode::SubmitSpawnChoice(ALDPlayerController& Controller, uint32 Generation, uint32 Life, uint8 Index)
{
    const FGuid Id = ContenderId(&Controller);
    if (!Id.IsValid() || !IsMemberPresent(Id)) { return ELDSpawnChoiceResult::NotContender; }
    if (Generation != MatchContext.Generation) { return ELDSpawnChoiceResult::StaleMatch; }
    FLDSpawnChoice* Choice = SpawnChoices.Find(Id);
    const FLDLifeRecord* Record = Lives.Find(Id);
    if (!Choice || !Record || Record->Phase != ELDLifePhase::Dead || AuthorityNowMs > Choice->DeadlineMs)
    {
        return ELDSpawnChoiceResult::NoChoice;
    }
    if (Choice->Life != Life || Record->Life != Life) { return ELDSpawnChoiceResult::StaleLife; }
    if (!Choice->Candidates.IsValidIndex(Index)) { return ELDSpawnChoiceResult::OutOfRange; }
    Choice->Chosen = Index;
    UE_LOG(LogLDMatch, Display, TEXT("LD_SPAWN_CHOICE contender=%s life=%u anchor=%s"), *LDLog::Id(Id), Life, *Choice->Candidates[Index].ToString());
    SyncOwner(&Controller);
    return ELDSpawnChoiceResult::Chosen;
}

FName ALDGameMode::NearbyVending(const FGuid& Contender) const
{
    const ULDMapRegistry* Registry = GetWorld() ? GetWorld()->GetSubsystem<ULDMapRegistry>() : nullptr;
    const FLDDemoWorldGeometry* Geometry = Registry ? Registry->GetGeometry() : nullptr;
    FLDCombatBodyRef Body;
    FVector Foot;
    if (!DemoContent || !Geometry || !IsFieldRunning() || IsShowdown() || RestLives.Contains(Contender) || !ReadLivingBody(Contender, Body, Foot))
    {
        return NAME_None;
    }
    const double Range = LDGameModePacingDetail::Cm(DemoContent->Get().Parameters.AirdropPickupRadiusCm);
    for (const FLDDemoWorldAnchor& Anchor : Geometry->Anchors)
    {
        if (Anchor.Kind != ELDDemoWorldAnchorKind::Vending) { continue; }
        const FLDMapBinding* Binding = Registry->Resolve(FName(*Anchor.Id), Registry->Generation);
        if (Binding && FVector::DistSquared2D(Foot, Binding->Position) <= Range * Range) { return FName(*Anchor.Id); }
    }
    return NAME_None;
}

ELDVendingResult ALDGameMode::QueueVending(const FGuid& Contender, uint32 Life, ELDVendingItem Item, uint8 Element)
{
    if (Item == ELDVendingItem::None || uint8(Item) > uint8(ELDVendingItem::Drink)
        || (Item == ELDVendingItem::Drink && (Element < 1 || Element > 5))) { return ELDVendingResult::BadItem; }
    FLDCombatBodyRef Body;
    FVector Foot;
    if (!ReadLivingBody(Contender, Body, Foot) || Body.Victim.Life != Life) { return ELDVendingResult::NotAlive; }
    if (NearbyVending(Contender).IsNone()) { return ELDVendingResult::NotAtMachine; }
    const FLDProgressionState* Progression = Development ? Development->FindProgression(Contender) : nullptr;
    const auto& X = DemoContent->Get().Parameters.Explore;
    const int32 Price = Item == ELDVendingItem::Drink ? X.VendingDrinkPrice : X.VendingBuffPrice;
    if (!Progression || Progression->Vouchers < Price) { return ELDVendingResult::NotEnoughVouchers; }
    if (PendingVending.Contains(Contender)) { return ELDVendingResult::Busy; }
    PendingVending.Add(Contender, {Life, Item, Element});
    return ELDVendingResult::Queued;
}

ELDVendingResult ALDGameMode::SubmitVending(ALDPlayerController& Controller, uint32 Generation, uint32 Life, ELDVendingItem Item, uint8 Element)
{
    const FGuid Id = ContenderId(&Controller);
    if (!Id.IsValid() || !IsMemberPresent(Id)) { return ELDVendingResult::NotContender; }
    if (Generation != MatchContext.Generation) { return ELDVendingResult::StaleMatch; }
    return QueueVending(Id, Life, Item, Element);
}

void ALDGameMode::ReadSeenLockers(const FGuid& Contender, TArray<FLDLockerView>& Out) const
{
    Out.Reset();
    for (const FLDLockerState::FLocker& Locker : Lockers.Lockers)
    {
        if (!Locker.SeenBy.Contains(Contender)) { continue; }
        FLDLockerView& View = Out.AddDefaulted_GetRef();
        View.AnchorId = Locker.Point.Id;
        View.PositionCm = Locker.Point.PositionCm;
        View.bOpened = Locker.bOpened;
    }
}
