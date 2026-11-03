#include "Framework/LDGameMode.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDPlayerController.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"

namespace
{
    FString RejectionText(ELDSessionReject Reason)
    {
        switch (Reason)
        {
        case ELDSessionReject::NotOwner: return TEXT("这个连接不是房间成员。");
        case ELDSessionReject::NotHost: return TEXT("只有房主可以调整 AI 对手、开始或结束本局、带房间返回 Lobby。");
        case ELDSessionReject::WrongPhase: return TEXT("当前阶段不能执行这个房间命令。");
        case ELDSessionReject::RoomFull: return TEXT("房间已有 3 名真人（含房主）。");
        case ELDSessionReject::NotAllReady: return TEXT("所有在线真人（含房主）都准备后才能开始对局。");
        case ELDSessionReject::StaleRoster: return TEXT("房间名单已变化，请确认后重新准备。");
        case ELDSessionReject::StaleMatch: return TEXT("这个请求已不属于当前对局。");
        case ELDSessionReject::RequestConflict: return TEXT("这个请求编号已用于不同的内容。");
        case ELDSessionReject::RequestInFlight: return TEXT("请等待上一个房间命令完成。");
        case ELDSessionReject::ReceiptExpired: return TEXT("这个请求早于已保留的回执，不会再次执行。");
        case ELDSessionReject::MatchClosed: return TEXT("本局结果已锁定。");
        case ELDSessionReject::SpawnBlocked: return TEXT("开局出生位置无法放置，房间已回到 Lobby。");
        case ELDSessionReject::MissingDemoDefinition: return TEXT("当前版本无法开始 Demo 对局，房间已回到 Lobby。");
        case ELDSessionReject::AuthorityFault: return TEXT("房间因权威错误已停止，请离开对局返回主菜单。");
        case ELDSessionReject::None: return TEXT("");
        default: return TEXT("无效的房间命令。");
        }
    }
}

bool ALDGameMode::IsGameplayWriteOpen(const FLDMatchContext& Context) const
{
    return IsDemoRoom() && Context.IsValid() && Context == MatchContext
        && (MatchPhase == ELDMatchPhase::Development || MatchPhase == ELDMatchPhase::CaptureOpen);
}

bool ALDGameMode::AdvanceRoster(FString& Reason)
{
    if (RosterRevision == MAX_uint64 || LobbyPublicRevision == MAX_uint64)
    {
        MatchPhase = ELDMatchPhase::Faulted;
        Reason = TEXT("房间版本号已用尽，请返回主菜单后重新创建房间。");
        UE_LOG(LogTemp, Error, TEXT("LD_ROOM_REVISION_EXHAUSTED"));
        return false;
    }
    ++RosterRevision;
    for (auto& Pair : Contenders)
    {
        Pair.Value.State.bReady = false;
        Pair.Value.ReadyRosterRevision = 0;
    }
    return true;
}

bool ALDGameMode::AdmitRoomMember(ALDPlayerController* Controller, FString& Reason)
{
    Reason.Reset();
    if (!Controller || !IsDemoRoom() || !MatchContext.IsValid())
    {
        Reason = TEXT("The Demo room has not completed entry validation.");
        return false;
    }
    if (MatchPhase != ELDMatchPhase::Lobby)
    {
        Reason = TEXT("The match has already started. Mid-match joining and reconnecting are not supported.");
        return false;
    }
    if (Contenders.Contains(Controller)) { return true; }
    if (Contenders.Num() >= 3)
    {
        Reason = RejectionText(ELDSessionReject::RoomFull);
        return false;
    }
    if (!LobbyHost.IsValid() && !Controller->IsLocalController())
    {
        Reason = TEXT("The Listen Host has not joined the room yet.");
        return false;
    }
    if (!AdvanceRoster(Reason)) { return false; }
    if (!LobbyHost.IsValid()) { LobbyHost = Controller; }
    FLDContenderRecord Record;
    Record.Session = FGuid::NewGuid();
    Record.Name = Controller->PlayerState ? Controller->PlayerState->GetPlayerName().Left(64) : TEXT("玩家");
    Record.State.Match = MatchContext.Generation;
    Record.State.Life = 0;
    Record.State.bActive = false;
    Record.State.bLobbyHost = LobbyHost == Controller;
    Record.State.Hp = Record.State.MaxHp = 0;
    Record.State.Mp = Record.State.MaxMp = 0;
    RosterOrder.Add(Record.Session);
    Lives.Add(Record.Session);
    Contenders.Add(Controller, MoveTemp(Record));
    return true;
}

bool ALDGameMode::CheckDemoStartInputs(FString& Reason) const
{
    if (!LDDemoEntry::VerifyRuntimeFiles(Reason) || !LDDemoEntry::VerifyWorldPackages(Reason)) { return false; }
    Reason = StartGateBlocker();
    return Reason.IsEmpty();
}

FLDLobbyView ALDGameMode::ReadLobby() const
{
    FLDLobbyView View;
    View.Generation = MatchContext.Generation;
    View.Phase = MatchPhase;
    View.RosterRevision = RosterRevision;
    View.PublicRevision = LobbyPublicRevision;
    View.AICount = uint8(LobbyAISlots.Num());
    bool AllReady = !Contenders.IsEmpty();
    for (const FGuid& Id : RosterOrder)
    {
        for (const auto& Pair : Contenders)
        {
            const FLDContenderRecord& Record = Pair.Value;
            // AI records exist only during a match; the AI slots below list them once.
            if (Record.Session != Id || Record.Kind != ELDContenderKind::Human || !IsMemberPresent(Id)) { continue; }
            FLDLobbyMember Member;
            Member.ContenderId = Id;
            Member.Name = Record.Name;
            Member.Kind = Record.Kind;
            Member.Host = Pair.Key == LobbyHost;
            Member.Ready = Record.State.bReady && Record.ReadyRosterRevision == RosterRevision;
            AllReady &= Member.Ready;
            View.Members.Add(MoveTemp(Member));
        }
    }
    for (int32 Index = 0; Index < LobbyAISlots.Num(); ++Index)
    {
        FLDLobbyMember Member;
        Member.ContenderId = LobbyAISlots[Index];
        Member.Name = FString::Printf(TEXT("AI 对手 %d"), Index + 1);
        Member.Kind = ELDContenderKind::OpponentAI;
        View.Members.Add(MoveTemp(Member));
    }
    View.StartBlocker = !EntryFailure.IsEmpty() ? EntryFailure.Left(512) : StartGateBlocker();
    // A faulted room publishes the generic stop reason; the diagnostic stays in the authority log.
    if (MatchPhase == ELDMatchPhase::Faulted) { View.StartBlocker = RejectionText(ELDSessionReject::AuthorityFault); }
    else if (MatchPhase != ELDMatchPhase::Lobby) { View.StartBlocker = RejectionText(ELDSessionReject::WrongPhase); }
    else if (!AllReady) { View.StartBlocker = RejectionText(ELDSessionReject::NotAllReady); }
    View.CanStart = View.StartBlocker.IsEmpty() && LobbyHost.IsValid();
    return View;
}

void ALDGameMode::PublishLobby()
{
    if (LobbyPublicRevision == MAX_uint64)
    {
        MatchPhase = ELDMatchPhase::Faulted;
        UE_LOG(LogTemp, Error, TEXT("LD_LOBBY_PUBLIC_REVISION_EXHAUSTED"));
    }
    else { ++LobbyPublicRevision; }
    if (auto* Public = GetGameState<ALDGameState>())
    {
        Public->DemoLobby = ReadLobby();
        Public->OnRep_DemoLobby();
        Public->ForceNetUpdate();
    }
    for (const auto& Pair : Contenders)
    {
        if (auto* Controller = Pair.Key.Get()) { SyncOwner(Controller); }
    }
}

FLDSessionReceipt ALDGameMode::ProcessSessionCommand(ALDPlayerController* Controller, const FLDSessionRequest& Request)
{
    FLDSessionReceipt Receipt;
    Receipt.Key = Request.Key;
    Receipt.Phase = MatchPhase;
    Receipt.RosterRevision = RosterRevision;
    auto* Record = Contenders.Find(Controller);
    if (!Controller || !Record || Record->Kind != ELDContenderKind::Human || !IsMemberPresent(Record->Session))
    {
        Receipt.Reason = ELDSessionReject::NotOwner;
    }
    else if (!MatchContext.IsValid() || Request.Key.Generation != MatchContext.Generation)
    {
        Receipt.Reason = ELDSessionReject::StaleMatch;
    }
    else if (!Request.HasValidShape()) { Receipt.Reason = ELDSessionReject::InvalidRequest; }
    else
    {
        if (Record->SessionRequests.Begin(Request, Receipt) == ELDSessionWindowResult::ExistingResult)
        {
            if (Receipt.Detail.IsEmpty()) { Receipt.Detail = RejectionText(Receipt.Reason); }
            return Receipt;
        }
        bool Changed = false;
        bool PublishOutcome = false;
        const bool LobbyCommand = Request.Command == ELDSessionCommand::SetReady
            || Request.Command == ELDSessionCommand::SetAICount || Request.Command == ELDSessionCommand::StartMatch;
        if (!IsDemoRoom()) { Receipt.Reason = ELDSessionReject::WrongPhase; }
        else if (Request.ExpectedRosterRevision != RosterRevision) { Receipt.Reason = ELDSessionReject::StaleRoster; }
        else if (LobbyPublicRevision == MAX_uint64)
        {
            MatchPhase = ELDMatchPhase::Faulted;
            Receipt.Reason = ELDSessionReject::AuthorityFault;
        }
        else if (Request.Command != ELDSessionCommand::SetReady && LobbyHost != Controller) { Receipt.Reason = ELDSessionReject::NotHost; }
        else if (LobbyCommand && MatchPhase != ELDMatchPhase::Lobby) { Receipt.Reason = ELDSessionReject::WrongPhase; }
        else if (Request.Command == ELDSessionCommand::SetReady)
        {
            Changed = Record->State.bReady != Request.Ready || Record->ReadyRosterRevision != RosterRevision;
            Record->State.bReady = Request.Ready;
            Record->ReadyRosterRevision = RosterRevision;
            Receipt.Outcome = ELDSessionOutcome::Committed;
            Receipt.Reason = ELDSessionReject::None;
            Receipt.Detail = Request.Ready ? TEXT("已准备。") : TEXT("已取消准备。");
        }
        else if (Request.Command == ELDSessionCommand::SetAICount)
        {
            Changed = Request.AICount != LobbyAISlots.Num();
            if (!Changed || AdvanceRoster(Receipt.Detail))
            {
                while (LobbyAISlots.Num() > Request.AICount) { LobbyAISlots.Pop(); }
                while (LobbyAISlots.Num() < Request.AICount) { LobbyAISlots.Add(FGuid::NewGuid()); }
                Receipt.Outcome = ELDSessionOutcome::Committed;
                Receipt.Reason = ELDSessionReject::None;
                Receipt.Detail = Changed ? TEXT("AI 对手数量已更新，所有真人需要重新准备。") : TEXT("AI 对手数量未变化。");
            }
            else { Receipt.Reason = ELDSessionReject::AuthorityFault; }
        }
        else if (Request.Command == ELDSessionCommand::StartMatch)
        {
            bool AllReady = !Contenders.IsEmpty() && Contenders.Num() <= 3;
            for (const auto& Pair : Contenders)
            {
                AllReady &= Pair.Value.State.bReady && Pair.Value.ReadyRosterRevision == RosterRevision;
            }
            if (!AllReady) { Receipt.Reason = ELDSessionReject::NotAllReady; }
            else if (!CheckDemoStartInputs(Receipt.Detail)) { Receipt.Reason = ELDSessionReject::MissingDemoDefinition; }
            else if (StartDemoMatch(Receipt.Reason, Receipt.Detail))
            {
                Receipt.Outcome = ELDSessionOutcome::Committed;
                Receipt.Reason = ELDSessionReject::None;
            }
        }
        else if (Request.Command == ELDSessionCommand::EndMatch)
        {
            if (MatchPhase == ELDMatchPhase::Results) { Receipt.Reason = ELDSessionReject::MatchClosed; }
            else if (!IsGameplayWriteOpen(MatchContext)) { Receipt.Reason = ELDSessionReject::WrongPhase; }
            else if (bAuthorityStepOpen || ActiveWritePermit) { Receipt.Reason = ELDSessionReject::RequestInFlight; }
            else if (!TryCloseMatch(ELDMatchEndReason::HostEnded, FGuid(), AuthorityNowMs, Receipt.Detail))
            {
                FaultAuthority(Receipt.Detail);
                Receipt.Reason = ELDSessionReject::AuthorityFault;
            }
            else
            {
                Receipt.Outcome = ELDSessionOutcome::Committed;
                Receipt.Reason = ELDSessionReject::None;
                Receipt.Detail = TEXT("房主结束了本局，没有胜者。");
                PublishOutcome = true;
            }
        }
        else if (Request.Command == ELDSessionCommand::ReturnToLobby)
        {
            if (MatchPhase != ELDMatchPhase::Results) { Receipt.Reason = ELDSessionReject::WrongPhase; }
            else if (Request.ExpectedResult != ResultView.ResultId) { Receipt.Reason = ELDSessionReject::StaleMatch; }
            else if (!RetireMatch(false, Receipt.Detail))
            {
                FaultAuthority(Receipt.Detail);
                Receipt.Reason = ELDSessionReject::AuthorityFault;
            }
            else
            {
                Receipt.Outcome = ELDSessionOutcome::Committed;
                Receipt.Reason = ELDSessionReject::None;
                Receipt.Detail = TEXT("房间已回到 Lobby，可以准备新的一局。");
            }
        }
        else { Receipt.Reason = ELDSessionReject::InvalidRequest; }
        Receipt.Phase = MatchPhase;
        Receipt.RosterRevision = RosterRevision;
        if (Receipt.Detail.IsEmpty()) { Receipt.Detail = RejectionText(Receipt.Reason); }
        // A start abort or a return to the Lobby replaces every request window with the new generation's.
        const bool WindowReplaced = MatchContext.Generation != Request.Key.Generation;
        // Starting may register AI contenders and grow the contender map: never reuse the old pointer.
        Record = Contenders.Find(Controller);
        if (!WindowReplaced && (!Record || !Record->SessionRequests.Complete(Receipt)))
        {
            MatchPhase = ELDMatchPhase::Faulted;
            UE_LOG(LogTemp, Error, TEXT("LD_SESSION_RECEIPT_COMMIT_FAILED"));
            Receipt.Outcome = ELDSessionOutcome::Rejected;
            Receipt.Reason = ELDSessionReject::AuthorityFault;
            Receipt.Detail = RejectionText(Receipt.Reason);
            Changed = true;
        }
        if (PublishOutcome) { PublishMatchOutcome(); }
        if (Changed) { PublishLobby(); }
        return Receipt;
    }
    Receipt.Detail = RejectionText(Receipt.Reason);
    return Receipt;
}
