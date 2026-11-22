#pragma once

#include "CoreMinimal.h"
#include "NavMesh/RecastNavMesh.h"

namespace LDTraversalTopology
{
struct FNode
{
    NavNodeRef Ref=INVALID_NAVNODEREF;
    FVector Point;
    TArray<FVector> Vertices;
    TArray<int32> Neighbours;
};
struct FCell
{
    int32 Parent=INDEX_NONE;
    double Area=0;
    TArray<FVector> Vertices;
    TArray<int32> Members;
    TSet<int32> Neighbours;
};
struct FCells
{
    TArray<FCell> Cells;
    TArray<int32> Active;
    int32 Find(int32 Index) const
    { while(Cells[Index].Parent!=Index){Index=Cells[Index].Parent;}return Index; }
};
inline double Area(const TArray<FVector>& Vertices)
{
    double Sum=0;
    for(int32 I=0;I<Vertices.Num();++I)
    {const auto& A=Vertices[I];const auto& B=Vertices[(I+1)%Vertices.Num()];Sum+=A.X*B.Y-A.Y*B.X;}
    return FMath::Abs(Sum)*.5;
}
inline TArray<FVector> Hull(TArray<FVector> Points)
{
    Points.Sort([](const FVector& A,const FVector& B){return A.X<B.X||(A.X==B.X&&A.Y<B.Y);});
    TArray<FVector> Unique;
    for(const FVector& P:Points)
    {if(Unique.IsEmpty()||!FVector2D(P).Equals(FVector2D(Unique.Last()),.001)){Unique.Add(P);}}
    if(Unique.Num()<3){return Unique;}
    const auto Cross=[](const FVector& A,const FVector& B,const FVector& C)
    {return (B.X-A.X)*(C.Y-A.Y)-(B.Y-A.Y)*(C.X-A.X);};
    TArray<FVector> Result;
    for(const FVector& P:Unique)
    {while(Result.Num()>1&&Cross(Result[Result.Num()-2],Result.Last(),P)<=0){Result.Pop();}Result.Add(P);}
    const int32 Lower=Result.Num();
    for(int32 I=Unique.Num()-2;I>=0;--I)
    {while(Result.Num()>Lower&&Cross(Result[Result.Num()-2],Result.Last(),Unique[I])<=0){Result.Pop();}Result.Add(Unique[I]);}
    Result.Pop();return Result;
}

// Recast tile/triangle edges are tessellation, not independent street entrances. Merge only an exact
// convex union: native adjacency remains required, and equal area forbids filling a hole or a wall.
inline FCells ConvexCells(const TArray<FNode>& Nodes,const TSet<int32>& Field)
{
    FCells Result;Result.Cells.SetNum(Nodes.Num());
    for(int32 I=0;I<Nodes.Num();++I){Result.Cells[I].Parent=I;}
    TArray<int32> Ordered=Field.Array();Ordered.Sort();
    for(const int32 I:Ordered)
    {
        auto& C=Result.Cells[I];C.Vertices=Nodes[I].Vertices;C.Area=Area(C.Vertices);C.Members.Add(I);
        for(const int32 N:Nodes[I].Neighbours){if(Field.Contains(N)){C.Neighbours.Add(N);}}
    }
    struct FMerge {double Cost;int32 A,B;};
    const auto Less=[](const FMerge& A,const FMerge& B)
    {return A.Cost<B.Cost||(A.Cost==B.Cost&&(A.A<B.A||(A.A==B.A&&A.B<B.B)));};
    TArray<FMerge> Pending;
    const auto Push=[&](int32 A,int32 B)
    {if(A!=B){if(A>B){Swap(A,B);}Pending.HeapPush({Result.Cells[A].Area+Result.Cells[B].Area,A,B},Less);}};
    for(const int32 I:Ordered){for(const int32 N:Result.Cells[I].Neighbours){if(I<N){Push(I,N);}}}
    while(!Pending.IsEmpty())
    {
        FMerge Item;Pending.HeapPop(Item,Less);int32 A=Result.Find(Item.A),B=Result.Find(Item.B);
        if(A==B||!Result.Cells[A].Neighbours.Contains(B)){continue;}
        TArray<FVector> Points=Result.Cells[A].Vertices;Points.Append(Result.Cells[B].Vertices);
        TArray<FVector> Boundary=Hull(MoveTemp(Points));
        const double Combined=Result.Cells[A].Area+Result.Cells[B].Area;
        if(Boundary.Num()<3||FMath::Abs(Area(Boundary)-Combined)>1.){continue;}
        if(Result.Cells[A].Members.Num()<Result.Cells[B].Members.Num()){Swap(A,B);}
        auto& Keep=Result.Cells[A];auto& Drop=Result.Cells[B];Drop.Parent=A;
        Keep.Vertices=MoveTemp(Boundary);Keep.Area=Combined;Keep.Members.Append(Drop.Members);
        for(const int32 N:Drop.Neighbours){Keep.Neighbours.Add(N);}
        Keep.Neighbours.Remove(A);Keep.Neighbours.Remove(B);
        TArray<int32> Neighbours=Keep.Neighbours.Array();Neighbours.Sort();
        for(const int32 N:Neighbours)
        {Result.Cells[N].Neighbours.Remove(B);Result.Cells[N].Neighbours.Add(A);Push(A,N);}
        Drop.Neighbours.Reset();Drop.Members.Reset();
    }
    for(int32 I=0;I<Result.Cells.Num();++I){Result.Cells[I].Parent=Result.Find(I);}
    for(const int32 I:Ordered){if(Result.Cells[I].Parent==I){Result.Active.Add(I);}}
    return Result;
}

struct FPortal {FVector Left,Right;};
// Depth starts at the nearest point of the entire entrance, not its arbitrary midpoint.
inline FVector ClosestEntrancePoint(const FVector& Goal,const FPortal& Entrance)
{
    const FVector D=Entrance.Right-Entrance.Left;
    const double T=D.SizeSquared()>.0001?FMath::Clamp(FVector::DotProduct(Goal-Entrance.Left,D)/D.SizeSquared(),0.,1.):0.;
    return Entrance.Left+D*T;
}
inline double PointSegmentDistanceSquared(const FVector& P,const FPortal& E)
{
    const FVector2D A(E.Left),B(E.Right),Point(P),D=B-A;
    const double T=D.SizeSquared()>.0001?FMath::Clamp(FVector2D::DotProduct(Point-A,D)/D.SizeSquared(),0.,1.):0.;
    return FVector2D::DistSquared(Point,A+D*T);
}
// A doorway split across tile edges remains one opening. Separate doors into the same plaza remain separate.
inline TArray<TArray<int32>> PortalGroups(const TArray<FPortal>& Edges)
{
    TArray<TArray<int32>> Groups;TSet<int32> Seen;
    for(int32 I=0;I<Edges.Num();++I)
    {
        if(Seen.Contains(I)){continue;}TArray<int32> Group{I};Seen.Add(I);
        for(int32 Cursor=0;Cursor<Group.Num();++Cursor)
        {
            const auto& A=Edges[Group[Cursor]];
            for(int32 N=0;N<Edges.Num();++N)
            {
                if(Seen.Contains(N)){continue;}const auto& B=Edges[N];
                if(FMath::Min(FMath::Min(PointSegmentDistanceSquared(A.Left,B),PointSegmentDistanceSquared(A.Right,B)),
                    FMath::Min(PointSegmentDistanceSquared(B.Left,A),PointSegmentDistanceSquared(B.Right,A)))<=.01)
                {Seen.Add(N);Group.Add(N);}
            }
        }
        Groups.Add(MoveTemp(Group));
    }
    return Groups;
}
inline double PortalWidth(const TArray<FPortal>& Edges,const TArray<int32>& Group)
{
    double Width=0;
    for(const int32 A:Group){for(const int32 B:Group)
    {for(const FVector& P:{Edges[A].Left,Edges[A].Right}){for(const FVector& Q:{Edges[B].Left,Edges[B].Right})
    {Width=FMath::Max(Width,FVector::Dist2D(P,Q));}}}}
    return Width;
}
}
