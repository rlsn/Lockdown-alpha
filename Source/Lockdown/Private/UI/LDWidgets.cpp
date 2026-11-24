#include "UI/LDWidgets.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Components/ButtonSlot.h"
#include "Framework/LDPlayerController.h"
#include "UI/LDHUDStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/TextBlock.h"
#include "UI/LDUIFonts.h"
#include "Audio/LDAudioSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
UTextBlock* LDWidgets::Text(UWidgetTree* Tree,const FString& Value,int32 Size,FName Name,LDUIFonts::ERole Role)
{
    auto* W=Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);W->SetText(FText::FromString(Value));W->SetFont(LDUIFonts::Font(Role,Size));
    W->SetColorAndOpacity(LDHUDStyle::TextPrimary);W->SetAutoWrapText(true);W->SetVisibility(ESlateVisibility::HitTestInvisible);return W;
}
UScaleBox* LDWidgets::Fit(UWidgetTree* Tree,UWidget* Content)
{
    UScaleBox* Box=Tree->ConstructWidget<UScaleBox>();Box->SetStretch(EStretch::ScaleToFit);Box->SetStretchDirection(EStretchDirection::DownOnly);
    Box->SetClipping(EWidgetClipping::ClipToBounds);
    Box->SetContent(Content);
    if(UScaleBoxSlot* FitSlot=Cast<UScaleBoxSlot>(Content->Slot)){FitSlot->SetHorizontalAlignment(HAlign_Left);FitSlot->SetVerticalAlignment(VAlign_Top);}
    return Box;
}
namespace
{
    void LDButtonFace(ULDCommandButton* W,UWidgetTree* Tree,const FString& Value)
    {
        auto* Caption=LDWidgets::Text(Tree,Value,LDHUDStyle::TypeBody,NAME_None,LDUIFonts::ERole::Strong);Caption->SetAutoWrapText(false);
        Caption->SetColorAndOpacity(LDHUDStyle::UIText);W->SetContent(Caption);
        // The face is painted under the caption (LDUIPaint::Button); the engine chrome stays empty, and the same padding
        // pressed or released keeps the caption still.
        auto Style=W->GetStyle();
        Style.Normal=FSlateNoResource();Style.Hovered=FSlateNoResource();Style.Pressed=FSlateNoResource();Style.Disabled=FSlateNoResource();
        Style.NormalPadding=FMargin(18,11);Style.PressedPadding=FMargin(18,11);W->SetStyle(Style);W->EnableFeel();
    }
}
ULDCommandButton* LDWidgets::Button(UWidgetTree* Tree,const FString& Value,TFunction<void()> Callback,bool World,FName Name)
{
    ULDCommandButton* W=World?Tree->ConstructWidget<ULDWorldButton>(ULDWorldButton::StaticClass(),Name):Tree->ConstructWidget<ULDCommandButton>(ULDCommandButton::StaticClass(),Name);
    W->Callback=MoveTemp(Callback);LDButtonFace(W,Tree,Value);
    W->OnClicked.AddDynamic(W,&ULDCommandButton::Execute);W->OnClicked.AddDynamic(W,&ULDCommandButton::FeelClicked);return W;
}
namespace
{
    // The button's own SButton paints the face under its caption, so the button stays the focusable root and the face
    // inherits the tree's opacity like the caption does.
    class SLDSkinnedButton : public SButton
    {
    public:
        SLATE_BEGIN_ARGS(SLDSkinnedButton){}
        SLATE_END_ARGS()
        void Construct(const FArguments&,TWeakObjectPtr<ULDCommandButton> InOwner,const SButton::FArguments& ButtonArgs)
        {Owner=InOwner;SButton::Construct(ButtonArgs);}
        virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& WidgetStyle,bool bParentEnabled) const override
        {
            const ULDCommandButton* Button=Owner.Get();
            if(Button&&Button->GetStyle().Normal.DrawAs==ESlateBrushDrawType::NoDrawType)
            {
                LDUIPaint::FButtonState State;
                State.bHovered=IsHovered();State.bPressed=IsPressed();State.bEnabled=bParentEnabled&&IsEnabled();State.Tint=Button->Tint;
                State.Fade=WidgetStyle.GetColorAndOpacityTint().A;
                LDUIPaint::Button(Elements,Layer,Geometry,FBox2D(FVector2D::ZeroVector,Geometry.GetLocalSize()),Button->Look,State);
                Layer+=2;
            }
            return SButton::OnPaint(Args,Geometry,Culling,Elements,Layer,WidgetStyle,bParentEnabled);
        }
    private:
        TWeakObjectPtr<ULDCommandButton> Owner;
    };
}
namespace
{
    class SLDPaintLayer : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SLDPaintLayer){}
        SLATE_END_ARGS()
        void Construct(const FArguments&,TWeakObjectPtr<ULDPaintLayer> InOwner){Owner=InOwner;SetVisibility(EVisibility::HitTestInvisible);}
        virtual FVector2D ComputeDesiredSize(float) const override{return FVector2D::ZeroVector;}
        virtual int32 OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool) const override
        {
            const ULDPaintLayer* Paint=Owner.Get();
            if(Paint&&Paint->Painter){Paint->Painter(LDUIPaint::FContext(Elements,Layer,Geometry,Style.GetColorAndOpacityTint().A));}
            // The painter draws on a handful of layers above Layer.
            return Layer+12;
        }
    private:
        TWeakObjectPtr<ULDPaintLayer> Owner;
    };
}
TSharedRef<SWidget> ULDPaintLayer::RebuildWidget()
{
    Layer=SNew(SLDPaintLayer,TWeakObjectPtr<ULDPaintLayer>(this));
    return Layer.ToSharedRef();
}
void ULDPaintLayer::ReleaseSlateResources(bool bReleaseChildren){Super::ReleaseSlateResources(bReleaseChildren);Layer.Reset();}
TSharedRef<SWidget> ULDCommandButton::RebuildWidget()
{
    // UButton::RebuildWidget with the skinned SButton in place of the plain one.
    SButton::FArguments Args;
PRAGMA_DISABLE_DEPRECATION_WARNINGS
    Args.OnClicked(BIND_UOBJECT_DELEGATE(FOnClicked,SlateHandleClicked))
        .OnPressed(BIND_UOBJECT_DELEGATE(FSimpleDelegate,SlateHandlePressed))
        .OnReleased(BIND_UOBJECT_DELEGATE(FSimpleDelegate,SlateHandleReleased))
        .OnHovered_UObject(this,&ThisClass::SlateHandleHovered)
        .OnUnhovered_UObject(this,&ThisClass::SlateHandleUnhovered)
        .OnReceivedFocus_UObject(this,&ThisClass::SlateHandleOnReceivedFocus)
        .OnLostFocus_UObject(this,&ThisClass::SlateHandleOnLostFocus)
        .OnSlateButtonDragDetected(BIND_UOBJECT_DELEGATE(FOnDragDetected,SlateHandleDragDetected))
        .OnSlateButtonDragEnter(BIND_UOBJECT_DELEGATE(FOnDragEnter,SlateHandleDragEnter))
        .OnSlateButtonDragLeave(BIND_UOBJECT_DELEGATE(FOnDragLeave,SlateHandleDragLeave))
        .OnSlateButtonDragOver(BIND_UOBJECT_DELEGATE(FOnDragOver,SlateHandleDragOver))
        .OnSlateButtonDrop(BIND_UOBJECT_DELEGATE(FOnDrop,SlateHandleDrop))
        .ButtonStyle(&WidgetStyle)
        .ClickMethod(ClickMethod)
        .TouchMethod(TouchMethod)
        .PressMethod(PressMethod)
        .IsFocusable(IsFocusable)
        .AllowDragDrop(bAllowDragDrop);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
    MyButton=SNew(SLDSkinnedButton,TWeakObjectPtr<ULDCommandButton>(this),Args);
    if(GetChildrenCount()>0){Cast<UButtonSlot>(GetContentSlot())->BuildSlot(MyButton.ToSharedRef());}
    return MyButton.ToSharedRef();
}
void LDWidgets::SetLook(ULDCommandButton* Button,LDUIPaint::EButton Look)
{
    if(!Button){return;}
    Button->Look=Look;
    // Yellow and pink faces carry dark or white captions; the dark faces carry the paper colour.
    if(UTextBlock* Caption=Cast<UTextBlock>(Button->GetContent()))
    {
        const FLinearColor Ink=Look==LDUIPaint::EButton::Primary||Look==LDUIPaint::EButton::Plus||Look==LDUIPaint::EButton::TagOn?LDUIPaint::Hex(0x15130A)
            :Look==LDUIPaint::EButton::Event?FLinearColor::White:Look==LDUIPaint::EButton::Tag||Look==LDUIPaint::EButton::Key?LDHUDStyle::TextSecondary:LDHUDStyle::UIText;
        Caption->SetColorAndOpacity(Ink);
    }
    if(TSharedPtr<SWidget> Cached=Button->GetCachedWidget()){Cached->Invalidate(EInvalidateWidgetReason::Paint);}
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
void LDWidgets::PlayCue(const UObject* Context,ELDAudioCue Cue)
{
    const UWorld* World=Context?Context->GetWorld():nullptr;UGameInstance* Instance=World?World->GetGameInstance():nullptr;
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
{StopTicker(FeelTicker);StopTicker(RepeatTicker);Super::ReleaseSlateResources(bReleaseChildren);}
namespace
{
// A held repeating button waits this long, then repeats at this interval.
constexpr double RepeatDelay=.35;
constexpr double RepeatInterval=.1;
}
void ULDCommandButton::EnableRepeat(TFunction<bool()> Again)
{
    RepeatCallback=MoveTemp(Again);
    OnPressed.AddUniqueDynamic(this,&ULDCommandButton::RepeatPressed);OnReleased.AddUniqueDynamic(this,&ULDCommandButton::RepeatReleased);
}
void ULDCommandButton::RepeatPressed()
{
    bRepeated=false;
    StopTicker(RepeatTicker);
    if(!RepeatCallback){return;}
    RepeatNextAt=FPlatformTime::Seconds()+RepeatDelay;
    RepeatTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[this](float)
    {
        if(!IsPressed()||!GetIsEnabled()||!IsVisible()){RepeatTicker.Reset();return false;}
        const double Now=FPlatformTime::Seconds();
        if(Now<RepeatNextAt){return true;}
        RepeatNextAt=Now+RepeatInterval;
        bRepeated=true;
        if(RepeatCallback()){PlayCue(ELDAudioCue::UIClick);return true;}
        RepeatTicker.Reset();return false;
    }));
}
// A release outside the button raises no click, so the mark of a repeated hold never waits for one.
void ULDCommandButton::RepeatReleased(){StopTicker(RepeatTicker);if(!IsHovered()){bRepeated=false;}}

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
    if(E.GetKey()==EKeys::Enter && E.IsControlDown())
    {
        if(auto* PC=Cast<ALDPlayerController>(GetOwningPlayer()); PC && !E.IsRepeat() && PC->ToggleDevTestMenu())
        {return FReply::Handled();}
        // A repeated test-menu chord must never activate the focused action button.
        return FReply::Handled();
    }
    if(E.GetKey()==EKeys::Tab){MovePanelFocus(E.IsShiftDown());return FReply::Handled();}
    if(E.GetKey()==EKeys::Enter||E.GetKey()==EKeys::SpaceBar)
    {
        for(auto* W:PanelFocusTargets()){if(W->HasKeyboardFocus()||W->HasFocusedDescendants())
        {
            if(auto* B=Cast<UButton>(W)){if(!E.IsRepeat()){B->OnClicked.Broadcast();}return FReply::Handled();}break;
        }}
    }
    return Super::NativeOnPreviewKeyDown(G,E);
}
int32 ULDPanel::NativePaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& C,FSlateWindowElementList& E,int32 L,const FWidgetStyle& S,bool Enabled) const
{
    const int32 Base=Super::NativePaint(A,G,C,E,L,S,Enabled);
    const auto Opacity=[&S](const UWidget* Widget)
    {
        float Alpha=S.GetColorAndOpacityTint().A;
        for(const UWidget* Current=Widget;Current;Current=Current->GetParent()){Alpha*=Current->GetRenderOpacity();}
        return Alpha;
    };
    for(auto* W:PanelFocusTargets()){if(W->HasKeyboardFocus()||W->HasFocusedDescendants())
    {
        const auto& WG=W->GetCachedGeometry();const FVector2D P=G.AbsoluteToLocal(WG.LocalToAbsolute(FVector2D::ZeroVector)),Q=G.AbsoluteToLocal(WG.LocalToAbsolute(WG.GetLocalSize()));
        // A focused target scrolled out of its list keeps focus; its outline is clipped to the list instead of floating over
        // the rest of the panel.
        const UScrollBox* List=nullptr;
        for(const UWidget* Parent=W->GetParent();Parent&&!List;Parent=Parent->GetParent()){List=Cast<UScrollBox>(Parent);}
        if(List){E.PushClip(FSlateClippingZone(List->GetCachedGeometry()));}
        FSlateDrawElement::MakeLines(E,Base+1,G.ToPaintGeometry(),{P,FVector2D(Q.X,P.Y),Q,FVector2D(P.X,Q.Y),P},ESlateDrawEffect::None,
            LDHUDStyle::FocusOutline.CopyWithNewOpacity(LDHUDStyle::FocusOutline.A*Opacity(W)),true,3);
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
