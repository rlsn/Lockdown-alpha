#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Navigation/LDNavigationComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

namespace LDPlayerOrdersDetail
{
    const FLDCombatEntryView* FindEntry(const FLDCombatOwnerView& View, ELDCombatEntry Entry)
    { return View.Entries.FindByPredicate([Entry](const FLDCombatEntryView& Row) { return Row.Entry==Entry; }); }

    bool IsDirectEntry(ELDCombatEntry Entry)
    { return Entry==ELDCombatEntry::Q || Entry==ELDCombatEntry::W || Entry==ELDCombatEntry::Item1 || Entry==ELDCombatEntry::Item2; }

    bool IsArmedMode(ELDDemoTargetMode Mode)
    {
        return Mode==ELDDemoTargetMode::Unit || Mode==ELDDemoTargetMode::DirectionProjectile
            || Mode==ELDDemoTargetMode::DirectionDash;
    }
}

void ALDPlayerController::ActivateCombatEntry(ELDCombatEntry Entry)
{
    // The full world-command gate: under the menu, Settings, a child confirmation or a text edit no entry arms or
    // sends (UI 4); the root already cancelled any older preview.
    if (!IsLocalController() || !LDPlayerOrdersDetail::IsDirectEntry(Entry) || !CanWorldCommand()) { return; }
    const bool bCurrentView=CombatView.Generation==OwnerState.Match && CombatView.Life==OwnerState.Life;
    const FLDCombatEntryView* View=bCurrentView?LDPlayerOrdersDetail::FindEntry(CombatView,Entry):nullptr;
    const bool bHeld=View && !View->DefinitionId.IsNone() && View->bHasActive;
    const ELDDemoTargetMode Mode=bHeld?ELDDemoTargetMode(View->TargetMode):ELDDemoTargetMode::Self;
    if (bHeld && LDPlayerOrdersDetail::IsArmedMode(Mode))
    {
        bAttackMoveArmed=false;
        ArmedEntry=Entry;
        return;
    }
    // Self, an empty slot or gear without an active still reaches the server: the attempt is judged there
    // and an admitted aggressive intent ends protection even when it is rejected.
    FLDCombatTarget Aim;
    Aim.Kind=bHeld && Mode==ELDDemoTargetMode::Self?ELDCombatTargetKind::Self:ELDCombatTargetKind::Missing;
    SendCombatAction(Entry,Aim);
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
        // A confirmation without ground is still a normalized aggressive intent; the server rejects it explicitly.
        if (GetHitResultUnderCursor(ECC_GameTraceChannel1,false,GroundHit))
        { Navigation->AttackMove(GroundHit.ImpactPoint,Navigation->GroundSurfaceHint()); }
        else { Navigation->AttackMove(FVector::ZeroVector,NAME_None); }
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
        if (Body && GetHitResultUnderCursor(ECC_GameTraceChannel1,false,GroundHit))
        {
            const FVector Foot=Body->GetActorLocation()-FVector(0,0,Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            const FVector Direction=FVector(GroundHit.ImpactPoint.X-Foot.X,GroundHit.ImpactPoint.Y-Foot.Y,0).GetSafeNormal();
            if (!Direction.IsZero())
            {
                Aim.Kind=ELDCombatTargetKind::Direction;
                Aim.Direction=Direction;
            }
        }
    }
    return ConfirmArmedEntry(Aim);
}

bool ALDPlayerController::ConfirmArmedEntry(const FLDCombatTarget& Aim)
{
    const ELDCombatEntry Entry=ArmedEntry;
    if (Entry==ELDCombatEntry::None) { return false; }
    CancelArmedCombat();
    if (!IsLocalController() || !CanWorldCommand()) { return true; }
    SendCombatAction(Entry,Aim);
    return true;
}

void ALDPlayerController::StopCombatAndOrders()
{
    // S is a new world command: the menu, Settings, a child confirmation, a text edit or an unreleased key keeps it
    // from the world, so it never stops the accepted order those leave running (UI 4, 7).
    if (IsWorldInputSuppressed()) { return; }
    CancelArmedCombat();
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

void ALDPlayerController::SendCombatAction(ELDCombatEntry Entry, const FLDCombatTarget& Aim)
{
    if (!IsLocalController()) { return; }
    if (NextDirectSequence==MAX_uint64)
    {
        IntentFeedback=TEXT("技能请求编号已用尽，无法再发送");
        return;
    }
    FLDCombatActionRequest Request;
    Request.Generation=OwnerState.Match;
    Request.Life=OwnerState.Life;
    Request.DirectSequence=++NextDirectSequence;
    Request.Entry=Entry;
    Request.Target=Aim;
    Request.Target.ServerBody={};
    Request.ExpectedSourceRevision=CombatView.SourceRevision;
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
    ClientCombatReceipt(Mode->Combat->SubmitAction(Caller,Incoming));
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
    LatestCombatReceipt=Receipt;
    if (CombatReceiptSerial<MAX_uint32) { ++CombatReceiptSerial; }
    else { UE_LOG(LogTemp,Error,TEXT("LD_COMBAT_RECEIPT_SERIAL_EXHAUSTED")); }
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
    if (bNewLine || CombatView.SourceRevision!=LatestCombatView.SourceRevision) { CancelArmedCombat(); }
    LatestCombatView=CombatView;
}
