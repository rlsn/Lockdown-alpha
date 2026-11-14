#include "UI/LDWidgets.h"
#include "UI/LDHUDStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"
#include "Engine/DataTable.h"
#include "UI/LDUIFonts.h"
#include "Audio/LDAudioSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
UTextBlock* LDWidgets::Text(UWidgetTree* Tree,const FString& Value,int32 Size,FName Name,LDUIFonts::ERole Role)
{
    auto* W=Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);W->SetText(FText::FromString(Value));W->SetFont(LDUIFonts::Font(Role,Size));
    W->SetColorAndOpacity(LDHUDStyle::TextPrimary);W->SetAutoWrapText(true);W->SetVisibility(ESlateVisibility::HitTestInvisible);return W;
}
void LDWidgets::SetRichTextSize(URichTextBlock* Rich,int32 Size)
{
    if(!Rich){return;}
    UDataTable* Styles=NewObject<UDataTable>(Rich);
    Styles->RowStruct=FRichTextStyleRow::StaticStruct();
    FRichTextStyleRow Plain;
    Plain.TextStyle.SetFont(LDUIFonts::Font(LDUIFonts::ERole::Body,Size));
    Plain.TextStyle.SetColorAndOpacity(FSlateColor(LDHUDStyle::TextPrimary));
    FRichTextStyleRow Bold=Plain;
    Bold.TextStyle.SetFont(LDUIFonts::Font(LDUIFonts::ERole::Strong,Size));
    Bold.TextStyle.SetColorAndOpacity(FSlateColor(LDHUDStyle::TextAccent));
    Styles->AddRow(TEXT("Default"),Plain);
    Styles->AddRow(TEXT("b"),Bold);
    Rich->SetTextStyleSet(Styles);
}
int32 LDWidgets::RichTextSize(const URichTextBlock* Rich)
{
    const UDataTable* Styles=Rich?Rich->GetTextStyleSet():nullptr;
    const FRichTextStyleRow* Plain=Styles?Styles->FindRow<FRichTextStyleRow>(TEXT("Default"),TEXT("RichTextSize"),false):nullptr;
    return Plain?Plain->TextStyle.Font.Size:0;
}
URichTextBlock* LDWidgets::RichText(UWidgetTree* Tree,int32 Size,FName Name)
{
    auto* W=Tree->ConstructWidget<URichTextBlock>(URichTextBlock::StaticClass(),Name);
    SetRichTextSize(W,Size);W->SetAutoWrapText(true);W->SetVisibility(ESlateVisibility::HitTestInvisible);return W;
}
namespace
{
    void LDButtonFace(ULDCommandButton* W,UWidgetTree* Tree,const FString& Value)
    {
        auto* Caption=LDWidgets::Text(Tree,Value);Caption->SetAutoWrapText(false);W->SetContent(Caption);
        // Flat rounded faces from one teal family replace the stock engine chrome; the hairline edge keeps the shape readable
        // on any panel, and the same padding pressed or released keeps the caption still.
        auto Face=[](const FLinearColor& Fill,const FLinearColor& Edge)
        {FSlateRoundedBoxBrush Brush(Fill,LDHUDStyle::RadiusButton,Edge,1.f);Brush.OutlineSettings.bUseBrushTransparency=true;return Brush;};
        auto Style=W->GetStyle();
        Style.Normal=Face(LDHUDStyle::ButtonNormalFill,LDHUDStyle::ButtonNormalEdge);Style.Hovered=Face(LDHUDStyle::ButtonHoveredFill,LDHUDStyle::ButtonHoveredEdge);
        Style.Pressed=Face(LDHUDStyle::ButtonPressedFill,LDHUDStyle::ButtonPressedEdge);Style.Disabled=Face(LDHUDStyle::ButtonDisabledFill,LDHUDStyle::ButtonDisabledEdge);
        Style.NormalPadding=FMargin(12,11);Style.PressedPadding=FMargin(12,11);W->SetStyle(Style);W->EnableFeel();
    }
}
ULDCommandButton* LDWidgets::Button(UWidgetTree* Tree,const FString& Value,TFunction<void()> Callback,bool World,FName Name)
{
    ULDCommandButton* W=World?Tree->ConstructWidget<ULDWorldButton>(ULDWorldButton::StaticClass(),Name):Tree->ConstructWidget<ULDCommandButton>(ULDCommandButton::StaticClass(),Name);
    W->Callback=MoveTemp(Callback);LDButtonFace(W,Tree,Value);
    W->OnClicked.AddDynamic(W,&ULDCommandButton::Execute);W->OnClicked.AddDynamic(W,&ULDCommandButton::FeelClicked);return W;
}
ULDHoldButton* LDWidgets::HoldButton(UWidgetTree* Tree,const FString& Value,TFunction<void()> Callback,FName Name)
{
    ULDHoldButton* W=Tree->ConstructWidget<ULDHoldButton>(ULDHoldButton::StaticClass(),Name);
    W->Callback=MoveTemp(Callback);LDButtonFace(W,Tree,Value);
    W->OnPressed.AddDynamic(W,&ULDHoldButton::HoldPressed);W->OnReleased.AddDynamic(W,&ULDHoldButton::HoldReleased);
    W->OnClicked.AddDynamic(W,&ULDHoldButton::InstantClicked);return W;
}
void ULDCommandButton::EnableFeel()
{
    SetRenderTransformPivot(FVector2D(.5f,.5f));
    OnHovered.AddUniqueDynamic(this,&ULDCommandButton::FeelHovered);OnUnhovered.AddUniqueDynamic(this,&ULDCommandButton::FeelUnhovered);
    OnPressed.AddUniqueDynamic(this,&ULDCommandButton::FeelPressed);OnReleased.AddUniqueDynamic(this,&ULDCommandButton::FeelReleased);
}
void ULDCommandButton::FeelHovered(){PlayCue(ELDAudioCue::UIHover);Animate(IsPressed()?LDHUDStyle::PressScale:LDHUDStyle::HoverScale,LDHUDStyle::MotionHover);}
void ULDCommandButton::FeelUnhovered(){Animate(1.f,LDHUDStyle::MotionHover);}
void ULDCommandButton::FeelPressed(){Animate(LDHUDStyle::PressScale,LDHUDStyle::MotionSnap);}
void ULDCommandButton::FeelReleased(){Animate(IsHovered()?LDHUDStyle::HoverScale:1.f,LDHUDStyle::MotionSnap);}
void ULDCommandButton::PlayCue(ELDAudioCue Cue) const
{
    const UWorld* World=GetWorld();UGameInstance* Instance=World?World->GetGameInstance():nullptr;
    if(auto* Audio=Instance?Instance->GetSubsystem<ULDAudioSubsystem>():nullptr){Audio->Play(Cue);}
}
void ULDCommandButton::Animate(float Target,double Duration)
{
    Scale.Retarget(Target,FPlatformTime::Seconds(),Duration,LDTween::ECurve::EaseOut);
    if(FeelTicker.IsValid()){return;}
    FeelTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[this,Duration](float)
    {
        const double Now=FPlatformTime::Seconds();
        LDTween::SetScale(this,Scale.Value(Now,Duration,LDTween::ECurve::EaseOut));
        if(Scale.IsMoving(Now,Duration)){return true;}
        FeelTicker.Reset();return false;
    }));
}
void ULDCommandButton::StopTicker(FTSTicker::FDelegateHandle& Handle)
{if(Handle.IsValid()){FTSTicker::GetCoreTicker().RemoveTicker(Handle);Handle.Reset();}}
void ULDCommandButton::ReleaseSlateResources(bool bReleaseChildren)
{StopTicker(FeelTicker);Super::ReleaseSlateResources(bReleaseChildren);}
float ULDHoldButton::HoldProgress() const
{return HoldStartedAt<0.?0.f:FMath::Clamp(float((FPlatformTime::Seconds()-HoldStartedAt)/LDHUDStyle::MotionHoldConfirm),0.f,1.f);}
void ULDHoldButton::HoldPressed()
{
    if(!bHoldRequired||HoldStartedAt>=0.||!GetIsEnabled()){return;}
    HoldStartedAt=FPlatformTime::Seconds();
    HoldTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[this](float)
    {
        // Losing the press, the button or its enablement withdraws the hold; only a full hold commits, and only once.
        if(!IsPressed()||!GetIsEnabled()||!IsVisible()){HoldStartedAt=-1.;HoldTicker.Reset();Repaint();return false;}
        Repaint();
        if(HoldProgress()<1.f){return true;}
        HoldStartedAt=-1.;HoldTicker.Reset();PlayCue(ELDAudioCue::UIClick);Execute();return false;
    }));
}
void ULDHoldButton::InstantClicked(){if(!bHoldRequired){PlayCue(ELDAudioCue::UIClick);Execute();}}
void ULDHoldButton::SetHoldRequired(bool bRequired)
{if(bHoldRequired!=bRequired){bHoldRequired=bRequired;HoldReleased();}}
void ULDHoldButton::HoldReleased(){StopTicker(HoldTicker);HoldStartedAt=-1.;Repaint();}
void ULDHoldButton::ReleaseSlateResources(bool bReleaseChildren)
{StopTicker(HoldTicker);HoldStartedAt=-1.;Super::ReleaseSlateResources(bReleaseChildren);}
void ULDHoldButton::Repaint() const
{if(ULDPanel* Panel=GetTypedOuter<ULDPanel>()){Panel->InvalidateLayoutAndVolatility();}}

#include "Components/PanelWidget.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/Slider.h"
#include "Components/Border.h"
#include "Layout/Clipping.h"
#include "Rendering/DrawElements.h"
UWidget* ULDPanel::FocusScope() const{return WidgetTree?WidgetTree->RootWidget:nullptr;}
bool ULDPanel::IsPanelHit(FVector2D Absolute) const
{
    const UWidget* Area=HitArea();
    return IsPanelOpen()&&IsVisible()&&Area&&Area->IsVisible()&&Area->GetCachedGeometry().IsUnderLocation(Absolute);
}
void ULDPanel::ApplyTextScale(float Scale)
{
    // Only a built tree is rescaled; recording the scale earlier would leave later-built text at the base size.
    if(Scale<=0.f||FMath::IsNearlyEqual(Scale,AppliedTextScale)||!WidgetTree||!WidgetTree->RootWidget){return;}
    TArray<UWidget*> All;WidgetTree->GetAllWidgets(All);
    for(UWidget* W:All)
    {
        if(auto* Label=Cast<UTextBlock>(W)){auto Font=Label->GetFont();Font.Size=FMath::RoundToInt(Font.Size*Scale/AppliedTextScale);Label->SetFont(Font);}
        else if(auto* Rich=Cast<URichTextBlock>(W)){LDWidgets::SetRichTextSize(Rich,FMath::RoundToInt(LDWidgets::RichTextSize(Rich)*Scale/AppliedTextScale));}
    }
    AppliedTextScale=Scale;
}
void ULDPanel::ScaleNewText(UTextBlock* Label) const
{
    if(!Label||FMath::IsNearlyEqual(AppliedTextScale,1.f)){return;}
    auto Font=Label->GetFont();Font.Size=FMath::RoundToInt(Font.Size*AppliedTextScale);Label->SetFont(Font);
}
TArray<UWidget*> ULDPanel::PanelFocusTargets() const
{
    TArray<UWidget*> Result;
    TFunction<void(UWidget*)> Visit=[&](UWidget* W)
    {
        if(!W||!W->IsVisible()||!W->GetIsEnabled()){return;}
        if((Cast<UButton>(W)&&CastChecked<UButton>(W)->GetIsFocusable())||(Cast<USlider>(W)&&CastChecked<USlider>(W)->IsFocusable)
            ||Cast<UEditableTextBox>(W)){Result.Add(W);}
        if(auto* Panel=Cast<UPanelWidget>(W)){for(int32 I=0;I<Panel->GetChildrenCount();++I){Visit(Panel->GetChildAt(I));}}
    };
    Visit(FocusScope());return Result;
}
bool ULDPanel::MovePanelFocus(bool Backward)
{
    const auto Targets=PanelFocusTargets();if(Targets.IsEmpty()){return false;}
    const int32 Current=Targets.IndexOfByPredicate([](const UWidget* W){return W->HasKeyboardFocus()||W->HasFocusedDescendants();});
    const int32 Next=Current<0?(Backward?Targets.Num()-1:0):(Current+(Backward?Targets.Num()-1:1))%Targets.Num();
    auto* Target=Targets[Next];Target->SetKeyboardFocus();
    for(auto* Parent=Target->GetParent();Parent;Parent=Parent->GetParent()){if(auto* Scroll=Cast<UScrollBox>(Parent)){Scroll->ScrollWidgetIntoView(Target,false,EDescendantScrollDestination::IntoView,8);}}
    return true;
}
FReply ULDPanel::NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    if(E.GetKey()==EKeys::Tab){MovePanelFocus(E.IsShiftDown());return FReply::Handled();}
    if(E.GetKey()==EKeys::Enter||E.GetKey()==EKeys::SpaceBar)
    {
        for(auto* W:PanelFocusTargets()){if(W->HasKeyboardFocus()||W->HasFocusedDescendants())
        {
            // A hold button takes the key itself: key down presses it and key up releases it, so the keys hold like the pointer.
            if(Cast<ULDHoldButton>(W)){break;}
            if(auto* B=Cast<UButton>(W)){if(!E.IsRepeat()){B->OnClicked.Broadcast();}return FReply::Handled();}break;
        }}
    }
    return Super::NativeOnPreviewKeyDown(G,E);
}
int32 ULDPanel::NativePaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& C,FSlateWindowElementList& E,int32 L,const FWidgetStyle& S,bool Enabled) const
{
    const int32 Base=Super::NativePaint(A,G,C,E,L,S,Enabled);
    for(auto* W:PanelFocusTargets())
    {
        const auto* Hold=Cast<ULDHoldButton>(W);const float Progress=Hold?Hold->HoldProgress():0.f;
        if(Progress<=0.f){continue;}
        const auto& WG=W->GetCachedGeometry();const FVector2D P=G.AbsoluteToLocal(WG.LocalToAbsolute(FVector2D::ZeroVector)),Q=G.AbsoluteToLocal(WG.LocalToAbsolute(WG.GetLocalSize()));
        const FSlateRoundedBoxBrush Fill(LDHUDStyle::HoldFill,LDHUDStyle::RadiusButton);
        FSlateDrawElement::MakeBox(E,Base+1,G.ToPaintGeometry(FVector2D((Q.X-P.X)*Progress,Q.Y-P.Y),FSlateLayoutTransform(P)),&Fill);
    }
    for(auto* W:PanelFocusTargets()){if(W->HasKeyboardFocus()||W->HasFocusedDescendants())
    {
        const auto& WG=W->GetCachedGeometry();const FVector2D P=G.AbsoluteToLocal(WG.LocalToAbsolute(FVector2D::ZeroVector)),Q=G.AbsoluteToLocal(WG.LocalToAbsolute(WG.GetLocalSize()));
        // A focused target scrolled out of its list keeps focus; its outline is clipped to the list instead of floating over
        // the rest of the panel.
        const UScrollBox* List=nullptr;
        for(const UWidget* Parent=W->GetParent();Parent&&!List;Parent=Parent->GetParent()){List=Cast<UScrollBox>(Parent);}
        if(List){E.PushClip(FSlateClippingZone(List->GetCachedGeometry()));}
        FSlateDrawElement::MakeLines(E,Base+1,G.ToPaintGeometry(),{P,FVector2D(Q.X,P.Y),Q,FVector2D(P.X,Q.Y),P},ESlateDrawEffect::None,LDHUDStyle::FocusOutline,true,3);
        if(List){E.PopClip();}
        break;
    }}return Base+1;
}
UBorder* LDWidgets::Panel(UWidgetTree* Tree,UWidget* Child,FMargin Padding)
{
    // The white rounded face takes its fill from the brush colour, so a caller that recolours a panel keeps its shape and rim.
    auto* W=Tree->ConstructWidget<UBorder>();W->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,LDHUDStyle::RadiusPanel,LDHUDStyle::PanelRim,1.f));
    W->SetBrushColor(LDHUDStyle::PanelFill);W->SetPadding(Padding);if(Child){W->SetContent(Child);}return W;
}
