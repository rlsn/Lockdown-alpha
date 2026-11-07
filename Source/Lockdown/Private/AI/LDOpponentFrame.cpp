#include "AI/LDOpponentFrame.h"
#include "Combat/LDAttributeProjection.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
#include "Development/LDGrowthProtocol.h"
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

bool OptionKind(ELDChoiceOptionKind Kind, ELDOpponentOptionKind& Out)
{
    switch (Kind)
    {
    case ELDChoiceOptionKind::LearnActive: Out = ELDOpponentOptionKind::LearnActive; return true;
    case ELDChoiceOptionKind::UpgradeActive: Out = ELDOpponentOptionKind::UpgradeActive; return true;
    case ELDChoiceOptionKind::LearnPassive: Out = ELDOpponentOptionKind::LearnPassive; return true;
    case ELDChoiceOptionKind::UpgradePassive: Out = ELDOpponentOptionKind::UpgradePassive; return true;
    case ELDChoiceOptionKind::Attribute: Out = ELDOpponentOptionKind::Attribute; return true;
    case ELDChoiceOptionKind::Opening: Out = ELDOpponentOptionKind::Opening; return true;
    default: return false;
    }
}

bool SourceSlot(ELDCombatEntry Entry, ELDOpponentSourceSlot& Out)
{
    switch (Entry)
    {
    case ELDCombatEntry::Q: Out = ELDOpponentSourceSlot::Q; return true;
    case ELDCombatEntry::W: Out = ELDOpponentSourceSlot::W; return true;
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
    int32 Shops = 0, Captures = 0, Dorms = 0, Airdrops = 0;
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
        FLDOpponentPOI POI;
        POI.Id = FName(*Anchor.Id);
        POI.SurfaceId = FName(*Anchor.SurfaceId);
        bool bApproved = false;
        switch (Anchor.Kind)
        {
        case ELDDemoWorldAnchorKind::Spawn:
            POI.Kind = ELDOpponentPOIKind::Spawn;
            bApproved = References.SpawnIds.Contains(Anchor.Id);
            break;
        case ELDDemoWorldAnchorKind::Shop:
            POI.Kind = ELDOpponentPOIKind::Shop;
            bApproved = References.ShopIds.Contains(Anchor.Id);
            ++Shops;
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
    if (Shops != References.ShopIds.Num() || Captures != 1 || Dorms < 1 || Airdrops < 1)
    {
        Out.Reset();
        return Fail(Error, TEXT("The public world must expose every approved shop, one capture place, and at least one dorm and airdrop point."));
    }
    Error.Reset();
    return true;
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

bool ReadOwnBuild(const FLDProgressionView& Progression, const FLDEquipmentView& Equipment,
    const FLDDemoContentSnapshot& Catalog, FLDOpponentSelf& Out, FString& Error)
{
    using namespace Detail;
    if (!Progression.bValid || !Equipment.bValid || Progression.Generation != Equipment.Generation
        || Equipment.Slots.Num() != 2 || Progression.Level == 0)
    {
        return Fail(Error, TEXT("Own progression and equipment views must be one valid publication of this match."));
    }
    Out.Skills.Reset();
    Out.Offer.Reset();
    TArray<FLDLearnedPassiveValue> Passives;
    for (const auto& Skill : Progression.Skills)
    {
        FLDOpponentSkill Mapped;
        Mapped.DefinitionId = CanonicalId(Skill.DefinitionId);
        Mapped.Rank = Skill.Rank;
        if (Skill.bActive)
        {
            if (Skill.Slot > 1) { return Fail(Error, TEXT("An own active skill is outside the Q/W slots.")); }
            Mapped.Slot = Skill.Slot == 0 ? ELDOpponentSkillSlot::Q : ELDOpponentSkillSlot::W;
        }
        else
        {
            Mapped.Slot = ELDOpponentSkillSlot::Passive;
            Passives.Emplace(Mapped.DefinitionId, Skill.Rank);
        }
        Out.Skills.Add(MoveTemp(Mapped));
    }
    TArray<FLDAttributeCardValue> Cards;
    for (const auto& Card : Progression.AttributeCards) { Cards.Emplace(CanonicalId(Card.DefinitionId), Card.Count); }
    if (Progression.bHasOffer && Progression.bCanChoose)
    {
        FLDOpponentOffer Offer;
        Offer.OfferId = Progression.Offer.OfferId;
        Offer.OfferRevision = Progression.Offer.OfferRevision;
        Offer.bRerolled = Progression.Offer.bRerolled;
        for (const auto& Option : Progression.Offer.Options)
        {
            FLDOpponentOption Mapped;
            Mapped.OptionId = Option.OptionId;
            if (!OptionKind(Option.Kind, Mapped.Kind)) { return Fail(Error, TEXT("The own offer has an unknown option kind.")); }
            Mapped.DefinitionId = CanonicalId(Option.DefinitionId);
            Mapped.FromRank = Option.FromRank;
            Mapped.ToRank = Option.ToRank;
            Offer.Options.Add(MoveTemp(Mapped));
        }
        Out.Offer = MoveTemp(Offer);
    }
    TArray<FString> Equipped;
    TArray<bool> Upgraded;
    for (int32 Slot = 0; Slot < 2; ++Slot)
    {
        const auto& View = Equipment.Slots[Slot];
        if (View.SlotIndex != Slot || (View.bOccupied && View.DefinitionId.IsNone()))
        {
            return Fail(Error, TEXT("Own equipment slots must be the two ordered positions."));
        }
        Out.Equipment[Slot] = View.bOccupied ? CanonicalId(View.DefinitionId) : FString();
        Out.EquipmentUpgraded[Slot] = View.bOccupied && View.bUpgraded;
        if (View.bOccupied)
        {
            Equipped.Add(Out.Equipment[Slot]);
            Upgraded.Add(View.bUpgraded);
        }
    }
    Out.bUpgradeUsed = Equipment.bUpgradeUsed;
    Out.RerollTokens = Progression.RerollTokens;
    FLDAttributeBuildValues Values(Progression.Level, MoveTemp(Passives), MoveTemp(Cards), MoveTemp(Equipped));
    Values.EquippedUpgraded = MoveTemp(Upgraded);
    Values.OpeningCard = CanonicalId(Progression.OpeningCard);
    const auto Projection = LDAttributeProjection::Project(Values, Catalog);
    if (!Projection.IsSuccess()) { return Fail(Error, Projection.Error.Message); }
    Out.Attributes = Projection.Value->Final;
    Out.Gold = Equipment.Gold;
    Out.BuildRevision = Progression.BuildRevision;
    Out.Level = Progression.Level;
    Out.ChoicesConsumed = Progression.ConsumedThrough;
    Error.Reset();
    return true;
}

bool ReadOwnCombat(const FLDCombatOwnerView& Combat, int64 NowMs, FLDOpponentSelf& Out, FString& Error)
{
    using namespace Detail;
    if (!Combat.bHasBody || Combat.MaxHPP2 <= 0 || Combat.HPP2 < 0 || Combat.HPP2 > Combat.MaxHPP2
        || Combat.MPP2 < 0 || Combat.MaxMPP2 < 0 || Combat.MPP2 > Combat.MaxMPP2)
    {
        return Fail(Error, TEXT("The own combat view has no consistent living body."));
    }
    Out.HpP2 = Combat.HPP2;
    Out.MaxHpP2 = Combat.MaxHPP2;
    Out.MpP2 = Combat.MPP2;
    Out.MaxMpP2 = Combat.MaxMPP2;
    Out.BasicReadyAtMs = Combat.NextAttackAtMs;
    Out.MoveBlockedUntilMs = 0;
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
