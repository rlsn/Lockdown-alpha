#include "UI/LDDevelopmentPanel.h"
#include "UI/LDControlSettings.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Development/LDDevelopmentDirector.h"
#include "Objectives/LDObjectiveRules.h"
#include "Region/LDRegionDefinition.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Styling/CoreStyle.h"
#include "Engine/World.h"
#include "Navigation/LDNavigationComponent.h"

ULDDevelopmentButton::ULDDevelopmentButton(){InitIsFocusable(true);}
void ULDDevelopmentButton::Route(){if(Panel){Panel->Choose(Operation,Index);}}
TSharedRef<SWidget> ULDDevelopmentPanel::RebuildWidget()
{
    SetIsFocusable(true);
    if(!Heading)
    {
        auto Text=[&](const FString& Value,int32 Size)
        {auto* W=WidgetTree->ConstructWidget<UTextBlock>();W->SetText(FText::FromString(Value));W->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFontStyle("Regular",Size)));W->SetColorAndOpacity(FLinearColor(.9f,.94f,.88f));W->SetAutoWrapText(true);W->SetVisibility(ESlateVisibility::HitTestInvisible);return W;};
        auto Button=[&](const FString& Caption,uint8 Operation,uint8 Index)
        {
            auto* W=WidgetTree->ConstructWidget<ULDDevelopmentButton>();W->Panel=this;W->Operation=Operation;W->Index=Index;
            auto Style=W->GetStyle();Style.Normal.TintColor=FSlateColor(FLinearColor(.055f,.14f,.15f));Style.Hovered.TintColor=FSlateColor(FLinearColor(.11f,.28f,.27f));Style.NormalPadding=FMargin(8,12);Style.PressedPadding=FMargin(8,12);W->SetStyle(Style);
            auto* Label=Text(Caption,14);Label->SetAutoWrapText(false);W->SetContent(Label);W->OnClicked.AddDynamic(W,&ULDDevelopmentButton::Route);return W;
        };
        auto AddRow=[](UVerticalBox* To,UWidget* W,float RowPadding=4.f){To->AddChildToVerticalBox(W)->SetPadding(FMargin(0,RowPadding));};
        auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Canvas;
        auto* Border=LDWidgets::Panel(WidgetTree,nullptr);
        auto* Frame=Canvas->AddChildToCanvas(Border);Frame->SetAnchors(FAnchors(.04f,.14f,.96f,.96f));Frame->SetOffsets(FMargin(0));
        auto* Shell=WidgetTree->ConstructWidget<UVerticalBox>();Border->SetContent(Shell);auto* Main=WidgetTree->ConstructWidget<UVerticalBox>();auto* Page=WidgetTree->ConstructWidget<UScrollBox>();Page->SetAlwaysShowScrollbar(true);Page->AddChild(Main);Shell->AddChildToVerticalBox(Page)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Heading=Text(TEXT("CHARACTER · PREPARATION & BOOKS"),24);Shell->InsertChildAt(0,Heading);
        AddRow(Main,Text(TEXT("Preview freely. Preparation: Apply, then hold the context key for 2 seconds at Guild or a town station. Six quiet seconds required."),15));
        auto* Columns=WidgetTree->ConstructWidget<UHorizontalBox>();AddRow(Main,Columns);
        auto* Left=WidgetTree->ConstructWidget<UVerticalBox>();auto* Right=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* LS=Columns->AddChildToHorizontalBox(Left);LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));LS->SetPadding(FMargin(0,0,20,0));
        auto* RS=Columns->AddChildToHorizontalBox(Right);FSlateChildSize RightSize(ESlateSizeRule::Fill);RightSize.Value=.6f;RS->SetSize(RightSize);
        AddRow(Left,Text(TEXT("ATTRIBUTES · Add points to your preview"),17));
        auto* AttrRow=WidgetTree->ConstructWidget<UHorizontalBox>();AddRow(Left,AttrRow);
        for(uint8 I=0;I<6;++I)
        {
            auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();AttrRow->AddChildToHorizontalBox(Box)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            auto* Label=Text(TEXT(""),13);Label->SetAutoWrapText(false);AttributeLabels.Add(Label);Box->AddChildToVerticalBox(Label);Box->AddChildToVerticalBox(Button(TEXT("+1"),1,I))->SetPadding(FMargin(0,3,6,0));
        }
        AddRow(Left,Text(TEXT("EQUIPPED · Four shared slots · Click a slot, then Equip a learned skill"),16));
        auto* Slots=WidgetTree->ConstructWidget<UHorizontalBox>();AddRow(Left,Slots);
        for(uint8 I=0;I<4;++I){auto* W=Button(TEXT(""),3,I);SlotLabels.Add(CastChecked<UTextBlock>(W->GetContent()));Slots->AddChildToHorizontalBox(W)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));}
        auto* Tools=WidgetTree->ConstructWidget<UHorizontalBox>();AddRow(Left,Tools);Tools->AddChildToHorizontalBox(Button(TEXT("Clear selected slot"),4,0));Tools->AddChildToHorizontalBox(Button(TEXT("Reset preview"),5,0))->SetPadding(FMargin(10,0));
        AddRow(Left,Text(TEXT("LIBRARY · Scroll for all skills · Ranks cost 1 then 2 SP"),16));
        auto* LibrarySize=WidgetTree->ConstructWidget<USizeBox>();LibrarySize->SetHeightOverride(260);AddRow(Left,LibrarySize);
        auto* LibraryScroll=WidgetTree->ConstructWidget<UScrollBox>();LibraryScroll->SetAlwaysShowScrollbar(true);LibrarySize->SetContent(LibraryScroll);
        auto* LibraryRows=WidgetTree->ConstructWidget<UVerticalBox>();LibraryScroll->AddChild(LibraryRows);
        for(uint8 I=1;I<LDDevelopment::SkillCount;++I)
        {
            auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();AddRow(LibraryRows,Row,2);
            auto* Label=Text(TEXT(""),14);SkillLabels.Add(Label);Row->AddChildToHorizontalBox(Label)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            auto* Rank=Button(TEXT("Train / +rank"),2,I);RankButtons.Add(Rank);Row->AddChildToHorizontalBox(Rank)->SetPadding(FMargin(5,0));
            auto* Equip=Button(TEXT("Equip"),6,I);EquipButtons.Add(Equip);Row->AddChildToHorizontalBox(Equip);
        }
        AddRow(Left,Text(TEXT("SUBCLASS · Free at level 4 · Permanent after confirmation"),16));
        auto* Subclasses=WidgetTree->ConstructWidget<UHorizontalBox>();AddRow(Left,Subclasses);
        for(uint8 I=0;I<2;++I){auto* W=Button(TEXT(""),7,I);SubclassButtons.Add(W);Subclasses->AddChildToHorizontalBox(W)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));}
        Subclasses->AddChildToHorizontalBox(Button(TEXT("Defer"),7,2));
        PreviewInfo=Text(TEXT(""),15);AddRow(Left,PreviewInfo);
        ApplyButton=Button(TEXT("Apply preview · then hold E"),8,0);AddRow(Left,ApplyButton);
        HoldInfo=Text(TEXT(""),15);AddRow(Left,HoldInfo);

        AddRow(Right,Text(CastChecked<ALDPlayerController>(GetOwningPlayer())->Navigation->Enabled()?TEXT("BOOK PREVIEW · Open bag for all items"):TEXT("UNREAD BOOKS · 4 spaces"),18));BookInfo=Text(TEXT(""),15);AddRow(Right,BookInfo);
        if(CastChecked<ALDPlayerController>(GetOwningPlayer())->Navigation->Enabled()){AddRow(Right,Button(TEXT("Open carried bag"),13,0));}
        for(uint8 I=0;I<4;++I)
        {auto* Label=Text(TEXT("Empty"),15);BookLabels.Add(Label);AddRow(Right,Label,2);auto* W=Button(TEXT("Use book"),9,I);ReadButtons.Add(W);AddRow(Right,W,3);}
        if(CastChecked<ALDPlayerController>(GetOwningPlayer())->Navigation->Enabled()){AddRow(Right,Button(TEXT("Open Guild bookshop · current prices"),14,0));}
        else{AddRow(Right,Text(TEXT("GUILD BOOKSHOP · 40 gold each"),17));const ELDSkill Stock[]={ELDSkill::Step,ELDSkill::Spark,ELDSkill::Patch,ELDSkill::Parley};for(auto Skill:Stock){auto* W=Button(FString::Printf(TEXT("Buy %s book · 40 gold"),LDDevelopment::SkillName(Skill)),10,uint8(Skill));BuyButtons.Add(W);AddRow(Right,W,3);}}
        AddRow(Right,Text(TEXT("Read anywhere: stand still and hold the context key for 3 seconds after 6 quiet seconds. Reading consumes one book, teaches R1, and does not equip or upgrade. Known books are retained. Unread books drop on death."),14));
        if(CastChecked<ALDPlayerController>(GetOwningPlayer())->Navigation->Enabled()){AddRow(Right,Button(TEXT("Arm City Charter activation"),12,0));}
        Feedback=Text(TEXT(""),15);AddRow(Right,Feedback);
        AddRow(Shell,Button(TEXT("Close / Back"),11,0));
    }
    return Super::RebuildWidget();
}
void ULDDevelopmentPanel::ResetDraft()
{Draft=LDDevelopment::PlanFrom(CastChecked<ALDPlayerController>(GetOwningPlayer())->OwnerState.Build);Armed=0;SelectedBook.Invalidate();}
void ULDDevelopmentPanel::Open()
{bOpen=true;ResetDraft();SetVisibility(ESlateVisibility::Visible);auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());PC->SetMenuOpen(true);SetUserFocus(PC);SetKeyboardFocus();}
void ULDDevelopmentPanel::Close()
{StopConfirm();bOpen=false;Armed=0;SetVisibility(ESlateVisibility::Collapsed);CastChecked<ALDPlayerController>(GetOwningPlayer())->SetMenuOpen(false);}
void ULDDevelopmentPanel::Choose(uint8 Operation,uint8 Index)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto& O=PC->OwnerState;
    StopConfirm();
    if(Operation==11){Close();return;}
    if(Operation==14){Close();PC->OpenVendor();return;}
    if(Operation==13){Close();PC->ToggleInventoryPanel();return;}
    if(!O.bActive){PC->IntentFeedback=TEXT("Read-only while waiting to return");return;}
    if(Operation==12){Close();PC->ArmFabrication();return;}
    if(Operation==10){PC->BuyBook(ELDSkill(Index));return;}
    if(Operation==9)
    {if(O.Books.IsValidIndex(Index)){SelectedBook=O.Books[Index].Id;if(PC->Navigation->Enabled()){PC->ArmRead(SelectedBook);}else{Armed=LDDevelopmentTransaction::Read;}}return;}
    if(Operation==8){if(PC->Navigation->Enabled()){PC->ArmPreparation(Draft);}else{Armed=LDDevelopmentTransaction::Prepare;}return;}
    Armed=0;
    if(Operation==1&&Draft.Attributes.IsValidIndex(Index)){Draft.Attributes[Index]=FMath::Min(16,Draft.Attributes[Index]+1);}
    if(Operation==2&&Draft.Ranks.IsValidIndex(Index)){Draft.Ranks[Index]=FMath::Min(3,int32(Draft.Ranks[Index])+1);}
    if(Operation==3&&Index<4){SelectedSlot=Index;}
    if(Operation==4){Draft.Slots[SelectedSlot]=ELDSkill::None;}
    if(Operation==5){ResetDraft();}
    if(Operation==6&&Index>0&&Index<LDDevelopment::SkillCount){Draft.Slots[SelectedSlot]=ELDSkill(Index);}
    if(Operation==7){Draft.Subclass=Index==2?O.Build.Subclass:O.Build.ClassId*2+1+Index;}
}
void ULDDevelopmentPanel::StartConfirm()
{
    if(!bOpen||bHolding||!Armed){return;}auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
    if(Armed==LDDevelopmentTransaction::Prepare){FLDCharacterBuild Candidate;FString Error;if(!LDDevelopment::Prepare(PC->OwnerState.Build,Draft,Candidate,Error)){PC->IntentFeedback=Error;return;}}
    bHolding=true;PC->StartDevelopmentHold(Armed,SelectedBook,Draft);
}
void ULDDevelopmentPanel::StopConfirm(){if(bHolding){CastChecked<ALDPlayerController>(GetOwningPlayer())->StopDevelopmentHold();}bHolding=false;}
void ULDDevelopmentPanel::NativeTick(const FGeometry& Geometry,float Delta)
{
    Super::NativeTick(Geometry,Delta);if(!bOpen||!Heading){return;}auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto& O=PC->OwnerState;
    const float Scale=PC->Controls?PC->Controls->TextScale:1;if(Scale!=AppliedTextScale){TArray<UWidget*> All;WidgetTree->GetAllWidgets(All);for(auto* W:All){if(auto* T=Cast<UTextBlock>(W)){auto Font=T->GetFont();Font.Size=FMath::RoundToInt(Font.Size*Scale/AppliedTextScale);T->SetFont(Font);}}AppliedTextScale=Scale;}
    const auto* GS=GetWorld()->GetGameState<ALDGameState>();if((!O.bActive&&!PC->Navigation->Enabled())||!GS||!GS->bMatchStarted||GS->EndReason){Close();return;}
    if(Draft.ExpectedRevision!=O.Build.Revision){StopConfirm();ResetDraft();}
    const double Now=GS->GetServerWorldTimeSeconds();FLDCharacterBuild Candidate;FString Error;const bool Valid=LDDevelopment::Prepare(O.Build,Draft,Candidate,Error);
    FLDCharacterBuild Ghost=O.Build;Ghost.Attributes=Draft.Attributes;Ghost.Ranks=Draft.Ranks;Ghost.Slots=Draft.Slots;Ghost.Subclass=Draft.Subclass;
    Heading->SetText(FText::FromString(FString::Printf(TEXT("%s · Level %d · %d attribute points · %d skill points · Gold %lld"),LDDevelopment::ClassName(O.Build.ClassId),O.Build.Level,O.Build.AttributePoints,O.Build.SkillPoints,O.Gold)));
    const TCHAR* Attrs[]={TEXT("STR"),TEXT("DEX"),TEXT("CON"),TEXT("INT"),TEXT("WIS"),TEXT("CHA")};
    for(int32 I=0;I<6;++I){AttributeLabels[I]->SetText(FText::FromString(FString::Printf(TEXT("%s %d → %d"),Attrs[I],O.Build.Attributes[I],Draft.Attributes[I])));}
    for(int32 I=0;I<4;++I){const FString Key=PC->Navigation->Enabled()?PC->BindingLabel(FName(*FString::Printf(TEXT("Skill%d"),I+1))):FString::FromInt(I+1);SlotLabels[I]->SetText(FText::FromString(FString::Printf(TEXT("%s[%s]\n%s"),SelectedSlot==I?TEXT("› "):TEXT(""),*Key,LDDevelopment::SkillName(Draft.Slots[I]))));}
    for(uint8 I=1;I<LDDevelopment::SkillCount;++I)
    {
        const auto Skill=ELDSkill(I);const int32 Rank=LDDevelopment::Rank(O.Build,Skill);const bool Trainable=Rank>0||(O.Build.Level>=2&&LDDevelopment::CanTrain(O.Build.ClassId,Skill));
        auto ValueBuild=Ghost;if(!ValueBuild.Ranks[I]){ValueBuild.Ranks[I]=1;}
        const TCHAR* Kind=I<=3?TEXT("combat"):TEXT("utility");
        const FString OwnValue=Skill==ELDSkill::Pickpocket?FString::Printf(TEXT("Own roll 1–20 %+d"),FMath::FloorToInt((ValueBuild.Attributes[1]-10)/2.)+ValueBuild.Ranks[I]+(ValueBuild.Subclass==3?1:0)):FString::Printf(TEXT("Own preview %.2f"),LDDevelopment::SkillValue(ValueBuild,Skill));
        SkillLabels[I-1]->SetText(FText::FromString(FString::Printf(TEXT("%s · %s R%d → %d · %s\n%s · CD %.0fs%s"),LDDevelopment::SkillName(Skill),Rank?TEXT("Learned"):TEXT("Unknown"),Rank,Draft.Ranks[I],Kind,*OwnValue,LDDevelopment::Cooldown(Skill),Rank?TEXT(""):LDDevelopment::CanTrain(O.Build.ClassId,Skill)?TEXT(" · class Lv2 / 1 SP"):TEXT(" · book only"))));
        RankButtons[I-1]->SetIsEnabled(Trainable&&Draft.Ranks[I]<3);EquipButtons[I-1]->SetIsEnabled(Draft.Ranks[I]>0);
        CastChecked<UTextBlock>(RankButtons[I-1]->GetContent())->SetText(FText::FromString(Draft.Ranks[I]?FString::Printf(TEXT("R%d (%d SP)"),FMath::Min(3,int32(Draft.Ranks[I])+1),Draft.Ranks[I]==2?2:1):TEXT("Train (1 SP)")));
    }
    for(uint8 I=0;I<2;++I)
    {const uint8 Choice=O.Build.ClassId*2+1+I;const TCHAR* Effects[]={TEXT(""),TEXT("Heave +2"),TEXT("Patch +3"),TEXT("theft roll +1"),TEXT("Survey +2"),TEXT("Spark +3"),TEXT("Parley +2")};CastChecked<UTextBlock>(SubclassButtons[I]->GetContent())->SetText(FText::FromString(FString::Printf(TEXT("%s%s\n%s"),Draft.Subclass==Choice?TEXT("✓ "):TEXT(""),LDDevelopment::SubclassName(Choice),Effects[Choice])));SubclassButtons[I]->SetIsEnabled(O.Build.Level>=4&&(!O.Build.Subclass||O.Build.Subclass==Choice));}
    PreviewInfo->SetText(FText::FromString(FString::Printf(TEXT("BUILD PREVIEW · HP %.0f → %.0f (current %.2f; no heal)\nWalk %.1f → %.1f · Sprint %.1f → %.1f · Basic %.2f → %.2f\n%s"),O.MaxHp,LDDevelopment::MaxHP(Ghost),O.Hp,180*LDDevelopment::SpeedFactor(O.Build),180*LDDevelopment::SpeedFactor(Ghost),300*LDDevelopment::SpeedFactor(O.Build),300*LDDevelopment::SpeedFactor(Ghost),LDDevelopment::BasicDamage(O.Build),LDDevelopment::BasicDamage(Ghost),Valid?*FString::Printf(TEXT("After applying: %d attribute points / %d SP"),Candidate.AttributePoints,Candidate.SkillPoints):*Error)));
    ApplyButton->SetIsEnabled(Valid&&!bHolding);
    if(PC->Navigation->Enabled())
    {
        bool Near=false;if(PC->GetPawn()){for(uint8 Site:{uint8(1),uint8(2),uint8(6)}){Near|=FVector::DistSquared2D(PC->GetPawn()->GetActorLocation(),LDRegion::Site(GetWorld(),Site))<=120*120;}}
        ApplyButton->SetIsEnabled(Valid&&Near&&O.bActive&&Now>=O.ProtectedUntil);
        CastChecked<UTextBlock>(ApplyButton->GetContent())->SetText(FText::FromString(TEXT("Apply preview")));
    }
    FString Confirm=Armed==LDDevelopmentTransaction::Prepare?TEXT("Armed: hold E for 2s at a Preparation station."):Armed==LDDevelopmentTransaction::Read?TEXT("Book selected: hold E for 3s to read."):TEXT("Apply a preview or select an unread book, then hold E.");
    if(O.DevelopmentKind){Confirm=FString::Printf(TEXT("%s · %.1fs remaining · Release E or lose focus to cancel."),O.DevelopmentKind==2?TEXT("Reading"):TEXT("Preparing"),FMath::Max(0.,O.DevelopmentEndsAt-Now));}
    HoldInfo->SetText(FText::FromString(Confirm));BookInfo->SetText(FText::FromString(PC->Navigation->Enabled()?FString::Printf(TEXT("%d books · %d / %d carried slots · first four previewed here"),O.Books.Num(),O.Inventory.Items.Num(),O.Inventory.Capacity):FString::Printf(TEXT("%d / 4 unread · learned library is separate"),O.Books.Num())));
    if(PC->Navigation->Enabled()){HoldInfo->SetText(FText::FromString(TEXT("Apply or Use closes this panel and arms a fresh ")+PC->BindingLabel(TEXT("Context"))+TEXT(" hold. Closing alone spends nothing.")));}
    for(int32 I=0;I<4;++I){const bool Present=O.Books.IsValidIndex(I);BookLabels[I]->SetText(FText::FromString(Present?FString::Printf(TEXT("%s%s · unread"),SelectedBook==O.Books[I].Id?TEXT("› "):TEXT(""),LDDevelopment::SkillName(O.Books[I].Skill)):TEXT("Empty space")));ReadButtons[I]->SetIsEnabled(Present&&!bHolding);}
    const bool AtGuild=PC->GetPawn()&&FVector::DistSquared2D(PC->GetPawn()->GetActorLocation(),LDRegion::Site(GetWorld(),LDObjective::Guild))<=120*120;
    for(auto W:BuyButtons){W->SetIsEnabled(AtGuild&&O.Gold>=40&&(PC->Navigation->Enabled()?O.Inventory.Items.Num()<O.Inventory.Capacity:O.Books.Num()<4)&&!bHolding&&!O.DevelopmentKind&&!PC->InventoryPending());}
    if(PC->Navigation->Enabled())
    {
        TArray<UWidget*> Widgets;WidgetTree->GetAllWidgets(Widgets);
        for(auto* W:Widgets){if(auto* Button=Cast<ULDDevelopmentButton>(W))
        {if(!O.bActive){Button->SetIsEnabled(Button->Operation==11||Button->Operation==13);}else if(Button->Operation==1||Button->Operation==3||Button->Operation==4||Button->Operation==5||Button->Operation==11||Button->Operation==12||Button->Operation==13){Button->SetIsEnabled(true);}}}
    }
    Feedback->SetText(FText::FromString(PC->IntentFeedback));
}
FReply ULDDevelopmentPanel::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
    if(PC->Navigation->Enabled())
    {if(Event.GetKey()==EKeys::Escape||Event.GetKey()==PC->BindingKey(TEXT("Development"))){if(!Event.IsRepeat()){Close();}return FReply::Handled();}return Super::NativeOnKeyDown(Geometry,Event);}
    const auto Key=Event.GetKey();if(Key==EKeys::Escape||Key==EKeys::P){Close();return FReply::Handled();}
    if(Key==EKeys::E){if(!Event.IsRepeat()){StartConfirm();}return FReply::Handled();}
    if(Key==EKeys::W||Key==EKeys::A||Key==EKeys::S||Key==EKeys::D){StopConfirm();return FReply::Handled();}
    return Super::NativeOnKeyDown(Geometry,Event);
}
FReply ULDDevelopmentPanel::NativeOnKeyUp(const FGeometry& Geometry,const FKeyEvent& Event)
{if(Event.GetKey()==EKeys::E){StopConfirm();return FReply::Handled();}return Super::NativeOnKeyUp(Geometry,Event);}
void ULDDevelopmentPanel::NativeOnFocusLost(const FFocusEvent& Event){StopConfirm();Super::NativeOnFocusLost(Event);}
