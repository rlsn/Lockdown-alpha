#include "Framework/LDPlayerController.h"
#include "Core/LDLog.h"
#include "Framework/LDGameMode.h"
#include "Character/LDCharacter.h"
#include "Character/LDModelPicking.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatPreInput.h"
#include "Combat/LDCombatSourceRules.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameInstance.h"
#include "Navigation/LDNavigationComponent.h"
#include "Navigation/LDOrderRules.h"
#include "UI/LDControlSettings.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"

namespace LDPlayerOrdersDetail
{
    const FLDCombatEntryView* FindEntry(const FLDCombatOwnerView& View, ELDCombatEntry Entry)
    { return View.Entries.FindByPredicate([Entry](const FLDCombatEntryView& Row) { return Row.Entry==Entry; }); }

    bool IsArmedMode(ELDDemoTargetMode Mode)
    {
        return Mode==ELDDemoTargetMode::Unit || Mode==ELDDemoTargetMode::DirectionProjectile
            || Mode==ELDDemoTargetMode::DirectionDash || Mode==ELDDemoTargetMode::GroundPoint || Mode==ELDDemoTargetMode::DirectionCone
            || Mode==ELDDemoTargetMode::DirectionLine;
    }
}

void ALDPlayerController::ActivateCombatEntry(ELDCombatEntry Entry, const FLDCombatTarget* PressAim, const FVector* AimCm)
{
    // The full world-command gate: under the menu, Settings, a child confirmation or a text edit no entry arms or
    // sends (MOVEMENT_INPUT_AND_CAMERA input ownership); the root already cancelled any older preview.
    if (!IsLocalController() || !LDCombatPreInput::IsDirectEntry(Entry) || !CanWorldCommand()) { return; }
    // Another entry takes over: an overdrive armed for a different skill ends with its charge.
    if (bOverdriveArmed && OverdriveEntry!=Entry) { EndOverdriveHold(); }
    const bool bCurrentView=CombatView.Generation==OwnerState.Match && CombatView.Life==OwnerState.Life;
    const FLDCombatEntryView* View=bCurrentView?LDPlayerOrdersDetail::FindEntry(CombatView,Entry):nullptr;
    // An entry whose source does not resolve reports Self (0) and is sent as is for the server's explicit refusal.
    const bool bHeld=View && !View->DefinitionId.IsNone();
    const ELDDemoTargetMode Mode=bHeld?ELDDemoTargetMode(View->TargetMode):ELDDemoTargetMode::Self;
    if (bHeld && LDPlayerOrdersDetail::IsArmedMode(Mode))
    {
        // The D dash goes toward the pointer at once without dropping any overlapping unreleased QuickCast Q/W/E hold,
        // while clearing any stale unheld manual aim.
        if (LDCombatPreInput::FiresOnPress(Entry))
        {
            const ELDCombatEntry PreviousHeldArmed=IsQuickCastHolding(ArmedEntry)?ArmedEntry:ELDCombatEntry::None;
            bAttackMoveArmed=false;
            ArmedEntry=Entry;
            if (PressAim) { ConfirmArmedEntry(*PressAim,AimCm); }
            else { ConfirmArmedCombat(); }
            if (PreviousHeldArmed!=ELDCombatEntry::None && IsQuickCastHolding(PreviousHeldArmed))
            {
                ArmedEntry=PreviousHeldArmed;
            }
            else
            {
                SyncArmedEntryFromQuickCastHolds();
            }
            return;
        }
        bAttackMoveArmed=false;
        ClearQuickCastHold(Entry);
        if (ArmedEntry!=Entry) { UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_INPUT armed entry=%u"),uint32(Entry)); }
        ArmedEntry=Entry;
        return;
    }
    // Self, an empty slot or gear without an active still reaches the server: the attempt is judged there
    // and an admitted aggressive intent ends protection even when it is rejected. Clear only this entry's hold and any
    // unheld manual aim while preserving overlapping unreleased QuickCast holds on other entries.
    ClearQuickCastHold(Entry);
    SyncArmedEntryFromQuickCastHolds();
    FLDCombatTarget Aim;
    Aim.Kind=bHeld && Mode==ELDDemoTargetMode::Self?ELDCombatTargetKind::Self:ELDCombatTargetKind::Missing;
    SendCombatAction(Entry,Aim,IsQueueModifierDown());
}

int32 ALDPlayerController::QuickCastSlotIndex(ELDCombatEntry Entry)
{
    switch (Entry)
    {
    case ELDCombatEntry::Q: return 0;
    case ELDCombatEntry::W: return 1;
    case ELDCombatEntry::E: return 2;
    default: return INDEX_NONE;
    }
}

bool ALDPlayerController::IsQuickCastHolding(ELDCombatEntry Entry, double* OutPressSec) const
{
    const int32 Index=QuickCastSlotIndex(Entry);
    if (Index==INDEX_NONE || !QuickCastHolds[Index].bHolding) { return false; }
    if (OutPressSec) { *OutPressSec=QuickCastHolds[Index].PressSec; }
    return true;
}

bool ALDPlayerController::HasAnyQuickCastHold() const
{
    return QuickCastHolds[0].bHolding || QuickCastHolds[1].bHolding || QuickCastHolds[2].bHolding;
}

bool ALDPlayerController::HasPreviewedQuickCastHold() const
{
    const double NowSec=QuickCastClockSeconds();
    for (int32 Index=0; Index<3; ++Index)
    {
        if (QuickCastHolds[Index].bHolding && (NowSec-QuickCastHolds[Index].PressSec)>=QuickCastHoldPreviewSeconds)
        {
            return true;
        }
    }
    return false;
}

void ALDPlayerController::ClearQuickCastHold(ELDCombatEntry Entry)
{
    const int32 Index=QuickCastSlotIndex(Entry);
    if (Index!=INDEX_NONE) { QuickCastHolds[Index]={}; }
}

bool ALDPlayerController::CancelPreviewedQuickCastHolds()
{
    static constexpr ELDCombatEntry SlotEntries[3]={ELDCombatEntry::Q,ELDCombatEntry::W,ELDCombatEntry::E};
    const double NowSec=QuickCastClockSeconds();
    bool bCancelledQuickHold=false;
    for (int32 Index=0; Index<3; ++Index)
    {
        if (!QuickCastHolds[Index].bHolding || (NowSec-QuickCastHolds[Index].PressSec)<QuickCastHoldPreviewSeconds)
        {
            continue;
        }
        const ELDCombatEntry Entry=SlotEntries[Index];
        QuickCastHolds[Index]={};
        if ((bOverdriveHolding || bOverdriveArmed) && OverdriveEntry==Entry) { EndOverdriveHold(); }
        UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_INPUT armed-cancelled entry=%u"),uint32(Entry));
        ArmedCancelEntry=Entry;
        ++ArmedCancelSerial;
        bCancelledQuickHold=true;
    }
    if (ArmedEntry!=ELDCombatEntry::None && !IsQuickCastHolding(ArmedEntry))
    {
        if ((bOverdriveHolding || bOverdriveArmed) && OverdriveEntry==ArmedEntry) { EndOverdriveHold(); }
        if (!bCancelledQuickHold)
        {
            UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_INPUT armed-cancelled entry=%u"),uint32(ArmedEntry));
            ArmedCancelEntry=ArmedEntry;
            ++ArmedCancelSerial;
        }
    }
    SyncArmedEntryFromQuickCastHolds();
    return bCancelledQuickHold;
}

void ALDPlayerController::ClearAllQuickCastHolds()
{
    QuickCastHolds[0]={};
    QuickCastHolds[1]={};
    QuickCastHolds[2]={};
}

void ALDPlayerController::SyncArmedEntryFromQuickCastHolds()
{
    static constexpr ELDCombatEntry Entries[3]={ELDCombatEntry::Q,ELDCombatEntry::W,ELDCombatEntry::E};
    ELDCombatEntry Newest=ELDCombatEntry::None;
    double NewestPressSec=-1.;
    for (int32 Index=0; Index<3; ++Index)
    {
        if (QuickCastHolds[Index].bHolding && QuickCastHolds[Index].PressSec>=NewestPressSec)
        {
            NewestPressSec=QuickCastHolds[Index].PressSec;
            Newest=Entries[Index];
        }
    }
    bAttackMoveArmed=false;
    ArmedEntry=Newest;
}

double ALDPlayerController::QuickCastClockSeconds() const
{
    return QuickCastTestClockSec>=0. ? QuickCastTestClockSec : FPlatformTime::Seconds();
}

bool ALDPlayerController::ShouldShowArmedPreview() const
{
    if (bAttackMoveArmed) { return true; }
    if (ArmedEntry==ELDCombatEntry::None) { return false; }
    double PressSec=0.;
    if (!IsQuickCastHolding(ArmedEntry,&PressSec)) { return true; }
    return (QuickCastClockSeconds()-PressSec)>=QuickCastHoldPreviewSeconds;
}

void ALDPlayerController::ExecuteQuickCastAim(ELDCombatEntry Entry)
{
    if (!IsLocalController() || !LDCombatPreInput::IsDirectEntry(Entry) || !CanWorldCommand())
    {
        CancelArmedCombat();
        return;
    }
    const bool bCurrentView=CombatView.Generation==OwnerState.Match && CombatView.Life==OwnerState.Life;
    const FLDCombatEntryView* View=bCurrentView?LDPlayerOrdersDetail::FindEntry(CombatView,Entry):nullptr;
    const bool bHeld=View && !View->DefinitionId.IsNone();
    const ELDDemoTargetMode Mode=bHeld?ELDDemoTargetMode(View->TargetMode):ELDDemoTargetMode::Self;
    if (!bHeld || !LDPlayerOrdersDetail::IsArmedMode(Mode))
    {
        ActivateCombatEntry(Entry);
        return;
    }
    ArmedEntry=Entry;
    if (Mode==ELDDemoTargetMode::Unit)
    {
        FLDCombatTarget Aim;
        if (ObservedNearPointer(LDModelPicking::QuickCastSnapPx,Aim.Observed,Aim.ObservationEpoch))
        {
            Aim.Kind=ELDCombatTargetKind::Unit;
            ConfirmArmedEntry(Aim);
        }
        else
        {
            if ((bOverdriveHolding || bOverdriveArmed) && OverdriveEntry==Entry) { EndOverdriveHold(); }
            ClearQuickCastHold(Entry);
            ArmedCancelEntry=Entry;
            ++ArmedCancelSerial;
            SyncArmedEntryFromQuickCastHolds();
            PostIntentNotice(TEXT("指针附近没有目标"));
        }
        return;
    }
    ConfirmArmedCombat();
}

// Shift is no mapped action, so the player input's key state may never see it; Slate's modifier state is the fallback.
bool ALDPlayerController::IsQueueModifierDown() const
{
    if (!IsLocalController()) { return false; }
    if (IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift)) { return true; }
    return FSlateApplication::IsInitialized() && FSlateApplication::Get().GetModifierKeys().IsShiftDown();
}

bool ALDPlayerController::ReadArmedCombatEntry(ELDCombatEntry& Out) const
{
    Out=ArmedEntry;
    return ArmedEntry!=ELDCombatEntry::None;
}

void ALDPlayerController::ArmAttackMove()
{
    if (!IsLocalController() || !CanWorldCommand()) { return; }
    ArmedEntry=ELDCombatEntry::None;
    ClearAllQuickCastHolds();
    bAttackMoveArmed=true;
}

bool ALDPlayerController::ClearArmedAim()
{
    const bool bWasArmed=bAttackMoveArmed || ArmedEntry!=ELDCombatEntry::None || HasAnyQuickCastHold();
    VectorAimEntry=ELDCombatEntry::None;
    bVectorAimMouse=false;
    bAttackMoveArmed=false;
    ArmedEntry=ELDCombatEntry::None;
    ClearAllQuickCastHolds();
    return bWasArmed;
}

bool ALDPlayerController::CancelArmedCombat()
{
    // A real cancel (S, Escape, a menu, a move order, a new life or source revision) also ends a running or completed
    // overdrive hold and the visible charge with it. A held skill with no armed aim (a self skill charging) is reported like
    // an armed one, so its key never goes quiet without a sign.
    const ELDCombatEntry Charging=bOverdriveHolding || bOverdriveArmed ? OverdriveEntry : ELDCombatEntry::None;
    if (bOverdriveHolding || bOverdriveArmed) { EndOverdriveHold(); }
    const ELDCombatEntry Cancelled=ArmedEntry!=ELDCombatEntry::None ? ArmedEntry : Charging;
    if (Cancelled!=ELDCombatEntry::None)
    {
        UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_INPUT armed-cancelled entry=%u overdrive=%u"),uint32(Cancelled),uint32(Charging));
        ArmedCancelEntry=Cancelled;
        ++ArmedCancelSerial;
    }
    return ClearArmedAim();
}

bool ALDPlayerController::ConfirmArmedCombat()
{
    if (bAttackMoveArmed)
    {
        ClearArmedAim();
        if (!IsLocalController() || !CanWorldCommand()) { return true; }
        FHitResult GroundHit;
        const bool bQueue=IsQueueModifierDown();
        // A click on an attackable visible unit attacks it exactly like a right click (2026-11-07 decision); anywhere else
        // is an attack-move. The pick is the right click's (and the hover ring's) pointer pick.
        FGuid Handle;
        uint32 Epoch=0;
        const bool bUnit=ObservedUnderPointer(Handle,Epoch);
        if (LDOrderRules::AttackConfirmKind(bUnit)==ELDSpatialOrderKind::AttackUnit)
        {
            Navigation->AttackUnit(Handle,Epoch,bQueue);
            return true;
        }
        // A confirmation without ground is still a normalized aggressive intent; the server rejects it explicitly.
        if (Navigation->GroundUnderPointer(GroundHit))
        { Navigation->AttackMove(GroundHit.ImpactPoint,Navigation->GroundSurfaceHint(),bQueue); }
        else { Navigation->AttackMove(FVector::ZeroVector,NAME_None,bQueue); }
        return true;
    }
    if (ArmedEntry==ELDCombatEntry::None) { return false; }
    // A missing or unresolved aim still confirms: the server rejects it explicitly.
    FLDCombatTarget Aim;
    const bool bCurrentView=CombatView.Generation==OwnerState.Match && CombatView.Life==OwnerState.Life;
    const FLDCombatEntryView* View=bCurrentView?LDPlayerOrdersDetail::FindEntry(CombatView,ArmedEntry):nullptr;
    const ELDDemoTargetMode Mode=View?ELDDemoTargetMode(View->TargetMode):ELDDemoTargetMode::Self;
    if (!IsLocalController() || !CanWorldCommand()) { return ConfirmArmedEntry(Aim); }
    if (Mode==ELDDemoTargetMode::Unit)
    {
        FGuid Handle;
        uint32 Epoch=0;
        if (ObservedUnderPointer(Handle,Epoch))
        {
            Aim.Kind=ELDCombatTargetKind::Unit;
            Aim.Observed=Handle;
            Aim.ObservationEpoch=Epoch;
        }
    }
    else if (Mode==ELDDemoTargetMode::DirectionProjectile || Mode==ELDDemoTargetMode::DirectionDash || Mode==ELDDemoTargetMode::DirectionCone
        || Mode==ELDDemoTargetMode::DirectionLine)
    {
        // #335 §1.2: intersect the cursor ray directly with the caster's foot elevation plane (Z = Foot.Z) so walls,
        // roofs and foliage never deflect skill or dash directions; within 8 cm of Foot fall back to ActorForward.
        const auto* Body=Cast<ALDCharacter>(GetPawn());
        if (Body && Navigation)
        {
            const FVector Foot=Body->GetActorLocation()-FVector(0,0,Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            FVector AimWorldPoint=FVector::ZeroVector;
            if (Navigation->AimPointAtElevation(Foot.Z,AimWorldPoint))
            {
                const FVector Direction=LDOrderRules::ResolveElevationAimDirection(Foot,AimWorldPoint,Body->GetActorForwardVector());
                Aim.Kind=ELDCombatTargetKind::Direction;
                Aim.Direction=Direction;
                const FVector Point=FVector::DistSquared2D(AimWorldPoint,Foot)>=FMath::Square(LDOrderRules::AimDeadzoneCm)
                    ?AimWorldPoint:(Foot+Direction*200.);
                return ConfirmArmedEntry(Aim,&Point);
            }
        }
    }
    else if (Mode==ELDDemoTargetMode::GroundPoint)
    {
        // #335 §1.2: evaluate desired ground aim on the caster's elevation plane first so tall wall tops never pull the
        // target point toward the camera; fall back to GroundUnderPointer only when bodiless.
        const auto* Body=Cast<ALDCharacter>(GetPawn());
        FVector AimWorldPoint=FVector::ZeroVector;
        if (Body && Navigation && Navigation->AimPointAtElevation(
            Body->GetActorLocation().Z-Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),AimWorldPoint))
        {
            Aim.Kind=ELDCombatTargetKind::Point;
            Aim.Point=AimWorldPoint;
        }
        else
        {
            FHitResult GroundHit;
            if (Navigation && Navigation->GroundUnderPointer(GroundHit))
            {
                Aim.Kind=ELDCombatTargetKind::Point;
                Aim.Point=GroundHit.ImpactPoint;
            }
        }
        // Vector cast: a drag past the deadzone casts at its start (clamped into reach, so the body does not walk) facing
        // the drag; a shorter one is a tap at the pointer with the default facing.
        if (VectorAimEntry==ArmedEntry && Body && View && Aim.Kind==ELDCombatTargetKind::Point
            && FVector::Dist2D(Aim.Point,VectorAimStartCm)>=LDCombatSourceRules::VectorAimDeadzoneCm)
        {
            const FVector Foot=Body->GetActorLocation()-FVector(0,0,Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            const FVector Drag=Aim.Point-VectorAimStartCm;
            const FVector Offset=FVector(VectorAimStartCm.X-Foot.X,VectorAimStartCm.Y-Foot.Y,0).GetClampedToMaxSize(double(View->RangeCm));
            Aim.Point=FVector(Foot.X+Offset.X,Foot.Y+Offset.Y,VectorAimStartCm.Z);
            Aim.Direction=FVector(Drag.X,Drag.Y,0).GetSafeNormal();
        }
        VectorAimEntry=ELDCombatEntry::None;
        bVectorAimMouse=false;
    }
    return ConfirmArmedEntry(Aim);
}

bool ALDPlayerController::ConfirmArmedEntry(const FLDCombatTarget& Aim, const FVector* AimCm)
{
    const ELDCombatEntry Entry=ArmedEntry;
    if (Entry==ELDCombatEntry::None) { return false; }
    ClearQuickCastHold(Entry);
    SyncArmedEntryFromQuickCastHolds();
    if (!IsLocalController() || !CanWorldCommand()) { EndOverdriveHold(); return true; }
    SendCombatAction(Entry,Aim,IsQueueModifierDown(),AimCm);
    // An overdrive the send neither consumed nor queued ends here instead of lingering as a visible charge.
    if (bOverdriveArmed && OverdriveEntry==Entry && QueuedCombat.Entry!=Entry) { EndOverdriveHold(); }
    // Confirmed before the hold completed: an ordinary cast, and the hold ends with it.
    if (bOverdriveHolding && OverdriveEntry==Entry) { EndOverdriveHold(); }
    return true;
}

void ALDPlayerController::StopCombatAndOrders()
{
    // S is a new world command: the menu, Settings, a child confirmation, a text edit or an unreleased key keeps it
    // from the world, so it never stops the accepted order those leave running (MOVEMENT_INPUT_AND_CAMERA input ownership).
    if (IsWorldInputSuppressed()) { return; }
    CancelArmedCombat();
    QueuedCombat={};
    Navigation->Stop();
    // Cancels every Direct request issued so far, including ones the server has not received yet.
    if (!IsLocalController() || NextDirectSequence==0 || OwnerState.LifePhase!=ELDLifePhase::Alive) { return; }
    FLDCombatCancelRequest Request;
    Request.Generation=OwnerState.Match;
    Request.Life=OwnerState.Life;
    Request.ThroughDirectSequence=NextDirectSequence;
    ServerCancelCombatAction(Request);
}

void ALDPlayerController::PostIntentNotice(const FString& Text)
{
    if (Text.IsEmpty() || !IsLocalController()) { return; }
    if (IntentFeedbackSerial==MAX_uint32)
    {
        UE_LOG(LogLDCombat,Error,TEXT("LD_INTENT_FEEDBACK_EXHAUSTED"));
        return;
    }
    ++IntentFeedbackSerial;
    IntentFeedback=Text;
}

void ALDPlayerController::SendCombatAction(ELDCombatEntry Entry, const FLDCombatTarget& Aim, bool bQueue, const FVector* AimCm)
{
    if (!IsLocalController()) { return; }
    // A unit-target cast is an order (2026-11-06 decision): the body walks into the skill's reach and casts there, and
    // the server waits out the own windup itself, so it is never held as pre-input.
    if (Aim.Kind==ELDCombatTargetKind::Unit)
    {
        QueuedCombat={};
        FLDCombatActionRequest Request;
        if (!NextCombatRequest(Entry,Aim,Request)) { return; }
        UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_INPUT cast-order match=%u life=%u sequence=%llu entry=%u queue=%d"),
            Request.Generation,Request.Life,Request.DirectSequence,uint32(Entry),bQueue?1:0);
        Navigation->CastAtUnit(Request,bQueue);
        return;
    }
    // A ground-point or projectile skill aimed at a ground point is an order too: the body walks until the point lies
    // within the skill's reach, then casts at it (2026-11-06 decision). Dashes keep firing at once.
    {
        const bool bCurrentView=CombatView.Generation==OwnerState.Match && CombatView.Life==OwnerState.Life;
        const FLDCombatEntryView* View=bCurrentView?LDPlayerOrdersDetail::FindEntry(CombatView,Entry):nullptr;
        const ELDDemoTargetMode Mode=View?ELDDemoTargetMode(View->TargetMode):ELDDemoTargetMode::Self;
        const FVector Point=Aim.Kind==ELDCombatTargetKind::Point?FVector(Aim.Point):AimCm?*AimCm:FVector::ZeroVector;
        const bool bPointCast=(Mode==ELDDemoTargetMode::GroundPoint && Aim.Kind==ELDCombatTargetKind::Point)
            || ((Mode==ELDDemoTargetMode::DirectionProjectile || Mode==ELDDemoTargetMode::DirectionCone
                    || Mode==ELDDemoTargetMode::DirectionLine)
                && Aim.Kind==ELDCombatTargetKind::Direction && AimCm);
        if (bPointCast)
        {
            QueuedCombat={};
            FLDCombatActionRequest Request;
            if (!NextCombatRequest(Entry,Aim,Request)) { return; }
            UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_INPUT point-cast-order match=%u life=%u sequence=%llu entry=%u queue=%d"),
                Request.Generation,Request.Life,Request.DirectSequence,uint32(Entry),bQueue?1:0);
            Navigation->CastAtPoint(Request,Point,bQueue);
            return;
        }
    }
    if (bQueue)
    {
        // The shift queue waits for its own turn on the server, so a queued press is never held as pre-input; it
        // replaces a held one like any newer press.
        QueuedCombat={};
        FLDCombatActionRequest Request;
        if (!NextCombatRequest(Entry,Aim,Request)) { return; }
        UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_INPUT queued-step match=%u life=%u sequence=%llu entry=%u target=%u"),
            Request.Generation,Request.Life,Request.DirectSequence,uint32(Entry),uint32(Aim.Kind));
        Navigation->QueueCombatAction(Request,Aim.Kind==ELDCombatTargetKind::Direction?AimCm:nullptr);
        return;
    }
    // Issue #329 §3.2: send Direct presses immediately to the server's unified 2-step 450ms FIFO pre-input queue
    // so mixed Direct / Unit / GroundPoint combinations preserve every press in exact arrival order.
    QueuedCombat={};
    SendCombatNow(Entry,Aim);
}

// The held press goes out as an ordinary Direct request (fresh sequence, current source revision) once the own windup
// ended; a new life or match, suppressed input or an expired hold drops it.
void ALDPlayerController::FlushQueuedCombat()
{
    if (QueuedCombat.Entry==ELDCombatEntry::None) { return; }
    if (QueuedCombat.Match!=OwnerState.Match || QueuedCombat.Life!=OwnerState.Life || !CanWorldCommand())
    {
        QueuedCombat={};
        return;
    }
    if (QueuedCombat.SourceRevision!=CombatView.SourceRevision)
    {
        QueuedCombat={};
        PostIntentNotice(TEXT("技能或装备已变化，预输入已取消"));
        return;
    }
    if (FPlatformTime::Seconds()-QueuedCombat.QueuedAt>LDCombatPreInput::HoldSeconds)
    {
        QueuedCombat={};
        PostIntentNotice(TEXT("前一个技能未结束，预输入已取消"));
        return;
    }
    int64 NowMs=0;
    ReadDemoAuthorityTimeMs(NowMs);
    const bool bCurrentView=CombatView.Generation==OwnerState.Match && CombatView.Life==OwnerState.Life;
    if (bCurrentView && LDCombatPreInput::IsWindingUp(CombatView,NowMs)) { return; }
    const FQueuedCombat Held=QueuedCombat;
    QueuedCombat={};
    SendCombatNow(Held.Entry,Held.Aim);
}

bool ALDPlayerController::NextCombatRequest(ELDCombatEntry Entry, const FLDCombatTarget& Aim, FLDCombatActionRequest& Request)
{
    if (NextDirectSequence==MAX_uint64)
    {
        PostIntentNotice(TEXT("技能请求编号已用尽，无法再发送"));
        return false;
    }
    Request={};
    Request.Generation=OwnerState.Match;
    Request.Life=OwnerState.Life;
    Request.DirectSequence=++NextDirectSequence;
    Request.Entry=Entry;
    Request.Target=Aim;
    Request.Target.ServerBody={};
    Request.ExpectedSourceRevision=CombatView.SourceRevision;
    // The overdrive armed by a completed hold rides on this entry's next request only.
    if (bOverdriveArmed && Entry==OverdriveEntry)
    {
        Request.bOverdrive=true;
        bOverdriveArmed=false;
        OverdriveEntry=ELDCombatEntry::None;
    }
    if (auto* GI=Cast<ULDGameInstance>(GetGameInstance()))
    {
        GI->NoteCrashCommand(*FString::Printf(TEXT("CombatEntry=%u Kind=%u Seq=%llu"),
            uint32(Entry),uint32(Aim.Kind),Request.DirectSequence));
    }
    return true;
}

bool ALDPlayerController::IsVectorAimedEntry(ELDCombatEntry Entry)
{
    const bool bCurrentView=CombatView.Generation==OwnerState.Match && CombatView.Life==OwnerState.Life;
    const FLDCombatEntryView* View=bCurrentView?LDPlayerOrdersDetail::FindEntry(CombatView,Entry):nullptr;
    if (!View || View->DefinitionId.IsNone() || ELDDemoTargetMode(View->TargetMode)!=ELDDemoTargetMode::GroundPoint || !OverdriveTerms())
    { return false; }
    const FLDDemoContentDefinitions& Content=OverdriveContent->Get();
    const FLDDemoAbilityDefinition* Ability=Content.Abilities.FindByPredicate([View](const FLDDemoAbilityDefinition& Row)
        { return Row.Id==View->DefinitionId.ToString(); });
    if (!Ability) { return false; }
    const FLDActiveSlotView* Learned=OwnerState.Growth.Progression.Actives.FindByPredicate([View](const FLDActiveSlotView& Row)
        { return Row.DefinitionId==View->DefinitionId; });
    const FLDDemoAbilityBranch* Branch=Learned && Learned->Branch!=ELDSkillBranch::None
        ? Ability->Branches.FindByPredicate([Learned](const FLDDemoAbilityBranch& Row) { return Row.Path==Learned->Branch; }) : nullptr;
    return LDCombatSourceRules::VectorAimed(Branch?Branch->Values:Ability->Values);
}

bool ALDPlayerController::BeginVectorAim(ELDCombatEntry Entry, bool bMouse)
{
    VectorAimEntry=ELDCombatEntry::None;
    bVectorAimMouse=false;
    const auto* Body=Cast<ALDCharacter>(GetPawn());
    FVector Start=FVector::ZeroVector;
    if (!Body || !Navigation || !IsVectorAimedEntry(Entry)
        || !Navigation->AimPointAtElevation(Body->GetActorLocation().Z-Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),Start))
    { return false; }
    VectorAimEntry=Entry;
    VectorAimStartCm=Start;
    bVectorAimMouse=bMouse;
    return true;
}

void ALDPlayerController::TickVectorAimRelease()
{
    if (!bVectorAimMouse || VectorAimEntry==ELDCombatEntry::None) { return; }
    if (VectorAimEntry!=ArmedEntry) { VectorAimEntry=ELDCombatEntry::None; bVectorAimMouse=false; return; }
    if (!IsInputKeyDown(EKeys::LeftMouseButton)) { ConfirmArmedCombat(); }
}

bool ALDPlayerController::ReadVectorAim(ELDCombatEntry& OutEntry, FVector& OutStartCm) const
{
    OutEntry=VectorAimEntry;
    OutStartCm=VectorAimStartCm;
    return VectorAimEntry!=ELDCombatEntry::None && VectorAimEntry==ArmedEntry;
}

const FLDDemoOverdriveParameters* ALDPlayerController::OverdriveTerms()
{
    if (!bOverdriveContentTried)
    {
        bOverdriveContentTried=true;
        FString Error;
        if (!LDDemoEntry::LoadRuntimeContent(OverdriveContent,Error)) { OverdriveContent.Reset(); }
    }
    return OverdriveContent ? &OverdriveContent->Get().Parameters.Overdrive : nullptr;
}

void ALDPlayerController::SendCharge(ELDCombatEntry Entry, bool bCharging)
{
    FLDCombatChargeRequest Request;
    Request.Generation=OwnerState.Match;
    Request.Life=OwnerState.Life;
    Request.Entry=Entry;
    Request.bCharging=bCharging;
    ServerChargeOverdrive(Request);
}

void ALDPlayerController::EndOverdriveHold()
{
    if (OverdriveEntry!=ELDCombatEntry::None && (bOverdriveHolding || bOverdriveArmed)) { SendCharge(OverdriveEntry,false); }
    OverdriveEntry=ELDCombatEntry::None;
    bOverdriveHolding=false;
    bOverdriveArmed=false;
    bOverdriveLatched=false;
}

void ALDPlayerController::PressSkill(ELDCombatEntry Entry)
{
    if (!IsLocalController() || !LDCombatPreInput::IsDirectEntry(Entry) || !CanWorldCommand()) { return; }
    // A full bar and a ready learned active start the hold: the release decides between a normal cast and overdrive.
    const bool bCurrentView=CombatView.Generation==OwnerState.Match && CombatView.Life==OwnerState.Life;
    const FLDCombatEntryView* View=bCurrentView?LDPlayerOrdersDetail::FindEntry(CombatView,Entry):nullptr;
    const bool bHeld=View && !View->DefinitionId.IsNone();
    const ELDDemoTargetMode Mode=bHeld?ELDDemoTargetMode(View->TargetMode):ELDDemoTargetMode::Self;
    const bool bAimed=bHeld && LDPlayerOrdersDetail::IsArmedMode(Mode) && !LDCombatPreInput::FiresOnPress(Entry);
    const bool bQuick=Controls && Controls->GetQuickCast();
    const FLDDemoOverdriveParameters* Terms=OverdriveTerms();
    // Unavailable is the published snapshot: a cooldown or momentum reason in it can be stale, so readiness is judged on the
    // current authority time and the current bar; any other reason (control, busy) still blocks. The server decides.
    int64 NowMs=0;
    const bool bClock=ReadDemoAuthorityTimeMs(NowMs);
    const bool bStaleOnlyReason=View && (View->Unavailable==ELDCombatReject::None || View->Unavailable==ELDCombatReject::Cooldown
        || View->Unavailable==ELDCombatReject::MomentumShort);
    const bool bOverdriveEligible=Terms && bHeld && View->SourceKind==ELDCombatSourceKind::Learned && bStaleOnlyReason && bClock
        && View->ReadyAtMs<=NowMs
        && CombatView.MomentumP2>=(CombatView.OverdriveThresholdP2>0 ? CombatView.OverdriveThresholdP2 : Terms->ThresholdP2)
        && CombatView.MomentumP2>=View->MomentumCostP2 && (!bOverdriveHolding || OverdriveEntry!=Entry);

    // Keyboard QuickCast on an aimed skill: < 0.18s tap hides the preview and casts on release; >= 0.18s hold reveals the
    // aim indicator and casts on release (right-click after >= 0.18s cancels without firing). Tracked per entry so
    // overlapping Q/W/E presses preserve every pending release-cast.
    const int32 HoldSlot=QuickCastSlotIndex(Entry);
    if (bQuick && bAimed && HoldSlot!=INDEX_NONE)
    {
        if (bOverdriveArmed && OverdriveEntry!=Entry) { EndOverdriveHold(); }
        const double NowSec=QuickCastClockSeconds();
        bAttackMoveArmed=false;
        if (ArmedEntry!=Entry) { UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_INPUT armed entry=%u"),uint32(Entry)); }
        ArmedEntry=Entry;
        QuickCastHolds[HoldSlot].bHolding=true;
        QuickCastHolds[HoldSlot].PressSec=NowSec;
        BeginVectorAim(Entry,false);
        if (bOverdriveEligible)
        {
            if ((bOverdriveHolding || bOverdriveArmed) && OverdriveEntry!=Entry) { EndOverdriveHold(); }
            else if (bOverdriveArmed) { EndOverdriveHold(); }
            OverdriveEntry=Entry;
            OverdriveHoldStartSec=NowSec;
            bOverdriveHolding=true;
            bOverdriveLatched=FSlateApplication::IsInitialized()&&FSlateApplication::Get().GetModifierKeys().IsAltDown();
            SendCharge(Entry,true);
        }
        return;
    }

    if (bOverdriveEligible)
    {
        if ((bOverdriveHolding || bOverdriveArmed) && OverdriveEntry!=Entry) { EndOverdriveHold(); }
        else if (bOverdriveArmed) { EndOverdriveHold(); }
        OverdriveEntry=Entry;
        OverdriveHoldStartSec=QuickCastClockSeconds();
        bOverdriveHolding=true;
        // Alt with the key: one press is enough, the charge completes without holding the key.
        bOverdriveLatched=FSlateApplication::IsInitialized()&&FSlateApplication::Get().GetModifierKeys().IsAltDown();
        SendCharge(Entry,true);
        // An aimed skill with QuickCast off is armed from the press itself: a left click during the hold is an ordinary cast,
        // and only a hold that completes turns the armed aim into overdrive.
        if (bAimed) { ActivateCombatEntry(Entry); }
        return;
    }
    ActivateCombatEntry(Entry);
}

void ALDPlayerController::ReleaseSkill(ELDCombatEntry Entry)
{
    if (IsQuickCastHolding(Entry))
    {
        ClearQuickCastHold(Entry);
        if (bOverdriveHolding && OverdriveEntry==Entry)
        {
            // Alt-latched overdrive keeps charging after early key release and fires once HoldMs completes.
            if (bOverdriveLatched)
            {
                SyncArmedEntryFromQuickCastHolds();
                return;
            }
            EndOverdriveHold();
        }
        ExecuteQuickCastAim(Entry);
        return;
    }
    if (!bOverdriveHolding || Entry!=OverdriveEntry) { return; }
    // A one-press (Alt) charge keeps going after the key is let go.
    if (bOverdriveLatched) { return; }
    // Let go before the hold completed: the charge ends and the press is an ordinary cast.
    EndOverdriveHold();
    if (ArmedEntry!=Entry) { ActivateCombatEntry(Entry); }
}

void ALDPlayerController::TickOverdriveHold()
{
    if (!bOverdriveHolding) { return; }
    const FLDDemoOverdriveParameters* Terms=OverdriveTerms();
    if (!Terms || !CanWorldCommand()) { EndOverdriveHold(); return; }
    if ((QuickCastClockSeconds()-OverdriveHoldStartSec)*1000.<double(Terms->HoldMs)) { return; }
    // Held through OVERDRIVE.HoldSec: the next request of this entry asks for overdrive (the server decides by the bar).
    bOverdriveHolding=false;
    bOverdriveArmed=true;
    // While the player still holds a QuickCast aimed key (without Alt latch), keep previewing and fire on key release.
    if (IsQuickCastHolding(OverdriveEntry) && !bOverdriveLatched) { return; }
    const bool bCurrentView=CombatView.Generation==OwnerState.Match && CombatView.Life==OwnerState.Life;
    const FLDCombatEntryView* View=bCurrentView?LDPlayerOrdersDetail::FindEntry(CombatView,OverdriveEntry):nullptr;
    const bool bAimed=View && !View->DefinitionId.IsNone() && LDPlayerOrdersDetail::IsArmedMode(ELDDemoTargetMode(View->TargetMode));
    if (Controls && Controls->GetQuickCast() && bAimed)
    {
        ClearQuickCastHold(OverdriveEntry);
        ExecuteQuickCastAim(OverdriveEntry);
        return;
    }
    ActivateCombatEntry(OverdriveEntry);
}

float ALDPlayerController::OverdriveHoldProgress(ELDCombatEntry Entry) const
{
    if (Entry!=OverdriveEntry) { return 0.f; }
    if (bOverdriveArmed) { return 1.f; }
    if (!bOverdriveHolding || !OverdriveContent) { return 0.f; }
    const double HoldMs=double(OverdriveContent->Get().Parameters.Overdrive.HoldMs);
    return HoldMs>0 ? float(FMath::Clamp((QuickCastClockSeconds()-OverdriveHoldStartSec)*1000./HoldMs,0.,1.)) : 0.f;
}

void ALDPlayerController::SendCombatNow(ELDCombatEntry Entry, const FLDCombatTarget& Aim)
{
    if (!IsLocalController()) { return; }
    FLDCombatActionRequest Request;
    if (!NextCombatRequest(Entry,Aim,Request)) { return; }
    if (Navigation) { Navigation->InvalidateClickMoveCoalesce(); }
    UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_INPUT sent match=%u life=%u sequence=%llu entry=%u target=%u source=%llu"),
        Request.Generation,Request.Life,Request.DirectSequence,uint32(Entry),uint32(Aim.Kind),Request.ExpectedSourceRevision);
    ServerSubmitCombatAction(Request);
}

void ALDPlayerController::ServerSubmitCombatAction_Implementation(const FLDCombatActionRequest& Request)
{
    FLDCombatActionRequest Incoming=Request;
    Incoming.Target.ServerBody={};
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    FLDCombatCaller Caller;
    FString Error;
    if (!Mode || !Mode->Combat || !Mode->Combat->ResolveControllerCaller(*this,Caller,Error))
    {
        UE_LOG(LogLDCombat,Warning,TEXT("LD_COMBAT_CALLER_REJECTED %s"),*Error);
        FLDCombatActionReceipt Receipt;
        Receipt.Generation=Request.Generation;
        Receipt.Life=Request.Life;
        Receipt.DirectSequence=Request.DirectSequence;
        Receipt.Entry=Request.Entry;
        Receipt.Phase=ELDCombatPhase::Rejected;
        Receipt.Reject=ELDCombatReject::NotOwner;
        ClientCombatReceipt(Receipt);
        return;
    }
    // A non-Shift Q/W/D never touches the current order. A refused one leaves the shift queue alone too (2026-11-07
    // decision): while steps wait, an entry that is not ready is refused before Combat sees it, and an admitted one drops
    // the steps sent before it (their lower sequences could never run after it) once Combat accepted it.
    FLDCombatActionReceipt Receipt;
    if (Navigation && Navigation->RefuseUnreadyDirect(Incoming,Receipt))
    {
        ClientCombatReceipt(Receipt);
        return;
    }
    Receipt=Mode->Combat->SubmitAction(Caller,Incoming);
    if (Receipt.Phase!=ELDCombatPhase::Rejected && Navigation) { Navigation->AwaitDirectVerdict(Incoming.DirectSequence); }
    ClientCombatReceipt(Receipt);
}

void ALDPlayerController::ServerCancelCombatAction_Implementation(const FLDCombatCancelRequest& Request)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    FLDCombatCaller Caller;
    FString Error;
    if (!Mode || !Mode->Combat || !Mode->Combat->ResolveControllerCaller(*this,Caller,Error))
    {
        UE_LOG(LogLDCombat,Warning,TEXT("LD_COMBAT_CALLER_REJECTED %s"),*Error);
        FLDCombatActionReceipt Receipt;
        Receipt.Generation=Request.Generation;
        Receipt.Life=Request.Life;
        Receipt.DirectSequence=Request.ThroughDirectSequence;
        Receipt.Phase=ELDCombatPhase::Rejected;
        Receipt.Reject=ELDCombatReject::NotOwner;
        ClientCombatReceipt(Receipt);
        return;
    }
    ClientCombatReceipt(Mode->Combat->CancelAction(Caller,Request));
}

void ALDPlayerController::ServerChargeOverdrive_Implementation(const FLDCombatChargeRequest& Request)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    FLDCombatCaller Caller;
    FString Error;
    if (!Mode || !Mode->Combat || !Mode->Combat->ResolveControllerCaller(*this,Caller,Error))
    {
        UE_LOG(LogLDCombat,Verbose,TEXT("LD_COMBAT_CHARGE_CALLER_REJECTED %s"),*Error);
        return;
    }
    // A charge is presentation and a gate for the later request: no receipt, a stale one simply changes nothing.
    Mode->Combat->SubmitCharge(Caller,Request);
}

void ALDPlayerController::RequestEmpowerHost(ELDCombatEntry Host)
{
    EmpowerHostSequence=FMath::Max(EmpowerHostSequence,CombatView.EmpowerHostSequence)+1;
    ServerSetEmpowerHost(CombatView.Generation,EmpowerHostSequence,Host);
}

void ALDPlayerController::ServerSetEmpowerHost_Implementation(uint32 Generation, uint64 Sequence, ELDCombatEntry Host)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    FLDCombatCaller Caller;
    FString Reason;
    if (!Mode || !Mode->Combat || !Mode->Combat->ResolveControllerCaller(*this,Caller,Reason))
    {
        ClientEmpowerHostRefused(Sequence,TEXT("对局已变化"));
        return;
    }
    if (Generation!=Caller.Match.Generation) { ClientEmpowerHostRefused(Sequence,TEXT("对局已变化")); return; }
    if (!Mode->Combat->SubmitEmpowerHost(Caller,Sequence,Host,Reason)) { ClientEmpowerHostRefused(Sequence,Reason); }
}

void ALDPlayerController::ClientEmpowerHostRefused_Implementation(uint64 Sequence, const FString& Reason)
{
    UE_LOG(LogLDCombat,Display,TEXT("LD_EMPOWER_HOST refused sequence=%llu reason=%s"),Sequence,*Reason);
    PostIntentNotice(Reason);
}

// Only records the receipt: the HUD alone presents combat receipts (LDHUD::CombatReceiptNotice).
void ALDPlayerController::ClientCombatReceipt_Implementation(const FLDCombatActionReceipt& Receipt)
{
    if (Receipt.Generation!=OwnerState.Match) { return; }
    UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_INPUT receipt match=%u life=%u sequence=%llu entry=%u phase=%u reject=%u token=%llu"),
        Receipt.Generation,Receipt.Life,Receipt.DirectSequence,uint32(Receipt.Entry),uint32(Receipt.Phase),
        uint32(Receipt.Reject),Receipt.CombatToken);
    if (Navigation) { Navigation->NoteCombatReceipt(Receipt); }
    if (!CombatReceipts.Push(Receipt)) { UE_LOG(LogLDCombat,Error,TEXT("LD_COMBAT_RECEIPT_SERIAL_EXHAUSTED")); }
}

void ALDPlayerController::OnRep_CombatView()
{
    const bool bNewLine=CombatView.Generation!=LatestCombatView.Generation || CombatView.Life!=LatestCombatView.Life;
    const bool bOlderLine=CombatView.Generation<LatestCombatView.Generation
        || (CombatView.Generation==LatestCombatView.Generation && CombatView.Life<LatestCombatView.Life);
    // A view of an older match or life, or an older revision of the same life, never replaces the accepted one.
    if ((OwnerState.Match!=0 && CombatView.Generation<OwnerState.Match) || bOlderLine
        || (!bNewLine && CombatView.Revision<=LatestCombatView.Revision && CombatView.bHasBody==LatestCombatView.bHasBody))
    {
        CombatView=LatestCombatView;
        return;
    }
    // An armed preview belongs to its life and to the skill it was armed for: a build change elsewhere (a level, a
    // stat point, a card) keeps it, and the confirmation is sent against the current source revision.
    if (bNewLine || CombatView.SourceRevision!=LatestCombatView.SourceRevision)
    {
        if (bNewLine)
        {
            CancelArmedCombat();
        }
        else
        {
            if ((bOverdriveHolding || bOverdriveArmed) && OverdriveEntry!=ELDCombatEntry::None)
            {
                const FLDCombatEntryView* OldOver=LDPlayerOrdersDetail::FindEntry(LatestCombatView,OverdriveEntry);
                const FLDCombatEntryView* NewOver=LDPlayerOrdersDetail::FindEntry(CombatView,OverdriveEntry);
                if (!OldOver || !NewOver || OldOver->DefinitionId!=NewOver->DefinitionId || OldOver->TargetMode!=NewOver->TargetMode)
                {
                    EndOverdriveHold();
                }
            }
            static constexpr ELDCombatEntry SlotEntries[3]={ELDCombatEntry::Q,ELDCombatEntry::W,ELDCombatEntry::E};
            for (int32 Index=0; Index<3; ++Index)
            {
                if (!QuickCastHolds[Index].bHolding) { continue; }
                const FLDCombatEntryView* OldSlot=LDPlayerOrdersDetail::FindEntry(LatestCombatView,SlotEntries[Index]);
                const FLDCombatEntryView* NewSlot=LDPlayerOrdersDetail::FindEntry(CombatView,SlotEntries[Index]);
                if (!OldSlot || !NewSlot || OldSlot->DefinitionId!=NewSlot->DefinitionId || OldSlot->TargetMode!=NewSlot->TargetMode)
                {
                    QuickCastHolds[Index]={};
                }
            }
            const FLDCombatEntryView* Was=LDPlayerOrdersDetail::FindEntry(LatestCombatView,ArmedEntry);
            const FLDCombatEntryView* Now=LDPlayerOrdersDetail::FindEntry(CombatView,ArmedEntry);
            const bool bSameSkill=ArmedEntry!=ELDCombatEntry::None && Was && Now && Was->DefinitionId==Now->DefinitionId
                && Was->TargetMode==Now->TargetMode;
            if (!bSameSkill)
            {
                if (ArmedEntry!=ELDCombatEntry::None)
                {
                    ArmedCancelEntry=ArmedEntry;
                    ++ArmedCancelSerial;
                }
                SyncArmedEntryFromQuickCastHolds();
            }
        }
        if (QueuedCombat.Entry!=ELDCombatEntry::None)
        {
            QueuedCombat={};
            if (!bNewLine) { PostIntentNotice(TEXT("技能或装备已变化，预输入已取消")); }
        }
    }
    LatestCombatView=CombatView;
}
