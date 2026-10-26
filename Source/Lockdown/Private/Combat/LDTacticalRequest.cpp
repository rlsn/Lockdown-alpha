#include "Combat/LDTacticalRequest.h"
bool FLDTacticalRequest::NetSerialize(FArchive& Ar,UPackageMap* Map,bool& Success)
{
    Ar<<Schema<<WireVersion<<TargetMode;Success=((Schema==7&&(WireVersion==2||WireVersion==3))||(Schema==8&&WireVersion==4))&&TargetMode<10;if(!Success){Ar.SetError();return false;}
    uint8 Digest[32]={};
    if(Ar.IsSaving())
    {
        if(CatalogDigest.Len()!=64){Success=false;Ar.SetError();return false;}
        for(TCHAR C:CatalogDigest){if(!FChar::IsHexDigit(C)){Success=false;Ar.SetError();return false;}}
        HexToBytes(CatalogDigest,Digest);
    }
    Ar.Serialize(Digest,32);if(Ar.IsLoading()){CatalogDigest=BytesToHex(Digest,32).ToLower();}
    Ar<<Match<<Life<<Token<<BuildRevision<<Slot<<Observed;
    bool ValidSecond=true;
    if(WireVersion>=3){Ar<<ActionKind<<SecondObserved<<Item<<InventoryRevision<<ItemRevision;SecondPoint.NetSerialize(Ar,Map,ValidSecond);}
    else if(Ar.IsLoading()){ActionKind=0;SecondObserved.Invalidate();SecondPoint=FVector::ZeroVector;Item.Invalidate();InventoryRevision=ItemRevision=0;}
    if(Schema==8){Ar<<SourceKind;uint8 Name[64]={};if(Ar.IsSaving()){FTCHARToUTF8 Bytes(*SourceDefinition.ToString());if(Bytes.Length()>63){Success=false;Ar.SetError();return false;}FMemory::Memcpy(Name,Bytes.Get(),Bytes.Length());}Ar.Serialize(Name,64);if(Ar.IsLoading()){Name[63]=0;SourceDefinition=FName(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Name)));}if(SourceKind>4){Success=false;Ar.SetError();return false;}}
    bool ValidDirection=false;Direction.NetSerialize(Ar,Map,ValidDirection);
    bool ValidPoint=false;Point.NetSerialize(Ar,Map,ValidPoint);
    Success=ActionKind<5&&ValidSecond&&!SecondPoint.ContainsNaN()&&SecondPoint.GetAbsMax()<=20000&&ValidPoint&&!Point.ContainsNaN()&&FMath::Abs(Point.X)<=20000&&FMath::Abs(Point.Y)<=20000&&FMath::Abs(Point.Z)<=20000&&ValidDirection&&!Ar.IsError()&&Slot<(Schema==8?6:5)&&Match&&Life&&Token&&BuildRevision;
    if(!Success){Ar.SetError();}return Success;
}
