#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class FSkeletalMeshLODRenderData;
class FSkinWeightVertexBuffer;
class USkeletalMesh;

namespace LDModelPicking
{
    // Approved control feel: a pointer this many screen pixels outside a unit's pick volume still selects it.
    constexpr double PickTolerancePx = 12.;
    // Narrow phase in the displayed mesh's space; empty space inside its bounds is never a hit.
    LOCKDOWN_API bool Trace(TConstArrayView<FVector3f> Vertices, TConstArrayView<uint32> Indices,
        const FVector& Start, const FVector& End, double& OutFraction);
    // Lenient fallback when no model surface is under the pointer: the ray passes within AngleTolerance (radians, the
    // screen tolerance seen from the ray start) of the upright pick volume, the segment Foot..Top swollen by RadiusCm.
    // OutAngle is how far outside the volume the ray passes (0 when it goes through it) and OutDistance the ray length
    // to its closest approach; only that approach within MaxDistance counts.
    LOCKDOWN_API bool NearVolume(const FVector& Start, const FVector& Direction, double MaxDistance, const FVector& Foot,
        const FVector& Top, double RadiusCm, double AngleTolerance, double& OutAngle, double& OutDistance);

    // One mesh bone's share of a vertex, as the renderer's CPU skinning applies it (weights are not renormalised).
    struct FInfluence
    {
        int32 Bone = 0;
        float Weight = 0.f;
    };
    // Every leaf vertex the bone moves lies within Radius of Point in the reference pose.
    struct FAnchor
    {
        int32 Bone = 0;
        FVector3f Point = FVector3f::ZeroVector;
        float Radius = 0.f;
    };
    // A compact patch of one material's triangles, mostly moved by one bone. Its posed bounds come from its bone
    // anchors, so a ray skins and tests only the patches it can reach.
    struct FSkinnedLeaf
    {
        int32 FirstVertex = 0, NumVertices = 0; // into FSkinnedModel::Rest, Source and InfluenceStart
        int32 FirstIndex = 0, NumIndices = 0;   // into FSkinnedModel::Indices, relative to FirstVertex
        int32 FirstAnchor = 0, NumAnchors = 0;  // into FSkinnedModel::Anchors
        int32 Material = 0;
        // Largest gap between a vertex's summed weights and 1; the posed bounds widen by that share of their reach.
        float WeightDeficit = 0.f;
    };
    // CPU picking model of one skeletal mesh LOD, built once and shared by every body showing it.
    struct FSkinnedModel
    {
        TArray<FSkinnedLeaf> Leaves;
        TArray<FVector3f> Rest;        // reference-pose position of each leaf vertex
        TArray<uint32> Source;         // the mesh vertex each leaf vertex copies
        TArray<uint32> InfluenceStart; // per leaf vertex, plus the end
        TArray<FInfluence> Influences;
        TArray<uint32> Indices;
        TArray<FAnchor> Anchors;
        int32 RequiredBones = 0;       // the pose's RefToLocal must cover every bone below this
        int32 DroppedTriangles = 0;    // invalid indices or no influenced vertices (all skin to zero)
    };
    // One body's pose of a model: its bone matrices, each leaf's posed bounds and the leaves skinned so far.
    struct FSkinnedPose
    {
        TArray<FMatrix44f> RefToLocal;
        TArray<FBox3f> LeafBounds;
        TArray<int32> LeafSlot; // a skinned leaf's first vertex in Vertices; INDEX_NONE until it is skinned
        TArray<FVector3f> Vertices;
    };
    // Groups the triangles by material and dominant bone and splits each group into compact leaves. Influences
    // [InfluenceStart[V], InfluenceStart[V + 1]) move vertex V; TriangleMaterials has one entry per triangle.
    LOCKDOWN_API TSharedRef<const FSkinnedModel> BuildSkinnedModel(TConstArrayView<FVector3f> Positions,
        TConstArrayView<uint32> InfluenceStart, TConstArrayView<FInfluence> Influences, TConstArrayView<uint32> Indices,
        TConstArrayView<int32> TriangleMaterials);
    // The shared model of a skeletal mesh LOD, built on first use from its CPU copy of the render data, enabled
    // sections only. Game thread only; null when the LOD keeps no CPU copy.
    LOCKDOWN_API TSharedPtr<const FSkinnedModel> SharedSkinnedModel(const USkeletalMesh& Mesh, int32 LODIndex,
        const FSkeletalMeshLODRenderData& LOD, const FSkinWeightVertexBuffer& Weights);
    // Starts a new pose from Pose.RefToLocal: computes every leaf's posed bounds and forgets the skinned leaves. The
    // bounds always contain the leaf's skinned vertices. False when the matrices do not cover the model's bones.
    LOCKDOWN_API bool PoseBounds(const FSkinnedModel& Model, FSkinnedPose& Pose);
    // The leaf's vertices in this pose, skinned on first request as the renderer's CPU skinning does.
    LOCKDOWN_API TConstArrayView<FVector3f> SkinLeaf(const FSkinnedModel& Model, FSkinnedPose& Pose, int32 Leaf);
    // Trace against the posed model in its mesh space. Only leaves whose material is Shown and whose posed bounds the
    // segment reaches are skinned and tested, nearest bounds first, until no closer surface remains possible.
    LOCKDOWN_API bool TraceSkinned(const FSkinnedModel& Model, FSkinnedPose& Pose, const FVector& Start, const FVector& End,
        TFunctionRef<bool(int32 Material)> Shown, double& OutFraction);
}
