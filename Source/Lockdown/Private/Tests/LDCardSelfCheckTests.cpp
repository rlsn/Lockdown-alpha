#include "Development/LDDemoCardText.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Development/LDDemoContentCatalog.h"
#include "Misc/AutomationTest.h"
#include "Tests/LDTestApprovedFixture.h"

namespace LDCardSelfCheckTests
{
namespace
{
// Words a card face must never show (#147 kickoff): the capture verbs and an unexplained effect.
bool Forbidden(const FString& Text)
{
    return Text.Contains(TEXT("喷漆")) || Text.Contains(TEXT("守护")) || Text.Contains(TEXT("未知效果")) || Text.Contains(TEXT("?"))
        || Text.Contains(TEXT("内容目录中找不到"));
}

// Whether the authored data behind an effect placeholder is non-zero (before growth and the owner's stats).
bool Configured(const LDDemoCardText::FToken& Token, const FLDDemoEffectDefinition& Effect)
{
    using LDDemoCardText::EField;
    switch (Token.Field)
    {
    case EField::Amount: return Effect.BaseP2 != 0 || Effect.ADRatioBp != 0 || Effect.APRatioBp != 0 || Effect.MaxHPRatioBp != 0
        || Effect.ArmorRatioBp != 0;
    case EField::Burst: return Effect.SecondaryRatioBp != 0;
    case EField::BasicCount: return Effect.BasicCount != 0;
    case EField::Refund: return Effect.AmountMs != 0;
    case EField::Duration: return Effect.DurationMs != 0;
    case EField::Ratio: return Effect.RatioBp != 0;
    case EField::Count: return Effect.Count != 0;
    case EField::Radius: return Effect.RadiusCm.Numerator != 0;
    case EField::Distance: return Effect.DistanceCm.Numerator != 0;
    case EField::Interval: return Effect.IntervalMs != 0;
    case EField::Delay: return Effect.DelayMs != 0;
    case EField::SecondaryRatio: return Effect.SecondaryRatioBp != 0;
    case EField::SecondaryDuration: return Effect.SecondaryDurationMs != 0;
    case EField::Limit: return Effect.LimitP2 != 0;
    case EField::Multiplier: return Effect.MultiplierBp != 0;
    case EField::Angle: return Effect.AngleDegrees.Numerator != 0;
    case EField::CostRatio: return Effect.CostRatioBp != 0;
    case EField::Stat:
    {
        const FLDDemoStatBonus* Stat = Effect.Stats.FindByPredicate([&Token](const FLDDemoStatBonus& S) { return S.Stat == Token.Stat; });
        return Stat && (Stat->Amount != 0 || Stat->Cm.Numerator != 0);
    }
    default: return false;
    }
}

const FLDDemoDisplayDefinition* DisplayOf(const FLDDemoContentDefinitions& Content, const FString& Id)
{
    return Content.Display.FindByPredicate([&Id](const FLDDemoDisplayDefinition& Row) { return Row.Id == Id; });
}
}
}

// #147 v7 / #198 self-check of every card template in the approved catalog: each active, point-bought stat and component has
// a named display row whose description parses; every placeholder reads a non-zero authored number of the active's single
// value set (or of the stat's per-point bonus, or of the component's own effects), so no card can show a number read from the
// wrong field; and no template shows spray/guard wording or an unknown effect. How many cards there are comes from the
// catalog itself. The rendered faces (line length, glossary keywords) belong to the HUD.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDCardSelfCheckTest, "Lockdown.Rules.HUDCardSelfCheck",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDHUDCardSelfCheckTest::RunTest(const FString& Parameters)
{
    using namespace LDCardSelfCheckTests;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(*this, Catalog)) { return false; }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    // Every active, stat and component of the approved catalog has exactly one display row, and every one of them is
    // checked below (the catalog, not a pinned count, says how many cards there are).
    TSet<FString> Definitions;
    for (const FLDDemoAbilityDefinition& Ability : Content.Abilities) { Definitions.Add(Ability.Id); }
    for (const FLDDemoStatDefinition& Stat : Content.Stats) { Definitions.Add(Stat.Id); }
    for (const FLDDemoComponentDefinition& Component : Content.Components) { Definitions.Add(Component.Id); }
    TestTrue(TEXT("The approved catalog has actives, stats and components"),
        !Content.Abilities.IsEmpty() && !Content.Stats.IsEmpty() && !Content.Components.IsEmpty());
    TestEqual(TEXT("Every active, stat and component id is distinct"), Definitions.Num(),
        Content.Abilities.Num() + Content.Stats.Num() + Content.Components.Num());
    TestEqual(TEXT("Every active, stat and component has exactly one display row"),
        Content.Display.FilterByPredicate([&Definitions](const FLDDemoDisplayDefinition& Row) { return Definitions.Contains(Row.Id); }).Num(),
        Definitions.Num());
    TSet<FString> Checked;
    for (const FLDDemoAbilityDefinition& Ability : Content.Abilities)
    {
        const FLDDemoDisplayDefinition* Display = DisplayOf(Content, Ability.Id);
        if (!TestTrue(Ability.Id + TEXT(" has a named display row with a description"),
            Display && !Display->NameText.IsEmpty() && !Display->DescriptionText.IsEmpty()))
        {
            continue;
        }
        TestFalse(Ability.Id + TEXT(": ") + Display->DescriptionText, Forbidden(Display->DescriptionText) || Forbidden(Display->NameText));
        TArray<LDDemoCardText::FToken> Tokens;
        FString Error;
        if (!TestTrue(Ability.Id + TEXT(": the template parses ") + Error, LDDemoCardText::Parse(Display->DescriptionText, Tokens, Error)))
        {
            continue;
        }
        int32 Values = 0;
        for (const LDDemoCardText::FToken& Token : Tokens)
        {
            if (!Token.bValue) { continue; }
            ++Values;
            const FLDDemoEffectDefinition* Effect = LDDemoCardText::FindEffect(Ability.Values, Token);
            TestFalse(FString::Printf(TEXT("%s: placeholder %d reads an active's effect, never a per-point bonus"), *Ability.Id, Values),
                Token.bPerPoint);
            TestTrue(FString::Printf(TEXT("%s: placeholder %d reads a non-zero authored number of its value set"), *Ability.Id, Values),
                Effect && Configured(Token, *Effect));
        }
        TestTrue(Ability.Id + TEXT(": the card shows at least one number"), Values > 0);
        Checked.Add(Ability.Id);
    }
    for (const FLDDemoStatDefinition& Stat : Content.Stats)
    {
        const FLDDemoDisplayDefinition* Display = DisplayOf(Content, Stat.Id);
        if (!TestTrue(Stat.Id + TEXT(" has a named display row with a description"),
            Display && !Display->NameText.IsEmpty() && !Display->DescriptionText.IsEmpty()))
        {
            continue;
        }
        TestFalse(Stat.Id + TEXT(": ") + Display->DescriptionText, Forbidden(Display->DescriptionText) || Forbidden(Display->NameText));
        TArray<LDDemoCardText::FToken> Tokens;
        FString Error;
        if (!TestTrue(Stat.Id + TEXT(": the template parses ") + Error, LDDemoCardText::Parse(Display->DescriptionText, Tokens, Error)))
        {
            continue;
        }
        TSet<ELDDemoBonusStat> Shown;
        for (const LDDemoCardText::FToken& Token : Tokens)
        {
            if (!Token.bValue) { continue; }
            const FLDDemoStatBonus* Bonus = LDDemoCardText::FindPerPoint(Stat, Token);
            TestTrue(FString::Printf(TEXT("%s: every placeholder reads one of its non-zero per-point bonuses"), *Stat.Id),
                Token.bPerPoint && Bonus && (Bonus->Amount != 0 || Bonus->Cm.Numerator != 0));
            if (Bonus) { Shown.Add(Bonus->Stat); }
        }
        TestTrue(Stat.Id + TEXT(": the card shows a per-point bonus"), Shown.Num() > 0);
        Checked.Add(Stat.Id);
    }
    // #198 components: every placeholder reads a non-zero authored number of the component's own effects (a binding
    // component may show none).
    for (const FLDDemoComponentDefinition& Component : Content.Components)
    {
        const FLDDemoDisplayDefinition* Display = DisplayOf(Content, Component.Id);
        if (!TestTrue(Component.Id + TEXT(" has a named display row with a description"),
            Display && !Display->NameText.IsEmpty() && !Display->DescriptionText.IsEmpty()))
        {
            continue;
        }
        TestFalse(Component.Id + TEXT(": ") + Display->DescriptionText, Forbidden(Display->DescriptionText) || Forbidden(Display->NameText));
        TArray<LDDemoCardText::FToken> Tokens;
        FString Error;
        if (!TestTrue(Component.Id + TEXT(": the template parses ") + Error, LDDemoCardText::Parse(Display->DescriptionText, Tokens, Error)))
        {
            continue;
        }
        for (const LDDemoCardText::FToken& Token : Tokens)
        {
            if (!Token.bValue) { continue; }
            const FLDDemoEffectDefinition* Effect = LDDemoCardText::FindEffect(Component.Effects, Token);
            TestTrue(FString::Printf(TEXT("%s: every placeholder reads a non-zero authored number of its effects"), *Component.Id),
                !Token.bPerPoint && Effect && Configured(Token, *Effect));
        }
        Checked.Add(Component.Id);
    }
    TestTrue(TEXT("Every active, stat and component card of the approved catalog was checked"), Checked.Includes(Definitions)
        && Checked.Num() == Definitions.Num());
    return true;
}

#endif
