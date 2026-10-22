#include "Inventory/LDInventoryTypes.h"
ALDItem::ALDItem(){bReplicates=false;RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));}
FString LDInventory::Name(ELDItemKind Kind,ELDSkill Skill)
{return Kind==ELDItemKind::Book?FString(TEXT("Book: "))+LDDevelopment::SkillName(Skill):Kind==ELDItemKind::WardCharge?TEXT("Ward charge"):TEXT("Concord Seal");}
bool LDInventory::BindQuick(FLDInventory& I,FGuid Id,uint8 Slot,FString& Reason)
{
    const auto* Item=I.Find(Id);if(Slot>=4||!Item||Item->bReserved||Item->Kind==ELDItemKind::Seal){Reason=TEXT("Item cannot be assigned");return false;}
    for(auto& Ref:I.Quick){if(Ref==Id){Ref.Invalidate();}}I.Quick[Slot]=Id;++I.Revision;return true;
}
void LDInventory::Remove(FLDInventory& I,FGuid Id)
{I.Items.RemoveAll([&](const auto& Item){return Item.Id==Id;});for(auto& Ref:I.Quick){if(Ref==Id){Ref.Invalidate();}}++I.Revision;}
bool LDInventory::Valid(const FLDInventory& I)
{
    if(!I.Capacity||I.Items.Num()>I.Capacity||I.Quick.Num()!=4){return false;}TSet<FGuid> Ids,Refs;TSet<uint8> Slots;int32 Seals=0;
    for(const auto& Item:I.Items){if(!Item.Id.IsValid()||Ids.Contains(Item.Id)||Item.Slot>=I.Capacity||Slots.Contains(Item.Slot)){return false;}Ids.Add(Item.Id);Slots.Add(Item.Slot);Seals+=Item.Kind==ELDItemKind::Seal;}
    for(auto Id:I.Quick){if(!Id.IsValid()){continue;}const auto* Item=I.Find(Id);if(!Item||Item->bReserved||Item->Kind==ELDItemKind::Seal||Refs.Contains(Id)){return false;}Refs.Add(Id);}return Seals<=1;
}
