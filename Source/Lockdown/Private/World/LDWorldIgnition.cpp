#include "World/LDWorldObjects.h"
#include "World/LDEnvironmentDirector.h"
#include "World/LDPortableTools.h"
#include "Combat/LDCombatDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Character/LDCharacter.h"
#include "Core/LDGroundQueries.h"
#include "Engine/World.h"

bool ALDWorldObjects::IgnitionEligible(ALDB91Controller* PC,ALDWorldObject* Socket,const FLDObjectRequest& R,FString& Why) const
{
    auto Fail=[&](const TCHAR* Text){Why=Text;return false;};auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M->StateFor(PC);auto* Body=Cast<ALDCharacter>(PC->GetPawn());const auto* S=M->Combat->StateFor(Body);const double Now=GetWorld()->GetTimeSeconds();const int64 At=LDTactical::Milliseconds(Now);
    if(Traversing(PC)||Carried(PC)||Now-FMath::Max3(S->LastDamage,S->LastDealt,S->QuietSince)<6||!M->Combat->Allows(Body,LDTactical::EAction::Item,&Why)){if(Why.IsEmpty()){Why=TEXT("Finish traversal/cargo and wait six quiet seconds");}return false;}
    const auto* Item=M->Inventory->Owned(PC,R.Item);const auto* H=Holds.Find(PC);const bool Held=H&&H->Request.Verb==ELDObjectVerb::Ignite&&H->Request.Sequence==R.Sequence;
    if(!Item||Item->Kind!=ELDItemKind::Torch||Item->Quality!=0||R.Accessory>=4||O->Inventory.Bindings[7+R.Accessory]!=R.Item||!(Item->BindingMask&(1<<(7+R.Accessory)))||O->Inventory.Revision!=R.InventoryRevision||Item->Revision!=R.ItemRevision||Item->bReserved!=Held){return Fail(TEXT("Current bound Torch and exact inventory revision required"));}
    if(LDPortableTools::Remaining(Item,Now)<1000||Item->ReadyAt>At||O->ItemFamilies.FindRef(TEXT("torch_socket"))>At){return Fail(TEXT("Ignition needs one second of fuel and a ready12s family"));}
    if(Socket)
    {if((Socket->Kind!=ELDObjectKind::Sapling&&Socket->Kind!=ELDObjectKind::TallGrass)||!Socket->Available()||Socket->Hp<=0||Socket->BurnReceipt.IsValid()||Socket->Revision!=R.ObjectRevision||!Visible(PC,Socket)||FVector::Dist2D(Body->GetActorLocation(),Socket->GetActorLocation())>120||FMath::Abs(Body->GetActorLocation().Z-Socket->LastClear.Z-90)>100){return Fail(TEXT("A living observed sapling or grass socket within120cm is required"));}}
    else
    {FVector Ground;if(R.Observed.IsValid()||FVector::Dist2D(Body->GetActorLocation(),R.Point)>120||!LDGround::MapSurface(GetWorld(),R.Point,Ground)||FMath::Abs(Ground.Z-R.Point.Z)>20||FMath::Abs(Body->GetActorLocation().Z-Ground.Z-90)>100||!ALDEnvironmentDirector::Get(GetWorld())->Ray(Body->GetActorLocation(),Ground+FVector(0,0,90))){return Fail(TEXT("Observed Oil ground within120cm on this floor is required"));}}
    return true;
}
bool ALDWorldObjects::BeginIgnition(ALDB91Controller* PC,ALDWorldObject* Socket,FHold& H,FString& Why)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* Own=M->StateFor(PC);auto* Item=M->Inventory->Owned(PC,H.Request.Item);
    H.FieldLease=M->Combat->ReserveIgnition(H.Body.Get(),Socket?Socket->LastClear:FVector(H.Request.Point),Socket,LDTactical::Milliseconds(GetWorld()->GetTimeSeconds()));
    if(!H.FieldLease.IsValid()){Why=TEXT("No dry foliage/Oil cells or reserved field capacity; nothing spent");return false;}
    H.Tool=Item;Item->bReserved=true;++Item->Revision;++Own->Inventory.Revision;
    H.Request.ItemRevision=Item->Revision;H.Request.InventoryRevision=Own->Inventory.Revision;if(auto* E=const_cast<FLDInventoryItem*>(Own->Inventory.Find(Item->Identity))){Item->Project(*E);}M->Inventory->Changed(PC);return true;
}
void ALDWorldObjects::EndIgnition(const FHold& H)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M){return;}M->Combat->ReleaseEnvironment(H.FieldLease);auto* Item=H.Tool.Get();if(!Item||!Item->bReserved){return;}
    Item->bReserved=false;++Item->Revision;if(auto* PC=Cast<ALDPlayerController>(Item->Holder.Get())){if(auto* Own=M->StateFor(PC)){if(auto* E=const_cast<FLDInventoryItem*>(Own->Inventory.Find(Item->Identity))){Item->Project(*E);}++Own->Inventory.Revision;M->Inventory->Changed(PC);}}
}
bool ALDWorldObjects::CommitIgnition(ALDB91Controller* PC,ALDWorldObject* Socket,const FHold& H,FString& Why)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* Own=M->StateFor(PC);auto* Item=H.Tool.Get();const double Now=GetWorld()->GetTimeSeconds();const int64 At=LDTactical::Milliseconds(Now);
    if(!Item||!M->Combat->CommitIgnition(H.FieldLease,H.Body.Get(),Socket,At)){Why=TEXT("Ignition surface changed; fuel and cooldown retained");return false;}
    Item->FuelMs=LDPortableTools::Remaining(Item,Now)-1000;Item->FuelAt=Now;Item->ReadyAt=At+12000;Item->bReserved=false;++Item->Revision;Own->ItemFamilies.FindOrAdd(TEXT("torch_socket"))=Item->ReadyAt;
    if(auto* E=const_cast<FLDInventoryItem*>(Own->Inventory.Find(Item->Identity))){Item->Project(*E);}++Own->Inventory.Revision;M->Inventory->Changed(PC);
    if(auto* S=M->Combat->StateFor(H.Body.Get())){S->LastDealt=Now;S->ExposedUntil=FMath::Max(S->ExposedUntil,Now+4);}
    M->Combat->BreakConcealment(H.Body.Get(),true);
    if(Socket){const uint64 Key=(uint64(H.Request.Life)<<32)|H.Request.Sequence;Socket->BurnReceipt=Clients.FindChecked(PC).Receipts.FindChecked(Key).Id;Socket->BurnClearAt=Now+3;Socket->Apply(Socket->GetActorLocation(),Socket->Flags(),Socket->Revision+1);}
    Why=TEXT("Ignited once:1s fuel,12s cooldown,3s bounded fire; no harvest reward");return true;
}
void ALDWorldObjects::TickBurning(double Now)
{for(auto Weak:Objects){auto* O=Weak.Get();if(O&&(O->Kind==ELDObjectKind::Sapling||O->Kind==ELDObjectKind::TallGrass)&&!O->Broken&&O->BurnReceipt.IsValid()&&Now>=O->BurnClearAt){O->Hp=0;O->Broken=true;O->Apply(O->GetActorLocation(),O->Flags(),O->Revision+1);}}}
