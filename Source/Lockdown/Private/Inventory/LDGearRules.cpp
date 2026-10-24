#include "Inventory/LDEquipmentCatalog.h"
#include "Region/LDRegionDefinition.h"
bool LDGear::NaturalGate(const FLDCharacterBuild& B,ELDSkill S)
{
    if(uint8(S)<10){return true;}const auto* C=ULDEquipmentCatalog::Load();const auto* G=C?C->Gate(S):nullptr;
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
    FLDGearStats S;const auto* C=ULDEquipmentCatalog::Load();if(I.RulesVersion!=7||!C){return S;}
    TArray<int32> Requests;Requests.Init(0,36);TSet<FGuid> Seen;int32 Satchel=0;
    for(int32 Slot=0;Slot<6&&Slot<I.Bindings.Num();++Slot)
    {
        const auto Id=I.Bindings[Slot];if(!Id.IsValid()||Seen.Contains(Id)){continue;}Seen.Add(Id);const auto* Item=I.Find(Id);const auto* D=Item?C->Find(Item->Kind):nullptr;if(!D){continue;}
        const auto& P=D->Stats;for(int32 A=0;A<6;++A){Requests[6*Slot+A]=P.Primary[A];}
        if(Slot==0){S.WeaponBase=P.WeaponBase;S.Reach=P.Reach;S.Interval=P.Interval;}
        S.Armor+=P.Armor;S.MagicResist+=P.MagicResist;S.Movement+=P.Movement;S.RoadMovement+=P.RoadMovement;S.MPRegen+=P.MPRegen;
        if(Item->Kind==ELDItemKind::WeightedSatchel){Satchel=FMath::Max(Satchel,P.Capacity);}else{S.Capacity+=P.Capacity;}
        S.SpellPercent+=P.SpellPercent;S.CastRange=FMath::Max(S.CastRange,P.CastRange);S.ControlPotency=FMath::Min<int32>(1,S.ControlPotency+P.ControlPotency);S.Detector=FMath::Max(S.Detector,P.Detector);
    }
    S.Primary=Allocate(Requests,S.Grants);S.Capacity+=Satchel;return S;
}
bool LDGear::Propose(const FLDInventory& I,const FLDCharacterBuild& B,uint8 Command,FGuid Id,uint8 Slot,FLDInventory& Out,FString& Why)
{
    auto Reject=[&](const TCHAR* S){Why=S;return false;};const auto* Item=I.Find(Id);const auto* C=ULDEquipmentCatalog::Load();
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
    const auto* C=ULDEquipmentCatalog::Load();const auto S=Derive(I);FString Text=TEXT("Six bindings · same carried instances\n");
    for(int32 N=0;N<6;++N)
    {const auto* Item=I.Bindings.IsValidIndex(N)?I.Find(I.Bindings[N]):nullptr;const auto* D=Item&&C?C->Find(Item->Kind):nullptr;Text+=FString(Names[N])+TEXT(": ")+(D?D->Name:TEXT("Empty"));if(D){for(int32 A=0;A<6;++A){const int32 Grant=S.Grants.IsValidIndex(N*6+A)?S.Grants[N*6+A]:0;if(D->Stats.Primary[A]>Grant&&!(N==1&&I.Bindings[0]==I.Bindings[1])){static const TCHAR* Attr[]={TEXT("STR"),TEXT("DEX"),TEXT("CON"),TEXT("INT"),TEXT("WIS"),TEXT("CHA")};Text+=FString::Printf(TEXT(" · %s +%d inactive"),Attr[A],D->Stats.Primary[A]-Grant);}}}Text+=TEXT("\n");}return Text;
}
int32 LDGear::Magnitude(const FLDCharacterBuild& B,ELDSkill Skill,const FString& Key,const TArray<int32>& Values)
{
    const auto* C=ULDEquipmentCatalog::Load();const auto* G=C?C->Gate(Skill):nullptr;int32 Rank=FMath::Clamp(LDDevelopment::Rank(B,Skill),1,3);
    if(G&&G->School==TEXT("control")&&G->Potency.Contains(Key)){Rank=FMath::Min(3,Rank+int32(B.Gear.ControlPotency));}return Values.IsValidIndex(Rank-1)?Values[Rank-1]:0;
}
