#include "UI/LDHUDModel.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDDemoContentIds.h"

FString LDHUD::Inspector(const FLDOwnerState& Own)
{
    return FString::Printf(TEXT("YOUR CHARACTER\nHP %.0f/%.0f · MP %.0f/%.0f"),Own.Hp,Own.MaxHp,Own.Mp,Own.MaxMp);
}
namespace
{
// Rounds a live remainder up to the next tenth, so it cannot read 0.0s while the deadline is still ahead.
FString HudTenthsUp(int64 RemainingMs)
{
    const int64 Tenths=(RemainingMs+99)/100;
    return FString::Printf(TEXT("%lld.%llds"),Tenths/10,Tenths%10);
}
}
FString LDHUD::LifeStatus(const FLDOwnerState& Own,bool bClock,int64 NowMs)
{
    switch(Own.LifePhase)
    {
    case ELDLifePhase::Alive:
        if(Own.ProtectedUntilMs<=0){return TEXT("Alive");}
        if(!bClock){return TEXT("Alive · protection time unavailable");}
        return Own.ProtectedUntilMs>NowMs?TEXT("Protected ")+HudTenthsUp(Own.ProtectedUntilMs-NowMs):FString(TEXT("Alive"));
    case ELDLifePhase::Dead:
        if(Own.RespawnAtMs<=0){return TEXT("DEAD");}
        if(!bClock){return TEXT("RETURN · time unavailable");}
        return TEXT("RETURN ")+(Own.RespawnAtMs>NowMs?HudTenthsUp(Own.RespawnAtMs-NowMs):FString(TEXT("0.0s")));
    case ELDLifePhase::Unspawned: return TEXT("Not spawned");
    case ELDLifePhase::Left: return TEXT("Left the match");
    default: return TEXT("Unknown life state");
    }
}
namespace
{
FString HudWholeP2(int64 Value)
{
    return FString::Printf(TEXT("%.0f"),double(Value)/100.);
}
const TCHAR* HudStatusName(uint8 Id)
{
    switch(static_cast<ELDDemoStatusId>(Id))
    {
    case ELDDemoStatusId::Root: return TEXT("Rooted");
    case ELDDemoStatusId::Shield: return TEXT("Shielded");
    case ELDDemoStatusId::CampReturning: return TEXT("Returning to camp");
    case ELDDemoStatusId::RespawnProtection: return TEXT("Respawn protection");
    default: return nullptr;
    }
}
}
FString LDHUD::ObservedUnit(const FLDObservedUnitView& Unit,const FString& ContenderName,const FLDDemoContentDefinitions* Content,double SampledAtServerSec)
{
    const bool bContender=Unit.Kind==ELDObservedUnitKind::Contender;
    FString Name=bContender?ContenderName:FString();
    if(!bContender&&Content)
    {if(const auto* Monster=Content->Monsters.FindByPredicate([&Unit](const FLDDemoMonsterDefinition& M){return M.NativeId==Unit.CreatureId;})){Name=Monster->Id;}}
    if(Name.IsEmpty()){Name=bContender?FString(TEXT("Unlisted contender")):FString::Printf(TEXT("Creature #%d"),Unit.CreatureId);}
    FString Text=bContender?FString::Printf(TEXT("%s · Level %d"),*Name,Unit.Level):Name;
    Text+=FString::Printf(TEXT("\nHP %s/%s"),*HudWholeP2(Unit.Health.CurrentP2),*HudWholeP2(Unit.Health.MaxP2));
    if(Unit.AuthorizedFieldMask&LDObservation::Mana){Text+=FString::Printf(TEXT(" · MP %s/%s"),*HudWholeP2(Unit.Mana.CurrentP2),*HudWholeP2(Unit.Mana.MaxP2));}
    FString Statuses;
    for(const FLDObservedStatus& Status:Unit.Statuses)
    {
        const TCHAR* Label=HudStatusName(Status.StatusId);
        Statuses+=(Statuses.IsEmpty()?TEXT(""):TEXT(" · "))+FString(Label?Label:TEXT("Unknown status"));
        if(Status.Stacks>1){Statuses+=FString::Printf(TEXT(" x%d"),Status.Stacks);}
        if(Status.ExpiresAtServerMs>0){Statuses+=FString::Printf(TEXT(" %.1fs"),FMath::Max(0.,double(Status.ExpiresAtServerMs)/1000.-SampledAtServerSec));}
    }
    Text+=TEXT("\n")+(Statuses.IsEmpty()?FString(TEXT("No public status")):Statuses);
    if(bContender)
    {
        auto Slot=[Content](uint8 Id)->FString
        {
            if(!Id){return TEXT("Empty slot");}
            if(Content){if(const auto* Item=Content->Equipment.FindByPredicate([Id](const FLDDemoEquipmentDefinition& E){return E.NativeId==Id;})){return Item->Id;}}
            return FString::Printf(TEXT("Equipment #%d"),Id);
        };
        Text+=FString::Printf(TEXT("\nEquipment %s · %s"),*Slot(Unit.Equipment.Slots[0]),*Slot(Unit.Equipment.Slots[1]));
    }
    return Text;
}
FVector2D LDHUD::MapPoint(const FVector& P,const FBox2D& G)
{const FVector2D Span=G.GetSize();return FVector2D((P.X-G.Min.X)/Span.X,1-(P.Y-G.Min.Y)/Span.Y);}
FVector LDHUD::MapWorld(const FVector2D& P,const FBox2D& G)
{const FVector2D Span=G.GetSize();return FVector(G.Min.X+FMath::Clamp(P.X,0.,.999999)*Span.X,G.Min.Y+(1-FMath::Clamp(P.Y,.000001,1.))*Span.Y,0);}
