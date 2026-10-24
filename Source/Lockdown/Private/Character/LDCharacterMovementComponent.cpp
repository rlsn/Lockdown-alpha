#include "Character/LDCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/RootMotionSource.h"
#include "Components/SceneComponent.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Engine/World.h"

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

float ULDCharacterMovementComponent::GetMaxSpeed() const
{
    if (const auto* PC=CharacterOwner?Cast<ALDPlayerController>(CharacterOwner->GetController()):nullptr;PC&&PC->OwnerState.bDevelopmentProfile)
    {
        const auto* Public=GetWorld()->GetGameState<ALDGameState>();
        if (!Public || !Public->bMatchStarted || Public->EndReason || !PC->OwnerState.bActive) { return 0; }
        if(PC->OwnerState.Build.RulesVersion==7)
        {
            const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* S=M&&M->Combat?M->Combat->StateFor(Cast<ALDCharacter>(CharacterOwner)):nullptr;
            const uint16 Tags=S?S->Tactical.Mask(LDTactical::Milliseconds(GetWorld()->GetTimeSeconds())):PC->OwnerState.StatusMask;if(Tags&((1<<0)|(1<<3))){return 0;}
            return 420.f*((Tags&(1<<5))?.5f:1.f);
        }
        return float((IsMovingOnGround()&&bWantsSprint&&!bCombatWalkOnly?300.:180.)*LDDevelopment::SpeedFactor(PC->OwnerState.Build));
    }
    return IsMovingOnGround() && bWantsSprint && !bCombatWalkOnly ? SprintSpeed : Super::GetMaxSpeed();
}

bool ULDCharacterMovementComponent::StartAuthorityDash(const FVector& Direction,double Distance)
{
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
