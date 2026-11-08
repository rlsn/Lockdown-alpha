#include "UI/LDHUDModel.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "../Development/LDDemoContentJson.h"
#include "Combat/LDAttributeProjection.h"
#include "Development/LDDemoCardText.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDGrowthProtocol.h"
#include "Internationalization/StringTableCore.h"
#include "Internationalization/StringTableRegistry.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Tests/LDTestApprovedDisplay.h"
#include "UObject/Package.h"

namespace LDCardSelfCheckTests
{
namespace
{
bool ReadApprovedFixture(FAutomationTestBase& Test, TSharedPtr<const FLDDemoContentSnapshot>& OutCatalog)
{
    FString Root;
    FString Error;
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoContentFixtures="), Root) || Root.IsEmpty())
    {
        Test.AddError(TEXT("Missing -LDDemoContentFixtures=<isolated Artifacts directory>; export approved_base first. No live-data fallback."));
        return false;
    }
    if (!Test.TestTrue(TEXT("Card fixtures are isolated under this project's Artifacts"),
        FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Root),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts")))))
    {
        return false;
    }
    LDDemoContent::FCheck Check(Error);
    TArray<uint8> Bytes;
    LDDemoContent::FValue IndexValue;
    if (!LDDemoContent::ReadFile(FPaths::Combine(Root, TEXT("index.json")), Bytes, Error)
        || !LDDemoContent::Parse(Bytes, IndexValue, Check, TEXT("fixture index")))
    {
        Test.AddError(Error);
        return false;
    }
    LDDemoContent::FFields Index(Check, IndexValue, TEXT("fixture index"));
    bool bTestOnly = false;
    const TArray<LDDemoContent::FValue>* Valid = nullptr;
    const TArray<LDDemoContent::FValue>* Invalid = nullptr;
    if (!Index.Literal(TEXT("format"), TEXT("demo-native-fixtures-1"))
        || !Index.Flag(TEXT("test_only"), bTestOnly) || !Check.Require(bTestOnly, Index.Path, TEXT("test-only marker required"))
        || !LDDemoContent::Array(Index.Take(TEXT("valid")), Valid, Check, Index.Path, 1, 1024)
        || !LDDemoContent::Array(Index.Take(TEXT("invalid")), Invalid, Check, Index.Path, 0, 1024) || !Index.Done())
    {
        Test.AddError(Error);
        return false;
    }
    bool bFound = false;
    FLDDemoContentDigest Digest;
    for (const LDDemoContent::FValue& RowValue : *Valid)
    {
        LDDemoContent::FFields Row(Check, RowValue, TEXT("fixture case"));
        FString Name;
        if (!Row.Identifier(TEXT("name"), Name))
        {
            Test.AddError(Error);
            return false;
        }
        if (!Name.Equals(TEXT("approved_base"), ESearchCase::CaseSensitive)) { continue; }
        const TArray<LDDemoContent::FValue>* ChangedPaths = nullptr;
        FString MutatedQuantity;
        if (!Check.Require(!bFound, Row.Path, TEXT("duplicate approved_base fixture"))
            || !Row.Digest(TEXT("catalog_sha256"), Digest)
            || !LDDemoContent::Array(Row.Take(TEXT("changed_paths")), ChangedPaths, Check, Row.Path, 0, LDDemoContent::MaxFileBytes)
            || !Row.Text(TEXT("mutated_quantity"), MutatedQuantity, 0) || !Row.Done())
        {
            Test.AddError(Error);
            return false;
        }
        bFound = true;
    }
    if (!Test.TestTrue(TEXT("approved_base fixture is present"), bFound)) { return false; }
    FLDDemoContentBundle Bundle;
    ULDDemoContentCatalog* Asset = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
    TUniquePtr<FLDDemoPreparedContent> Prepared;
    if (!FLDDemoContentBundle::ReadImportFiles(FPaths::Combine(Root, TEXT("approved_base")), Bundle, Error)
        || !ULDDemoContentCatalog::PrepareImport(*Asset, Bundle, Digest, Prepared, Error)
        || !Asset->CommitImport(Prepared, Digest, Error) || !Asset->Read(Digest, OutCatalog, Error))
    {
        Test.AddError(Error);
        return false;
    }
    return true;
}

// The game registers the approved display table at startup; when this editor session has not, the test registers the same
// names for the card faces (OutTable stays None when the existing table is used and nothing must be unregistered).
bool RegisterDisplayNames(FAutomationTestBase& Test, const FLDDemoContentDefinitions& Content, FName& OutTable)
{
    OutTable = NAME_None;
    TMap<FString, FString> Names;
    if (!LDTestApprovedDisplay::Names(Test, Names) || Content.Display.IsEmpty()) { return false; }
    const FName Namespace(*Content.Display[0].Namespace);
    if (FStringTableRegistry::Get().FindStringTable(Namespace).IsValid()) { return true; }
    OutTable = Namespace;
    FStringTableRef Table = FStringTable::NewStringTable();
    Table->SetNamespace(OutTable.ToString());
    for (const FLDDemoDisplayDefinition& Row : Content.Display)
    {
        const FString* Name = Names.Find(Row.Id);
        if (!Name || Row.Namespace != OutTable.ToString()) { continue; }
#if WITH_EDITORONLY_DATA
        Table->SetSourceString(FTextKey(*Row.NameKey), *Name, FString());
#else
        Table->SetSourceString(FTextKey(*Row.NameKey), *Name);
#endif
    }
    FStringTableRegistry::Get().RegisterStringTable(OutTable, Table);
    return true;
}

// Words a card face must never show (#147 kickoff): the capture verbs and an unexplained effect.
bool Forbidden(const FString& Text)
{
    return Text.Contains(TEXT("喷漆")) || Text.Contains(TEXT("守护")) || Text.Contains(TEXT("未知效果")) || Text.Contains(TEXT("?"))
        || Text.Contains(TEXT("内容目录中找不到"));
}

// Whether the rank's authored data behind a placeholder is non-zero (before growth and the owner's stats).
bool Configured(const LDDemoCardText::FToken& Token, const FLDDemoRankDefinition& Rank)
{
    using LDDemoCardText::EField;
    const FLDDemoEffectDefinition* Effect = LDDemoCardText::FindEffect(Rank, Token);
    if (!Effect) { return false; }
    switch (Token.Field)
    {
    case EField::Amount: return Effect->BaseP2 != 0 || Effect->ADRatioBp != 0 || Effect->APRatioBp != 0 || Effect->MaxHPRatioBp != 0
        || Effect->ArmorRatioBp != 0;
    case EField::Burst: return Effect->SecondaryRatioBp != 0;
    case EField::BasicCount: return Effect->BasicCount != 0;
    case EField::CooldownCut: return Effect->CooldownRatioBp != 10000;
    case EField::Refund: return Effect->AmountMs != 0;
    case EField::Duration: return Effect->DurationMs != 0;
    case EField::Ratio: return Effect->RatioBp != 0;
    case EField::Count: return Effect->Count != 0;
    case EField::Radius: return Effect->RadiusCm.Numerator != 0;
    case EField::Distance: return Effect->DistanceCm.Numerator != 0;
    case EField::Interval: return Effect->IntervalMs != 0;
    case EField::Delay: return Effect->DelayMs != 0;
    case EField::SecondaryRatio: return Effect->SecondaryRatioBp != 0;
    case EField::SecondaryDuration: return Effect->SecondaryDurationMs != 0;
    case EField::Stat:
    {
        const FLDDemoStatBonus* Stat = Effect->Stats.FindByPredicate([&Token](const FLDDemoStatBonus& S) { return S.Stat == Token.Stat; });
        return Stat && (Stat->Amount != 0 || Stat->Cm.Numerator != 0);
    }
    default: return false;
    }
}

// Every placeholder whose authored data is non-zero must read a non-zero number (a value read from the wrong storage
// field shows as 0, e.g. a stat held in Cm read through Amount).
void CheckNonZero(FAutomationTestBase& Test, const FString& Label, const FString& Template, const FLDDemoRankDefinition& Rank,
    const LDHUD::FCardText& Text)
{
    TArray<LDDemoCardText::FToken> Tokens;
    FString Error;
    if (!Test.TestTrue(Label + TEXT(": the template parses ") + Error, LDDemoCardText::Parse(Template, Tokens, Error))) { return; }
    int32 Index = 0;
    for (const LDDemoCardText::FToken& Token : Tokens)
    {
        if (!Token.bValue) { continue; }
        const FString Value = Text.Values.IsValidIndex(Index) ? Text.Values[Index] : FString();
        ++Index;
        if (!Configured(Token, Rank)) { continue; }
        Test.TestFalse(FString::Printf(TEXT("%s: placeholder %d reads %s from a non-zero authored value"), *Label, Index, *Value),
            Value.IsEmpty() || FMath::IsNearlyZero(FCString::Atod(*Value)));
    }
}

FLDChoiceOption Option(ELDChoiceOptionKind Kind, const FString& Id, uint8 From, uint8 To)
{
    FLDChoiceOption Out;
    Out.OptionId = FGuid::NewGuid();
    Out.Kind = Kind;
    Out.DefinitionId = FName(*Id);
    Out.FromRank = From;
    Out.ToRank = To;
    return Out;
}
}
}

// #147 v7 R1-R3 self-check over every draftable card of the approved catalog with the character's base stats: at level 1 a
// learn card's sentence is at most 26 read characters and its advanced line at most 18; at levels 1 and 10 each line uses
// at most 2 glossary keywords, every placeholder reads a number, and no face (sentence, advanced line, details, keyword
// lines) shows spray/guard wording or an unknown effect. Upgrade cards ("A -> B") and attribute cards are checked for
// completeness and wording.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDCardSelfCheckTest, "Lockdown.Rules.HUDCardSelfCheck",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDHUDCardSelfCheckTest::RunTest(const FString& Parameters)
{
    using namespace LDCardSelfCheckTests;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(*this, Catalog)) { return false; }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    FName Table;
    if (!RegisterDisplayNames(*this, Content, Table)) { return false; }
    ON_SCOPE_EXIT { if (!Table.IsNone()) { FStringTableRegistry::Get().UnregisterStringTable(Table); } };
    const auto Base = LDAttributeProjection::Project(FLDAttributeBuildValues(1, {}, {}, {}), *Catalog);
    if (!TestTrue(TEXT("The bare level-1 build projects"), Base.IsSuccess())) { return false; }
    LDHUD::FCardStats L1;
    L1.ADP2 = Base.Value->Final.ADP2;
    L1.APP2 = Base.Value->Final.APP2;
    L1.MaxHPP2 = Base.Value->Final.MaxHPP2;
    L1.ArmorP2 = Base.Value->Final.ArmorP2;
    LDHUD::FCardStats L10 = L1;
    L10.Level = 10;
    const int64 Growth = Content.Parameters.SkillGrowthPerLevelBp;
    FLDProgressionView Progression;
    int32 Actives = 0, Passives = 0;
    const auto CheckFace = [this](const FString& Label, const LDHUD::FChoiceCardView& Card)
    {
        TestFalse(Label + TEXT(": sentence ") + Card.SentencePlain, Card.SentencePlain.IsEmpty() || Forbidden(Card.SentencePlain));
        TestFalse(Label + TEXT(": advanced line ") + Card.AdvancedPlain, Forbidden(Card.AdvancedPlain));
        for (const FString& Line : Card.Lines) { TestFalse(Label + TEXT(": detail ") + Line, Forbidden(Line)); }
        for (const FString& Line : Card.Keywords) { TestFalse(Label + TEXT(": keyword ") + Line, Forbidden(Line)); }
    };
    for (const FLDDemoAbilityDefinition& Ability : Content.Abilities)
    {
        const bool bActive = Ability.Kind == ELDDemoAbilityKind::Active;
        (bActive ? Actives : Passives) += 1;
        const FLDDemoDisplayDefinition* Display = Content.Display.FindByPredicate(
            [&Ability](const FLDDemoDisplayDefinition& Row) { return Row.Id == Ability.Id; });
        const FLDDemoRankDefinition* Basic = Ability.Ranks.FindByPredicate([](const FLDDemoRankDefinition& Row) { return Row.Rank == 1; });
        const FLDDemoRankDefinition* Advanced = Ability.Ranks.FindByPredicate([](const FLDDemoRankDefinition& Row) { return Row.Rank == 2; });
        if (!TestTrue(Ability.Id + TEXT(" has a display row, a basic and an advanced rank and an advanced line"),
            Display && Basic && Advanced && Ability.Ranks.Num() == 2 && !Display->AdvancedText.IsEmpty()))
        {
            continue;
        }
        for (const LDHUD::FCardStats& Stats : {L1, L10})
        {
            const FString At = FString::Printf(TEXT("%s L%d"), *Ability.Id, int32(Stats.Level));
            const LDHUD::FCardText Sentence = LDHUD::CardText(Display->DescriptionText, *Basic, nullptr, Stats, Growth);
            const LDHUD::FCardText Line = LDHUD::CardText(Display->AdvancedText, *Advanced, nullptr, Stats, Growth);
            const LDHUD::FCardText Upgrade = LDHUD::CardText(Display->DescriptionText, *Advanced, Basic, Stats, Growth);
            TestTrue(At + TEXT(": every placeholder reads a number"), Sentence.bComplete && Line.bComplete && Upgrade.bComplete);
            CheckNonZero(*this, At + TEXT(" sentence"), Display->DescriptionText, *Basic, Sentence);
            CheckNonZero(*this, At + TEXT(" advanced line"), Display->AdvancedText, *Advanced, Line);
            CheckNonZero(*this, At + TEXT(" upgrade"), Display->DescriptionText, *Advanced, Upgrade);
            if (Ability.Id == TEXT("passive_iced_americano"))
            {
                TestTrue(At + TEXT(": iced americano shows its mana regeneration (") + Sentence.Plain + TEXT(")"),
                    Sentence.Plain.Contains(TEXT("+2/秒")));
            }
            // v7 measured R1/R2 with level-1 numbers; at level 10 a grown number may add a digit (reported, not asserted).
            const int32 SentenceLength = LDHUD::CardLength(Sentence.Plain), LineLength = LDHUD::CardLength(Line.Plain);
            if (Stats.Level == 1)
            {
                TestTrue(FString::Printf(TEXT("%s: sentence %d <= 26"), *At, SentenceLength), SentenceLength <= 26);
                TestTrue(FString::Printf(TEXT("%s: advanced line %d <= 18"), *At, LineLength), LineLength <= 18);
            }
            else if (SentenceLength > 26 || LineLength > 18)
            {
                AddInfo(FString::Printf(TEXT("%s: grown numbers read %d / %d characters"), *At, SentenceLength, LineLength));
            }
            // R3 per line: the sentence and the advanced line each use at most two glossary keywords.
            TestTrue(FString::Printf(TEXT("%s: %d sentence keywords <= 2"), *At, Sentence.Keywords.Num()), Sentence.Keywords.Num() <= 2);
            TestTrue(FString::Printf(TEXT("%s: %d advanced keywords <= 2"), *At, Line.Keywords.Num()), Line.Keywords.Num() <= 2);
            const ELDChoiceOptionKind Learn = bActive ? ELDChoiceOptionKind::LearnActive : ELDChoiceOptionKind::LearnPassive;
            const ELDChoiceOptionKind Raise = bActive ? ELDChoiceOptionKind::UpgradeActive : ELDChoiceOptionKind::UpgradePassive;
            const LDHUD::FChoiceCardView LearnCard = LDHUD::ChoiceCard(Option(Learn, Ability.Id, 0, 1), Progression, &Content, Stats);
            const LDHUD::FChoiceCardView RaiseCard = LDHUD::ChoiceCard(Option(Raise, Ability.Id, 1, 2), Progression, &Content, Stats);
            CheckFace(At + TEXT(" learn"), LearnCard);
            CheckFace(At + TEXT(" upgrade"), RaiseCard);
            TestTrue(At + TEXT(": the learn card shows the sentence and the advanced line"),
                LearnCard.SentencePlain == Sentence.Plain && LearnCard.AdvancedPlain == TEXT("高级：") + Line.Plain);
            TestTrue(At + TEXT(": an upgrade card reads basic -> advanced"), RaiseCard.RankText == TEXT("初级 → 高级"));
            if (bActive) { TestEqual(At + TEXT(": an active card lists targeting and cost"), LearnCard.Lines.Num(), 2); }
        }
    }
    TestTrue(FString::Printf(TEXT("18 actives and 16 passives (%d, %d)"), Actives, Passives), Actives == 18 && Passives == 16);
    TestEqual(TEXT("12 attribute cards"), Content.Attributes.Num(), 12);
    for (const FLDDemoAttributeDefinition& Attribute : Content.Attributes)
    {
        const LDHUD::FChoiceCardView Card = LDHUD::ChoiceCard(Option(ELDChoiceOptionKind::Attribute, Attribute.Id, 0, 0),
            Progression, &Content, L1);
        CheckFace(Attribute.Id, Card);
        TestTrue(Attribute.Id + TEXT(" shows each of its stat lines"), Card.SentencePlain.Len() > 0 && !Card.Name.IsEmpty());
    }
    // #184: every approved equipment passive, plain and upgraded, has a player line (the item, shop and attribute
    // panels list them as "被动：..."), never the "未知效果 #n" fallback.
    for (const FLDDemoEquipmentDefinition& Item : Content.Equipment)
    {
        for (const bool bUpgraded : {false, true})
        {
            const TArray<FLDDemoEffectDefinition>* Effects = Item.HasPassive ? LDDemoEquipment::EffectivePassiveEffects(Item, bUpgraded) : nullptr;
            if (!Effects) { continue; }
            for (const FLDDemoEffectDefinition& Effect : *Effects)
            {
                const FString Line = LDHUD::EffectLine(Effect);
                TestFalse(FString::Printf(TEXT("%s%s passive op %u has a display line: %s"), *Item.Id, bUpgraded ? TEXT(" (upgraded)") : TEXT(""),
                    uint32(Effect.Opcode), *Line), Line.IsEmpty() || Line.StartsWith(TEXT("未知")));
            }
        }
    }
    return true;
}

#endif
