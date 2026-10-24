#include "Inventory/LDInventoryTypes.h"
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
    default:return TEXT("Unknown item");
    }
}
bool LDInventory::QuickUsable(ELDItemKind Kind)
{return Kind==ELDItemKind::Book||Kind==ELDItemKind::WardCharge||Kind==ELDItemKind::NullBell;}
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
    default:return -1;
    }
}
int32 LDInventory::Weight(const FLDInventory& I)
{int32 Total=0;for(const auto& Item:I.Items){const int32 W=UnitWeight(Item.Kind);if(W<0){return -1;}Total+=W;}return Total;}
int32 LDInventory::WeightCapacity(const FLDCharacterBuild& B)
{return B.Attributes.Num()==6&&B.Attributes[0]>=8&&B.Attributes[0]<=16?60+2*(B.Attributes[0]-10):0;}
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
        if(!Item||Removed.Contains(Id)){return Reject(TEXT("Outgoing item changed; review again"));}
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
    const auto* Item=I.Find(Id);if(Slot>=4||!Item||Item->bReserved||!QuickUsable(Item->Kind)){Reason=TEXT("Item cannot be assigned");return false;}
    for(auto& Ref:I.Quick){if(Ref==Id){Ref.Invalidate();}}I.Quick[Slot]=Id;++I.Revision;return true;
}
void LDInventory::Remove(FLDInventory& I,FGuid Id)
{I.Items.RemoveAll([&](const auto& Item){return Item.Id==Id;});for(auto& Ref:I.Quick){if(Ref==Id){Ref.Invalidate();}}++I.Revision;}
bool LDInventory::Valid(const FLDInventory& I)
{
    if(!I.Capacity||I.Items.Num()>I.Capacity||I.Quick.Num()!=4){return false;}TSet<FGuid> Ids,Refs;TSet<uint8> Slots;int32 Seals=0;
    for(const auto& Item:I.Items){if(!Item.Id.IsValid()||Ids.Contains(Item.Id)||Item.Slot>=I.Capacity||Slots.Contains(Item.Slot)||UnitWeight(Item.Kind)<0){return false;}Ids.Add(Item.Id);Slots.Add(Item.Slot);Seals+=Item.Kind==ELDItemKind::Seal;}
    for(auto Id:I.Quick){if(!Id.IsValid()){continue;}const auto* Item=I.Find(Id);if(!Item||!QuickUsable(Item->Kind)||Refs.Contains(Id)){return false;}Refs.Add(Id);}return Seals<=1;
}
