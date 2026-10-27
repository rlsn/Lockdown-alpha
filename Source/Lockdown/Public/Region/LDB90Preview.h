#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "LDB90Preview.generated.h"

// Spatial review only. B92 will admit full matches after roster/economy integration.
UCLASS()
class LOCKDOWN_API ALDB90PreviewMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ALDB90PreviewMode();
    virtual void InitGame(const FString& MapName, const FString& Options, FString& Error) override;
};

UCLASS()
class LOCKDOWN_API ALDB90PreviewController : public APlayerController
{
    GENERATED_BODY()
public:
    ALDB90PreviewController();
    virtual void PlayerTick(float DeltaSeconds) override;
    bool NavigateTo(const FVector& Goal);
    void StopNavigation();
    bool IsNavigating() const { return Path.IsValidIndex(Waypoint); }
    uint32 NavigationReplans = 0;
    uint32 NavigationFailures = 0;
    bool RoutesVisible = false, PadsVisible = true, BandsVisible = false, ProxiesVisible = false;
    FString Feedback;
private:
    TArray<FVector> Path;
    int32 Waypoint = 0;
    double ProbeStart = -1;
    double ProbeLength = 0;
    bool ProbeStarted = false;
    int32 CaptureState = 0;
    bool TraversalStarted = false;
    FVector Destination = FVector::ZeroVector;
    FVector ProgressPosition = FVector::ZeroVector;
    double ProgressAt = 0;
    int32 Stalls = 0;
    FVector ProbeOrigin = FVector::ZeroVector;
    bool Plan(const FVector& Goal);
};

UCLASS()
class LOCKDOWN_API ALDB90PreviewHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};
