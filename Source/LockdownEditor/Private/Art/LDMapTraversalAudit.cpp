#include "LDSceneAuthoringLibrary.h"
#include "Art/LDTraversalTopology.h"
#include "Character/LDCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/LDDemoEntry.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MeshDescription.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "AI/Navigation/NavQueryFilter.h"
#include "NavMesh/RecastNavMesh.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Region/LDMapRegistry.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshAttributes.h"
#include "Vision/LDVisionDirector.h"
#include "World/LDNativeGround.h"
#include "World/LDSceneTags.h"
#include "World/LDSpawnPlacement.h"

namespace
{
using FRows = TArray<TSharedPtr<FJsonValue>>;
TArray<TSharedPtr<FJsonValue>> Coordinates(const FVector& At)
{
    return {MakeShared<FJsonValueNumber>(At.X),MakeShared<FJsonValueNumber>(At.Y),MakeShared<FJsonValueNumber>(At.Z)};
}
using FAuditNode=LDTraversalTopology::FNode;
struct FAuditTriangle { FVector A, B, C; };
bool HasClearPortalCrossing(ULDMapRegistry& Registry, const UWorld& World, const FCollisionQueryParams& GroundQuery,
    const FNavigationPortalEdge& Edge, const FVector& FromPolygon, const FVector& ToPolygon,
    const FCollisionShape& Capsule, FName Surface, double StepHeight)
{
    const double Approach = Capsule.GetCapsuleRadius() * .5;
    // Stay inside both convex polygons. A fixed perpendicular offset can overshoot a short corner polygon.
    const auto ApproachInside = [Approach](const FVector& Portal, const FVector& Polygon)
    {
        const double Distance = FVector::Dist2D(Portal, Polygon);
        return Distance <= Approach ? Polygon : FMath::Lerp(Portal, Polygon, Approach / Distance);
    };

    const auto StandingAt = [&](const FVector& At, FVector& Origin)
    {
        double SupportZ;
        int32 Tier;
        FHitResult Support;
        if (!Registry.GetTerrain().GroundNear(At + FVector(0., 0., StepHeight), SupportZ, Tier, true, &Support)
            || FMath::Abs(SupportZ - At.Z) > StepHeight)
        {
            return false;
        }
        FLDLegalGround Ground;
        ELDWorldQueryFailure Failure;
        FString Error;
        if (!Registry.QueryGround(Registry.Revision(), Support.ImpactPoint, Capsule, Surface, Ground, Failure, Error))
        {
            return false;
        }
        Origin = LDSpawnPlacement::StandingOrigin(Ground);
        return !LDNativeGround::Overlaps(World, Origin, Capsule, GroundQuery);
    };
    for (const double Alpha : {.5, .25, .75})
    {
        const FVector Centre = FMath::Lerp(Edge.Left, Edge.Right, Alpha);
        FVector Start, Middle, End;
        FHitResult Hit;
        if (StandingAt(ApproachInside(Centre, FromPolygon), Start) && StandingAt(Centre, Middle)
            && StandingAt(ApproachInside(Centre, ToPolygon), End)
            && !LDNativeGround::Sweep(World, Hit, Start, Middle, Capsule, GroundQuery)
            && !LDNativeGround::Sweep(World, Hit, Middle, End, Capsule, GroundQuery))
        {
            return true;
        }
    }
    return false;
}

FRows AnalysePockets(const TArray<FAuditNode>& Nodes,const TMap<NavNodeRef,int32>& Indices,
    const TSet<int32>& Field,int32 Hub,ARecastNavMesh& Recast,UNavigationSystemV1& Nav,UWorld& World,
    double CapsuleRadius,ULDMapRegistry& Registry,const FCollisionShape& Capsule,FName Surface,
    FRows& Errors,int32& CellCount,int32& DualExitCount,int32& RejectedGoals)
{
    using namespace LDTraversalTopology;
    FCells Topology=ConvexCells(Nodes,Field);auto& Cells=Topology.Cells;
    CellCount=Topology.Active.Num();DualExitCount=0;RejectedGoals=0;
    TArray<int32> Discovery,Low,Parent,Last,Order;
    Discovery.Init(-1,Nodes.Num());Low.Init(-1,Nodes.Num());Parent.Init(-1,Nodes.Num());Last.Init(-1,Nodes.Num());
    TArray<TPair<int32,int32>> Cuts;
    TFunction<void(int32)> Visit=[&](int32 At)
    {
        Discovery[At]=Low[At]=Order.Num();Order.Add(At);TArray<int32> Children;
        TArray<int32> Neighbours=Cells[At].Neighbours.Array();Neighbours.Sort();
        for(const int32 N:Neighbours)
        {
            if(Discovery[N]<0)
            {
                Parent[N]=At;Children.Add(N);Visit(N);Low[At]=FMath::Min(Low[At],Low[N]);
                if(Low[N]>=Discovery[At]&&Parent[At]>=0){Cuts.Emplace(At,N);}
            }
            else if(N!=Parent[At]){Low[At]=FMath::Min(Low[At],Discovery[N]);}
        }
        Last[At]=Order.Num();
        if(Parent[At]<0&&Children.Num()>1){for(const int32 N:Children){Cuts.Emplace(At,N);}}
    };
    Visit(Topology.Find(Hub));
    double FieldArea=0;for(const int32 I:Topology.Active){FieldArea+=Cells[I].Area;}
    TMap<FIntPoint,TArray<FPortal>> PortalCache;
    const auto Boundary=[&](int32 A,int32 B)->const TArray<FPortal>&
    {
        const FIntPoint Key(FMath::Min(A,B),FMath::Max(A,B));
        if(const auto* Existing=PortalCache.Find(Key)){return *Existing;}
        auto& Segments=PortalCache.Add(Key);
        if(Cells[A].Members.Num()>Cells[B].Members.Num()){Swap(A,B);}
        for(const int32 Member:Cells[A].Members)
        {
            TArray<FNavigationPortalEdge> Edges;Recast.GetPolyEdges(Nodes[Member].Ref,Edges);
            for(const auto& Edge:Edges)
            {
                const int32* Other=Indices.Find(Edge.ToRef);
                if(Other&&Topology.Find(*Other)==B&&FVector::DistSquared2D(Edge.Left,Edge.Right)>.01)
                {Segments.Add({Edge.Left,Edge.Right});}
            }
        }
        return Segments;
    };
    struct FPocket
    {
        int32 Cut=INDEX_NONE;
        TSet<int32> Cells;
        double Area=0;
        FVector Mouth;
        TArray<double> Openings;
        TArray<FPortal> Entrances;
    };
    TArray<FPocket> Candidates;
    for(const auto& Cut:Cuts)
    {
        double SubtreeArea=0;
        for(int32 I=Discovery[Cut.Value];I<Last[Cut.Value];++I){SubtreeArea+=Cells[Order[I]].Area;}
        const bool bSubtree=SubtreeArea<=FieldArea-SubtreeArea-Cells[Cut.Key].Area;
        FPocket Pocket;Pocket.Cut=Cut.Key;
        for(const int32 I:Topology.Active)
        {
            const bool bInside=Discovery[I]>=Discovery[Cut.Value]&&Discovery[I]<Last[Cut.Value];
            if(I!=Cut.Key&&bInside==bSubtree){Pocket.Cells.Add(I);Pocket.Area+=Cells[I].Area;}
        }
        if(Pocket.Cells.IsEmpty()){continue;}
        TArray<FPortal> Edges;
        TArray<int32> Neighbours=Cells[Cut.Key].Neighbours.Array();Neighbours.Sort();
        for(const int32 N:Neighbours){if(Pocket.Cells.Contains(N)){Edges.Append(Boundary(Cut.Key,N));}}
        const auto Groups=PortalGroups(Edges);int32 Qualifying=0,Largest=INDEX_NONE;double LargestWidth=-1;
        for(int32 I=0;I<Groups.Num();++I)
        {
            const double Width=PortalWidth(Edges,Groups[I]);Pocket.Openings.Add(Width+2.*CapsuleRadius);
            Qualifying+=Width>=FMath::Max(0.,240.-2.*CapsuleRadius);
            if(Width>LargestWidth){LargestWidth=Width;Largest=I;}
        }
        if(Qualifying>=2){++DualExitCount;continue;}
        if(Largest==INDEX_NONE)
        {Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("No native portal at convex cell %d."),Cut.Key)));continue;}
        FVector Average=FVector::ZeroVector;double Weight=0;
        for(const int32 E:Groups[Largest])
        {const double Width=FVector::Dist2D(Edges[E].Left,Edges[E].Right);Average+=(Edges[E].Left+Edges[E].Right)*.5*Width;Weight+=Width;}
        Average/=Weight;double Nearest=DBL_MAX;
        for(const int32 E:Groups[Largest])
        {const FVector Mid=(Edges[E].Left+Edges[E].Right)*.5;const double Distance=FVector::DistSquared(Mid,Average);
            if(Distance<Nearest){Nearest=Distance;Pocket.Mouth=Mid;}}
        Pocket.Entrances=Edges;
        Candidates.Add(MoveTemp(Pocket));
    }
    Candidates.Sort([](const FPocket& A,const FPocket& B){return A.Area>B.Area||(A.Area==B.Area&&A.Cut<B.Cut);});
    TArray<int32> Maximal;
    for(int32 I=0;I<Candidates.Num();++I)
    {
        bool bContained=false;
        for(const int32 Existing:Maximal)
        {
            if(Candidates[I].Cells.Num()>Candidates[Existing].Cells.Num()){continue;}
            bool bSubset=true;for(const int32 Cell:Candidates[I].Cells)
            {if(!Candidates[Existing].Cells.Contains(Cell)){bSubset=false;break;}}
            if(bSubset){bContained=true;break;}
        }
        if(!bContained){Maximal.Add(I);}
    }
    FRows Result;
    for(const int32 Index:Maximal)
    {
        const auto& Pocket=Candidates[Index];TArray<FVector> Goals;TSet<FIntVector> Seen;TArray<int32> Members;
        const auto AddGoal=[&](const FVector& Goal)
        {const FIntVector Key(FMath::RoundToInt(Goal.X*100.),FMath::RoundToInt(Goal.Y*100.),FMath::RoundToInt(Goal.Z*100.));
            if(!Seen.Contains(Key)){Seen.Add(Key);Goals.Add(Goal);}};
        for(const int32 Cell:Pocket.Cells)
        {
            for(const int32 Member:Cells[Cell].Members){Members.Add(Member);AddGoal(Nodes[Member].Point);}
            for(const FVector& Vertex:Cells[Cell].Vertices)
            {
                int32 Owner=INDEX_NONE;
                for(const int32 Member:Cells[Cell].Members)
                {if(Nodes[Member].Vertices.ContainsByPredicate([&](const FVector& V){return FVector2D(V).Equals(FVector2D(Vertex),.001);}))
                    {Owner=Member;break;}}
                if(Owner!=INDEX_NONE){AddGoal(FMath::Lerp(Vertex,Nodes[Owner].Point,.02));}
            }
        }
        Goals.Sort([&](const FVector& A,const FVector& B){return FVector::DistSquared2D(A,Pocket.Mouth)>FVector::DistSquared2D(B,Pocket.Mouth);});
        double Depth=0;FVector Deepest=Pocket.Mouth,WitnessMouth=Pocket.Mouth;bool bWitness=false;
        for(const FVector& Goal:Goals)
        {
            // Navigation vertices are voxel contour samples. A pocket-depth witness must be a
            // physically standable capsule foot, rather than an eroded corner or steep detail.
            const double Step=GetDefault<ALDCharacter>()->GetCharacterMovement()->MaxStepHeight;
            double SupportZ=0;int32 SupportTier=0;FHitResult Support;
            FLDLegalGround Ground;ELDWorldQueryFailure Failure;FString GroundError;
            if(!Registry.GetTerrain().GroundNear(Goal+FVector(0,0,Step),SupportZ,SupportTier,true,&Support)
                ||FMath::Abs(SupportZ-Goal.Z)>Step
                ||!Registry.QueryGround(Registry.Revision(),Support.ImpactPoint,Capsule,Surface,Ground,Failure,GroundError)
                ||LDSpawnPlacement::OverlapsStatic(World,LDSpawnPlacement::StandingOrigin(Ground),Capsule))
            {++RejectedGoals;continue;}
            double Distance=DBL_MAX;FVector NearestMouth=Pocket.Mouth;
            for(const auto& Entrance:Pocket.Entrances)
            {
                const FVector Start=ClosestEntrancePoint(Goal,Entrance);
                if(FVector::Dist(Start,Goal)>=Distance){continue;}
                FPathFindingQuery Query(&World,Recast,Start,Goal);Query.SetAllowPartialPaths(false);
                const auto Path=Nav.FindPathSync(Recast.GetConfig(),Query);
                if(!Path.IsSuccessful()||!Path.Path.IsValid()||Path.Path->IsPartial()){continue;}
                double Length=0;const auto& Points=Path.Path->GetPathPoints();
                for(int32 I=1;I<Points.Num();++I){Length+=FVector::Dist(Points[I-1].Location,Points[I].Location);}
                if(Length<Distance){Distance=Length;NearestMouth=Start;}
            }
            if(Distance==DBL_MAX)
            {auto Error=MakeShared<FJsonObject>();Error->SetArrayField(TEXT("from"),Coordinates(Pocket.Mouth));Error->SetArrayField(TEXT("to"),Coordinates(Goal));
                Errors.Add(MakeShared<FJsonValueObject>(Error));continue;}
            if(Distance>Depth){Depth=Distance;Deepest=Goal;WitnessMouth=NearestMouth;}
            if(Depth>350.){bWitness=true;break;}
        }
        if(!bWitness){continue;}
        auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("cut_index"),Pocket.Cut);
        Row->SetNumberField(TEXT("convex_cells"),Pocket.Cells.Num());Row->SetNumberField(TEXT("polygons"),Members.Num());
        Row->SetNumberField(TEXT("depth_cm"),Depth);Row->SetBoolField(TEXT("depth_is_witness"),true);
        Row->SetArrayField(TEXT("mouth"),Coordinates(WitnessMouth));Row->SetArrayField(TEXT("deepest"),Coordinates(Deepest));
        FRows MemberRows;Members.Sort();for(const int32 Member:Members){MemberRows.Add(MakeShared<FJsonValueNumber>(Member));}
        FRows Widths;for(const double Width:Pocket.Openings){Widths.Add(MakeShared<FJsonValueNumber>(Width));}
        Row->SetArrayField(TEXT("members"),MemberRows);Row->SetArrayField(TEXT("opening_widths_cm"),Widths);
        FRows CutMembers;for(const int32 Member:Cells[Pocket.Cut].Members)
        {CutMembers.Add(MakeShared<FJsonValueNumber>(Member));}
        Row->SetArrayField(TEXT("cut_members"),CutMembers);
        Result.Add(MakeShared<FJsonValueObject>(Row));
    }
    return Result;
}

}

FString ULDSceneAuthoringLibrary::GameplayTraversalAudit(UWorld* World, int32 PathPairCount)
{
    auto Report = MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("ready"),false);
    const auto Finish = [&]() { FString Text; FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text)); return Text; };
    if (!World || World->IsGameWorld() || PathPairCount < 1 || PathPairCount > 2000)
    { Report->SetStringField(TEXT("error"),TEXT("Traversal audit requires an editor world and 1..2000 path pairs.")); return Finish(); }
    auto* Registry = World->GetSubsystem<ULDMapRegistry>();
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    auto* Recast = Nav ? Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) : nullptr;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    FCollisionShape Capsule;
    FString Error;
    if (!Registry || !Recast || !LDDemoEntry::LoadRuntimeContent(Content,Error)
        || !ULDMapRegistry::ContentCapsule(*Content,Capsule,Error))
    { Report->SetStringField(TEXT("error"),Error.IsEmpty()?TEXT("Native navigation/registry is unavailable."):Error); return Finish(); }
    if (!Registry->Rebuild(Error))
    {
        // A blocked mandatory site must still fail readiness, but must not hide the independent native
        // navigation evidence needed to diagnose the rest of the map. This never supplies gameplay defaults.
        Report->SetStringField(TEXT("error"),Error);
        FRows Raw;
        TArray<FNavTileRef> RawTiles;Recast->GetAllNavMeshTiles(RawTiles);
        for(const FNavTileRef Tile:RawTiles)
        {
            TArray<FNavPoly> Polys;if(!Recast->GetPolysInTile(Tile,Polys)){continue;}
            for(const auto& Poly:Polys)
            {
                auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("ref"),LexToString(Poly.Ref));
                Row->SetArrayField(TEXT("point"),Coordinates(Poly.Center));
                TArray<FVector> Corners;Recast->GetPolyVerts(Poly.Ref,Corners);FRows Vertices;
                for(const FVector& Corner:Corners){Vertices.Add(MakeShared<FJsonValueArray>(Coordinates(Corner)));}
                Row->SetArrayField(TEXT("vertices"),Vertices);
                TArray<NavNodeRef> Neighbours;Recast->GetPolyNeighbors(Poly.Ref,Neighbours);FRows Links;
                for(const NavNodeRef Ref:Neighbours){Links.Add(MakeShared<FJsonValueString>(LexToString(Ref)));}
                Row->SetArrayField(TEXT("neighbours"),Links);Raw.Add(MakeShared<FJsonValueObject>(Row));
            }
        }
        Report->SetArrayField(TEXT("raw_navigation_graph"),Raw);
        return Finish();
    }
    const FName Surface(*Registry->GetGeometry()->Surfaces[0].Id);
    const auto* Move = GetDefault<ALDCharacter>()->GetCharacterMovement();
    const FCollisionQueryParams GroundQuery = LDNativeGround::Query(*World);
    TArray<FAuditNode> Nodes;
    TMap<NavNodeRef,int32> Indices;
    TArray<FNavTileRef> Tiles;Recast->GetAllNavMeshTiles(Tiles);
    for (const FNavTileRef Tile:Tiles)
    {
        TArray<FNavPoly> Polys;
        if (!Recast->GetPolysInTile(Tile,Polys)) { continue; }
        for (const auto& Poly:Polys)
        {
            if (Indices.Contains(Poly.Ref)) { continue; }
            Indices.Add(Poly.Ref,Nodes.Num());
            auto& Node=Nodes.AddDefaulted_GetRef();Node.Ref=Poly.Ref;Node.Point=Poly.Center;Recast->GetPolyVerts(Poly.Ref,Node.Vertices);
        }
    }
    for (auto& Node:Nodes)
    {
        TArray<NavNodeRef> Neighbours;Recast->GetPolyNeighbors(Node.Ref,Neighbours);
        for (const auto Ref:Neighbours) { if (const int32* Other=Indices.Find(Ref)) { Node.Neighbours.AddUnique(*Other); } }
    }
    const auto* Capture=Registry->GetGeometry()->Anchors.FindByPredicate([](const auto& A){return A.Kind==ELDDemoWorldAnchorKind::Capture;});
    FVector Hub;
    if (!Capture || !Capture->PositionCm.ToVector(Hub,Error))
    {Report->SetStringField(TEXT("error"),TEXT("The field capture anchor is unavailable."));return Finish();}
    const NavNodeRef HubRef=Recast->FindNearestPoly(Hub,FVector(Capsule.GetCapsuleRadius()*2,Capsule.GetCapsuleRadius()*2,180));
    const int32* HubIndex=Indices.Find(HubRef);
    if (!HubIndex)
    {Report->SetStringField(TEXT("error"),TEXT("The field capture is outside native navigation."));return Finish();}
    TSet<int32> Field;TArray<int32> Queue{*HubIndex};Field.Add(*HubIndex);
    for(int32 Cursor=0;Cursor<Queue.Num();++Cursor)
    {for(const int32 N:Nodes[Queue[Cursor]].Neighbours){if(!Field.Contains(N)){Field.Add(N);Queue.Add(N);}}}
    FRows Graph,AllGraph;
    for(int32 Index=0;Index<Nodes.Num();++Index)
    {
        auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("index"),Index);Row->SetArrayField(TEXT("point"),Coordinates(Nodes[Index].Point));
        FRows Vertices;for(const FVector& Vertex:Nodes[Index].Vertices){Vertices.Add(MakeShared<FJsonValueArray>(Coordinates(Vertex)));}
        Row->SetArrayField(TEXT("vertices"),Vertices);
        FRows Neighbours;for(const int32 N:Nodes[Index].Neighbours){Neighbours.Add(MakeShared<FJsonValueNumber>(N));}
        uint16 PolyFlags=0,AreaFlags=0;Recast->GetPolyFlags(Nodes[Index].Ref,PolyFlags,AreaFlags);
        Row->SetNumberField(TEXT("area"),Recast->GetPolyAreaID(Nodes[Index].Ref));Row->SetNumberField(TEXT("flags"),PolyFlags);
        Row->SetArrayField(TEXT("neighbours"),Neighbours);Row->SetBoolField(TEXT("in_field"),Field.Contains(Index));
        const auto Value=MakeShared<FJsonValueObject>(Row);AllGraph.Add(Value);if(Field.Contains(Index)){Graph.Add(Value);}
    }
    Report->SetArrayField(TEXT("field_graph"),Graph);
    Report->SetArrayField(TEXT("all_navigation_graph"),AllGraph);
    Report->SetNumberField(TEXT("field_root_index"),*HubIndex);
    Report->SetNumberField(TEXT("all_nav_polygons"),Nodes.Num());
    Report->SetNumberField(TEXT("field_nav_polygons"),Field.Num());
    Report->SetNumberField(TEXT("capsule_radius_cm"),Capsule.GetCapsuleRadius());
    Report->SetNumberField(TEXT("capsule_half_height_cm"),Capsule.GetCapsuleHalfHeight());

    FRows PocketErrors;int32 ConvexCells=0,DualExitRegions=0,RejectedPocketGoals=0;
    FRows Pockets=AnalysePockets(Nodes,Indices,Field,*HubIndex,*Recast,*Nav,*World,Capsule.GetCapsuleRadius(),
        *Registry,Capsule,Surface,PocketErrors,ConvexCells,DualExitRegions,RejectedPocketGoals);
    Report->SetNumberField(TEXT("convex_nav_cells"),ConvexCells);
    Report->SetNumberField(TEXT("dual_exit_regions"),DualExitRegions);
    Report->SetNumberField(TEXT("rejected_pocket_goal_samples"),RejectedPocketGoals);
    Report->SetArrayField(TEXT("pocket_path_errors"),PocketErrors);
    Report->SetArrayField(TEXT("single_exit_pockets"),Pockets);

    FRows BadCentres,Narrow,BlockedNarrow,Curbs;
    TSet<FIntVector> TestedGaps,TestedCurbs;
    TArray<FLDLegalGround> Candidates;FRows StandablePoints;
    for(const int32 Index:Queue)
    {
        FLDLegalGround Ground;ELDWorldQueryFailure Failure;
        // Recast polygon centres are voxel heights, not capsule feet. Resolve the local physical support
        // within one native step before asking the same strict standing-ground contract used by gameplay.
        double SupportZ=0;int32 SupportTier=0;FHitResult NativeSupport;Error.Reset();
        const bool bSupported=Registry->GetTerrain().GroundNear(Nodes[Index].Point+FVector(0,0,Move->MaxStepHeight),
            SupportZ,SupportTier,true,&NativeSupport)&&FMath::Abs(SupportZ-Nodes[Index].Point.Z)<=Move->MaxStepHeight;
        if(bSupported&&Registry->QueryGround(Registry->Revision(),NativeSupport.ImpactPoint,Capsule,Surface,Ground,Failure,Error)
            &&!LDSpawnPlacement::OverlapsStatic(*World,LDSpawnPlacement::StandingOrigin(Ground),Capsule))
        {Candidates.Add(Ground);auto Row=MakeShared<FJsonObject>();
            Row->SetNumberField(TEXT("node_index"),Index);Row->SetArrayField(TEXT("foot"),Coordinates(Ground.FootCm));
            Row->SetStringField(TEXT("support"),GetPathNameSafe(Ground.Floor.Get()));StandablePoints.Add(MakeShared<FJsonValueObject>(Row));}
        else
        {auto Row=MakeShared<FJsonObject>();Row->SetArrayField(TEXT("point"),Coordinates(Nodes[Index].Point));Row->SetStringField(TEXT("error"),Error);BadCentres.Add(MakeShared<FJsonValueObject>(Row));}
        TArray<FNavigationPortalEdge> Edges;Recast->GetPolyEdges(Nodes[Index].Ref,Edges);
        for(const auto& Edge:Edges)
        {
            const FVector Centre=Edge.GetMiddlePoint();
            const FIntVector Key(FMath::RoundToInt(Centre.X/60.),FMath::RoundToInt(Centre.Y/60.),FMath::RoundToInt(Centre.Z/20.));
            if(Edge.ToRef!=INVALID_NAVNODEREF)
            {
                if(TestedGaps.Contains(Key)){continue;}TestedGaps.Add(Key);
                double Z=Centre.Z;int32 Tier;FHitResult Support;
                if(!Registry->GetTerrain().GroundNear(Centre+FVector(0,0,Move->MaxStepHeight),Z,Tier,true,&Support)
                    ||FMath::Abs(Z-Centre.Z)>Move->MaxStepHeight) {continue;}
                double BestWidth=120.;FVector BestLeft,BestRight;FString LeftName,RightName;
                for(int32 Direction=0;Direction<8;++Direction)
                {
                    const double A=Direction*PI/8.;const FVector D(FMath::Cos(A),FMath::Sin(A),0),At(Centre.X,Centre.Y,Z+90.);
                    FHitResult Left,Right;
                    if(!LDNativeGround::Trace(*World,Left,At,At-D*180.,GroundQuery)||!LDNativeGround::Trace(*World,Right,At,At+D*180.,GroundQuery)
                        ||Left.bStartPenetrating||Right.bStartPenetrating){continue;}
                    const double Width=FVector::Dist2D(Left.ImpactPoint,Right.ImpactPoint);
                    if(Width>.1&&Width<BestWidth)
                    {BestWidth=Width;BestLeft=Left.ImpactPoint;BestRight=Right.ImpactPoint;LeftName=GetPathNameSafe(Left.GetComponent());RightName=GetPathNameSafe(Right.GetComponent());}
                }
                if(BestWidth<120.)
                {
                    auto Row=MakeShared<FJsonObject>();Row->SetArrayField(TEXT("point"),Coordinates(Centre));Row->SetNumberField(TEXT("width_cm"),BestWidth);
                    Row->SetArrayField(TEXT("left"),Coordinates(BestLeft));Row->SetArrayField(TEXT("right"),Coordinates(BestRight));
                    Row->SetStringField(TEXT("left_component"),LeftName);Row->SetStringField(TEXT("right_component"),RightName);
                    const int32* Other = Indices.Find(Edge.ToRef);
                    const bool bClear = Other && HasClearPortalCrossing(*Registry, *World, GroundQuery, Edge,
                        Nodes[Index].Point, Nodes[*Other].Point, Capsule, Surface, Move->MaxStepHeight);
                    Row->SetBoolField(TEXT("capsule_crossing_clear"), bClear);
                    const auto Value = MakeShared<FJsonValueObject>(Row);
                    Narrow.Add(Value);
                    if (!bClear) { BlockedNarrow.Add(Value); }
                }
            }
            else
            {
                if(TestedCurbs.Contains(Key)){continue;}TestedCurbs.Add(Key);
                FVector D=(Edge.Right-Edge.Left).GetSafeNormal2D();D=FVector(-D.Y,D.X,0);
                FHitResult A,B;
                const FVector P=Centre+D*60.,Q=Centre-D*60.;
                if(!LDNativeGround::Trace(*World,A,P+FVector(0,0,100),P-FVector(0,0,100),GroundQuery)
                    ||!LDNativeGround::Trace(*World,B,Q+FVector(0,0,100),Q-FVector(0,0,100),GroundQuery)
                    ||A.ImpactNormal.Z<.999||B.ImpactNormal.Z<.999){continue;}
                const double Rise=FMath::Abs(A.ImpactPoint.Z-B.ImpactPoint.Z);
                if(Rise>35.01&&Rise<=65.01)
                {auto Row=MakeShared<FJsonObject>();Row->SetArrayField(TEXT("point"),Coordinates(Centre));Row->SetNumberField(TEXT("rise_cm"),Rise);
                    Row->SetStringField(TEXT("a"),GetPathNameSafe(A.GetComponent()));Row->SetStringField(TEXT("b"),GetPathNameSafe(B.GetComponent()));Curbs.Add(MakeShared<FJsonValueObject>(Row));}
            }
        }
    }
    // Recast voxel polygon centres are candidates, not guaranteed legal capsule feet.
    // Rejected candidates remain diagnostic evidence; path acceptance only samples physically standable feet.
    Report->SetArrayField(TEXT("rejected_nav_samples"),BadCentres);
    Report->SetNumberField(TEXT("standable_nav_samples"),Candidates.Num());
    Report->SetArrayField(TEXT("standable_points"),StandablePoints);
    Report->SetArrayField(TEXT("narrow_passages"),Narrow);
    Report->SetArrayField(TEXT("blocked_narrow_passages"),BlockedNarrow);
    Report->SetArrayField(TEXT("curb_candidates"),Curbs);
    FRows Paths;
    FRandomStream Random(338);int32 Passed=0;
    if(Candidates.Num()<2)
    {Report->SetStringField(TEXT("error"),TEXT("Fewer than two grounded field samples."));return Finish();}
    for(int32 Pair=0;Pair<PathPairCount;++Pair)
    {
        const int32 FromIndex=Random.RandRange(0,Candidates.Num()-1);
        int32 ToIndex=Random.RandRange(0,Candidates.Num()-2);if(ToIndex>=FromIndex){++ToIndex;}
        const auto& A=Candidates[FromIndex];const auto& B=Candidates[ToIndex];
        FLDCompletePath Path;ELDWorldQueryFailure Failure;Error.Reset();
        const bool Ok=Registry->QueryCompletePath(Registry->Revision(),A,B,Capsule,Path,Failure,Error);
        auto Row=MakeShared<FJsonObject>();Row->SetBoolField(TEXT("passed"),Ok);Row->SetArrayField(TEXT("from"),Coordinates(A.FootCm));Row->SetArrayField(TEXT("to"),Coordinates(B.FootCm));
        if(!Ok)
        {
            FPathFindingQuery Probe(World,*Recast,A.SurfaceCm,B.SurfaceCm);Probe.SetAllowPartialPaths(false);
            const auto Original=Nav->FindPathSync(Recast->GetConfig(),Probe);
            Row->SetBoolField(TEXT("search_limit"),Original.Path.IsValid()&&Original.Path->DidSearchReachedLimit());
            auto Expanded=Recast->GetDefaultQueryFilter()->GetCopy();Expanded->SetMaxSearchNodes(FMath::Max(32768,Nodes.Num()*2));
            FPathFindingQuery Wider(World,*Recast,A.SurfaceCm,B.SurfaceCm,Expanded);Wider.SetAllowPartialPaths(false);
            const auto Diagnostic=Nav->FindPathSync(Recast->GetConfig(),Wider);
            Row->SetBoolField(TEXT("complete_with_larger_search"),Diagnostic.IsSuccessful()&&Diagnostic.Path.IsValid()&&!Diagnostic.Path->IsPartial());
        }
        Row->SetStringField(TEXT("error"),Error);Paths.Add(MakeShared<FJsonValueObject>(Row));Passed+=Ok;
    }
    Report->SetArrayField(TEXT("sampled_paths"),Paths);Report->SetNumberField(TEXT("path_pairs_passed"),Passed);Report->SetNumberField(TEXT("path_pairs"),PathPairCount);

    // Compare native simple collision to the actual visible mesh at player-body heights, rather than render AABBs.
    TMap<UStaticMesh*,TArray<FAuditTriangle>> Meshes;
    FRows Airwalls,Overhead,MissingMeshes,HiddenCollision;int32 BodySamples=0;
    for(TActorIterator<AActor> It(World);It;++It)
    {
        if(It->IsEditorOnly()||*It==World->GetDefaultBrush()){continue;}
        TInlineComponentArray<UStaticMeshComponent*> Parts(*It);
        for(auto* Part:Parts)
        {
            if(!LDNativeGround::IsSupport(*Part)||!Part->GetStaticMesh()){continue;}
            UStaticMesh* Mesh=Part->GetStaticMesh().Get();
            if(!Meshes.Contains(Mesh))
            {
                const FMeshDescription* Desc=Mesh->GetMeshDescription(0);
                if(!Desc){MissingMeshes.Add(MakeShared<FJsonValueString>(Mesh->GetPathName()));continue;}
                auto& Triangles=Meshes.Add(Mesh);const FStaticMeshConstAttributes Attr(*Desc);const auto Positions=Attr.GetVertexPositions();
                for(const FTriangleID Tri:Desc->Triangles().GetElementIDs())
                {const auto Corners=Desc->GetTriangleVertexInstances(Tri);Triangles.Add({FVector(Positions[Desc->GetVertexInstanceVertex(Corners[0])]),FVector(Positions[Desc->GetVertexInstanceVertex(Corners[1])]),FVector(Positions[Desc->GetVertexInstanceVertex(Corners[2])])});}
            }
            const FBox Bounds=Part->GetBodyInstance()->GetBodyBounds();const FVector Centre=Bounds.GetCenter();const FTransform Transform=Part->GetComponentTransform();
            if(Part->bHiddenInGame||It->IsHidden()){HiddenCollision.Add(MakeShared<FJsonValueString>(Part->GetPathName()));}
            double Worst=12.,MinBaseClearance=DBL_MAX;FVector WorstPoint,GroundAt;
            FCollisionQueryParams Query=GroundQuery;Query.AddIgnoredActor(*It);
            for(int32 Direction=0;Direction<16;++Direction)
            {
                const double Angle=Direction*PI/8.;const FVector D(FMath::Cos(Angle),FMath::Sin(Angle),0);
                const FVector Outside=Centre+D*(Bounds.GetExtent().Size2D()+120.);
                FHitResult Floor;
                if(!LDNativeGround::Trace(*World,Floor,FVector(Outside.X,Outside.Y,Bounds.Min.Z+Move->MaxStepHeight),FVector(Outside.X,Outside.Y,Bounds.Min.Z-2500),Query)
                    ||Floor.ImpactNormal.Z<Move->GetWalkableFloorZ()){continue;}
                GroundAt=Floor.ImpactPoint;MinBaseClearance=FMath::Min(MinBaseClearance,Bounds.Min.Z-Floor.ImpactPoint.Z);
                for(const double Height:{15.,90.,180.})
                {
                    const FVector Start(Outside.X,Outside.Y,Floor.ImpactPoint.Z+Height),End(Centre.X,Centre.Y,Floor.ImpactPoint.Z+Height);
                    FHitResult Hit;
                    if(!Part->LineTraceComponent(Hit,Start,End,FCollisionQueryParams(SCENE_QUERY_STAT(DemoCollisionEnvelope),false))||Hit.bStartPenetrating){continue;}
                    ++BodySamples;
                    const FVector LocalStart=Transform.InverseTransformPosition(Start),LocalEnd=Transform.InverseTransformPosition(End);
                    double First=DBL_MAX;const double Length=(LocalEnd-LocalStart).Size();
                    if(Length<=.01){continue;}
                    bool bOnVisibleMesh=false;
                    const FVector LocalImpact=Transform.InverseTransformPosition(Hit.ImpactPoint);
                    for(const auto& Tri:Meshes.FindChecked(Mesh))
                    {
                        // Cooked triangle vertices can differ slightly from source floats at a shared edge.
                        const FVector Closest=FMath::ClosestPointOnTriangleToPoint(LocalImpact,Tri.A,Tri.B,Tri.C);
                        if(FVector::DistSquared(Transform.TransformPosition(Closest),Hit.ImpactPoint)<=.01){bOnVisibleMesh=true;break;}
                        FVector Point,Normal;
                        if(FMath::SegmentTriangleIntersection(LocalStart,LocalEnd,Tri.A,Tri.B,Tri.C,Point,Normal))
                        {First=FMath::Min(First,(Point-LocalStart).Size()/Length);}
                    }
                    if(bOnVisibleMesh){continue;}
                    const double Overreach=First==DBL_MAX ? (End-Start).Size() : (End-Start).Size()*First-(Hit.ImpactPoint-Start).Size();
                    if(Overreach>Worst){Worst=Overreach;WorstPoint=Hit.ImpactPoint;}
                }
            }
            if(Worst>12.01)
            {auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("component"),Part->GetPathName());Row->SetStringField(TEXT("actor_label"),It->GetActorLabel());Row->SetStringField(TEXT("mesh"),Mesh->GetPathName());
                Row->SetArrayField(TEXT("point"),Coordinates(WorstPoint));Row->SetNumberField(TEXT("overreach_cm"),Worst);Airwalls.Add(MakeShared<FJsonValueObject>(Row));}
            if(MinBaseClearance>200.&&MinBaseClearance<DBL_MAX)
            {auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("component"),Part->GetPathName());Row->SetArrayField(TEXT("ground"),Coordinates(GroundAt));Overhead.Add(MakeShared<FJsonValueObject>(Row));}
        }
    }
    Report->SetArrayField(TEXT("airwall_violations"),Airwalls);Report->SetArrayField(TEXT("overhead_candidates"),Overhead);
    Report->SetArrayField(TEXT("unreadable_visual_meshes"),MissingMeshes);Report->SetArrayField(TEXT("hidden_collision"),HiddenCollision);
    Report->SetNumberField(TEXT("collision_ray_samples"),BodySamples);
    FLDSightGeometry Sight;
    FRows CoastalFailures,CoastalSamples;
    int32 NegativeCells=0,CoastalPairs=0;
    if(Sight.Build(*Registry->GetGeometry(),Registry->Revision(),Content->Get().Parameters.VisionRadiusCm,
        Content->Get().Parameters.VisionRange,Error,World))
    {
        TArray<FLDLegalGround> Coast;
        for(int32 Cell=0;Cell<LDObservation::CoverageCells;++Cell)
        {
            const FVector2D At=Sight.CellCentre(Cell);FHitResult Hit;
            if(!Registry->GetTerrain().TraceGround(At,Hit)||Hit.ImpactPoint.Z>25.||Hit.ImpactNormal.Z<Move->GetWalkableFloorZ()){continue;}
            FNavLocation Location;
            if(!Nav->ProjectPointToNavigation(Hit.ImpactPoint,Location,FVector(5,5,50))){continue;}
            const int32* Node=Indices.Find(Location.NodeRef);
            if(!Node||!Field.Contains(*Node)){continue;}
            FLDLegalGround Ground;ELDWorldQueryFailure Failure;
            if(!Registry->QueryGround(Registry->Revision(),Hit.ImpactPoint,Capsule,Surface,Ground,Failure,Error)
                ||LDSpawnPlacement::OverlapsStatic(*World,LDSpawnPlacement::StandingOrigin(Ground),Capsule)){continue;}
            Coast.Add(Ground);NegativeCells+=Hit.ImpactPoint.Z<0.;
        }
        FCollisionQueryParams ClearQuery(SCENE_QUERY_STAT(DemoCoastalAudit),false);
        ClearQuery.AddIgnoredActor(World->GetDefaultBrush());
        for(TActorIterator<AActor> It(World);It;++It)
        {TInlineComponentArray<UPrimitiveComponent*> Parts(*It);for(const auto* Part:Parts)
            {if(LDSceneTags::NoCollision(*Part)||LDSceneTags::NoViewBlock(*Part)){ClearQuery.AddIgnoredComponent(Part);}}}
        const double Eye=double(Registry->GetGeometry()->SightSampleHeightCm.Numerator)/Registry->GetGeometry()->SightSampleHeightCm.Denominator;
        const int32 SourceCount=FMath::Min(200,Coast.Num());
        for(int32 Sample=0;Sample<SourceCount;++Sample)
        {
            const auto& Source=Coast[Sample*Coast.Num()/SourceCount];
            const FVector Foot=LDSpawnPlacement::StandingOrigin(Source)-FVector(0,0,Capsule.GetCapsuleHalfHeight());
            TArray<uint8> Coverage;Sight.Coverage(Foot,Coverage);int32 Count=0;
            for(const uint8 Byte:Coverage){Count+=FMath::CountBits(uint32(Byte));}
            auto Row=MakeShared<FJsonObject>();Row->SetArrayField(TEXT("point"),Coordinates(Foot));Row->SetNumberField(TEXT("covered_cells"),Count);
            const bool SelfVisible=Sight.Trace(Foot,Foot)==ELDSightResult::Visible;
            Row->SetBoolField(TEXT("self_visible"),SelfVisible);CoastalSamples.Add(MakeShared<FJsonValueObject>(Row));
            if(!SelfVisible||Count==0){CoastalFailures.Add(MakeShared<FJsonValueObject>(Row));}
            for(int32 Direction=0;Direction<8;++Direction)
            {
                const double Angle=Direction*PI/4.;FLDLegalGround Target;ELDWorldQueryFailure Failure;
                const FVector Nearby=Foot+FVector(FMath::Cos(Angle)*150.,FMath::Sin(Angle)*150.,0);
                if(!Registry->QueryGround(Registry->Revision(),Nearby,Capsule,Surface,Target,Failure,Error)
                    ||LDSpawnPlacement::OverlapsStatic(*World,LDSpawnPlacement::StandingOrigin(Target),Capsule)){continue;}
                FVector NavHit;
                if(UNavigationSystemV1::NavigationRaycast(World,Source.SurfaceCm,Target.SurfaceCm,NavHit)
                    ||World->LineTraceTestByChannel(Source.SurfaceCm+FVector(0,0,Eye),Target.SurfaceCm+FVector(0,0,Eye),ECC_GameTraceChannel2,ClearQuery)){continue;}
                ++CoastalPairs;
                const FVector TargetFoot=LDSpawnPlacement::StandingOrigin(Target)-FVector(0,0,Capsule.GetCapsuleHalfHeight());
                const auto Forward=Sight.Trace(Foot,TargetFoot),Backward=Sight.Trace(TargetFoot,Foot);
                if(Forward!=ELDSightResult::Visible||Backward!=ELDSightResult::Visible)
                {auto Bad=MakeShared<FJsonObject>();Bad->SetArrayField(TEXT("from"),Coordinates(Foot));Bad->SetArrayField(TEXT("to"),Coordinates(TargetFoot));
                    Bad->SetNumberField(TEXT("forward"),int32(Forward));Bad->SetNumberField(TEXT("backward"),int32(Backward));CoastalFailures.Add(MakeShared<FJsonValueObject>(Bad));}
            }
        }
    }
    else{Report->SetStringField(TEXT("coastal_error"),Error);}
    Report->SetArrayField(TEXT("coastal_sight_samples"),CoastalSamples);Report->SetArrayField(TEXT("coastal_sight_failures"),CoastalFailures);
    Report->SetNumberField(TEXT("negative_coastal_cells"),NegativeCells);Report->SetNumberField(TEXT("coastal_visible_pairs"),CoastalPairs);
    Error.Reset();Report->SetBoolField(TEXT("field_spawns_reachable"),Registry->ValidateFieldSpawnPaths(false,Error));Report->SetStringField(TEXT("field_spawns_error"),Error);
    Error.Reset();Report->SetBoolField(TEXT("final_spawns_reachable"),Registry->ValidateFieldSpawnPaths(true,Error));Report->SetStringField(TEXT("final_spawns_error"),Error);
    Report->SetBoolField(TEXT("ready"),true);
    Report->SetBoolField(TEXT("passed"),Passed==PathPairCount&&Airwalls.IsEmpty()&&BlockedNarrow.IsEmpty()&&Curbs.IsEmpty()&&MissingMeshes.IsEmpty()
        &&PocketErrors.IsEmpty()&&HiddenCollision.IsEmpty()&&Candidates.Num()>=2&&CoastalFailures.IsEmpty()&&NegativeCells>0&&CoastalPairs>0
        &&Report->GetBoolField(TEXT("field_spawns_reachable"))&&Report->GetBoolField(TEXT("final_spawns_reachable")));
    return Finish();
}
