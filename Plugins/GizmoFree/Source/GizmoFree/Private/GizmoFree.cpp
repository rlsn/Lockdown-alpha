#include "GizmoFreeDrag.h"
#include "GizmoFreeSettings.h"

#include "Editor.h"
#include "Engine/Selection.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "ILevelEditor.h"
#include "Input/Events.h"
#include "Layout/WidgetPath.h"
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "Modules/ModuleManager.h"
#include "SceneView.h"
#include "Slate/SceneViewport.h"
#include "SLevelViewport.h"
#include "Widgets/SViewport.h"

DEFINE_LOG_CATEGORY_STATIC(LogGizmoFree, Log, All);

class FGizmoFreeInputProcessor final : public IInputProcessor
{
public:
    virtual const TCHAR* GetDebugName() const override { return TEXT("GizmoFree UE5.8"); }
    virtual void Tick(float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override
    {
        if (bTracking && (!GEditor || GEditor->PlayWorld || !GetDefault<UGizmoFreeSettings>()->bEnabled ||
            !SlateApp.IsActive() || SlateApp.GetActiveModalWindow().IsValid() || !Viewport.IsValid()))
        {
            Finish(false);
            bTracking = false;
        }
    }

    virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& Event) override
    {
        if (bTracking) { return Event.GetUserIndex() == UserIndex && Event.GetPointerIndex() == PointerIndex; }
        if (!GEditor || GEditor->PlayWorld || !Event.IsAltDown() || Event.IsShiftDown() || Event.IsCommandDown() ||
            !GetDefault<UGizmoFreeSettings>()->bEnabled || GEditor->IsTransactionActive() || SlateApp.HasAnyMouseCaptor() ||
            SlateApp.GetActiveModalWindow().IsValid()) { return false; }
        const FKey Button = Event.GetEffectingButton();
        if (Button != EKeys::LeftMouseButton && Button != EKeys::RightMouseButton && Button != EKeys::MiddleMouseButton) { return false; }

        TSharedPtr<SLevelViewport> Target = FindViewport(SlateApp, Event);
        if (!Target.IsValid()) { return false; }
        FLevelEditorViewportClient& Client = Target->GetLevelViewportClient();
        if (Client.IsTracking() || Client.IsSimulateInEditorViewport()) { return false; }
        EGizmoFreeOperation Operation;
        if (Button == EKeys::LeftMouseButton)
        {
            Operation = Event.IsControlDown() ? EGizmoFreeOperation::VerticalMove : EGizmoFreeOperation::PlaneMove;
        }
        else if (Button == EKeys::MiddleMouseButton) { Operation = EGizmoFreeOperation::Roll; }
        else { Operation = Client.GetWidgetMode() == UE::Widget::WM_Scale ? EGizmoFreeOperation::Scale : EGizmoFreeOperation::Rotate; }

        TArray<AActor*> Actors;
        GEditor->GetSelectedActors()->GetSelectedObjects(Actors);
        const UGizmoFreeSettings* Settings = GetDefault<UGizmoFreeSettings>();
        FGizmoFreeDragFrame Frame;
        Frame.MoveSensitivity = Settings->MoveSensitivity;
        Frame.RotationSensitivity = Settings->RotationSensitivity;
        Frame.ScaleSensitivity = Settings->ScaleSensitivity;
        if (!Session.Begin(Actors, Operation, Frame)) { return false; }

        // Derive actual world units per render pixel at the selection's depth. Also works in orthographic viewports.
        const TSharedPtr<FSceneViewport> SceneViewport = Target->GetSceneViewport();
        FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(SceneViewport.Get(), Client.GetScene(), Client.EngineShowFlags));
        const FSceneView* View = Client.CalcSceneView(&Family);
        if (!View) { Session.End(); return false; }
        const FIntRect Rect = View->UnscaledViewRect;
        const FVector2D Center(Rect.Min.X + Rect.Width() * 0.5, Rect.Min.Y + Rect.Height() * 0.5);
        const FMatrix Inv = View->ViewMatrices.GetClipToWorld();
        FVector Origin, Direction;
        FSceneView::DeprojectScreenToWorld(Center, Rect, Inv, Origin, Direction);
        Frame.ViewForward = Direction.GetSafeNormal();
        auto OnPlane = [&](const FVector2D& Pixel)
        {
            FVector RayOrigin, RayDirection;
            FSceneView::DeprojectScreenToWorld(Pixel, Rect, Inv, RayOrigin, RayDirection);
            const double Denominator = FVector::DotProduct(RayDirection, Frame.ViewForward);
            return RayOrigin + RayDirection * (FVector::DotProduct(Session.GetCentroid() - RayOrigin, Frame.ViewForward) / Denominator);
        };
        const FVector PlaneCenter = OnPlane(Center);
        Frame.PixelRight = OnPlane(Center + FVector2D(1, 0)) - PlaneCenter;
        Frame.PixelDown = OnPlane(Center + FVector2D(0, 1)) - PlaneCenter;
        Frame.ViewRight = Frame.PixelRight.GetSafeNormal();
        if (Frame.PixelRight.IsNearlyZero() || Frame.PixelDown.IsNearlyZero() || Frame.PixelRight.ContainsNaN() || Frame.PixelDown.ContainsNaN())
        {
            Session.End(); return false;
        }
        Session.End();
        if (!Session.Begin(Actors, Operation, Frame)) { return false; }
        Viewport = Target;
        TotalPixels = FVector2D::ZeroVector;
        DragButton = Button;
        UserIndex = Event.GetUserIndex();
        PointerIndex = Event.GetPointerIndex();
        bTracking = true;
        return true;
    }

    virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& Event) override
    {
        if (!Matches(Event)) { return false; }
        if (!Event.IsMouseButtonDown(DragButton)) { Finish(false); bTracking = false; return false; }
        if (!Event.IsAltDown()) { Finish(false); return true; }
        if (Session.IsActive())
        {
            // Slate deltas are desktop units; account for DPI when converting to render pixels.
            if (TSharedPtr<SLevelViewport> Target = Viewport.Pin())
            {
                const TSharedPtr<FSceneViewport> Scene = Target->GetSceneViewport();
                const FGeometry& Geometry = Scene->GetCachedGeometry();
                const FVector2D LocalDelta = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()) -
                    Geometry.AbsoluteToLocal(Event.GetLastScreenSpacePosition());
                const FVector2D LocalSize = Geometry.GetLocalSize();
                const FIntPoint RenderSize = Scene->GetSizeXY();
                if (LocalSize.X > 0 && LocalSize.Y > 0)
                {
                    TotalPixels += LocalDelta * FVector2D(RenderSize.X / LocalSize.X, RenderSize.Y / LocalSize.Y);
                    Session.Update(TotalPixels);
                }
            }
        }
        return true;
    }

    virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& Event) override
    {
        if (!Matches(Event)) { return false; }
        if (Event.GetEffectingButton() == DragButton) { Finish(false); bTracking = false; }
        return true;
    }

    virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& Event) override
    {
        if (!bTracking || Event.GetUserIndex() != UserIndex) { return false; }
        if (Event.GetKey() == EKeys::Escape) { Finish(true); return true; }
        // Suppress mode/selection shortcuts until the drag is released.
        return true;
    }

    virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& Event) override
    {
        if (!bTracking || Event.GetUserIndex() != UserIndex) { return false; }
        if (Event.GetKey() == EKeys::LeftAlt || Event.GetKey() == EKeys::RightAlt) { Finish(false); }
        return true;
    }

    void Finish(bool bCancel) { Session.End(bCancel); }

private:
    static TSharedPtr<SLevelViewport> FindViewport(FSlateApplication& SlateApp, const FPointerEvent& Event)
    {
        FLevelEditorModule* Module = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
        const TSharedPtr<ILevelEditor> Editor = Module ? Module->GetLevelEditorInstance().Pin() : nullptr;
        if (!Editor) { return nullptr; }
        const FWidgetPath Path = SlateApp.LocateWindowUnderMouse(Event.GetScreenSpacePosition(), SlateApp.GetInteractiveTopLevelWindows(), false, Event.GetUserIndex());
        for (const TSharedPtr<SLevelViewport>& Candidate : Editor->GetViewports())
        {
            if (!Candidate || !Candidate->IsVisible() || !Candidate->IsInForegroundTab() || !Candidate->GetSceneViewport()) { continue; }
            const TSharedPtr<SViewport> Widget = Candidate->GetSceneViewport()->GetViewportWidget().Pin();
            for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
            {
                const FArrangedWidget& Arranged = Path.Widgets[Index];
                if (Arranged.Widget == Widget) { return Candidate; }
            }
        }
        return nullptr;
    }

    bool Matches(const FPointerEvent& Event) const
    {
        return bTracking && Event.GetUserIndex() == UserIndex && Event.GetPointerIndex() == PointerIndex;
    }
    FGizmoFreeDragSession Session;
    TWeakPtr<SLevelViewport> Viewport;
    FVector2D TotalPixels = FVector2D::ZeroVector;
    FKey DragButton;
    uint32 UserIndex = 0;
    uint32 PointerIndex = 0;
    bool bTracking = false;
};

class FGizmoFreeModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        if (IsRunningCommandlet() || !FSlateApplication::IsInitialized()) { return; }
        Processor = MakeShared<FGizmoFreeInputProcessor>();
        FSlateApplication::Get().RegisterInputPreProcessor(Processor, 0);
        PrePIE = FEditorDelegates::PreBeginPIE.AddLambda([this](bool) { Processor->Finish(false); });
        MapChanged = FEditorDelegates::MapChange.AddLambda([this](uint32) { Processor->Finish(false); });
        UE_LOG(LogGizmoFree, Display, TEXT("GizmoFree 3.0 loaded for UE5.8: one Slate input processor registered."));
    }
    virtual void ShutdownModule() override
    {
        FEditorDelegates::PreBeginPIE.Remove(PrePIE);
        FEditorDelegates::MapChange.Remove(MapChanged);
        if (Processor)
        {
            Processor->Finish(false);
            if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().UnregisterInputPreProcessor(Processor); }
            Processor.Reset();
        }
    }
private:
    TSharedPtr<FGizmoFreeInputProcessor> Processor;
    FDelegateHandle PrePIE, MapChanged;
};

IMPLEMENT_MODULE(FGizmoFreeModule, GizmoFree)
