#include "UI/LDWorkshopModel.h"
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

LDWorkshop::FCardFacts LDWorkshop::CardFacts(FName Card,const FLDDemoContentDefinitions* Content)
{
    FCardFacts Facts;
    if(const FLDDemoCardDefinition* Definition=LDHUD::CardDefinitionOf(Content,Card))
    {
        Facts.bKnown=true;Facts.Form=Definition->Form;Facts.Type=Definition->Type;
    }
    return Facts;
}
LDWorkshop::FLibraryGroups LDWorkshop::CollectLibrary(const LDHUD::FStorageView& Storage,const FLDDemoContentDefinitions* Content,int32 TypeFilter)
{
    FLibraryGroups Groups;
    for(const LDHUD::FStoredView& Stored:Storage.Stored)
    {
        const FCardFacts Facts=CardFacts(Stored.Card,Content);
        const int32 Form=int32(Facts.Form);
        if(!Facts.bKnown||Form<0||Form>=LibraryGroupCount||(TypeFilter!=INDEX_NONE&&int32(Facts.Type)!=TypeFilter)){continue;}
        Groups.Items[Form].Add(Stored.Card);
    }
    return Groups;
}
LDWorkshop::FLibraryTier LDWorkshop::LibraryTier(int32 Count)
{
    FLibraryTier Tier;
    if(Count>30){Tier.Icon=46.f;Tier.PerRow=7;}
    else if(Count>15){Tier.Icon=56.f;Tier.PerRow=6;}
    return Tier;
}
namespace
{
float WorkshopLibraryLabelHeight(float TextScale)
{
    return FMath::CeilToFloat(12.f*TextScale*2.7f);
}
float WorkshopLibraryHeight(FIntVector4 Counts,int32 PerRow,float Icon,float TextScale,bool bCompact)
{
    if(PerRow<1){return TNumericLimits<float>::Max();}
    const float Head=FMath::Max(12.f,FMath::CeilToFloat(13.f*TextScale*1.35f));
    const float Label=WorkshopLibraryLabelHeight(TextScale);
    const float Row=Icon+(bCompact?3.f:6.f)+Label;
    float Height=0.f;
    for(int32 Kind=0;Kind<LDWorkshop::LibraryGroupCount;++Kind)
    {
        const int32 Count=Counts[Kind];
        if(Count<=0){continue;}
        const int32 Rows=(Count+PerRow-1)/PerRow;
        Height+=(Kind?(bCompact?6.f:16.f):(bCompact?2.f:4.f))+Head+(bCompact?4.f:10.f)
            +Rows*Row+(Rows-1)*(bCompact?3.f:8.f);
    }
    return Height;
}
}
LDWorkshop::FLibraryFit LDWorkshop::FitLibrary(FIntVector4 Counts,float Width,float Height,float MinIcon,float TextScale)
{
    FLibraryFit Result;
    const int32 Total=Counts.X+Counts.Y+Counts.Z+Counts.W;
    Result.LabelHeight=WorkshopLibraryLabelHeight(TextScale);
    const float Tier=FMath::Max(LibraryTier(Total).Icon,MinIcon);
    Result.Icon=Tier;
    if(Total<=0){Result.bFits=true;Result.Pages.AddDefaulted();return Result;}
    for(const bool bCompact:{false,true})
    {
        for(const float Choice:{Tier,56.f,46.f})
        {
            if(Choice>Tier){continue;}
            const float Icon=FMath::Max(Choice,MinIcon);
            const float Gap=bCompact?4.f:14.f;
            const int32 PerRow=FMath::FloorToInt((Width+Gap)/(Icon+10.f+Gap));
            const float Needed=WorkshopLibraryHeight(Counts,PerRow,Icon,TextScale,bCompact);
            Result.Icon=Icon;Result.PerRow=PerRow;Result.RequiredHeight=Needed;Result.bCompact=bCompact;
            if(PerRow>0&&Needed<=Height)
            {
                Result.bFits=true;
                Result.Pages.AddDefaulted_GetRef().Counts=Counts;
                return Result;
            }
        }
    }

    Result.Icon=FMath::Max(46.f,MinIcon);
    Result.bCompact=true;
    Result.PerRow=FMath::FloorToInt((Width+4.f)/(Result.Icon+14.f));
    Result.RequiredHeight=0.f;
    if(Result.PerRow<1){return Result;}
    FIntVector4 Cursor(0,0,0,0);
    while(Cursor!=Counts)
    {
        FLibraryPage Page;
        Page.First=Cursor;
        for(int32 Kind=0;Kind<LibraryGroupCount;++Kind)
        {
            while(Cursor[Kind]<Counts[Kind])
            {
                FIntVector4 Next=Page.Counts;
                ++Next[Kind];
                if(WorkshopLibraryHeight(Next,Result.PerRow,Result.Icon,TextScale,true)>Height){break;}
                Page.Counts=Next;
                ++Cursor[Kind];
            }
            if(Cursor[Kind]<Counts[Kind]){break;}
        }
        if(Page.Counts==FIntVector4(0,0,0,0)){Result.Pages.Reset();return Result;}
        Result.RequiredHeight=FMath::Max(Result.RequiredHeight,WorkshopLibraryHeight(Page.Counts,Result.PerRow,Result.Icon,TextScale,true));
        Result.Pages.Add(Page);
    }
    Result.bFits=true;
    return Result;
}
float LDWorkshop::StackedTrayLimit(FIntVector4 Counts,float Width,float AvailableHeight,float MinIcon,float TextScale)
{
    FIntVector4 First(0,0,0,0);
    for(int32 Kind=0;Kind<LibraryGroupCount;++Kind)
    {
        if(Counts[Kind]>0){First[Kind]=1;break;}
    }
    float Reserve=36.f;
    if(First!=FIntVector4(0,0,0,0))
    {
        const FLibraryFit Minimum=FitLibrary(First,Width,TNumericLimits<float>::Max(),MinIcon,TextScale);
        if(!Minimum.bFits){return 0.f;}
        Reserve=FMath::Max(Reserve,Minimum.RequiredHeight);
    }
    return FMath::Max(0.f,AvailableHeight-Reserve);
}
LDWorkshop::FPendingFit LDWorkshop::FitPending(const TArray<float>& Heights,float Height,float PagerHeight)
{
    FPendingFit Result;
    if(!FMath::IsFinite(Height)||!FMath::IsFinite(PagerHeight)||Height<0.f||PagerHeight<0.f){return Result;}
    float Total=0.f;
    for(const float Row:Heights)
    {
        if(!FMath::IsFinite(Row)||Row<=0.f){return Result;}
        Total+=Row;
    }
    if(Total<=Height)
    {
        Result.Pages.Add({0,Heights.Num()});Result.bFits=true;return Result;
    }
    const float Room=Height-PagerHeight;
    if(Room<=0.f){return Result;}
    FPendingPage Page;
    float Used=0.f;
    for(int32 Index=0;Index<Heights.Num();++Index)
    {
        const float Row=Heights[Index];
        if(Row>Room){Result.Pages.Reset();return Result;}
        if(Used+Row>Room)
        {
            Result.Pages.Add(Page);
            Page={Index,0};Used=0.f;
        }
        ++Page.Count;Used+=Row;
    }
    if(Page.Count>0){Result.Pages.Add(Page);}
    Result.bFits=true;
    return Result;
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
LDWorkshop::FColumnHead LDWorkshop::ColumnHead(LDHUD::EStorageColumn Column,FName Active,ELDSkillBranch Branch,const FString& HolderName,
    const FLDDemoContentDefinitions* Content)
{
    FColumnHead Head;
    if(Column==LDHUD::EStorageColumn::Basic)
    {
        Head.Name=LDTerms::ColumnBasic;Head.Glyph=TEXT("拳");
        return Head;
    }
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
TArray<ELDDemoElement> LDWorkshop::BuildElements(const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content)
{
    TArray<ELDDemoElement> Out;
    for(const FLDActiveSlotView& Held:Progression.Actives){for(ELDDemoElement Element:ActiveElements(Held.DefinitionId,Content)){WorkshopAddElement(Out,Element);}}
    for(const FLDCardInstanceView& Owned:Progression.Cards)
    {
        if(!Owned.IsInstalled()){continue;}
        WorkshopAddElement(Out,Owned.Target.Element);
        if(const FLDDemoCardDefinition* Definition=LDHUD::CardDefinitionOf(Content,Owned.DefinitionId))
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
    for(int32 Position=0;Position<LDHUD::StorageColumnCount;++Position)
    {
        const int32 Column=int32(LDHUD::BoardColumn(Position));
        if(!Storage.Columns.IsValidIndex(Column)){continue;}
        for(const LDHUD::FEquippedView& Entry:Storage.Columns[Column].Entries)
        {
            for(const FName& Key:Entry.Keys){View.Used.Add(CardFacts(Key,Content).Type);View.Cards.Add(Key);}
        }
    }
    if(Content)
    {
        for(int32 Level=int32(Progression.Level)+1;Level<=int32(Progression.MaxLevel)&&Level<=255;++Level)
        {
            if(LDProgression::CardCapacity(Content->Parameters,uint8(Level))>View.Capacity){View.UnlockLevel=Level;break;}
        }
    }
    return View;
}
FString LDWorkshop::MissingPlace(FName Card,const FLDProgressionView& Progression,const FLDDemoContentDefinitions* Content)
{
    const FCardFacts Facts=CardFacts(Card,Content);
    return Facts.bKnown&&Facts.Form==ELDCardForm::Skill&&Progression.Actives.IsEmpty()?FString(TEXT("需要先学会一个技能")):FString();
}
LDWorkshop::FOfferLinks LDWorkshop::LinkableCards(FName Active,const FLDProgressionView& Progression,
    const FLDDemoContentDefinitions* Content,LDProgression::EMatchStage Stage,ELDLifePhase LifePhase)
{
    FOfferLinks Out;
    if(!Content||!Progression.bValid){Out.Failure=ELDGrowthReason::DataInvalid;return Out;}
    if(!Progression.bHasActiveOffer||!Progression.ActiveOffer.Options.Contains(Active))
    {Out.Failure=ELDGrowthReason::InvalidOption;return Out;}
    LDProgression::FBuildContext Context;Context.Stage=Stage;Context.LifePhase=LifePhase;
    FLDGrowthBuild Current=LDProgression::BuildFromView(*Content,Progression);
    Current.FinalMod=Progression.FinalMod;
    FLDGrowthBuild Chosen;
    LDProgression::FChoiceOutcome Choice;
    Out.Failure=LDProgression::ApplyActiveChoice(*Content,Current,Active,true,FLDBuildCommand::AnySlot,Context,Chosen,Choice);
    if(Out.Failure!=ELDGrowthReason::None||Choice.Result!=ELDBuildChoiceResult::Learned){return Out;}
    Out.bLearned=true;
    const FLDDemoAbilityDefinition* Ability=WorkshopAbility(Content,Active);
    const uint32 ActiveVocab=Ability?Ability->VocabMask:0;
    for(const FLDCardInstanceView& Owned:Progression.Cards)
    {
        const FLDDemoCardDefinition* Definition=LDHUD::CardDefinitionOf(Content,Owned.DefinitionId);
        if(!Definition){continue;}
        FLDCardTarget Target;Target.Kind=ELDCardTargetKind::Skill;Target.Active=Active;
        Target.Element=Definition->ChooseElements.IsEmpty()?ELDDemoElement::None:Definition->ChooseElements[0];
        const bool bTakes=Definition->Form==ELDCardForm::Skill&&!Owned.IsInstalled()
            &&LDProgression::CardTargetReason(*Content,Chosen,Owned.DefinitionId,Target)==ELDGrowthReason::None;
        if((bTakes||(Definition->VocabMask&ActiveVocab)!=0)
            &&!Out.Cards.ContainsByPredicate([&Owned](const FName& Key){return LDHUD::CardDefinition(Key)==Owned.DefinitionId;}))
        {
            Out.Cards.Add(LDHUD::CardKey(Owned.DefinitionId,Owned.Serial));
        }
    }
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
