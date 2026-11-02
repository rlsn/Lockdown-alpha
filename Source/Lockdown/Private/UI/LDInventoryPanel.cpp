#include "UI/LDInventoryPanel.h"
#include "UI/LDWidgets.h"
#include "UI/LDControlSettings.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
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
        MainPanel=LDWidgets::Panel(WidgetTree,nullptr);auto* PanelSlot=Canvas->AddChildToCanvas(MainPanel);PanelSlot->SetAnchors(FAnchors(.04f,.16f,.96f,.96f));PanelSlot->SetOffsets(FMargin(0));
        auto* All=WidgetTree->ConstructWidget<UVerticalBox>();MainPanel->SetContent(All);Heading=LDWidgets::Text(WidgetTree,TEXT("INVENTORY"),20);All->AddChildToVerticalBox(Heading);
        Status=LDWidgets::Text(WidgetTree,TEXT(""));All->AddChildToVerticalBox(Status);
        auto Button=[&](const FString& Label,TFunction<void()> Action){return LDWidgets::Button(WidgetTree,Label,MoveTemp(Action));};
        auto* Tools=WidgetTree->ConstructWidget<UHorizontalBox>();All->AddChildToVerticalBox(Tools)->SetPadding(FMargin(0,8));
        Search=WidgetTree->ConstructWidget<UEditableTextBox>();Search->SetHintText(FText::FromString(TEXT("Search your items")));Search->OnTextChanged.AddDynamic(this,&ULDInventoryPanel::SearchChanged);Tools->AddChildToHorizontalBox(Search)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto SearchStyle=Search->GetWidgetStyle();SearchStyle.BackgroundImageNormal.TintColor=FSlateColor(FLinearColor(.04f,.08f,.11f));SearchStyle.BackgroundImageHovered.TintColor=SearchStyle.BackgroundImageNormal.TintColor;SearchStyle.BackgroundImageFocused.TintColor=SearchStyle.BackgroundImageNormal.TintColor;SearchStyle.ForegroundColor=FSlateColor(FLinearColor::White);Search->SetWidgetStyle(SearchStyle);
        SortButton=Button(TEXT("Sort"),[PC](){PC->InventoryCommand(LDInventory::Sort);});Tools->AddChildToHorizontalBox(SortButton);
        auto* Body=WidgetTree->ConstructWidget<UHorizontalBox>();All->AddChildToVerticalBox(Body)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Scroll->SetClipping(EWidgetClipping::ClipToBounds);Body->AddChildToHorizontalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));Grid=WidgetTree->ConstructWidget<UUniformGridPanel>();Grid->SetSlotPadding(FMargin(3));Scroll->AddChild(Grid);
        DetailWidth=WidgetTree->ConstructWidget<USizeBox>();auto* Right=WidgetTree->ConstructWidget<UVerticalBox>();auto* DetailScroll=WidgetTree->ConstructWidget<UScrollBox>();DetailScroll->SetClipping(EWidgetClipping::ClipToBounds);DetailScroll->AddChild(Right);DetailWidth->SetContent(DetailScroll);Body->AddChildToHorizontalBox(DetailWidth)->SetPadding(FMargin(12,0,0,0));
        Detail=LDWidgets::Text(WidgetTree,TEXT("Choose one of your items"),18);Right->AddChildToVerticalBox(Detail);
        MoveButton=Button(TEXT("Move to first empty slot"),[this,PC](){for(uint8 I=0;I<PC->OwnerState.Inventory.Capacity;++I){if(!PC->OwnerState.Inventory.Items.ContainsByPredicate([&](const auto& Item){return Item.Slot==I;})){PC->InventoryCommand(LDInventory::Move,Selected,I);return;}}PC->IntentFeedback=TEXT("Inventory full");});Right->AddChildToVerticalBox(MoveButton)->SetPadding(FMargin(0,8));
        DropButton=Button(TEXT("Drop…"),[this](){AskDrop();});Right->AddChildToVerticalBox(DropButton)->SetPadding(FMargin(0,8));
        ConfirmPanel=WidgetTree->ConstructWidget<UBorder>();ConfirmPanel->SetBrushColor(FLinearColor(.20f,.09f,.04f));auto* Confirmation=WidgetTree->ConstructWidget<UVerticalBox>();ConfirmPanel->SetContent(Confirmation);ConfirmText=LDWidgets::Text(WidgetTree,TEXT(""));Confirmation->AddChildToVerticalBox(ConfirmText);
        auto* Choices=WidgetTree->ConstructWidget<UHorizontalBox>();Confirmation->AddChildToVerticalBox(Choices);ConfirmButton=Button(TEXT("Confirm drop"),[this](){ConfirmDrop();});Choices->AddChildToHorizontalBox(ConfirmButton);
        Choices->AddChildToHorizontalBox(Button(TEXT("Cancel"),[this](){bConfirm=false;bDirty=true;SetKeyboardFocus();}))->SetPadding(FMargin(8,0));ConfirmText->SetWrapTextAt(540);ConfirmPanel->SetPadding(FMargin(20));auto* ModalSlot=Canvas->AddChildToCanvas(ConfirmPanel);ModalSlot->SetAnchors(FAnchors(.5f,.5f));ModalSlot->SetAlignment(FVector2D(.5));ModalSlot->SetAutoSize(true);ModalSlot->SetZOrder(10);
        All->AddChildToVerticalBox(Button(TEXT("Close"),[this](){Close();}))->SetPadding(FMargin(0,8));
        TArray<UWidget*> Contents;WidgetTree->GetAllWidgets(Contents);for(auto* W:Contents){if(auto* B=Cast<UButton>(W)){if(auto* Caption=Cast<UTextBlock>(B->GetContent())){Caption->SetAutoWrapText(true);Caption->SetJustification(ETextJustify::Center);if(auto* CaptionSlot=Cast<UButtonSlot>(Caption->Slot)){CaptionSlot->SetHorizontalAlignment(HAlign_Fill);}}}}
        Feedback=LDWidgets::Text(WidgetTree,TEXT("The match continues. Drag outside cancels; Drop requires confirmation."),14);All->AddChildToVerticalBox(Feedback);
    }
    return Super::RebuildWidget();
}
void ULDInventoryPanel::Open(){bOpen=true;bDirty=true;bConfirm=false;SetVisibility(ESlateVisibility::Visible);CastChecked<ALDPlayerController>(GetOwningPlayer())->SetMenuOpen(true);SetKeyboardFocus();Refresh();}
void ULDInventoryPanel::Close(){CancelDrag();bOpen=bConfirm=false;SetVisibility(ESlateVisibility::Collapsed);CastChecked<ALDPlayerController>(GetOwningPlayer())->SetMenuOpen(false);}
void ULDInventoryPanel::CancelDrag(){const bool Captured=bPointerDown;bPointerDown=bDragging=false;Dragged.Invalidate();if(Captured&&FSlateApplication::IsInitialized()){FSlateApplication::Get().ReleaseAllPointerCapture();}}
void ULDInventoryPanel::Select(FGuid Id){Selected=Id;bDirty=true;}
void ULDInventoryPanel::SearchChanged(const FText& Value){bDirty=true;}
FString ULDInventoryPanel::DetailText() const{return Detail?Detail->GetText().ToString():FString();}
void ULDInventoryPanel::AskDrop()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());if(const auto* I=PC->OwnerState.Inventory.Find(Selected)){DropItem=I->Id;DropItemRevision=I->Revision;DropInventoryRevision=PC->OwnerState.Inventory.Revision;bConfirm=true;CancelDrag();bDirty=true;Refresh();ConfirmButton->SetKeyboardFocus();}
}
void ULDInventoryPanel::ConfirmDrop()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto* I=PC->OwnerState.Inventory.Find(DropItem);if(!bConfirm||!I||PC->InventoryPending()||I->Revision!=DropItemRevision||PC->OwnerState.Inventory.Revision!=DropInventoryRevision||!PC->GetPawn()){return;}
    int64 NowMs;if(!PC->ReadDemoAuthorityTimeMs(NowMs)){PC->IntentFeedback=TEXT("Authority time unavailable");return;}
    if(PC->OwnerState.LifePhase!=ELDLifePhase::Alive||NowMs<PC->OwnerState.ProtectedUntilMs){PC->IntentFeedback=TEXT("Item action unavailable for the current life");return;}
    PC->InventoryCommand(LDInventory::Drop,DropItem,0,PC->GetPawn()->GetActorLocation()+PC->GetPawn()->GetActorForwardVector()*50);bConfirm=false;bDirty=true;
}
void ULDInventoryPanel::Refresh()
{
    if(!bOpen||!Heading){return;}auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto& O=PC->OwnerState;const auto& I=O.Inventory;const auto* G=GetWorld()->GetGameState<ALDGameState>();
    if(!G||G->DemoLobby.Generation!=O.Match||(G->DemoLobby.Phase!=ELDMatchPhase::Development&&G->DemoLobby.Phase!=ELDMatchPhase::CaptureOpen)||O.LifePhase==ELDLifePhase::Left){Close();return;}
    if(SeenLife!=O.Life||!O.bActive){CancelDrag();bConfirm=false;}SeenLife=O.Life;
    int32 W=0,H=0;PC->GetViewportSize(W,H);const float Logical=FMath::Max(900.f,float(W)/FMath::Max(.1f,UWidgetLayoutLibrary::GetViewportScale(this)));
    const float RightWidth=FMath::Clamp(Logical*.40f,350.f,520.f);DetailWidth->SetWidthOverride(RightWidth);const uint8 Desired=FMath::Clamp(FMath::FloorToInt((Logical*.92f-RightWidth-64)/156),2,6);
    if(Columns!=Desired||Cells.Num()!=I.Capacity)
    {
        Columns=Desired;Grid->ClearChildren();Cells.Reset();for(uint8 N=0;N<I.Capacity;++N){auto* B=LDWidgets::Button(WidgetTree,TEXT(""),[this,PC,N](){const auto* Entry=PC->OwnerState.Inventory.Items.FindByPredicate([&](const auto& E){return E.Slot==N;});Select(Entry?Entry->Id:FGuid());});auto* Caption=CastChecked<UTextBlock>(B->GetContent());Caption->SetAutoWrapText(true);Caption->SetWrapTextAt(170);auto Font=Caption->GetFont();Font.Size=FMath::RoundToInt(16*AppliedScale);Caption->SetFont(Font);auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetMinDesiredWidth(150);Size->SetMinDesiredHeight(78);Size->SetContent(B);auto* CellSlot=Grid->AddChildToUniformGrid(Size,N/Columns,N%Columns);CellSlot->SetHorizontalAlignment(HAlign_Fill);CellSlot->SetVerticalAlignment(VAlign_Fill);Cells.Add(B);}bDirty=true;
    }
    const float Scale=PC->Controls?PC->Controls->TextScale:1;if(Scale!=AppliedScale){TArray<UWidget*> Widgets;WidgetTree->GetAllWidgets(Widgets);for(auto* Widget:Widgets){if(auto* T=Cast<UTextBlock>(Widget)){auto Font=T->GetFont();Font.Size=FMath::RoundToInt(Font.Size*Scale/AppliedScale);T->SetFont(Font);}}AppliedScale=Scale;bDirty=true;}
    int64 NowMs;const bool ClockAvailable=PC->ReadDemoAuthorityTimeMs(NowMs);
    const bool Pending=PC->InventoryPending();const bool Active=ClockAvailable&&O.LifePhase==ELDLifePhase::Alive&&NowMs>=O.ProtectedUntilMs;
    Status->SetText(FText::FromString(FString::Printf(TEXT("%d/%d slots · %d/%d weight · Gold %lld · %s · Match continues"),I.Items.Num(),I.Capacity,LDInventory::Weight(I),LDInventory::WeightCapacity(O.Build),O.Gold,!ClockAvailable?TEXT("Authority time unavailable"):Pending?TEXT("Pending…"):O.LifePhase==ELDLifePhase::Alive?TEXT("Alive"):TEXT("Waiting to return · organization only"))));
    Feedback->SetText(FText::FromString(PC->IntentFeedback));const auto* SelectedItem=I.Find(Selected);if(!SelectedItem&&Selected.IsValid()){Selected.Invalidate();bConfirm=false;bDirty=true;}
    const bool Usable=SelectedItem&&!SelectedItem->bReserved&&!Pending&&!bConfirm;
    MoveButton->SetIsEnabled(Usable);DropButton->SetIsEnabled(Usable&&Active&&!SelectedItem->BindingMask);SortButton->SetIsEnabled(!Pending&&!bConfirm);
    MainPanel->SetIsEnabled(!bConfirm);ConfirmPanel->SetVisibility(bConfirm?ESlateVisibility::Visible:ESlateVisibility::Collapsed);ConfirmButton->SetIsEnabled(bConfirm&&Active&&!Pending&&I.Revision==DropInventoryRevision&&I.Find(DropItem)&&I.Find(DropItem)->Revision==DropItemRevision);
    if(bConfirm&&I.Find(DropItem)){ConfirmText->SetText(FText::FromString(TEXT("Drop ")+LDInventory::Name(I.Find(DropItem)->Kind,I.Find(DropItem)->Skill)+TEXT("? This removes it from your inventory.")));}
    if(!bDirty&&SeenInventory==I.Revision){return;}SeenInventory=I.Revision;bDirty=false;
    const FString Query=Search->GetText().ToString();
    for(uint8 N=0;N<Cells.Num();++N){const auto* Item=I.Items.FindByPredicate([&](const auto& E){return E.Slot==N;});const FString Label=Item?LDInventory::Name(Item->Kind,Item->Skill):TEXT("Empty");const bool Match=!Item||Query.IsEmpty()||Label.Contains(Query);FString Text=FString::Printf(TEXT("%d · %s%s"),N+1,Item&&Item->Id==Selected?TEXT("› "):TEXT(""),Match?*Label:TEXT("Filtered"));if(Item&&Match){Text+=Item->BindingMask?TEXT("\nEQUIPPED · one instance"):Item->bReserved?TEXT("\nReserved"):TEXT("\nOwned");}CastChecked<UTextBlock>(Cells[N]->GetContent())->SetText(FText::FromString(Text));Cells[N]->SetIsEnabled(Match&&!bConfirm);}
    Detail->SetText(FText::FromString(SelectedItem?LDInventory::Name(SelectedItem->Kind,SelectedItem->Skill)+FString::Printf(TEXT("\nOne physical instance · Weight %d%s"),LDInventory::UnitWeight(SelectedItem->Kind),SelectedItem->bReserved?TEXT("\nReserved"):SelectedItem->BindingMask?TEXT("\nEquipped"):TEXT("")):TEXT("Choose one of your items.\nDrag between inventory cells. Drag outside cancels.")));
}
int32 ULDInventoryPanel::HitCell(FVector2D P) const{if(!Grid->GetParent()->GetCachedGeometry().IsUnderLocation(P)){return INDEX_NONE;}for(int32 I=0;I<Cells.Num();++I){if(Cells[I]->GetIsEnabled()&&Cells[I]->GetCachedGeometry().IsUnderLocation(P)){return I;}}return INDEX_NONE;}
FReply ULDInventoryPanel::NativeOnPreviewMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
    if(E.GetEffectingButton()==EKeys::LeftMouseButton&&!bConfirm){const int32 N=HitCell(E.GetScreenSpacePosition());if(N>=0){auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto* I=PC->OwnerState.Inventory.Items.FindByPredicate([&](const auto& Item){return Item.Slot==N;});Select(I?I->Id:FGuid());if(I&&!I->bReserved&&!PC->InventoryPending()){Dragged=I->Id;bPointerDown=true;DragStart=E.GetScreenSpacePosition();return FReply::Handled().CaptureMouse(TakeWidget());}return FReply::Handled();}}
    return Super::NativeOnPreviewMouseButtonDown(G,E);
}
FReply ULDInventoryPanel::NativeOnMouseMove(const FGeometry& G,const FPointerEvent& E)
{if(bPointerDown&&FVector2D::DistSquared(DragStart,E.GetScreenSpacePosition())>64){bDragging=true;}return bPointerDown?FReply::Handled():Super::NativeOnMouseMove(G,E);}
FReply ULDInventoryPanel::NativeOnMouseButtonUp(const FGeometry& G,const FPointerEvent& E)
{
    if(E.GetEffectingButton()==EKeys::LeftMouseButton&&bPointerDown){auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());if(bDragging){const int32 N=HitCell(E.GetScreenSpacePosition());if(N>=0){PC->InventoryCommand(LDInventory::Move,Dragged,uint8(N));}else{PC->IntentFeedback=TEXT("Drag cancelled; item kept");}}CancelDrag();return FReply::Handled().ReleaseMouseCapture();}return Super::NativeOnMouseButtonUp(G,E);
}
void ULDInventoryPanel::NativeOnMouseCaptureLost(const FCaptureLostEvent& E){CancelDrag();Super::NativeOnMouseCaptureLost(E);}
void ULDInventoryPanel::NativeOnFocusLost(const FFocusEvent& E){CancelDrag();Super::NativeOnFocusLost(E);}
FReply ULDInventoryPanel::NativeOnKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
    if(E.GetKey()==PC->BindingKey(TEXT("Back"))){if(bConfirm){bConfirm=false;bDirty=true;Refresh();DropButton->SetKeyboardFocus();}else{Close();}return FReply::Handled();}
    return Super::NativeOnKeyDown(G,E);
}
UWidget* ULDInventoryPanel::FocusScope() const{return bConfirm?ConfirmPanel.Get():MainPanel.Get();}
FReply ULDInventoryPanel::NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
    if(E.GetKey()==PC->BindingKey(TEXT("Back"))){return NativeOnKeyDown(G,E);}
    const bool Editing=Search->HasKeyboardFocus()||Search->HasFocusedDescendants();
    if(!Editing&&E.GetKey()==PC->BindingKey(TEXT("Stop"))){PC->ClearControls();return FReply::Handled();}
    return Super::NativeOnPreviewKeyDown(G,E);
}
