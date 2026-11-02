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

    const TCHAR* RejectText(ELDCombatReject Reason)
    {
        switch (Reason)
        {
        case ELDCombatReject::InvalidRequest: return TEXT("invalid request");
        case ELDCombatReject::NotOwner: return TEXT("not the current owner");
        case ELDCombatReject::StaleMatch: return TEXT("another match");
        case ELDCombatReject::StaleLife: return TEXT("another life");
        case ELDCombatReject::StaleRequest: return TEXT("stale request");
        case ELDCombatReject::RequestConflict: return TEXT("request conflict");
        case ELDCombatReject::ReceiptExpired: return TEXT("receipt expired");
        case ELDCombatReject::Cancelled: return TEXT("cancelled");
        case ELDCombatReject::StaleSource: return TEXT("the source changed; prepare it again");
        case ELDCombatReject::SourceUnavailable: return TEXT("empty slot");
        case ELDCombatReject::NoActiveEffect: return TEXT("no active effect");
        case ELDCombatReject::Busy: return TEXT("busy");
        case ELDCombatReject::Cooldown: return TEXT("on cooldown");
        case ELDCombatReject::AttackNotReady: return TEXT("attack not ready");
        case ELDCombatReject::InsufficientMana: return TEXT("not enough mana");
        case ELDCombatReject::ControlBlocked: return TEXT("blocked by a control effect");
        case ELDCombatReject::TargetUnavailable: return TEXT("target unavailable");
        case ELDCombatReject::InvalidDirection: return TEXT("invalid direction");
        case ELDCombatReject::OutOfRange: return TEXT("out of range");
        case ELDCombatReject::Blocked: return TEXT("blocked");
        case ELDCombatReject::Dead: return TEXT("dead");
        case ELDCombatReject::MatchClosed: return TEXT("match closed");
        case ELDCombatReject::AuthorityDataUnavailable: return TEXT("server data unavailable");
        case ELDCombatReject::CapacityUnavailable: return TEXT("server capacity unavailable");
        default: return TEXT("rejected");
        }
    }
}

void ALDPlayerController::ActivateCombatEntry(ELDCombatEntry Entry)
{
    if (!IsLocalController() || !LDPlayerOrdersDetail::IsDirectEntry(Entry) || !CanWorldCommand()
        || OwnerState.LifePhase!=ELDLifePhase::Alive) { return; }
    const bool bCurrentView=CombatView.Generation==OwnerState.Match && CombatView.Life==OwnerState.Life;
    const FLDCombatEntryView* View=bCurrentView?LDPlayerOrdersDetail::FindEntry(CombatView,Entry):nullptr;
    const bool bHeld=View && !View->DefinitionId.IsNone() && View->bHasActive;
    const ELDDemoTargetMode Mode=bHeld?ELDDemoTargetMode(View->TargetMode):ELDDemoTargetMode::Self;
    if (bHeld && LDPlayerOrdersDetail::IsArmedMode(Mode))
    {
        bAttackMoveArmed=false;
        ArmedEntry=Entry;
        IntentFeedback=Mode==ELDDemoTargetMode::Unit?TEXT("Left-click a visible unit"):TEXT("Left-click to aim");
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
    IntentFeedback=TEXT("Attack-move: left-click a destination");
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
    const bool bAttackMove=bAttackMoveArmed;
    const ELDCombatEntry Entry=ArmedEntry;
    if (!CancelArmedCombat()) { return false; }
    if (!IsLocalController() || !CanWorldCommand()) { return true; }
    FHitResult GroundHit;
    const bool bGround=GetHitResultUnderCursor(ECC_GameTraceChannel1,false,GroundHit);
    if (bAttackMove)
    {
        // A confirmation without ground is still a normalized aggressive intent; the server rejects it explicitly.
        if (bGround) { Navigation->AttackMove(GroundHit.ImpactPoint,Navigation->GroundSurfaceHint()); }
        else { Navigation->AttackMove(FVector::ZeroVector,NAME_None); }
        return true;
    }
    const bool bCurrentView=CombatView.Generation==OwnerState.Match && CombatView.Life==OwnerState.Life;
    const FLDCombatEntryView* View=bCurrentView?LDPlayerOrdersDetail::FindEntry(CombatView,Entry):nullptr;
    const ELDDemoTargetMode Mode=View?ELDDemoTargetMode(View->TargetMode):ELDDemoTargetMode::Self;
    FLDCombatTarget Aim;
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
        const auto* Body=Cast<ALDCharacter>(GetPawn());
        if (Body && bGround)
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
    SendCombatAction(Entry,Aim);
    return true;
}

void ALDPlayerController::StopCombatAndOrders()
{
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

void ALDPlayerController::SendCombatAction(ELDCombatEntry Entry, const FLDCombatTarget& Aim)
{
    if (!IsLocalController()) { return; }
    if (NextDirectSequence==MAX_uint64)
    {
        IntentFeedback=TEXT("Combat request sequence exhausted; no further action can be sent.");
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

void ALDPlayerController::ClientCombatReceipt_Implementation(const FLDCombatActionReceipt& Receipt)
{
    if (Receipt.Generation!=OwnerState.Match) { return; }
    LatestCombatReceipt=Receipt;
    if (CombatReceiptSerial<MAX_uint32) { ++CombatReceiptSerial; }
    else { UE_LOG(LogTemp,Error,TEXT("LD_COMBAT_RECEIPT_SERIAL_EXHAUSTED")); }
    FString Text;
    if (Receipt.Phase==ELDCombatPhase::Rejected)
    { Text=FString(TEXT("Action rejected: "))+LDPlayerOrdersDetail::RejectText(Receipt.Reject); }
    if (Receipt.bProtectionRemoved) { Text+=Text.IsEmpty()?TEXT("Protection removed"):TEXT(" (protection removed)"); }
    if (Text.IsEmpty()) { return; }
    IntentFeedback=Text;
    if (IntentFeedbackSerial<MAX_uint32) { ++IntentFeedbackSerial; }
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
