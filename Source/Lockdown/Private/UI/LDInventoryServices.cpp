#include "UI/LDInventoryPanel.h"
#include "UI/LDWidgets.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Engine/World.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
void ULDInventoryPanel::ShowService(uint8 Tab)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
    if(Tab>=7||ServiceTab==Tab||PC->InventoryPending()||bConfirm||bTradeReview){return;}
    ServiceTab=Tab;bDirty=true;bProductionPreview=false;bBankReviewed=false;CancelDrag();
    if(Tab!=2){PC->CloseCommerce();}if(Tab!=5){PC->CloseBank();}if(Tab!=1){PC->CloseVendor();}if(Tab!=6){PC->CancelTrade();}
    if(ServiceScroll){ServiceScroll->ScrollToStart();}Refresh();SetKeyboardFocus();
}
void ULDInventoryPanel::RefreshServices()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto& O=PC->OwnerState;
    if(QuoteMatch!=O.Match){for(auto It=PC->LastCommerceQuotes.CreateIterator();It;++It){if(It.Value().Match!=O.Match){It.RemoveCurrent();}}QuoteMatch=O.Match;}
    for(uint8 Tab=0;Tab<Services.Num();++Tab){Services[Tab]->SetVisibility((Tab==ServiceTab||(Tab==3&&ServiceTab==4))&&Tab!=4?ESlateVisibility::Visible:ESlateVisibility::Collapsed);ServiceTabs[Tab]->SetIsEnabled(!PC->InventoryPending()&&!bConfirm&&!bTradeReview);}
    const float Cell=(ServiceWidth->GetWidthOverride()-12)/2;
    TFunction<void(UWidget*)> Layout=[&](UWidget* Widget){if(auto* Grid=Cast<UUniformGridPanel>(Widget)){Grid->SetMinDesiredSlotWidth(Cell);Grid->SetMinDesiredSlotHeight(48);for(int32 N=0;N<Grid->GetChildrenCount();++N){auto* Child=Grid->GetChildAt(N);if(auto* CellSlot=Cast<UUniformGridSlot>(Child->Slot)){CellSlot->SetHorizontalAlignment(HAlign_Fill);CellSlot->SetVerticalAlignment(VAlign_Fill);}if(auto* B=Cast<UButton>(Child)){if(auto* T=Cast<UTextBlock>(B->GetContent())){T->SetWrapTextAt(FMath::Max(80.f,Cell-28));}}}}if(auto* Panel=Cast<UPanelWidget>(Widget)){for(int32 N=0;N<Panel->GetChildrenCount();++N){Layout(Panel->GetChildAt(N));}}};
    for(const auto& Section:Services){Layout(Section.Get());}
    const TCHAR* Names[]={TEXT("Items"),TEXT("Books"),TEXT("Markets"),TEXT("Craft"),TEXT("Orders"),TEXT("Treasury"),TEXT("Trade")};
    for(uint8 Tab=0;Tab<ServiceTabs.Num();++Tab){CastChecked<UTextBlock>(ServiceTabs[Tab]->GetContent())->SetAutoWrapText(false);CastChecked<UTextBlock>(ServiceTabs[Tab]->GetContent())->SetText(FText::FromString((ServiceTab==Tab?FString(TEXT("› ")):FString())+Names[Tab]));}
    const TCHAR* Hints[]={TEXT("YOUR ITEMS · Cargo has no quick use. Select a bag item to inspect, offer or reserve it."),TEXT("GUILD BOOKS · Protected official shop. Approach within 120 cm to open its catalog."),TEXT("MARKETS · Visit a protected official service to request prices. Saved quotes are timestamped snapshots."),TEXT("WORKSHOP · Select exact inputs in your bag. Preview, close and arm, then make a fresh context hold."),TEXT("PROCUREMENT · Core at City; two Toolkits at West. Endorsements stay with you through death."),TEXT("CITY TREASURY · Protected bank and bound Charter checkout. Deposit fee ceil(2%); withdrawals free. No remote access; match continues."),TEXT("PEER EXCHANGE · Both players review and confirm. Only offered items are shared.")};
    FString Hint=Hints[ServiceTab];
    if(ServiceTab==4||ServiceTab==5){Hint+=FString::Printf(TEXT("\nYours: Supply %s · Industrial %s · Witness %s\nCharter %s"),O.SupplyEndorsement.IsValid()?TEXT("recorded"):TEXT("needed"),O.IndustrialEndorsement.IsValid()?TEXT("recorded"):TEXT("needed"),O.Witness.IsValid()?TEXT("recorded"):TEXT("needed"),O.CharterActivation.IsValid()?TEXT("spent"):O.CharterPurchase.IsValid()?TEXT("registered"):TEXT("requires all three + 100 gold"));}
    ServiceHint->SetText(FText::FromString(Hint));
}
