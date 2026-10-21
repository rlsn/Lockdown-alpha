#include "Character/LDCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/RootMotionSource.h"
#include "Components/SceneComponent.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Engine/World.h"

void ULDCharacterMovementComponent::OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData,float TimeStamp,FVector NewLocation,FVector NewVelocity,FMovementBaseInterfaceData* NewBase,FName NewBaseBoneName,bool bHasBase,bool bBaseRelativePosition,uint8 ServerMovementMode,FVector ServerGravityDirection)
{
    ++CorrectionCount;if(!bBaseRelativePosition&&UpdatedComponent){MaxCorrectionCm=FMath::Max(MaxCorrectionCm,FVector::Distance(UpdatedComponent->GetComponentLocation(),NewLocation));}
    Super::OnClientCorrectionReceived(ClientData,TimeStamp,NewLocation,NewVelocity,NewBase,NewBaseBoneName,bHasBase,bBaseRelativePosition,ServerMovementMode,ServerGravityDirection);
}

namespace
{
    class FLDSavedMove : public FSavedMove_Character
    {
    public:
        using Super = FSavedMove_Character;
        bool bSavedSprint = false;
        virtual void Clear() override { Super::Clear(); bSavedSprint = false; }
        virtual uint8 GetCompressedFlags() const override
        {
            return Super::GetCompressedFlags() | (bSavedSprint ? FLAG_Custom_0 : 0);
        }
        virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
        {
            return bSavedSprint == static_cast<const FLDSavedMove*>(NewMove.Get())->bSavedSprint
                && Super::CanCombineWith(NewMove, Character, MaxDelta);
        }
        virtual void SetMoveFor(ACharacter* Character, float Delta, const FVector& Accel, FNetworkPredictionData_Client_Character& Data) override
        {
            Super::SetMoveFor(Character, Delta, Accel, Data);
            bSavedSprint = CastChecked<ULDCharacterMovementComponent>(Character->GetCharacterMovement())->bWantsSprint;
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

float ULDCharacterMovementComponent::GetMaxSpeed() const
{
    if (const auto* PC=CharacterOwner?Cast<ALDPlayerController>(CharacterOwner->GetController()):nullptr;PC&&PC->OwnerState.bDevelopmentProfile)
    {
        const auto* Public=GetWorld()->GetGameState<ALDGameState>();
        if (!Public || !Public->bMatchStarted || Public->EndReason || !PC->OwnerState.bActive) { return 0; }
        return float((IsMovingOnGround()&&bWantsSprint&&!bCombatWalkOnly?300.:180.)*LDDevelopment::SpeedFactor(PC->OwnerState.Build));
    }
    return IsMovingOnGround() && bWantsSprint && !bCombatWalkOnly ? SprintSpeed : Super::GetMaxSpeed();
}

bool ULDCharacterMovementComponent::StartAuthorityDash(const FVector& Direction,double Distance)
{
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
