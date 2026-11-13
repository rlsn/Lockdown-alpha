#include "World/LDNativeGround.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/Brush.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PhysicsEngine/BodyInstance.h"

namespace
{
const FCollisionResponseParams& StaticResponse()
{
    static const FCollisionResponseParams Response = []
    {
        FCollisionResponseParams Result(ECR_Ignore);
        Result.CollisionResponse.SetResponse(ECC_WorldStatic, ECR_Block);
        return Result;
    }();
    return Response;
}
}

bool LDNativeGround::IsSupport(const UPrimitiveComponent& Component)
{
    const auto* Body = Component.GetBodyInstance();
    return Component.GetCollisionObjectType() == ECC_WorldStatic && Component.IsQueryCollisionEnabled()
        && Component.GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block
        && Component.GetOwner() && !Component.GetOwner()->ActorHasTag(TEXT("LDDemoCliffGuard"))
        && Body && Body->IsValidBodyInstance();
}

FCollisionQueryParams LDNativeGround::Query(const UWorld& World)
{
    FCollisionQueryParams Result(SCENE_QUERY_STAT(DemoNativeGround), false);
    Result.AddIgnoredActor(World.GetDefaultBrush());
    // Invisible cliff guards stop lateral movement; their tops are not standing surfaces.
    for (TActorIterator<AActor> It(&World); It; ++It)
    { if (It->ActorHasTag(TEXT("LDDemoCliffGuard"))) { Result.AddIgnoredActor(*It); } }
    return Result;
}

bool LDNativeGround::Trace(const UWorld& World, FHitResult& Hit, const FVector& Start, const FVector& End,
    const FCollisionQueryParams& Params)
{
    return World.LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params, StaticResponse());
}

bool LDNativeGround::Sweep(const UWorld& World, FHitResult& Hit, const FVector& Start, const FVector& End,
    const FCollisionShape& Shape, const FCollisionQueryParams& Params)
{
    return World.SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Pawn, Shape, Params, StaticResponse());
}

bool LDNativeGround::Overlaps(const UWorld& World, const FVector& Centre, const FCollisionShape& Shape,
    const FCollisionQueryParams& Params)
{
    return World.OverlapBlockingTestByChannel(Centre, FQuat::Identity, ECC_Pawn, Shape, Params, StaticResponse());
}
