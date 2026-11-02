#include "UI/LDInventoryPanel.h"
#include "UI/LDWidgets.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"

TSharedRef<SWidget> ULDInventoryPanel::RebuildWidget()
{
    if(!Heading)
    {
        SetIsFocusable(true);auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Canvas;
        MainPanel=LDWidgets::Panel(WidgetTree,nullptr);auto* PanelSlot=Canvas->AddChildToCanvas(MainPanel);PanelSlot->SetAnchors(FAnchors(.30f,.20f,.70f,.80f));PanelSlot->SetOffsets(FMargin(0));
        auto* All=WidgetTree->ConstructWidget<UVerticalBox>();MainPanel->SetContent(All);
        Heading=LDWidgets::Text(WidgetTree,TEXT("EQUIPMENT"),20);All->AddChildToVerticalBox(Heading);
        Status=LDWidgets::Text(WidgetTree,TEXT(""));All->AddChildToVerticalBox(Status)->SetPadding(FMargin(0,8));
        Slots=LDWidgets::Text(WidgetTree,TEXT(""),18);All->AddChildToVerticalBox(Slots)->SetPadding(FMargin(0,8));
        All->AddChildToVerticalBox(LDWidgets::Button(WidgetTree,TEXT("Close"),[this](){Close();}))->SetPadding(FMargin(0,8));
        Feedback=LDWidgets::Text(WidgetTree,TEXT("Buy and sell at the shop. The match continues."),14);All->AddChildToVerticalBox(Feedback);
    }
    return Super::RebuildWidget();
}
void ULDInventoryPanel::Open(){bOpen=true;SetVisibility(ESlateVisibility::Visible);CastChecked<ALDPlayerController>(GetOwningPlayer())->SetMenuOpen(true);SetKeyboardFocus();Refresh();}
void ULDInventoryPanel::Close(){CancelDrag();bOpen=false;SetVisibility(ESlateVisibility::Collapsed);CastChecked<ALDPlayerController>(GetOwningPlayer())->SetMenuOpen(false);}
void ULDInventoryPanel::CancelDrag(){if(HasMouseCapture()&&FSlateApplication::IsInitialized()){FSlateApplication::Get().ReleaseAllPointerCapture();}}
void ULDInventoryPanel::Refresh()
{
    if(!bOpen||!Heading){return;}auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto& O=PC->OwnerState;const auto* G=GetWorld()->GetGameState<ALDGameState>();
    if(!G||G->DemoLobby.Generation!=O.Match||(G->DemoLobby.Phase!=ELDMatchPhase::Development&&G->DemoLobby.Phase!=ELDMatchPhase::CaptureOpen)||O.LifePhase==ELDLifePhase::Left){Close();return;}
    const FLDEquipmentView* Equipment=PC->ReadOwnEquipment();
    if(!Equipment){Status->SetText(FText::FromString(TEXT("Equipment unavailable for this match")));Slots->SetText(FText::GetEmpty());return;}
    Status->SetText(FText::FromString(FString::Printf(TEXT("Gold %lld · %s"),Equipment->Gold,O.LifePhase==ELDLifePhase::Alive?TEXT("Alive"):TEXT("Waiting to return"))));
    FString Text;for(const FLDEquipmentSlotView& Row:Equipment->Slots){Text+=FString::Printf(TEXT("%d · %s\n"),Row.SlotIndex+1,Row.bOccupied?*Row.DefinitionId.ToString():TEXT("Empty"));}
    Slots->SetText(FText::FromString(Text.TrimEnd()));Feedback->SetText(FText::FromString(PC->IntentFeedback));
}
FReply ULDInventoryPanel::NativeOnKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
    if(E.GetKey()==PC->BindingKey(TEXT("Back"))){Close();return FReply::Handled();}
    return Super::NativeOnKeyDown(G,E);
}
UWidget* ULDInventoryPanel::FocusScope() const{return MainPanel.Get();}