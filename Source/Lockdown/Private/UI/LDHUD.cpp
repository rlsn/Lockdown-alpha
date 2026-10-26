#include "UI/LDHUD.h"
#include "Combat/LDTacticalRules.h"
#include "Development/LDV08Loadout.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "UI/LDHUDModel.h"
#include "UI/LDMinimap.h"
#include "UI/LDWidgets.h"
#include "UI/LDControlSettings.h"
#include "UI/LDDevelopmentPanel.h"
#include "UI/LDControlsPanel.h"
#include "UI/LDInventoryPanel.h"
#include "UI/LDUIRoot.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Character/LDCharacter.h"
#include "Navigation/LDNavigationComponent.h"
#include "Vision/LDWorldViewRelay.h"
#include "Region/LDRegionDefinition.h"
#include "Objectives/LDObjectiveRules.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/ScrollBox.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/World.h"

TSharedRef<SWidget> ULDHUD::RebuildWidget()
{
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    if(!Band)
    {
        auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
        auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);WidgetTree->RootWidget=Canvas;
        auto Text=[&](const FString& S,int32 Size=16){return LDWidgets::Text(WidgetTree,S,Size);};
        auto Panel=[&](UWidget* Child){auto* Border=LDWidgets::Panel(WidgetTree,Child,FMargin(12,8));HitPanels.Add(Border);return Border;};
        auto Button=[&](const FString& S,TFunction<void()> Action){auto* B=LDWidgets::Button(WidgetTree,S,MoveTemp(Action),true);auto* Caption=CastChecked<UTextBlock>(B->GetContent());Caption->SetAutoWrapText(false);Caption->SetJustification(ETextJustify::Center);CastChecked<UButtonSlot>(Caption->Slot)->SetHorizontalAlignment(HAlign_Fill);auto Style=B->GetStyle();Style.NormalPadding=FMargin(8,11);Style.PressedPadding=FMargin(8,11);B->SetStyle(Style);return B;};
        auto Hold=[&](ULDCommandButton* B,TFunction<void()> Press,TFunction<void()> Release){B->PressCallback=MoveTemp(Press);B->ReleaseCallback=MoveTemp(Release);B->OnPressed.AddDynamic(B,&ULDCommandButton::Press);B->OnReleased.AddDynamic(B,&ULDCommandButton::Release);};
        auto* Bottom=WidgetTree->ConstructWidget<UHorizontalBox>();Band=Panel(Bottom);BandSlot=Canvas->AddChildToCanvas(Band);BandSlot->SetAnchors(FAnchors(0,1,1,1));BandSlot->SetAlignment(FVector2D(0,1));BandSlot->SetOffsets(FMargin(12,-12,12,220));
        auto* MapColumn=WidgetTree->ConstructWidget<UVerticalBox>();
        MapColumn->AddChildToVerticalBox(Text(TEXT("MAP · sight"),14));
        Minimap=CreateWidget<ULDMinimap>(PC);auto* MapSize=WidgetTree->ConstructWidget<USizeBox>();MapFrame=MapSize;MapSize->SetWidthOverride(204);MapSize->SetHeightOverride(136);MapSize->SetContent(Minimap);MapColumn->AddChildToVerticalBox(MapSize);
        SourceText=Text(TEXT(""),14);SourceText->SetAutoWrapText(true);MapColumn->AddChildToVerticalBox(SourceText);auto* MapColumnSize=WidgetTree->ConstructWidget<USizeBox>();MapColumnSize->SetWidthOverride(220);MapColumnSize->SetContent(MapColumn);Bottom->AddChildToHorizontalBox(MapColumnSize)->SetPadding(FMargin(0,0,12,0));
        auto* InspectColumn=WidgetTree->ConstructWidget<UVerticalBox>();auto* InspectSize=WidgetTree->ConstructWidget<USizeBox>();InspectFrame=InspectSize;InspectSize->SetWidthOverride(330);InspectSize->SetContent(InspectColumn);Bottom->AddChildToHorizontalBox(InspectSize)->SetPadding(FMargin(0,0,16,0));
        Inspector=Text(TEXT(""),14);auto* InspectScroll=WidgetTree->ConstructWidget<UScrollBox>();InspectScroll->AddChild(Inspector);InspectColumn->AddChildToVerticalBox(InspectScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* InspectActions=WidgetTree->ConstructWidget<UHorizontalBox>();InspectColumn->AddChildToVerticalBox(InspectActions);
        auto* Self=Button(TEXT("Self"),[PC](){PC->Inspect({});});CastChecked<UTextBlock>(Self->GetContent())->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",14));InspectActions->AddChildToHorizontalBox(Self);
        auto* Character=Button(TEXT("Character"),[PC](){PC->ToggleDevelopmentPanel();});CastChecked<UTextBlock>(Character->GetContent())->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",14));Commands.Add(TEXT("Development"),Character);InspectActions->AddChildToHorizontalBox(Character)->SetPadding(FMargin(4,0));
        auto* Bag=Button(TEXT("Bag"),[PC](){PC->ToggleInventoryPanel();});CastChecked<UTextBlock>(Bag->GetContent())->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",14));Commands.Add(TEXT("Inventory"),Bag);InspectActions->AddChildToHorizontalBox(Bag);
        TradeButton=Button(TEXT("Offer trade"),[PC](){PC->OfferTrade();});InspectColumn->AddChildToVerticalBox(TradeButton);
        auto* Own=WidgetTree->ConstructWidget<UVerticalBox>();Bottom->AddChildToHorizontalBox(Own)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Resources=Text(TEXT(""),16);Own->AddChildToVerticalBox(Resources)->SetPadding(FMargin(0,0,0,4));
        auto* SkillRow=WidgetTree->ConstructWidget<UHorizontalBox>();Own->AddChildToVerticalBox(SkillRow);auto* SKILLSSize=WidgetTree->ConstructWidget<USizeBox>();SKILLSSize->SetWidthOverride(94);SKILLSSize->SetContent(Text(TEXT("SKILLS"),14));SkillRow->AddChildToHorizontalBox(SKILLSSize)->SetVerticalAlignment(VAlign_Center);
        for(uint8 I=0;I<6;++I){auto* B=Button(TEXT(""),{});CastChecked<UTextBlock>(B->GetContent())->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",14));auto SkillStyle=B->GetStyle();SkillStyle.NormalPadding=FMargin(8,6);SkillStyle.PressedPadding=SkillStyle.NormalPadding;B->SetStyle(SkillStyle);Skills.Add(B);Hold(B,[PC,I](){PC->SetSkillHeld(I,true);},[PC,I](){PC->SetSkillHeld(I,false);});auto* SlotSize=WidgetTree->ConstructWidget<USizeBox>();SlotSize->SetMinDesiredHeight(40);SlotSize->SetMinDesiredWidth(40);SlotSize->SetContent(B);auto* Cell=SkillRow->AddChildToHorizontalBox(SlotSize);Cell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));Cell->SetPadding(FMargin(2));}
        auto* ItemRow=WidgetTree->ConstructWidget<UHorizontalBox>();Own->AddChildToVerticalBox(ItemRow);auto* ITEMSSize=WidgetTree->ConstructWidget<USizeBox>();ITEMSSize->SetWidthOverride(94);ITEMSSize->SetContent(Text(TEXT("ITEMS"),14));ItemRow->AddChildToHorizontalBox(ITEMSSize)->SetVerticalAlignment(VAlign_Center);
        for(uint8 I=0;I<4;++I){auto* B=Button(TEXT(""),{});Hold(B,[PC,I](){PC->SetQuickItemHeld(I,true,2);},[PC,I](){PC->SetQuickItemHeld(I,false,2);});CastChecked<UTextBlock>(B->GetContent())->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",14));Items.Add(B);auto Style=B->GetStyle();Style.NormalPadding=FMargin(8,6);Style.PressedPadding=Style.NormalPadding;Style.Normal.TintColor=FSlateColor(FLinearColor(.12f,.12f,.17f));B->SetStyle(Style);auto* SlotSize=WidgetTree->ConstructWidget<USizeBox>();SlotSize->SetMinDesiredHeight(40);SlotSize->SetMinDesiredWidth(40);SlotSize->SetContent(B);auto* Cell=ItemRow->AddChildToHorizontalBox(SlotSize);Cell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));Cell->SetPadding(FMargin(2));}
        auto* Actions=WidgetTree->ConstructWidget<UHorizontalBox>();Own->AddChildToVerticalBox(Actions)->SetPadding(FMargin(0,4));
        auto* Attack=Button(TEXT("Attack"),[PC](){PC->ArmAttack();});Commands.Add(TEXT("Attack"),Attack);Actions->AddChildToHorizontalBox(Attack);
        ContextButton=Button(TEXT(""),{});Hold(ContextButton,[PC](){PC->SetInteractionHeld(true);},[PC](){PC->SetInteractionHeld(false);});Actions->AddChildToHorizontalBox(ContextButton)->SetPadding(FMargin(4,0));
        auto* Stop=Button(TEXT("Stop"),[PC](){if(PC->CanWorldCommand()){PC->Navigation->Stop();PC->CancelInterruptibleHolds();PC->CancelArmed();}});Commands.Add(TEXT("Stop"),Stop);Actions->AddChildToHorizontalBox(Stop);
        VendorButton=Button(TEXT("Guild books"),[PC](){PC->OpenVendor();});Actions->AddChildToHorizontalBox(VendorButton)->SetPadding(FMargin(4,0));
        auto* Help=Button(TEXT("Controls"),[PC](){PC->ToggleControlsPanel();});Commands.Add(TEXT("Help"),Help);Actions->AddChildToHorizontalBox(Help)->SetPadding(FMargin(4,0));
        ActionText=Text(TEXT(""),14);Own->AddChildToVerticalBox(ActionText);
        Progress=WidgetTree->ConstructWidget<UProgressBar>();Progress->SetFillColorAndOpacity(FLinearColor(.25f,.85f,.7f));auto* ProgressSize=WidgetTree->ConstructWidget<USizeBox>();ProgressSize->SetHeightOverride(4);ProgressSize->SetContent(Progress);Own->AddChildToVerticalBox(ProgressSize);
        ObjectiveText=Text(TEXT(""),17);Objective=Panel(ObjectiveText);auto* Top=Canvas->AddChildToCanvas(Objective);Top->SetAnchors(FAnchors(.32f,.015f,.74f,.015f));Top->SetOffsets(FMargin(0,0,0,70));
        TaskText=Text(TEXT(""),15);TaskText->SetWrapTextAt(366);Task=Panel(TaskText);auto* Work=Canvas->AddChildToCanvas(Task);Work->SetAnchors(FAnchors(.01f,.02f));Work->SetAutoSize(true);
        Toast=Text(TEXT(""),16);ToastPanel=Panel(Toast);FeedbackSlot=Canvas->AddChildToCanvas(ToastPanel);FeedbackSlot->SetAnchors(FAnchors(.3f,1,.9f,1));FeedbackSlot->SetAlignment(FVector2D(0,1));FeedbackSlot->SetOffsets(FMargin(0,-240,0,48));ToastPanel->SetVisibility(ESlateVisibility::Collapsed);
        auto* Results=WidgetTree->ConstructWidget<UVerticalBox>();ResultPanel=Panel(Results);ResultText=Text(TEXT(""),28);ResultText->SetWrapTextAt(656);Results->AddChildToVerticalBox(ResultText);
        auto* ResultActions=WidgetTree->ConstructWidget<UHorizontalBox>();Results->AddChildToVerticalBox(ResultActions)->SetPadding(FMargin(0,16,0,0));
        ResultActions->AddChildToHorizontalBox(Button(TEXT("Return to menu / new lobby"),[PC](){if(PC->RootUI){PC->RootUI->ReturnToMenu();}}));
        auto* ResultSlot=Canvas->AddChildToCanvas(ResultPanel);ResultSlot->SetAnchors(FAnchors(.5f,.4f));ResultSlot->SetAlignment(FVector2D(.5));ResultSlot->SetAutoSize(true);ResultPanel->SetVisibility(ESlateVisibility::Collapsed);
        // Independent top layer stays readable above every major nonpausing panel.
        Warning=Text(TEXT(""),22);auto* WarnSlot=Canvas->AddChildToCanvas(Warning);WarnSlot->SetAnchors(FAnchors(.25f,.12f,.9f,.12f));WarnSlot->SetOffsets(FMargin(0,0,0,60));Warning->SetColorAndOpacity(FLinearColor(1,.75f,.3f));
    }
    return Super::RebuildWidget();
}
bool ULDHUD::PointerOverUI() const
{
    if(!IsVisible()||!FSlateApplication::IsInitialized()){return false;}return IsUIAt(FSlateApplication::Get().GetCursorPos());
}
bool ULDHUD::IsUIAt(FVector2D P) const
{
    if(Minimap&&Minimap->IsDragging()){return true;}
    for(const auto& Panel:HitPanels){if(Panel&&Panel->IsVisible()&&Panel->GetCachedGeometry().IsUnderLocation(P)){return true;}}return false;
}
FReply ULDHUD::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{return IsUIAt(E.GetScreenSpacePosition())?FReply::Handled():Super::NativeOnMouseButtonDown(G,E);}
FReply ULDHUD::NativeOnMouseWheel(const FGeometry& G,const FPointerEvent& E)
{return IsUIAt(E.GetScreenSpacePosition())?FReply::Handled():Super::NativeOnMouseWheel(G,E);}
FString ULDHUD::InspectionText() const{return Inspector?Inspector->GetText().ToString():FString();}
FString ULDHUD::CommandText() const
{FString Text=Resources?Resources->GetText().ToString():FString();for(const auto& B:Skills){Text+=CastChecked<UTextBlock>(B->GetContent())->GetText().ToString();}return Text;}
FString ULDHUD::TerminalText() const{return ResultText?ResultText->GetText().ToString():FString();}
void ULDHUD::RefreshReadModel()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto* GS=GetWorld()->GetGameState<ALDGameState>();if(!GS||!Resources){return;}
    const auto& O=PC->OwnerState;const auto& B=O.Build;const double Now=GS->GetServerWorldTimeSeconds();
    VendorButton->SetVisibility(PC->NearGuildVendor()?ESlateVisibility::Visible:ESlateVisibility::Collapsed);VendorButton->SetIsEnabled(!PC->InventoryPending()&&O.bActive&&Now>=O.ProtectedUntil&&!GS->EndReason);
    const bool Playing=GS->bMatchStarted;const bool Overlay=(PC->DevelopmentUI&&PC->DevelopmentUI->IsOpen())||(PC->ControlsUI&&PC->ControlsUI->IsOpen())||(PC->InventoryUI&&PC->InventoryUI->IsOpen())||(PC->RootUI&&PC->RootUI->IsMenuOpen());
    Band->SetVisibility(Playing&&!Overlay?ESlateVisibility::Visible:ESlateVisibility::Collapsed);Task->SetVisibility(Playing&&!Overlay?ESlateVisibility::Visible:ESlateVisibility::Collapsed);Objective->SetVisibility(Playing?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    const float TextScale=PC->Controls?PC->Controls->TextScale:1;int32 ViewW=0,ViewH=0;PC->GetViewportSize(ViewW,ViewH);const float UIScale=UWidgetLayoutLibrary::GetViewportScale(this);const bool Compact=ViewH<=720;const float BandHeight=Compact?180.f/FMath::Max(.1f,UIScale):(TextScale>1?250.f:220.f);MapFrame->SetWidthOverride(Compact?180:204);MapFrame->SetHeightOverride(110);InspectFrame->SetWidthOverride(Compact?300:330);BandSlot->SetSize(FVector2D(0,BandHeight));FeedbackSlot->SetPosition(FVector2D(0,-BandHeight-24));
    if(AppliedScale!=TextScale){const float Previous=AppliedScale>0?AppliedScale:1;AppliedScale=TextScale;TArray<UWidget*> All;WidgetTree->GetAllWidgets(All);for(auto* W:All){if(auto* T=Cast<UTextBlock>(W)){auto Font=T->GetFont();Font.Size=FMath::RoundToInt(Font.Size*TextScale/Previous);T->SetFont(Font);}}}
    FLDObservedEntity Subject;const bool Selected=PC->ReadInspected(Subject);TradeButton->SetVisibility(Selected&&Subject.Kind==ELDObservedKind::Body?ESlateVisibility::Visible:ESlateVisibility::Collapsed);TradeButton->SetIsEnabled(O.bActive&&!GS->EndReason&&!PC->InventoryPending());CastChecked<UTextBlock>(TradeButton->GetContent())->SetText(FText::FromString(TEXT("Offer trade [")+PC->BindingLabel(TEXT("Trade"))+TEXT("]")));Inspector->SetText(FText::FromString(LDHUD::Inspector(O,Selected?&Subject:nullptr,Now)));
    if(Selected&&PC->GetPawn())
    {
        double Reach=120;for(auto Skill:B.Slots){if(Skill!=ELDSkill::None&&Skill!=ELDSkill::Step&&Skill!=ELDSkill::Patch){Reach=FMath::Max(Reach,LDHUD::Range(Skill));}}
        const double Distance=FVector::Dist2D(PC->GetPawn()->GetActorLocation(),Subject.Position);
        if(Distance>Reach){Inspector->SetText(FText::FromString(InspectionText()+TEXT("\nOut of reach · inspect only")));}
        else if(Distance>120){Inspector->SetText(FText::FromString(InspectionText()+TEXT("\nContext: out of reach")));}
    }
    const FString Life=O.bActive?(Now<O.ProtectedUntil?FString::Printf(TEXT("Protected %.1fs"),O.ProtectedUntil-Now):TEXT("Alive")):FString::Printf(TEXT("RETURN %.1fs · Lost %lld gold"),FMath::Max(0.,O.ReturnAt-Now),O.LastLostGold);
    Resources->SetText(FText::FromString(FString::Printf(TEXT("YOURS · HP %.0f / %.0f · Lv %d · XP %lld / 420 · Gold %lld · %s · Points %d / %d"),O.Hp,O.MaxHp,B.Level,O.Xp,O.Gold,*Life,B.AttributePoints,B.SkillPoints)));
    if(B.RulesVersion>=7){Resources->SetText(FText::FromString(FString::Printf(TEXT("HP %.0f/%.0f · MP %.0f/%.0f · Barrier %.0f · Lv%d · Gold %lld\n%s"),O.Hp,O.MaxHp,O.Mp,O.MaxMp,O.Barrier,B.Level,O.Gold,*O.StatusFeedback)));}
    Skills[5]->SetVisibility(B.RulesVersion==8?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    Skills[4]->SetVisibility(B.RulesVersion>=7?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(B.RulesVersion>=7){const auto Fifth=B.Slots[4];CastChecked<UTextBlock>(Skills[4]->GetContent())->SetText(FText::FromString(FString(TEXT("["))+PC->BindingLabel(TEXT("Skill5"))+TEXT("] ")+LDDevelopment::SkillName(Fifth)));Skills[4]->SetIsEnabled(Fifth!=ELDSkill::None&&O.bActive&&Now>=O.ProtectedUntil&&!GS->EndReason);Items[3]->SetVisibility(ESlateVisibility::Collapsed);}
    const FString Remaining=FString::Printf(TEXT("%02d:%02d"),int32(FMath::Max(0.,GS->MatchDeadline-Now))/60,int32(FMath::Max(0.,GS->MatchDeadline-Now))%60);
    const FString Heart=GS->bUIControlProfile?LDHUD::PublicOpening(*GS,Now):GS->bClaimActive?FString::Printf(TEXT("%s %.1f/10s"),GS->bContested?TEXT("CONTESTED"):TEXT("Claim"),GS->ClaimProgress):TEXT("Heart protected");
    const bool WorldRegion=B.RulesVersion>=7&&GetWorld()->GetMapName().Contains(TEXT("L_V07Region"));
    const FString Secondary=WorldRegion?FString::Printf(TEXT("Towers %d/3 destroyed · Siege / Charter / Keys / Heartbreaker"),3-FMath::CountBits(uint32(GS->DefensiveFlags&7))):Overlay?FString::Printf(TEXT("YOURS · HP %.0f / %.0f · %s · Match continues"),O.Hp,O.MaxHp,*Life):(GS->bPublicSensorActive?FString(TEXT("Public watch ON")):FString(TEXT("Public watch OFF")))+TEXT(" · Conquest unavailable");
    const FString Economy=GS->bUIControlProfile?(GS->ShortagePhase==1?FString(TEXT("\nEAST TOOLKIT · "))+(GS->ShortageRemaining?TEXT("One order open: Toolkit → 40g / 20XP"):TEXT("Order filled; price premium remains")):GS->ShortagePhase==2?TEXT("\nEast shortage expired"):TEXT("\nEast shortage at 10:00–13:00")):TEXT("");
    CastChecked<UCanvasPanelSlot>(Objective->Slot)->SetSize(FVector2D(0,Overlay?100:TextScale>1?140:110));
    ObjectiveText->SetText(FText::FromString(Remaining+(B.RulesVersion>=7?TEXT(" / 35:00 · "):TEXT(" / 25:00 · "))+Heart+(Overlay?FString():TEXT("\n")+Secondary+Economy)));
    const FString Job=O.ContractStage==1?TEXT("Collect at ")+LDRegion::SiteName(GetWorld(),O.ContractDestination):O.ContractStage==2?TEXT("Deliver at Guild"):TEXT("No active courier job");
    const FString Context=PC->ContextPrompt();const FString Brief=Context.Len()>90?Context.Left(87)+TEXT("…"):Context;
    TaskText->SetText(FText::FromString(FString::Printf(TEXT("WORK · %s\nPaid %d / 8 · Credits %d / 4 · Witness %s\n%s"),*Job,O.PaidJobs,O.Commissions,O.Witness.IsValid()?TEXT("recorded"):TEXT("needed"),*Brief)));if(GS->bUIControlProfile){TaskText->SetText(FText::FromString(FString::Printf(TEXT("%s · Paid %d/8\nSupply %s · Industrial %s · Witness %s\n%s"),*Job,O.PaidJobs,O.SupplyEndorsement.IsValid()?TEXT("✓"):TEXT("—"),O.IndustrialEndorsement.IsValid()?TEXT("✓"):TEXT("—"),O.Witness.IsValid()?TEXT("✓"):TEXT("—"),*LDHUD::EconomyStep(O,*GS,Now))));}TaskText->SetToolTipText(FText::FromString(Context));
    ContextButton->SetToolTipText(FText::FromString(PC->ContextPrompt()));CastChecked<UTextBlock>(ContextButton->GetContent())->SetText(FText::FromString(TEXT("Hold ")+PC->BindingLabel(TEXT("Context"))));
    ContextButton->SetIsEnabled(O.bActive&&!GS->EndReason&&Now>=O.ProtectedUntil);
    const auto* R=PC->LocalView();const auto* V=R?&R->LastView():nullptr;
    SourceText->SetText(FText::FromString(FString::Printf(TEXT("Ward %d · Watch %d\nSight / Fog"),V?V->WardSources:0,V?V->PublicSources:0)));
    for(auto& Pair:Commands){const FString Label=Pair.Key==TEXT("Development")?TEXT("Build"):Pair.Key==TEXT("Inventory")?TEXT("Bag"):Pair.Key==TEXT("Help")?TEXT("Controls"):Pair.Key.ToString();CastChecked<UTextBlock>(Pair.Value->GetContent())->SetText(FText::FromString(Label+TEXT(" [")+PC->BindingLabel(Pair.Key)+TEXT("]")));Pair.Value->SetIsEnabled(Pair.Key==TEXT("Help")||(!GS->EndReason&&((Pair.Key!=TEXT("Attack")&&Pair.Key!=TEXT("Stop"))||(O.bActive&&(Pair.Key!=TEXT("Attack")||Now>=O.ProtectedUntil)))));}
    for(uint8 I=0;I<4;++I)
    {
        const auto Skill=B.Slots.IsValidIndex(I)?B.Slots[I]:ELDSkill::None;const double CD=B.Cooldowns[uint8(Skill)]-Now;
        const FString BlockReason=!O.bActive?TEXT("returning"):GS->EndReason?TEXT("match ended"):Now<O.ProtectedUntil?TEXT("protected"):TEXT("panel open");
        const FString State=Skill==ELDSkill::None?TEXT("Empty"):!O.bActive||Now<O.ProtectedUntil||Overlay||GS->EndReason?TEXT("Blocked"):PC->ArmedSlot()==I?TEXT("Armed"):O.ActiveSkill==Skill&&(O.ActionCue&15)?TEXT("Active"):PC->PendingSlot()==I?TEXT("Pending"):CD>0?FString::Printf(TEXT("Cooldown %.1fs"),CD):TEXT("Ready");
        CastChecked<UTextBlock>(Skills[I]->GetContent())->SetText(FText::FromString(FString::Printf(TEXT("[%s] %s · %s"),*PC->BindingLabel(FName(*FString::Printf(TEXT("Skill%d"),I+1))),LDDevelopment::SkillName(Skill),*State)));
        Skills[I]->SetToolTipText(FText::FromString(LDHUD::Tooltip(B,Skill)+TEXT("\n")+State+(State==TEXT("Blocked")?TEXT(": ")+BlockReason:FString())));Skills[I]->SetIsEnabled(O.bActive&&Now>=O.ProtectedUntil&&Skill!=ELDSkill::None&&!GS->EndReason);
        const auto* Item=O.Inventory.Find(O.Inventory.Quick.IsValidIndex(I)?O.Inventory.Quick[I]:FGuid());const FString Name=Item?Item->Kind==ELDItemKind::Book?FString(LDDevelopment::SkillName(Item->Skill))+TEXT(" book"):LDInventory::Name(Item->Kind,Item->Skill):TEXT("Empty");
        const FString ItemState=!Item?FString():!O.bActive||GS->EndReason||Now<O.ProtectedUntil||Item->bReserved?TEXT("Blocked"):PC->InventoryPending()?TEXT("Pending"):PC->ArmedItem()==Item->Id?TEXT("Armed"):O.DevelopmentItem==Item->Id?TEXT("Active"):TEXT("Ready");
        CastChecked<UTextBlock>(Items[I]->GetContent())->SetText(FText::FromString(TEXT("[")+PC->BindingLabel(FName(*FString::Printf(TEXT("Item%d"),I+1)))+TEXT("] ")+Name+(Item?TEXT(" · ")+ItemState:TEXT(""))));Items[I]->SetIsEnabled(O.bActive&&!GS->EndReason&&Now>=O.ProtectedUntil&&!PC->InventoryPending()&&(!Item||!Item->bReserved));Items[I]->SetToolTipText(FText::FromString(Item?LDInventory::Name(Item->Kind,Item->Skill)+TEXT(" · ")+ItemState+(ItemState==TEXT("Blocked")?FString(TEXT(": "))+(Item->bReserved?TEXT("offered in trade"):*BlockReason):FString())+TEXT(" · Own physical item reference. Books teach; they never cast. Ward placement consumes only on success."):TEXT("Empty reference. Assign an owned book or ward charge in the bag.")));
    }
    if(B.RulesVersion==8&&B.Actions.Num()==6&&O.Inventory.Bindings.Num()==11)
    {
        Resources->SetText(FText::FromString(TEXT("v0.8 DEVELOPMENT · Slots & traits\n")+Resources->GetText().ToString()));
        auto StateFor=[&](bool Item,double End,bool Empty){if(Empty){return FString(TEXT("Empty / passive"));}if(!O.bActive||GS->EndReason||Now<O.ProtectedUntil){return FString(TEXT("Unavailable"));}if(O.StatusMask&((1<<uint8(LDTactical::EStatus::Stun))|(1<<uint8(LDTactical::EStatus::Hex)))){return FString(TEXT("Hard disabled"));}if(O.StatusMask&(1<<uint8(Item?LDTactical::EStatus::Mute:LDTactical::EStatus::Silence))){return FString(Item?TEXT("Muted"):TEXT("Silenced"));}return End>Now?FString::Printf(TEXT("Cooldown %.1fs"),End-Now):FString(TEXT("Ready"));};
        auto ItemEnd=[&](const FLDInventoryItem* I,const FLDEquipmentDefinition* D){return I&&D?FMath::Max3(I->ReadyAt,O.ItemFamilies.FindRef(D->CooldownKey()),D->Family==TEXT("major_protection")?O.MajorProtectionReadyAt:int64(0))/1000.:0.;};
        for(int32 N=0;N<6;++N){const auto& A=B.Actions[N];const bool Gear=A.Source==ELDActionSource::Gear;const auto* I=Gear?O.Inventory.Find(A.Instance):nullptr;const auto* D=I?ULDEquipmentCatalog::Load(8)->Find(I->Kind):nullptr;const bool Empty=Gear?!D:A.Skill==ELDSkill::None;const double End=Gear?ItemEnd(I,D):B.Cooldowns[uint8(A.Skill)];const FString State=StateFor(Gear,End,Empty);const FString Name=Gear?(D?D->Name:TEXT("Missing source")):LDDevelopment::SkillName(A.Skill);CastChecked<UTextBlock>(Skills[N]->GetContent())->SetText(FText::FromString(PC->BindingLabel(FName(*FString::Printf(TEXT("Skill%d"),N+1)))+(Gear?TEXT(" [ITEM] "):TEXT(" [SKILL] "))+Name+TEXT(" · ")+State));Skills[N]->SetIsEnabled(State==TEXT("Ready"));Skills[N]->SetToolTipText(FText::FromString(Name+TEXT(" · ")+State+(Gear?TEXT(" · 0 MP, item cooldown, Mute applies"):TEXT(" · learned action, MP/cooldown, Silence applies"))));}
        for(int32 N=0;N<4;++N){const auto* I=O.Inventory.Find(O.Inventory.Bindings[7+N]);const auto* D=I?ULDEquipmentCatalog::Load(8)->Find(I->Kind):nullptr;const bool Passive=!D||D->Operation.IsEmpty();const FString State=StateFor(true,ItemEnd(I,D),Passive);Items[N]->SetVisibility(ESlateVisibility::Visible);Items[N]->SetIsEnabled(State==TEXT("Ready"));CastChecked<UTextBlock>(Items[N]->GetContent())->SetText(FText::FromString(PC->BindingLabel(FName(*FString::Printf(TEXT("Item%d"),N+1)))+TEXT(" [ACC] ")+(D?D->Name:TEXT("Empty"))+TEXT(" · ")+State));Items[N]->SetToolTipText(FText::FromString(State+TEXT(" · One physical accessory. Consuming empties this binding; no auto-refill.")));}
    }
    FString Action;double Fraction=0;
    if(O.DevelopmentKind){const double Duration=O.DevelopmentKind==4?6:(O.DevelopmentKind==5||O.DevelopmentKind==6||O.DevelopmentKind==7)?O.ProductionSeconds:O.DevelopmentKind==2?3:2;const double Left=FMath::Max(0.,O.DevelopmentEndsAt-Now);Action=FString::Printf(TEXT("%s · Hold %s · %.1fs"),O.DevelopmentKind==7?TEXT("Civic service"):O.DevelopmentKind==6?TEXT("Shortage delivery"):O.DevelopmentKind==4?TEXT("Gathering"):O.DevelopmentKind==5?TEXT("Crafting"):O.DevelopmentKind==2?TEXT("Learning"):TEXT("Preparing"),*PC->HeldInputLabel(),Left);Fraction=1-Left/Duration;}
    else if((O.ActionCue&15)==10){const double Left=FMath::Max(0.,O.SkillEndsAt-Now);Action=FString::Printf(TEXT("Held item / traversal · Hold %s · %.1fs"),*PC->HeldInputLabel(),Left);Fraction=0;}
    else if((O.ActionCue&15)==7){const double Left=FMath::Max(0.,O.SkillEndsAt-Now);Action=FString::Printf(TEXT("%s · Hold %s · %.1fs"),LDDevelopment::SkillName(O.ActiveSkill),*PC->HeldInputLabel(),Left);Fraction=1-Left/LDDevelopment::ChannelSeconds(O.ActiveSkill);}
    else if(O.ChannelKind){Action=FString::Printf(TEXT("%s · Hold %s · %.1f / %.0fs"),O.ChannelKind==1?TEXT("Collecting"):O.ChannelKind==2?TEXT("Delivering"):O.ChannelKind==4?TEXT("Fabricating"):O.ChannelKind==5?TEXT("Activating / key pickup"):O.ChannelKind==6?TEXT("Recovering"):TEXT("Claiming"),*PC->BindingLabel(TEXT("Context")),O.ChannelProgress,(O.ChannelDuration>0?O.ChannelDuration:LDObjective::Duration(O.ChannelKind)));Fraction=O.ChannelProgress/(O.ChannelDuration>0?O.ChannelDuration:LDObjective::Duration(O.ChannelKind));}
    else{Action=PC->IsTargeting()?TEXT("Armed · Confirm in world / ")+PC->BindingLabel(TEXT("Navigate"))+TEXT(" cancels"):PC->HasPendingAim()?TEXT("Pending…"):!O.bActive?TEXT("Waiting to return · Organize your bag"):PC->Navigation->bFollowCamera?TEXT("Camera: follow"):TEXT("Camera: free · ")+PC->BindingLabel(TEXT("Follow"))+TEXT(" follows");}
    ActionText->SetText(FText::FromString(Action));Progress->SetPercent(float(FMath::Clamp(Fraction,0.,1.)));
    if(LastFeedback!=PC->IntentFeedback||SeenFeedback!=PC->FeedbackSerial()){LastFeedback=PC->IntentFeedback;SeenFeedback=PC->FeedbackSerial();FeedbackAt=Now;}Toast->SetText(FText::FromString(Now-FeedbackAt<5?LastFeedback:FString()));
    ToastPanel->SetVisibility(Toast->GetText().IsEmpty()||Overlay||!Playing?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    Warning->SetText(FText::FromString(Now<O.WarningUntil?TEXT("⚠ INCOMING CAST · Escape, Step or interrupt"):Now<O.DamageUntil?TEXT("HIT · Interruptible action cancelled"):FString()));
    if(GS->EndReason)
    {
        if(SeenResult!=GS->ResultSerial){SeenResult=GS->ResultSerial;PC->ClearControls();PC->CancelArmed();if(PC->DevelopmentUI&&PC->DevelopmentUI->IsOpen()){PC->DevelopmentUI->Close();}if(PC->ControlsUI&&PC->ControlsUI->IsOpen()){PC->ControlsUI->Close();}}
        if(PC->InventoryUI&&PC->InventoryUI->IsOpen()){PC->InventoryUI->Close();}
        ResultText->SetText(FText::FromString(GS->EndReason==2?TEXT("NO ACQUISITION\nTime expired. No one claimed the Heart."):O.bWinner?TEXT("HEART ACQUIRED\nYou claimed the Heart."):TEXT("HEART ACQUIRED\nAnother contender claimed the Heart.")));ResultPanel->SetVisibility(ESlateVisibility::Visible);
    }
    else{ResultPanel->SetVisibility(ESlateVisibility::Collapsed);}
}
int32 ULDHUD::NativePaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& C,FSlateWindowElementList& E,int32 L,const FWidgetStyle& S,bool Enabled) const
{
    const int32 Base=Super::NativePaint(A,G,C,E,L,S,Enabled);const auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());if(!PC->CanWorldCommand()){return Base;}
    const FVector2D ClipMin=G.LocalToAbsolute(FVector2D::ZeroVector),ClipMax=G.LocalToAbsolute(FVector2D(G.GetLocalSize().X,G.GetLocalSize().Y-BandSlot->GetSize().Y-16));
    E.PushClip(FSlateClippingZone(FSlateRect(float(ClipMin.X),float(ClipMin.Y),float(ClipMax.X),float(ClipMax.Y))));
    auto Lines=[&](TArray<FVector2D> Points,FLinearColor Color,float Thickness=2.f){FSlateDrawElement::MakeLines(E,Base+1,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Thickness);};
    auto Project=[&](FVector P,FVector2D& Out){FVector2D Screen;if(!PC->ProjectWorldLocationToScreen(P,Screen)){return false;}int32 W=0,H=0;PC->GetViewportSize(W,H);Out=Screen*G.GetLocalSize()/FVector2D(FMath::Max(1,W),FMath::Max(1,H));return true;};
    FLDObservedEntity Subject;if(PC->ReadInspected(Subject)){FVector2D P;if(Project(Subject.Position,P)){Lines({P+FVector2D(-14,12),P+FVector2D(-20,12),P+FVector2D(-20,-30),P+FVector2D(20,-30),P+FVector2D(20,12),P+FVector2D(14,12)},FLinearColor(1,.78f,.3f));}}
    if(PC->IsTargeting()&&PC->GetPawn())
    {
        const double Range=PC->ArmedRange();TArray<FVector2D> Ring;const FVector Body=PC->GetPawn()->GetActorLocation();for(int32 I=0;I<=48;++I){const double Angle=I*2*PI/48;FVector2D P;if(Project(Body+FVector(FMath::Cos(Angle)*Range,FMath::Sin(Angle)*Range,-70),P)){Ring.Add(P);}}if(Ring.Num()>2){Lines(Ring,FLinearColor(.3f,1,.8f,.8f),1.5f);}
        float X=0,Y=0;if(PC->GetMousePosition(X,Y)&&!PointerOverUI()){int32 W=0,H=0;PC->GetViewportSize(W,H);const FVector2D P=FVector2D(X,Y)*G.GetLocalSize()/FVector2D(FMath::Max(1,W),FMath::Max(1,H));Lines({P-FVector2D(9,0),P+FVector2D(9,0)},FLinearColor(1,.85f,.35f));Lines({P-FVector2D(0,9),P+FVector2D(0,9)},FLinearColor(1,.85f,.35f));}
    }
    E.PopClip();return Base+1;
}

bool ULDHUD::CommandLayoutFits() const
{
    if(!Band||!Band->IsVisible()){return false;}
    const auto& BG=Band->GetCachedGeometry();const FVector2D Min=BG.LocalToAbsolute(FVector2D::ZeroVector),Max=BG.LocalToAbsolute(BG.GetLocalSize());
    TArray<UWidget*> Widgets={Resources.Get(),ActionText.Get(),Progress.Get()};TArray<FSlateRect> Buttons;
    for(auto B:Skills){Widgets.Add(B);}for(auto B:Items){Widgets.Add(B);}for(const auto& Pair:Commands){Widgets.Add(Pair.Value);}Widgets.Add(ContextButton);Widgets.Add(VendorButton);Widgets.Add(TradeButton);
    for(auto* W:Widgets){if(!W||!W->IsVisible()){continue;}const auto& WG=W->GetCachedGeometry();const FVector2D P=WG.LocalToAbsolute(FVector2D::ZeroVector),Q=WG.LocalToAbsolute(WG.GetLocalSize());
        if(P.X<Min.X-.5||P.Y<Min.Y-.5||Q.X>Max.X+.5||Q.Y>Max.Y+.5||WG.GetLocalSize().IsNearlyZero()){return false;}
        if(Cast<UButton>(W)){if(WG.GetLocalSize().X<39.5||WG.GetLocalSize().Y<39.5){return false;}const FSlateRect Rect(P.X,P.Y,Q.X,Q.Y);for(const auto& R:Buttons){if(FMath::Min(R.Right,Rect.Right)-FMath::Max(R.Left,Rect.Left)>.5&&FMath::Min(R.Bottom,Rect.Bottom)-FMath::Max(R.Top,Rect.Top)>.5){return false;}}Buttons.Add(Rect);}
    }return true;
}
