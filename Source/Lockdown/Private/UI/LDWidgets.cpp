#include "UI/LDWidgets.h"
#include "UI/LDHUDStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"
UTextBlock* LDWidgets::Text(UWidgetTree* Tree,const FString& Value,int32 Size)
{
    auto* W=Tree->ConstructWidget<UTextBlock>();W->SetText(FText::FromString(Value));W->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",Size));
    W->SetColorAndOpacity(LDHUDStyle::TextPrimary);W->SetAutoWrapText(true);W->SetVisibility(ESlateVisibility::HitTestInvisible);return W;
}
ULDCommandButton* LDWidgets::Button(UWidgetTree* Tree,const FString& Value,TFunction<void()> Callback,bool World,FName Name)
{
    ULDCommandButton* W=World?Tree->ConstructWidget<ULDWorldButton>(ULDWorldButton::StaticClass(),Name):Tree->ConstructWidget<ULDCommandButton>(ULDCommandButton::StaticClass(),Name);W->Callback=MoveTemp(Callback);auto* Caption=Text(Tree,Value);Caption->SetAutoWrapText(false);W->SetContent(Caption);
    // Flat rounded faces from one teal family replace the stock engine chrome; the hairline edge keeps the shape readable on
    // any panel, and the same padding pressed or released keeps the caption still.
    auto Face=[](const FLinearColor& Fill,const FLinearColor& Edge){FSlateRoundedBoxBrush Brush(Fill,6.f,Edge,1.f);Brush.OutlineSettings.bUseBrushTransparency=true;return Brush;};
    auto Style=W->GetStyle();
    Style.Normal=Face(FLinearColor(.045f,.12f,.15f),FLinearColor(.12f,.30f,.33f));Style.Hovered=Face(FLinearColor(.09f,.24f,.27f),FLinearColor(.30f,.62f,.62f));
    Style.Pressed=Face(FLinearColor(.12f,.32f,.32f),FLinearColor(.40f,.75f,.72f));Style.Disabled=Face(FLinearColor(.035f,.05f,.065f),FLinearColor(.08f,.11f,.13f));
    Style.NormalPadding=FMargin(12,11);Style.PressedPadding=FMargin(12,11);W->SetStyle(Style);W->OnClicked.AddDynamic(W,&ULDCommandButton::Execute);return W;
}

#include "Components/PanelWidget.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/Slider.h"
#include "Components/Border.h"
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
    for(UWidget* W:All){if(auto* Label=Cast<UTextBlock>(W)){auto Font=Label->GetFont();Font.Size=FMath::RoundToInt(Font.Size*Scale/AppliedTextScale);Label->SetFont(Font);}}
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
        for(auto* W:PanelFocusTargets()){if(W->HasKeyboardFocus()||W->HasFocusedDescendants()){if(auto* B=Cast<UButton>(W)){if(!E.IsRepeat()){B->OnClicked.Broadcast();}return FReply::Handled();}break;}}
    }
    return Super::NativeOnPreviewKeyDown(G,E);
}
int32 ULDPanel::NativePaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& C,FSlateWindowElementList& E,int32 L,const FWidgetStyle& S,bool Enabled) const
{
    const int32 Base=Super::NativePaint(A,G,C,E,L,S,Enabled);
    for(auto* W:PanelFocusTargets()){if(W->HasKeyboardFocus()||W->HasFocusedDescendants())
    {
        const auto& WG=W->GetCachedGeometry();const FVector2D P=G.AbsoluteToLocal(WG.LocalToAbsolute(FVector2D::ZeroVector)),Q=G.AbsoluteToLocal(WG.LocalToAbsolute(WG.GetLocalSize()));
        FSlateDrawElement::MakeLines(E,Base+1,G.ToPaintGeometry(),{P,FVector2D(Q.X,P.Y),Q,FVector2D(P.X,Q.Y),P},ESlateDrawEffect::None,FLinearColor(1,.82f,.28f),true,3);break;
    }}return Base+1;
}
UBorder* LDWidgets::Panel(UWidgetTree* Tree,UWidget* Child,FMargin Padding)
{
    // The white rounded face takes its fill from the brush colour, so a caller that recolours a panel keeps its shape and rim.
    auto* W=Tree->ConstructWidget<UBorder>();W->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,6.f,LDHUDStyle::PanelRim,1.f));
    W->SetBrushColor(LDHUDStyle::PanelFill);W->SetPadding(Padding);if(Child){W->SetContent(Child);}return W;
}
