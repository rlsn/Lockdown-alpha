#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Character/LDBasketballMotion.h"
#include "LDBasketballAnimInstance.generated.h"

class UStaticMeshComponent;

// One local presentation graph for the owned body and authorized proxies alike. There is no game-rule state here.
UCLASS(Transient)
class LOCKDOWN_API ULDBasketballAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    void Bind(ULDBasketballMotionSet* InMotion, UStaticMeshComponent* InBall, const FVector& InMeshForward);
    void Prepare(double Clock, float Speed, float FacingYaw, LDBodyMotion::EAction Action, float Age,
        float ImpactSeconds, double ActionIdentity, UAnimSequence* Override, float OverrideTime, bool bDash, bool bShowBall);
    // The motion family of the next attack or cast (LDActionMotion): the shown basic's combo step and the cast's ability.
    void SetActionStyle(uint8 InComboStep, uint8 InAbility) { StyleStep = InComboStep; StyleAbility = InAbility; }
    const UAnimSequence* CurrentAction() const { return ActionSequence; }
    float CurrentActionTime() const { return ActionTime; }
    bool UsesUpperBody() const { return UpperWeight > 0; }
    uint32 TurnCount() const { return Turns; }
    virtual void NativeUninitializeAnimation() override;
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
private:
    friend struct FLDBasketballAnimProxy;
    void UpdateBallFromFinalPose();
    UPROPERTY(Transient) TObjectPtr<ULDBasketballMotionSet> Motion;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Ball;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BaseSequence;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> PreviousSequence;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> ActionSequence;
    float BaseTime=0,PreviousTime=0,BaseBlend=1,ActionTime=0,FullWeight=0,UpperWeight=0,ArmWeight=0;
    double LastClock=-1,LastActionIdentity=-1,TurnAt=-1,NextTurnAt=0;
    LDBodyMotion::EAction LastAction=LDBodyMotion::EAction::None;
    FLDBasketballActionClip Active;
    float IdleAge=0,LastFacing=0,TurnDuration=0,ActionWeight=0;
    float CurrentAge=0,CurrentImpact=0,CurrentDuration=0;
    float RunBounceHeight=0,RunDribbleWeight=0;
    bool bHasFacing=false,bLeft=false,bBallVisible=false,bThrowing=false,bWasReleased=false,bNormalizeTurn=false;
    FVector MeshForward=FVector::ForwardVector;
    FVector ReleasePoint=FVector::ZeroVector,ReleaseForward=FVector::ForwardVector;
    FDelegateHandle BallPoseHandle;
    uint32 Variation=0,Turns=0;
    uint8 StyleStep=0,StyleAbility=0;
};
