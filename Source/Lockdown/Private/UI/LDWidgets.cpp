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
    ULDCommandButton* W=World?Tree->ConstructWidget<ULDWorldButton>():Tree->ConstructWidget<ULDCommandButton>();W->Callback=MoveTemp(Callback);W->SetContent(Text(Tree,Value));
    auto Style=W->GetStyle();Style.Normal.TintColor=FSlateColor(FLinearColor(.045f,.12f,.15f));Style.Hovered.TintColor=FSlateColor(FLinearColor(.09f,.24f,.27f));Style.Pressed.TintColor=FSlateColor(FLinearColor(.12f,.32f,.32f));
    Style.NormalPadding=FMargin(12,11);Style.PressedPadding=FMargin(12,11);W->SetStyle(Style);W->OnClicked.AddDynamic(W,&ULDCommandButton::Execute);return W;
}
