#include "UI/LDInventoryPanel.h"
#include "UI/LDWidgets.h"
#include "Framework/LDPlayerController.h"
#include "Economy/LDEconomyLedger.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/UniformGridPanel.h"
#include "Framework/LDGameMode.h"
#include "Engine/World.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/EditableTextBox.h"
void ULDInventoryPanel::BuildBank(UVerticalBox* All)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
    BankOpenButton=LDWidgets::Button(WidgetTree,TEXT("City Treasury bank · at Guild"),[PC](){PC->OpenBank();});All->AddChildToVerticalBox(BankOpenButton);
    BankPanel=WidgetTree->ConstructWidget<UVerticalBox>();All->AddChildToVerticalBox(BankPanel);
    BankStatus=LDWidgets::Text(WidgetTree,TEXT(""),15);BankPanel->AddChildToVerticalBox(BankStatus);
    BankPanel->AddChildToVerticalBox(LDWidgets::Text(WidgetTree,TEXT("Gold amount (Charter: wallet share 0–100; bank pays the rest)"),14));
    BankAmount=WidgetTree->ConstructWidget<UEditableTextBox>();BankAmount->SetHintText(FText::FromString(TEXT("Whole gold amount")));auto FieldStyle=BankAmount->GetWidgetStyle();FieldStyle.BackgroundImageNormal.TintColor=FSlateColor(FLinearColor(.04f,.08f,.11f));FieldStyle.BackgroundImageHovered.TintColor=FieldStyle.BackgroundImageNormal.TintColor;FieldStyle.BackgroundImageFocused.TintColor=FieldStyle.BackgroundImageNormal.TintColor;FieldStyle.ForegroundColor=FSlateColor(FLinearColor::White);BankAmount->SetWidgetStyle(FieldStyle);auto* Field=WidgetTree->ConstructWidget<USizeBox>();Field->SetMinDesiredHeight(44);Field->SetContent(BankAmount);BankPanel->AddChildToVerticalBox(Field);
    auto* Choices=WidgetTree->ConstructWidget<UUniformGridPanel>();Choices->SetSlotPadding(FMargin(3));BankPanel->AddChildToVerticalBox(Choices);
    Choices->AddChildToUniformGrid(LDWidgets::Button(WidgetTree,TEXT("Review deposit"),[this](){ReviewBank(true);}),0,0);
    Choices->AddChildToUniformGrid(LDWidgets::Button(WidgetTree,TEXT("Review withdrawal"),[this](){ReviewBank(false);}),0,1);
    Choices->AddChildToUniformGrid(LDWidgets::Button(WidgetTree,TEXT("Review Charter · 100g"),[this](){ReviewBank(false,true);}),1,0);
    BankConfirm=LDWidgets::Button(WidgetTree,TEXT("Confirm payment"),[this,PC](){if(bBankReviewed){if(bCharterReview){PC->ConfirmCharter(BankReviewedAmount,BankReviewedWallet,BankReviewedBalance);}else{PC->ConfirmBank(BankReviewedAmount,bBankDeposit,BankReviewedWallet,BankReviewedBalance);}bBankReviewed=false;Refresh();}});Choices->AddChildToUniformGrid(BankConfirm,1,1);
    BankPanel->AddChildToVerticalBox(LDWidgets::Button(WidgetTree,TEXT("Close Treasury"),[this,PC](){PC->CloseBank();bBankReviewed=false;Refresh();}));

}
void ULDInventoryPanel::ReviewBank(bool Deposit,bool Charter)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());bBankReviewed=false;const FString Text=BankAmount->GetText().ToString();int64 Amount=0;
    if(Text.IsEmpty()||Text.Len()>19||PC->InventoryPending()||!PC->BankView.Session.IsValid()){return;}
    for(TCHAR C:Text){if(C<'0'||C>'9'||Amount>(MAX_int64-(C-'0'))/10){PC->BankFeedback=TEXT("Enter a positive whole gold amount");Refresh();return;}Amount=Amount*10+(C-'0');}
    if(Charter){const auto* Public=GetWorld()->GetGameState<ALDGameState>();if(!Public||Public->GetServerWorldTimeSeconds()+94>Public->MatchDeadline){PC->BankFeedback=TEXT("Too late: Charter needs 94 seconds for activation and a full public opening");Refresh();return;}}
    const auto& Q=PC->BankView;const int64 Fee=Deposit?LDEconomy::DepositFee(Amount):0;
    if(Charter?(Amount>100||Amount>Q.Wallet||100-Amount>Q.Bank||!PC->OwnerState.Witness.IsValid()||!PC->OwnerState.IndustrialEndorsement.IsValid()||!PC->OwnerState.SupplyEndorsement.IsValid()||PC->OwnerState.CharterPurchase.IsValid()):(Amount<=0||Fee<0||(Deposit?(Amount<=Fee||Amount>Q.Wallet||Q.Bank>MAX_int64-(Amount-Fee)):(Amount>Q.Bank||Q.Wallet>MAX_int64-Amount)))){PC->BankFeedback=Charter?TEXT("Charter needs Supply, Industrial, Witness and an affordable 100-gold split"):TEXT("Invalid amount or insufficient funds");Refresh();return;}
    bCharterReview=Charter;BankReviewedAmount=Amount;bBankDeposit=Deposit;BankReviewedWallet=Q.WalletRevision;BankReviewedBalance=Q.BankRevision;BankReviewedSession=Q.Session;bBankReviewed=true;Refresh();BankConfirm->SetKeyboardFocus();
}
void ULDInventoryPanel::RefreshBank()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto& Q=PC->BankView;const bool Open=PC->BankIsOpen();
    BankOpenButton->SetIsEnabled(PC->NearGuildVendor()&&!PC->InventoryPending()&&!Open&&!bConfirm&&!bTradeReview);
    BankPanel->SetVisibility(Open?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(!Open||Q.Session!=BankReviewedSession||Q.WalletRevision!=BankReviewedWallet||Q.BankRevision!=BankReviewedBalance){bBankReviewed=false;}
    int64 Entered=0;const FString Text=BankAmount->GetText().ToString();bool Valid=!Text.IsEmpty()&&Text.Len()<=19;if(Valid){for(TCHAR C:Text){if(C<'0'||C>'9'||Entered>(MAX_int64-(C-'0'))/10){Valid=false;break;}Entered=Entered*10+(C-'0');}}
    if(!Valid||Entered!=BankReviewedAmount){bBankReviewed=false;}
    const FString Review=bBankReviewed&&bCharterReview?FString::Printf(TEXT("\nCONFIRM REGISTERED CHARTER · Wallet %lld + bank %lld = 100 gold burned. Bound registration; activate once at City. Activation needs a full 4+30+60 seconds before deadline; delay can forfeit it. No refund or automatic win."),BankReviewedAmount,100-BankReviewedAmount):bBankReviewed?FString::Printf(TEXT("\nConfirm %s %lld · Fee %lld · %s %lld"),bBankDeposit?TEXT("deposit"):TEXT("withdrawal"),BankReviewedAmount,bBankDeposit?LDEconomy::DepositFee(BankReviewedAmount):0,bBankDeposit?TEXT("Bank receives"):TEXT("Wallet receives"),bBankDeposit?BankReviewedAmount-LDEconomy::DepositFee(BankReviewedAmount):BankReviewedAmount):TEXT("\nChoose an amount, review, then separately confirm.");
    BankStatus->SetText(FText::FromString(FString::Printf(TEXT("CITY TREASURY · Wallet %lld · Protected bank %lld\nDeposit fee: ceil(2%%) · Withdrawals free · %s%s"),Q.Wallet,Q.Bank,*PC->BankFeedback,*Review)));
    CastChecked<UTextBlock>(BankConfirm->GetContent())->SetText(FText::FromString(PC->InventoryPending()?TEXT("Pending…"):bBankReviewed?TEXT("Confirm payment"):TEXT("Review first")));
    BankConfirm->SetIsEnabled(bBankReviewed&&!PC->InventoryPending()&&!bConfirm&&!bTradeReview);
    BankAmount->SetIsEnabled(!PC->InventoryPending());
}
