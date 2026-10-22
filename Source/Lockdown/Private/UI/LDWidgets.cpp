#include "UI/LDWidgets.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"
UTextBlock* LDWidgets::Text(UWidgetTree* Tree,const FString& Value,int32 Size)
{
    auto* W=Tree->ConstructWidget<UTextBlock>();W->SetText(FText::FromString(Value));W->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",Size));
    W->SetColorAndOpacity(FLinearColor(.9f,.94f,.96f));W->SetAutoWrapText(true);W->SetVisibility(ESlateVisibility::HitTestInvisible);return W;
}
ULDCommandButton* LDWidgets::Button(UWidgetTree* Tree,const FString& Value,TFunction<void()> Callback,bool World)
{
    ULDCommandButton* W=World?Tree->ConstructWidget<ULDWorldButton>():Tree->ConstructWidget<ULDCommandButton>();W->Callback=MoveTemp(Callback);auto* Caption=Text(Tree,Value);Caption->SetAutoWrapText(false);W->SetContent(Caption);
    auto Style=W->GetStyle();Style.Normal.TintColor=FSlateColor(FLinearColor(.045f,.12f,.15f));Style.Hovered.TintColor=FSlateColor(FLinearColor(.09f,.24f,.27f));Style.Pressed.TintColor=FSlateColor(FLinearColor(.12f,.32f,.32f));
    Style.NormalPadding=FMargin(12,11);Style.PressedPadding=FMargin(12,11);W->SetStyle(Style);W->OnClicked.AddDynamic(W,&ULDCommandButton::Execute);return W;
}

#include "Components/PanelWidget.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/Border.h"
#include "Rendering/DrawElements.h"
UWidget* ULDPanel::FocusScope() const{return WidgetTree?WidgetTree->RootWidget:nullptr;}
TArray<UWidget*> ULDPanel::PanelFocusTargets() const
{
    TArray<UWidget*> Result;
    TFunction<void(UWidget*)> Visit=[&](UWidget* W)
    {
        if(!W||!W->IsVisible()||!W->GetIsEnabled()){return;}
        if((Cast<UButton>(W)&&CastChecked<UButton>(W)->GetIsFocusable())||Cast<UEditableTextBox>(W)){Result.Add(W);}
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
    auto* W=Tree->ConstructWidget<UBorder>();W->SetBrushColor(FLinearColor(.025f,.045f,.07f,.99f));W->SetPadding(Padding);if(Child){W->SetContent(Child);}return W;
}
UScrollBox* LDWidgets::OfferTray(UWidgetTree* Tree,UTextBlock*& Label,int32 Size)
{
    Label=Text(Tree,TEXT(""),Size);auto* Scroll=Tree->ConstructWidget<UScrollBox>();Scroll->SetAlwaysShowScrollbar(true);Scroll->AddChild(Label);return Scroll;
}
