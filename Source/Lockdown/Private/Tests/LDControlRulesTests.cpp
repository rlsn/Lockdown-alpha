#include "Combat/LDControlRules.h"
#include "Combat/LDAttributeProjection.h"
#include "Combat/LDCombatTypes.h"
#include "AI/LDOpponentRequests.h"
#include "Navigation/LDOrderRules.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDViewTypes.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
#include "Misc/AutomationTest.h"
#include "Combat/LDCombatDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDCaptureReducer.h"
#include "Character/LDCharacter.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

// Taunt and stealth rules (CONTENT_AND_BALANCE 15.4/15.5, issue #83).
namespace LDControlRulesTests
{
bool ReadApprovedFixture(FAutomationTestBase& Test, TSharedPtr<const FLDDemoContentSnapshot>& Out)
{
    FString Root, Error, Text;
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoContentFixtures="), Root) || Root.IsEmpty()
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Root),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts"))))
    {
        Test.AddError(TEXT("Control rule tests require isolated LDDemoContentFixtures with approved_base; no live-data fallback."));
        return false;
    }
    TSharedPtr<FJsonObject> Index;
    if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Root, TEXT("index.json")))
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Index) || !Index)
    {
        Test.AddError(TEXT("Unreadable content fixture index."));
        return false;
    }
    FString Format;
    bool bTestOnly = false;
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    if (!Index->TryGetStringField(TEXT("format"), Format) || Format != TEXT("demo-native-fixtures-1")
        || !Index->TryGetBoolField(TEXT("test_only"), bTestOnly) || !bTestOnly || !Index->TryGetArrayField(TEXT("valid"), Rows))
    {
        Test.AddError(TEXT("Invalid content fixture index."));
        return false;
    }
    bool bFound = false;
    FLDDemoContentDigest Digest;
    for (const auto& Value : *Rows)
    {
        const TSharedPtr<FJsonObject>* Row = nullptr;
        FString Name, Hex;
        if (!Value->TryGetObject(Row) || !(*Row)->TryGetStringField(TEXT("name"), Name))
        {
            Test.AddError(TEXT("Invalid fixture case."));
            return false;
        }
        if (Name != TEXT("approved_base")) { continue; }
        if (bFound || !(*Row)->TryGetStringField(TEXT("catalog_sha256"), Hex) || !FLDDemoContentDigest::ParseHex(Hex, Digest, Error))
        {
            Test.AddError(TEXT("Duplicate or invalid approved_base fixture."));
            return false;
        }
        bFound = true;
    }
    if (!bFound)
    {
        Test.AddError(TEXT("Missing approved_base fixture."));
        return false;
    }
    FLDDemoContentBundle Bundle;
    TUniquePtr<FLDDemoPreparedContent> Prepared;
    auto* Asset = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
    if (!FLDDemoContentBundle::ReadImportFiles(FPaths::Combine(Root, TEXT("approved_base")), Bundle, Error)
        || !ULDDemoContentCatalog::PrepareImport(*Asset, Bundle, Digest, Prepared, Error)
        || !Asset->CommitImport(Prepared, Digest, Error) || !Asset->Read(Digest, Out, Error))
    {
        Test.AddError(Error);
        return false;
    }
    return true;
}

FLDCombatBodyRef Contender(uint32 Id, uint32 Life = 1)
{ return {{ELDVictimKind::Contender, FGuid(Id, 0, 0, 0), Life}, FGuid(Id, Life, 0, 0)}; }

FLDCombatBodyRef CampMember(uint32 Slot)
{ return {{ELDVictimKind::CampMember, FGuid(9, 0, 0, Slot), 1}, FGuid(9, 1, 0, Slot)}; }

// The taunt of active_come_here is not in the catalog yet (assembled at integration); CONTENT_AND_BALANCE 15.3 rank 1 terms.
constexpr double TauntRadiusCm = 300;
constexpr int64 TauntMs = 1000;

FLDCombatTaunt TauntBy(const FLDCombatBodyRef& Source, int64 AtMs, int64 DurationMs = TauntMs)
{
    FLDCombatTaunt Taunt;
    Taunt.EffectId = FGuid::NewGuid();
    Taunt.SourceEntity = Source.Victim.EntityId;
    Taunt.SourceBody = Source;
    Taunt.ExpiresAtMs = AtMs + DurationMs;
    return Taunt;
}

FLDTauntCandidate Candidate(const FLDCombatBodyRef& Body, const FVector& Foot)
{
    FLDTauntCandidate Out;
    Out.Body = Body;
    Out.FootCm = Foot;
    Out.bSeesCaster = true;
    return Out;
}

bool PaperBox(FAutomationTestBase& Test, const FLDDemoContentSnapshot& Content, const FLDDemoEquipmentDefinition*& Box,
    const FLDDemoEffectDefinition*& Stealth)
{
    FString Error;
    Box = nullptr;
    Stealth = nullptr;
    if (!Content.FindEquipment(TEXT("item_paper_box"), Box, Error) || !Box || !Box->HasActive)
    {
        Test.AddError(TEXT("The approved catalog has no paper box active: ") + Error);
        return false;
    }
    Stealth = Box->Active.Rank.Effects.FindByPredicate([](const FLDDemoEffectDefinition& Effect)
    { return Effect.GetOp() == ELDDemoEffectOp::Stealth; });
    if (!Stealth)
    {
        Test.AddError(TEXT("The paper box active has no stealth effect."));
        return false;
    }
    return true;
}

double Cm(const FLDDemoRational& Value) { return double(Value.Numerator) / double(Value.Denominator); }

FLDCombatStealth StealthFrom(const FLDDemoEffectDefinition& Effect, int64 AtMs)
{
    FLDCombatStealth Out;
    Out.EffectId = FGuid::NewGuid();
    Out.ExpiresAtMs = AtMs + Effect.DurationMs;
    Out.RevealRadiusCm = Cm(Effect.RadiusCm);
    Out.MoveSpeedPenaltyBp = Effect.RatioBp;
    return Out;
}

FLDObservedTarget ObservedContender(const FLDCombatBodyRef& Body, const FVector& Foot, double ConcealBeyondCm)
{
    FLDObservedTarget Out;
    Out.Body = Body;
    Out.FootCm = Foot;
    Out.ConcealBeyondCm = ConcealBeyondCm;
    FLDObservedUnitView& Unit = Out.Presented;
    Unit.AuthorizedFieldMask = LDObservation::ContenderFields;
    Unit.Kind = ELDObservedUnitKind::Contender;
    Unit.Presentation = 1;
    Unit.FeetCm = Foot;
    Unit.ContenderId = Body.Victim.EntityId;
    Unit.Level = 2;
    Unit.Health.CurrentP2 = 40000;
    Unit.Health.MaxP2 = 60000;
    Unit.Mana.CurrentP2 = 10000;
    Unit.Mana.MaxP2 = 30000;
    Unit.Equipment.Slots[0] = uint8(ELDDemoEquipmentId::ItemPaperBox);
    return Out;
}

// What one observer at ObserverFoot may see of Target under the approved rule: open ground (geometry Visible)
// within the vision radius, then the target's stealth.
ELDSightResult OpenGround(const FVector& ObserverFoot, const FLDObservedTarget& Target, double VisionCm)
{
    const ELDSightResult Geometric = FVector::Dist2D(ObserverFoot, Target.FootCm) <= VisionCm ? ELDSightResult::Visible : ELDSightResult::OutOfRange;
    return LDObservation::ConcealedSight(Geometric, ObserverFoot, Target.FootCm, Target.ConcealBeyondCm);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDControlTauntForcedTargetTest, "Lockdown.Rules.DemoControl.TauntForcedTarget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDControlTauntForcedTargetTest::RunTest(const FString& Parameters)
{
    using namespace LDControlRulesTests;
    using EForced = LDOrderRules::EForcedOrder;
    const FLDCombatBodyRef Caster = Contender(1), Victim = Contender(2), Other = Contender(3), Camp = CampMember(1);
    constexpr int64 Now = 10000;

    // Selection at release: only other living, unprotected contenders inside the radius that see the caster.
    TArray<FLDTauntCandidate> Candidates;
    Candidates.Add(Candidate(Other, FVector(TauntRadiusCm, 0, 0)));              // boundary: inclusive
    Candidates.Add(Candidate(Victim, FVector(100, 50, 0)));
    Candidates.Add(Candidate(Contender(4), FVector(TauntRadiusCm + 0.5, 0, 0))); // outside
    Candidates.Add(Candidate(Camp, FVector(50, 0, 0)));                          // camp members are not taunted
    Candidates.Add(Candidate(Caster, FVector::ZeroVector));                      // never the caster
    FLDTauntCandidate Hidden = Candidate(Contender(5), FVector(80, 0, 0));
    Hidden.bSeesCaster = false;                                                  // blocked: cannot be taunted
    Candidates.Add(Hidden);
    FLDTauntCandidate Protected = Candidate(Contender(6), FVector(80, 0, 0));
    Protected.bProtected = true;
    Candidates.Add(Protected);
    FLDTauntCandidate Dying = Candidate(Contender(7), FVector(80, 0, 0));
    Dying.bPendingDeath = true;
    Candidates.Add(Dying);
    const TArray<int32> Selected = LDControl::SelectTauntTargets(Caster.Victim.EntityId, FVector(0, 0, 50), TauntRadiusCm, Candidates);
    TestTrue(TEXT("Exactly the eligible contenders inside the inclusive planar radius are taunted, in entity order"),
        Selected.Num() == 2 && Candidates[Selected[0]].Body == Victim && Candidates[Selected[1]].Body == Other);
    TestTrue(TEXT("A nonpositive radius taunts nobody"),
        LDControl::SelectTauntTargets(Caster.Victim.EntityId, FVector::ZeroVector, 0, Candidates).IsEmpty());

    // Forced target: a taunted body may only basic-attack the taunter's exact body.
    TOptional<FLDCombatTaunt> Taunt;
    TestTrue(TEXT("Untaunted: every action and target is allowed"),
        LDControl::TauntAllows(Taunt, Now, true, Other) && LDControl::TauntAllows(Taunt, Now, false, FLDCombatBodyRef()));
    TestTrue(TEXT("The taunt applies"), LDControl::ApplyTaunt(Taunt, TauntBy(Caster, Now), Now) && LDControl::IsTaunted(Taunt, Now));
    TestTrue(TEXT("A basic attack at the taunter is allowed"), LDControl::TauntAllows(Taunt, Now, true, Caster));
    TestFalse(TEXT("A basic attack at anyone else is refused"), LDControl::TauntAllows(Taunt, Now, true, Other));
    TestFalse(TEXT("A later life of the taunter is not the taunter"), LDControl::TauntAllows(Taunt, Now, true, Contender(1, 2)));
    TestFalse(TEXT("No ability or item, not even aimed at the taunter"), LDControl::TauntAllows(Taunt, Now, false, Caster)
        || LDControl::TauntAllows(Taunt, Now, false, FLDCombatBodyRef()));
    const FLDCombatTaunt First = Taunt.GetValue();
    TestFalse(TEXT("Re-applying the same taunt effect is not a new taunt"), LDControl::ApplyTaunt(Taunt, First, Now + 10));
    TestTrue(TEXT("A newer taunter takes over"), LDControl::ApplyTaunt(Taunt, TauntBy(Other, Now + 200), Now + 200)
        && LDControl::TauntAllows(Taunt, Now + 200, true, Other) && !LDControl::TauntAllows(Taunt, Now + 200, true, Caster));
    TestFalse(TEXT("An already expired taunt never applies"), LDControl::ApplyTaunt(Taunt, TauntBy(Caster, Now, 0), Now));

    // Navigation: the forced chase replaces the own order, follows a new taunter and ends with the taunt.
    TestTrue(TEXT("Taunt begins a forced chase over any own order"), LDOrderRules::ForcedOrderStep(true, false, false) == EForced::Begin);
    TestTrue(TEXT("The chase of the same taunter continues"), LDOrderRules::ForcedOrderStep(true, true, true) == EForced::Keep);
    TestTrue(TEXT("A new taunter restarts the chase"), LDOrderRules::ForcedOrderStep(true, true, false) == EForced::Begin);
    TestTrue(TEXT("Untaunted bodies keep their ordinary orders"), LDOrderRules::ForcedOrderStep(false, false, false) == EForced::None);

    // The opponent AI obeys the same rule: it holds body intents and an interrupted order teaches it nothing.
    FLDCombatOwnerView Own;
    Own.bHasBody = true;
    FLDCombatStatusView Status;
    Status.StatusId = uint8(ELDDemoStatusId::Taunt);
    Status.ExpiresAtMs = Now + TauntMs;
    Own.Statuses.Add(Status);
    TestTrue(TEXT("A taunted AI holds its body intents"), LDOpponentRequests::BodyIntentsHeld(Own, Now));
    TestFalse(TEXT("At the taunt's end the AI decides again"), LDOpponentRequests::BodyIntentsHeld(Own, Now + TauntMs));
    Own.bHasBody = false;
    TestFalse(TEXT("Without a body there is nothing to hold"), LDOpponentRequests::BodyIntentsHeld(Own, Now));
    FLDOrderReceipt Interrupted;
    Interrupted.Status = ELDOrderStatus::Cancelled;
    Interrupted.Failure = ELDOrderFailure::Taunted;
    TestTrue(TEXT("An order ended by a taunt is dropped, not a failed attempt"),
        LDOpponentRequests::SettleOrder(Interrupted) == ELDOpponentSettle::Dropped);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDControlTauntRestoreTest, "Lockdown.Rules.DemoControl.TauntRestore",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDControlTauntRestoreTest::RunTest(const FString& Parameters)
{
    using namespace LDControlRulesTests;
    const FLDCombatBodyRef Caster = Contender(1), Other = Contender(3);
    constexpr int64 Now = 20000;
    const FLDCombatTaunt Taunt = TauntBy(Caster, Now);
    TestTrue(TEXT("A taunt holds while its taunter lives and is observed"), LDControl::TauntHolds(Taunt, Now + TauntMs - 1, true, true));
    TestFalse(TEXT("It ends exactly at its expiry (half-open)"), LDControl::TauntHolds(Taunt, Now + TauntMs, true, true));
    TestFalse(TEXT("The taunter's death or leave ends it"), LDControl::TauntHolds(Taunt, Now, false, true));
    TestFalse(TEXT("Losing observation of the taunter (sight, range or stealth) ends it"), LDControl::TauntHolds(Taunt, Now, true, false));

    TOptional<FLDCombatTaunt> Held = Taunt;
    TOptional<FLDCombatStealth> NoStealth;
    FLDStatusExpiry Expired = LDControl::ExpireControlStatuses(Held, NoStealth, Now + TauntMs - 1);
    TestTrue(TEXT("Before expiry nothing is removed"), Held.IsSet() && !Expired.bTauntChanged);
    Expired = LDControl::ExpireControlStatuses(Held, NoStealth, Now + TauntMs);
    TestTrue(TEXT("At expiry the taunt record is removed"), !Held.IsSet() && Expired.bTauntChanged && !Expired.bStealthChanged);
    TestTrue(TEXT("Restored: every action and target is allowed again"),
        LDControl::TauntAllows(Held, Now + TauntMs, true, Other) && LDControl::TauntAllows(Held, Now + TauntMs, false, FLDCombatBodyRef()));
    TestFalse(TEXT("An expired record that was not yet swept no longer taunts"),
        LDControl::IsTaunted(TOptional<FLDCombatTaunt>(Taunt), Now + TauntMs));
    TestTrue(TEXT("Control returns: the forced chase ends and the replaced order is not resumed"),
        LDOrderRules::ForcedOrderStep(false, true, true) == LDOrderRules::EForcedOrder::End);
    // The forced-chase retry is stamped on the match-relative clock; a new match (clock back at 0) must not inherit it.
    LDOrderRules::FForcedRetry Retry;
    Retry.Hold(150., .25);
    TestFalse(TEXT("A chase that stopped by itself waits one query interval"), Retry.IsDue(150.1));
    TestTrue(TEXT("It is retried after the interval"), Retry.IsDue(150.25));
    Retry.Hold(150., .25);
    TestFalse(TEXT("Without a match reset the next match's early seconds would still wait"), Retry.IsDue(1.));
    Retry.ResetMatch();
    TestTrue(TEXT("After the match reset a taunt chases from the new match's first second"), Retry.IsDue(0.));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDControlStealthObservationTest, "Lockdown.Rules.DemoControl.StealthObservation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDControlStealthObservationTest::RunTest(const FString& Parameters)
{
    using namespace LDControlRulesTests;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    if (!LDControlRulesTests::ReadApprovedFixture(*this, Content)) { return false; }
    const FLDDemoEquipmentDefinition* Box = nullptr;
    const FLDDemoEffectDefinition* Effect = nullptr;
    if (!PaperBox(*this, *Content, Box, Effect)) { return false; }
    const double Reveal = Cm(Effect->RadiusCm);
    const double Vision = Cm(Content->Get().Parameters.VisionRadiusCm);
    TestTrue(TEXT("The reveal distance is inside the vision radius"), Reveal > 0 && Reveal < Vision);
    constexpr int64 Now = 30000;
    TOptional<FLDCombatStealth> Stealth;
    TestTrue(TEXT("The approved box stealth applies"), LDControl::ApplyStealth(Stealth, StealthFrom(*Effect, Now), Now));

    // Pure rule: stealth never grants sight and never turns a fault into a missing target.
    const FVector Observer(0, 0, 2);
    const double Hidden = LDControl::ConcealBeyondCm(Stealth, Now);
    TestEqual(TEXT("A stealthed body is concealed beyond its reveal distance"), Hidden, Reveal);
    TestTrue(TEXT("Within the reveal distance (inclusive) it is seen"),
        LDObservation::ConcealedSight(ELDSightResult::Visible, Observer, FVector(Reveal, 0, 2), Hidden) == ELDSightResult::Visible);
    TestTrue(TEXT("Beyond it the body is ordinary out-of-range, i.e. TargetUnavailable"),
        LDObservation::ConcealedSight(ELDSightResult::Visible, Observer, FVector(Reveal + 0.5, 0, 2), Hidden) == ELDSightResult::OutOfRange
        && LDObservation::ResolveFromSight(ELDSightResult::OutOfRange) == ELDObservedResolve::TargetUnavailable);
    TestTrue(TEXT("Stealth never sees through a blocker, even up close"),
        LDObservation::ConcealedSight(ELDSightResult::Blocked, Observer, FVector(10, 0, 2), Hidden) == ELDSightResult::Blocked);
    TestTrue(TEXT("Faults pass through unchanged"),
        LDObservation::ConcealedSight(ELDSightResult::InvalidPoint, Observer, FVector(10, 0, 2), Hidden) == ELDSightResult::InvalidPoint);
    TestTrue(TEXT("Without stealth the vision radius alone decides"),
        LDObservation::ConcealedSight(ELDSightResult::Visible, Observer, FVector(Vision, 0, 2), 0) == ELDSightResult::Visible);

    // Authorization and revocation through the observer ledger with the same rule the director samples with.
    FLDObserverLedger Ledger;
    FString Error;
    bool bReset = false;
    TArray<FGuid> Removed;
    TArray<FLDObservedUnitView> Units;
    TestTrue(TEXT("Observer alive"), Ledger.Begin(7, Error) && Ledger.Bind(ELDLifePhase::Alive, 1, bReset, Error));
    const FLDCombatBodyRef Body = Contender(42);
    const auto SampleAt = [&](const FVector& Foot, double Conceal)
    {
        const FLDObservedTarget Target = ObservedContender(Body, Foot, Conceal);
        TArray<const FLDObservedTarget*> Visible;
        if (OpenGround(Observer, Target, Vision) == ELDSightResult::Visible) { Visible.Add(&Target); }
        return Ledger.Sample(Visible, Removed, Units, Error);
    };
    const FVector Far(Reveal + 300, 0, 2);
    TestTrue(TEXT("Before stealth the body is observed"), SampleAt(Far, 0) && Units.Num() == 1);
    const FGuid Handle = Units[0].Handle;
    const uint32 Epoch = Units[0].ObservationEpoch;
    FGuid Found;
    uint32 FoundEpoch = 0;
    TestTrue(TEXT("Its handle is bound (and reverse lookup finds it)"), Ledger.FindHandle(Body, Found, FoundEpoch)
        && Found == Handle && FoundEpoch == Epoch);
    TestTrue(TEXT("Entering stealth beyond the reveal distance conceals the handle at the next sample"),
        SampleAt(Far, Hidden) && Units.IsEmpty() && Removed.Num() == 1 && Removed[0] == Handle);
    FLDCombatBodyRef Bound;
    TestFalse(TEXT("The concealed handle no longer authorizes anything (no lock, no LastSeen)"),
        Ledger.FindBinding(Handle, Epoch, Bound) || Ledger.FindHandle(Body, Found, FoundEpoch));
    FLDMoveIdentity Order;
    Order.Match = 7;
    Order.Life = 1;
    Order.Order = 3;
    TestFalse(TEXT("An order cannot pin the hidden handle"), Ledger.BindOrder(Order, Handle, Epoch, Error));
    TestTrue(TEXT("Approaching within the reveal distance re-discovers it under a fresh interval"),
        SampleAt(FVector(Reveal, 0, 2), Hidden) && Units.Num() == 1 && Units[0].Handle != Handle && Units[0].ObservationEpoch > Epoch);
    const FGuid Near = Units[0].Handle;
    TestTrue(TEXT("Stepping back out conceals it again"), SampleAt(Far, Hidden) && Removed.Contains(Near) && Units.IsEmpty());
    TestTrue(TEXT("When stealth ends it is observed again, never under an old handle"),
        SampleAt(Far, LDControl::ConcealBeyondCm(Stealth, Now + Effect->DurationMs)) && Units.Num() == 1
        && Units[0].Handle != Handle && Units[0].Handle != Near);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDControlStealthRevealTest, "Lockdown.Rules.DemoControl.StealthReveal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDControlStealthRevealTest::RunTest(const FString& Parameters)
{
    using namespace LDControlRulesTests;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    if (!LDControlRulesTests::ReadApprovedFixture(*this, Content)) { return false; }
    const FLDDemoEquipmentDefinition* Box = nullptr;
    const FLDDemoEffectDefinition* Effect = nullptr;
    if (!PaperBox(*this, *Content, Box, Effect)) { return false; }
    TestTrue(TEXT("The box active is a self-cast with a cooldown and a positive stealth"),
        Box->Active.Target.Mode == ELDDemoTargetMode::Self && Box->Active.Rank.CooldownMs > 0 && Effect->DurationMs > 0
        && Effect->RatioBp > 0 && Effect->RatioBp < 10000);
    constexpr int64 Now = 40000;
    TOptional<FLDCombatStealth> Stealth;
    FLDCombatStealth Bad = StealthFrom(*Effect, Now);
    Bad.RevealRadiusCm = 0;
    TestFalse(TEXT("A stealth without a reveal distance is refused"), LDControl::ApplyStealth(Stealth, Bad, Now));
    Bad = StealthFrom(*Effect, Now);
    Bad.MoveSpeedPenaltyBp = 10000;
    TestFalse(TEXT("A stealth may not remove all movement"), LDControl::ApplyStealth(Stealth, Bad, Now));
    TestTrue(TEXT("The approved stealth applies"), LDControl::ApplyStealth(Stealth, StealthFrom(*Effect, Now), Now)
        && LDControl::IsStealthed(Stealth, Now));

    // Revealing: attack, ability/item and being hurt all end it through the same Reveal; expiry is half-open.
    TOptional<FLDCombatStealth> Revealed = Stealth;
    TestTrue(TEXT("A reveal trigger ends stealth at once"), LDControl::Reveal(Revealed) && !LDControl::IsStealthed(Revealed, Now)
        && LDControl::ConcealBeyondCm(Revealed, Now) == 0);
    TestFalse(TEXT("A second trigger has nothing to reveal"), LDControl::Reveal(Revealed));
    // A taunt is a control: landing it on a stealthed body reveals it like any other control (15.4).
    TOptional<FLDCombatTaunt> Taunted;
    TOptional<FLDCombatStealth> HiddenVictim = Stealth;
    bool bTauntRevealed = false;
    TestTrue(TEXT("A taunt that lands reveals its stealthed victim"),
        LDControl::TauntVictim(Taunted, HiddenVictim, TauntBy(Contender(1), Now), Now, bTauntRevealed)
        && bTauntRevealed && !LDControl::IsStealthed(HiddenVictim, Now) && LDControl::IsTaunted(Taunted, Now));
    TOptional<FLDCombatStealth> StillHidden = Stealth;
    TestFalse(TEXT("A taunt that does not land reveals nothing"),
        LDControl::TauntVictim(Taunted, StillHidden, Taunted.GetValue(), Now, bTauntRevealed) || bTauntRevealed
        || !LDControl::IsStealthed(StillHidden, Now));
    TOptional<FLDCombatTaunt> NoTaunt;
    TOptional<FLDCombatStealth> Expiring = Stealth;
    TestTrue(TEXT("Stealth lasts until just before its end"), LDControl::IsStealthed(Expiring, Now + Effect->DurationMs - 1));
    const FLDStatusExpiry Expired = LDControl::ExpireControlStatuses(NoTaunt, Expiring, Now + Effect->DurationMs);
    TestTrue(TEXT("And ends exactly at it"), !Expiring.IsSet() && Expired.bStealthChanged);
    TOptional<FLDCombatStealth> Again = Stealth;
    TestTrue(TEXT("Re-entering a box restarts the duration"), LDControl::ApplyStealth(Again, StealthFrom(*Effect, Now + 500), Now + 500)
        && Again->ExpiresAtMs == Now + 500 + Effect->DurationMs);

    // Movement: the penalty is one more additive ratio term of FORMULA.MoveSpeed, on the box's own speed bonus.
    const FLDAttributeProjectionResult Projected = LDAttributeProjection::Project(
        FLDAttributeBuildValues(1, {}, {}, {TEXT("item_paper_box")}), *Content);
    if (!TestTrue(TEXT("A level-one body holding the box projects"), Projected.IsSuccess())) { return false; }
    const FLDAttributeContributions& Row = Projected.Value->Contributions[int32(ELDProjectedAttribute::MoveSpeed)];
    const FLDDemoStatBonus* Bonus = Box->Stats.FindByPredicate([](const FLDDemoStatBonus& Stat)
    { return Stat.Stat == ELDDemoBonusStat::MoveSpeedBonusRatio; });
    if (!TestNotNull(TEXT("The box grants a move speed bonus"), Bonus)) { return false; }
    const auto& Bounds = Content->Get().Parameters.MoveSpeedCmPerSec;
    const double Base = Cm(Bounds.Base);
    double Plain = 0, Hidden = 0;
    TestTrue(TEXT("Without stealth the speed is the projection"), LDControl::MoveSpeedCmPerSec(Row, 0, Bounds, Plain)
        && FMath::IsNearlyEqual(Plain, Base * (1. + Bonus->Amount / 10000.), 1e-6));
    TestTrue(TEXT("Stealthed, the penalty subtracts from the same additive ratio"), LDControl::MoveSpeedCmPerSec(Row, Effect->RatioBp, Bounds, Hidden)
        && FMath::IsNearlyEqual(Hidden, Base * (1. + (Bonus->Amount - Effect->RatioBp) / 10000.), 1e-6) && Hidden < Plain);
    double Refused = 0;
    TestFalse(TEXT("A full penalty is refused"), LDControl::MoveSpeedCmPerSec(Row, 10000, Bounds, Refused));
    // Temporary terms stay: a timed boost (active_slick_moves +30%) or the strongest slow (-30%) still applies while hidden.
    const double BoxRatio = Bonus->Amount / 10000., Penalty = Effect->RatioBp / 10000.;
    FLDAttributeContributions Boosted = Row;
    Boosted.Temporary.AdditiveRatio.Numerator = 3000;
    Boosted.Temporary.AdditiveRatio.Denominator = 10000;
    double BoostedHidden = 0;
    TestTrue(TEXT("A timed move-speed boost survives stealth"), LDControl::MoveSpeedCmPerSec(Boosted, Effect->RatioBp, Bounds, BoostedHidden)
        && FMath::IsNearlyEqual(BoostedHidden, FMath::Clamp(Base * (1. + BoxRatio + .30 - Penalty), Cm(Bounds.Min), Cm(Bounds.Max)), 1e-6)
        && BoostedHidden > Hidden);
    FLDAttributeContributions Slowed = Row;
    Slowed.Temporary.AdditiveRatio.Numerator = -3000;
    Slowed.Temporary.AdditiveRatio.Denominator = 10000;
    double SlowedHidden = 0;
    TestTrue(TEXT("A slow still applies while hidden"), LDControl::MoveSpeedCmPerSec(Slowed, Effect->RatioBp, Bounds, SlowedHidden)
        && FMath::IsNearlyEqual(SlowedHidden, FMath::Clamp(Base * (1. + BoxRatio - .30 - Penalty), Cm(Bounds.Min), Cm(Bounds.Max)), 1e-6)
        && SlowedHidden < Hidden);
    return true;
}
// #112 §9 + §8: a real combat hit through ApplyHit -> CommitControl reaches the spray zone. Damage, slow and disarm leave the
// sprayer's progress alone; a stun clears it at its instant, and the control alone (no damage) is its owner's assist contribution.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCaptureCombatInterruptTest, "Lockdown.Rules.DemoCapture.CombatHitInterrupt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCaptureCombatInterruptTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDControlRulesTests::ReadApprovedFixture(*this, Catalog)) { return false; }
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Fixture world"), World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Mode = World->SpawnActor<ALDGameMode>();
    auto* Public = World->SpawnActor<ALDGameState>();
    auto* Driver = World->SpawnActor<ALDCombatDirector>();
    auto* RivalActor = World->SpawnActor<ALDCharacter>();
    if (!Mode || !Public || !Driver || !RivalActor) { AddError(TEXT("Fixture actors")); return false; }
    Mode->bDemoWorld = Mode->bEntryAccepted = Mode->bAuthorityInitialized = true;
    Mode->GameState = Public;
    World->CopyGameState(Mode, Public);
    Mode->MatchContext = {1, FGuid::NewGuid()};
    Mode->MatchPhase = ELDMatchPhase::Development;
    Mode->RosterRevision = 1;
    Mode->Combat = Driver;
    Mode->DemoContent = Catalog;
    Mode->AuthorityStep = {Mode->MatchContext, 1, 0, 0};
    const FGuid Attacker = FGuid::NewGuid(), Controller = FGuid::NewGuid(), Rival = FGuid::NewGuid();
    for (const FGuid& Id : {Attacker, Controller, Rival})
    {
        FLDCombatEntityRecord& Record = Driver->Records.Add(Id);
        Record.Kind = ELDVictimKind::Contender;
        Record.Entity = Id;
        Record.Build.Level = 1;
    }
    const FLDCombatBodyRef RivalBody{{ELDVictimKind::Contender, Rival, 1}, FGuid::NewGuid()};
    const FLDCombatBodyRef AttackerBody{{ELDVictimKind::Contender, Attacker, 1}, FGuid::NewGuid()};
    const FLDCombatBodyRef ControllerBody{{ELDVictimKind::Contender, Controller, 1}, FGuid::NewGuid()};
    {
        FLDBodyCombatState State;
        State.Identity = RivalBody.BodyIdentity;
        State.Victim = RivalBody.Victim;
        State.Resources.MaxHpP2 = State.Resources.HpP2 = 100000;
        Driver->Bodies.Add(RivalActor, MoveTemp(State));
    }
    FString Error;
    const auto Fact = [&](ELDCaptureEventKind Kind, const FLDCombatBodyRef& Body, double Seconds, ELDCommitPhase Phase, bool Inside)
    {
        FLDCaptureEvent Event;
        Event.Kind = Kind;
        Event.Body = Body;
        Event.Phase = Phase;
        Event.bInside = Inside;
        return FLDCaptureEventTime::FromSeconds(Seconds, Event.Time, Error) && Mode->CaptureReducer.Enqueue(Event, Error);
    };
    // Open at 0 with a 100 ms spray; the rival sprays alone from 1 ms, the others stay outside.
    if (!Mode->CaptureReducer.Begin(0, 100, 5000, Error)
        || !Fact(ELDCaptureEventKind::Joined, RivalBody, .001, ELDCommitPhase::RespawnsAndWorld, true)
        || !Fact(ELDCaptureEventKind::Joined, AttackerBody, .001, ELDCommitPhase::RespawnsAndWorld, false)
        || !Fact(ELDCaptureEventKind::Joined, ControllerBody, .001, ELDCommitPhase::RespawnsAndWorld, false))
    { AddError(Error); return false; }
    World->Tick(LEVELTICK_TimeOnly, .025f);
    Mode->CaptureCircle.CaptureId = TEXT("capture_fixture");
    Mode->CaptureCircle.RadiusCm = 300;
    Mode->CaptureCircle.Ground = NewObject<UBoxComponent>(GetTransientPackage());
    const auto Hit = [&](const FLDCombatBodyRef& From, int64 BaseP2)
    {
        FLDCombatHit Out;
        Out.HitId = Out.EffectId = FGuid::NewGuid();
        Out.Target = RivalBody;
        Out.SourceBody = From;
        Out.SourceEntity = Out.CreditOwner = From.Victim.EntityId;
        Out.HitClass = ELDCombatHitClass::Ability;
        Out.BaseP2 = BaseP2;
        return Out;
    };
    // One authority step to AtMs whose Effects phase applies Hits through the real combat path.
    const auto Step = [&](int64 AtMs, const TArray<FLDCombatHit>& Hits, TFunction<bool()> Extra = nullptr,
        TFunction<bool()> Motion = nullptr)
    {
        const auto Pass = [](const FLDGameplayWritePermit&) { return true; };
        const ELDCommitReject Begin = Mode->BeginAuthorityStep(*Driver, Mode->MatchContext, AtMs);
        if (Begin != ELDCommitReject::None)
        {
            Error = FString::Printf(TEXT("the step to %lld ms was refused (%u)"), AtMs, uint32(Begin));
            return false;
        }
        return Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Motion,
                [&](const FLDGameplayWritePermit&) { return !Motion || Motion(); }, Error)
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Actions, Pass, Error)
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Effects, [&](const FLDGameplayWritePermit&)
            {
                for (const FLDCombatHit& Each : Hits) { if (!Driver->ApplyHit(Each, Error)) { return false; } }
                return !Extra || Extra();
            }, Error)
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Capture,
                [&](const FLDGameplayWritePermit& Permit) { return Mode->ExecuteCapturePhase(Permit, Error); }, Error)
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Close,
                [&](const FLDGameplayWritePermit& Permit) { return Mode->ExecuteClosePhase(Permit, Error); }, Error)
            && Mode->FinishAuthorityStep(*Driver, Error);
    };
    const FLDBodyCombatState& Sprayer = Driver->Bodies.FindChecked(RivalActor);

    FLDCombatHit Damage = Hit(AttackerBody, 1000);
    FLDCombatHit Slow = Hit(AttackerBody, 0);
    Slow.SlowBp = 3000;
    Slow.SlowDurationMs = 1000;
    FLDCombatHit Disarm = Hit(AttackerBody, 0);
    Disarm.DisarmDurationMs = 500;
    if (!TestTrue(FString::Printf(TEXT("Damage, slow and disarm step: %s"), *Error), Step(10, {Damage, Slow, Disarm}))) { return false; }
    TestTrue(TEXT("Ordinary damage, a slow and a disarm leave the spray running"),
        Sprayer.Resources.HpP2 < 100000 && Mode->CaptureReducer.GetState() == ELDCircleState::Solo
        && Mode->CaptureReducer.GetOccupier() == Rival && Mode->CaptureReducer.GetSoloSinceMs() == 1
        && Mode->CaptureReducer.GetDueAtMs() == 101);
    TestTrue(TEXT("The damage is the attacker's assist contribution"), Sprayer.AssistContributions.ContainsByPredicate(
        [&](const FLDAssistContribution& Each) { return Each.Contributor == Attacker && Each.LastAtMs == 10; }));

    FLDCombatHit Stun = Hit(ControllerBody, 0);
    Stun.StunDurationMs = 300;
    if (!TestTrue(FString::Printf(TEXT("Stun step: %s"), *Error), Step(20, {Stun}))) { return false; }
    TestTrue(TEXT("A stun from a real hit clears the spray at its instant; still alone, the rival restarts from zero"),
        Mode->CaptureReducer.GetState() == ELDCircleState::Solo && Mode->CaptureReducer.GetOccupier() == Rival
        && Mode->CaptureReducer.GetSoloSinceMs() == 20 && Mode->CaptureReducer.GetDueAtMs() == 120);
    TestTrue(TEXT("The damage-free stun is its owner's assist contribution, recorded once by the control path"),
        Sprayer.AssistContributions.FilterByPredicate(
            [&](const FLDAssistContribution& Each) { return Each.Contributor == Controller; }).Num() == 1);
    const FLDCaptureDevTally::FHolder* Holder = Mode->CaptureReducer.DevTally().Holders.Find(Rival);

    TestTrue(TEXT("The tally counts exactly one interruption"), Holder && Holder->Interrupted == 1);

    // A knockback commits through the same control path (its displacement needs the Demo map's movement; a push out of the
    // zone then reaches the reducer as an ordinary presence change, i.e. leaving the zone).
    World->Tick(LEVELTICK_TimeOnly, .025f);
    bool bPushed = false;
    const auto Push = [&]()
    {
        FLDControlApplication App;
        App.Kind = ELDControlKind::Knockback;
        App.EffectId = FGuid::NewGuid();
        App.SourceEntity = App.CreditOwner = Attacker;
        App.SourceBody = AttackerBody;
        App.DurationMs = 200;
        return Driver->CommitControl(Driver->Bodies.FindChecked(RivalActor), App, Mode->GetAuthorityNowMs(), bPushed, Error);
    };
    if (!TestTrue(FString::Printf(TEXT("Knockback step: %s"), *Error), Step(40, {}, Push))) { return false; }
    TestTrue(TEXT("A knockback clears the spray at its instant and never diminishes"), bPushed
        && Mode->CaptureReducer.GetState() == ELDCircleState::Solo && Mode->CaptureReducer.GetSoloSinceMs() == 40
        && Mode->CaptureReducer.GetDueAtMs() == 140 && Sprayer.Knockbacks.Num() == 1 && Sprayer.Knockbacks[0].ExpiresAtMs == 240);
    Holder = Mode->CaptureReducer.DevTally().Holders.Find(Rival);
    TestTrue(TEXT("The tally counts the second interruption"), Holder && Holder->Interrupted == 2);
    // Pushed out of the zone: the leave clears the progress like any other exit.
    const auto Leave = [&]() { return Fact(ELDCaptureEventKind::Presence, RivalBody, .045, ELDCommitPhase::Motion, false); };
    if (!TestTrue(FString::Printf(TEXT("Leave step: %s"), *Error), Step(50, {}, nullptr, Leave))) { return false; }
    TestTrue(TEXT("Leaving the zone empties the flagpole and nothing of the spray is kept"),
        Mode->CaptureReducer.GetState() == ELDCircleState::Empty && !Mode->CaptureReducer.GetOccupier().IsValid());
    return true;
}
// #112 review fixes: every flight of a volley is observed under its own identity while sharing its effect, and a
// leaving source takes the vulnerability its roots installed with it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoMechanicsEffectLifetimesTest, "Lockdown.Rules.DemoMechanics.EffectLifetimes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoMechanicsEffectLifetimesTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDControlRulesTests::ReadApprovedFixture(*this, Catalog)) { return false; }
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Fixture world"), World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Mode = World->SpawnActor<ALDGameMode>();
    auto* Driver = World->SpawnActor<ALDCombatDirector>();
    auto* ShooterActor = World->SpawnActor<ALDCharacter>();
    auto* VictimActor = World->SpawnActor<ALDCharacter>();
    if (!Mode || !Driver || !ShooterActor || !VictimActor) { AddError(TEXT("Fixture actors")); return false; }
    Mode->Combat = Driver;
    Mode->DemoContent = Catalog;
    World->CopyGameState(Mode, nullptr);
    const FLDCombatBodyRef ShooterRef{{ELDVictimKind::Contender, FGuid::NewGuid(), 1}, FGuid::NewGuid()};
    const FLDCombatBodyRef VictimRef{{ELDVictimKind::Contender, FGuid::NewGuid(), 1}, FGuid::NewGuid()};
    for (const auto& Pair : {TPair<ALDCharacter*, FLDCombatBodyRef>(ShooterActor, ShooterRef), TPair<ALDCharacter*, FLDCombatBodyRef>(VictimActor, VictimRef)})
    {
        FLDBodyCombatState State;
        State.Identity = Pair.Value.BodyIdentity;
        State.Victim = Pair.Value.Victim;
        State.Resources.MaxHpP2 = State.Resources.HpP2 = 100000;
        Driver->Bodies.Add(Pair.Key, MoveTemp(State));
    }
    FString Error;

    // A three-shot volley 150 ms apart: the first flight is released, the later ones are scheduled from it.
    FLDCombatSourceRef Source;
    Source.Kind = ELDCombatSourceKind::Learned;
    Source.DefinitionId = TEXT("active_barrage_rant");
    const FGuid EffectId = FGuid::NewGuid();
    FLDCombatProjectile First;
    First.EffectId = EffectId;
    First.FlightId = FGuid::NewGuid();
    First.SourceBody = ShooterRef;
    First.SourceEntity = First.CreditOwner = ShooterRef.Victim.EntityId;
    First.Source = Source;
    First.Direction = FVector(1, 0, 0);
    First.SpeedCmPerSec = 2000;
    First.RangeCm = 500;
    Driver->Projectiles.Add(First);
    FLDDemoRankDefinition Rank;
    FLDDemoEffectDefinition& Volley = Rank.Effects.AddDefaulted_GetRef();
    Volley.Opcode = uint8(ELDDemoEffectOp::Volley);
    Volley.Count = 3;
    Volley.IntervalMs = 150;
    FLDBodyCombatState& Shooter = Driver->Bodies.FindChecked(ShooterActor);
    if (!TestTrue(FString::Printf(TEXT("The volley is scheduled: %s"), *Error), Driver->ReleaseMechanics(*ShooterActor, Shooter, Source, Rank,
        nullptr, FVector::ZeroVector, FVector::ZeroVector, EffectId, 1, 0, Error))) { return false; }
    Mode->AuthorityNowMs = 150;
    if (!TestTrue(FString::Printf(TEXT("The second shot leaves: %s"), *Error), Driver->AdvanceMechanics(150, Error))) { return false; }
    TArray<FLDObservedProjectileFact> Flights;
    TestTrue(FString::Printf(TEXT("Two flights of one volley are observed at once: %s"), *Error),
        Driver->ReadObservedProjectiles(Flights, Error) && Flights.Num() == 2 && Flights[0].Key != Flights[1].Key
        && Flights[0].Key.IsValid() && Flights[1].Key.IsValid());
    TestTrue(TEXT("Both flights keep the release's shared effect identity"), Driver->Projectiles.Num() == 2
        && Driver->Projectiles[0].EffectId == EffectId && Driver->Projectiles[1].EffectId == EffectId);
    TestTrue(TEXT("Every scheduled shot already holds its own fixed flight identity"), Driver->VolleyShots.Num() == 1
        && Driver->VolleyShots[0].Template.FlightId.IsValid() && Driver->VolleyShots[0].Template.FlightId != Flights[0].Key
        && Driver->VolleyShots[0].Template.FlightId != Flights[1].Key);

    // active_binding_snare Lv3 from the shooter: when he leaves, his root and its vulnerability both end.
    FLDBodyCombatState& Victim = Driver->Bodies.FindChecked(VictimActor);
    const FGuid Snare = FGuid::NewGuid(), OtherRoot = FGuid::NewGuid(), Bystander = FGuid::NewGuid();
    Victim.Roots.Add({Snare, ShooterRef.Victim.EntityId, 2000});
    Victim.Vulnerabilities.Add({Snare, 1500, 2000});
    Victim.Roots.Add({OtherRoot, Bystander, 2000});
    Victim.Vulnerabilities.Add({OtherRoot, 1500, 2000});
    Driver->CancelEntityEffects(ShooterRef.Victim.EntityId);
    TestTrue(TEXT("The leaving source's root and its vulnerability are gone; another source's stay"),
        Victim.Roots.Num() == 1 && Victim.Roots[0].EffectId == OtherRoot
        && Victim.Vulnerabilities.Num() == 1 && Victim.Vulnerabilities[0].EffectId == OtherRoot);
    TestTrue(TEXT("Its flights are cancelled too"), Driver->Projectiles.IsEmpty() && Driver->VolleyShots.IsEmpty());
    return true;
}
#endif
