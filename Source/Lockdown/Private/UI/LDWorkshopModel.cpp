#include "UI/LDWorkshopModel.h"
#include "Development/LDProgressionRules.h"
#include "UI/LDTerms.h"
#include "UI/LDUIIcons.h"

namespace
{
const FLDDemoComponentDefinition* WorkshopComponent(const FLDDemoContentDefinitions* Content,FName Id)
{
    if(!Content||Id.IsNone()){return nullptr;}
    const FString Text=Id.ToString();
    return Content->Components.FindByPredicate([&Text](const FLDDemoComponentDefinition& D){return D.Id==Text;});
}
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
    case ELDDemoEffectOp::BasicElement: return Effect.Element;
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

LDWorkshop::FComponentFacts LDWorkshop::ComponentFacts(FName Component,const FLDDemoContentDefinitions* Content)
{
    FComponentFacts Facts;
    if(const FLDDemoComponentDefinition* Definition=WorkshopComponent(Content,Component))
    {
        Facts.bKnown=true;Facts.Kind=Definition->Kind;Facts.bScarce=Definition->Pool==ELDDemoComponentPool::Scarce;Facts.Tags=Definition->Tags;
    }
    return Facts;
}
LDWorkshop::FLibraryTier LDWorkshop::LibraryTier(int32 Count)
{
    FLibraryTier Tier;
    if(Count>30){Tier.Icon=46.f;Tier.PerRow=7;}
    else if(Count>15){Tier.Icon=56.f;Tier.PerRow=6;}
    return Tier;
}
TArray<ELDDemoElement> LDWorkshop::ActiveElements(FName Active,const FLDDemoContentDefinitions* Content)
{
    TArray<ELDDemoElement> Out;
    if(const FLDDemoAbilityDefinition* Ability=WorkshopAbility(Content,Active))
    {
        for(const FLDDemoEffectDefinition& Effect:Ability->Values.Effects){WorkshopAddElement(Out,WorkshopEffectElement(Effect));}
    }
    WorkshopSortElements(Out);
    return Out;
}
LDWorkshop::FColumnHead LDWorkshop::ColumnHead(LDHUD::EStorageColumn Column,FName Active,const FString& HolderName,const FLDDemoContentDefinitions* Content)
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
            if(Values.ManaCostP2>0){Head.Mana=WorkshopWhole(Values.ManaCostP2);}
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
        Head.Mana=WorkshopWhole(Ability->Values.ManaCostP2);
        Head.Cooldown=WorkshopSeconds(Ability->Values.CooldownMs);
    }
    Head.Elements=ActiveElements(Active,Content);
    return Head;
}
TArray<ELDDemoElement> LDWorkshop::BuildElements(const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content)
{
    TArray<ELDDemoElement> Out;
    for(const FLDActiveSlotView& Held:Progression.Actives){for(ELDDemoElement Element:ActiveElements(Held.DefinitionId,Content)){WorkshopAddElement(Out,Element);}}
    for(const FLDComponentInstanceView& Owned:Progression.Components)
    {
        if(!Owned.bInstalled){continue;}
        WorkshopAddElement(Out,Owned.Target.Element);
        if(const FLDDemoComponentDefinition* Definition=WorkshopComponent(Content,Owned.DefinitionId))
        {
            for(const FLDDemoEffectDefinition& Effect:Definition->Effects){WorkshopAddElement(Out,WorkshopEffectElement(Effect));}
        }
    }
    WorkshopSortElements(Out);
    return Out;
}
FString LDWorkshop::ReactionName(ELDDemoElement First,ELDDemoElement Second)
{
    const auto Is=[First,Second](ELDDemoElement A,ELDDemoElement B){return (First==A&&Second==B)||(First==B&&Second==A);};
    if(Is(ELDDemoElement::Fire,ELDDemoElement::Water)){return LDTerms::ReactionFog;}
    if(Is(ELDDemoElement::Water,ELDDemoElement::Electric)){return LDTerms::ReactionConduct;}
    if(Is(ELDDemoElement::Fire,ELDDemoElement::Electric)){return LDTerms::ReactionOverload;}
    return FString();
}
TArray<LDWorkshop::FReactionRow> LDWorkshop::Reactions(const TArray<ELDDemoElement>& BuildApplies)
{
    TArray<FReactionRow> Rows;
    const TPair<ELDDemoElement,ELDDemoElement> Pairs[]={{ELDDemoElement::Fire,ELDDemoElement::Water},{ELDDemoElement::Water,ELDDemoElement::Electric},
        {ELDDemoElement::Fire,ELDDemoElement::Electric}};
    for(const auto& Pair:Pairs)
    {
        FReactionRow& Row=Rows.AddDefaulted_GetRef();
        Row.First=Pair.Key;Row.Second=Pair.Value;Row.Name=ReactionName(Pair.Key,Pair.Value);
        Row.bLit=BuildApplies.Contains(Pair.Key)&&BuildApplies.Contains(Pair.Value);
    }
    return Rows;
}
LDWorkshop::FCapacityView LDWorkshop::Capacity(const FLDProgressionView& Progression,const LDHUD::FStorageView& Storage,const FLDDemoContentDefinitions* Content)
{
    FCapacityView View;
    View.Capacity=Storage.Capacity;
    for(const LDHUD::FStorageColumnView& Column:Storage.Columns)
    {
        for(const LDHUD::FEquippedView& Entry:Column.Entries)
        {
            View.Used.Add(ComponentFacts(Entry.Component,Content).Kind);View.Components.Add(Entry.Component);
        }
    }
    if(Content)
    {
        for(int32 Level=int32(Progression.Level)+1;Level<=int32(Progression.MaxLevel)&&Level<=255;++Level)
        {
            if(LDProgression::WorkshopCapacity(Content->Progression,uint8(Level))>View.Capacity){View.UnlockLevel=Level;break;}
        }
    }
    return View;
}
FString LDWorkshop::MissingPlace(FName Component,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content)
{
    const FLDDemoComponentDefinition* Definition=WorkshopComponent(Content,Component);
    if(!Definition||(Definition->Kind==ELDDemoComponentKind::Base&&Definition->Binding==ELDDemoComponentBinding::None)){return FString();}
    if(Definition->bTargetDash||Definition->bTargetSelf){return FString();}
    for(const FString& Host:Definition->TargetComponents)
    {
        const FName HostId(*Host);
        if(Progression.Components.ContainsByPredicate([HostId](const FLDComponentInstanceView& C){return C.DefinitionId==HostId&&C.bInstalled;})){return FString();}
    }
    TArray<FString> Needed;
    for(const TArray<FString>* List:{&Definition->TargetActives,&Definition->BindActives})
    {
        for(const FString& Active:*List)
        {
            const FName ActiveId(*Active);
            if(Progression.Actives.ContainsByPredicate([ActiveId](const FLDActiveSlotView& A){return A.DefinitionId==ActiveId;})){return FString();}
            Needed.AddUnique(LDHUD::ContentName(Content,Active));
        }
    }
    return Needed.IsEmpty()?FString():LDTerms::NeedsLearned(FString::Join(Needed,TEXT("、")));
}
TArray<FString> LDWorkshop::TriggerChips(FName Component,const FLDDemoContentDefinitions* Content)
{
    TArray<FString> Out;
    const FLDDemoComponentDefinition* Definition=WorkshopComponent(Content,Component);
    if(!Definition||Definition->Kind!=ELDDemoComponentKind::Base){return Out;}
    for(const FLDDemoEffectDefinition& Effect:Definition->Effects)
    {
        switch(Effect.Trigger)
        {
        case ELDDemoEffectTrigger::BasicHit: Out.AddUnique(LDTerms::TriggerBasicHit);break;
        case ELDDemoEffectTrigger::AbilityHit: Out.AddUnique(LDTerms::TriggerAbilityHit);break;
        case ELDDemoEffectTrigger::Damaged: Out.AddUnique(LDTerms::TriggerDamaged);break;
        case ELDDemoEffectTrigger::DashComplete: Out.AddUnique(LDTerms::TriggerDashComplete);break;
        default: break;
        }
        switch(Effect.GetOp())
        {
        case ELDDemoEffectOp::BasicElement: case ELDDemoEffectOp::NthBasicBonus: Out.AddUnique(LDTerms::TriggerBasicHit);break;
        case ELDDemoEffectOp::Burn: case ELDDemoEffectOp::Breach: if(Effect.BasicCount>0){Out.AddUnique(LDTerms::TriggerBasicHit);}break;
        case ELDDemoEffectOp::Thorns: Out.AddUnique(LDTerms::TriggerDamaged);break;
        default: break;
        }
    }
    return Out;
}
TArray<FName> LDWorkshop::LinkableComponents(FName Active,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content)
{
    TArray<FName> Out;
    const FString ActiveId=Active.ToString();
    for(const FLDComponentInstanceView& Owned:Progression.Components)
    {
        const FLDDemoComponentDefinition* Definition=WorkshopComponent(Content,Owned.DefinitionId);
        if(Definition&&(Definition->TargetActives.Contains(ActiveId)||Definition->BindActives.Contains(ActiveId))){Out.AddUnique(Owned.DefinitionId);}
    }
    return Out;
}
LDWorkshop::FFinalText LDWorkshop::SplitFinalText(const FString& Description)
{
    TArray<FString> Sentences;
    FString Rest=Description.TrimStartAndEnd();
    int32 End=INDEX_NONE;
    while(Rest.FindChar(TEXT('。'),End))
    {
        Sentences.Add(Rest.Left(End+1).TrimStartAndEnd());
        Rest=Rest.Mid(End+1).TrimStartAndEnd();
    }
    if(!Rest.IsEmpty()){Sentences.Add(Rest);}
    FFinalText Out;
    if(Sentences.IsValidIndex(0)){Out.Effect=Sentences[0];}
    if(Sentences.IsValidIndex(1)){Out.Cost=Sentences[1];}
    for(int32 Index=2;Index<Sentences.Num();++Index){Out.Notes+=Sentences[Index];}
    return Out;
}
TArray<FString> LDWorkshop::KeywordNames(const TArray<FString>& KeywordLines)
{
    TArray<FString> Out;
    for(const FString& Line:KeywordLines)
    {
        int32 Start=Line.Find(TEXT("<b>"));
        if(Start==INDEX_NONE){continue;}
        Start+=3;
        const int32 Stop=Line.Find(TEXT("</>"),ESearchCase::CaseSensitive,ESearchDir::FromStart,Start);
        if(Stop>Start){Out.AddUnique(Line.Mid(Start,Stop-Start));}
    }
    return Out;
}
int32 LDWorkshop::RestStepDot(ELDRestStep Step)
{
    switch(Step)
    {
    case ELDRestStep::Build: return 0;
    case ELDRestStep::Draw: case ELDRestStep::Betting: return 1;
    case ELDRestStep::PKReady: return 2;
    case ELDRestStep::PK: return 3;
    case ELDRestStep::Settlement: case ELDRestStep::Payout: return 4;
    case ELDRestStep::Return: return 5;
    default: return INDEX_NONE;
    }
}
bool LDWorkshop::FullScreen(bool bRest,ELDRestStep Step,bool bDeadWait)
{
    if(bRest){return Step==ELDRestStep::Build||Step==ELDRestStep::Payout||Step==ELDRestStep::Return;}
    return bDeadWait;
}
