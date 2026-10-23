#include "UI/LDInventoryPanel.h"
#include "UI/LDWidgets.h"
#include "UI/LDControlSettings.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"

TSharedRef<SWidget> ULDInventoryPanel::RebuildWidget()
{
    if(!Heading)
    {
        SetIsFocusable(true);auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Canvas;
        auto* Border=LDWidgets::Panel(WidgetTree,nullptr);MainPanel=Border;auto* PanelSlot=Canvas->AddChildToCanvas(Border);PanelSlot->SetAnchors(FAnchors(.04f,.16f,.96f,.96f));PanelSlot->SetOffsets(FMargin(0));
        auto* All=WidgetTree->ConstructWidget<UVerticalBox>();Border->SetContent(All);Heading=LDWidgets::Text(WidgetTree,TEXT("YOUR CARRIED BAG"),24);All->AddChildToVerticalBox(Heading);
        Status=LDWidgets::Text(WidgetTree,TEXT(""));All->AddChildToVerticalBox(Status);
        auto Button=[&](const FString& S,TFunction<void()> Action){return LDWidgets::Button(WidgetTree,S,MoveTemp(Action));};
        auto* Tools=WidgetTree->ConstructWidget<UHorizontalBox>();All->AddChildToVerticalBox(Tools)->SetPadding(FMargin(0,8));Search=WidgetTree->ConstructWidget<UEditableTextBox>();Search->SetHintText(FText::FromString(TEXT("Search your items")));Search->OnTextChanged.AddDynamic(this,&ULDInventoryPanel::SearchChanged);Tools->AddChildToHorizontalBox(Search)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto SearchStyle=Search->GetWidgetStyle();SearchStyle.BackgroundImageNormal.TintColor=FSlateColor(FLinearColor(.04f,.08f,.11f));SearchStyle.BackgroundImageHovered.TintColor=SearchStyle.BackgroundImageNormal.TintColor;SearchStyle.BackgroundImageFocused.TintColor=SearchStyle.BackgroundImageNormal.TintColor;SearchStyle.ForegroundColor=FSlateColor(FLinearColor::White);Search->SetWidgetStyle(SearchStyle);
        const TCHAR* Labels[]={TEXT("All"),TEXT("Books"),TEXT("Wards"),TEXT("Objective"),TEXT("Cargo")};for(uint8 I=0;I<5;++I){Tools->AddChildToHorizontalBox(Button(Labels[I],[this,I](){Filter(I);}))->SetPadding(FMargin(3,0));}
        SortButton=Button(TEXT("Sort"),[PC](){PC->InventoryCommand(LDInventory::Sort);});Tools->AddChildToHorizontalBox(SortButton);
        VendorOpen=Button(TEXT("Guild books"),[PC](){PC->OpenVendor();});Tools->AddChildToHorizontalBox(VendorOpen)->SetPadding(FMargin(6,0));
        CatalogPanel=WidgetTree->ConstructWidget<UVerticalBox>();All->AddChildToVerticalBox(CatalogPanel)->SetPadding(FMargin(0,4));
        auto* CatalogRows=WidgetTree->ConstructWidget<UHorizontalBox>();CatalogPanel->AddChildToVerticalBox(CatalogRows);
        for(auto Skill:{ELDSkill::Step,ELDSkill::Spark,ELDSkill::Patch,ELDSkill::Parley}){auto* B=Button(TEXT(""),[this,Skill](){SelectOffer(Skill);});Offers.Add(B);CatalogRows->AddChildToHorizontalBox(B)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));}
        auto* QuoteRow=WidgetTree->ConstructWidget<UHorizontalBox>();CatalogPanel->AddChildToVerticalBox(QuoteRow)->SetPadding(FMargin(0,4));CatalogInfo=LDWidgets::Text(WidgetTree,TEXT(""),15);QuoteRow->AddChildToHorizontalBox(CatalogInfo)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));VendorBuy=Button(TEXT("Buy"),[this](){BuyOffer();});QuoteRow->AddChildToHorizontalBox(VendorBuy)->SetPadding(FMargin(8,0));

        BuildCommerce(All);
        BuildBank(All);
        BuildTradePanel(All);
        auto* Body=WidgetTree->ConstructWidget<UHorizontalBox>();All->AddChildToVerticalBox(Body)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Body->AddChildToHorizontalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));Grid=WidgetTree->ConstructWidget<UUniformGridPanel>();Grid->SetSlotPadding(FMargin(4));Scroll->AddChild(Grid);
        auto* RightSize=WidgetTree->ConstructWidget<USizeBox>();RightSize->SetWidthOverride(480);auto* Right=WidgetTree->ConstructWidget<UVerticalBox>();auto* ActionsScroll=WidgetTree->ConstructWidget<UScrollBox>();ActionsScroll->AddChild(Right);RightSize->SetContent(ActionsScroll);Body->AddChildToHorizontalBox(RightSize)->SetPadding(FMargin(16,0,0,0));
        Detail=LDWidgets::Text(WidgetTree,TEXT("Choose one of your items"),18);Detail->SetWrapTextAt(460);auto* DetailScroll=WidgetTree->ConstructWidget<UScrollBox>();DetailScroll->AddChild(Detail);auto* DetailSize=WidgetTree->ConstructWidget<USizeBox>();DetailSize->SetHeightOverride(96);DetailSize->SetContent(DetailScroll);Right->AddChildToVerticalBox(DetailSize);
        UseButton=Button(TEXT("Use"),[this](){Use();});Right->AddChildToVerticalBox(UseButton)->SetPadding(FMargin(0,8));
        BuildProduction(Right);
        auto* AssignGrid=WidgetTree->ConstructWidget<UUniformGridPanel>();AssignGrid->SetSlotPadding(FMargin(2));Right->AddChildToVerticalBox(AssignGrid);for(uint8 I=0;I<4;++I){auto* B=Button(TEXT(""),[this,PC,I](){PC->InventoryCommand(LDInventory::Bind,Selected,I);});Assignments.Add(B);AssignGrid->AddChildToUniformGrid(B,I/2,I%2);}
        auto* Unassign=Button(TEXT("Unassign shortcut"),[this,PC](){PC->InventoryCommand(LDInventory::Unbind,Selected);});Organizations.Add(Unassign);Right->AddChildToVerticalBox(Unassign)->SetPadding(FMargin(0,4));
        auto* Move=Button(TEXT("Move to first empty slot"),[this,PC](){for(uint8 I=0;I<PC->OwnerState.Inventory.Capacity;++I){if(!PC->OwnerState.Inventory.Items.ContainsByPredicate([&](const auto& Item){return Item.Slot==I;})){PC->InventoryCommand(LDInventory::Move,Selected,I);return;}}PC->IntentFeedback=TEXT("Inventory full");});Organizations.Add(Move);Right->AddChildToVerticalBox(Move);
        DropButton=Button(TEXT("Drop…"),[this](){AskDrop();});Right->AddChildToVerticalBox(DropButton)->SetPadding(FMargin(0,8));
        ConfirmPanel=WidgetTree->ConstructWidget<UBorder>();ConfirmPanel->SetBrushColor(FLinearColor(.20f,.09f,.04f));auto* Confirmation=WidgetTree->ConstructWidget<UVerticalBox>();ConfirmPanel->SetContent(Confirmation);ConfirmText=LDWidgets::Text(WidgetTree,TEXT(""));Confirmation->AddChildToVerticalBox(ConfirmText);
        auto* Choices=WidgetTree->ConstructWidget<UHorizontalBox>();Confirmation->AddChildToVerticalBox(Choices);ConfirmButton=Button(TEXT("Confirm drop"),[this](){ConfirmDrop();});Choices->AddChildToHorizontalBox(ConfirmButton);Choices->AddChildToHorizontalBox(Button(TEXT("Cancel"),[this](){bConfirm=false;bDirty=true;SetKeyboardFocus();}))->SetPadding(FMargin(8,0));ConfirmText->SetWrapTextAt(540);ConfirmPanel->SetPadding(FMargin(20));auto* ModalSlot=Canvas->AddChildToCanvas(ConfirmPanel);ModalSlot->SetAnchors(FAnchors(.5f,.5f));ModalSlot->SetAlignment(FVector2D(.5));ModalSlot->SetAutoSize(true);ModalSlot->SetZOrder(10);
        BuildTradeReview(Canvas);
        auto* Footer=WidgetTree->ConstructWidget<UHorizontalBox>();All->AddChildToVerticalBox(Footer)->SetPadding(FMargin(0,8));for(uint8 I=0;I<4;++I){auto* B=Button(TEXT(""),[this,PC,I](){if(Selected.IsValid()){PC->InventoryCommand(LDInventory::Bind,Selected,I);}});Quick.Add(B);Footer->AddChildToHorizontalBox(B)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));}
        Footer->AddChildToHorizontalBox(Button(TEXT("Character"),[this,PC](){Close();PC->ToggleDevelopmentPanel();}))->SetPadding(FMargin(8,0));Footer->AddChildToHorizontalBox(Button(TEXT("Close"),[this](){Close();}));
        Feedback=LDWidgets::Text(WidgetTree,TEXT("The match continues. Drag outside cancels; Drop requires confirmation."),14);All->AddChildToVerticalBox(Feedback);
    }
    return Super::RebuildWidget();
}
void ULDInventoryPanel::Open(){bOpen=true;bDirty=true;bConfirm=bTradeReview=false;SetVisibility(ESlateVisibility::Visible);auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());PC->SetMenuOpen(true);SetKeyboardFocus();Refresh();}
void ULDInventoryPanel::Close(){CastChecked<ALDPlayerController>(GetOwningPlayer())->CloseCommerce();CastChecked<ALDPlayerController>(GetOwningPlayer())->CloseBank();bBankReviewed=false;CastChecked<ALDPlayerController>(GetOwningPlayer())->CancelTrade();CastChecked<ALDPlayerController>(GetOwningPlayer())->CloseVendor();CancelDrag();bOpen=bConfirm=bTradeReview=false;if(!CastChecked<ALDPlayerController>(GetOwningPlayer())->TradeView.Active()){CastChecked<ALDPlayerController>(GetOwningPlayer())->TradeView=FLDTradeView();}SetVisibility(ESlateVisibility::Collapsed);CastChecked<ALDPlayerController>(GetOwningPlayer())->SetMenuOpen(false);}
void ULDInventoryPanel::CancelDrag(){const bool Captured=bPointerDown;bPointerDown=bDragging=false;Dragged.Invalidate();if(Captured&&FSlateApplication::IsInitialized()){FSlateApplication::Get().ReleaseAllPointerCapture();}}
void ULDInventoryPanel::Select(FGuid Id){Selected=Id;bDirty=true;}
void ULDInventoryPanel::Filter(uint8 Kind){FilterKind=Kind;bDirty=true;}
void ULDInventoryPanel::SearchChanged(const FText& Value){bDirty=true;}
FString ULDInventoryPanel::DetailText() const{return Detail?Detail->GetText().ToString():FString();}
void ULDInventoryPanel::Use()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto* Item=PC->OwnerState.Inventory.Find(Selected);if(!Item||Item->bReserved||PC->InventoryPending()){return;}const auto Kind=Item->Kind;const FGuid Id=Item->Id;
    if(!LDInventory::QuickUsable(Kind)){return;}Close();if(Kind==ELDItemKind::Book){PC->ArmRead(Id);}else{PC->ArmWard(Id);}
}
void ULDInventoryPanel::AskDrop()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());if(const auto* I=PC->OwnerState.Inventory.Find(Selected)){DropItem=I->Id;DropItemRevision=I->Revision;DropInventoryRevision=PC->OwnerState.Inventory.Revision;bConfirm=true;CancelDrag();bDirty=true;Refresh();ConfirmButton->SetKeyboardFocus();}
}
void ULDInventoryPanel::ConfirmDrop()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto* I=PC->OwnerState.Inventory.Find(DropItem);if(!bConfirm||!I||PC->InventoryPending()||I->Revision!=DropItemRevision||PC->OwnerState.Inventory.Revision!=DropInventoryRevision||!PC->GetPawn()){return;}
    // No hidden click through the modal: confirmation names this exact snapshot.
    PC->InventoryCommand(LDInventory::Drop,DropItem,0,PC->GetPawn()->GetActorLocation()+PC->GetPawn()->GetActorForwardVector()*50);bConfirm=false;bDirty=true;
}
void ULDInventoryPanel::Refresh()
{
    if(!bOpen||!Heading){return;}auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto& O=PC->OwnerState;const auto& I=O.Inventory;const auto* G=GetWorld()->GetGameState<ALDGameState>();if(!G||G->EndReason){Close();return;}
    if(SeenLife!=O.Life||!O.bActive){CancelDrag();bConfirm=false;}SeenLife=O.Life;
    int32 W=0,H=0;PC->GetViewportSize(W,H);const uint8 Desired=W<=1280?4:6;bool Changed=Columns!=Desired||Cells.Num()!=I.Capacity;
    if(Changed){Columns=Desired;Grid->ClearChildren();Cells.Reset();for(uint8 N=0;N<I.Capacity;++N){auto* B=LDWidgets::Button(WidgetTree,TEXT(""),[this,PC,N](){const auto* Entry=PC->OwnerState.Inventory.Items.FindByPredicate([&](const auto& E){return E.Slot==N;});Select(Entry?Entry->Id:FGuid());});CastChecked<UTextBlock>(B->GetContent())->SetAutoWrapText(true);CastChecked<UTextBlock>(B->GetContent())->SetWrapTextAt(170);auto Font=CastChecked<UTextBlock>(B->GetContent())->GetFont();Font.Size=FMath::RoundToInt(16*AppliedScale);CastChecked<UTextBlock>(B->GetContent())->SetFont(Font);auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetMinDesiredWidth(150);Size->SetMinDesiredHeight(78);Size->SetContent(B);auto* CellSlot=Grid->AddChildToUniformGrid(Size,N/Columns,N%Columns);CellSlot->SetHorizontalAlignment(HAlign_Fill);CellSlot->SetVerticalAlignment(VAlign_Fill);Cells.Add(B);}bDirty=true;}
    const float Scale=PC->Controls?PC->Controls->TextScale:1;if(Scale!=AppliedScale){TArray<UWidget*> Widgets;WidgetTree->GetAllWidgets(Widgets);for(auto* Widget:Widgets){if(auto* T=Cast<UTextBlock>(Widget)){auto Font=T->GetFont();Font.Size=FMath::RoundToInt(Font.Size*Scale/AppliedScale);T->SetFont(Font);}}AppliedScale=Scale;bDirty=true;}

    RefreshProduction();RefreshCommerce();RefreshBank();RefreshTradePanel();const bool Shop=PC->VendorIsOpen();CatalogPanel->SetVisibility(Shop?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    Heading->SetText(FText::FromString(Shop?TEXT("GUILD BOOKSHOP · YOUR CARRIED BAG"):PC->TradeView.Session.IsValid()&&(PC->TradeView.Active()||PC->TradeView.Stage==ELDTradeStage::Committed)?TEXT("PEER EXCHANGE · YOUR CARRIED BAG"):TEXT("YOUR CARRIED BAG")));
    VendorOpen->SetIsEnabled(PC->NearGuildVendor()&&!PC->InventoryPending()&&!bConfirm&&!Shop);
    if(Shop)
    {
        const auto& Q=PC->VendorQuote;const auto* SelectedQuote=Q.Offers.FindByPredicate([&](const auto& Offer){return Offer.Skill==SelectedOffer;});
        DisplayedCatalogRevision=Q.CatalogRevision;DisplayedPrice=SelectedQuote?SelectedQuote->Price:0;
        for(int32 N=0;N<Offers.Num();++N){const bool Has=Q.Offers.IsValidIndex(N);CastChecked<UTextBlock>(Offers[N]->GetContent())->SetText(FText::FromString(Has?FString::Printf(TEXT("%s%s · %d gold"),Q.Offers[N].Skill==SelectedOffer?TEXT("› "):TEXT(""),LDDevelopment::SkillName(Q.Offers[N].Skill),Q.Offers[N].Price):TEXT("Opening catalog…")));Offers[N]->SetIsEnabled(Has&&!PC->VendorPending());}
        const FString Preview=SelectedQuote?FString::Printf(TEXT("Book: %s · %s · Unlimited stock\nTeaches R1: hold 3s after 6 quiet seconds; does not equip or cast.\n%s"),LDDevelopment::SkillName(SelectedOffer),LDDevelopment::Rank(O.Build,SelectedOffer)?TEXT("Already learned · may buy for trade"):TEXT("Not learned"),*PC->VendorFeedback):PC->VendorFeedback;
        FString CapacityReason;const bool CanCarry=LDInventory::Fits(I,O.Build,{}, {ELDItemKind::Book},&CapacityReason);
        CatalogInfo->SetText(FText::FromString(Preview+(!CanCarry?TEXT("\n")+CapacityReason:O.Gold<DisplayedPrice?TEXT("\nNot enough gold"):TEXT(""))));CastChecked<UTextBlock>(VendorBuy->GetContent())->SetText(FText::FromString(PC->VendorPending()?TEXT("Pending…"):PC->VendorNeedsReview?TEXT("Review new price"):FString::Printf(TEXT("Buy one · %d gold"),DisplayedPrice)));
        VendorBuy->SetIsEnabled(SelectedQuote&&!PC->InventoryPending()&&CanCarry&&O.Gold>=DisplayedPrice);
    }
    const bool Pending=PC->InventoryPending();const bool Active=O.bActive&&G->GetServerWorldTimeSeconds()>=O.ProtectedUntil&&!O.DevelopmentKind&&!O.ChannelKind&&!(O.ActionCue&15);
    Status->SetText(FText::FromString(FString::Printf(TEXT("%d/%d slots · %d/%d weight · Gold %lld · %s · Match continues"),I.Items.Num(),I.Capacity,LDInventory::Weight(I),LDInventory::WeightCapacity(O.Build),O.Gold,Pending?TEXT("Pending…"):O.bActive?TEXT("Alive"):TEXT("Waiting to return · organization only"))));
    Feedback->SetText(FText::FromString(PC->TradeView.Active()||PC->TradeView.Stage==ELDTradeStage::Committed?PC->TradeView.Message:PC->IntentFeedback));const auto* SelectedItem=I.Find(Selected);if(!SelectedItem&&Selected.IsValid()){Selected.Invalidate();bConfirm=false;bDirty=true;}
    const bool Usable=SelectedItem&&!SelectedItem->bReserved&&!Pending&&!bConfirm;
    UseButton->SetIsEnabled(Usable&&Active&&LDInventory::QuickUsable(SelectedItem->Kind));DropButton->SetIsEnabled(Usable&&Active);SortButton->SetIsEnabled(!Pending&&!bConfirm);
    for(auto B:Organizations){B->SetIsEnabled(Usable);}for(uint8 N=0;N<4;++N){Assignments[N]->SetIsEnabled(Usable&&LDInventory::QuickUsable(SelectedItem->Kind));CastChecked<UTextBlock>(Assignments[N]->GetContent())->SetText(FText::FromString(TEXT("Assign to ")+PC->BindingLabel(FName(*FString::Printf(TEXT("Item%d"),N+1)))));}
    MainPanel->SetIsEnabled(!bConfirm&&!bTradeReview);ConfirmPanel->SetVisibility(bConfirm?ESlateVisibility::Visible:ESlateVisibility::Collapsed);ConfirmButton->SetIsEnabled(bConfirm&&Active&&!Pending&&I.Revision==DropInventoryRevision&&I.Find(DropItem)&&I.Find(DropItem)->Revision==DropItemRevision);if(bConfirm&&I.Find(DropItem)){ConfirmText->SetText(FText::FromString(TEXT("Drop ")+LDInventory::Name(I.Find(DropItem)->Kind,I.Find(DropItem)->Skill)+TEXT("? This removes it from your bag.")));}
    if(!bDirty&&SeenInventory==I.Revision){return;}SeenInventory=I.Revision;SeenOwner=O.Revision;bDirty=false;
    const FString Query=Search->GetText().ToString();for(uint8 N=0;N<Cells.Num();++N){const auto* Item=I.Items.FindByPredicate([&](const auto& E){return E.Slot==N;});const FString Name=Item?LDInventory::Name(Item->Kind,Item->Skill):TEXT("Empty");const bool Match=!Item||((FilterKind==0||(FilterKind==4?LDInventory::Commodity(Item->Kind):uint8(Item->Kind)+1==FilterKind))&&(Query.IsEmpty()||Name.Contains(Query)));
        FString Text=FString::Printf(TEXT("%d · %s%s"),N+1,Item&&Item->Id==Selected?TEXT("› "):TEXT(""),Match?*Name:TEXT("Filtered"));if(Item&&Match){const int32 Q=I.Quick.IndexOfByKey(Item->Id);Text+=Item->bReserved?TEXT("\nBlocked: reserved"):Q>=0?TEXT("\nShortcut ")+PC->BindingLabel(FName(*FString::Printf(TEXT("Item%d"),Q+1))):TEXT("\nOwned");}CastChecked<UTextBlock>(Cells[N]->GetContent())->SetText(FText::FromString(Text));Cells[N]->SetIsEnabled(Match&&!bConfirm);}
    for(uint8 N=0;N<4;++N){const auto* Item=I.Find(I.Quick[N]);CastChecked<UTextBlock>(Quick[N]->GetContent())->SetText(FText::FromString(TEXT("ITEM [")+PC->BindingLabel(FName(*FString::Printf(TEXT("Item%d"),N+1)))+TEXT("]\n")+(Item?LDInventory::Name(Item->Kind,Item->Skill):TEXT("Empty"))));Quick[N]->SetIsEnabled(!Pending&&!bConfirm&&(!SelectedItem||LDInventory::QuickUsable(SelectedItem->Kind)));}
    Detail->SetText(FText::FromString(SelectedItem?LDInventory::Name(SelectedItem->Kind,SelectedItem->Skill)+TEXT("\n")+(SelectedItem->Kind==ELDItemKind::Book?FString(TEXT("Teaches "))+LDDevelopment::SkillName(SelectedItem->Skill)+TEXT(" rank 1. Hold for 3 seconds after 6 quiet seconds. Does not equip or cast. Known books are kept."):SelectedItem->Kind==ELDItemKind::WardCharge?TEXT("One finite placement. Confirm ground within 96 cm; sight 560 cm for 60 seconds. Cancel/failure keeps this charge."):LDInventory::Commodity(SelectedItem->Kind)?TEXT("Commercial cargo. Trade, production or delivery input; drops on death. Cannot be quick-used."):TEXT("One per carrier. Carry to the Heart. Cannot be assigned or used as a reusable shortcut."))+TEXT("\n")+(SelectedItem->bReserved?TEXT("Blocked: reserved"):FString::Printf(TEXT("Owned · one item · Weight %d"),LDInventory::UnitWeight(SelectedItem->Kind))):TEXT("Choose one of your items.\nDrag between bag cells or onto an item shortcut. Drag outside cancels.\nUse/Assign/Unassign/Move/Drop also work with buttons.")));
    CastChecked<UTextBlock>(UseButton->GetContent())->SetText(FText::FromString(SelectedItem&&!LDInventory::QuickUsable(SelectedItem->Kind)?TEXT("No quick use"):SelectedItem&&SelectedItem->Kind==ELDItemKind::Book?TEXT("Learn · hold after selecting"):TEXT("Use · select ward location")));
}
int32 ULDInventoryPanel::HitCell(FVector2D P) const{if(!Grid->GetParent()->GetCachedGeometry().IsUnderLocation(P)){return INDEX_NONE;}for(int32 I=0;I<Cells.Num();++I){if(Cells[I]->GetIsEnabled()&&Cells[I]->GetCachedGeometry().IsUnderLocation(P)){return I;}}return INDEX_NONE;}
FReply ULDInventoryPanel::NativeOnPreviewMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
    if(E.GetEffectingButton()==EKeys::LeftMouseButton&&!bConfirm&&!bTradeReview){const int32 N=HitCell(E.GetScreenSpacePosition());if(N>=0){auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto* I=PC->OwnerState.Inventory.Items.FindByPredicate([&](const auto& Item){return Item.Slot==N;});Select(I?I->Id:FGuid());if(I&&!I->bReserved&&!PC->InventoryPending()){Dragged=I->Id;bPointerDown=true;DragStart=E.GetScreenSpacePosition();return FReply::Handled().CaptureMouse(TakeWidget());}return FReply::Handled();}}
    return Super::NativeOnPreviewMouseButtonDown(G,E);
}
FReply ULDInventoryPanel::NativeOnMouseMove(const FGeometry& G,const FPointerEvent& E)
{if(bPointerDown&&FVector2D::DistSquared(DragStart,E.GetScreenSpacePosition())>64){bDragging=true;}return bPointerDown?FReply::Handled():Super::NativeOnMouseMove(G,E);}
FReply ULDInventoryPanel::NativeOnMouseButtonUp(const FGeometry& G,const FPointerEvent& E)
{
    if(E.GetEffectingButton()==EKeys::LeftMouseButton&&bPointerDown){auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());bool Applied=false;if(bDragging){for(uint8 I=0;I<4;++I){if(Quick[I]->GetCachedGeometry().IsUnderLocation(E.GetScreenSpacePosition())){PC->InventoryCommand(LDInventory::Bind,Dragged,I);Applied=true;break;}}const int32 N=HitCell(E.GetScreenSpacePosition());if(!Applied&&N>=0){PC->InventoryCommand(LDInventory::Move,Dragged,uint8(N));Applied=true;}if(!Applied){PC->IntentFeedback=TEXT("Drag cancelled; item kept");}}CancelDrag();return FReply::Handled().ReleaseMouseCapture();}return Super::NativeOnMouseButtonUp(G,E);
}
void ULDInventoryPanel::NativeOnMouseCaptureLost(const FCaptureLostEvent& E){CancelDrag();Super::NativeOnMouseCaptureLost(E);}
void ULDInventoryPanel::NativeOnFocusLost(const FFocusEvent& E){CancelDrag();Super::NativeOnFocusLost(E);}
FReply ULDInventoryPanel::NativeOnKeyDown(const FGeometry& G,const FKeyEvent& E)
{auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());if(E.GetKey()==EKeys::Escape){if(bTradeReview){bTradeReview=false;Refresh();if(ConfirmTradeButton->IsVisible()&&ConfirmTradeButton->GetIsEnabled()){ConfirmTradeButton->SetKeyboardFocus();}else{SetKeyboardFocus();}}else if(bConfirm){bConfirm=false;bDirty=true;Refresh();DropButton->SetKeyboardFocus();}else{Close();}return FReply::Handled();}if(E.GetKey()==PC->BindingKey(TEXT("Inventory"))&&!Search->HasKeyboardFocus()&&!Search->HasFocusedDescendants()&&!GoldInput->HasKeyboardFocus()&&!GoldInput->HasFocusedDescendants()&&!BankAmount->HasKeyboardFocus()&&!BankAmount->HasFocusedDescendants()){Close();return FReply::Handled();}return Super::NativeOnKeyDown(G,E);}

void ULDInventoryPanel::SelectOffer(ELDSkill Skill){SelectedOffer=Skill;Refresh();}
void ULDInventoryPanel::BuyOffer(){auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());if(PC->VendorNeedsReview){PC->VendorNeedsReview=false;PC->VendorFeedback=TEXT("Price refreshed · Click Buy again to confirm the shown price");Refresh();return;}PC->BuyVendorBook(SelectedOffer,DisplayedCatalogRevision,DisplayedPrice);Refresh();}
FString ULDInventoryPanel::VendorText() const{return CatalogInfo?CatalogInfo->GetText().ToString():FString();}

UWidget* ULDInventoryPanel::FocusScope() const{return bTradeReview?TradeReview.Get():bConfirm?ConfirmPanel.Get():MainPanel.Get();}

FReply ULDInventoryPanel::NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E){if(E.GetKey()==EKeys::Escape){return NativeOnKeyDown(G,E);}return Super::NativeOnPreviewKeyDown(G,E);}
