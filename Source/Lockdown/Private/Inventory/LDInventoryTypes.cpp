#include "Inventory/LDInventoryTypes.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Inventory/LDAcquisitionCatalog.h"
ALDItem::ALDItem(){bReplicates=false;RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));}
FString LDInventory::Name(ELDItemKind Kind,ELDSkill Skill)
{
    switch(Kind)
    {
    case ELDItemKind::Book:return FString(TEXT("Book: "))+LDDevelopment::SkillName(Skill);
    case ELDItemKind::WardCharge:return TEXT("Ward charge");
    case ELDItemKind::Seal:return TEXT("Concord Seal");
    case ELDItemKind::Fiber:return TEXT("Fiber");
    case ELDItemKind::Ore:return TEXT("Ore");
    case ELDItemKind::Crystal:return TEXT("Crystal");
    case ELDItemKind::Toolkit:return TEXT("Toolkit");
    case ELDItemKind::Core:return TEXT("Core");
    case ELDItemKind::NullBell:return TEXT("Null Bell");
    case ELDItemKind::ArcaneCatalyst:return TEXT("Arcane Catalyst");
    case ELDItemKind::HeartFragment:return TEXT("Heart Fragment");
    case ELDItemKind::AncientKeyA:return TEXT("Ancient Key A");
    case ELDItemKind::AncientKeyB:return TEXT("Ancient Key B");
    case ELDItemKind::AncientKeyC:return TEXT("Ancient Key C");
    case ELDItemKind::Heartbreaker:return TEXT("Heartbreaker");
    default:if(const auto* C=ULDEquipmentCatalog::Load()){if(const auto* D=C->Find(Kind)){return D->Name;}}if(const auto* C=ULDAcquisitionCatalog::Load()){if(const auto* D=C->Find(Kind)){return D->Name;}}return TEXT("Unknown item");
    }
}
bool LDInventory::QuickUsable(ELDItemKind Kind,uint8 Rules)
{return (Rules!=7&&Kind==ELDItemKind::Book)||Kind==ELDItemKind::WardCharge||Kind==ELDItemKind::NullBell;}
bool LDInventory::Commodity(ELDItemKind Kind)
{return Kind>=ELDItemKind::Fiber&&Kind<=ELDItemKind::Core;}
int32 LDInventory::UnitWeight(ELDItemKind Kind)
{
    switch(Kind)
    {
    case ELDItemKind::NullBell:case ELDItemKind::Book:case ELDItemKind::WardCharge:case ELDItemKind::Fiber:return 1;
    case ELDItemKind::Seal:case ELDItemKind::Crystal:return 2;
    case ELDItemKind::Ore:return 3;
    case ELDItemKind::Toolkit:return 4;
    case ELDItemKind::Core:return 6;
    default:if(const auto* C=ULDEquipmentCatalog::Load()){if(const auto* D=C->Find(Kind)){return D->Weight;}}if(Kind==ELDItemKind::ArcaneCatalyst||Kind==ELDItemKind::AncientKeyA||Kind==ELDItemKind::AncientKeyB||Kind==ELDItemKind::AncientKeyC){return 1;}if(Kind==ELDItemKind::HeartFragment){return 2;}if(Kind==ELDItemKind::Heartbreaker){return 4;}if(const auto* C=ULDAcquisitionCatalog::Load()){if(const auto* D=C->Find(Kind)){return D->Weight;}}return -1;
    }
}
int32 LDInventory::Weight(const FLDInventory& I)
{int32 Total=0;for(const auto& Item:I.Items){const int32 W=UnitWeight(Item.Kind);if(W<0){return -1;}Total+=W;}return Total;}
int32 LDInventory::WeightCapacity(const FLDCharacterBuild& B)
{return B.Attributes.Num()==6&&B.Attributes[0]>=8&&B.Attributes[0]<=16?60+2*(B.Attributes[0]-10)+(B.RulesVersion==7?B.Gear.Capacity:0):0;}
bool LDInventory::Fits(const FLDInventory& I,const FLDCharacterBuild& B,const TArray<FGuid>& Outgoing,const TArray<ELDItemKind>& Incoming,FString* Reason)
{
    auto Reject=[&](const TCHAR* Why){if(Reason){*Reason=Why;}return false;};
    int32 Mass=Weight(I),Seals=0;const int32 Limit=WeightCapacity(B);
    if(Mass<0||!Limit){return Reject(TEXT("Invalid item weight or carrying capacity"));}
    for(const auto& Item:I.Items){Seals+=Item.Kind==ELDItemKind::Seal;}
    TSet<FGuid> Removed;
    for(const auto Id:Outgoing)
    {
        const auto* Item=I.Find(Id);
        if(!Item||Item->BindingMask||Removed.Contains(Id)){return Reject(TEXT("Outgoing item changed; review again"));}
        Removed.Add(Id);Mass-=UnitWeight(Item->Kind);Seals-=Item->Kind==ELDItemKind::Seal;
    }
    for(const auto Kind:Incoming)
    {const int32 W=UnitWeight(Kind);if(W<0){return Reject(TEXT("Unknown item weight"));}Mass+=W;Seals+=Kind==ELDItemKind::Seal;}
    if(I.Items.Num()-Outgoing.Num()+Incoming.Num()>I.Capacity){return Reject(TEXT("Inventory full; free a bag slot"));}
    if(Mass>Limit){return Reject(TEXT("Cargo too heavy; reduce carried weight"));}
    if(Seals>1){return Reject(TEXT("Only one legacy Seal may be carried"));}
    if(Reason){Reason->Reset();}return true;
}
bool LDInventory::BindQuick(FLDInventory& I,FGuid Id,uint8 Slot,FString& Reason)
{
    const auto* Item=I.Find(Id);if(Slot>=I.Quick.Num()||!Item||Item->bReserved||!QuickUsable(Item->Kind,I.RulesVersion)){Reason=TEXT("Item cannot be assigned");return false;}
    for(auto& Ref:I.Quick){if(Ref==Id){Ref.Invalidate();}}I.Quick[Slot]=Id;++I.Revision;return true;
}
void LDInventory::Remove(FLDInventory& I,FGuid Id)
{for(auto& Ref:I.Bindings){if(Ref==Id){Ref.Invalidate();}}I.Items.RemoveAll([&](const auto& Item){return Item.Id==Id;});for(auto& Ref:I.Quick){if(Ref==Id){Ref.Invalidate();}}++I.Revision;}
bool LDInventory::Valid(const FLDInventory& I)
{
    if(!I.Capacity||I.Items.Num()>I.Capacity||(I.RulesVersion!=6&&I.RulesVersion!=7)||I.Quick.Num()!=(I.RulesVersion==7?3:4)||I.Bindings.Num()!=6){return false;}TSet<FGuid> Ids,Refs;TSet<uint8> Slots;int32 Seals=0;
    for(const auto& Item:I.Items){if(!Item.Id.IsValid()||Ids.Contains(Item.Id)||Item.Slot>=I.Capacity||Slots.Contains(Item.Slot)||(Item.Quality>2||UnitWeight(Item.Kind)<0)){return false;}Ids.Add(Item.Id);Slots.Add(Item.Slot);Seals+=Item.Kind==ELDItemKind::Seal;}
    for(auto Id:I.Quick){if(!Id.IsValid()){continue;}const auto* Item=I.Find(Id);if(!Item||!QuickUsable(Item->Kind,I.RulesVersion)||Refs.Contains(Id)){return false;}Refs.Add(Id);}
    for(const auto& Item:I.Items){uint8 Mask=0;for(int32 N=0;N<6;++N){if(I.Bindings[N]==Item.Id){Mask|=1<<N;}}if(Mask!=Item.BindingMask){return false;}}
    for(int32 N=0;N<6;++N){if(!I.Bindings[N].IsValid()){continue;}const auto* Item=I.Find(I.Bindings[N]);const auto* C=ULDEquipmentCatalog::Load();const auto* D=Item&&C?C->Find(Item->Kind):nullptr;if(!D||I.RulesVersion!=7||!(N==D->Binding||(D->Binding==4&&N==5)||(D->TwoHanded&&N==1&&I.Bindings[0]==Item->Id))){return false;}if(D->TwoHanded&&I.Bindings[1]!=Item->Id){return false;}for(int32 Earlier=0;Earlier<N;++Earlier){if(I.Bindings[Earlier]==Item->Id&&!(N==1&&Earlier==0&&D->TwoHanded)){return false;}}}
    return Seals<=1;
}
