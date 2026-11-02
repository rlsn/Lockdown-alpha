#include "UI/LDHUD.h"
#include "UI/LDHUDModel.h"
#include "UI/LDMinimap.h"
#include "UI/LDWidgets.h"
#include "UI/LDControlSettings.h"
#include "UI/LDControlsPanel.h"
#include "UI/LDInventoryPanel.h"
#include "UI/LDUIRoot.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Framework/LDDemoEntry.h"
#include "Development/LDDemoContentCatalog.h"
#include "Navigation/LDNavigationComponent.h"
#include "Vision/LDWorldViewRelay.h"
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
        auto* Bottom=WidgetTree->ConstructWidget<UHorizontalBox>();Band=Panel(Bottom);BandSlot=Canvas->AddChildToCanvas(Band);BandSlot->SetAnchors(FAnchors(0,1,1,1));BandSlot->SetAlignment(FVector2D(0,1));BandSlot->SetOffsets(FMargin(12,-12,12,220));
        auto* MapColumn=WidgetTree->ConstructWidget<UVerticalBox>();
        MapColumn->AddChildToVerticalBox(Text(TEXT("MAP · sight"),14));
        Minimap=CreateWidget<ULDMinimap>(PC);auto* MapSize=WidgetTree->ConstructWidget<USizeBox>();MapFrame=MapSize;MapSize->SetWidthOverride(204);MapSize->SetHeightOverride(136);MapSize->SetContent(Minimap);MapColumn->AddChildToVerticalBox(MapSize);
        SourceText=Text(TEXT(""),14);SourceText->SetAutoWrapText(true);MapColumn->AddChildToVerticalBox(SourceText);auto* MapColumnSize=WidgetTree->ConstructWidget<USizeBox>();MapColumnSize->SetWidthOverride(220);MapColumnSize->SetContent(MapColumn);Bottom->AddChildToHorizontalBox(MapColumnSize)->SetPadding(FMargin(0,0,12,0));
        auto* InspectColumn=WidgetTree->ConstructWidget<UVerticalBox>();auto* InspectSize=WidgetTree->ConstructWidget<USizeBox>();InspectFrame=InspectSize;InspectSize->SetWidthOverride(330);InspectSize->SetContent(InspectColumn);Bottom->AddChildToHorizontalBox(InspectSize)->SetPadding(FMargin(0,0,16,0));
        Inspector=Text(TEXT(""),14);auto* InspectScroll=WidgetTree->ConstructWidget<UScrollBox>();InspectScroll->AddChild(Inspector);InspectColumn->AddChildToVerticalBox(InspectScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* InspectActions=WidgetTree->ConstructWidget<UHorizontalBox>();InspectColumn->AddChildToVerticalBox(InspectActions);
        auto* Self=Button(TEXT("Self"),[PC](){PC->Inspect({},0);});CastChecked<UTextBlock>(Self->GetContent())->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",14));InspectActions->AddChildToHorizontalBox(Self);
        // Bag and Controls open their panels directly: the Demo action registry has no Inventory or Help binding to show.
        auto* Bag=Button(TEXT("Bag"),[PC](){PC->ToggleInventoryPanel();});CastChecked<UTextBlock>(Bag->GetContent())->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",14));InspectActions->AddChildToHorizontalBox(Bag)->SetPadding(FMargin(4,0));
        auto* Own=WidgetTree->ConstructWidget<UVerticalBox>();Bottom->AddChildToHorizontalBox(Own)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Resources=Text(TEXT(""),16);Own->AddChildToVerticalBox(Resources)->SetPadding(FMargin(0,0,0,4));
        auto* Actions=WidgetTree->ConstructWidget<UHorizontalBox>();Own->AddChildToVerticalBox(Actions)->SetPadding(FMargin(0,4));
        auto* Stop=Button(TEXT("Stop"),[PC](){if(PC->CanWorldCommand()){PC->Navigation->Stop();}});Commands.Add(TEXT("Stop"),Stop);Actions->AddChildToHorizontalBox(Stop);
        auto* ControlsButton=Button(TEXT("Controls"),[PC](){PC->ToggleControlsPanel();});Actions->AddChildToHorizontalBox(ControlsButton)->SetPadding(FMargin(4,0));
        ActionText=Text(TEXT(""),14);Own->AddChildToVerticalBox(ActionText);
        Toast=Text(TEXT(""),16);ToastPanel=Panel(Toast);FeedbackSlot=Canvas->AddChildToCanvas(ToastPanel);FeedbackSlot->SetAnchors(FAnchors(.3f,1,.9f,1));FeedbackSlot->SetAlignment(FVector2D(0,1));FeedbackSlot->SetOffsets(FMargin(0,-240,0,48));ToastPanel->SetVisibility(ESlateVisibility::Collapsed);
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
const FLDDemoContentDefinitions* ULDHUD::ObservedDefinitions()
{
    if(!bObservedContentTried)
    {
        bObservedContentTried=true;FString Error;
        if(!LDDemoEntry::LoadRuntimeContent(ObservedContent,Error)||!ObservedContent){ObservedContent.Reset();UE_LOG(LogTemp,Warning,TEXT("LD_HUD_OBSERVED_LABELS_UNAVAILABLE %s"),*Error);}
    }
    return ObservedContent?&ObservedContent->Get():nullptr;
}
void ULDHUD::RefreshReadModel()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto* GS=GetWorld()->GetGameState<ALDGameState>();if(!GS||!Resources){return;}
    const auto& O=PC->OwnerState;
    const ELDMatchPhase Phase=GS->DemoLobby.Phase;const bool Playing=GS->DemoLobby.Generation&&(Phase==ELDMatchPhase::Development||Phase==ELDMatchPhase::CaptureOpen);
    const bool Overlay=(PC->ControlsUI&&PC->ControlsUI->IsOpen())||(PC->InventoryUI&&PC->InventoryUI->IsOpen())||(PC->RootUI&&PC->RootUI->IsMenuOpen());
    Band->SetVisibility(Playing&&!Overlay?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    const float TextScale=PC->Controls?PC->Controls->TextScale:1;int32 ViewW=0,ViewH=0;PC->GetViewportSize(ViewW,ViewH);const float UIScale=UWidgetLayoutLibrary::GetViewportScale(this);const bool Compact=ViewH<=720;const float BandHeight=Compact?180.f/FMath::Max(.1f,UIScale):(TextScale>1?250.f:220.f);MapFrame->SetWidthOverride(Compact?180:204);MapFrame->SetHeightOverride(110);InspectFrame->SetWidthOverride(Compact?300:330);BandSlot->SetSize(FVector2D(0,BandHeight));FeedbackSlot->SetPosition(FVector2D(0,-BandHeight-24));
    if(AppliedScale!=TextScale){const float Previous=AppliedScale>0?AppliedScale:1;AppliedScale=TextScale;TArray<UWidget*> All;WidgetTree->GetAllWidgets(All);for(auto* W:All){if(auto* T=Cast<UTextBlock>(W)){auto Font=T->GetFont();Font.Size=FMath::RoundToInt(Font.Size*TextScale/Previous);T->SetFont(Font);}}}
    FLDObservedUnitView Subject;const bool Selected=PC->ReadInspected(Subject);const ALDWorldViewRelay* ObservationRelay=PC->LocalView();
    const FLDObservedFrameRead ObservedFrame=ObservationRelay?ObservationRelay->ReadObservedFrame():FLDObservedFrameRead();
    FString SubjectName;if(Selected){if(const auto* Member=GS->DemoLobby.Members.FindByPredicate([&Subject](const FLDLobbyMember& Candidate){return Candidate.ContenderId==Subject.ContenderId;})){SubjectName=Member->Name;}}
    Inspector->SetText(FText::FromString(Selected?LDHUD::ObservedUnit(Subject,SubjectName,ObservedDefinitions(),ObservedFrame.Frame?ObservedFrame.Frame->SampledAtServerSec:0.):LDHUD::Inspector(O)));
    int64 NowMs=0;const bool bClock=PC->ReadDemoAuthorityTimeMs(NowMs);
    Resources->SetText(FText::FromString(FString::Printf(TEXT("HP %.0f/%.0f · MP %.0f/%.0f · Barrier %.0f · Gold %lld · XP %lld\n%s"),O.Hp,O.MaxHp,O.Mp,O.MaxMp,O.Barrier,O.Gold,O.Xp,*LDHUD::LifeStatus(O,bClock,NowMs))));
    const bool bSightCurrent=ObservedFrame.Frame&&ObservedFrame.Status==ELDObservedViewStatus::Current;
    SourceText->SetText(FText::FromString(FString::Printf(TEXT("Sight %s · Visible %d\nFog"),bSightCurrent?TEXT("current"):ObservedFrame.Status==ELDObservedViewStatus::Stale?TEXT("STALE"):TEXT("unavailable"),bSightCurrent?ObservedFrame.Frame->Units.Num():0)));
    for(auto& Pair:Commands){CastChecked<UTextBlock>(Pair.Value->GetContent())->SetText(FText::FromString(Pair.Key.ToString()+TEXT(" [")+PC->BindingLabel(Pair.Key)+TEXT("]")));Pair.Value->SetIsEnabled(Pair.Key!=TEXT("Stop")||O.bActive);}
    ActionText->SetText(FText::FromString(!O.bActive?TEXT("Waiting to return · Organize your bag"):PC->Navigation->bFollowCamera?TEXT("Camera: follow"):TEXT("Camera: free · ")+PC->BindingLabel(TEXT("Follow"))+TEXT(" follows")));
    // Toast fade is local presentation time; it is never compared with an authority deadline.
    const double UiNow=GetWorld()->GetRealTimeSeconds();
    if(LastFeedback!=PC->IntentFeedback||SeenFeedback!=PC->FeedbackSerial()){LastFeedback=PC->IntentFeedback;SeenFeedback=PC->FeedbackSerial();FeedbackAt=UiNow;}Toast->SetText(FText::FromString(UiNow-FeedbackAt<5?LastFeedback:FString()));
    ToastPanel->SetVisibility(Toast->GetText().IsEmpty()||Overlay||!Playing?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
}
int32 ULDHUD::NativePaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& C,FSlateWindowElementList& E,int32 L,const FWidgetStyle& S,bool Enabled) const
{
    const int32 Base=Super::NativePaint(A,G,C,E,L,S,Enabled);const auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());if(!PC->CanWorldCommand()){return Base;}
    const FVector2D ClipMin=G.LocalToAbsolute(FVector2D::ZeroVector),ClipMax=G.LocalToAbsolute(FVector2D(G.GetLocalSize().X,G.GetLocalSize().Y-BandSlot->GetSize().Y-16));
    E.PushClip(FSlateClippingZone(FSlateRect(float(ClipMin.X),float(ClipMin.Y),float(ClipMax.X),float(ClipMax.Y))));
    auto Lines=[&](TArray<FVector2D> Points,FLinearColor Color,float Thickness=2.f){FSlateDrawElement::MakeLines(E,Base+1,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Thickness);};
    auto Project=[&](FVector P,FVector2D& Out){FVector2D Screen;if(!PC->ProjectWorldLocationToScreen(P,Screen)){return false;}int32 W=0,H=0;PC->GetViewportSize(W,H);Out=Screen*G.GetLocalSize()/FVector2D(FMath::Max(1,W),FMath::Max(1,H));return true;};
    FLDObservedUnitView Subject;if(PC->ReadInspected(Subject)){FVector2D P;if(Project(FVector(Subject.FeetCm)+FVector(0,0,GetDefault<ALDCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()),P)){Lines({P+FVector2D(-14,12),P+FVector2D(-20,12),P+FVector2D(-20,-30),P+FVector2D(20,-30),P+FVector2D(20,12),P+FVector2D(14,12)},FLinearColor(1,.78f,.3f));}}
    E.PopClip();return Base+1;
}

bool ULDHUD::CommandLayoutFits() const
{
    if(!Band||!Band->IsVisible()){return false;}
    const auto& BG=Band->GetCachedGeometry();const FVector2D Min=BG.LocalToAbsolute(FVector2D::ZeroVector),Max=BG.LocalToAbsolute(BG.GetLocalSize());
    TArray<UWidget*> Widgets={Resources.Get(),ActionText.Get()};TArray<FSlateRect> Buttons;
    for(const auto& Pair:Commands){Widgets.Add(Pair.Value);}
    for(auto* W:Widgets){if(!W||!W->IsVisible()){continue;}const auto& WG=W->GetCachedGeometry();const FVector2D P=WG.LocalToAbsolute(FVector2D::ZeroVector),Q=WG.LocalToAbsolute(WG.GetLocalSize());
        if(P.X<Min.X-.5||P.Y<Min.Y-.5||Q.X>Max.X+.5||Q.Y>Max.Y+.5||WG.GetLocalSize().IsNearlyZero()){return false;}
        if(Cast<UButton>(W)){if(WG.GetLocalSize().X<39.5||WG.GetLocalSize().Y<39.5){return false;}const FSlateRect Rect(P.X,P.Y,Q.X,Q.Y);for(const auto& R:Buttons){if(FMath::Min(R.Right,Rect.Right)-FMath::Max(R.Left,Rect.Left)>.5&&FMath::Min(R.Bottom,Rect.Bottom)-FMath::Max(R.Top,Rect.Top)>.5){return false;}}Buttons.Add(Rect);}
    }return true;
}
