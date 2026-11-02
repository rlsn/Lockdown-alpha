#include "Region/LDMapRegistry.h"
#include "Region/LDMapGeometry.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

bool ULDMapRegistry::QueryShopReach(const FLDWorldRevision& Expected, const FVector& FootCm,
    bool& WithinReach, ELDWorldQueryFailure& Failure, FString& Error) const
{
    WithinReach=false;
    Failure=ELDWorldQueryFailure::MissingWorld;
    if (!Enabled() || !Content || !Geometry)
    { Error=TEXT("Shop geometry has no registered Demo world."); return false; }
    if (Expected!=WorldRevision)
    { Failure=ELDWorldQueryFailure::StaleWorld; Error=TEXT("Shop geometry belongs to another world revision."); return false; }
    const FName ShopId(*Content->Get().World.ShopId);
    const auto* Shop=Resolve(ShopId,Generation);
    const auto* ShopDefinition=Geometry->Anchors.FindByPredicate([&](const auto& Row)
    { return Row.Id==Content->Get().World.ShopId && Row.Kind==ELDDemoWorldAnchorKind::Shop; });
    if (!Shop || !ShopDefinition)
    { Error=TEXT("The unique compiled Shop anchor is missing."); return false; }
    FCollisionShape Capsule;
    FLDLegalGround Ground;
    if (!ContentCapsule(*Content,Capsule,Error)
        || !QueryGround(Expected,FootCm,Capsule,Shop->Surface,Ground,Failure,Error)) { return false; }
    const auto& Radius=Content->Get().Parameters.ShopInteractRadiusCm;
    if (Radius.Denominator<=0)
    { Failure=ELDWorldQueryFailure::InvalidPosition; Error=TEXT("The compiled shop radius has an invalid denominator."); return false; }
    const double NativeRadius=double(Radius.Numerator)/double(Radius.Denominator);
    if (!FMath::IsFinite(NativeRadius) || NativeRadius<=0)
    { Failure=ELDWorldQueryFailure::InvalidPosition; Error=TEXT("The compiled shop interaction radius is invalid."); return false; }
    WithinReach=FVector::DistSquared2D(Ground.FootCm,Shop->Position)<=FMath::Square(NativeRadius);
    Failure=ELDWorldQueryFailure::None;
    Error.Reset();
    return true;
}

FLDShopEligibilityResult ULDMapRegistry::QueryShopEligibility(const FLDGameplayWritePermit& Permit,
    const FLDCombatBodyRef& Buyer, FName ShopId, const FLDWorldRevision& Expected) const
{
    FLDShopEligibilityResult Result;
    const auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if (!Mode)
    { Result.CommitFailure=ELDCommitReject::NotAuthority; Result.Error=TEXT("Shop eligibility is authority-only."); return Result; }
    Result.CommitFailure=Mode->ValidateWritePermit(Permit,ELDCommitPhase::Transactions);
    if (Result.CommitFailure!=ELDCommitReject::None)
    { Result.Error=TEXT("Shop eligibility requires the current Transactions permit."); return Result; }
    if (Buyer.Victim.Kind!=ELDVictimKind::Contender)
    { Result.CommitFailure=ELDCommitReject::InvalidRequest; Result.Error=TEXT("Only a registered contender can be a shop buyer."); return Result; }
    ALDCharacter* Body=nullptr;
    Result.CommitFailure=Mode->ResolveBody(Permit.GetBoundary().Match,Buyer,Body);
    if (Result.CommitFailure!=ELDCommitReject::None)
    { Result.Error=TEXT("Shop eligibility requires the current living buyer body."); return Result; }
    if (!Enabled() || !Content || ShopId!=FName(*Content->Get().World.ShopId))
    { Result.Error=TEXT("Unknown shop or unregistered world."); return Result; }
    const auto* Movement=Body->GetCharacterMovement();
    if (!Movement->IsMovingOnGround() || !Movement->CurrentFloor.IsWalkableFloor()
        || Movement->CurrentFloor.HitResult.GetComponent()!=GroundComponent.Get())
    {
        Result.WorldFailure=ELDWorldQueryFailure::NotGrounded;
        Result.Error=TEXT("Buyer has no current native support on the registered shop ground.");
        return Result;
    }
    FCollisionShape Required;
    const auto* Capsule=Body->GetCapsuleComponent();
    if (!ContentCapsule(*Content,Required,Result.Error)
        || Capsule->GetScaledCapsuleRadius()!=Required.GetCapsuleRadius()
        || Capsule->GetScaledCapsuleHalfHeight()!=Required.GetCapsuleHalfHeight())
    {
        Result.WorldFailure=ELDWorldQueryFailure::InvalidCapsule;
        if (Result.Error.IsEmpty()) { Result.Error=TEXT("Buyer capsule differs from the registered world agent."); }
        return Result;
    }
    Result.FootCm=Movement->GetActorFeetLocation();
    if (!QueryShopReach(Expected,Result.FootCm,Result.WithinReach,Result.WorldFailure,Result.Error)) { return Result; }
    Result.Succeeded=true;
    Result.Buyer=Buyer;
    Result.ShopId=ShopId;
    Result.World=WorldRevision;
    Result.SurfaceId=Resolve(ShopId,Generation)->Surface;
    Result.SampledAtServerSeconds=Mode->GetAuthorityNowSeconds();
    return Result;
}
