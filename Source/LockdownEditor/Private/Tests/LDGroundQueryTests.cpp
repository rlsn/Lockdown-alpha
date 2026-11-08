#include "Region/LDMapRegistry.h"
#include "Framework/LDDemoEntry.h"
#include "Character/LDCharacter.h"
#include "AssetCompilingManager.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDGroundQueryParity, "Lockdown.Rules.DemoGround.ParityAndCache",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDGroundQueryParity::RunTest(const FString& Parameters)
{
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(
        FPackageName::LongPackageNameToFilename(LDDemoEntry::WorldMap, FPackageName::GetMapPackageExtension()));
    if (!TestNotNull(TEXT("Saved Demo world"), World)) { return false; }
    FlushAsyncLoading();
    FAssetCompilingManager::Get().FinishAllCompilation();
    auto* Registry = World->GetSubsystem<ULDMapRegistry>();
    FString Error;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    FCollisionShape Capsule;
    if (!TestNotNull(TEXT("Registry"), Registry)
        || !TestTrue(TEXT("Native world binding: ") + Error, Registry->Rebuild(Error))
        || !TestTrue(TEXT("Runtime content"), LDDemoEntry::LoadRuntimeContent(Content, Error))
        || !TestTrue(TEXT("Approved capsule"), ULDMapRegistry::ContentCapsule(*Content, Capsule, Error))) { return false; }

    FLDMatchContext Match{1, FGuid::NewGuid()};
    FLDCombatBodyRef Body{{ELDVictimKind::CampMember, FGuid::NewGuid(), 1}, FGuid::NewGuid()};
    uint64 Epoch = 1;
    const auto* Geometry = Registry->GetGeometry();
    const FName Surface(*Geometry->Surfaces[0].Id);
    bool Passed = true;
    TSet<ELDWorldQueryFailure> Seen;
    auto Compare = [&](const FVector& Foot, FName Hint, const FCollisionShape& Shape, bool Cache)
    {
        FLDLegalGround Reference, Optimized;
        ELDWorldQueryFailure RefFailure, Failure;
        FString RefError, Why;
        const bool RefLegal = Registry->QueryGroundReference(Foot, Shape, Hint, Reference, RefFailure, RefError);
        const bool Legal = Registry->QueryGround(Registry->Revision(), Foot, Shape, Hint, Optimized, Failure, Why);
        Seen.Add(RefFailure);
        auto Equal = [&](bool Result)
        {
            return Result == RefLegal && Failure == RefFailure && Why == RefError
                && (!Result || (Optimized.World == Reference.World && Optimized.FootCm == Reference.FootCm
                    && Optimized.SurfaceCm == Reference.SurfaceCm && Optimized.SurfaceId == Reference.SurfaceId
                    && Optimized.Floor == Reference.Floor));
        };
        Passed &= TestTrue(TEXT("Exact unfiltered parity at ") + Foot.ToString(), Equal(Legal));
        if (Cache)
        {
            // A fresh identity makes each sample a miss, then the same sample must perform no native query.
            Body.BodyIdentity = FGuid::NewGuid();
            for (int32 Repeat = 0; Repeat < 2; ++Repeat)
            {
                const uint64 Before = Registry->GroundQueryCountForTests();
                const bool Cached = Registry->QueryBodyGround(Match, Body, Epoch, *Content, Foot, Hint, Optimized, Failure, Why);
                Passed &= TestTrue(TEXT("Cached success/rejection and text match reference"), Equal(Cached));
                Passed &= TestEqual(TEXT("Only the first read runs the query"),
                    Registry->GroundQueryCountForTests() - Before, uint64(Repeat == 0 ? 1 : 0));
            }
        }
        return RefLegal;
    };
    auto SampleXY = [&](const FVector2D& XY)
    {
        double Z;
        FVector Normal;
        Registry->GetTerrain().Ground(XY, Z, Normal);
        for (double Offset : {0., 2.15, -20., 400.})
        { Compare(FVector(XY.X, XY.Y, Z + Offset), Surface, Capsule, true); }
    };
    for (const auto& Site : Registry->Sites()) { SampleXY(FVector2D(Site.Position)); }
    // Both sides and ends of every ramp/plateau edge: seams, rims and cliffs, including capsule-sized offsets.
    const double Radius = Capsule.GetCapsuleRadius();
    for (const auto& Region : Registry->GetTerrain().Regions)
    {
        for (int32 I = 0; I < Region.Polygon.Num(); ++I)
        {
            const FVector2D A = Region.Polygon[I], B = Region.Polygon[(I + 1) % Region.Polygon.Num()];
            const FVector2D Along = (B - A).GetSafeNormal();
            const FVector2D Across(-Along.Y, Along.X);
            for (double T : {0., .5, 1.})
            {
                for (double D : {-Radius - 1., -1., 0., 1., Radius + 1.})
                { SampleXY(A + (B - A) * T + Across * D); }
            }
        }
    }
    int32 Cylinders = 0;
    for (const auto& Blocker : Geometry->Blockers)
    {
        if (Blocker.Shape == ELDDemoWorldBlockerShape::Cylinder)
        {
            if (++Cylinders > 16) { continue; }
            FVector Center;
            if (!Blocker.CenterCm.ToVector(Center, Error)) { return false; }
            const double R = double(Blocker.RadiusCm.Numerator) / Blocker.RadiusCm.Denominator;
            for (double D : {0., R + Radius - 1., R + Radius + 1.})
            { SampleXY(FVector2D(Center) + FVector2D(D, 0)); }
        }
        else
        {
            TArray<FVector2D> Polygon;
            if (!LDDemoWorld::Footprint(Blocker, Polygon, Error)) { return false; }
            for (int32 I = 0; I < Polygon.Num(); ++I)
            {
                const FVector2D A = Polygon[I], B = Polygon[(I + 1) % Polygon.Num()];
                const FVector2D Along = (B - A).GetSafeNormal(), Across(-Along.Y, Along.X);
                for (double D : {-Radius - 1., -1., 1., Radius + 1.})
                { SampleXY((A + B) * .5 + Across * D); }
            }
        }
    }
    FVector Min, Max;
    if (!Geometry->Surfaces[0].MinCm.ToVector(Min, Error) || !Geometry->Surfaces[0].MaxCm.ToVector(Max, Error)) { return false; }
    for (double X : {Min.X, Min.X + Radius - 1., Min.X + Radius + 1., Max.X - Radius - 1., Max.X})
    { SampleXY(FVector2D(X, (Min.Y + Max.Y) * .5)); }
    for (double Y : {Min.Y, Min.Y + Radius - 1., Min.Y + Radius + 1., Max.Y - Radius - 1., Max.Y})
    { SampleXY(FVector2D((Min.X + Max.X) * .5, Y)); }

    const FVector Foot = Registry->Sites()[0].Position;
    Compare(Foot, NAME_None, Capsule, true);
    Compare(Foot, Surface, FCollisionShape::MakeSphere(float(Radius)), false);
    Compare(FVector(std::numeric_limits<double>::infinity(), 0, 0), Surface, Capsule, true);

    FVector CacheFoot = Foot;
    FName CacheSurface = Surface;
    auto QueryBody = [&]()
    {
        FLDLegalGround Ground;
        ELDWorldQueryFailure Failure;
        FString Why;
        return Registry->QueryBodyGround(Match, Body, Epoch, *Content, CacheFoot, CacheSurface, Ground, Failure, Why);
    };
    QueryBody();
    auto MustMiss = [&](const TCHAR* Detail)
    {
        const uint64 Before = Registry->GroundQueryCountForTests();
        QueryBody();
        Passed &= TestEqual(Detail, Registry->GroundQueryCountForTests() - Before, uint64(1));
    };
    CacheFoot.X += .00001;
    MustMiss(TEXT("Even a sub-tolerance foot change invalidates"));
    CacheFoot = Foot;
    MustMiss(TEXT("Restoring the foot still runs a fresh query"));
    CacheSurface = NAME_None;
    MustMiss(TEXT("Surface change invalidates"));
    CacheSurface = Surface;
    MustMiss(TEXT("A cached surface rejection cannot poison a legal surface"));
    ++Epoch;
    MustMiss(TEXT("Completed camp reset epoch invalidates"));
    ++Body.Victim.Life;
    Body.BodyIdentity = FGuid::NewGuid();
    MustMiss(TEXT("A respawned body cannot reuse a dead body's result"));
    Match = {2, FGuid::NewGuid()};
    MustMiss(TEXT("Restart invalidates"));
    const auto Revision = Registry->Revision();
    Passed &= TestTrue(TEXT("Rebind static world"), Registry->Rebuild(Error));
    Passed &= TestTrue(TEXT("Same authored revision"), Registry->Revision() == Revision);
    MustMiss(TEXT("Even same-revision rebuild invalidates"));

    // Production bodies can spawn, move, dash and disappear without world revision changes. Their Pawn object
    // type and ignored ground channel exclude them from both native queries used by legal-ground validation.
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ALDCharacter* Pawn = World->SpawnActor<ALDCharacter>(ALDCharacter::StaticClass(),
        Foot + FVector(0, 0, Capsule.GetCapsuleHalfHeight()), FRotator::ZeroRotator, Spawn);
    if (!TestNotNull(TEXT("Dynamic production body"), Pawn)) { return false; }
    Passed &= TestEqual(TEXT("Dynamic body is not WorldStatic"), Pawn->GetCapsuleComponent()->GetCollisionObjectType(), ECC_Pawn);
    Passed &= TestEqual(TEXT("Dynamic body cannot become floor support"),
        Pawn->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_GameTraceChannel1), ECR_Ignore);
    auto CompareDynamic = [&]()
    {
        FLDLegalGround Reference, Cached;
        ELDWorldQueryFailure RefFailure, Failure;
        FString RefError, Why;
        const bool Expected = Registry->QueryGroundReference(Foot, Capsule, Surface, Reference, RefFailure, RefError);
        const uint64 Before = Registry->GroundQueryCountForTests();
        const bool Actual = Registry->QueryBodyGround(Match, Body, Epoch, *Content, Foot, Surface, Cached, Failure, Why);
        Passed &= TestTrue(TEXT("Dynamic mutation/despawn preserves complete cached result"),
            Actual == Expected && Failure == RefFailure && Why == RefError
            && (!Actual || (Cached.World == Reference.World && Cached.FootCm == Reference.FootCm
                && Cached.SurfaceCm == Reference.SurfaceCm && Cached.SurfaceId == Reference.SurfaceId
                && Cached.Floor == Reference.Floor)));
        Passed &= TestEqual(TEXT("Dynamic mutation/despawn retains a cache hit"),
            Registry->GroundQueryCountForTests() - Before, uint64(0));
    };
    CompareDynamic();
    for (int32 I = 0; I < 3; ++I)
    {
        Pawn->SetActorLocation(Foot + FVector(0, 0, 10. * I));
        Pawn->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, I == 0 ? ECR_Ignore : ECR_Block);
        CompareDynamic();
    }
    Pawn->Destroy();
    CompareDynamic();
    Passed &= TestTrue(TEXT("Body despawn leaves authored revision unchanged"), Registry->Revision() == Revision);

    // A foreign native support reports WrongSurface, rather than being promoted to registered terrain.
    AActor* Foreign = World->SpawnActor<AActor>();
    UBoxComponent* Box = NewObject<UBoxComponent>(Foreign);
    Foreign->SetRootComponent(Box);
    Foreign->AddInstanceComponent(Box);
    Box->SetBoxExtent(FVector(100, 100, 10));
    Box->SetCollisionObjectType(ECC_WorldDynamic);
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionResponseToAllChannels(ECR_Ignore);
    Box->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
    Box->RegisterComponent();
    Foreign->SetActorLocation(Foot - FVector(0, 0, 10));
    auto* Floor = Registry->Sites()[0].Floor.Get();
    const auto Response = Floor->GetCollisionResponseToChannel(ECC_GameTraceChannel1);
    Floor->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
    ON_SCOPE_EXIT
    {
        Floor->SetCollisionResponseToChannel(ECC_GameTraceChannel1, Response);
        Foreign->Destroy();
    };
    Compare(Foot, Surface, Capsule, false);
    for (ELDWorldQueryFailure Required : {ELDWorldQueryFailure::None, ELDWorldQueryFailure::Blocked,
        ELDWorldQueryFailure::NotGrounded, ELDWorldQueryFailure::WrongSurface, ELDWorldQueryFailure::InvalidSurface,
        ELDWorldQueryFailure::InvalidCapsule, ELDWorldQueryFailure::InvalidPosition})
    { Passed &= TestTrue(TEXT("Corpus actually exercises rejection/success ") + FString::FromInt(int32(Required)), Seen.Contains(Required)); }
    return Passed;
}
