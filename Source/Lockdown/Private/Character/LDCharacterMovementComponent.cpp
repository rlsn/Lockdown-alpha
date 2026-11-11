#include "Character/LDCharacterMovementComponent.h"
#include "Core/LDLog.h"
#include "GameFramework/Character.h"
#include "GameFramework/RootMotionSource.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Combat/LDCombatDirector.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Character/LDCharacter.h"
#include "Engine/World.h"
#include "Framework/LDDemoEntry.h"
#include "Region/LDMapRegistry.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace LDCharacterMovementDetail
{
    bool IsDemoMovement(const UWorld* World)
    { return World && UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())==LDDemoEntry::WorldMap; }

    const FName DashSourceName(TEXT("LDDemoDash"));
}

void ULDCharacterMovementComponent::OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData,float TimeStamp,FVector NewLocation,FVector NewVelocity,FMovementBaseInterfaceData* NewBase,FName NewBaseBoneName,bool bHasBase,bool bBaseRelativePosition,uint8 ServerMovementMode,FVector ServerGravityDirection)
{
    ++CorrectionCount;
    if (UpdatedComponent)
    {
        if (!BeforeCorrection) { BeforeCorrection=UpdatedComponent->GetComponentLocation()+OwnerSmoothing.Offset; }
        // NewLocation is already rebased by CMC before this callback, including base-relative corrections.
        const FVector Predicted=ClientData.LastAckedMove.IsValid()
            ? ClientData.LastAckedMove->SavedLocation : UpdatedComponent->GetComponentLocation();
        LastCorrectionCm=FVector::Distance(Predicted,NewLocation);
        MaxCorrectionCm=FMath::Max(MaxCorrectionCm,LastCorrectionCm);
    }
    Super::OnClientCorrectionReceived(ClientData,TimeStamp,NewLocation,NewVelocity,NewBase,NewBaseBoneName,bHasBase,bBaseRelativePosition,ServerMovementMode,ServerGravityDirection);
}

void FLDOwnerCorrectionSmoothing::Reconcile(const FVector& OldPresented, const FVector& NewPosition, double MaxSmoothCm, double SnapCm)
{
    const FVector Difference=OldPresented-NewPosition;
    Offset=Difference.SizeSquared()>FMath::Square(SnapCm) ? FVector::ZeroVector : Difference.GetClampedToMaxSize(MaxSmoothCm);
}

void FLDOwnerCorrectionSmoothing::Advance(float DeltaSeconds, float SmoothSeconds)
{
    if (SmoothSeconds<=0) { Offset=FVector::ZeroVector; return; }
    Offset*=FMath::Exp(-FMath::Max(0.f,DeltaSeconds)/SmoothSeconds);
    if (Offset.IsNearlyZero(.01)) { Offset=FVector::ZeroVector; }
}

bool ULDCharacterMovementComponent::ClientUpdatePositionAfterServerUpdate()
{
    const bool Replayed=Super::ClientUpdatePositionAfterServerUpdate();
    if (BeforeCorrection && UpdatedComponent)
    {
        const auto* Data=GetPredictionData_Client_Character();
        OwnerSmoothing.Reconcile(*BeforeCorrection,UpdatedComponent->GetComponentLocation(),
            Data->MaxSmoothNetUpdateDist,Data->NoSmoothNetUpdateDist);
        BeforeCorrection.Reset();
    }
    return Replayed;
}

void ULDCharacterMovementComponent::AdvanceOwnerPresentation(float DeltaSeconds)
{ OwnerSmoothing.Advance(DeltaSeconds,NetworkSimulatedSmoothLocationTime); }

namespace LDCharacterMovementDetail
{
    class FLDSavedMove : public FSavedMove_Character
    {
    public:
        using Super = FSavedMove_Character;
        FLDMoveIdentity Navigation;
        virtual void Clear() override { Super::Clear(); Navigation={}; }
        virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
        {
            return Navigation==static_cast<const FLDSavedMove*>(NewMove.Get())->Navigation
                && Super::CanCombineWith(NewMove, Character, MaxDelta);
        }
        virtual void SetMoveFor(ACharacter* Character, float Delta, const FVector& Accel, FNetworkPredictionData_Client_Character& Data) override
        {
            Super::SetMoveFor(Character, Delta, Accel, Data);
            if(const auto* PC=Cast<ALDPlayerController>(Character->GetController())){Navigation=PC->Navigation->PredictedIdentity();}
        }
    };
    class FLDClientPrediction : public FNetworkPredictionData_Client_Character
    {
    public:
        explicit FLDClientPrediction(const UCharacterMovementComponent& Movement) : FNetworkPredictionData_Client_Character(Movement) {}
        virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FLDSavedMove()); }
    };
}

ULDCharacterMovementComponent::ULDCharacterMovementComponent(){SetNetworkMoveDataContainer(NetworkMoves);}
void FLDNetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& Move,ENetworkMoveType Type)
{FCharacterNetworkMoveData::ClientFillNetworkMoveData(Move,Type);Navigation=static_cast<const LDCharacterMovementDetail::FLDSavedMove&>(Move).Navigation;}
bool FLDNetworkMoveData::Serialize(UCharacterMovementComponent& Movement,FArchive& Ar,UPackageMap* Map,ENetworkMoveType Type)
{
    const bool Valid=FCharacterNetworkMoveData::Serialize(Movement,Ar,Map,Type);
    Ar.SerializeIntPacked(Navigation.Match);Ar.SerializeIntPacked(Navigation.Life);
    Ar.SerializeIntPacked(Navigation.Order);Ar.SerializeIntPacked(Navigation.Epoch);
    return Valid&&!Ar.IsError();
}
void ULDCharacterMovementComponent::MoveAutonomous(float TimeStamp,float Delta,uint8 Flags,const FVector& Accel)
{
    if (CharacterOwner && CharacterOwner->HasAuthority() && LDCharacterMovementDetail::IsDemoMovement(GetWorld()))
    {
        ++SuppressedNetworkSimulationMoves;
        return;
    }
    FVector Applied=Accel;
    auto* PC=CharacterOwner?Cast<ALDPlayerController>(CharacterOwner->GetController()):nullptr;
    if(PC&&PC->Navigation->Enabled())
    {
        const auto* Data=static_cast<const FLDNetworkMoveData*>(GetCurrentNetworkMoveData());
        const bool Legal=Data&&(CharacterOwner->HasAuthority()?PC->Navigation->AuthorizesMove(Data->Navigation):Data->Navigation==PC->Navigation->PredictedIdentity());
        if(!Legal)
        {
            Applied=FVector::ZeroVector;
            if(!Accel.IsNearlyZero()){++PC->Navigation->RejectedMoves;}
            // Reject stale horizontal steering without resetting native gravity or committed root motion.
            if(IsFalling()){Velocity.X=Velocity.Y=0;}else if(!HasRootMotionSources()){StopMovementImmediately();}
        }
    }
    Super::MoveAutonomous(TimeStamp,Delta,Flags,Applied);
}

bool ULDCharacterMovementComponent::ForcePositionUpdate(float DeltaTime)
{
    // A late client is not a reason to integrate: only the match driver moves Demo bodies, so no update happened.
    if (CharacterOwner && CharacterOwner->HasAuthority() && LDCharacterMovementDetail::IsDemoMovement(GetWorld())) { return false; }
    return Super::ForcePositionUpdate(DeltaTime);
}

void ULDCharacterMovementComponent::PerformMovement(float DeltaSeconds)
{
    if (CharacterOwner && CharacterOwner->HasAuthority() && LDCharacterMovementDetail::IsDemoMovement(GetWorld()))
    {
        const auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
        if (!bAuthorityMovementRunning || bNativeIntegrationActive || !NativeWritePermit || !Mode
            || Mode->ValidateWritePermit(*NativeWritePermit,ELDCommitPhase::Motion)!=ELDCommitReject::None
            || Mode->ValidateMotionStep(Mode->GetAuthorityStep(),NativeBody)!=ELDCommitReject::None)
        {
            ++RejectedAuthorityMoves;
            UE_LOG(LogLDNav, Warning, TEXT("LD_DEMO_NATIVE_MOVE_REJECTED Native integration requires the current body step."));
            return;
        }
        TGuardValue<bool> NativeScope(bNativeIntegrationActive,true);
        const auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
        const bool CanSleep=bStationaryAuthorityInput && NativeBody.Victim.Kind==ELDVictimKind::CampMember && Registry
            && StationaryWorld==Registry->Revision() && StationaryBody==NativeBody;
        if (CanSleep && TryAdvanceStationaryMovement(DeltaSeconds)) { ++AuthorityStationarySteps; }
        else
        {
            Super::PerformMovement(DeltaSeconds);
            RememberStationarySupport();
            if (Registry) { StationaryWorld=Registry->Revision(); }
            StationaryBody=NativeBody;
        }
        ++AuthorityIntegrationCount;
        AuthorityIntegratedSeconds+=DeltaSeconds;
        return;
    }
    Super::PerformMovement(DeltaSeconds);
    RememberStationarySupport();
}

void ULDCharacterMovementComponent::RememberStationarySupport()
{
    StationaryFloor=CurrentFloor.HitResult.GetComponent();
    if (StationaryFloor.IsValid()) { StationaryFloorTransform=StationaryFloor->GetComponentTransform(); }
    if (CharacterOwner)
    {
        const auto* Capsule=CharacterOwner->GetCapsuleComponent();
        StationaryCapsuleSize=FVector(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight(),0);
    }
}

bool ULDCharacterMovementComponent::TryAdvanceStationaryMovement(float DeltaSeconds)
{
    const auto* Floor=StationaryFloor.Get();
    if (!HasValidData() || !UpdatedPrimitive || !Floor || MovementMode!=MOVE_Walking || !CurrentFloor.IsWalkableFloor()
        || CurrentFloor.HitResult.GetComponent()!=Floor || CurrentFloor.HitResult.bStartPenetrating
        || bForceNextFloorCheck || bJustTeleported || !Velocity.IsZero() || !Acceleration.IsZero()
        || !LastUpdateVelocity.IsZero() || !PendingImpulseToApply.IsZero() || !PendingForceToApply.IsZero()
        || !PendingLaunchVelocity.IsZero() || bHasRequestedVelocity || bForceMaxAccel
        || HasRootMotionSources() || HasAnimRootMotion() || CharacterOwner->IsPlayingRootMotion()
        || bWantsToCrouch || CharacterOwner->bIsCrouched || CharacterOwner->bPressedJump
        || bOrientRotationToMovement || bUseControllerDesiredRotation
        || CharacterOwner->OnCharacterMovementUpdated.IsBound()
        || !UpdatedComponent->IsQueryCollisionEnabled() || UpdatedComponent->IsSimulatingPhysics()
        || Floor->Mobility!=EComponentMobility::Static || !Floor->IsQueryCollisionEnabled()
        || Floor->GetCollisionResponseToChannel(UpdatedComponent->GetCollisionObjectType())!=ECR_Block
        || !Floor->GetComponentTransform().Equals(StationaryFloorTransform,0)
        || UpdatedComponent->GetComponentLocation()!=LastUpdateLocation
        || UpdatedComponent->GetComponentQuat()!=LastUpdateRotation)
    { return false; }
    const auto* Base=GetMovementBaseInterfaceData();
    const auto* Capsule=CharacterOwner->GetCapsuleComponent();
    const double FloorDistance=CurrentFloor.bLineTrace?CurrentFloor.LineDist:CurrentFloor.FloorDist;
    if (!Base || !Base->IsValid() || MovementBaseUtility::IsDynamicBase(Base)
        || Base->GetBodyInstanceOwner()!=Floor
        || StationaryCapsuleSize!=FVector(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight(),0)
        || !FMath::IsFinite(FloorDistance) || FloorDistance<MIN_FLOOR_DIST || FloorDistance>MAX_FLOOR_DIST)
    { return false; }
    // Another body can finish a dash inside this capsule without setting this CMC's floor/teleport flags.
    FCollisionQueryParams Query(SCENE_QUERY_STAT(LDStationaryPenetration),false,CharacterOwner);
    FCollisionResponseParams Response;
    UpdatedPrimitive->InitSweepCollisionParams(Query,Response);
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(LDStationaryPenetration);
        if (GetWorld()->OverlapBlockingTestByChannel(LastUpdateLocation,LastUpdateRotation,
            UpdatedPrimitive->GetCollisionObjectType(),UpdatedPrimitive->GetCollisionShape(),Query,Response))
        { return false; }
    }
    // ControlledCharacterMove already processed jump/input. Stable static support needs no physics or floor sweep.
    bTeleportedSinceLastUpdate=false;
    CharacterOwner->ClearJumpInput(DeltaSeconds);
    NumJumpApexAttempts=0;
    LastUpdateRequestedVelocity=FVector::ZeroVector;
    OnMovementUpdated(DeltaSeconds,LastUpdateLocation,LastUpdateVelocity);
    CallMovementUpdateDelegate(DeltaSeconds,LastUpdateLocation,LastUpdateVelocity);
    return true;
}
// Demo speed is the sole Combat projection: the authority reads it for every registered body (contenders and
// camp members), the owning client predicts from its own owner view; nothing else may move voluntarily.
float ULDCharacterMovementComponent::GetMaxSpeed() const
{
    if (!LDCharacterMovementDetail::IsDemoMovement(GetWorld())) { return Super::GetMaxSpeed(); }
    const auto* Body=Cast<ALDCharacter>(CharacterOwner);
    if (!Body) { return 0; }
    if (Body->HasAuthority())
    {
        const auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
        float Speed=0;
        return Mode && Mode->Combat && !Mode->Combat->IsMoveBlocked(*Body) && Mode->Combat->ReadMoveSpeed(*Body,Speed)
            && FMath::IsFinite(Speed) && Speed>0 ? Speed : 0;
    }
    const auto* PC=Cast<ALDPlayerController>(Body->GetController());
    if (!PC || !PC->OwnerState.BodyRef.IsValid() || !PC->OwnerState.bActive) { return 0; }
    const FLDCombatOwnerView& View=PC->CombatView;
    return View.Generation==PC->OwnerState.Match && View.Life==PC->OwnerState.Life && View.bHasBody && !View.bRooted
        && FMath::IsFinite(View.MoveSpeedCmPerSec) && View.MoveSpeedCmPerSec>0 ? View.MoveSpeedCmPerSec : 0;
}

bool ULDCharacterMovementComponent::StartDemoDash(const FVector& Direction, double DistanceCm, double DurationSeconds, FString& Error,
    bool bKnockback)
{
    bKnockbackMotion=bKnockback;
    StopKnockbackWatch();
    if (!CharacterOwner || !CharacterOwner->HasAuthority() || !UpdatedComponent
        || !LDCharacterMovementDetail::IsDemoMovement(GetWorld()))
    {
        Error=TEXT("A Demo dash runs only on the server authority body.");
        return false;
    }
    return ApplyDashRootMotion(Direction,DistanceCm,DurationSeconds,Error);
}

bool ULDCharacterMovementComponent::ApplyDashRootMotion(const FVector& Direction, double DistanceCm, double DurationSeconds, FString& Error)
{
    const FVector Planar(Direction.X,Direction.Y,0);
    const double Speed=DistanceCm/DurationSeconds;
    if (!CharacterOwner || !UpdatedComponent || Direction.ContainsNaN() || FMath::Abs(Direction.Z)>UE_KINDA_SMALL_NUMBER
        || Planar.IsNearlyZero() || !FMath::IsFinite(DistanceCm) || DistanceCm<=0 || !FMath::IsFinite(DurationSeconds)
        || DurationSeconds<=0 || DurationSeconds>MAX_flt || !FMath::IsFinite(Speed) || Speed>MAX_flt)
    {
        Error=TEXT("A Demo dash needs a finite non-zero planar direction, distance and duration.");
        return false;
    }
    // Displacement is native root motion integrated by the issued authority steps; never a teleport.
    auto Source=MakeShared<FRootMotionSource_ConstantForce>();
    Source->InstanceName=LDCharacterMovementDetail::DashSourceName;
    Source->AccumulateMode=ERootMotionAccumulateMode::Override;
    Source->Priority=5;
    Source->Force=Planar.GetSafeNormal()*Speed;
    Source->Duration=float(DurationSeconds);
    Source->StrengthOverTime=nullptr;
    Source->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
    Source->FinishVelocityParams.Mode=ERootMotionFinishVelocityMode::SetVelocity;
    Source->FinishVelocityParams.SetVelocity=FVector::ZeroVector;
    // The driver integrates authority motion itself: start with the next issued step, not a client timestamp.
    Source->StartTime=0.f;
    RemoveRootMotionSource(LDCharacterMovementDetail::DashSourceName);
    if (ApplyRootMotionSource(Source)==uint16(ERootMotionSourceID::Invalid))
    {
        Error=TEXT("The native movement component refused the dash root motion.");
        return false;
    }
    Error.Reset();
    return true;
}

bool ULDCharacterMovementComponent::IsDashing() const
{
    return const_cast<ULDCharacterMovementComponent*>(this)->GetRootMotionSource(LDCharacterMovementDetail::DashSourceName).IsValid();
}

bool ULDCharacterMovementComponent::CanStartDash(const FVector& Direction, double ProbeCm) const
{
    const FVector Planar(Direction.X,Direction.Y,0);
    if (!CharacterOwner || !UpdatedPrimitive || !GetWorld() || Direction.ContainsNaN()
        || FMath::Abs(Direction.Z)>UE_KINDA_SMALL_NUMBER || Planar.IsNearlyZero() || !FMath::IsFinite(ProbeCm) || ProbeCm<=0)
    { return false; }
    const FVector Heading=Planar.GetSafeNormal();
    const FVector Start=UpdatedComponent->GetComponentLocation();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(LDDemoDashProbe),false,CharacterOwner);
    FCollisionResponseParams Response;
    UpdatedPrimitive->InitSweepCollisionParams(Query,Response);
    // Static geometry only: other bodies never decide whether a dash may start.
    Response.CollisionResponse.SetResponse(ECC_Pawn,ECR_Ignore);
    FHitResult Hit;
    if (!GetWorld()->SweepSingleByChannel(Hit,Start,Start+Heading*ProbeCm,UpdatedComponent->GetComponentQuat(),
        UpdatedPrimitive->GetCollisionObjectType(),UpdatedPrimitive->GetCollisionShape(),Query,Response)) { return true; }
    // Walkable support and grazing contacts let the native movement slide; only a surface facing the dash blocks it.
    return IsWalkable(Hit) || FVector::DotProduct(FVector(Hit.Normal.X,Hit.Normal.Y,0).GetSafeNormal(),Heading)>-.1;
}

FNetworkPredictionData_Client* ULDCharacterMovementComponent::GetPredictionData_Client() const
{
    if (!ClientPredictionData)
    {
        const_cast<ULDCharacterMovementComponent*>(this)->ClientPredictionData = new LDCharacterMovementDetail::FLDClientPrediction(*this);
    }
    return ClientPredictionData;
}

void ULDCharacterMovementComponent::ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData)
{
    if (CharacterOwner && CharacterOwner->HasAuthority() && LDCharacterMovementDetail::IsDemoMovement(GetWorld()))
    {
        const auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
        const auto* Body=Cast<ALDCharacter>(CharacterOwner);
        FLDCombatBodyRef Ref;
        const auto& Data=static_cast<const FLDNetworkMoveData&>(MoveData);
        if (!Mode || !Body || !Mode->ReadBodyRef(*Body,Ref)
            || Data.Navigation.Match!=Mode->GetMatchContext().Generation || Data.Navigation.Life!=Ref.Victim.Life)
        {
            ++RejectedAuthorityMoves;
            UE_LOG(LogLDNav, Verbose, TEXT("LD_DEMO_NETWORK_MOVE_REJECTED Stale body identity."));
            return;
        }
        ++NetworkBookkeepingMoves;
    }
    // Keep native timestamp validation, ACKs and corrections together. Demo's
    // MoveAutonomous cannot perform a second integration outside the driver.
    Super::ServerMove_PerformMovement(MoveData);
}

bool ULDCharacterMovementComponent::ServerShouldUseAuthoritativePosition(float ClientTimeStamp, float DeltaTime,
    const FVector& Accel, const FVector& ClientWorldLocation, const FVector& RelativeClientLocation,
    FMovementBaseInterfaceData* ClientBase, FName ClientBaseBoneName, uint8 ClientMovementMode)
{
    if (LDCharacterMovementDetail::IsDemoMovement(GetWorld())) { return false; }
    return Super::ServerShouldUseAuthoritativePosition(ClientTimeStamp,DeltaTime,Accel,ClientWorldLocation,
        RelativeClientLocation,ClientBase,ClientBaseBoneName,ClientMovementMode);
}

bool ULDCharacterMovementComponent::ServerCheckClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel,
    const FVector& ClientWorldLocation, const FVector& RelativeClientLocation,
    FMovementBaseInterfaceData* ClientBase, FName ClientBaseBoneName, uint8 ClientMovementMode)
{
    const bool bError=Super::ServerCheckClientError(ClientTimeStamp,DeltaTime,Accel,ClientWorldLocation,RelativeClientLocation,
        ClientBase,ClientBaseBoneName,ClientMovementMode);
    if (bError && ServerCorrectionCount<MAX_uint32) { ++ServerCorrectionCount; }
    return bError;
}

bool ULDCharacterMovementComponent::AdvanceAuthorityMovement(const FLDAuthorityStep& Step, const FLDCombatBodyRef& Body,
    const FVector& Input, const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    ALDCharacter* Current=nullptr;
    const double Delta=Step.EndServerSeconds-Step.StartServerSeconds;
    if (!Mode || !CharacterOwner || !CharacterOwner->HasAuthority() || IsComponentTickEnabled()
        || bAuthorityMovementRunning || NativeWritePermit || Input.ContainsNaN() || Input.SizeSquared()>1.+UE_SMALL_NUMBER
        || !FMath::IsFinite(Delta) || Delta<=0 || Delta>MAX_flt
        || Mode->ValidateWritePermit(Permit,ELDCommitPhase::Motion)!=ELDCommitReject::None
        || Mode->ValidateMotionStep(Step,Body)!=ELDCommitReject::None
        || Mode->ResolveBody(Step.Match,Body,Current)!=ELDCommitReject::None || Current!=CharacterOwner)
    {
        Error=TEXT("Native movement rejected a stale, duplicate, tick-enabled or unpermitted body step.");
        return false;
    }
    if (AuthorityIntegrationCount==MAX_uint64)
    { Error=TEXT("Native integration counter exhausted."); return false; }
    TGuardValue<bool> Running(bAuthorityMovementRunning,true);
    TGuardValue<bool> StationaryInput(bStationaryAuthorityInput,Input.IsZero());
    TGuardValue<const FLDGameplayWritePermit*> Scope(NativeWritePermit,&Permit);
    TGuardValue<FLDCombatBodyRef> Binding(NativeBody,Body);
    const uint64 Before=AuthorityIntegrationCount;
    CharacterOwner->ConsumeMovementInputVector();
    if (!Input.IsNearlyZero()) { CharacterOwner->SetActorRotation(FRotator(0,Input.Rotation().Yaw,0)); }
    FGuid Stream;
    const auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
    if (!Registry || !Mode->ReadBodyMotionStream(Step.Match,Body,Stream,Error)
        || !BeginNativeMotionCapture(Step,Body,Stream,Registry->Revision(),Error))
    { return false; }
    ControlledCharacterMove(Input,float(Delta));
    TSharedPtr<const FLDNativeMotionBatch> Batch;
    if (!FinishNativeMotionCapture(Batch,Error)) { return false; }
    if (AuthorityIntegrationCount!=Before+1 || CharacterOwner->GetActorLocation().ContainsNaN()
        || Mode->CompleteNativeMotion(Step,Body,Permit,MoveTemp(Batch))!=ELDCommitReject::None)
    {
        Error=TEXT("Native movement did not commit exactly once for its issued body step.");
        return false;
    }
    Error.Reset();
    return true;
}
