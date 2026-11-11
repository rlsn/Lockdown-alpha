#include "Character/LDModelPicking.h"
#include "Algo/Sort.h"
#include "BoneWeights.h"
#include "Core/LDLog.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/PlatformTime.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "UObject/ObjectKey.h"

namespace LDModelPickingDetail
{
    using namespace LDModelPicking;
    // Triangles per leaf: few enough that a hit skins little, enough that posing every leaf's bounds stays cheap.
    constexpr int32 MaxLeafTriangles = 64;
    // Float rounding of posed positions, far below a pointer pixel.
    constexpr float PoseSlackCm = .1f;

    struct FBuilder
    {
        TConstArrayView<FVector3f> Positions;
        TConstArrayView<uint32> InfluenceStart;
        TConstArrayView<FInfluence> Influences;
        TConstArrayView<uint32> Indices;
        FSkinnedModel& Model;
        TArray<FVector3f> Centroids;

        TConstArrayView<FInfluence> Of(uint32 Vertex) const
        {
            return Influences.Slice(InfluenceStart[Vertex], InfluenceStart[Vertex + 1] - InfluenceStart[Vertex]);
        }

        // Halves the triangles at the median centroid of their longest extent until each part is one leaf.
        void Split(TArrayView<int32> Triangles, int32 Material)
        {
            if (Triangles.Num() <= MaxLeafTriangles) { Emit(Triangles, Material); return; }
            FBox3f Box(ForceInit);
            for (const int32 Triangle : Triangles) { Box += Centroids[Triangle]; }
            const FVector3f Size = Box.GetSize();
            const int32 Axis = Size.X >= Size.Y && Size.X >= Size.Z ? 0 : Size.Y >= Size.Z ? 1 : 2;
            Algo::Sort(Triangles, [this, Axis](int32 A, int32 B)
            {
                return Centroids[A][Axis] < Centroids[B][Axis] || (Centroids[A][Axis] == Centroids[B][Axis] && A < B);
            });
            const int32 Half = Triangles.Num() / 2;
            Split(Triangles.Slice(0, Half), Material);
            Split(Triangles.Slice(Half, Triangles.Num() - Half), Material);
        }

        void Emit(TConstArrayView<int32> Triangles, int32 Material)
        {
            FSkinnedLeaf Leaf;
            Leaf.Material = Material;
            Leaf.FirstVertex = Model.Rest.Num();
            Leaf.FirstIndex = Model.Indices.Num();
            Leaf.FirstAnchor = Model.Anchors.Num();
            TMap<uint32, uint32> Local;
            for (const int32 Triangle : Triangles)
            {
                for (int32 Corner = 0; Corner < 3; ++Corner)
                {
                    const uint32 Vertex = Indices[Triangle * 3 + Corner];
                    if (const uint32* Found = Local.Find(Vertex)) { Model.Indices.Add(*Found); continue; }
                    const uint32 Index = uint32(Model.Rest.Num() - Leaf.FirstVertex);
                    Local.Add(Vertex, Index);
                    Model.Indices.Add(Index);
                    Model.Rest.Add(Positions[Vertex]);
                    Model.Source.Add(Vertex);
                    Model.InfluenceStart.Add(uint32(Model.Influences.Num()));
                    float Sum = 0.f;
                    for (const FInfluence& Influence : Of(Vertex))
                    {
                        if (Influence.Weight > 0.f) { Model.Influences.Add(Influence); Sum += Influence.Weight; }
                    }
                    Leaf.WeightDeficit = FMath::Max(Leaf.WeightDeficit, FMath::Abs(1.f - Sum));
                }
            }
            Leaf.NumVertices = Model.Rest.Num() - Leaf.FirstVertex;
            Leaf.NumIndices = Model.Indices.Num() - Leaf.FirstIndex;
            const auto Moving = [this](int32 Vertex)
            {
                const uint32 End = Vertex + 1 < Model.InfluenceStart.Num() ? Model.InfluenceStart[Vertex + 1] : uint32(Model.Influences.Num());
                return MakeArrayView(Model.Influences).Slice(Model.InfluenceStart[Vertex], End - Model.InfluenceStart[Vertex]);
            };
            // One anchor per bone: the centre of the vertices it moves, reaching the farthest of them.
            TMap<int32, FBox3f> Reach;
            for (int32 Vertex = Leaf.FirstVertex; Vertex < Model.Rest.Num(); ++Vertex)
            {
                for (const FInfluence& Influence : Moving(Vertex)) { Reach.FindOrAdd(Influence.Bone, FBox3f(ForceInit)) += Model.Rest[Vertex]; }
            }
            Reach.KeySort(TLess<int32>());
            for (const TPair<int32, FBox3f>& Bone : Reach)
            {
                Model.Anchors.Add({Bone.Key, Bone.Value.GetCenter(), 0.f});
                Model.RequiredBones = FMath::Max(Model.RequiredBones, Bone.Key + 1);
            }
            Leaf.NumAnchors = Model.Anchors.Num() - Leaf.FirstAnchor;
            const TArrayView<FAnchor> Anchors = MakeArrayView(Model.Anchors).Slice(Leaf.FirstAnchor, Leaf.NumAnchors);
            for (int32 Vertex = Leaf.FirstVertex; Vertex < Model.Rest.Num(); ++Vertex)
            {
                for (const FInfluence& Influence : Moving(Vertex))
                {
                    FAnchor* Anchor = Anchors.FindByPredicate([&Influence](const FAnchor& Each) { return Each.Bone == Influence.Bone; });
                    Anchor->Radius = FMath::Max(Anchor->Radius, FVector3f::Dist(Model.Rest[Vertex], Anchor->Point));
                }
            }
            Model.Leaves.Add(Leaf);
        }
    };

    // Where the segment Start..Start + Direction first enters the box, as a fraction of it.
    bool SegmentEntry(const FBox3f& Box, const FVector& Start, const FVector& Direction, double& OutEntry)
    {
        if (!Box.IsValid) { return false; }
        double Near = 0., Far = 1.;
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            const double Min = Box.Min[Axis], Max = Box.Max[Axis], From = Start[Axis], Step = Direction[Axis];
            if (Step == 0.)
            {
                if (From < Min || From > Max) { return false; }
                continue;
            }
            double A = (Min - From) / Step, B = (Max - From) / Step;
            if (A > B) { Swap(A, B); }
            Near = FMath::Max(Near, A);
            Far = FMath::Min(Far, B);
            if (Near > Far) { return false; }
        }
        OutEntry = Near;
        return true;
    }
}

TSharedRef<const LDModelPicking::FSkinnedModel> LDModelPicking::BuildSkinnedModel(TConstArrayView<FVector3f> Positions,
    TConstArrayView<uint32> InfluenceStart, TConstArrayView<FInfluence> Influences, TConstArrayView<uint32> Indices,
    TConstArrayView<int32> TriangleMaterials)
{
    using namespace LDModelPickingDetail;
    const TSharedRef<FSkinnedModel> Model = MakeShared<FSkinnedModel>();
    const int32 NumVertices = Positions.Num(), NumTriangles = Indices.Num() / 3;
    bool bValid = InfluenceStart.Num() == NumVertices + 1 && Indices.Num() % 3 == 0 && TriangleMaterials.Num() == NumTriangles
        && InfluenceStart[0] == 0 && InfluenceStart.Last() <= uint32(Influences.Num());
    TBitArray<> Moved(false, NumVertices);
    for (int32 Vertex = 0; bValid && Vertex < NumVertices; ++Vertex)
    {
        bValid = InfluenceStart[Vertex] <= InfluenceStart[Vertex + 1] && InfluenceStart[Vertex + 1] <= uint32(Influences.Num());
        for (uint32 Index = InfluenceStart[Vertex]; bValid && Index < InfluenceStart[Vertex + 1]; ++Index)
        {
            bValid = Influences[Index].Bone >= 0 && FMath::IsFinite(Influences[Index].Weight);
            if (Influences[Index].Weight > 0.f) { Moved[Vertex] = true; }
        }
    }
    if (!ensureMsgf(bValid, TEXT("Skinned picking model input is inconsistent; nothing of it is selectable."))) { return Model; }
    FBuilder Builder{Positions, InfluenceStart, Influences, Indices, *Model};
    Builder.Centroids.SetNumZeroed(NumTriangles);
    // Triangles grouped by material and the bone that moves them most, so a leaf follows one limb.
    TMap<int64, TArray<int32>> Groups;
    for (int32 Triangle = 0; Triangle < NumTriangles; ++Triangle)
    {
        const uint32* Corners = &Indices[Triangle * 3];
        if (Corners[0] >= uint32(NumVertices) || Corners[1] >= uint32(NumVertices) || Corners[2] >= uint32(NumVertices)
            || !Moved[Corners[0]] || !Moved[Corners[1]] || !Moved[Corners[2]])
        {
            ++Model->DroppedTriangles;
            continue;
        }
        Builder.Centroids[Triangle] = (Positions[Corners[0]] + Positions[Corners[1]] + Positions[Corners[2]]) / 3.f;
        TArray<FInfluence, TInlineAllocator<24>> Share;
        for (int32 Corner = 0; Corner < 3; ++Corner)
        {
            for (const FInfluence& Influence : Builder.Of(Corners[Corner]))
            {
                if (Influence.Weight <= 0.f) { continue; }
                FInfluence* Same = Share.FindByPredicate([&Influence](const FInfluence& Each) { return Each.Bone == Influence.Bone; });
                if (Same) { Same->Weight += Influence.Weight; }
                else { Share.Add(Influence); }
            }
        }
        FInfluence Dominant = Share[0];
        for (const FInfluence& Each : Share)
        {
            if (Each.Weight > Dominant.Weight || (Each.Weight == Dominant.Weight && Each.Bone < Dominant.Bone)) { Dominant = Each; }
        }
        Groups.FindOrAdd((int64(TriangleMaterials[Triangle]) << 32) | uint32(Dominant.Bone)).Add(Triangle);
    }
    Groups.KeySort(TLess<int64>());
    for (TPair<int64, TArray<int32>>& Group : Groups) { Builder.Split(Group.Value, int32(Group.Key >> 32)); }
    Model->InfluenceStart.Add(uint32(Model->Influences.Num()));
    return Model;
}

TSharedPtr<const LDModelPicking::FSkinnedModel> LDModelPicking::SharedSkinnedModel(const USkeletalMesh& Mesh, int32 LODIndex,
    const FSkeletalMeshLODRenderData& LOD, const FSkinWeightVertexBuffer& Weights)
{
    check(IsInGameThread());
    struct FEntry
    {
        TObjectKey<USkeletalMesh> Mesh;
        int32 LODIndex = 0;
        const FSkeletalMeshLODRenderData* LOD = nullptr;
        const FSkinWeightVertexBuffer* Weights = nullptr;
        TSharedPtr<const FSkinnedModel> Model;
    };
    static TArray<FEntry> Cache;
    const TObjectKey<USkeletalMesh> Key(&Mesh);
    for (const FEntry& Entry : Cache)
    {
        if (Entry.Mesh == Key && Entry.LODIndex == LODIndex && Entry.LOD == &LOD && Entry.Weights == &Weights) { return Entry.Model; }
    }
    const FPositionVertexBuffer& PositionBuffer = LOD.StaticVertexBuffers.PositionVertexBuffer;
    const FRawStaticIndexBuffer16or32Interface* IndexBuffer = LOD.MultiSizeIndexContainer.GetIndexBuffer();
    const uint32 NumVertices = PositionBuffer.GetNumVertices();
    if (!PositionBuffer.GetAllowCPUAccess() || !Weights.GetNeedsCPUAccess() || !IndexBuffer || !IndexBuffer->GetNeedsCPUAccess()
        || Weights.GetNumVertices() != NumVertices) { return nullptr; }
    const double Began = FPlatformTime::Seconds();
    // Influences exactly as the renderer's CPU skinning reads them: each section's bone map, unnormalised weights.
    TArray<int32> SectionOf;
    SectionOf.Init(INDEX_NONE, NumVertices);
    for (int32 Section = 0; Section < LOD.RenderSections.Num(); ++Section)
    {
        const FSkelMeshRenderSection& Each = LOD.RenderSections[Section];
        for (uint32 Vertex = Each.BaseVertexIndex; Vertex < FMath::Min(Each.BaseVertexIndex + Each.NumVertices, NumVertices); ++Vertex)
        {
            SectionOf[Vertex] = Section;
        }
    }
    TArray<FVector3f> Positions;
    TArray<uint32> InfluenceStart;
    TArray<FInfluence> Influences;
    Positions.SetNumUninitialized(NumVertices);
    InfluenceStart.SetNumUninitialized(NumVertices + 1);
    const uint32 MaxInfluences = Weights.GetMaxBoneInfluences();
    for (uint32 Vertex = 0; Vertex < NumVertices; ++Vertex)
    {
        Positions[Vertex] = PositionBuffer.VertexPosition(Vertex);
        InfluenceStart[Vertex] = uint32(Influences.Num());
        if (SectionOf[Vertex] == INDEX_NONE) { continue; }
        const TArray<FBoneIndexType>& BoneMap = LOD.RenderSections[SectionOf[Vertex]].BoneMap;
        for (uint32 Influence = 0; Influence < MaxInfluences; ++Influence)
        {
            const uint16 Raw = Weights.GetBoneWeight(Vertex, Influence);
            const uint32 Local = Weights.GetBoneIndex(Vertex, Influence);
            if (Raw == 0 || !BoneMap.IsValidIndex(Local)) { continue; }
            Influences.Add({int32(BoneMap[Local]), float(Raw) * UE::AnimationCore::InvMaxRawBoneWeightFloat});
        }
    }
    InfluenceStart[NumVertices] = uint32(Influences.Num());
    TArray<uint32> Indices;
    TArray<int32> Materials;
    for (const FSkelMeshRenderSection& Section : LOD.RenderSections)
    {
        if (Section.bDisabled) { continue; }
        for (uint32 Triangle = 0; Triangle < Section.NumTriangles; ++Triangle)
        {
            for (uint32 Corner = 0; Corner < 3; ++Corner) { Indices.Add(IndexBuffer->Get(Section.BaseIndex + Triangle * 3 + Corner)); }
            Materials.Add(Section.MaterialIndex);
        }
    }
    const TSharedRef<const FSkinnedModel> Model = BuildSkinnedModel(Positions, InfluenceStart, Influences, Indices, Materials);
    UE_LOG(LogLDContent, Log, TEXT("LD_PICK_MODEL mesh=%s lod=%d triangles=%d leaves=%d dropped=%d ms=%.1f"),
        *LDLog::Quote(Mesh.GetName()), LODIndex, Materials.Num(), Model->Leaves.Num(), Model->DroppedTriangles,
        (FPlatformTime::Seconds() - Began) * 1000.);
    if (Model->DroppedTriangles > 0)
    {
        UE_LOG(LogLDContent, Warning, TEXT("LD_PICK_MODEL_DROPPED mesh=%s lod=%d triangles=%d reason=unweighted_vertex"),
            *LDLog::Quote(Mesh.GetName()), LODIndex, Model->DroppedTriangles);
    }
    Cache.RemoveAll([&Key, LODIndex](const FEntry& Entry)
        { return !Entry.Mesh.ResolveObjectPtr() || (Entry.Mesh == Key && Entry.LODIndex == LODIndex); });
    Cache.Add({Key, LODIndex, &LOD, &Weights, Model});
    return Model;
}

bool LDModelPicking::PoseBounds(const FSkinnedModel& Model, FSkinnedPose& Pose)
{
    using namespace LDModelPickingDetail;
    Pose.LeafBounds.Init(FBox3f(ForceInit), Model.Leaves.Num());
    Pose.LeafSlot.Init(INDEX_NONE, Model.Leaves.Num());
    Pose.Vertices.Reset();
    if (Pose.RefToLocal.Num() < Model.RequiredBones) { return false; }
    // How far each bone matrix can stretch the distance between two points: the root of the largest Gershgorin row sum
    // of A * A^T for its linear part A. Exact for a rotation with axis scales, an upper bound under shear.
    TArray<float, TInlineAllocator<256>> Stretch;
    Stretch.SetNumUninitialized(Model.RequiredBones);
    for (int32 Bone = 0; Bone < Model.RequiredBones; ++Bone)
    {
        const FMatrix44f& Matrix = Pose.RefToLocal[Bone];
        const FVector3f Rows[3] = {
            {Matrix.M[0][0], Matrix.M[0][1], Matrix.M[0][2]},
            {Matrix.M[1][0], Matrix.M[1][1], Matrix.M[1][2]},
            {Matrix.M[2][0], Matrix.M[2][1], Matrix.M[2][2]}};
        float Largest = 0.f;
        for (const FVector3f& Row : Rows)
        {
            Largest = FMath::Max(Largest, FMath::Abs(Row | Rows[0]) + FMath::Abs(Row | Rows[1]) + FMath::Abs(Row | Rows[2]));
        }
        Stretch[Bone] = FMath::Sqrt(Largest);
    }
    for (int32 Index = 0; Index < Model.Leaves.Num(); ++Index)
    {
        // A skinned vertex blends its bones' rigid copies of it; each copy lies within its bone's stretched anchor
        // radius of the posed anchor, so the blend stays inside the box around all posed anchors.
        const FSkinnedLeaf& Leaf = Model.Leaves[Index];
        FBox3f Box(ForceInit);
        float Margin = 0.f;
        for (const FAnchor& Anchor : MakeArrayView(Model.Anchors).Slice(Leaf.FirstAnchor, Leaf.NumAnchors))
        {
            Box += FVector3f(Pose.RefToLocal[Anchor.Bone].TransformPosition(Anchor.Point));
            Margin = FMath::Max(Margin, Stretch[Anchor.Bone] * Anchor.Radius);
        }
        Box = Box.ExpandBy(Margin);
        if (Leaf.WeightDeficit > 0.f)
        {
            const FVector3f Far = Box.Min.GetAbs().ComponentMax(Box.Max.GetAbs());
            Box = Box.ExpandBy(Leaf.WeightDeficit * Far.Size());
        }
        Pose.LeafBounds[Index] = Box.ExpandBy(PoseSlackCm);
    }
    return true;
}

TConstArrayView<FVector3f> LDModelPicking::SkinLeaf(const FSkinnedModel& Model, FSkinnedPose& Pose, int32 Leaf)
{
    if (!Model.Leaves.IsValidIndex(Leaf) || Pose.LeafSlot.Num() != Model.Leaves.Num() || Pose.RefToLocal.Num() < Model.RequiredBones)
    {
        return {};
    }
    const FSkinnedLeaf& Patch = Model.Leaves[Leaf];
    int32& Slot = Pose.LeafSlot[Leaf];
    if (Slot == INDEX_NONE)
    {
        Slot = Pose.Vertices.Num();
        Pose.Vertices.AddUninitialized(Patch.NumVertices);
        FVector3f* Out = Pose.Vertices.GetData() + Slot;
        for (int32 Vertex = Patch.FirstVertex; Vertex < Patch.FirstVertex + Patch.NumVertices; ++Vertex)
        {
            // The same sum, in the same influence order, as USkinnedMeshComponent::ComputeSkinnedPositions.
            FVector3f Skinned(0, 0, 0);
            for (uint32 Index = Model.InfluenceStart[Vertex]; Index < Model.InfluenceStart[Vertex + 1]; ++Index)
            {
                const FInfluence& Influence = Model.Influences[Index];
                Skinned += FVector3f(Pose.RefToLocal[Influence.Bone].TransformPosition(Model.Rest[Vertex])) * Influence.Weight;
            }
            *Out++ = Skinned;
        }
    }
    return MakeArrayView(Pose.Vertices).Slice(Slot, Patch.NumVertices);
}

bool LDModelPicking::TraceSkinned(const FSkinnedModel& Model, FSkinnedPose& Pose, const FVector& Start, const FVector& End,
    TFunctionRef<bool(int32 Material)> Shown, double& OutFraction)
{
    using namespace LDModelPickingDetail;
    OutFraction = 1.;
    const FVector Direction = End - Start;
    if (Start.ContainsNaN() || End.ContainsNaN() || Direction.IsNearlyZero() || Pose.LeafBounds.Num() != Model.Leaves.Num())
    {
        return false;
    }
    TArray<TPair<double, int32>, TInlineAllocator<64>> Reached;
    for (int32 Leaf = 0; Leaf < Model.Leaves.Num(); ++Leaf)
    {
        double Entry = 0.;
        if (SegmentEntry(Pose.LeafBounds[Leaf], Start, Direction, Entry) && Shown(Model.Leaves[Leaf].Material)) { Reached.Add({Entry, Leaf}); }
    }
    Reached.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B)
        { return A.Key < B.Key || (A.Key == B.Key && A.Value < B.Value); });
    bool bHit = false;
    for (const TPair<double, int32>& Each : Reached)
    {
        // Every surface of a leaf lies inside its bounds, so none past the nearest hit can be closer.
        if (bHit && Each.Key > OutFraction) { break; }
        const FSkinnedLeaf& Leaf = Model.Leaves[Each.Value];
        double Fraction = 1.;
        if (Trace(SkinLeaf(Model, Pose, Each.Value), MakeArrayView(Model.Indices).Slice(Leaf.FirstIndex, Leaf.NumIndices),
            Start, End, Fraction) && Fraction <= OutFraction)
        {
            OutFraction = Fraction;
            bHit = true;
        }
    }
    return bHit;
}

bool LDModelPicking::NearVolume(const FVector& Start, const FVector& Direction, double MaxDistance, const FVector& Foot,
    const FVector& Top, double RadiusCm, double AngleTolerance, double& OutAngle, double& OutDistance)
{
    OutAngle = 0.;
    OutDistance = 0.;
    const FVector Ray = Direction.GetSafeNormal();
    if (Start.ContainsNaN() || Ray.IsNearlyZero() || Foot.ContainsNaN() || Top.ContainsNaN() || !FMath::IsFinite(RadiusCm)
        || RadiusCm < 0 || !(AngleTolerance >= 0) || !FMath::IsFinite(MaxDistance) || !(MaxDistance > 0)) { return false; }
    const double SearchDistance = FMath::Max(MaxDistance, FMath::Max(FVector::Dist(Start, Foot), FVector::Dist(Start, Top)));
    FVector OnRay, OnAxis;
    FMath::SegmentDistToSegmentSafe(Start, Start + Ray * SearchDistance, Foot, Top, OnRay, OnAxis);
    const double Along = FVector::DotProduct(OnRay - Start, Ray);
    if (Along <= UE_KINDA_SMALL_NUMBER || Along > MaxDistance) { return false; }
    const double Outside = FMath::Max(0., FVector::Dist(OnRay, OnAxis) - RadiusCm);
    OutAngle = FMath::Atan2(Outside, Along);
    OutDistance = Along;
    return OutAngle <= AngleTolerance;
}

bool LDModelPicking::Trace(TConstArrayView<FVector3f> Vertices, TConstArrayView<uint32> Indices,
    const FVector& Start, const FVector& End, double& OutFraction)
{
    OutFraction = 1.;
    if (Start.ContainsNaN() || End.ContainsNaN() || Indices.Num() % 3 != 0) { return false; }
    const FVector Direction = End - Start;
    if (Direction.IsNearlyZero()) { return false; }
    bool bHit = false;
    for (int32 Index = 0; Index < Indices.Num(); Index += 3)
    {
        const uint32 A = Indices[Index], B = Indices[Index + 1], C = Indices[Index + 2];
        if (A >= uint32(Vertices.Num()) || B >= uint32(Vertices.Num()) || C >= uint32(Vertices.Num())) { continue; }
        const FVector V0(Vertices[A]), Edge1 = FVector(Vertices[B]) - V0, Edge2 = FVector(Vertices[C]) - V0;
        const FVector P = FVector::CrossProduct(Direction, Edge2);
        const double Determinant = FVector::DotProduct(Edge1, P);
        if (FMath::Abs(Determinant) <= UE_DOUBLE_SMALL_NUMBER) { continue; }
        const double Inverse = 1. / Determinant;
        const FVector T = Start - V0;
        const double U = FVector::DotProduct(T, P) * Inverse;
        if (U < -UE_DOUBLE_SMALL_NUMBER || U > 1. + UE_DOUBLE_SMALL_NUMBER) { continue; }
        const FVector Q = FVector::CrossProduct(T, Edge1);
        const double V = FVector::DotProduct(Direction, Q) * Inverse;
        if (V < -UE_DOUBLE_SMALL_NUMBER || U + V > 1. + UE_DOUBLE_SMALL_NUMBER) { continue; }
        const double Fraction = FVector::DotProduct(Edge2, Q) * Inverse;
        if (Fraction >= 0. && Fraction <= OutFraction)
        {
            OutFraction = Fraction;
            bHit = true;
        }
    }
    return bHit;
}
