#include "UI/LDComponentStorage.h"
#include "Core/LDLog.h"
#include "UI/LDTerms.h"
#include "UI/LDWidgets.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Engine/World.h"

namespace
{
// A move whose uninstall is applied but never shows in the own view gives up and says where the component is.
constexpr double StorageMoveTimeout=5.;
}

double ULDComponentStorage::UiNow() const
{
    return GetWorld()?GetWorld()->GetRealTimeSeconds():0.;
}
FString ULDComponentStorage::ComponentName(FName Component) const
{
    return LDHUD::ContentName(LatestContent,LDHUD::CardDefinition(Component).ToString());
}

void ULDComponentStorage::Present(const FLDProgressionView* View,uint32 Match,LDProgression::EMatchStage Stage,ELDLifePhase LifePhase,
    const FLDDemoContentDefinitions* Content,bool bShown)
{
    // Another match's answers never reach this one: its request, move and chooser end with it.
    if(Match!=LatestMatch){PendingRequest=0;Follow=FFollowUp();CloseChooser();SeenComponents.Reset();}
    LatestMatch=Match;
    Latest=View?*View:FLDProgressionView();
    Gate=LDHUD::BuildGate(Stage,LifePhase);
    LatestContent=Content;
    if(!bShown||!Latest.bValid){CloseChooser();}
    LatestModel=LDHUD::CardStorage(Latest,Content,&SeenComponents);
    {
        // A newly received card plays its cue; a new match only takes its first view as the baseline.
        TSet<FName> Owned;for(const FLDCardInstanceView& Each:Latest.Cards){Owned.Add(LDHUD::CardKey(Each.DefinitionId,Each.Serial));}
        if(KnownMatch==Match&&Latest.bValid)
        {
            for(const FLDCardInstanceView& Each:Latest.Cards)
            {
                if(KnownOwned.Contains(LDHUD::CardKey(Each.DefinitionId,Each.Serial))||Each.IsInstalled()){continue;}
                LDWidgets::PlayCue(this,ELDAudioCue::UIGain);
            }
        }
        if(Latest.bValid){KnownOwned=MoveTemp(Owned);KnownMatch=Match;}
    }
    // A move's install half follows once the own view shows its uninstall; a view that never does gives up.
    if(Follow.State==EFollow::AwaitView&&!PendingRequest)
    {
        if(Latest.BuildRevision>=Follow.AfterRevision)
        {
            const FLDBuildCommand Install=Follow.Install;Follow=FFollowUp();
            Send(Install);
        }
        else if(UiNow()-Follow.Since>StorageMoveTimeout)
        {
            if(Notify){Notify(FString::Printf(TEXT("%s：没有移动完，%s 在牌库里"),LDTerms::Storage,*ComponentName(Follow.Component)));}
            Follow=FFollowUp();
        }
    }
}
void ULDComponentStorage::MarkSeen(FName Component)
{
    if(Component.IsNone()||!LatestMatch||SeenComponents.Contains(Component)){return;}
    // Looking at a stored face marks every copy it stands for.
    for(const LDHUD::FStoredView& Stored:LatestModel.Stored)
    {
        if(Stored.Keys.Contains(Component)){for(const FName& Key:Stored.Keys){SeenComponents.Add(Key);}}
    }
    SeenComponents.Add(Component);
    LatestModel=LDHUD::CardStorage(Latest,LatestContent,&SeenComponents);
}
bool ULDComponentStorage::CancelInteraction()
{
    const bool bHad=Chooser.bOpen;
    CloseChooser();
    return bHad;
}
FString ULDComponentStorage::PlaceName(LDHUD::EStorageColumn Column,FName Host) const
{
    if(!Host.IsNone()){return ComponentName(Host);}
    const int32 Index=int32(Column);
    if(!LatestModel.Columns.IsValidIndex(Index)){return FString();}
    const LDHUD::FStorageColumnView& Place=LatestModel.Columns[Index];
    return Place.Title.IsEmpty()&&!Place.Active.IsNone()?ComponentName(Place.Active):Place.Title;
}

bool ULDComponentStorage::DropInStorage(FName Component)
{
    const FLDCardInstanceView* Owned=LDHUD::FindCard(Latest,Component);
    if(!Owned||!Owned->IsInstalled()){return false;}
    if(PendingRequest||Follow.State!=EFollow::None){if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：上一个请求仍在处理中。"));}return false;}
    FLDBuildCommand Command;Command.Kind=ELDBuildCommandKind::UninstallCard;Command.CardSerial=Owned->Serial;
    return Send(Command);
}
bool ULDComponentStorage::DropOn(FName Component,LDHUD::EStorageColumn Column,FName Host)
{
    const FLDCardInstanceView* Owned=LDHUD::FindCard(Latest,Component);
    if(!Owned){return false;}
    if(PendingRequest||Follow.State!=EFollow::None){if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：上一个请求仍在处理中。"));}return false;}
    if(!Gate.bCanInstall){if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：")+Gate.InstallBlocked);}return false;}
    // Put where it already sits: nothing to do.
    if(Owned->IsInstalled()&&(Host.IsNone()||Host==Component)&&LatestModel.Columns.IsValidIndex(int32(Column)))
    {
        const LDHUD::FStorageColumnView& Place=LatestModel.Columns[int32(Column)];
        const bool bThere=(Place.bHasMedal&&Place.Medal.Keys.Contains(Component))
            ||Place.Entries.ContainsByPredicate([Component](const LDHUD::FEquippedView& Entry){return Entry.Keys.Contains(Component);});
        if(bThere){return false;}
    }
    // The shared capacity is the only limit (CardDrops applies it); an element medal takes none of it.
    const TArray<LDHUD::FCardDrop> Drops=LDHUD::CardDrops(Component,Latest,LatestContent);
    // An installed card of the same column is the column itself unless it is a replace drop.
    const LDHUD::FCardDrop* Drop=LDHUD::FindDrop(Drops,Column,NAME_None);
    // A stored card put on an installed card of a full workshop replaces it in one commit.
    const TArray<LDHUD::FCardDrop> ColReplaces=Drop||Owned->IsInstalled()?TArray<LDHUD::FCardDrop>():LDHUD::ColumnReplaceDrops(Component,Column,Latest,LatestContent);
    if(!Drop&&!Host.IsNone())
    {
        Drop=ColReplaces.FindByPredicate([Column,Host](const LDHUD::FCardDrop& D){return D.Column==Column&&D.Host==Host;});
    }
    if(!Drop&&Host.IsNone()&&!ColReplaces.IsEmpty())
    {
        // A full workshop: the card replaces one installed card of this column, which the player puts it on.
        if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：")+LDTerms::ReplacePickPrompt);}
        return false;
    }
    if(!Drop&&!Owned->IsInstalled()&&Drops.IsEmpty()&&LDHUD::WorkshopFull(Latest,LatestContent))
    {
        if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：")+LDHUD::StoredPlaceText(Component,Latest,LatestContent));}
        return false;
    }
    if(!Drop)
    {
        if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：")+LDTerms::CannotGoThere(ComponentName(Component),PlaceName(Column,Host)));}
        return false;
    }
    // Y25 asks for its element, unless there is only one.
    if(Drop->Elements.Num()>1){OpenChooser(Component,*Drop);return true;}
    return Commit(Component,*Drop,Drop->Elements.Num()==1?Drop->Elements[0]:ELDDemoElement::None);
}
void ULDComponentStorage::OpenChooser(FName Component,const LDHUD::FCardDrop& Drop)
{
    Chooser.bOpen=true;Chooser.Component=Component;Chooser.Drop=Drop;
}
void ULDComponentStorage::CloseChooser()
{
    Chooser=FChooser();
}
bool ULDComponentStorage::ChooseOption(int32 Index)
{
    if(!Chooser.bOpen){return false;}
    const FName Component=Chooser.Component;const LDHUD::FCardDrop Drop=Chooser.Drop;
    if(!Drop.Elements.IsValidIndex(Index)){return false;}
    CloseChooser();
    return Commit(Component,Drop,Drop.Elements[Index]);
}
bool ULDComponentStorage::Commit(FName Component,const LDHUD::FCardDrop& Drop,ELDDemoElement Element)
{
    const FLDCardInstanceView* Owned=LDHUD::FindCard(Latest,Component);
    if(!Owned){return false;}
    FLDBuildCommand Install;Install.Kind=ELDBuildCommandKind::InstallCard;Install.CardSerial=Owned->Serial;
    Install.CardTarget=Drop.Kind;Install.Element=Element;
    const auto Refuse=[this](const TCHAR* Why){if(Notify){Notify(FString(TEXT("没有提交："))+Why);}return false;};
    if(!Drop.TargetId.IsNone()&&!FLDDefinitionWire::Encode(Drop.TargetId,Install.TargetDefinition)){return Refuse(TEXT("安装位置的标识无效。"));}
    if(!Drop.Replacing.IsNone())
    {
        const FLDCardInstanceView* Replaced=LDHUD::FindCard(Latest,Drop.Replacing);
        if(Owned->IsInstalled()||!Replaced){return Refuse(TEXT("被替换的牌已经不在了。"));}
        bPendingMedalFuse=(Owned->DefinitionId==Replaced->DefinitionId);
        Install.ReplacingSerial=Replaced->Serial;
        return Send(Install);
    }
    bPendingMedalFuse=Latest.Cards.ContainsByPredicate([&](const FLDCardInstanceView& C)
    {
        return C.IsInstalled() && C.DefinitionId==Owned->DefinitionId && C.Target.Kind==Drop.Kind
            && (Drop.Kind!=ELDCardTargetKind::Skill || C.Target.Active==Drop.TargetId);
    });
    if(!Owned->IsInstalled()){return Send(Install);}
    // A move: uninstall first, then install once the view shows the uninstall.
    FLDBuildCommand Uninstall;Uninstall.Kind=ELDBuildCommandKind::UninstallCard;Uninstall.CardSerial=Owned->Serial;
    Follow.State=EFollow::AwaitUninstall;Follow.Component=Component;Follow.Install=Install;
    return Send(Uninstall);
}
bool ULDComponentStorage::Send(FLDBuildCommand Command)
{
    if(!SubmitBuild)
    {
        UE_LOG(LogLDUI,Error,TEXT("LD_STORAGE_SUBMIT_UNAVAILABLE The growth build entry is not connected to the card storage."));
        Follow=FFollowUp();
        if(Notify){Notify(TEXT("成长入口不可用，本次没有提交。"));}
        return false;
    }
    Command.ExpectedBuildRevision=Latest.BuildRevision;Command.CatalogDigest=Latest.CatalogDigest;
    // A Listen Host answers inside the submit call, so the pending entry is keyed to the id it will receive first.
    const uint64 Expected=PeekRequestId?PeekRequestId():0;
    const uint32 Before=Answers;PendingRequest=Expected;
    ELDGrowthReason Reason=ELDGrowthReason::None;
    const uint64 Request=SubmitBuild(Command,Reason);
    if(!Request)
    {
        if(PendingRequest==Expected){PendingRequest=0;}
        Follow=FFollowUp();
        const FString Why=LDHUD::GrowthReasonText(Reason);
        if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：没有提交：")+(Why.IsEmpty()?FString(TEXT("原因未知")):Why));}
        return false;
    }
    if(Answers==Before){PendingRequest=Request;}
    return true;
}
void ULDComponentStorage::FinishRequest(const FString& Text,bool bRefused)
{
    PendingRequest=0;
    const bool bFused=bPendingMedalFuse;
    bPendingMedalFuse=false;
    if(bRefused){LDWidgets::PlayCue(this,ELDAudioCue::UIReject);}
    else if(!Text.IsEmpty()){LDWidgets::PlayCue(this,bFused?ELDAudioCue::UIMedalFuse:ELDAudioCue::UIInstall);}
    if(!Text.IsEmpty()&&Notify){Notify(FString(LDTerms::Storage)+TEXT("：")+Text);}
}
void ULDComponentStorage::ShowFinal(const FLDBuildReceipt& Receipt)
{
    const bool bApplied=Receipt.Header.Outcome==ELDReceiptOutcome::Applied;
    if(Follow.State==EFollow::AwaitUninstall&&Receipt.Kind==ELDBuildCommandKind::UninstallCard)
    {
        // The move's first half: its install waits for the own view to carry this build revision.
        if(bApplied)
        {
            Follow.State=EFollow::AwaitView;
            Follow.AfterRevision=Receipt.Header.Revisions.bHasBuild?Receipt.Header.Revisions.BuildRevision:Latest.BuildRevision+1;
            Follow.Since=UiNow();
            FinishRequest(FString());
            return;
        }
        Follow=FFollowUp();
    }
    FinishRequest(LDHUD::BuildReceiptText(Receipt,LatestContent),!bApplied);
}
// The pending entry clears exactly when the controller releases its in-flight request (like the growth panel's).
void ULDComponentStorage::HandleSubmit(const FLDSubmitResult& Result)
{
    if(Result.Family!=ELDGrowthFamily::Build||!PendingRequest||Result.RequestId!=PendingRequest||Result.Generation!=LatestMatch){return;}
    ++Answers;
    switch(Result.Status)
    {
    case ELDAdmissionStatus::AcceptedPending: return;
    case ELDAdmissionStatus::AdmissionRejected:
        if(Result.bSequenceReserved){return;}
        Follow=FFollowUp();
        FinishRequest(TEXT("没有受理：")+LDHUD::GrowthReasonText(LDGrowthWire::Reason(Result.Reason)),true);
        return;
    case ELDAdmissionStatus::Final:
        if(Result.bHasBuildReceipt){ShowFinal(Result.BuildReceipt);return;}
        Follow=FFollowUp();FinishRequest(TEXT("请求已结束，请以当前状态为准。"));
        return;
    default:
        if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：无法识别服务器的答复，请以当前状态为准。"));}
        return;
    }
}
void ULDComponentStorage::HandleReceipt(const FLDBuildReceiptQueryResult& Result)
{
    if(Result.Query.Family!=ELDGrowthFamily::Build||!PendingRequest||Result.Query.RequestId!=PendingRequest||Result.Query.Generation!=LatestMatch){return;}
    ++Answers;
    switch(Result.Lookup)
    {
    case ELDReceiptLookup::Pending: return;
    case ELDReceiptLookup::Final:
        if(Result.bHasReceipt){ShowFinal(Result.Receipt);return;}
        Follow=FFollowUp();FinishRequest(TEXT("回执缺少结果，请以当前状态为准。"));
        return;
    case ELDReceiptLookup::ReceiptExpired: Follow=FFollowUp();FinishRequest(TEXT("回执已过期，请以当前状态为准。"));return;
    case ELDReceiptLookup::NotFound:
        if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：服务器没有找到这次请求，请以当前状态为准。"));}
        return;
    default:
        if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：回执查询无效，请以当前状态为准。"));}
        return;
    }
}
