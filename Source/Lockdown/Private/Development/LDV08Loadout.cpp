#include "Development/LDV08Loadout.h"
#include "Inventory/LDEquipmentCatalog.h"

bool LDV08::PresetEnabled(uint8 P){return P<9;} // B82 supplies the authored Frostline teaching kit.
const TCHAR* LDV08::TraitName(const FLDCharacterBuild& B)
{
    static const TCHAR* Names[]={TEXT("Load Planning"),TEXT("Patient Pursuit"),TEXT("Field Practice"),TEXT("Directional Discipline"),TEXT("Patient Pursuit"),TEXT("Field Practice"),TEXT("Trailcraft"),TEXT("Careful Assembly"),TEXT("Convincing Decoy")};
    return B.RulesVersion==8&&B.Preset<9?Names[B.Preset]:TEXT("None");
}
int32 LDV08::TraitValue(const FLDCharacterBuild& B,FName Rule,int32 Base)
{
    if(B.RulesVersion!=8){return Base;}
    if(Rule==TEXT("capacity")&&B.Preset==0){return Base+4;}
    if(Rule==TEXT("brace_arc")&&B.Preset==3){return 140;}
    if(Rule==TEXT("mark_ms")&&(B.Preset==1||B.Preset==4)){return Base+1000;}
    if(Rule==TEXT("sight")&&B.Preset==6){return FMath::Min(50,Base+30);}
    if(Rule==TEXT("assembly_discount")&&B.Preset==7){return FMath::Max(1,Base);}
    if(Rule==TEXT("decoy_ms")&&B.Preset==8){return Base+1000;}
    if((Rule==TEXT("frozen_ms")||Rule==TEXT("burning_ms")||Rule==TEXT("electrified_ms"))&&(B.Preset==2||B.Preset==5)){return FMath::Min(10000,Base+1000);}
    return Base;
}
bool LDV08::ItemSource(const FLDInventory& I,const FLDCharacterBuild& B,FGuid Item,uint8 Slot,bool Accessory)
{
    const auto* C=ULDEquipmentCatalog::Load(8);const auto* Entry=I.Find(Item);const auto* D=Entry&&C?C->Find(Entry->Kind):nullptr;
    if(I.RulesVersion!=8||B.RulesVersion!=8||!D||D->Operation.IsEmpty()||!Entry->BindingMask){return false;}
    if(Accessory){return Slot<4&&I.Bindings.IsValidIndex(7+Slot)&&D->Binding==7&&I.Bindings[7+Slot]==Item;}
    return Slot<6&&B.Actions.IsValidIndex(Slot)&&D->Binding<7&&B.Actions[Slot].Source==ELDActionSource::Gear&&B.Actions[Slot].Instance==Item&&B.Actions[Slot].Definition==FName(*D->Id);
}
bool LDV08::Validate(const FLDInventory& I,const FLDCharacterBuild& B,FString& Why)
{
    auto Reject=[&](const TCHAR* Text){Why=Text;return false;};const auto* C=ULDEquipmentCatalog::Load(8);
    if(!C||I.RulesVersion!=8||B.RulesVersion!=8||I.Bindings.Num()!=11||I.Quick.Num()!=0||B.Actions.Num()!=6||B.Slots.Num()!=6||!PresetEnabled(B.Preset)){return Reject(TEXT("Invalid schema8 loadout shape or unavailable preset"));}
    TSet<FGuid> Seen;TSet<FName> Families;
    for(int32 N=0;N<11;++N)
    {
        const auto Id=I.Bindings[N];if(!Id.IsValid()){continue;}const auto* Item=I.Find(Id);const auto* D=Item?C->Find(Item->Kind):nullptr;
        if(!D||Item->bReserved||!(D->Binding==N||D->Binding==7&&N>=7||N==6&&D->TwoHanded&&I.Bindings[5]==Id)){return Reject(TEXT("Binding requires a supported owned unreserved matching item"));}
        if(Seen.Contains(Id)&&!(N==6&&D->TwoHanded&&I.Bindings[5]==Id)){return Reject(TEXT("One physical instance cannot occupy multiple bindings"));}Seen.Add(Id);
        if(D->TwoHanded&&(I.Bindings[5]!=Id||I.Bindings[6]!=Id)){return Reject(TEXT("Two-handed weapon reserves both hands"));}
        if(N>=7&&!D->Family.IsEmpty()&&!D->Operation.IsEmpty()){if(Families.Contains(D->CooldownKey())){return Reject(TEXT("Duplicate action family"));}Families.Add(D->CooldownKey());}
    }
    TSet<ELDSkill> Learned;TSet<FGuid> Gear;
    for(int32 N=0;N<6;++N)
    {
        const auto& A=B.Actions[N];
        if(A.Source==ELDActionSource::Learned)
        {if(A.Instance.IsValid()||!A.Definition.IsNone()||A.Skill!=B.Slots[N]){return Reject(TEXT("Invalid learned action source"));}if(A.Skill==ELDSkill::None){continue;}if(Learned.Contains(A.Skill)){return Reject(TEXT("Duplicate learned action"));}Learned.Add(A.Skill);}
        else if(A.Source==ELDActionSource::Gear)
        {if(A.Skill!=ELDSkill::None||B.Slots[N]!=ELDSkill::None||Gear.Contains(A.Instance)||!ItemSource(I,B,A.Instance,N,false)){return Reject(TEXT("Body action needs one current equipped source and shared slot"));}Gear.Add(A.Instance);const auto* D=C->Find(I.Find(A.Instance)->Kind);if(Families.Contains(D->CooldownKey())){return Reject(TEXT("Duplicate action family"));}Families.Add(D->CooldownKey());}
        else{return Reject(TEXT("Unknown action source"));}
    }
    Why.Reset();return true;
}
bool LDV08::InvalidateSources(FLDCharacterBuild& B,const FLDInventory& I)
{
    bool Changed=false;if(B.RulesVersion!=8){return false;}
    for(int32 N=0;N<B.Actions.Num();++N){if(B.Actions[N].Source==ELDActionSource::Gear&&!ItemSource(I,B,B.Actions[N].Instance,N,false)){B.Actions[N]=FLDActionRef();B.Slots[N]=ELDSkill::None;Changed=true;}}
    if(Changed){++B.Revision;}return Changed;
}
bool LDV08::PreparationPermissions(const FLDCharacterBuild& B,const FLDBuildPlan& P,bool SkillAllowed,bool ItemAllowed)
{
    if(B.RulesVersion!=8){return SkillAllowed;}
    if(P.Actions.Num()!=6){return false;}
    const bool Stats=P.Attributes!=B.Attributes||P.Ranks!=B.Ranks||P.Subclass!=B.Subclass;
    if(Stats&&!SkillAllowed){return false;}
    for(int32 N=0;N<6;++N){if(!(B.Actions[N]==P.Actions[N])&&((B.Actions[N].Source==ELDActionSource::Learned&&B.Actions[N].Skill!=ELDSkill::None||P.Actions[N].Source==ELDActionSource::Learned&&P.Actions[N].Skill!=ELDSkill::None)&&!SkillAllowed||
        (B.Actions[N].Source==ELDActionSource::Gear||P.Actions[N].Source==ELDActionSource::Gear)&&!ItemAllowed)){return false;}}
    return true;
}
bool LDV08::Project(const FLDInventory& I,const FLDCharacterBuild& B,const FLDBuildPlan& P,FLDInventory& Out,FLDCharacterBuild& Build,FString& Why)
{
    if(P.InventoryRevision!=I.Revision||P.Bindings.Num()!=11||!LDDevelopment::Prepare(B,P,Build,Why)){Why=TEXT("Stale or invalid atomic loadout draft");return false;}
    Out=I;Out.Bindings=P.Bindings;
    for(auto& Item:Out.Items){Item.BindingMask=0;for(int32 N=0;N<11;++N){if(Out.Bindings[N]==Item.Id){Item.BindingMask|=uint16(1<<N);}}}
    if(!Validate(Out,Build,Why)){return false;}
    Build.Gear=LDGear::Derive(Out);return LDInventory::Fits(Out,Build,{}, {},&Why);
}
