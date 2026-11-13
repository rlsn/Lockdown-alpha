#include "Art/LDStaticScene.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/Brush.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Region/LDMapGeometry.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerStart.h"
#include <numeric>

FString LDStaticScene::Value(const AActor& Actor, const TCHAR* Prefix)
{
    for (FName Tag : Actor.Tags)
    {
        if (Tag.ToString().StartsWith(Prefix)) { return Tag.ToString().RightChop(FCString::Strlen(Prefix)); }
    }
    return {};
}

UStaticMeshComponent* LDStaticScene::Collision(AStaticMeshActor& Actor)
{
    TInlineComponentArray<UStaticMeshComponent*> Parts(&Actor);
    for (auto* Part : Parts) { if (Part->ComponentHasTag(CollisionTag)) { return Part; } }
    return Actor.GetStaticMeshComponent();
}

UStaticMeshComponent* LDStaticScene::Collision(const AStaticMeshActor& Actor)
{
    return Collision(const_cast<AStaticMeshActor&>(Actor));
}

TArray<UStaticMeshComponent*> LDStaticScene::Visuals(const UWorld& World)
{
    TArray<AStaticMeshActor*> Actors;
    for (AActor* Actor : World.PersistentLevel->Actors)
    {
        if (auto* Mesh = Cast<AStaticMeshActor>(Actor); Mesh && Mesh->ActorHasTag(VisualTag)) { Actors.Add(Mesh); }
    }
    Actors.Sort([](const AStaticMeshActor& A, const AStaticMeshActor& B) { return A.GetName().ToLower() < B.GetName().ToLower(); });
    TArray<UStaticMeshComponent*> Parts;
    for (auto* Actor : Actors) { Parts.Add(Actor->GetStaticMeshComponent()); }
    return Parts;
}

bool LDStaticScene::Read(const UWorld& World, const ULDArtCatalog& Assets,
    TArray<FLDArtDecoration>& Placements, TArray<FString>& Hidden, FString& Error)
{
    Placements.Reset(); Hidden.Reset();
    auto Fail = [&](const FString& Why) { Error = Why; return false; };
    for (UStaticMeshComponent* Part : Visuals(World))
    {
        const AActor* Actor = Part->GetOwner();
        const FLDArtProp* Prop = Assets.Props.FindByPredicate([&](const FLDArtProp& Row) { return Row.Mesh == Part->GetStaticMesh(); });
        if (!Prop) { return Fail(TEXT("Scene mesh must be a licensed imported prop: ") + Actor->GetPathName()); }
        if (Part->GetCollisionEnabled() != ECollisionEnabled::NoCollision || Part->GetGenerateOverlapEvents()
            || Part->CanEverAffectNavigation() || Part->GetCollisionResponseToChannels() != FCollisionResponseContainer(ECR_Ignore))
        { return Fail(TEXT("Visual mesh must not change gameplay collision/navigation: ") + Actor->GetPathName()); }
        FLDArtDecoration& Row = Placements.AddDefaulted_GetRef();
        Row.Prop = Prop->Id;
        Row.Transform = Part->GetComponentTransform();
        if (!LDArt::ParseZoneId(Value(*Actor, TEXT("LDSceneZone:")), Row.Zone))
        { return Fail(TEXT("Scene actor needs a placement zone: ") + Actor->GetPathName()); }
        if (Row.Zone == ELDArtZone::Blocker)
        {
            if (!Actor->ActorHasTag(LDDemoWorld::StaticGeometryTag) || Collision(*CastChecked<AStaticMeshActor>(Actor)) == Part)
            { return Fail(TEXT("Blocker visual needs its saved collision component: ") + Actor->GetPathName()); }
            Row.Blocker = Actor->Tags[0].ToString();
        }
        if (Part->ComponentHasTag(ULDArtCatalog::FadeableTag) != Prop->bFadeable)
        { return Fail(TEXT("Scene fade tag differs from the selected prop; synchronize the map: ") + Actor->GetPathName()); }
    }
    for (const AActor* Actor : World.PersistentLevel->Actors)
    {
        if (Actor && Actor->ActorHasTag(HiddenTag))
        {
            if (!Actor->ActorHasTag(LDDemoWorld::StaticGeometryTag) || Actor->ActorHasTag(VisualTag))
            { return Fail(TEXT("Invalid hidden geometry binding: ") + Actor->GetPathName()); }
            Hidden.Add(Actor->Tags[0].ToString());
        }
    }
    Hidden.Sort();
    if (Placements.IsEmpty()) { return Fail(TEXT("The saved Demo map contains no editable scene visuals.")); }
    Error.Reset(); return true;
}

bool LDStaticScene::Validate(const UWorld& World, const ULDArtCatalog& Index, FString& Error)
{
    TArray<FLDArtDecoration> Rows;
    TArray<FString> Hidden;
    if (!Read(World, Index, Rows, Hidden, Error)) { return false; }
    TArray<FString> ExpectedHidden = Index.HiddenBlockers;
    ExpectedHidden.Sort();
    if (Rows.Num() != Index.Decorations.Num() || Hidden != ExpectedHidden)
    { Error = TEXT("Saved scene differs from its derived art index. Run SyncDemoMap.ps1 after editing."); return false; }
    for (int32 I = 0; I < Rows.Num(); ++I)
    {
        const auto& A = Rows[I]; const auto& B = Index.Decorations[I];
        if (A.Prop != B.Prop || A.Zone != B.Zone || A.Blocker != B.Blocker || !A.Transform.Equals(B.Transform, 1e-2))
        { Error = FString::Printf(TEXT("Saved scene row %d differs from its baked art index. Run SyncDemoMap.ps1 after editing."), I); return false; }
    }
    Error.Reset(); return true;
}

bool LDStaticScene::ProjectGeometry(const UWorld& World, const FLDDemoWorldGeometry& Base,
    FLDDemoWorldGeometry& Out, FString& Error)
{
    Out = Base;
    TMap<FString, const AStaticMeshActor*> Shapes;
    TMap<FString, const AActor*> Points;
    auto Fail = [&](const FString& Why) { Error = Why; return false; };
    for (const AActor* Actor : World.PersistentLevel->Actors)
    {
        if (!Actor) { continue; }
        if (Actor == World.GetDefaultBrush()) { continue; }
        if (!Actor->ActorHasTag(LDDemoWorld::StaticGeometryTag))
        {
            TInlineComponentArray<UPrimitiveComponent*> Parts(Actor);
            for (const auto* Part : Parts)
            {
                if (Part->Mobility == EComponentMobility::Static && Part->GetCollisionEnabled() != ECollisionEnabled::NoCollision
                    && (Part->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block
                        || Part->GetCollisionResponseToChannel(ECC_GameTraceChannel2) == ECR_Block
                        || Part->GetCollisionResponseToChannel(ECC_GameTraceChannel1) == ECR_Block))
                { return Fail(TEXT("Unregistered static collision; copy a registered scene object or remove visual collision: ") + Actor->GetPathName()); }
            }
        }
        if (auto* Shape = Cast<AStaticMeshActor>(Actor); Shape && Shape->ActorHasTag(LDDemoWorld::StaticGeometryTag))
        {
            const FString Id = Actor->Tags[0].ToString();
            if (Shapes.Contains(Id)) { return Fail(TEXT("Duplicate geometry identity; synchronize the map: ") + Id); }
            Shapes.Add(Id, Shape);
        }
        const FString Anchor = Value(*Actor, TEXT("LDSceneAnchor:"));
        const FString Member = Value(*Actor, TEXT("LDSceneMember:"));
        const auto* Start = Cast<APlayerStart>(Actor);
        const FString Id = Start ? Start->PlayerStartTag.ToString() : !Anchor.IsEmpty() ? Anchor : Member;
        if (!Id.IsEmpty())
        {
            if (Points.Contains(Id)) { return Fail(TEXT("Duplicate layout point: ") + Id); }
            Points.Add(Id, Actor);
        }
    }
    const auto Number = [](double V)
    {
        FLDDemoRational Result;
        Result.Numerator = FMath::RoundToInt64(V * 1000.);
        Result.Denominator = 1000;
        const int64 Divisor = std::gcd(FMath::Abs(Result.Numerator), Result.Denominator);
        Result.Numerator /= Divisor; Result.Denominator /= Divisor;
        return Result;
    };
    const auto Point = [&](const FVector& V)
    { FLDDemoWorldPoint P; P.X = Number(V.X); P.Y = Number(V.Y); P.Z = Number(V.Z); return P; };
    const auto TransformRing = [&](TArray<FLDDemoWorldPoint>& Ring, const FTransform& Transform)
    {
        for (auto& P : Ring)
        {
            FVector V;
            if (!P.ToVector(V, Error)) { return false; }
            P = Point(Transform.TransformPosition(V));
        }
        return true;
    };
    const auto TransformOf = [&](const FString& Id, FTransform& Transform)
    {
        const auto* Actor = Shapes.FindRef(Id);
        if (!Actor) { return Fail(TEXT("Missing saved geometry: ") + Id); }
        Transform = Collision(*Actor)->GetComponentTransform();
        const FVector Scale = Transform.GetScale3D();
        if (!Transform.IsValid() || Scale.GetMin() <= 0 || !Transform.GetRotation().GetUpVector().Equals(FVector::UpVector, 1e-5))
        { return Fail(TEXT("Gameplay geometry requires positive scale and an upright transform: ") + Id); }
        return true;
    };
    FVector Min, Max;
    if (!Base.Surfaces[0].MinCm.ToVector(Min, Error) || !Base.Surfaces[0].MaxCm.ToVector(Max, Error)) { return false; }
    FTransform Ground;
    if (!TransformOf(Base.Surfaces[0].Id, Ground)) { return false; }
    if (!Ground.GetRotation().IsIdentity()) { return Fail(TEXT("The registered rectangular ground must remain axis aligned.")); }
    const FVector Size = Ground.GetScale3D() * 100.;
    const FVector Centre = Ground.GetLocation() + FVector(0, 0, Size.Z * .5);
    Out.Surfaces[0].MinCm = Point(Centre - FVector(Size.X, Size.Y, 0) * .5);
    Out.Surfaces[0].MaxCm = Point(Centre + FVector(Size.X, Size.Y, 0) * .5);
    for (auto& Plateau : Out.Plateaus)
    { FTransform T; if (!TransformOf(Plateau.Id, T) || !TransformRing(Plateau.PolygonCm, T)) { return false; } }
    for (auto& Ramp : Out.Ramps)
    { FTransform T; if (!TransformOf(Ramp.Id, T) || !TransformRing(Ramp.PolygonCm, T)) { return false; } }
    Out.Blockers.Reset();
    for (const auto& Pair : Shapes)
    {
        const FString PrototypeId = Value(*Pair.Value, TEXT("LDScenePrototype:"));
        const auto* Prototype = Base.Blockers.FindByPredicate([&](const FLDDemoWorldBlocker& B) { return B.Id == Pair.Key; });
        if (!Prototype && !PrototypeId.IsEmpty())
        { Prototype = Base.Blockers.FindByPredicate([&](const FLDDemoWorldBlocker& B) { return B.Id == PrototypeId; }); }
        if (!Prototype)
        {
            if (Pair.Key == Base.Surfaces[0].Id || Pair.Value->ActorHasTag(LDDemoWorld::TerrainTag)
                || Pair.Value->ActorHasTag(TEXT("LDDemoCliffGuard"))) { continue; }
            return Fail(TEXT("Unknown saved geometry identity: ") + Pair.Key);
        }
        auto& B = Out.Blockers.Add_GetRef(*Prototype);
        B.Id = Pair.Key;
        FTransform T;
        if (!TransformOf(B.Id, T)) { return false; }
        const double Height = double(B.HeightCm.Numerator) / B.HeightCm.Denominator;
        B.HeightCm = Number(Height * T.GetScale3D().Z);
        B.BlocksMovement = Collision(*Pair.Value)->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;
        B.BlocksSight = Collision(*Pair.Value)->GetCollisionResponseToChannel(ECC_GameTraceChannel2) == ECR_Block;
        if (B.Shape == ELDDemoWorldBlockerShape::Cylinder)
        {
            if (!FMath::IsNearlyEqual(T.GetScale3D().X, T.GetScale3D().Y, 1e-5))
            { return Fail(TEXT("A tree trunk must keep a circular footprint; use equal X/Y scale: ") + B.Id); }
            B.CenterCm = Point(T.GetLocation());
            B.RadiusCm = Number(double(B.RadiusCm.Numerator) / B.RadiusCm.Denominator * T.GetScale3D().X);
        }
        else if (!TransformRing(B.PolygonCm, T)) { return false; }
    }
    // Preserve base order so an unchanged editor save produces identical source bytes and identities.
    Out.Blockers.Sort([&](const FLDDemoWorldBlocker& A, const FLDDemoWorldBlocker& B)
    {
        const auto At = [&](const FString& Id) { return Base.Blockers.IndexOfByPredicate([&](const FLDDemoWorldBlocker& V) { return V.Id == Id; }); };
        const int32 IA = At(A.Id), IB = At(B.Id);
        if (IA == INDEX_NONE && IB == INDEX_NONE) { return A.Id < B.Id; }
        return (IA == INDEX_NONE ? MAX_int32 : IA) < (IB == INDEX_NONE ? MAX_int32 : IB);
    });
    for (auto& Anchor : Out.Anchors)
    {
        const AActor* Actor = Points.FindRef(Anchor.Id);
        if (!Actor) { return Fail(TEXT("Missing saved layout point: ") + Anchor.Id); }
        FVector Position = Actor->GetActorLocation();
        if (Cast<APlayerStart>(Actor)) { Position.Z -= GetDefault<ALDCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(); }
        Anchor.PositionCm = Point(Position);
        for (auto& Member : Anchor.Members)
        {
            const AActor* Home = Points.FindRef(Member.Id);
            if (!Home) { return Fail(TEXT("Missing camp member home point: ") + Member.Id); }
            Member.PositionCm = Point(Home->GetActorLocation());
        }
    }
    bool HasFinalPoint = false;
    for (int32 Slot = 0; Slot < LDDemoWorld::FinalSpawns; ++Slot)
    {
        HasFinalPoint |= Points.Contains(LDDemoWorld::FinalSpawnId(Slot));
    }
    HasFinalPoint |= Points.Contains(LDDemoWorld::FinalCaptureId());
    const bool HasFinalAnchor = Out.Anchors.ContainsByPredicate([](const FLDDemoWorldAnchor& Anchor)
    {
        return Anchor.Kind == ELDDemoWorldAnchorKind::FinalSpawn || Anchor.Kind == ELDDemoWorldAnchorKind::FinalCapture;
    });
    if (HasFinalPoint || HasFinalAnchor)
    {
        const auto AddFinalAnchor = [&](const FString& Id, ELDDemoWorldAnchorKind Kind)
        {
            const AActor* Actor = Points.FindRef(Id);
            if (!Actor) { return Fail(TEXT("Missing saved final-arena anchor: ") + Id); }
            auto* Existing = Out.Anchors.FindByPredicate([&](const FLDDemoWorldAnchor& Anchor) { return Anchor.Id == Id; });
            if (Existing)
            {
                if (Existing->Kind != Kind) { return Fail(TEXT("Final-arena anchor kind changed: ") + Id); }
                return true;
            }
            auto& Anchor = Out.Anchors.AddDefaulted_GetRef();
            Anchor.Id = Id;
            Anchor.Kind = Kind;
            Anchor.SurfaceId = Out.Surfaces[0].Id;
            Anchor.PositionCm = Point(Actor->GetActorLocation());
            return true;
        };
        for (int32 Slot = 0; Slot < LDDemoWorld::FinalSpawns; ++Slot)
        {
            if (!AddFinalAnchor(LDDemoWorld::FinalSpawnId(Slot), ELDDemoWorldAnchorKind::FinalSpawn)) { return false; }
        }
        if (!AddFinalAnchor(LDDemoWorld::FinalCaptureId(), ELDDemoWorldAnchorKind::FinalCapture)) { return false; }
    }
    Error.Reset(); return true;
}

bool LDStaticScene::ValidateGeometry(const UWorld& World, const FLDDemoWorldGeometry& Base, FString& Error)
{
    FLDDemoWorldGeometry Projected;
    if (!ProjectGeometry(World, Base, Projected, Error)) { return false; }
    if (!FLDDemoWorldGeometry::StaticStruct()->CompareScriptStruct(&Base, &Projected, PPF_None))
    { Error = TEXT("Saved layout differs from its baked geometry. Save the map and run SyncDemoMap.ps1 before playing."); return false; }
    return true;
}
