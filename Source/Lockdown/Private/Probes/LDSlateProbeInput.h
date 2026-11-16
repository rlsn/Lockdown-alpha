#pragma once

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/Widget.h"
#include "Framework/Application/SlateApplication.h"

// All events use the actual window hit-test/focus paths, never widget delegates or command handlers.
namespace LDSlateProbeInput
{
inline FWidgetPath Path(UWidget* Target)
{
    if(!Target||!FSlateApplication::IsInitialized()){return FWidgetPath();}
    const TSharedPtr<SWidget> Slate=Target->GetCachedWidget();
    auto& App=FSlateApplication::Get();
    const TSharedPtr<SWindow> Window=Slate?App.FindWidgetWindow(Slate.ToSharedRef()):nullptr;
    if(!Window){return FWidgetPath();}
    const FVector2D Point=Target->GetCachedGeometry().GetAbsolutePositionAtCoordinates(FVector2D(.5,.5));
    const FWidgetPath Found(Window->GetHittestGrid().GetBubblePath(Point,0.f,false,App.GetUserIndexForMouse()));
    return Found.IsValid()&&Found.ContainsWidget(Slate.Get())?Found:FWidgetPath();
}
inline bool Pointer(UWidget* Target,const FKey& Button,bool bDown,bool bUp)
{
    const FWidgetPath Hit=Path(Target);
    if(!Hit.IsValid()){return false;}
    auto& App=FSlateApplication::Get();
    const FVector2D At=Target->GetCachedGeometry().GetAbsolutePositionAtCoordinates(FVector2D(.5,.5));
    const uint32 User=App.GetUserIndexForMouse(),Index=FSlateApplicationBase::CursorPointerIndex;
    App.RoutePointerMoveEvent(Hit,FPointerEvent(User,Index,At,At,TSet<FKey>(),EKeys::Invalid,0,FModifierKeysState()),false);
    if(bDown){App.RoutePointerDownEvent(Hit,FPointerEvent(User,Index,At,At,TSet<FKey>({Button}),Button,0,FModifierKeysState()));}
    if(bUp){App.RoutePointerUpEvent(Hit,FPointerEvent(User,Index,At,At,TSet<FKey>(),Button,0,FModifierKeysState()));}
    return true;
}
inline bool SendClick(UWidget* Target,const FKey& Button=EKeys::LeftMouseButton)
{return Pointer(Target,Button,true,true);}
inline bool SendWheel(UWidget* Target,float Delta)
{
    const FWidgetPath Hit=Path(Target);
    if(!Hit.IsValid()){return false;}
    auto& App=FSlateApplication::Get();
    const FVector2D At=Target->GetCachedGeometry().GetAbsolutePositionAtCoordinates(FVector2D(.5,.5));
    return App.RouteMouseWheelOrGestureEvent(Hit,FPointerEvent(App.GetUserIndexForMouse(),FSlateApplicationBase::CursorPointerIndex,
        At,At,TSet<FKey>(),EKeys::Invalid,Delta,FModifierKeysState())).IsEventHandled();
}
inline bool SendKey(UWidget* Focus,const FKey& Key)
{
    if(!Focus||!FSlateApplication::IsInitialized()){return false;}
    Focus->SetKeyboardFocus();
    auto& App=FSlateApplication::Get();
    const FKeyEvent Event(Key,FModifierKeysState(),App.GetUserIndexForKeyboard(),false,0,0);
    App.ProcessKeyDownEvent(Event);App.ProcessKeyUpEvent(Event);return true;
}
inline bool FullyVisible(const UWidget* Target,const UWidget* Viewport)
{
    if(!Target||!Viewport||!Target->IsVisible()){return false;}
    const FGeometry& G=Target->GetCachedGeometry();
    const FGeometry& V=Viewport->GetCachedGeometry();
    const FVector2D A=V.AbsoluteToLocal(G.LocalToAbsolute(FVector2D::ZeroVector));
    const FVector2D B=V.AbsoluteToLocal(G.LocalToAbsolute(G.GetLocalSize()));
    return A.X>=0&&A.Y>=0&&B.X<=V.GetLocalSize().X+.5&&B.Y<=V.GetLocalSize().Y+.5&&B.X>A.X&&B.Y>A.Y;
}
}
#endif
