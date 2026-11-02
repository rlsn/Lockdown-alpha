#include "Character/LDCharacterMovementComponent.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "GameFramework/Character.h"
#include "GameFramework/RootMotionSource.h"
#include "Components/SceneComponent.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Combat/LDCombatDirector.h"
#include "World/LDEnvironmentDirector.h"
#include "Character/LDCharacter.h"
#include "Engine/World.h"
#include "Region/LDMapSurfaceSubsystem.h"
#include "Framework/LDDemoEntry.h"
#include "Region/LDMapRegistry.h"

namespace
{
    bool IsDemoMovement(const UWorld* World)
    { return World && UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())==LDDemoEntry::WorldMap; }
}

void ULDCharacterMovementComponent::OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData,float TimeStamp,FVector NewLocation,FVector NewVelocity,FMovementBaseInterfaceData* NewBase,FName NewBaseBoneName,bool bHasBase,bool bBaseRelativePosition,uint8 ServerMovementMode,FVector ServerGravityDirection)
{
    ++CorrectionCount;if(!bBaseRelativePosition&&UpdatedComponent){LastCorrectionCm=FVector::Distance(UpdatedComponent->GetComponentLocation(),NewLocation);MaxCorrectionCm=FMath::Max(MaxCorrectionCm,LastCorrectionCm);}
    Super::OnClientCorrectionReceived(ClientData,TimeStamp,NewLocation,NewVelocity,NewBase,NewBaseBoneName,bHasBase,bBaseRelativePosition,ServerMovementMode,ServerGravityDirection);
}

namespace
{
    class FLDSavedMove : public FSavedMove_Character
    {
    public:
        using Super = FSavedMove_Character;
        bool bSavedSprint = false;
        FLDMoveIdentity Navigation;
        virtual void Clear() override { Super::Clear(); bSavedSprint = false;Navigation={}; }
        virtual uint8 GetCompressedFlags() const override
        {
            return Super::GetCompressedFlags() | (bSavedSprint ? FLAG_Custom_0 : 0);
        }
        virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
        {
            return bSavedSprint == static_cast<const FLDSavedMove*>(NewMove.Get())->bSavedSprint
                &&Navigation==static_cast<const FLDSavedMove*>(NewMove.Get())->Navigation
                && Super::CanCombineWith(NewMove, Character, MaxDelta);
        }
        virtual void SetMoveFor(ACharacter* Character, float Delta, const FVector& Accel, FNetworkPredictionData_Client_Character& Data) override
        {
            Super::SetMoveFor(Character, Delta, Accel, Data);
            bSavedSprint = CastChecked<ULDCharacterMovementComponent>(Character->GetCharacterMovement())->bWantsSprint;
            if(const auto* PC=Cast<ALDPlayerController>(Character->GetController())){Navigation=PC->Navigation->LocalIdentity();}
        }
        virtual void PrepMoveFor(ACharacter* Character) override
        {
            Super::PrepMoveFor(Character);
            CastChecked<ULDCharacterMovementComponent>(Character->GetCharacterMovement())->bWantsSprint = bSavedSprint;
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
{FCharacterNetworkMoveData::ClientFillNetworkMoveData(Move,Type);Navigation=static_cast<const FLDSavedMove&>(Move).Navigation;}
bool FLDNetworkMoveData::Serialize(UCharacterMovementComponent& Movement,FArchive& Ar,UPackageMap* Map,ENetworkMoveType Type)
{
    const bool Valid=FCharacterNetworkMoveData::Serialize(Movement,Ar,Map,Type);
    Ar.SerializeIntPacked(Navigation.Match);Ar.SerializeIntPacked(Navigation.Life);
    Ar.SerializeIntPacked(Navigation.Order);Ar.SerializeIntPacked(Navigation.Epoch);
    return Valid&&!Ar.IsError();
}
void ULDCharacterMovementComponent::MoveAutonomous(float TimeStamp,float Delta,uint8 Flags,const FVector& Accel)
{
    if (CharacterOwner && CharacterOwner->HasAuthority() && IsDemoMovement(GetWorld()))
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
        if(CharacterOwner->HasAuthority()&&!Applied.IsNearlyZero()&&(PC->OwnerState.ActionCue&15)==0)
        {CharacterOwner->SetActorRotation(FRotator(0,Applied.Rotation().Yaw,0));}
    }
    Super::MoveAutonomous(TimeStamp,Delta,Flags,Applied);
}

void ULDCharacterMovementComponent::PerformMovement(float DeltaSeconds)
{
    if (CharacterOwner && CharacterOwner->HasAuthority() && IsDemoMovement(GetWorld()))
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
    if(CharacterOwner&&CharacterOwner->HasAuthority()){if(auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M&&M->IsV08Profile()&&M->Combat){M->Combat->FlushPastCommand();}}
    Super::PerformMovement(DeltaSeconds);
}
void ULDCharacterMovementComponent::OnMovementUpdated(float DeltaSeconds,const FVector& OldLocation,const FVector& OldVelocity)
{
    Super::OnMovementUpdated(DeltaSeconds,OldLocation,OldVelocity);
    if (IsDemoMovement(GetWorld())) { return; }
    if(auto* Surface=GetWorld()->GetSubsystem<ULDMapSurfaceSubsystem>();Surface&&Surface->Enabled())
    { Surface->Refresh(Cast<ALDCharacter>(CharacterOwner),LDTactical::Milliseconds(GetWorld()->GetTimeSeconds())); }
    if(CharacterOwner&&CharacterOwner->HasAuthority()){if(auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M&&M->IsV08Profile()&&M->Combat){M->Combat->RefreshRiverContact(Cast<ALDCharacter>(CharacterOwner),LDTactical::Milliseconds(GetWorld()->GetTimeSeconds()));}}
}
float ULDCharacterMovementComponent::GetMaxSpeed() const
{
    if (IsDemoMovement(GetWorld()))
    {
        const auto* PC=CharacterOwner?Cast<ALDPlayerController>(CharacterOwner->GetController()):nullptr;
        return PC && PC->OwnerState.BodyRef.IsValid() && PC->OwnerState.bActive ? MaxWalkSpeed : 0;
    }
    if(GetWorld()) { if(const auto* Surface=GetWorld()->GetSubsystem<ULDMapSurfaceSubsystem>();Surface&&Surface->Enabled())
    { const auto* C=Surface->Contact(Cast<ALDCharacter>(CharacterOwner));
      if(const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M&&M->IsPopulationProfile()&&CharacterOwner&&!Cast<ALDPlayerController>(CharacterOwner->GetController()))
      {auto* B=Cast<ALDCharacter>(CharacterOwner);const auto* S=M->Combat?M->Combat->StateFor(B):nullptr;if(!S||!M->Combat->Allows(B,LDTactical::EAction::Move)){return 0;}const int64 At=LDTactical::Milliseconds(GetWorld()->GetTimeSeconds());int32 Slow=C?C->State.SurfaceSlow:0;for(const auto& T:S->Tactical.Statuses){if(T.Kind==LDTactical::EStatus::Slow&&T.Until>At){Slow=FMath::Max(Slow,T.Magnitude);}}return Super::GetMaxSpeed()*FMath::Min(S->Tactical.Has(LDTactical::EStatus::Hex,At)?.5f:1.f,1-FMath::Clamp(Slow,0,60)/100.f);}
      return Super::GetMaxSpeed()*(1-(C?C->State.SurfaceSlow:0)/100.f); } }
    // Ordinary native NPCs use the same root/stun/hex movement contract.
    if(const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M&&M->IsTacticalProfile()&&CharacterOwner&&!Cast<ALDPlayerController>(CharacterOwner->GetController()))
    {if(const auto* S=M->Combat?M->Combat->StateFor(Cast<ALDCharacter>(CharacterOwner)):nullptr){const uint16 Tags=S->Tactical.Mask(LDTactical::Milliseconds(GetWorld()->GetTimeSeconds()));if(Tags&((1<<0)|(1<<3))){return 0;}int32 Slow=S->Tactical.SurfaceSlow;for(const auto& T:S->Tactical.Statuses){if(T.Kind==LDTactical::EStatus::Slow&&T.Until>LDTactical::Milliseconds(GetWorld()->GetTimeSeconds())){Slow=FMath::Max(Slow,T.Magnitude);}}return Super::GetMaxSpeed()*FMath::Min((Tags&(1<<5))?.5f:1.f,1-FMath::Clamp(Slow,0,60)/100.f);}}
    if (const auto* PC=CharacterOwner?Cast<ALDPlayerController>(CharacterOwner->GetController()):nullptr;PC&&PC->OwnerState.bDevelopmentProfile)
    {
        const auto* Public=GetWorld()->GetGameState<ALDGameState>();
        if (!Public || !Public->bMatchStarted || Public->EndReason || !PC->OwnerState.bActive) { return 0; }
        if(PC->OwnerState.Build.RulesVersion>=7)
        {
            const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* S=M&&M->Combat?M->Combat->StateFor(Cast<ALDCharacter>(CharacterOwner)):nullptr;
            const uint16 Tags=S?S->Tactical.Mask(LDTactical::Milliseconds(GetWorld()->GetTimeSeconds())):PC->OwnerState.StatusMask;if(Tags&((1<<0)|(1<<3))){return 0;}
            int32 ContactSlow=PC->OwnerState.SlowPercent;if(S){ContactSlow=S->Tactical.SurfaceSlow;for(const auto& T:S->Tactical.Statuses){if(T.Kind==LDTactical::EStatus::Slow&&T.Until>LDTactical::Milliseconds(GetWorld()->GetTimeSeconds())){ContactSlow=FMath::Max(ContactSlow,T.Magnitude);}}}
            const auto* EnvironmentPC=Cast<ALDB91Controller>(PC);const float CargoFactor=EnvironmentPC&&EnvironmentPC->HasObjectCargo()?.7f:1.f;
            const auto& Gear=PC->OwnerState.Build.Gear;const int32 Positive=FMath::Clamp(FMath::Max(0,Gear.Movement)+(LDGear::OnRoad(GetWorld(),CharacterOwner->GetActorLocation())?Gear.RoadMovement:0),0,20);return CargoFactor*420.f*(1+(FMath::Min(0,Gear.Movement)+Positive)/100.f)*FMath::Min((Tags&(1<<5))?.5f:1.f,1-ContactSlow/100.f);
        }
        return float((IsMovingOnGround()&&bWantsSprint&&!bCombatWalkOnly?300.:180.)*LDDevelopment::SpeedFactor(PC->OwnerState.Build));
    }
    return IsMovingOnGround() && bWantsSprint && !bCombatWalkOnly ? SprintSpeed : Super::GetMaxSpeed();
}

bool ULDCharacterMovementComponent::StartAuthorityDash(const FVector& Direction,double Distance)
{
    if (IsDemoMovement(GetWorld()))
    { UE_LOG(LogTemp, Warning, TEXT("LD_DEMO_DASH_REJECTED Legacy root-motion entry has no Demo release permit.")); return false; }
    if(auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M&&M->Combat&&!M->Combat->Allows(Cast<ALDCharacter>(CharacterOwner),LDTactical::EAction::Dash)){return false;}
    if (!CharacterOwner || !CharacterOwner->HasAuthority() || !IsMovingOnGround() || Direction.ContainsNaN() || Direction.SizeSquared2D()<UE_SMALL_NUMBER || !FMath::IsFinite(Distance) || Distance<300 || Distance>420) { return false; }
    TSharedPtr<FRootMotionSource_ConstantForce> Dash=MakeShared<FRootMotionSource_ConstantForce>();
    Dash->InstanceName=TEXT("LD_AuthorityDash"); Dash->Priority=500;
    Dash->AccumulateMode=ERootMotionAccumulateMode::Override; Dash->bInLocalSpace=false;
    Dash->Force=Direction.GetSafeNormal2D()*float(Distance/.2); Dash->Duration=.2f;
    // A distance-bounded dash needs only the remaining fraction of its last movement tick.
    Dash->Settings.UnSetFlag(ERootMotionSourceSettingsFlags::DisablePartialEndTick);
    Dash->FinishVelocityParams.Mode=ERootMotionFinishVelocityMode::SetVelocity;
    Dash->FinishVelocityParams.SetVelocity=FVector::ZeroVector;
    return ApplyRootMotionSource(Dash)!=uint16(ERootMotionSourceID::Invalid);
}

void ULDCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
    Super::UpdateFromCompressedFlags(Flags);
    bWantsSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
}

FNetworkPredictionData_Client* ULDCharacterMovementComponent::GetPredictionData_Client() const
{
    if (!ClientPredictionData)
    {
        const_cast<ULDCharacterMovementComponent*>(this)->ClientPredictionData = new FLDClientPrediction(*this);
    }
    return ClientPredictionData;
}

bool ULDCharacterMovementComponent::StartContentDash(const FVector& Direction,double Distance,double Duration)
{
    if (IsDemoMovement(GetWorld()))
    { UE_LOG(LogTemp, Warning, TEXT("LD_DEMO_DASH_REJECTED Legacy content dash has no Demo release permit.")); return false; }
    if(!CharacterOwner||!CharacterOwner->HasAuthority()||!IsMovingOnGround()||Distance<=0||Distance>450||Duration!=.3||Direction.ContainsNaN()){return false;}TSharedPtr<FRootMotionSource_ConstantForce> Dash=MakeShared<FRootMotionSource_ConstantForce>();Dash->InstanceName=TEXT("LD_AuthorityDash");Dash->Priority=500;Dash->AccumulateMode=ERootMotionAccumulateMode::Override;Dash->Force=Direction.GetSafeNormal2D()*(Distance/Duration);Dash->Duration=Duration;Dash->Settings.UnSetFlag(ERootMotionSourceSettingsFlags::DisablePartialEndTick);Dash->FinishVelocityParams.Mode=ERootMotionFinishVelocityMode::SetVelocity;Dash->FinishVelocityParams.SetVelocity=FVector::ZeroVector;bCanWalkOffLedges=false;return ApplyRootMotionSource(Dash)!=uint16(ERootMotionSourceID::Invalid);
}

void ULDCharacterMovementComponent::ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData)
{
    if (CharacterOwner && CharacterOwner->HasAuthority() && IsDemoMovement(GetWorld()))
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
    if (IsDemoMovement(GetWorld())) { return false; }
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
