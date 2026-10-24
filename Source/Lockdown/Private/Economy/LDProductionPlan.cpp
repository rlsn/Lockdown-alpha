#include "Economy/LDProductionDirector.h"
bool FLDProductionPlan::NetSerialize(FArchive& Ar,UPackageMap* Map,bool& Success)
{
    uint8 Version=1;Ar<<Version;Success=Version==1;if(!Success){Ar.SetError();return false;}
    Ar<<Site<<Recipe<<RecipeRevision<<InventoryRevision<<BuildRevision<<SkillSlot<<Fee<<Coupon<<Mode<<Target;
    uint8 HasDigest=Ar.IsSaving()&&!CatalogDigest.IsEmpty();Ar.SerializeBits(&HasDigest,1);
    if(HasDigest){uint8 Bytes[32]={};if(Ar.IsSaving()){if(CatalogDigest.Len()!=64){Ar.SetError();Success=false;return false;}for(TCHAR C:CatalogDigest){if(!FChar::IsHexDigit(C)){Ar.SetError();Success=false;return false;}}HexToBytes(CatalogDigest,Bytes);}Ar.Serialize(Bytes,32);if(Ar.IsLoading()){CatalogDigest=BytesToHex(Bytes,32).ToLower();}}
    else if(Ar.IsLoading()){CatalogDigest.Reset();}
    uint8 Count=Ar.IsSaving()?Inputs.Num():0;if(Ar.IsSaving()&&Inputs.Num()>4){Ar.SetError();Success=false;return false;}Ar.SerializeBits(&Count,3);if(Count>4){Ar.SetError();Success=false;return false;}if(Ar.IsLoading()){Inputs.SetNum(Count);}
    for(auto& I:Inputs){uint8 Kind=uint8(I.Kind),Skill=uint8(I.Skill);Ar<<I.Id<<Kind<<Skill<<I.Slot<<I.Revision<<I.bReserved<<I.BindingMask<<I.ReadyAt<<I.Quality;if(Ar.IsLoading()){I.Kind=ELDItemKind(Kind);I.Skill=ELDSkill(Skill);}}
    Success=!Ar.IsError()&&Site<9&&Mode<2;if(!Success){Ar.SetError();}return Success;
}
