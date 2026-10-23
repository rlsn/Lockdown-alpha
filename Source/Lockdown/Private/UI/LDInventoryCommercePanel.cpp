#include "UI/LDInventoryPanel.h"
#include "UI/LDWidgets.h"
#include "Framework/LDPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
void ULDInventoryPanel::BuildCommerce(UVerticalBox* All)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());auto* Open=WidgetTree->ConstructWidget<UHorizontalBox>();All->AddChildToVerticalBox(Open);
    for(uint8 S=0;S<4;++S){auto* B=LDWidgets::Button(WidgetTree,ALDCommerceDirector::ServiceName(S),[PC,S](){PC->OpenCommerce(S);});CommerceOpenButtons.Add(B);Open->AddChildToHorizontalBox(B)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));}
    CommercePanel=WidgetTree->ConstructWidget<UVerticalBox>();All->AddChildToVerticalBox(CommercePanel);CommerceStatus=LDWidgets::Text(WidgetTree,TEXT(""),14);CommercePanel->AddChildToVerticalBox(CommerceStatus);
    for(int32 N=0;N<4;++N)
    {
        const auto Kind=ELDItemKind(uint8(ELDItemKind::Fiber)+N);auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();CommercePanel->AddChildToVerticalBox(Row);
        auto* Label=LDWidgets::Text(WidgetTree,TEXT(""),14);CommerceLabels.Add(Label);Row->AddChildToHorizontalBox(Label)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* Buy=LDWidgets::Button(WidgetTree,TEXT("Buy one"),[this,Kind](){CommerceAction(1,Kind);});CommerceBuyButtons.Add(Buy);Row->AddChildToHorizontalBox(Buy);
        auto* Sell=LDWidgets::Button(WidgetTree,TEXT("Sell selected"),[this,Kind](){CommerceAction(2,Kind);});CommerceSellButtons.Add(Sell);Row->AddChildToHorizontalBox(Sell);
    }
    CommercePanel->AddChildToVerticalBox(LDWidgets::Button(WidgetTree,TEXT("Close service"),[this,PC](){PC->CloseCommerce();Refresh();}));
}
void ULDInventoryPanel::RefreshCommerce()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto& Q=PC->CommerceView;const bool Pending=PC->InventoryPending();DisplayedCommerce=Q;
    for(uint8 S=0;S<4;++S){CommerceOpenButtons[S]->SetIsEnabled(PC->NearCommerce(S)&&!Pending&&!bConfirm&&!bTradeReview&&(!PC->CommerceIsOpen()||Q.Service!=S));}
    CommercePanel->SetVisibility(PC->CommerceIsOpen()?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    CommerceStatus->SetText(FText::FromString(FString::Printf(TEXT("%s · Official noncontender service · Reserve %lld · Quote time %.1f\n%s"),*ALDCommerceDirector::ServiceName(Q.Service),Q.Cash,Q.QuotedAt,*PC->CommerceFeedback)));
    const auto* SelectedItem=PC->OwnerState.Inventory.Find(Selected);
    for(int32 N=0;N<4;++N)
    {
        const auto Kind=ELDItemKind(uint8(ELDItemKind::Fiber)+N);const auto* Offer=Q.Offers.FindByPredicate([&](const auto& F){return F.Kind==Kind;});const bool Visible=Offer!=nullptr;
        CommerceLabels[N]->GetParent()->SetVisibility(Visible?ESlateVisibility::Visible:ESlateVisibility::Collapsed);if(!Offer){continue;}
        const FString Stock=Offer->Stock<0?TEXT("Unlimited stock"):FString::Printf(TEXT("Stock %d/%d · Bid %d"),Offer->Stock,Offer->Cap,Offer->Bid);
        FString Capacity;const bool Fits=LDInventory::Fits(PC->OwnerState.Inventory,PC->OwnerState.Build,{}, {Kind},&Capacity);
        const bool CanSell=Q.Service&&SelectedItem&&SelectedItem->Kind==Kind&&!SelectedItem->bReserved&&Offer->Stock<Offer->Cap&&Q.Cash>=Offer->Bid&&PC->OwnerState.Gold<=MAX_int64-Offer->Bid;
        FString Reason=!Fits?Capacity:PC->OwnerState.Gold<Offer->Ask?TEXT("Insufficient gold"):Offer->Stock==0?TEXT("Stock empty"):TEXT("");
        if(Q.Service&&Offer->Stock>=Offer->Cap){Reason+=TEXT(" · Cannot sell: stock full");}else if(Q.Service&&Q.Cash<Offer->Bid){Reason+=TEXT(" · Cannot sell: reserve empty");}
        CommerceLabels[N]->SetText(FText::FromString(FString::Printf(TEXT("%s · %s · Ask %d · %s"),*LDInventory::Name(Kind,ELDSkill::None),*Stock,Offer->Ask,*Reason)));
        CastChecked<UTextBlock>(CommerceBuyButtons[N]->GetContent())->SetText(FText::FromString(Pending?TEXT("Pending…"):PC->CommerceNeedsReview?TEXT("Review new quote"):TEXT("Buy one")));
        CommerceBuyButtons[N]->SetIsEnabled(!Pending&&Q.Session.IsValid()&&Fits&&PC->OwnerState.Gold>=Offer->Ask&&Offer->Stock!=0&&!bConfirm&&!bTradeReview);
        CommerceSellButtons[N]->SetVisibility(Q.Service?ESlateVisibility::Visible:ESlateVisibility::Collapsed);CommerceSellButtons[N]->SetIsEnabled(!Pending&&Q.Session.IsValid()&&CanSell&&!bConfirm&&!bTradeReview);
    }
}
void ULDInventoryPanel::CommerceAction(uint8 Command,ELDItemKind Kind)
{auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto* Offer=DisplayedCommerce.Offers.FindByPredicate([&](const auto& F){return F.Kind==Kind;});if(!Offer){return;}PC->SendCommerce(Command,Kind,Command==2?Selected:FGuid(),DisplayedCommerce.Revision,Command==1?Offer->Ask:Offer->Bid);Refresh();}
