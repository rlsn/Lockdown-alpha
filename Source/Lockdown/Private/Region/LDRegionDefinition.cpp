#include "Region/LDRegionDefinition.h"
#include "Objectives/LDObjectiveRules.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/SecureHash.h"
#include "EngineUtils.h"
#include "Engine/World.h"

namespace
{
bool Vector(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,FVector& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(!O->TryGetArrayField(Key,A)||A->Num()!=3){return false;}
    double V[3];for(int32 I=0;I<3;++I){if(!(*A)[I]->TryGetNumber(V[I])||!FMath::IsFinite(V[I])){return false;}}Out=FVector(V[0],V[1],V[2]);return true;
}
bool Points(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,TArray<FVector>& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(!O->TryGetArrayField(Key,A)||A->Num()>64){return false;}Out.Reset();
    for(const auto& P:*A){auto T=MakeShared<FJsonObject>();T->SetField(TEXT("p"),P);FVector V;if(!Vector(T,TEXT("p"),V)){return false;}Out.Add(V);}return true;
}
}
bool ULDRegionDefinition::Import(const FString& Json,FString& Error)
{
    TSharedPtr<FJsonObject> O;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),O)||!O){Error=TEXT("Region JSON invalid");return false;}
    double Schema=0,Cell=0,Trained=0,City=0;FVector Minimum;
    if(!O->TryGetNumberField(TEXT("cityId"),City)||City<1||City>255||City!=FMath::FloorToDouble(City)||!O->TryGetNumberField(TEXT("schema"),Schema)||Schema!=1||!O->TryGetStringField(TEXT("name"),Name)||!Vector(O,TEXT("min"),Minimum)||!O->TryGetNumberField(TEXT("cellSize"),Cell)
        ||!Vector(O,TEXT("guild"),Guild)||!Vector(O,TEXT("heart"),Heart)||!Vector(O,TEXT("tower"),Tower)||!Vector(O,TEXT("control"),Control)||!Vector(O,TEXT("sentinel"),Sentinel)
        ||!Points(O,TEXT("spawns"),Spawns)||!Points(O,TEXT("residents"),Residents)||!O->TryGetNumberField(TEXT("trainedResident"),Trained)||Trained!=FMath::FloorToDouble(Trained)||Trained<0||Trained>=Residents.Num())
    {Error=TEXT("Region fields invalid");return false;}CityRegion=uint8(City);Grid.Min=FVector2D(Minimum.X,Minimum.Y);Grid.CellSize=float(Cell);TrainedResident=uint8(Trained);
    const TArray<TSharedPtr<FJsonValue>> *N=nullptr,*R=nullptr,*G=nullptr;
    if(!O->TryGetArrayField(TEXT("nodes"),N)||N->Num()>16||!O->TryGetArrayField(TEXT("routes"),R)||R->Num()>32||!O->TryGetArrayField(TEXT("geometry"),G)||G->Num()>64){Error=TEXT("Region arrays invalid");return false;}
    Nodes.Reset();Routes.Reset();Geometry.Reset();
    for(const auto& V:*N){auto J=V->AsObject();FLDRegionNode E;double Id=0,Site=0,Radius=0;
        if(!J||!J->TryGetNumberField(TEXT("id"),Id)||Id<1||Id>255||Id!=FMath::FloorToDouble(Id)||!J->TryGetNumberField(TEXT("site"),Site)||Site<0||Site>6||Site!=FMath::FloorToDouble(Site)||!J->TryGetStringField(TEXT("name"),E.Name)||!Vector(J,TEXT("position"),E.Position)||!J->TryGetNumberField(TEXT("radius"),Radius)||!J->TryGetBoolField(TEXT("visit"),E.Visit)){Error=TEXT("Region node invalid");return false;}E.Id=uint8(Id);E.Site=uint8(Site);E.VisitRadius=float(Radius);Nodes.Add(E);}
    for(const auto& V:*R){auto J=V->AsObject();FLDRegionRoute E;double A=0,B=0;if(!J||!J->TryGetNumberField(TEXT("from"),A)||!J->TryGetNumberField(TEXT("to"),B)||A<1||A>255||B<1||B>255||A!=FMath::FloorToDouble(A)||B!=FMath::FloorToDouble(B)||!J->TryGetBoolField(TEXT("covered"),E.Covered)||!Points(J,TEXT("points"),E.Points)){Error=TEXT("Region route invalid");return false;}E.From=uint8(A);E.To=uint8(B);Routes.Add(E);}
    for(const auto& V:*G){auto J=V->AsObject();FLDRegionGeometry E;double Pitch=0;if(!J||!J->TryGetStringField(TEXT("name"),E.Name)||!J->TryGetStringField(TEXT("mesh"),E.Mesh)||!Vector(J,TEXT("position"),E.Position)||!Vector(J,TEXT("size"),E.Size)||!J->TryGetNumberField(TEXT("pitch"),Pitch)||!J->TryGetBoolField(TEXT("floor"),E.Floor)||!J->TryGetBoolField(TEXT("high"),E.High)){Error=TEXT("Region geometry invalid");return false;}E.Pitch=float(Pitch);Geometry.Add(E);}
    SourceJson=Json;Signature=TEXT("b45:")+FMD5::HashAnsiString(*Json);return Validate(Error);
}
const FLDRegionNode* ULDRegionDefinition::Node(uint8 Id) const{return Nodes.FindByPredicate([&](const FLDRegionNode& N){return N.Id==Id;});}
TArray<uint8> ULDRegionDefinition::Towns() const{TArray<uint8> Out;for(const auto& N:Nodes){if(N.Site==2||N.Site==6){Out.Add(N.Id);}}return Out;}
FVector ULDRegionDefinition::SitePosition(uint8 Site) const{if(Site==1){return Guild;}if(Site==3){return Heart;}if(Site==7){return Control;}for(const auto& N:Nodes){if(N.Site==Site||(Site==8&&N.Visit&&!N.Site)){return N.Position;}}return Guild;}
uint8 ULDRegionDefinition::NearestRegion(const FVector& P) const{const FLDRegionNode* Best=nullptr;double D=DBL_MAX;for(const auto& N:Nodes){const double Next=FVector::DistSquared2D(P,N.Position);if(Next<D){D=Next;Best=&N;}}return Best?Best->Id:0;}
bool ULDRegionDefinition::Validate(FString& Error) const
{
    auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
    if(!Grid.Valid()||Name.IsEmpty()||Nodes.Num()<4||Nodes.Num()>16||Towns().Num()!=2||Spawns.Num()<4||Residents.Num()!=8||TrainedResident>=Residents.Num()||Geometry.IsEmpty()||Signature!=TEXT("b45:")+FMD5::HashAnsiString(*SourceJson)){return Fail(TEXT("Region shape/signature invalid"));}
    for(const auto& P:{Guild,Heart,Tower,Control,Sentinel}){if(!LDVision::InWorld(P,Grid)){return Fail(TEXT("Region POI outside grid"));}}
    if(FVector::DistSquared2D(Guild,Heart)<840*840||FVector::DistSquared2D(Control,Tower)>200*200){return Fail(TEXT("Guild separation/control reach invalid"));}
    TSet<uint8> Ids,Sites;int32 Visits=0;const uint8 City=CityRegion;
    for(const auto& N:Nodes){if(!N.Id||Ids.Contains(N.Id)||N.Name.IsEmpty()||!LDVision::InWorld(N.Position,Grid)||!FMath::IsFinite(N.VisitRadius)||N.VisitRadius<100||N.VisitRadius>1500||(N.Site&&N.Site!=2&&N.Site!=6)||(N.Site&&Sites.Contains(N.Site))){return Fail(TEXT("Region identities/positions invalid"));}Ids.Add(N.Id);if(N.Site){Sites.Add(N.Site);}if(N.Visit){++Visits;}}
    if(Visits!=3||!Sites.Contains(2)||!Sites.Contains(6)||!Node(City)||Node(City)->Visit){return Fail(TEXT("Two towns and three visits required"));}
    for(const auto& P:Spawns){if(!LDVision::InWorld(P,Grid)||FVector::DistSquared2D(P,Heart)<600*600||NearestRegion(P)!=CityRegion){return Fail(TEXT("Initial/return pool must be separated from Heart"));}}
    for(const auto& P:Residents){if(!LDVision::InWorld(P,Grid)){return Fail(TEXT("Resident outside grid"));}}
    TSet<FString> Labels;for(const auto& E:Geometry){if(E.Name.IsEmpty()||Labels.Contains(E.Name)||(E.Mesh!=TEXT("Cube")&&E.Mesh!=TEXT("Cylinder"))||E.Position.ContainsNaN()||E.Size.ContainsNaN()||E.Size.GetMin()<=0||!FMath::IsFinite(E.Pitch)){return Fail(TEXT("Invalid authored geometry"));}Labels.Add(E.Name);}
    TSet<uint8> Reachable;Reachable.Add(City);
    for(const auto& R:Routes){if(!Node(R.From)||!Node(R.To)||R.From==R.To||R.Points.Num()<2||!R.Points[0].Equals(Node(R.From)->Position,1)||!R.Points.Last().Equals(Node(R.To)->Position,1)){return Fail(TEXT("Invalid route endpoints"));}for(const auto& P:R.Points){if(!LDVision::InWorld(P,Grid)){return Fail(TEXT("Route outside grid"));}}}
    for(int32 I=0;I<Nodes.Num();++I){for(const auto& R:Routes){if(Reachable.Contains(R.From)){Reachable.Add(R.To);}if(Reachable.Contains(R.To)){Reachable.Add(R.From);}}}
    if(Reachable.Num()!=Nodes.Num()){return Fail(TEXT("Region graph disconnected"));}
    for(uint8 Town:Towns()){bool Road=false,Cover=false;for(const auto& R:Routes){if((R.From==City&&R.To==Town)||(R.To==City&&R.From==Town)){Road|=!R.Covered;Cover|=R.Covered;}}if(!Road||!Cover){return Fail(TEXT("Each town needs public and covered city routes"));}}
    // The graph with City removed must still connect both outer settlements.
    Reachable.Reset();Reachable.Add(Towns()[0]);for(int32 I=0;I<Nodes.Num();++I){for(const auto& R:Routes){if(R.From==City||R.To==City){continue;}if(Reachable.Contains(R.From)){Reachable.Add(R.To);}if(Reachable.Contains(R.To)){Reachable.Add(R.From);}}}
    if(!Reachable.Contains(Towns()[1])){return Fail(TEXT("Outer-to-outer route cannot require City"));}return true;
}
ALDRegionAnchor::ALDRegionAnchor(){bReplicates=false;RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));}
const ULDRegionDefinition* LDRegion::Definition(const UWorld* W){if(!W){return nullptr;}for(TActorIterator<ALDRegionAnchor> It(W);It;++It){return It->Definition;}return nullptr;}
FVector LDRegion::Site(const UWorld* W,uint8 A){const auto* D=Definition(W);return D?D->SitePosition(A):LDObjective::Position(A);}
FString LDRegion::SiteName(const UWorld* W,uint8 A){if(A==1){return TEXT("Guild");}if(A==3){return TEXT("Heart");}if(A==7){return TEXT("Watch control");}if(const auto* D=Definition(W)){for(const auto& N:D->Nodes){if(N.Site==A||(A==8&&N.Visit&&!N.Site)){return N.Name;}}}return TEXT("West station");}
uint8 LDRegion::District(const UWorld* W,const FVector& P){const auto* D=Definition(W);return D?D->NearestRegion(P):LDObjective::District(P);}
FString LDRegion::DistrictName(const UWorld* W,uint8 Id){if(const auto* D=Definition(W)){if(const auto* N=D->Node(Id)){return N->Name;}return TEXT("Unknown region");}return LDObjective::DistrictName(Id);}
