#include "Inventory/LDSupplyRequest.h"
bool FLDSupplyRequest::NetSerialize(FArchive& Ar,UPackageMap* Map,bool& Success)
{
    uint8 Version=1;Ar<<Version;Success=Version==1;if(!Success){Ar.SetError();return false;}
    uint8 Bytes[32]={};if(Ar.IsSaving()){if(Digest.Len()!=64){Ar.SetError();Success=false;return false;}for(TCHAR C:Digest){if(!FChar::IsHexDigit(C)){Ar.SetError();Success=false;return false;}}HexToBytes(Digest,Bytes);}Ar.Serialize(Bytes,32);if(Ar.IsLoading()){Digest=BytesToHex(Bytes,32).ToLower();}
    uint8 K=uint8(Kind),S=uint8(Skill);Ar<<Match<<Life<<Token<<InventoryRevision<<Site<<K<<S;if(Ar.IsLoading()){Kind=ELDItemKind(K);Skill=ELDSkill(S);}Success=!Ar.IsError()&&Match&&Life&&Token&&InventoryRevision&&Site>=2&&Site<=4&&K<=uint8(ELDItemKind::Heartbreaker)&&S<38;if(!Success){Ar.SetError();}return Success;
}
