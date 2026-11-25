#include "UI/LDBuildRingModel.h"
#include "Combat/LDAttributeProjection.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDProgressionRules.h"
#include "UI/LDTerms.h"
#include "UI/LDWorkshopModel.h"

namespace
{
const FLDDemoAbilityDefinition* RingAbility(const FLDDemoContentDefinitions* Content,FName Id)
{
    if(!Content||Id.IsNone()){return nullptr;}
    const FString Text=Id.ToString();
    return Content->Abilities.FindByPredicate([&Text](const FLDDemoAbilityDefinition& Row){return Row.Id==Text;});
}
// The projected attribute a per-point bonus adds to.
uint8 RingBonusAttribute(ELDDemoBonusStat Bonus)
{
    using A=ELDProjectedAttribute;
    switch(Bonus)
    {
    case ELDDemoBonusStat::ADFlat: case ELDDemoBonusStat::ADBonusRatio: return uint8(A::AD);
    case ELDDemoBonusStat::APFlat: case ELDDemoBonusStat::APBonusRatio: return uint8(A::AP);
    case ELDDemoBonusStat::MaxHPFlat: return uint8(A::MaxHP);
    case ELDDemoBonusStat::ArmorFlat: return uint8(A::Armor);
    case ELDDemoBonusStat::MagicResistFlat: return uint8(A::MagicResist);
    case ELDDemoBonusStat::CooldownHasteFlat: return uint8(A::CooldownHaste);
    case ELDDemoBonusStat::AttackRangeCmFlat: return uint8(A::AttackRange);
    case ELDDemoBonusStat::AttackSpeedBonusRatio: return uint8(A::AttackSpeed);
    case ELDDemoBonusStat::MoveSpeedBonusRatio: return uint8(A::MoveSpeed);
    case ELDDemoBonusStat::CritChanceBonusRatio: return uint8(A::CritChance);
    case ELDDemoBonusStat::HPRegenFlat: return uint8(A::HPRegen);
    case ELDDemoBonusStat::MomentumRegenFlat: return uint8(A::MomentumRegen);
    }
    return uint8(A::Count);
}
FString RingChoiceCount(int32 Count)
{
    static const TCHAR* Digits[]={TEXT("一"),TEXT("二"),TEXT("三"),TEXT("四"),TEXT("五")};
    return FString::Printf(TEXT("%s选一"),Count>=1&&Count<=5?Digits[Count-1]:TEXT(""));
}
FString RingCardOfferTitle(ELDCardOfferSource Source)
{
    switch(Source)
    {
    case ELDCardOfferSource::Opening: return TEXT("开局奖励");
    case ELDCardOfferSource::LevelUp: return TEXT("升级奖励");
    case ELDCardOfferSource::PvPKill: case ELDCardOfferSource::Bounty: case ELDCardOfferSource::LeaderBounty: return TEXT("击杀奖励");
    case ELDCardOfferSource::BossLegendary: case ELDCardOfferSource::BossMedal: return TEXT("Boss 奖励");
    case ELDCardOfferSource::Locker: return TEXT("外卖柜");
    case ELDCardOfferSource::Peddler: return TEXT("货郎");
    case ELDCardOfferSource::Arena: return TEXT("擂台奖励");
    case ELDCardOfferSource::MiniFlag: return TEXT("小旗奖励");
    default: return TEXT("野外奖励");
    }
}
LDBuildRing::FCard RingCard(const FLDDemoContentDefinitions* Content,FName Key,const TArray<FName>& Keys,int32 Copies)
{
    LDBuildRing::FCard Card;
    Card.Key=Key;Card.Keys=Keys;Card.Copies=FMath::Max(1,Copies);
    Card.Definition=LDHUD::CardDefinition(Key);
    Card.Rarity=LDBuildRing::RarityOf(Content,Card.Definition);
    if(const FLDDemoCardDefinition* Definition=LDHUD::CardDefinitionOf(Content,Card.Definition)){Card.Form=Definition->Form;}
    Card.Name=LDHUD::ContentName(Content,Card.Definition.ToString());
    Card.IconId=LDHUD::ContentIconId(Content,Card.Definition.ToString());
    if(Card.Form==ELDCardForm::Medal)
    {
        const TArray<ELDDemoElement> Elements=LDWorkshop::CardElements(Card.Definition,Content);
        Card.Element=Elements.IsEmpty()?ELDDemoElement::None:Elements[0];
    }
    return Card;
}
}

FString LDBuildRing::ArtPackage(const FString& Id)
{
    for(const FArt& Entry:Art){if(Id==Entry.Id){return Entry.Package;}}
    return FString();
}
FVector2D LDBuildRing::SlotPolar(int32 Index,float HostAngleDegrees)
{
    static const int32 Order3[]={1,0,2},Order4[]={1,2,0,3},Order5[]={2,1,3,0,4};
    int32 Row=0,Within=FMath::Max(0,Index);
    while(Row<RowCount-1&&Within>=RowSlots[Row]){Within-=RowSlots[Row];++Row;}
    // Past the fourth row the last row repeats outwards (never reached: 3 + 4 + 5 + 5 places hold every capacity).
    const int32 Extra=Row==RowCount-1?Within/RowSlots[Row]:0;
    Within%=RowSlots[Row];
    const int32 Count=RowSlots[Row];
    const int32 Column=Count==3?Order3[Within]:Count==4?Order4[Within]:Order5[Within];
    const float Radius=RowRadius[Row]+Extra*70.f;
    return FVector2D(Radius,HostAngleDegrees+(Column-(Count-1)*.5f)*RowPitch[Row]*RowRadius[Row]/Radius);
}
int32 LDBuildRing::RowsFor(int32 Count)
{
    int32 Rows=0,Left=FMath::Max(1,Count);
    while(Left>0){Left-=RowSlots[FMath::Min(Rows,RowCount-1)];++Rows;}
    return Rows;
}
float LDBuildRing::OuterFor(int32 Count)
{
    const int32 Rows=RowsFor(Count);
    return Rows<=2?BandOuter:RowRadius[FMath::Min(Rows,RowCount)-1]+NodeSize*.5f+11.f+(Rows>RowCount?(Rows-RowCount)*70.f:0.f);
}
LDBuildRing::FScale LDBuildRing::ScaleFor(float PixelHeight,float TextScale)
{
    FScale Out;
    Out.Layout=FMath::Max(.01f,PixelHeight/1080.f);
    Out.Ring=FMath::Max(Out.Layout,2.f/3.f);
    Out.Text=FMath::Max(Out.Layout,2.f/3.f)*FMath::Clamp(TextScale,.5f,2.f);
    return Out;
}
LDBuildRing::ERarity LDBuildRing::RarityOf(const FLDDemoContentDefinitions* Content,FName Definition)
{
    if(!Content){return ERarity::Common;}
    switch(LDProgression::CardRarity(*Content,Definition))
    {
    case LDProgression::ELDCardRarity::Legendary: return ERarity::Legendary;
    case LDProgression::ELDCardRarity::Rare: return ERarity::Rare;
    default: return ERarity::Common;
    }
}
FString LDBuildRing::RarityName(ERarity Rarity)
{
    return Rarity==ERarity::Legendary?TEXT("传说"):Rarity==ERarity::Rare?TEXT("稀有"):TEXT("普通");
}
uint32 LDBuildRing::CardVocab(const FLDDemoContentDefinitions* Content,FName Definition)
{
    const FLDDemoCardDefinition* Card=LDHUD::CardDefinitionOf(Content,Definition);
    return Card?Card->VocabMask:0;
}
uint32 LDBuildRing::ActiveVocab(const FLDDemoContentDefinitions* Content,FName Active)
{
    const FLDDemoAbilityDefinition* Ability=RingAbility(Content,Active);
    return Ability?Ability->VocabMask:0;
}
TArray<FIntPoint> LDBuildRing::StatAttributes(const FLDDemoContentDefinitions* Content)
{
    TArray<FIntPoint> Out;
    if(!Content){return Out;}
    const TArray<const FLDDemoStatDefinition*> Stats=Content->StatsInDesignOrder();
    for(int32 Index=0;Index<Stats.Num();++Index)
    {
        for(const FLDDemoStatBonus& Bonus:Stats[Index]->PerPoint)
        {
            const uint8 Attribute=RingBonusAttribute(Bonus.Stat);
            if(Attribute<uint8(ELDProjectedAttribute::Count)&&!Out.ContainsByPredicate([Attribute](const FIntPoint& Row){return Row.Y==Attribute;}))
            {Out.Add(FIntPoint(Index,Attribute));}
        }
    }
    return Out;
}
FString LDBuildRing::FormTag(ELDCardForm Form)
{
    switch(Form)
    {
    case ELDCardForm::Skill: return TEXT("技");
    case ELDCardForm::Dash: return TEXT("冲");
    case ELDCardForm::Medal: return TEXT("元");
    default: return TEXT("体");
    }
}
FString LDBuildRing::FormPlace(ELDCardForm Form)
{
    switch(Form)
    {
    case ELDCardForm::Skill: return TEXT("装在技能上");
    case ELDCardForm::Dash: return TEXT("装在冲刺上");
    case ELDCardForm::Medal: return TEXT("镶进元素槽");
    default: return TEXT("装在本体上");
    }
}
int32 LDBuildRing::HandGroup(ELDCardForm Form)
{
    switch(Form)
    {
    case ELDCardForm::Skill: return 0;
    case ELDCardForm::Dash: return 1;
    case ELDCardForm::Medal: return 3;
    default: return 2;
    }
}

LDBuildRing::FView LDBuildRing::Build(const FInputs& In)
{
    FView View;
    const FLDProgressionView* Progression=In.Progression;
    const FLDDemoContentDefinitions* Content=In.Content;
    if(!Progression||!Progression->bValid||!In.Storage||!Content){return View;}
    View.bValid=true;
    const LDHUD::FStorageView& Storage=*In.Storage;
    View.Level=Progression->Level;
    View.Installed=Storage.Installed;
    View.Capacity=Storage.Capacity;
    View.MaxCapacity=FMath::Max(View.Capacity,LDProgression::CardCapacity(Content->Parameters,FMath::Max<uint8>(Progression->MaxLevel,Progression->Level)));
    View.Unspent=In.Unspent;
    View.Keystone=int32(Content->Parameters.KeystoneThresholdPoints);
    View.Fresh=Storage.Fresh;

    // The hosts, each with its installed cards in serial order and its medal.
    for(int32 Index=0;Index<HostCount;++Index)
    {
        FHost& Host=View.Hosts.AddDefaulted_GetRef();
        Host.Column=static_cast<LDHUD::EStorageColumn>(Index);
        const LDHUD::FStorageColumnView* Column=Storage.Columns.IsValidIndex(Index)?&Storage.Columns[Index]:nullptr;
        Host.Active=Column?Column->Active:NAME_None;
        if(Index>=1&&Index<=3)
        {
            const uint8 Slot=uint8(Index-1);
            if(const FLDActiveSlotView* Learned=Progression->Actives.FindByPredicate([Slot](const FLDActiveSlotView& Row){return Row.Slot==Slot;}))
            {Host.Branch=Learned->Branch;}
            Host.Key=In.Keys.Of(Slot);
            Host.Form=ELDCardForm::Skill;
        }
        else if(Host.Column==LDHUD::EStorageColumn::Dash){Host.Key=In.DashKey.IsEmpty()?FString(TEXT("D")):In.DashKey;Host.Form=ELDCardForm::Dash;}
        const LDWorkshop::FColumnHead Head=LDWorkshop::ColumnHead(Host.Column,Host.Active,Host.Branch,FString(),Content);
        Host.Name=Head.Name;Host.IconId=Head.IconId;Host.bEmpty=Head.bEmpty;
        if(!Column){continue;}
        Host.bSocket=Column->bMedalSlot;
        Host.bHasMedal=Column->bHasMedal;
        if(Host.bHasMedal)
        {
            Host.Medal=RingCard(Content,Column->Medal.Card,Column->Medal.Keys,1);
            Host.Medal.Tier=Column->Medal.MedalTier;
        }
        for(const LDHUD::FEquippedView& Entry:Column->Entries){Host.Cards.Add(RingCard(Content,Entry.Card,Entry.Keys,Entry.Copies));}
    }
    // Reactions between socketed hosts holding different elements (the dash has no socket).
    for(int32 A=0;A<HostCount;++A)
    {
        for(int32 B=A+1;B<HostCount;++B)
        {
            const FHost& First=View.Hosts[A];const FHost& Second=View.Hosts[B];
            if(!First.bHasMedal||!Second.bHasMedal||First.Medal.Element==ELDDemoElement::None||Second.Medal.Element==ELDDemoElement::None
                ||First.Medal.Element==Second.Medal.Element){continue;}
            const FString Name=LDWorkshop::ReactionName(First.Medal.Element,Second.Medal.Element);
            if(Name.IsEmpty()){continue;}
            FReaction& Reaction=View.Reactions.AddDefaulted_GetRef();
            Reaction.A=A;Reaction.B=B;Reaction.First=First.Medal.Element;Reaction.Second=Second.Medal.Element;Reaction.Name=Name;
        }
    }
    // The hand: every stored definition once, grouped by form in the hand's order, the storage's order within a group.
    for(const LDHUD::FStoredView& Stored:Storage.Stored)
    {
        FCard Card=RingCard(Content,Stored.Card,Stored.Keys,Stored.Copies);
        Card.bFresh=Stored.bFresh;
        View.Hand.Add(MoveTemp(Card));
    }
    View.Hand.StableSort([](const FCard& A,const FCard& B){return HandGroup(A.Form)<HandGroup(B.Form);});
    // The five stats in design order.
    const TArray<const FLDDemoStatDefinition*> Stats=Content->StatsInDesignOrder();
    for(int32 Index=0;Index<Stats.Num();++Index)
    {
        const FName Id(*Stats[Index]->Id);
        const int32 Points=In.Points.IsValidIndex(Index)?In.Points[Index]:LDHUD::StatPointsOf(*Progression,Id);
        const LDHUD::FStatRowView Row=LDHUD::StatRow(*Stats[Index],Points,Content);
        FStat& Stat=View.Stats.AddDefaulted_GetRef();
        Stat.Id=Id;Stat.Name=Row.Name;Stat.IconId=Row.IconId;Stat.Points=Points;
        Stat.bKeystone=View.Keystone>0&&Points>=View.Keystone;
    }
    // The five-stat table: each stat's attributes with the own projection's final values (an attribute the projection
    // lacks is left out).
    if(In.Attributes)
    {
        for(const FIntPoint& Pair:StatAttributes(Content))
        {
            const uint8 Attribute=uint8(Pair.Y);
            const FLDCombatAttributeRowView* Row=In.Attributes->FindByPredicate([Attribute](const FLDCombatAttributeRowView& R){return R.Attribute==Attribute;});
            if(!Row){continue;}
            const LDHUD::FAttributeRowView Shown=LDHUD::AttributeRow(*Row);
            FDerived& Derived=View.Derived.AddDefaulted_GetRef();
            Derived.Attribute=Attribute;Derived.Name=Shown.Name;Derived.Source=Pair.X;
            Derived.Value=Shown.Value;
            if(Attribute==uint8(ELDProjectedAttribute::HPRegen)||Attribute==uint8(ELDProjectedAttribute::MomentumRegen)){Derived.Value+=TEXT("/s");}
            else if(Attribute==uint8(ELDProjectedAttribute::AttackSpeed)){Derived.Value+=TEXT("/分");}
        }
    }
    // The decision on top: the final mod, the active offer, the card offer, then the blueprints.
    {
        const bool bFinal=Progression->bFinalModPending&&(Progression->FinalModOptions.Num()==3||Progression->FinalModOptions.Num()==4);
        const bool bActive=Progression->bHasActiveOffer&&!Progression->ActiveOffer.Options.IsEmpty();
        const bool bCard=Progression->bHasCardOffer&&!Progression->CardOffer.Options.IsEmpty();
        const bool bBlueprint=!Progression->PendingBlueprints.IsEmpty();
        const int32 Total=(bFinal?1:0)+(bActive?FMath::Max<int32>(Progression->QueuedActiveOpportunities,1):0)
            +(bCard?FMath::Max<int32>(Progression->QueuedCardOffers,1):0)+(bBlueprint?1:0);
        FBatch& Batch=View.Batch;
        Batch.Queued=FMath::Max(0,Total-1);
        const auto ActiveOption=[&](FName Id)
        {
            const LDHUD::FActiveCardView Face=LDHUD::ActiveCard(Id,Content,In.Stats);
            FOption Option;Option.Id=Id;Option.Name=Face.Name;Option.IconId=Face.IconId;Option.Text=Face.SentencePlain;Option.Form=ELDCardForm::Skill;
            return Option;
        };
        if(bFinal)
        {
            Batch.Kind=EBatch::Final;Batch.Title=FString(TEXT("终局改装 · "))+RingChoiceCount(Progression->FinalModOptions.Num());
            for(const FName Id:Progression->FinalModOptions)
            {
                const LDHUD::FFinalModCardView Face=LDHUD::FinalModCard(Id,Content,In.Stats);
                FOption Option;Option.Id=Id;Option.Name=Face.Name;Option.IconId=Face.IconId;
                LDHUD::ContentText(Content,Id.ToString(),true,Option.Text);
                Option.Rarity=ERarity::Legendary;
                Batch.Options.Add(MoveTemp(Option));
            }
        }
        else if(bActive)
        {
            Batch.Kind=EBatch::Active;Batch.Title=FString(TEXT("新技能 · "))+RingChoiceCount(Progression->ActiveOffer.Options.Num());
            Batch.SkipPoints=Progression->ActiveOffer.SkipPoints;
            for(const FName Id:Progression->ActiveOffer.Options){Batch.Options.Add(ActiveOption(Id));}
        }
        else if(bCard)
        {
            Batch.Kind=EBatch::Card;
            Batch.Title=RingCardOfferTitle(Progression->CardOffer.Source)+TEXT(" · ")+RingChoiceCount(Progression->CardOffer.Options.Num());
            Batch.Rerolls=Progression->CardRerolls;
            for(const FName Id:Progression->CardOffer.Options)
            {
                const LDHUD::FCardFaceView Face=LDHUD::CardFace(Id,Content,In.Stats);
                FOption Option;Option.Id=Id;Option.Name=Face.Name;Option.IconId=Face.IconId;Option.Text=Face.SentencePlain;
                Option.Rarity=RarityOf(Content,Id);Option.Form=Face.Form;
                Batch.Options.Add(MoveTemp(Option));
            }
        }
        else if(bBlueprint)
        {
            Batch.Kind=EBatch::Blueprint;Batch.Title=FString(LDTerms::Blueprint)+TEXT(" · 学一个");
            for(const FName Id:Progression->PendingBlueprints){Batch.Options.Add(ActiveOption(Id));}
        }
    }
    // The rest shop: the shelf, then the skill point.
    View.bShop=In.bRestShop&&Progression->Shop.bStocked;
    if(View.bShop)
    {
        const FLDShopView& Shop=Progression->Shop;
        View.RefreshPrice=Shop.RefreshPrice;
        for(int32 Slot=0;Slot<Shop.Slots.Num();++Slot)
        {
            const FLDShopSlotView& Row=Shop.Slots[Slot];
            if(Row.Kind==ELDShopItemKind::None){continue;}
            FShopItem& Item=View.Shop.AddDefaulted_GetRef();
            Item.Slot=Slot;Item.Kind=Row.Kind;Item.Id=Row.DefinitionId;Item.Price=Row.Price;Item.bSold=Row.bSold;
            Item.Name=LDHUD::ContentName(Content,Row.DefinitionId.ToString());
            Item.IconId=LDHUD::ContentIconId(Content,Row.DefinitionId.ToString());
            if(Row.Kind==ELDShopItemKind::Card){Item.Rarity=RarityOf(Content,Row.DefinitionId);}
            else{Item.Note=TEXT("新技能");}
        }
        FShopItem& Point=View.Shop.AddDefaulted_GetRef();
        Point.Slot=FLDBuildCommand::ShopSkillPointSlot;Point.Name=TEXT("技能点");Point.Price=Shop.SkillPointPrice;
        Point.bSold=Shop.SkillPointsBought>=Shop.SkillPointsMax;
        Point.Note=FString::Printf(TEXT("限购 %d"),FMath::Max(0,Shop.SkillPointsMax-Shop.SkillPointsBought));
    }
    return View;
}
