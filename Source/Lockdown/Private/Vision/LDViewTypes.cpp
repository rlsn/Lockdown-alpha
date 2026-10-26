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
    uint8 Small=uint8(FMath::Min(255,Entities.Num()));Ar<<Small;uint16 Count=Small;if(Small==255){Count=uint16(Entities.Num());Ar<<Count;}
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
        bSuccess &= bPosition && Kind <= uint8(ELDObservedKind::WorldSite) && (Entity.Kind==ELDObservedKind::TacticalField?(Entity.Presentation<23&&(Entity.Presentation>=16?Entity.ActionCue<128:Entity.Presentation==9?Entity.ActionCue>=15&&Entity.ActionCue<=17:Entity.ActionCue==0)):Entity.Kind==ELDObservedKind::WorldCreature?Entity.Presentation<2:Entity.Kind==ELDObservedKind::WorldStructure||Entity.Kind==ELDObservedKind::WorldSite?Entity.Presentation<3:Entity.Kind==ELDObservedKind::Cargo?Entity.Presentation<2:Entity.Kind==ELDObservedKind::Institution?(Entity.Presentation<5&&Entity.ActionCue<=5):Entity.Kind==ELDObservedKind::Production?(Entity.Presentation<9&&Entity.ActionCue<=3):Entity.Presentation<4)
            && LDVision::InWorld(Entity.Position,Grid) && FMath::IsFinite(Entity.ExposedUntil);
    }
    bSuccess &= !Ar.IsError() && FMath::IsFinite(AuthoritySeconds) && BodySources <= 1 && WardSources <= 8 && PublicSources <= 4;
    if (!bSuccess) { Ar.SetError(); }
    return bSuccess;
}
