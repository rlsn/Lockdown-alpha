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
    return Validate(Error);
}

bool ULDMapDefinition::Validate(FString& Error) const
{
    auto Fail = [&](const TCHAR* Why) { Error = Why; return false; };
    if (Checksum != LD_B90_MAP_SHA || Hash(SourceJson) != Checksum || Seed != 9001 || LayoutRevision != 1 || FitterRevision != 2
        || POIs.Num() != 96 || Spawns.Num() != 24 || Buildings.Num() != 144 || Settlements.Num() != 11
        || Routes.Num() != 117 || Rivers.Num() != 3 || Bridges.Num() != 4)
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
