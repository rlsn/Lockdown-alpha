#include "UI/LDHUDModel.h"
#include "Combat/LDTacticalCatalog.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Development/LDWorldChecks.h"
#include "Economy/LDProductionDirector.h"
#include "Economy/LDInstitutionSite.h"

FString LDHUD::Cue(uint8 Value)
{
    const TCHAR* States[]={TEXT("No visible action"),TEXT("Attack windup"),TEXT("Ambush windup"),TEXT("Recovering"),TEXT("Step"),TEXT("Fallen"),TEXT("Spark cast"),TEXT("Holding a channel"),TEXT("Preparing / reading"),TEXT("Spell / item windup"),TEXT("Held item / traversal"),TEXT("Detected illusion")};
    FString Result=States[FMath::Min<int32>(Value&15,11)];
    if(Value&64){Result+=TEXT(" · Protected");}if(Value&128){Result+=TEXT(" · Exposed");}return Result;
}
FString LDHUD::Inspector(const FLDOwnerState& Own,const FLDObservedEntity* E,double Now)
{
    if(E)
    {
        if(E->Kind==ELDObservedKind::TacticalField){return E->Presentation==11?TEXT("LINKED GATE\nInspect then hold Context1s within80cm. Root/grounding, damage, Heart claim and objective cargo prevent travel. Arrival lock3s. No remote sight."):E->Presentation==14?TEXT("SCRAMBLE LINK\nCliffwalkers required. Inspect then hold Context1s within80cm. Root/grounding prevent traversal."):E->Presentation==12?TEXT("STALE TRAIL CLUE\nLast observed ground position; expires4s after creation. No fog tracking."):E->Presentation==13?TEXT("AUDIBLE FOOTSTEP\nAnonymous local ground cue; no identity, cargo or private stats."):TEXT("OBSERVED TACTICAL FIELD\nVisible anchor / area. Attackable emitters can end their source. No private owner or HP data.");}
        if(E->Kind==ELDObservedKind::Projectile){return TEXT("OBSERVED PROJECTILE\nSnapshot trajectory · No private caster or target data");}
        if(E->Kind==ELDObservedKind::Institution){return ALDInstitutionSite::Name(E->Presentation)+TEXT("\nProtected official noncontender\n")+ALDInstitutionSite::StatusName(E->ActionCue)+TEXT("\nFunded production, then real stock consumption at 180s intervals; eight opportunities. No private player data.");}
        if(E->Kind==ELDObservedKind::Production){return ALDProductionDirector::SiteName(E->Presentation)+(E->Presentation<2?FString::Printf(TEXT("\nPublic remaining charges %d/3\nHold context six seconds, or use equipped Survey score 12 in one second."),E->ActionCue):E->Presentation>=6?TEXT("\nOpen the Ruins panel. Quiet4s open; each item individually owned. Keys3s."):E->Presentation==5?TEXT("\nHeartForge: two Cores, catalyst and Fragment;20g /8s. Objective activation remains B76."):TEXT("\nSelect exact inputs in Bag, preview then close and arm a fresh context hold. Equipped Analyze score 14 plans one private coupon."));}
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
    if(B.RulesVersion==7){const auto A=LDDevelopment::Effective(B);return FString::Printf(TEXT("YOUR CHARACTER · %s\nNatural STR %d DEX %d CON %d INT %d WIS %d CHA %d\nEffective STR %d DEX %d CON %d INT %d WIS %d CHA %d\nHP %.0f/%.0f · MP %.0f/%.0f\nBase move %.0f · Armor %.1f · Basic %.2f"),LDDevelopment::PresetName(B),B.Attributes[0],B.Attributes[1],B.Attributes[2],B.Attributes[3],B.Attributes[4],B.Attributes[5],A[0],A[1],A[2],A[3],A[4],A[5],Own.Hp,Own.MaxHp,Own.Mp,Own.MaxMp,420.*(1+B.Gear.Movement/100.),2+.5*(A[2]-8)+B.Gear.Armor,LDDevelopment::BasicDamage(B));}
    return FString::Printf(TEXT("YOUR CHARACTER · %s\nSTR %d  DEX %d  CON %d\nINT %d  WIS %d  CHA %d\nWalk %.0f · Sprint %.0f · Basic %.2f\n%s"),LDDevelopment::PresetName(B),B.Attributes[0],B.Attributes[1],B.Attributes[2],B.Attributes[3],B.Attributes[4],B.Attributes[5],180*LDDevelopment::SpeedFactor(B),300*LDDevelopment::SpeedFactor(B),LDDevelopment::BasicDamage(B),B.Subclass?LDDevelopment::SubclassName(B.Subclass):TEXT("No subclass"));
}
double LDHUD::Range(ELDSkill Skill)
{return Skill==ELDSkill::ArcBolt?650:Skill==ELDSkill::AnchorBash?220:Skill==ELDSkill::PurgeTouch?250:Skill==ELDSkill::SuretyShell?0:Skill==ELDSkill::Spark?500:Skill==ELDSkill::Ambush?112:Skill==ELDSkill::Pickpocket?100:Skill==ELDSkill::Patch?0:Skill==ELDSkill::Step?360:120;}
FString LDHUD::Tooltip(const FLDCharacterBuild& B,ELDSkill Skill)
{
    if(Skill==ELDSkill::None){return TEXT("Empty equipped skill slot. Prepare a learned skill at a station.");}
    if(uint8(Skill)>=10)
    {
        const auto* Catalog=ULDTacticalCatalog::Load();const auto* D=Catalog?Catalog->Find(Skill):nullptr;
        if(!D){return TEXT("Not implemented in the current catalog");}
        const int32 Trained=FMath::Clamp(LDDevelopment::Rank(B,Skill),1,3)-1;const int32 Value=LDGear::Magnitude(B,Skill,D->RankKey,D->Magnitudes);
        const double Cooldown=(D->RankKey==TEXT("cooldown_ms")?D->Magnitudes[Trained]:D->Cooldown)/1000.;
        const TCHAR* Field=D->RankKey==TEXT("damage")?TEXT("Damage"):D->RankKey==TEXT("barrier")?TEXT("Barrier"):D->RankKey==TEXT("radius_cm")?TEXT("Radius cm"):D->RankKey==TEXT("anchor_hp")?TEXT("Anchor HP"):D->RankKey==TEXT("stun_ms")?TEXT("Stun ms"):D->RankKey==TEXT("duration_ms")?TEXT("Duration ms"):TEXT("Cooldown ms");
        return FString::Printf(TEXT("%s · Trained rank %d · %d MP\nWindup %dms · Range %dcm · Cooldown %.1fs\n%s: %d before eligible magnitude modifiers\nEquipment potency affects declared fields only.\nMP/CD commit at release; Stop or relevant control cancels before release."),LDDevelopment::SkillName(Skill),Trained+1,D->Mana,D->Windup,D->Skill==ELDSkill::GateWeave?650+B.Gear.CastRange:D->Range+(D->Range?B.Gear.CastRange:0),Cooldown,Field,Value);
    }
    const double Windup=Skill==ELDSkill::Spark?.4:Skill==ELDSkill::Ambush?.5:Skill==ELDSkill::Step?.2:LDDevelopment::ChannelSeconds(Skill);
    const TCHAR* Outcome=Skill==ELDSkill::Step?TEXT("Move in a confirmed direction; collision limits distance"):Skill==ELDSkill::Patch?TEXT("Hold to heal yourself; release cancels"):uint8(Skill)>=5?TEXT("Confirm an eligible observed task and hold; release cancels"):TEXT("Confirm one observed body; range and sight rechecked at impact");
    return FString::Printf(TEXT("%s · Rank %d\n%s\nOwn value %.2f · Range %.0f cm\nDuration %.1fs · Cooldown %.0fs\nAccepted cooldown stays spent if interrupted."),LDDevelopment::SkillName(Skill),LDDevelopment::Rank(B,Skill),Outcome,LDDevelopment::SkillValue(B,Skill),Skill==ELDSkill::Step?LDDevelopment::SkillValue(B,Skill):Range(Skill),Windup,LDDevelopment::Cooldown(Skill));
}
FVector2D LDHUD::MapPoint(const FVector& P,const FLDVisionGrid& G)
{return FVector2D((P.X-G.Min.X)/(60*G.CellSize),1-(P.Y-G.Min.Y)/(40*G.CellSize));}
FVector LDHUD::MapWorld(const FVector2D& P,const FLDVisionGrid& G)
{return FVector(G.Min.X+FMath::Clamp(P.X,0.,.999999)*60*G.CellSize,G.Min.Y+(1-FMath::Clamp(P.Y,.000001,1.))*40*G.CellSize,0);}

#include "Framework/LDGameMode.h"
FString LDHUD::PublicOpening(const ALDGameState& G,double Now)
{
    if(G.EndReason){return G.EndReason==2?TEXT("No claim · match ended"):TEXT("Heart claimed · match ended");}
    if(G.bClaimActive){return FString::Printf(TEXT("%s · %.1f/10s"),G.bContested?TEXT("CONTESTED"):TEXT("Public claim"),G.ClaimProgress);}
    if(G.CharterWindow.IsValid()&&Now>=G.CharterOpenAt&&Now<G.CharterCloseAt){return FString::Printf(TEXT("HEART OPEN TO ALL · %.1fs left"),G.CharterCloseAt-Now);}
    if(G.CharterWindow.IsValid()&&Now<G.CharterOpenAt){return FString::Printf(TEXT("PUBLIC WARNING · opens in %.1fs"),G.CharterOpenAt-Now);}
    return TEXT("HEART PROTECTED · Charter opens public contest");
}
FString LDHUD::EconomyStep(const FLDOwnerState& O,const ALDGameState& G,double Now)
{
    if(G.EndReason){return O.bWinner?TEXT("You claimed the Heart"):TEXT("Match ended");}
    if(!O.bActive){return TEXT("Waiting to return · progress and bank retained");}
    if(G.CharterWindow.IsValid()&&Now>=G.CharterOpenAt&&Now<G.CharterCloseAt){return Now+10<=G.CharterCloseAt?TEXT("Heart: hold context 10s; no Charter required"):TEXT("Opening ends before a new 10s claim can finish");}
    if(O.CharterActivation.IsValid()){return TEXT("Charter spent · anyone may win the public opening");}
    if(O.CharterPurchase.IsValid())
    {if(O.bCharterAwaitingRespawn){return TEXT("Return first; then 30s Charter recovery");}if(Now<O.CharterReadyAt){return FString::Printf(TEXT("Charter recovery · %.1fs left"),O.CharterReadyAt-Now);}if(G.CharterWindow.IsValid()&&Now<G.CharterCloseAt){return TEXT("Another opening is active; keep your registration");}if(Now+94>G.MatchDeadline){return TEXT("Too late to activate a full public opening");}return TEXT("City civic terminal: arm activation, then hold 4s");}
    if(!O.SupplyEndorsement.IsValid()){return TEXT("West Supply: deliver 2 Toolkits · 50g / 30XP");}
    if(!O.IndustrialEndorsement.IsValid()){return TEXT("City procurement: deliver 1 Core · 80g / 60XP");}
    if(!O.Witness.IsValid()){return TEXT("Earn Witness with an equipped Charter-service skill");}
    if(Now+94>G.MatchDeadline){return TEXT("Too late to purchase a full Charter opening");}
    return TEXT("City Treasury: review Charter · 100g wallet/bank split");
}
