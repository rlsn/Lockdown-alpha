#pragma once
#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "SceneView.h"

// World to screen for a paint pass that projects many points: the same pixels as
// APlayerController::ProjectWorldLocationToScreen, with the player's view projection read once instead of once per point.
// Made and used inside one paint; it holds no reference to the controller.
struct FLDWorldProjector
{
    explicit FLDWorldProjector(const APlayerController* PC)
    {
        const ULocalPlayer* Player=PC?PC->GetLocalPlayer():nullptr;
        FSceneViewProjectionData Data;
        if(Player&&Player->ViewportClient&&Player->GetProjectionData(Player->ViewportClient->Viewport,Data))
        {
            ViewProjection=Data.ComputeViewProjectionMatrix();
            Rect=Data.GetConstrainedViewRect();
            bValid=true;
        }
    }
    bool Project(const FVector& World,FVector2D& Screen) const
    {
        if(!bValid){Screen=FVector2D::ZeroVector;return false;}
        return FSceneView::ProjectWorldToScreen(World,Rect,ViewProjection,Screen);
    }
private:
    FMatrix ViewProjection=FMatrix::Identity;
    FIntRect Rect;
    bool bValid=false;
};
