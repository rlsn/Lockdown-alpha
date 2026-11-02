#include "Character/LDCharacterMovementComponent.h"
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

namespace LDCharacterMovementDetail
{
    bool IsDemoMovement(const UWorld* World)
    { return World && UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())==LDDemoEntry::WorldMap; }

    const FName DashSourceName(TEXT("LDDemoDash"));
}

void ULDCharacterMovementComponent::OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData,float TimeStamp,FVector NewLocation,FVector NewVelocity,FMovementBaseInterfaceData* NewBase,FName NewBaseBoneName,bool bHasBase,bool bBaseRelativePosition,uint8 ServerMovementMode,FVector ServerGravityDirection)
{
    ++CorrectionCount;if(!bBaseRelativePosition&&UpdatedComponent){LastCorrectionCm=FVector::Distance(UpdatedComponent->GetComponentLocation(),NewLocation);MaxCorrectionCm=FMath::Max(MaxCorrectionCm,LastCorrectionCm);}
    Super::OnClientCorrectionReceived(ClientData,TimeStamp,NewLocation,NewVelocity,NewBase,NewBaseBoneName,bHasBase,bBaseRelativePosition,ServerMovementMode,ServerGravityDirection);
}

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
            if(const auto* PC=Cast<ALDPlayerController>(Character->GetController())){Navigation=PC->Navigation->LocalIdentity();}
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
        const bool Legal=Data&&(CharacterOwner->HasAuthority()?PC->Navigation->AuthorizesMove(Data->Navigation):Data->Navigation==PC->Navigation->LocalIdentity());
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
            UE_LOG(LogTemp, Warning, TEXT("LD_DEMO_NATIVE_MOVE_REJECTED Native integration requires the current body step."));
            return;
        }
        TGuardValue<bool> NativeScope(bNativeIntegrationActive,true);
        Super::PerformMovement(DeltaSeconds);
        ++AuthorityIntegrationCount;
        AuthorityIntegratedSeconds+=DeltaSeconds;
        return;
    }
    Super::PerformMovement(DeltaSeconds);
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
        return Mode && Mode->Combat && !Mode->Combat->IsRooted(*Body) && Mode->Combat->ReadMoveSpeed(*Body,Speed)
            && FMath::IsFinite(Speed) && Speed>0 ? Speed : 0;
    }
    const auto* PC=Cast<ALDPlayerController>(Body->GetController());
    if (!PC || !PC->OwnerState.BodyRef.IsValid() || !PC->OwnerState.bActive) { return 0; }
    const FLDCombatOwnerView& View=PC->CombatView;
    return View.Generation==PC->OwnerState.Match && View.Life==PC->OwnerState.Life && View.bHasBody && !View.bRooted
        && FMath::IsFinite(View.MoveSpeedCmPerSec) && View.MoveSpeedCmPerSec>0 ? View.MoveSpeedCmPerSec : 0;
}

bool ULDCharacterMovementComponent::StartDemoDash(const FVector& Direction, double DistanceCm, double DurationSeconds, FString& Error)
{
    const FVector Planar(Direction.X,Direction.Y,0);
    const double Speed=DistanceCm/DurationSeconds;
    if (!CharacterOwner || !CharacterOwner->HasAuthority() || !UpdatedComponent
        || !LDCharacterMovementDetail::IsDemoMovement(GetWorld()))
    {
        Error=TEXT("A Demo dash runs only on the server authority body.");
        return false;
    }
    if (Direction.ContainsNaN() || FMath::Abs(Direction.Z)>UE_KINDA_SMALL_NUMBER || Planar.IsNearlyZero()
        || !FMath::IsFinite(DistanceCm) || DistanceCm<=0 || !FMath::IsFinite(DurationSeconds) || DurationSeconds<=0
        || DurationSeconds>MAX_flt || !FMath::IsFinite(Speed) || Speed>MAX_flt)
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
            UE_LOG(LogTemp, Verbose, TEXT("LD_DEMO_NETWORK_MOVE_REJECTED Stale body identity."));
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
