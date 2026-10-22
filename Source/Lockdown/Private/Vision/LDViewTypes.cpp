#include "Vision/LDViewTypes.h"

bool FLDWorldView::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bSuccess)
{
    bSuccess = true;
    Ar << Generation << Revision << AuthoritySeconds << BodySources << WardSources << PublicSources << Grid.Min.X << Grid.Min.Y << Grid.CellSize;
    if(!Grid.Valid()){bSuccess=false;Ar.SetError();return false;}
    if (Ar.IsLoading()) { Current.SetNumZeroed(LDVision::MaskBytes); Explored.SetNumZeroed(LDVision::MaskBytes); }
    if (Current.Num() != LDVision::MaskBytes || Explored.Num() != LDVision::MaskBytes) { bSuccess=false; return false; }
    Ar.Serialize(Current.GetData(),LDVision::MaskBytes);
    Ar.Serialize(Explored.GetData(),LDVision::MaskBytes);
    uint8 Count = uint8(Entities.Num());
    Ar << Count;
    if (Count > LDVision::MaxEntities || (!Ar.IsLoading() && Entities.Num() > LDVision::MaxEntities)) { bSuccess=false; Ar.SetError(); return false; }
    if (Ar.IsLoading()) { Entities.SetNum(Count); }
    for (FLDObservedEntity& Entity : Entities)
    {
        Ar << Entity.Id;
        bool bPosition = true;
        Entity.Position.NetSerialize(Ar, Map, bPosition);
        uint8 Kind = uint8(Entity.Kind);
        Ar << Entity.Facing << Entity.Speed << Entity.Presentation << Kind << Entity.ActionCue << Entity.ExposedUntil;
        if (Ar.IsLoading()) { Entity.Kind = ELDObservedKind(Kind); }
        bSuccess &= bPosition && Kind <= uint8(ELDObservedKind::WardCharge) && Entity.Presentation < 4
            && LDVision::InWorld(Entity.Position,Grid) && FMath::IsFinite(Entity.ExposedUntil);
    }
    bSuccess &= !Ar.IsError() && FMath::IsFinite(AuthoritySeconds) && BodySources <= 1 && WardSources <= 2 && PublicSources <= 4;
    if (!bSuccess) { Ar.SetError(); }
    return bSuccess;
}
