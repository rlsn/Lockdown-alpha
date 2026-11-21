#include "Vision/LDVisionSupport.h"
#include "World/LDNativeGround.h"
#include "Character/LDCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace LDVisionSupportDetail
{
constexpr double CellCm=400.,PlaneToleranceCm=.15;
struct FLocalTriangle{FVector A,B,C,Normal;};
void AddConvexTriangle(TArray<FLocalTriangle>& Out,FVector A,FVector B,FVector C,const FVector& Centre)
{
    FVector N=FVector::CrossProduct(B-A,C-A).GetSafeNormal();
    if(FVector::DotProduct(N,(A+B+C)/3.-Centre)<0){N=-N;}
    if(!N.IsZero()){Out.Add({A,B,C,N});}
}
void ReadCollision(UBodySetup& Setup,TArray<FLocalTriangle>& Out)
{
    if(Setup.GetCollisionTraceFlag()==CTF_UseComplexAsSimple)
    {
        for(const auto& Mesh:Setup.TriMeshGeometries)
        {
            if(!Mesh){continue;}
            const auto Read=[&](const auto& Indices)
            {
                for(int32 I=0;I<Indices.Num();++I)
                {
                    const auto& T=Indices[I];
                    Out.Add({FVector(Mesh->Particles().GetX(T[0])),FVector(Mesh->Particles().GetX(T[1])),
                        FVector(Mesh->Particles().GetX(T[2])),FVector(Mesh->GetFaceNormal(I))});
                }
            };
            const auto& E=Mesh->Elements();
            if(E.RequiresLargeIndices()){Read(E.GetLargeIndexBuffer());}else{Read(E.GetSmallIndexBuffer());}
        }
        return;
    }
    for(const auto& Box:Setup.AggGeom.BoxElems)
    {
        const FTransform T=Box.GetTransform();FVector V[8];
        for(int32 I=0;I<8;++I){V[I]=T.TransformPosition(FVector((I&1?1.:-1.)*Box.X*.5,
            (I&2?1.:-1.)*Box.Y*.5,(I&4?1.:-1.)*Box.Z*.5));}
        const int32 Faces[6][4]={{0,1,3,2},{4,6,7,5},{0,4,5,1},{2,3,7,6},{0,2,6,4},{1,5,7,3}};
        for(const auto& F:Faces)
        {
            AddConvexTriangle(Out,V[F[0]],V[F[1]],V[F[2]],Box.Center);
            AddConvexTriangle(Out,V[F[0]],V[F[2]],V[F[3]],Box.Center);
        }
    }
    for(const auto& Hull:Setup.AggGeom.ConvexElems)
    {
        if(Hull.VertexData.IsEmpty()){continue;}
        const FTransform T=Hull.GetTransform();FVector Centre=FVector::ZeroVector;
        for(const auto& V:Hull.VertexData){Centre+=T.TransformPosition(V)/Hull.VertexData.Num();}
        for(int32 I=0;I+2<Hull.IndexData.Num();I+=3)
        {AddConvexTriangle(Out,T.TransformPosition(Hull.VertexData[Hull.IndexData[I]]),
            T.TransformPosition(Hull.VertexData[Hull.IndexData[I+1]]),T.TransformPosition(Hull.VertexData[Hull.IndexData[I+2]]),Centre);}
    }
}
bool Clip(const FVector2D& From,const FVector2D& To,const FVector2D* V,double& Lo,double& Hi)
{
    Lo=0;Hi=1;
    for(int32 I=0;I<3;++I)
    {
        const FVector2D Edge=V[(I+1)%3]-V[I];
        const double C=FVector2D::CrossProduct(Edge,From-V[I]),D=FVector2D::CrossProduct(Edge,To-From);
        if(FMath::Abs(D)<UE_DOUBLE_SMALL_NUMBER){if(C<0){return false;}}
        else if(D>0){Lo=FMath::Max(Lo,-C/D);}else{Hi=FMath::Min(Hi,-C/D);}
    }
    return Lo<=Hi;
}
}

FLDVisionSupport::FKey FLDVisionSupport::Key(const FVector& N,double H)
{
    FKey K;K.Normal=FIntVector(FMath::RoundToInt32(N.X*10000.),FMath::RoundToInt32(N.Y*10000.),FMath::RoundToInt32(N.Z*10000.));
    K.Height=FMath::RoundToInt32(H*10.);return K;
}

void FLDVisionSupport::Build(UWorld& World)
{
    using namespace LDVisionSupportDetail;
    Planes.Reset();TMap<UBodySetup*,TArray<FLocalTriangle>> Prototypes;
    const double WalkableZ=GetDefault<ALDCharacter>()->GetCharacterMovement()->GetWalkableFloorZ();
    for(TActorIterator<AActor> It(&World);It;++It)
    {
        TInlineComponentArray<UPrimitiveComponent*> Parts(*It);
        for(auto* Part:Parts)
        {
            if(!LDNativeGround::IsSupport(*Part)){continue;}
            auto* Setup=Part->GetBodySetup();if(!Setup){continue;}
            auto* Local=Prototypes.Find(Setup);
            if(!Local){Local=&Prototypes.Add(Setup);ReadCollision(*Setup,*Local);}
            const FTransform T=Part->GetComponentTransform();const FVector Scale=T.GetScale3D();
            if(Scale.GetAbsMin()<UE_DOUBLE_SMALL_NUMBER){continue;}
            for(const auto& Triangle:*Local)
            {
                const FVector N=T.TransformVectorNoScale(Triangle.Normal/Scale).GetSafeNormal();
                if(N.Z<WalkableZ){continue;}
                const FVector A=T.TransformPosition(Triangle.A),B=T.TransformPosition(Triangle.B),C=T.TransformPosition(Triangle.C);
                const double H=FVector::DotProduct(N,A);
                auto& Plane=Planes.FindOrAdd(Key(N,H));
                FTriangle Face{FVector2D(A),FVector2D(B),FVector2D(C),N,H};
                if(FVector2D::CrossProduct(Face.B-Face.A,Face.C-Face.A)<0){Swap(Face.B,Face.C);}
                if(FMath::Abs(FVector2D::CrossProduct(Face.B-Face.A,Face.C-Face.A))<UE_DOUBLE_SMALL_NUMBER){continue;}
                const int32 Index=Plane.Triangles.Add(Face);
                FBox2D Bounds(ForceInit);Bounds+=Face.A;Bounds+=Face.B;Bounds+=Face.C;
                for(int32 Y=FMath::FloorToInt32(Bounds.Min.Y/CellCm);Y<=FMath::FloorToInt32(Bounds.Max.Y/CellCm);++Y)
                {for(int32 X=FMath::FloorToInt32(Bounds.Min.X/CellCm);X<=FMath::FloorToInt32(Bounds.Max.X/CellCm);++X)
                    {Plane.Cells.FindOrAdd(FIntPoint(X,Y)).Add(Index);}}
            }
        }
    }
}

double FLDVisionSupport::DepartureExit(const FVector& From,const FVector& To,const FHitResult& Support) const
{
    using namespace LDVisionSupportDetail;
    if(!Support.GetComponent()){return 0;}
    const FVector N=Support.ImpactNormal;
    const FVector2D A(From),B(To);
    const double Distance=FVector2D::Distance(A,B);if(Distance<.001){return 0;}
    const FKey K=Key(N,FVector::DotProduct(N,From));
    TArray<FVector2D,TInlineAllocator<32>> Intervals;
    for(int32 Neighbour=-1;Neighbour<=1;++Neighbour)
    {
        FKey Nearby=K;Nearby.Height+=Neighbour;
        const auto* Plane=Planes.Find(Nearby);if(!Plane){continue;}
        TBitArray<TInlineAllocator<4>> Seen(false,Plane->Triangles.Num());
        for(int32 Y=FMath::FloorToInt32(FMath::Min(A.Y,B.Y)/CellCm);Y<=FMath::FloorToInt32(FMath::Max(A.Y,B.Y)/CellCm);++Y)
        {for(int32 X=FMath::FloorToInt32(FMath::Min(A.X,B.X)/CellCm);X<=FMath::FloorToInt32(FMath::Max(A.X,B.X)/CellCm);++X)
        {
            const auto* Indices=Plane->Cells.Find(FIntPoint(X,Y));if(!Indices){continue;}
            for(int32 I:*Indices)
            {
                if(Seen[I]){continue;}Seen[I]=true;const auto& F=Plane->Triangles[I];
                if(FMath::Abs(FVector::DotProduct(F.Normal,From)-F.Height)>PlaneToleranceCm){continue;}
                const FVector2D V[3]={F.A,F.B,F.C};double Lo,Hi;
                if(Clip(A,B,V,Lo,Hi)){Intervals.Add(FVector2D(Lo,Hi));}
            }
        }}
    }
    Intervals.Sort([](const FVector2D& L,const FVector2D& R){return L.X<R.X;});
    double Exit=0;
    for(const auto& Interval:Intervals)
    {if(Interval.X>Exit+PlaneToleranceCm/Distance){break;}Exit=FMath::Max(Exit,Interval.Y);}
    return Exit;
}
