#include "Art/LDArtSubsystem.h"
#include "Core/LDLog.h"
#include "Art/LDStaticScene.h"
#include "Character/LDModelPicking.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/LDDemoEntry.h"
#include "Region/LDMapGeometry.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Tests/LDDemoArtCaptureProbe.h"
#include "Tests/LDDemoPerfBudgetProbe.h"

bool ULDArtSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    // A dedicated server draws nothing; every other game instance presents bodies and decorations.
    return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void ULDArtSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    // Resolve the whole catalog once; a missing asset or entry is an explicit error, never a placeholder body.
    Catalog = nullptr;
    Resolved = FLDArtResolved();
    if (const auto* Loaded = LoadObject<ULDArtCatalog>(nullptr, ULDArtCatalog::AssetPath, nullptr, LOAD_NoWarn))
    {
        if (Loaded->Resolve(Resolved, Error)) { Catalog = Loaded; }
    }
    else { Error = FString::Printf(TEXT("Art catalog %s is missing."), ULDArtCatalog::AssetPath); }
    if (Catalog)
    {
        UE_LOG(LogLDContent, Display, TEXT("LD_ART_CATALOG_READY %d characters, %d props and %d decorations resolved."),
            Catalog->Characters.Num(), Catalog->Props.Num(), Catalog->Decorations.Num());
        // Each body's shared picking model is built while loading rather than on the first pointer hover.
        for (const FLDArtCharacter& Body : Catalog->Characters)
        {
            const FSkeletalMeshRenderData* RenderData = Body.Mesh->GetResourceForRendering();
            for (int32 LOD = 0; RenderData && LOD < RenderData->LODRenderData.Num(); ++LOD)
            {
                const FSkeletalMeshLODRenderData& Data = RenderData->LODRenderData[LOD];
                if (!Data.GetSkinWeightVertexBuffer() || !LDModelPicking::SharedSkinnedModel(*Body.Mesh, LOD, Data, *Data.GetSkinWeightVertexBuffer()))
                {
                    UE_LOG(LogLDContent, Error, TEXT("LD_PICK_MODEL_UNAVAILABLE mesh=%s lod=%d keeps no CPU mesh data; rerun ImportDemoArt.ps1 -Mode Import."),
                        *LDLog::Quote(Body.Mesh->GetName()), LOD);
                }
            }
        }
        return;
    }
    Resolved = FLDArtResolved();
    UE_LOG(LogLDContent, Error, TEXT("LD_ART_CATALOG_INVALID %s Run Scripts\\ImportDemoArt.ps1 -Mode Import or restore the LFS assets."), *Error);
}

const FLDArtResolved* ULDArtSubsystem::Get(const UObject* WorldContext)
{
    const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
    const UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
    const auto* Art = Instance ? Instance->GetSubsystem<ULDArtSubsystem>() : nullptr;
    return Art ? Art->GetResolved() : nullptr;
}

bool ULDArtWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool ULDArtWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void ULDArtWorldSubsystem::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World);
    // Decorations dress the Demo map only; the frontend and fixture worlds show none.
    if (World.GetNetMode() == NM_DedicatedServer || UWorld::RemovePIEPrefix(World.GetOutermost()->GetName()) != LDDemoEntry::WorldMap)
    {
        return;
    }
    const auto* Art = World.GetGameInstance() ? World.GetGameInstance()->GetSubsystem<ULDArtSubsystem>() : nullptr;
    FString Error;
    if (Art && Art->GetCatalog() && LDStaticScene::Validate(World, *Art->GetCatalog(), Error))
    {
        UE_LOG(LogLDContent, Display, TEXT("LD_ART_DECORATIONS_READY %d saved static actors; no runtime decoration generation."),
            LDStaticScene::Visuals(World).Num());
    }
    else { UE_LOG(LogLDContent, Error, TEXT("LD_ART_DECORATIONS_UNAVAILABLE %s"), *Error); }
#if WITH_DEV_AUTOMATION_TESTS
    LDDemoTests::StartArtCaptureProbe(World);
    LDDemoTests::StartPerfBudgetProbe(World);
#endif
}
