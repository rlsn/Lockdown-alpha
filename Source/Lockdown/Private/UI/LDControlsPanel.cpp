#include "UI/LDControlsPanel.h"
#include "UI/LDControlSettings.h"
#include "UI/LDWidgets.h"
#include "Framework/LDPlayerController.h"
#include "Navigation/LDNavigationComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> ULDControlsPanel::RebuildWidget()
{
    SetIsFocusable(true);
    if(!Rows)
    {
        auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Canvas;
        auto* Border=WidgetTree->ConstructWidget<UBorder>();Border->SetBrushColor(FLinearColor(.018f,.035f,.05f,.99f));Border->SetPadding(FMargin(20));
        auto* Frame=Canvas->AddChildToCanvas(Border);Frame->SetAnchors(FAnchors(.12f,.06f,.88f,.94f));Frame->SetOffsets(FMargin(0));
        auto* Main=WidgetTree->ConstructWidget<UVerticalBox>();Border->SetContent(Main);
        Main->AddChildToVerticalBox(LDWidgets::Text(WidgetTree,TEXT("CONTROLS & SETTINGS"),24));
        Main->AddChildToVerticalBox(LDWidgets::Text(WidgetTree,TEXT("The match continues. Inspecting changes no ownership. Arm an attack or skill, then confirm over the world. Release a held action to cancel.")))->SetPadding(FMargin(0,8));
        Feedback=LDWidgets::Text(WidgetTree,TEXT("Choose a binding to change. Escape always goes Back."));Main->AddChildToVerticalBox(Feedback);
        Preferences=LDWidgets::Text(WidgetTree,TEXT(""));Main->AddChildToVerticalBox(Preferences)->SetPadding(FMargin(0,6));
        auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Main->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Rows=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Rows);
        auto* Footer=WidgetTree->ConstructWidget<UHorizontalBox>();Main->AddChildToVerticalBox(Footer)->SetPadding(FMargin(0,12,0,0));
        Footer->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("Reset bindings"),[this](){auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());PC->Controls->ResetBindings();PC->RebuildV05Mappings();SavePreferences();}));
        Footer->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("Toggle edge pan"),[this](){auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());PC->Controls->bEdgePan=!PC->Controls->bEdgePan;PC->Navigation->bEdgePan=PC->Controls->bEdgePan;SavePreferences();}));
        Footer->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("Start follow on/off"),[this](){auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());PC->Controls->bStartFollowing=!PC->Controls->bStartFollowing;PC->Navigation->bFollowCamera=PC->Controls->bStartFollowing;SavePreferences();}));
        Footer->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("Text 100 / 125%"),[this](){auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());PC->Controls->TextScale=PC->Controls->TextScale>1?1:1.25;SavePreferences();}));
        Footer->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("Close"),[this](){Close();}));
    }
    Populate();return Super::RebuildWidget();
}
void ULDControlsPanel::Populate()
{
    if(!Rows){return;}auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());Rows->ClearChildren();
    const int32 Size=FMath::RoundToInt(16*PC->Controls->TextScale);
    Preferences->SetText(FText::FromString(FString::Printf(TEXT("Edge pan: %s   Start in follow: %s   Text: %.0f%%   Saved zoom: %.0f cm"),PC->Controls->bEdgePan?TEXT("ON"):TEXT("OFF"),PC->Controls->bStartFollowing?TEXT("ON"):TEXT("OFF"),100*PC->Controls->TextScale,PC->Controls->Zoom)));
    for(const auto& B:ULDControlSettings::Defaults())
    {
        const FName Id=B.Action;auto* Button=LDWidgets::Button(WidgetTree,B.Label+TEXT("  [")+PC->Controls->Label(Id)+TEXT("]"),[this,Id](){Capture=Id;Feedback->SetText(FText::FromString(TEXT("Press a new key or mouse button. Escape cancels.")));});
        CastChecked<UTextBlock>(Button->GetContent())->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",Size));Button->SetIsEnabled(Id!=TEXT("Back"));Rows->AddChildToVerticalBox(Button)->SetPadding(FMargin(0,3));
    }
}
void ULDControlsPanel::SavePreferences()
{auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());Feedback->SetText(FText::FromString(PC->Controls->Save()?TEXT("Local preferences saved."):TEXT("Changed for this session; preferences could not be saved.")));Populate();}
void ULDControlsPanel::Open(){bOpen=true;Capture=NAME_None;SetVisibility(ESlateVisibility::Visible);auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());PC->SetMenuOpen(true);SetKeyboardFocus();Populate();}
void ULDControlsPanel::Close(){Capture=NAME_None;bOpen=false;SetVisibility(ESlateVisibility::Collapsed);CastChecked<ALDPlayerController>(GetOwningPlayer())->SetMenuOpen(false);}
FReply ULDControlsPanel::CaptureKey(FKey Key)
{
    if(Key==EKeys::Escape){Capture=NAME_None;Feedback->SetText(FText::FromString(TEXT("Binding unchanged.")));return FReply::Handled();}
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());FString Error;
    if(PC->Controls->Rebind(Capture,Key,Error)){Capture=NAME_None;PC->RebuildV05Mappings();const bool Saved=PC->Controls->Save();Feedback->SetText(FText::FromString(Saved?TEXT("Binding saved."):TEXT("Binding changed for this session; preferences could not be saved.")));Populate();}
    else{Feedback->SetText(FText::FromString(Error));}
    return FReply::Handled();
}
FReply ULDControlsPanel::NativeOnKeyDown(const FGeometry& G,const FKeyEvent& E)
{if(!Capture.IsNone()&&!E.IsRepeat()){return CaptureKey(E.GetKey());}if(E.GetKey()==EKeys::Escape){if(!E.IsRepeat()){Close();}return FReply::Handled();}return Super::NativeOnKeyDown(G,E);}
FReply ULDControlsPanel::NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E)
{if(!Capture.IsNone()){return E.IsRepeat()?FReply::Handled():CaptureKey(E.GetKey());}return Super::NativeOnPreviewKeyDown(G,E);}
FReply ULDControlsPanel::NativeOnPreviewMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{if(!Capture.IsNone()){return CaptureKey(E.GetEffectingButton());}return Super::NativeOnPreviewMouseButtonDown(G,E);}
FReply ULDControlsPanel::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{if(!Capture.IsNone()){return CaptureKey(E.GetEffectingButton());}return FReply::Handled();}
FReply ULDControlsPanel::NativeOnMouseWheel(const FGeometry& G,const FPointerEvent& E)
{if(!Capture.IsNone()){return CaptureKey(EKeys::MouseWheelAxis);}return Super::NativeOnMouseWheel(G,E);}
