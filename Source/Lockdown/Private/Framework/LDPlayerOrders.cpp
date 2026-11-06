#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatPreInput.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Navigation/LDNavigationComponent.h"
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
            || Mode==ELDDemoTargetMode::DirectionDash || Mode==ELDDemoTargetMode::GroundPoint;
    }
}

void ALDPlayerController::ActivateCombatEntry(ELDCombatEntry Entry)
{
    // The full world-command gate: under the menu, Settings, a child confirmation or a text edit no entry arms or
    // sends (UI 4); the root already cancelled any older preview.
    if (!IsLocalController() || !LDCombatPreInput::IsDirectEntry(Entry) || !CanWorldCommand()) { return; }
    const bool bCurrentView=CombatView.Generation==OwnerState.Match && CombatView.Life==OwnerState.Life;
    const FLDCombatEntryView* View=bCurrentView?LDPlayerOrdersDetail::FindEntry(CombatView,Entry):nullptr;
    const bool bHeld=View && !View->DefinitionId.IsNone() && View->bHasActive;
    const ELDDemoTargetMode Mode=bHeld?ELDDemoTargetMode(View->TargetMode):ELDDemoTargetMode::Self;
    if (bHeld && LDPlayerOrdersDetail::IsArmedMode(Mode))
    {
        bAttackMoveArmed=false;
        ArmedEntry=Entry;
        // The D dash goes toward the pointer at once; without ground under the pointer the server refuses it explicitly.
        if (LDCombatPreInput::FiresOnPress(Entry))
        {
            ConfirmArmedCombat();
            return;
        }
        // Quick cast (local setting): fires at the pointer at once when it already names the aim; a unit skill without
        // a visible unit under the pointer stays armed for the ordinary left-click confirmation.
        if (Controls && Controls->GetQuickCast())
        {
            FGuid Handle;
            uint32 Epoch=0;
            FHitResult GroundHit;
            const bool bAimReady=Mode==ELDDemoTargetMode::Unit ? ObservedUnderPointer(Handle,Epoch)
                : Navigation->GroundUnderPointer(GroundHit);
            if (bAimReady) { ConfirmArmedCombat(); }
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

bool ALDPlayerController::CancelArmedCombat()
{
    const bool bWasArmed=bAttackMoveArmed || ArmedEntry!=ELDCombatEntry::None;
    bAttackMoveArmed=false;
    ArmedEntry=ELDCombatEntry::None;
    return bWasArmed;
}

bool ALDPlayerController::ConfirmArmedCombat()
{
    if (bAttackMoveArmed)
    {
        CancelArmedCombat();
        if (!IsLocalController() || !CanWorldCommand()) { return true; }
        FHitResult GroundHit;
        const bool bQueue=IsQueueModifierDown();
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
    else if (Mode==ELDDemoTargetMode::DirectionProjectile || Mode==ELDDemoTargetMode::DirectionDash)
    {
        FHitResult GroundHit;
        const auto* Body=Cast<ALDCharacter>(GetPawn());
        if (Body && Navigation->GroundUnderPointer(GroundHit))
        {
            const FVector Foot=Body->GetActorLocation()-FVector(0,0,Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            const FVector Direction=FVector(GroundHit.ImpactPoint.X-Foot.X,GroundHit.ImpactPoint.Y-Foot.Y,0).GetSafeNormal();
            if (!Direction.IsZero())
            {
                Aim.Kind=ELDCombatTargetKind::Direction;
                Aim.Direction=Direction;
                // A queued direction keeps its ground point: the server aims from the body when the step's turn comes.
                const FVector Point=GroundHit.ImpactPoint;
                return ConfirmArmedEntry(Aim,&Point);
            }
        }
    }
    else if (Mode==ELDDemoTargetMode::GroundPoint)
    {
        // The requested ground point; the server clamps it to the reach and judges the path.
        FHitResult GroundHit;
        if (Navigation->GroundUnderPointer(GroundHit))
        {
            Aim.Kind=ELDCombatTargetKind::Point;
            Aim.Point=GroundHit.ImpactPoint;
        }
    }
    return ConfirmArmedEntry(Aim);
}

bool ALDPlayerController::ConfirmArmedEntry(const FLDCombatTarget& Aim, const FVector* AimCm)
{
    const ELDCombatEntry Entry=ArmedEntry;
    if (Entry==ELDCombatEntry::None) { return false; }
    CancelArmedCombat();
    if (!IsLocalController() || !CanWorldCommand()) { return true; }
    SendCombatAction(Entry,Aim,IsQueueModifierDown(),AimCm);
    return true;
}

void ALDPlayerController::StopCombatAndOrders()
{
    // S is a new world command: the menu, Settings, a child confirmation, a text edit or an unreleased key keeps it
    // from the world, so it never stops the accepted order those leave running (UI 4, 7).
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

bool ALDPlayerController::ReadShopInteraction(FName& OutShopId, uint32& OutLife) const
{
    OutShopId=NAME_None;
    OutLife=0;
    return Navigation && Navigation->ReadShopInteraction(OutShopId,OutLife);
}

void ALDPlayerController::PostIntentNotice(const FString& Text)
{
    if (Text.IsEmpty() || !IsLocalController()) { return; }
    if (IntentFeedbackSerial==MAX_uint32)
    {
        UE_LOG(LogTemp,Error,TEXT("LD_INTENT_FEEDBACK_EXHAUSTED"));
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
        UE_LOG(LogTemp,Display,TEXT("LD_COMBAT_INPUT cast-order match=%u life=%u sequence=%llu entry=%u queue=%d"),
            Request.Generation,Request.Life,Request.DirectSequence,uint32(Entry),bQueue?1:0);
        Navigation->CastAtUnit(Request,bQueue);
        return;
    }
    if (bQueue)
    {
        // The shift queue waits for its own turn on the server, so a queued press is never held as pre-input; it
        // replaces a held one like any newer press.
        QueuedCombat={};
        FLDCombatActionRequest Request;
        if (!NextCombatRequest(Entry,Aim,Request)) { return; }
        UE_LOG(LogTemp,Display,TEXT("LD_COMBAT_INPUT queued-step match=%u life=%u sequence=%llu entry=%u target=%u"),
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
        UE_LOG(LogTemp,Display,TEXT("LD_COMBAT_INPUT queued match=%u life=%u entry=%u source=%llu"),
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
    return true;
}

void ALDPlayerController::SendCombatNow(ELDCombatEntry Entry, const FLDCombatTarget& Aim)
{
    if (!IsLocalController()) { return; }
    FLDCombatActionRequest Request;
    if (!NextCombatRequest(Entry,Aim,Request)) { return; }
    UE_LOG(LogTemp,Display,TEXT("LD_COMBAT_INPUT sent match=%u life=%u sequence=%llu entry=%u target=%u source=%llu"),
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
        UE_LOG(LogTemp,Warning,TEXT("LD_COMBAT_CALLER_REJECTED %s"),*Error);
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
    const FLDCombatActionReceipt Receipt=Mode->Combat->SubmitAction(Caller,Incoming);
    // A non-Shift Q/W/1/2/D Combat took is a new command: the shift queue still waiting behind the current order is
    // dropped (its lower sequences could never run after this one). The current order keeps running as before.
    if (Receipt.Phase!=ELDCombatPhase::Rejected && Navigation) { Navigation->ClearQueue(ELDOrderFailure::Superseded); }
    ClientCombatReceipt(Receipt);
}

void ALDPlayerController::ServerCancelCombatAction_Implementation(const FLDCombatCancelRequest& Request)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    FLDCombatCaller Caller;
    FString Error;
    if (!Mode || !Mode->Combat || !Mode->Combat->ResolveControllerCaller(*this,Caller,Error))
    {
        UE_LOG(LogTemp,Warning,TEXT("LD_COMBAT_CALLER_REJECTED %s"),*Error);
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

// Only records the receipt: the HUD alone presents combat receipts (LDHUD::CombatReceiptNotice).
void ALDPlayerController::ClientCombatReceipt_Implementation(const FLDCombatActionReceipt& Receipt)
{
    if (Receipt.Generation!=OwnerState.Match) { return; }
    UE_LOG(LogTemp,Display,TEXT("LD_COMBAT_INPUT receipt match=%u life=%u sequence=%llu entry=%u phase=%u reject=%u token=%llu"),
        Receipt.Generation,Receipt.Life,Receipt.DirectSequence,uint32(Receipt.Entry),uint32(Receipt.Phase),
        uint32(Receipt.Reject),Receipt.CombatToken);
    if (!CombatReceipts.Push(Receipt)) { UE_LOG(LogTemp,Error,TEXT("LD_COMBAT_RECEIPT_SERIAL_EXHAUSTED")); }
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
    // An armed preview belongs to the life and the source revision it was armed against.
    if (bNewLine || CombatView.SourceRevision!=LatestCombatView.SourceRevision)
    {
        CancelArmedCombat();
        if (QueuedCombat.Entry!=ELDCombatEntry::None)
        {
            QueuedCombat={};
            if (!bNewLine) { PostIntentNotice(TEXT("技能或装备已变化，预输入已取消")); }
        }
    }
    LatestCombatView=CombatView;
}
