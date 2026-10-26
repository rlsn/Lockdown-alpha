#include "UI/LDHUDModel.h"
#include "Combat/LDTacticalCatalog.h"
#include "Combat/LDTacticalRules.h"
#include "Development/LDV08Loadout.h"
#include "Inventory/LDEquipmentCatalog.h"

LDHUD::FActionView LDHUD::Action(const FLDOwnerState& O,uint8 Slot,bool Accessory,double Now,bool Finished,bool PanelOpen)
{
    FActionView V;V.Name=TEXT("Empty");V.Source=Accessory?TEXT("ACC"):TEXT("SKILL");V.State=TEXT("Empty");
    const auto& B=O.Build;
    if(B.RulesVersion!=8||Slot>=(Accessory?4:6)||B.Actions.Num()!=6||O.Inventory.Bindings.Num()!=11){return V;}
    const auto& A=B.Actions[Accessory?0:Slot];const bool Gear=Accessory||A.Source==ELDActionSource::Gear;
    const auto* I=Gear?O.Inventory.Find(Accessory?O.Inventory.Bindings[7+Slot]:A.Instance):nullptr;
    const auto* C=ULDEquipmentCatalog::Load(8);const auto* D=I&&C?C->Find(I->Kind):nullptr;
    double Mana=0,End=0;
    if(Gear)
    {
        V.Source=Accessory?TEXT("ACC"):TEXT("ITEM");
        if(!D){V.State=I||(!Accessory&&A.Instance.IsValid())?TEXT("Missing source"):TEXT("Empty");return V;}
        V.Name=D->Name;V.Tooltip=D->ModifierHelp;
        if(D->Operation.IsEmpty()){V.State=TEXT("Passive");V.Tooltip+=TEXT("\nEquipped passive; no activation button.");return V;}
        if(!LDV08::ItemSource(O.Inventory,B,I->Id,Slot,Accessory)){V.State=TEXT("Source unbound");return V;}
        Mana=D->Param(TEXT("mana_cost"));
        End=FMath::Max(I->ReadyAt,O.ItemFamilies.FindRef(D->CooldownKey()))/1000.;
        if(D->Family==TEXT("major_protection")){End=FMath::Max(End,O.MajorProtectionReadyAt/1000.);}
        V.Tooltip+=FString::Printf(TEXT("\nPhysical %s · %.0f MP · %.1fs reuse\nMute blocks activation; Silence does not.\nUnbinding removes dependent actions; consuming empties this accessory without refill."),Accessory?TEXT("accessory"):TEXT("body action"),Mana,D->Cooldown/1000.);
        const double Range=D->Param(TEXT("range_cm")),Radius=D->Param(TEXT("radius_cm")),Duration=D->Param(TEXT("lifetime_ms"),D->Param(TEXT("duration_ms")));
        if(Range>0){V.Tooltip+=FString::Printf(TEXT("\nRange %.0f cm"),Range);}if(Radius>0){V.Tooltip+=FString::Printf(TEXT(" · Radius %.0f cm"),Radius);}if(Duration>0){V.Tooltip+=FString::Printf(TEXT(" · Duration %.1fs"),Duration/1000.);}
    }
    else
    {
        if(A.Skill==ELDSkill::None){return V;}V.Name=LDDevelopment::SkillName(A.Skill);V.Tooltip=Tooltip(B,A.Skill);
        if(!LDDevelopment::Equipped(B,A.Skill)||!LDGear::NaturalGate(B,A.Skill)){V.State=TEXT("Needs preparation");return V;}
        const auto* T=ULDTacticalCatalog::Load(8);const auto* Skill=T?T->Find(A.Skill):nullptr;
        Mana=Skill?Skill->Mana:0;End=B.Cooldowns.IsValidIndex(uint8(A.Skill))?B.Cooldowns[uint8(A.Skill)]:0;
        if(A.Skill==ELDSkill::SovereignWard){End=FMath::Max(End,O.MajorProtectionReadyAt/1000.);}
    }
    if(Finished){V.State=TEXT("Match ended");}
    else if(!O.bActive){V.State=TEXT("Returning");}
    else if(Now<O.ProtectedUntil){V.State=TEXT("Protected");}
    else if(PanelOpen){V.State=TEXT("Panel open");}
    else if(I&&I->bReserved){V.State=TEXT("Reserved in trade");}
    else if(O.StatusMask&(1<<uint8(LDTactical::EStatus::Stun))){V.State=TEXT("Stunned");}
    else if(O.StatusMask&(1<<uint8(LDTactical::EStatus::Hex))){V.State=TEXT("Hexed");}
    else if(O.StatusMask&(1<<uint8(Gear?LDTactical::EStatus::Mute:LDTactical::EStatus::Silence))){V.State=Gear?TEXT("Muted"):TEXT("Silenced");}
    else if(O.DevelopmentKind||O.ChannelKind||(O.ActionCue&15)){V.State=TEXT("Busy / Stop first");}
    else if(End>Now){V.State=FString::Printf(TEXT("Reuse %.1fs"),End-Now);}
    else if(O.Mp+1.e-6<Mana){V.State=FString::Printf(TEXT("Need %.0f MP"),Mana);}
    else{V.State=TEXT("Ready");V.CanActivate=true;}
    V.Tooltip+=TEXT("\n")+V.State+TEXT(". Target, range and release conditions must still be valid.");
    return V;
}
