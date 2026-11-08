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
    uint32 ServerCorrectionCount=0;
    bool AdvanceAuthorityMovement(const FLDAuthorityStep& Step, const FLDCombatBodyRef& Body,
        const FVector& Input, const FLDGameplayWritePermit& Permit, FString& Error);
    // Demo dash on the server authority body: one planar override root-motion source (Distance/Duration,
    // zero finish velocity) integrated by the issued authority steps; never a teleport.
    bool StartDemoDash(const FVector& Direction, double DistanceCm, double DurationSeconds, FString& Error);
    // Sweeps the current capsule along a planar direction against static geometry; false when immediately blocked.
    bool CanStartDash(const FVector& Direction, double ProbeCm) const;
    // The dash root motion is running: its moves pass through other bodies (#97), static geometry still stops it.
    bool IsDashing() const;
    virtual void PhysWalking(float DeltaSeconds, int32 Iterations) override;
    virtual void PhysFalling(float DeltaSeconds, int32 Iterations) override;
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
