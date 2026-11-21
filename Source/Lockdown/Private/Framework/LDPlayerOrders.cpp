#include "Framework/LDPlayerController.h"
#include "Core/LDLog.h"
#include "Framework/LDGameMode.h"
#include "Character/LDCharacter.h"
#include "Character/LDModelPicking.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatPreInput.h"
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
        bAttackMoveArmed=false;
        if (ArmedEntry!=Entry) { UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_INPUT armed entry=%u"),uint32(Entry)); }
        ArmedEntry=Entry;
        // The D dash goes toward the pointer at once; without ground under the pointer the server refuses it explicitly.
        if (LDCombatPreInput::FiresOnPress(Entry))
        {
            if (PressAim) { ConfirmArmedEntry(*PressAim,AimCm); }
            else { ConfirmArmedCombat(); }
            return;
        }
        // Quick cast (local setting): fires at the pointer at once when it already names the aim. A unit skill takes the
        // nearest visible unit around the pointer; with none there it is dropped with a notice instead of staying armed
        // for a confirmation the next move click would silently cancel.
        if (Controls && Controls->GetQuickCast())
        {
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
                    CancelArmedCombat();
                    PostIntentNotice(TEXT("指针附近没有目标"));
                }
                return;
            }
            const auto* Body=Cast<ALDCharacter>(GetPawn());
            const double PlaneZ=Body?Body->GetActorLocation().Z-Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight():0.;
            FVector AimWorldPoint=FVector::ZeroVector;
            if (Navigation->AimPointAtElevation(PlaneZ,AimWorldPoint)) { ConfirmArmedCombat(); }
        }
        return;
    }
    // Self, an empty slot or gear without an active still reaches the server: the attempt is judged there
    // and an admitted aggressive intent ends protection even when it is rejected.
    FLDCombatTarget Aim;
    Aim.Kind=bHeld && Mode==ELDDemoTargetMode::Self?ELDCombatTargetKind::Self:ELDCombatTargetKind::Missing;
    SendCombatAction(Entry,Aim,IsQueueModifierDown());
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
    bAttackMoveArmed=true;
}

bool ALDPlayerController::ClearArmedAim()
{
    const bool bWasArmed=bAttackMoveArmed || ArmedEntry!=ELDCombatEntry::None;
    bAttackMoveArmed=false;
    ArmedEntry=ELDCombatEntry::None;
    return bWasArmed;
}

bool ALDPlayerController::CancelArmedCombat()
{
    // A real cancel (S, Escape, a menu, a move order, a new life or source revision) also ends a running or completed
    // overdrive hold and the visible charge with it.
    if (bOverdriveHolding || bOverdriveArmed) { EndOverdriveHold(); }
    if (ArmedEntry!=ELDCombatEntry::None)
    {
        UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_INPUT armed-cancelled entry=%u"),uint32(ArmedEntry));
        ArmedCancelEntry=ArmedEntry;
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
    }
    return ConfirmArmedEntry(Aim);
}

bool ALDPlayerController::ConfirmArmedEntry(const FLDCombatTarget& Aim, const FVector* AimCm)
{
    const ELDCombatEntry Entry=ArmedEntry;
    if (Entry==ELDCombatEntry::None) { return false; }
    ClearArmedAim();
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
    int64 NowMs=0;
    ReadDemoAuthorityTimeMs(NowMs);
    const bool bCurrentView=CombatView.Generation==OwnerState.Match && CombatView.Life==OwnerState.Life;
    if (bCurrentView && LDCombatPreInput::IsDirectEntry(Entry) && LDCombatPreInput::IsWindingUp(CombatView,NowMs))
    {
        QueuedCombat={Entry,Aim,OwnerState.Match,OwnerState.Life,FPlatformTime::Seconds(),CombatView.SourceRevision};
        UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_INPUT queued match=%u life=%u entry=%u source=%llu"),
            OwnerState.Match,OwnerState.Life,uint32(Entry),CombatView.SourceRevision);
        return;
    }
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
    return true;
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
    // A full bar and a ready learned active start the hold: the release decides between a normal cast and overdrive.
    const bool bCurrentView=CombatView.Generation==OwnerState.Match && CombatView.Life==OwnerState.Life;
    const FLDCombatEntryView* View=bCurrentView?LDPlayerOrdersDetail::FindEntry(CombatView,Entry):nullptr;
    const FLDDemoOverdriveParameters* Terms=OverdriveTerms();
    // Unavailable is the published snapshot: a cooldown or momentum reason in it can be stale, so readiness is judged on the
    // current authority time and the current bar; any other reason (control, busy) still blocks. The server decides.
    int64 NowMs=0;
    const bool bClock=ReadDemoAuthorityTimeMs(NowMs);
    const bool bStaleOnlyReason=View && (View->Unavailable==ELDCombatReject::None || View->Unavailable==ELDCombatReject::Cooldown
        || View->Unavailable==ELDCombatReject::MomentumShort);
    if (IsLocalController() && CanWorldCommand() && Terms && View && !View->DefinitionId.IsNone()
        && View->SourceKind==ELDCombatSourceKind::Learned && bStaleOnlyReason && bClock && View->ReadyAtMs<=NowMs
        && CombatView.MomentumP2>=(CombatView.OverdriveThresholdP2>0 ? CombatView.OverdriveThresholdP2 : Terms->ThresholdP2)
        && CombatView.MomentumP2>=View->MomentumCostP2 && !bOverdriveHolding)
    {
        if (bOverdriveArmed) { EndOverdriveHold(); }
        OverdriveEntry=Entry;
        OverdriveHoldStartSec=FPlatformTime::Seconds();
        bOverdriveHolding=true;
        // Alt with the key: one press is enough, the charge completes without holding the key.
        bOverdriveLatched=FSlateApplication::IsInitialized()&&FSlateApplication::Get().GetModifierKeys().IsAltDown();
        SendCharge(Entry,true);
        // An aimed skill is armed from the press itself: a left click during the hold is an ordinary cast, and only a
        // hold that completes turns the armed aim into overdrive. Quick cast and self skills fire on activation, so
        // for them the release still decides.
        if (LDPlayerOrdersDetail::IsArmedMode(ELDDemoTargetMode(View->TargetMode)) && !(Controls && Controls->GetQuickCast()))
        { ActivateCombatEntry(Entry); }
        return;
    }
    ActivateCombatEntry(Entry);
}

void ALDPlayerController::ReleaseSkill(ELDCombatEntry Entry)
{
    if (!bOverdriveHolding || Entry!=OverdriveEntry) { return; }
    // A one-press (Alt) charge keeps going after the key is let go.
    if (bOverdriveLatched) { return; }
    // Let go before the hold completed: the charge ends and the press is an ordinary cast.
    EndOverdriveHold();
    // Armed from the press already: nothing to repeat. Quick cast was not activated on the press, so it reads the aim now.
    if (ArmedEntry!=Entry || (Controls && Controls->GetQuickCast())) { ActivateCombatEntry(Entry); }
}

void ALDPlayerController::TickOverdriveHold()
{
    if (!bOverdriveHolding) { return; }
    const FLDDemoOverdriveParameters* Terms=OverdriveTerms();
    if (!Terms || !CanWorldCommand()) { EndOverdriveHold(); return; }
    if ((FPlatformTime::Seconds()-OverdriveHoldStartSec)*1000.<double(Terms->HoldMs)) { return; }
    // Held through OVERDRIVE.HoldSec: the next request of this entry asks for overdrive (the server decides by the bar).
    bOverdriveHolding=false;
    bOverdriveArmed=true;
    ActivateCombatEntry(OverdriveEntry);
}

float ALDPlayerController::OverdriveHoldProgress(ELDCombatEntry Entry) const
{
    if (Entry!=OverdriveEntry) { return 0.f; }
    if (bOverdriveArmed) { return 1.f; }
    if (!bOverdriveHolding || !OverdriveContent) { return 0.f; }
    const double HoldMs=double(OverdriveContent->Get().Parameters.Overdrive.HoldMs);
    return HoldMs>0 ? float(FMath::Clamp((FPlatformTime::Seconds()-OverdriveHoldStartSec)*1000./HoldMs,0.,1.)) : 0.f;
}

void ALDPlayerController::SendCombatNow(ELDCombatEntry Entry, const FLDCombatTarget& Aim)
{
    if (!IsLocalController()) { return; }
    FLDCombatActionRequest Request;
    if (!NextCombatRequest(Entry,Aim,Request)) { return; }
    if (auto* GI=Cast<ULDGameInstance>(GetGameInstance()))
    {
        GI->NoteCrashCommand(*FString::Printf(TEXT("CombatEntry=%u Seq=%llu"),uint32(Entry),Request.DirectSequence));
    }
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
        const FLDCombatEntryView* Was=LDPlayerOrdersDetail::FindEntry(LatestCombatView,ArmedEntry);
        const FLDCombatEntryView* Now=LDPlayerOrdersDetail::FindEntry(CombatView,ArmedEntry);
        const bool bSameSkill=ArmedEntry!=ELDCombatEntry::None && Was && Now && Was->DefinitionId==Now->DefinitionId
            && Was->TargetMode==Now->TargetMode;
        if (bNewLine || !bSameSkill) { CancelArmedCombat(); }
        if (QueuedCombat.Entry!=ELDCombatEntry::None)
        {
            QueuedCombat={};
            if (!bNewLine) { PostIntentNotice(TEXT("技能或装备已变化，预输入已取消")); }
        }
    }
    LatestCombatView=CombatView;
}
