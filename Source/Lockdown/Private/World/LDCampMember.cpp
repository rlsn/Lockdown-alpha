#include "World/LDCampMember.h"
#include "Development/LDDemoContentCatalog.h"
#include "GameFramework/CharacterMovementComponent.h"

ALDCampMemberCharacter::ALDCampMemberCharacter(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    bReplicates = false;
}

bool ALDCampMemberCharacter::PrepareMonster(const FLDDemoContentSnapshot& Content, const FLDDemoMonsterDefinition& Monster, FString& Error)
{
    const FLDDemoRational& Speed = Monster.MoveSpeedCmPerSec;
    const FLDDemoRational& Radius = Monster.CollisionRadiusCm;
    const FLDDemoRational& Shared = Content.Get().Parameters.CollisionRadiusCm;
    const double NativeSpeed = Speed.Denominator > 0 ? double(Speed.Numerator) / double(Speed.Denominator) : 0;
    const double NativeRadius = Radius.Denominator > 0 ? double(Radius.Numerator) / double(Radius.Denominator) : 0;
    const double SharedRadius = Shared.Denominator > 0 ? double(Shared.Numerator) / double(Shared.Denominator) : -1;
    if (HasActorBegunPlay() || Monster.Id.IsEmpty() || !FMath::IsFinite(NativeSpeed) || NativeSpeed <= 0 || NativeSpeed > MAX_flt
        || !FMath::IsFinite(NativeRadius) || NativeRadius <= 0 || NativeRadius != SharedRadius)
    {
        Error = FString::Printf(TEXT("Camp body %s needs a positive native speed and the shared navigation radius before spawning."), *Monster.Id);
        return false;
    }
    MonsterId = FName(*Monster.Id);
    MonsterSpeed = float(NativeSpeed);
    Error.Reset();
    return true;
}

void ALDCampMemberCharacter::BeginPlay()
{
    Super::BeginPlay();
    if (!IsValid(this) || IsActorBeingDestroyed()) { return; }
    if (MonsterId.IsNone() || MonsterSpeed <= 0)
    {
        UE_LOG(LogTemp, Error, TEXT("LD_CAMP_BODY_REJECTED Camp body began play without an approved monster definition."));
        Destroy();
        return;
    }
    // The shared body applied the contender walking speed; camp members walk at their own approved speed.
    GetCharacterMovement()->MaxWalkSpeed = MonsterSpeed;
}
