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
        case ELDSessionReject::NotOwner: return TEXT("This connection is not a member of the room.");
        case ELDSessionReject::NotHost: return TEXT("Only the Host can change AI slots or start the match.");
        case ELDSessionReject::WrongPhase: return TEXT("This command is available in the Lobby only.");
        case ELDSessionReject::RoomFull: return TEXT("The room already has three human players, including the Host.");
        case ELDSessionReject::NotAllReady: return TEXT("Every connected human player, including the Host, must be ready.");
        case ELDSessionReject::StaleRoster: return TEXT("The room roster changed. Review it and ready again.");
        case ELDSessionReject::StaleMatch: return TEXT("This request belongs to a different match.");
        case ELDSessionReject::RequestConflict: return TEXT("This request ID was already used with different terms.");
        case ELDSessionReject::RequestInFlight: return TEXT("Wait for the pending room command to finish.");
        case ELDSessionReject::ReceiptExpired: return TEXT("This request is older than the retained receipt and will not execute again.");
        case ELDSessionReject::AuthorityFault: return TEXT("The room stopped after an authority error. Return to the menu.");
        case ELDSessionReject::None: return TEXT("");
        default: return TEXT("Invalid room command.");
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
        Reason = TEXT("Room revision exhausted. Return to the menu before creating another room.");
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
    Record.Name = Controller->PlayerState ? Controller->PlayerState->GetPlayerName().Left(64) : TEXT("Player");
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
    Reason = TEXT("Demo combat, progression, capture, camps, AI and continuous-motion providers are not fully wired in this build. No legacy match will start.");
    return false;
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
            if (Record.Session != Id || !IsMemberPresent(Id)) { continue; }
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
        Member.Name = FString::Printf(TEXT("Opponent AI %d"), Index + 1);
        Member.Kind = ELDContenderKind::OpponentAI;
        View.Members.Add(MoveTemp(Member));
    }
    View.StartBlocker = !EntryFailure.IsEmpty() ? EntryFailure
        : TEXT("Demo gameplay providers are not wired in this build. Approved content alone does not enable them.");
    if (MatchPhase != ELDMatchPhase::Lobby) { View.StartBlocker = RejectionText(ELDSessionReject::WrongPhase); }
    else if (!AllReady) { View.StartBlocker = RejectionText(ELDSessionReject::NotAllReady); }
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
        if (!IsDemoRoom() || MatchPhase != ELDMatchPhase::Lobby) { Receipt.Reason = ELDSessionReject::WrongPhase; }
        else if (Request.ExpectedRosterRevision != RosterRevision) { Receipt.Reason = ELDSessionReject::StaleRoster; }
        else if (LobbyPublicRevision == MAX_uint64)
        {
            MatchPhase = ELDMatchPhase::Faulted;
            Receipt.Reason = ELDSessionReject::AuthorityFault;
        }
        else if (Request.Command != ELDSessionCommand::SetReady && LobbyHost != Controller) { Receipt.Reason = ELDSessionReject::NotHost; }
        else if (Request.Command == ELDSessionCommand::SetReady)
        {
            Changed = Record->State.bReady != Request.Ready || Record->ReadyRosterRevision != RosterRevision;
            Record->State.bReady = Request.Ready;
            Record->ReadyRosterRevision = RosterRevision;
            Receipt.Outcome = ELDSessionOutcome::Committed;
            Receipt.Reason = ELDSessionReject::None;
            Receipt.Detail = Request.Ready ? TEXT("Ready.") : TEXT("Not ready.");
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
                Receipt.Detail = Changed ? TEXT("AI slots updated. All human players must ready again.") : TEXT("AI slots unchanged.");
            }
            else { Receipt.Reason = ELDSessionReject::AuthorityFault; }
        }
        else
        {
            bool AllReady = !Contenders.IsEmpty() && Contenders.Num() <= 3;
            for (const auto& Pair : Contenders)
            {
                AllReady &= Pair.Value.State.bReady && Pair.Value.ReadyRosterRevision == RosterRevision;
            }
            if (!AllReady) { Receipt.Reason = ELDSessionReject::NotAllReady; }
            else
            {
                CheckDemoStartInputs(Receipt.Detail);
                Receipt.Reason = ELDSessionReject::MissingDemoDefinition;
            }
        }
        Receipt.Phase = MatchPhase;
        Receipt.RosterRevision = RosterRevision;
        if (Receipt.Detail.IsEmpty()) { Receipt.Detail = RejectionText(Receipt.Reason); }
        if (!Record->SessionRequests.Complete(Receipt))
        {
            MatchPhase = ELDMatchPhase::Faulted;
            UE_LOG(LogTemp, Error, TEXT("LD_SESSION_RECEIPT_COMMIT_FAILED"));
            Receipt.Outcome = ELDSessionOutcome::Rejected;
            Receipt.Reason = ELDSessionReject::AuthorityFault;
            Receipt.Detail = RejectionText(Receipt.Reason);
            Changed = true;
        }
        if (Changed) { PublishLobby(); }
        return Receipt;
    }
    Receipt.Detail = RejectionText(Receipt.Reason);
    return Receipt;
}
