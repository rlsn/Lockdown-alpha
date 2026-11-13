#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatKeywords.h"
#include "Combat/LDCombatElements.h"
#include "Combat/LDCombatControlRules.h"
#include "Combat/LDCombatMechanics.h"
#include "Combat/LDTacticalRules.h"
#include "Combat/LDDashContactRules.h"
#include "Combat/LDCombatSourceRules.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Character/LDNativeMotionTypes.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Region/LDMapDefinition.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapRegistry.h"
#include "Vision/LDVisionDirector.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/LDTestApprovedFixture.h"

#if WITH_DEV_AUTOMATION_TESTS

// #147 build v7 combat primitives (docs/systems/COMBAT_AND_GROWTH.md, "构筑 v7 战斗原语") and the #198 build safe window.
// The installed base components that feed the trigger handlers are covered by LDComponentCombatTests.

struct FLDBuildPrimitivesAccess
{
    static TMap<TWeakObjectPtr<ALDCharacter>, FLDBodyCombatState>& Bodies(ALDCombatDirector& Driver) { return Driver.Bodies; }
    static TMap<FGuid, FLDCombatEntityRecord>& Records(ALDCombatDirector& Driver) { return Driver.Records; }
    static TArray<FLDCombatHit>& PendingHits(ALDCombatDirector& Driver) { return Driver.PendingHits; }
    static TArray<FLDCombatZone>& Zones(ALDCombatDirector& Driver) { return Driver.Zones; }
    static bool ApplyHit(ALDCombatDirector& Driver, const FLDCombatHit& Hit, FString& Error) { return Driver.ApplyHit(Hit, Error); }
    static bool AdvanceBurns(ALDCombatDirector& Driver, int64 NowMs, FString& Error) { return Driver.AdvanceBurns(NowMs, Error); }
    static bool LandBasic(ALDCombatDirector& Driver, ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDCombatAction& Action,
        FString& Error)
    { return Driver.LandBasic(Actor, Body, Action, Error); }
    static bool ApplyCleanses(ALDCombatDirector& Driver, FLDBodyCombatState& Body, const FLDDemoAbilityValues& Values, int64 NowMs)
    { return Driver.ApplyCleanses(Body, Values, NowMs); }
    static void ExpireStatuses(ALDCombatDirector& Driver, int64 NowMs) { Driver.ExpireStatuses(NowMs); }
    static void CancelEntityEffects(ALDCombatDirector& Driver, const FGuid& Entity) { Driver.CancelEntityEffects(Entity); }
    static void CancelChannel(ALDCombatDirector& Driver, FLDBodyCombatState& Body) { Driver.CancelChannel(Body); }
    static void KeywordStatuses(const ALDCombatDirector& Driver, const FLDBodyCombatState& Body, int64 NowMs, TArray<FLDObservedStatus>& Out)
    { Driver.AppendKeywordStatuses(Body, NowMs, Out); }
    static void MarkBuildUnsafe(ALDCombatDirector& Driver, const FGuid& Entity, int64 NowMs) { Driver.MarkBuildUnsafe(Entity, NowMs); }
    static bool ReleaseMechanics(ALDCombatDirector& Driver, FLDBodyCombatState& Body, const FLDCombatSourceRef& Source,
        const FLDDemoAbilityValues& Values, const FVector& Origin, const FVector& Point, FString& Error)
    {
        FLDCombatActiveTerms Terms;
        Terms.Values = Values;
        return Driver.ReleaseMechanics(Body, Source, Terms, Origin, Point, FGuid::NewGuid(), 0, 0, MakeShared<FLDCombatCastLedger>(), Error);
    }
    static void Bind(ALDGameMode& Mode, ALDCombatDirector& Driver, const TSharedPtr<const FLDDemoContentSnapshot>& Catalog)
    {
        Mode.Combat = &Driver;
        Mode.DemoContent = Catalog;
    }
    static void SetNow(ALDGameMode& Mode, int64 NowMs) { Mode.AuthorityNowMs = NowMs; }
    static bool Delivery(ALDCombatDirector& Driver, FLDBodyCombatState& Body, const FLDDemoAbilityValues& Values,
        const FLDDemoTargetDefinition& Target, const FLDCombatAction& Action, FLDCombatHit& Hit, int64 Now, bool& Handled, FString& Error)
    {
        FLDCombatActiveTerms Terms;
        Terms.Values = Values;
        Terms.Target = Target;
        return Driver.StartSkillDelivery(Body, Terms, Action, Hit, Now, Handled, Error);
    }
    static TArray<FLDCombatGroundEffect>& Ground(ALDCombatDirector& Driver) { return Driver.GroundEffects; }
    // #198 elements.
    static TArray<FLDCombatFog>& Fogs(ALDCombatDirector& Driver) { return Driver.Fogs; }
    static TArray<FLDCombatReactionFact>& Reactions(ALDCombatDirector& Driver) { return Driver.ReactionFacts; }
    static bool AdvanceElements(ALDCombatDirector& Driver, int64 NowMs, FString& Error) { return Driver.AdvanceElements(NowMs, Error); }
    static bool ReleaseBrace(ALDCombatDirector& Driver, FLDBodyCombatState& Body, const FLDDemoEffectDefinition& Effect, int64 NowMs,
        FString& Error)
    { return Driver.ReleaseBrace(Body, Effect, FLDCombatSourceRef(), FGuid::NewGuid(), 1, NowMs, Error); }
    static bool GrantShield(ALDCombatDirector& Driver, FLDBodyCombatState& Body, int64 AmountP2, int64 NowMs, FString& Error)
    { return Driver.GrantShieldTo(Body, AmountP2, 3000, FGuid::NewGuid(), Body.Victim.EntityId, NowMs, Error); }
    static bool Heal(ALDCombatDirector& Driver, FLDBodyCombatState& Body, int64 AmountP2) { return Driver.HealBody(Body, AmountP2); }
    // #198 B components.
    static bool ResolveDashCompletions(ALDCombatDirector& Driver, int64 NowMs, FString& Error) { return Driver.ResolveDashCompletions(NowMs, Error); }
    static bool ApplyComponentElement(const ALDCombatDirector& Driver, FLDCombatHit& Hit, ELDDemoElement Element,
        const FLDCombatSourceRef& Component, FString& Error)
    { return Driver.ApplyComponentElement(Hit, Element, Component, Error); }
    static bool Collect(const ALDCombatDirector& Driver, const FLDAttributeProjection& Projection, ELDDemoEffectOp Op,
        TArray<FLDPassiveEffectRef>& Out, FString& Error)
    { return Driver.CollectPassiveEffects(Projection, Op, Out, Error); }
    // #198 F mods and connectors.
    static bool Compile(const ALDCombatDirector& Driver, const FLDAttributeProjection& Projection, const FLDCombatSourceRef& Source,
        FLDCombatActiveTerms& Terms, FString& Error)
    { return Driver.CompileActive(Projection, Source, Terms, Error); }
    static bool ReleaseTerms(ALDCombatDirector& Driver, FLDBodyCombatState& Body, const FLDCombatSourceRef& Source,
        const FLDCombatActiveTerms& Terms, const FVector& Origin, const FVector& Point, FString& Error)
    { return Driver.ReleaseMechanics(Body, Source, Terms, Origin, Point, FGuid::NewGuid(), 0, 0, MakeShared<FLDCombatCastLedger>(), Error); }
    static bool DeliverTerms(ALDCombatDirector& Driver, FLDBodyCombatState& Body, const FLDCombatActiveTerms& Terms,
        const FLDCombatAction& Action, FLDCombatHit& Hit, int64 Now, bool& Handled, FString& Error)
    { return Driver.StartSkillDelivery(Body, Terms, Action, Hit, Now, Handled, Error); }
    static TArray<FLDCombatChannel>& Channels(ALDCombatDirector& Driver) { return Driver.Channels; }
    static TArray<FLDCombatLockedLedger>& Locked(ALDCombatDirector& Driver) { return Driver.LockedLedgers; }
    static bool AdvanceWorkshop(ALDCombatDirector& Driver, int64 NowMs, FString& Error) { return Driver.AdvanceWorkshop(NowMs, Error); }
    static bool Watch(ALDCombatDirector& Driver, ALDCharacter& Target, FLDBodyCombatState& Body, const FLDCombatHit& Hit, int64 NowMs,
        FString& Error)
    { return Driver.WatchKnockbackImpact(Target, Body, Hit, NowMs, Error); }
    static bool Impact(ALDCombatDirector& Driver, FLDBodyCombatState& Body, const FLDCombatKnockbackImpact& Impact, bool bBody,
        const ALDCharacter* Blocker, int64 NowMs, FString& Error)
    { return Driver.ResolveKnockbackImpact(Body, Impact, bBody, Blocker, NowMs, Error); }
    // #198 H combat event correctness: the real release, delivery and Effects entries.
    static bool BuildPayload(const ALDCombatDirector& Driver, const FLDBodyCombatState& Body, const FLDCombatSourceRef& Source,
        const FLDCombatActiveTerms& Terms, const FVector& Foot, FLDCombatHit& Out, FString& Error)
    { return Driver.BuildReleasePayload(Body, Source, Terms, Foot, FGuid::NewGuid(), 1, 0, MakeShared<FLDCombatCastLedger>(), Out, Error); }
    static bool DeliverPayload(ALDCombatDirector& Driver, const ALDCharacter& Actor, const FLDBodyCombatState& Body,
        const FLDCombatActiveTerms& Terms, const FLDCombatAction& Action, const FLDCombatHit& Template, const FLDCombatBodyRef& Unit,
        const FVector& UnitFoot, const FVector& Foot, int64 NowMs, FString& Error)
    { return Driver.DeliverReleasePayload(Actor, Body, Terms, Action, Template, Unit, UnitFoot, Foot, {}, NowMs, Error); }
    static bool ApplyPendingHits(ALDCombatDirector& Driver, int64 NowMs, FString& Error) { return Driver.ApplyPendingHits(NowMs, Error); }
    static bool AdvanceMechanics(ALDCombatDirector& Driver, int64 NowMs, FString& Error) { return Driver.AdvanceMechanics(NowMs, Error); }
    static int64 NextWorkshopDue(const ALDCombatDirector& Driver, int64 NowMs) { return Driver.NextWorkshopDueMs(NowMs); }
    static bool MakeEmpower(const ALDCombatDirector& Driver, const FLDBodyCombatState& Body, const FLDCombatSourceRef& Source,
        const FLDDemoEffectDefinition& Effect, int64 NowMs, FLDCombatEmpower& Out, FString& Error)
    { return Driver.MakeEmpower(Body, Source, Effect, FGuid::NewGuid(), NowMs, Out, Error); }
    static bool AdvanceSkillContent(ALDCombatDirector& Driver, int64 NowMs, FString& Error) { return Driver.AdvanceSkillContent(NowMs, Error); }
    // B15: the amplification a release takes and the previous-release record ReleaseAbility keeps.
    static bool AlternateAmp(const ALDCombatDirector& Driver, const FLDBodyCombatState& Body, const FLDCombatSourceRef& Source,
        int64 NowMs, int64& OutBp, FString& Error)
    { return Driver.AlternateAmpBp(Body, Source, NowMs, OutBp, Error); }
    static void NoteRelease(FLDBodyCombatState& Body, const FLDCombatSourceRef& Source, int64 NowMs)
    { ALDCombatDirector::NoteActiveRelease(Body, Source, NowMs); }
    static bool AdvanceProjectiles(ALDCombatDirector& Driver, FString& Error) { return Driver.AdvanceProjectiles(Error); }
    static TArray<FLDCombatProjectile>& Projectiles(ALDCombatDirector& Driver) { return Driver.Projectiles; }
    // C13: the cut ResolveDashContacts applies at a hosted dash's contact (and OnNativeDirectHit at a host hit).
    static bool HasteOther(ALDCombatDirector& Driver, FLDBodyCombatState& Body, const FLDCombatSourceRef& Host, const FGuid& EffectId,
        int64 NowMs, FString& Error)
    { return Driver.HasteOtherActives(Body, Host, EffectId, NowMs, Error); }
    static bool ReleaseAbility(ALDCombatDirector& Driver, ALDCharacter& Actor, FLDBodyCombatState& Body,
        const FLDCombatAction& Action, ELDCombatReject& Reject, FString& Error)
    { return Driver.ReleaseAbility(Actor, Body, Action, Reject, Error); }
    static void CaptureSkillMotion(ALDCombatDirector& Driver, TSharedPtr<const FLDNativeMotionBatch> Batch)
    { Driver.CaptureSkillMotion(MoveTemp(Batch)); }
    static bool ResolveDashContacts(ALDCombatDirector& Driver, int64 NowMs, FString& Error)
    { return Driver.ResolveDashContacts(NowMs, Error); }
    static bool NoteInstall(ALDCombatDirector& Driver, FLDCombatEntityRecord& Record, const FLDAttributeProjection& Previous,
        FLDBodyCombatState* Body, int64 NowMs, FString& Error)
    { return Driver.NoteWorkshopInstall(Record, Previous, Body, NowMs, Error); }
    // The registered sight of the approved geometry for an isolated world: the registry revision and the observation
    // owner's sight, as a match's world binding would give them.
    static bool GiveSight(ALDGameMode& Mode, UWorld& World, const FLDDemoWorldGeometry& Geometry, const FLDWorldRevision& Revision,
        const FLDDemoRational& Radius, FString& Error)
    {
        ULDMapRegistry* Registry = World.GetSubsystem<ULDMapRegistry>();
        ALDVisionDirector* Vision = World.SpawnActor<ALDVisionDirector>();
        if (!Registry || !Vision) { Error = TEXT("The sight world needs its map registry and observation owner."); return false; }
        Registry->WorldRevision = Revision;
        if (!Vision->Sight.Build(Geometry, Revision, Radius, Error)) { return false; }
        Mode.Vision = Vision;
        return true;
    }
    static const FLDSightGeometry& Sight(const ALDVisionDirector& Vision) { return Vision.Sight; }
};

namespace LDBuildPrimitivesTests
{
using FAccess = FLDBuildPrimitivesAccess;
// One isolated world with a real Combat director over the approved fixture.
struct FFixture
{
    UWorld* World = nullptr;
    ALDGameMode* Mode = nullptr;
    ALDCombatDirector* Driver = nullptr;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    TMap<FGuid, ALDCharacter*> Actors;
    // Sight worlds (Init with bSight): a flat open foot point of the approved geometry that Place offsets from (up to 6.75 m).
    FVector Origin = FVector::ZeroVector;
    LDDemoWorld::FTerrain Terrain;

    ~FFixture()
    {
        if (World)
        {
            World->DestroyWorld(false);
            GEngine->DestroyWorldContext(World);
        }
    }

    // bSight: the world is bound as the Demo world (its map registry exists) with the registered sight of the approved
    // geometry, so area, line, fog and projectile queries run as in a match; bodies are placed with Place.
    bool Init(FAutomationTestBase& Test, bool bSight = false)
    {
        if (!LDTestApprovedFixture::Read(Test, Catalog)) { return false; }
        const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
            .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
        UPackage* Package = nullptr;
        FName WorldName = NAME_None;
        if (bSight)
        {
            // A PIE-prefixed name of the Demo world package selects its map registry without touching the saved map.
            const FString Short = FString::Printf(TEXT("UEDPIE_%u_%s"), (FGuid::NewGuid().A & 0xFFFF) + 100u,
                *FPackageName::GetShortName(LDDemoEntry::WorldMap));
            Package = CreatePackage(*(FPackageName::GetLongPackagePath(LDDemoEntry::WorldMap) / Short));
            WorldName = FName(*Short);
        }
        World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, Package, true, ERHIFeatureLevel::Num, &Values);
        if (!World) { Test.AddError(TEXT("Fixture world")); return false; }
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        Mode = World->SpawnActor<ALDGameMode>();
        Driver = World->SpawnActor<ALDCombatDirector>();
        if (!Mode || !Driver) { Test.AddError(TEXT("Fixture actors")); return false; }
        FAccess::Bind(*Mode, *Driver, Catalog);
        World->CopyGameState(Mode, nullptr);
        Now(1000);
        return !bSight || InitSight(Test);
    }

    bool InitSight(FAutomationTestBase& Test)
    {
        TSharedPtr<const FLDDemoContentSnapshot> Runtime;
        TSharedPtr<const FLDDemoWorldGeometry> Geometry;
        FString Error;
        const auto* Definition = LoadObject<ULDMapDefinition>(nullptr, *(FString(LDDemoEntry::WorldDefinition) + TEXT(".DA_DemoWorld")));
        if (!Definition || !LDDemoEntry::LoadRuntimeContent(Runtime, Error) || !Definition->Read(*Runtime, Geometry, Error))
        {
            Test.AddError(TEXT("Sight fixture geometry: ") + Error);
            return false;
        }
        FLDWorldRevision Revision;
        Revision.MapId = FName(*Geometry->MapId);
        Revision.LayoutRevision = Geometry->LayoutRevision;
        Revision.GeometryRevision = Geometry->GeometryRevision;
        if (!Terrain.Build(*Geometry, Error)
            || !FAccess::GiveSight(*Mode, *World, *Geometry, Revision, Runtime->Get().Parameters.VisionRadiusCm, Error))
        {
            Test.AddError(TEXT("Sight fixture binding: ") + Error);
            return false;
        }
        const ALDVisionDirector& Vision = *Mode->Vision;
        // The first 13.5 x 13.5 m patch of one tier in the approved world, every sampled pair within 7 m in clear sight.
        int32 MixedTiers = 0, Blocked = 0, Other = 0;
        const auto Open = [&](const FVector2D& At, double& OutZ)
        {
            TArray<FVector> Feet;
            FVector Normal;
            Terrain.Ground(At, OutZ, Normal);
            for (int32 X = -3; X <= 3; ++X)
            {
                for (int32 Y = -3; Y <= 3; ++Y)
                {
                    const FVector2D Point = At + FVector2D(X * 225., Y * 225.);
                    double Z = 0;
                    Terrain.Ground(Point, Z, Normal);
                    if (Terrain.Tier(Point) != Terrain.Tier(At)) { ++MixedTiers; return false; }
                    Feet.Emplace(Point.X, Point.Y, Z);
                }
            }
            // Every pair within the sight radius sees each other (no body interaction of these tests reaches farther).
            for (const FVector& From : Feet)
            {
                for (const FVector& To : Feet)
                {
                    const ELDSightResult Seen = FVector::Dist2D(From, To) <= 700. ? Vision.TraceSight(From, To, Revision) : ELDSightResult::Visible;
                    if (Seen != ELDSightResult::Visible) { ++(Seen == ELDSightResult::Blocked ? Blocked : Other); return false; }
                }
            }
            return true;
        };
        const FVector2D Low = FAccess::Sight(Vision).MinCm() + FVector2D(700.), High = FAccess::Sight(Vision).MaxCm() - FVector2D(700.);
        for (double X = Low.X; X <= High.X; X += 300.)
        {
            for (double Y = Low.Y; Y <= High.Y; Y += 300.)
            {
                double Z = 0;
                if (Open(FVector2D(X, Y), Z)) { Origin = FVector(X, Y, Z); return true; }
            }
        }
        Test.AddError(FString::Printf(TEXT("Sight fixture: the approved world has no open patch (mixed tiers %d, blocked %d, other %d)"),
            MixedTiers, Blocked, Other));
        return false;
    }

    // Puts a registered body's foot on the ground at Origin + (X, Y) of the open patch.
    void Place(const FLDCombatBodyRef& Ref, double X, double Y)
    {
        ALDCharacter& Body = Actor(Ref);
        const FVector2D At(Origin.X + X, Origin.Y + Y);
        double Z = Origin.Z;
        FVector Normal;
        if (Terrain.IsValid()) { Terrain.Ground(At, Z, Normal); }
        Body.SetActorLocation(FVector(At.X, At.Y, Z + Body.GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    }

    FVector Foot(const FLDCombatBodyRef& Ref) { return ALDCombatDirector::FootOf(Actor(Ref)); }

    void Now(int64 Ms) { FAccess::SetNow(*Mode, Ms); }

    // A registered body (a contender also gets its Combat record, so assists can credit it).
    FLDCombatBodyRef AddBody(ELDVictimKind Kind = ELDVictimKind::Contender, int64 HPP2 = 100000)
    {
        ALDCharacter* Actor = World->SpawnActor<ALDCharacter>();
        const FLDCombatBodyRef Ref{{Kind, FGuid::NewGuid(), 1}, FGuid::NewGuid()};
        FLDBodyCombatState State;
        State.Identity = Ref.BodyIdentity;
        State.Victim = Ref.Victim;
        State.Resources.MaxHpP2 = State.Resources.HpP2 = HPP2;
        FAccess::Bodies(*Driver).Add(Actor, MoveTemp(State));
        Actors.Add(Ref.Victim.EntityId, Actor);
        if (Kind == ELDVictimKind::Contender)
        {
            FLDCombatEntityRecord& Record = FAccess::Records(*Driver).Add(Ref.Victim.EntityId);
            Record.Kind = Kind;
            Record.Entity = Ref.Victim.EntityId;
            Record.Build.Level = 1;
        }
        return Ref;
    }

    FLDBodyCombatState& Body(const FLDCombatBodyRef& Ref) { return FAccess::Bodies(*Driver).FindChecked(Actors.FindChecked(Ref.Victim.EntityId)); }
    ALDCharacter& Actor(const FLDCombatBodyRef& Ref) { return *Actors.FindChecked(Ref.Victim.EntityId); }
    int64 LastUnsafe(const FLDCombatBodyRef& Ref) { return FAccess::Records(*Driver).FindChecked(Ref.Victim.EntityId).LastBuildUnsafeAtMs; }

    // A magical ability hit of BaseP2 from one body to another.
    static FLDCombatHit Hit(const FLDCombatBodyRef& From, const FLDCombatBodyRef& To, int64 BaseP2)
    {
        FLDCombatHit Out;
        Out.HitId = Out.EffectId = FGuid::NewGuid();
        Out.Target = To;
        Out.SourceBody = From;
        Out.SourceEntity = From.Victim.EntityId;
        Out.CreditOwner = From.Victim.Kind == ELDVictimKind::Contender ? From.Victim.EntityId : FGuid();
        Out.HitClass = ELDCombatHitClass::Ability;
        Out.DamageType = ELDDemoDamageType::Magical;
        Out.BaseP2 = BaseP2;
        return Out;
    }

    bool Apply(const FLDCombatHit& Hit, FString& Error) { return FAccess::ApplyHit(*Driver, Hit, Error); }

    // Applies every queued hit (and what those queue) like one Effects phase.
    bool Flush(FString& Error)
    {
        TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*Driver);
        for (int32 Index = 0; Index < Pending.Num(); ++Index)
        {
            const FLDCombatHit Each = Pending[Index];
            if (!Apply(Each, Error)) { return false; }
        }
        Pending.Reset();
        return true;
    }

    bool HasContribution(const FLDCombatBodyRef& Victim, const FLDCombatBodyRef& From)
    {
        return Body(Victim).AssistContributions.ContainsByPredicate([&From](const FLDAssistContribution& Row)
        { return Row.Contributor == From.Victim.EntityId; });
    }

    static const FLDObservedStatus* FindStatus(const TArray<FLDObservedStatus>& Statuses, ELDDemoStatusId Id)
    {
        return Statuses.FindByPredicate([Id](const FLDObservedStatus& Row) { return Row.StatusId == uint8(Id); });
    }
};

FLDKeywordTerms Terms()
{
    FLDKeywordTerms Out;
    Out.BreachPerStackP2 = 300;
    Out.BreachMaxStacks = 5;
    Out.BreachMs = 4000;
    Out.BurnPerSecP2 = 300;
    Out.BurnAPRatioBp = 300;
    Out.BurnTickMs = 500;
    Out.BurnMs = 3000;
    Out.BurnMaxStacks = 5;
    Out.LifestealCapBp = 3500;
    return Out;
}

// A level-1 layer; its AP sets the per-second damage (3 + 3% AP).
FLDCombatBurnLayer Layer(const FGuid& Source, int64 SourceAPP2)
{
    FLDCombatBurnLayer Out;
    Out.SourceEntity = Out.CreditOwner = Source;
    Out.SourceBody = {{ELDVictimKind::Contender, Source, 1}, FGuid::NewGuid()};
    Out.Level = 1;
    Out.SourceAPP2 = SourceAPP2;
    return Out;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildKeywordRulesTest, "Lockdown.Rules.DemoBuildPrimitives.KeywordRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildKeywordRulesTest::RunTest(const FString& Parameters)
{
    using namespace LDCombatKeywords;
    using namespace LDBuildPrimitivesTests;
    const FLDKeywordTerms T = Terms();
    // D12 breach: one shared group, fixed -3 per stack, five stacks, four seconds refreshed whole.
    FLDCombatSharedBreach Breach;
    TestFalse(TEXT("No stacks is no breach"), ApplyBreach(Breach, 0, 1000, T));
    TestTrue(TEXT("Two stacks land"), ApplyBreach(Breach, 2, 1000, T) && Breach.Stacks == 2 && Breach.ExpiresAtMs == 5000);
    TestTrue(TEXT("Any source adds to the same group, capped at five, and restarts it"),
        ApplyBreach(Breach, 4, 2000, T) && Breach.Stacks == 5 && Breach.ExpiresAtMs == 6000);
    TestTrue(TEXT("A full group only restarts"), ApplyBreach(Breach, 1, 3000, T) && Breach.Stacks == 5 && Breach.ExpiresAtMs == 7000);
    TestEqual(TEXT("Each stack takes 3 armor and magic resist"), BreachFlatP2(Breach, 3000, T), int64(1500));
    TestEqual(TEXT("A lapsed group takes nothing (half-open)"), BreachFlatP2(Breach, 7000, T), int64(0));
    TestTrue(TEXT("and ends"), ExpireBreach(Breach, 7000) && Breach.Stacks == 0);
    TestTrue(TEXT("A new stack after a lapse starts over"), ApplyBreach(Breach, 1, 8000, T) && Breach.Stacks == 1);
    TestEqual(TEXT("A stack takes its flat off the resistance"), BreachedResistanceP2(2000, Breach, 8000, T), int64(1700));
    TestEqual(TEXT("and never drives a resistance below zero"), BreachedResistanceP2(200, Breach, 8000, T), int64(0));

    // #198 fire: every layer keeps its applier, snapshot and own three-second timer; same-instant ticks of one applier merge.
    const FGuid A = FGuid::NewGuid(), B = FGuid::NewGuid();
    TestEqual(TEXT("A level-1 layer burns 3 + 3% AP per second"), BurnLayerPerSecondP2(1, 5000, T), int64(300 + 150));
    FLDCombatBurn Burn;
    TestTrue(TEXT("Two layers of A"), ApplyBurn(Burn, Layer(A, 0), 2, 1000, T) && Burn.Layers.Num() == 2
        && Burn.Layers[0].NextTickAtMs == 1500 && Burn.Layers[0].ExpiresAtMs == 4000);
    TestTrue(TEXT("A layer of B joins on its own timer; A's layers keep theirs"),
        ApplyBurn(Burn, Layer(B, 10000), 1, 1200, T) && Burn.Layers.Num() == 3 && Burn.Layers[2].NextTickAtMs == 1700
        && Burn.Layers[2].ExpiresAtMs == 4200 && Burn.Layers[0].ExpiresAtMs == 4000 && BurnExpiresAtMs(Burn) == 4200);
    TArray<FLDBurnTickShare> Shares;
    int64 TickAt = 0;
    TestTrue(TEXT("The earliest tick merges A's two layers into one share"), PopBurnTick(Burn, 1500, T, TickAt, Shares)
        && TickAt == 1500 && Shares.Num() == 1 && Shares[0].SourceEntity == A && Shares[0].DamageP2 == 300);
    TestTrue(TEXT("B ticks on its own instant"), !PopBurnTick(Burn, 1600, T, TickAt, Shares)
        && PopBurnTick(Burn, 1700, T, TickAt, Shares) && Shares.Num() == 1 && Shares[0].SourceEntity == B && Shares[0].DamageP2 == 300);
    BurnRemainingShares(Burn, T, Shares);
    TestTrue(TEXT("An early settle pays every tick still owed, per applier"), Shares.Num() == 2 && Shares[0].SourceEntity == A
        && Shares[0].DamageP2 == 1500 && Shares[1].SourceEntity == B && Shares[1].DamageP2 == 1500);
    TestTrue(TEXT("At the cap each new layer replaces the one that ends first"), ApplyBurn(Burn, Layer(B, 10000), 3, 1300, T)
        && Burn.Layers.Num() == 5
        && Burn.Layers.FilterByPredicate([&A](const FLDCombatBurnLayer& Row) { return Row.SourceEntity == A; }).Num() == 1);
    TestEqual(TEXT("A leaving applier takes its layers"), RemoveBurnSource(Burn, B), 4);
    FLDCombatBurn Fresh;
    TestTrue(TEXT("A new burn"), ApplyBurn(Fresh, Layer(A, 0), 1, 0, T));
    int32 Ticks = 0;
    int64 LastTick = 0;
    while (PopBurnTick(Fresh, 3000, T, TickAt, Shares)) { ++Ticks; LastTick = TickAt; }
    TestTrue(TEXT("ticks six times, the last on its deadline, and then ends"), Ticks == 6 && LastTick == 3000 && Fresh.Layers.IsEmpty());
    TestTrue(TEXT("A layer lasts through its deadline"), ApplyBurn(Fresh, Layer(A, 0), 1, 0, T) && BurnActive(Fresh, 3000)
        && !ExpireBurn(Fresh, 3000));
    TestTrue(TEXT("and ends after it"), !BurnActive(Fresh, 3001) && ExpireBurn(Fresh, 3001) && Fresh.Layers.IsEmpty());

    // D22 lifesteal cap.
    TestEqual(TEXT("Lifesteal adds up to the 35% cap"), CapLifestealBp(6000, T), int64(3500));
    TestEqual(TEXT("below the cap it is kept"), CapLifestealBp(2000, T), int64(2000));

    // Charged empowerment.
    TArray<FLDCombatEmpower> Armed;
    FLDCombatEmpower Charged;
    Charged.Source.Kind = ELDCombatSourceKind::Learned;
    Charged.Source.DefinitionId = TEXT("active_triple_combo");
    Charged.Charges = 3;
    Charged.AmountP2 = 2000;
    Charged.ExpiresAtMs = 5000;
    LDCombatControl::ArmEmpower(Armed, Charged);
    TArray<FLDCombatEmpower> Taken;
    int64 SumP2 = 0;
    TestTrue(TEXT("The first charge"), LDCombatControl::ConsumeEmpowers(Armed, 1000, Taken, SumP2) && SumP2 == 2000
        && Armed.Num() == 1 && Armed[0].Charges == 2);
    TestTrue(TEXT("the second"), LDCombatControl::ConsumeEmpowers(Armed, 1100, Taken, SumP2) && SumP2 == 2000
        && Armed.Num() == 1 && Armed[0].Charges == 1);
    TestTrue(TEXT("and the last end the record"), LDCombatControl::ConsumeEmpowers(Armed, 1200, Taken, SumP2)
        && Taken.Num() == 1 && Taken[0].Charges == 0 && Armed.IsEmpty());
    TestTrue(TEXT("then nothing is left"), LDCombatControl::ConsumeEmpowers(Armed, 1300, Taken, SumP2) && Taken.IsEmpty() && SumP2 == 0);
    Charged.ExpiresAtMs = 1000;
    LDCombatControl::ArmEmpower(Armed, Charged);
    TestTrue(TEXT("An expired empowerment is dropped untaken"), LDCombatControl::ConsumeEmpowers(Armed, 1000, Taken, SumP2)
        && Taken.IsEmpty() && Armed.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildDamagePoolTest, "Lockdown.Rules.DemoBuildPrimitives.DamagePool",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildDamagePoolTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(*this, Catalog)) { return false; }
    const auto Evaluate = [&Catalog](ELDCombatHitClass Class, bool bCritical, int64 PoolBp)
    {
        FLDDamageEvaluationInput Input;
        const bool bBasic = Class != ELDCombatHitClass::Ability;
        Input.Definition.Emplace(10000, 0, 0, bBasic ? ELDDemoDamageType::Physical : ELDDemoDamageType::Magical);
        Input.HitClass = Class;
        Input.bCritical = bCritical;
        Input.TargetResistanceP2 = 0;
        Input.TargetHPP2 = 1000000;
        Input.TargetShieldP2 = 0;
        Input.bTargetProtected = false;
        Input.DamageBonusBp = PoolBp;
        return LDTactical::EvaluateDamage(Input, *Catalog);
    };
    const auto Damage = [](const FLDDamageEvaluationResult& Result) { return Result.IsSuccess() ? Result.Value->DamageP2 : int64(-1); };
    // D21: every "+X%" adds into one pool applied once; a basic crit x2 before it. #198: only a primary basic crits.
    TestEqual(TEXT("A 30% pool"), Damage(Evaluate(ELDCombatHitClass::Ability, false, 3000)), int64(13000));
    TestEqual(TEXT("A basic crit doubles before the pool"), Damage(Evaluate(ELDCombatHitClass::BasicPrimary, true, 3000)), int64(26000));
    const FLDDamageEvaluationResult AbilityCrit = Evaluate(ELDCombatHitClass::Ability, true, 3000);
    TestTrue(TEXT("An ability never crits: a critical ability hit is refused, not scaled"),
        !AbilityCrit.IsSuccess() && AbilityCrit.Error.Code == ELDCombatEvaluationError::Domain);
    TestFalse(TEXT("A negative pool is refused"), Evaluate(ELDCombatHitClass::Ability, false, -1).IsSuccess());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildSilenceTest, "Lockdown.Rules.DemoBuildPrimitives.Silence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildSilenceTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Caster = F.AddBody(), Victim = F.AddBody();
    FLDBodyCombatState& Body = F.Body(Victim);
    const auto Commit = [&](ELDControlKind Kind, int64 DurationMs, bool& bApplied)
    {
        FLDControlApplication App;
        App.Kind = Kind;
        App.EffectId = FGuid::NewGuid();
        App.SourceEntity = App.CreditOwner = Caster.Victim.EntityId;
        App.SourceBody = Caster;
        App.DurationMs = DurationMs;
        FString Error;
        const bool bOk = F.Driver->CommitControl(Body, App, F.Mode->GetAuthorityNowMs(), bApplied, Error);
        if (!bOk) { AddError(Error); }
        return bOk;
    };
    // A learned skill winding up ends unpaid; a basic or the innate dash winding up goes on.
    FLDCombatAction Windup;
    Windup.Kind = ELDCombatActionKind::Ability;
    Windup.Key.Origin = ELDCombatRequestOrigin::Nav;
    Windup.Source.Kind = ELDCombatSourceKind::Learned;
    Windup.Source.DefinitionId = TEXT("active_snark_bolt");
    Windup.ReleaseAtMs = 2000;
    Body.Action = Windup;
    bool bApplied = false;
    TestTrue(TEXT("A silence commits through the single control path"), Commit(ELDControlKind::Silence, 1000, bApplied) && bApplied);
    TestTrue(TEXT("It lasts its full time and is a hard control"),
        Body.Silences.Num() == 1 && Body.Silences[0].ExpiresAtMs == 2000 && Body.HardControl.Count == 0 && Body.HardControl.WindowEndMs > 0);
    TestFalse(TEXT("The silenced skill windup ended unpaid"), Body.Action.IsSet());
    TestTrue(TEXT("It forbids Q/W only"), F.Driver->IsActionBlocked(Body, ELDCombatControlAction::Skill)
        && !F.Driver->IsActionBlocked(Body, ELDCombatControlAction::Cast) && !F.Driver->IsActionBlocked(Body, ELDCombatControlAction::Move)
        && !F.Driver->IsActionBlocked(Body, ELDCombatControlAction::Basic)
        && !F.Driver->IsActionBlocked(Body, ELDCombatControlAction::Displacement));
    TestTrue(TEXT("A learned active is blocked, the innate dash is not"),
        F.Driver->IsCastBlocked(Body, ELDCombatSourceKind::Learned) && !F.Driver->IsCastBlocked(Body, ELDCombatSourceKind::Innate));
    TestTrue(TEXT("A silenced body counts as controlled (\"被减速或被控制\")"), F.Driver->IsImpaired(Body, 1500));
    TestTrue(TEXT("The silence locks the cast bar in the display"), LDCombatControl::SilenceBlockBits() == uint8(ELDCombatControlBlock::Cast));
    FLDCombatAction Basic = Windup;
    Basic.Kind = ELDCombatActionKind::Basic;
    Basic.Source.Kind = ELDCombatSourceKind::Basic;
    Body.Action = Basic;
    TestTrue(TEXT("A second silence diminishes with the shared chain"), Commit(ELDControlKind::Silence, 1000, bApplied) && bApplied
        && Body.Silences.Last().ExpiresAtMs == 1600);
    TestTrue(TEXT("and leaves a basic windup alone"), Body.Action.IsSet());
    FLDCombatAction Dash = Windup;
    Dash.Source.Kind = ELDCombatSourceKind::Innate;
    Body.Action = Dash;
    TestTrue(TEXT("A third silence"), Commit(ELDControlKind::Silence, 1000, bApplied) && bApplied);
    TestTrue(TEXT("leaves a dash windup alone"), Body.Action.IsSet());
    Body.Action.Reset();
    TArray<FLDObservedStatus> Statuses;
    FAccess::KeywordStatuses(*F.Driver, Body, 1500, Statuses);
    const FLDObservedStatus* Shown = FFixture::FindStatus(Statuses, ELDDemoStatusId::Silence);
    TestTrue(TEXT("Every observer sees the silence with its latest deadline"), Shown && Shown->ExpiresAtServerMs == 2000);
    FAccess::ExpireStatuses(*F.Driver, 2000);
    TestTrue(TEXT("It ends at its deadline"), Body.Silences.IsEmpty() && !F.Driver->IsActionBlocked(Body, ELDCombatControlAction::Skill));
    TestTrue(TEXT("A new one"), Commit(ELDControlKind::Silence, 1000, bApplied) && bApplied);
    FAccess::CancelEntityEffects(*F.Driver, Caster.Victim.EntityId);
    TestTrue(TEXT("ends with its leaving source"), Body.Silences.IsEmpty());
    // S14's cleanse ends roots and slows, never a disarm.
    Body.Disarms.Add({FGuid::NewGuid(), Caster.Victim.EntityId, 5000});
    Body.Roots.Add({FGuid::NewGuid(), Caster.Victim.EntityId, 5000});
    Body.Slows.Add({FGuid::NewGuid(), Caster.Victim.EntityId, 3000, 5000});
    FLDDemoAbilityValues Cleanse;
    Cleanse.Effects.AddDefaulted_GetRef().Opcode = uint8(ELDDemoEffectOp::DispelSlow);
    Cleanse.Effects.AddDefaulted_GetRef().Opcode = uint8(ELDDemoEffectOp::DispelRoot);
    TestTrue(TEXT("A slow and root cleanse changes the move-speed term"), FAccess::ApplyCleanses(*F.Driver, Body, Cleanse, 1500));
    TestTrue(TEXT("and removes every root and slow, keeping the disarm"), Body.Roots.IsEmpty() && Body.Slows.IsEmpty()
        && Body.Disarms.Num() == 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildSharedBreachBurnTest, "Lockdown.Rules.DemoBuildPrimitives.SharedBreachAndBurn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildSharedBreachBurnTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef A = F.AddBody(), B = F.AddBody(), Target = F.AddBody();
    FLDBodyCombatState& Body = F.Body(Target);
    Body.AttributeProjection.Final.ArmorP2 = 1500;
    FString Error;
    // Breach from two sources lands in one group capped at five stacks of -3.
    FLDCombatHit Hit = FFixture::Hit(A, Target, 0);
    Hit.BreachStacks = 2;
    if (!TestTrue(FString::Printf(TEXT("A breach hit: %s"), *Error), F.Apply(Hit, Error))) { return false; }
    TestTrue(TEXT("Two stacks for four seconds"), Body.SharedBreach.Stacks == 2 && Body.SharedBreach.ExpiresAtMs == 5000);
    TestTrue(TEXT("The breach is its applier's assist contribution"), F.HasContribution(Target, A));
    Hit = FFixture::Hit(B, Target, 0);
    Hit.BreachStacks = 4;
    TestTrue(TEXT("Another source's stacks join the same group up to five"), F.Apply(Hit, Error) && Body.SharedBreach.Stacks == 5);
    FLDCombatHit Strike = FFixture::Hit(A, Target, 10000);
    Strike.DamageType = ELDDemoDamageType::Physical;
    TestTrue(TEXT("Five stacks take 15 armor off: the full 100 lands"), F.Apply(Strike, Error) && Body.Resources.HpP2 == 90000);
    // Burn: layers keep their applier; one tick credits each applier its own damage.
    Hit = FFixture::Hit(A, Target, 0);
    Hit.BurnStacks = 2;
    Hit.ElementAPP2 = 0;
    TestTrue(TEXT("Two burn layers of A"), F.Apply(Hit, Error) && Body.Burn.Layers.Num() == 2
        && LDCombatKeywords::NextBurnTickAtMs(Body.Burn) == 1500);
    Hit = FFixture::Hit(B, Target, 0);
    Hit.BurnStacks = 1;
    Hit.ElementAPP2 = 10000;
    TestTrue(TEXT("One of B"), F.Apply(Hit, Error) && Body.Burn.Layers.Num() == 3 && LDCombatKeywords::BurnExpiresAtMs(Body.Burn) == 4000);
    TArray<FLDObservedStatus> Statuses;
    FAccess::KeywordStatuses(*F.Driver, Body, 1000, Statuses);
    const FLDObservedStatus* Exposed = FFixture::FindStatus(Statuses, ELDDemoStatusId::Exposed);
    const FLDObservedStatus* Burning = FFixture::FindStatus(Statuses, ELDDemoStatusId::Burn);
    TestTrue(TEXT("Observers see breach x5 and burn x3"), Exposed && Exposed->Stacks == 5 && Burning && Burning->Stacks == 3
        && Burning->ExpiresAtServerMs == 4000);
    F.Now(1500);
    TestTrue(FString::Printf(TEXT("The first tick: %s"), *Error), FAccess::AdvanceBurns(*F.Driver, 1500, Error));
    const TArray<FLDCombatHit>& Ticks = FAccess::PendingHits(*F.Driver);
    TestTrue(TEXT("One magic tick per applier, never a crit or on-hit"), Ticks.Num() == 2 && Ticks[0].SourceEntity == A.Victim.EntityId
        && Ticks[0].BaseP2 == 300 && Ticks[1].SourceEntity == B.Victim.EntityId && Ticks[1].BaseP2 == 300 && Ticks[0].bBurnTick
        && Ticks[0].bPeriodic && Ticks[0].OnHitScaleBp == 0 && Ticks[0].DamageType == ELDDemoDamageType::Magical);
    TestTrue(TEXT("The ticks land: 3.00 each"), F.Flush(Error) && Body.Resources.HpP2 == 89400
        && LDCombatKeywords::NextBurnTickAtMs(Body.Burn) == 2000);
    FAccess::CancelEntityEffects(*F.Driver, A.Victim.EntityId);
    TestTrue(TEXT("A leaving applier's layers end; the shared breach stays"), Body.Burn.Layers.Num() == 1 && Body.SharedBreach.Stacks == 5);
    Hit.BurnStacks = 6;
    F.Now(1500);
    TestTrue(TEXT("At five layers each new one replaces the one ending first"), F.Apply(Hit, Error) && Body.Burn.Layers.Num() == 5);
    TestEqual(TEXT("Every new layer runs its own three seconds"), LDCombatKeywords::BurnExpiresAtMs(Body.Burn), int64(4500));
    F.Now(4500);
    FAccess::ExpireStatuses(*F.Driver, 4500);
    TestTrue(TEXT("The burn ticks every half second through its last instant"), FAccess::AdvanceBurns(*F.Driver, 4500, Error)
        && FAccess::PendingHits(*F.Driver).Num() == 6);
    TestTrue(TEXT("and then ends"), F.Flush(Error) && Body.Burn.Layers.IsEmpty());
    F.Now(5000);
    FAccess::ExpireStatuses(*F.Driver, 5000);
    TestFalse(TEXT("The breach ended at its deadline"), Body.SharedBreach.Stacks > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildPoolHitsTest, "Lockdown.Rules.DemoBuildPrimitives.PoolHits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildPoolHitsTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Source = F.AddBody(), Target = F.AddBody();
    FLDBodyCombatState& Body = F.Body(Target);
    FString Error;
    const auto Lost = [&](const FLDCombatHit& Hit, int64 StartHPP2, bool bSlowed)
    {
        Body.Resources.HpP2 = StartHPP2;
        Body.Slows.Reset();
        if (bSlowed) { Body.Slows.Add({FGuid::NewGuid(), Source.Victim.EntityId, 3000, 9000}); }
        if (!F.Apply(Hit, Error)) { AddError(Error); return int64(-1); }
        return StartHPP2 - Body.Resources.HpP2;
    };
    FLDCombatHit Hit = FFixture::Hit(Source, Target, 10000);
    Hit.ControlAmpBp = 1200;
    TestEqual(TEXT("Neither slowed nor controlled: no bonus"), Lost(Hit, 100000, false), int64(10000));
    TestEqual(TEXT("Slowed: +12%"), Lost(Hit, 40000, true), int64(11200));
    TestEqual(TEXT("A low-HP body alone gains nothing (B07 reads controls only)"), Lost(Hit, 40000, false), int64(10000));
    FLDCombatHit Idle = Hit;
    Idle.IdleReadyAmpBp = 2000;
    TestEqual(TEXT("B07 and B17 add into one pool, never multiplied"), Lost(Idle, 100000, true), int64(13200));
    FLDCombatHit Basic = FFixture::Hit(Source, Target, 10000);
    Basic.HitClass = ELDCombatHitClass::BasicPrimary;
    Basic.DamageType = ELDDemoDamageType::Physical;
    Basic.bCritical = true;
    Basic.ControlAmpBp = 1200;
    TestEqual(TEXT("A basic crit doubles before the pool"), Lost(Basic, 100000, true), int64(22400));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildLifestealCapTest, "Lockdown.Rules.DemoBuildPrimitives.LifestealCap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildLifestealCapTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Source = F.AddBody(), Target = F.AddBody();
    FLDBodyCombatState& Healer = F.Body(Source);
    FString Error;
    FLDCombatHit Hit = FFixture::Hit(Source, Target, 10000);
    Hit.HitClass = ELDCombatHitClass::BasicPrimary;
    Hit.DamageType = ELDDemoDamageType::Physical;
    Hit.OnHitScaleBp = 10000;
    Hit.bLifesteal = true;
    Hit.LifestealRatioBp = 3000;
    Healer.Resources.HpP2 = 50000;
    TestTrue(TEXT("A primary basic heals its lifesteal share of the HP lost"), F.Apply(Hit, Error) && Healer.Resources.HpP2 == 53000);
    Hit.LifestealRatioBp = 6000;
    TestTrue(TEXT("Lifesteal stops at the 35% cap"), F.Apply(Hit, Error) && Healer.Resources.HpP2 == 56500);
    FLDCombatHit Tick = FFixture::Hit(Source, Target, 1000);
    Tick.bBurnTick = Tick.bPeriodic = true;
    Tick.OnHitScaleBp = 10000;
    Tick.bLifesteal = true;
    Tick.LifestealRatioBp = 3000;
    TestTrue(TEXT("A burn tick never heals its applier"), F.Apply(Tick, Error) && Healer.Resources.HpP2 == 56500);
    return true;
}

// #198: S02 roots, it never turns into a stun on a slowed body (the retired slowed-stun).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildRootOnSlowedTest, "Lockdown.Rules.DemoBuildPrimitives.RootOnSlowed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildRootOnSlowedTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Source = F.AddBody(), Slowed = F.AddBody(), Plain = F.AddBody();
    FString Error;
    F.Body(Slowed).Slows.Add({FGuid::NewGuid(), Source.Victim.EntityId, 3000, 5000});
    FLDCombatHit Hit = FFixture::Hit(Source, Slowed, 0);
    Hit.RootDurationMs = 1000;
    TestTrue(TEXT("A root on a slowed body roots it"), F.Apply(Hit, Error) && F.Body(Slowed).Roots.Num() == 1
        && F.Body(Slowed).Roots[0].ExpiresAtMs == 2000 && F.Body(Slowed).Stuns.IsEmpty() && F.Body(Slowed).Slows.Num() == 1);
    Hit.Target = Plain;
    TestTrue(TEXT("exactly as a body that is not slowed"), F.Apply(Hit, Error) && F.Body(Plain).Roots.Num() == 1
        && F.Body(Plain).Stuns.IsEmpty());
    return true;
}

// #198 build safe window: Combat's per-contender timestamp of the last combat action. Hostile positive damage dealt or
// taken (burn ticks included), a hard control or a knockback received mark it; a slow, healing, a self hit, a zero hit and
// reading or expiring statuses do not. The basic windup and offensive release triggers sit in StartNavAttack and
// ReleaseAbility; IsOffensive below is their classification of every approved active.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildSafeWindowTest, "Lockdown.Rules.DemoBuildPrimitives.BuildSafeWindow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildSafeWindowTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const int64 WindowMs = F.Catalog->Get().Parameters.BuildSafeWindowMs;
    TestEqual(TEXT("The approved window is 4 s"), WindowMs, int64(4000));
    TestEqual(TEXT("Never unsafe is safe at once"), LDCombatSourceRules::BuildSafeAtMs(0, WindowMs), int64(0));
    TestEqual(TEXT("The window runs from the last combat action"), LDCombatSourceRules::BuildSafeAtMs(1500, WindowMs), int64(5500));
    TestEqual(TEXT("A window past the clock saturates instead of wrapping"), LDCombatSourceRules::BuildSafeAtMs(MAX_int64 - 10, WindowMs),
        MAX_int64);

    // Offensive releases: every approved active with a hostile effect; a self buff, a shield, a plain dash or the S16
    // stance (its answer is hostile damage, marked when it lands) is not one.
    const TSet<FString> Peaceful = {TEXT("active_bail_out"), TEXT("active_not_mad"), TEXT("active_triple_combo"), TEXT("active_brace")};
    for (const FLDDemoAbilityDefinition& Ability : F.Catalog->Get().Abilities)
    {
        TestEqual(FString::Printf(TEXT("%s is %s"), *Ability.Id, Peaceful.Contains(Ability.Id) ? TEXT("not offensive") : TEXT("offensive")),
            LDCombatSourceRules::IsOffensive(Ability.Values), !Peaceful.Contains(Ability.Id));
    }
    TestFalse(TEXT("The innate dash is not offensive"), LDCombatSourceRules::IsOffensive(F.Catalog->Get().Character.Dash.Values));

    const FLDCombatBodyRef Attacker = F.AddBody(), Victim = F.AddBody(), Bystander = F.AddBody(), Camp = F.AddBody(ELDVictimKind::CampMember);
    FString Error;
    TestEqual(TEXT("A new contender has no combat action"), F.Driver->ReadBuildSafeAtMs(Victim.Victim.EntityId), int64(0));
    F.Now(2000);
    TestTrue(TEXT("A zero hit lands"), F.Apply(FFixture::Hit(Attacker, Victim, 0), Error));
    TestTrue(TEXT("A hit without damage marks nobody"), F.LastUnsafe(Attacker) == 0 && F.LastUnsafe(Victim) == 0);
    FLDCombatHit SelfHit = FFixture::Hit(Victim, Victim, 1000);
    TestTrue(TEXT("A self hit lands"), F.Apply(SelfHit, Error));
    TestEqual(TEXT("A self-inflicted hit marks nobody"), F.LastUnsafe(Victim), int64(0));
    F.Now(2500);
    TestTrue(TEXT("A hostile hit lands"), F.Apply(FFixture::Hit(Attacker, Victim, 1000), Error));
    TestTrue(TEXT("Hostile positive damage marks its victim and its credited contender"),
        F.LastUnsafe(Victim) == 2500 && F.LastUnsafe(Attacker) == 2500 && F.LastUnsafe(Bystander) == 0);
    TestEqual(TEXT("Combat reads the window for growth commits"), F.Driver->ReadBuildSafeAtMs(Victim.Victim.EntityId), int64(2500) + WindowMs);
    F.Now(2600);
    TestTrue(TEXT("A camp member's hit lands"), F.Apply(FFixture::Hit(Camp, Bystander, 1000), Error));
    TestEqual(TEXT("A camp's hostile damage marks the contender it hit"), F.LastUnsafe(Bystander), int64(2600));
    F.Now(2700);
    TestTrue(TEXT("A hit on a camp member lands"), F.Apply(FFixture::Hit(Attacker, Camp, 1000), Error));
    TestEqual(TEXT("Damaging a camp member marks the contender credited with it"), F.LastUnsafe(Attacker), int64(2700));
    FAccess::MarkBuildUnsafe(*F.Driver, Attacker.Victim.EntityId, 2650);
    TestEqual(TEXT("An older combat action never moves the window back"), F.LastUnsafe(Attacker), int64(2700));

    // Burn ticks are hostile damage of their applier.
    FLDCombatHit Burning = FFixture::Hit(Bystander, Victim, 0);
    Burning.BurnStacks = 1;
    Burning.ElementAPP2 = 0;
    TestTrue(TEXT("A burn layer without damage lands"), F.Apply(Burning, Error));
    TestEqual(TEXT("A layer alone is no damage yet"), F.LastUnsafe(Bystander), int64(2600));
    F.Now(3200);
    TestTrue(TEXT("The burn ticks"), FAccess::AdvanceBurns(*F.Driver, 3200, Error) && F.Flush(Error));
    TestTrue(TEXT("A burn tick marks its victim and its applier"), F.LastUnsafe(Victim) == 3200 && F.LastUnsafe(Bystander) == 3200);

    // Controls received: hard controls and knockbacks mark the victim; a slow does not.
    const auto Commit = [&](ELDControlKind Kind, int64 AtMs)
    {
        F.Now(AtMs);
        FLDControlApplication App;
        App.Kind = Kind;
        App.EffectId = FGuid::NewGuid();
        App.SourceEntity = App.CreditOwner = Camp.Victim.EntityId;
        App.SourceBody = Camp;
        App.DurationMs = 500;
        App.RatioBp = Kind == ELDControlKind::Slow ? 3000 : 0;
        bool bApplied = false;
        return F.Driver->CommitControl(F.Body(Bystander), App, AtMs, bApplied, Error) && bApplied;
    };
    TestTrue(TEXT("A slow lands"), Commit(ELDControlKind::Slow, 4000));
    TestEqual(TEXT("A slow alone is no combat action"), F.LastUnsafe(Bystander), int64(3200));
    int64 At = 4100;
    for (const ELDControlKind Kind : {ELDControlKind::Root, ELDControlKind::Stun, ELDControlKind::Silence, ELDControlKind::Disarm,
        ELDControlKind::Taunt, ELDControlKind::Knockback})
    {
        TestTrue(FString::Printf(TEXT("Control %u lands"), uint32(Kind)), Commit(Kind, At));
        TestEqual(FString::Printf(TEXT("Control %u received marks its victim"), uint32(Kind)), F.LastUnsafe(Bystander), At);
        At += 100;
    }

    // Healing, reading the body and expiring statuses never refresh the window.
    const int64 Before = F.LastUnsafe(Victim);
    F.Now(9000);
    F.Body(Victim).Resources.HpP2 = 50000;
    FLDCombatHit Heal = FFixture::Hit(Victim, Victim, 0);
    TestTrue(TEXT("A self hit without damage lands"), F.Apply(Heal, Error));
    TArray<FLDObservedStatus> Statuses;
    F.Driver->AppendObservedStatuses(F.Actor(Victim), 9000, Statuses);
    FAccess::ExpireStatuses(*F.Driver, 9000);
    TestTrue(TEXT("Observation and expiry are no combat actions"), F.LastUnsafe(Victim) == Before
        && F.Driver->ReadBuildSafeAtMs(Victim.Victim.EntityId) == Before + WindowMs);
    // The window is the contender's, not the body's: it outlives a removed body.
    FAccess::Bodies(*F.Driver).Remove(&F.Actor(Victim));
    TestEqual(TEXT("A death keeps the window"), F.Driver->ReadBuildSafeAtMs(Victim.Victim.EntityId), Before + WindowMs);
    return true;
}

// #198 S08 脚底开锅: a fixed zone with three pulses at +0/+1/+2 s; each pulse deals its damage, adds one burn layer and slows
// 20% for 0.5 s (zone_slow DurationMs, no linger, no root).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildZonePulsesTest, "Lockdown.Rules.DemoBuildPrimitives.ZonePulses",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildZonePulsesTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDDemoAbilityDefinition* Ability = nullptr;
    FString Error;
    if (!TestTrue(TEXT("S08 is approved"), F.Catalog->FindAbility(TEXT("active_quick_meeting"), Ability, Error) && Ability)) { return false; }
    const FLDDemoAbilityValues* Values = F.Catalog->FindValues(*Ability, 1);
    if (!TestNotNull(TEXT("S08 has its level-1 values"), Values)) { return false; }
    const auto Op = [Values](ELDDemoEffectOp Wanted) { return Values->Effects.FindByPredicate(
        [Wanted](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp() == Wanted; }); };
    const FLDDemoEffectDefinition* Zone = Op(ELDDemoEffectOp::Zone);
    const FLDDemoEffectDefinition* Slow = Op(ELDDemoEffectOp::ZoneSlow);
    const FLDDemoEffectDefinition* Burn = Op(ELDDemoEffectOp::ZoneBurn);
    if (!TestTrue(TEXT("S08 carries a zone, its slow and its burn"), Zone && Slow && Burn && Op(ELDDemoEffectOp::ZoneDamage)
        && !Op(ELDDemoEffectOp::Root)))
    { return false; }
    TestTrue(TEXT("Three pulses one second apart from the release, inside three seconds"), Zone->Count == 3 && Zone->IntervalMs == 1000
        && Zone->DelayMs == 0 && Zone->DurationMs == 3000);
    TestTrue(TEXT("Each check slows 20% for 0.5 s and burns one layer every second"), Slow->RatioBp == 2000 && Slow->IntervalMs == 1000
        && Slow->DurationMs == 500 && Burn->Count == 1 && Burn->IntervalMs == 1000);

    const FLDCombatBodyRef Caster = F.AddBody();
    FLDCombatSourceRef Source;
    Source.Kind = ELDCombatSourceKind::Learned;
    Source.DefinitionId = TEXT("active_quick_meeting");
    F.Now(5000);
    if (!TestTrue(FString::Printf(TEXT("The zone is released: %s"), *Error), FAccess::ReleaseMechanics(*F.Driver, F.Body(Caster), Source,
        *Values, FVector(0, 0, 0), FVector(300, 0, 0), Error) && FAccess::Zones(*F.Driver).Num() == 1))
    { return false; }
    const FLDCombatZone& Placed = FAccess::Zones(*F.Driver)[0];
    TestTrue(TEXT("The zone keeps the approved schedule from its release"), Placed.StartMs == 5000 && Placed.EndMs == 8000
        && Placed.Count == 3 && Placed.IntervalMs == 1000 && Placed.DelayMs == 0 && Placed.bDamage && Placed.CentreCm.X == 300);
    TestTrue(TEXT("Each slow check slows for the check's own duration, starting at the release"), Placed.bSlow && Placed.SlowBp == 2000
        && Placed.SlowIntervalMs == 1000 && Placed.SlowDurationMs == 500 && Placed.NextSlowAtMs == 5000);
    TestTrue(TEXT("The burn rides on every damage pulse (one element batch), with no separate burn checks"),
        Placed.PulseBurnStacks == 1 && Placed.BurnStacks == 0);
    TArray<int64> Pulses;
    for (int64 At = Placed.StartMs; At <= Placed.EndMs; ++At)
    {
        const int64 Next = LDCombatMechanics::NextPulseAtMs(Placed.StartMs, Placed.DelayMs, Placed.IntervalMs, Placed.Count, At);
        if (Next == At) { Pulses.Add(At); }
    }
    TestTrue(TEXT("Exactly three damage pulses: +0, +1 and +2 s, none at the end"),
        Pulses == TArray<int64>({5000, 6000, 7000}));
    TArray<int64> Checks;
    for (int64 At = Placed.NextSlowAtMs; At < Placed.EndMs; At += Placed.SlowIntervalMs) { Checks.Add(At); }
    TestTrue(TEXT("Exactly three slow checks on the pulse beats (the end is no check)"),
        Checks == TArray<int64>({5000, 6000, 7000}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildEmpowerChargesTest, "Lockdown.Rules.DemoBuildPrimitives.EmpowerCharges",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildEmpowerChargesTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Attacker = F.AddBody(), Camp = F.AddBody(ELDVictimKind::CampMember);
    FLDBodyCombatState& Body = F.Body(Attacker);
    const FLDAttributeProjectionResult Base = LDAttributeProjection::Project(FLDAttributeBuildValues(1, {}), *F.Catalog);
    if (!TestTrue(TEXT("A level-1 projection"), Base.IsSuccess())) { return false; }
    Body.BuildProjection = Body.AttributeProjection = Base.Value.GetValue();
    FLDCombatEntityRecord& Record = FAccess::Records(*F.Driver).FindChecked(Attacker.Victim.EntityId);
    Record.Build.Actives = {{TEXT("active_snark_bolt"), ELDCombatEntry::Q}, {TEXT("active_pants_down"), ELDCombatEntry::W},
        {TEXT("active_elbow"), ELDCombatEntry::E}};
    for (const TCHAR* Id : {TEXT("active_snark_bolt"), TEXT("active_pants_down"), TEXT("active_elbow")})
    {
        FLDCombatTrack& Track = Record.Tracks.AddDefaulted_GetRef();
        Track.IssuedCooldownId = FGuid::NewGuid();
        Track.Kind = ELDCombatSourceKind::Learned;
        Track.DefinitionId = Id;
        Track.ReadyAtMs = 6000;
    }
    // S17 左右开抽: three charges of +16 physical each, attack speed +25% while charges remain.
    const FGuid Release = FGuid::NewGuid();
    FLDCombatEmpower Armed;
    Armed.EffectId = Release;
    Armed.Source.Kind = ELDCombatSourceKind::Learned;
    Armed.Source.DefinitionId = TEXT("active_triple_combo");
    Armed.AmountP2 = 1600;
    Armed.ExpiresAtMs = 6000;
    Armed.Charges = 3;
    Armed.bChargeBonus = true;
    Body.Empowers.Add(Armed);
    FLDCombatTimedBonus Haste;
    Haste.EffectId = Release;
    Haste.SourceDefinition = Armed.Source.DefinitionId;
    Haste.Stats.Add({ELDDemoBonusStat::AttackSpeedBonusRatio, 2500, {}});
    Haste.ExpiresAtMs = 6000;
    Body.TimedBonuses.Add(Haste);
    FString Error;
    for (int32 Swing = 1; Swing <= 4; ++Swing)
    {
        FAccess::PendingHits(*F.Driver).Reset();
        FLDCombatAction Action;
        Action.Kind = ELDCombatActionKind::Basic;
        Action.Key.Origin = ELDCombatRequestOrigin::Nav;
        Action.Token = uint64(Swing);
        Action.Entry = ELDCombatEntry::Basic;
        Action.Source.Kind = ELDCombatSourceKind::Basic;
        Action.TargetBody = Camp;
        // The landing half only: release sight and observation need a Vision director this fixture does not build.
        if (!TestTrue(FString::Printf(TEXT("Swing %d lands: %s"), Swing, *Error),
            FAccess::LandBasic(*F.Driver, F.Actor(Attacker), Body, Action, Error)))
        { return false; }
        const TArray<FLDCombatHit>& Hits = FAccess::PendingHits(*F.Driver);
        const FLDCombatHit* Primary = Hits.FindByPredicate([](const FLDCombatHit& Row) { return Row.HitClass == ELDCombatHitClass::BasicPrimary; });
        if (!TestNotNull(TEXT("The swing's primary hit"), Primary)) { return false; }
        TestEqual(TEXT("A basic hits its one target only"), Hits.Num(), 1);
        if (Swing <= 3)
        {
            TestEqual(FString::Printf(TEXT("Charge %d adds its damage"), Swing), Primary->BaseP2, int64(1600));
            TestEqual(FString::Printf(TEXT("No empowered hit adds breach (%d)"), Swing), Primary->BreachStacks, 0);
            TestEqual(TEXT("The attack-speed bonus lasts while charges remain"),
                Body.TimedBonuses.ContainsByPredicate([&Release](const FLDCombatTimedBonus& Row) { return Row.EffectId == Release; }), Swing < 3);
        }
        else
        {
            TestTrue(TEXT("After three charges the basic is plain"), Primary->BaseP2 == 0 && Body.Empowers.IsEmpty());
        }
        TestFalse(TEXT("Without a held trigger a basic carries no lifesteal"), Primary->bLifesteal);
    }
    TestTrue(TEXT("Without a held trigger a landed basic refunds no cooldown"),
        Record.Tracks[0].ReadyAtMs == 6000 && Record.Tracks[1].ReadyAtMs == 6000 && Record.Tracks[2].ReadyAtMs == 6000);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildFullDebuffObservationTest, "Lockdown.Rules.DemoBuildPrimitives.FullDebuffObservation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildFullDebuffObservationTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Source = F.AddBody(), Target = F.AddBody();
    FLDBodyCombatState& Body = F.Body(Target);
    const FGuid From = Source.Victim.EntityId;
    const auto Control = [&From](int64 Until) { return FLDCombatRoot{FGuid::NewGuid(), From, Until}; };
    // Every public combat cue at once.
    Body.Roots.Add(Control(5000));
    Body.Shield = FLDCombatShield{FGuid::NewGuid(), From, 5000};
    Body.Resources.ShieldP2 = 1000;
    Body.Slows.Add({FGuid::NewGuid(), From, 3000, 5000});
    FLDCombatTimedBonus Boost;
    Boost.ExpiresAtMs = 5000;
    Body.TimedBonuses.Add(Boost);
    Body.SharedBreach.Stacks = 3;
    Body.SharedBreach.ExpiresAtMs = 5000;
    Body.Disarms.Add(Control(5000));
    Body.Stuns.Add(Control(5000));
    Body.Knockbacks.Add(Control(5000));
    Body.Silences.Add(Control(5000));
    TestTrue(TEXT("Five burn layers"), LDCombatKeywords::ApplyBurn(Body.Burn, Layer(From, 0), 5, 1000, Terms()));
    TArray<FLDObservedStatus> Statuses;
    F.Driver->AppendObservedStatuses(F.Actor(Target), 1000, Statuses);
    TestEqual(TEXT("Each public combat cue shows once"), Statuses.Num(), 10);
    const FLDObservedStatus* Exposed = FFixture::FindStatus(Statuses, ELDDemoStatusId::Exposed);
    const FLDObservedStatus* Burn = FFixture::FindStatus(Statuses, ELDDemoStatusId::Burn);
    TestTrue(TEXT("with the breach and burn stacks"), Exposed && Exposed->Stacks == 3 && Burn && Burn->Stacks == 5
        && FFixture::FindStatus(Statuses, ELDDemoStatusId::Silence));
    // The observation adds respawn protection to a contender's cues; all of them stay inside the published bound.
    Statuses.Add({uint8(ELDDemoStatusId::RespawnProtection), 1, 2000});
    FLDObservedUnitView Unit;
    Unit.Handle = FGuid::NewGuid();
    Unit.ObservationEpoch = Unit.StateRevision = 1;
    Unit.AuthorizedFieldMask = LDObservation::ContenderFields;
    Unit.Kind = ELDObservedUnitKind::Contender;
    Unit.ContenderId = FGuid::NewGuid();
    Unit.Level = 1;
    Unit.Health.CurrentP2 = Unit.Health.MaxP2 = 100000;
    Unit.Mana.MaxP2 = 10000;
    Unit.Statuses = Statuses;
    FString Error;
    TestTrue(TEXT("A fully debuffed contender is still a valid observation"), LDObservation::ValidateUnit(Unit, Error));
    AddInfo(Error);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDExpandedDashCapsulesTest, "Lockdown.Rules.DemoSkillExpansion.DashCapsuleContact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDExpandedDashCapsulesTest::RunTest(const FString& Parameters)
{
    TOptional<double> Contact;
    FString Error;
    const FVector A(0,0,88), B(300,0,88), Target(150,0,88);
    TestTrue(TEXT("Swept capsules contact before their centres coincide"),
        LDDashContactRules::FirstContact(A,B,Target,Target,80,96,Contact,Error)
        && Contact.IsSet() && FMath::IsNearlyEqual(Contact.GetValue(),70./300.,.000001));
    TestTrue(TEXT("Two moving bodies can cross between step endpoints"),
        LDDashContactRules::FirstContact(A,B,FVector(150,-200,88),FVector(150,200,88),80,96,Contact,Error) && Contact.IsSet());
    TestTrue(TEXT("Vertical separation is not a planar false hit"),
        LDDashContactRules::FirstContact(A,B,Target+FVector(0,0,300),Target+FVector(0,0,300),80,96,Contact,Error) && !Contact.IsSet());
    TestTrue(TEXT("Leaving an initial overlap is not a new collision"),
        LDDashContactRules::FirstContact(A,FVector(-300,0,88),FVector(70,0,88),FVector(70,0,88),80,96,Contact,Error) && !Contact.IsSet());
    TestTrue(TEXT("Entering from initial contact is a collision"),
        LDDashContactRules::FirstContact(A,B,FVector(80,0,88),FVector(80,0,88),80,96,Contact,Error)
        && Contact.IsSet() && Contact.GetValue()==0.);
    TestTrue(TEXT("A dash blocked without displacement cannot farm contacts"),
        LDDashContactRules::FirstContact(A,A,Target,A,80,96,Contact,Error) && !Contact.IsSet());
    TestFalse(TEXT("Invalid geometry is an explicit error"),
        LDDashContactRules::FirstContact(A,B,Target,Target,0,96,Contact,Error));
    TestFalse(TEXT("Invalid geometry has a diagnostic"),Error.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDExpandedMitigationTest, "Lockdown.Rules.DemoSkillExpansion.MitigationAndAttackSlow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDExpandedMitigationTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(*this,Catalog)) { return false; }
    FLDDamageEvaluationInput Hit;
    Hit.Definition.Emplace(10000,0,0,ELDDemoDamageType::Physical);
    Hit.HitClass=ELDCombatHitClass::BasicPrimary;
    Hit.bCritical=false;
    Hit.TargetResistanceP2=0;
    Hit.TargetHPP2=100000;
    Hit.TargetShieldP2=2000;
    Hit.bTargetProtected=false;
    Hit.DeliveryScaleBp=4800;
    Hit.PrimaryGuardP2=1200;
    Hit.PrimaryGuardCapBp=2000;
    const auto Result=LDTactical::EvaluateDamage(Hit,*Catalog);
    if (!TestTrue(TEXT("Exact mitigation evaluates"),Result.IsSuccess())) { return false; }
    TestEqual(TEXT("The primary guard cap follows the delivered amount"),Result.Value->DamageP2,int64(3840));
    TestEqual(TEXT("Shield absorbs only after mitigation"),Result.Value->HPLostP2,int64(1840));
    Hit.HitClass=ELDCombatHitClass::Ability;
    const auto Ability=LDTactical::EvaluateDamage(Hit,*Catalog);
    TestTrue(TEXT("Primary guard does not reduce a separate ability hit"),
        Ability.IsSuccess() && Ability.Value->DamageP2==4800);
    Hit.DeliveryScaleBp=-1;
    TestFalse(TEXT("A negative delivery share is refused"),LDTactical::EvaluateDamage(Hit,*Catalog).IsSuccess());

    // Seventy agility points past level 16 push attack speed past its 2.3 cap.
    FLDAttributeBuildValues Values(16,{FLDStatPointsValue(TEXT("stat_agility"),70)});
    const auto Build=LDAttributeProjection::Project(Values,*Catalog);
    if (!TestTrue(TEXT("The capped build projects"),Build.IsSuccess())) { return false; }
    FLDTemporaryAttributeTerms Terms;
    FLDDemoStatBonus Speed;
    Speed.Stat=ELDDemoBonusStat::AttackSpeedBonusRatio;
    Speed.Amount=3000;
    Terms.Bonuses.Add(Speed);
    Terms.AttackSlowBp=2500;
    const auto Slowed=LDAttributeProjection::ApplyTemporary(Build.Value.GetValue(),Terms,*Catalog);
    TestTrue(TEXT("An attack slow slows the capped attack speed, not the pre-cap sum"),
        Slowed.IsSuccess() && Slowed.Value->Final.AttackSpeed.Numerator==69 && Slowed.Value->Final.AttackSpeed.Denominator==40);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDExpandedDeliveryTest, "Lockdown.Rules.DemoSkillExpansion.PublicEffects",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDExpandedDeliveryTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const auto Source=F.AddBody();
    auto& Body=F.Body(Source);
    FString Error;
    for (const TCHAR* Id : {TEXT("active_open_mic"),TEXT("active_calculus_drop"),TEXT("active_flyer_trap")})
    {
        const FLDDemoAbilityDefinition* Ability=nullptr;
        if (!TestTrue(TEXT("Approved delivery exists"),F.Catalog->FindAbility(Id,Ability,Error))) { return false; }
        auto Hit=FFixture::Hit(Source,Source,10000);
        Hit.Source.Kind=ELDCombatSourceKind::Learned; Hit.Source.DefinitionId=Id;
        Hit.CastLedger=MakeShared<FLDCombatCastLedger>();
        Hit.bDirectAbilityDamage=true;
        FLDCombatAction Action;
        Action.Direction=FVector(1,0,0); Action.TargetPoint=FVector(2000,0,0); Action.Key.DirectSequence=1;
        bool Handled=false;
        TestTrue(TEXT("Delivery is registered instead of immediate area damage"),
            FAccess::Delivery(*F.Driver,Body,Ability->Values,Ability->Target,Action,Hit,1000,Handled,Error) && Handled);
    }
    TestTrue(TEXT("Channel is an ongoing action cue"),ALDCombatDirector::CueFactsOf(Body).bActing);
    TArray<FLDObservedProjectileFact> Facts;
    TestTrue(TEXT("Telegraphs and channel export public geometry"),F.Driver->ReadObservedProjectiles(Facts,Error));
    TestEqual(TEXT("Three distinct visible effects"),Facts.Num(),3);
    TestTrue(TEXT("The brick telegraph exposes its outer and full-damage radii"),
        Facts.ContainsByPredicate([](const auto& Fact) { return Fact.Look==ELDObservedProjectileLook::BookShadow
            && Fact.RadiusCm==200 && Fact.InnerRadiusCm==80 && Fact.PositionCm.X==650; }));
    FAccess::CancelChannel(*F.Driver,Body);
    TestFalse(TEXT("Cancelling the channel ends it"),Body.ChannelEffectId.IsValid());
    Body.bPendingDeath=true;
    Facts.Reset();
    TestTrue(TEXT("Released ground effects survive normal source death"),F.Driver->ReadObservedProjectiles(Facts,Error));
    TestEqual(TEXT("Only the two independent ground effects remain visible"),Facts.Num(),2);
    FAccess::CancelEntityEffects(*F.Driver,Source.Victim.EntityId);
    TestTrue(TEXT("Departure cancels those independent effects"),FAccess::Ground(*F.Driver).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDExpandedCatalogTest, "Lockdown.Rules.DemoSkillExpansion.CatalogGrowthBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDExpandedCatalogTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(*this,Catalog)) { return false; }
    for (const auto& Ability : Catalog->Get().Abilities)
    {
        const FLDDemoAbilityValues* First=Catalog->FindValues(Ability,1);
        if (!TestNotNull(*FString::Printf(TEXT("%s has its level-1 values"),*Ability.Id),First)) { continue; }
        for (const uint8 Level : {uint8(5),uint8(10),uint8(13),uint8(16)})
        {
            const FLDDemoAbilityValues* Grown=Catalog->FindValues(Ability,Level);
            bool bBounded=Grown && Grown->ManaCostP2==First->ManaCostP2 && Grown->CooldownMs==First->CooldownMs
                && Grown->Effects.Num()==First->Effects.Num();
            for (int32 Index=0; bBounded && Index<First->Effects.Num(); ++Index)
            {
                bBounded=Grown->Effects[Index].BaseP2>=First->Effects[Index].BaseP2
                    && Grown->Effects[Index].DurationMs==First->Effects[Index].DurationMs
                    && Grown->Effects[Index].RatioBp==First->Effects[Index].RatioBp;
            }
            TestTrue(*FString::Printf(TEXT("%s at level %u grows its amounts only, never mana, cooldown, ratios or durations"),
                *Ability.Id,uint32(Level)),bBounded);
        }
    }
    return true;
}

// #198 elements (docs/systems/COMBAT_AND_GROWTH.md "水火电与反应"): the reaction table, R, the S16 record and the S21 cut.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoElementRulesTest, "Lockdown.Rules.DemoElements.Rules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoElementRulesTest::RunTest(const FString& Parameters)
{
    using namespace LDCombatElements;
    using E = ELDCombatElement;
    using R = ELDCombatReaction;
    TestTrue(TEXT("Fire and water fog either way"), ReactionOf(E::Fire, E::Water) == R::Fog && ReactionOf(E::Water, E::Fire) == R::Fog);
    TestTrue(TEXT("Water and electric conduct"), ReactionOf(E::Water, E::Electric) == R::Conduct && ReactionOf(E::Electric, E::Water) == R::Conduct);
    TestTrue(TEXT("Fire and electric overload"), ReactionOf(E::Fire, E::Electric) == R::Overload && ReactionOf(E::Electric, E::Fire) == R::Overload);
    TestTrue(TEXT("The same element or none never reacts"), ReactionOf(E::Water, E::Water) == R::None && ReactionOf(E::None, E::Fire) == R::None);
    FLDElementTerms Terms;
    Terms.ReactionBaseP2 = 2000;
    Terms.ReactionPerLevelP2 = 100;
    Terms.ReactionAPRatioBp = 2000;
    TestEqual(TEXT("R at level 1 without AP is 20"), ReactionDamageP2(1, 0, Terms), int64(2000));
    TestEqual(TEXT("R = 20 + (L-1) + 0.2 AP, never scaled by G"), ReactionDamageP2(6, 5000, Terms), int64(2000 + 500 + 1000));
    TestEqual(TEXT("The S16 record grows by the cut"), BraceRecordP2(2000, 1500, 10000), int64(3500));
    TestEqual(TEXT("and stops at its cap"), BraceRecordP2(9000, 5000, 10000), int64(10000));
    FLDCombatBrace Brace;
    Brace.BaseP2 = 5000;
    Brace.RecordRatioBp = 6000;
    Brace.RecordedP2 = 10000;
    TestEqual(TEXT("The answer is 50 + 60% of the record"), BraceAnswerP2(Brace), int64(11000));
    FLDCombatGrievous Grievous;
    TestTrue(TEXT("A grievous lands"), ApplyGrievous(Grievous, 3500, 4000, 1000));
    TestEqual(TEXT("It cuts a new heal or shield by 35%"), AfterGrievousP2(10000, Grievous, 2000), int64(6500));
    TestEqual(TEXT("and nothing after it ends"), AfterGrievousP2(10000, Grievous, 4000), int64(10000));
    FLDCombatHit Hit;
    Hit.HitClass = ELDCombatHitClass::Ability;
    Hit.bDirectAbilityDamage = true;
    TestTrue(TEXT("An active's own damage is skill-direct"), CategoryOf(Hit) == ELDCombatHitCategory::SkillDirect);
    Hit.bStatusSettle = true;
    TestTrue(TEXT("Reaction, discharge and settle damage is status damage"), CategoryOf(Hit) == ELDCombatHitCategory::StatusSettle);
    return true;
}

// #198: one element slot per target; wet refreshes; a new fire on wet fogs (R plus the fire's whole budget at once); a new
// water on fire settles the held layers to their own applier. One fog per owner; its 0 s pulse wets the element-free bodies
// inside before the batch that made it continues (spec §11.1), the next one a second later.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoElementFogTest, "Lockdown.Rules.DemoElements.FogAndSlot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoElementFogTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef A = F.AddBody(), B = F.AddBody(), Target = F.AddBody(), Second = F.AddBody(), Third = F.AddBody();
    // Every body stands outside the others' 2.5 m fog.
    F.Place(A, -600, -600);
    F.Place(B, 600, -600);
    F.Place(Target, -600, 600);
    F.Place(Second, 600, 600);
    F.Place(Third, 0, 0);
    FLDBodyCombatState& Body = F.Body(Target);
    FString Error;
    FLDCombatHit Wet = FFixture::Hit(A, Target, 0);
    Wet.bApplyWet = true;
    if (!TestTrue(FString::Printf(TEXT("A wet hit: %s"), *Error), F.Apply(Wet, Error))) { return false; }
    TestTrue(TEXT("The target is wet for four seconds with its slow"), Body.ElementMark.Kind == ELDCombatElement::Water
        && Body.ElementMark.ExpiresAtMs == 5000 && Body.Slows.Num() == 1 && Body.Slows[0].RatioBp == 1500);
    TArray<FLDObservedStatus> Statuses;
    FAccess::KeywordStatuses(*F.Driver, Body, 1000, Statuses);
    TestNotNull(TEXT("Observers see wet"), FFixture::FindStatus(Statuses, ELDDemoStatusId::Wet));
    F.Now(1500);
    TestTrue(TEXT("Wet again refreshes it and keeps one slow"), F.Apply(Wet, Error) && Body.ElementMark.ExpiresAtMs == 5500
        && Body.Slows.Num() == 1);
    FLDCombatHit Fire = FFixture::Hit(B, Target, 0);
    Fire.BurnStacks = 1;
    TestTrue(FString::Printf(TEXT("A fire hit on wet: %s"), *Error), F.Apply(Fire, Error));
    const TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    TestTrue(TEXT("Fog consumes both elements; its 0 s pulse wets the element-free target again as B's fog, with one new slow"),
        Body.ElementMark.Kind == ELDCombatElement::Water && Body.ElementMark.Applier.SourceEntity == B.Victim.EntityId
        && Body.ElementMark.ExpiresAtMs == 5500 && Body.Slows.Num() == 1 && Body.Burn.Layers.IsEmpty());
    TestTrue(TEXT("The new fire's whole budget (six ticks of 1.50) settles at once as B's fire, then R = 20 from B"),
        Pending.Num() == 2 && Pending[0].bBurnTick && Pending[0].BaseP2 == 900 && Pending[0].SourceEntity == B.Victim.EntityId
        && Pending[1].bStatusSettle && !Pending[1].bBurnTick && Pending[1].BaseP2 == 2000 && Pending[1].SourceEntity == B.Victim.EntityId
        && Pending[1].Target == Target);
    TestTrue(TEXT("B owns one fog, its next pulse one second later, and the reaction is reported with B as trigger"),
        FAccess::Fogs(*F.Driver).Num() == 1 && FAccess::Fogs(*F.Driver)[0].Owner.SourceEntity == B.Victim.EntityId
        && FAccess::Fogs(*F.Driver)[0].NextPulseMs == 2500 && FAccess::Reactions(*F.Driver).Num() == 1
        && FAccess::Reactions(*F.Driver)[0].Kind == ELDCombatReaction::Fog
        && FAccess::Reactions(*F.Driver)[0].TriggerEntity == B.Victim.EntityId);
    TestTrue(TEXT("The settles land"), F.Flush(Error) && Body.Resources.HpP2 == 100000 - 2900);

    FLDBodyCombatState& Other = F.Body(Second);
    FLDCombatHit TwoLayers = FFixture::Hit(A, Second, 0);
    TwoLayers.BurnStacks = 2;
    TestTrue(TEXT("Two fire layers of A"), F.Apply(TwoLayers, Error) && Other.Burn.Layers.Num() == 2);
    F.Now(2000);
    FLDCombatHit Water = FFixture::Hit(B, Second, 0);
    Water.bApplyWet = true;
    TestTrue(FString::Printf(TEXT("Water on fire: %s"), *Error), F.Apply(Water, Error));
    TestTrue(TEXT("A's held layers settle their remaining six ticks each to A; B's R follows; the new fog wets the target"),
        Pending.Num() == 2 && Pending[0].SourceEntity == A.Victim.EntityId && Pending[0].BaseP2 == 1800 && Pending[0].bBurnTick
        && Pending[1].SourceEntity == B.Victim.EntityId && Pending[1].BaseP2 == 2000
        && Other.Burn.Layers.IsEmpty() && Other.ElementMark.Kind == ELDCombatElement::Water);
    TestEqual(TEXT("B's newer fog replaced its first one"), FAccess::Fogs(*F.Driver).Num(), 1);
    TestTrue(TEXT("The settles land"), F.Flush(Error));

    FLDBodyCombatState& Free = F.Body(Third);
    FLDCombatHit Zap = FFixture::Hit(A, Third, 0);
    Zap.bApplyElectric = true;
    TestTrue(TEXT("Electric on an element-free target marks it"), F.Apply(Zap, Error)
        && Free.ElementMark.Kind == ELDCombatElement::Electric && Free.ElementMark.ExpiresAtMs == 6000);
    FAccess::CancelEntityEffects(*F.Driver, A.Victim.EntityId);
    TestTrue(TEXT("A leaving applier's mark ends with it"), Free.ElementMark.Kind == ELDCombatElement::None);
    return true;
}

// #198 S16 顶着揍 (AI spec §9.1): 20% of hostile damage after resistance is cut and recorded up to the cap; its own damage is
// not; a dying stance ends without its answer. S21: a grievous cuts new shields and heals by 35%.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoElementBraceTest, "Lockdown.Rules.DemoElements.BraceAndGrievous",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoElementBraceTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef A = F.AddBody(), Target = F.AddBody(ELDVictimKind::Contender, 10000000);
    FLDBodyCombatState& Body = F.Body(Target);
    FString Error;
    FLDDemoEffectDefinition Brace;
    Brace.Opcode = uint8(ELDDemoEffectOp::Brace);
    Brace.DurationMs = 3000;
    Brace.RatioBp = 2000;
    Brace.LimitP2 = 10000;
    Brace.RadiusCm.Numerator = 300;
    Brace.BaseP2 = 5000;
    Brace.SecondaryRatioBp = 6000;
    if (!TestTrue(FString::Printf(TEXT("The stance starts: %s"), *Error), FAccess::ReleaseBrace(*F.Driver, Body, Brace, 1000, Error)))
    { return false; }
    TArray<FLDObservedStatus> Statuses;
    FAccess::KeywordStatuses(*F.Driver, Body, 1000, Statuses);
    const FLDObservedStatus* Shown = FFixture::FindStatus(Statuses, ELDDemoStatusId::Braced);
    TestTrue(TEXT("Observers see the stance and its end"), Shown && Shown->ExpiresAtServerMs == 4000);
    TestTrue(TEXT("A hostile 100 lands as 80 and records the 20 cut"), F.Apply(FFixture::Hit(A, Target, 10000), Error)
        && Body.Resources.HpP2 == 10000000 - 8000 && Body.Brace.RecordedP2 == 2000);
    TestTrue(TEXT("Its own damage is neither cut nor recorded"), F.Apply(FFixture::Hit(Target, Target, 10000), Error)
        && Body.Resources.HpP2 == 10000000 - 18000 && Body.Brace.RecordedP2 == 2000);
    TestTrue(TEXT("The record stops at its cap"), F.Apply(FFixture::Hit(A, Target, 500000), Error) && Body.Brace.RecordedP2 == 10000);
    TestEqual(TEXT("Its answer would be 50 + 60% of the record"), LDCombatElements::BraceAnswerP2(Body.Brace), int64(11000));
    F.Now(4500);
    Body.bPendingDeath = true;
    TestTrue(TEXT("A dying stance ends without its answer"), FAccess::AdvanceElements(*F.Driver, 4500, Error)
        && !Body.Brace.EffectId.IsValid() && FAccess::PendingHits(*F.Driver).IsEmpty());
    Body.bPendingDeath = false;

    FLDCombatHit Sock = FFixture::Hit(A, Target, 0);
    Sock.GrievousBp = 3500;
    Sock.GrievousMs = 3000;
    TestTrue(TEXT("The sock lands its cut"), F.Apply(Sock, Error) && Body.Grievous.RatioBp == 3500 && Body.Grievous.ExpiresAtMs == 7500);
    Statuses.Reset();
    FAccess::KeywordStatuses(*F.Driver, Body, 4500, Statuses);
    TestNotNull(TEXT("Observers see it"), FFixture::FindStatus(Statuses, ELDDemoStatusId::Soured));
    TestTrue(TEXT("A new 100 shield arrives as 65"), FAccess::GrantShield(*F.Driver, Body, 10000, 4500, Error)
        && Body.Resources.ShieldP2 == 6500);
    const int64 Before = Body.Resources.HpP2;
    TestTrue(TEXT("A 100 heal arrives as 65"), FAccess::Heal(*F.Driver, Body, 10000) && Body.Resources.HpP2 == Before + 6500);
    return true;
}

// #198 B base components (docs/systems/COMBAT_AND_GROWTH.md, "基础构件"): the projection terms, the merged empowered basic,
// B12's dash completion, the B02 ledger and the component element gate, over the approved component catalog.
namespace LDBuildPrimitivesTests
{
struct FInstall
{
    const TCHAR* Id = nullptr;
    FName BoundActive;
    ELDDemoElement Element = ELDDemoElement::None;
    // #198 F install target and C12's base.
    ELDComponentTargetKind TargetKind = ELDComponentTargetKind::None;
    FName TargetId;
    FName BoundComponent;
};

FLDAttributeProjectionResult ProjectComponents(const FLDDemoContentSnapshot& Catalog, uint8 Level, const TArray<FInstall>& Rows)
{
    FLDAttributeBuildValues Values(Level, {});
    for (const FInstall& Row : Rows)
    {
        FLDInstalledComponentValue& Installed = Values.Components.AddDefaulted_GetRef();
        Installed.ComponentId = FName(Row.Id);
        Installed.BoundActive = Row.BoundActive;
        Installed.Element = Row.Element;
        Installed.TargetKind = Row.TargetKind;
        Installed.TargetId = Row.TargetId;
        Installed.BoundComponent = Row.BoundComponent;
    }
    return LDAttributeProjection::Project(Values, Catalog);
}

// Installs a projection with Rows on a fixture contender at Level (its record's level is what G reads).
bool InstallComponents(FAutomationTestBase& Test, FFixture& F, const FLDCombatBodyRef& Ref, uint8 Level, const TArray<FInstall>& Rows)
{
    const FLDAttributeProjectionResult Result = ProjectComponents(*F.Catalog, Level, Rows);
    if (!Result.IsSuccess())
    {
        Test.AddError(FString::Printf(TEXT("Component projection: %s: %s"), *Result.Error.Field, *Result.Error.Message));
        return false;
    }
    FLDBodyCombatState& Body = F.Body(Ref);
    Body.BuildProjection = Body.AttributeProjection = Result.Value.GetValue();
    FAccess::Records(*F.Driver).FindChecked(Ref.Victim.EntityId).Build.Level = Level;
    return true;
}

bool SameRational(const FLDDemoRational& Value, int64 Numerator, int64 Denominator = 1)
{
    return Value.Denominator > 0 && Value.Numerator * Denominator == Numerator * Value.Denominator;
}

FLDCombatSourceRef SourceOf(ELDCombatSourceKind Kind, const TCHAR* Id)
{
    FLDCombatSourceRef Out;
    Out.Kind = Kind;
    Out.DefinitionId = FName(Id);
    return Out;
}

// #198 §10: the component triggers of a contender's current build version (0 before its first fact).
int64 Triggers(const FFixture& F, const FLDCombatBodyRef& Ref)
{
    const TArray<FLDBlueprintVersion>* Versions = F.Driver->GetBuildLedger().Find(Ref.Victim.EntityId);
    return Versions && !Versions->IsEmpty() ? Versions->Last().Facts[int32(ELDBlueprintFact::Triggers)].Value : 0;
}

FLDCombatAction BasicAt(const FLDCombatBodyRef& Target, uint64 Token)
{
    FLDCombatAction Action;
    Action.Kind = ELDCombatActionKind::Basic;
    Action.Key.Origin = ELDCombatRequestOrigin::Nav;
    Action.Token = Token;
    Action.Entry = ELDCombatEntry::Basic;
    Action.Source.Kind = ELDCombatSourceKind::Basic;
    Action.TargetBody = Target;
    return Action;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentProjectionTest, "Lockdown.Rules.Combat.Components.Projection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentProjectionTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    using EAttribute = ELDProjectedAttribute;
    using EError = ELDAttributeProjectionError;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDDemoContentParameters& P = F.Catalog->Get().Parameters;
    // B13-B15 (skill rules, no stat terms) and B07's control amplification, listed out of order.
    const FLDAttributeProjectionResult Result = ProjectComponents(*F.Catalog, 10, {{TEXT("component_seeing_red")},
        {TEXT("component_catch_breath")}, {TEXT("component_kick_down")}, {TEXT("component_long_arm")}});
    if (!TestTrue(FString::Printf(TEXT("Installed components project: %s"), *Result.Error.Message), Result.IsSuccess())) { return false; }
    const FLDAttributeProjection& Projected = Result.Value.GetValue();
    const auto Row = [&Projected](EAttribute Attribute) -> const FLDAttributeContributions& { return Projected.Contributions[int32(Attribute)]; };
    TestTrue(TEXT("B13-B15 add no stat or low-HP term"), SameRational(Row(EAttribute::CooldownHaste).Components.Flat, 0)
        && Projected.Final.CooldownHasteP2 == P.CooldownHasteBaseP2 && SameRational(Row(EAttribute::MPRegen).Components.Flat, 0)
        && SameRational(Row(EAttribute::AttackRange).Components.Flat, 0) && !Projected.LowHPBonus.IsSet());
    {
        TArray<FLDPassiveEffectRef> Refund, Distance, Alternate;
        FString RuleError;
        TestTrue(TEXT("B13-B15 are trigger effects read by Combat, ratios and times never grown"),
            FAccess::Collect(*F.Driver, Projected, ELDDemoEffectOp::CastHitRefund, Refund, RuleError)
            && FAccess::Collect(*F.Driver, Projected, ELDDemoEffectOp::DistanceAmp, Distance, RuleError)
            && FAccess::Collect(*F.Driver, Projected, ELDDemoEffectOp::AlternateAmp, Alternate, RuleError)
            && Refund.Num() == 1 && Refund[0].Effect.RatioBp == 3000
            && Distance.Num() == 1 && Distance[0].Effect.RatioBp == 2000 && SameRational(Distance[0].Effect.DistanceCm, 550)
            && Alternate.Num() == 1 && Alternate[0].Effect.RatioBp == 2000 && Alternate[0].Effect.DurationMs == 4000);
    }
    TestTrue(TEXT("B07 is the control amplification term (+12%)"), Projected.ControlAmp.IsSet()
        && Projected.ControlAmp->RatioBp == 1200 && Projected.ControlAmp->ComponentId == FName(TEXT("component_kick_down")));
    TestTrue(TEXT("Every installed component is listed once, sorted by id, at the build level"),
        Projected.InstalledComponents.Num() == 4
        && Projected.InstalledComponents[0].ComponentId == FName(TEXT("component_catch_breath"))
        && Projected.InstalledComponents[1].ComponentId == FName(TEXT("component_kick_down"))
        && Projected.InstalledComponents[2].ComponentId == FName(TEXT("component_long_arm"))
        && Projected.InstalledComponents[3].ComponentId == FName(TEXT("component_seeing_red"))
        && !Projected.InstalledComponents.ContainsByPredicate([](const FLDInstalledComponentTerm& Term) { return Term.Level != 10; }));
    const FLDAttributeProjectionResult Plain = ProjectComponents(*F.Catalog, 10, {});
    TestTrue(TEXT("Without components there is no term"), Plain.IsSuccess() && !Plain.Value->LowHPBonus.IsSet()
        && !Plain.Value->ControlAmp.IsSet() && Plain.Value->InstalledComponents.IsEmpty()
        && SameRational(Plain.Value->Contributions[int32(EAttribute::CooldownHaste)].Components.Flat, 0));

    // Explicit refusals.
    const auto Refused = [&F](const TArray<FInstall>& Rows, EError Code)
    {
        const FLDAttributeProjectionResult Bad = ProjectComponents(*F.Catalog, 1, Rows);
        return !Bad.IsSuccess() && Bad.Error.Code == Code;
    };
    TestTrue(TEXT("A component installed twice is refused"), Refused({{TEXT("component_long_arm")}, {TEXT("component_long_arm")}},
        EError::DuplicateSource));
    TestTrue(TEXT("An unknown component is refused"), Refused({{TEXT("component_nope")}}, EError::UnknownDefinition));
    TestTrue(TEXT("B18 without its binding is refused"), Refused({{TEXT("component_dirty_trick")}}, EError::InputDomain));
    TestTrue(TEXT("A binding on an unbound component is refused"),
        Refused({{TEXT("component_long_arm"), TEXT("active_elbow"), ELDDemoElement::Fire}}, EError::InputDomain));
    TestTrue(TEXT("B18 bound to an active outside its list is refused"),
        Refused({{TEXT("component_dirty_trick"), TEXT("active_bail_out"), ELDDemoElement::Fire}}, EError::InputDomain));
    const FLDAttributeProjectionResult Bound = ProjectComponents(*F.Catalog, 1,
        {{TEXT("component_dirty_trick"), TEXT("active_elbow"), ELDDemoElement::Fire}});
    TestTrue(TEXT("B18 keeps its bound active and element"), Bound.IsSuccess() && Bound.Value->InstalledComponents.Num() == 1
        && Bound.Value->InstalledComponents[0].BoundActive == FName(TEXT("active_elbow"))
        && Bound.Value->InstalledComponents[0].Element == ELDDemoElement::Fire);

    // Trigger effects arrive with their fixed amounts grown for the holder's level; ratios never grow (B16 at G(10)=2.35).
    const FLDAttributeProjectionResult Guard = ProjectComponents(*F.Catalog, 10, {{TEXT("component_thick_hide")}});
    TArray<FLDPassiveEffectRef> Held;
    FString Error;
    if (!TestTrue(TEXT("B16 projects"), Guard.IsSuccess())
        || !TestTrue(FString::Printf(TEXT("B16 resolves: %s"), *Error),
            FAccess::Collect(*F.Driver, Guard.Value.GetValue(), ELDDemoEffectOp::TriggeredShield, Held, Error)))
    { return false; }
    TestTrue(TEXT("B16 holds its ability-hit shield 40G = 94 at level 10, 3% max HP, 3 s, 8 s rest"), Held.Num() == 1
        && Held[0].Effect.Trigger == ELDDemoEffectTrigger::AbilityHit && Held[0].Effect.BaseP2 == 9400
        && Held[0].Effect.MaxHPRatioBp == 300 && Held[0].Effect.DurationMs == 3000 && Held[0].Effect.IntervalMs == 8000
        && Held[0].Source.Kind == ELDCombatSourceKind::Component && Held[0].Source.DefinitionId == FName(TEXT("component_thick_hide")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentEmpowerMergeTest, "Lockdown.Rules.Combat.Components.EmpowerMerge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentEmpowerMergeTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Attacker = F.AddBody(), Camp = F.AddBody(ELDVictimKind::CampMember);
    if (!InstallComponents(*this, F, Attacker, 1, {{TEXT("component_punch_heal")}, {TEXT("component_third_punch")}}))
    { return false; }
    FLDBodyCombatState& Body = F.Body(Attacker);
    // Spec §10.2: one pending package per source; the same source re-arming replaces its unused value, others coexist.
    FLDCombatEmpower Dash;
    Dash.Source = SourceOf(ELDCombatSourceKind::Learned, TEXT("active_apology_slide"));
    Dash.AmountP2 = 4000;
    Dash.ExpiresAtMs = 4000;
    LDCombatControl::ArmEmpower(Body.Empowers, Dash);
    Dash.AmountP2 = 5000;
    LDCombatControl::ArmEmpower(Body.Empowers, Dash);
    TestTrue(TEXT("The same source re-arming replaces its package"), Body.Empowers.Num() == 1 && Body.Empowers[0].AmountP2 == 5000);
    FLDCombatEmpower Punch;
    Punch.Source = SourceOf(ELDCombatSourceKind::Component, TEXT("component_dash_punch"));
    Punch.AmountP2 = 2500;
    Punch.ExpiresAtMs = 4000;
    LDCombatControl::ArmEmpower(Body.Empowers, Punch);
    TestEqual(TEXT("Another source's package coexists"), Body.Empowers.Num(), 2);
    const int64 BasicRatioBp = F.Catalog->Get().Character.BasicADRatioBp;
    FString Error;
    for (int32 Swing = 1; Swing <= 3; ++Swing)
    {
        FAccess::PendingHits(*F.Driver).Reset();
        if (!TestTrue(FString::Printf(TEXT("Swing %d lands: %s"), Swing, *Error),
            FAccess::LandBasic(*F.Driver, F.Actor(Attacker), Body, BasicAt(Camp, uint64(Swing)), Error)))
        { return false; }
        const TArray<FLDCombatHit>& Hits = FAccess::PendingHits(*F.Driver);
        if (!TestEqual(FString::Printf(TEXT("Swing %d is one primary hit"), Swing), Hits.Num(), 1)) { return false; }
        const FLDCombatHit& Primary = Hits[0];
        TestEqual(FString::Printf(TEXT("Swing %d: the basic keeps its own AD package"), Swing), Primary.ADRatioBp, BasicRatioBp);
        TestTrue(FString::Printf(TEXT("Swing %d: B04 lifesteal rides on the one primary hit"), Swing),
            Primary.bLifesteal && Primary.LifestealRatioBp == 1200 && Primary.OnHitScaleBp == 10000);
        TestEqual(FString::Printf(TEXT("Swing %d: B06 counts one primary hit per swing"), Swing), Body.LandedBasics, Swing);
        if (Swing == 1)
        {
            TestTrue(TEXT("Both packages merge into the first primary hit, unscaled, and are consumed once"),
                Primary.BaseP2 == 7500 && Body.Empowers.IsEmpty());
            const auto Part = [](int64 ValueP2, int64 Bp) { return ValueP2 / 10000 * Bp + ValueP2 % 10000 * Bp / 10000; };
            const int64 OwnP2 = Part(Primary.SourceADP2, Primary.ADRatioBp) + Part(Primary.SourceAPP2, Primary.APRatioBp);
            TestEqual(TEXT("B04 heals only the basic's own share of the merged package"), Primary.LifestealShareBp,
                OwnP2 * 10000 / (OwnP2 + 7500));
        }
        else
        {
            TestTrue(FString::Printf(TEXT("Swing %d is plain and wholly its own"), Swing),
                Primary.BaseP2 == 0 && Primary.LifestealShareBp == 10000);
        }
        TestTrue(FString::Printf(TEXT("Swing %d: B06 pushes 120 cm over 0.12 s on the third basic only"), Swing),
            Swing == 3 ? Primary.KnockbackCm == 120. && Primary.KnockbackMs == 120 && !Primary.bKnockbackFromAnchor
                : Primary.KnockbackCm == 0. && Primary.KnockbackMs == 0);
    }
    TestEqual(TEXT("Only the consumed B12 package is a trigger"),
        Triggers(F, Attacker), int64(1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentDashEmpowerTest, "Lockdown.Rules.Combat.Components.DashEmpower",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentDashEmpowerTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Runner = F.AddBody(), Plain = F.AddBody();
    if (!InstallComponents(*this, F, Runner, 1, {{TEXT("component_dash_punch")}})) { return false; }
    FLDBodyCombatState& Body = F.Body(Runner);
    ALDCharacter& Actor = F.Actor(Runner);
    Actor.SetActorLocation(FVector(0, 0, 200));
    const auto StartDash = [](FLDBodyCombatState& Into, const ALDCharacter& From, int64 EndMs)
    {
        FLDCombatVoluntaryDash& Dash = Into.VoluntaryDash.Emplace();
        Dash.EffectId = FGuid::NewGuid();
        Dash.Source = SourceOf(ELDCombatSourceKind::Innate, TEXT("innate_dash"));
        Dash.StartFootCm = ALDCombatDirector::FootOf(From);
        Dash.EndMs = EndMs;
    };
    FString Error;
    StartDash(Body, Actor, 1200);
    Actor.SetActorLocation(FVector(300, 0, 200));
    TestTrue(TEXT("Before its end a dash arms nothing"), FAccess::ResolveDashCompletions(*F.Driver, 1100, Error)
        && Body.Empowers.IsEmpty() && Body.VoluntaryDash.IsSet());
    TestTrue(TEXT("A completed voluntary dash that moved arms B12"), FAccess::ResolveDashCompletions(*F.Driver, 1200, Error)
        && Body.Empowers.Num() == 1 && !Body.VoluntaryDash.IsSet());
    const int64 ExpectedP2 = 2500 + Body.AttributeProjection.Final.ADP2 * 2500 / 10000;
    TestTrue(TEXT("B12 snapshots 25G + 0.25 AD at the completion and lasts 3 s"), Body.Empowers.Num() == 1
        && Body.Empowers[0].AmountP2 == ExpectedP2 && Body.Empowers[0].ExpiresAtMs == 4200 && Body.Empowers[0].Charges == 1
        && Body.Empowers[0].Source.Kind == ELDCombatSourceKind::Component
        && Body.Empowers[0].Source.DefinitionId == FName(TEXT("component_dash_punch")));
    StartDash(Body, Actor, 2000);
    TestTrue(TEXT("A dash blocked in place is no completed dash"), FAccess::ResolveDashCompletions(*F.Driver, 2000, Error)
        && Body.Empowers.Num() == 1 && Body.Empowers[0].ExpiresAtMs == 4200 && !Body.VoluntaryDash.IsSet());
    StartDash(Body, Actor, 2500);
    Actor.SetActorLocation(FVector(600, 0, 200));
    // ApplyKnockback drops the voluntary dash it interrupts.
    Body.VoluntaryDash.Reset();
    TestTrue(TEXT("A knocked-back (cancelled) dash arms nothing"), FAccess::ResolveDashCompletions(*F.Driver, 2500, Error)
        && Body.Empowers[0].ExpiresAtMs == 4200);
    StartDash(Body, Actor, 3000);
    Actor.SetActorLocation(FVector(900, 0, 200));
    TestTrue(TEXT("A later completed dash replaces the unused package of the same source"),
        FAccess::ResolveDashCompletions(*F.Driver, 3000, Error) && Body.Empowers.Num() == 1 && Body.Empowers[0].ExpiresAtMs == 6000);
    FLDBodyCombatState& Other = F.Body(Plain);
    StartDash(Other, F.Actor(Plain), 3000);
    F.Actor(Plain).SetActorLocation(FVector(0, 500, 200));
    TestTrue(TEXT("Without B12 a completed dash arms nothing"), FAccess::ResolveDashCompletions(*F.Driver, 3000, Error)
        && Other.Empowers.IsEmpty() && !Other.VoluntaryDash.IsSet());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentLedgerTest, "Lockdown.Rules.Combat.Components.Ledger",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentLedgerTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    using namespace LDCombatKeywords;
    // The pure batch rule.
    FLDCombatLedger Ledger;
    int64 CashP2 = 0;
    bool bRefused = false;
    for (int32 Entry = 1; Entry <= 4; ++Entry)
    {
        TestFalse(FString::Printf(TEXT("Entry %d is booked"), Entry), AddLedgerEntry(Ledger, 1000, 1000 + Entry * 100, 5, 6000, CashP2, bRefused)
            || bRefused);
    }
    TestTrue(TEXT("The fifth entry cashes the batch out at once"), AddLedgerEntry(Ledger, 1300, 1500, 5, 6000, CashP2, bRefused)
        && CashP2 == 5300 && Ledger.EntriesP2.IsEmpty());
    TestFalse(TEXT("A new batch starts"), AddLedgerEntry(Ledger, 1000, 2000, 5, 6000, CashP2, bRefused));
    TestFalse(TEXT("Its window has not passed"), ExpireLedger(Ledger, 7999));
    TestTrue(TEXT("6 s without a new entry clears the unlocked entries"), ExpireLedger(Ledger, 8000) && Ledger.EntriesP2.IsEmpty());
    TestFalse(TEXT("A batch fixes its threshold at its first entry"), AddLedgerEntry(Ledger, 1000, 9000, 3, 6000, CashP2, bRefused));
    TestFalse(TEXT("a later threshold does not move it"), AddLedgerEntry(Ledger, 1000, 9100, 5, 6000, CashP2, bRefused));
    TestTrue(TEXT("so the third entry cashes out"), AddLedgerEntry(Ledger, 1000, 9200, 5, 6000, CashP2, bRefused) && CashP2 == 3000);
    TestFalse(TEXT("An idle batch is cleared before the next entry"), AddLedgerEntry(Ledger, 1000, 10000, 5, 6000, CashP2, bRefused)
        || AddLedgerEntry(Ledger, 1000, 16000, 5, 6000, CashP2, bRefused));
    TestEqual(TEXT("leaving only the new entry"), Ledger.EntriesP2.Num(), 1);
    TestTrue(TEXT("A ledger without a threshold is refused"), !AddLedgerEntry(Ledger, 1000, 16100, 0, 6000, CashP2, bRefused) && bRefused);

    // B02 on Combat: an owner at level 3 books 10G = 13 per effective hit; the fifth cashes out once as magic damage.
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Owner = F.AddBody(), Target = F.AddBody();
    if (!InstallComponents(*this, F, Owner, 3, {{TEXT("component_tab_ledger")}})) { return false; }
    FLDBodyCombatState& Victim = F.Body(Target);
    FString Error;
    for (int32 Hit = 1; Hit <= 4; ++Hit)
    {
        TestTrue(FString::Printf(TEXT("Hit %d lands: %s"), Hit, *Error), F.Apply(FFixture::Hit(Owner, Target, 10000), Error));
    }
    TestTrue(TEXT("Four effective hits book four entries of 13 on the target life"), Victim.Ledgers.Num() == 1
        && Victim.Ledgers[0].EntriesP2 == TArray<int64>({1300, 1300, 1300, 1300}) && FAccess::PendingHits(*F.Driver).IsEmpty());
    TestEqual(TEXT("Booked entries are records, not B02 triggers"), Triggers(F, Owner), int64(0));
    FLDCombatHit Tick = FFixture::Hit(Owner, Target, 1000);
    Tick.bBurnTick = Tick.bPeriodic = true;
    TestTrue(TEXT("A burn tick is status damage and books nothing"), F.Apply(Tick, Error) && Victim.Ledgers[0].EntriesP2.Num() == 4);
    TestTrue(TEXT("A zero hit books nothing"), F.Apply(FFixture::Hit(Owner, Target, 0), Error) && Victim.Ledgers[0].EntriesP2.Num() == 4);
    TestTrue(TEXT("The fifth hit lands"), F.Apply(FFixture::Hit(Owner, Target, 10000), Error));
    const TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    TestTrue(TEXT("The batch cashes out as one magic skill-class package of 65"), Pending.Num() == 1 && Pending[0].bLedgerCashOut
        && Pending[0].BaseP2 == 6500 && Pending[0].DamageType == ELDDemoDamageType::Magical
        && Pending[0].HitClass == ELDCombatHitClass::Ability && Pending[0].Source.DefinitionId == FName(TEXT("component_tab_ledger"))
        && Victim.Ledgers.IsEmpty());
    const int64 Before = Victim.Resources.HpP2;
    TestTrue(TEXT("The cash-out lands"), F.Flush(Error) && Before - Victim.Resources.HpP2 == 6500);
    TestTrue(TEXT("and never books an entry itself"), Victim.Ledgers.IsEmpty());
    TestEqual(TEXT("The cash-out is one B02 trigger of its owner, its hit no second one"), Triggers(F, Owner), int64(1));
    return true;
}

// #198 B10/B11/B18: a component's element rides its hit. Fire adds a burn layer credited to the component; water and
// electric set the hit's element flags and ApplyElements marks the target (B10 wet / B11 electric on a primary basic, B18 on
// a native direct hit of its bound active).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentElementsTest, "Lockdown.Rules.Combat.Components.Elements",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentElementsTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatSourceRef Trick = SourceOf(ELDCombatSourceKind::Component, TEXT("component_dirty_trick"));
    FString Error;
    FLDCombatHit Probe;
    TestTrue(TEXT("Fire adds one burn layer of the component"), FAccess::ApplyComponentElement(*F.Driver, Probe, ELDDemoElement::Fire,
        Trick, Error) && Probe.ComponentBurnStacks == 1 && Probe.ComponentBurnSource == Trick);
    FLDCombatHit Wet, Zap;
    TestTrue(TEXT("Water names the component as the hit's own water application"), FAccess::ApplyComponentElement(*F.Driver, Wet,
        ELDDemoElement::Water, Trick, Error) && Wet.ComponentWetSource == Trick && !Wet.bApplyWet
        && Wet.ComponentElectricSource.Kind == ELDCombatSourceKind::None);
    TestTrue(TEXT("Electric names the component as the hit's own electric application"), FAccess::ApplyComponentElement(*F.Driver, Zap,
        ELDDemoElement::Electric, Trick, Error) && Zap.ComponentElectricSource == Trick && !Zap.bApplyElectric
        && Zap.ComponentWetSource.Kind == ELDCombatSourceKind::None);
    TestFalse(TEXT("Two components' water on one hit is an error"), FAccess::ApplyComponentElement(*F.Driver, Wet,
        ELDDemoElement::Water, SourceOf(ELDCombatSourceKind::Component, TEXT("component_fist_water")), Error));
    TestFalse(TEXT("No element is an error"), FAccess::ApplyComponentElement(*F.Driver, Probe, ELDDemoElement::None, Trick, Error));

    // B10 / B11: a primary basic of the holder marks its target wet / electric for four seconds; an ability hit does not.
    const FLDCombatBodyRef Splasher = F.AddBody(), Shocker = F.AddBody(), Soaked = F.AddBody(), Shocked = F.AddBody();
    if (!InstallComponents(*this, F, Splasher, 1, {{TEXT("component_fist_water"), NAME_None, ELDDemoElement::None}})
        || !InstallComponents(*this, F, Shocker, 1, {{TEXT("component_fist_shock"), NAME_None, ELDDemoElement::None}}))
    { return false; }
    const auto Basic = [](const FLDCombatBodyRef& From, const FLDCombatBodyRef& To)
    {
        FLDCombatHit Hit = FFixture::Hit(From, To, 1000);
        Hit.HitClass = ELDCombatHitClass::BasicPrimary;
        Hit.DamageType = ELDDemoDamageType::Physical;
        return Hit;
    };
    TestTrue(TEXT("An ability hit of B10's holder carries no water"), F.Apply(FFixture::Hit(Splasher, Soaked, 1000), Error)
        && F.Body(Soaked).ElementMark.Kind == ELDCombatElement::None);
    TestTrue(FString::Printf(TEXT("B10: a primary basic marks the target wet (%s)"), *Error), F.Apply(Basic(Splasher, Soaked), Error)
        && F.Body(Soaked).ElementMark.Kind == ELDCombatElement::Water && F.Body(Soaked).ElementMark.ExpiresAtMs == 5000);
    TestTrue(FString::Printf(TEXT("B11: a primary basic marks the target electric (%s)"), *Error), F.Apply(Basic(Shocker, Shocked), Error)
        && F.Body(Shocked).ElementMark.Kind == ELDCombatElement::Electric && F.Body(Shocked).ElementMark.ExpiresAtMs == 5000);
    TestTrue(TEXT("The hits land"), F.Flush(Error));

    // B18 bound to S01 (active_elbow) with fire: each native direct effective hit of S01 adds one layer.
    const FLDCombatBodyRef Owner = F.AddBody(), Target = F.AddBody();
    if (!InstallComponents(*this, F, Owner, 1, {{TEXT("component_dirty_trick"), TEXT("active_elbow"), ELDDemoElement::Fire}}))
    { return false; }
    FAccess::Records(*F.Driver).FindChecked(Owner.Victim.EntityId).Build.Actives = {{TEXT("active_elbow"), ELDCombatEntry::Q},
        {TEXT("active_snark_bolt"), ELDCombatEntry::W}};
    const auto Native = [&](const TCHAR* Active, bool bDirect)
    {
        FLDCombatHit Hit = FFixture::Hit(Owner, Target, 1000);
        Hit.Source = SourceOf(ELDCombatSourceKind::Learned, Active);
        Hit.Token = 1;
        Hit.bDirectAbilityDamage = bDirect;
        return Hit;
    };
    FLDBodyCombatState& Victim = F.Body(Target);
    TestTrue(TEXT("A native hit of the bound active lands"), F.Apply(Native(TEXT("active_elbow"), true), Error));
    TestTrue(TEXT("and adds one fire layer credited to B18"), Victim.Burn.Layers.Num() == 1 && Victim.Burn.Layers[0].Source == Trick);
    TestTrue(TEXT("Another active's hit adds none"), F.Apply(Native(TEXT("active_snark_bolt"), true), Error) && Victim.Burn.Layers.Num() == 1);
    TestTrue(TEXT("A derived hit of the bound active adds none"), F.Apply(Native(TEXT("active_elbow"), false), Error)
        && Victim.Burn.Layers.Num() == 1);
    FLDCombatHit Zero = Native(TEXT("active_elbow"), true);
    Zero.BaseP2 = 0;
    TestTrue(TEXT("An ineffective hit adds none"), F.Apply(Zero, Error) && Victim.Burn.Layers.Num() == 1);

    // B18 bound with water / electric: a native direct hit of the bound active marks the target; another active's does not.
    for (const ELDDemoElement Element : {ELDDemoElement::Water, ELDDemoElement::Electric})
    {
        const FLDCombatBodyRef Binder = F.AddBody(), Marked = F.AddBody();
        if (!InstallComponents(*this, F, Binder, 1, {{TEXT("component_dirty_trick"), TEXT("active_elbow"), Element}})) { return false; }
        FAccess::Records(*F.Driver).FindChecked(Binder.Victim.EntityId).Build.Actives = {{TEXT("active_elbow"), ELDCombatEntry::Q},
            {TEXT("active_snark_bolt"), ELDCombatEntry::W}};
        const ELDCombatElement Expected = Element == ELDDemoElement::Water ? ELDCombatElement::Water : ELDCombatElement::Electric;
        const auto BoundHit = [&](const TCHAR* Active)
        {
            FLDCombatHit Hit = FFixture::Hit(Binder, Marked, 1000);
            Hit.Source = SourceOf(ELDCombatSourceKind::Learned, Active);
            Hit.Token = 1;
            Hit.bDirectAbilityDamage = true;
            return Hit;
        };
        TestTrue(FString::Printf(TEXT("B18 element %u: another active's hit marks nothing"), uint32(Element)),
            F.Apply(BoundHit(TEXT("active_snark_bolt")), Error) && F.Body(Marked).ElementMark.Kind == ELDCombatElement::None);
        TestTrue(FString::Printf(TEXT("B18 element %u: the bound active's native hit marks the target (%s)"), uint32(Element), *Error),
            F.Apply(BoundHit(TEXT("active_elbow")), Error) && F.Body(Marked).ElementMark.Kind == Expected);
        TestTrue(TEXT("The hits land"), F.Flush(Error));
    }
    return true;
}
#endif
// ---- #198 F mods and connectors (LDCombatWorkshop.cpp).
namespace LDBuildPrimitivesTests
{
FInstall On(const TCHAR* Id, ELDComponentTargetKind Kind, const TCHAR* Target = nullptr)
{
    FInstall Out;
    Out.Id = Id;
    Out.TargetKind = Kind;
    Out.TargetId = Target ? FName(Target) : NAME_None;
    return Out;
}

bool ActiveTerms(FAutomationTestBase& Test, FFixture& F, const TCHAR* Active, FLDCombatActiveTerms& Out)
{
    const FLDDemoAbilityDefinition* Ability = nullptr;
    FString Error;
    if (!F.Catalog->FindAbility(Active, Ability, Error) || !Ability) { Test.AddError(FString(TEXT("Missing active ")) + Active); return false; }
    const FLDDemoAbilityValues* Values = F.Catalog->FindValues(*Ability, 1);
    if (!Values) { Test.AddError(TEXT("Missing level-1 values")); return false; }
    Out = FLDCombatActiveTerms();
    Out.Target = Ability->Target;
    Out.Values = *Values;
    Out.WindupMs = Ability->WindupMs;
    return true;
}

const FLDDemoEffectDefinition* FindEffect(const FLDDemoAbilityValues& Values, ELDDemoEffectOp Op)
{
    return Values.Effects.FindByPredicate([Op](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp() == Op; });
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorkshopCompileTest, "Lockdown.Rules.Combat.Workshop.Compile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDWorkshopCompileTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    using EKind = ELDComponentTargetKind;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Owner = F.AddBody();
    FLDBodyCombatState& Body = F.Body(Owner);
    FString Error;
    // M07 on S22: three 0/300/600 ms waves become one 35-degree cone 600 ms after release carrying 3 x 115% of a wave.
    if (!InstallComponents(*this, F, Owner, 1, {On(TEXT("component_one_slam"), EKind::Active, TEXT("active_open_mic")),
        On(TEXT("component_hold_down"), EKind::Active, TEXT("active_pants_down")),
        On(TEXT("component_again_now"), EKind::Active, TEXT("active_pants_down")),
        On(TEXT("component_ram_through"), EKind::Dash)}))
    { return false; }
    FLDCombatActiveTerms Cone;
    if (!ActiveTerms(*this, F, TEXT("active_open_mic"), Cone)) { return false; }
    if (!TestTrue(FString::Printf(TEXT("S22 compiles: %s"), *Error), FAccess::Compile(*F.Driver, Body.AttributeProjection,
        SourceOf(ELDCombatSourceKind::Learned, TEXT("active_open_mic")), Cone, Error))) { return false; }
    TestTrue(TEXT("M07 marks S22 compressed"), Cone.CompressCone.IsSet() && !Cone.CompressZone.IsSet());
    FLDCombatAction Action;
    Action.Direction = FVector(1, 0, 0);
    FLDCombatHit Payload;
    Payload.EffectId = FGuid::NewGuid();
    bool bHandled = false;
    TestTrue(TEXT("The compressed cone is delivered"), FAccess::DeliverTerms(*F.Driver, Body, Cone, Action, Payload, 1000, bHandled, Error)
        && bHandled && FAccess::Channels(*F.Driver).Num() == 1);
    if (FAccess::Channels(*F.Driver).Num() == 1)
    {
        const FLDCombatChannel& Channel = FAccess::Channels(*F.Driver)[0];
        TestTrue(TEXT("One 35-degree wave at +600 ms carrying 3 x 115% (with the final slow)"), Channel.Remaining == 1
            && Channel.NextPulseMs == 1600 && Channel.AngleDegrees == 35. && Channel.Payload.PackageScaleBp == 34500);
    }
    // M07 on S08: the three pulses become one fixed blast after 900 ms, radius x 0.75, 3 x 110% of a pulse, burn and slow once.
    FFixture G;
    if (!G.Init(*this)) { return false; }
    const FLDCombatBodyRef Mage = G.AddBody();
    if (!InstallComponents(*this, G, Mage, 1, {On(TEXT("component_one_slam"), EKind::Active, TEXT("active_quick_meeting"))})) { return false; }
    FLDCombatActiveTerms Zone;
    if (!ActiveTerms(*this, G, TEXT("active_quick_meeting"), Zone)
        || !TestTrue(TEXT("S08 compiles"), FAccess::Compile(*G.Driver, G.Body(Mage).AttributeProjection,
            SourceOf(ELDCombatSourceKind::Learned, TEXT("active_quick_meeting")), Zone, Error) && Zone.CompressZone.IsSet()))
    { return false; }
    TestTrue(TEXT("The compressed zone releases"), FAccess::ReleaseTerms(*G.Driver, G.Body(Mage),
        SourceOf(ELDCombatSourceKind::Learned, TEXT("active_quick_meeting")), Zone, FVector::ZeroVector, FVector(300, 0, 0), Error));
    const TArray<FLDCombatGroundEffect>& Blasts = FAccess::Ground(*G.Driver);
    TestTrue(TEXT("No lasting zone, one blast"), FAccess::Zones(*G.Driver).IsEmpty() && Blasts.Num() == 1);
    if (Blasts.Num() == 1)
    {
        const FLDCombatGroundEffect& Blast = Blasts[0];
        TestTrue(TEXT("Radius 240 x 0.75, +900 ms, 3 x 110%, burn and slow once, a C06 completion"),
            FMath::IsNearlyEqual(Blast.RadiusCm, 180.) && Blast.ReadyAtMs == 1900 && Blast.Payload.PackageScaleBp == 33000
            && Blast.Payload.BurnStacks == 1 && Blast.Payload.SlowBp == 2000 && Blast.Payload.bDirectAbilityDamage
            && Blast.Origin == ELDCombatGroundOrigin::CompressedZone && Blast.Payload.Source.DefinitionId == FName(TEXT("active_quick_meeting")));
    }
    // M07 on S10: no bounce; the first body takes (1 + 0.7 + 0.49) x 0.8 = 1.752 of the first hop.
    FFixture H;
    if (!H.Init(*this)) { return false; }
    const FLDCombatBodyRef Caster = H.AddBody();
    if (!InstallComponents(*this, H, Caster, 1, {On(TEXT("component_one_slam"), EKind::Active, TEXT("active_zap"))})) { return false; }
    FLDCombatActiveTerms Chain;
    if (!ActiveTerms(*this, H, TEXT("active_zap"), Chain)) { return false; }
    TestTrue(TEXT("M07 compresses S10's chain into one 175.2% hit"), FAccess::Compile(*H.Driver, H.Body(Caster).AttributeProjection,
        SourceOf(ELDCombatSourceKind::Learned, TEXT("active_zap")), Chain, Error) && Chain.CompressChain.IsSet()
        && Chain.PackageScaleBp == 17520);
    // M04 + M06 on S02: root 1.5 s x 1.25; cooldown 12 s x 1.25 x 0.8; mana 55 x 1.25 rounded up to 0.01.
    FLDCombatActiveTerms Root;
    if (!ActiveTerms(*this, F, TEXT("active_pants_down"), Root)
        || !TestTrue(TEXT("S02 compiles"), FAccess::Compile(*F.Driver, Body.AttributeProjection,
            SourceOf(ELDCombatSourceKind::Learned, TEXT("active_pants_down")), Root, Error)))
    { return false; }
    const FLDDemoEffectDefinition* Hold = FindEffect(Root.Values, ELDDemoEffectOp::Root);
    TestTrue(TEXT("M04 and M06 rewrite S02's control, cooldown and cost"), Hold && Hold->DurationMs == 1875
        && Root.Values.CooldownMs == 12000 && Root.Values.ManaCostP2 == 6875);
    // M08 on D: distance and time x 1.5 (same speed), cooldown x 1.25, contact x 1.3.
    FLDCombatActiveTerms Dash;
    Dash.Target = F.Catalog->Get().Character.Dash.Target;
    Dash.Values = F.Catalog->Get().Character.Dash.Values;
    const FLDDemoEffectDefinition* Before = FindEffect(Dash.Values, ELDDemoEffectOp::Dash);
    const int64 BeforeMs = Before ? Before->DurationMs : 0;
    TestTrue(TEXT("D compiles"), FAccess::Compile(*F.Driver, Body.AttributeProjection, SourceOf(ELDCombatSourceKind::Innate,
        TEXT("innate_dash")), Dash, Error));
    const FLDDemoEffectDefinition* After = FindEffect(Dash.Values, ELDDemoEffectOp::Dash);
    TestTrue(TEXT("M08 on D scales its reach, time, cooldown and contact"), After && After->DurationMs == BeforeMs * 3 / 2
        && Dash.DashScaleBp == 15000 && Dash.CooldownScaleBp == 12500 && Dash.ContactScaleBp == 13000);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorkshopControlMarkTest, "Lockdown.Rules.Combat.Workshop.ControlMark",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDWorkshopControlMarkTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    using EKind = ELDComponentTargetKind;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(), Victim = F.AddBody(), Second = F.AddBody(), Third = F.AddBody(), Fourth = F.AddBody();
    // C05 and C07 both sit on S02's root.
    if (!InstallComponents(*this, F, Holder, 1, {On(TEXT("component_pin_punch"), EKind::Active, TEXT("active_pants_down")),
        On(TEXT("component_control_mana"), EKind::Active, TEXT("active_pants_down"))}))
    { return false; }
    FLDBodyCombatState& Body = F.Body(Holder);
    Body.Resources.MaxMpP2 = 100000;
    Body.Resources.MpP2 = 0;
    const FGuid FirstCast = FGuid::NewGuid();
    const auto Control = [&](const FLDCombatBodyRef& To, ELDControlKind Kind, const TCHAR* Exit, const FGuid& EffectId)
    {
        FLDControlApplication App;
        App.Kind = Kind;
        App.EffectId = EffectId;
        App.SourceEntity = Holder.Victim.EntityId;
        App.SourceBody = Holder;
        App.CreditOwner = Holder.Victim.EntityId;
        App.DurationMs = 1500;
        App.Source = SourceOf(ELDCombatSourceKind::Learned, Exit);
        bool bApplied = false;
        FString Error;
        return F.Driver->CommitControl(F.Body(To), App, F.Mode->GetAuthorityNowMs(), bApplied, Error) && bApplied;
    };
    TestTrue(TEXT("S02's root lands"), Control(Victim, ELDControlKind::Root, TEXT("active_pants_down"), FirstCast));
    TestTrue(TEXT("C05 marks the rooted life for 3 s; C07 grants 4 MP"), Body.ControlMarks.Num() == 1
        && Body.ControlMarks[0].Kind == ELDControlKind::Root && Body.ControlMarks[0].ExpiresAtMs == 4000 && Body.Resources.MpP2 == 400);
    FAccess::PendingHits(*F.Driver).Reset();
    TestTrue(TEXT("A second root lands"), Control(Victim, ELDControlKind::Root, TEXT("active_crotch_grab"), FGuid::NewGuid()));
    TestTrue(TEXT("The same kind of hard control does not consume the mark"), FAccess::PendingHits(*F.Driver).IsEmpty()
        && Body.ControlMarks.Num() == 1);
    F.Now(1500);
    TestTrue(TEXT("Another source's stun lands"), Control(Victim, ELDControlKind::Stun, TEXT("active_crotch_grab"), FGuid::NewGuid()));
    const TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    TestTrue(TEXT("Another kind consumes the mark for one physical 70G package, adding no control"), Pending.Num() == 1
        && Pending[0].BaseP2 == 7000 && Pending[0].DamageType == ELDDemoDamageType::Physical && Pending[0].StunDurationMs == 0
        && Pending[0].Source.DefinitionId == FName(TEXT("component_pin_punch")) && Body.ControlMarks.IsEmpty());
    TestEqual(TEXT("C07 reads its own exit only"), Body.Resources.MpP2, int64(400));
    // C07: one original action grants at most 12 MP however many bodies it controls.
    for (const FLDCombatBodyRef& Other : {Second, Third, Fourth}) { Control(Other, ELDControlKind::Root, TEXT("active_pants_down"), FirstCast); }
    TestEqual(TEXT("The action's mana is capped at 12"), Body.Resources.MpP2, int64(1200));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorkshopKnockbackImpactTest, "Lockdown.Rules.Combat.Workshop.KnockbackImpact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDWorkshopKnockbackImpactTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(), Pushed = F.AddBody(), Bystander = F.AddBody();
    if (!InstallComponents(*this, F, Holder, 1, {On(TEXT("component_ball_kick"), ELDComponentTargetKind::Active, TEXT("active_elbow"))}))
    { return false; }
    FLDBodyCombatState& Body = F.Body(Pushed);
    FLDCombatHit Push = FFixture::Hit(Holder, Pushed, 0);
    Push.KnockbackCm = 300;
    Push.KnockbackMs = 250;
    Push.KnockbackSource = SourceOf(ELDCombatSourceKind::Learned, TEXT("active_crotch_grab"));
    FString Error;
    TestTrue(TEXT("A push of another exit is not watched"), FAccess::Watch(*F.Driver, F.Actor(Pushed), Body, Push, 1000, Error)
        && !Body.PendingImpact.IsSet());
    Push.KnockbackSource = SourceOf(ELDCombatSourceKind::Learned, TEXT("active_elbow"));
    if (!TestTrue(TEXT("S01's push is watched until its end"), FAccess::Watch(*F.Driver, F.Actor(Pushed), Body, Push, 1000, Error)
        && Body.PendingImpact.IsSet() && Body.PendingImpact->EndMs == 1250))
    { return false; }
    const FLDCombatKnockbackImpact Impact = Body.PendingImpact.GetValue();
    TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    // A static blocker: 30G + 0.2 AD physical and a 0.6 s stun on the pushed body.
    TestTrue(TEXT("Wall"), FAccess::Impact(*F.Driver, Body, Impact, false, nullptr, 1100, Error));
    TestTrue(TEXT("A wall hurts and stuns the pushed body only"), Pending.Num() == 1 && Pending[0].Target == Pushed
        && Pending[0].BaseP2 == 3000 && Pending[0].ADRatioBp == 2000 && Pending[0].StunDurationMs == 600
        && Pending[0].Source.DefinitionId == FName(TEXT("component_ball_kick")));
    TestTrue(TEXT("The wall's stun lands through the control path"), F.Flush(Error) && Body.Stuns.Num() == 1);
    // Another legal enemy body: both take the same package, nobody is stunned.
    TestTrue(TEXT("Body"), FAccess::Impact(*F.Driver, Body, Impact, true, &F.Actor(Bystander), 1100, Error));
    TestTrue(TEXT("Both bodies take the same physical package without a stun"), Pending.Num() == 2
        && Pending[0].Target == Pushed && Pending[1].Target == Bystander && Pending[0].BaseP2 == Pending[1].BaseP2
        && Pending[0].StunDurationMs == 0 && Pending[1].StunDurationMs == 0);
    Pending.Reset();
    // The holder's own body only blocks the motion.
    TestTrue(TEXT("Holder"), FAccess::Impact(*F.Driver, Body, Impact, true, &F.Actor(Holder), 1100, Error) && Pending.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorkshopDamageDeferTest, "Lockdown.Rules.Combat.Workshop.DamageDefer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDWorkshopDamageDeferTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Attacker = F.AddBody(), Holder = F.AddBody();
    if (!InstallComponents(*this, F, Holder, 1, {On(TEXT("component_owe_it"), ELDComponentTargetKind::None)})) { return false; }
    FLDBodyCombatState& Body = F.Body(Holder);
    FString Error;
    const int64 Start = Body.Resources.HpP2;
    if (!TestTrue(FString::Printf(TEXT("The enemy hit lands: %s"), *Error), F.Apply(FFixture::Hit(Attacker, Holder, 10000), Error))) { return false; }
    if (!TestEqual(TEXT("One debt"), Body.Debts.Num(), 1)) { return false; }
    const int64 Debt = Body.Debts[0].AmountP2;
    const int64 Now = Start - Body.Resources.HpP2;
    TestTrue(TEXT("25% of the mitigated damage is deferred for 3 s"), Debt > 0 && Debt == (Now + Debt) * 2500 / 10000
        && Body.Debts[0].DueAtMs == 4000);
    const int64 AfterHit = Body.Resources.HpP2;
    TestTrue(TEXT("A heal repays the debt first"), FAccess::Heal(*F.Driver, Body, 1000) && Body.Resources.HpP2 == AfterHit
        && Body.Debts.Num() == 1 && Body.Debts[0].AmountP2 == Debt - 1000);
    TestTrue(TEXT("Nothing is due before 3 s"), FAccess::AdvanceWorkshop(*F.Driver, 3999, Error) && FAccess::PendingHits(*F.Driver).IsEmpty());
    F.Now(4000);
    TestTrue(FString::Printf(TEXT("The debt falls due: %s"), *Error), FAccess::AdvanceWorkshop(*F.Driver, 4000, Error)
        && FAccess::PendingHits(*F.Driver).Num() == 1 && FAccess::PendingHits(*F.Driver)[0].bDebtPayment && Body.Debts.IsEmpty());
    TestTrue(TEXT("It is paid at its remaining amount, deferring nothing again"), F.Flush(Error)
        && Body.Resources.HpP2 == AfterHit - (Debt - 1000) && Body.Debts.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorkshopLedgerTest, "Lockdown.Rules.Combat.Workshop.Ledger",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDWorkshopLedgerTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    using EKind = ELDComponentTargetKind;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Owner = F.AddBody(), Target = F.AddBody();
    // M10 and M02 on the owner's B02.
    if (!InstallComponents(*this, F, Owner, 1, {{TEXT("component_tab_ledger")},
        On(TEXT("component_big_tab"), EKind::Component, TEXT("component_tab_ledger")),
        On(TEXT("component_settle_later"), EKind::Component, TEXT("component_tab_ledger"))}))
    { return false; }
    FLDBodyCombatState& Victim = F.Body(Target);
    FString Error;
    for (int32 Hit = 1; Hit <= 7; ++Hit)
    {
        TestTrue(FString::Printf(TEXT("Hit %d lands: %s"), Hit, *Error), F.Apply(FFixture::Hit(Owner, Target, 10000), Error));
    }
    TestTrue(TEXT("M10: seven entries of 10 x 1.25 wait for an 8-entry batch with a 4 s window"), Victim.Ledgers.Num() == 1
        && Victim.Ledgers[0].EntriesP2.Num() == 7 && Victim.Ledgers[0].EntriesP2[0] == 1250 && Victim.Ledgers[0].Count == 8
        && Victim.Ledgers[0].WindowMs == 4000);
    TestTrue(TEXT("The eighth hit lands"), F.Apply(FFixture::Hit(Owner, Target, 10000), Error));
    const TArray<FLDCombatLockedLedger>& Locked = FAccess::Locked(*F.Driver);
    TestTrue(TEXT("M02 locks the full batch for 2 s at 130% within 8 m instead of cashing it out now"),
        FAccess::PendingHits(*F.Driver).IsEmpty() && Locked.Num() == 1 && Locked[0].AmountP2 == 10000 && Locked[0].ScaleBp == 13000
        && Locked[0].DueAtMs == 3000 && Locked[0].MaxDistanceCm == 800. && Locked[0].Target == Target && Victim.Ledgers.IsEmpty());
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS
// ---- #198 H combat event correctness (review R03-R05, R07-R11, S20 teammates, C01 windows). Every case starts from the
// production release, delivery, zone, projectile, basic, burn or workshop entry; nothing hand-builds the event under test.
namespace LDBuildPrimitivesTests
{
// One release of Active by Caster as Combat builds it: its catalog terms compiled with the caster's mods, then the payload.
bool Released(FAutomationTestBase& Test, FFixture& F, const FLDCombatBodyRef& Caster, const TCHAR* Active, FLDCombatActiveTerms& Terms,
    FLDCombatHit& Payload)
{
    FString Error;
    if (!ActiveTerms(Test, F, Active, Terms)) { return false; }
    const FLDCombatSourceRef Source = SourceOf(ELDCombatSourceKind::Learned, Active);
    if (!FAccess::Compile(*F.Driver, F.Body(Caster).AttributeProjection, Source, Terms, Error)
        || !FAccess::BuildPayload(*F.Driver, F.Body(Caster), Source, Terms, F.Foot(Caster), Payload, Error))
    {
        Test.AddError(FString::Printf(TEXT("Release of %s: %s"), Active, *Error));
        return false;
    }
    return true;
}

// The standard unit delivery of a released payload onto Target.
bool DeliverOnUnit(FAutomationTestBase& Test, FFixture& F, const FLDCombatBodyRef& Caster, const FLDCombatActiveTerms& Terms,
    const FLDCombatHit& Payload, const FLDCombatBodyRef& Target)
{
    FLDCombatAction Action;
    Action.Kind = ELDCombatActionKind::Ability;
    Action.TargetBody = Target;
    FString Error;
    if (!FAccess::DeliverPayload(*F.Driver, F.Actor(Caster), F.Body(Caster), Terms, Action, Payload, Target, F.Foot(Target),
        F.Foot(Caster), F.Mode->GetAuthorityNowMs(), Error))
    {
        Test.AddError(TEXT("Unit delivery: ") + Error);
        return false;
    }
    return true;
}

// One landed primary basic of Holder on Target through LandBasic.
bool Basic(FAutomationTestBase& Test, FFixture& F, const FLDCombatBodyRef& Holder, const FLDCombatBodyRef& Target)
{
    FString Error;
    if (!FAccess::LandBasic(*F.Driver, F.Actor(Holder), F.Body(Holder), BasicAt(Target, 1), Error))
    {
        Test.AddError(TEXT("Basic: ") + Error);
        return false;
    }
    return true;
}

bool Drain(FAutomationTestBase& Test, FFixture& F)
{
    FString Error;
    if (!FAccess::ApplyPendingHits(*F.Driver, F.Mode->GetAuthorityNowMs(), Error)) { Test.AddError(TEXT("Effects: ") + Error); return false; }
    return true;
}

// The one queued hit taken out and applied alone, leaving what it queued.
bool ApplyFirst(FAutomationTestBase& Test, FFixture& F)
{
    TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    if (Pending.IsEmpty()) { Test.AddError(TEXT("No queued hit")); return false; }
    const FLDCombatHit First = Pending[0];
    Pending.Reset();
    FString Error;
    if (!F.Apply(First, Error)) { Test.AddError(TEXT("Hit: ") + Error); return false; }
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventEmpoweredCritLifestealTest, "Lockdown.Rules.Combat.Events.EmpoweredCritLifesteal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventEmpoweredCritLifestealTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    constexpr int64 HP = 10000000;
    const FLDCombatBodyRef Attacker = F.AddBody(ELDVictimKind::Contender, HP), Target = F.AddBody(ELDVictimKind::Contender, HP);
    if (!InstallComponents(*this, F, Attacker, 1, {{TEXT("component_punch_heal")}})
        || !InstallComponents(*this, F, Target, 1, {})) { return false; }
    FLDBodyCombatState& Body = F.Body(Attacker);
    FLDBodyCombatState& Victim = F.Body(Target);
    FLDCombatActiveTerms S11, S17;
    if (!ActiveTerms(*this, F, TEXT("active_bail_out"), S11) || !ActiveTerms(*this, F, TEXT("active_triple_combo"), S17))
    { return false; }
    const FLDDemoEffectDefinition* Effects[] = {FindEffect(S11.Values, ELDDemoEffectOp::EmpowerBasic),
        FindEffect(S17.Values, ELDDemoEffectOp::EmpowerBasic)};
    if (!TestTrue(TEXT("S11 and S17 provide their real empowered-basic terms"), Effects[0] && Effects[1])) { return false; }
    const TCHAR* Actives[] = {TEXT("active_bail_out"), TEXT("active_triple_combo")};
    int64 Damage[2][4] = {}, Healing[2][4] = {};
    for (int32 Crit = 0; Crit < 2; ++Crit)
    {
        for (int32 Mask = 0; Mask < 4; ++Mask)
        {
            Body.Resources.HpP2 = HP / 2;
            Victim.Resources.HpP2 = HP;
            Body.Empowers.Reset();
            Body.AttributeProjection.Final.CritChanceBp = Crit ? 10000 : 0;
            FString Error;
            for (int32 Skill = 0; Skill < 2; ++Skill)
            {
                if (!(Mask & (1 << Skill))) { continue; }
                FLDCombatEmpower Package;
                if (!TestTrue(FString::Printf(TEXT("The real %s package is prepared: %s"), Actives[Skill], *Error),
                    FAccess::MakeEmpower(*F.Driver, Body, SourceOf(ELDCombatSourceKind::Learned, Actives[Skill]),
                        *Effects[Skill], 1000, Package, Error))) { return false; }
                LDCombatControl::ArmEmpower(Body.Empowers, Package);
            }
            if (!Basic(*this, F, Attacker, Target)) { return false; }
            const TArray<FLDCombatHit>& Hits = FAccess::PendingHits(*F.Driver);
            if (!TestTrue(TEXT("One primary basic carries B04 and the forced crit outcome"), Hits.Num() == 1
                && Hits[0].HitClass == ELDCombatHitClass::BasicPrimary && Hits[0].bCritical == bool(Crit)
                && Hits[0].bLifesteal && Hits[0].LifestealRatioBp == 1200)) { return false; }
            const int64 ShareBp = Hits[0].LifestealShareBp;
            if (!TestTrue(TEXT("B04 heals the whole of a plain basic and only the own share of an empowered one"),
                Mask == 0 ? ShareBp == 10000 : ShareBp > 0 && ShareBp < 10000)) { return false; }
            if (!Drain(*this, F)) { return false; }
            Damage[Crit][Mask] = HP - Victim.Resources.HpP2;
            Healing[Crit][Mask] = Body.Resources.HpP2 - HP / 2;
            AddInfo(FString::Printf(TEXT("LD_EVAL_MEASURE empowered_basic crit=%d s11=%d s17=%d hp_loss_p2=%lld heal_p2=%lld"),
                Crit, (Mask & 1) != 0, (Mask & 2) != 0, Damage[Crit][Mask], Healing[Crit][Mask]));
            const int64 FullHeal = Damage[Crit][Mask] * 1200 / 10000;
            TestEqual(TEXT("B04 heals 12% of the actual HP lost, scaled to the basic's own package share"), Healing[Crit][Mask],
                FullHeal / 10000 * ShareBp + FullHeal % 10000 * ShareBp / 10000);
            TestTrue(TEXT("An S11/S17 empowered hit heals less than 12% of its whole HP loss"),
                Mask == 0 ? Healing[Crit][Mask] == FullHeal : Healing[Crit][Mask] < FullHeal);
        }
    }
    for (int32 Mask = 0; Mask < 4; ++Mask)
    {
        TestTrue(TEXT("The primary crit doubles its merged package, within one hundredth of rounding"),
            FMath::Abs(Damage[1][Mask] - 2 * Damage[0][Mask]) <= 1);
    }
    TestTrue(TEXT("Either S11 or S17 raises the hit; together they raise it further"),
        Damage[0][1] > Damage[0][0] && Damage[0][2] > Damage[0][0]
        && Damage[0][3] > Damage[0][1] && Damage[0][3] > Damage[0][2]);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventAttributeMarginalTest, "Lockdown.Rules.Combat.Events.AttributeMarginal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventAttributeMarginalTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    for (const uint8 Level : {uint8(3), uint8(6), uint8(10), uint8(16)})
    {
        FFixture F;
        if (!F.Init(*this)) { return false; }
        constexpr int64 HP = 10000000;
        const FLDCombatBodyRef Caster = F.AddBody(), Target = F.AddBody(ELDVictimKind::Contender, HP);
        const FLDAttributeProjectionResult TargetStats = LDAttributeProjection::Project(FLDAttributeBuildValues(Level, {}), *F.Catalog);
        if (!TestTrue(TEXT("The same-level unallocated target projects"), TargetStats.IsSuccess())) { return false; }
        F.Body(Target).BuildProjection = F.Body(Target).AttributeProjection = TargetStats.Value.GetValue();
        const FLDDemoAbilityDefinition* Ability = nullptr;
        FString Error;
        if (!F.Catalog->FindAbility(TEXT("active_snark_bolt"), Ability, Error) || !Ability)
        { AddError(TEXT("S06 lookup: ") + Error); return false; }
        const FLDDemoAbilityValues* Values = F.Catalog->FindValues(*Ability, Level);
        if (!TestNotNull(TEXT("S06 has level-grown values"), Values)) { return false; }
        int64 SkillLoss[2] = {}, BasicLoss[2] = {};
        for (int32 Variant = 0; Variant < 2; ++Variant)
        {
            const int32 Budget = 3 * Level;
            TArray<FLDStatPointsValue> Points = {FLDStatPointsValue(TEXT("stat_brains"), Budget - Variant)};
            if (Variant) { Points.Emplace(TEXT("stat_strength"), 1); }
            const FLDAttributeProjectionResult Projected = LDAttributeProjection::Project(
                FLDAttributeBuildValues(Level, MoveTemp(Points)), *F.Catalog);
            if (!TestTrue(TEXT("Equal-budget source attributes project"), Projected.IsSuccess())) { return false; }
            FLDBodyCombatState& Body = F.Body(Caster);
            Body.BuildProjection = Body.AttributeProjection = Projected.Value.GetValue();
            FAccess::Records(*F.Driver).FindChecked(Caster.Victim.EntityId).Build.Level = Level;
            const FLDProjectedAttributes Stats = Body.AttributeProjection.Final;
            FLDCombatActiveTerms Terms;
            Terms.Target = Ability->Target;
            Terms.Values = *Values;
            Terms.WindupMs = Ability->WindupMs;
            const FLDCombatSourceRef Source = SourceOf(ELDCombatSourceKind::Learned, TEXT("active_snark_bolt"));
            FLDCombatHit Payload;
            if (!FAccess::Compile(*F.Driver, Body.AttributeProjection, Source, Terms, Error)
                || !FAccess::BuildPayload(*F.Driver, Body, Source, Terms, F.Foot(Caster), Payload, Error))
            { AddError(TEXT("S06 release: ") + Error); return false; }
            Payload.Target = Target;
            F.Body(Target).Resources.HpP2 = HP;
            if (!F.Apply(Payload, Error)) { AddError(TEXT("S06 hit: ") + Error); return false; }
            SkillLoss[Variant] = HP - F.Body(Target).Resources.HpP2;
            F.Body(Target).Resources.HpP2 = HP;
            Body.AttributeProjection.Final.CritChanceBp = 0;
            if (!Basic(*this, F, Caster, Target) || !Drain(*this, F)) { return false; }
            BasicLoss[Variant] = HP - F.Body(Target).Resources.HpP2;
            AddInfo(FString::Printf(TEXT("LD_EVAL_MEASURE attribute_marginal level=%u strength_points=%d ap_p2=%lld ad_p2=%lld crit_bp=%lld haste_p2=%lld s06_hp_loss_p2=%lld noncrit_basic_hp_loss_p2=%lld"),
                uint32(Level), Variant, Stats.APP2, Stats.ADP2, Stats.CritChanceBp, Stats.CooldownHasteP2,
                SkillLoss[Variant], BasicLoss[Variant]));
        }
        TestTrue(TEXT("One brain point increases this S06 hit; one strength point increases a noncritical basic"),
            SkillLoss[0] > SkillLoss[1] && BasicLoss[1] > BasicLoss[0]);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventDefenseBudgetTest, "Lockdown.Rules.Combat.Events.DefenseBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventDefenseBudgetTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    for (const uint8 Level : {uint8(3), uint8(6), uint8(10), uint8(16)})
    {
        for (const TCHAR* Stat : {TEXT("stat_physique"), TEXT("stat_thick_skin")})
        {
            for (const ELDDemoDamageType Type : {ELDDemoDamageType::Physical, ELDDemoDamageType::Magical})
            {
                for (const bool bShield : {false, true})
                {
                    FFixture F;
                    if (!F.Init(*this)) { return false; }
                    const FLDCombatBodyRef Attacker = F.AddBody(), Target = F.AddBody();
                    const FLDAttributeProjectionResult Projected = LDAttributeProjection::Project(
                        FLDAttributeBuildValues(Level, {FLDStatPointsValue(Stat, 3 * Level)}), *F.Catalog);
                    if (!TestTrue(TEXT("An equal-budget defensive build projects"), Projected.IsSuccess())) { return false; }
                    FLDBodyCombatState& Body = F.Body(Target);
                    Body.BuildProjection = Body.AttributeProjection = Projected.Value.GetValue();
                    Body.Resources.MaxHpP2 = Body.Resources.HpP2 = Projected.Value->Final.MaxHPP2;
                    FString Error;
                    if (bShield && !FAccess::GrantShield(*F.Driver, Body, 10000, 1000, Error))
                    { AddError(TEXT("Shield: ") + Error); return false; }
                    int64 TotalLost = 0, TotalAbsorbed = 0, Healed = 0;
                    for (int32 HitIndex = 0; HitIndex < 3; ++HitIndex)
                    {
                        F.Now(1000 + HitIndex * 100);
                        if (HitIndex == 1)
                        {
                            const int64 Before = Body.Resources.HpP2;
                            if (!FAccess::Heal(*F.Driver, Body, 10000)) { AddError(TEXT("The planned heal failed")); return false; }
                            Healed = Body.Resources.HpP2 - Before;
                        }
                        FLDCombatHit Hit = FFixture::Hit(Attacker, Target, 30000);
                        Hit.DamageType = Type;
                        const int64 BeforeHP = Body.Resources.HpP2, BeforeShield = Body.Resources.ShieldP2;
                        if (!F.Apply(Hit, Error)) { AddError(TEXT("Defensive hit: ") + Error); return false; }
                        TotalLost += BeforeHP - Body.Resources.HpP2;
                        TotalAbsorbed += BeforeShield - Body.Resources.ShieldP2;
                    }
                    AddInfo(FString::Printf(TEXT("LD_EVAL_MEASURE defense_budget level=%u stat=%s type=%s shield=%d max_hp_p2=%lld resistance_p2=%lld hp_lost_p2=%lld shield_absorbed_p2=%lld healed_p2=%lld hp_end_p2=%lld"),
                        uint32(Level), Stat, Type == ELDDemoDamageType::Physical ? TEXT("physical") : TEXT("magical"), bShield,
                        Body.Resources.MaxHpP2, Type == ELDDemoDamageType::Physical ? Body.AttributeProjection.Final.ArmorP2
                            : Body.AttributeProjection.Final.MagicResistP2, TotalLost, TotalAbsorbed, Healed, Body.Resources.HpP2));
                    TestTrue(TEXT("The controlled sequence preserves health, uses the shield only when granted, and heals actual HP"),
                        Body.Resources.HpP2 > 0 && TotalLost > 0 && Healed > 0 && (bShield ? TotalAbsorbed == 10000 : TotalAbsorbed == 0));
                }
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventCounterHealStoreTest, "Lockdown.Rules.Combat.Events.CounterHealStore",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventCounterHealStoreTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    constexpr int64 HP = 10000000;
    const FLDCombatBodyRef Holder = F.AddBody(ELDVictimKind::Contender, HP), Foe = F.AddBody(ELDVictimKind::Contender, HP);
    F.Place(Holder, 0, 0);
    F.Place(Foe, 150, 0);
    if (!InstallComponents(*this, F, Holder, 1, {{TEXT("component_punch_heal")},
        On(TEXT("component_breath_strike"), ELDComponentTargetKind::Component, TEXT("component_punch_heal"))})
        || !InstallComponents(*this, F, Foe, 1, {})) { return false; }
    FString Error;
    if (!TestTrue(TEXT("The foe damages the holder and becomes a recent attacker"), F.Apply(FFixture::Hit(Foe, Holder, 100000), Error)))
    { return false; }
    const int64 HolderBefore = F.Body(Holder).Resources.HpP2;
    const int64 FoeBefore = F.Body(Foe).Resources.HpP2;
    if (!Basic(*this, F, Holder, Foe) || !Drain(*this, F)) { return false; }
    const int64 Lost = FoeBefore - F.Body(Foe).Resources.HpP2;
    const int64 Healed = F.Body(Holder).Resources.HpP2 - HolderBefore;
    const int64 Stored = F.Body(Holder).CounterStoreP2;
    AddInfo(FString::Printf(TEXT("LD_EVAL_MEASURE b04_c08 hp_loss_p2=%lld heal_p2=%lld counter_store_p2=%lld"),
        Lost, Healed, Stored));
    TestTrue(TEXT("B04 heals from real HP loss and C08 stores half the applied heal"),
        Lost > 0 && Healed == Lost * 1200 / 10000 && Stored == Healed / 2 && Stored > 0);
    F.Now(2000);
    if (!TestTrue(FString::Printf(TEXT("The C08 interval is processed: %s"), *Error),
        FAccess::AdvanceWorkshop(*F.Driver, 2000, Error))) { return false; }
    const TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    TestTrue(TEXT("One counter carries the stored amount, not the storage cap"), Pending.Num() == 1
        && Pending[0].Target == Foe && Pending[0].BaseP2 == Stored && !Pending[0].bCritical
        && F.Body(Holder).CounterStoreP2 == 0);
    return Drain(*this, F);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventCounterExtraBaseTest, "Lockdown.Rules.Combat.Events.CounterExtraBase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventCounterExtraBaseTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    constexpr int64 HP = 10000000;
    const FLDCombatBodyRef With = F.AddBody(ELDVictimKind::Contender, HP), WithFoe = F.AddBody(ELDVictimKind::Contender, HP),
        Without = F.AddBody(ELDVictimKind::Contender, HP), WithoutFoe = F.AddBody(ELDVictimKind::Contender, HP);
    F.Place(With, 0, 0); F.Place(WithFoe, 150, 0);
    F.Place(Without, 1000, 0); F.Place(WithoutFoe, 1150, 0);
    FInstall Repeat = On(TEXT("component_dirty_fist"), ELDComponentTargetKind::Component, TEXT("component_spiky_hide"));
    Repeat.BoundComponent = TEXT("component_tab_ledger");
    if (!InstallComponents(*this, F, With, 1, {{TEXT("component_spiky_hide")}, {TEXT("component_tab_ledger")}, Repeat})
        || !InstallComponents(*this, F, Without, 1, {{TEXT("component_spiky_hide")}, {TEXT("component_tab_ledger")}})
        || !InstallComponents(*this, F, WithFoe, 1, {}) || !InstallComponents(*this, F, WithoutFoe, 1, {}))
    { return false; }
    for (int32 Swing = 1; Swing <= 3; ++Swing)
    {
        F.Now(1000 + (Swing - 1) * 500);
        if (!Basic(*this, F, WithFoe, With) || !Drain(*this, F)
            || !Basic(*this, F, WithoutFoe, Without) || !Drain(*this, F)) { return false; }
        const auto Entries = [&F](const FLDCombatBodyRef& Target)
        {
            const auto& Ledgers = F.Body(Target).Ledgers;
            return Ledgers.IsEmpty() ? 0 : Ledgers[0].EntriesP2.Num();
        };
        const int32 ExpectedWith[] = {0, 2, 4, 1};
        TestEqual(FString::Printf(TEXT("C12 produces two B02 entries per B03 answer, swing %d"), Swing),
            Entries(WithFoe), ExpectedWith[Swing]);
        TestEqual(FString::Printf(TEXT("B03 alone produces one B02 entry per answer, swing %d"), Swing),
            Entries(WithoutFoe), Swing);
    }
    AddInfo(FString::Printf(TEXT("LD_EVAL_MEASURE b03_b02_c12 three_attacks_hp_loss_with_p2=%lld hp_loss_without_p2=%lld"),
        HP - F.Body(WithFoe).Resources.HpP2, HP - F.Body(WithoutFoe).Resources.HpP2));
    TestTrue(TEXT("The C12 group has cashed out by the third answer"), F.Body(WithFoe).Resources.HpP2 < F.Body(WithoutFoe).Resources.HpP2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventElementChainFrequencyTest, "Lockdown.Rules.Combat.Events.ElementChainFrequency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventElementChainFrequencyTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    constexpr int64 HP = 10000000;
    const FLDCombatBodyRef Holder = F.AddBody(), Target = F.AddBody(ELDVictimKind::Contender, HP);
    F.Place(Holder, 0, 0);
    F.Place(Target, 150, 0);
    if (!InstallComponents(*this, F, Holder, 1, {{TEXT("component_fist_water")}, {TEXT("component_fist_shock")},
        On(TEXT("component_blast_chase"), ELDComponentTargetKind::Component, TEXT("component_fist_shock"))})
        || !InstallComponents(*this, F, Target, 1, {})) { return false; }
    int64 Damage[3] = {};
    for (int32 Swing = 0; Swing < 3; ++Swing)
    {
        F.Now(1000 + Swing * 1000);
        FAccess::Reactions(*F.Driver).Reset();
        const int64 Before = F.Body(Target).Resources.HpP2;
        if (!Basic(*this, F, Holder, Target)) { return false; }
        const TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
        if (!TestTrue(TEXT("The first basic is plain; later basics consume C09's preceding charge"),
            Pending.Num() == 1 && (Swing == 0 ? Pending[0].BaseP2 == 0 : Pending[0].BaseP2 > 0)))
        { return false; }
        if (!Drain(*this, F)) { return false; }
        Damage[Swing] = Before - F.Body(Target).Resources.HpP2;
        const auto& Reactions = FAccess::Reactions(*F.Driver);
        AddInfo(FString::Printf(TEXT("LD_EVAL_MEASURE b10_b11_c09 swing=%d hp_loss_p2=%lld reaction_count=%d"),
            Swing + 1, Damage[Swing], Reactions.Num()));
        TestTrue(TEXT("The same basic applies water then electric, producing one B11 conduct and one new C09 charge"),
            Reactions.Num() == 1 && Reactions[0].Kind == ELDCombatReaction::Conduct
            && Reactions[0].TriggerSource == SourceOf(ELDCombatSourceKind::Component, TEXT("component_fist_shock"))
            && F.Body(Holder).Empowers.ContainsByPredicate([](const FLDCombatEmpower& Row)
                { return Row.Source.DefinitionId == FName(TEXT("component_blast_chase")); }));
    }
    TestTrue(TEXT("C09 raises the second and third committed hits in this controlled fixture"),
        Damage[1] > Damage[0] && Damage[2] == Damage[1]);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventProjectileCounterplayTest, "Lockdown.Rules.Combat.Events.ProjectileCounterplay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventProjectileCounterplayTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    for (const TCHAR* Active : {TEXT("active_snark_bolt"), TEXT("active_sock_shot")})
    {
        for (int32 Case = 0; Case < 3; ++Case)
        {
            FFixture F;
            if (!F.Init(*this, true)) { return false; }
            const FLDCombatBodyRef Caster = F.AddBody(), Target = F.AddBody(), Blocker = F.AddBody();
            F.Place(Caster, 0, 0);
            F.Place(Target, 300, 0);
            F.Place(Blocker, Case == 2 ? 150 : 600, Case == 2 ? 0 : 600);
            if (!InstallComponents(*this, F, Caster, 1, {})) { return false; }
            FLDCombatActiveTerms Terms;
            FLDCombatHit Payload;
            if (!Released(*this, F, Caster, Active, Terms, Payload)) { return false; }
            FLDCombatAction Action;
            Action.Direction = FVector(1, 0, 0);
            FString Error;
            if (!TestTrue(FString::Printf(TEXT("%s projectile is released: %s"), Active, *Error),
                FAccess::DeliverPayload(*F.Driver, F.Actor(Caster), F.Body(Caster), Terms, Action, Payload,
                    FLDCombatBodyRef(), FVector::ZeroVector, F.Foot(Caster), 1000, Error))) { return false; }
            if (Case == 1) { F.Place(Target, 300, 200); }
            F.Now(1400);
            if (!TestTrue(FString::Printf(TEXT("%s projectile advances: %s"), Active, *Error),
                FAccess::AdvanceProjectiles(*F.Driver, Error))) { return false; }
            const TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
            if (Case == 1)
            {
                TestTrue(TEXT("A lateral move outside the projectile and body radii avoids contact"), Pending.IsEmpty());
            }
            else
            {
                const FLDCombatBodyRef Expected = Case == 2 ? Blocker : Target;
                if (!TestTrue(TEXT("The first body on the line receives the only projectile hit"),
                    Pending.Num() == 1 && Pending[0].Target == Expected)) { return false; }
                if (!Drain(*this, F)) { return false; }
                if (Case == 0 && FString(Active) == TEXT("active_snark_bolt"))
                { TestTrue(TEXT("S06 applies its movement slow"), !F.Body(Target).Slows.IsEmpty()); }
                if (Case == 0 && FString(Active) == TEXT("active_sock_shot"))
                { TestTrue(TEXT("S21 applies attack slow and grievous"), !F.Body(Target).AttackSlows.IsEmpty()
                    && F.Body(Target).Grievous.RatioBp == 3500); }
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventRangeModTradeoffTest, "Lockdown.Rules.Combat.Events.RangeModTradeoff",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventRangeModTradeoffTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    constexpr int64 HP = 10000000;
    const auto Fire = [this](bool bMod, double Distance, int64& OutDamage)
    {
        FFixture F;
        if (!F.Init(*this, true)) { return false; }
        const FLDCombatBodyRef Caster = F.AddBody(), Target = F.AddBody(ELDVictimKind::Contender, HP);
        F.Place(Caster, -425, 0);
        F.Place(Target, -425 + Distance, 0);
        const TArray<FInstall> Mods = bMod
            ? TArray<FInstall>{On(TEXT("component_reach_out"), ELDComponentTargetKind::Active, TEXT("active_snark_bolt"))}
            : TArray<FInstall>{};
        if (!InstallComponents(*this, F, Caster, 1, Mods) || !InstallComponents(*this, F, Target, 1, {})) { return false; }
        FLDCombatActiveTerms Terms;
        FLDCombatHit Payload;
        if (!Released(*this, F, Caster, TEXT("active_snark_bolt"), Terms, Payload)) { return false; }
        if (!TestTrue(TEXT("S06 gains 200 cm up to the 1000 cm cap and pays the native 20% package cost"),
            SameRational(Terms.Target.RangeCm, bMod ? 1000 : 850) && Terms.PackageScaleBp == (bMod ? 8000 : 10000)
            && Payload.PackageScaleBp == Terms.PackageScaleBp)) { return false; }
        FLDCombatAction Action;
        Action.Direction = FVector(1, 0, 0);
        FString Error;
        if (!TestTrue(FString::Printf(TEXT("S06 is released: %s"), *Error), FAccess::DeliverPayload(*F.Driver, F.Actor(Caster),
                F.Body(Caster), Terms, Action, Payload, FLDCombatBodyRef(), FVector::ZeroVector, F.Foot(Caster), 1000, Error)))
        { return false; }
        F.Now(1700);
        if (!TestTrue(FString::Printf(TEXT("S06 finishes its flight: %s"), *Error),
            FAccess::AdvanceProjectiles(*F.Driver, Error)) || !Drain(*this, F)) { return false; }
        OutDamage = HP - F.Body(Target).Resources.HpP2;
        return true;
    };
    int64 Near = 0, NearMod = 0, Far = 0, FarMod = 0;
    if (!Fire(false, 300, Near) || !Fire(true, 300, NearMod) || !Fire(false, 950, Far) || !Fire(true, 950, FarMod))
    { return false; }
    AddInfo(FString::Printf(TEXT("LD_EVAL_MEASURE m05 near_without_p2=%lld near_with_p2=%lld far_without_p2=%lld far_with_p2=%lld"),
        Near, NearMod, Far, FarMod));
    TestTrue(TEXT("At equal near distance M05 pays its 20% damage cost, within rounding"),
        Near > 0 && FMath::Abs(NearMod * 5 - Near * 4) <= 5);
    TestTrue(TEXT("At 950 cm only the extended projectile reaches the target"), Far == 0 && FarMod > 0);
    return true;
}

// R03: a real S02 root and S03 stun carry their active as the control's exit, so C05 marks and consumes and C07 grants.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventControlSourceTest, "Lockdown.Rules.Combat.Events.ControlSource",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventControlSourceTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    using EKind = ELDComponentTargetKind;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(), Victim = F.AddBody(ELDVictimKind::Contender, 10000000);
    if (!InstallComponents(*this, F, Holder, 1, {On(TEXT("component_pin_punch"), EKind::Active, TEXT("active_pants_down")),
        On(TEXT("component_control_mana"), EKind::Active, TEXT("active_pants_down"))}))
    { return false; }
    FLDBodyCombatState& Body = F.Body(Holder);
    Body.Resources.MaxMpP2 = 100000;
    Body.Resources.MpP2 = 0;
    FLDCombatActiveTerms Root, Stun;
    FLDCombatHit RootHit, StunHit;
    if (!Released(*this, F, Holder, TEXT("active_pants_down"), Root, RootHit)
        || !DeliverOnUnit(*this, F, Holder, Root, RootHit, Victim) || !Drain(*this, F)) { return false; }
    TestTrue(TEXT("S02's root lands"), F.Body(Victim).Roots.Num() == 1);
    TestTrue(TEXT("C05 reads S02 as the root's exit and marks the rooted life; C07 grants 4 MP"), Body.ControlMarks.Num() == 1
        && Body.ControlMarks[0].Kind == ELDControlKind::Root && Body.Resources.MpP2 == 400);
    F.Now(1500);
    if (!Released(*this, F, Holder, TEXT("active_crotch_grab"), Stun, StunHit)
        || !DeliverOnUnit(*this, F, Holder, Stun, StunHit, Victim) || !ApplyFirst(*this, F)) { return false; }
    const TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    TestTrue(TEXT("S03's stun (another kind) consumes the mark for C05's one physical 70G package"), F.Body(Victim).Stuns.Num() == 1
        && Body.ControlMarks.IsEmpty() && Pending.Num() == 1 && Pending[0].Source.DefinitionId == FName(TEXT("component_pin_punch"))
        && Pending[0].BaseP2 == 7000 && Pending[0].StunDurationMs == 0);
    TestEqual(TEXT("C07 on S02 does not read S03"), Body.Resources.MpP2, int64(400));
    return Drain(*this, F);
}

// R04: B10/B11 water and electric are their component's own applications: C09 on B11 arms on its conduct and M11 on B11 picks
// its discharge target.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventElementSourceTest, "Lockdown.Rules.Combat.Events.ComponentElementSource",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventElementSourceTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    using EKind = ELDComponentTargetKind;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    constexpr int64 Big = 10000000;
    const FLDCombatBodyRef Holder = F.AddBody(), Target = F.AddBody(ELDVictimKind::Contender, Big),
        Near = F.AddBody(ELDVictimKind::Contender, Big), Marked = F.AddBody(ELDVictimKind::Contender, Big),
        Soaked = F.AddBody(ELDVictimKind::Contender, Big), Rain = F.AddBody(), Splasher = F.AddBody(),
        Splashed = F.AddBody(ELDVictimKind::Contender, Big);
    F.Place(Holder, 0, 0);
    F.Place(Target, 150, 0);
    F.Place(Near, 150, 200);
    F.Place(Marked, 150, -320);
    F.Place(Soaked, -300, 0);
    F.Place(Rain, -600, 600);
    F.Place(Splasher, 600, 600);
    F.Place(Splashed, 600, 450);
    const FLDCombatSourceRef Shock = SourceOf(ELDCombatSourceKind::Component, TEXT("component_fist_shock"));
    if (!InstallComponents(*this, F, Holder, 1, {{TEXT("component_fist_shock")},
        On(TEXT("component_blast_chase"), EKind::Component, TEXT("component_fist_shock")),
        On(TEXT("component_pick_you"), EKind::Component, TEXT("component_fist_shock"))})
        || !InstallComponents(*this, F, Splasher, 1, {{TEXT("component_fist_water")}}))
    { return false; }
    FAccess::Records(*F.Driver).FindChecked(Holder.Victim.EntityId).Build.Actives = {{TEXT("active_pants_down"), ELDCombatEntry::Q}};
    FString Error;
    // C09 on B11: a wet body conducts on the holder's B11 basic; the reaction's trigger is B11, which arms C09.
    FLDCombatHit Wet = FFixture::Hit(Rain, Soaked, 0);
    Wet.bApplyWet = true;
    if (!TestTrue(TEXT("The body is wet"), F.Apply(Wet, Error))) { return false; }
    FAccess::Reactions(*F.Driver).Reset();
    if (!Basic(*this, F, Holder, Soaked) || !Drain(*this, F)) { return false; }
    const TArray<FLDCombatReactionFact>& Reactions = FAccess::Reactions(*F.Driver);
    TestTrue(TEXT("B11's electric conducts as B11"), Reactions.Num() == 1 && Reactions[0].Kind == ELDCombatReaction::Conduct
        && Reactions[0].TriggerSource == Shock);
    TestTrue(TEXT("C09 on B11 arms one empowered basic"), F.Body(Holder).Empowers.ContainsByPredicate([](const FLDCombatEmpower& Row)
    { return Row.Source.DefinitionId == FName(TEXT("component_blast_chase")); }));
    // M11 on B11: the holder's S02 hits Marked directly; B11 electrifies Target; Target's next damage discharges to Marked
    // instead of the nearer Near.
    FLDCombatActiveTerms Root;
    FLDCombatHit RootHit;
    if (!Released(*this, F, Holder, TEXT("active_pants_down"), Root, RootHit) || !DeliverOnUnit(*this, F, Holder, Root, RootHit, Marked)
        || !Drain(*this, F) || !Basic(*this, F, Holder, Target) || !Drain(*this, F)) { return false; }
    const FLDBodyCombatState& Charged = F.Body(Target);
    TestTrue(TEXT("B11 electrifies the target as B11"), Charged.ElementMark.Kind == ELDCombatElement::Electric
        && Charged.ElementMark.Applier.Source == Shock);
    F.Now(1200);
    if (!Basic(*this, F, Holder, Target) || !ApplyFirst(*this, F)) { return false; }
    const TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    const FLDCombatHit* Discharge = Pending.FindByPredicate([](const FLDCombatHit& Row) { return Row.bStatusSettle; });
    TestTrue(TEXT("M11 on B11 sends the discharge to the holder's most recent direct target at 85%"), Discharge
        && Discharge->Target == Marked && Discharge->PackageScaleBp == 8500 && Discharge->Source == Shock);
    if (!Drain(*this, F)) { return false; }
    // B10: its wet is credited to B10, not to the basic attack.
    if (!Basic(*this, F, Splasher, Splashed) || !Drain(*this, F)) { return false; }
    TestTrue(TEXT("B10 wets as B10"), F.Body(Splashed).ElementMark.Kind == ELDCombatElement::Water
        && F.Body(Splashed).ElementMark.Applier.Source == SourceOf(ELDCombatSourceKind::Component, TEXT("component_fist_water")));
    return true;
}

// R05 (spec §11.1/§18.1): an S08 pulse carries its native fire and B18's electric in one batch: fire fogs the wet target, the
// fog's 0 s wet lands at once, then the electric conducts. Fire before electric, never the other way round.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventZoneBatchTest, "Lockdown.Rules.Combat.Events.ZoneElementBatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventZoneBatchTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef Mage = F.AddBody(), Target = F.AddBody(ELDVictimKind::Contender, 10000000), Rain = F.AddBody();
    F.Place(Mage, 0, 0);
    F.Place(Target, 400, 0);
    F.Place(Rain, -600, 0);
    FInstall Trick{TEXT("component_dirty_trick"), TEXT("active_quick_meeting"), ELDDemoElement::Electric};
    if (!InstallComponents(*this, F, Mage, 1, {Trick})) { return false; }
    FAccess::Records(*F.Driver).FindChecked(Mage.Victim.EntityId).Build.Actives = {{TEXT("active_quick_meeting"), ELDCombatEntry::Q}};
    FString Error;
    FLDCombatHit Wet = FFixture::Hit(Rain, Target, 0);
    Wet.bApplyWet = true;
    if (!TestTrue(TEXT("The target is wet"), F.Apply(Wet, Error))) { return false; }
    FLDCombatActiveTerms Zone;
    if (!ActiveTerms(*this, F, TEXT("active_quick_meeting"), Zone)) { return false; }
    const FLDCombatSourceRef S08 = SourceOf(ELDCombatSourceKind::Learned, TEXT("active_quick_meeting"));
    FAccess::Reactions(*F.Driver).Reset();
    if (!TestTrue(FString::Printf(TEXT("S08 is released on the target: %s"), *Error), FAccess::Compile(*F.Driver,
        F.Body(Mage).AttributeProjection, S08, Zone, Error) && FAccess::ReleaseTerms(*F.Driver, F.Body(Mage), S08, Zone, F.Foot(Mage),
        F.Foot(Target), Error))
        || !TestTrue(FString::Printf(TEXT("Its first pulse: %s"), *Error), FAccess::AdvanceMechanics(*F.Driver, 1000, Error))
        || !Drain(*this, F))
    { return false; }
    const TArray<FLDCombatReactionFact>& Reactions = FAccess::Reactions(*F.Driver);
    TestTrue(TEXT("Fog from S08's fire first, then conduct from B18's electric on the fog's immediate wet"), Reactions.Num() == 2
        && Reactions[0].Kind == ELDCombatReaction::Fog && Reactions[0].TriggerSource == S08
        && Reactions[1].Kind == ELDCombatReaction::Conduct
        && Reactions[1].TriggerSource == SourceOf(ELDCombatSourceKind::Component, TEXT("component_dirty_trick")));
    const FLDBodyCombatState& Struck = F.Body(Target);
    TestTrue(TEXT("Nothing is left on the target: the fire settled in the fog, the fog's wet in the conduct"),
        Struck.ElementMark.Kind == ELDCombatElement::None && Struck.Burn.Layers.IsEmpty());
    TestTrue(TEXT("The fog's next pulse is a second later"), FAccess::Fogs(*F.Driver).Num() == 1
        && FAccess::Fogs(*F.Driver)[0].NextPulseMs == 2000);
    return true;
}

// R07: B02 counts the basic, not the reaction or fire settle the same basic set off.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventLedgerSettleTest, "Lockdown.Rules.Combat.Events.LedgerSkipsSettles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventLedgerSettleTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(), Target = F.AddBody(ELDVictimKind::Contender, 10000000);
    F.Place(Holder, 0, 0);
    F.Place(Target, 150, 0);
    if (!InstallComponents(*this, F, Holder, 1, {{TEXT("component_fist_fire")}, {TEXT("component_fist_water")},
        {TEXT("component_fist_shock")}, {TEXT("component_tab_ledger")}})) { return false; }
    FAccess::Reactions(*F.Driver).Reset();
    if (!Basic(*this, F, Holder, Target) || !Drain(*this, F)) { return false; }
    const TArray<FLDCombatReactionFact>& Reactions = FAccess::Reactions(*F.Driver);
    TestTrue(TEXT("The basic applies fire, water and electric in order: one fog then one conduct"), Reactions.Num() == 2
        && Reactions[0].Kind == ELDCombatReaction::Fog
        && Reactions[0].TriggerSource == SourceOf(ELDCombatSourceKind::Component, TEXT("component_fist_water"))
        && Reactions[1].Kind == ELDCombatReaction::Conduct
        && Reactions[1].TriggerSource == SourceOf(ELDCombatSourceKind::Component, TEXT("component_fist_shock")));
    const FLDBodyCombatState& Victim = F.Body(Target);
    TestTrue(TEXT("One B02 entry: the basic; the reaction and the fire settle add none"), Victim.Ledgers.Num() == 1
        && Victim.Ledgers[0].EntriesP2.Num() == 1);
    TestTrue(TEXT("The conduct consumes the fog's immediate wet"), Victim.ElementMark.Kind == ELDCombatElement::None);
    return true;
}

// R08 (spec §9 S19): armed for 6 s from placement; a trigger starts a 0.45 s fuse at the trap's point; untriggered it ends
// without a blast; a trigger just before the end still blows.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventTrapFuseTest, "Lockdown.Rules.Combat.Events.TrapFuse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventTrapFuseTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef Caster = F.AddBody(), Enemy = F.AddBody(ELDVictimKind::Contender, 10000000);
    F.Place(Caster, 0, 0);
    F.Place(Enemy, 500, 0);
    TArray<FLDCombatGroundEffect>& Ground = FAccess::Ground(*F.Driver);
    const TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    FString Error;
    const auto Place = [&](int64 AtMs)
    {
        F.Now(AtMs);
        FLDCombatActiveTerms Trap;
        FLDCombatHit Payload;
        if (!Released(*this, F, Caster, TEXT("active_flyer_trap"), Trap, Payload)) { return false; }
        FLDCombatAction Action;
        Action.TargetPoint = F.Origin + FVector(300, 0, 0);
        bool bHandled = false;
        return TestTrue(FString::Printf(TEXT("The trap is placed: %s"), *Error), FAccess::DeliverTerms(*F.Driver, F.Body(Caster), Trap,
            Action, Payload, AtMs, bHandled, Error) && bHandled && Ground.Num() == 1);
    };
    const auto Advance = [&](int64 AtMs)
    {
        F.Now(AtMs);
        return TestTrue(FString::Printf(TEXT("Skill content at %lld: %s"), AtMs, *Error), FAccess::AdvanceSkillContent(*F.Driver, AtMs, Error));
    };
    if (!Place(1000)) { return false; }
    TestTrue(TEXT("Armed from placement for 6 s with a 0.45 s fuse"), Ground[0].ReadyAtMs == 1000 && Ground[0].ExpiresAtMs == 7000
        && Ground[0].FuseMs == 450 && !Ground[0].bTriggered);
    if (!Advance(3000)) { return false; }
    TestTrue(TEXT("Nobody within 1 m: it waits"), Ground.Num() == 1 && !Ground[0].bTriggered && Pending.IsEmpty());
    F.Place(Enemy, 350, 0);
    if (!Advance(3000)) { return false; }
    TestTrue(TEXT("A step inside triggers the fuse; nothing blows yet"), Ground.Num() == 1 && Ground[0].bTriggered
        && Ground[0].ReadyAtMs == 3450 && Pending.IsEmpty());
    if (!Advance(3449)) { return false; }
    TestTrue(TEXT("Still fusing"), Ground.Num() == 1 && Pending.IsEmpty());
    if (!Advance(3450)) { return false; }
    TestTrue(TEXT("It blows at its own point 0.45 s after the trigger"), Ground.IsEmpty() && Pending.Num() == 1 && Pending[0].Target == Enemy);
    FAccess::PendingHits(*F.Driver).Reset();
    F.Place(Enemy, 650, 0);
    if (!Place(4000) || !Advance(9999)) { return false; }
    TestTrue(TEXT("An untriggered trap lasts until its 6 s end"), Ground.Num() == 1 && Pending.IsEmpty());
    if (!Advance(10000)) { return false; }
    TestTrue(TEXT("and ends without a blast"), Ground.IsEmpty() && Pending.IsEmpty());
    if (!Place(10000) || !Advance(15900)) { return false; }
    F.Place(Enemy, 330, 0);
    if (!Advance(15900) || !Advance(16000)) { return false; }
    TestTrue(TEXT("A trigger before its end keeps fusing past the end"), Ground.Num() == 1 && Ground[0].bTriggered && Pending.IsEmpty());
    if (!Advance(16350)) { return false; }
    TestTrue(TEXT("and blows"), Ground.IsEmpty() && Pending.Num() == 1);
    return true;
}

// R09 (spec §9.1/§12.1): a due M12 debt is paid at its original amount through a live S16 stance and feeds no record.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventDebtBraceTest, "Lockdown.Rules.Combat.Events.DebtBypassesBrace",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventDebtBraceTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Attacker = F.AddBody(), Holder = F.AddBody(ELDVictimKind::Contender, 10000000);
    if (!InstallComponents(*this, F, Holder, 1, {On(TEXT("component_owe_it"), ELDComponentTargetKind::None)})) { return false; }
    FLDBodyCombatState& Body = F.Body(Holder);
    FString Error;
    if (!TestTrue(TEXT("A hostile hit"), F.Apply(FFixture::Hit(Attacker, Holder, 40000), Error)) || !TestEqual(TEXT("One debt"),
        Body.Debts.Num(), 1)) { return false; }
    const int64 Debt = Body.Debts[0].AmountP2;
    FLDCombatActiveTerms Brace;
    if (!ActiveTerms(*this, F, TEXT("active_brace"), Brace)) { return false; }
    const FLDDemoEffectDefinition* Stance = FindEffect(Brace.Values, ELDDemoEffectOp::Brace);
    F.Now(2000);
    if (!TestNotNull(TEXT("S16's stance"), Stance) || !TestTrue(TEXT("S16 starts"), FAccess::ReleaseBrace(*F.Driver, Body, *Stance, 2000, Error)))
    { return false; }
    F.Now(4000);
    const int64 Before = Body.Resources.HpP2;
    if (!TestTrue(FString::Printf(TEXT("The debt falls due: %s"), *Error), FAccess::AdvanceWorkshop(*F.Driver, 4000, Error))
        || !Drain(*this, F)) { return false; }
    TestTrue(TEXT("Paid in full through the stance, which records nothing"), Debt > 0 && Body.Resources.HpP2 == Before - Debt
        && Body.Brace.EffectId.IsValid() && Body.Brace.RecordedP2 == 0);
    return true;
}

// R10: C10's one answer to a broken shield shares one effect identity over every body it reaches, so C11's 30G on it is one
// budget.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventShieldBurstTest, "Lockdown.Rules.Combat.Events.ShieldBurstBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventShieldBurstTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    using EKind = ELDComponentTargetKind;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    constexpr int64 Big = 10000000;
    const FLDCombatBodyRef Holder = F.AddBody(ELDVictimKind::Contender, Big), First = F.AddBody(ELDVictimKind::Contender, Big),
        Second = F.AddBody(ELDVictimKind::Contender, Big);
    F.Place(Holder, 0, 0);
    F.Place(First, 150, 0);
    F.Place(Second, -150, 0);
    if (!InstallComponents(*this, F, Holder, 1, {{TEXT("component_hard_skin")},
        On(TEXT("component_shell_burst"), EKind::Component, TEXT("component_hard_skin")),
        On(TEXT("component_hurt_heal"), EKind::Component, TEXT("component_shell_burst"))}))
    { return false; }
    FLDBodyCombatState& Body = F.Body(Holder);
    FString Error;
    if (!TestTrue(TEXT("A hostile hit grants B09's shield"), F.Apply(FFixture::Hit(First, Holder, 1000), Error)
        && Body.Resources.ShieldP2 > 0 && Body.ShieldGrantSource == SourceOf(ELDCombatSourceKind::Component, TEXT("component_hard_skin"))))
    { return false; }
    F.Now(1100);
    FAccess::PendingHits(*F.Driver).Reset();
    if (!TestTrue(FString::Printf(TEXT("A big hit breaks it: %s"), *Error), F.Apply(FFixture::Hit(First, Holder, 2000000), Error)
        && Body.Resources.ShieldP2 == 0)) { return false; }
    const TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    TestTrue(TEXT("C10 answers both bodies around the holder as one effect"), Pending.Num() == 2 && Pending[0].EffectId == Pending[1].EffectId
        && Pending[0].Source.DefinitionId == FName(TEXT("component_shell_burst")) && Pending[0].Target != Pending[1].Target);
    const int64 Before = Body.Resources.HpP2;
    if (!Drain(*this, F)) { return false; }
    TestEqual(TEXT("C11 heals 30G in all for that one answer, not 30G per body"), Body.Resources.HpP2 - Before, int64(3000));
    return true;
}

// Spec §13.3: a C08 counter fed by a C11 heal continues that root action, so C11 cannot reopen a new 30G budget for it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventCounterRootTest, "Lockdown.Rules.Combat.Events.CounterKeepsRootBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventCounterRootTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    using EKind = ELDComponentTargetKind;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    constexpr int64 Big = 10000000;
    const FLDCombatBodyRef Holder = F.AddBody(ELDVictimKind::Contender, Big), Foe = F.AddBody(ELDVictimKind::Contender, Big);
    F.Place(Holder, 0, 0);
    F.Place(Foe, 200, 0);
    if (!InstallComponents(*this, F, Holder, 1, {On(TEXT("component_hurt_heal"), EKind::Active, TEXT("active_snark_bolt")),
        On(TEXT("component_breath_strike"), EKind::Component, TEXT("component_hurt_heal"))}))
    { return false; }
    FLDBodyCombatState& Body = F.Body(Holder);
    FString Error;
    F.Now(1000);
    if (!TestTrue(TEXT("The foe hurts the holder first"), F.Apply(FFixture::Hit(Foe, Holder, 1000000), Error))) { return false; }
    FLDCombatHit Bolt = FFixture::Hit(Holder, Foe, 1000000);
    Bolt.Source = SourceOf(ELDCombatSourceKind::Learned, TEXT("active_snark_bolt"));
    if (!TestTrue(FString::Printf(TEXT("The holder's S06 lands: %s"), *Error), F.Apply(Bolt, Error))) { return false; }
    if (!TestTrue(TEXT("C11's heal fills C08's store under the bolt's effect"), Body.CounterStoreP2 > 0
        && Body.CounterRootEffectId == Bolt.EffectId)) { return false; }
    FAccess::PendingHits(*F.Driver).Reset();
    F.Now(2000);
    if (!TestTrue(FString::Printf(TEXT("The store fires: %s"), *Error), FAccess::AdvanceWorkshop(*F.Driver, 2000, Error))) { return false; }
    const TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    TestTrue(TEXT("C08 strikes the foe as the bolt's own effect"), Pending.Num() == 1 && Pending[0].Target == Foe
        && Pending[0].EffectId == Bolt.EffectId && Pending[0].Source.DefinitionId == FName(TEXT("component_breath_strike")));
    TestTrue(TEXT("The fired store and its root are spent"), Body.CounterStoreP2 == 0 && !Body.CounterRootEffectId.IsValid());
    return true;
}

namespace LDBuildPrimitivesTests
{
const FLDCombatEffectBudget* BudgetOf(const FLDBodyCombatState& Body, const FGuid& EffectId, const TCHAR* Component)
{
    return Body.EffectBudgets.FindByPredicate([&](const FLDCombatEffectBudget& Row)
    { return Row.EffectId == EffectId && Row.Component == FName(Component); });
}
}

// Two C11 roots that feed one C08 store before it fires merge into one counter, spent once under the latest root;
// no second counter and no fresh root identity appear.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventCounterMergedRootsTest, "Lockdown.Rules.Combat.Events.CounterMergedRootsFireOnce",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventCounterMergedRootsTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    using EKind = ELDComponentTargetKind;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    constexpr int64 Big = 10000000;
    const FLDCombatBodyRef Holder = F.AddBody(ELDVictimKind::Contender, Big), Foe = F.AddBody(ELDVictimKind::Contender, Big);
    F.Place(Holder, 0, 0);
    F.Place(Foe, 200, 0);
    if (!InstallComponents(*this, F, Holder, 1, {On(TEXT("component_hurt_heal"), EKind::Active, TEXT("active_snark_bolt")),
        On(TEXT("component_breath_strike"), EKind::Component, TEXT("component_hurt_heal"))}))
    { return false; }
    FLDBodyCombatState& Body = F.Body(Holder);
    FString Error;
    F.Now(1000);
    if (!TestTrue(TEXT("The foe hurts the holder first"), F.Apply(FFixture::Hit(Foe, Holder, 1000000), Error))) { return false; }
    FLDCombatHit First = FFixture::Hit(Holder, Foe, 1000000);
    First.Source = SourceOf(ELDCombatSourceKind::Learned, TEXT("active_snark_bolt"));
    if (!TestTrue(FString::Printf(TEXT("The first S06 lands: %s"), *Error), F.Apply(First, Error))) { return false; }
    const int64 AfterFirst = Body.CounterStoreP2;
    F.Now(1100);
    FLDCombatHit Second = FFixture::Hit(Holder, Foe, 1000000);
    Second.Source = First.Source;
    Second.EffectId = FGuid::NewGuid();
    if (!TestTrue(FString::Printf(TEXT("A second, separate S06 lands before the store fires: %s"), *Error), F.Apply(Second, Error)))
    { return false; }
    const FLDCombatEffectBudget* BudgetA = BudgetOf(Body, First.EffectId, TEXT("component_hurt_heal"));
    const FLDCombatEffectBudget* BudgetB = BudgetOf(Body, Second.EffectId, TEXT("component_hurt_heal"));
    TestTrue(TEXT("Each root keeps its own full C11 30G budget"), BudgetA && BudgetB && BudgetA->SpentP2 == 3000 && BudgetB->SpentP2 == 3000);
    TestTrue(TEXT("Half of each real heal joins one store under the latest root"), AfterFirst == 1500 && Body.CounterStoreP2 == 3000
        && Body.CounterRootEffectId == Second.EffectId);
    FAccess::PendingHits(*F.Driver).Reset();
    F.Now(2000);
    if (!TestTrue(FString::Printf(TEXT("The store fires: %s"), *Error), FAccess::AdvanceWorkshop(*F.Driver, 2000, Error))) { return false; }
    const TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    TestTrue(TEXT("One counter carries the merged store as the latest root's effect"), Pending.Num() == 1 && Pending[0].Target == Foe
        && Pending[0].EffectId == Second.EffectId && Pending[0].BaseP2 == 3000);
    TestTrue(TEXT("The merged store is spent once"), Body.CounterStoreP2 == 0 && !Body.CounterRootEffectId.IsValid());
    return true;
}

// C11's 30G per root is consumed by the root and its derived same-effect hits; the C08 counter it feeds spends no
// C11 budget and refills nothing, so the loop cannot reopen healing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventSkillLifestealBudgetTest, "Lockdown.Rules.Combat.Events.SkillLifestealBudgetConsumed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventSkillLifestealBudgetTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    using EKind = ELDComponentTargetKind;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    constexpr int64 Big = 10000000;
    const FLDCombatBodyRef Holder = F.AddBody(ELDVictimKind::Contender, Big), Foe = F.AddBody(ELDVictimKind::Contender, Big);
    F.Place(Holder, 0, 0);
    F.Place(Foe, 200, 0);
    if (!InstallComponents(*this, F, Holder, 1, {On(TEXT("component_hurt_heal"), EKind::Active, TEXT("active_snark_bolt")),
        On(TEXT("component_breath_strike"), EKind::Component, TEXT("component_hurt_heal"))}))
    { return false; }
    FLDBodyCombatState& Body = F.Body(Holder);
    FString Error;
    F.Now(1000);
    if (!TestTrue(TEXT("The foe hurts the holder first"), F.Apply(FFixture::Hit(Foe, Holder, 2000000), Error))) { return false; }
    FLDCombatHit Bolt = FFixture::Hit(Holder, Foe, 1000000);
    Bolt.Source = SourceOf(ELDCombatSourceKind::Learned, TEXT("active_snark_bolt"));
    const int64 BeforeBolt = Body.Resources.HpP2;
    if (!TestTrue(FString::Printf(TEXT("The S06 lands: %s"), *Error), F.Apply(Bolt, Error))) { return false; }
    const FLDCombatEffectBudget* Budget = BudgetOf(Body, Bolt.EffectId, TEXT("component_hurt_heal"));
    TestTrue(TEXT("8% of 10000 real HP is capped at 30G for the root"), Budget && Budget->SpentP2 == 3000
        && Body.Resources.HpP2 - BeforeBolt == 3000);
    const int64 Stored = Body.CounterStoreP2;
    F.Now(1050);
    FLDCombatHit Derived = FFixture::Hit(Holder, Foe, 1000000);
    Derived.Source = Bolt.Source;
    Derived.EffectId = Bolt.EffectId;
    const int64 BeforeDerived = Body.Resources.HpP2;
    if (!TestTrue(FString::Printf(TEXT("A derived hit of the same action lands: %s"), *Error), F.Apply(Derived, Error))) { return false; }
    TestTrue(TEXT("The spent root budget heals nothing more and feeds no store"), Body.Resources.HpP2 == BeforeDerived
        && Budget->SpentP2 == 3000 && Body.CounterStoreP2 == Stored);
    FAccess::PendingHits(*F.Driver).Reset();
    F.Now(2000);
    if (!TestTrue(FString::Printf(TEXT("The store fires: %s"), *Error), FAccess::AdvanceWorkshop(*F.Driver, 2000, Error))) { return false; }
    const int64 FoeBefore = F.Body(Foe).Resources.HpP2, HolderBefore = Body.Resources.HpP2;
    if (!TestEqual(TEXT("One counter is pending"), FAccess::PendingHits(*F.Driver).Num(), 1) || !Drain(*this, F)) { return false; }
    TestTrue(TEXT("The counter really damages the foe"), F.Body(Foe).Resources.HpP2 < FoeBefore);
    Budget = BudgetOf(Body, Bolt.EffectId, TEXT("component_hurt_heal"));
    TestTrue(TEXT("It spends no C11 budget, heals nothing and refills no store"), Body.EffectBudgets.Num() == 1 && Budget
        && Budget->SpentP2 == 3000 && Body.Resources.HpP2 == HolderBefore && Body.CounterStoreP2 == 0);
    return true;
}

// R11 (spec §12.1): M01 spreads S21's damage around the struck body; the cut of heals and shields and the attack slow stay on it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventSpreadTest, "Lockdown.Rules.Combat.Events.SpreadDamageOnly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventSpreadTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    constexpr int64 Big = 10000000;
    const FLDCombatBodyRef Caster = F.AddBody(), Target = F.AddBody(ELDVictimKind::Contender, Big),
        Beside = F.AddBody(ELDVictimKind::Contender, Big);
    F.Place(Caster, 0, 0);
    F.Place(Target, 300, 0);
    F.Place(Beside, 300, 130);
    if (!InstallComponents(*this, F, Caster, 1, {On(TEXT("component_crowd_hit"), ELDComponentTargetKind::Active, TEXT("active_sock_shot"))}))
    { return false; }
    FLDCombatActiveTerms Sock;
    FLDCombatHit Payload;
    if (!Released(*this, F, Caster, TEXT("active_sock_shot"), Sock, Payload)) { return false; }
    TestTrue(TEXT("S21's payload carries its cut"), Payload.GrievousBp == 3500 && Payload.AttackSlowBp > 0 && Sock.SpreadProjectile.IsSet());
    FLDCombatAction Action;
    Action.Direction = FVector(1, 0, 0);
    FString Error;
    if (!TestTrue(FString::Printf(TEXT("The sock flies: %s"), *Error), FAccess::DeliverPayload(*F.Driver, F.Actor(Caster), F.Body(Caster),
        Sock, Action, Payload, FLDCombatBodyRef(), FVector::ZeroVector, F.Foot(Caster), 1000, Error)
        && FAccess::Projectiles(*F.Driver).Num() == 1)) { return false; }
    F.Now(1400);
    if (!TestTrue(FString::Printf(TEXT("It lands: %s"), *Error), FAccess::AdvanceProjectiles(*F.Driver, Error)
        && FAccess::PendingHits(*F.Driver).Num() == 2) || !Drain(*this, F)) { return false; }
    const FLDBodyCombatState& Struck = F.Body(Target);
    const FLDBodyCombatState& Splashed = F.Body(Beside);
    TestTrue(TEXT("The struck body takes the cut and the slow"), Struck.Grievous.RatioBp == 3500 && !Struck.AttackSlows.IsEmpty());
    TestTrue(TEXT("The body beside it takes the damage only"), Splashed.Resources.HpP2 < Big && Splashed.Grievous.RatioBp == 0
        && Splashed.Grievous.ExpiresAtMs == 0 && Splashed.AttackSlows.IsEmpty());
    return true;
}

// S20 (spec §9): its owner and the owner's current-match teammates run faster inside the puddle; enemies and teammates outside do
// not; leaving drops it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventZoneHasteTest, "Lockdown.Rules.Combat.Events.ZoneHasteTeam",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventZoneHasteTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    constexpr int64 Big = 10000000;
    const FLDCombatBodyRef Owner = F.AddBody(), Mate = F.AddBody(ELDVictimKind::Contender, Big), Far = F.AddBody(ELDVictimKind::Contender, Big),
        Enemy = F.AddBody(ELDVictimKind::Contender, Big);
    const FGuid Group = FGuid::NewGuid();
    const auto Join = [&](const FLDCombatBodyRef& Ref, uint8 Side)
    {
        F.Body(Ref).IsolationGroup = Group;
        F.Body(Ref).PKSide = Side;
    };
    Join(Owner, 1);
    Join(Mate, 1);
    Join(Far, 1);
    Join(Enemy, 2);
    F.Place(Owner, 250, 0);
    F.Place(Mate, 350, 60);
    F.Place(Far, -600, 0);
    F.Place(Enemy, 300, -80);
    FLDCombatActiveTerms Puddle;
    if (!ActiveTerms(*this, F, TEXT("active_dirty_puddle"), Puddle)) { return false; }
    FString Error;
    const FLDCombatSourceRef S20 = SourceOf(ELDCombatSourceKind::Learned, TEXT("active_dirty_puddle"));
    if (!TestTrue(FString::Printf(TEXT("S20 is released: %s"), *Error), FAccess::ReleaseTerms(*F.Driver, F.Body(Owner), S20, Puddle,
        F.Foot(Owner), F.Origin + FVector(300, 0, 0), Error) && FAccess::Zones(*F.Driver).Num() == 1)) { return false; }
    const FGuid ZoneId = FAccess::Zones(*F.Driver)[0].EffectId;
    const auto Hasted = [&](const FLDCombatBodyRef& Ref)
    { return F.Body(Ref).TimedBonuses.ContainsByPredicate([&ZoneId](const FLDCombatTimedBonus& Row) { return Row.EffectId == ZoneId; }); };
    if (!TestTrue(FString::Printf(TEXT("The zone runs: %s"), *Error), FAccess::AdvanceMechanics(*F.Driver, 1000, Error))) { return false; }
    TestTrue(TEXT("The owner and its teammate inside are hasted"), Hasted(Owner) && Hasted(Mate));
    TestTrue(TEXT("A teammate outside and an enemy inside are not"), !Hasted(Far) && !Hasted(Enemy));
    TestTrue(TEXT("Its pulse still reaches the enemy, never the teammate"), FAccess::PendingHits(*F.Driver).ContainsByPredicate(
        [&Enemy](const FLDCombatHit& Row) { return Row.Target == Enemy; }) && !FAccess::PendingHits(*F.Driver).ContainsByPredicate(
        [&Mate](const FLDCombatHit& Row) { return Row.Target == Mate; }));
    FAccess::PendingHits(*F.Driver).Reset();
    F.Place(Mate, 650, 0);
    F.Place(Far, 300, 60);
    F.Now(1100);
    if (!TestTrue(FString::Printf(TEXT("The zone runs on: %s"), *Error), FAccess::AdvanceMechanics(*F.Driver, 1100, Error))) { return false; }
    TestTrue(TEXT("Leaving drops it; entering grants it"), !Hasted(Mate) && Hasted(Far) && Hasted(Owner));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventZoneControlAmpTest, "Lockdown.Rules.Combat.Events.ZoneControlAmp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventZoneControlAmpTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    constexpr int64 HP = 10000000;
    const FLDCombatBodyRef Caster = F.AddBody(), Slowed = F.AddBody(ELDVictimKind::Contender, HP),
        Clear = F.AddBody(ELDVictimKind::Contender, HP);
    F.Place(Caster, 0, 0);
    F.Place(Slowed, 300, 50);
    F.Place(Clear, 300, -50);
    if (!InstallComponents(*this, F, Caster, 1, {{TEXT("component_kick_down")}})) { return false; }
    if (!TestTrue(TEXT("B07 projects its 12% term before release"), F.Body(Caster).AttributeProjection.ControlAmp.IsSet()
        && F.Body(Caster).AttributeProjection.ControlAmp->RatioBp == 1200)) { return false; }
    F.Body(Slowed).Slows.Add({FGuid::NewGuid(), Caster.Victim.EntityId, 3000, 9000});
    FLDCombatActiveTerms Zone;
    if (!ActiveTerms(*this, F, TEXT("active_dirty_puddle"), Zone)) { return false; }
    const FLDCombatSourceRef S20 = SourceOf(ELDCombatSourceKind::Learned, TEXT("active_dirty_puddle"));
    FString Error;
    if (!TestTrue(FString::Printf(TEXT("S20 is released: %s"), *Error), FAccess::ReleaseTerms(*F.Driver, F.Body(Caster),
            S20, Zone, F.Foot(Caster), F.Origin + FVector(300, 0, 0), Error))
        || !TestTrue(FString::Printf(TEXT("The real zone emits its first pulse: %s"), *Error),
            FAccess::AdvanceMechanics(*F.Driver, 1000, Error)))
    { return false; }
    const TArray<FLDCombatHit>& Hits = FAccess::PendingHits(*F.Driver);
    const FLDCombatHit* ControlledHit = Hits.FindByPredicate([&Slowed](const FLDCombatHit& Hit)
        { return Hit.Target == Slowed && Hit.bDirectAbilityDamage; });
    const FLDCombatHit* ClearHit = Hits.FindByPredicate([&Clear](const FLDCombatHit& Hit)
        { return Hit.Target == Clear && Hit.bDirectAbilityDamage; });
    if (!TestNotNull(TEXT("Zone emits a damage pulse for the slowed target"), ControlledHit)
        || !TestNotNull(TEXT("Zone emits a damage pulse for the clear target"), ClearHit))
    { return false; }
    const bool bControlledStamped = TestEqual(TEXT("Slowed target pulse carries B07"), ControlledHit->ControlAmpBp, int64(1200));
    const bool bClearStamped = TestEqual(TEXT("Clear target pulse carries B07"), ClearHit->ControlAmpBp, int64(1200));
    if (!bControlledStamped || !bClearStamped) { return false; }
    if (!Drain(*this, F)) { return false; }
    const int64 ClearDamage = HP - F.Body(Clear).Resources.HpP2;
    const int64 SlowedDamage = HP - F.Body(Slowed).Resources.HpP2;
    AddInfo(FString::Printf(TEXT("LD_EVAL_MEASURE b07_zone clear_hp_loss_p2=%lld slowed_hp_loss_p2=%lld"),
        ClearDamage, SlowedDamage));
    TestTrue(TEXT("Only the already-slowed target receives the 12% bonus"),
        ClearDamage > 0 && SlowedDamage == ClearDamage * 11200 / 10000);
    return true;
}

// C01 (spec §13.3): fixed 1 s windows from its first install; a window settles after every fire fact up to its boundary;
// an uninstall drops the open window and a reinstall keeps the original boundaries.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventFireWindowTest, "Lockdown.Rules.Combat.Events.FireShieldWindows",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventFireWindowTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(), Target = F.AddBody(ELDVictimKind::Contender, 10000000);
    if (!InstallComponents(*this, F, Holder, 1, {{TEXT("component_fist_fire")},
        On(TEXT("component_fire_skin"), ELDComponentTargetKind::Component, TEXT("component_fist_fire"))}))
    { return false; }
    FLDBodyCombatState& Body = F.Body(Holder);
    FLDCombatEntityRecord& Record = FAccess::Records(*F.Driver).FindChecked(Holder.Victim.EntityId);
    const FLDAttributeProjection Installed = Body.BuildProjection;
    const FLDAttributeProjection Bare = ProjectComponents(*F.Catalog, 1, {{TEXT("component_fist_fire")}}).Value.GetValue();
    FString Error;
    Record.Projection = Installed;
    if (!TestTrue(TEXT("C01 is installed at 1000"), FAccess::NoteInstall(*F.Driver, Record, Bare, &Body, 1000, Error)
        && Record.FireWindowAnchorMs.IsSet() && Record.FireWindowAnchorMs.GetValue() == 1000)) { return false; }
    // B01's layer at 1500 ticks 1.50 at 2000, 2500, 3000, 3500 ...
    F.Now(1500);
    if (!Basic(*this, F, Holder, Target) || !Drain(*this, F) || !TestEqual(TEXT("One B01 layer"), F.Body(Target).Burn.Layers.Num(), 1))
    { return false; }
    const auto Boundary = [&](int64 AtMs)
    {
        F.Now(AtMs);
        return TestTrue(FString::Printf(TEXT("Burns at %lld: %s"), AtMs, *Error), FAccess::AdvanceBurns(*F.Driver, AtMs, Error))
            && Drain(*this, F);
    };
    if (!Boundary(2000)) { return false; }
    TestTrue(TEXT("(1000, 2000] holds the tick on its boundary and settles after it: 12G + 60% of 1.50"),
        Body.Resources.ShieldP2 == 1290 && Body.FireWindowEndMs == 0);
    if (!Boundary(2500)) { return false; }
    TestTrue(TEXT("(2000, 3000] stays open"), Body.FireWindowEndMs == 3000 && Body.FireWindowSumP2 == 150 && Body.Resources.ShieldP2 == 1290);
    TestEqual(TEXT("An open fire window schedules its own closing boundary"), FAccess::NextWorkshopDue(*F.Driver, 2500), int64(3000));
    if (!Boundary(3000)) { return false; }
    TestTrue(TEXT("Its boundary tick joins it before it settles: 12G + 60% of 3.00 replaces the weaker shield"),
        Body.Resources.ShieldP2 == 1380 && Body.FireWindowEndMs == 0);
    if (!Boundary(3500)) { return false; }
    Record.Projection = Bare;
    TestTrue(TEXT("An uninstall drops the open window"), FAccess::NoteInstall(*F.Driver, Record, Installed, &Body, 3600, Error)
        && Body.FireWindowEndMs == 0 && Body.FireWindowSumP2 == 0);
    Record.Projection = Installed;
    TestTrue(TEXT("A reinstall keeps the first boundaries"), FAccess::NoteInstall(*F.Driver, Record, Bare, &Body, 3700, Error)
        && Record.FireWindowAnchorMs.GetValue() == 1000);
    if (!Boundary(4000)) { return false; }
    TestEqual(TEXT("The tick on 4000 closes (3000, 4000] at once"), Body.FireWindowEndMs, int64(0));
    return true;
}
// ---- Skill-rule base components (B04, B13-B16) on the real basic and release payloads. A "native direct hit" is a
// hostile ability hit of the host active's own release (IsNativeDirectHit); basics never are.
namespace LDBuildPrimitivesTests
{
// One S06 release of Caster as Combat builds it, aimed at Target (its payload applied alone, as a landed hit).
bool SnarkBolt(FAutomationTestBase& Test, FFixture& F, const FLDCombatBodyRef& Caster, const FLDCombatBodyRef& Target,
    FLDCombatHit& Payload)
{
    FLDCombatActiveTerms Terms;
    if (!Released(Test, F, Caster, TEXT("active_snark_bolt"), Terms, Payload)) { return false; }
    Payload.Target = Target;
    return Test.TestTrue(TEXT("S06's payload is a native direct hit with its release ledger"),
        IsNativeDirectHit(Payload) && Payload.CastLedger.IsValid());
}

bool Land(FAutomationTestBase& Test, FFixture& F, const FLDCombatHit& Hit)
{
    FString Error;
    if (!F.Apply(Hit, Error)) { Test.AddError(TEXT("Hit: ") + Error); return false; }
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentPunchHealShareTest, "Lockdown.Rules.Combat.Components.PunchHealOwnShare",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentPunchHealShareTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    constexpr int64 HP = 10000000;
    const FLDCombatBodyRef Holder = F.AddBody(ELDVictimKind::Contender, HP), Target = F.AddBody(ELDVictimKind::Contender, HP);
    if (!InstallComponents(*this, F, Holder, 1, {{TEXT("component_punch_heal")}}) || !InstallComponents(*this, F, Target, 1, {}))
    { return false; }
    FLDBodyCombatState& Body = F.Body(Holder);
    FLDBodyCombatState& Victim = F.Body(Target);
    Body.AttributeProjection.Final.CritChanceBp = 0;
    const auto Swing = [&](int64& Lost, int64& Healed)
    {
        Body.Resources.HpP2 = HP / 2;
        const int64 VictimBefore = Victim.Resources.HpP2;
        if (!Basic(*this, F, Holder, Target) || !Drain(*this, F)) { return false; }
        Lost = VictimBefore - Victim.Resources.HpP2;
        Healed = Body.Resources.HpP2 - HP / 2;
        return true;
    };
    int64 Lost = 0, Healed = 0;
    if (!Swing(Lost, Healed)) { return false; }
    TestTrue(TEXT("A plain basic heals 12% of the HP it took"), Lost > 0 && Healed == Lost * 1200 / 10000);

    // S17's empowered package merges into the next basic; only the basic's own share of its HP loss heals.
    FLDCombatActiveTerms S17;
    if (!ActiveTerms(*this, F, TEXT("active_triple_combo"), S17)) { return false; }
    const FLDDemoEffectDefinition* Empower = FindEffect(S17.Values, ELDDemoEffectOp::EmpowerBasic);
    FLDCombatEmpower Package;
    FString Error;
    if (!TestTrue(FString::Printf(TEXT("S17's package is armed: %s"), *Error), Empower
        && FAccess::MakeEmpower(*F.Driver, Body, SourceOf(ELDCombatSourceKind::Learned, TEXT("active_triple_combo")), *Empower,
            F.Mode->GetAuthorityNowMs(), Package, Error)))
    { return false; }
    LDCombatControl::ArmEmpower(Body.Empowers, Package);
    int64 EmpoweredLost = 0, EmpoweredHealed = 0;
    if (!Swing(EmpoweredLost, EmpoweredHealed)) { return false; }
    AddInfo(FString::Printf(TEXT("LD_EVAL_MEASURE b04_share plain_loss_p2=%lld plain_heal_p2=%lld empowered_loss_p2=%lld empowered_heal_p2=%lld"),
        Lost, Healed, EmpoweredLost, EmpoweredHealed));
    TestTrue(TEXT("The S17 empowered hit takes more HP but heals less than 12% of it, never less than the plain share"),
        EmpoweredLost > Lost && EmpoweredHealed < EmpoweredLost * 1200 / 10000 && EmpoweredHealed + 1 >= Healed);

    // A shield that absorbs the whole basic leaves no HP loss to heal from.
    if (!TestTrue(FString::Printf(TEXT("The target is shielded: %s"), *Error),
        FAccess::GrantShield(*F.Driver, Victim, HP, F.Mode->GetAuthorityNowMs(), Error))) { return false; }
    if (!Swing(Lost, Healed)) { return false; }
    TestTrue(TEXT("A wholly absorbed basic heals nothing"), Lost == 0 && Healed == 0 && Victim.Resources.ShieldP2 < HP);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentCastHitRefundTest, "Lockdown.Rules.Combat.Components.CastHitRefund",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentCastHitRefundTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(), Target = F.AddBody(ELDVictimKind::Contender, 10000000),
        Second = F.AddBody(ELDVictimKind::Contender, 10000000);
    if (!InstallComponents(*this, F, Holder, 1, {{TEXT("component_catch_breath")}})) { return false; }
    FLDBodyCombatState& Body = F.Body(Holder);
    Body.Resources.MaxMpP2 = 100000;
    Body.Resources.MpP2 = 0;
    FLDCombatHit Payload;
    if (!SnarkBolt(*this, F, Holder, Target, Payload)) { return false; }
    // ReleaseAbility records the mana the release really paid (after M06) on its ledger.
    Payload.CastLedger->PaidManaP2 = 4000;
    if (!Land(*this, F, Payload)) { return false; }
    TestEqual(TEXT("The first native hit refunds 30% of the 40 MP paid"), Body.Resources.MpP2, int64(1200));
    FLDCombatHit Again = Payload;
    Again.HitId = FGuid::NewGuid();
    Again.Target = Second;
    if (!Land(*this, F, Again)) { return false; }
    TestEqual(TEXT("Another hit of the same release refunds nothing more"), Body.Resources.MpP2, int64(1200));
    FLDCombatHit Free;
    if (!SnarkBolt(*this, F, Holder, Target, Free)) { return false; }
    Free.CastLedger->PaidManaP2 = 0;
    if (!Land(*this, F, Free)) { return false; }
    TestEqual(TEXT("A release that paid nothing refunds nothing"), Body.Resources.MpP2, int64(1200));
    if (!Basic(*this, F, Holder, Target) || !Drain(*this, F)) { return false; }
    TestEqual(TEXT("A basic is no native hit"), Body.Resources.MpP2, int64(1200));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentDistanceAmpTest, "Lockdown.Rules.Combat.Components.DistanceAmp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentDistanceAmpTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    constexpr int64 HP = 10000000;
    const FLDCombatBodyRef Holder = F.AddBody(), Plain = F.AddBody(), Target = F.AddBody(ELDVictimKind::Contender, HP);
    if (!InstallComponents(*this, F, Holder, 1, {{TEXT("component_long_arm")}}) || !InstallComponents(*this, F, Plain, 1, {})
        || !InstallComponents(*this, F, Target, 1, {}))
    { return false; }
    const auto LossAt = [&](const FLDCombatBodyRef& Caster, double DistanceCm, int64& Out)
    {
        F.Place(Caster, 0, 0);
        F.Place(Target, DistanceCm, 0);
        FLDCombatHit Payload;
        F.Body(Target).Resources.HpP2 = HP;
        if (!SnarkBolt(*this, F, Caster, Target, Payload) || !Land(*this, F, Payload)) { return false; }
        Out = HP - F.Body(Target).Resources.HpP2;
        return true;
    };
    int64 Base = 0, Near = 0, Far = 0, Farther = 0;
    if (!LossAt(Plain, 600, Base) || !LossAt(Holder, 549, Near) || !LossAt(Holder, 550, Far) || !LossAt(Holder, 900, Farther))
    { return false; }
    TestTrue(TEXT("Below 550 cm B14 adds nothing"), Base > 0 && Near == Base);
    TestTrue(TEXT("At 550 cm and beyond B14 adds 20% to the native direct hit"),
        FMath::Abs(Far * 10000 - Base * 12000) <= 20000 && Farther == Far);
    // A basic is no native hit, however far it lands.
    int64 PlainBasic = 0, FarBasic = 0;
    for (const FLDCombatBodyRef& Caster : {Plain, Holder})
    {
        F.Place(Caster, 0, 0);
        F.Place(Target, 600, 0);
        F.Body(Target).Resources.HpP2 = HP;
        F.Body(Caster).AttributeProjection.Final.CritChanceBp = 0;
        if (!Basic(*this, F, Caster, Target) || !Drain(*this, F)) { return false; }
        (Caster == Plain ? PlainBasic : FarBasic) = HP - F.Body(Target).Resources.HpP2;
    }
    TestTrue(TEXT("B14 never amplifies a basic"), PlainBasic > 0 && FarBasic == PlainBasic);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentAlternateAmpTest, "Lockdown.Rules.Combat.Components.AlternateAmp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentAlternateAmpTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    constexpr int64 HP = 10000000;
    const FLDCombatBodyRef Holder = F.AddBody(), Target = F.AddBody(ELDVictimKind::Contender, HP);
    if (!InstallComponents(*this, F, Holder, 1, {{TEXT("component_seeing_red")}}) || !InstallComponents(*this, F, Target, 1, {}))
    { return false; }
    FLDBodyCombatState& Body = F.Body(Holder);
    const FLDCombatSourceRef Elbow = SourceOf(ELDCombatSourceKind::Learned, TEXT("active_elbow")),
        Bolt = SourceOf(ELDCombatSourceKind::Learned, TEXT("active_snark_bolt")),
        Dash = SourceOf(ELDCombatSourceKind::Innate, TEXT("innate_dash"));
    FString Error;
    const auto AmpAt = [&](const FLDCombatSourceRef& Source, int64 NowMs)
    {
        int64 Bp = -1;
        return FAccess::AlternateAmp(*F.Driver, Body, Source, NowMs, Bp, Error) ? Bp : int64(-1);
    };
    TestEqual(TEXT("A first release takes nothing"), AmpAt(Bolt, 1000), int64(0));
    FAccess::NoteRelease(Body, Elbow, 1000);
    TestEqual(TEXT("A different active within 4 s takes +20%"), AmpAt(Bolt, 5000), int64(2000));
    TestEqual(TEXT("The same active again takes nothing"), AmpAt(Elbow, 2000), int64(0));
    TestEqual(TEXT("After 4 s the window is gone"), AmpAt(Bolt, 5001), int64(0));
    TestEqual(TEXT("The innate dash takes nothing"), AmpAt(Dash, 2000), int64(0));
    FAccess::NoteRelease(Body, Dash, 2000);
    TestTrue(TEXT("The innate dash is no previous release"), Body.LastActiveRelease == Elbow.DefinitionId
        && Body.LastActiveReleaseAtMs == 1000);

    // The release locks the amplification on its ledger; only its native direct hits take it.
    const auto Loss = [&](int64 AmpBp, int64& Out)
    {
        FLDCombatHit Payload;
        F.Body(Target).Resources.HpP2 = HP;
        if (!SnarkBolt(*this, F, Holder, Target, Payload)) { return false; }
        Payload.CastLedger->AlternateAmpBp = AmpBp;
        if (!Land(*this, F, Payload)) { return false; }
        Out = HP - F.Body(Target).Resources.HpP2;
        return true;
    };
    int64 Plain = 0, Amplified = 0;
    if (!Loss(0, Plain) || !Loss(2000, Amplified)) { return false; }
    TestTrue(TEXT("The locked +20% joins the hit's amplification pool"), Plain > 0
        && FMath::Abs(Amplified * 10000 - Plain * 12000) <= 20000);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentAbilityHitShieldTest, "Lockdown.Rules.Combat.Components.AbilityHitShield",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentAbilityHitShieldTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(), Target = F.AddBody(ELDVictimKind::Contender, 10000000);
    if (!InstallComponents(*this, F, Holder, 1, {{TEXT("component_thick_hide")}}) || !InstallComponents(*this, F, Target, 1, {}))
    { return false; }
    FLDBodyCombatState& Body = F.Body(Holder);
    const auto Clear = [&Body]()
    {
        Body.Shield.Reset();
        Body.Resources.ShieldP2 = 0;
        Body.ShieldGrantedP2 = 0;
    };
    FString Error;
    if (!Basic(*this, F, Holder, Target) || !Drain(*this, F)) { return false; }
    TestTrue(TEXT("A basic grants no shield"), Body.Resources.ShieldP2 == 0 && !Body.Shield.IsSet());
    if (!TestTrue(TEXT("The foe's ability hit lands"), F.Apply(FFixture::Hit(Target, Holder, 1000), Error))) { return false; }
    TestTrue(TEXT("Being hit grants no shield"), Body.Resources.ShieldP2 == 0);
    const auto Bolt = [&](int64 NowMs)
    {
        F.Now(NowMs);
        FLDCombatHit Payload;
        return SnarkBolt(*this, F, Holder, Target, Payload) && Land(*this, F, Payload);
    };
    if (!Bolt(2000)) { return false; }
    const int64 Expected = 4000 + Body.Resources.MaxHpP2 * 300 / 10000;
    TestTrue(TEXT("A native ability hit grants 40G + 3% max HP for 3 s from B16"), Body.Resources.ShieldP2 == Expected
        && Body.Shield.IsSet() && Body.Shield->ExpiresAtMs == 5000
        && Body.ShieldGrantSource == SourceOf(ELDCombatSourceKind::Component, TEXT("component_thick_hide"))
        && Body.AbilityHitShieldReadyAtMs == 10000);
    Clear();
    if (!Bolt(9999)) { return false; }
    TestEqual(TEXT("Within its 8 s rest no shield is granted"), Body.Resources.ShieldP2, int64(0));
    if (!Bolt(10000)) { return false; }
    TestTrue(TEXT("After the rest the next native hit grants it again"), Body.Resources.ShieldP2 == Expected
        && Body.AbilityHitShieldReadyAtMs == 18000);
    return true;
}
// ---- M13-M15: derived host hits of the same release (spec §4 "Hit identity vocabulary").
namespace LDBuildPrimitivesTests
{
// One release of Active by Caster, sent along +X through the real projectile delivery.
bool Throw(FAutomationTestBase& Test, FFixture& F, const FLDCombatBodyRef& Caster, const FLDCombatActiveTerms& Terms,
    const FLDCombatHit& Payload)
{
    FLDCombatAction Action;
    Action.Direction = FVector(1, 0, 0);
    FString Error;
    if (!FAccess::DeliverPayload(*F.Driver, F.Actor(Caster), F.Body(Caster), Terms, Action, Payload, FLDCombatBodyRef(),
        FVector::ZeroVector, F.Foot(Caster), F.Mode->GetAuthorityNowMs(), Error))
    {
        Test.AddError(TEXT("Projectile delivery: ") + Error);
        return false;
    }
    return true;
}

bool Fly(FAutomationTestBase& Test, FFixture& F, int64 NowMs)
{
    F.Now(NowMs);
    FString Error;
    if (!FAccess::AdvanceProjectiles(*F.Driver, Error)) { Test.AddError(TEXT("Projectiles: ") + Error); return false; }
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentProjectileReturnTest, "Lockdown.Rules.Combat.Components.ProjectileReturn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentProjectileReturnTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    constexpr int64 HP = 10000000;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(), Target = F.AddBody(ELDVictimKind::Contender, HP);
    F.Place(Holder, 0, 0);
    F.Place(Target, 300, 0);
    if (!InstallComponents(*this, F, Holder, 10, {{TEXT("component_catch_breath")},
        On(TEXT("component_boomerang"), ELDComponentTargetKind::Active, TEXT("active_snark_bolt"))})
        || !InstallComponents(*this, F, Target, 1, {})) { return false; }
    FLDBodyCombatState& Body = F.Body(Holder);
    Body.AttributeProjection.Final.CritChanceBp = 0;
    Body.Resources.MaxMpP2 = 100000;
    Body.Resources.MpP2 = 0;
    F.Now(1000);
    FLDCombatActiveTerms Terms;
    FLDCombatHit Payload;
    if (!Released(*this, F, Holder, TEXT("active_snark_bolt"), Terms, Payload)) { return false; }
    Payload.CastLedger->PaidManaP2 = 4000;
    if (!TestTrue(TEXT("M13 compiles onto S06"), Terms.ProjectileReturn.IsSet() && Payload.SlowBp > 0)
        || !Throw(*this, F, Holder, Terms, Payload) || !Fly(*this, F, 1400)) { return false; }
    const TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    const TArray<FLDCombatProjectile>& Flights = FAccess::Projectiles(*F.Driver);
    if (!TestTrue(TEXT("The outbound hit lands whole and a return flight starts where it ended"), Pending.Num() == 1
        && Pending[0].Target == Target && Pending[0].DeliveryScaleBp == 10000 && Flights.Num() == 1 && Flights[0].bReturn
        && Flights[0].EffectId == Payload.EffectId && Flights[0].ReturnScaleBp == 0 && Flights[0].PierceRemaining == 0))
    { return false; }
    const int64 Before = F.Body(Target).Resources.HpP2;
    if (!Drain(*this, F)) { return false; }
    const int64 Outbound = Before - F.Body(Target).Resources.HpP2;
    TestEqual(TEXT("B13 refunds 30% of the paid 40 MP on the first native hit"), Body.Resources.MpP2, int64(1200));
    if (!Fly(*this, F, 2400)) { return false; }
    if (!TestTrue(TEXT("The return strikes the body it started on at 50% without slow and does not return again"),
        Pending.Num() == 1 && Pending[0].Target == Target && Pending[0].DeliveryScaleBp == 5000 && Pending[0].SlowBp == 0
        && Pending[0].SlowDurationMs == 0 && IsNativeDirectHit(Pending[0]) && Pending[0].CastLedger == Payload.CastLedger
        && Flights.IsEmpty())) { return false; }
    const int64 Mid = F.Body(Target).Resources.HpP2;
    if (!Drain(*this, F)) { return false; }
    const int64 Back = Mid - F.Body(Target).Resources.HpP2;
    AddInfo(FString::Printf(TEXT("LD_EVAL_MEASURE m13 outbound_p2=%lld return_p2=%lld"), Outbound, Back));
    TestTrue(TEXT("The return deals half the outbound damage, within rounding"), Outbound > 0 && FMath::Abs(Back * 2 - Outbound) <= 2);
    TestEqual(TEXT("B13 is budgeted per release: the return refunds nothing more"), Body.Resources.MpP2, int64(1200));

    // Elements, burn and grievous ride home; slow and controls do not. A return never starts for a dead thrower.
    FLDCombatActiveTerms Sock;
    FLDCombatHit SockPayload;
    if (!InstallComponents(*this, F, Holder, 10, {On(TEXT("component_boomerang"), ELDComponentTargetKind::Active, TEXT("active_sock_shot"))})
        || !Released(*this, F, Holder, TEXT("active_sock_shot"), Sock, SockPayload)) { return false; }
    SockPayload.BurnStacks = 1;
    SockPayload.bApplyWet = true;
    F.Now(3000);
    if (!Throw(*this, F, Holder, Sock, SockPayload) || !Fly(*this, F, 3400)) { return false; }
    if (!TestTrue(TEXT("S21 launches a return"), Flights.Num() == 1 && Flights[0].bReturn)) { return false; }
    const FLDCombatHit& Home = Flights[0].Payload;
    TestTrue(TEXT("The return keeps burn, wet and grievous but drops the attack slow"), SockPayload.GrievousBp > 0
        && SockPayload.AttackSlowBp > 0 && Home.BurnStacks == 1 && Home.bApplyWet && Home.GrievousBp == SockPayload.GrievousBp
        && Home.AttackSlowBp == 0 && Home.SlowBp == 0 && Home.RootDurationMs == 0 && Home.StunDurationMs == 0);
    FAccess::PendingHits(*F.Driver).Reset();
    FAccess::Projectiles(*F.Driver).Reset();
    F.Now(4000);
    if (!Throw(*this, F, Holder, Sock, SockPayload)) { return false; }
    Body.bPendingDeath = true;
    if (!Fly(*this, F, 4400)) { return false; }
    TestTrue(TEXT("A thrower pending death gets no return"), Pending.Num() == 1 && Flights.IsEmpty());
    Body.bPendingDeath = false;
    {
        FFixture Combo;
        if (!Combo.Init(*this, true)) { return false; }
        const FLDCombatBodyRef Caster = Combo.AddBody(), First = Combo.AddBody(ELDVictimKind::Contender, HP),
            Second = Combo.AddBody(ELDVictimKind::Contender, HP);
        Combo.Place(Caster, 0, 0);
        Combo.Place(First, 200, 0);
        Combo.Place(Second, 450, 0);
        if (!InstallComponents(*this, Combo, Caster, 10, {{TEXT("component_catch_breath")},
            On(TEXT("component_boomerang"), ELDComponentTargetKind::Active, TEXT("active_snark_bolt")),
            On(TEXT("component_skewer"), ELDComponentTargetKind::Active, TEXT("active_snark_bolt"))})
            || !InstallComponents(*this, Combo, First, 1, {}) || !InstallComponents(*this, Combo, Second, 1, {}))
        { return false; }
        FLDBodyCombatState& ComboBody = Combo.Body(Caster);
        ComboBody.Resources.MaxMpP2 = 100000;
        ComboBody.Resources.MpP2 = 0;
        Combo.Now(1000);
        FLDCombatActiveTerms CombinedTerms;
        FLDCombatHit CombinedPayload;
        if (!Released(*this, Combo, Caster, TEXT("active_snark_bolt"), CombinedTerms, CombinedPayload)) { return false; }
        CombinedPayload.CastLedger->PaidManaP2 = 4000;
        if (!TestTrue(TEXT("Pierce and return both compile on the same release"),
            CombinedTerms.ProjectileReturn.IsSet() && CombinedTerms.ProjectilePierce.IsSet())
            || !Throw(*this, Combo, Caster, CombinedTerms, CombinedPayload) || !Fly(*this, Combo, 2000)) { return false; }
        const auto& Hits = FAccess::PendingHits(*Combo.Driver);
        const auto& CombinedFlights = FAccess::Projectiles(*Combo.Driver);
        if (!TestTrue(TEXT("The outbound flight hits two distinct bodies and launches one return"), Hits.Num() == 2
            && Hits[0].Target == First && Hits[0].DeliveryScaleBp == 10000
            && Hits[1].Target == Second && Hits[1].DeliveryScaleBp == 7000
            && Hits[0].HitId != Hits[1].HitId && Hits[0].EffectId == CombinedPayload.EffectId
            && Hits[1].EffectId == CombinedPayload.EffectId && IsNativeDirectHit(Hits[0]) && IsNativeDirectHit(Hits[1])
            && !Hits[0].bCritical && !Hits[1].bCritical && Hits[0].CastLedger == CombinedPayload.CastLedger
            && Hits[1].CastLedger == CombinedPayload.CastLedger && CombinedFlights.Num() == 1 && CombinedFlights[0].bReturn))
        { return false; }
        if (!Drain(*this, Combo)) { return false; }
        TestEqual(TEXT("The pierced hits share one mana-refund budget"), ComboBody.Resources.MpP2, int64(1200));
        if (!Fly(*this, Combo, 2100)) { return false; }
        if (!TestTrue(TEXT("The return hits the last body once at 50%, without control, and ends"),
            Hits.Num() == 1 && Hits[0].Target == Second && Hits[0].DeliveryScaleBp == 5000 && Hits[0].SlowBp == 0
            && Hits[0].EffectId == CombinedPayload.EffectId && Hits[0].CastLedger == CombinedPayload.CastLedger
            && IsNativeDirectHit(Hits[0]) && !Hits[0].bCritical && CombinedFlights.IsEmpty())) { return false; }
        if (!Drain(*this, Combo) || !Fly(*this, Combo, 3000)) { return false; }
        TestTrue(TEXT("The return neither repeats nor refunds mana again"),
            Hits.IsEmpty() && CombinedFlights.IsEmpty() && ComboBody.Resources.MpP2 == 1200);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentPierceChainTest, "Lockdown.Rules.Combat.Components.PierceAndChainExtra",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentPierceChainTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    constexpr int64 HP = 10000000;
    {
        FFixture F;
        if (!F.Init(*this, true)) { return false; }
        const FLDCombatBodyRef Holder = F.AddBody(), First = F.AddBody(ELDVictimKind::Contender, HP),
            Second = F.AddBody(ELDVictimKind::Contender, HP), Third = F.AddBody(ELDVictimKind::Contender, HP);
        F.Place(Holder, 0, 0);
        F.Place(First, 200, 0);
        F.Place(Second, 450, 0);
        F.Place(Third, 700, 0);
        if (!InstallComponents(*this, F, Holder, 1, {On(TEXT("component_skewer"), ELDComponentTargetKind::Active, TEXT("active_snark_bolt"))}))
        { return false; }
        F.Now(1000);
        FLDCombatActiveTerms Terms;
        FLDCombatHit Payload;
        if (!Released(*this, F, Holder, TEXT("active_snark_bolt"), Terms, Payload) || !Throw(*this, F, Holder, Terms, Payload)
            || !Fly(*this, F, 2000)) { return false; }
        const TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
        TestTrue(TEXT("M14 carries S06 through the first body to one more distinct body at 70% with its slow, then stops"),
            Pending.Num() == 2 && Pending[0].Target == First && Pending[0].DeliveryScaleBp == 10000 && Pending[1].Target == Second
            && Pending[1].DeliveryScaleBp == 7000 && Pending[1].SlowBp == Payload.SlowBp && Pending[1].SlowBp > 0
            && IsNativeDirectHit(Pending[1]) && Pending[1].CastLedger == Payload.CastLedger
            && FAccess::Projectiles(*F.Driver).IsEmpty());
    }
    // On S10 the same mod adds exactly one bounce.
    const auto Hops = [this](bool bMod, int32& Out)
    {
        FFixture F;
        if (!F.Init(*this, true)) { return false; }
        const FLDCombatBodyRef Holder = F.AddBody();
        TArray<FLDCombatBodyRef> Line;
        for (int32 Index = 0; Index < 5; ++Index)
        {
            Line.Add(F.AddBody(ELDVictimKind::Contender, HP));
            F.Place(Line.Last(), 300 + Index * 200, 0);
        }
        F.Place(Holder, 0, 0);
        const TArray<FInstall> Mods = bMod
            ? TArray<FInstall>{On(TEXT("component_skewer"), ELDComponentTargetKind::Active, TEXT("active_zap"))} : TArray<FInstall>{};
        if (!InstallComponents(*this, F, Holder, 1, Mods)) { return false; }
        F.Now(1000);
        FLDCombatActiveTerms Terms;
        FLDCombatHit Payload;
        if (!Released(*this, F, Holder, TEXT("active_zap"), Terms, Payload)) { return false; }
        const FLDDemoEffectDefinition* Bounce = FindEffect(Terms.Values, ELDDemoEffectOp::ChainBounce);
        if (!TestTrue(TEXT("S10 compiles its chain"), Bounce && Bounce->Count == (bMod ? 3 : 2))
            || !DeliverOnUnit(*this, F, Holder, Terms, Payload, Line[0])) { return false; }
        FString Error;
        F.Now(3000);
        if (!TestTrue(FString::Printf(TEXT("Chains advance: %s"), *Error), FAccess::AdvanceElements(*F.Driver, 3000, Error)))
        { return false; }
        Out = FAccess::PendingHits(*F.Driver).Num();
        return true;
    };
    int32 Plain = 0, Extra = 0;
    if (!Hops(false, Plain) || !Hops(true, Extra)) { return false; }
    TestTrue(TEXT("M14 adds exactly one S10 hop"), Plain == 3 && Extra == 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentEchoImpactTest, "Lockdown.Rules.Combat.Components.EchoImpact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentEchoImpactTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    constexpr int64 HP = 10000000;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(), Inner = F.AddBody(ELDVictimKind::Contender, HP),
        Outer = F.AddBody(ELDVictimKind::Contender, HP);
    F.Place(Holder, 0, 0);
    F.Place(Inner, 300, 0);
    F.Place(Outer, 300, 150);
    if (!InstallComponents(*this, F, Holder, 10, {On(TEXT("component_encore"), ELDComponentTargetKind::Active, TEXT("active_calculus_drop"))}))
    { return false; }
    FLDCombatActiveTerms Plain, Drop;
    FLDCombatHit Payload;
    if (!ActiveTerms(*this, F, TEXT("active_calculus_drop"), Plain)
        || !Released(*this, F, Holder, TEXT("active_calculus_drop"), Drop, Payload)) { return false; }
    TestTrue(TEXT("M15 multiplies S18's cooldown by 1.15"), Drop.EchoImpact.IsSet() && Plain.Values.CooldownMs == 12000
        && Drop.Values.CooldownMs == 13800);
    TArray<FLDCombatGroundEffect>& Ground = FAccess::Ground(*F.Driver);
    TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    FString Error;
    const auto Advance = [&](int64 AtMs)
    {
        F.Now(AtMs);
        return TestTrue(FString::Printf(TEXT("Skill content at %lld: %s"), AtMs, *Error), FAccess::AdvanceSkillContent(*F.Driver, AtMs, Error));
    };
    F.Now(1000);
    FLDCombatAction Action;
    Action.TargetPoint = F.Origin + FVector(300, 0, 0);
    bool bHandled = false;
    if (!TestTrue(FString::Printf(TEXT("S18 is placed: %s"), *Error), FAccess::DeliverTerms(*F.Driver, F.Body(Holder), Drop, Action,
        Payload, 1000, bHandled, Error) && bHandled && Ground.Num() == 1 && Ground[0].Echo.IsSet())) { return false; }
    if (!Advance(1550)) { return false; }
    const auto Scale = [&Pending](const FLDCombatBodyRef& Ref)
    {
        const FLDCombatHit* Hit = Pending.FindByPredicate([&Ref](const FLDCombatHit& Row) { return Row.Target == Ref; });
        return Hit ? Hit->DeliveryScaleBp : int64(-1);
    };
    if (!TestTrue(TEXT("The impact lands whole inside and 60% outside, and one echo is due 0.8 s later"), Pending.Num() == 2
        && Scale(Inner) == 10000 && Scale(Outer) == 6000 && Ground.Num() == 1 && Ground[0].Origin == ELDCombatGroundOrigin::Echo
        && Ground[0].ReadyAtMs == 2350 && !Ground[0].Echo.IsSet() && Ground[0].ObservedKey.IsValid())) { return false; }
    Pending.Reset();
    if (!Advance(2349)) { return false; }
    TestTrue(TEXT("Nothing before 0.8 s"), Pending.IsEmpty() && Ground.Num() == 1);
    if (!Advance(2350)) { return false; }
    TestTrue(TEXT("The echo lands at 50% with the same outline and falloff, as a native hit of the same release"), Pending.Num() == 2
        && Scale(Inner) == 5000 && Scale(Outer) == 3000 && Pending[0].EffectId == Payload.EffectId && IsNativeDirectHit(Pending[0])
        && Ground.IsEmpty());
    Pending.Reset();
    if (!Advance(6000)) { return false; }
    TestTrue(TEXT("The echo does not echo"), Pending.IsEmpty() && Ground.IsEmpty());

    // S07: a blast of its radius at the release foot after 0.8 s at 50% with one burn stack.
    FLDCombatActiveTerms Seethe;
    FLDCombatHit Rage;
    if (!InstallComponents(*this, F, Holder, 10, {On(TEXT("component_encore"), ELDComponentTargetKind::Active, TEXT("active_seethe"))})
        || !Released(*this, F, Holder, TEXT("active_seethe"), Seethe, Rage)) { return false; }
    TestEqual(TEXT("M15 multiplies S07's cooldown by 1.15"), Seethe.Values.CooldownMs, int64(11500));
    F.Now(7000);
    FLDCombatAction Self;
    if (!TestTrue(FString::Printf(TEXT("S07 is released: %s"), *Error), FAccess::DeliverPayload(*F.Driver, F.Actor(Holder),
        F.Body(Holder), Seethe, Self, Rage, FLDCombatBodyRef(), FVector::ZeroVector, F.Foot(Holder), 7000, Error))) { return false; }
    TestTrue(TEXT("S07's echo is scheduled at its foot"), Ground.Num() == 1 && Ground[0].Origin == ELDCombatGroundOrigin::Echo
        && Ground[0].ReadyAtMs == 7800 && Ground[0].RadiusCm == 300 && Ground[0].Payload.BurnStacks == 1
        && Ground[0].Payload.DeliveryScaleBp == 5000 && FVector::Dist2D(Ground[0].CentreCm, F.Foot(Holder)) < 1);

    // Install legality: each mod only on its listed actives.
    TestFalse(TEXT("M13 does not install on S07"), ProjectComponents(*F.Catalog, 10,
        {On(TEXT("component_boomerang"), ELDComponentTargetKind::Active, TEXT("active_seethe"))}).IsSuccess());
    TestFalse(TEXT("M14 does not install on S18"), ProjectComponents(*F.Catalog, 10,
        {On(TEXT("component_skewer"), ELDComponentTargetKind::Active, TEXT("active_calculus_drop"))}).IsSuccess());
    TestFalse(TEXT("M15 does not install on S06"), ProjectComponents(*F.Catalog, 10,
        {On(TEXT("component_encore"), ELDComponentTargetKind::Active, TEXT("active_snark_bolt"))}).IsSuccess());
    TestTrue(TEXT("M14 installs on S10 and M15 on S19"), ProjectComponents(*F.Catalog, 10,
        {On(TEXT("component_skewer"), ELDComponentTargetKind::Active, TEXT("active_zap")),
         On(TEXT("component_encore"), ELDComponentTargetKind::Active, TEXT("active_flyer_trap"))}).IsSuccess());
    return true;
}

// Connectors C13-C16 (docs/systems/COMBAT_AND_GROWTH.md): learned-active cooldown tracks of a fixture contender.
namespace LDBuildPrimitivesTests
{
FLDCombatTrack& TrackOf(FFixture& F, const FLDCombatBodyRef& Ref, const TCHAR* Active, int64 ReadyAtMs)
{
    FLDCombatEntityRecord& Record = FAccess::Records(*F.Driver).FindChecked(Ref.Victim.EntityId);
    FLDCombatTrack* Track = Record.Tracks.FindByPredicate([Active](const FLDCombatTrack& Row) { return Row.DefinitionId == FName(Active); });
    if (!Track)
    {
        Track = &Record.Tracks.AddDefaulted_GetRef();
        Track->IssuedCooldownId = FGuid::NewGuid();
        Track->Kind = ELDCombatSourceKind::Learned;
        Track->DefinitionId = FName(Active);
    }
    Track->ReadyAtMs = ReadyAtMs;
    return *Track;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentHitHasteOtherTest, "Lockdown.Rules.Combat.Components.HitHasteOther",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentHitHasteOtherTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(), Target = F.AddBody(ELDVictimKind::Contender, 10000000),
        Second = F.AddBody(ELDVictimKind::Contender, 10000000);
    if (!InstallComponents(*this, F, Holder, 1,
        {On(TEXT("component_next_up"), ELDComponentTargetKind::Active, TEXT("active_snark_bolt"))})) { return false; }
    F.Now(1000);
    TrackOf(F, Holder, TEXT("active_snark_bolt"), 10000);
    TrackOf(F, Holder, TEXT("active_elbow"), 10000);
    TrackOf(F, Holder, TEXT("active_zap"), 1500);
    const auto ReadyAt = [&F, &Holder](const TCHAR* Active)
    {
        const FLDCombatTrack* Track = FAccess::Records(*F.Driver).FindChecked(Holder.Victim.EntityId).Tracks.FindByPredicate(
            [Active](const FLDCombatTrack& Row) { return Row.DefinitionId == FName(Active); });
        return Track ? Track->ReadyAtMs : int64(-1);
    };
    FLDCombatHit Payload;
    if (!SnarkBolt(*this, F, Holder, Target, Payload) || !Land(*this, F, Payload)) { return false; }
    TestTrue(TEXT("The host's first native hit cuts every other active by 1.5 s, never below now, never the host"),
        ReadyAt(TEXT("active_elbow")) == 8500 && ReadyAt(TEXT("active_zap")) == 1000 && ReadyAt(TEXT("active_snark_bolt")) == 10000);
    FLDCombatHit Derived = Payload;
    Derived.HitId = FGuid::NewGuid();
    Derived.Target = Second;
    if (!Land(*this, F, Derived)) { return false; }
    TestEqual(TEXT("Another hit of the same release cuts nothing more"), ReadyAt(TEXT("active_elbow")), int64(8500));
    FLDCombatHit Other;
    FLDCombatActiveTerms Terms;
    if (!Released(*this, F, Holder, TEXT("active_unplug"), Terms, Other)) { return false; }
    Other.Target = Target;
    if (!Land(*this, F, Other)) { return false; }
    TestTrue(TEXT("A different active's hit is no host hit"), ReadyAt(TEXT("active_elbow")) == 8500
        && ReadyAt(TEXT("active_snark_bolt")) == 10000);
    FLDCombatHit Next;
    if (!SnarkBolt(*this, F, Holder, Target, Next) || !Land(*this, F, Next)) { return false; }
    TestEqual(TEXT("The next release cuts again"), ReadyAt(TEXT("active_elbow")), int64(7000));

    // On the innate dash: its first real contact cuts every learned active, once per dash action.
    if (!InstallComponents(*this, F, Holder, 1, {On(TEXT("component_next_up"), ELDComponentTargetKind::Dash)})) { return false; }
    FString Error;
    const FLDCombatSourceRef Dash = SourceOf(ELDCombatSourceKind::Innate, TEXT("innate_dash"));
    const FGuid DashId = FGuid::NewGuid();
    if (!TestTrue(FString::Printf(TEXT("Dash contact: %s"), *Error), FAccess::HasteOther(*F.Driver, F.Body(Holder), Dash, DashId, 1000, Error)
        && FAccess::HasteOther(*F.Driver, F.Body(Holder), Dash, DashId, 1000, Error))) { return false; }
    TestTrue(TEXT("A hosted dash contact cuts every learned active by 1.5 s, once per dash"), ReadyAt(TEXT("active_elbow")) == 5500
        && ReadyAt(TEXT("active_snark_bolt")) == 8500);
    if (!SnarkBolt(*this, F, Holder, Target, Payload) || !Land(*this, F, Payload)) { return false; }
    TestTrue(TEXT("Installed on the dash it no longer hosts S06"), ReadyAt(TEXT("active_elbow")) == 5500
        && ReadyAt(TEXT("active_snark_bolt")) == 8500);
    TestFalse(TEXT("C13 does not install on the non-damaging bail-out"), ProjectComponents(*F.Catalog, 1,
        {On(TEXT("component_next_up"), ELDComponentTargetKind::Active, TEXT("active_bail_out"))}).IsSuccess());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentHitHasteOtherDashContactTest, "Lockdown.Rules.Combat.Components.HitHasteOtherDashContact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentHitHasteOtherDashContactTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef Holder=F.AddBody(), Target=F.AddBody();
    if (!InstallComponents(*this,F,Holder,1,{On(TEXT("component_next_up"),ELDComponentTargetKind::Dash)})) { return false; }
    F.Place(Holder,0,0);
    F.Place(Target,120,0);
    FLDBodyCombatState& Body=F.Body(Holder);
    FLDCombatEntityRecord& Record=FAccess::Records(*F.Driver).FindChecked(Holder.Victim.EntityId);
    Record.Projection=Body.AttributeProjection;
    FLDCombatTrack& DashTrack=Record.Tracks.AddDefaulted_GetRef();
    DashTrack.IssuedCooldownId=FGuid::NewGuid();
    DashTrack.Kind=ELDCombatSourceKind::Innate;
    DashTrack.DefinitionId=LDCombatSourceRules::InnateDashId();
    FLDCombatTrack& ActiveTrack=TrackOf(F,Holder,TEXT("active_elbow"),10000);

    FLDCombatAction Action;
    Action.Kind=ELDCombatActionKind::Ability;
    Action.Entry=ELDCombatEntry::Dash;
    Action.Source.Kind=ELDCombatSourceKind::Innate;
    Action.Source.DefinitionId=LDCombatSourceRules::InnateDashId();
    Action.Direction=FVector(1,0,0);
    ELDCombatReject Reject=ELDCombatReject::None;
    FString Error;
    if (!FAccess::ReleaseAbility(*F.Driver,F.Actor(Holder),Body,Action,Reject,Error) || Reject!=ELDCombatReject::None)
    {
        AddError(TEXT("The real innate dash release failed: ")+Error);
        return false;
    }
    TestTrue(TEXT("C13 alone registers the innate dash's real contact window"),Body.DashContact.IsValid()
        && !Body.DashContact->bCanDamage && !Body.DashContact->bCanRefund && !Body.DashContact->bCanKnock);
    if (!Body.DashContact.IsValid()) { return false; }

    const double StartSec=double(Body.DashContact->StartMs)/1000.;
    const double EndSec=double(Body.DashContact->EndMs)/1000.;
    const FVector StartFoot=F.Foot(Holder), EndFoot=StartFoot+FVector(350,0,0), TargetFoot=F.Foot(Target);
    auto SourceBatch=MakeShared<FLDNativeMotionBatch>();
    SourceBatch->Body=Holder;
    SourceBatch->Step.StartServerSeconds=StartSec;
    SourceBatch->Step.EndServerSeconds=EndSec;
    SourceBatch->StartFootCm=StartFoot;
    SourceBatch->EndFootCm=EndFoot;
    FLDNativeMotionRecord& Movement=SourceBatch->Records.AddDefaulted_GetRef();
    Movement.Kind=ELDNativeMotionRecordKind::Movement;
    Movement.FromFootCm=StartFoot;
    Movement.ToFootCm=EndFoot;
    Movement.StartServerSeconds=StartSec;
    Movement.EndServerSeconds=EndSec;

    auto TargetBatch=MakeShared<FLDNativeMotionBatch>();
    TargetBatch->Body=Target;
    TargetBatch->Step=SourceBatch->Step;
    TargetBatch->StartFootCm=TargetFoot;
    TargetBatch->EndFootCm=TargetFoot;
    FAccess::CaptureSkillMotion(*F.Driver,SourceBatch);
    FAccess::CaptureSkillMotion(*F.Driver,TargetBatch);
    F.Now(Body.DashContact->EndMs);
    if (!FAccess::ResolveDashContacts(*F.Driver,F.Mode->GetAuthorityNowMs(),Error))
    {
        AddError(TEXT("Resolving the real dash-contact path failed: ")+Error);
        return false;
    }
    TestTrue(TEXT("The actual enemy contact cuts another learned active by 1.5 s"),ActiveTrack.ReadyAtMs==8500
        && Body.DashContact->Struck.Contains(Target));
    {
        FFixture Combo;
        if (!Combo.Init(*this, true)) { return false; }
        const FLDCombatBodyRef Runner = Combo.AddBody(), Victim = Combo.AddBody();
        Combo.Place(Runner, 0, 0);
        Combo.Place(Victim, 120, 0);
        if (!InstallComponents(*this, Combo, Runner, 10, {{TEXT("component_dash_bump")}, {TEXT("component_dash_punch")},
            On(TEXT("component_bump_again"), ELDComponentTargetKind::Dash),
            On(TEXT("component_roll_bump"), ELDComponentTargetKind::Dash)})) { return false; }
        FLDBodyCombatState& DashBody = Combo.Body(Runner);
        FLDCombatEntityRecord& DashRecord = FAccess::Records(*Combo.Driver).FindChecked(Runner.Victim.EntityId);
        DashRecord.Projection = DashBody.AttributeProjection;
        FLDCombatTrack& Track = DashRecord.Tracks.AddDefaulted_GetRef();
        Track.IssuedCooldownId = FGuid::NewGuid();
        Track.Kind = ELDCombatSourceKind::Innate;
        Track.DefinitionId = LDCombatSourceRules::InnateDashId();
        FLDCombatAction DashAction;
        DashAction.Kind = ELDCombatActionKind::Ability;
        DashAction.Entry = ELDCombatEntry::Dash;
        DashAction.Source.Kind = ELDCombatSourceKind::Innate;
        DashAction.Source.DefinitionId = LDCombatSourceRules::InnateDashId();
        DashAction.Direction = FVector(1, 0, 0);
        ELDCombatReject DashReject = ELDCombatReject::None;
        FString DashError;
        if (!FAccess::ReleaseAbility(*Combo.Driver, Combo.Actor(Runner), DashBody, DashAction, DashReject, DashError)
            || DashReject != ELDCombatReject::None || !DashBody.DashContact.IsValid())
        {
            AddError(TEXT("The combined dash release failed: ") + DashError);
            return false;
        }
        TestTrue(TEXT("A single dash enables contact damage, refund and knockback together"),
            DashBody.DashContact->bCanDamage && DashBody.DashContact->bCanRefund && DashBody.DashContact->bCanKnock);
        const int64 ReadyBefore = Track.ReadyAtMs;
        const double Start = double(DashBody.DashContact->StartMs) / 1000.;
        const double End = double(DashBody.DashContact->EndMs) / 1000.;
        const FVector From = Combo.Foot(Runner), To = From + FVector(350, 0, 0), At = Combo.Foot(Victim);
        auto Motion = MakeShared<FLDNativeMotionBatch>();
        Motion->Body = Runner;
        Motion->Step.StartServerSeconds = Start;
        Motion->Step.EndServerSeconds = End;
        Motion->StartFootCm = From;
        Motion->EndFootCm = To;
        FLDNativeMotionRecord& Move = Motion->Records.AddDefaulted_GetRef();
        Move.Kind = ELDNativeMotionRecordKind::Movement;
        Move.FromFootCm = From;
        Move.ToFootCm = To;
        Move.StartServerSeconds = Start;
        Move.EndServerSeconds = End;
        auto Still = MakeShared<FLDNativeMotionBatch>();
        Still->Body = Victim;
        Still->Step = Motion->Step;
        Still->StartFootCm = Still->EndFootCm = At;
        FAccess::CaptureSkillMotion(*Combo.Driver, Motion);
        FAccess::CaptureSkillMotion(*Combo.Driver, Still);
        Combo.Now(DashBody.DashContact->EndMs);
        if (!TestTrue(TEXT("The combined dash contact resolves: ") + DashError,
            FAccess::ResolveDashContacts(*Combo.Driver, Combo.Mode->GetAuthorityNowMs(), DashError))) { return false; }
        const auto& Hits = FAccess::PendingHits(*Combo.Driver);
        if (!TestTrue(TEXT("One contact produces one added damage and one knockback, not native skill hits"), Hits.Num() == 2
            && Hits[0].Source.DefinitionId == FName(TEXT("component_dash_bump")) && Hits[0].bAddedDamage
            && !IsNativeDirectHit(Hits[0]) && Hits[1].Source.DefinitionId == FName(TEXT("component_roll_bump"))
            && Hits[1].KnockbackCm > 0 && !IsNativeDirectHit(Hits[1]))) { return false; }
        const int64 Refunded = Track.ReadyAtMs;
        TestTrue(TEXT("The contact refunds the dash cooldown once"),
            Refunded == FMath::Max<int64>(Combo.Mode->GetAuthorityNowMs(), ReadyBefore - 1500));
        FAccess::CaptureSkillMotion(*Combo.Driver, Motion);
        FAccess::CaptureSkillMotion(*Combo.Driver, Still);
        TestTrue(TEXT("Replaying the same contact adds no hits or refund"),
            FAccess::ResolveDashContacts(*Combo.Driver, Combo.Mode->GetAuthorityNowMs(), DashError)
            && Hits.Num() == 2 && Track.ReadyAtMs == Refunded);
        Combo.Actor(Runner).SetActorLocation(Combo.Actor(Runner).GetActorLocation() + FVector(350, 0, 0));
        TestTrue(TEXT("The same completed dash arms exactly one empowered basic"),
            FAccess::ResolveDashCompletions(*Combo.Driver, Combo.Mode->GetAuthorityNowMs(), DashError)
            && DashBody.Empowers.Num() == 1
            && DashBody.Empowers[0].Source.DefinitionId == FName(TEXT("component_dash_punch"))
            && DashBody.Empowers[0].Charges == 1);
        TestTrue(TEXT("The completed dash cannot arm the basic twice"),
            FAccess::ResolveDashCompletions(*Combo.Driver, Combo.Mode->GetAuthorityNowMs(), DashError)
            && DashBody.Empowers.Num() == 1);
        if (!Drain(*this, Combo)) { return false; }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentSetupMarkTest, "Lockdown.Rules.Combat.Components.SetupMark",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentSetupMarkTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    constexpr int64 HP = 10000000;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(), Target = F.AddBody(ELDVictimKind::Contender, HP);
    if (!InstallComponents(*this, F, Holder, 1,
        {On(TEXT("component_setup_line"), ELDComponentTargetKind::Active, TEXT("active_snark_bolt"))})
        || !InstallComponents(*this, F, Target, 1, {})) { return false; }
    FLDBodyCombatState& Body = F.Body(Holder);
    F.Now(1000);
    const auto Loss = [&](const TCHAR* Active, int64& Out)
    {
        FLDCombatActiveTerms Terms;
        FLDCombatHit Payload;
        F.Body(Target).Resources.HpP2 = HP;
        if (!Released(*this, F, Holder, Active, Terms, Payload)) { return false; }
        Payload.Target = Target;
        if (!Land(*this, F, Payload)) { return false; }
        Out = HP - F.Body(Target).Resources.HpP2;
        return true;
    };
    int64 ZapBase = 0, BoltFirst = 0, BoltMarked = 0, ZapMarked = 0, ZapAfter = 0;
    if (!Loss(TEXT("active_unplug"), ZapBase)) { return false; }
    TestTrue(TEXT("Another active's hit sets no mark"), Body.SetupMarks.IsEmpty());
    if (!Loss(TEXT("active_snark_bolt"), BoltFirst)) { return false; }
    TestTrue(TEXT("The host's hit marks the struck life for 4 s"), Body.SetupMarks.Num() == 1 && Body.SetupMarks[0].Target == Target
        && Body.SetupMarks[0].ExpiresAtMs == 5000);
    F.Now(2000);
    if (!Loss(TEXT("active_snark_bolt"), BoltMarked)) { return false; }
    TestTrue(TEXT("The host's own hit neither takes nor consumes the mark; it refreshes the one mark"), BoltMarked == BoltFirst
        && Body.SetupMarks.Num() == 1 && Body.SetupMarks[0].ExpiresAtMs == 6000);
    if (!Loss(TEXT("active_unplug"), ZapMarked) || !Loss(TEXT("active_unplug"), ZapAfter)) { return false; }
    TestTrue(TEXT("A different active's next native hit takes +25% once and consumes the mark"), ZapBase > 0
        && FMath::Abs(ZapMarked * 10000 - ZapBase * 12500) <= 20000 && ZapAfter == ZapBase && Body.SetupMarks.IsEmpty());
    int64 Ignored = 0, ZapLate = 0;
    if (!Loss(TEXT("active_snark_bolt"), Ignored)) { return false; }
    F.Now(6000);
    if (!Loss(TEXT("active_unplug"), ZapLate)) { return false; }
    TestEqual(TEXT("An expired mark gives nothing"), ZapLate, ZapBase);
    if (!Loss(TEXT("active_snark_bolt"), Ignored)) { return false; }
    if (!Basic(*this, F, Holder, Target) || !Drain(*this, F)) { return false; }
    TestEqual(TEXT("A basic neither takes nor consumes the mark"), Body.SetupMarks.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentReactionHasteTest, "Lockdown.Rules.Combat.Components.ReactionHaste",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentReactionHasteTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(), Other = F.AddBody(), Target = F.AddBody(ELDVictimKind::Contender, 10000000);
    F.Place(Holder, -600, -600);
    F.Place(Other, 600, -600);
    F.Place(Target, 0, 600);
    if (!InstallComponents(*this, F, Holder, 1,
        {On(TEXT("component_blast_reload"), ELDComponentTargetKind::Active, TEXT("active_zap"))})) { return false; }
    F.Now(1000);
    TrackOf(F, Holder, TEXT("active_zap"), 11000);
    TrackOf(F, Holder, TEXT("active_elbow"), 11000);
    const auto ReadyAt = [&F, &Holder](const TCHAR* Active)
    {
        const FLDCombatTrack* Track = FAccess::Records(*F.Driver).FindChecked(Holder.Victim.EntityId).Tracks.FindByPredicate(
            [Active](const FLDCombatTrack& Row) { return Row.DefinitionId == FName(Active); });
        return Track ? Track->ReadyAtMs : int64(-1);
    };
    FString Error;
    const auto Wet = [&]()
    {
        FLDCombatHit Water = FFixture::Hit(Other, Target, 0);
        Water.bApplyWet = true;
        return TestTrue(FString::Printf(TEXT("Wet: %s"), *Error), F.Apply(Water, Error));
    };
    FLDCombatActiveTerms Terms;
    FLDCombatHit Payload;
    if (!Released(*this, F, Holder, TEXT("active_zap"), Terms, Payload)) { return false; }
    Payload.Target = Target;
    TestTrue(TEXT("S10's payload electrifies"), Payload.bApplyElectric);
    if (!Wet() || !Land(*this, F, Payload)) { return false; }
    TestTrue(TEXT("S10 triggers the reaction"), FAccess::Reactions(*F.Driver).Num() == 1
        && FAccess::Reactions(*F.Driver)[0].TriggerEntity == Holder.Victim.EntityId);
    TestTrue(TEXT("The host-triggered reaction cuts the host by 3 s and nothing else"), ReadyAt(TEXT("active_zap")) == 8000
        && ReadyAt(TEXT("active_elbow")) == 11000);
    if (!F.Flush(Error)) { return false; }
    FLDCombatHit Again = Payload;
    Again.HitId = FGuid::NewGuid();
    if (!Wet() || !Land(*this, F, Again)) { return false; }
    TestEqual(TEXT("A second reaction of the same release cuts nothing more"), ReadyAt(TEXT("active_zap")), int64(8000));
    if (!F.Flush(Error)) { return false; }
    FLDCombatHit Foreign = FFixture::Hit(Holder, Target, 0);
    Foreign.Source = SourceOf(ELDCombatSourceKind::Learned, TEXT("active_elbow"));
    Foreign.bApplyElectric = true;
    const int32 Before = FAccess::Reactions(*F.Driver).Num();
    if (!Wet() || !TestTrue(FString::Printf(TEXT("Another trigger: %s"), *Error), F.Apply(Foreign, Error))) { return false; }
    TestTrue(TEXT("A reaction another source triggered never cuts the host"), FAccess::Reactions(*F.Driver).Num() > Before
        && ReadyAt(TEXT("active_zap")) == 8000 && ReadyAt(TEXT("active_elbow")) == 11000);
    if (!F.Flush(Error)) { return false; }
    FLDCombatHit Lone;
    if (!Released(*this, F, Holder, TEXT("active_zap"), Terms, Lone)) { return false; }
    Lone.Target = Target;
    const int32 Prior = FAccess::Reactions(*F.Driver).Num();
    if (!Land(*this, F, Lone)) { return false; }
    TestTrue(TEXT("A new release without a reaction cuts nothing"), FAccess::Reactions(*F.Driver).Num() == Prior
        && ReadyAt(TEXT("active_zap")) == 8000);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentMultiHitManaTest, "Lockdown.Rules.Combat.Components.MultiHitMana",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentMultiHitManaTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody();
    TArray<FLDCombatBodyRef> Targets;
    for (int32 Index = 0; Index < 5; ++Index) { Targets.Add(F.AddBody(ELDVictimKind::Contender, 10000000)); }
    if (!InstallComponents(*this, F, Holder, 1,
        {On(TEXT("component_group_refund"), ELDComponentTargetKind::Active, TEXT("active_snark_bolt"))})) { return false; }
    FLDBodyCombatState& Body = F.Body(Holder);
    Body.Resources.MaxMpP2 = 100000;
    Body.Resources.MpP2 = 0;
    FLDCombatHit Payload;
    if (!SnarkBolt(*this, F, Holder, Targets[0], Payload)) { return false; }
    const auto Strike = [&](const FLDCombatHit& Release, int32 Index)
    {
        FLDCombatHit Hit = Release;
        Hit.HitId = FGuid::NewGuid();
        Hit.Target = Targets[Index];
        return Land(*this, F, Hit);
    };
    if (!Strike(Payload, 0) || !Strike(Payload, 0)) { return false; }
    TestEqual(TEXT("One enemy, struck twice by the release, gives nothing"), Body.Resources.MpP2, int64(0));
    if (!Strike(Payload, 1) || !Strike(Payload, 1)) { return false; }
    TestEqual(TEXT("A second distinct enemy gives 8 MP once, however often a derived hit strikes it"), Body.Resources.MpP2, int64(800));
    if (!Strike(Payload, 2) || !Strike(Payload, 3) || !Strike(Payload, 4)) { return false; }
    TestEqual(TEXT("At most 24 MP per release"), Body.Resources.MpP2, int64(2400));
    FLDCombatHit Next;
    if (!SnarkBolt(*this, F, Holder, Targets[0], Next) || !Strike(Next, 0)) { return false; }
    TestEqual(TEXT("A single-target release gives nothing"), Body.Resources.MpP2, int64(2400));
    if (!Strike(Next, 1)) { return false; }
    TestEqual(TEXT("Each release counts its own enemies"), Body.Resources.MpP2, int64(3200));
    return true;
}

// ---- Mods M16-M20 (docs/systems/COMBAT_AND_GROWTH.md): real releases of a fixture contender's learned active.
namespace LDBuildPrimitivesTests
{
// One real ReleaseAbility of Active on Q, with the body's installed projection as its record's build.
bool ReleaseOnQ(FAutomationTestBase& Test, FFixture& F, const FLDCombatBodyRef& Ref, const TCHAR* Active)
{
    FLDBodyCombatState& Body = F.Body(Ref);
    FLDCombatEntityRecord& Record = FAccess::Records(*F.Driver).FindChecked(Ref.Victim.EntityId);
    Record.Projection = Body.AttributeProjection;
    Record.Build.Actives.Reset();
    Record.Build.Actives.Add({FName(Active), ELDCombatEntry::Q});
    TrackOf(F, Ref, Active, 0);
    Body.Resources.MaxMpP2 = Body.Resources.MpP2 = 100000;
    FLDCombatAction Action;
    Action.Kind = ELDCombatActionKind::Ability;
    Action.Entry = ELDCombatEntry::Q;
    Action.Source = SourceOf(ELDCombatSourceKind::Learned, Active);
    Action.Direction = FVector(1, 0, 0);
    ELDCombatReject Reject = ELDCombatReject::None;
    FString Error;
    if (!FAccess::ReleaseAbility(*F.Driver, F.Actor(Ref), Body, Action, Reject, Error) || Reject != ELDCombatReject::None)
    {
        Test.AddError(FString::Printf(TEXT("Release of %s: reject %d: %s"), Active, int32(Reject), *Error));
        return false;
    }
    return true;
}

// The planar displacement of the body's running forced move (its dash root motion), if any.
bool PushOf(FFixture& F, const FLDCombatBodyRef& Ref, FVector& Out)
{
    auto* Movement = Cast<ULDCharacterMovementComponent>(F.Actor(Ref).GetCharacterMovement());
    const TSharedPtr<FRootMotionSource> Source = Movement ? Movement->GetRootMotionSource(FName(TEXT("LDDemoDash"))) : nullptr;
    if (!Source.IsValid()) { return false; }
    const FRootMotionSource_ConstantForce& Force = static_cast<const FRootMotionSource_ConstantForce&>(*Source);
    Out = Force.Force * double(Force.Duration);
    return true;
}

double CmOf(const FLDDemoRational& Value) { return Value.Denominator > 0 ? double(Value.Numerator) / double(Value.Denominator) : -1.; }

const FLDCombatHit* PendingOn(FFixture& F, const FLDCombatBodyRef& Target)
{
    return FAccess::PendingHits(*F.Driver).FindByPredicate([&Target](const FLDCombatHit& Row) { return Row.Target == Target; });
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentZoneTetherTest, "Lockdown.Rules.Combat.Components.ZoneTether",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentZoneTetherTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody();
    F.Place(Holder, 0, 0);
    if (!InstallComponents(*this, F, Holder, 1,
        {On(TEXT("component_zone_tether"), ELDComponentTargetKind::Active, TEXT("active_quick_meeting"))})) { return false; }
    FLDCombatActiveTerms Plain, Tethered;
    FLDCombatHit Payload;
    if (!ActiveTerms(*this, F, TEXT("active_quick_meeting"), Plain)
        || !Released(*this, F, Holder, TEXT("active_quick_meeting"), Tethered, Payload)) { return false; }
    const FLDDemoEffectDefinition* PlainZone = FindEffect(Plain.Values, ELDDemoEffectOp::Zone);
    const FLDDemoEffectDefinition* Zone = FindEffect(Tethered.Values, ELDDemoEffectOp::Zone);
    if (!TestTrue(TEXT("M16 scales S08's zone radius by 0.8"), PlainZone && Zone && SameRational(PlainZone->RadiusCm, 240)
        && SameRational(Zone->RadiusCm, 192) && Tethered.ZoneTether.IsSet())) { return false; }
    TArray<FLDCombatZone>& Zones = FAccess::Zones(*F.Driver);
    const FLDCombatSourceRef Source = SourceOf(ELDCombatSourceKind::Learned, TEXT("active_quick_meeting"));
    const FVector Start = F.Foot(Holder), Point = Start + FVector(300, 0, 0);
    FString Error;
    if (!TestTrue(FString::Printf(TEXT("Tethered zone: %s"), *Error),
        FAccess::ReleaseTerms(*F.Driver, F.Body(Holder), Source, Tethered, Start, Point, Error))
        || !TestTrue(FString::Printf(TEXT("Plain zone: %s"), *Error),
        FAccess::ReleaseTerms(*F.Driver, F.Body(Holder), Source, Plain, Start, Point, Error))) { return false; }
    if (!TestEqual(TEXT("Both zones are placed"), Zones.Num(), 2)) { return false; }
    TestTrue(TEXT("The tethered zone starts on the caster's foot; a plain zone sits at its point"), Zones[0].bTether
        && FVector::Dist2D(Zones[0].CentreCm, Start) < 1 && FMath::IsNearlyEqual(Zones[0].RadiusCm, 192.)
        && !Zones[1].bTether && FVector::Dist2D(Zones[1].CentreCm, Point) < 1);
    F.Place(Holder, 300, 200);
    F.Now(1500);
    if (!TestTrue(FString::Printf(TEXT("Advance: %s"), *Error), FAccess::AdvanceMechanics(*F.Driver, 1500, Error))) { return false; }
    const FVector Moved = F.Foot(Holder);
    TestTrue(TEXT("The tethered centre follows the living caster across advances; the plain one stays"), Zones.Num() == 2
        && FVector::Dist2D(Zones[0].CentreCm, Moved) < 1 && FVector::Dist2D(Zones[1].CentreCm, Point) < 1);
    F.Body(Holder).bPendingDeath = true;
    F.Place(Holder, -300, -200);
    F.Now(1600);
    if (!TestTrue(FString::Printf(TEXT("Advance: %s"), *Error), FAccess::AdvanceMechanics(*F.Driver, 1600, Error))) { return false; }
    TestTrue(TEXT("A dead caster's tethered zone stays where it was"), !Zones.IsEmpty() && Zones[0].bTether
        && FVector::Dist2D(Zones[0].CentreCm, Moved) < 1);
    F.Body(Holder).bPendingDeath = false;
    TestFalse(TEXT("M16 does not install on S01"), ProjectComponents(*F.Catalog, 1,
        {On(TEXT("component_zone_tether"), ELDComponentTargetKind::Active, TEXT("active_elbow"))}).IsSuccess());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentDefenseShoveTest, "Lockdown.Rules.Combat.Components.DefenseShove",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentDefenseShoveTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    constexpr int64 HP = 10000000;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(ELDVictimKind::Contender, HP), Plain = F.AddBody(ELDVictimKind::Contender, HP),
        Attacker = F.AddBody(ELDVictimKind::Contender, HP), Far = F.AddBody(ELDVictimKind::Contender, HP);
    F.Place(Holder, 0, 0);
    F.Place(Attacker, 150, 0);
    F.Place(Far, 400, 0);
    F.Place(Plain, -500, 500);
    if (!InstallComponents(*this, F, Holder, 1,
        {On(TEXT("component_shove_off"), ELDComponentTargetKind::Active, TEXT("active_not_mad"))})
        || !InstallComponents(*this, F, Plain, 1, {})
        || !ReleaseOnQ(*this, F, Holder, TEXT("active_not_mad")) || !ReleaseOnQ(*this, F, Plain, TEXT("active_not_mad"))) { return false; }
    FLDBodyCombatState& Body = F.Body(Holder);
    const int64 Shield = Body.Resources.ShieldP2, PlainShield = F.Body(Plain).Resources.ShieldP2;
    TestTrue(TEXT("M17 makes S14's shield 0.85 times as strong"), PlainShield > 0
        && FMath::Abs(Shield * 10000 - PlainShield * 8500) <= 30000);
    TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    Pending.Reset();
    FString Error;
    if (!TestTrue(FString::Printf(TEXT("Break: %s"), *Error), F.Apply(FFixture::Hit(Attacker, Holder, Shield * 4), Error))) { return false; }
    const FLDCombatHit* Shove = PendingOn(F, Attacker);
    TestTrue(TEXT("An enemy breaking the host shield shoves the enemies within 250 cm away from the holder"), Body.Resources.ShieldP2 == 0
        && Shove && Shove->KnockbackCm == 220 && Shove->KnockbackMs == 200 && Shove->bKnockbackFromAnchor && !Shove->bKnockbackPull
        && FVector::Dist2D(Shove->KnockbackAnchorCm, F.Foot(Holder)) < 1
        && Shove->KnockbackSource == SourceOf(ELDCombatSourceKind::Learned, TEXT("active_not_mad")) && !PendingOn(F, Far));
    if (!F.Flush(Error)) { AddError(TEXT("Shove: ") + Error); return false; }
    FVector Push;
    TestTrue(TEXT("The shove is a real knockback away from the holder"), F.Body(Attacker).Knockbacks.Num() == 1 && PushOf(F, Attacker, Push)
        && Push.X > 200 && FMath::Abs(Push.Y) < 1);

    // An expired shield shoves nothing.
    F.Now(2000);
    if (!ReleaseOnQ(*this, F, Holder, TEXT("active_not_mad"))) { return false; }
    TestTrue(TEXT("The recast grants a shield"), Body.Resources.ShieldP2 > 0);
    F.Now(6000);
    FAccess::ExpireStatuses(*F.Driver, 6000);
    Pending.Reset();
    if (!TestTrue(FString::Printf(TEXT("Late hit: %s"), *Error), F.Apply(FFixture::Hit(Far, Holder, 10000), Error))) { return false; }
    TestTrue(TEXT("After the shield expired no hit shoves"), Body.Resources.ShieldP2 == 0
        && !Pending.ContainsByPredicate([](const FLDCombatHit& Row) { return Row.KnockbackCm > 0; }));

    // S16: the answer of a recorded amount shoves what it reaches.
    const FLDCombatBodyRef Bracer = F.AddBody(ELDVictimKind::Contender, HP), Near = F.AddBody(ELDVictimKind::Contender, HP);
    F.Place(Bracer, -500, -500);
    F.Place(Near, -350, -500);
    F.Now(7000);
    if (!InstallComponents(*this, F, Bracer, 1,
        {On(TEXT("component_shove_off"), ELDComponentTargetKind::Active, TEXT("active_brace"))})
        || !ReleaseOnQ(*this, F, Bracer, TEXT("active_brace"))) { return false; }
    Pending.Reset();
    if (!TestTrue(FString::Printf(TEXT("Braced hit: %s"), *Error), F.Apply(FFixture::Hit(Near, Bracer, 100000), Error))) { return false; }
    const int64 EndsAt = F.Body(Bracer).Brace.EndsAtMs;
    TestTrue(TEXT("S16 records the hostile hit"), F.Body(Bracer).Brace.RecordedP2 > 0 && EndsAt > 7000);
    Pending.Reset();
    F.Now(EndsAt);
    if (!TestTrue(FString::Printf(TEXT("Answer: %s"), *Error), FAccess::AdvanceElements(*F.Driver, EndsAt, Error))) { return false; }
    const FLDCombatHit* Answer = PendingOn(F, Near);
    TestTrue(TEXT("S16's answer with M17 also pushes away from the holder"), Answer && Answer->BaseP2 > 0 && Answer->KnockbackCm == 220
        && Answer->KnockbackMs == 200 && Answer->bKnockbackFromAnchor && FVector::Dist2D(Answer->KnockbackAnchorCm, F.Foot(Bracer)) < 1
        && Answer->KnockbackSource == SourceOf(ELDCombatSourceKind::Learned, TEXT("active_brace")));
    Pending.Reset();
    TestFalse(TEXT("M17 does not install on S01"), ProjectComponents(*F.Catalog, 1,
        {On(TEXT("component_shove_off"), ELDComponentTargetKind::Active, TEXT("active_elbow"))}).IsSuccess());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentFinalKnockbackTest, "Lockdown.Rules.Combat.Components.FinalKnockback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentFinalKnockbackTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(), Target = F.AddBody(ELDVictimKind::Contender, 10000000);
    F.Place(Holder, 0, 0);
    F.Place(Target, 200, 0);
    if (!InstallComponents(*this, F, Holder, 1,
        {On(TEXT("component_finisher"), ELDComponentTargetKind::Active, TEXT("active_triple_combo"))})) { return false; }
    FLDCombatActiveTerms Plain;
    if (!ActiveTerms(*this, F, TEXT("active_triple_combo"), Plain)) { return false; }
    const FLDDemoEffectDefinition* Package = FindEffect(Plain.Values, ELDDemoEffectOp::EmpowerBasic);
    const FLDDemoStatBonus* PlainSpeed = Package ? Package->Stats.FindByPredicate([](const FLDDemoStatBonus& Row)
    { return Row.Stat == ELDDemoBonusStat::AttackSpeedBonusRatio; }) : nullptr;
    if (!TestTrue(TEXT("S17 grants attack speed"), PlainSpeed && PlainSpeed->Amount > 0)
        || !ReleaseOnQ(*this, F, Holder, TEXT("active_triple_combo"))) { return false; }
    FLDBodyCombatState& Body = F.Body(Holder);
    const FLDCombatTimedBonus* Bonus = Body.TimedBonuses.FindByPredicate([](const FLDCombatTimedBonus& Row)
    { return Row.SourceDefinition == FName(TEXT("active_triple_combo")); });
    const FLDDemoStatBonus* Speed = Bonus ? Bonus->Stats.FindByPredicate([](const FLDDemoStatBonus& Row)
    { return Row.Stat == ELDDemoBonusStat::AttackSpeedBonusRatio; }) : nullptr;
    TestTrue(TEXT("M18 halves S17's attack-speed bonus"), Speed && Speed->Amount * 2 == PlainSpeed->Amount);
    TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    const FLDCombatSourceRef Host = SourceOf(ELDCombatSourceKind::Learned, TEXT("active_triple_combo"));
    for (int32 Index = 1; Index <= 3; ++Index)
    {
        Pending.Reset();
        if (!Basic(*this, F, Holder, Target)) { return false; }
        const FLDCombatHit* Hit = PendingOn(F, Target);
        if (Index < 3)
        {
            TestTrue(FString::Printf(TEXT("Empowered basic %d does not knock"), Index), Hit && Hit->KnockbackCm == 0);
            continue;
        }
        TestTrue(TEXT("The last package pushes the target 250 cm away from the holder"), Hit && Hit->KnockbackCm == 250
            && Hit->KnockbackMs == 200 && !Hit->bKnockbackFromAnchor && !Hit->bKnockbackPull && Hit->KnockbackSource == Host
            && Body.Empowers.IsEmpty());
    }
    Pending.Reset();
    if (!Basic(*this, F, Holder, Target)) { return false; }
    TestTrue(TEXT("A plain basic after the packages does not knock"), PendingOn(F, Target) && PendingOn(F, Target)->KnockbackCm == 0);
    Pending.Reset();

    // S22: only the final wave knocks, away from the caster, without the final slow.
    if (!InstallComponents(*this, F, Holder, 1,
        {On(TEXT("component_finisher"), ELDComponentTargetKind::Active, TEXT("active_open_mic"))})) { return false; }
    FLDCombatActiveTerms Terms;
    FLDCombatHit Payload;
    if (!Released(*this, F, Holder, TEXT("active_open_mic"), Terms, Payload)) { return false; }
    F.Now(2000);
    FLDCombatAction Action;
    Action.Kind = ELDCombatActionKind::Ability;
    Action.Direction = FVector(1, 0, 0);
    bool bHandled = false;
    FString Error;
    if (!TestTrue(FString::Printf(TEXT("S22 channels: %s"), *Error), FAccess::DeliverTerms(*F.Driver, Body, Terms, Action, Payload, 2000,
        bHandled, Error) && bHandled && FAccess::Channels(*F.Driver).Num() == 1)) { return false; }
    for (int64 At : {int64(2000), int64(2300), int64(2600)})
    {
        Pending.Reset();
        F.Now(At);
        if (!TestTrue(FString::Printf(TEXT("Wave at %lld: %s"), At, *Error), FAccess::AdvanceSkillContent(*F.Driver, At, Error))) { return false; }
        const FLDCombatHit* Wave = PendingOn(F, Target);
        if (At < 2600)
        {
            TestTrue(FString::Printf(TEXT("Wave at %lld hits without a knockback"), At), Wave && Wave->KnockbackCm == 0);
            continue;
        }
        TestTrue(TEXT("The final wave pushes 250 cm away from the caster and does not slow"), Wave && Wave->KnockbackCm == 250
            && Wave->KnockbackMs == 200 && Wave->bKnockbackFromAnchor && FVector::Dist2D(Wave->KnockbackAnchorCm, F.Foot(Holder)) < 1
            && Wave->SlowBp == 0 && Wave->KnockbackSource == SourceOf(ELDCombatSourceKind::Learned, TEXT("active_open_mic"))
            && FAccess::Channels(*F.Driver).IsEmpty());
    }
    Pending.Reset();
    TestFalse(TEXT("M18 does not install on S01"), ProjectComponents(*F.Catalog, 1,
        {On(TEXT("component_finisher"), ELDComponentTargetKind::Active, TEXT("active_elbow"))}).IsSuccess());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentEmpowerReachTest, "Lockdown.Rules.Combat.Components.EmpowerReach",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentEmpowerReachTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(), Target = F.AddBody(ELDVictimKind::Contender, 10000000);
    F.Place(Holder, 0, 0);
    F.Place(Target, 150, 0);
    if (!InstallComponents(*this, F, Holder, 1,
        {On(TEXT("component_long_handle"), ELDComponentTargetKind::Active, TEXT("active_triple_combo"))})) { return false; }
    FLDBodyCombatState& Body = F.Body(Holder);
    FLDCombatActiveTerms Plain;
    if (!ActiveTerms(*this, F, TEXT("active_triple_combo"), Plain)) { return false; }
    const FLDDemoEffectDefinition* Effect = FindEffect(Plain.Values, ELDDemoEffectOp::EmpowerBasic);
    const FLDCombatSourceRef Host = SourceOf(ELDCombatSourceKind::Learned, TEXT("active_triple_combo"));
    FLDCombatEmpower PlainPackage;
    FString Error;
    if (!TestTrue(FString::Printf(TEXT("Plain package: %s"), *Error), Effect
        && FAccess::MakeEmpower(*F.Driver, Body, Host, *Effect, 1000, PlainPackage, Error) && PlainPackage.AmountP2 > 0)) { return false; }
    const double RangeBefore = CmOf(Body.AttributeProjection.Final.AttackRangeCm);
    if (!ReleaseOnQ(*this, F, Holder, TEXT("active_triple_combo"))) { return false; }
    const FLDCombatEmpower* Armed = Body.Empowers.FindByPredicate([&Host](const FLDCombatEmpower& Row) { return Row.Source == Host; });
    const int64 Weaker = PlainPackage.AmountP2 / 10000 * 7000 + PlainPackage.AmountP2 % 10000 * 7000 / 10000;
    if (!TestTrue(TEXT("M19's package is 0.7 times the plain one"), Armed && Armed->AmountP2 == Weaker && Armed->Charges == 3)) { return false; }
    const FGuid PackageId = Armed->EffectId;
    TestTrue(TEXT("The basic range is 250 cm longer while the package is pending"),
        FMath::IsNearlyEqual(CmOf(Body.AttributeProjection.Final.AttackRangeCm), RangeBefore + 250., 0.01));
    TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    for (int32 Index = 1; Index <= 3; ++Index)
    {
        Pending.Reset();
        if (!Basic(*this, F, Holder, Target)) { return false; }
        if (Index < 3)
        {
            TestTrue(FString::Printf(TEXT("The reach holds after basic %d"), Index),
                FMath::IsNearlyEqual(CmOf(Body.AttributeProjection.Final.AttackRangeCm), RangeBefore + 250., 0.01));
        }
    }
    Pending.Reset();
    TestTrue(TEXT("The last package ends the reach with its charge bonus"), Body.Empowers.IsEmpty()
        && FMath::IsNearlyEqual(CmOf(Body.AttributeProjection.Final.AttackRangeCm), RangeBefore, 0.01)
        && !Body.TimedBonuses.ContainsByPredicate([&PackageId](const FLDCombatTimedBonus& Row) { return Row.EffectId == PackageId; }));
    TestFalse(TEXT("M19 does not install on S01"), ProjectComponents(*F.Catalog, 1,
        {On(TEXT("component_long_handle"), ELDComponentTargetKind::Active, TEXT("active_elbow"))}).IsSuccess());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentPullTest, "Lockdown.Rules.Combat.Components.Pull",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentPullTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    constexpr int64 HP = 10000000;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(), Target = F.AddBody(ELDVictimKind::Contender, HP);
    F.Place(Holder, 0, 0);
    F.Place(Target, 200, 0);
    if (!InstallComponents(*this, F, Holder, 1,
        {On(TEXT("component_come_here"), ELDComponentTargetKind::Active, TEXT("active_elbow"))})) { return false; }
    FLDCombatActiveTerms Terms;
    FLDCombatHit Payload;
    if (!Released(*this, F, Holder, TEXT("active_elbow"), Terms, Payload)) { return false; }
    TestTrue(TEXT("M20 turns S01's push into a pull that stops 120 cm short"), Payload.bKnockbackPull && !Payload.bKnockbackFromAnchor
        && Payload.KnockbackCm == 300 && Payload.KnockbackMs == 250 && Payload.KnockbackStopCm == 120);
    Payload.Target = Target;
    if (!Land(*this, F, Payload)) { return false; }
    FVector Push;
    TestTrue(TEXT("The pull moves the target toward the caster and only to 120 cm of it"), F.Body(Target).Knockbacks.Num() == 1
        && PushOf(F, Target, Push) && FMath::IsNearlyEqual(Push.X, -80., 1.) && FMath::Abs(Push.Y) < 1);

    // S15: every body the taunt lands on is pulled toward the caster.
    const FLDCombatBodyRef Taunter = F.AddBody(), Taunted = F.AddBody(ELDVictimKind::Contender, HP),
        Close = F.AddBody(ELDVictimKind::Contender, HP);
    F.Place(Taunter, -500, 400);
    F.Place(Taunted, -250, 400);
    F.Place(Close, -500, 300);
    if (!InstallComponents(*this, F, Taunter, 1,
        {On(TEXT("component_come_here"), ELDComponentTargetKind::Active, TEXT("active_stare_down"))})) { return false; }
    TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    Pending.Reset();
    if (!ReleaseOnQ(*this, F, Taunter, TEXT("active_stare_down"))) { return false; }
    const FLDCombatHit* Pull = PendingOn(F, Taunted);
    TestTrue(TEXT("S15's landed taunt queues a pull of the taunt"), Pull && Pull->bKnockbackPull && Pull->KnockbackCm == 150
        && Pull->KnockbackMs == 150 && Pull->KnockbackStopCm == 120
        && Pull->KnockbackSource == SourceOf(ELDCombatSourceKind::Learned, TEXT("active_stare_down")) && PendingOn(F, Close));
    FString Error;
    if (!F.Flush(Error)) { AddError(TEXT("Pull: ") + Error); return false; }
    TestTrue(TEXT("The taunted body is pulled 130 cm, to 120 cm of the caster"), PushOf(F, Taunted, Push)
        && FMath::IsNearlyEqual(Push.X, -130., 1.) && FMath::Abs(Push.Y) < 1);
    TestTrue(TEXT("A body already within 120 cm is not moved"), F.Body(Close).Knockbacks.IsEmpty() && !PushOf(F, Close, Push));
    TestFalse(TEXT("M20 does not install on S14"), ProjectComponents(*F.Catalog, 1,
        {On(TEXT("component_come_here"), ELDComponentTargetKind::Active, TEXT("active_not_mad"))}).IsSuccess());
    return true;
}

// ---- Connectors C17-C21 (docs/systems/COMBAT_AND_GROWTH.md): the production dash completion, shield, release and
// native-hit entries of a fixture contender's installed build.
namespace LDBuildPrimitivesTests
{
const FLDCombatHit* ComponentHitOn(FFixture& F, const FLDCombatBodyRef& Target, const TCHAR* Component)
{
    return FAccess::PendingHits(*F.Driver).FindByPredicate([&](const FLDCombatHit& Row)
    {
        return Row.Target == Target && Row.Source.Kind == ELDCombatSourceKind::Component && Row.Source.DefinitionId == FName(Component);
    });
}

void StartDash(FFixture& F, const FLDCombatBodyRef& Ref, int64 EndMs)
{
    FLDCombatVoluntaryDash& Dash = F.Body(Ref).VoluntaryDash.Emplace();
    Dash.EffectId = FGuid::NewGuid();
    Dash.Source = SourceOf(ELDCombatSourceKind::Innate, TEXT("innate_dash"));
    Dash.StartFootCm = F.Foot(Ref);
    Dash.EndMs = EndMs;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentZoneRelocateTest, "Lockdown.Rules.Combat.Components.ZoneRelocate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentZoneRelocateTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(), Plain = F.AddBody();
    F.Place(Holder, 0, 0);
    F.Place(Plain, -800, 0);
    if (!InstallComponents(*this, F, Holder, 1, {On(TEXT("component_move_meeting"), ELDComponentTargetKind::Dash)})
        || !InstallComponents(*this, F, Plain, 1, {})) { return false; }
    FLDCombatActiveTerms Zone, Trap;
    FLDCombatHit Payload;
    if (!ActiveTerms(*this, F, TEXT("active_quick_meeting"), Zone)) { return false; }
    TArray<FLDCombatZone>& Zones = FAccess::Zones(*F.Driver);
    TArray<FLDCombatGroundEffect>& Ground = FAccess::Ground(*F.Driver);
    const FLDCombatSourceRef ZoneSource = SourceOf(ELDCombatSourceKind::Learned, TEXT("active_quick_meeting"));
    FString Error;
    const auto PlaceZone = [&](const FLDCombatBodyRef& Caster, const FVector& Point)
    {
        return TestTrue(FString::Printf(TEXT("Zone: %s"), *Error),
            FAccess::ReleaseTerms(*F.Driver, F.Body(Caster), ZoneSource, Zone, F.Foot(Caster), Point, Error));
    };
    const auto Dash = [&](const FLDCombatBodyRef& Ref, double X, double Y, int64 AtMs)
    {
        StartDash(F, Ref, AtMs);
        F.Place(Ref, X, Y);
        F.Now(AtMs);
        return TestTrue(FString::Printf(TEXT("Dash completion: %s"), *Error), FAccess::ResolveDashCompletions(*F.Driver, AtMs, Error));
    };
    F.Now(1000);
    const FVector Point = F.Origin + FVector(400, 0, 0);
    if (!PlaceZone(Holder, Point) || !TestEqual(TEXT("The zone is placed"), Zones.Num(), 1)) { return false; }
    const int64 EndMs = Zones[0].EndMs;
    const int32 Pulsed = Zones[0].Pulsed;
    if (!Dash(Holder, 0, 300, 1200)) { return false; }
    const FVector Landing = F.Foot(Holder);
    TestTrue(TEXT("A completed dash of the host moves the holder's live zone to its foot, keeping its timing"), Zones.Num() == 1
        && Zones[0].bRelocated && FVector::Dist2D(Zones[0].CentreCm, Landing) < 1 && Zones[0].EndMs == EndMs && Zones[0].Pulsed == Pulsed);
    if (!Dash(Holder, 300, 300, 1400)) { return false; }
    TestTrue(TEXT("A zone moves only once in its life"), FVector::Dist2D(Zones[0].CentreCm, Landing) < 1);

    // The newest of a zone and an armed trap moves; the older zone stays.
    if (!PlaceZone(Holder, Point) || !Released(*this, F, Holder, TEXT("active_flyer_trap"), Trap, Payload)) { return false; }
    FLDCombatAction Action;
    Action.TargetPoint = F.Origin + FVector(300, -300, 0);
    bool bHandled = false;
    if (!TestTrue(FString::Printf(TEXT("Trap: %s"), *Error), FAccess::DeliverTerms(*F.Driver, F.Body(Holder), Trap, Action, Payload,
        1500, bHandled, Error) && bHandled && Ground.Num() == 1)) { return false; }
    if (!Dash(Holder, 0, -300, 1600)) { return false; }
    TestTrue(TEXT("The armed, untriggered trap placed last moves; the zone placed before it stays"), Ground.Num() == 1
        && Ground[0].bRelocated && FVector::Dist2D(Ground[0].CentreCm, F.Foot(Holder)) < 1 && Zones.Num() == 2 && !Zones[1].bRelocated
        && FVector::Dist2D(Zones[1].CentreCm, Point) < 1);
    if (!Dash(Holder, -300, -300, 1800)) { return false; }
    TestTrue(TEXT("The next dash moves the remaining zone"), Zones[1].bRelocated && FVector::Dist2D(Zones[1].CentreCm, F.Foot(Holder)) < 1);

    // A tethered (M16) zone ignores relocation.
    Zones.Reset();
    Ground.Reset();
    if (!InstallComponents(*this, F, Holder, 1, {On(TEXT("component_move_meeting"), ELDComponentTargetKind::Dash),
        On(TEXT("component_zone_tether"), ELDComponentTargetKind::Active, TEXT("active_quick_meeting"))})) { return false; }
    FLDCombatActiveTerms Tethered;
    if (!Released(*this, F, Holder, TEXT("active_quick_meeting"), Tethered, Payload)
        || !TestTrue(FString::Printf(TEXT("Tethered zone: %s"), *Error), FAccess::ReleaseTerms(*F.Driver, F.Body(Holder), ZoneSource,
            Tethered, F.Foot(Holder), Point, Error)) || !TestTrue(TEXT("Tethered"), Zones.Num() == 1 && Zones[0].bTether)) { return false; }
    if (!Dash(Holder, 500, 500, 2000)) { return false; }
    TestFalse(TEXT("A tethered zone is never relocated"), Zones[0].bRelocated);

    // Without C17 a completed dash moves nothing.
    Zones.Reset();
    if (!PlaceZone(Plain, F.Origin + FVector(-500, 0, 0)) || !TestEqual(TEXT("The plain zone is placed"), Zones.Num(), 1)) { return false; }
    const FVector PlainCentre = Zones[0].CentreCm;
    if (!Dash(Plain, -800, 300, 2200)) { return false; }
    TestTrue(TEXT("Without C17 the zone stays"), Zones.Num() == 1 && !Zones[0].bRelocated
        && FVector::Dist2D(Zones[0].CentreCm, PlainCentre) < 1);
    TestFalse(TEXT("C17 does not install on S01"), ProjectComponents(*F.Catalog, 1,
        {On(TEXT("component_move_meeting"), ELDComponentTargetKind::Active, TEXT("active_elbow"))}).IsSuccess());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentStoredForceTest, "Lockdown.Rules.Combat.Components.StoredForce",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentStoredForceTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    constexpr int64 HP = 10000000;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(ELDVictimKind::Contender, HP), Plain = F.AddBody(ELDVictimKind::Contender, HP),
        Enemy = F.AddBody(ELDVictimKind::Contender, HP);
    F.Place(Holder, 0, 0);
    F.Place(Enemy, 300, 0);
    F.Place(Plain, -500, 500);
    if (!InstallComponents(*this, F, Holder, 1,
        {On(TEXT("component_bottled_up"), ELDComponentTargetKind::Active, TEXT("active_not_mad"))})
        || !InstallComponents(*this, F, Plain, 1, {})) { return false; }
    FLDBodyCombatState& Body = F.Body(Holder);
    TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    FString Error;
    F.Now(1000);
    if (!ReleaseOnQ(*this, F, Holder, TEXT("active_not_mad"))) { return false; }
    const int64 Granted = Body.Resources.ShieldP2;
    if (!TestTrue(TEXT("S14 grants its shield and C18 watches it"), Granted > 0 && Body.StoredForceWatch.IsSet()
        && !Body.StoredForce.IsSet())) { return false; }
    if (!TestTrue(FString::Printf(TEXT("Chip: %s"), *Error), F.Apply(FFixture::Hit(Enemy, Holder, 2000), Error))) { return false; }
    const int64 Chipped = Granted - Body.Resources.ShieldP2;
    TestTrue(TEXT("A partial absorb stores nothing while the shield lives"), Chipped > 0 && Body.StoredForceWatch.IsSet()
        && Body.StoredForceWatch->AbsorbedP2 == Chipped && !Body.StoredForce.IsSet());
    Pending.Reset();
    if (!TestTrue(FString::Printf(TEXT("Break: %s"), *Error), F.Apply(FFixture::Hit(Enemy, Holder, Granted * 4), Error))) { return false; }
    TestTrue(TEXT("A broken shield stores half of everything it actually absorbed for 5 s"), Body.Resources.ShieldP2 == 0
        && !Body.StoredForceWatch.IsSet() && Body.StoredForce.IsSet() && FMath::Abs(Body.StoredForce->AmountP2 * 2 - Granted) <= 1
        && Body.StoredForce->ExpiresAtMs == 6000 && Body.StoredForce->Host == FName(TEXT("active_not_mad")));
    const int64 Stored = Body.StoredForce.IsSet() ? Body.StoredForce->AmountP2 : 0;

    // A different learned active's native direct hit spends the whole store once, as an added hit of its damage type.
    Pending.Reset();
    F.Now(2000);
    FLDCombatHit Bolt;
    if (!SnarkBolt(*this, F, Holder, Enemy, Bolt) || !Land(*this, F, Bolt)) { return false; }
    const FLDCombatHit* Added = ComponentHitOn(F, Enemy, TEXT("component_bottled_up"));
    TestTrue(TEXT("S06's native hit spends the store on its target: an added magical hit of the stored amount, never a crit"),
        Added && Added->BaseP2 == Stored && Added->DamageType == ELDDemoDamageType::Magical && Added->bAddedDamage
        && Added->HitClass != ELDCombatHitClass::BasicPrimary && !IsNativeDirectHit(*Added) && Added->ADRatioBp == 0 && Added->APRatioBp == 0
        && !Body.StoredForce.IsSet());
    if (!F.Flush(Error)) { AddError(TEXT("Spend: ") + Error); return false; }
    FLDCombatHit Again;
    if (!SnarkBolt(*this, F, Holder, Enemy, Again) || !Land(*this, F, Again)) { return false; }
    TestNull(TEXT("One store, one consumption"), ComponentHitOn(F, Enemy, TEXT("component_bottled_up")));
    Pending.Reset();

    // An expired shield stores its partial absorb; the store lapses after 5 s.
    F.Now(3000);
    if (!ReleaseOnQ(*this, F, Holder, TEXT("active_not_mad"))) { return false; }
    const int64 Second = Body.Resources.ShieldP2;
    if (!TestTrue(FString::Printf(TEXT("Chip: %s"), *Error), F.Apply(FFixture::Hit(Enemy, Holder, 2000), Error))) { return false; }
    const int64 Partial = Second - Body.Resources.ShieldP2;
    F.Now(7000);
    FAccess::ExpireStatuses(*F.Driver, 7000);
    TestTrue(TEXT("An expired shield stores half of what it absorbed"), Partial > 0 && Body.StoredForce.IsSet()
        && FMath::Abs(Body.StoredForce->AmountP2 * 2 - Partial) <= 1 && Body.StoredForce->ExpiresAtMs == 12000);
    F.Now(12000);
    Pending.Reset();
    FLDCombatHit Late;
    if (!SnarkBolt(*this, F, Holder, Enemy, Late) || !Land(*this, F, Late)) { return false; }
    TestTrue(TEXT("A store past its 5 s spends nothing"), !ComponentHitOn(F, Enemy, TEXT("component_bottled_up"))
        && !Body.StoredForce.IsSet());

    // Without C18 a broken shield stores nothing.
    if (!ReleaseOnQ(*this, F, Plain, TEXT("active_not_mad"))
        || !TestTrue(FString::Printf(TEXT("Break: %s"), *Error), F.Apply(FFixture::Hit(Enemy, Plain, Granted * 4), Error))) { return false; }
    TestTrue(TEXT("Without C18 nothing is watched or stored"), !F.Body(Plain).StoredForceWatch.IsSet() && !F.Body(Plain).StoredForce.IsSet());
    TestFalse(TEXT("C18 does not install on S06"), ProjectComponents(*F.Catalog, 1,
        {On(TEXT("component_bottled_up"), ELDComponentTargetKind::Active, TEXT("active_snark_bolt"))}).IsSuccess());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentEmpowerSpendTest, "Lockdown.Rules.Combat.Components.EmpowerSpend",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentEmpowerSpendTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    constexpr int64 HP = 10000000;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(ELDVictimKind::Contender, HP), Plain = F.AddBody(ELDVictimKind::Contender, HP),
        Enemy = F.AddBody(ELDVictimKind::Contender, HP);
    F.Place(Holder, 0, 0);
    F.Place(Enemy, 300, 0);
    F.Place(Plain, 0, 300);
    if (!InstallComponents(*this, F, Holder, 1,
        {On(TEXT("component_spend_fist"), ELDComponentTargetKind::Active, TEXT("active_triple_combo"))})
        || !InstallComponents(*this, F, Plain, 1, {})) { return false; }
    FLDBodyCombatState& Body = F.Body(Holder);
    TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    FString Error;
    F.Now(1000);
    if (!ReleaseOnQ(*this, F, Holder, TEXT("active_triple_combo"))) { return false; }
    if (!TestTrue(TEXT("S17 arms its three-charge package"), Body.Empowers.Num() == 1 && Body.Empowers[0].Charges == 3)) { return false; }
    const int64 Amount = Body.Empowers[0].AmountP2;
    const FGuid Package = Body.Empowers[0].EffectId;
    const auto HasBonus = [&Body, &Package]()
    { return Body.TimedBonuses.ContainsByPredicate([&Package](const FLDCombatTimedBonus& Row) { return Row.EffectId == Package; }); };
    Pending.Reset();
    FLDCombatHit Bolt;
    if (!SnarkBolt(*this, F, Holder, Enemy, Bolt) || !Land(*this, F, Bolt)) { return false; }
    const FLDCombatHit* Added = ComponentHitOn(F, Enemy, TEXT("component_spend_fist"));
    TestTrue(TEXT("Another active's native hit spends one charge on its target as an added physical hit of the package"), Added
        && Added->BaseP2 == Amount && Added->DamageType == ELDDemoDamageType::Physical && Added->bAddedDamage
        && Added->HitClass != ELDCombatHitClass::BasicPrimary && !IsNativeDirectHit(*Added) && Body.Empowers.Num() == 1
        && Body.Empowers[0].Charges == 2);
    Pending.Reset();
    if (!Land(*this, F, Bolt)) { return false; }
    TestTrue(TEXT("At most once per release"), !ComponentHitOn(F, Enemy, TEXT("component_spend_fist")) && Body.Empowers[0].Charges == 2);
    if (!F.Flush(Error)) { AddError(TEXT("Hits: ") + Error); return false; }
    for (int32 Index = 0; Index < 2; ++Index)
    {
        FLDCombatHit Next;
        if (!SnarkBolt(*this, F, Holder, Enemy, Next) || !Land(*this, F, Next)) { return false; }
        if (!F.Flush(Error)) { AddError(TEXT("Hits: ") + Error); return false; }
    }
    TestTrue(TEXT("The last spent charge removes the package and its attack-speed bonus"), Body.Empowers.IsEmpty() && !HasBonus());
    FLDCombatHit Empty;
    if (!SnarkBolt(*this, F, Holder, Enemy, Empty) || !Land(*this, F, Empty)) { return false; }
    TestNull(TEXT("No package, no spend"), ComponentHitOn(F, Enemy, TEXT("component_spend_fist")));
    Pending.Reset();

    // Without C19 the package waits for a basic.
    if (!ReleaseOnQ(*this, F, Plain, TEXT("active_triple_combo"))) { return false; }
    FLDCombatHit PlainBolt;
    if (!SnarkBolt(*this, F, Plain, Enemy, PlainBolt) || !Land(*this, F, PlainBolt)) { return false; }
    TestTrue(TEXT("Without C19 nothing is spent"), !ComponentHitOn(F, Enemy, TEXT("component_spend_fist"))
        && F.Body(Plain).Empowers.Num() == 1 && F.Body(Plain).Empowers[0].Charges == 3);
    TestFalse(TEXT("C19 does not install on S06"), ProjectComponents(*F.Catalog, 1,
        {On(TEXT("component_spend_fist"), ELDComponentTargetKind::Active, TEXT("active_snark_bolt"))}).IsSuccess());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentControlWidenTest, "Lockdown.Rules.Combat.Components.ControlWiden",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentControlWidenTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    constexpr int64 HP = 10000000;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(ELDVictimKind::Contender, HP), Enemy = F.AddBody(ELDVictimKind::Contender, HP);
    F.Place(Holder, 0, 0);
    F.Place(Enemy, 200, 0);
    if (!InstallComponents(*this, F, Holder, 1,
        {On(TEXT("component_wide_open"), ELDComponentTargetKind::Active, TEXT("active_stare_down"))})) { return false; }
    FLDBodyCombatState& Body = F.Body(Holder);
    TArray<FLDCombatZone>& Zones = FAccess::Zones(*F.Driver);
    FString Error;
    F.Now(1000);
    if (!ReleaseOnQ(*this, F, Holder, TEXT("active_quick_meeting"))
        || !TestTrue(TEXT("Without a token the zone keeps its radius"), Zones.Num() == 1 && FMath::IsNearlyEqual(Zones[0].RadiusCm, 240.)))
    { return false; }
    if (!ReleaseOnQ(*this, F, Holder, TEXT("active_stare_down"))) { return false; }
    TestTrue(TEXT("The host's landed taunt arms one wide token for 3 s"), Body.WideToken.IsSet() && Body.WideToken->ExpiresAtMs == 4000
        && Body.WideToken->Host == FName(TEXT("active_stare_down")) && Body.WideToken->RatioBp == 3500);
    if (!F.Flush(Error)) { AddError(TEXT("Taunt: ") + Error); return false; }
    // The host's own release (taunting nobody) neither spends nor refreshes it.
    F.Place(Enemy, 1500, 0);
    F.Now(1500);
    if (!ReleaseOnQ(*this, F, Holder, TEXT("active_stare_down"))) { return false; }
    TestTrue(TEXT("The host's own release keeps the token"), Body.WideToken.IsSet() && Body.WideToken->ExpiresAtMs == 4000);
    F.Now(2000);
    if (!ReleaseOnQ(*this, F, Holder, TEXT("active_quick_meeting"))) { return false; }
    TestTrue(TEXT("The next other release spends it: its zone radius x1.35"), Zones.Num() == 2
        && FMath::IsNearlyEqual(Zones[1].RadiusCm, 324., 0.01) && !Body.WideToken.IsSet());
    if (!ReleaseOnQ(*this, F, Holder, TEXT("active_quick_meeting"))) { return false; }
    TestTrue(TEXT("Only one release is widened"), Zones.Num() == 3 && FMath::IsNearlyEqual(Zones[2].RadiusCm, 240.));
    F.Place(Enemy, 200, 0);
    F.Now(5000);
    if (!ReleaseOnQ(*this, F, Holder, TEXT("active_stare_down")) || !F.Flush(Error)) { return false; }
    F.Now(8000);
    if (!ReleaseOnQ(*this, F, Holder, TEXT("active_quick_meeting"))) { return false; }
    TestTrue(TEXT("A token past its 3 s widens nothing"), Zones.Num() == 4 && FMath::IsNearlyEqual(Zones[3].RadiusCm, 240.));
    TestFalse(TEXT("C20 does not install on S08"), ProjectComponents(*F.Catalog, 1,
        {On(TEXT("component_wide_open"), ELDComponentTargetKind::Active, TEXT("active_quick_meeting"))}).IsSuccess());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentZoneDetonateTest, "Lockdown.Rules.Combat.Components.ZoneDetonate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentZoneDetonateTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    constexpr int64 HP = 10000000;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(ELDVictimKind::Contender, HP), Enemy = F.AddBody(ELDVictimKind::Contender, HP),
        Other = F.AddBody(ELDVictimKind::Contender, HP);
    F.Place(Holder, 0, 0);
    F.Place(Enemy, 400, 0);
    F.Place(Other, 450, 100);
    if (!InstallComponents(*this, F, Holder, 1,
        {On(TEXT("component_set_it_off"), ELDComponentTargetKind::Active, TEXT("active_zap"))})) { return false; }
    FLDCombatActiveTerms Zone;
    if (!ActiveTerms(*this, F, TEXT("active_quick_meeting"), Zone)) { return false; }
    TArray<FLDCombatZone>& Zones = FAccess::Zones(*F.Driver);
    TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    const FLDCombatSourceRef ZoneSource = SourceOf(ELDCombatSourceKind::Learned, TEXT("active_quick_meeting"));
    FString Error;
    const auto PlaceZone = [&](const FVector& Point)
    {
        return TestTrue(FString::Printf(TEXT("Zone: %s"), *Error),
            FAccess::ReleaseTerms(*F.Driver, F.Body(Holder), ZoneSource, Zone, F.Foot(Holder), Point, Error));
    };
    const auto Zap = [&](const FLDCombatBodyRef& Target, FLDCombatHit& Payload)
    {
        FLDCombatActiveTerms Terms;
        if (!Released(*this, F, Holder, TEXT("active_zap"), Terms, Payload)) { return false; }
        Payload.Target = Target;
        return Land(*this, F, Payload);
    };
    F.Now(1000);
    if (!PlaceZone(F.Origin + FVector(0, -600, 0))) { return false; }
    Pending.Reset();
    FLDCombatHit Outside;
    if (!Zap(Enemy, Outside)) { return false; }
    TestTrue(TEXT("A host hit on an enemy outside every zone detonates nothing"), Zones.Num() == 1
        && !ComponentHitOn(F, Enemy, TEXT("component_set_it_off")));
    Zones.Reset();
    Pending.Reset();
    if (!PlaceZone(F.Origin + FVector(400, 0, 0)) || !TestEqual(TEXT("The zone is placed"), Zones.Num(), 1)) { return false; }
    const FLDCombatZone Snapshot = Zones[0];
    const int32 Remaining = Snapshot.Count - Snapshot.Pulsed;
    FLDCombatHit Payload;
    if (!Zap(Enemy, Payload)) { return false; }
    const FLDCombatHit* Blast = ComponentHitOn(F, Enemy, TEXT("component_set_it_off"));
    const FLDCombatHit* OtherBlast = ComponentHitOn(F, Other, TEXT("component_set_it_off"));
    const int64 ScaleBp = Snapshot.PackageScaleBp * 8000 / 10000 * Remaining;
    TestTrue(TEXT("A host hit on an enemy inside the holder's zone ends it at once"), Zones.IsEmpty() && Remaining > 0);
    TestTrue(TEXT("Every enemy inside takes one added hit of 0.8 x the remaining pulses of the zone's package"), Blast && OtherBlast
        && Blast->BaseP2 == Snapshot.BaseP2 && Blast->APRatioBp == Snapshot.APRatioBp && Blast->DamageType == Snapshot.DamageType
        && FMath::Abs(Blast->PackageScaleBp - ScaleBp) <= Remaining && Blast->BurnStacks == Snapshot.PulseBurnStacks
        && Blast->bAddedDamage && !IsNativeDirectHit(*Blast) && OtherBlast->PackageScaleBp == Blast->PackageScaleBp);
    if (!F.Flush(Error)) { AddError(TEXT("Blast: ") + Error); return false; }
    // The same release cannot detonate a second zone.
    if (!PlaceZone(F.Origin + FVector(400, 0, 0))) { return false; }
    Pending.Reset();
    if (!Land(*this, F, Payload)) { return false; }
    TestTrue(TEXT("Once per host release"), Zones.Num() == 1 && !ComponentHitOn(F, Enemy, TEXT("component_set_it_off")));
    // An S06 hit (not a host) never detonates.
    FLDCombatHit Bolt;
    if (!SnarkBolt(*this, F, Holder, Enemy, Bolt) || !Land(*this, F, Bolt)) { return false; }
    TestTrue(TEXT("A non-host native hit leaves the zone"), Zones.Num() == 1 && !ComponentHitOn(F, Enemy, TEXT("component_set_it_off")));
    TestFalse(TEXT("C21 does not install on S06"), ProjectComponents(*F.Catalog, 1,
        {On(TEXT("component_set_it_off"), ELDComponentTargetKind::Active, TEXT("active_snark_bolt"))}).IsSuccess());
    return true;
}

// C22: a host release's first native direct hit calls the holder's newest unmoved zone to the struck enemy (once per
// release, once per zone life, shared with C17); with C21 on the same host the called zone detonates on that same hit.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComponentMeetHereTest, "Lockdown.Rules.Combat.Components.MeetHere",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComponentMeetHereTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    constexpr int64 HP = 10000000;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(ELDVictimKind::Contender, HP), Enemy = F.AddBody(ELDVictimKind::Contender, HP);
    F.Place(Holder, 0, 0);
    F.Place(Enemy, 400, 0);
    if (!InstallComponents(*this, F, Holder, 1,
        {On(TEXT("component_meet_here"), ELDComponentTargetKind::Active, TEXT("active_zap"))})) { return false; }
    FLDCombatActiveTerms Zone;
    if (!ActiveTerms(*this, F, TEXT("active_quick_meeting"), Zone)) { return false; }
    TArray<FLDCombatZone>& Zones = FAccess::Zones(*F.Driver);
    TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    const FLDCombatSourceRef ZoneSource = SourceOf(ELDCombatSourceKind::Learned, TEXT("active_quick_meeting"));
    FString Error;
    const auto PlaceZone = [&](const FVector& Point)
    {
        return TestTrue(FString::Printf(TEXT("Zone: %s"), *Error),
            FAccess::ReleaseTerms(*F.Driver, F.Body(Holder), ZoneSource, Zone, F.Foot(Holder), Point, Error));
    };
    const auto Zap = [&](FLDCombatHit& Payload)
    {
        FLDCombatActiveTerms Terms;
        if (!Released(*this, F, Holder, TEXT("active_zap"), Terms, Payload)) { return false; }
        Payload.Target = Enemy;
        return Land(*this, F, Payload);
    };
    F.Now(1000);
    const FVector Far = F.Origin + FVector(0, -600, 0);
    if (!PlaceZone(Far) || !TestEqual(TEXT("The zone is placed"), Zones.Num(), 1)) { return false; }
    const int64 EndMs = Zones[0].EndMs;
    FLDCombatHit Payload;
    if (!Zap(Payload)) { return false; }
    TestTrue(TEXT("The host hit calls the zone to the struck enemy, keeping its timing"), Zones.Num() == 1 && Zones[0].bRelocated
        && FVector::Dist2D(Zones[0].CentreCm, F.Foot(Enemy)) < 1 && Zones[0].EndMs == EndMs);
    if (!PlaceZone(Far)) { return false; }
    if (!Land(*this, F, Payload)) { return false; }
    TestTrue(TEXT("Once per host release"), Zones.Num() == 2 && !Zones[1].bRelocated && FVector::Dist2D(Zones[1].CentreCm, Far) < 1);
    FLDCombatHit Bolt;
    if (!SnarkBolt(*this, F, Holder, Enemy, Bolt) || !Land(*this, F, Bolt)) { return false; }
    TestFalse(TEXT("A non-host native hit calls nothing"), Zones[1].bRelocated);
    FLDCombatHit Again;
    if (!Zap(Again)) { return false; }
    TestTrue(TEXT("The next release calls the newest unmoved zone; a moved zone never moves again"), Zones[1].bRelocated
        && FVector::Dist2D(Zones[1].CentreCm, F.Foot(Enemy)) < 1 && FVector::Dist2D(Zones[0].CentreCm, F.Foot(Enemy)) < 1);

    // With C21 on the same host: the zone called under the enemy detonates on the same hit.
    Zones.Reset();
    Pending.Reset();
    if (!InstallComponents(*this, F, Holder, 1, {On(TEXT("component_meet_here"), ELDComponentTargetKind::Active, TEXT("active_zap")),
        On(TEXT("component_set_it_off"), ELDComponentTargetKind::Active, TEXT("active_zap"))})) { return false; }
    if (!PlaceZone(Far)) { return false; }
    FLDCombatHit Combo;
    if (!Zap(Combo)) { return false; }
    TestTrue(TEXT("Called, then detonated on the enemy by the same hit"), Zones.IsEmpty() && ComponentHitOn(F, Enemy, TEXT("component_set_it_off")));
    TestFalse(TEXT("C22 does not install on its zone active"), ProjectComponents(*F.Catalog, 1,
        {On(TEXT("component_meet_here"), ELDComponentTargetKind::Active, TEXT("active_quick_meeting"))}).IsSuccess());
    return true;
}
#endif
