#include "Framework/LDBuildBlueprintComponent.h"
#include "Core/LDLog.h"

#include "Development/LDBuildBlueprintStore.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "UI/LDBlueprintPanel.h"

ULDBuildBlueprintComponent::ULDBuildBlueprintComponent()
{
    PrimaryComponentTick.bCanEverTick=false;
    SetIsReplicatedByDefault(true);
}

void ULDBuildBlueprintComponent::OfferFromAuthority(const FGuid& ResultId, TArray<uint8>&& Bytes, const FString& Failure)
{
    const APlayerController* Owner=Cast<APlayerController>(GetOwner());
    if (!Owner || !Owner->HasAuthority() || !ResultId.IsValid()) { return; }
    OutboxResult=ResultId;
    OutboxNext=0;
    Outbox.Reset();
    const bool bFailed=!Failure.IsEmpty() || Bytes.IsEmpty() || Bytes.Num()>LDBuildBlueprint::MaxTransferBytes;
    const FString Reason=!Failure.IsEmpty() ? Failure
        : FString(TEXT("房主没有为你生成可传输的构筑蓝图。"));
    if (!bFailed) { Outbox=MoveTemp(Bytes); }
    if (Owner->IsLocalController())
    {
        // The Listen Host's own player owns this machine: no transfer.
        ReceivingResult=ResultId;
        ReceivingTotal=Outbox.Num();
        if (bFailed) { Fail(Reason); }
        else { Accept(Outbox); }
        Outbox.Empty();
        return;
    }
    ClientBlueprintOffer(ResultId,bFailed ? 0 : Outbox.Num(),bFailed ? Reason : FString());
}

void ULDBuildBlueprintComponent::ServerBlueprintChunk_Implementation(const FGuid& ResultId, int32 Offset)
{
    // Only the next chunk of the current offer, once: a replay, a skip or an old result is refused.
    if (!OutboxResult.IsValid() || ResultId!=OutboxResult || Offset!=OutboxNext || Offset<0 || Offset>=Outbox.Num())
    {
        UE_LOG(LogLDUI,Warning,TEXT("LD_BUILD_BLUEPRINT_CHUNK_REFUSED result=%s offset=%d next=%d size=%d"),
            *LDLog::Id(ResultId),Offset,OutboxNext,Outbox.Num());
        return;
    }
    const int32 Count=FMath::Min(LDBuildBlueprint::ChunkBytes,Outbox.Num()-Offset);
    TArray<uint8> Slice;
    Slice.Append(Outbox.GetData()+Offset,Count);
    OutboxNext=Offset+Count;
    ClientBlueprintChunk(ResultId,Offset,Slice);
    if (OutboxNext>=Outbox.Num()) { Outbox.Empty(); }
}

void ULDBuildBlueprintComponent::ClientBlueprintOffer_Implementation(const FGuid& ResultId, int32 TotalBytes, const FString& Failure)
{
    ReceivingResult=ResultId;
    ReceivingTotal=0;
    Received.Reset();
    if (!Failure.IsEmpty())
    {
        Fail(Failure);
        return;
    }
    if (!ResultId.IsValid() || TotalBytes<=0 || TotalBytes>LDBuildBlueprint::MaxTransferBytes)
    {
        Fail(TEXT("房主发来的构筑蓝图大小不合法，没有接收。"));
        return;
    }
    State=ELDBlueprintDelivery::Receiving;
    Current=FLDBlueprintRecord();
    bSaved=false;
    SavedFile.Reset();
    ProblemText.Reset();
    ReceivingTotal=TotalBytes;
    Received.Reserve(TotalBytes);
    ServerBlueprintChunk(ResultId,0);
}

void ULDBuildBlueprintComponent::ClientBlueprintChunk_Implementation(const FGuid& ResultId, int32 Offset, const TArray<uint8>& Bytes)
{
    if (State!=ELDBlueprintDelivery::Receiving || ResultId!=ReceivingResult || Offset!=Received.Num() || Bytes.IsEmpty()
        || Bytes.Num()>ReceivingTotal-Received.Num())
    {
        // A partial record is never kept or saved as a blueprint.
        Fail(TEXT("构筑蓝图传输中断，没有保存不完整的记录。"));
        return;
    }
    Received.Append(Bytes);
    if (Received.Num()<ReceivingTotal)
    {
        ServerBlueprintChunk(ResultId,Received.Num());
        return;
    }
    const TArray<uint8> Complete=MoveTemp(Received);
    Received.Reset();
    Accept(Complete);
}

void ULDBuildBlueprintComponent::Fail(const FString& Reason)
{
    State=ELDBlueprintDelivery::Failed;
    Current=FLDBlueprintRecord();
    Received.Reset();
    bSaved=false;
    SavedFile.Reset();
    ProblemText=Reason;
    UE_LOG(LogLDUI,Warning,TEXT("LD_BUILD_BLUEPRINT_FAILED result=%s reason=%s"),*LDLog::Id(ReceivingResult),*LDLog::Quote(Reason));
}

void ULDBuildBlueprintComponent::Accept(const TArray<uint8>& Bytes)
{
    FLDBlueprintRecord Record;
    FString Detail;
    const ELDBlueprintParseError Error=LDBuildBlueprint::Parse(Bytes,LDBuildBlueprint::MaxTransferBytes,Record,Detail);
    if (Error!=ELDBlueprintParseError::None)
    {
        Fail(FString(TEXT("收到的构筑蓝图未通过校验："))+LDBuildBlueprint::DescribeError(Error,Detail));
        return;
    }
    if (!Record.MatchId.Equals(ReceivingResult.ToString(EGuidFormats::Digits),ESearchCase::IgnoreCase))
    {
        Fail(TEXT("收到的构筑蓝图不属于本局结果，没有保存。"));
        return;
    }
    // Retention orders this machine's automatic records by its own clock.
    Record.RecordedAtUtc=FDateTime::UtcNow();
    Current=MoveTemp(Record);
    State=ELDBlueprintDelivery::Received;
    const LDBuildBlueprintStore::FWriteResult Write=LDBuildBlueprintStore::WriteAuto(Current,LDBuildBlueprintStore::UserDirectory());
    bSaved=Write.bSaved;
    SavedFile=Write.bSaved ? Write.Path : FString();
    ProblemText=Write.Error;
    if (Write.bSaved)
    {
        UE_LOG(LogLDUI,Display,TEXT("LD_BUILD_BLUEPRINT_SAVED result=%s file=%s versions=%d pruned=%d"),*LDLog::Id(ReceivingResult),
            *LDBuildBlueprintStore::DisplayPath(Write.Path),Current.Versions.Num(),Write.Prune.Deleted);
    }
    else
    {
        UE_LOG(LogLDUI,Warning,TEXT("LD_BUILD_BLUEPRINT_SAVE_FAILED result=%s reason=%s"),*LDLog::Id(ReceivingResult),*LDLog::Quote(Write.Error));
    }
}

FString ULDBuildBlueprintComponent::ExportCurrent()
{
    if (State!=ELDBlueprintDelivery::Received) { return TEXT("本局没有可导出的构筑蓝图。"); }
    const LDBuildBlueprintStore::FWriteResult Write=LDBuildBlueprintStore::Export(Current,LDBuildBlueprintStore::UserDirectory());
    if (!Write.bSaved) { return Write.Error; }
    return FString::Printf(TEXT("已导出：%s。把这个文件发给朋友即可；对方放进 Blueprints/Imports 后用“导入查看”。"),
        *LDBuildBlueprintStore::DisplayPath(Write.Path));
}

FString ULDBuildBlueprintComponent::OpenFolder() const
{
    const FString Directory=LDBuildBlueprintStore::UserDirectory();
    IFileManager& Files=IFileManager::Get();
    if (!Files.MakeDirectory(*LDBuildBlueprintStore::ExportDirectory(Directory),true)
        || !Files.MakeDirectory(*LDBuildBlueprintStore::ImportDirectory(Directory),true))
    {
        return TEXT("无法创建构筑蓝图目录。");
    }
    FPlatformProcess::ExploreFolder(*FPaths::ConvertRelativePathToFull(Directory));
    return FString::Printf(TEXT("已打开 %s。收到的蓝图放进 Imports 后用“导入查看”。"),*LDBuildBlueprintStore::DisplayPath(Directory));
}

void ULDBuildBlueprintComponent::ToggleViewer()
{
    APlayerController* Owner=Cast<APlayerController>(GetOwner());
    if (!Owner || !Owner->IsLocalController()) { return; }
    if (!Viewer)
    {
        Viewer=CreateWidget<ULDBlueprintPanel>(Owner);
        if (!Viewer) { return; }
        // Above the HUD (30) and the controls panel (25); it stays collapsed while closed.
        Viewer->AddToViewport(40);
    }
    if (Viewer->IsPanelOpen())
    {
        Viewer->ClosePanel();
        return;
    }
    Viewer->OpenPanel();
}

bool ULDBuildBlueprintComponent::IsViewerOpen() const
{
    return Viewer && Viewer->IsPanelOpen();
}
