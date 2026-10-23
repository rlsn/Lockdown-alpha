#include "UI/LDHUDModel.h"
#include "Development/LDWorldChecks.h"

FString LDHUD::Cue(uint8 Value)
{
    const TCHAR* States[]={TEXT("No visible action"),TEXT("Attack windup"),TEXT("Ambush windup"),TEXT("Recovering"),TEXT("Step"),TEXT("Fallen"),TEXT("Spark cast"),TEXT("Holding a channel"),TEXT("Preparing / reading")};
    FString Result=States[FMath::Min<int32>(Value&15,8)];
    if(Value&64){Result+=TEXT(" · Protected");}if(Value&128){Result+=TEXT(" · Exposed");}return Result;
}
FString LDHUD::Inspector(const FLDOwnerState& Own,const FLDObservedEntity* E,double Now)
{
    if(E)
    {
        const TCHAR* Label=E->Kind==ELDObservedKind::Body?TEXT("OBSERVED STRANGER"):E->Kind==ELDObservedKind::Sentinel?TEXT("HOSTILE SENTINEL"):E->Kind==ELDObservedKind::Ward?(E->Presentation==1?TEXT("YOUR WARD SOURCE"):TEXT("OBSERVED WARD")):E->Kind==ELDObservedKind::Seal?TEXT("DROPPED CONCORD SEAL"):E->Kind==ELDObservedKind::Book?TEXT("UNREAD BOOK"):E->Kind==ELDObservedKind::Charter?LDCharter::Name(E->Presentation):TEXT("WATCH CONTROL");
        FString Detail=(E->Kind==ELDObservedKind::Body||E->Kind==ELDObservedKind::Sentinel)?TEXT("HP / level / class: Unknown\nAttributes / skills / ranks: Unknown\nGold / inventory: Unknown\n")+Cue(E->ActionCue):TEXT("Visible public object\n");
        if(E->Kind==ELDObservedKind::Charter){Detail+=FString::Printf(TEXT("%s · Public threshold 12"),LDDevelopment::SkillName(LDCharter::Skill(E->Presentation)));}
        if(E->Kind==ELDObservedKind::WatchControl){Detail+=TEXT("Analyze · Public threshold 14");}
        if(E->Kind==ELDObservedKind::WardCharge){return TEXT("DROPPED WARD CHARGE\nFinite physical item · Pick up within 80 cm");}
        if(E->Kind==ELDObservedKind::Cargo){return TEXT("DROPPED COMMERCIAL CARGO\nPhysical item · Pick up within 80 cm · Counts toward slots and weight");}
        if(E->ExposedUntil>Now){Detail+=TEXT(" · Exposed");}
        return FString(Label)+TEXT("\n")+Detail;
    }
    const auto& B=Own.Build;
    return FString::Printf(TEXT("YOUR CHARACTER · %s\nSTR %d  DEX %d  CON %d\nINT %d  WIS %d  CHA %d\nWalk %.0f · Sprint %.0f · Basic %.2f\n%s"),LDDevelopment::ClassName(B.ClassId),B.Attributes[0],B.Attributes[1],B.Attributes[2],B.Attributes[3],B.Attributes[4],B.Attributes[5],180*LDDevelopment::SpeedFactor(B),300*LDDevelopment::SpeedFactor(B),LDDevelopment::BasicDamage(B),B.Subclass?LDDevelopment::SubclassName(B.Subclass):TEXT("No subclass"));
}
double LDHUD::Range(ELDSkill Skill)
{return Skill==ELDSkill::Spark?500:Skill==ELDSkill::Ambush?112:Skill==ELDSkill::Pickpocket?100:Skill==ELDSkill::Patch?0:Skill==ELDSkill::Step?360:120;}
FString LDHUD::Tooltip(const FLDCharacterBuild& B,ELDSkill Skill)
{
    if(Skill==ELDSkill::None){return TEXT("Empty equipped skill slot. Prepare a learned skill at a station.");}
    const double Windup=Skill==ELDSkill::Spark?.4:Skill==ELDSkill::Ambush?.5:Skill==ELDSkill::Step?.2:LDDevelopment::ChannelSeconds(Skill);
    const TCHAR* Outcome=Skill==ELDSkill::Step?TEXT("Move in a confirmed direction; collision limits distance"):Skill==ELDSkill::Patch?TEXT("Hold to heal yourself; release cancels"):uint8(Skill)>=5?TEXT("Confirm an eligible observed task and hold; release cancels"):TEXT("Confirm one observed body; range and sight rechecked at impact");
    return FString::Printf(TEXT("%s · Rank %d\n%s\nOwn value %.2f · Range %.0f cm\nDuration %.1fs · Cooldown %.0fs\nAccepted cooldown stays spent if interrupted."),LDDevelopment::SkillName(Skill),LDDevelopment::Rank(B,Skill),Outcome,LDDevelopment::SkillValue(B,Skill),Skill==ELDSkill::Step?LDDevelopment::SkillValue(B,Skill):Range(Skill),Windup,LDDevelopment::Cooldown(Skill));
}
FVector2D LDHUD::MapPoint(const FVector& P,const FLDVisionGrid& G)
{return FVector2D((P.X-G.Min.X)/(60*G.CellSize),1-(P.Y-G.Min.Y)/(40*G.CellSize));}
FVector LDHUD::MapWorld(const FVector2D& P,const FLDVisionGrid& G)
{return FVector(G.Min.X+FMath::Clamp(P.X,0.,.999999)*60*G.CellSize,G.Min.Y+(1-FMath::Clamp(P.Y,.000001,1.))*40*G.CellSize,0);}
