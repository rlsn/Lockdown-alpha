#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Character/LDCharacterMovementComponent.h"
#include "LDNativeMotionProbe.generated.h"

struct FLDNativeMotionTraceRow
{
    FName Kind;
    FVector From=FVector::ZeroVector;
    FVector To=FVector::ZeroVector;
    double NativeSeconds=0;
    double NativeFraction=0;
    int32 ScopeDepth=0;
    bool Result=false;
    bool DiscardedStepUp=false;
    bool GroundedBefore=false;
    bool GroundedAfter=false;
};

UCLASS(Transient)
class ULDNativeMotionProbeMovement : public ULDCharacterMovementComponent
{
    GENERATED_BODY()
public:
    mutable TArray<FLDNativeMotionTraceRow> Trace;
    bool Tracing=false;
    TSharedPtr<const FLDNativeMotionBatch> Captured;
    FString CaptureError;
    bool PrepareGround();
    void TraceWalking(const FVector& InVelocity, float Seconds);
    bool TraceStationary(const FVector& Input=FVector::ZeroVector);
    virtual void PerformMovement(float DeltaSeconds) override;
    void TraceUnrecordedMove();
    // Applies the production dash root motion and integrates it in fixed native steps outside any capture scope.
    bool TraceDash(const FVector& Direction, double DistanceCm, double DurationSeconds, float StepSeconds, int32 Steps, FString& Error);
    virtual void MoveAlongFloor(const FVector& InVelocity, float DeltaSeconds, FStepDownResult* OutStepDownResult) override;
    virtual bool StepUp(const FVector& GravDir, const FVector& Delta, const FHitResult& InHit, FStepDownResult* OutStepDownResult) override;
    virtual float SlideAlongSurface(const FVector& Delta, float Time, const FVector& Normal, FHitResult& Hit, bool HandleImpact) override;
    using UMovementComponent::SafeMoveUpdatedComponent;
    virtual bool SafeMoveUpdatedComponent(const FVector& Delta, const FQuat& Rotation, bool Sweep,
        FHitResult& Hit, ETeleportType Teleport=ETeleportType::None) override;
    virtual bool ResolvePenetrationImpl(const FVector& Adjustment, const FHitResult& Hit, const FQuat& Rotation) override;
    virtual FVector GetLedgeMove(const FVector& OldLocation, const FVector& Delta, const FFindFloorResult& OldFloor) const override;
    virtual bool CheckFall(const FFindFloorResult& OldFloor, const FHitResult& Hit, const FVector& Delta,
        const FVector& OldLocation, float RemainingTime, float TimeTick, int32 Iterations, bool MustJump) override;
    virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
    virtual void FindFloor(const FVector& CapsuleLocation, FFindFloorResult& OutFloorResult,
        bool UseCachedLocation, const FHitResult* DownwardSweepResult=nullptr) const override;
protected:
    virtual bool MoveUpdatedComponentImpl(const FVector& Delta, const FQuat& Rotation, bool Sweep,
        FHitResult* Hit=nullptr, ETeleportType Teleport=ETeleportType::None) override;
private:
    bool CheckStationary=false, SkippedStationary=false;
    int32 BeginTrace(FName Kind, double Seconds=0, double Fraction=0) const;
    void EndTrace(int32 Index, bool Result) const;
};

UCLASS(Transient)
class ALDNativeMotionProbeCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    ALDNativeMotionProbeCharacter(const FObjectInitializer& Initializer);
};
