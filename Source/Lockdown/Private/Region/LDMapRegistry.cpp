#include "Region/LDMapRegistry.h"
#include "Region/LDMapDefinition.h"
#include "Region/LDB90Preview.h"
#include "Framework/LDGameInstance.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "GameFramework/PlayerStart.h"

void ULDMapRegistry::OnWorldBeginPlay(UWorld& W)
{
    Super::OnWorldBeginPlay(W);
    if(W.GetNetMode()==NM_Standalone&&W.GetAuthGameMode<ALDB90PreviewMode>())
    { Definition=ULDMapDefinition::Get(&W); if(auto* GI=W.GetGameInstance<ULDGameInstance>()) { Generation=++GI->AuthorityGeneration; } }
}
bool ULDMapRegistry::Ensure(FString& Error)
{ return Bindings.Num()==96||Rebuild(Error); }
bool ULDMapRegistry::Rebuild(FString& Error)
{
    if(!Definition||!Definition->Validate(Error)) { return false; }
    int32 Anchors=0,Starts=0; TSet<FName> Pads;
    for(TActorIterator<ALDMapAnchor> It(GetWorld());It;++It) { ++Anchors; }
    for(TActorIterator<APlayerStart> It(GetWorld());It;++It) { ++Starts; Pads.Add(It->PlayerStartTag); }
    if(Anchors!=1||Starts!=24||Pads.Num()!=24) { Error=TEXT("Duplicate/missing map anchor or spawn identity"); return false; }
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if(!Nav) { Error=TEXT("Map navigation unavailable"); return false; }
    TArray<FLDMapBinding> Sites,Seats;
    auto Bind=[&](FName Id,FVector Point,bool Bridge,TArray<FLDMapBinding>& Out)
    {
        FNavLocation N; FHitResult Hit;
        if(!Nav->ProjectPointToNavigation(Point,N,FVector(60,60,Bridge?80:900))
           ||FVector::DistSquared2D(Point,N.Location)>60*60
           ||!GetWorld()->LineTraceSingleByChannel(Hit,N.Location+FVector(0,0,30),N.Location-FVector(0,0,100),ECC_GameTraceChannel1)
           ||Hit.ImpactNormal.Z<FMath::Cos(FMath::DegreesToRadians(40.)))
        { Error=TEXT("Unbound spatial address: ")+Id.ToString(); return false; }
        FLDMapBinding B; B.Id=Id; B.Position=Hit.ImpactPoint; B.Floor=Hit.GetComponent();
        const auto* A=Hit.GetActor(); B.Surface=A&&!A->Tags.IsEmpty()?A->Tags[0]:NAME_None;
        if(B.Surface.IsNone()) { Error=TEXT("Spatial address lacks a stable floor proxy: ")+Id.ToString(); return false; }
        Out.Add(B); return true;
    };
    for(const auto& P:Definition->POIs) { if(!Bind(P.Id,Definition->NavigationTarget(P.Id),Definition->Bridges.Contains(P.Id),Sites)) { return false; } }
    for(const auto& S:Definition->Services)
    {
        if(!Bind(S.Id,S.Position,false,Seats)) { return false; }
        const auto* Hub=Definition->Find(S.Settlement);
        const auto* P=Hub?UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Hub->Position+FVector(0,0,2.15),S.Position):nullptr;
        if(!P||!P->IsValid()||P->IsPartial()) { Error=TEXT("Unreachable service seat: ")+S.Id.ToString(); return false; }
        Seats.Last().HubPath=P->PathPoints;
    }
    Bindings=MoveTemp(Sites); ServiceBindings=MoveTemp(Seats); Error.Reset(); return true;
}
const FLDMapBinding* ULDMapRegistry::Resolve(FName Id,uint32 ExpectedGeneration) const
{
    if(!Definition||ExpectedGeneration!=Generation) { return nullptr; }
    const FName Canonical=Definition->CanonicalId(Id);
    return Bindings.FindByPredicate([&](const auto& B){return B.Id==Canonical&&B.Floor.IsValid();});
}
FString ULDMapRegistry::CanonicalBindings() const
{
    TArray<FString> Lines;
    for(const auto* Rows:{&Bindings,&ServiceBindings}) { for(const auto& B:*Rows)
    { Lines.Add(B.Id.ToString()+TEXT("|")+B.Position.ToString()+TEXT("|")+B.Surface.ToString()); } }
    Lines.Sort(); return FString::Join(Lines,TEXT("\n"));
}
TSharedRef<FJsonObject> ULDMapRegistry::Report() const
{
    auto R=MakeShared<FJsonObject>(); R->SetNumberField(TEXT("generation"),Generation);
    R->SetNumberField(TEXT("site_count"),Bindings.Num()); R->SetNumberField(TEXT("service_seats"),ServiceBindings.Num());
    R->SetStringField(TEXT("payload_sha256"),Definition?Definition->Checksum:TEXT(""));
    R->SetStringField(TEXT("scope"),TEXT("B90 spatial addresses and service mapping; no B92 transaction/reward/source handlers enabled"));
    TArray<TSharedPtr<FJsonValue>> Points,Services;
    for(const auto& B:Bindings)
    { auto J=MakeShared<FJsonObject>(); J->SetStringField(TEXT("id"),B.Id.ToString()); J->SetStringField(TEXT("native_cm"),B.Position.ToString());
      J->SetStringField(TEXT("floor_proxy"),B.Surface.ToString()); J->SetBoolField(TEXT("floor_valid"),B.Floor.IsValid()); Points.Add(MakeShared<FJsonValueObject>(J)); }
    if(Definition) { for(const auto& S:Definition->Services)
    {
        auto J=MakeShared<FJsonObject>(); J->SetStringField(TEXT("id"),S.Id.ToString()); J->SetStringField(TEXT("poi"),S.POI.ToString());
        J->SetStringField(TEXT("building"),S.Building.ToString()); J->SetStringField(TEXT("settlement"),S.Settlement.ToString());
        J->SetStringField(TEXT("source_building_cm"),S.Position.ToString()); J->SetStringField(TEXT("execution_stage"),TEXT("B92"));
        J->SetBoolField(TEXT("transactions_enabled"),false); TArray<TSharedPtr<FJsonValue>> Functions;
        for(FName F:S.Functions) { Functions.Add(MakeShared<FJsonValueString>(F.ToString())); } J->SetArrayField(TEXT("declared_functions"),Functions);
        if(const auto* Seat=ServiceBindings.FindByPredicate([&](const auto& B){return B.Id==S.Id;}))
        { J->SetStringField(TEXT("native_cm"),Seat->Position.ToString()); J->SetBoolField(TEXT("floor_valid"),Seat->Floor.IsValid());
          TArray<TSharedPtr<FJsonValue>> Path; for(FVector P:Seat->HubPath) { Path.Add(MakeShared<FJsonValueString>(P.ToString())); } J->SetArrayField(TEXT("hub_path_cm"),Path); }
        Services.Add(MakeShared<FJsonValueObject>(J));
    } }
    R->SetArrayField(TEXT("sites"),Points); R->SetArrayField(TEXT("services"),Services); return R;
}
TArray<FBox> LDMapFitting::StructuralWalls(UWorld* W)
{
    TArray<FBox> Boxes;
    for(TActorIterator<AStaticMeshActor> It(W);It;++It)
    { if(It->Tags.ContainsByPredicate([](FName T){return T.ToString().StartsWith(TEXT("building_"));})) { Boxes.Add(It->GetComponentsBoundingBox(true)); } }
    return Boxes;
}
bool LDMapFitting::RoadOverlaps(const FTransform& T,const FBox& Wall,double Padding)
{
    const FBox B=Wall.ExpandBy(Padding); const FVector C=T.GetLocation();
    const FVector X=T.TransformVector(FVector(50,0,0)),Y=T.TransformVector(FVector(0,50,0)),Z=T.TransformVector(FVector(0,0,50));
    const double Height=FMath::Abs(X.Z)+FMath::Abs(Y.Z)+FMath::Abs(Z.Z);
    if(C.Z+Height<=B.Min.Z||C.Z-Height>=B.Max.Z) { return false; }
    const FVector2D Centre(C),Delta=Centre-FVector2D(B.GetCenter()),Extent(B.GetExtent());
    for(FVector2D Axis:{FVector2D(1,0),FVector2D(0,1),FVector2D(X).GetSafeNormal(),FVector2D(Y).GetSafeNormal()})
    {
        if(Axis.IsNearlyZero()) { continue; }
        const double Radius=FMath::Abs(FVector2D::DotProduct(FVector2D(X),Axis))+FMath::Abs(FVector2D::DotProduct(FVector2D(Y),Axis))+FMath::Abs(FVector2D::DotProduct(FVector2D(Z),Axis));
        const double WallRadius=Extent.X*FMath::Abs(Axis.X)+Extent.Y*FMath::Abs(Axis.Y);
        if(FMath::Abs(FVector2D::DotProduct(Delta,Axis))>=Radius+WallRadius-.001) { return false; }
    }
    return true;
}
