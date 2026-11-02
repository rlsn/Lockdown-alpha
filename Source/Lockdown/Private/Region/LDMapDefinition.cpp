#include "Region/LDMapDefinition.h"
#include "LDMapGeometryJson.h"
#include "Framework/LDDemoEntry.h"
#include "../Development/LDDemoContentJson.h"
#include "EngineUtils.h"
#include "Engine/World.h"

namespace
{
TArray<uint8> SourceBytes(const FString& Text)
{
    const FTCHARToUTF8 Bytes(*Text);
    TArray<uint8> Result;
    Result.Append(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
    return Result;
}
double Blend(double Distance, double Inner, double Apron)
{
    const double T = FMath::Clamp((Distance - Inner) / Apron, 0., 1.);
    return 1 - T * T * (3 - 2 * T);
}
double Nearest(const FLDMapLine& Line, const FVector2D& XY, double& Z)
{
    double Best = DBL_MAX;
    for (int32 I = 1; I < Line.Points.Num(); ++I)
    {
        const FVector A = Line.Points[I-1], B = Line.Points[I];
        const FVector2D Start(A.X, A.Y), Delta(B.X-A.X, B.Y-A.Y);
        const double T = FMath::Clamp(FVector2D::DotProduct(XY-Start, Delta) / Delta.SizeSquared(), 0., 1.);
        const double D = (XY-Start-Delta*T).Size();
        if (D < Best) { Best = D; Z = FMath::Lerp(A.Z, B.Z, T); }
    }
    return Best;
}
}

bool ULDMapDefinition::Import(const FString& Json, FString& Error)
{
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    return LDDemoEntry::LoadRuntimeContent(Content, Error) && Import(SourceBytes(Json), *Content, Error);
}

bool ULDMapDefinition::Import(const TArray<uint8>& Json, const FLDDemoContentSnapshot& Content, FString& Error)
{
    Error.Reset();
    LDDemoContent::FCheck Check(Error);
    TUniquePtr<FLDDemoPreparedWorld> Prepared;
    if (!Check.Require(IsInGameThread(), TEXT("world import"), TEXT("import requires the game thread"))
        || !PrepareProjection(Json, Content, Prepared, Error))
    {
        return false;
    }
    DemoGeometry = MoveTemp(Prepared->Geometry);
    ContentRoot = Prepared->ContentRoot;
    SourceJson = MoveTemp(Prepared->SourceJson);
    Checksum = MoveTemp(Prepared->Checksum);
    Seed = LayoutRevision = FitterRevision = 0;
    POIs.Reset(); Spawns.Reset(); Settlements.Reset(); Buildings.Reset(); Plateaus.Reset();
    Routes.Reset(); Rivers.Reset(); Bridges.Reset(); Services.Reset(); Aliases.Reset();
    WetInset = FVector::ZeroVector; WetSize = FVector2D::ZeroVector; CivicHeartOverride = 0;
    MarkPackageDirty();
    return true;
}

bool ULDMapDefinition::PrepareProjection(const TArray<uint8>& Json, const FLDDemoContentSnapshot& Content,
    TUniquePtr<FLDDemoPreparedWorld>& Out, FString& Error)
{
    Error.Reset();
    LDDemoContent::FCheck Check(Error);
    LDDemoContent::FValue Root;
    FLDDemoContentDigest Hash;
    TUniquePtr<FLDDemoPreparedWorld> Candidate(new FLDDemoPreparedWorld());
    if (!LDDemoContent::Hash(Json, Hash, Check, TEXT("world"))
        || !Check.Require(Hash == Content.Get().Identity.World, TEXT("world"), TEXT("bytes differ from the trusted content root"))
        || !LDDemoContent::Parse(Json, Root, Check, TEXT("world"))
        || !LDDemoWorld::Project(Root, Content.Get(), Candidate->Geometry, Check))
    {
        return false;
    }
    const FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Json.GetData()), Json.Num());
    Candidate->SourceJson = FString(Decoded.Length(), Decoded.Get());
    Candidate->Checksum = Hash.ToHex();
    Candidate->ContentRoot = Content.Get().Identity.Root;
    Out = MoveTemp(Candidate);
    return true;
}

bool ULDMapDefinition::ValidatePrepared(const FLDDemoPreparedWorld& Expected, FString& Error) const
{
    Error.Reset();
    LDDemoContent::FCheck Check(Error);
    if (!Check.Require(ContentRoot == Expected.ContentRoot, TEXT("world asset"), TEXT("asset belongs to another content root"))
        || !Check.Require(SourceJson.Equals(Expected.SourceJson, ESearchCase::CaseSensitive)
            && Checksum.Equals(Expected.Checksum, ESearchCase::CaseSensitive),
            TEXT("world asset"), TEXT("saved world bytes differ from the trusted root"))
        || !Check.Require(LDDemoContent::ExactStoredStruct(FLDDemoWorldGeometry::StaticStruct(), &DemoGeometry, &Expected.Geometry),
            TEXT("world asset"), TEXT("saved geometry differs from its complete typed projection"))
        || !Check.Require(Seed == 0 && LayoutRevision == 0 && FitterRevision == 0
            && POIs.IsEmpty() && Spawns.IsEmpty() && Settlements.IsEmpty() && Buildings.IsEmpty() && Plateaus.IsEmpty()
            && Routes.IsEmpty() && Rivers.IsEmpty() && Bridges.IsEmpty() && Services.IsEmpty() && Aliases.IsEmpty()
            && WetInset == FVector::ZeroVector && WetSize == FVector2D::ZeroVector && CivicHeartOverride == 0,
            TEXT("world asset"), TEXT("retired city/river/service fields cannot accompany the Demo projection")))
    {
        return false;
    }
    return true;
}

bool ULDMapDefinition::Read(const FLDDemoContentSnapshot& Content, TSharedPtr<const FLDDemoWorldGeometry>& Out, FString& Error) const
{
    TUniquePtr<FLDDemoPreparedWorld> Expected;
    if (!PrepareProjection(SourceBytes(SourceJson), Content, Expected, Error) || !ValidatePrepared(*Expected, Error)) { return false; }
    Out = MakeShared<const FLDDemoWorldGeometry>(MoveTemp(Expected->Geometry));
    return true;
}

bool ULDMapDefinition::Validate(FString& Error) const
{
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TSharedPtr<const FLDDemoWorldGeometry> Geometry;
    return LDDemoEntry::LoadRuntimeContent(Content, Error) && Read(*Content, Geometry, Error);
}

const FLDMapPoint* ULDMapDefinition::Find(FName Id) const
{ return POIs.FindByPredicate([&](const auto& P) { return P.Id == Id; }); }
FVector ULDMapDefinition::NavigationTarget(FName Id) const
{
    const auto* P=Find(Id); if(!P) { return FVector::ZeroVector; }
    FVector Target=P->Position;
    if(Bridges.Contains(Id)) { Target.Z=FMath::Max(300.,Target.Z); }
    return Target;
}
FName ULDMapDefinition::CanonicalId(FName Id) const
{ if(Find(Id)) { return Id; } const auto* Alias=Aliases.Find(Id); return Alias?*Alias:NAME_None; }
const FLDMapService* ULDMapDefinition::ServiceFor(FName POI) const
{ return Services.FindByPredicate([&](const auto& S){return S.POI==CanonicalId(POI);}); }
FName ULDMapDefinition::CivicPolicy(const FVector& P) const
{
    if(!InBounds(P)) { return TEXT("outside"); }
    if(const auto* H=Find(TEXT("heart"));H&&FVector::DistSquared2D(P,H->Position)<=CivicHeartOverride*CivicHeartOverride) { return TEXT("contested_precinct"); }
    for(const auto& S:Settlements) { if(FVector::DistSquared2D(P,S.Position)<=S.CivicRadius*S.CivicRadius) { return TEXT("civic_core"); } }
    return TEXT("unwatched_edge");
}

double ULDMapDefinition::Height(const FVector2D& XY) const
{
    // Lowland, terrace and ridge with broad continuous aprons. Noise never changes pads.
    double Z = 0;
    for (const auto& P : Plateaus)
    { Z = FMath::Max(Z, P.Position.Z * Blend((XY-FVector2D(P.Position)).Size(), P.Radius, 6000)); }
    // Roads fit terrain; they are not elevated collision ribbons over disconnected land.
    double Best = DBL_MAX, RoadZ = Z, RoadWeight = 0;
    for (const auto& R : Routes)
    {
        double Target = 0; const double Distance = Nearest(R, XY, Target);
        const double EdgeDistance = Distance-R.Width*.5;
        if (EdgeDistance < Best) { Best = EdgeDistance; RoadZ = Target; RoadWeight = Blend(Distance, R.Width*.5, 6000); }
    }
    Z = FMath::Lerp(Z, RoadZ, RoadWeight);
    for (const auto& S : Settlements)
    { Z = FMath::Lerp(Z, S.Position.Z, Blend((XY-FVector2D(S.Position)).Size(), S.Radius, 6000)); }
    // Fit independent wild POIs to their authored bands, preserving IDs and source Z.
    Best = DBL_MAX; const FLDMapPoint* Pad = nullptr;
    for (const auto& P : POIs)
    {
        if (Bridges.Contains(P.Id)) { continue; }
        const double Distance = (XY-FVector2D(P.Position)).Size();
        if (Distance < Best) { Best = Distance; Pad = &P; }
    }
    if (Pad) { Z = FMath::Lerp(Z, Pad->Position.Z, Blend(Best, 800, 6000)); }
    // Floors stay permanently connected under bridge decks. Expanded bank aprons fit <=25% grade.
    for (const auto& R : Rivers)
    { double Floor = 0; const double Distance = Nearest(R, XY, Floor);
      const double Apron=FMath::Max(3000.,FMath::Max(R.Bank,FMath::Abs(Z-Floor)*6.5));
      Z = FMath::Lerp(Z, Floor, Blend(Distance, R.Width*.5, Apron)); }
    return Z;
}

bool ULDMapDefinition::RiverAt(const FVector& P) const
{
    if (FMath::Abs(P.X-WetInset.X) <= WetSize.X*.5 && FMath::Abs(P.Y-WetInset.Y) <= WetSize.Y*.5 && FMath::Abs(P.Z-WetInset.Z) < 40) { return true; }
    for (const auto& R : Rivers)
    { double Floor = 0; if (Nearest(R, FVector2D(P), Floor) <= R.Width*.5 && FMath::Abs(P.Z-Floor) < 40) { return true; } }
    return false;
}
bool ULDMapDefinition::InBounds(const FVector& P)
{ return !P.ContainsNaN() && FMath::Abs(P.X) <= 118000 && FMath::Abs(P.Y) <= 118000 && P.Z >= -100 && P.Z <= 1600; }
const ULDMapDefinition* ULDMapDefinition::Get(const UWorld* W)
{ if (W) { for (TActorIterator<ALDMapAnchor> It(W); It; ++It) { return It->Definition; } } return nullptr; }
ALDMapAnchor::ALDMapAnchor()
{ bReplicates = false; RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root")); }

// Semantic side-ledge tags sit halfway across each authored plateau apron.
// Approved route aprons are ramp openings in the ring, never opaque edges.
bool ULDMapDefinition::UpwardLedge(const FVector& From,const FVector& To) const
{
    if(To.Z-From.Z<100){return false;}
    const double Low=Height(FVector2D(From)),High=Height(FVector2D(To));
    if(High-Low<100||From.Z>Low+200){return false;}
    for(const auto& P:Plateaus)
    {
        const FVector2D C(P.Position),A(From),B(To);const double R=P.Radius+3000;
        if((A-C).Size()<=R||(B-C).Size()>=R){continue;}
        const FVector2D V=B-A,D=A-C;const double AA=V.SizeSquared(),BB=2*FVector2D::DotProduct(D,V),CC=D.SizeSquared()-R*R,Disc=BB*BB-4*AA*CC;
        if(AA<1||Disc<0){continue;}const double T=(-BB-FMath::Sqrt(Disc))/(2*AA);if(T<0||T>1){continue;}
        const FVector2D Edge=A+V*T;bool Ramp=false;
        for(const auto& Road:Routes){for(int I=1;I<Road.Points.Num();++I){const FVector Q(Edge,0),L(Road.Points[I-1].X,Road.Points[I-1].Y,0),U(Road.Points[I].X,Road.Points[I].Y,0);if(FMath::PointDistToSegment(Q,L,U)<=Road.Width*.5+6000){Ramp=true;break;}}if(Ramp){break;}}
        if(!Ramp){return true;}
    }
    return false;
}
