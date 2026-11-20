#pragma once
#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/LDNavigationComponent.h"
#include "Character/LDNativeMotionTypes.h"
#include "LDCharacterMovementComponent.generated.h"

struct FLDNetworkMoveData : FCharacterNetworkMoveData
{
    FLDMoveIdentity Navigation;
    virtual void ClientFillNetworkMoveData(const FSavedMove_Character& Move,ENetworkMoveType Type) override;
    virtual bool Serialize(UCharacterMovementComponent& Movement,FArchive& Ar,UPackageMap* Map,ENetworkMoveType Type) override;
};
struct FLDNetworkMoveDataContainer : FCharacterNetworkMoveDataContainer
{
    FLDNetworkMoveData Moves[3];
    FLDNetworkMoveDataContainer(){NewMoveData=&Moves[0];PendingMoveData=&Moves[1];OldMoveData=&Moves[2];}
};

// Owner prediction of a walk: the owning client starts walking in the click frame and the server starts the same order
// when the request arrives, so both stand at the same point at the instant the server checks a client move sent at the
// same moment; what is left is frame and arrival quantization. While either side walks, an error within
// MaxSpeed*WalkLeadSeconds+WalkSlackCm is no correction. At rest, under root motion (dash, knockback) and in another
// movement mode the native check stands.
namespace LDOwnerPrediction
{
    inline constexpr double WalkLeadSeconds=.1;
    inline constexpr double WalkSlackCm=10.;
    LOCKDOWN_API bool AcceptsWalkError(const FVector& ServerMinusClient, double MaxSpeedCmPerSec);
}

// Owner corrections are still applied to the capsule immediately. Only the local model/camera ease back afterwards.
struct LOCKDOWN_API FLDOwnerCorrectionSmoothing
{
    FVector Offset=FVector::ZeroVector;
    void Reconcile(const FVector& OldPresented, const FVector& NewPosition, double MaxSmoothCm, double SnapCm);
    void Advance(float DeltaSeconds, float SmoothSeconds);
};

UCLASS()
class LOCKDOWN_API ULDCharacterMovementComponent : public UCharacterMovementComponent
{
    GENERATED_BODY()
public:
    ULDCharacterMovementComponent();
    virtual void PerformMovement(float DeltaSeconds) override;
    virtual float GetMaxSpeed() const override;
    // Spec §8.3 ice inertia: acceleration and braking scaled by the authoritative movement parameter (server: Combat; owning
    // client: its owner view), so prediction and correction agree.
    virtual float GetMaxAcceleration() const override;
    virtual float GetMaxBrakingDeceleration() const override;
    float AccelScale() const;
    virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
    virtual void MoveAutonomous(float TimeStamp,float Delta,uint8 Flags,const FVector& Accel) override;
    virtual bool ForcePositionUpdate(float DeltaTime) override;
    virtual void ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData) override;
    virtual bool ServerShouldUseAuthoritativePosition(float ClientTimeStamp, float DeltaTime, const FVector& Accel,
        const FVector& ClientWorldLocation, const FVector& RelativeClientLocation,
        FMovementBaseInterfaceData* ClientBase, FName ClientBaseBoneName, uint8 ClientMovementMode) override;
    // Server: counts the client moves this server answers with a correction (the Host's per-connection playtest summary).
    virtual bool ServerCheckClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel,
        const FVector& ClientWorldLocation, const FVector& RelativeClientLocation,
        FMovementBaseInterfaceData* ClientBase, FName ClientBaseBoneName, uint8 ClientMovementMode) override;
    // Server: a walking owner's position error within LDOwnerPrediction::AcceptsWalkError is no error.
    using UCharacterMovementComponent::ServerExceedsAllowablePositionError;
    virtual bool ServerExceedsAllowablePositionError(float ClientTimeStamp, float DeltaTime, const FVector& Accel,
        const FVector& ClientWorldLocation, const FVector& RelativeClientLocation,
        FMovementBaseInterfaceData* ClientBase, FName ClientBaseBoneName, uint8 ClientMovementMode) override;
    uint32 ServerCorrectionCount=0;
    // Server: client moves whose walking lead stayed inside the prediction bound (no correction sent).
    uint64 ServerAcceptedWalkLeads=0;
    bool AdvanceAuthorityMovement(const FLDAuthorityStep& Step, const FLDCombatBodyRef& Body,
        const FVector& Input, const FLDGameplayWritePermit& Permit, FString& Error);
    // Demo dash on the server authority body: one planar override root-motion source (Distance/Duration,
    // zero finish velocity) integrated by the issued authority steps; never a teleport.
    // bKnockback: a forced push, which other bodies block like static geometry (#198 F C02); a voluntary dash runs
    // through bodies.
    bool StartDemoDash(const FVector& Direction, double DistanceCm, double DurationSeconds, FString& Error, bool bKnockback=false);
    // #198 F C02: the first real block of the current push (a body, or registered static geometry facing the push; walkable
    // support and grazing slides are none). Watching starts with WatchKnockbackBlock and ends with the first fact.
    struct FKnockbackBlock
    {
        bool bBody=false;
        TWeakObjectPtr<AActor> Blocker;
        FVector Direction=FVector::ZeroVector;
    };
    void WatchKnockbackBlock() { bWatchKnockbackBlock=true; KnockbackBlock.Reset(); }
    TOptional<FKnockbackBlock> TakeKnockbackBlock()
    { TOptional<FKnockbackBlock> Out=MoveTemp(KnockbackBlock); KnockbackBlock.Reset(); return Out; }
    void StopKnockbackWatch() { bWatchKnockbackBlock=false; KnockbackBlock.Reset(); }
    // Sweeps the current capsule along a planar direction against static geometry; false when immediately blocked by a
    // surface facing the dash within DashHeadOnDegrees of head-on (a wall met at an angle is slid along).
    static constexpr double DashHeadOnDegrees = 25.;
    bool CanStartDash(const FVector& Direction, double ProbeCm) const;
    // The dash root motion is running: its moves pass through other bodies (#97), static geometry still stops it.
    bool IsDashing() const;
    void StopDemoDash();
    // A skill branch's ghost window (S11 B): this body's own moves pass through other bodies for Seconds from now, the dash
    // included; static geometry still stops them.
    void SetBodyPassThroughSeconds(double Seconds);
    virtual void StartFalling(int32 Iterations, float RemainingTime, float TimeTick, const FVector& Delta, const FVector& SubLocation) override;
    virtual void PhysWalking(float DeltaSeconds, int32 Iterations) override;
    virtual void PhysFalling(float DeltaSeconds, int32 Iterations) override;
    virtual FVector NewFallVelocity(const FVector& InitialVelocity, const FVector& Gravity, float DeltaTime) const override;
    virtual void MoveAlongFloor(const FVector& InVelocity, float DeltaSeconds, FStepDownResult* OutStepDownResult) override;
    virtual bool StepUp(const FVector& GravityDirection, const FVector& Delta, const FHitResult& Hit, FStepDownResult* OutStepDownResult) override;
    virtual FVector GetLedgeMove(const FVector& OldLocation, const FVector& Delta, const FFindFloorResult& OldFloor) const override;
    virtual bool CheckFall(const FFindFloorResult& OldFloor, const FHitResult& Hit, const FVector& Delta,
        const FVector& OldLocation, float RemainingTime, float TimeTick, int32 Iterations, bool MustJump) override;
    using UMovementComponent::SafeMoveUpdatedComponent;
    virtual bool SafeMoveUpdatedComponent(const FVector& Delta, const FQuat& Rotation, bool Sweep,
        FHitResult& Hit, ETeleportType Teleport=ETeleportType::None) override;
    virtual bool ResolvePenetrationImpl(const FVector& Adjustment, const FHitResult& Hit, const FQuat& Rotation) override;
    virtual void AdjustFloorHeight() override;
    virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
    virtual void FindFloor(const FVector& CapsuleLocation, FFindFloorResult& OutFloorResult,
        bool UseCachedLocation, const FHitResult* DownwardSweepResult=nullptr) const override;
    virtual void OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData,float TimeStamp,FVector NewLocation,FVector NewVelocity,FMovementBaseInterfaceData* NewBase,FName NewBaseBoneName,bool bHasBase,bool bBaseRelativePosition,uint8 ServerMovementMode,FVector ServerGravityDirection) override;
    virtual bool ClientUpdatePositionAfterServerUpdate() override;
    void AdvanceOwnerPresentation(float DeltaSeconds);
    FVector OwnerPresentationOffset() const { return OwnerSmoothing.Offset; }
    uint32 CorrectionCount=0;
    double MaxCorrectionCm=0;
    double LastCorrectionCm=0;
    uint64 AuthorityIntegrationCount=0, RejectedAuthorityMoves=0, NetworkBookkeepingMoves=0, SuppressedNetworkSimulationMoves=0;
    uint64 AuthorityStationarySteps=0;
    double AuthorityIntegratedSeconds=0;
protected:
    bool TryAdvanceStationaryMovement(float DeltaSeconds);
    // The dash root motion itself (no authority or Demo-world gate): a planar override force covering DistanceCm in
    // DurationSeconds that stops against blocking geometry and ends with zero velocity. Only StartDemoDash and the
    // isolated native probes call it.
    bool ApplyDashRootMotion(const FVector& Direction, double DistanceCm, double DurationSeconds, FString& Error);
    virtual bool MoveUpdatedComponentImpl(const FVector& Delta, const FQuat& Rotation, bool Sweep,
        FHitResult* Hit=nullptr, ETeleportType Teleport=ETeleportType::None) override;
#if WITH_DEV_AUTOMATION_TESTS
    bool BeginIsolatedNativeMotionProbe(float DeltaSeconds, FString& Error);
    bool EndIsolatedNativeMotionProbe(TSharedPtr<const FLDNativeMotionBatch>& Out, FString& Error);
#endif
private:
    void RememberStationarySupport();
    bool bKnockbackMotion=false;
    bool bWatchKnockbackBlock=false;
    double BodyPassThroughUntilSeconds=0;
    TOptional<FKnockbackBlock> KnockbackBlock;
    TWeakObjectPtr<UPrimitiveComponent> StationaryFloor;
    FTransform StationaryFloorTransform;
    FVector StationaryCapsuleSize=FVector::ZeroVector;
    FLDWorldRevision StationaryWorld;
    FLDCombatBodyRef StationaryBody;
    struct FNativeCapture;
    TSharedPtr<FNativeCapture> NativeCapture;
    uint64 NativeRecordSequence=0;
    bool BeginNativeMotionCapture(const FLDAuthorityStep& Step, const FLDCombatBodyRef& Body,
        const FGuid& Stream, const FLDWorldRevision& World, FString& Error);
    bool FinishNativeMotionCapture(TSharedPtr<const FLDNativeMotionBatch>& Out, FString& Error);
    void CaptureNativeSupport() const;
    bool bAuthorityMovementRunning=false;
    bool bStationaryAuthorityInput=false;
    bool bNativeIntegrationActive=false;
    const FLDGameplayWritePermit* NativeWritePermit=nullptr;
    FLDCombatBodyRef NativeBody;
    FLDNetworkMoveDataContainer NetworkMoves;
    FLDOwnerCorrectionSmoothing OwnerSmoothing;
    TOptional<FVector> BeforeCorrection;
};
