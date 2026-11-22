#include "AI/LDOpponentFrame.h"
#include "Combat/LDAttributeProjection.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
#include "Development/LDGrowthProtocol.h"
#include "Development/LDProgressionRules.h"
#include "Navigation/LDOrderTypes.h"
#include "Region/LDMapGeometry.h"
#include "Vision/LDViewTypes.h"
#include "World/LDCampLifecycle.h"

namespace LDOpponentFrame
{
namespace Detail
{
bool Fail(FString& Error, const FString& Message)
{
    Error = Message;
    return false;
}

bool SourceSlot(ELDCombatEntry Entry, ELDOpponentSourceSlot& Out)
{
    switch (Entry)
    {
    case ELDCombatEntry::Q: Out = ELDOpponentSourceSlot::Q; return true;
    case ELDCombatEntry::W: Out = ELDOpponentSourceSlot::W; return true;
    case ELDCombatEntry::E: Out = ELDOpponentSourceSlot::E; return true;
    case ELDCombatEntry::Dash: Out = ELDOpponentSourceSlot::Dash; return true;
    default: return false;
    }
}
}

bool ReadPublicPOIs(const FLDDemoWorldGeometry& World, const FLDDemoContentSnapshot& Catalog,
    TArray<FLDOpponentPOI>& Out, FString& Error)
{
    using namespace Detail;
    Out.Reset();
    const auto& References = Catalog.Get().World;
    // Camps come from the camp domain's single read-only roster of the validated roots.
    FLDCampRoster Roster;
    if (!LDCamp::BuildRoster(Catalog.Get(), World, Roster, Error)) { return false; }
    for (const auto& Camp : Roster.Camps)
    {
        FLDOpponentPOI POI;
        POI.Id = Camp.CampId;
        POI.Kind = ELDOpponentPOIKind::Camp;
        POI.SurfaceId = Camp.SurfaceId;
        POI.FootCm = Camp.AnchorCm;
        POI.MonsterId = CanonicalId(Camp.MonsterId);
        POI.Tier = Camp.Tier;
        Out.Add(MoveTemp(POI));
    }
    int32 Captures = 0, Dorms = 0, Airdrops = 0;
    for (const auto& Anchor : World.Anchors)
    {
        if (Anchor.Kind == ELDDemoWorldAnchorKind::Camp)
        {
            if (Roster.FindCamp(FName(*Anchor.Id)) == INDEX_NONE)
            {
                Out.Reset();
                return Fail(Error, FString::Printf(TEXT("Camp anchor '%s' is not an approved camp."), *Anchor.Id));
            }
            continue;
        }
        // D26: a Boss patrol point is the Boss's route, not a place a body goes; the visible Boss is a unit.
        if (Anchor.Kind == ELDDemoWorldAnchorKind::BossWaypoint) { continue; }
        // P6 spec §11.3: a locker is a place only while it is out (the public match view).
        if (Anchor.Kind == ELDDemoWorldAnchorKind::Locker) { continue; }
        // Rest-only places are supplied separately and never considered by field decisions.
        if (Anchor.Kind == ELDDemoWorldAnchorKind::PKSpawn || Anchor.Kind == ELDDemoWorldAnchorKind::PKThrow
            || Anchor.Kind == ELDDemoWorldAnchorKind::PKSignup || Anchor.Kind == ELDDemoWorldAnchorKind::RestSpawn
            || Anchor.Kind == ELDDemoWorldAnchorKind::RestService || Anchor.Kind == ELDDemoWorldAnchorKind::TrainingDummy
            || Anchor.Kind == ELDDemoWorldAnchorKind::FinalSpawn || Anchor.Kind == ELDDemoWorldAnchorKind::FinalCapture) { continue; }
        FLDOpponentPOI POI;
        POI.Id = FName(*Anchor.Id);
        POI.SurfaceId = FName(*Anchor.SurfaceId);
        bool bApproved = false;
        switch (Anchor.Kind)
        {
        case ELDDemoWorldAnchorKind::Spawn:
            POI.Kind = ELDOpponentPOIKind::Spawn;
            bApproved = World.bSceneLayout || References.SpawnIds.Contains(Anchor.Id);
            break;
        case ELDDemoWorldAnchorKind::Capture:
            POI.Kind = ELDOpponentPOIKind::Capture;
            bApproved = Anchor.Id.Equals(References.CaptureId, ESearchCase::CaseSensitive);
            ++Captures;
            break;
        case ELDDemoWorldAnchorKind::Dorm:
            // D21: public recovery ground; the world import has already bounded the dorm count.
            POI.Kind = ELDOpponentPOIKind::Dorm;
            bApproved = true;
            ++Dorms;
            break;
        case ELDDemoWorldAnchorKind::Airdrop:
            // D26: a public drop point; whether it holds the current drop is the public match view's business.
            POI.Kind = ELDOpponentPOIKind::Airdrop;
            bApproved = true;
            ++Airdrops;
            break;
        case ELDDemoWorldAnchorKind::MiniFlag:
            // P6 spec §10.3: a public mini flag point; whether it holds the live objective is the public view's business.
            POI.Kind = ELDOpponentPOIKind::MiniFlag;
            bApproved = true;
            break;
        default:
            break;
        }
        if (!bApproved || POI.Id.IsNone() || POI.SurfaceId.IsNone()
            || Out.ContainsByPredicate([&POI](const FLDOpponentPOI& Existing) { return Existing.Id == POI.Id; }))
        {
            Out.Reset();
            return Fail(Error, FString::Printf(TEXT("Public anchor '%s' is not a unique approved place with a registered surface."), *Anchor.Id));
        }
        if (!Anchor.PositionCm.ToVector(POI.FootCm, Error))
        {
            Out.Reset();
            return false;
        }
        Out.Add(MoveTemp(POI));
    }
    if (Captures != 1 || Dorms < 1 || Airdrops < 1)
    {
        Out.Reset();
        return Fail(Error, TEXT("The public world must expose one capture place, and at least one dorm and airdrop point."));
    }
    Error.Reset();
    return true;
}

void ReadGrounds(const FLDWorldView& View, uint8 OwnSeat, TArray<FLDOpponentGround>& Out)
{
    Out.Reset();
    for (const FLDObservedGroundState& Seen : View.GroundStates)
    {
        FLDOpponentGround& Ground = Out.AddDefaulted_GetRef();
        Ground.Kind = Seen.Kind;
        Ground.View = Seen;
        Ground.bEnemy = Seen.OwnerSeat != OwnSeat;
    }
}

void ReadWarnings(const FLDWorldView& View, TArray<FLDOpponentWarning>& Out)
{
    Out.Reset();
    // An enemy warning still pending (ground point, trap, cone, camp telegraph); the AI reads its public outline only.
    for (const FLDObservedProjectileView& Seen : View.Projectiles)
    {
        if (Seen.bOwn || Seen.WarningRemainingMs == 0) { continue; }
        FLDOpponentWarning& Warning = Out.AddDefaulted_GetRef();
        Warning.Handle = Seen.Handle;
        Warning.CentreCm = Seen.PositionCm;
        Warning.Heading = FRotator(0, double(Seen.Heading) * 360. / 65536., 0).Vector();
        Warning.RadiusCm = Seen.RadiusCm;
        Warning.LengthCm = Seen.LengthCm;
        Warning.AngleDegrees = Seen.AngleDegrees;
        // A public cone carries its reach as RadiusCm; the dodge test reads a cone's reach from LengthCm.
        if (Seen.AngleDegrees > 0 && Seen.LengthCm <= 0) { Warning.LengthCm = Seen.RadiusCm; }
        Warning.RemainingMs = Seen.WarningRemainingMs;
    }
}

bool ReadObservedUnits(const FLDWorldView& View, uint32 MatchGeneration, const FLDDemoContentSnapshot& Catalog,
    TArray<FLDOpponentUnit>& Out, FString& Error)
{
    using namespace Detail;
    Out.Reset();
    if (!LDObservation::ValidateView(View, Error)) { return false; }
    if (!MatchGeneration || View.MatchGeneration != MatchGeneration)
    {
        return Fail(Error, TEXT("The observation frame belongs to another match."));
    }
    for (const auto& Unit : View.Units)
    {
        FLDOpponentUnit Mapped;
        Mapped.Handle = Unit.Handle;
        Mapped.Epoch = Unit.ObservationEpoch;
        Mapped.FootCm = Unit.FeetCm;
        Mapped.bPeddler = Unit.bPeddler;
        if (Unit.AuthorizedFieldMask & LDObservation::Health)
        {
            Mapped.HpP2 = Unit.Health.CurrentP2;
            Mapped.MaxHpP2 = Unit.Health.MaxP2;
        }
        if (Unit.AuthorizedFieldMask & LDObservation::Statuses)
        {
            for (const auto& Status : Unit.Statuses)
            {
                Mapped.bReturning |= Status.StatusId == uint8(ELDDemoStatusId::CampReturning);
                Mapped.bProtected |= Status.StatusId == uint8(ELDDemoStatusId::RespawnProtection);
                // Spec §13: the AI reads a visible enemy's element from its public statuses.
                switch (static_cast<ELDDemoStatusId>(Status.StatusId))
                {
                case ELDDemoStatusId::Burn: Mapped.HeldElement = 1; break;
                case ELDDemoStatusId::Wet: Mapped.HeldElement = 2; break;
                case ELDDemoStatusId::Electrified: Mapped.HeldElement = 3; break;
                case ELDDemoStatusId::Chilled: Mapped.HeldElement = 4; break;
                case ELDDemoStatusId::Rotten: Mapped.HeldElement = 5; break;
                default: break;
                }
            }
        }
        if (Unit.Kind == ELDObservedUnitKind::Contender)
        {
            Mapped.Kind = ELDOpponentUnitKind::Contender;
            Mapped.ContenderId = Unit.ContenderId;
        }
        else
        {
            Mapped.Kind = ELDOpponentUnitKind::CampMember;
            const auto* Monster = Catalog.Get().Monsters.FindByPredicate([&Unit](const FLDDemoMonsterDefinition& Definition)
            { return Definition.NativeId == Unit.CreatureId; });
            if (!Monster)
            {
                Out.Reset();
                return Fail(Error, TEXT("An observed camp member references an unknown public creature."));
            }
            Mapped.MonsterId = Monster->Id;
        }
        Out.Add(MoveTemp(Mapped));
    }
    Error.Reset();
    return true;
}

bool ReadOwnBuild(const FLDProgressionView& Progression, const TArray<FLDDemoStatBonus>& CardGrowth,
    const FLDDemoContentSnapshot& Catalog, FLDOpponentSelf& Out, FString& Error)
{
    using namespace Detail;
    if (!Progression.bValid || Progression.Level == 0)
    {
        return Fail(Error, TEXT("The own progression view must be a valid publication of this match."));
    }
    Out.Actives.Reset();
    Out.SkillPoints = Progression.SkillPoints;
    Out.Blueprints.Reset();
    Out.StatPoints.Reset();
    Out.Offer.Reset();
    Out.Cards.Reset();
    Out.CardOffer.Reset();
    Out.CardCapacity = Progression.CardCapacity;
    for (const FLDActiveSlotView& Active : Progression.Actives)
    {
        if (Active.Slot > 2 || Active.DefinitionId.IsNone()) { return Fail(Error, TEXT("An own active is outside the Q/W/E slots.")); }
        Out.Actives.Add({CanonicalId(Active.DefinitionId), Active.Slot, Active.Branch});
    }
    for (const FName& Blueprint : Progression.PendingBlueprints) { Out.Blueprints.Add(CanonicalId(Blueprint)); }
    TArray<FLDStatPointsValue> Points;
    for (const FLDStatPointsView& Stat : Progression.StatPoints)
    {
        Out.StatPoints.Add({CanonicalId(Stat.StatId), Stat.Points});
        if (Stat.Points > 0) { Points.Emplace(CanonicalId(Stat.StatId), Stat.Points); }
    }
    if (Progression.bHasActiveOffer)
    {
        FLDOpponentOffer Offer;
        Offer.OfferId = Progression.ActiveOffer.OfferId;
        Offer.OfferRevision = Progression.ActiveOffer.OfferRevision;
        Offer.Ordinal = Progression.ActiveOffer.OpportunityOrdinal;
        for (const FName& Option : Progression.ActiveOffer.Options) { Offer.Options.Add(CanonicalId(Option)); }
        if (Offer.Options.IsEmpty()) { return Fail(Error, TEXT("The own active offer has no option.")); }
        Out.Offer = MoveTemp(Offer);
    }
    if (Progression.bFinalModPending)
    {
        Out.FinalModOfferId = Progression.FinalModOfferId;
        for (const FName& Option : Progression.FinalModOptions) { Out.FinalModOptions.Add(CanonicalId(Option)); }
    }
    // Owned cards; the installed ones also feed the own attribute projection.
    TArray<FLDInstalledCardValue> Installed;
    for (const FLDCardInstanceView& Owned : Progression.Cards)
    {
        if (Owned.DefinitionId.IsNone() || Owned.Serial == 0) { return Fail(Error, TEXT("An own card has no definition or serial.")); }
        FLDOpponentCard& Card = Out.Cards.AddDefaulted_GetRef();
        Card.Serial = Owned.Serial;
        Card.DefinitionId = CanonicalId(Owned.DefinitionId);
        if (!Owned.IsInstalled()) { continue; }
        Card.TargetKind = Owned.Target.Kind;
        Card.TargetActive = Owned.Target.Active.IsNone() ? FString() : CanonicalId(Owned.Target.Active);
        Card.Element = Owned.Target.Element;
        FLDInstalledCardValue& Value = Installed.AddDefaulted_GetRef();
        Value.CardId = Owned.DefinitionId;
        Value.Serial = Owned.Serial;
        Value.TargetKind = Owned.Target.Kind;
        Value.Active = Owned.Target.Active;
        Value.Element = Owned.Target.Element;
    }
    if (Progression.bHasCardOffer)
    {
        FLDOpponentCardOffer Offer;
        Offer.OfferId = Progression.CardOffer.OfferId;
        Offer.OfferRevision = Progression.CardOffer.OfferRevision;
        for (const FName& Option : Progression.CardOffer.Options) { Offer.Options.Add(CanonicalId(Option)); }
        if (Offer.Options.IsEmpty()) { return Fail(Error, TEXT("The own card offer has no option.")); }
        Out.CardOffer = MoveTemp(Offer);
    }
    FLDAttributeBuildValues Values(Progression.Level, MoveTemp(Points));
    Values.Cards = MoveTemp(Installed);
    Values.CardGrowth = CardGrowth;
    const auto Projection = LDAttributeProjection::Project(Values, Catalog);
    if (!Projection.IsSuccess()) { return Fail(Error, Projection.Error.Message); }
    Out.Attributes = Projection.Value->Final;
    Out.BuildRevision = Progression.BuildRevision;
    Out.Level = Progression.Level;
    Out.UnspentPoints = Progression.UnspentPoints;
    Out.OffersConsumed = Progression.ActiveOpportunitiesConsumed;
    Out.QueuedOpportunities = Progression.QueuedActiveOpportunities;
    Out.Shop = Progression.Shop;
    Error.Reset();
    return true;
}

bool ReadOwnCombat(const FLDCombatOwnerView& Combat, int64 NowMs, FLDOpponentSelf& Out, FString& Error)
{
    using namespace Detail;
    if (!Combat.bHasBody || Combat.MaxHPP2 <= 0 || Combat.HPP2 < 0 || Combat.HPP2 > Combat.MaxHPP2
        || Combat.MomentumP2 < 0 || Combat.MaxMomentumP2 < 0 || Combat.MomentumP2 > Combat.MaxMomentumP2)
    {
        return Fail(Error, TEXT("The own combat view has no consistent living body."));
    }
    Out.HpP2 = Combat.HPP2;
    Out.MaxHpP2 = Combat.MaxHPP2;
    Out.MomentumP2 = Combat.MomentumP2;
    Out.MaxMomentumP2 = Combat.MaxMomentumP2;
    Out.BasicReadyAtMs = Combat.NextAttackAtMs;
    Out.MoveBlockedUntilMs = 0;
    Out.bChanneling = Combat.Cues.bActing && !Combat.Cues.bActingBasic && Combat.ActionPhase == ELDCombatPhase::Finished;
    int64 SilencedUntilMs = 0;
    for (const auto& Status : Combat.Statuses)
    {
        // Root and Stun both hold the body in place; the policy plans no movement until they end.
        if (Status.StatusId == uint8(ELDDemoStatusId::Root) || Status.StatusId == uint8(ELDDemoStatusId::Stun))
        { Out.MoveBlockedUntilMs = FMath::Max(Out.MoveBlockedUntilMs, Status.ExpiresAtMs); }
        // #147: a silence keeps the learned Q/W unready until it ends; the dash stays available.
        if (Status.StatusId == uint8(ELDDemoStatusId::Silence)) { SilencedUntilMs = FMath::Max(SilencedUntilMs, Status.ExpiresAtMs); }
    }
    // Rooted without a timed status still blocks movement now; the policy never assumes it has ended.
    if (Combat.bRooted && Out.MoveBlockedUntilMs <= NowMs) { Out.MoveBlockedUntilMs = NowMs + 1; }
    Out.Sources.Reset();
    for (const auto& Entry : Combat.Entries)
    {
        ELDOpponentSourceSlot Slot;
        if (!SourceSlot(Entry.Entry, Slot) || Entry.DefinitionId.IsNone()) { continue; }
        const bool bActiveSource = Entry.SourceKind == ELDCombatSourceKind::Learned || Entry.SourceKind == ELDCombatSourceKind::Innate;
        if (!bActiveSource) { continue; }
        FLDOpponentSource Source;
        Source.Slot = Slot;
        Source.DefinitionId = CanonicalId(Entry.DefinitionId);
        Source.bReady = Entry.Unavailable == ELDCombatReject::None && Entry.ReadyAtMs <= NowMs
            && !(Entry.SourceKind == ELDCombatSourceKind::Learned && SilencedUntilMs > NowMs);
        Source.RangeCm = Entry.RangeCm;
        Out.Sources.Add(MoveTemp(Source));
    }
    Error.Reset();
    return true;
}

ELDOpponentOrderStatus OrderStatus(ELDOrderStatus Status)
{
    switch (Status)
    {
    case ELDOrderStatus::Queued: return ELDOpponentOrderStatus::Pending;
    case ELDOrderStatus::Activated:
    case ELDOrderStatus::Progress: return ELDOpponentOrderStatus::Active;
    case ELDOrderStatus::Completed: return ELDOpponentOrderStatus::Completed;
    case ELDOrderStatus::Cancelled: return ELDOpponentOrderStatus::Cancelled;
    default: return ELDOpponentOrderStatus::Failed;
    }
}

FString CanonicalId(FName Id)
{
    return Id.IsNone() ? FString() : Id.ToString().ToLower();
}
}
