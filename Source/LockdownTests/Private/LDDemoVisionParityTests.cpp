#include "Vision/LDVisionDirector.h"
#include "Region/LDMapDefinition.h"
#include "Framework/LDDemoEntry.h"
#include "Development/LDDemoContentCatalog.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace LDDemoVisionParityTests
{
namespace
{
// Independent unindexed 3D prism oracle: the production narrow phase clips 2D footprints and heights.
struct FReferenceSight
{
    struct FBlocker
    {
        TArray<FVector2D> Polygon;
        FVector2D Centre=FVector2D::ZeroVector;
        double Radius=0,Base=0,Height=0;
    };
    struct FTriangle { FVector A,B,C; bool bTerrain; };
    LDDemoWorld::FTerrain Terrain;
    TArray<FBlocker> Blockers;
    TArray<FTriangle> Triangles;
    FVector2D Min=FVector2D::ZeroVector,Max=FVector2D::ZeroVector;
    FBox2D CoverageBounds{ForceInit};
    FLDDemoVisionRangeParameters Range;
    double Radius=0,EyeHeight=0;
    void Prism(const TArray<FVector>& Top,double Base,bool bTerrain)
    {
        for(int32 I=1;I+1<Top.Num();++I){Triangles.Add({Top[0],Top[I],Top[I+1],bTerrain});}
        for(int32 I=0;I<Top.Num();++I)
        {
            const FVector A=Top[I],B=Top[(I+1)%Top.Num()];
            const FVector LowA(A.X,A.Y,Base),LowB(B.X,B.Y,Base);
            Triangles.Add({A,LowA,B,bTerrain});Triangles.Add({LowA,LowB,B,bTerrain});
        }
    }
    bool Build(const FLDDemoWorldGeometry& Geometry,double InRadius,const FLDDemoVisionRangeParameters& InRange,FString& Error)
    {
        if(!Terrain.Build(Geometry,Error)){return false;}
        FVector Low,High;
        if(!Geometry.Surfaces[0].MinCm.ToVector(Low,Error)||!Geometry.Surfaces[0].MaxCm.ToVector(High,Error)){return false;}
        Min=FVector2D(Low);Max=FVector2D(High);Radius=InRadius;Range=InRange;
        CoverageBounds=LDObservation::SquareCoverageBounds(FBox2D(Min,Max));
        EyeHeight=double(Geometry.SightSampleHeightCm.Numerator)/Geometry.SightSampleHeightCm.Denominator;
        for(const auto& Region:Terrain.Regions)
        {
            TArray<FVector> Top;
            for(const auto& P:Region.Polygon)
            {Top.Add(FVector(P,Region.Base+Region.Rise*FVector2D::CrossProduct(Region.Along,P-Region.Origin)/Region.Width));}
            Prism(Top,0,true);
        }
        for(const auto& Source:Geometry.Blockers)
        {
            if(!Source.BlocksSight){continue;}
            auto& B=Blockers.AddDefaulted_GetRef();
            B.Height=double(Source.HeightCm.Numerator)/Source.HeightCm.Denominator;
            FVector V=FVector::ZeroVector;
            if(Source.Shape==ELDDemoWorldBlockerShape::Cylinder)
            {
                if(!Source.CenterCm.ToVector(V,Error)){return false;}
                B.Centre=FVector2D(V);B.Base=V.Z;B.Radius=double(Source.RadiusCm.Numerator)/Source.RadiusCm.Denominator;
            }
            else
            {
                TArray<FVector> Top;
                for(const auto& Point:Source.PolygonCm)
                {if(!Point.ToVector(V,Error)){return false;}B.Polygon.Add(FVector2D(V));Top.Add(V+FVector(0,0,B.Height));}
                B.Base=V.Z;Prism(Top,B.Base,false);
            }
        }
        return true;
    }
    int32 Tier(const FVector2D& P)const{return Terrain.Tier(P);}
    static bool Intersects(const FVector& Start,const FVector& End,const FTriangle& T)
    {
        const FVector D=End-Start,E1=T.B-T.A,E2=T.C-T.A;
        const FVector P=FVector::CrossProduct(D,E2);
        const double Det=FVector::DotProduct(E1,P);
        if(FMath::Abs(Det)<1.e-10){return false;}
        const FVector S=Start-T.A;
        const double U=FVector::DotProduct(S,P)/Det;
        const FVector Q=FVector::CrossProduct(S,E1);
        const double V=FVector::DotProduct(D,Q)/Det,Distance=FVector::DotProduct(E2,Q)/Det;
        return U>=-1.e-10&&V>=-1.e-10&&U+V<=1.+1.e-10&&Distance>=0&&Distance<=1.;
    }
    double Departure(const FVector& Source,const FVector& Target,const FVector& Normal)const
    {
        const FVector2D A(Source),D=FVector2D(Target)-A;
        TArray<double> Cuts={0,1};TArray<int32> Coplanar;
        for(int32 I=0;I<Terrain.Regions.Num();++I)
        {
            const auto& R=Terrain.Regions[I];const double Slope=R.Rise/R.Width;
            const FVector2D Up(-R.Along.Y,R.Along.X);
            if(!FVector(-Up.X*Slope,-Up.Y*Slope,1).GetSafeNormal().Equals(Normal,.001)
                ||FMath::Abs(R.Base+Slope*FVector2D::CrossProduct(R.Along,A-R.Origin)-Source.Z)>.15){continue;}
            Coplanar.Add(I);
            for(int32 J=0;J<R.Polygon.Num();++J)
            {
                const FVector2D P=R.Polygon[J],Edge=R.Polygon[(J+1)%R.Polygon.Num()]-P;
                const double Den=FVector2D::CrossProduct(D,Edge);
                if(FMath::Abs(Den)<1.e-10){continue;}
                const double T=FVector2D::CrossProduct(P-A,Edge)/Den,U=FVector2D::CrossProduct(P-A,D)/Den;
                if(T>=0&&T<=1&&U>=0&&U<=1){Cuts.Add(T);}
            }
        }
        Cuts.Sort();double Exit=0;
        for(int32 I=1;I<Cuts.Num();++I)
        {
            if(Cuts[I]-Cuts[I-1]<1.e-10){continue;}
            const FVector2D Mid=A+D*((Cuts[I]+Cuts[I-1])*.5);bool Inside=false;
            for(int32 J:Coplanar)
            {
                const auto& P=Terrain.Regions[J].Polygon;bool This=true;
                for(int32 K=0;K<P.Num();++K)
                {if(FVector2D::CrossProduct(P[(K+1)%P.Num()]-P[K],Mid-P[K])<0){This=false;break;}}
                if(This){Inside=true;break;}
            }
            if(!Inside){break;}Exit=Cuts[I];
        }
        return Exit;
    }
    ELDSightResult Trace(const FVector& Source,const FVector& Target)const
    {
        const auto Valid=[this](const FVector& P)
        {return !P.ContainsNaN()&&FMath::IsFinite(P.Z)&&P.X>=Min.X&&P.X<=Max.X&&P.Y>=Min.Y&&P.Y<=Max.Y;};
        if(!Valid(Source)||!Valid(Target)){return ELDSightResult::InvalidPoint;}
        double A,B;FVector SourceNormal,TargetNormal;Terrain.Ground(FVector2D(Source),A,SourceNormal);Terrain.Ground(FVector2D(Target),B,TargetNormal);
        const double LocalRadius=Radius*LDObservation::HeightRadiusScale(Range,A,B);
        const double DistanceFractionSquared=FVector::DistSquared2D(Source,Target)/FMath::Square(LocalRadius);
        if(DistanceFractionSquared>1.){return ELDSightResult::OutOfRange;}
        const FVector Eye(Source.X,Source.Y,A+EyeHeight),GroundEnd(Target.X,Target.Y,B+.1);
        // For uphill terrain the guard ray samples progressively lower from the eye as distance increases.
        const FVector TerrainEye=Eye-FVector(0,0,.5*FMath::Clamp(B-A,0.,EyeHeight)*DistanceFractionSquared);
        const FVector TerrainEnd(Target.X,Target.Y,FMath::Max(A,B)+.1);
        const double Exit=B<A?Departure(FVector(Source.X,Source.Y,A),FVector(Target.X,Target.Y,B),SourceNormal):1.;
        const FVector Start=FMath::Lerp(Eye,GroundEnd,FMath::Min(1.,Exit+.000001));
        if(B<A&&Exit<1.)
        {
            double Z;FVector N;Terrain.Ground(FVector2D(Start),Z,N);
            if(Z>Start.Z){return ELDSightResult::Blocked;}
        }
        for(const auto& T:Triangles)
        {
            if(Intersects(T.bTerrain?TerrainEye:Eye,T.bTerrain?TerrainEnd:GroundEnd,T)
                ||(T.bTerrain&&B<A&&Exit<1.&&Intersects(Start,GroundEnd,T))){return ELDSightResult::Blocked;}
        }
        // Cylinder oracle intersects the ray's vertical slab, then finds its closest horizontal point.
        for(const auto& C:Blockers)
        {
            if(C.Radius<=0)
            {
                const auto Inside=[&](const FVector& P)
                {
                    if(P.Z<C.Base||P.Z>C.Base+C.Height){return false;}
                    for(int32 I=0;I<C.Polygon.Num();++I)
                    {if(FVector2D::CrossProduct(C.Polygon[(I+1)%C.Polygon.Num()]-C.Polygon[I],FVector2D(P)-C.Polygon[I])<0){return false;}}
                    return true;
                };
                if(Inside(Eye)||Inside(GroundEnd)){return ELDSightResult::Blocked;}
                continue;
            }
            double Lo=0,Hi=1;
            const FVector D=GroundEnd-Eye;
            if(FMath::Abs(D.Z)<1.e-10){if(Eye.Z<C.Base||Eye.Z>C.Base+C.Height){continue;}}
            else
            {
                const double X=(C.Base-Eye.Z)/D.Z,Y=(C.Base+C.Height-Eye.Z)/D.Z;
                Lo=FMath::Max(Lo,FMath::Min(X,Y));Hi=FMath::Min(Hi,FMath::Max(X,Y));
            }
            if(Lo>Hi){continue;}
            const FVector2D P(Eye),Delta(D);
            const double T=Delta.SizeSquared()>0?FMath::Clamp(FVector2D::DotProduct(C.Centre-P,Delta)/Delta.SizeSquared(),Lo,Hi):Lo;
            if(FVector2D::DistSquared(P+Delta*T,C.Centre)<=C.Radius*C.Radius){return ELDSightResult::Blocked;}
        }
        return ELDSightResult::Visible;
    }
    void Coverage(const FVector& Source,TArray<uint8>& Out)const
    {
        Out.Init(0,LDObservation::CoverageBytes);
        const FVector2D Span=CoverageBounds.GetSize();
        for(int32 I=0;I<LDObservation::CoverageCells;++I)
        {
            const FVector2D P=CoverageBounds.Min+FVector2D(((I%LDObservation::CoverageWidth)+.5)*Span.X/LDObservation::CoverageWidth,
                ((I/LDObservation::CoverageWidth)+.5)*Span.Y/LDObservation::CoverageHeight);
            if(Trace(Source,FVector(P,0))==ELDSightResult::Visible){LDObservation::SetCoverageBit(Out,I);}
        }
    }
};}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoVisionGeometryParityTest, "Lockdown.Rules.DemoVision.GeometryParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoVisionGeometryParityTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TSharedPtr<const FLDDemoWorldGeometry> Geometry;
    FString Error;
    const auto* Definition = LoadObject<ULDMapDefinition>(nullptr, *(FString(LDDemoEntry::WorldDefinition) + TEXT(".DA_DemoWorld")));
    if (!TestNotNull(TEXT("Registered Demo world asset"), Definition)
        || !TestTrue(TEXT("Approved runtime content"), LDDemoEntry::LoadRuntimeContent(Content, Error))
        || !TestTrue(TEXT("Approved world projection"), Definition->Read(*Content, Geometry, Error))) { AddError(Error); return false; }
    FLDWorldRevision World;
    World.MapId = FName(*Geometry->MapId);
    World.LayoutRevision = Geometry->LayoutRevision;
    World.GeometryRevision = Geometry->GeometryRevision;
    FLDSightGeometry Sight;
    FReferenceSight Reference;
    const auto& Radius = Content->Get().Parameters.VisionRadiusCm;
    if (!TestTrue(TEXT("Indexed sight builds"), Sight.Build(*Geometry, World, Radius, Content->Get().Parameters.VisionRange, Error))
        || !TestTrue(TEXT("Reference sight builds"), Reference.Build(*Geometry, Sight.Radius(), Content->Get().Parameters.VisionRange, Error))) { AddError(Error); return false; }
    TArray<FVector> Sources;
    Sources.Add(FVector(Reference.Min, 0));
    Sources.Add(FVector(Reference.Max, 0));
    Sources.Add(FVector(Reference.Min.X, Reference.Max.Y, 0));
    Sources.Add(FVector(Reference.Max.X, Reference.Min.Y, 0));
    for (const auto& Anchor : Geometry->Anchors)
    {
        FVector At;
        if (!Anchor.PositionCm.ToVector(At, Error)) { AddError(Error); return false; }
        Sources.Add(At);
    }
    const auto ProbePolygon = [&](const TArray<FVector2D>& Polygon)
    {
        FVector2D Centre = FVector2D::ZeroVector;
        for (int32 I = 0; I < Polygon.Num(); ++I)
        {
            Centre += Polygon[I] / Polygon.Num();
            const FVector2D Mid = (Polygon[I] + Polygon[(I + 1) % Polygon.Num()]) * .5;
            for (const FVector2D& At : {Polygon[I], Mid})
            {
                for (const FVector2D& Offset : {FVector2D::ZeroVector, FVector2D(.000001, .000001), FVector2D(-.000001, -.000001)})
                {
                    const FVector2D Point = At + Offset;
                    TestEqual(TEXT("Tier parity at terrain/blocker corners and edges"), Reference.Terrain.Tier(Point), Reference.Tier(Point));
                    const FVector A(Point, 0), B(Point + FVector2D(80, -80), 0);
                    TestEqual(*FString::Printf(TEXT("Trace parity at %s to %s"),*A.ToString(),*B.ToString()),int32(Sight.Trace(A,B)),int32(Reference.Trace(A,B)));
                }
            }
        }
        Sources.Add(FVector(Centre, 0));
    };
    for (const auto& Region : Reference.Terrain.Regions) { ProbePolygon(Region.Polygon); }
    for (const FReferenceSight::FBlocker& Blocker : Reference.Blockers)
    {
        if (Blocker.Radius == 0) { ProbePolygon(Blocker.Polygon); }
        else
        {
            const FVector A(Blocker.Centre + FVector2D(-100, Blocker.Radius), 0);
            const FVector B(Blocker.Centre + FVector2D(100, Blocker.Radius), 0);
            TestTrue(TEXT("Closed trunk tangent parity"), Sight.Trace(A, B) == Reference.Trace(A, B));
        }
    }
    FRandomStream Random(178);
    for (int32 I = 0; I < 2048; ++I)
    {
        const FVector2D Point(Random.FRandRange(Reference.Min.X - 400, Reference.Max.X + 400),
            Random.FRandRange(Reference.Min.Y - 400, Reference.Max.Y + 400));
        const FVector A(Point, 0), B(Point + FVector2D(Random.FRandRange(-1200, 1200), Random.FRandRange(-1200, 1200)), 0);
        TestEqual(TEXT("Seeded tier parity including beyond the map"), Reference.Terrain.Tier(Point), Reference.Tier(Point));
        TestEqual(*FString::Printf(TEXT("Seeded trace parity at %s to %s"),*A.ToString(),*B.ToString()),int32(Sight.Trace(A,B)),int32(Reference.Trace(A,B)));
    }
    Sources.SetNum(FMath::Min(12, Sources.Num()));
    for (const FVector& Source : Sources)
    {
        TArray<uint8> Actual, Expected;
        Sight.Coverage(Source, Actual);
        Reference.Coverage(Source, Expected);
        if (!TestTrue(*FString::Printf(TEXT("All coverage bits agree at %s"), *Source.ToString()), Actual == Expected))
        { return false; }
    }
    AddInfo(FString::Printf(TEXT("Exact full-bitmap parity at %d representative anchors and map corners against unindexed 3D prisms."), Sources.Num()));
    return true;
}
}
#endif
