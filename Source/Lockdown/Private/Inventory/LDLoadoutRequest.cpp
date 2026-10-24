#include "Inventory/LDLoadoutRequest.h"
#include "Inventory/LDInventoryTypes.h"
bool FLDLoadoutRequest::NetSerialize(FArchive& Ar,UPackageMap* Map,bool& Success)
{
    Ar<<Version;Success=Version==1;if(!Success){Ar.SetError();return false;}
    uint8 Digest[32]={};if(Ar.IsSaving()){if(EquipmentDigest.Len()!=64){Success=false;Ar.SetError();return false;}for(TCHAR C:EquipmentDigest){if(!FChar::IsHexDigit(C)){Success=false;Ar.SetError();return false;}}HexToBytes(EquipmentDigest,Digest);}
    Ar.Serialize(Digest,32);if(Ar.IsLoading()){EquipmentDigest=BytesToHex(Digest,32).ToLower();}
    Ar<<Match<<Life<<Token<<InventoryRevision<<BuildRevision<<ItemRevision<<Command<<Binding<<Item;
    Success=!Ar.IsError()&&Match&&Life&&Token&&InventoryRevision&&BuildRevision&&ItemRevision&&Item.IsValid()&&Binding<6&&(Command==LDInventory::Bind||Command==LDInventory::Unbind||Command==LDInventory::Equip||Command==LDInventory::Unequip);
    if(!Success){Ar.SetError();}return Success;
}
