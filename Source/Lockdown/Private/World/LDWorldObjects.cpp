#include "Framework/LDCommandLiveness.h"
#include "World/LDWorldObjects.h"
#include "World/LDEnvironmentDirector.h"
#include "World/LDB91ObjectsContract.h"
#include "World/LDB91ToolsContract.h"
#include "World/LDB91TraversalContract.h"
#include "World/LDB91ForestryContract.h"
#include "Region/LDMapDefinition.h"
#include "Combat/LDCombatDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDTradeDirector.h"
#include "Character/LDCharacter.h"
#include "Navigation/LDNavigationComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END
namespace
{
uint64 ObjectReceiptKey(uint32 Life,uint32 Seq){return (uint64(Life)<<32)|Seq;}
bool ItemVerb(ELDObjectVerb V){return V==ELDObjectVerb::Take||V==ELDObjectVerb::Store||V==ELDObjectVerb::Drop;}
double Duration(ELDObjectVerb V)
{switch(V){case ELDObjectVerb::Ignite:return 1.;case ELDObjectVerb::Harvest:case ELDObjectVerb::Chop:return 4.;case ELDObjectVerb::Open:return .3;case ELDObjectVerb::Carry:case ELDObjectVerb::Push:return 1.;case ELDObjectVerb::Place:case ELDObjectVerb::Attach:case ELDObjectVerb::Deploy:case ELDObjectVerb::Retrieve:return 1.5;case ELDObjectVerb::Use:return 2.;case ELDObjectVerb::Break:return 3.;default:return .5;}}
}
ALDWorldObject::ALDWorldObject()
{
    bReplicates=false;Proxy=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ObjectProxy"));SetRootComponent(Proxy);
    Proxy->SetMobility(EComponentMobility::Movable);Proxy->SetCollisionProfileName(TEXT("BlockAll"));
    Proxy->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Ignore);
    Glow=CreateDefaultSubobject<UPointLightComponent>(TEXT("ObservedCosmeticGlow"));Glow->SetupAttachment(Proxy);Glow->SetMobility(EComponentMobility::Movable);Glow->SetCastShadows(false);Glow->SetIntensity(1500);Glow->SetLightColor(FLinearColor(1.f,.65f,.25f));Glow->SetVisibility(false);
}
FVector ALDWorldObject::Size() const
{switch(Kind){case ELDObjectKind::Chest:return FVector(100,80,80);case ELDObjectKind::Crate:return FVector(120,120,220);case ELDObjectKind::Barrel:return FVector(120,120,140);case ELDObjectKind::Torch:return FVector(20,20,90);case ELDObjectKind::Lantern:return FVector(40,40,60);case ELDObjectKind::Rope:return FVector(40,40,30);case ELDObjectKind::Ladder:return AppliedState!=255&&(AppliedState&16)?FVector(70,40,400):FVector(70,500,30);case ELDObjectKind::Anchor:return FVector(30,30,30);case ELDObjectKind::Furniture:return FVector(200,100,100);case ELDObjectKind::Sapling:return FVector(120,120,400);default:return FVector(20,20,30);}}
bool ALDWorldObject::Available() const
{return !Broken&&(Container()||Kind==ELDObjectKind::Ladder||Kind==ELDObjectKind::Anchor||Kind==ELDObjectKind::Sapling||Physical.IsValid()&&Physical->State==0&&!Physical->Holder.IsValid());}
int32 ALDWorldObject::Weight() const
{int32 W=Kind==ELDObjectKind::Crate?12:Kind==ELDObjectKind::Barrel?16:Kind==ELDObjectKind::Ladder?10:0;for(auto P:Contents){if(P.IsValid()){W+=LDInventory::UnitWeight(P->Kind);}}return W;}
void ALDWorldObject::Apply(FVector Position,uint8 State,uint32 NewRevision)
{
    if(NewRevision<Revision||(NewRevision==Revision&&State==AppliedState&&Proxy->GetStaticMesh()&&GetActorLocation().Equals(Position,.01))){return;}AppliedState=State;
    Broken=State&1;bCarriedWorld=State&2;SetActorLocation(Position);Proxy->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Proxy->SetRelativeScale3D(Size()/100.);Proxy->SetVisibility(!(State&5));
    Glow->SetAttenuationRadius(Kind==ELDObjectKind::Torch?600:Kind==ELDObjectKind::Lantern?800:0);Glow->SetVisibility(false);
    Proxy->SetCollisionEnabled(BlocksWalk()&&!(State&7)?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
    Proxy->SetCollisionResponseToChannel(ECC_GameTraceChannel2,(Kind==ELDObjectKind::Crate||Kind==ELDObjectKind::Sapling)?ECR_Block:ECR_Ignore);
    Proxy->SetCanEverAffectNavigation(BlocksWalk()&&!(State&7));
    if(Kind==ELDObjectKind::Sapling){auto* M=Cast<UMaterialInstanceDynamic>(Proxy->GetMaterial(0));if(!M){M=Proxy->CreateAndSetMaterialInstanceDynamic(0);}if(M){M->SetVectorParameterValue(TEXT("Color"),State&32?FLinearColor(1,.25f,0):FLinearColor(.2f,.5f,.15f));}}
    Revision=CollisionRevision=SightRevision=NavigationRevision=NewRevision;++ProxyUpdates;FNavigationSystem::UpdateComponentData(*Proxy);
}
void ALDWorldObject::SetObserved(bool Visible)
{Proxy->SetVisibility(Visible&&!(AppliedState&5));Glow->SetVisibility(Visible&&!(AppliedState&5)&&(AppliedState&8)&&(Kind==ELDObjectKind::Torch||Kind==ELDObjectKind::Lantern));}
ALDWorldObjects::ALDWorldObjects()
{bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
FString ALDWorldObjects::ActiveContract(){return LD_B91_CONTROL_WORLD_SHA;}
ALDWorldObjects* ALDWorldObjects::Get(UWorld* W)
{if(W){for(TActorIterator<ALDWorldObjects> I(W);I;++I){return *I;}}return nullptr;}
ALDWorldObject* ALDWorldObjects::Find(FName Id) const
{for(auto P:Objects){if(P.IsValid()&&P->SourceId==Id){return P.Get();}}return nullptr;}
ALDWorldObject* ALDWorldObjects::Carried(const ALDB91Controller* PC) const
{if(!PC){return nullptr;}for(const auto& P:Cargo){if(P.Key.Get()==PC){return P.Value.Object.Get();}}return nullptr;}
bool ALDWorldObjects::SurfaceAt(FVector Point,FVector& Surface) const
{
    if(Point.ContainsNaN()||!ULDMapDefinition::InBounds(Point)){return false;}FHitResult H;
    if(!GetWorld()->LineTraceSingleByChannel(H,Point+FVector(0,0,150),Point-FVector(0,0,200),ECC_GameTraceChannel1)
       ||H.ImpactNormal.Z<1/FMath::Sqrt(1+.25*.25)){return false;}Surface=H.ImpactPoint;return true;
}
bool ALDWorldObjects::Reserved(FVector P,FVector Size) const
{
    const auto* D=ULDMapDefinition::Get(GetWorld());if(!D){return true;}
    const double R=FMath::Max(Size.X,Size.Y)*.5;
    for(const auto& L:D->Routes){for(int32 I=1;I<L.Points.Num();++I)
    {auto A=L.Points[I-1],B=L.Points[I];A.Z=B.Z=P.Z;if(FMath::PointDistToSegment(P,A,B)<FMath::Max(L.Width*.5,400.)+R){return true;}}}
    for(const auto& S:D->Spawns){if(FVector::Dist2D(P,S.Position)<400+R){return true;}}
    for(const auto& S:D->Services){if(FVector::Dist2D(P,S.Position)<400+R){return true;}}
    for(const auto& S:D->POIs){if((S.Kind.Contains(TEXT("heart"))||S.Id.ToString().Contains(TEXT("heart")))&&FVector::Dist2D(P,S.Position)<600+R){return true;}}
    if(auto* E=ALDEnvironmentDirector::Get(GetWorld()))
    {
        for(auto Door:E->Doors){if(Door.IsValid()&&FVector::Dist2D(P,Door->GetActorLocation())<300+R){return true;}}
        for(const auto& S:E->StairStarts){if(FVector::Dist2D(P,S)<300+R){return true;}}
        for(const auto& S:E->RoofGoals){if(FVector::Dist2D(P,S)<300+R){return true;}}
    }
    // Do not turn interior corridors or stair landings into placement pads.
    for(const auto& B:D->Buildings)
    {if(FMath::Abs(P.X-B.Position.X)<700+R&&FMath::Abs(P.Y-B.Position.Y)<900+R){return true;}
     if(B.Roof&&FVector::Dist2D(P,B.Position+FVector(950,740,0))<200+R){return true;}}
    return D->RiverAt(P);
}
bool ALDWorldObjects::Placement(ALDWorldObject* Object,FVector Point,FVector& Surface) const
{
    if(!Object||!SurfaceAt(Point,Surface)||FMath::Abs(Surface.Z-Point.Z)>80||Reserved(Surface,Object->Size())){return false;}
    const FVector Extent=Object->Size()*.5;
    const double CentreZ=Surface.Z;double PadZ=CentreZ;
    for(int X:{-1,1}){for(int Y:{-1,1}){FVector Corner=Surface;
    const double Grade=Object->Kind==ELDObjectKind::Ladder?FMath::Max(20.,Extent.Size2D()*.25):20.;
    if(!SurfaceAt(Surface+FVector(X*Extent.X,Y*Extent.Y,0),Corner)||FMath::Abs(Corner.Z-CentreZ)>Grade){return false;}PadZ=FMath::Max(PadZ,Corner.Z);}}
    // Upright props rest on the highest sampled corner. A horizontal box at the
    // centre height otherwise penetrates even a legal, gentle terrain slope.
    Surface.Z=PadZ;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(B91Placement),false,Object);
    // Character capsules intentionally ignore Pawn-channel blocking. Querying
    // that channel alone cannot establish occupancy, so include every capsule
    // explicitly without displacing it or disclosing which body caused refusal.
    const FVector Centre=Surface+FVector(0,0,Extent.Z+3);
    for(TActorIterator<ALDCharacter> B(GetWorld());B;++B)
    {const FVector P=B->GetActorLocation();const double R=B->GetCapsuleComponent()->GetScaledCapsuleRadius();
     const double X=FMath::Max(0.,FMath::Abs(P.X-Centre.X)-Extent.X),Y=FMath::Max(0.,FMath::Abs(P.Y-Centre.Y)-Extent.Y);
     if(X*X+Y*Y<=R*R&&FMath::Abs(P.Z-Centre.Z)<Extent.Z+B->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()){return false;}}
    // Full solid proxy, every body included. This is never a character push.
    if(GetWorld()->OverlapAnyTestByChannel(Surface+FVector(0,0,Extent.Z+3),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeBox(Extent),Q)){return false;}
    for(auto P:Objects){if(P.IsValid()&&P!=Object&&P->Available()&&!P->Carrier.IsValid())
    {const double Clearance=FMath::Max(P->Size().X,P->Size().Y)*.5+FMath::Max(Extent.X,Extent.Y)+(Object->Heavy()||P->Heavy()||Object->BlocksWalk()||P->BlocksWalk()?300:30);
     if(FVector::Dist2D(P->GetActorLocation(),Surface)<Clearance){return false;}}}
    return true;
}
bool ALDWorldObjects::Initialize(FString& Error)
{
    if(Ready){return true;}if(GetNetMode()==NM_Client){Error=TEXT("Object authority cannot initialize on a client");return false;}
    FString Json;if(!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/V09/objects_b91.json")))){Error=TEXT("Missing B91 objects contract");return false;}
    Json=Json.Replace(TEXT("\r\n"),TEXT("\n"));FTCHARToUTF8 Bytes(*Json);FSHA256Signature H;SHA256(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length(),H.Signature);
    if(H.ToString().ToLower()!=LD_B91_OBJECTS_SHA){Error=TEXT("Stale B91 objects contract");return false;}Contract=LD_B91_OBJECTS_SHA;
    TSharedPtr<FJsonObject> Root;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)){return false;}
    const auto* D=ULDMapDefinition::Get(GetWorld());if(!D){return false;}
    for(const auto& V:Root->GetArrayField(TEXT("rows")))
    {
        const auto R=V->AsObject();const auto& A=R->GetArrayField(TEXT("position"));const FVector Source(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber());
        auto* O=GetWorld()->SpawnActor<ALDWorldObject>();O->SourceId=FName(*R->GetStringField(TEXT("id")));O->Kind=ELDObjectKind(int32(R->GetNumberField(TEXT("kind"))));O->SourcePosition=Source;O->HarvestSource=FName(*R->GetStringField(TEXT("harvest_source")));
        FVector Surface=Source;const bool Fitted=FitSource(O,Source,Surface);
        if(!Fitted){Error=TEXT("No legal fitted pad within 10m: ")+O->SourceId.ToString();O->Destroy();return false;}
        O->LastClear=Surface;O->Apply(Surface+FVector(0,0,O->Size().Z*.5+3),0,1);Objects.Add(O);
        for(const auto& C:R->GetArrayField(TEXT("contents")))
        {const auto Item=C->AsObject();for(int N=0;N<int(Item->GetNumberField(TEXT("count")));++N)
         {auto* P=GetWorld()->SpawnActor<ALDItem>();P->Kind=Item->GetStringField(TEXT("definition_id"))==TEXT("Fiber")?ELDItemKind::Fiber:ELDItemKind::Ore;P->State=1;P->Holder=O;P->Institution=O->SourceId;P->OriginReceipt=FGuid::NewGuid();O->Contents.Add(P);Finite.Add(P->Identity,P);}}
        if(O->Kind==ELDObjectKind::Water||O->Kind==ELDObjectKind::Oil)
        {auto* P=GetWorld()->SpawnActor<ALDItem>();P->Kind=O->Kind==ELDObjectKind::Water?ELDItemKind::WaterFlask:ELDItemKind::OilFlask;P->Institution=O->SourceId;P->OriginReceipt=FGuid::NewGuid();P->SetActorLocation(O->GetActorLocation());O->Physical=P;Finite.Add(P->Identity,P);}
    }
    if(Objects.Num()!=252||Finite.Num()!=84){Error=TEXT("B91 base object census mismatch");return false;}
    if(!InitializeTools(Error)||!InitializeTraversal(Error)||!InitializeForestry(Error)){return false;}Contract=ActiveContract();
    Ready=Objects.Num()==339&&Finite.Num()==143;if(!Ready){Error=TEXT("B91 object/tool/link census mismatch");}return Ready;
}
bool ALDWorldObjects::FitSource(ALDWorldObject* Object,FVector Source,FVector& Surface) const
{
    const auto* D=ULDMapDefinition::Get(GetWorld());if(!D){return false;}
    for(int Ring=0;Ring<=10;++Ring){for(int Y=-Ring;Y<=Ring;++Y){for(int X=-Ring;X<=Ring;++X)
    {if(FMath::Max(FMath::Abs(X),FMath::Abs(Y))!=Ring||X*X+Y*Y>100){continue;}FVector P=Source+FVector(X*100,Y*100,0);P.Z=D->Height(FVector2D(P));if(Placement(Object,P,Surface)){return true;}}}}return false;
}
bool ALDWorldObjects::Visible(ALDB91Controller* PC,ALDWorldObject* O) const
{
    auto* E=ALDEnvironmentDirector::Get(GetWorld());const auto* B=PC?PC->GetPawn().Get():nullptr;
    if(!E||!O||!B||!PC->OwnerState.bActive){return false;}if(O->Carrier==PC){return true;}
    const double Radius=E->SightFor(PC);
    // Eye height at the object's floor; ignore its own cover at the endpoint.
    const FVector Target=O->Kind==ELDObjectKind::Anchor?O->EndFor(B->GetActorLocation())+FVector(0,0,90):O->GetActorLocation()-FVector(0,0,O->Size().Z*.5-90);
    return E->Ray(B->GetActorLocation(),Target,O,Radius);
}
ALDWorldObject* ALDWorldObjects::Resolve(ALDB91Controller* PC,FGuid Id) const
{const auto* C=Clients.Find(PC);if(!C||!C->Visible.Contains(Id)){return nullptr;}for(const auto& P:C->Handles){if(P.Value==Id&&Visible(PC,P.Key.Get())){return P.Key.Get();}}return nullptr;}
bool ALDWorldObjects::Eligible(ALDB91Controller* PC,ALDWorldObject* O,const FLDObjectRequest& R,FString& Why) const
{
    auto Fail=[&](const TCHAR* S){Why=S;return false;};auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* Own=M?M->StateFor(PC):nullptr;
    const auto* B=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;const auto* S=B&&M&&M->Combat?M->Combat->StateFor(B):nullptr;const double Now=GetWorld()->GetTimeSeconds();
    if(!Ready||!Own||!B||!S||!Own->bActive||!M->IsPlaying()||S->Hp<=0||S->Action||Own->DevelopmentKind||Now<Own->ProtectedUntil||Now<S->ProtectionUntil
       ||!LDTactical::Allows(S->Tactical,LDTactical::EAction::Service,LDTactical::Milliseconds(Now))){return Fail(TEXT("World interaction unavailable"));}
    if(R.Verb==ELDObjectVerb::Ignite){return IgnitionEligible(PC,O,R,Why);}
    if((uint8(R.Verb)>=uint8(ELDObjectVerb::Attach)&&uint8(R.Verb)<=uint8(ELDObjectVerb::Retrieve))){return LinkEligible(PC,O,R,Why);}
    if(Traversing(PC)){return Fail(TEXT("Finish traversal before another interaction"));}
    const bool Drop=R.Verb==ELDObjectVerb::Drop;
    if(!Drop&&(!O||!O->Available()||O->Revision!=R.ObjectRevision||O->bCarriedWorld&&O->Carrier!=PC)){return Fail(TEXT("Object changed or unavailable"));}
    if(!Drop&&O->Carrier!=PC&&(FVector::Dist2D(B->GetActorLocation(),O->GetActorLocation())>(R.Verb==ELDObjectVerb::Push&&Holds.Contains(PC)?270:120)||FMath::Abs(B->GetActorLocation().Z-(O->GetActorLocation().Z-O->Size().Z*.5+90))>100||!Visible(PC,O))){return Fail(TEXT("Visible object must be within 120cm on this floor"));}
    if(R.Verb!=ELDObjectVerb::Open&&R.Verb!=ELDObjectVerb::Drop&&Now-FMath::Max3(S->LastDamage,S->LastDealt,S->QuietSince)<6){return Fail(TEXT("Six combat-quiet seconds required"));}
    if(Carried(PC)&&R.Verb!=ELDObjectVerb::Place){return Fail(TEXT("Put down cargo before another interaction"));}
    if(R.Verb==ELDObjectVerb::Open&&(!O->Container()||O->Carrier.IsValid())){return Fail(TEXT("No accessible container"));}
    if(O&&O->InstalledAt.IsValid()){return Fail(TEXT("Use the approved anchor to retrieve an installed ladder"));}
    if(R.Verb==ELDObjectVerb::Push&&O&&!(O->Heavy()&&O->BlocksWalk())){return Fail(TEXT("Only crates and barrels can be pushed"));}
    if(R.Verb==ELDObjectVerb::Carry||R.Verb==ELDObjectVerb::Push)
    {const int Natural=Own->Build.Attributes.Num()==6?60+2*(Own->Build.Attributes[0]-10):0;if(!O->Heavy()||O->Weight()>Natural||O->Carrier.IsValid()){return Fail(TEXT("Cargo exceeds natural carrying capacity or is occupied"));}}
    if(R.Verb==ELDObjectVerb::Place&&(!O||O->Carrier!=PC||Carried(PC)!=O)){return Fail(TEXT("No carried cargo"));}
    if(R.Verb==ELDObjectVerb::Break){return Fail(TEXT("Only an ordinary attack or authored ability can break a placed prop"));}
    if(R.Verb==ELDObjectVerb::Break&&(!(O->Heavy()&&O->BlocksWalk())||O->Carrier.IsValid())){return Fail(TEXT("Only a placed crate or barrel can break"));}
    if(R.Verb==ELDObjectVerb::Pickup&&(!O->Physical.IsValid()||O->Physical->State!=0||O->Physical->Holder.IsValid()||FVector::Dist(O->Physical->GetActorLocation(),O->GetActorLocation())>10)){return Fail(TEXT("Physical item unavailable"));}
    if(R.Verb==ELDObjectVerb::Take||R.Verb==ELDObjectVerb::Store)
    {const auto* C=Clients.Find(PC);const auto* Quote=C?C->Opened.Find(O):nullptr;if(!O->Container()||!Quote||*Quote!=O->Revision){return Fail(TEXT("Open the current container revision first"));}}
    if(ItemVerb(R.Verb)||R.Verb==ELDObjectVerb::Pickup)
    {
        if(Own->Inventory.Revision!=R.InventoryRevision){return Fail(TEXT("Inventory changed; review again"));}
        ALDItem* P=R.Verb==ELDObjectVerb::Pickup?O->Physical.Get():M->Inventory->Owned(R.Verb==ELDObjectVerb::Take?static_cast<AActor*>(O):static_cast<AActor*>(PC),R.Item);
        if(!P||P->Revision!=R.ItemRevision||P->bReserved||P->BindingMask){return Fail(TEXT("Exact unbound, unreserved item revision required"));}
        if(R.Verb==ELDObjectVerb::Take||R.Verb==ELDObjectVerb::Pickup){if(!M->Inventory->HasSpace(PC,P->Kind,&Why)){return false;}}
        if(R.Verb==ELDObjectVerb::Store)
        {int Weight=0;for(auto I:O->Contents){if(I.IsValid()){Weight+=LDInventory::UnitWeight(I->Kind);}}if(O->Contents.Num()>=24||Weight+LDInventory::UnitWeight(P->Kind)>60){return Fail(TEXT("Container is full or too heavy"));}}
    }
    if(R.Verb==ELDObjectVerb::Harvest||R.Verb==ELDObjectVerb::Chop)
    {if(!ForestryEligible(PC,R,Why)){return false;}if(O->Kind!=ELDObjectKind::Sapling||O->Hp<=0||O->HarvestReceipt.IsValid()||O->BurnReceipt.IsValid()){return Fail(TEXT("A living finite sapling is required"));}
     if(R.Verb==ELDObjectVerb::Harvest&&(Own->Inventory.Revision!=R.InventoryRevision||!M->Inventory->HasSpace(PC,ELDItemKind::Fiber,&Why))){if(Why.IsEmpty()){Why=TEXT("Inventory changed");}return false;}}
    if(uint8(R.Verb)<1||uint8(R.Verb)>16){return Fail(TEXT("Unknown object verb"));}return true;
}
void ALDWorldObjects::Reply(ALDB91Controller* PC,const FLDObjectRequest& R,uint8 Result,const FString& Why,uint32 Revision)
{
    if(!PC){return;}auto& C=Clients.FindOrAdd(PC);auto& P=C.Receipts.FindOrAdd(ObjectReceiptKey(R.Life,R.Sequence));if(!P.Id.IsValid()){P.Id=FGuid::NewGuid();}P.Result=Result;P.Why=Why;P.Revision=Revision;
    FLDWorldReceipt Receipt;Receipt.Sequence=R.Sequence;Receipt.Receipt=P.Id;Receipt.Result=Result;Receipt.Reason=Why;Receipt.Revision=Revision;PC->ClientObjectReceipt(Receipt);
    FLDB91Channel Progress;Progress.Match=R.Match;Progress.Life=R.Life;Progress.Sequence=R.Sequence;Progress.Domain=2;const auto* H=Holds.Find(PC);if(!Result&&H){Progress.Started=H->Started;Progress.Ends=H->Ends;Progress.Label=R.Verb==ELDObjectVerb::Ignite?TEXT("Torch ignition"):R.Verb==ELDObjectVerb::Harvest?TEXT("Harvesting with Forestry Knife"):R.Verb==ELDObjectVerb::Chop?TEXT("Chopping with Forestry Knife"):R.Verb==ELDObjectVerb::Use?TEXT("Traversing installed route"):TEXT("Item / storage action");}PC->ClientB91Channel(Progress,!Result&&H!=nullptr);
    while(C.Receipts.Num()>64){uint64 Old=MAX_uint64;for(const auto& X:C.Receipts){Old=FMath::Min(Old,X.Key);}C.Receipts.Remove(Old);}
}
void ALDWorldObjects::Begin(ALDB91Controller* PC,const FLDObjectRequest& R)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M||!M->IsEnvironmentProfile()||!PC){return;}M->Combat->FlushPastCommand();const auto* O=M->StateFor(PC);auto& C=Clients.FindOrAdd(PC);
    if(const auto* Old=C.Receipts.Find(ObjectReceiptKey(R.Life,R.Sequence));Old&&O&&R.Match==O->Match)
    {FLDWorldReceipt Receipt;Receipt.Sequence=R.Sequence;Receipt.Receipt=Old->Id;Receipt.Result=Old->Result;Receipt.Reason=Old->Why;Receipt.Revision=Old->Revision;PC->ClientObjectReceipt(Receipt);return;}
    if(!O||O->Match!=R.Match||O->Life!=R.Life||R.Layout!=1||R.Contract!=Contract||R.Point.ContainsNaN()||!R.Sequence||R.Sequence<=C.Last||R.Sequence<=C.Cancelled)
    {Reply(PC,R,2,TEXT("Stale object request/profile/layout/life"));return;}
    const double Now=GetWorld()->GetTimeSeconds();C.Tokens=FMath::Min(8.,C.Tokens+FMath::Max(0.,Now-C.At)*8);C.At=Now;C.Last=R.Sequence;
    if(C.Tokens<1){Reply(PC,R,2,TEXT("Object request busy"));return;}--C.Tokens;
    if(R.Verb==ELDObjectVerb::Toggle){FString Why;const bool Done=ToggleTool(PC,R,Why);if(Done){++Committed;}Reply(PC,R,Done?1:2,Why);return;}
    auto* Object=R.Verb==ELDObjectVerb::Drop?nullptr:Resolve(PC,R.Observed);FString Why;
    if(R.Verb==ELDObjectVerb::Ignite&&!ForceDrop(PC)){Reply(PC,R,2,TEXT("No safe cargo drop; ignition unavailable"));return;}
    if(!Eligible(PC,Object,R,Why)){Reply(PC,R,2,Why);return;}
    const auto* RequestedAnchor=R.Anchor.IsValid()?Resolve(PC,R.Anchor):nullptr;
    for(const auto& P:Holds){if(P.Key!=PC&&(Object&&(P.Value.Object==Object||P.Value.Link==Object)||RequestedAnchor&&(P.Value.Object==RequestedAnchor||P.Value.Link==RequestedAnchor)||R.Item.IsValid()&&P.Value.Request.Item==R.Item)&&!(R.Verb==ELDObjectVerb::Open&&P.Value.Request.Verb==ELDObjectVerb::Open)){Reply(PC,R,2,TEXT("Object reserved; try again"));return;}}
    Cancel(PC,TEXT("Superseded held interaction"));if(auto* E=ALDEnvironmentDirector::Get(GetWorld())){E->Cancel(PC,TEXT("Object interaction started"));}
    FHold H;H.Request=R;H.Object=Object;H.Body=Cast<ALDCharacter>(PC->GetPawn());H.Start=PC->GetPawn()->GetActorLocation();H.Started=H.Heartbeat=Now;H.Ends=Now+Duration(R.Verb);H.Damage=M->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()))->LastDamage;
    if((uint8(R.Verb)>=uint8(ELDObjectVerb::Attach)&&uint8(R.Verb)<=uint8(ELDObjectVerb::Retrieve))&&!BeginLink(PC,Object,H,Why)){Reply(PC,R,2,Why);return;}
    if(R.Verb==ELDObjectVerb::Place||R.Verb==ELDObjectVerb::Drop)
    {
        FVector Surface;const double Range=R.Verb==ELDObjectVerb::Place?250:80;
        if(FVector::Dist2D(H.Start,R.Point)>Range||!SurfaceAt(R.Point,Surface)||FMath::Abs(Surface.Z-(H.Start.Z-90))>50
           ||!ALDEnvironmentDirector::Get(GetWorld())->Ray(H.Start,Surface+FVector(0,0,90))||(Object&&!Placement(Object,Surface,H.Destination)))
        {Reply(PC,R,2,TEXT("No legal placement in reach"));return;}if(!Object){H.Destination=Surface;}
    }
    if(R.Verb==ELDObjectVerb::Push)
    {
        const FVector Direction=(Object->GetActorLocation()-H.Start).GetSafeNormal2D();H.PushStart=Object->GetActorLocation()-FVector(0,0,Object->Size().Z*.5+3);H.Destination=H.PushStart;
        for(int N=1;N<=15;++N){FVector Next;const FVector Want=H.PushStart+Direction*(N*10);if(!Placement(Object,Want,Next)){break;}
         FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(B91Push),false,Object);
         if(GetWorld()->SweepSingleByChannel(Hit,H.Destination+FVector(0,0,Object->Size().Z*.5+3),Next+FVector(0,0,Object->Size().Z*.5+3),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeBox(Object->Size()*.5),Q)){break;}H.Destination=Next;}
        if(FVector::Dist2D(H.Destination,H.PushStart)<1){Reply(PC,R,2,TEXT("Push blocked"));return;}
    }
    if(R.Verb==ELDObjectVerb::Ignite&&!BeginIgnition(PC,Object,H,Why)){Reply(PC,R,2,Why);return;}
    if(R.Verb==ELDObjectVerb::Harvest||R.Verb==ELDObjectVerb::Chop){ReserveTool(PC,H);}
    Holds.Add(PC,H);Reply(PC,R,0,TEXT("Action started; S stops. Movement, damage, invalid target or lost connection cancels"));
}
void ALDWorldObjects::Hold(ALDB91Controller* PC,uint32 Match,uint32 Life,uint32 Seq,uint32 Nonce,bool Release)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M?M->StateFor(PC):nullptr;if(!O||O->Match!=Match||O->Life!=Life){return;}
    if(Release){Clients.FindOrAdd(PC).Cancelled=FMath::Max(Clients.FindOrAdd(PC).Cancelled,Seq);}auto* H=Holds.Find(PC);if(!H||H->Request.Sequence!=Seq){return;}
    if(Release){Cancel(PC,TEXT("Action stopped"));return;}if(Nonce>H->Nonce){H->Nonce=Nonce;H->Heartbeat=GetWorld()->GetTimeSeconds();}
}
void ALDWorldObjects::Cancel(ALDB91Controller* PC,const FString& Why)
{if(const auto* H=Holds.Find(PC)){const auto Copy=*H;Holds.Remove(PC);if(Copy.Request.Verb==ELDObjectVerb::Use){EndTraversal(PC,Copy,false);}if((Copy.Request.Verb==ELDObjectVerb::Ignite||Copy.Request.Verb==ELDObjectVerb::Harvest||Copy.Request.Verb==ELDObjectVerb::Chop)){EndIgnition(Copy);}Reply(PC,Copy.Request,2,Why);}}
void ALDWorldObjects::Quote(ALDB91Controller* PC,ALDWorldObject* O)
{
    if(!PC||!O||!O->Container()||!Visible(PC,O)||FVector::Dist2D(PC->GetPawn()->GetActorLocation(),O->GetActorLocation())>120){return;}
    auto& C=Clients.FindOrAdd(PC);const auto* Id=C.Handles.Find(O);if(!Id){return;}C.Opened.Add(O,O->Revision);TArray<FLDContainerEntry> Items;
    for(auto P:O->Contents){if(P.IsValid()&&P->State==1&&P->Holder==O){FLDContainerEntry E;E.Item=P->Identity;E.Kind=P->Kind;E.Revision=P->Revision;Items.Add(E);}}
    PC->ClientContainerView(PC->OwnerState.Match,PC->OwnerState.Life,*Id,O->Revision,Items);
}
ALDWorldObject* ALDWorldObjects::Wrap(ALDItem* Item,FVector Surface)
{
    for(auto O:Objects){if(O.IsValid()&&O->Physical==Item){O->Apply(Surface+FVector(0,0,O->Size().Z*.5+3),O->Flags(),O->Revision+1);Item->SetActorLocation(O->GetActorLocation());return O.Get();}}
    auto* O=GetWorld()->SpawnActor<ALDWorldObject>();O->Kind=ELDObjectKind::LooseItem;O->Physical=Item;O->Apply(Surface+FVector(0,0,18),0,1);Item->SetActorLocation(O->GetActorLocation());Objects.Add(O);return O;
}
bool ALDWorldObjects::Commit(ALDB91Controller* PC,ALDWorldObject* O,const FHold& H,FString& Why)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto& R=H.Request;auto* Inventory=M->Inventory.Get();
    if(R.Verb==ELDObjectVerb::Ignite){return CommitIgnition(PC,O,H,Why);}
    if((uint8(R.Verb)>=uint8(ELDObjectVerb::Attach)&&uint8(R.Verb)<=uint8(ELDObjectVerb::Retrieve))){return CommitLink(PC,O,H,Why);}
    if(R.Verb==ELDObjectVerb::Harvest||R.Verb==ELDObjectVerb::Chop)
    {
        const FGuid Receipt=Clients.FindChecked(PC).Receipts.FindChecked(ObjectReceiptKey(R.Life,R.Sequence)).Id;
        if(R.Verb==ELDObjectVerb::Harvest)
        {auto* Item=GetWorld()->SpawnActor<ALDItem>();if(!Item){return false;}Item->Kind=ELDItemKind::Fiber;Item->Institution=O->HarvestSource;Item->OriginReceipt=Receipt;
         if(!Inventory->Adopt(PC,Item,TEXT("SaplingHarvest"))){Item->Destroy();Why=TEXT("Harvest unavailable; source retained");return false;}Finite.Add(Item->Identity,Item);O->YieldIdentity=Item->Identity;}
        O->HarvestReceipt=Receipt;O->Hp=0;O->Broken=true;O->Apply(O->GetActorLocation(),O->Flags(),O->Revision+1);return true;
    }
    if(R.Verb==ELDObjectVerb::Open){Quote(PC,O);return true;}
    if(R.Verb==ELDObjectVerb::Take||R.Verb==ELDObjectVerb::Pickup)
    {
        auto* P=R.Verb==ELDObjectVerb::Take?Inventory->Owned(O,R.Item):O->Physical.Get();const auto PreviousHolder=P->Holder;const uint8 State=P->State;
        P->Holder.Reset();P->State=0;if(!Inventory->Adopt(PC,P)){P->Holder=PreviousHolder;P->State=State;Why=TEXT("Item transfer unavailable; no change");return false;}
        if(R.Verb==ELDObjectVerb::Take){O->Contents.Remove(P);}O->Apply(O->GetActorLocation(),O->Flags(),O->Revision+1);if(O->Container()){Quote(PC,O);}return true;
    }
    if(R.Verb==ELDObjectVerb::Store||R.Verb==ELDObjectVerb::Drop)
    {
        auto* P=Inventory->Owned(PC,R.Item);if(M->Trade){M->Trade->CancelFor(PC);}Inventory->Remove(PC,P,false);if(P->Holder.IsValid()){Why=TEXT("Item transfer unavailable");return false;}
        if(R.Verb==ELDObjectVerb::Store){P->Holder=O;P->State=1;++P->Revision;O->Contents.Add(P);O->Apply(O->GetActorLocation(),O->Flags(),O->Revision+1);Quote(PC,O);}
        else{P->SetActorLocation(H.Destination+FVector(0,0,18));Wrap(P,H.Destination);}return true;
    }
    if(R.Verb==ELDObjectVerb::Carry)
    {O->Carrier=PC;O->bCarriedWorld=true;FCargo C;C.Object=O;C.Life=R.Life;C.Damage=H.Damage;Cargo.Add(PC,C);PC->ClientCargoState(R.Match,R.Life,true,R.Observed);O->Apply(O->GetActorLocation(),O->Flags(),O->Revision+1);return true;}
    if(R.Verb==ELDObjectVerb::Place)
    {FVector Surface;if(!Placement(O,H.Destination,Surface)){Why=TEXT("Placement unavailable; cargo retained");return false;}O->Carrier.Reset();O->bCarriedWorld=false;Cargo.Remove(PC);PC->ClientCargoState(R.Match,R.Life,false,FGuid());O->LastClear=Surface;O->Apply(Surface+FVector(0,0,O->Size().Z*.5+3),O->Flags(),O->Revision+1);return true;}
    if(R.Verb==ELDObjectVerb::Push)
    {FVector Surface;if(!Placement(O,H.Destination,Surface)){Why=TEXT("Push endpoint unavailable");return false;}O->LastClear=Surface;return true;}
    if(R.Verb==ELDObjectVerb::Break)
    {
        const FVector Surface=O->GetActorLocation()-FVector(0,0,O->Size().Z*.5+3);const auto Contents=O->Contents;O->Contents.Reset();O->Broken=true;O->Apply(O->GetActorLocation(),O->Flags(),O->Revision+1);
        for(auto P:Contents){if(P.IsValid()){P->Holder.Reset();P->State=0;P->bReserved=false;++P->Revision;P->SetActorLocation(Surface+FVector(0,0,18));Wrap(P.Get(),Surface);}}return true;
    }
    return false;
}
bool ALDWorldObjects::ForceDrop(ALDB91Controller* PC)
{
    auto* O=Carried(PC);if(!O){return true;}Cancel(PC,TEXT("Cargo dropped"));const FVector Near=PC&&PC->GetPawn()?PC->GetPawn()->GetActorLocation()-FVector(0,0,90):O->LastClear;
    FVector Surface=Near;bool Found=false;const auto* Map=ULDMapDefinition::Get(GetWorld());
    for(int Ring=1;Ring<=20&&!Found;++Ring){for(int Y=-Ring;Y<=Ring&&!Found;++Y){for(int X=-Ring;X<=Ring&&!Found;++X)
    {if(FMath::Max(FMath::Abs(X),FMath::Abs(Y))!=Ring){continue;}FVector Point=Near+FVector(X*100,Y*100,0);Found=Placement(O,Point,Surface);
     // A retrieved roof link must be able to drop onto nearby ground below
     // the upper floor instead of snapping back to its distant source pad.
     if(!Found&&Map){Point.Z=Map->Height(FVector2D(Point));Found=Placement(O,Point,Surface);}}}}
    if(!Found){Found=Placement(O,O->LastClear,Surface);}if(!Found){return false;}
    O->Carrier.Reset();O->bCarriedWorld=false;Cargo.Remove(PC);if(PC){PC->ClientCargoState(PC->OwnerState.Match,PC->OwnerState.Life,false,FGuid());}O->LastClear=Surface;O->Apply(Surface+FVector(0,0,O->Size().Z*.5+3),O->Flags(),O->Revision+1);return true;
}
void ALDWorldObjects::DiscoverDrops()
{
    for(TActorIterator<ALDItem> P(GetWorld());P;++P){if(P->State==0&&!P->Holder.IsValid())
    {const auto* Existing=Objects.FindByPredicate([&](auto O){return O.IsValid()&&O->Physical==*P;});
     if(!Existing||!(*Existing)->GetActorLocation().Equals(P->GetActorLocation(),.1)){FVector Surface=P->GetActorLocation()-FVector(0,0,12);Wrap(*P,Surface);}}}
}
bool ALDWorldObjects::Conserved(FString& Error) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();for(const auto& Pair:Finite)
    {
        const auto* P=Pair.Value.Get();if(!P){if(Consumed.Contains(Pair.Key)){continue;}Error=TEXT("Finite item disappeared without a consume receipt");return false;}
        int Locations=0;
        for(auto O:Objects){if(O.IsValid()){Locations+=O->Contents.ContainsByPredicate([&](auto I){return I.Get()==P;});Locations+=O->Physical.Get()==P&&P->State==0&&!P->Holder.IsValid();}}
        if(auto* PC=Cast<ALDPlayerController>(P->Holder.Get())){const auto* Own=M->StateFor(PC);Locations+=Own&&Own->Inventory.Find(P->Identity)!=nullptr;}
        if(Locations!=1){Error=FString::Printf(TEXT("Finite item %s has %d locations"),*Pair.Key.ToString(),Locations);return false;}
    }return true;
}
void ALDWorldObjects::RecordConsumption(ALDItem* Item)
{if(Item&&Finite.Contains(Item->Identity)){Consumed.Add(Item->Identity);}}
void ALDWorldObjects::Publish()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M){return;}++ViewRevision;DiscoverDrops();
    for(FConstPlayerControllerIterator I=GetWorld()->GetPlayerControllerIterator();I;++I)
    {
        auto* PC=Cast<ALDB91Controller>(I->Get());const auto* Own=PC?M->StateFor(PC):nullptr;if(!Own){continue;}auto& C=Clients.FindOrAdd(PC);C.Visible.Reset();TArray<FLDObjectView> View;
        for(auto O:Objects){if(!O.IsValid()||!Visible(PC,O.Get())||View.Num()>=128){continue;}auto& Id=C.Handles.FindOrAdd(O);if(!Id.IsValid()){Id=FGuid::NewGuid();}C.Visible.Add(Id);
        FLDObjectView V;V.Handle=Id;V.Kind=O->Kind;V.Position=O->GetActorLocation();if(O->Kind==ELDObjectKind::Anchor&&PC->GetPawn()){const FVector Near=O->EndFor(PC->GetPawn()->GetActorLocation());V.Position=Near+FVector(0,0,18);V.Endpoint=Near.Equals(O->Lower)?O->Upper:O->Lower;}
        V.Revision=O->Revision;V.State=O->Flags();V.PhysicalRevision=O->Physical.IsValid()&&O->Physical->State==0&&!O->Physical->Holder.IsValid()?O->Physical->Revision:0;View.Add(V);C.LastSeen.Add(Id,V);}
        // Observing empty space at a previously seen cargo position clears its
        // stale client obstacle. This reveals absence here, never its new hidden
        // position, state, carrier or inventory. It grants no interaction handle.
        if(auto* E=ALDEnvironmentDirector::Get(GetWorld());E&&PC->GetPawn()&&Own->bActive)
        {const double Radius=E->SightFor(PC);
         for(auto& Pair:C.LastSeen){auto& Old=Pair.Value;
         if(View.Num()>=128||C.Visible.Contains(Pair.Key)||(Old.State&4)){continue;}
         const double Half=Old.Kind==ELDObjectKind::Crate?110:Old.Kind==ELDObjectKind::Barrel?70:40;
         if(E->Ray(PC->GetPawn()->GetActorLocation(),FVector(Old.Position)+FVector(0,0,90-Half),nullptr,Radius))
         {Old.State=4;Old.PhysicalRevision=0;View.Add(Old);}}}
        for(auto Q=C.Opened.CreateIterator();Q;++Q){if(!Q.Key().IsValid()||!Visible(PC,Q.Key().Get())||Q.Value()!=Q.Key()->Revision||!PC->GetPawn()||FVector::Dist2D(PC->GetPawn()->GetActorLocation(),Q.Key()->GetActorLocation())>120){Q.RemoveCurrent();}}
        PC->ClientObjectView(Own->Match,Own->Life,ViewRevision,View);
    }
    if(GetNetMode()!=NM_DedicatedServer)
    {auto* Local=Cast<ALDB91Controller>(GetWorld()->GetFirstPlayerController());for(auto O:Objects){if(O.IsValid()){O->SetObserved(O->Available()&&Local&&Local->IsLocalController()&&Visible(Local,O.Get()));}}}
}
void ALDWorldObjects::Tick(float Delta)
{
    Super::Tick(Delta);if(!Ready||GetNetMode()==NM_Client){return;}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M){return;}const double Now=GetWorld()->GetTimeSeconds();
    TickTools(Now);TickBurning(Now);
    TArray<TWeakObjectPtr<ALDB91Controller>> Returning;Returns.GetKeys(Returning);
    for(auto Weak:Returning){if(!Weak.IsValid()||RestoreTraversal(Weak.Get(),Returns.FindChecked(Weak),false)){Returns.Remove(Weak);}}
    TArray<TWeakObjectPtr<ALDB91Controller>> Carriers;Cargo.GetKeys(Carriers);
    for(auto Weak:Carriers)
    {auto* PC=Weak.Get();const auto C=Cargo.FindRef(Weak);auto* O=C.Object.Get();const auto* Own=PC?M->StateFor(PC):nullptr;const auto* B=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;const auto* S=B?M->Combat->StateFor(B):nullptr;
     if(!PC||!Own||!B||!S||S->Hp<=0||Own->Life!=C.Life||S->LastDamage!=C.Damage)
     {if(PC){ForceDrop(PC);}else if(O)
      {FVector Surface=O->LastClear;bool Safe=Placement(O,O->LastClear,Surface);
       for(int Ring=1;Ring<=20&&!Safe;++Ring){for(int Y=-Ring;Y<=Ring&&!Safe;++Y){for(int X=-Ring;X<=Ring&&!Safe;++X)
       {if(FMath::Max(FMath::Abs(X),FMath::Abs(Y))!=Ring){continue;}Safe=Placement(O,O->LastClear+FVector(X*100,Y*100,0),Surface);}}}
       if(Safe){O->Carrier.Reset();O->bCarriedWorld=false;O->LastClear=Surface;O->Apply(Surface+FVector(0,0,O->Size().Z*.5+3),O->Flags(),O->Revision+1);Cargo.Remove(Weak);}}
      continue;}
     if(O){O->SetActorLocation(B->GetActorLocation()+B->GetActorForwardVector()*100+FVector(0,0,40));}}
    TArray<TWeakObjectPtr<ALDB91Controller>> Active;Holds.GetKeys(Active);
    for(auto Weak:Active)
    {
        auto* PC=Weak.Get();auto* Live=Holds.Find(Weak);if(!Live){continue;}const auto H=*Live;auto* O=H.Object.Get();const auto* Own=PC?M->StateFor(PC):nullptr;const auto* B=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;const auto* S=B?M->Combat->StateFor(B):nullptr;FString Why;
        if(!PC){if((H.Request.Verb==ELDObjectVerb::Ignite||H.Request.Verb==ELDObjectVerb::Harvest||H.Request.Verb==ELDObjectVerb::Chop)){EndIgnition(H);}Holds.Remove(Weak);continue;}
        const bool Use=H.Request.Verb==ELDObjectVerb::Use;
        FName Cause;
        if(!Own||!B||!S||Own->Match!=H.Request.Match||Own->Life!=H.Request.Life||H.Body!=B){Cause=TEXT("lifecycle");Why=TEXT("Action stopped: character or life changed");}
        else if(S->LastDamage!=H.Damage){Cause=TEXT("damage");Why=TEXT("Action interrupted by damage");}
        else if(!Use&&FVector::DistSquared(H.Start,B->GetActorLocation())>25){Cause=TEXT("movement");Why=TEXT("Action interrupted by movement");}
        else if(!LDCommandLiveness::Fresh(Now+1.e-6>=H.Ends?FMath::Max(Now,H.Ends):Now,H.Heartbeat)){Cause=TEXT("liveness");Why=TEXT("Action stopped: command connection inactive");}
        else if(Use&&Now-H.Started>3){Cause=TEXT("traversal_timeout");Why=TEXT("Traversal timed out");}
        else if(!Eligible(PC,O,H.Request,Why)){Cause=TEXT("eligibility");if(Why.IsEmpty()){Why=TEXT("Action is no longer eligible");}}
        if(!Cause.IsNone())
        {
#if !UE_BUILD_SHIPPING
            UE_LOG(LogTemp,Display,TEXT("LD_OBJECT_CANCEL role=%d sequence=%u verb=%u cause=%s age=%.6f movement_cm=%.6f elapsed=%.6f frame=%.6f reason=%s"),PC->EnvironmentProbeRole,H.Request.Sequence,uint8(H.Request.Verb),*Cause.ToString(),Now-H.Heartbeat,B?FVector::Dist(H.Start,B->GetActorLocation()):-1.,Now-H.Started,double(Delta),*Why);
#endif
            Cancel(PC,Why);continue;
        }
        if(Use&&!MoveTraversal(PC,H,Now,Why)){Cancel(PC,Why);continue;}
        if(H.Request.Verb==ELDObjectVerb::Push&&O)
        {
            const FVector Want=FMath::Lerp(H.PushStart,H.Destination,FMath::Clamp((Now-H.Started)/(H.Ends-H.Started),0.,1.));FVector Surface;
            FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(B91PushLive),false,O);
            if(!Placement(O,Want,Surface)||GetWorld()->SweepSingleByChannel(Hit,O->GetActorLocation(),Surface+FVector(0,0,O->Size().Z*.5+3),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeBox(O->Size()*.5),Q))
            {Cancel(PC,TEXT("Push stopped at an occupied endpoint"));continue;}
            O->LastClear=Surface;O->Apply(Surface+FVector(0,0,O->Size().Z*.5+3),O->Flags(),O->Revision+1);Live=Holds.Find(Weak);if(Live){Live->Request.ObjectRevision=O->Revision;}
        }
        if(Now+1e-6<H.Ends){continue;}const bool Done=Commit(PC,O,H,Why);Holds.Remove(Weak);if((H.Request.Verb==ELDObjectVerb::Ignite||H.Request.Verb==ELDObjectVerb::Harvest||H.Request.Verb==ELDObjectVerb::Chop)){EndIgnition(H);}if(Use){EndTraversal(PC,H,Done);}if(Done){++Committed;}Reply(PC,H.Request,Done?1:2,Why.IsEmpty()?(Done?TEXT("Object transaction committed"):TEXT("Object transaction unavailable")):Why,O?O->Revision:0);
    }
    if(Now>=NextPublish){NextPublish=Now+.1;Publish();}for(auto I=Clients.CreateIterator();I;++I){if(!I.Key().IsValid()){I.RemoveCurrent();}}
}
