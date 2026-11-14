#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDGameInstance.h"
#include "Core/LDLog.h"
#include "Framework/LDDemoEntry.h"
#include "Engine/World.h"
#include "Engine/NetConnection.h"
#include "Navigation/LDNavigationComponent.h"

void ALDPlayerController::SetDemoReady(bool Ready)
{
    SendDemoSessionCommand(ELDSessionCommand::SetReady, Ready);
}

void ALDPlayerController::SetDemoAICount(uint8 Count)
{
    SendDemoSessionCommand(ELDSessionCommand::SetAICount, false, Count);
}

void ALDPlayerController::StartDemoMatch()
{
    SendDemoSessionCommand(ELDSessionCommand::StartMatch);
}

void ALDPlayerController::EndDemoMatch()
{
    SendDemoSessionCommand(ELDSessionCommand::EndMatch);
}

void ALDPlayerController::ReturnDemoToLobby()
{
    const auto* Public = GetWorld()->GetGameState<ALDGameState>();
    if (!Public || !Public->MatchResult.ResultId.IsValid() || Public->MatchResult.Generation != Public->DemoLobby.Generation)
    {
        IntentFeedback = TEXT("没有已锁定的对局结果，无法带房间返回 Lobby。");
        ++IntentFeedbackSerial;
        return;
    }

    SendDemoSessionCommand(ELDSessionCommand::ReturnToLobby, false, 0, Public->MatchResult.ResultId);
}

void ALDPlayerController::LeaveDemoRoom()
{
    if(auto* Instance=GetGameInstance<ULDGameInstance>()){Instance->ClearReconnect();}
    ClearControls();
    ServerLeaveDemoRoom();
}

void ALDPlayerController::ServerLeaveDemoRoom_Implementation()
{
    if(auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>()){Mode->LeaveRoomMember(*this);}
}

void ALDPlayerController::ClientReconnectTicket_Implementation(const FGuid& Ticket)
{
    if(auto* Instance=GetGameInstance<ULDGameInstance>())
    {
        if(GetWorld()->GetNetMode()==NM_Client)
        {
            if(const UNetConnection* Connection=GetNetConnection()){Instance->RememberRoomURL(Connection->URL.ToString());}
        }
        Instance->SetReconnectTicket(Ticket);
    }
}

void ALDPlayerController::ClientReconnectChallenge_Implementation()
{
    const auto* Instance=GetGameInstance<ULDGameInstance>();
    ServerReconnectRoom(Instance?Instance->GetReconnectTicket():FGuid());
}

void ALDPlayerController::ServerReconnectRoom_Implementation(const FGuid& Ticket)
{
    if(auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>())
    {FString Error;Mode->ResumeRoomMember(*this,Ticket,Error);}
}

void ALDPlayerController::ClientRestoreRequestCursors_Implementation(uint32 Generation,uint64 BuildNext,uint64 BuildPending,
    uint64 DevNext,uint64 DevPending,uint64 DirectThrough,uint64 SessionThrough,uint32 MoveOrder,uint32 MoveEpoch)
{
    if(!Generation||!BuildNext||BuildNext==MAX_uint64||!DevNext||DevNext==MAX_uint64
        ||(BuildPending&&BuildPending!=BuildNext)||(DevPending&&DevPending!=DevNext)
        ||DirectThrough==MAX_uint64||SessionThrough==MAX_uint64)
    {
        UE_LOG(LogLDMatch,Error,TEXT("LD_RECONNECT_CURSOR_INVALID"));
        if(auto* Instance=GetGameInstance<ULDGameInstance>())
        {Instance->ReturnAfterRoomClosed(GetWorld(),TEXT("恢复失败：服务器请求编号无效。"));}
        return;
    }
    BuildCursor={Generation,BuildNext,BuildPending};
    DevTestCursor={Generation,DevNext,DevPending};
    NextDirectSequence=DirectThrough;
    DemoRequestGeneration=Generation;DemoRequestSequence=SessionThrough;DemoPendingSequence=0;
    Navigation->RestoreLocalOrderCursor(MoveOrder,MoveEpoch);
    if(BuildPending)
    {
        FLDReceiptQuery Query;
        Query.Generation=Generation;Query.Family=ELDGrowthFamily::Build;Query.RequestId=BuildPending;
        ServerQueryGrowthReceipt(Query);
    }
    PostIntentNotice(TEXT("已恢复本局角色与构筑。"));
}

void ALDPlayerController::SendDemoSessionCommand(ELDSessionCommand Command, bool Ready, uint8 AICount, FGuid ExpectedResult)
{
    const auto* Public = GetWorld()->GetGameState<ALDGameState>();
    if (!Public || !Public->DemoLobby.Generation)
    {
        IntentFeedback = TEXT("房间尚未加载完成。");
        return;
    }
    if (DemoRequestGeneration != Public->DemoLobby.Generation)
    {
        DemoRequestGeneration = Public->DemoLobby.Generation;
        DemoRequestSequence = DemoPendingSequence = 0;
    }
    if (DemoPendingSequence)
    {
        IntentFeedback = TEXT("请等待上一个房间命令完成。");
        return;
    }
    if (DemoRequestSequence == MAX_uint64)
    {
        IntentFeedback = TEXT("房间命令编号已用尽，请离开并重新创建房间。");
        return;
    }
    FLDSessionRequest Request;
    Request.Key.Generation = DemoRequestGeneration;
    Request.Key.RequestSequence = ++DemoRequestSequence;
    Request.Command = Command;
    Request.ExpectedRosterRevision = Public->DemoLobby.RosterRevision;
    Request.Ready = Ready;
    Request.AICount = AICount;
    Request.ExpectedResult = ExpectedResult;
    DemoPendingSequence = Request.Key.RequestSequence;
    IntentFeedback = TEXT("房间命令处理中…");
    ServerDemoSessionCommand(Request);
}

void ALDPlayerController::ServerDemoSessionCommand_Implementation(const FLDSessionRequest& Request)
{
    FLDSessionReceipt Receipt;
    if (auto* Mode = GetWorld()->GetAuthGameMode<ALDGameMode>())
    {
        Receipt = Mode->ProcessSessionCommand(this, Request);
    }
    else
    {
        Receipt.Key = Request.Key;
        Receipt.Reason = ELDSessionReject::NotOwner;
        Receipt.Detail = TEXT("没有可用的权威房间。");
    }
    ClientDemoSessionReceipt(Receipt);
}

void ALDPlayerController::ClientDemoSessionReceipt_Implementation(const FLDSessionReceipt& Receipt)
{
    if (Receipt.Key.Generation != DemoRequestGeneration || Receipt.Key.RequestSequence != DemoPendingSequence) { return; }
    if (Receipt.Outcome == ELDSessionOutcome::Pending)
    {
        IntentFeedback = TEXT("房间命令处理中…");
        return;
    }
    DemoPendingSequence = 0;
    IntentFeedback = Receipt.Detail;
    ++IntentFeedbackSerial;
}

void ALDPlayerController::ClientDemoRoomClosed_Implementation(uint32 Generation, const FString& Reason)
{
    const auto* Public = GetWorld()->GetGameState<ALDGameState>();
    if (!Generation || (OwnerState.Match != Generation && (!Public || Public->DemoLobby.Generation != Generation))) { return; }
    DemoPendingSequence = 0;
    ClearControls();
    // The Host's closing connection follows this notice; the room's first reason is the one shown.
    if (auto* Instance = GetGameInstance<ULDGameInstance>()) { Instance->ClearReconnect();Instance->ReturnAfterRoomClosed(GetWorld(), Reason); }
    else { ClientTravel(LDDemoEntry::FrontendMap, TRAVEL_Absolute); }
}

void ALDPlayerController::ClientWasKicked_Implementation(const FText& KickReason)
{
    // Its PreLogin passed, but the room changed while it loaded; the coded refusal is its reason, not a lost session.
    DemoPendingSequence = 0;
    ClearControls();
    if (auto* Instance = GetGameInstance<ULDGameInstance>()) { Instance->ClearReconnect();Instance->ReturnAfterRefusal(GetWorld(), KickReason.ToString()); }
}
