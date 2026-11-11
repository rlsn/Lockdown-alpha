#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Development/LDBuildBlueprint.h"
#include "LDBuildBlueprintComponent.generated.h"

class ULDBlueprintPanel;

// Where the local player's blueprint of the last match stands.
enum class ELDBlueprintDelivery : uint8
{
    // No result has offered a blueprint to this player yet.
    None,
    Receiving,
    // The authority built no blueprint for this player, the transfer broke or the received record failed validation.
    Failed,
    // A valid record is held for read-only viewing; SaveError tells whether the automatic file was written.
    Received
};

// #198 §10 owner-only channel of the end-of-match build blueprint, on ALDPlayerController. The authority builds each
// human contender's own record and offers it only through that contender's owning connection; the owner pulls it in
// chunks, validates it strictly and writes the file on its own machine. A Listen Host's own player is handed the bytes
// directly. Nothing here touches gameplay state, and nobody else's build ever travels on this channel.
UCLASS()
class LOCKDOWN_API ULDBuildBlueprintComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    ULDBuildBlueprintComponent();

    // ---- Authority: the owner's record of the just-published result, or the explicit reason it has none.
    void OfferFromAuthority(const FGuid& ResultId, TArray<uint8>&& Bytes, const FString& Failure);

    UFUNCTION(Client, Reliable) void ClientBlueprintOffer(const FGuid& ResultId, int32 TotalBytes, const FString& Failure);
    UFUNCTION(Server, Reliable) void ServerBlueprintChunk(const FGuid& ResultId, int32 Offset);
    UFUNCTION(Client, Reliable) void ClientBlueprintChunk(const FGuid& ResultId, int32 Offset, const TArray<uint8>& Bytes);

    // ---- Owning client: the current match's record and its save status, read-only.
    ELDBlueprintDelivery Delivery() const { return State; }
    const FGuid& DeliveredResult() const { return ReceivingResult; }
    // Valid while Delivery() is Received.
    const FLDBlueprintRecord& Record() const { return Current; }
    bool IsSaved() const { return bSaved; }
    const FString& SavedPath() const { return SavedFile; }
    // Why the record is missing (Failed) or why its automatic file was not written (Received and not saved).
    const FString& Problem() const { return ProblemText; }

    // Explicit player actions; each returns the player-facing outcome.
    FString ExportCurrent();
    FString OpenFolder() const;
    // The read-only viewer panel, created on first use.
    void ToggleViewer();
    bool IsViewerOpen() const;

private:
    void Fail(const FString& Reason);
    void Accept(const TArray<uint8>& Bytes);

    // Authority outbox of the latest offer; Next is the only chunk offset the owner may ask for.
    FGuid OutboxResult;
    TArray<uint8> Outbox;
    int32 OutboxNext = 0;

    ELDBlueprintDelivery State = ELDBlueprintDelivery::None;
    FGuid ReceivingResult;
    int32 ReceivingTotal = 0;
    TArray<uint8> Received;
    FLDBlueprintRecord Current;
    bool bSaved = false;
    FString SavedFile;
    FString ProblemText;

    UPROPERTY() TObjectPtr<ULDBlueprintPanel> Viewer;
};
