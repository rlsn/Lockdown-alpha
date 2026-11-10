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

bool BuildCommand(const FLDOpponentBuildIntent& Intent, const FLDProgressionView& Progression, uint64 RequestId,
    FLDBuildCommand& Out, FString& Error)
{
    using namespace Detail;
    Out = FLDBuildCommand();
    if (Intent.Kind == ELDOpponentBuildKind::None || RequestId == 0)
    {
        return Fail(Error, TEXT("Only a build intent with a request id becomes a build command."));
    }
    if (!Progression.bValid)
    {
        return Fail(Error, TEXT("The own progression view is not valid."));
    }
    switch (Intent.Kind)
    {
    case ELDOpponentBuildKind::ChooseActive:
    {
        const FLDActiveOfferView& Offer = Progression.ActiveOffer;
        const FName Option(*Intent.Option);
        if (!Progression.bHasActiveOffer || Offer.OfferId != Intent.OfferId || Offer.OfferRevision != Intent.OfferRevision)
        {
            return Fail(Error, TEXT("The chosen offer is no longer the current active offer."));
        }
        if (!Offer.Options.Contains(Option) || !FLDDefinitionWire::Encode(Option, Out.Option))
        {
            Out = FLDBuildCommand();
            return Fail(Error, TEXT("The chosen active is not an option of the current offer."));
        }
        Out.Kind = ELDBuildCommandKind::ActiveChoice;
        Out.OfferId = Intent.OfferId;
        Out.OfferRevision = Intent.OfferRevision;
        // Like a player's click: learn it now when the growth domain allows it, otherwise it is kept as a blueprint.
        Out.bLearnIfPossible = true;
        Out.Slot = FLDBuildCommand::AnySlot;
        break;
    }
    case ELDOpponentBuildKind::LearnBlueprint:
    {
        const FName Blueprint(*Intent.Blueprint);
        if (Intent.Slot > 2 || !Progression.PendingBlueprints.Contains(Blueprint)
            || Progression.Actives.ContainsByPredicate([&Intent](const FLDActiveSlotView& Active) { return Active.Slot == Intent.Slot; })
            || !FLDDefinitionWire::Encode(Blueprint, Out.Blueprint))
        {
            Out = FLDBuildCommand();
            return Fail(Error, TEXT("The blueprint is no longer pending or its slot is no longer empty."));
        }
        Out.Kind = ELDBuildCommandKind::LearnBlueprint;
        Out.Slot = Intent.Slot;
        break;
    }
    case ELDOpponentBuildKind::AllocatePoints:
    {
        int64 Total = 0;
        for (const uint8 Count : Intent.PointCounts) { Total += Count; }
        if (Intent.PointCounts.Num() != Progression.StatPoints.Num() || Total <= 0 || Total > Progression.UnspentPoints)
        {
            return Fail(Error, TEXT("The allocation does not fit the own stats or unspent points."));
        }
        Out.Kind = ELDBuildCommandKind::AllocatePoints;
        Out.PointCounts = Intent.PointCounts;
        break;
    }
    case ELDOpponentBuildKind::OpeningBase:
    case ELDOpponentBuildKind::InstallComponent:
    {
        const FName Component(*Intent.Component);
        if (Intent.Kind == ELDOpponentBuildKind::OpeningBase)
        {
            if (!Progression.bOpeningBasePending || !Progression.OpeningBaseOptions.Contains(Component))
            {
                return Fail(Error, TEXT("The component is no longer an option of the open opening base pick."));
            }
            Out.Kind = ELDBuildCommandKind::OpeningBase;
            // Like a player's pick: enable it now when allowed, otherwise it is kept in the inventory.
            Out.bInstallIfPossible = true;
        }
        else
        {
            const FLDComponentInstanceView* Owned = Progression.Components.FindByPredicate(
                [&Component](const FLDComponentInstanceView& View) { return View.DefinitionId == Component; });
            if (!Owned || Owned->bInstalled || Owned->bEverInstalled)
            {
                return Fail(Error, TEXT("The component is no longer owned and waiting for its first install."));
            }
            Out.Kind = ELDBuildCommandKind::InstallComponent;
        }
        Out.TargetKind = ELDComponentTargetKind::None;
        if (!FLDDefinitionWire::Encode(Component, Out.Component))
        {
            Out = FLDBuildCommand();
            return Fail(Error, TEXT("The component id cannot travel."));
        }
        if (!Intent.BoundActive.IsEmpty())
        {
            const FName Bound(*Intent.BoundActive);
            if (!Progression.Actives.ContainsByPredicate([&Bound](const FLDActiveSlotView& Active) { return Active.DefinitionId == Bound; })
                || Intent.Element == ELDDemoElement::None || !FLDDefinitionWire::Encode(Bound, Out.BoundActive))
            {
                Out = FLDBuildCommand();
                return Fail(Error, TEXT("The bound active is no longer learned or the binding has no element."));
            }
            Out.Element = Intent.Element;
        }
        else if (Intent.Element != ELDDemoElement::None)
        {
            Out = FLDBuildCommand();
            return Fail(Error, TEXT("An element without a bound active is no binding."));
        }
        break;
    }
    default:
        return Fail(Error, TEXT("The build intent has an unknown kind."));
    }
    Out.Generation = Progression.Generation;
    Out.Life = Progression.Life;
    Out.ExpectedLifePhase = Progression.LifePhase;
    Out.RequestId = RequestId;
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
    case ELDGrowthReason::OfferAlreadyCommitted:
    // #198: the window, the phase or the offer moved on between the read and the commit; the next decision rereads them.
    case ELDGrowthReason::NotSafe:
    case ELDGrowthReason::PhaseClosed:
    case ELDGrowthReason::NoActiveOffer: return ELDOpponentSettle::Dropped;
    // Stale guards, offers, slots, points and authority faults never loop unbounded.
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
        if (Result.Family == ELDGrowthFamily::Build && Result.bHasBuildReceipt) { return SettleHeader(Result.BuildReceipt.Header); }
        return ELDOpponentSettle::Failed;
    case ELDAdmissionStatus::AdmissionRejected: return SettleGrowthRejection(LDGrowthWire::Reason(Result.Reason));
    default: return ELDOpponentSettle::Failed;
    }
}

ELDOpponentSettle SettleBuildQuery(const FLDBuildReceiptQueryResult& Result)
{
    return Detail::SettleLookup(Result.Lookup, Result.bHasReceipt, Result.Receipt.Header);
}
}
