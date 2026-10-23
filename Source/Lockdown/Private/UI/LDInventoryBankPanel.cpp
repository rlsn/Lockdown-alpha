#include "UI/LDInventoryPanel.h"
#include "UI/LDWidgets.h"
#include "Framework/LDPlayerController.h"
#include "Economy/LDEconomyLedger.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
void ULDInventoryPanel::BuildBank(UVerticalBox* All)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
    BankOpenButton=LDWidgets::Button(WidgetTree,TEXT("City Treasury bank · at Guild"),[PC](){PC->OpenBank();});All->AddChildToVerticalBox(BankOpenButton);
    BankPanel=WidgetTree->ConstructWidget<UVerticalBox>();All->AddChildToVerticalBox(BankPanel);
    BankStatus=LDWidgets::Text(WidgetTree,TEXT(""),15);BankPanel->AddChildToVerticalBox(BankStatus);
    auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();BankPanel->AddChildToVerticalBox(Row);
    BankAmount=WidgetTree->ConstructWidget<UEditableTextBox>();BankAmount->SetHintText(FText::FromString(TEXT("Whole gold amount")));Row->AddChildToHorizontalBox(BankAmount)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Row->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("Review deposit"),[this](){ReviewBank(true);}));
    Row->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("Review withdrawal"),[this](){ReviewBank(false);}));
    BankConfirm=LDWidgets::Button(WidgetTree,TEXT("Confirm"),[this,PC](){if(bBankReviewed){PC->ConfirmBank(BankReviewedAmount,bBankDeposit,BankReviewedWallet,BankReviewedBalance);bBankReviewed=false;Refresh();}});Row->AddChildToHorizontalBox(BankConfirm);
    Row->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("Close bank"),[this,PC](){PC->CloseBank();bBankReviewed=false;Refresh();}));
}
void ULDInventoryPanel::ReviewBank(bool Deposit)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());bBankReviewed=false;const FString Text=BankAmount->GetText().ToString();int64 Amount=0;
    if(Text.IsEmpty()||Text.Len()>19||PC->InventoryPending()||!PC->BankView.Session.IsValid()){return;}
    for(TCHAR C:Text){if(C<'0'||C>'9'||Amount>(MAX_int64-(C-'0'))/10){PC->BankFeedback=TEXT("Enter a positive whole gold amount");Refresh();return;}Amount=Amount*10+(C-'0');}
    const auto& Q=PC->BankView;const int64 Fee=Deposit?LDEconomy::DepositFee(Amount):0;
    if(Amount<=0||Fee<0||(Deposit?(Amount<=Fee||Amount>Q.Wallet||Q.Bank>MAX_int64-(Amount-Fee)):(Amount>Q.Bank||Q.Wallet>MAX_int64-Amount))){PC->BankFeedback=TEXT("Invalid amount or insufficient funds");Refresh();return;}
    BankReviewedAmount=Amount;bBankDeposit=Deposit;BankReviewedWallet=Q.WalletRevision;BankReviewedBalance=Q.BankRevision;BankReviewedSession=Q.Session;bBankReviewed=true;Refresh();BankConfirm->SetKeyboardFocus();
}
void ULDInventoryPanel::RefreshBank()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto& Q=PC->BankView;const bool Open=PC->BankIsOpen();
    BankOpenButton->SetIsEnabled(PC->NearGuildVendor()&&!PC->InventoryPending()&&!Open&&!bConfirm&&!bTradeReview);
    BankPanel->SetVisibility(Open?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(!Open||Q.Session!=BankReviewedSession||Q.WalletRevision!=BankReviewedWallet||Q.BankRevision!=BankReviewedBalance){bBankReviewed=false;}
    int64 Entered=0;const FString Text=BankAmount->GetText().ToString();bool Valid=!Text.IsEmpty()&&Text.Len()<=19;if(Valid){for(TCHAR C:Text){if(C<'0'||C>'9'||Entered>(MAX_int64-(C-'0'))/10){Valid=false;break;}Entered=Entered*10+(C-'0');}}
    if(!Valid||Entered!=BankReviewedAmount){bBankReviewed=false;}
    const FString Review=bBankReviewed?FString::Printf(TEXT("\nConfirm %s %lld · Fee %lld · %s %lld"),bBankDeposit?TEXT("deposit"):TEXT("withdrawal"),BankReviewedAmount,bBankDeposit?LDEconomy::DepositFee(BankReviewedAmount):0,bBankDeposit?TEXT("Bank receives"):TEXT("Wallet receives"),bBankDeposit?BankReviewedAmount-LDEconomy::DepositFee(BankReviewedAmount):BankReviewedAmount):TEXT("\nChoose an amount, review, then separately confirm.");
    BankStatus->SetText(FText::FromString(FString::Printf(TEXT("CITY TREASURY · Wallet %lld · Protected bank %lld\nDeposit fee: ceil(2%%) · Withdrawals free · %s%s"),Q.Wallet,Q.Bank,*PC->BankFeedback,*Review)));
    BankConfirm->SetIsEnabled(bBankReviewed&&!PC->InventoryPending()&&!bConfirm&&!bTradeReview);
    BankAmount->SetIsEnabled(!PC->InventoryPending());
}
