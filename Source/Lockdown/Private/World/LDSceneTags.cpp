#include "World/LDSceneTags.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
bool HasModelTag(const UPrimitiveComponent& Component, FName Tag)
{
    if (Component.ComponentHasTag(Tag)) { return true; }
    for (const AActor* Actor = Component.GetOwner(); Actor; Actor = Actor->GetAttachParentActor())
    { if (Actor->ActorHasTag(Tag)) { return true; } }
    return false;
}
}

bool LDSceneTags::NoCollision(const UPrimitiveComponent& Component)
{
    static const FName Tag(TEXT("Nocollision"));
    return HasModelTag(Component, Tag);
}

bool LDSceneTags::NoViewBlock(const UPrimitiveComponent& Component)
{
    static const FName Tag(TEXT("NoViewBlock"));
    return HasModelTag(Component, Tag);
}

void LDSceneTags::ApplyRuntimePolicy(AActor& Actor)
{
    TInlineComponentArray<UPrimitiveComponent*> Parts(&Actor);
    for (auto* Part : Parts)
    {
        if (NoCollision(*Part))
        {
            Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Part->SetGenerateOverlapEvents(false);
            Part->SetCanEverAffectNavigation(false);
        }
        else if (NoViewBlock(*Part))
        { Part->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore); }
    }
}

void LDSceneTags::ApplyRuntimePolicy(UWorld& World)
{
    if (!World.IsGameWorld()) { return; }
    for (TActorIterator<AActor> It(&World); It; ++It) { ApplyRuntimePolicy(**It); }
}

void LDSceneTags::PrepareEditorNavigation(UWorld& World)
{
#if WITH_EDITOR
    if (World.IsGameWorld()) { return; }
    for (TActorIterator<AActor> It(&World); It; ++It)
    {
        TInlineComponentArray<UPrimitiveComponent*> Parts(*It);
        for (auto* Part : Parts)
        {
            auto* Previous = Cast<ULDSceneTagNavigationState>(
                Part->GetAssetUserDataOfClass(ULDSceneTagNavigationState::StaticClass()));
            if (NoCollision(*Part))
            {
                if (!Previous && Part->CanEverAffectNavigation())
                {
                    Part->Modify();
                    Previous = NewObject<ULDSceneTagNavigationState>(Part, NAME_None, RF_Transactional);
                    Previous->bWasNavigationRelevant = Part->CanEverAffectNavigation();
                    Part->AddAssetUserData(Previous);
                }
                if (Part->CanEverAffectNavigation()) { Part->SetCanEverAffectNavigation(false); }
            }
            else if (Previous)
            {
                const bool bRestore = Previous->bWasNavigationRelevant;
                Part->Modify();
                Part->RemoveUserDataOfClass(ULDSceneTagNavigationState::StaticClass());
                Part->SetCanEverAffectNavigation(bRestore);
            }
        }
    }
#endif
}
