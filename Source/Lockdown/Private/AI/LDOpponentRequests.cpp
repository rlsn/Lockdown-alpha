#include "AI/LDOpponentRequests.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDGrowthProtocol.h"
#include "Development/LDDemoContentIds.h"
#include "Navigation/LDOrderTypes.h"

namespace LDOpponentRequests
{
namespace Detail
{
bool Fail(FString& Error, const TCHAR* Message)
{
    Error = Message;
    return false;
}

const FLDOpponentPOI* FindPlace(const TArray<FLDOpponentPOI>& POIs, FName Id)
{
    return POIs.FindByPredicate([Id](const FLDOpponentPOI& POI) { return POI.Id == Id; });
}

ELDCombatEntry EntryOf(ELDOpponentSourceSlot Source)
{
    switch (Source)
    {
    case ELDOpponentSourceSlot::Q: return ELDCombatEntry::Q;
    case ELDOpponentSourceSlot::W: return ELDCombatEntry::W;
    case ELDOpponentSourceSlot::E: return ELDCombatEntry::E;
    case ELDOpponentSourceSlot::Dash: return ELDCombatEntry::Dash;
    default: return ELDCombatEntry::None;
    }
}

ELDOpponentSettle SettleHeader(const FLDGrowthReceiptHeader& Header)
{
    switch (Header.Outcome)
    {
    case ELDReceiptOutcome::Applied: return ELDOpponentSettle::Succeeded;
    case ELDReceiptOutcome::Rejected: return SettleGrowthRejection(LDGrowthWire::Reason(Header.Reason));
    default: return ELDOpponentSettle::Failed;
    }
}

ELDOpponentSettle SettleLookup(ELDReceiptLookup Lookup, bool bHasReceipt, const FLDGrowthReceiptHeader& Header)
{
    switch (Lookup)
    {
    case ELDReceiptLookup::Pending: return ELDOpponentSettle::InFlight;
    case ELDReceiptLookup::Final: return bHasReceipt ? SettleHeader(Header) : ELDOpponentSettle::Failed;
    // The record is gone or belongs to another match: re-read the current projection instead of guessing.
    case ELDReceiptLookup::NotFound:
    case ELDReceiptLookup::ReceiptExpired:
    case ELDReceiptLookup::InvalidContext: return ELDOpponentSettle::Dropped;
    default: return ELDOpponentSettle::Failed;
    }
}
}

bool ToOutcome(ELDOpponentSettle Settle, ELDOpponentOutcome& Out)
{
    switch (Settle)
    {
    case ELDOpponentSettle::Succeeded: Out = ELDOpponentOutcome::Succeeded; return true;
    case ELDOpponentSettle::Failed: Out = ELDOpponentOutcome::Failed; return true;
    case ELDOpponentSettle::TargetLost: Out = ELDOpponentOutcome::TargetLost; return true;
    default: return false;
    }
}

bool NextMoveIdentity(const FLDMoveIdentity& Last, uint32 Generation, uint32 Life, bool bStop, FLDMoveIdentity& Out)
{
    if (Generation == 0 || Life == 0) { return false; }
    const bool bSameLife = Last.Match == Generation && Last.Life == Life;
    const uint32 PriorOrder = bSameLife ? Last.Order : 0;
    const uint32 PriorEpoch = bSameLife ? Last.Epoch : 0;
    if (PriorOrder == MAX_uint32 || (bStop && PriorEpoch == MAX_uint32)) { return false; }
    Out.Match = Generation;
    Out.Life = Life;
    Out.Order = PriorOrder + 1;
    Out.Epoch = bStop ? PriorEpoch + 1 : PriorEpoch;
    return true;
}

bool BuildOrder(const FLDOpponentBodyIntent& Intent, const TArray<FLDOpponentPOI>& POIs, const FLDMoveIdentity& Identity,
    FLDSpatialOrderRequest& Out, FString& Error)
{
    using namespace Detail;
    Out = FLDSpatialOrderRequest();
    Out.Identity = Identity;
    switch (Intent.Kind)
    {
    case ELDOpponentBodyKind::MoveGround:
    {
        const FLDOpponentPOI* Place = FindPlace(POIs, Intent.POI);
        if (!Place || Place->SurfaceId.IsNone())
        {
            return Fail(Error, TEXT("MoveGround needs a registered public place and its surface."));
        }
        Out.Kind = ELDSpatialOrderKind::MoveGround;
        Out.GroundCm = Place->FootCm;
        Out.SurfaceHint = Place->SurfaceId;
        break;
    }
    case ELDOpponentBodyKind::AttackUnit:
        if (!Intent.Handle.IsValid() || Intent.Epoch == 0)
        {
            return Fail(Error, TEXT("AttackUnit needs a current observation handle and its epoch."));
        }
        Out.Kind = ELDSpatialOrderKind::AttackUnit;
        Out.Observed = Intent.Handle;
        Out.ObservationEpoch = Intent.Epoch;
        break;
    default:
        return Fail(Error, TEXT("Only MoveGround and AttackUnit intents become spatial orders."));
    }
    if (!Out.HasValidShape()) { return Fail(Error, TEXT("The spatial order has no valid shape for its identity.")); }
    return true;
}

bool BuildAction(const FLDOpponentBodyIntent& Intent, uint32 Generation, uint32 Life, uint64 DirectSequence,
    uint64 ExpectedSourceRevision, FLDCombatActionRequest& Out, FString& Error)
{
    using namespace Detail;
    Out = FLDCombatActionRequest();
    const ELDCombatEntry Entry = EntryOf(Intent.Source);
    if (Intent.Kind != ELDOpponentBodyKind::Activate || Entry == ELDCombatEntry::None || Generation == 0 || Life == 0
        || DirectSequence == 0)
    {
        return Fail(Error, TEXT("An activation needs a source slot, the current match and life, and a direct sequence."));
    }
    switch (Intent.TargetMode)
    {
    case ELDDemoTargetMode::Self:
        Out.Target.Kind = ELDCombatTargetKind::Self;
        break;
    case ELDDemoTargetMode::Unit:
        if (!Intent.Handle.IsValid() || Intent.Epoch == 0)
        {
            return Fail(Error, TEXT("A unit activation needs a current observation handle and its epoch."));
        }
        Out.Target.Kind = ELDCombatTargetKind::Unit;
        Out.Target.Observed = Intent.Handle;
        Out.Target.ObservationEpoch = Intent.Epoch;
        break;
    case ELDDemoTargetMode::DirectionProjectile:
    case ELDDemoTargetMode::DirectionCone:
    case ELDDemoTargetMode::DirectionDash:
    {
        const FVector Planar = FVector(Intent.Direction.X, Intent.Direction.Y, 0).GetSafeNormal();
        if (Planar.IsNearlyZero()) { return Fail(Error, TEXT("A directional activation needs a planar direction.")); }
        Out.Target.Kind = ELDCombatTargetKind::Direction;
        Out.Target.Direction = Planar;
        break;
    }
    case ELDDemoTargetMode::GroundPoint:
        if (Intent.Point.ContainsNaN())
        {
            return Fail(Error, TEXT("A ground activation needs a finite requested point."));
        }
        Out.Target.Kind = ELDCombatTargetKind::Point;
        Out.Target.Point = Intent.Point;
        break;
    default:
        return Fail(Error, TEXT("The activation has an unknown target mode."));
    }
    Out.Generation = Generation;
    Out.Life = Life;
    Out.DirectSequence = DirectSequence;
    Out.Entry = Entry;
    Out.ExpectedSourceRevision = ExpectedSourceRevision;
    return true;
}

bool BuildChoice(const FLDOpponentBuildIntent& Intent, const FLDProgressionView& Progression, uint64 RequestId,
    FLDChoiceCommand& Out, FString& Error)
{
    using namespace Detail;
    Out = FLDChoiceCommand();
    if (Intent.Kind != ELDOpponentBuildKind::Choose || RequestId == 0)
    {
        return Fail(Error, TEXT("Only a Choose intent with a request id becomes a choice command."));
    }
    const FLDChoiceOfferView& Offer = Progression.Offer;
    if (!Progression.bValid || !Progression.bCanChoose || !Progression.bHasOffer || Offer.OfferId != Intent.OfferId
        || Offer.OfferRevision != Intent.OfferRevision)
    {
        return Fail(Error, TEXT("The chosen offer is no longer the current head offer."));
    }
    if (Intent.Choice.bReroll)
    {
        if (Offer.bRerolled || Progression.RerollTokens == 0)
        {
            return Fail(Error, TEXT("The head offer cannot be rerolled now."));
        }
        Out.Generation = Progression.Generation;
        Out.Life = Progression.Life;
        Out.ExpectedLifePhase = Progression.LifePhase;
        Out.RequestId = RequestId;
        Out.OfferId = Intent.OfferId;
        Out.OfferRevision = Intent.OfferRevision;
        Out.ExpectedBuildRevision = Progression.BuildRevision;
        Out.CatalogDigest = Progression.CatalogDigest;
        Out.bReroll = true;
        return true;
    }
    const FGuid OptionId = Intent.Choice.OptionId;
    if (!Offer.Options.ContainsByPredicate([OptionId](const FLDChoiceOption& Option) { return Option.OptionId == OptionId; }))
    {
        return Fail(Error, TEXT("The chosen option is not part of the head offer."));
    }
    if (Intent.Choice.bReplace)
    {
        const FLDOpponentChoicePick& Pick = Intent.Choice;
        if (Pick.ReplaceSlot == ELDOpponentSkillSlot::Passive)
        {
            return Fail(Error, TEXT("Only an active slot can be replaced."));
        }
        const uint8 ActiveSlot = Pick.ReplaceSlot == ELDOpponentSkillSlot::Q ? 0
            : Pick.ReplaceSlot == ELDOpponentSkillSlot::W ? 1 : 2;
        const FName Victim(*Pick.ReplacedSkillId);
        const bool bHeld = Progression.Skills.ContainsByPredicate([&Pick, ActiveSlot, Victim](const FLDLearnedSkillView& Skill)
        {
            return Skill.bActive && Skill.Slot == ActiveSlot && Skill.DefinitionId == Victim && Skill.Rank == Pick.ReplacedRank;
        });
        if (!bHeld || !FLDDefinitionWire::Encode(Victim, Out.ExpectedOldSkillId))
        {
            Out = FLDChoiceCommand();
            return Fail(Error, TEXT("The replaced active is not the one currently held in that slot."));
        }
        Out.bReplace = true;
        Out.ReplaceSlot = ActiveSlot;
        Out.ExpectedOldRank = Pick.ReplacedRank;
        // The policy's explicit pick is the confirmation; nothing replaces a skill by default.
        Out.bReplaceConfirmed = true;
    }
    Out.Generation = Progression.Generation;
    Out.Life = Progression.Life;
    Out.ExpectedLifePhase = Progression.LifePhase;
    Out.RequestId = RequestId;
    Out.OfferId = Intent.OfferId;
    Out.OfferRevision = Intent.OfferRevision;
    Out.OptionId = OptionId;
    Out.ExpectedBuildRevision = Progression.BuildRevision;
    Out.CatalogDigest = Progression.CatalogDigest;
    return true;
}

bool BodyIntentsHeld(const FLDCombatOwnerView& Combat, int64 NowMs)
{
    return Combat.bHasBody && Combat.Statuses.ContainsByPredicate([NowMs](const FLDCombatStatusView& Status)
    { return Status.StatusId == uint8(ELDDemoStatusId::Taunt) && NowMs < Status.ExpiresAtMs; });
}

ELDOpponentSettle SettleOrder(const FLDOrderReceipt& Receipt)
{
    switch (Receipt.Status)
    {
    case ELDOrderStatus::Queued:
    case ELDOrderStatus::Activated:
    case ELDOrderStatus::Progress: return ELDOpponentSettle::InFlight;
    default: break;
    }
    // An attack order also ends Completed when its target died: either way the observed handle is gone for good.
    if (Receipt.Failure == ELDOrderFailure::TargetUnavailable || Receipt.Failure == ELDOrderFailure::TargetDied)
    {
        return ELDOpponentSettle::TargetLost;
    }
    if (Receipt.Status == ELDOrderStatus::Completed) { return ELDOpponentSettle::Succeeded; }
    switch (Receipt.Failure)
    {
    case ELDOrderFailure::None:
    case ELDOrderFailure::StaleMatch:
    case ELDOrderFailure::StaleLife:
    case ELDOrderFailure::Dead:
    case ELDOrderFailure::MatchEnded:
    // #153: the opening countdown refused it; the next decision after T=0 starts fresh.
    case ELDOrderFailure::NotStarted:
    case ELDOrderFailure::Superseded:
    case ELDOrderFailure::Stopped:
    case ELDOrderFailure::Expired:
    // An enemy's taunt interrupted the order: nothing about the intent itself failed.
    case ELDOrderFailure::Taunted:
    case ELDOrderFailure::KnockedBack: return ELDOpponentSettle::Dropped;
    // Unusable ground, no complete path, blocked progress and malformed or stale requests.
    default: return ELDOpponentSettle::Failed;
    }
}

ELDOpponentSettle SettleAction(const FLDCombatActionReceipt& Receipt)
{
    switch (Receipt.Phase)
    {
    case ELDCombatPhase::Queued:
    case ELDCombatPhase::Accepted:
    case ELDCombatPhase::Released: return ELDOpponentSettle::InFlight;
    case ELDCombatPhase::Finished: return ELDOpponentSettle::Succeeded;
    default: break;
    }
    switch (Receipt.Reject)
    {
    case ELDCombatReject::TargetUnavailable: return ELDOpponentSettle::TargetLost;
    case ELDCombatReject::Cancelled:
    case ELDCombatReject::Dead:
    case ELDCombatReject::StaleLife:
    case ELDCombatReject::StaleMatch:
    case ELDCombatReject::MatchClosed:
    case ELDCombatReject::NotStarted:
    case ELDCombatReject::ReceiptExpired: return ELDOpponentSettle::Dropped;
    // Readiness, mana, control, range, a changed source and malformed requests back off and stay bounded.
    default: return ELDOpponentSettle::Failed;
    }
}

ELDOpponentSettle SettleGrowthRejection(ELDGrowthReason Reason)
{
    switch (Reason)
    {
    case ELDGrowthReason::OldMatch:
    case ELDGrowthReason::OldLife:
    case ELDGrowthReason::LifePhaseChanged:
    case ELDGrowthReason::LeftMatch:
    case ELDGrowthReason::MatchClosed:
    case ELDGrowthReason::Dead:
    case ELDGrowthReason::StaleContext:
    case ELDGrowthReason::OfferAlreadyCommitted: return ELDOpponentSettle::Dropped;
    // Stale guards, offers, replacements and authority faults never loop unbounded.
    default: return ELDOpponentSettle::Failed;
    }
}

ELDOpponentSettle SettleSubmit(const FLDSubmitResult& Result)
{
    using namespace Detail;
    switch (Result.Status)
    {
    case ELDAdmissionStatus::AcceptedPending: return ELDOpponentSettle::InFlight;
    case ELDAdmissionStatus::Final:
        if (Result.Family == ELDGrowthFamily::Choice && Result.bHasChoiceReceipt) { return SettleHeader(Result.ChoiceReceipt.Header); }
        return ELDOpponentSettle::Failed;
    case ELDAdmissionStatus::AdmissionRejected: return SettleGrowthRejection(LDGrowthWire::Reason(Result.Reason));
    default: return ELDOpponentSettle::Failed;
    }
}

ELDOpponentSettle SettleChoiceQuery(const FLDChoiceReceiptQueryResult& Result)
{
    return Detail::SettleLookup(Result.Lookup, Result.bHasReceipt, Result.Receipt.Header);
}
}
