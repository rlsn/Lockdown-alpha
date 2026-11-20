#include "Art/LDStaticScene.h"
#include "World/LDNativeGround.h"
#include "World/LDLayoutPoint.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/Brush.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Region/LDMapGeometry.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerStart.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/BodyInstance.h"
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

FString LDStaticScene::OptionalCopyId(const FString& Id, const FString& ObjectName)
{
    // "_copy_" and eight hex digits of the saved object name are always kept; only the original stem is shortened to fit
    // the 63-character definition id limit, so two copies never collide and a copy never equals its original.
    const FString Suffix = FString::Printf(TEXT("_copy_%08x"), FCrc::StrCrc32(*ObjectName));
    return Id.Left(63 - Suffix.Len()) + Suffix;
}

bool LDStaticScene::ProjectGeometry(const UWorld& World, const FLDDemoWorldGeometry& Base,
    FLDDemoWorldGeometry& Out, FString& Error, bool bForPlay)
{
    Out = Base;
    Out.bSceneLayout = bForPlay;
    TMap<FString, const AStaticMeshActor*> Shapes;
    TMap<FString, FString> Prototypes;
    TMap<FString, const AActor*> Points;
    TMap<FString, const ALDLayoutPoint*> SharedTraining;
    auto Fail = [&](const FString& Why) { Error = Why; return false; };
    for (const AActor* Actor : World.PersistentLevel->Actors)
    {
        if (!Actor || Actor->IsEditorOnly()) { continue; }
        if (Actor == World.GetDefaultBrush()) { continue; }
        if (bForPlay && (!Value(*Actor,TEXT("LDSceneAnchor:")).IsEmpty()
            || !Value(*Actor,TEXT("LDSceneMember:")).IsEmpty()
            || (Cast<ALDLayoutPoint>(Actor) && CastChecked<ALDLayoutPoint>(Actor)->bSharedTrainingDummy)))
        {
            for (const AActor* Parent=Actor->GetAttachParentActor();Parent;Parent=Parent->GetAttachParentActor())
            {
                if (Parent->IsEditorOnly())
                { return Fail(TEXT("Runtime layout point has an editor-only attachment ancestor that cook would strip: ")+Actor->GetPathName()+TEXT(" -> ")+Parent->GetPathName()); }
            }
        }
        if (const auto* Layout = Cast<ALDLayoutPoint>(Actor); Layout && Layout->bSharedTrainingDummy)
        {
            if (!bForPlay) { return Fail(TEXT("Shared training points are authored in the saved level; the source snapshot exporter does not support them.")); }
            SharedTraining.Add(Layout->SharedTrainingId(), Layout);
            continue;
        }
        if (!bForPlay && !Actor->ActorHasTag(LDDemoWorld::StaticGeometryTag))
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
            FString Id = Actor->Tags[0].ToString();
            const FString OriginalId = Id;
            if (Shapes.Contains(Id))
            {
                if (!bForPlay) { return Fail(TEXT("Duplicate geometry identity; synchronize the map: ") + Id); }
                // Names are saved with the map and survive PIE duplication. Do not mutate editor objects or
                // assign a new random identity on each client when a designer duplicates a blocker.
                Id += TEXT("_copy_") + Actor->GetName();
            }
            Prototypes.Add(Id, Value(*Actor, TEXT("LDScenePrototype:")).IsEmpty()
                ? OriginalId : Value(*Actor, TEXT("LDScenePrototype:")));
            Shapes.Add(Id, Shape);
        }
        const FString Anchor = Value(*Actor, TEXT("LDSceneAnchor:"));
        FString Member = Value(*Actor, TEXT("LDSceneMember:"));
        if (bForPlay && !Member.IsEmpty() && Actor->GetAttachParentActor())
        {
            const AActor* Parent = Actor->GetAttachParentActor();
            const FString PrototypeId = Value(*Parent,TEXT("LDScenePrototypeAnchor:"));
            const auto* Prototype = Base.Anchors.FindByPredicate([&](const auto& A) { return A.Id == PrototypeId; });
            int32 Slot = INDEX_NONE;
            const auto* Centre = Cast<ALDLayoutPoint>(Parent);
            const bool bAuthored = Centre && Centre->bUseAuthoredCampMembers;
            if (bAuthored)
            {
                const FString Text=Value(*Actor,TEXT("LDSceneMemberSlot:"));
                Slot=FCString::Atoi(*Text);
                if (Slot<0 || FString::FromInt(Slot)!=Text) { return Fail(TEXT("Authored camp member needs a nonnegative integer slot: ")+Actor->GetPathName()); }
            }
            else if (Prototype)
            {
                Slot = Prototype->Members.IndexOfByPredicate([&](const auto& M) { return M.Id == Member; });
                if (Slot==INDEX_NONE)
                {
                    // The saved slot survives renumbering and copying a complete group.
                    const FString Text=Value(*Actor,TEXT("LDSceneMemberSlot:"));
                    if (Text.IsNumeric()) { Slot=FCString::Atoi(*Text); }
                }
            }
            if (Prototype && Prototype->Kind == ELDDemoWorldAnchorKind::Camp && (bAuthored ? Slot>=0 : Prototype->Members.IsValidIndex(Slot)))
            { Member=FString::Printf(TEXT("%s_%d"),*Value(*Parent,TEXT("LDSceneAnchor:")),Slot); }
        }
        const auto* Start = Cast<APlayerStart>(Actor);
        FString Id = Start ? Start->PlayerStartTag.ToString() : !Anchor.IsEmpty() ? Anchor : Member;
        if (!Id.IsEmpty())
        {
            ELDDemoWorldAnchorKind OptionalKind;
            if (Points.Contains(Id) && LDDemoWorld::OptionalKindOf(Id, OptionalKind))
            {
                // Spec §11.4: a copied mini flag, locker or vending point is a new point of the same kind. Its identity
                // derives from the saved object name, which a copy never shares.
                Id = LDStaticScene::OptionalCopyId(Id, Actor->GetName());
            }
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
        if (!Transform.IsValid() || (!bForPlay && (Scale.GetMin() <= 0
            || !Transform.GetRotation().GetUpVector().Equals(FVector::UpVector, 1e-5))))
        { return Fail(TEXT("Gameplay geometry requires positive scale and an upright transform: ") + Id); }
        return true;
    };
    FVector Min, Max;
    if (!Base.Surfaces[0].MinCm.ToVector(Min, Error) || !Base.Surfaces[0].MaxCm.ToVector(Max, Error)) { return false; }
    FTransform Ground;
    if (!bForPlay && !TransformOf(Base.Surfaces[0].Id, Ground)) { return false; }
    if (!bForPlay && !Ground.GetRotation().IsIdentity()) { return Fail(TEXT("The registered rectangular ground must remain axis aligned.")); }
    if (bForPlay)
    {
        FBox Bounds(ForceInit);
        for (const AActor* Actor : World.PersistentLevel->Actors)
        {
            if (!Actor || Actor->IsEditorOnly() || Actor == World.GetDefaultBrush()) { continue; }
            TInlineComponentArray<UPrimitiveComponent*> Parts(Actor);
            for (const auto* Part : Parts)
            {
                if (LDNativeGround::IsSupport(*Part)) { Bounds += Part->GetBodyInstance()->GetBodyBounds(); }
            }
        }
        if (!Bounds.IsValid) { return Fail(TEXT("NotGrounded: the scene has no native ground collision.")); }
        Out.Surfaces[0].MinCm = Point(FVector(Bounds.Min.X, Bounds.Min.Y, Bounds.Max.Z));
        Out.Surfaces[0].MaxCm = Point(FVector(Bounds.Max.X, Bounds.Max.Y, Bounds.Max.Z));
        Out.Plateaus.RemoveAll([&](const auto& P) { return !Shapes.Contains(P.Id); });
        Out.Ramps.RemoveAll([&](const auto& P) { return !Shapes.Contains(P.Id); });
    }
    else
    {
        const FVector Size = Ground.GetScale3D() * 100.;
        const FVector Centre = Ground.GetLocation() + FVector(0, 0, Size.Z * .5);
        Out.Surfaces[0].MinCm = Point(Centre - FVector(Size.X, Size.Y, 0) * .5);
        Out.Surfaces[0].MaxCm = Point(Centre + FVector(Size.X, Size.Y, 0) * .5);
    }
    // Runtime ground and sight use the native scene. The source polygons remain available to the explicit
    // export tool; transformed pieces are not reinterpreted as another approved two-level terrain layout.
    for (auto& Plateau : Out.Plateaus)
    { FTransform T; if (!bForPlay && (!TransformOf(Plateau.Id, T) || !TransformRing(Plateau.PolygonCm, T))) { return false; } }
    for (auto& Ramp : Out.Ramps)
    { FTransform T; if (!bForPlay && (!TransformOf(Ramp.Id, T) || !TransformRing(Ramp.PolygonCm, T))) { return false; } }
    Out.Blockers.Reset();
    for (const auto& Pair : Shapes)
    {
        const FString PrototypeId = Prototypes.FindRef(Pair.Key);
        const auto* Prototype = Base.Blockers.FindByPredicate([&](const FLDDemoWorldBlocker& B) { return B.Id == Pair.Key; });
        if (!Prototype && !PrototypeId.IsEmpty())
        { Prototype = Base.Blockers.FindByPredicate([&](const FLDDemoWorldBlocker& B) { return B.Id == PrototypeId; }); }
        if (!Prototype)
        {
            if (Pair.Key == Base.Surfaces[0].Id || Pair.Value->ActorHasTag(LDDemoWorld::TerrainTag)
                || Pair.Value->ActorHasTag(TEXT("LDDemoCliffGuard"))) { continue; }
            if (bForPlay) { continue; } // Native collision also covers newly placed/relabelled meshes.
            return Fail(TEXT("Unknown saved geometry identity: ") + Pair.Key);
        }
        const auto* Component = Collision(*Pair.Value);
        if (bForPlay && Component->GetCollisionEnabled() == ECollisionEnabled::NoCollision) { continue; }
        auto& B = Out.Blockers.Add_GetRef(*Prototype);
        B.Id = Pair.Key;
        FTransform T;
        if (!TransformOf(B.Id, T)) { return false; }
        const double Height = double(B.HeightCm.Numerator) / B.HeightCm.Denominator;
        B.HeightCm = Number(Height * T.GetScale3D().Z);
        B.BlocksMovement = Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;
        B.BlocksSight = Component->GetCollisionResponseToChannel(ECC_GameTraceChannel2) == ECR_Block;
        if (bForPlay && (T.GetScale3D().GetMin() <= 0
            || !T.GetRotation().GetUpVector().Equals(FVector::UpVector, 1e-5)
            || (B.Shape == ELDDemoWorldBlockerShape::Cylinder
                && !FMath::IsNearlyEqual(FMath::Abs(T.GetScale3D().X), FMath::Abs(T.GetScale3D().Y), 1e-5))))
        {
            // Use the native convex vertices for rotated/mirrored/stretched blockers. The broad footprint is
            // only map metadata; collision/ground admission below always queries the actual physics body.
            TArray<FVector2D> VerticesXY;
            const UBodySetup* Body = Component->GetStaticMesh() ? Component->GetStaticMesh()->GetBodySetup() : nullptr;
            if (Body)
            {
                for (const auto& Convex : Body->AggGeom.ConvexElems)
                {
                    for (const auto& Vertex : Convex.VertexData)
                    {
                        const FVector Position = T.TransformPosition(Convex.GetTransform().TransformPosition(Vertex));
                        VerticesXY.Add(FVector2D(Position.X, Position.Y));
                    }
                }
            }
            const auto* Physics = Component->GetBodyInstance();
            const FBox Bounds = Physics && Physics->IsValidBodyInstance() ? Physics->GetBodyBounds() : Component->Bounds.GetBox();
            if (VerticesXY.Num() < 3)
            {
                VerticesXY = {{Bounds.Min.X, Bounds.Min.Y}, {Bounds.Max.X, Bounds.Min.Y},
                    {Bounds.Max.X, Bounds.Max.Y}, {Bounds.Min.X, Bounds.Max.Y}};
            }
            VerticesXY.Sort([](const FVector2D& A, const FVector2D& C) { return A.X != C.X ? A.X < C.X : A.Y < C.Y; });
            TArray<FVector2D> Hull;
            const auto Append = [&](const FVector2D& P, int32 Minimum)
            {
                while (Hull.Num() > Minimum && FVector2D::CrossProduct(Hull.Last() - Hull[Hull.Num()-2], P - Hull.Last()) <= 0)
                { Hull.Pop(EAllowShrinking::No); }
                Hull.Add(P);
            };
            for (const auto& P : VerticesXY) { Append(P, 1); }
            const int32 Lower = Hull.Num();
            for (int32 I = VerticesXY.Num()-2; I >= 0; --I) { Append(VerticesXY[I], Lower); }
            Hull.Pop(EAllowShrinking::No);
            B.Shape = ELDDemoWorldBlockerShape::Polygon;
            B.CenterCm = {}; B.RadiusCm = {}; B.PolygonCm.Reset();
            B.HeightCm = Number(Bounds.Max.Z - Bounds.Min.Z);
            for (const auto& P : Hull) { B.PolygonCm.Add(Point(FVector(P.X, P.Y, Bounds.Min.Z))); }
        }
        else if (B.Shape == ELDDemoWorldBlockerShape::Cylinder)
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
    // Spec §11.4: the mini flag, locker and vending points are whatever the saved level holds (moved, copied or deleted by
    // the author), sorted by id; fewer than a kind's minimum is an authoring error, not a cue to restore old points.
    Out.Anchors.RemoveAll([](const FLDDemoWorldAnchor& Anchor) { return LDDemoWorld::OptionalPrefix(Anchor.Kind) != nullptr; });
    TArray<FString> OptionalIds;
    for (const auto& Pair : Points)
    {
        ELDDemoWorldAnchorKind Kind;
        if (LDDemoWorld::OptionalKindOf(Pair.Key, Kind)) { OptionalIds.Add(Pair.Key); }
    }
    OptionalIds.Sort();
    int32 OptionalCounts[3] = {};
    for (const FString& Id : OptionalIds)
    {
        auto& Anchor = Out.Anchors.AddDefaulted_GetRef();
        Anchor.Id = Id;
        LDDemoWorld::OptionalKindOf(Id, Anchor.Kind);
        Anchor.SurfaceId = Base.Surfaces[0].Id;
        ++OptionalCounts[Anchor.Kind == ELDDemoWorldAnchorKind::MiniFlag ? 0 : Anchor.Kind == ELDDemoWorldAnchorKind::Locker ? 1 : 2];
    }
    // The definition keeps its anchors in ordinal id order; the projection matches it.
    Out.Anchors.Sort([](const FLDDemoWorldAnchor& A, const FLDDemoWorldAnchor& B) { return A.Id.Compare(B.Id, ESearchCase::CaseSensitive) < 0; });
    // A kind the definition declares (the Demo world) needs its minimum; a level whose definition declares none of a kind
    // (a test or blockout scene) is not held to it.
    const auto Declared = [&Base](ELDDemoWorldAnchorKind Kind)
    { return Base.Anchors.ContainsByPredicate([Kind](const FLDDemoWorldAnchor& A) { return A.Kind == Kind; }); };
    const struct { const TCHAR* Prefix; int32 Count; int32 Min; bool bDeclared; } Minimums[] = {
        {LDDemoWorld::MiniFlagPrefix(), OptionalCounts[0], LDDemoWorld::MinMiniFlags, Declared(ELDDemoWorldAnchorKind::MiniFlag)},
        {LDDemoWorld::LockerPrefix(), OptionalCounts[1], LDDemoWorld::MinLockers, Declared(ELDDemoWorldAnchorKind::Locker)},
        {LDDemoWorld::VendingPrefix(), OptionalCounts[2], LDDemoWorld::MinVending, Declared(ELDDemoWorldAnchorKind::Vending)}};
    for (const auto& Minimum : Minimums)
    {
        if (Minimum.bDeclared && Minimum.Count < Minimum.Min)
        {
            return Fail(FString::Printf(TEXT("The level holds %d %s* layout points; at least %d are required. Copy an existing %s* point (or tag a TargetPoint LDSceneAnchor:%s<n>) and save."),
                Minimum.Count, Minimum.Prefix, Minimum.Min, Minimum.Prefix, Minimum.Prefix));
        }
    }
    if (bForPlay)
    {
        Out.Anchors.RemoveAll([&](const auto& Anchor)
        {
            return (Anchor.Kind == ELDDemoWorldAnchorKind::Camp || Anchor.Kind == ELDDemoWorldAnchorKind::Airdrop)
                && !Points.Contains(Anchor.Id);
        });
        TArray<FString> InstanceIds;
        Points.GetKeys(InstanceIds);
        InstanceIds.Sort();
        for (const FString& Id : InstanceIds)
        {
            if (Out.Anchors.ContainsByPredicate([&](const auto& A) { return A.Id == Id; })) { continue; }
            const AActor* Instance = Points[Id];
            const FString PrototypeId = Value(*Instance, TEXT("LDScenePrototypeAnchor:"));
            if (PrototypeId.IsEmpty()) { continue; }
            const auto* Prototype = Base.Anchors.FindByPredicate([&](const auto& A) { return A.Id == PrototypeId; });
            if (!Prototype || (Prototype->Kind != ELDDemoWorldAnchorKind::Camp && Prototype->Kind != ELDDemoWorldAnchorKind::Airdrop))
            { return Fail(TEXT("Scene instance has an unsupported anchor prototype: ") + Id); }
            auto& Anchor = Out.Anchors.Add_GetRef(*Prototype);
            Anchor.Id = Id;
            Anchor.PrototypeId = PrototypeId;
            for (int32 Slot=0; Slot<Anchor.Members.Num(); ++Slot)
            { Anchor.Members[Slot].Id = FString::Printf(TEXT("%s_%d"), *Id, Slot); }
        }
    }
    for (auto& Anchor : Out.Anchors)
    {
        const AActor* Actor = Points.FindRef(Anchor.Id);
        if (!Actor) { return Fail(TEXT("Missing saved layout point: ") + Anchor.Id); }
        FVector Position = Actor->GetActorLocation();
        if (Cast<APlayerStart>(Actor)) { Position.Z -= GetDefault<ALDCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(); }
        Anchor.PositionCm = Point(Position);
        if (bForPlay && Anchor.Kind == ELDDemoWorldAnchorKind::Camp)
        {
            const auto* Centre=Cast<ALDLayoutPoint>(Actor);
            const FString Prototype=Value(*Actor,TEXT("LDScenePrototypeAnchor:"));
            if (!Prototype.IsEmpty())
            {
                if (!Base.Anchors.ContainsByPredicate([&](const auto& Row) { return Row.Id==Prototype && Row.Kind==ELDDemoWorldAnchorKind::Camp; }))
                { return Fail(TEXT("Camp centre has an unknown approved prototype: ")+Actor->GetPathName()); }
                Anchor.PrototypeId=Prototype;
            }
            if (Centre && Centre->bUseAuthoredCampMembers)
            {
                Anchor.Members.Reset();
                TMap<int32,const ALDLayoutPoint*> Slots;
                for (const AActor* Child : World.PersistentLevel->Actors)
                {
                    if (!Child || Child->GetAttachParentActor()!=Centre || Value(*Child,TEXT("LDSceneMember:")).IsEmpty()) { continue; }
                    const auto* Member=Cast<ALDLayoutPoint>(Child);
                    const FString Text=Value(*Child,TEXT("LDSceneMemberSlot:"));
                    const int32 Slot=FCString::Atoi(*Text);
                    if (!Member || Slot<0 || FString::FromInt(Slot)!=Text || Slots.Contains(Slot))
                    { return Fail(TEXT("Authored camp has an invalid or repeated member slot: ")+Child->GetPathName()); }
                    Slots.Add(Slot,Member);
                }
                if (Slots.IsEmpty()) { return Fail(TEXT("Authored camp has no attached members: ")+Actor->GetPathName()); }
                for (int32 Slot=0; Slot<Slots.Num(); ++Slot)
                {
                    const ALDLayoutPoint* const* Home=Slots.Find(Slot);
                    if (!Home) { return Fail(TEXT("Authored camp slots must be contiguous from zero: ")+Actor->GetPathName()); }
                    auto& Member=Anchor.Members.AddDefaulted_GetRef();
                    Member.Id=FString::Printf(TEXT("%s_%d"),*Anchor.Id,Slot);
                    Member.PrototypeId=(*Home)->CampMemberPrototypeId.IsEmpty()
                        ? Value(*Centre,TEXT("LDScenePrototypeAnchor:")) : (*Home)->CampMemberPrototypeId;
                }
            }
        }
        for (auto& Member : Anchor.Members)
        {
            const AActor* Home = Points.FindRef(Member.Id);
            if (!Home) { return Fail(TEXT("Missing camp member home point: ") + Member.Id); }
            Member.PositionCm = Point(Home->GetActorLocation());
        }
    }
    TArray<FString> SharedIds;
    SharedTraining.GetKeys(SharedIds);
    SharedIds.Sort();
    for (const FString& Id : SharedIds)
    {
        auto& Anchor = Out.Anchors.AddDefaulted_GetRef();
        Anchor.Id = Id;
        Anchor.Kind = ELDDemoWorldAnchorKind::TrainingDummy;
        Anchor.SurfaceId = Base.Surfaces[0].Id;
        Anchor.PositionCm = Point(SharedTraining[Id]->GetActorLocation());
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

bool LDStaticScene::ProjectRuntimeGeometry(const UWorld& World, const FLDDemoWorldGeometry& Base,
    FLDDemoWorldGeometry& Out, FString& Error)
{
    return ProjectGeometry(World, Base, Out, Error, true);
}

bool LDStaticScene::ValidateGeometry(const UWorld& World, const FLDDemoWorldGeometry& Base, FString& Error)
{
    FLDDemoWorldGeometry Projected;
    if (!ProjectGeometry(World, Base, Projected, Error)) { return false; }
    if (!FLDDemoWorldGeometry::StaticStruct()->CompareScriptStruct(&Base, &Projected, PPF_None))
    { Error = TEXT("Saved layout differs from the export baseline. Run SyncDemoMap.ps1 to refresh the authored source snapshot."); return false; }
    return true;
}
