#include "Combat/LDTacticalRequest.h"
bool FLDTacticalRequest::NetSerialize(FArchive& Ar,UPackageMap* Map,bool& Success)
{
    Ar<<Schema<<WireVersion<<TargetMode;Success=Schema==7&&WireVersion==2&&TargetMode<=4;if(!Success){Ar.SetError();return false;}
    uint8 Digest[32]={};
    if(Ar.IsSaving())
    {
        if(CatalogDigest.Len()!=64){Success=false;Ar.SetError();return false;}
        for(TCHAR C:CatalogDigest){if(!FChar::IsHexDigit(C)){Success=false;Ar.SetError();return false;}}
        HexToBytes(CatalogDigest,Digest);
    }
    Ar.Serialize(Digest,32);if(Ar.IsLoading()){CatalogDigest=BytesToHex(Digest,32).ToLower();}
    Ar<<Match<<Life<<Token<<BuildRevision<<Slot<<Observed;
    bool ValidDirection=false;Direction.NetSerialize(Ar,Map,ValidDirection);
    bool ValidPoint=false;Point.NetSerialize(Ar,Map,ValidPoint);
    Success=ValidPoint&&!Point.ContainsNaN()&&FMath::Abs(Point.X)<=20000&&FMath::Abs(Point.Y)<=20000&&FMath::Abs(Point.Z)<=20000&&ValidDirection&&!Ar.IsError()&&Slot<5&&Match&&Life&&Token&&BuildRevision;
    if(!Success){Ar.SetError();}return Success;
}
