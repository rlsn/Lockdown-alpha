#include "Development/LDV08Loadout.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Region/LDRegionDefinition.h"
bool LDGear::NaturalGate(const FLDCharacterBuild& B,ELDSkill S)
{
    if(uint8(S)<10){return true;}const auto* C=ULDEquipmentCatalog::Load(B.RulesVersion);const auto* G=C?C->Gate(S):nullptr;
    if(!G||B.Attributes.Num()!=6||G->Alternatives.IsEmpty()||G->Alternatives.Num()%6){return false;}
    for(int32 I=0;I<G->Alternatives.Num();I+=6){bool Pass=true;for(int32 A=0;A<6;++A){Pass&=B.Attributes[A]>=G->Alternatives[I+A];}if(Pass){return true;}}return false;
}
TArray<int32> LDGear::Allocate(const TArray<int32>& Requested,TArray<int32>& Granted)
{
    TArray<int32> Total={0,0,0,0,0,0};Granted.Init(0,Requested.Num());int32 Budget=6;
    for(int32 I=0;I<Requested.Num();++I){const int32 A=I%6,Value=FMath::Min3(FMath::Clamp(Requested[I],0,2),3-Total[A],Budget);Granted[I]=Value;Total[A]+=Value;Budget-=Value;}return Total;
}
FLDGearStats LDGear::Derive(const FLDInventory& I)
{
    FLDGearStats S;const auto* C=ULDEquipmentCatalog::Load(I.RulesVersion);if(I.RulesVersion<7||!C){return S;}
    TArray<int32> Requests;Requests.Init(0,6*I.Bindings.Num());TSet<FGuid> Seen;int32 Satchel=0;
    for(int32 Slot=0;Slot<I.Bindings.Num();++Slot)
    {
        const auto Id=I.Bindings[Slot];if(!Id.IsValid()||Seen.Contains(Id)){continue;}Seen.Add(Id);const auto* Item=I.Find(Id);const auto* D=Item?C->Find(Item->Kind):nullptr;if(!D){continue;}
        const auto& P=D->Stats;for(int32 A=0;A<6;++A){Requests[6*Slot+A]=P.Primary[A];}
        if(Slot==(I.RulesVersion==8?5:0)){S.WeaponBase=FMath::FloorToDouble(P.WeaponBase*(Item->Quality==2?114:Item->Quality==1?107:100)/100.);S.Reach=P.Reach;S.Interval=P.Interval;S.Ranged=P.Ranged;S.Watchblade=P.Watchblade;}
        S.ModifierMask|=P.ModifierMask;S.AttackSpeed+=P.AttackSpeed;S.Armor+=FMath::FloorToDouble(P.Armor*(Item->Quality==2?114:Item->Quality==1?107:100)/100.);S.FeeDiscount=FMath::Max(S.FeeDiscount,P.FeeDiscount);S.MagicResist+=P.MagicResist;S.Movement+=P.Movement;S.RoadMovement+=P.RoadMovement;S.MPRegen+=P.MPRegen;
        if(Item->Kind==ELDItemKind::WeightedSatchel){Satchel=FMath::Max(Satchel,P.Capacity);}else{S.Capacity+=P.Capacity;}
        S.SightBonus=FMath::Max(S.SightBonus,P.SightBonus);S.FootstepReduction=FMath::Max(S.FootstepReduction,P.FootstepReduction);S.BarrierDamage=FMath::Max(S.BarrierDamage,P.BarrierDamage);S.DirectionalGuard=FMath::Max(S.DirectionalGuard,P.DirectionalGuard);S.Scramble|=P.Scramble;S.SpellPercent+=P.SpellPercent;S.CastRange=FMath::Max(S.CastRange,P.CastRange);S.ControlPotency=FMath::Min<int32>(1,S.ControlPotency+P.ControlPotency);S.Detector=FMath::Max(S.Detector,P.Detector);
    }
    S.Interval=FMath::Max(200,S.Interval*100/(100+FMath::Clamp(S.AttackSpeed,0,20)));S.Primary=Allocate(Requests,S.Grants);S.Capacity+=Satchel;if(I.RulesVersion==8){S.SpellPercent=FMath::Min(10,S.SpellPercent);S.CastRange=FMath::Min(50,S.CastRange);S.SightBonus=FMath::Min(50,S.SightBonus);}return S;
}
bool LDGear::Propose(const FLDInventory& I,const FLDCharacterBuild& B,uint8 Command,FGuid Id,uint8 Slot,FLDInventory& Out,FString& Why)
{
    auto Reject=[&](const TCHAR* S){Why=S;return false;};const auto* Item=I.Find(Id);const auto* C=ULDEquipmentCatalog::Load(I.RulesVersion);
    if(I.RulesVersion==8)
    {
        if(!Item||Item->bReserved||Slot>=11){return Reject(TEXT("Stale loadout item"));}auto P=LDDevelopment::PlanFrom(B);P.InventoryRevision=I.Revision;P.Bindings=I.Bindings;
        if(Command==LDInventory::Unequip||Command==LDInventory::Unbind){for(auto& Ref:P.Bindings){if(Ref==Id){Ref.Invalidate();}}for(int32 N=0;N<6;++N){if(P.Actions[N].Instance==Id){P.Actions[N]=FLDActionRef();P.Slots[N]=ELDSkill::None;}}}
        else if(Command==LDInventory::Equip||Command==LDInventory::Bind){const auto* D=C?C->Find(Item->Kind):nullptr;if(!D){return Reject(TEXT("Unsupported item"));}for(auto& Ref:P.Bindings){if(Ref==Id){Ref.Invalidate();}}P.Bindings[Slot]=Id;if(D->TwoHanded){if(Slot!=5||P.Bindings[6].IsValid()){return Reject(TEXT("Two-handed weapon conflicts with offhand"));}P.Bindings[6]=Id;}}
        else{return Reject(TEXT("Unsupported binding operation"));}
        for(int32 N=0;N<6;++N){if(P.Actions[N].Source==ELDActionSource::Gear&&!P.Bindings.Contains(P.Actions[N].Instance)){P.Actions[N]=FLDActionRef();P.Slots[N]=ELDSkill::None;}}
        FLDCharacterBuild Draft;if(!LDV08::Project(I,B,P,Out,Draft,Why)){return false;}Out.Revision=I.Revision+1;return true;
    }
    if(I.RulesVersion!=7||B.RulesVersion!=7||!C||I.Bindings.Num()!=6||I.Quick.Num()!=3||!Item||Item->bReserved){return Reject(TEXT("Loadout item or profile changed"));}
    Out=I;
    if(Command==LDInventory::Bind)
    {if(!LDInventory::BindQuick(Out,Id,Slot,Why)){return false;}}
    else if(Command==LDInventory::Unbind){for(auto& Q:Out.Quick){if(Q==Id){Q.Invalidate();}}}
    else if(Command==LDInventory::Unequip)
    {if(!Item->BindingMask){return Reject(TEXT("Item is not equipped"));}for(auto& Ref:Out.Bindings){if(Ref==Id){Ref.Invalidate();}}for(auto& Ref:Out.Quick){if(Ref==Id){Ref.Invalidate();}}}
    else if(Command==LDInventory::Equip)
    {
        const auto* D=C->Find(Item->Kind);if(!D||Slot>=6||(Slot!=D->Binding&&!(D->Binding==4&&Slot==5))){return Reject(TEXT("Equipment does not fit that binding"));}
        if(Slot==1&&Out.Bindings[0].IsValid()&&Out.Bindings[0]==Out.Bindings[1]){return Reject(TEXT("Two-handed weapon reserves offhand"));}
        if(Slot==0&&Out.Bindings[0]==Out.Bindings[1]){Out.Bindings[1].Invalidate();}
        for(auto& Ref:Out.Bindings){if(Ref==Id){Ref.Invalidate();}}
        Out.Bindings[Slot]=Id;if(D->TwoHanded){if(Out.Bindings[1].IsValid()){return Reject(TEXT("Unbind offhand before a two-handed weapon"));}Out.Bindings[1]=Id;}
    }
    else{return Reject(TEXT("Unsupported loadout operation"));}
    for(auto& Entry:Out.Items){Entry.BindingMask=0;for(int32 N=0;N<6;++N){if(Out.Bindings[N]==Entry.Id){Entry.BindingMask|=1<<N;}}}
    FLDCharacterBuild Candidate=B;Candidate.Gear=Derive(Out);if(!LDInventory::Fits(Out,Candidate,{}, {},&Why)){return false;}
    Out.Revision=I.Revision+1;if(!LDInventory::Valid(Out)){return Reject(TEXT("Invalid final loadout"));}Why.Reset();return true;
}
bool LDGear::OnRoad(const UWorld* World,const FVector& P)
{
    const auto* D=LDRegion::Definition(World);if(!D){return false;}for(const auto& Route:D->Routes){for(int32 N=1;N<Route.Points.Num();++N){const FVector A(Route.Points[N-1].X,Route.Points[N-1].Y,P.Z),B(Route.Points[N].X,Route.Points[N].Y,P.Z);if(FMath::PointDistToSegment(P,A,B)<=100){return true;}}}return false;
}
FString LDGear::Preview(const FLDInventory& I,const FLDCharacterBuild& B)
{
    static const TCHAR* Names[]={TEXT("Weapon"),TEXT("Offhand"),TEXT("Body"),TEXT("Feet"),TEXT("Charm A"),TEXT("Charm B")};
    const auto* C=ULDEquipmentCatalog::Load(I.RulesVersion);const auto S=Derive(I);FString Text=TEXT("Six bindings · same carried instances\n");
    static const TCHAR* V08Names[]={TEXT("Head"),TEXT("Torso"),TEXT("Legs"),TEXT("Feet"),TEXT("Gloves"),TEXT("Main hand"),TEXT("Off hand"),TEXT("Accessory 1"),TEXT("Accessory 2"),TEXT("Accessory 3"),TEXT("Accessory 4")};if(I.RulesVersion==8){Text=TEXT("Seven body + four accessories · same 24 carried instances\n");}
    for(int32 N=0;N<I.Bindings.Num();++N)
    {const auto* Item=I.Bindings.IsValidIndex(N)?I.Find(I.Bindings[N]):nullptr;const auto* D=Item&&C?C->Find(Item->Kind):nullptr;Text+=FString(I.RulesVersion==8?V08Names[N]:Names[N])+TEXT(": ")+(D?D->Name:TEXT("Empty"));if(D){for(int32 A=0;A<6;++A){const int32 Grant=S.Grants.IsValidIndex(N*6+A)?S.Grants[N*6+A]:0;if(D->Stats.Primary[A]>Grant&&!((I.RulesVersion==8?N==6&&I.Bindings[5]==I.Bindings[6]:N==1&&I.Bindings[0]==I.Bindings[1]))){static const TCHAR* Attr[]={TEXT("STR"),TEXT("DEX"),TEXT("CON"),TEXT("INT"),TEXT("WIS"),TEXT("CHA")};Text+=FString::Printf(TEXT(" · %s +%d inactive"),Attr[A],D->Stats.Primary[A]-Grant);}}}Text+=TEXT("\n");}return Text;
}
int32 LDGear::Magnitude(const FLDCharacterBuild& B,ELDSkill Skill,const FString& Key,const TArray<int32>& Values)
{
    const auto* C=ULDEquipmentCatalog::Load(B.RulesVersion);const auto* G=C?C->Gate(Skill):nullptr;int32 Rank=FMath::Clamp(LDDevelopment::Rank(B,Skill),1,3);
    if(G&&G->School==TEXT("control")&&G->Potency.Contains(Key)){Rank=FMath::Min(3,Rank+int32(B.Gear.ControlPotency));}return Values.IsValidIndex(Rank-1)?Values[Rank-1]:0;
}
