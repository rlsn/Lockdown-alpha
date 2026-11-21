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
    void Bind(ULDBasketballMotionSet* InMotion, UStaticMeshComponent* InBall);
    void Prepare(double Clock, float Speed, float FacingYaw, LDBodyMotion::EAction Action, float Age,
        float ImpactSeconds, double ActionIdentity, UAnimSequence* Override, float OverrideTime, bool bDash, bool bShowBall);
    const UAnimSequence* CurrentAction() const { return ActionSequence; }
    float CurrentActionTime() const { return ActionTime; }
    bool UsesUpperBody() const { return UpperWeight > 0; }
    uint32 TurnCount() const { return Turns; }
    virtual void NativePostEvaluateAnimation() override;
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
private:
    friend struct FLDBasketballAnimProxy;
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
    bool bHasFacing=false,bLeft=false,bBallVisible=false,bThrowing=false,bWasReleased=false,bNormalizeTurn=false;
    FVector ReleasePoint=FVector::ZeroVector,ReleaseForward=FVector::ForwardVector;
    uint32 Variation=0,Turns=0;
};
