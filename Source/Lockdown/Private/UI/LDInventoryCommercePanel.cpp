#include "UI/LDInventoryPanel.h"
#include "UI/LDWidgets.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Engine/World.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
void ULDInventoryPanel::BuildCommerce(UVerticalBox* All)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());auto* Open=WidgetTree->ConstructWidget<UUniformGridPanel>();Open->SetSlotPadding(FMargin(3));All->AddChildToVerticalBox(Open);
    for(uint8 S=0;S<4;++S){auto* B=LDWidgets::Button(WidgetTree,ALDCommerceDirector::ServiceName(S),[this,S](){CastChecked<ALDPlayerController>(GetOwningPlayer())->CloseCommerce();SelectedMarket=S;if(CastChecked<ALDPlayerController>(GetOwningPlayer())->NearCommerce(S)){CastChecked<ALDPlayerController>(GetOwningPlayer())->OpenCommerce(S);}Refresh();});CommerceOpenButtons.Add(B);CastChecked<UTextBlock>(B->GetContent())->SetAutoWrapText(true);Open->AddChildToUniformGrid(B,S/2,S%2);}
    CommercePanel=WidgetTree->ConstructWidget<UVerticalBox>();All->AddChildToVerticalBox(CommercePanel);CommerceStatus=LDWidgets::Text(WidgetTree,TEXT(""),14);CommercePanel->AddChildToVerticalBox(CommerceStatus);
    for(int32 N=0;N<5;++N)
    {
        const auto Kind=(N==4?ELDItemKind::NullBell:ELDItemKind(uint8(ELDItemKind::Fiber)+N));auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();CommercePanel->AddChildToVerticalBox(Row);
        auto* Label=LDWidgets::Text(WidgetTree,TEXT(""),14);CommerceLabels.Add(Label);Row->AddChildToHorizontalBox(Label)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* Buy=LDWidgets::Button(WidgetTree,TEXT("Buy one"),[this,Kind](){CommerceAction(1,Kind);});CommerceBuyButtons.Add(Buy);Row->AddChildToHorizontalBox(Buy);
        auto* Sell=LDWidgets::Button(WidgetTree,TEXT("Sell selected"),[this,Kind](){CommerceAction(2,Kind);});CommerceSellButtons.Add(Sell);Row->AddChildToHorizontalBox(Sell);
    }
    CommercePanel->AddChildToVerticalBox(LDWidgets::Button(WidgetTree,TEXT("Close service"),[this,PC](){PC->CloseCommerce();Refresh();}));
}
void ULDInventoryPanel::RefreshCommerce()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const bool Live=PC->CommerceIsOpen();const auto* Cached=PC->LastCommerceQuotes.Find(SelectedMarket);const FLDCommerceView Q=Live?PC->CommerceView:Cached?*Cached:FLDCommerceView();const bool Pending=PC->InventoryPending();DisplayedCommerce=Q;
    if(Live){SelectedMarket=Q.Service;}
    for(uint8 S=0;S<4;++S){CommerceOpenButtons[S]->SetIsEnabled(!Pending&&!bConfirm&&!bTradeReview&&(!PC->CommerceIsOpen()||Q.Service!=S));}
    CommercePanel->SetVisibility(ESlateVisibility::Visible);
    CommerceStatus->SetText(FText::FromString(Q.Offers.IsEmpty()?ALDCommerceDirector::ServiceName(SelectedMarket)+TEXT("\nNo visited quote. Approach within 120 cm with body sight to open this protected service."):FString::Printf(TEXT("%s · %s\nReserve %lld gold · Quoted at %.1fs · %s"),*ALDCommerceDirector::ServiceName(Q.Service),Live?TEXT("LOCAL SERVICE"):TEXT("LAST VISITED QUOTE · READ ONLY"),Q.Cash,Q.QuotedAt,Live?*PC->CommerceFeedback:TEXT("Visit again for current prices"))));
    if(Q.Service==0&&!Q.Offers.IsEmpty()){CommerceStatus->SetText(FText::FromString(FString::Printf(TEXT("OFFICIAL MATERIALS · %s\nUnlimited stock · paid creation · Quoted at %.1fs\n%s"),Live?TEXT("LOCAL SERVICE"):TEXT("LAST VISITED QUOTE · READ ONLY"),Q.QuotedAt,Live?*PC->CommerceFeedback:TEXT("Visit again for current prices"))));}
    if(const auto* G=GetWorld()->GetGameState<ALDGameState>();G&&Live&&Q.Session.IsValid())
    {CommerceStatus->SetText(FText::FromString(CommerceStatus->GetText().ToString()+(G->EconomyNextTick>0?FString::Printf(TEXT("\nNext official work in %.1fs"),FMath::Max(0.,double(G->EconomyNextTick)-G->GetServerWorldTimeSeconds())):TEXT("\nOfficial routine finished"))+(Q.Service==3&&G->ShortagePhase==1?TEXT(" · Toolkit shortage 5/4; normal clamps"):TEXT(""))));}
    CommercePanel->GetChildAt(CommercePanel->GetChildrenCount()-1)->SetVisibility(Live?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    const auto* SelectedItem=PC->OwnerState.Inventory.Find(Selected);
    for(int32 N=0;N<5;++N)
    {
        const auto Kind=(N==4?ELDItemKind::NullBell:ELDItemKind(uint8(ELDItemKind::Fiber)+N));const auto* Offer=Q.Offers.FindByPredicate([&](const auto& F){return F.Kind==Kind;});const bool Visible=Offer!=nullptr;
        CommerceLabels[N]->GetParent()->SetVisibility(Visible?ESlateVisibility::Visible:ESlateVisibility::Collapsed);if(!Offer){continue;}
        const FString Stock=Offer->Stock<0?TEXT("Unlimited stock"):FString::Printf(TEXT("Stock %d/%d · Bid %d"),Offer->Stock,Offer->Cap,Offer->Bid);
        FString Capacity;const bool Fits=LDInventory::Fits(PC->OwnerState.Inventory,PC->OwnerState.Build,{}, {Kind},&Capacity);
        const bool CanSell=Q.Service&&SelectedItem&&SelectedItem->Kind==Kind&&!SelectedItem->bReserved&&Offer->Stock<Offer->Cap&&Q.Cash>=Offer->Bid&&PC->OwnerState.Gold<=MAX_int64-Offer->Bid;
        FString Reason=!Live?TEXT("Visit again to buy or sell"):!Fits?Capacity:PC->OwnerState.Gold<Offer->Ask?TEXT("Insufficient gold"):Offer->Stock==0?TEXT("Stock empty"):TEXT("");
        if(Q.Service&&Offer->Stock>=Offer->Cap){Reason+=TEXT(" · Cannot sell: stock full");}else if(Q.Service&&Q.Cash<Offer->Bid){Reason+=TEXT(" · Cannot sell: reserve empty");}else if(Q.Service&&(!SelectedItem||SelectedItem->Kind!=Kind)){Reason+=TEXT(" · Sell: select your matching cargo");}else if(Q.Service&&SelectedItem->bReserved){Reason+=TEXT(" · Sell: selected input is reserved");}
        CommerceLabels[N]->SetText(FText::FromString(FString::Printf(TEXT("%s · %s · Ask %d · %s"),*LDInventory::Name(Kind,ELDSkill::None),*Stock,Offer->Ask,*Reason)));
        CastChecked<UTextBlock>(CommerceBuyButtons[N]->GetContent())->SetText(FText::FromString(Pending?TEXT("Pending…"):PC->CommerceNeedsReview?TEXT("Review new quote"):TEXT("Buy one")));
        CommerceBuyButtons[N]->SetIsEnabled(!Pending&&Live&&Q.Session.IsValid()&&Fits&&PC->OwnerState.Gold>=Offer->Ask&&Offer->Stock!=0&&!bConfirm&&!bTradeReview);
        CastChecked<UTextBlock>(CommerceSellButtons[N]->GetContent())->SetText(FText::FromString(Pending?TEXT("Pending…"):PC->CommerceNeedsReview?TEXT("Review quote"):FString::Printf(TEXT("Sell · %dg"),Offer->Bid)));
        CommerceSellButtons[N]->SetVisibility(Q.Service?ESlateVisibility::Visible:ESlateVisibility::Collapsed);CommerceSellButtons[N]->SetIsEnabled(!Pending&&Live&&Q.Session.IsValid()&&CanSell&&!bConfirm&&!bTradeReview);
    }
}
void ULDInventoryPanel::CommerceAction(uint8 Command,ELDItemKind Kind)
{auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto* Offer=DisplayedCommerce.Offers.FindByPredicate([&](const auto& F){return F.Kind==Kind;});if(!Offer||!PC->CommerceIsOpen()){return;}PC->SendCommerce(Command,Kind,Command==2?Selected:FGuid(),DisplayedCommerce.Revision,Command==1?Offer->Ask:Offer->Bid);Refresh();}
