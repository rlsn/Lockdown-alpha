#include "UI/LDInventoryPanel.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Inventory/LDVendorDirector.h"
#include "Inventory/LDAcquisitionCatalog.h"
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
        auto* Border=LDWidgets::Panel(WidgetTree,nullptr);MainPanel=Border;auto* PanelSlot=Canvas->AddChildToCanvas(Border);PanelSlot->SetAnchors(FAnchors(.04f,.16f,.96f,.96f));PanelSlot->SetOffsets(FMargin(0));
        auto* All=WidgetTree->ConstructWidget<UVerticalBox>();Border->SetContent(All);Heading=LDWidgets::Text(WidgetTree,TEXT("BAG & SERVICES"),20);All->AddChildToVerticalBox(Heading);
        Status=LDWidgets::Text(WidgetTree,TEXT(""));All->AddChildToVerticalBox(Status);
        auto Button=[&](const FString& S,TFunction<void()> Action){return LDWidgets::Button(WidgetTree,S,MoveTemp(Action));};
        auto HoldEvents=[](ULDCommandButton* B){B->OnPressed.AddUniqueDynamic(B,&ULDCommandButton::Press);B->OnReleased.AddUniqueDynamic(B,&ULDCommandButton::Release);};
        auto* Tools=WidgetTree->ConstructWidget<UHorizontalBox>();All->AddChildToVerticalBox(Tools)->SetPadding(FMargin(0,8));Search=WidgetTree->ConstructWidget<UEditableTextBox>();Search->SetHintText(FText::FromString(TEXT("Search your items")));Search->OnTextChanged.AddDynamic(this,&ULDInventoryPanel::SearchChanged);Tools->AddChildToHorizontalBox(Search)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto SearchStyle=Search->GetWidgetStyle();SearchStyle.BackgroundImageNormal.TintColor=FSlateColor(FLinearColor(.04f,.08f,.11f));SearchStyle.BackgroundImageHovered.TintColor=SearchStyle.BackgroundImageNormal.TintColor;SearchStyle.BackgroundImageFocused.TintColor=SearchStyle.BackgroundImageNormal.TintColor;SearchStyle.ForegroundColor=FSlateColor(FLinearColor::White);Search->SetWidgetStyle(SearchStyle);
        const TCHAR* Labels[]={TEXT("All"),TEXT("Books"),TEXT("Wards"),TEXT("Objective"),TEXT("Cargo")};for(uint8 I=0;I<5;++I){if(I==3&&GetWorld()->GetGameState<ALDGameState>()->bUIControlProfile){continue;}Tools->AddChildToHorizontalBox(Button(Labels[I],[this,I](){Filter(I);}))->SetPadding(FMargin(3,0));}
        SortButton=Button(TEXT("Sort"),[PC](){PC->InventoryCommand(LDInventory::Sort);});Tools->AddChildToHorizontalBox(SortButton);
        auto* Tabs=WidgetTree->ConstructWidget<UHorizontalBox>();All->AddChildToVerticalBox(Tabs)->SetPadding(FMargin(0,4));
        const TCHAR* Names[]={TEXT("Items"),TEXT("Books"),TEXT("Markets"),TEXT("Workshop"),TEXT("Orders"),TEXT("Treasury"),TEXT("Trade"),TEXT("Supply"),TEXT("Gear craft"),TEXT("Ruins")};
        for(uint8 Tab=0;Tab<10;++Tab){auto* B=Button(Names[Tab],[this,Tab](){ShowService(Tab);});ServiceTabs.Add(B);Tabs->AddChildToHorizontalBox(B)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));}
        auto* Body=WidgetTree->ConstructWidget<UHorizontalBox>();All->AddChildToVerticalBox(Body)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Scroll->SetClipping(EWidgetClipping::ClipToBounds);Body->AddChildToHorizontalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));Grid=WidgetTree->ConstructWidget<UUniformGridPanel>();Grid->SetSlotPadding(FMargin(3));Scroll->AddChild(Grid);
        ServiceWidth=WidgetTree->ConstructWidget<USizeBox>();auto* Right=WidgetTree->ConstructWidget<UVerticalBox>();ServiceScroll=WidgetTree->ConstructWidget<UScrollBox>();ServiceScroll->SetClipping(EWidgetClipping::ClipToBounds);ServiceScroll->AddChild(Right);ServiceWidth->SetContent(ServiceScroll);Body->AddChildToHorizontalBox(ServiceWidth)->SetPadding(FMargin(12,0,0,0));
        Detail=LDWidgets::Text(WidgetTree,TEXT("Choose one of your items"),18);Detail->SetAutoWrapText(true);auto* DetailScroll=WidgetTree->ConstructWidget<UScrollBox>();DetailScroll->AddChild(Detail);auto* DetailSize=WidgetTree->ConstructWidget<USizeBox>();DetailSize->SetMaxDesiredHeight(92);DetailSize->SetContent(DetailScroll);Right->AddChildToVerticalBox(DetailSize);
        for(uint8 Tab=0;Tab<10;++Tab){auto* Section=WidgetTree->ConstructWidget<UVerticalBox>();Services.Add(Section);Right->AddChildToVerticalBox(Section);}
        ServiceHint=LDWidgets::Text(WidgetTree,TEXT(""),14);Right->InsertChildAt(0,ServiceHint);
        VendorOpen=Button(TEXT("Open Guild bookshop"),[PC](){PC->OpenVendor();});Services[1]->AddChildToVerticalBox(VendorOpen);
        CatalogPanel=WidgetTree->ConstructWidget<UVerticalBox>();Services[1]->AddChildToVerticalBox(CatalogPanel)->SetPadding(FMargin(0,4));
        auto* CatalogRows=WidgetTree->ConstructWidget<UUniformGridPanel>();CatalogRows->SetSlotPadding(FMargin(3));CatalogPanel->AddChildToVerticalBox(CatalogRows);
        for(auto Skill:ALDVendorDirector::OfferedSkills(PC->OwnerState.Build.RulesVersion>=7)){auto* B=Button(TEXT(""),[this,Skill](){SelectOffer(Skill);});Offers.Add(B);CatalogRows->AddChildToUniformGrid(B,(Offers.Num()-1)/2,(Offers.Num()-1)%2);}
        auto* QuoteRow=WidgetTree->ConstructWidget<UHorizontalBox>();CatalogPanel->AddChildToVerticalBox(QuoteRow)->SetPadding(FMargin(0,4));CatalogInfo=LDWidgets::Text(WidgetTree,TEXT(""),15);QuoteRow->AddChildToHorizontalBox(CatalogInfo)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));VendorBuy=Button(TEXT("Buy"),[this](){BuyOffer();});CatalogPanel->AddChildToVerticalBox(VendorBuy);


        BuildAcquisition(Services[7],Services[8],Services[9]);BuildCommerce(Services[2]);BuildProduction(Services[3]);BuildBank(Services[5]);BuildTradePanel(Services[6]);
        Services[6]->AddChildToVerticalBox(LDWidgets::Text(WidgetTree,TEXT("Select a visible nearby contender and offer trade from the inspector. Only offered items are shared."),14));
        AidButton=Button(TEXT("Aid consent"),[PC](){PC->ServerAidConsent(PC->OwnerState.Match,PC->OwnerState.Life,!PC->OwnerState.bAidConsent);});Services[0]->AddChildToVerticalBox(AidButton);
        UseButton=Button(TEXT("Use"),[this](){Use();});Services[0]->AddChildToVerticalBox(UseButton)->SetPadding(FMargin(0,8));

        auto* AssignGrid=WidgetTree->ConstructWidget<UUniformGridPanel>();AssignGrid->SetSlotPadding(FMargin(2));Services[0]->AddChildToVerticalBox(AssignGrid);for(uint8 I=0;I<4;++I){auto* B=Button(TEXT(""),[this,PC,I](){if(PC->OwnerState.Build.RulesVersion<7){PC->InventoryCommand(LDInventory::Bind,Selected,I);}});B->PressCallback=[this,PC,I](){if(PC->OwnerState.Build.RulesVersion>=7){PC->StartLoadoutHold(LDInventory::Bind,Selected,I);}};B->ReleaseCallback=[PC](){if(PC->OwnerState.Build.RulesVersion>=7){PC->StopDevelopmentHold();}};HoldEvents(B);Assignments.Add(B);AssignGrid->AddChildToUniformGrid(B,I/2,I%2);}
        auto* Unassign=Button(TEXT("Unassign shortcut"),[this,PC](){if(PC->OwnerState.Build.RulesVersion<7){PC->InventoryCommand(LDInventory::Unbind,Selected);}});Unassign->PressCallback=[this,PC](){if(PC->OwnerState.Build.RulesVersion>=7){PC->StartLoadoutHold(LDInventory::Unbind,Selected);}};Unassign->ReleaseCallback=[PC](){if(PC->OwnerState.Build.RulesVersion>=7){PC->StopDevelopmentHold();}};HoldEvents(Unassign);Organizations.Add(Unassign);Services[0]->AddChildToVerticalBox(Unassign)->SetPadding(FMargin(0,4));
        GearPreview=LDWidgets::Text(WidgetTree,TEXT(""),14);Services[0]->AddChildToVerticalBox(GearPreview);
        static const TCHAR* GearNames[]={TEXT("Weapon"),TEXT("Offhand"),TEXT("Body"),TEXT("Feet"),TEXT("Charm A"),TEXT("Charm B")};static const TCHAR* HybridNames[]={TEXT("Head"),TEXT("Torso"),TEXT("Legs"),TEXT("Feet"),TEXT("Gloves"),TEXT("Main hand"),TEXT("Off hand"),TEXT("Accessory 1"),TEXT("Accessory 2"),TEXT("Accessory 3"),TEXT("Accessory 4")};
        for(uint8 N=0;N<(PC->OwnerState.Build.RulesVersion==8?11:6);++N){auto* G=Button(FString(TEXT("Hold 2s: bind "))+(PC->OwnerState.Build.RulesVersion==8?HybridNames[N]:GearNames[N]),[](){});G->PressCallback=[this,PC,N](){PC->StartLoadoutHold(LDInventory::Equip,Selected,N);};G->ReleaseCallback=[PC](){PC->StopDevelopmentHold();};HoldEvents(G);GearButtons.Add(G);Services[0]->AddChildToVerticalBox(G);}
        auto* Ungear=Button(TEXT("Hold 2s: unbind equipment"),[](){});Ungear->PressCallback=[this,PC](){PC->StartLoadoutHold(LDInventory::Unequip,Selected);};Ungear->ReleaseCallback=[PC](){PC->StopDevelopmentHold();};HoldEvents(Ungear);GearButtons.Add(Ungear);Services[0]->AddChildToVerticalBox(Ungear);
        auto* Move=Button(TEXT("Move to first empty slot"),[this,PC](){for(uint8 I=0;I<PC->OwnerState.Inventory.Capacity;++I){if(!PC->OwnerState.Inventory.Items.ContainsByPredicate([&](const auto& Item){return Item.Slot==I;})){PC->InventoryCommand(LDInventory::Move,Selected,I);return;}}PC->IntentFeedback=TEXT("Inventory full");});Organizations.Add(Move);Services[0]->AddChildToVerticalBox(Move);
        DropButton=Button(TEXT("Drop…"),[this](){AskDrop();});Services[0]->AddChildToVerticalBox(DropButton)->SetPadding(FMargin(0,8));
        ConfirmPanel=WidgetTree->ConstructWidget<UBorder>();ConfirmPanel->SetBrushColor(FLinearColor(.20f,.09f,.04f));auto* Confirmation=WidgetTree->ConstructWidget<UVerticalBox>();ConfirmPanel->SetContent(Confirmation);ConfirmText=LDWidgets::Text(WidgetTree,TEXT(""));Confirmation->AddChildToVerticalBox(ConfirmText);
        auto* Choices=WidgetTree->ConstructWidget<UHorizontalBox>();Confirmation->AddChildToVerticalBox(Choices);ConfirmButton=Button(TEXT("Confirm drop"),[this](){ConfirmDrop();});Choices->AddChildToHorizontalBox(ConfirmButton);Choices->AddChildToHorizontalBox(Button(TEXT("Cancel"),[this](){bConfirm=false;bDirty=true;SetKeyboardFocus();}))->SetPadding(FMargin(8,0));ConfirmText->SetWrapTextAt(540);ConfirmPanel->SetPadding(FMargin(20));auto* ModalSlot=Canvas->AddChildToCanvas(ConfirmPanel);ModalSlot->SetAnchors(FAnchors(.5f,.5f));ModalSlot->SetAlignment(FVector2D(.5));ModalSlot->SetAutoSize(true);ModalSlot->SetZOrder(10);
        BuildTradeReview(Canvas);
        auto* Footer=WidgetTree->ConstructWidget<UHorizontalBox>();All->AddChildToVerticalBox(Footer)->SetPadding(FMargin(0,8));for(uint8 I=0;I<4;++I){auto* B=Button(TEXT(""),[this,PC,I](){if(Selected.IsValid()&&PC->OwnerState.Build.RulesVersion<7){PC->InventoryCommand(LDInventory::Bind,Selected,I);}});Quick.Add(B);Footer->AddChildToHorizontalBox(B)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));}
        Footer->AddChildToHorizontalBox(Button(TEXT("Character"),[this,PC](){Close();PC->ToggleDevelopmentPanel();}))->SetPadding(FMargin(8,0));Footer->AddChildToHorizontalBox(Button(TEXT("Close"),[this](){Close();}));
        TArray<UWidget*> Contents;WidgetTree->GetAllWidgets(Contents);for(auto* W:Contents){if(auto* B=Cast<UButton>(W)){if(auto* Caption=Cast<UTextBlock>(B->GetContent())){Caption->SetAutoWrapText(true);Caption->SetJustification(ETextJustify::Center);if(auto* CaptionSlot=Cast<UButtonSlot>(Caption->Slot)){CaptionSlot->SetHorizontalAlignment(HAlign_Fill);}}}}
        Feedback=LDWidgets::Text(WidgetTree,TEXT("The match continues. Drag outside cancels; Drop requires confirmation."),14);All->AddChildToVerticalBox(Feedback);
    }
    return Super::RebuildWidget();
}
void ULDInventoryPanel::Open(){bOpen=true;bDirty=true;bConfirm=bTradeReview=false;bProductionPreview=false;ServiceTab=0;SetVisibility(ESlateVisibility::Visible);auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());PC->SetMenuOpen(true);SetKeyboardFocus();Refresh();}
void ULDInventoryPanel::Close(){CastChecked<ALDPlayerController>(GetOwningPlayer())->StopDevelopmentHold();CastChecked<ALDPlayerController>(GetOwningPlayer())->CloseCommerce();CastChecked<ALDPlayerController>(GetOwningPlayer())->CloseBank();bBankReviewed=false;CastChecked<ALDPlayerController>(GetOwningPlayer())->CancelTrade();CastChecked<ALDPlayerController>(GetOwningPlayer())->CloseVendor();CancelDrag();bOpen=bConfirm=bTradeReview=false;if(!CastChecked<ALDPlayerController>(GetOwningPlayer())->TradeView.Active()){CastChecked<ALDPlayerController>(GetOwningPlayer())->TradeView=FLDTradeView();}SetVisibility(ESlateVisibility::Collapsed);CastChecked<ALDPlayerController>(GetOwningPlayer())->SetMenuOpen(false);}
void ULDInventoryPanel::CancelDrag(){const bool Captured=bPointerDown;bPointerDown=bDragging=false;Dragged.Invalidate();if(Captured&&FSlateApplication::IsInitialized()){FSlateApplication::Get().ReleaseAllPointerCapture();}}
void ULDInventoryPanel::Select(FGuid Id){Selected=Id;bDirty=true;}
void ULDInventoryPanel::Filter(uint8 Kind){FilterKind=Kind;bDirty=true;}
void ULDInventoryPanel::SearchChanged(const FText& Value){bDirty=true;}
FString ULDInventoryPanel::DetailText() const{return Detail?Detail->GetText().ToString():FString();}
void ULDInventoryPanel::Use()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto* Item=PC->OwnerState.Inventory.Find(Selected);if(!Item||Item->bReserved||PC->InventoryPending()){return;}const auto Kind=Item->Kind;const FGuid Id=Item->Id;
    if(Kind!=ELDItemKind::Book&&!LDInventory::QuickUsable(Kind,PC->OwnerState.Build.RulesVersion)){return;}if(PC->OwnerState.Build.RulesVersion>=7&&Kind!=ELDItemKind::Book&&Kind!=ELDItemKind::WardCharge){const int32 Q=PC->OwnerState.Inventory.Quick.IndexOfByKey(Id);if(Q<0){PC->IntentFeedback=TEXT("Assign this item to a quick reference first");return;}Close();PC->IntentFeedback=TEXT("Use its quick reference to aim or hold the active");return;}Close();if(Kind==ELDItemKind::NullBell){PC->InventoryCommand(LDInventory::Use,Id);return;}if(Kind==ELDItemKind::Book){PC->ArmRead(Id);}else{PC->ArmWard(Id);}
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
    int32 W=0,H=0;PC->GetViewportSize(W,H);const float Logical=FMath::Max(900.f,float(W)/FMath::Max(.1f,UWidgetLayoutLibrary::GetViewportScale(this)));
    const float RightWidth=FMath::Clamp(Logical*.40f,350.f,520.f);ServiceWidth->SetWidthOverride(RightWidth);const uint8 Desired=FMath::Clamp(FMath::FloorToInt((Logical*.92f-RightWidth-64)/156),2,6);bool Changed=Columns!=Desired||Cells.Num()!=I.Capacity;
    if(Changed){Columns=Desired;Grid->ClearChildren();Cells.Reset();for(uint8 N=0;N<I.Capacity;++N){auto* B=LDWidgets::Button(WidgetTree,TEXT(""),[this,PC,N](){const auto* Entry=PC->OwnerState.Inventory.Items.FindByPredicate([&](const auto& E){return E.Slot==N;});Select(Entry?Entry->Id:FGuid());});CastChecked<UTextBlock>(B->GetContent())->SetAutoWrapText(true);CastChecked<UTextBlock>(B->GetContent())->SetWrapTextAt(170);auto Font=CastChecked<UTextBlock>(B->GetContent())->GetFont();Font.Size=FMath::RoundToInt(16*AppliedScale);CastChecked<UTextBlock>(B->GetContent())->SetFont(Font);auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetMinDesiredWidth(150);Size->SetMinDesiredHeight(78);Size->SetContent(B);auto* CellSlot=Grid->AddChildToUniformGrid(Size,N/Columns,N%Columns);CellSlot->SetHorizontalAlignment(HAlign_Fill);CellSlot->SetVerticalAlignment(VAlign_Fill);Cells.Add(B);}bDirty=true;}
    const float Scale=PC->Controls?PC->Controls->TextScale:1;if(Scale!=AppliedScale){TArray<UWidget*> Widgets;WidgetTree->GetAllWidgets(Widgets);for(auto* Widget:Widgets){if(auto* T=Cast<UTextBlock>(Widget)){auto Font=T->GetFont();Font.Size=FMath::RoundToInt(Font.Size*Scale/AppliedScale);T->SetFont(Font);}}AppliedScale=Scale;bDirty=true;}

    AidButton->SetVisibility(O.Build.RulesVersion>=7?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    CastChecked<UTextBlock>(AidButton->GetContent())->SetText(FText::FromString(O.bAidConsent?TEXT("Aid consent: ON · click to revoke"):TEXT("Aid consent: OFF · click to allow dispels")));
    RefreshServices();RefreshProduction();RefreshAcquisition();RefreshCommerce();RefreshBank();RefreshTradePanel();const bool Shop=PC->VendorIsOpen();CatalogPanel->SetVisibility(Shop?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    Heading->SetText(FText::FromString(PC->BankIsOpen()?TEXT("CITY TREASURY · YOUR BAG"):PC->CommerceIsOpen()?TEXT("LOCAL MARKET · YOUR BAG"):Shop?TEXT("GUILD BOOKSHOP · YOUR CARRIED BAG"):PC->TradeView.Session.IsValid()&&(PC->TradeView.Active()||PC->TradeView.Stage==ELDTradeStage::Committed)?TEXT("PEER EXCHANGE · YOUR CARRIED BAG"):TEXT("BAG & SERVICES")));
    VendorOpen->SetIsEnabled(PC->NearGuildVendor()&&!PC->InventoryPending()&&!bConfirm&&!Shop);
    if(Shop)
    {
        const auto& Q=PC->VendorQuote;const auto* SelectedQuote=Q.Offers.FindByPredicate([&](const auto& Offer){return Offer.Skill==SelectedOffer;});
        DisplayedCatalogRevision=Q.CatalogRevision;DisplayedPrice=SelectedQuote?SelectedQuote->Price:0;
        for(int32 N=0;N<Offers.Num();++N){const bool Has=Q.Offers.IsValidIndex(N);CastChecked<UTextBlock>(Offers[N]->GetContent())->SetText(FText::FromString(Has?FString::Printf(TEXT("%s%s · %dg"),Q.Offers[N].Skill==SelectedOffer?TEXT("› "):TEXT(""),LDDevelopment::SkillName(Q.Offers[N].Skill),Q.Offers[N].Price):TEXT("Opening catalog…")));Offers[N]->SetVisibility(Has?ESlateVisibility::Visible:ESlateVisibility::Collapsed);Offers[N]->SetIsEnabled(Has&&!PC->VendorPending());}
        const FString Preview=SelectedQuote?FString::Printf(TEXT("Book: %s · %s · Unlimited stock\nTeaches R1: hold 3s after 6 quiet seconds; does not equip or cast.\n%s"),LDDevelopment::SkillName(SelectedOffer),LDDevelopment::Rank(O.Build,SelectedOffer)?TEXT("Already learned · may buy for trade"):TEXT("Not learned"),*PC->VendorFeedback):PC->VendorFeedback;
        FString CapacityReason;const bool CanCarry=LDInventory::Fits(I,O.Build,{}, {ELDItemKind::Book},&CapacityReason);
        CatalogInfo->SetText(FText::FromString(Preview+(!CanCarry?TEXT("\n")+CapacityReason:O.Gold<DisplayedPrice?TEXT("\nNot enough gold"):TEXT(""))));CastChecked<UTextBlock>(VendorBuy->GetContent())->SetText(FText::FromString(PC->VendorPending()?TEXT("Pending…"):PC->VendorNeedsReview?TEXT("Review new price"):FString::Printf(TEXT("Buy · %dg"),DisplayedPrice)));
        VendorBuy->SetIsEnabled(SelectedQuote&&!PC->InventoryPending()&&CanCarry&&O.Gold>=DisplayedPrice);
    }
    const bool Pending=PC->InventoryPending();const bool Active=O.bActive&&G->GetServerWorldTimeSeconds()>=O.ProtectedUntil&&!O.DevelopmentKind&&!O.ChannelKind&&!(O.ActionCue&15);
    Status->SetText(FText::FromString(FString::Printf(TEXT("%d/%d slots · %d/%d weight · Gold %lld · %s · Match continues"),I.Items.Num(),I.Capacity,LDInventory::Weight(I),LDInventory::WeightCapacity(O.Build),O.Gold,Pending?TEXT("Pending…"):O.bActive?TEXT("Alive"):TEXT("Waiting to return · organization only"))));
    Feedback->SetText(FText::FromString(PC->TradeView.Active()||PC->TradeView.Stage==ELDTradeStage::Committed?PC->TradeView.Message:PC->IntentFeedback));const auto* SelectedItem=I.Find(Selected);if(!SelectedItem&&Selected.IsValid()){Selected.Invalidate();bConfirm=false;bDirty=true;}
    const bool Usable=SelectedItem&&!SelectedItem->bReserved&&!Pending&&!bConfirm;
    UseButton->SetIsEnabled(Usable&&Active&&(O.Build.RulesVersion!=8||SelectedItem->Kind==ELDItemKind::Book)&&(SelectedItem->Kind==ELDItemKind::Book||LDInventory::QuickUsable(SelectedItem->Kind,O.Build.RulesVersion))&&(SelectedItem->Kind!=ELDItemKind::NullBell||I.Quick.Contains(SelectedItem->Id)));DropButton->SetIsEnabled(Usable&&Active&&!SelectedItem->BindingMask);SortButton->SetIsEnabled(!Pending&&!bConfirm);
    for(auto B:Organizations){B->SetIsEnabled(Usable);}for(uint8 N=0;N<4;++N){Assignments[N]->SetIsEnabled(Usable&&LDInventory::QuickUsable(SelectedItem->Kind,O.Build.RulesVersion));CastChecked<UTextBlock>(Assignments[N]->GetContent())->SetText(FText::FromString((O.Build.RulesVersion>=7?TEXT("Hold 2s: assign to "):TEXT("Assign to "))+PC->BindingLabel(FName(*FString::Printf(TEXT("Item%d"),N+1)))));}
    const bool Tactical=O.Build.RulesVersion>=7;const auto* Catalog=ULDEquipmentCatalog::Load(O.Build.RulesVersion);const auto* Definition=SelectedItem&&Catalog?Catalog->Find(SelectedItem->Kind):nullptr;
    FString EquipmentText=Tactical?LDGear::Preview(I,O.Build):TEXT("");if(O.Build.RulesVersion==8){EquipmentText+=TEXT("\nBody actives also need a shared slot in Character. Unbinding a source clears its action. Consumable accessories drop on death and empty on use; no refill.");}
    for(int32 N=0;N<GearButtons.Num();++N){auto* B=GearButtons[N].Get();B->SetVisibility(Tactical?ESlateVisibility::Visible:ESlateVisibility::Collapsed);FLDInventory Candidate;FString Why;const bool Legal=Tactical&&Definition&&LDGear::Propose(I,O.Build,N==GearButtons.Num()-1?LDInventory::Unequip:LDInventory::Equip,Selected,uint8(N==GearButtons.Num()-1?0:N),Candidate,Why);B->SetIsEnabled(Usable&&O.bActive&&Legal);if(Legal&&N<GearButtons.Num()-1){EquipmentText+=TEXT("\nBinding preview: ")+LDGear::Preview(Candidate,O.Build);}else if(Tactical&&Definition&&!Why.IsEmpty()&&(N==Definition->Binding||N==GearButtons.Num()-1)){EquipmentText+=TEXT("\n")+Why;}}
    GearPreview->SetVisibility(Tactical?ESlateVisibility::Visible:ESlateVisibility::Collapsed);GearPreview->SetText(FText::FromString(EquipmentText));
    MainPanel->SetIsEnabled(!bConfirm&&!bTradeReview);ConfirmPanel->SetVisibility(bConfirm?ESlateVisibility::Visible:ESlateVisibility::Collapsed);ConfirmButton->SetIsEnabled(bConfirm&&Active&&!Pending&&I.Revision==DropInventoryRevision&&I.Find(DropItem)&&I.Find(DropItem)->Revision==DropItemRevision);if(bConfirm&&I.Find(DropItem)){ConfirmText->SetText(FText::FromString(TEXT("Drop ")+LDInventory::Name(I.Find(DropItem)->Kind,I.Find(DropItem)->Skill)+TEXT("? This removes it from your bag.")));}
    if(!bDirty&&SeenInventory==I.Revision){return;}SeenInventory=I.Revision;SeenOwner=O.Revision;bDirty=false;
    const FString Query=Search->GetText().ToString();for(uint8 N=0;N<Cells.Num();++N){const auto* Item=I.Items.FindByPredicate([&](const auto& E){return E.Slot==N;});const FString Name=Item?LDInventory::Name(Item->Kind,Item->Skill):TEXT("Empty");const bool Match=!Item||((FilterKind==0||(FilterKind==4?LDInventory::Commodity(Item->Kind):uint8(Item->Kind)+1==FilterKind))&&(Query.IsEmpty()||Name.Contains(Query)));
        FString Text=FString::Printf(TEXT("%d · %s%s"),N+1,Item&&Item->Id==Selected?TEXT("› "):TEXT(""),Match?*Name:TEXT("Filtered"));if(Item&&Match){const int32 Q=I.Quick.IndexOfByKey(Item->Id);Text+=Item->BindingMask?TEXT("\nEQUIPPED · one carried instance"):Item->bReserved?TEXT("\nReserved for a held order or exchange"):Q>=0?TEXT("\nShortcut ")+PC->BindingLabel(FName(*FString::Printf(TEXT("Item%d"),Q+1))):TEXT("\nOwned");}CastChecked<UTextBlock>(Cells[N]->GetContent())->SetText(FText::FromString(Text));Cells[N]->SetIsEnabled(Match&&!bConfirm);}
    for(uint8 N=0;N<4;++N){Quick[N]->SetVisibility(N<I.Quick.Num()?ESlateVisibility::Visible:ESlateVisibility::Collapsed);Assignments[N]->SetVisibility(N<I.Quick.Num()?ESlateVisibility::Visible:ESlateVisibility::Collapsed);if(N>=I.Quick.Num()){continue;}const auto* Item=I.Find(I.Quick[N]);CastChecked<UTextBlock>(Quick[N]->GetContent())->SetText(FText::FromString(TEXT("ITEM [")+PC->BindingLabel(FName(*FString::Printf(TEXT("Item%d"),N+1)))+TEXT("]\n")+(Item?LDInventory::Name(Item->Kind,Item->Skill):TEXT("Empty"))));Quick[N]->SetIsEnabled(!Pending&&!bConfirm&&(!SelectedItem||(SelectedItem->Kind==ELDItemKind::Book||LDInventory::QuickUsable(SelectedItem->Kind,O.Build.RulesVersion))));}
    Detail->SetText(FText::FromString(SelectedItem?LDInventory::Name(SelectedItem->Kind,SelectedItem->Skill)+TEXT("\n")+(SelectedItem->Kind==ELDItemKind::Book?FString(TEXT("Teaches "))+LDDevelopment::SkillName(SelectedItem->Skill)+TEXT(" rank 1. Hold for 3 seconds after 6 quiet seconds. Does not equip or cast. Known books are kept."):SelectedItem->Kind==ELDItemKind::WardCharge?TEXT("One finite placement. Confirm ground within 96 cm; sight 560 cm for 60 seconds. Cancel/failure keeps this charge."):LDInventory::Commodity(SelectedItem->Kind)?TEXT("Commercial cargo. Trade, production or delivery input; drops on death. Cannot be quick-used."):TEXT("One per carrier. Carry to the Heart. Cannot be assigned or used as a reusable shortcut."))+TEXT("\n")+(SelectedItem->bReserved?TEXT("Reserved for a held order or exchange"):FString::Printf(TEXT("Owned · one item · Weight %d"),LDInventory::UnitWeight(SelectedItem->Kind))):TEXT("Choose one of your items.\nDrag between bag cells or onto an item shortcut. Drag outside cancels.\nUse/Assign/Unassign/Move/Drop also work with buttons.")));
    if(Definition&&Definition->Binding!=255&&SelectedItem){Detail->SetText(FText::FromString(Definition->Name+(SelectedItem->BindingMask?TEXT(" · EQUIPPED"):TEXT(" · UNBOUND"))+FString::Printf(TEXT("\nOne carried instance · Weight %d%s\nHold a binding button for 2s after 6 quiet seconds.\nEquipped ordinary gear retains on death; spare gear drops."),Definition->Weight,Definition->TwoHanded?TEXT(" · reserves weapon + offhand"):TEXT(""))));}
    if(SelectedItem&&!Definition&&uint8(SelectedItem->Kind)>=20&&PC->OwnerState.Build.RulesVersion>=7){const auto* C=ULDAcquisitionCatalog::Load();const auto* D=C?C->Find(SelectedItem->Kind):nullptr;Detail->SetText(FText::FromString(LDInventory::Name(SelectedItem->Kind,SelectedItem->Skill)+(D?FString::Printf(TEXT(" · Tier %d\nPhysical equipment; bind before using its active.\n"),D->Tier):TEXT("\nObjective/material cargo; drops on death. Access and guardian integration arrives in B76.\n"))));}
    if(Definition&&SelectedItem&&!Definition->Operation.IsEmpty()){const TMap<FString,FString> Help={
        {TEXT("arm_next_hit"),TEXT("Arm next hit: silence1.3s; expires4s")},{TEXT("reflect_next_projectile"),TEXT("Reflect one projectile spell for2.5s")},{TEXT("spell_block"),TEXT("Block one targeted hostile spell for3s")},{TEXT("control_guard"),TEXT("Guard next stun/hex for1.5s")},{TEXT("anchor"),TEXT("Resist push/pull1.8s; slow self30%")},{TEXT("barrier"),TEXT("Damage barrier for3s")},{TEXT("heal_over_time"),TEXT("Heal160 over8s; positive damage cancels")},{TEXT("restore_mp"),TEXT("Hold1.5s: restore80MP; charge consumed at start")},{TEXT("recall"),TEXT("Aim a visited City/West/East service; hold4s; no objective cargo")},{TEXT("blink"),TEXT("Aim a valid ground endpoint; root/grounding/direct damage block")},{TEXT("grapple"),TEXT("Aim hostile body or terrain: self traverse and body pull")},{TEXT("terrain_grapple"),TEXT("Aim terrain: swept self traverse")},{TEXT("smoke"),TEXT("Aim ground:5s smoke blocks sight")},{TEXT("detector"),TEXT("Reveal nearby concealment and traps through sight for6s")},{TEXT("spawn_ward"),TEXT("Aim ground:560cm sight for60s; limit2")},{TEXT("spawn_trap"),TEXT("Aim ground: arms1s; roots1.5s; limit2")},{TEXT("element_item_hit"),TEXT("Aim hostile body: Frost35; Wet attempts600ms Stun / shared6s lock")},{TEXT("surface_clear"),TEXT("Aim ground: clear selected smoke/steam cells; no hidden reveal")},{TEXT("element_item_field"),TEXT("Aim ground: bounded Wet/Frozen/Oil footprint; inspect source duration and family")},{TEXT("targeted_mute"),TEXT("Aim hostile body: Mute")},{TEXT("basic_dispel"),TEXT("Aim body: dispel positive effects; press reference again for self cleanse")},{TEXT("basic_dispel_self"),TEXT("Cleanse basic negative effects on self")}};
        const FString Description=Help.FindRef(Definition->Operation);const FString Family=Definition->Family.Replace(TEXT("_"),TEXT(" "));const double Ready=FMath::Max(SelectedItem->ReadyAt,O.ItemFamilies.FindRef(Definition->CooldownKey()))/1000.;Detail->SetText(FText::FromString(Definition->Name+FString::Printf(TEXT("\nActive: %s · family %s · %.1fs cooldown · no MP\n%s · %s\nReady in %.1fs. Assign a quick reference; then aim or hold it.\nMana/Recall consume at channel start; other consumables at release."),*Description,*Family,Definition->Cooldown/1000.,Definition->Consumable?TEXT("One consumable; drops on death"):Definition->Kind==ELDItemKind::TowerRelic?TEXT("Always drops, even bound"):TEXT("Equip before use; bound ordinary gear retains"),Definition->Family==TEXT("major_protection")?TEXT("Shared 8s protection lock"):TEXT("Family and instance endpoints persist on transfer"),FMath::Max(0.,Ready-G->GetServerWorldTimeSeconds()))));}
    if(SelectedItem&&PC->OwnerState.Build.RulesVersion>=7){Detail->SetText(FText::FromString(Detail->GetText().ToString()+FString::Printf(TEXT("\nQuality: %s · instance ready at %.1fs"),SelectedItem->Quality==2?TEXT("Masterwork"):SelectedItem->Quality==1?TEXT("Fine"):TEXT("Standard"),SelectedItem->ReadyAt/1000.)));}
    if(ServiceTab!=0){Detail->SetText(FText::FromString(SelectedItem?FString::Printf(TEXT("Selected: bag %d · %s · weight %d%s"),SelectedItem->Slot+1,*LDInventory::Name(SelectedItem->Kind,SelectedItem->Skill),LDInventory::UnitWeight(SelectedItem->Kind),SelectedItem->bReserved?TEXT(" · reserved"):TEXT("")):TEXT("Select your cargo in the bag")));}
    CastChecked<UTextBlock>(UseButton->GetContent())->SetText(FText::FromString(SelectedItem&&!(SelectedItem->Kind==ELDItemKind::Book||LDInventory::QuickUsable(SelectedItem->Kind,O.Build.RulesVersion))?TEXT("No quick use"):SelectedItem&&SelectedItem->Kind==ELDItemKind::Book?TEXT("Learn · hold after selecting"):TEXT("Use · choose quick reference")));
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
    if(E.GetEffectingButton()==EKeys::LeftMouseButton&&bPointerDown){auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());bool Applied=false;if(bDragging){for(uint8 I=0;I<PC->OwnerState.Inventory.Quick.Num();++I){if(Quick[I]->GetCachedGeometry().IsUnderLocation(E.GetScreenSpacePosition())){if(PC->OwnerState.Build.RulesVersion>=7){Selected=Dragged;PC->IntentFeedback=TEXT("Selected item; hold its quick assignment for two quiet seconds");}else{PC->InventoryCommand(LDInventory::Bind,Dragged,I);}Applied=true;break;}}const int32 N=HitCell(E.GetScreenSpacePosition());if(!Applied&&N>=0){PC->InventoryCommand(LDInventory::Move,Dragged,uint8(N));Applied=true;}if(!Applied){PC->IntentFeedback=TEXT("Drag cancelled; item kept");}}CancelDrag();return FReply::Handled().ReleaseMouseCapture();}return Super::NativeOnMouseButtonUp(G,E);
}
void ULDInventoryPanel::NativeOnMouseCaptureLost(const FCaptureLostEvent& E){CancelDrag();Super::NativeOnMouseCaptureLost(E);}
void ULDInventoryPanel::NativeOnFocusLost(const FFocusEvent& E){CancelDrag();Super::NativeOnFocusLost(E);}
FReply ULDInventoryPanel::NativeOnKeyDown(const FGeometry& G,const FKeyEvent& E)
{auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());if(E.GetKey()==EKeys::Escape){if(bTradeReview){bTradeReview=false;Refresh();if(ConfirmTradeButton->IsVisible()&&ConfirmTradeButton->GetIsEnabled()){ConfirmTradeButton->SetKeyboardFocus();}else{SetKeyboardFocus();}}else if(bConfirm){bConfirm=false;bDirty=true;Refresh();DropButton->SetKeyboardFocus();}else{Close();}return FReply::Handled();}if(E.GetKey()==PC->BindingKey(TEXT("Inventory"))&&!Search->HasKeyboardFocus()&&!Search->HasFocusedDescendants()&&!GoldInput->HasKeyboardFocus()&&!GoldInput->HasFocusedDescendants()&&!BankAmount->HasKeyboardFocus()&&!BankAmount->HasFocusedDescendants()){Close();return FReply::Handled();}return Super::NativeOnKeyDown(G,E);}

void ULDInventoryPanel::SelectOffer(ELDSkill Skill){SelectedOffer=Skill;Refresh();}
void ULDInventoryPanel::BuyOffer(){auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());if(PC->VendorNeedsReview){PC->VendorNeedsReview=false;PC->VendorFeedback=TEXT("Price refreshed · Click Buy again to confirm the shown price");Refresh();return;}PC->BuyVendorBook(SelectedOffer,DisplayedCatalogRevision,DisplayedPrice);Refresh();}
FString ULDInventoryPanel::VendorText() const{return CatalogInfo?CatalogInfo->GetText().ToString():FString();}

UWidget* ULDInventoryPanel::FocusScope() const{return bTradeReview?TradeReview.Get():bConfirm?ConfirmPanel.Get():MainPanel.Get();}

FReply ULDInventoryPanel::NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    if(E.GetKey()==EKeys::Escape){return NativeOnKeyDown(G,E);}
    const auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
    const bool Editing=Search->HasKeyboardFocus()||Search->HasFocusedDescendants()||GoldInput->HasKeyboardFocus()||GoldInput->HasFocusedDescendants()||BankAmount->HasKeyboardFocus()||BankAmount->HasFocusedDescendants();
    // Text characters arrive through Slate's character event. Consume gameplay
    // shortcut key-down here so an unhandled editing key cannot bubble to close
    // the panel or enter the Enhanced Input queue on the following frame.
    if(Editing&&(E.GetKey()==PC->BindingKey(TEXT("Inventory"))||E.GetKey()==PC->BindingKey(TEXT("Context")))){return FReply::Handled();}
    return Super::NativeOnPreviewKeyDown(G,E);
}
