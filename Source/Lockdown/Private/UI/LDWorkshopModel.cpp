#include "UI/LDWorkshopModel.h"
#include "Combat/LDCombatElements.h"
#include "Development/LDProgressionRules.h"
#include "UI/LDTerms.h"
#include "UI/LDUIIcons.h"

namespace
{
const FLDDemoAbilityDefinition* WorkshopAbility(const FLDDemoContentDefinitions* Content,FName Id)
{
    if(!Content||Id.IsNone()){return nullptr;}
    const FString Text=Id.ToString();
    return Content->Abilities.FindByPredicate([&Text](const FLDDemoAbilityDefinition& D){return D.Id==Text;});
}
ELDDemoElement WorkshopEffectElement(const FLDDemoEffectDefinition& Effect)
{
    switch(Effect.GetOp())
    {
    case ELDDemoEffectOp::Burn: case ELDDemoEffectOp::ZoneBurn: return ELDDemoElement::Fire;
    case ELDDemoEffectOp::Wet: case ELDDemoEffectOp::ZoneWet: return ELDDemoElement::Water;
    case ELDDemoEffectOp::Electrify: return ELDDemoElement::Electric;
    case ELDDemoEffectOp::Chill: return ELDDemoElement::Chill;
    case ELDDemoEffectOp::Rot: return ELDDemoElement::Rot;
    case ELDDemoEffectOp::BasicElement: case ELDDemoEffectOp::SkillElement: case ELDDemoEffectOp::DashContactElement:
        return Effect.Element;
    default: return ELDDemoElement::None;
    }
}
void WorkshopAddElement(TArray<ELDDemoElement>& Out,ELDDemoElement Element)
{
    if(Element!=ELDDemoElement::None){Out.AddUnique(Element);}
}
void WorkshopSortElements(TArray<ELDDemoElement>& Elements)
{
    Elements.Sort([](ELDDemoElement A,ELDDemoElement B){return uint8(A)<uint8(B);});
}
FString WorkshopWhole(int64 P2)
{
    return P2%100==0?FString::Printf(TEXT("%lld"),P2/100):FString::Printf(TEXT("%.1f"),double(P2)/100.);
}
FString WorkshopSeconds(int64 Ms)
{
    return Ms%1000==0?FString::Printf(TEXT("%llds"),Ms/1000):FString::Printf(TEXT("%.1fs"),double(Ms)/1000.);
}
}

LDWorkshop::FCardFacts LDWorkshop::CardFacts(FName Card,const FLDDemoContentDefinitions* Content)
{
    FCardFacts Facts;
    if(const FLDDemoCardDefinition* Definition=LDHUD::CardDefinitionOf(Content,Card))
    {
        Facts.bKnown=true;Facts.Form=Definition->Form;Facts.Type=Definition->Type;
    }
    return Facts;
}
LDWorkshop::FColumnHead LDWorkshop::ColumnHead(LDHUD::EStorageColumn Column,FName Active,ELDSkillBranch Branch,const FString& HolderName,
    const FLDDemoContentDefinitions* Content)
{
    FColumnHead Head;
    if(Column==LDHUD::EStorageColumn::Body)
    {
        Head.Name=LDTerms::ColumnBody;Head.Glyph=HolderName.IsEmpty()?FString(TEXT("?")):HolderName.Left(1);
        return Head;
    }
    if(Column==LDHUD::EStorageColumn::Dash)
    {
        Head.Name=LDTerms::Dash;Head.IconId=LDUIIcons::Dash;Head.Glyph=LDHUD::IconGlyph(Head.Name);
        if(Content)
        {
            const FLDDemoAbilityValues& Values=Content->Character.Dash.Values;
            if(Values.MomentumCostP2>0){Head.Momentum=WorkshopWhole(Values.MomentumCostP2);}
            if(Values.CooldownMs>0){Head.Cooldown=WorkshopSeconds(Values.CooldownMs);}
        }
        return Head;
    }
    if(Active.IsNone()){Head.bEmpty=true;Head.Name=LDTerms::ColumnEmptySlot;return Head;}
    Head.Name=LDHUD::ContentName(Content,Active.ToString());
    Head.IconId=LDHUD::ContentIconId(Content,Active.ToString());
    Head.Glyph=LDHUD::IconGlyph(Head.Name);
    if(const FLDDemoAbilityDefinition* Ability=WorkshopAbility(Content,Active))
    {
        const FLDDemoAbilityBranch* Variant=Ability->FindBranch(Branch);
        const FLDDemoAbilityValues& Values=Variant?Variant->Values:Ability->Values;
        Head.Momentum=WorkshopWhole(Values.MomentumCostP2);
        Head.Cooldown=WorkshopSeconds(Values.CooldownMs);
        for(const FLDDemoEffectDefinition& Effect:Values.Effects){WorkshopAddElement(Head.Elements,WorkshopEffectElement(Effect));}
        WorkshopSortElements(Head.Elements);
    }
    return Head;
}
TArray<ELDDemoElement> LDWorkshop::CardElements(FName Card,const FLDDemoContentDefinitions* Content)
{
    TArray<ELDDemoElement> Out;
    if(const FLDDemoCardDefinition* Definition=LDHUD::CardDefinitionOf(Content,Card))
    {
        for(const FLDDemoEffectDefinition& Effect:Definition->Effects){WorkshopAddElement(Out,WorkshopEffectElement(Effect));}
    }
    return Out;
}

FString LDWorkshop::ReactionName(ELDDemoElement First,ELDDemoElement Second)
{
    // The workshop names unordered pairs.
    return LDTerms::ReactionTerm(uint8(LDCombatElements::ReactionOf(LDCombatElements::FromDemo(First),LDCombatElements::FromDemo(Second))));
}
bool LDWorkshop::FullScreen(bool bRest,ELDRestStep Step,bool bDeadWait)
{
    if(bRest){return Step==ELDRestStep::Build||Step==ELDRestStep::Settlement||Step==ELDRestStep::Payout||Step==ELDRestStep::Return;}
    return bDeadWait;
}
