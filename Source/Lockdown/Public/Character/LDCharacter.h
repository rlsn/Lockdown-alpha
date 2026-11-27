#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Vision/LDViewTypes.h"
#include "Character/LDHitFeel.h"
#include "LDCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class ULDHumanoidVisualComponent;
class FLDDemoContentSnapshot;
struct FLDCombatOwnerView;

UCLASS()
class LOCKDOWN_API ALDCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    explicit ALDCharacter(const FObjectInitializer& ObjectInitializer);
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float Delta) override;
    virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
    bool ConfigureDemoMovement(const FLDDemoContentSnapshot& Content, FString& Error);
    void SetFacing(float Yaw);
    void Zoom(float Axis);
    FVector PresentedLocation() const;
    void UpdateOwnerPresentation(float DeltaSeconds);
    // The one zoom rule for the body's camera arm and for the dead owner's static-map camera.
    static float ZoomedArmLength(float ArmLength, float Axis);
    // World pose of a camera on this class's default arm (fixed angle and world offset) centred on Origin.
    static FTransform DefaultArmCameraPose(const FVector& Origin, float ArmLength);
    // The presentation and camera subobjects are optional: a body class that no player ever owns or views through (a
    // camp member) creates none of them. A contender body always has all three.
    static const FName BodyComponentName;
    static const FName CameraArmComponentName;
    static const FName CameraComponentName;
    UPROPERTY(VisibleAnywhere) TObjectPtr<ULDHumanoidVisualComponent> Body;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> CameraArm;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
    // A contender's public identity slot (its locked roster position), written by the core before the body spawns
    // and the same for every life of the match; camp member bodies leave it unused.
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) uint8 PresentationKey = 0;
    static constexpr int32 ContenderPresentations = LDObservation::MaxPresentations;
    // The humanoid presentation code of an action cue and public statuses, shared by the own body and observed
    // proxies: pose Attack 1, Cast 2; markers root 16, camp_returning 32, shield 64, respawn_protection 128.
    static uint8 PresentationCue(ELDObservedActionCue Cue, const TArray<FLDObservedStatus>& Statuses);
    static bool PresentationKnockback(const TArray<FLDObservedStatus>& Statuses);
    static bool PresentationOwnerView(const FLDCombatOwnerView& View, const FLDOwnerState& Owner, bool bHasClock);
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
protected:
    UFUNCTION() void OnRep_Presentation();
private:
    // The local owner and life this body was last shown for: its removal after that life's death leaves a fall.
    TWeakObjectPtr<class ALDPlayerController> ShownOwner;
    uint32 ShownLife = 0;
    // The presentation key the body's look (model, worn parts and their cloth) was built for, or -1 before the body is
    // first shown. Only the local owner ever shows this body; every other authority body (camp members, the Boss, AI and
    // remote contenders on the Listen Host) stays hidden and is seen through observation proxies, so it builds no look.
    int32 LookKey = -1;
    void EnsureLook();
    // The last own hit receipt seen (LDHitFeel) and the camera kick it started along the facing.
    int64 ConfirmedAtMs = 0;
    float KickAge = 1.f;
    float KickCm = 0.f;
    // The own predicted swing (LDHitFeel::AdvanceSwing) for SwingLife, and the content's basic windup it shows until the
    // authority reports one.
    LDHitFeel::FSwingPrediction Swing;
    uint32 SwingLife = 0;
    int64 PredictWindupMs = 0;
    // COMBO.ResetSec: the own body's next swing after this long starts the combo again (its slash shape, LDSliceVfx).
    int64 ComboResetMs = 0;
    // The last tick showed the predicted swing rather than the authority's cue.
    bool bShownPredicted = false;
};
