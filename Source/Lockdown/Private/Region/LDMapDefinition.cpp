#include "Region/LDMapDefinition.h"
#include "LDB90Contract.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/SecureHash.h"
#include "EngineUtils.h"
#include "Engine/World.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

namespace
{
FString Hash(const FString& Text)
{
    FTCHARToUTF8 Bytes(*Text); FSHA256Signature Result;
    SHA256(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length(), Result.Signature);
    return Result.ToString().ToLower();
}
FVector Vector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key)
{
    const auto& Values = Object->GetArrayField(Key);
    return FVector(Values[0]->AsNumber(), Values[1]->AsNumber(), Values[2]->AsNumber());
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
    const FString Normalized = Json.Replace(TEXT("\r\n"), TEXT("\n"));
    // Closed compiler contract rejects unknown fields, units and behavior before parsing/spawning.
    if (Hash(Normalized) != LD_B90_MAP_SHA) { Error = TEXT("B90 payload checksum differs; run SyncV09Data.py and rebuild"); return false; }
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Normalized), Root) || !Root
        || Root->GetIntegerField(TEXT("schema")) != 9 || Root->GetStringField(TEXT("stage")) != TEXT("B90")
        || Root->GetStringField(TEXT("profile")) != TEXT("v09_full"))
    { Error = TEXT("Invalid B90 map schema/profile"); return false; }
    SourceJson = Normalized; Checksum = Hash(SourceJson);
    Seed = Root->GetIntegerField(TEXT("seed")); LayoutRevision = Root->GetIntegerField(TEXT("revision"));
    FitterRevision = Root->GetIntegerField(TEXT("fitter_revision"));
    auto ReadPoints = [&](const TCHAR* Key, TArray<FLDMapPoint>& Out)
    {
        Out.Reset();
        for (const auto& Value : Root->GetArrayField(Key))
        {
            const auto Object = Value->AsObject(); FLDMapPoint Point;
            Point.Id = FName(*Object->GetStringField(TEXT("id"))); Point.Position = Vector(Object, TEXT("position"));
            Object->TryGetStringField(TEXT("name"), Point.Name); Object->TryGetStringField(TEXT("kind"), Point.Kind);
            Object->TryGetStringField(TEXT("identity"),Point.Identity); Object->TryGetStringField(TEXT("security"),Point.Security);
            FString Settlement,POI; Object->TryGetStringField(TEXT("settlement"),Settlement); Object->TryGetStringField(TEXT("poi"),POI);
            Point.Settlement=FName(*Settlement); Point.POI=FName(*POI);
            Object->TryGetNumberField(TEXT("civic_radius"),Point.CivicRadius);
            if(Object->HasField(TEXT("floors"))) { Point.Floors=Object->GetIntegerField(TEXT("floors")); }
            Object->TryGetBoolField(TEXT("roof"),Point.Roof);
            for(const TCHAR* Field:{TEXT("functions"),TEXT("doors")}) { if(Object->HasField(Field))
            { auto& Values=FCString::Strcmp(Field,TEXT("functions"))==0?Point.Functions:Point.Doors;
              for(const auto& Item:Object->GetArrayField(Field)) { Values.Add(FName(*Item->AsString())); } } }
            Object->TryGetNumberField(TEXT("radius"), Point.Radius); Object->TryGetBoolField(TEXT("enterable"), Point.Enterable);
            if (Object->HasField(TEXT("size"))) { Point.Size = Vector(Object, TEXT("size")); }
            Out.Add(Point);
        }
    };
    ReadPoints(TEXT("pois"), POIs); ReadPoints(TEXT("spawns"), Spawns);
    ReadPoints(TEXT("settlements"), Settlements); ReadPoints(TEXT("buildings"), Buildings); ReadPoints(TEXT("plateaus"), Plateaus);
    auto ReadLines = [&](const TCHAR* Key, TArray<FLDMapLine>& Out)
    {
        Out.Reset();
        for (const auto& Value : Root->GetArrayField(Key))
        {
            const auto Object = Value->AsObject(); FLDMapLine Line;
            Line.Id = FName(*Object->GetStringField(TEXT("id"))); Line.Width = Object->GetNumberField(TEXT("width"));
            FString From, To; Object->TryGetStringField(TEXT("start"), From); Object->TryGetStringField(TEXT("end"), To);
            Line.From = FName(*From); Line.To = FName(*To); Object->TryGetNumberField(TEXT("bank"), Line.Bank);
            for (const auto& V : Object->GetArrayField(TEXT("points")))
            { const auto& A = V->AsArray(); Line.Points.Add(FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber())); }
            Out.Add(Line);
        }
    };
    ReadLines(TEXT("routes"), Routes); ReadLines(TEXT("rivers"), Rivers);
    Bridges.Reset(); for (const auto& Value : Root->GetArrayField(TEXT("bridges")))
    { Bridges.Add(FName(*Value->AsObject()->GetStringField(TEXT("id")))); }
    const auto Inset = Root->GetObjectField(TEXT("wet_inset")); WetInset = Vector(Inset, TEXT("position"));
    const auto& Size = Inset->GetArrayField(TEXT("size")); WetSize = FVector2D(Size[0]->AsNumber(), Size[1]->AsNumber());
    Services.Reset(); for(const auto& V:Root->GetArrayField(TEXT("services")))
    { const auto O=V->AsObject(); FLDMapService S;
      S.Id=FName(*O->GetStringField(TEXT("id"))); S.POI=FName(*O->GetStringField(TEXT("poi")));
      S.Building=FName(*O->GetStringField(TEXT("building"))); S.Settlement=FName(*O->GetStringField(TEXT("settlement")));
      S.Position=Vector(O,TEXT("position"));
      for(const auto& F:O->GetArrayField(TEXT("functions"))) { S.Functions.Add(FName(*F->AsString())); } Services.Add(S); }
    Aliases.Reset(); for(const auto& A:Root->GetObjectField(TEXT("aliases"))->Values) { Aliases.Add(FName(*A.Key),FName(*A.Value->AsString())); }
    CivicHeartOverride=Root->GetNumberField(TEXT("civic_heart_override"));
    return Validate(Error);
}

bool ULDMapDefinition::Validate(FString& Error) const
{
    auto Fail = [&](const TCHAR* Why) { Error = Why; return false; };
    if (Checksum != LD_B90_MAP_SHA || Hash(SourceJson) != Checksum || Seed != 9001 || LayoutRevision != 1 || FitterRevision != 3
        || POIs.Num() != 96 || Spawns.Num() != 24 || Buildings.Num() != 144 || Settlements.Num() != 11
        || Routes.Num() != 117 || Rivers.Num() != 3 || Bridges.Num() != 4 || Services.Num()!=36 || Aliases.Num()!=16 || CivicHeartOverride!=7000)
    { return Fail(TEXT("B90 source identity/count mismatch")); }
    for (const auto* Points : {&POIs, &Spawns, &Buildings, &Settlements, &Plateaus})
    {
        TSet<FName> Ids;
        for (const auto& P : *Points)
        { if (P.Id.IsNone() || Ids.Contains(P.Id) || !InBounds(P.Position)) { return Fail(TEXT("Invalid/duplicate map point")); } Ids.Add(P.Id); }
    }
    int32 Interiors = 0;
    for (const auto& B : Buildings) { Interiors += B.Enterable; if (B.Size.GetMin() <= 0) { return Fail(TEXT("Invalid building bounds")); } }
    if (Interiors != 36) { return Fail(TEXT("Wrong selected interior count")); }
    TSet<FName> ServiceIds,ServicePOIs,HostedBuildings,DoorIds;
    for(const auto& S:Services)
    {
        const auto* B=Buildings.FindByPredicate([&](const auto& V){return V.Id==S.Building;});
        if(ServiceIds.Contains(S.Id)||ServicePOIs.Contains(S.POI)||HostedBuildings.Contains(S.Building)||!Find(S.POI)||!B||!B->Enterable
           ||B->POI!=S.POI||B->Settlement!=S.Settlement||B->Position!=S.Position||S.Functions.IsEmpty()) { return Fail(TEXT("Invalid service/building binding")); }
        ServiceIds.Add(S.Id); ServicePOIs.Add(S.POI); HostedBuildings.Add(S.Building);
    }
    for(const auto& S:Settlements)
    {
        int32 Count=0,Open=0; for(const auto& B:Buildings) { if(B.Settlement==S.Id) { ++Count; Open+=B.Enterable; } }
        const bool City=S.Kind==TEXT("capital")||S.Kind==TEXT("city");
        if(S.Name.IsEmpty()||S.Identity.IsEmpty()||S.Security.IsEmpty()||S.CivicRadius!=(S.Kind==TEXT("capital")?12000:City?7000:3000)
           ||Count!=(City?24:4)||Open!=(City?6:1)) { return Fail(TEXT("Invalid settlement/district identity")); }
    }
    for(const auto& B:Buildings)
    { if(B.Enterable&&(B.Doors.Num()!=2||!HostedBuildings.Contains(B.Id))) { return Fail(TEXT("Unbound selected shell")); }
      for(FName Door:B.Doors) { if(DoorIds.Contains(Door)) { return Fail(TEXT("Duplicate door socket")); } DoorIds.Add(Door); } }
    if(DoorIds.Num()!=72) { return Fail(TEXT("Wrong door socket count")); }
    for(const auto& A:Aliases) { if(!Find(A.Value)||Find(A.Key)) { return Fail(TEXT("Invalid alias routing")); } }
    TSet<FName> RouteIds, Reached; Reached.Add(TEXT("capital"));
    for (const auto& R : Routes)
    {
        if (RouteIds.Contains(R.Id) || !Find(R.From) || !Find(R.To) || R.Points.Num() < 2 || R.Width < 400
            || !R.Points[0].Equals(Find(R.From)->Position, 1) || !R.Points.Last().Equals(Find(R.To)->Position, 1))
        { return Fail(TEXT("Invalid route identity/endpoints")); }
        RouteIds.Add(R.Id);
    }
    for (int32 I = 0; I < POIs.Num(); ++I)
    { for (const auto& R : Routes) { if (Reached.Contains(R.From)) { Reached.Add(R.To); } if (Reached.Contains(R.To)) { Reached.Add(R.From); } } }
    if (Reached.Num() != POIs.Num()) { return Fail(TEXT("Disconnected strategic graph")); }
    // The saved asset's reflected properties must agree with its closed source,
    // not merely carry a valid checksum string beside mutated coordinates/data.
    TSharedPtr<FJsonObject> Root;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SourceJson),Root)) { return Fail(TEXT("Unreadable closed source")); }
    auto Names=[](const TSharedPtr<FJsonObject>& O,const TCHAR* Field)
    { TArray<FName> Out; if(O->HasField(Field)) { for(const auto& V:O->GetArrayField(Field)) { Out.Add(FName(*V->AsString())); } } return Out; };
    auto PointsMatch=[&](const TCHAR* Key,const TArray<FLDMapPoint>& Rows)
    {
        const auto& Expected=Root->GetArrayField(Key); if(Expected.Num()!=Rows.Num()) { return false; }
        for(int32 I=0;I<Rows.Num();++I)
        {
            const auto O=Expected[I]->AsObject(); const auto& P=Rows[I];
            auto Text=[&](const TCHAR* F) { FString S; O->TryGetStringField(F,S); return S; };
            double Radius=0,Civic=0; bool Enterable=false,Roof=false;
            O->TryGetNumberField(TEXT("radius"),Radius); O->TryGetNumberField(TEXT("civic_radius"),Civic);
            O->TryGetBoolField(TEXT("enterable"),Enterable); O->TryGetBoolField(TEXT("roof"),Roof);
            const FVector Size=O->HasField(TEXT("size"))?Vector(O,TEXT("size")):FVector::ZeroVector;
            const int32 Floors=O->HasField(TEXT("floors"))?O->GetIntegerField(TEXT("floors")):1;
            if(P.Id!=FName(*Text(TEXT("id")))||P.Position!=Vector(O,TEXT("position"))||P.Size!=Size||P.Radius!=Radius||P.CivicRadius!=Civic
               ||P.Name!=Text(TEXT("name"))||P.Kind!=Text(TEXT("kind"))||P.Identity!=Text(TEXT("identity"))||P.Security!=Text(TEXT("security"))
               ||P.Settlement!=FName(*Text(TEXT("settlement")))||P.POI!=FName(*Text(TEXT("poi")))||P.Enterable!=Enterable||P.Roof!=Roof||P.Floors!=Floors
               ||P.Functions!=Names(O,TEXT("functions"))||P.Doors!=Names(O,TEXT("doors"))) { return false; }
        }
        return true;
    };
    if(!PointsMatch(TEXT("pois"),POIs)||!PointsMatch(TEXT("spawns"),Spawns)||!PointsMatch(TEXT("buildings"),Buildings)
       ||!PointsMatch(TEXT("settlements"),Settlements)||!PointsMatch(TEXT("plateaus"),Plateaus)) { return Fail(TEXT("Saved point metadata differs from closed source")); }
    auto LinesMatch=[&](const TCHAR* Key,const TArray<FLDMapLine>& Rows)
    {
        const auto& Expected=Root->GetArrayField(Key); if(Expected.Num()!=Rows.Num()) { return false; }
        for(int32 I=0;I<Rows.Num();++I)
        { const auto O=Expected[I]->AsObject(); const auto& R=Rows[I]; FString From,To;
          O->TryGetStringField(TEXT("start"),From); O->TryGetStringField(TEXT("end"),To); double Bank=0; O->TryGetNumberField(TEXT("bank"),Bank);
          if(R.Id!=FName(*O->GetStringField(TEXT("id")))||R.From!=FName(*From)||R.To!=FName(*To)||R.Width!=O->GetNumberField(TEXT("width"))||R.Bank!=Bank) { return false; }
          const auto& Points=O->GetArrayField(TEXT("points")); if(Points.Num()!=R.Points.Num()) { return false; }
          for(int32 J=0;J<Points.Num();++J) { const auto& P=Points[J]->AsArray(); if(R.Points[J]!=FVector(P[0]->AsNumber(),P[1]->AsNumber(),P[2]->AsNumber())) { return false; } }
        }
        return true;
    };
    if(!LinesMatch(TEXT("routes"),Routes)||!LinesMatch(TEXT("rivers"),Rivers)) { return Fail(TEXT("Saved route/river geometry differs from source")); }
    const auto& ExpectedBridges=Root->GetArrayField(TEXT("bridges"));
    for(int32 I=0;I<Bridges.Num();++I) { if(Bridges[I]!=FName(*ExpectedBridges[I]->AsObject()->GetStringField(TEXT("id")))) { return Fail(TEXT("Saved bridge binding differs from source")); } }
    const auto Inset=Root->GetObjectField(TEXT("wet_inset")); const auto& Size=Inset->GetArrayField(TEXT("size"));
    if(WetInset!=Vector(Inset,TEXT("position"))||WetSize!=FVector2D(Size[0]->AsNumber(),Size[1]->AsNumber())) { return Fail(TEXT("Saved Heart inset differs from source")); }
    const auto& ExpectedServices=Root->GetArrayField(TEXT("services"));
    for(int32 I=0;I<Services.Num();++I)
    { const auto O=ExpectedServices[I]->AsObject(); const auto& S=Services[I];
      if(S.Id!=FName(*O->GetStringField(TEXT("id")))||S.POI!=FName(*O->GetStringField(TEXT("poi")))||S.Building!=FName(*O->GetStringField(TEXT("building")))
         ||S.Settlement!=FName(*O->GetStringField(TEXT("settlement")))||S.Position!=Vector(O,TEXT("position"))||S.Functions!=Names(O,TEXT("functions"))) { return Fail(TEXT("Saved service mapping differs from source")); } }
    for(const auto& A:Root->GetObjectField(TEXT("aliases"))->Values)
    { const auto* V=Aliases.Find(FName(*A.Key)); if(!V||*V!=FName(*A.Value->AsString())) { return Fail(TEXT("Saved alias routing differs from source")); } }
    Error.Reset(); return true;
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
