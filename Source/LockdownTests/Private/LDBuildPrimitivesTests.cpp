#include "Combat/LDMomentum.h"
#include "Algo/AllOf.h"
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
#include "Character/LDFlightTrailComponent.h"
#include "Character/LDNativeMotionTypes.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Region/LDMapDefinition.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapRegistry.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/RootMotionSource.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "LDTestApprovedFixture.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

// #147 build v7 combat primitives (docs/systems/COMBAT_AND_GROWTH.md, "构筑 v7 战斗原语") and the #198 build safe window.
// The installed base components that feed the trigger handlers are covered by LDComponentCombatTests.

struct FLDBuildPrimitivesAccess
{
    static TMap<TWeakObjectPtr<ALDCharacter>, FLDBodyCombatState>& Bodies(ALDCombatDirector& Driver) { return Driver.Bodies; }
    static TMap<FGuid, FLDCombatEntityRecord>& Records(ALDCombatDirector& Driver) { return Driver.Records; }
    static TArray<FLDCombatHit>& PendingHits(ALDCombatDirector& Driver) { return Driver.PendingHits; }
    // Issue #287 P1: overdrive terms, perfect dodges and branch riders.
    static bool Overdrive(const ALDCombatDirector& Driver, const FLDBodyCombatState& Body, const FLDDemoAbilityDefinition& Ability,
        FLDCombatActiveTerms& Terms, FString& Error) { return Driver.ApplyOverdrive(Body, Ability, Terms, Error); }
    static bool PerfectDodges(ALDCombatDirector& Driver, const FGuid& EffectId, const FGuid& Source, TFunctionRef<bool(const FVector&)> InArea,
        int64 NowMs, FString& Error) { return Driver.ResolvePerfectDodges(EffectId, Source, InArea, NowMs, Error); }
    static int64 Vulnerable(const ALDCombatDirector& Driver, const FLDBodyCombatState& Target, const FLDCombatHit& Hit, int64 NowMs)
    { return Driver.BranchVulnerableBp(Target, Hit, NowMs); }
    static bool AdvanceBranches(ALDCombatDirector& Driver, int64 NowMs, FString& Error) { return Driver.AdvanceBranches(NowMs, Error); }
    static auto& MonsterStrikes(ALDCombatDirector& Driver){return Driver.MonsterStrikes;}
    static bool MonsterRelease(ALDCombatDirector& D,ALDCharacter& A,FLDBodyCombatState& B,const FLDCombatAction& Action,ELDCombatReject& Reject,FString& Error)
    {return D.ReleaseMonsterSkill(A,B,Action,Reject,Error);}
    static bool MonsterStep(ALDCombatDirector& D,int64 Now,FString& Error){return D.AdvanceMonsterStrikes(Now,Error);}
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
    static void AbandonAction(ALDCombatDirector& Driver, FLDBodyCombatState& Body, ELDCombatReject Reason) { Driver.AbandonAction(Body, Reason); }
    static void FinishAction(ALDCombatDirector& Driver, FLDBodyCombatState& Body, ELDCombatReject Reason, bool bReleased)
    { Driver.FinishAction(Body, Reason, bReleased); }
    static bool ReleaseMechanics(ALDCombatDirector& Driver, FLDBodyCombatState& Body, const FLDCombatSourceRef& Source,
        const FLDDemoAbilityValues& Values, const FVector& Origin, const FVector& Point, FString& Error)
    {
        FLDCombatActiveTerms Terms;
        Terms.Values = Values;
        return Driver.ReleaseMechanics(Body, Source, Terms, Origin, Point, (Point-Origin).GetSafeNormal2D(), FGuid::NewGuid(), 0, MakeShared<FLDCombatCastLedger>(), Error);
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
    // Issue #287 P4 ground states and the S10 chain.
    static TArray<FLDGroundState>& GroundStates(ALDCombatDirector& Driver) { return Driver.GroundStates; }
    static bool SpawnGround(ALDCombatDirector& Driver, ELDGroundStateKind Kind, const FLDElementApplier& Owner, const FVector& Centre,
        int64 NowMs, FString& Error) { return Driver.SpawnGround(Kind, Owner, Centre, NowMs, Error); }
    static bool ConvertGround(ALDCombatDirector& Driver, const FVector& Foot, ELDCombatElement Incoming, const FLDElementApplier& Applier,
        int64 NowMs, FString& Error) { return Driver.ConvertGroundAt(Foot, Incoming, Applier, NowMs, Error); }
    static bool AdvanceGround(ALDCombatDirector& Driver, int64 NowMs, FString& Error) { return Driver.AdvanceGround(NowMs, Error); }
    static void SetDaily(ALDGameMode& Mode, const TArray<ELDDailyModifier>& Daily) { Mode.DailyModifiers = Daily; }
    static int64 NextGroundDueMs(const ALDCombatDirector& Driver, int64 NowMs) { return Driver.NextGroundDueMs(NowMs); }
    // Spec §5 N4 bags.
    static TArray<FLDCombatBag>& Bags(ALDCombatDirector& Driver) { return Driver.Bags; }
    static bool PlaceBag(ALDCombatDirector& Driver, FLDBodyCombatState& Body, const FLDCombatHit& Payload, const FLDDemoEffectDefinition& Effect,
        const FVector& Centre, int64 NowMs, FString& Error) { return Driver.PlaceBag(Body, Payload, Effect, nullptr, Centre, NowMs, Error); }
    static bool AdvanceBags(ALDCombatDirector& Driver, int64 NowMs, FString& Error) { return Driver.AdvanceBags(NowMs, Error); }
    static void HitBags(ALDCombatDirector& Driver, const FVector& Centre, double RadiusCm, const FLDCombatHit& Payload, int64 NowMs)
    { FString Error; Driver.HitBagsInCircle(Centre, RadiusCm, Payload, NowMs, Error); }
    static int32 BagOnSegment(const ALDCombatDirector& Driver, const FGuid& Source, const FVector& From, const FVector& To, double& At)
    { return Driver.FirstBagOnSegment(Source, From, To, 10., At); }
    static void GroundFactors(const ALDCombatDirector& Driver, const FLDBodyCombatState& Source, const FLDBodyCombatState& Target,
        TArray<FLDDamageFactor>& Out) { Driver.GroundFactors(Source, Target, Out); }
    static TArray<FLDCombatChain>& Chains(ALDCombatDirector& Driver) { return Driver.Chains; }
    static bool StartChain(ALDCombatDirector& Driver, const FLDCombatHit& Payload, const FLDCombatBodyRef& First, const FVector& Foot,
        const FLDDemoEffectDefinition& Bounce, int64 NowMs, FString& Error) { return Driver.StartChain(Payload, First, Foot, Bounce, NowMs, Error); }
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
    static bool Collect(const ALDCombatDirector& Driver, const FLDAttributeProjection& Projection, ELDDemoEffectOp Op,
        TArray<FLDPassiveEffectRef>& Out, FString& Error)
    { return Driver.CollectPassiveEffects(Projection, Op, Out, Error); }
    // #198 F mods and connectors.
    static bool Compile(const ALDCombatDirector& Driver, const FLDAttributeProjection& Projection, const FLDCombatSourceRef& Source,
        FLDCombatActiveTerms& Terms, FString& Error)
    { return Driver.CompileActive(Projection, Source, Terms, Error); }
    static bool ReleaseTerms(ALDCombatDirector& Driver, FLDBodyCombatState& Body, const FLDCombatSourceRef& Source,
        const FLDCombatActiveTerms& Terms, const FVector& Origin, const FVector& Point, FString& Error)
    { return Driver.ReleaseMechanics(Body, Source, Terms, Origin, Point, (Point-Origin).GetSafeNormal2D(), FGuid::NewGuid(), 0, MakeShared<FLDCombatCastLedger>(), Error); }
    static bool DeliverTerms(ALDCombatDirector& Driver, FLDBodyCombatState& Body, const FLDCombatActiveTerms& Terms,
        const FLDCombatAction& Action, FLDCombatHit& Hit, int64 Now, bool& Handled, FString& Error)
    { return Driver.StartSkillDelivery(Body, Terms, Action, Hit, Now, Handled, Error); }
    static TArray<FLDCombatChannel>& Channels(ALDCombatDirector& Driver) { return Driver.Channels; }
    static bool Watch(ALDCombatDirector& Driver, ALDCharacter& Target, FLDBodyCombatState& Body, const FLDCombatHit& Hit, int64 NowMs,
        FString& Error)
    { return Driver.WatchKnockbackImpact(Target, Body, Hit, FVector::ForwardVector, NowMs, Error); }
    static bool Impact(ALDCombatDirector& Driver, FLDBodyCombatState& Body, const FLDCombatKnockbackImpact& Impact, bool bBody,
        const ALDCharacter* Blocker, int64 NowMs, FString& Error)
    { return Driver.ResolveKnockbackImpact(Body, Impact, bBody, Blocker, NowMs, Error); }
    // #198 H combat event correctness: the real release, delivery and Effects entries.
    static bool BuildPayload(const ALDCombatDirector& Driver, const FLDBodyCombatState& Body, const FLDCombatSourceRef& Source,
        const FLDCombatActiveTerms& Terms, const FVector& Foot, FLDCombatHit& Out, FString& Error)
    { return Driver.BuildReleasePayload(Body, Source, Terms, Foot, FGuid::NewGuid(), 1, MakeShared<FLDCombatCastLedger>(), Out, Error); }
    static bool DeliverPayload(ALDCombatDirector& Driver, const ALDCharacter& Actor, const FLDBodyCombatState& Body,
        const FLDCombatActiveTerms& Terms, const FLDCombatAction& Action, const FLDCombatHit& Template, const FLDCombatBodyRef& Unit,
        const FVector& UnitFoot, const FVector& Foot, int64 NowMs, FString& Error)
    { return Driver.DeliverReleasePayload(Actor, Body, Terms, Action, Template, Unit, UnitFoot, Foot, {}, NowMs, Error); }
    static bool ApplyPendingHits(ALDCombatDirector& Driver, int64 NowMs, FString& Error) { return Driver.ApplyPendingHits(NowMs, Error); }
    static bool AdvanceMechanics(ALDCombatDirector& Driver, int64 NowMs, FString& Error) { return Driver.AdvanceMechanics(NowMs, Error); }
    static bool MakeEmpower(const ALDCombatDirector& Driver, const FLDBodyCombatState& Body, const FLDCombatSourceRef& Source,
        const FLDDemoEffectDefinition& Effect, int64 NowMs, FLDCombatEmpower& Out, FString& Error)
    { return Driver.MakeEmpower(Body, Source, Effect, FGuid::NewGuid(), NowMs, Out, Error); }
    static bool AdvanceSkillContent(ALDCombatDirector& Driver, int64 NowMs, FString& Error) { return Driver.AdvanceSkillContent(NowMs, Error); }
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
    static bool BindReactionObserver(ALDGameMode& Mode, ALDPlayerController& Controller,
        const TArray<TPair<FLDCombatBodyRef, ALDCharacter*>>& Bodies, FString& Error)
    {
        Mode.MatchContext.Generation = 1;
        Mode.MatchContext.LedgerMatch = FGuid::NewGuid();
        Mode.Contenders.Add(&Controller).Session = Bodies[0].Key.Victim.EntityId;
        for (const auto& Entry : Bodies)
        {
            auto& Life = Mode.Lives.Add(Entry.Key.Victim.EntityId);
            Life.Kind = Entry.Key.Victim.Kind;
            Life.Phase = ELDLifePhase::Alive;
            Life.Life = Entry.Key.Victim.Life;
            Life.Body = Entry.Key;
            Life.Actor = Entry.Value;
        }
        ALDVisionDirector& Vision = *Mode.Vision;
        Vision.ActiveMatch = Mode.MatchContext;
        Vision.bActive = true;
        auto& Observer = Vision.Observers.AddDefaulted_GetRef();
        Observer.ContenderId = Bodies[0].Key.Victim.EntityId;
        Observer.Controller = &Controller;
        bool Reset = false;
        if (!Observer.Ledger.Begin(1, Error) || !Observer.Ledger.Bind(ELDLifePhase::Alive, 1, Reset, Error)) { return false; }
        TArray<FLDObservedTarget> Targets;
        for (int32 Index = 1; Index < Bodies.Num(); ++Index)
        {
            FLDObservedTarget& Target = Targets.AddDefaulted_GetRef();
            Target.Body = Bodies[Index].Key;
            Target.Presented.AuthorizedFieldMask = LDObservation::ContenderFields;
            Target.Presented.Kind = ELDObservedUnitKind::Contender;
            Target.Presented.Presentation = 2;
            Target.Presented.ContenderId = Target.Body.Victim.EntityId;
            Target.Presented.Level = 1;
            Target.Presented.Health.CurrentP2 = Target.Presented.Health.MaxP2 = 100000;
        }
        TArray<const FLDObservedTarget*> Visible;
        for (const FLDObservedTarget& Target : Targets) { Visible.Add(&Target); }
        TArray<FGuid> Removed;
        TArray<FLDObservedUnitView> Units;
        return Observer.Ledger.Sample(Visible, Removed, Units, Error);
    }
    static ELDReactionAssistDelivery SendReaction(ALDGameMode& Mode, const FLDCombatBodyRef& Setter, const FLDCombatBodyRef& Target,
        const FLDCombatBodyRef& Trigger, FLDReactionAssistView& Sent, FString& Error, FGuid Credit = FGuid())
    {
        FLDReactionAssistView View;
        View.Match = Mode.MatchContext.Generation;
        View.Trigger = Credit.IsValid() ? Credit : Trigger.Victim.EntityId;
        View.Consumed = 2;
        View.Reaction = 2;
        View.AtMs = 1000;
        return Mode.SendReactionAssist(Setter.Victim.EntityId, Target, Trigger, View, Sent, Error);
    }
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
            // DestroyWorld leaves its package until GC; never create another world inside that retained package.
            uint32 Instance = 100;
            FString Short, PackageName;
            do
            {
                Short = FString::Printf(TEXT("UEDPIE_%u_%s"), Instance++,
                    *FPackageName::GetShortName(LDDemoEntry::WorldMap));
                PackageName = FPackageName::GetLongPackagePath(LDDemoEntry::WorldMap) / Short;
            }
            while (FindPackage(nullptr, *PackageName));
            Package = CreatePackage(*PackageName);
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

    // A hand-built payload gets the hit identity every release path issues; queued hits keep theirs (Flush).
    bool Apply(const FLDCombatHit& Hit, FString& Error)
    {
        FLDCombatHit Issued = Hit;
        if (!Issued.HitId.IsValid()) { Issued.HitId = FGuid::NewGuid(); }
        return FAccess::ApplyHit(*Driver, Issued, Error);
    }

    // Applies every queued hit (and what those queue) like one Effects phase.
    bool Flush(FString& Error)
    {
        TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*Driver);
        for (int32 Index = 0; Index < Pending.Num(); ++Index)
        {
            const FLDCombatHit Each = Pending[Index];
            if (!FAccess::ApplyHit(*Driver, Each, Error)) { return false; }
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDReactionPromptSightTest, "Lockdown.Rules.DamageRecap.LiveSight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDReactionPromptSightTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef Setter = F.AddBody(), Target = F.AddBody(), Trigger = F.AddBody();
    F.Place(Setter, 0, 0); F.Place(Target, 200, 0); F.Place(Trigger, 400, 0);
    ALDPlayerController* Controller = F.World->SpawnActor<ALDPlayerController>();
    FString Error;
    if (!TestNotNull(TEXT("The setter has a controller"), Controller)
        || !TestTrue(TEXT("The setter has a sampled observation"),
            FAccess::BindReactionObserver(*F.Mode, *Controller,
                {{Setter, &F.Actor(Setter)}, {Target, &F.Actor(Target)}, {Trigger, &F.Actor(Trigger)}}, Error)))
    { AddError(Error); return false; }
    FLDReactionAssistView Sent;
    TestTrue(TEXT("Currently visible reaction and trigger are reported"),
        FAccess::SendReaction(*F.Mode, Setter, Target, Trigger, Sent, Error) == ELDReactionAssistDelivery::Sent
        && Sent.Trigger == Trigger.Victim.EntityId);
    const FVector Origin = F.Actor(Target).GetActorLocation();
    const FLDSightGeometry& Sight = FAccess::Sight(*F.Mode->Vision);
    const FVector2D FarXY = Sight.MaxCm() - FVector2D(10, 10);
    const FVector FarFoot(FarXY.X, FarXY.Y, F.Foot(Setter).Z);
    if (!TestTrue(TEXT("The test point is out of sight but inside the registered map"),
        F.Mode->Vision->TraceSight(F.Foot(Setter), FarFoot, Sight.Revision()) == ELDSightResult::OutOfRange)) { return false; }
    const FVector FarActor = FarFoot + FVector(0, 0, F.Actor(Target).GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    F.Actor(Target).SetActorLocation(FarActor);
    TestTrue(TEXT("A stale observation cannot report a target that has moved out of sight"),
        FAccess::SendReaction(*F.Mode, Setter, Target, Trigger, Sent, Error) == ELDReactionAssistDelivery::Unavailable && Error.IsEmpty());
    F.Actor(Target).SetActorLocation(Origin);
    F.Actor(Trigger).SetActorLocation(FarActor);
    TestTrue(TEXT("A stale trigger observation cannot disclose its identity"),
        FAccess::SendReaction(*F.Mode, Setter, Target, Trigger, Sent, Error) == ELDReactionAssistDelivery::Sent && !Sent.Trigger.IsValid());
    F.Place(Trigger, 400, 0);
    TestTrue(TEXT("A visible source body cannot disclose a different credited contender"),
        FAccess::SendReaction(*F.Mode, Setter, Target, Trigger, Sent, Error, FGuid::NewGuid()) == ELDReactionAssistDelivery::Sent
        && !Sent.Trigger.IsValid());
    F.Place(Setter, 0, 0);
    F.Body(Target).IsolationGroup = FGuid::NewGuid();
    TestTrue(TEXT("A target newly isolated from the setter cannot disclose its reaction"),
        FAccess::SendReaction(*F.Mode, Setter, Target, Trigger, Sent, Error) == ELDReactionAssistDelivery::Unavailable && Error.IsEmpty());
    F.Body(Target).IsolationGroup.Invalidate();
    F.Body(Target).bPendingDeath = true;
    TestTrue(TEXT("A fatal hit can report before the current target life is retired"),
        FAccess::SendReaction(*F.Mode, Setter, Target, Trigger, Sent, Error) == ELDReactionAssistDelivery::Sent);
    F.Actor(Target).SetActorLocation(F.Actor(Setter).GetActorLocation() + FVector(100000, 0, 0));
    TestTrue(TEXT("Invalid sight is an authority fault rather than a hidden target"),
        FAccess::SendReaction(*F.Mode, Setter, Target, Trigger, Sent, Error) == ELDReactionAssistDelivery::AuthorityFault && !Error.IsEmpty());
    return true;
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
        && Ticks[0].BaseP2 == 450 && Ticks[1].SourceEntity == B.Victim.EntityId && Ticks[1].BaseP2 == 375 && Ticks[0].bBurnTick
        && Ticks[0].bPeriodic && Ticks[0].OnHitScaleBp == 0 && Ticks[0].DamageType == ELDDemoDamageType::Magical);
    TestTrue(TEXT("The ticks land: 4.50 and 3.75"), F.Flush(Error) && Body.Resources.HpP2 == 89175
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

// A contender's basic windup committed its cadence at its start; cut before its release (a dash, a skill, S, control) it
// swung nothing and gives the cadence back. A swing refused at its release moment keeps it spent.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBasicWindupCancelRefundTest, "Lockdown.Rules.DemoBuildPrimitives.BasicWindupCancelRefund",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBasicWindupCancelRefundTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Attacker = F.AddBody(), Target = F.AddBody();
    FLDCombatEntityRecord& Record = FAccess::Records(*F.Driver).FindChecked(Attacker.Victim.EntityId);
    const auto Windup = [&](int64 StartMs)
    {
        F.Now(StartMs);
        FLDCombatAction Action;
        Action.Kind = ELDCombatActionKind::Basic;
        Action.Key.Origin = ELDCombatRequestOrigin::Nav;
        Action.Key.Entity = Attacker.Victim.EntityId;
        Action.TargetBody = Target;
        Action.StartedAtMs = StartMs;
        Action.ReleaseAtMs = StartMs + 300;
        F.Body(Attacker).Action = Action;
        Record.NextAttackAtMs = StartMs + 1000;
    };
    Windup(2000);
    F.Now(2080);
    FAccess::AbandonAction(*F.Driver, F.Body(Attacker), ELDCombatReject::Cancelled);
    TestFalse(TEXT("The cancelled windup ends"), F.Body(Attacker).Action.IsSet());
    TestEqual(TEXT("A windup cut before its release gives the cadence back"), Record.NextAttackAtMs, int64(2000));

    Windup(5000);
    F.Now(5300);
    FAccess::FinishAction(*F.Driver, F.Body(Attacker), ELDCombatReject::Blocked, false);
    TestEqual(TEXT("A swing refused at its release keeps the cadence spent"), Record.NextAttackAtMs, int64(6000));

    Windup(8000);
    F.Now(8300);
    FAccess::FinishAction(*F.Driver, F.Body(Attacker), ELDCombatReject::None, true);
    TestEqual(TEXT("A released swing keeps the cadence spent"), Record.NextAttackAtMs, int64(9000));

    // A Direct request still queued (held behind an own windup) ends with a terminal receipt when the body's effects are
    // cancelled (a training reset), never silently.
    FLDCombatActionRequest Held;
    Held.Generation = 1;
    Held.Life = 1;
    Held.DirectSequence = 7;
    Held.Entry = ELDCombatEntry::W;
    Record.Pending.Add(Held);
    FAccess::CancelEntityEffects(*F.Driver, Attacker.Victim.EntityId);
    const FLDCombatDirectEntry* Closed = Record.Recent.FindByPredicate([](const FLDCombatDirectEntry& Entry)
        { return Entry.Request.DirectSequence == 7; });
    TestTrue(TEXT("A cancelled queued request leaves the queue with a Cancelled receipt"), Record.Pending.IsEmpty() && Closed
        && Closed->Receipt.Phase == ELDCombatPhase::Rejected && Closed->Receipt.Reject == ELDCombatReject::Cancelled);
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
    const FLDDemoAbilityValues* Values = F.Catalog->FindValues(*Ability, 1, ELDSkillBranch::None);
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
    const auto Result=LDTactical::EvaluateDamage(Hit,*Catalog);
    if (!TestTrue(TEXT("Exact mitigation evaluates"),Result.IsSuccess())) { return false; }
    TestEqual(TEXT("The delivery scale applies to the mitigated amount"),Result.Value->DamageP2,int64(4800));
    TestEqual(TEXT("Shield absorbs only after mitigation"),Result.Value->HPLostP2,int64(2800));
    Hit.DeliveryScaleBp=-1;
    TestFalse(TEXT("A negative delivery share is refused"),LDTactical::EvaluateDamage(Hit,*Catalog).IsSuccess());

    // Eighty agility points past level 16 stay below the cap of 5 until a large temporary bonus is applied.
    FLDAttributeBuildValues Values(16,{FLDStatPointsValue(TEXT("stat_agility"),80)});
    const auto Build=LDAttributeProjection::Project(Values,*Catalog);
    if (!TestTrue(TEXT("The capped build projects"),Build.IsSuccess())) { return false; }
    FLDTemporaryAttributeTerms Terms;
    FLDDemoStatBonus Speed;
    Speed.Stat=ELDDemoBonusStat::AttackSpeedBonusRatio;
    Speed.Amount=60000;
    Terms.Bonuses.Add(Speed);
    Terms.AttackSlowBp=2500;
    const auto Slowed=LDAttributeProjection::ApplyTemporary(Build.Value.GetValue(),Terms,*Catalog);
    TestTrue(TEXT("An attack slow slows the capped attack speed, not the pre-cap sum"),
        Slowed.IsSuccess() && Slowed.Value->Final.AttackSpeed.Numerator==15 && Slowed.Value->Final.AttackSpeed.Denominator==4);
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
    auto* Brick=FAccess::Ground(*F.Driver).FindByPredicate([](const auto& Ground)
        { return Ground.Origin==ELDCombatGroundOrigin::Impact; });
    if (!TestNotNull(TEXT("The brick ground effect exists"),Brick)) { return false; }
    Brick->Origin=ELDCombatGroundOrigin::Echo;
    Facts.Reset();
    TestTrue(TEXT("The brick echo retains its own recognizable outline"),
        F.Driver->ReadObservedProjectiles(Facts,Error)
        && Facts.ContainsByPredicate([](const auto& Fact) { return Fact.Look==ELDObservedProjectileLook::BookShadow
            && Fact.RadiusCm==200 && Fact.InnerRadiusCm==80 && Fact.PositionCm.X==650; }));
    Brick->Origin=ELDCombatGroundOrigin::Impact;
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

// Every flight a contender can throw has a public look: A05's flying basic (an unnamed basic source) and N3 used to stop
// the match when the observation step met them.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDFlightLookTest, "Lockdown.Rules.DemoSkillExpansion.FlightLooks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDFlightLookTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const auto Source=F.AddBody();
    const auto Fly=[&](ELDCombatSourceKind Kind, const TCHAR* Definition)
    {
        FLDCombatProjectile Flight;
        Flight.EffectId=FGuid::NewGuid();
        Flight.FlightId=FGuid::NewGuid();
        Flight.SourceBody=Source;
        Flight.SourceEntity=Source.Victim.EntityId;
        Flight.Source.Kind=Kind;
        Flight.Source.DefinitionId=Definition ? FName(Definition) : NAME_None;
        Flight.OriginCm=FVector::ZeroVector;
        Flight.Direction=FVector(1,0,0);
        Flight.SpeedCmPerSec=1800;
        Flight.RangeCm=600;
        Flight.RadiusCm=55;
        FAccess::Projectiles(*F.Driver).Add(Flight);
        return Flight.FlightId;
    };
    const FGuid Basic=Fly(ELDCombatSourceKind::Basic,nullptr);
    const FGuid Takeout=Fly(ELDCombatSourceKind::Learned,TEXT("active_stale_takeout"));
    const FGuid Shoe=Fly(ELDCombatSourceKind::Learned,TEXT("active_snark_bolt"));
    TArray<FLDObservedProjectileFact> Facts;
    FString Error;
    if (!TestTrue(TEXT("Every contender flight is observable: ")+Error,F.Driver->ReadObservedProjectiles(Facts,Error))) { return false; }
    const auto LookOf=[&Facts](const FGuid& Key)
    {
        const FLDObservedProjectileFact* Fact=Facts.FindByPredicate([&Key](const auto& One) { return One.Key==Key; });
        return Fact ? Fact->Look : ELDObservedProjectileLook(0);
    };
    TestEqual(TEXT("A05's flying basic is drawn as a chalk throw"),LookOf(Basic),ELDObservedProjectileLook::Chalk);
    TestEqual(TEXT("N3 shares the rot sock look with S21"),LookOf(Takeout),ELDObservedProjectileLook::Sock);
    TestEqual(TEXT("S06 keeps its chalk look"),LookOf(Shoe),ELDObservedProjectileLook::Chalk);
    Fly(ELDCombatSourceKind::Learned,TEXT("active_elbow"));
    TestFalse(TEXT("A skill that never flies still has no projectile look"),F.Driver->ReadObservedProjectiles(Facts,Error));
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
        const FLDDemoAbilityValues* First=Catalog->FindValues(Ability,1, ELDSkillBranch::None);
        if (!TestNotNull(*FString::Printf(TEXT("%s has its level-1 values"),*Ability.Id),First)) { continue; }
        for (const uint8 Level : {uint8(5),uint8(10),uint8(13),uint8(16)})
        {
            const FLDDemoAbilityValues* Grown=Catalog->FindValues(Ability,Level, ELDSkillBranch::None);
            bool bBounded=Grown && Grown->MomentumCostP2==First->MomentumCostP2 && Grown->CooldownMs==First->CooldownMs
                && Grown->Effects.Num()==First->Effects.Num();
            for (int32 Index=0; bBounded && Index<First->Effects.Num(); ++Index)
            {
                bBounded=Grown->Effects[Index].BaseP2>=First->Effects[Index].BaseP2
                    && Grown->Effects[Index].DurationMs==First->Effects[Index].DurationMs
                    && Grown->Effects[Index].RatioBp==First->Effects[Index].RatioBp;
            }
            TestTrue(*FString::Printf(TEXT("%s at level %u grows its amounts only, never momentum, cooldown, ratios or durations"),
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
    TestTrue(TEXT("Water and shock conduct"), ReactionOf(E::Water, E::Shock) == R::Conduct && ReactionOf(E::Shock, E::Water) == R::Conduct);
    TestTrue(TEXT("Fire and shock overload"), ReactionOf(E::Fire, E::Shock) == R::Overload && ReactionOf(E::Shock, E::Fire) == R::Overload);
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
    FLDCombatElementMark Rot;
    Rot.Kind = E::Rot;
    Rot.HealCutBp = 2500;
    Rot.ExpiresAtMs = 4000;
    TestEqual(TEXT("A rot cuts a new heal or shield by 25%"), AfterRotP2(10000, Rot, 2000), int64(7500));
    TestEqual(TEXT("and nothing after it ends"), AfterRotP2(10000, Rot, 4000), int64(10000));
    FLDCombatHit Hit;
    Hit.HitClass = ELDCombatHitClass::Ability;
    Hit.bDirectAbilityDamage = true;
    TestTrue(TEXT("An active's own damage is skill-direct"), CategoryOf(Hit) == ELDCombatHitCategory::SkillDirect);
    Hit.bStatusSettle = true;
    TestTrue(TEXT("Reaction, shock and settle damage is status damage"), CategoryOf(Hit) == ELDCombatHitCategory::StatusSettle);
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
    Wet.NativeElements = LDCombatElements::Bit(ELDCombatElement::Water);
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
    TestTrue(TEXT("The new fire's whole budget (six ticks of 2.25) settles at once as B's fire, then R = 30 from B"),
        Pending.Num() == 2 && Pending[0].bBurnTick && Pending[0].BaseP2 == 1350 && Pending[0].SourceEntity == B.Victim.EntityId
        && Pending[1].bStatusSettle && !Pending[1].bBurnTick && Pending[1].BaseP2 == 3000 && Pending[1].SourceEntity == B.Victim.EntityId
        && Pending[1].Target == Target);
    TestTrue(TEXT("B owns one fog, its next pulse one second later, and the reaction is reported with B as trigger"),
        FAccess::Fogs(*F.Driver).Num() == 1 && FAccess::Fogs(*F.Driver)[0].Owner.SourceEntity == B.Victim.EntityId
        && FAccess::Fogs(*F.Driver)[0].NextPulseMs == 2500 && FAccess::Reactions(*F.Driver).Num() == 1
        && FAccess::Reactions(*F.Driver)[0].Kind == ELDCombatReaction::Fog
        && FAccess::Reactions(*F.Driver)[0].TriggerEntity == B.Victim.EntityId);
    TestTrue(TEXT("The settles land"), F.Flush(Error) && Body.Resources.HpP2 == 100000 - 4350);

    FLDBodyCombatState& Other = F.Body(Second);
    FLDCombatHit TwoLayers = FFixture::Hit(A, Second, 0);
    TwoLayers.BurnStacks = 2;
    TestTrue(TEXT("Two fire layers of A"), F.Apply(TwoLayers, Error) && Other.Burn.Layers.Num() == 2);
    F.Now(2000);
    FLDCombatHit Water = FFixture::Hit(B, Second, 0);
    Water.NativeElements = LDCombatElements::Bit(ELDCombatElement::Water);
    TestTrue(FString::Printf(TEXT("Water on fire: %s"), *Error), F.Apply(Water, Error));
    TestTrue(TEXT("A's held layers settle their remaining six ticks each to A; B's R follows; the new fog wets the target"),
        Pending.Num() == 2 && Pending[0].SourceEntity == A.Victim.EntityId && Pending[0].BaseP2 == 2700 && Pending[0].bBurnTick
        && Pending[1].SourceEntity == B.Victim.EntityId && Pending[1].BaseP2 == 3000
        && Other.Burn.Layers.IsEmpty() && Other.ElementMark.Kind == ELDCombatElement::Water);
    TestEqual(TEXT("B's newer fog replaced its first one"), FAccess::Fogs(*F.Driver).Num(), 1);
    TestTrue(TEXT("The settles land"), F.Flush(Error));

    FLDBodyCombatState& Free = F.Body(Third);
    FLDCombatHit Zap = FFixture::Hit(A, Third, 0);
    Zap.NativeElements = LDCombatElements::Bit(ELDCombatElement::Shock);
    TestTrue(TEXT("Shock on an element-free target marks it"), F.Apply(Zap, Error)
        && Free.ElementMark.Kind == ELDCombatElement::Shock && Free.ElementMark.ExpiresAtMs == 6000);
    FAccess::CancelEntityEffects(*F.Driver, A.Victim.EntityId);
    TestTrue(TEXT("A leaving applier's mark ends with it"), Free.ElementMark.Kind == ELDCombatElement::None);
    return true;
}

// #198 S16 顶着揍 (AI spec §9.1): 20% of hostile damage after resistance is cut and recorded up to the cap; its own damage is
// not; a dying stance ends without its answer.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoElementBraceTest, "Lockdown.Rules.DemoElements.Brace",
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
    return true;
}

// #198 B base components (docs/systems/COMBAT_AND_GROWTH.md, "基础构件"): the projection terms, the merged empowered basic,
// B12's dash completion, the B02 ledger and the component element gate, over the approved component catalog.
namespace LDBuildPrimitivesTests
{
// One installed card of a test build: its definition, place (a learned skill for a skill card) and Y25's element.
struct FCard
{
    const TCHAR* Id = nullptr;
    ELDCardTargetKind Target = ELDCardTargetKind::Body;
    FName Active;
    ELDDemoElement Element = ELDDemoElement::None;
};

FLDAttributeProjectionResult ProjectCards(const FLDDemoContentSnapshot& Catalog, uint8 Level, const TArray<FCard>& Rows)
{
    FLDAttributeBuildValues Values(Level, {});
    uint16 Serial = 0;
    for (const FCard& Row : Rows)
    {
        FLDInstalledCardValue& Installed = Values.Cards.AddDefaulted_GetRef();
        Installed.CardId = FName(Row.Id);
        Installed.Serial = ++Serial;
        Installed.TargetKind = Row.Target;
        Installed.Active = Row.Active;
        Installed.Element = Row.Element;
    }
    return LDAttributeProjection::Project(Values, Catalog);
}

// Installs a projection with Rows on a fixture contender at Level (its record's level is what G reads).
bool InstallCards(FAutomationTestBase& Test, FFixture& F, const FLDCombatBodyRef& Ref, uint8 Level, const TArray<FCard>& Rows)
{
    const FLDAttributeProjectionResult Result = ProjectCards(*F.Catalog, Level, Rows);
    if (!Result.IsSuccess())
    {
        Test.AddError(FString::Printf(TEXT("Card projection: %s: %s"), *Result.Error.Field, *Result.Error.Message));
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

// The card triggers of a contender's current build version (0 before its first fact).
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

#endif
namespace LDBuildPrimitivesTests
{
bool ActiveTerms(FAutomationTestBase& Test, FFixture& F, const TCHAR* Active, FLDCombatActiveTerms& Out)
{
    const FLDDemoAbilityDefinition* Ability = nullptr;
    FString Error;
    if (!F.Catalog->FindAbility(Active, Ability, Error) || !Ability) { Test.AddError(FString(TEXT("Missing active ")) + Active); return false; }
    const FLDDemoAbilityValues* Values = F.Catalog->FindValues(*Ability, 1, ELDSkillBranch::None);
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
        const FLDDemoAbilityValues* Values = F.Catalog->FindValues(*Ability, Level, ELDSkillBranch::None);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventProjectileCounterplayTest, "Lockdown.Rules.Combat.Events.ProjectileCounterplay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventProjectileCounterplayTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    TArray<TStrongObjectPtr<UPackage>> RetainedPackages;
    for (const TCHAR* Active : {TEXT("active_snark_bolt"), TEXT("active_sock_shot")})
    {
        for (int32 Case = 0; Case < 3; ++Case)
        {
            FFixture F;
            if (!F.Init(*this, true)) { return false; }
            UPackage* Package = F.World->GetOutermost();
            if (!TestFalse(TEXT("Successive sight worlds never reuse a retained fixture package"),
                RetainedPackages.ContainsByPredicate([Package](const TStrongObjectPtr<UPackage>& Previous)
                    { return Previous.Get() == Package; })))
            { return false; }
            RetainedPackages.Emplace(Package);
            const FLDCombatBodyRef Caster = F.AddBody(), Target = F.AddBody(), Blocker = F.AddBody();
            F.Place(Caster, 0, 0);
            F.Place(Target, 300, 0);
            F.Place(Blocker, Case == 2 ? 150 : 600, Case == 2 ? 0 : 600);
            if (!InstallCards(*this, F, Caster, 1, {})) { return false; }
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
                { TestTrue(TEXT("S21 applies attack slow and rot"), !F.Body(Target).AttackSlows.IsEmpty()
                    && F.Body(Target).ElementMark.Kind == ELDCombatElement::Rot); }
            }
        }
    }
    return true;
}

// R03: a real S02 root and S03 stun carry their active as the control's exit, so C05 marks and consumes and C07 grants.
// R04: B10/B11 water and electric are their component's own applications: C09 on B11 arms on its conduct and M11 on B11 picks
// its discharge target.
// R05 (spec §11.1/§18.1): an S08 pulse carries its native fire and B18's electric in one batch: fire fogs the wet target, the
// fog's 0 s wet lands at once, then the electric conducts. Fire before electric, never the other way round.
// R07: B02 counts the basic, not the reaction or fire settle the same basic set off.
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

// S19 A1 (issue #287 spec §5.1): two armed traps of one owner within the wire distance strike each enemy body on the segment
// between them once with the wire package; a body off the segment, or a pair farther apart than the wire, is not struck.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatSkillRankTrapWireTest, "Lockdown.Rules.Combat.SkillRank.TrapWire",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatSkillRankTrapWireTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef Caster = F.AddBody(), Enemy = F.AddBody(ELDVictimKind::Contender, 10000000);
    F.Place(Caster, 0, -1000);
    F.Place(Enemy, 200, 600);
    TArray<FLDCombatGroundEffect>& Ground = FAccess::Ground(*F.Driver);
    TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    FLDCombatActiveTerms Trap;
    FLDCombatHit Payload;
    F.Now(1000);
    if (!Released(*this, F, Caster, TEXT("active_flyer_trap"), Trap, Payload)) { return false; }
    // Two armed traps 400 cm apart on the X axis, wired at the S19 A1 values (400 cm, 30 + 0.3 AP).
    const auto Armed = [&](double X, uint64 Serial)
    {
        FLDCombatGroundEffect Row;
        Row.Payload = Payload;
        Row.CentreCm = F.Origin + FVector(X, 0, 0);
        Row.RadiusCm = 180;
        Row.InnerRadiusCm = 100;
        Row.bTrap = true;
        Row.Origin = ELDCombatGroundOrigin::Trap;
        Row.ReadyAtMs = 1000;
        Row.ExpiresAtMs = 7000;
        Row.FuseMs = 450;
        Row.WireCm = 400;
        Row.WireBaseP2 = 3000;
        Row.WireAPRatioBp = 3000;
        Row.CreatedSerial = Serial;
        return Row;
    };
    Ground.Add(Armed(0, 1));
    Ground.Add(Armed(400, 2));
    FString Error;
    const auto Advance = [&](int64 AtMs)
    {
        F.Now(AtMs);
        return TestTrue(FString::Printf(TEXT("Skill content at %lld: %s"), AtMs, *Error), FAccess::AdvanceSkillContent(*F.Driver, AtMs, Error));
    };
    if (!Advance(1100)) { return false; }
    TestTrue(TEXT("A body off the wire is not struck"), Pending.IsEmpty() && Ground.Num() == 2);
    // On the segment's middle, outside both trigger radii.
    F.Place(Enemy, 200, 0);
    if (!Advance(1200)) { return false; }
    TestTrue(TEXT("Crossing the wire strikes once with the wire package, magic, without triggering a trap"),
        Pending.Num() == 1 && Pending[0].Target == Enemy && Pending[0].BaseP2 == 3000 && Pending[0].APRatioBp == 3000
        && Pending[0].DamageType == ELDDemoDamageType::Magical && !Ground[0].bTriggered && !Ground[1].bTriggered);
    if (!Advance(1300)) { return false; }
    TestTrue(TEXT("Staying on the wire does not strike again"), Pending.Num() == 1);
    // A pair farther apart than the wire distance has no wire.
    Pending.Reset();
    Ground.Reset();
    Ground.Add(Armed(0, 3));
    Ground.Add(Armed(500, 4));
    F.Place(Enemy, 250, 0);
    if (!Advance(1400)) { return false; }
    TestTrue(TEXT("Traps 500 cm apart are not wired"), Pending.IsEmpty());
    return true;
}

// R09 (spec §9.1/§12.1): a due M12 debt is paid at its original amount through a live S16 stance and feeds no record.
// R10: C10's one answer to a broken shield shares one effect identity over every body it reaches, so C11's 30G on it is one
// budget.
// Spec §13.3: a C08 counter fed by a C11 heal continues that root action, so C11 cannot reopen a new 30G budget for it.
namespace LDBuildPrimitivesTests
{
}

// Two C11 roots that feed one C08 store before it fires merge into one counter, spent once under the latest root;
// no second counter and no fresh root identity appear.
// C11's 30G per root is consumed by the root and its derived same-effect hits; the C08 counter it feeds spends no
// C11 budget and refills nothing, so the loop cannot reopen healing.
// R11 (spec §12.1): M01 spreads S21's damage around the struck body; the cut of heals and shields and the attack slow stay on it.
// S20 (spec §9): its owner and the owner's current-match teammates run faster inside the puddle; enemies and teammates outside do
// not; leaving drops it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatEventZoneHasteTest, "Lockdown.Rules.Combat.Events.ZoneHaste",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatEventZoneHasteTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    constexpr int64 Big = 10000000;
    const FLDCombatBodyRef Owner = F.AddBody(), Other = F.AddBody(ELDVictimKind::Contender, Big);
    F.Place(Owner, 250, 0);
    F.Place(Other, 300, -80);
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
    TestTrue(TEXT("The owner inside is hasted; another contender inside is not"), Hasted(Owner) && !Hasted(Other));
    TestTrue(TEXT("Its pulse still reaches the other contender"), FAccess::PendingHits(*F.Driver).ContainsByPredicate(
        [&Other](const FLDCombatHit& Row) { return Row.Target == Other; }));
    FAccess::PendingHits(*F.Driver).Reset();
    F.Place(Owner, -600, 0);
    F.Now(1100);
    if (!TestTrue(FString::Printf(TEXT("The zone runs on: %s"), *Error), FAccess::AdvanceMechanics(*F.Driver, 1100, Error))) { return false; }
    TestFalse(TEXT("Leaving drops it"), Hasted(Owner));
    return true;
}

// C01 (spec §13.3): fixed 1 s windows from its first install; a window settles after every fire fact up to its boundary;
// an uninstall drops the open window and a reinstall keeps the original boundaries.
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
    Body.Resources.MomentumP2 = 10000;
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

// ---- Connectors C17-C21 (docs/systems/COMBAT_AND_GROWTH.md): the production dash completion, shield, release and
// native-hit entries of a fixture contender's installed build.
namespace LDBuildPrimitivesTests
{

void StartDash(FFixture& F, const FLDCombatBodyRef& Ref, int64 EndMs)
{
    FLDCombatVoluntaryDash& Dash = F.Body(Ref).VoluntaryDash.Emplace();
    Dash.EffectId = FGuid::NewGuid();
    Dash.Source = SourceOf(ELDCombatSourceKind::Innate, TEXT("innate_dash"));
    Dash.StartFootCm = F.Foot(Ref);
    Dash.EndMs = EndMs;
}
}

// C22: a host release's first native direct hit calls the holder's newest unmoved zone to the struck enemy (once per
// release, once per zone life, shared with C17); with C21 on the same host the called zone detonates on that same hit.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDMonsterDeliveryTest,"Lockdown.Rules.DemoCamp.SkillDelivery",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDMonsterDeliveryTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;if(!F.Init(*this,true)){return false;}FString Error;
    const auto Source=F.AddBody(ELDVictimKind::CampMember);
    const auto Target=F.AddBody();
    F.Place(Source,0,0);F.Place(Target,300,0);
    auto& SourceBody=F.Body(Source);SourceBody.MonsterId=TEXT("monster_client");
    SourceBody.AttributeProjection.Final.ADP2=9000;SourceBody.MonsterAttackCount=2;
    auto& Record=FAccess::Records(*F.Driver).Add(Source.Victim.EntityId);
    Record.Kind=ELDVictimKind::CampMember;Record.Entity=Source.Victim.EntityId;Record.MonsterId=SourceBody.MonsterId;
    Record.MonsterLives.Add(1,SourceBody.MonsterId);
    const FLDDemoMonsterDefinition* Definition=nullptr;
    if(!F.Catalog->FindMonster(TEXT("monster_client"),Definition,Error)){AddError(Error);return false;}
    FLDCombatAction Action;Action.Token=41;Action.bMonsterSpecial=true;Action.StartedAtMs=1000;Action.ReleaseAtMs=2000;
    SourceBody.Action=Action;
    const auto Areas=LDMonsterRules::Areas(Definition->Skill,0,F.Foot(Source),F.Foot(Target));
    auto& Strikes=FAccess::MonsterStrikes(*F.Driver);
    for(const auto& Area:Areas)
    {
        FLDMonsterStrike S;S.Key=FGuid::NewGuid();S.Area=Area;S.Token=Action.Token;S.AtMs=2000+Area.DelayMs;
        S.Payload.SourceBody=Source;S.Payload.SourceEntity=Source.Victim.EntityId;Strikes.Add(S);
    }
    TArray<FLDObservedProjectileFact> Warnings;
    auto OriginalStrikes=MoveTemp(Strikes);
    for(const auto& M:F.Catalog->Get().Monsters)
    {
        if(M.Tier==ELDDemoCampTier::Boss || M.Skill.Pattern==ELDMonsterPattern::Enrage){continue;}
        Strikes.Reset();
        for(const auto& Area:LDMonsterRules::Areas(M.Skill,6,F.Foot(Source),F.Foot(Target)))
        {
            FLDMonsterStrike S;S.Key=FGuid::NewGuid();S.Area=Area;S.Token=Action.Token;S.AtMs=2000+Area.DelayMs;
            S.Payload.SourceBody=Source;S.Payload.SourceEntity=Source.Victim.EntityId;Strikes.Add(S);
        }
        if(!F.Driver->ReadObservedProjectiles(Warnings,Error)){AddError(Error);return false;}
        for(const auto& Fact:Warnings)
        {
            FLDObservedProjectileView V;V.Handle=FGuid::NewGuid();V.PositionCm=Fact.PositionCm;V.Look=Fact.Look;
            V.RadiusCm=Fact.RadiusCm;V.AngleDegrees=Fact.AngleDegrees;V.LengthCm=Fact.LengthCm;V.WarningRemainingMs=Fact.WarningRemainingMs;
            if(!TestTrue(*M.Id,LDObservation::ValidateProjectile(V,Error))){AddError(Error);return false;}
            if(Fact.Look==ELDObservedProjectileLook::LineWarning)
            {TestEqual(TEXT("A line authorizes at the caster origin, even when its midpoint is occluded"),Fact.PositionCm,F.Foot(Source));}
        }
    }
    Strikes=MoveTemp(OriginalStrikes);Warnings.Reset();
    TestTrue(TEXT("Pending monster warnings publish on the existing effect channel"),F.Driver->ReadObservedProjectiles(Warnings,Error));
    TestEqual(TEXT("Both delayed warning areas are published"),Warnings.Num(),Areas.Num());
    for(const auto& Fact:Warnings)
    {
        FLDObservedProjectileView View;View.Handle=FGuid::NewGuid();View.PositionCm=Fact.PositionCm;View.Look=Fact.Look;
        View.RadiusCm=Fact.RadiusCm;View.InnerRadiusCm=Fact.InnerRadiusCm;View.AngleDegrees=Fact.AngleDegrees;View.LengthCm=Fact.LengthCm;
        View.WarningRemainingMs=Fact.WarningRemainingMs;
        TestTrue(TEXT("A monster telegraph satisfies the actual observation wire contract"),LDObservation::ValidateProjectile(View,Error));
    }
    const int64 Before=F.Body(Target).Resources.HpP2;
    TestTrue(TEXT("An unexpired warning cannot hurt"),FAccess::MonsterStep(*F.Driver,1999,Error)&&F.Flush(Error));
    TestEqual(TEXT("No damage before release"),F.Body(Target).Resources.HpP2,Before);
    F.Now(2000);F.Place(Target,300,300);ELDCombatReject Reject;
    if(!TestTrue(TEXT("Real monster release accepts its approved pattern"),FAccess::MonsterRelease(*F.Driver,F.Actor(Source),SourceBody,Action,Reject,Error))){AddError(Error);return false;}
    TestEqual(TEXT("Successful special wraps its shared attack loop"),SourceBody.MonsterAttackCount,0);
    TestTrue(TEXT("First pulse uses the locked warning location"),FAccess::MonsterStep(*F.Driver,2000,Error)&&F.Flush(Error));
    TestEqual(TEXT("Moving out of the warned circle avoids damage"),F.Body(Target).Resources.HpP2,Before);
    F.Place(Target,300,0);
    // The caster dies, then its camp changes species. The released pulse retains the old life and definition.
    FAccess::Bodies(*F.Driver).Remove(&F.Actor(Source));
    Record.MonsterId=TEXT("monster_advisor");Record.MonsterLives.Add(2,Record.MonsterId);
    F.Now(2800);
    if(!TestTrue(TEXT("Released second pulse survives caster death"),FAccess::MonsterStep(*F.Driver,2800,Error)&&F.Flush(Error))){AddError(Error);return false;}
    TestEqual(TEXT("Exactly one 1.0 AD pulse lands through Combat"),F.Body(Target).Resources.HpP2,Before-int64(9000));
    TestTrue(TEXT("Damage attribution keeps the releasing species after a refresh"),
        !F.Body(Target).DamageLog.IsEmpty()&&F.Body(Target).DamageLog.Last().Line.MonsterId==FName(TEXT("monster_client")));
    const int64 After=F.Body(Target).Resources.HpP2;
    TestTrue(TEXT("Revisiting the same authority deadline is idempotent"),FAccess::MonsterStep(*F.Driver,2800,Error)&&F.Flush(Error));
    TestEqual(TEXT("No duplicate delayed damage"),F.Body(Target).Resources.HpP2,After);
    FLDMonsterStrike Cancelled;Cancelled.Key=FGuid::NewGuid();Cancelled.Payload.SourceBody=Source;
    Cancelled.Payload.SourceEntity=Source.Victim.EntityId;Cancelled.AtMs=3000;Cancelled.Token=99;Strikes.Add(Cancelled);
    TestTrue(TEXT("An unreleased warning of a retired life is discarded"),FAccess::MonsterStep(*F.Driver,3000,Error)&&Strikes.IsEmpty());
    {
        FFixture Charge;if(!Charge.Init(*this,true)){return false;}
        const auto Runner=Charge.AddBody(ELDVictimKind::CampMember), Victim=Charge.AddBody();
        const auto Peer=Charge.AddBody(ELDVictimKind::CampMember);
        Charge.Place(Runner,0,0);Charge.Place(Victim,300,0);Charge.Place(Peer,120,0);
        auto& RunnerBody=Charge.Body(Runner);RunnerBody.MonsterId=TEXT("monster_courier");
        RunnerBody.AttributeProjection.Final.ADP2=1000;
        if(!Charge.Catalog->FindMonster(TEXT("monster_courier"),Definition,Error)){AddError(Error);return false;}
        const FVector Origin=Charge.Foot(Runner);
        Action.Direction=FVector(1,0,0);RunnerBody.Action=Action;
        for(const auto& Area:LDMonsterRules::Areas(Definition->Skill,0,Origin,Charge.Foot(Victim)))
        {
            FLDMonsterStrike S;S.Key=FGuid::NewGuid();S.Area=Area;S.Token=Action.Token;S.AtMs=2000+Area.DelayMs;
            S.Payload.SourceBody=Runner;S.Payload.SourceEntity=Runner.Victim.EntityId;
            FAccess::MonsterStrikes(*Charge.Driver).Add(S);
        }
        Charge.Now(2000);
        if(!FAccess::MonsterRelease(*Charge.Driver,Charge.Actor(Runner),RunnerBody,Action,Reject,Error)
            || Reject!=ELDCombatReject::None || !RunnerBody.DashContact){AddError(TEXT("Courier release: ")+Error);return false;}
        const int64 VictimHp=Charge.Body(Victim).Resources.HpP2, PeerHp=Charge.Body(Peer).Resources.HpP2;
        TestTrue(TEXT("Releasing a charge cannot instantly hit the full warned line"),
            FAccess::MonsterStep(*Charge.Driver,2000,Error)&&Charge.Flush(Error));
        TestEqual(TEXT("A distant target is untouched at release"),Charge.Body(Victim).Resources.HpP2,VictimHp);
        const auto Motion=[&](double From,double To,int64 Start,int64 End)
        {
            auto Batch=MakeShared<FLDNativeMotionBatch>();Batch->Body=Runner;
            Batch->Step.StartServerSeconds=double(Start)/1000.;Batch->Step.EndServerSeconds=double(End)/1000.;
            Batch->StartFootCm=Origin+FVector(From,0,0);Batch->EndFootCm=Origin+FVector(To,0,0);
            auto& Move=Batch->Records.AddDefaulted_GetRef();Move.Kind=ELDNativeMotionRecordKind::Movement;
            Move.FromFootCm=Batch->StartFootCm;Move.ToFootCm=Batch->EndFootCm;
            Move.StartServerSeconds=Batch->Step.StartServerSeconds;Move.EndServerSeconds=Batch->Step.EndServerSeconds;
            FAccess::CaptureSkillMotion(*Charge.Driver,Batch);
            for(const auto TargetRef:{Victim,Peer})
            {
                auto Still=MakeShared<FLDNativeMotionBatch>();Still->Body=TargetRef;Still->Step=Batch->Step;
                Still->StartFootCm=Still->EndFootCm=Charge.Foot(TargetRef);
                FAccess::CaptureSkillMotion(*Charge.Driver,Still);
            }
            Charge.Now(End);
            return FAccess::ResolveDashContacts(*Charge.Driver,End,Error)&&Charge.Flush(Error);
        };
        TestTrue(TEXT("A terrain-clipped short prefix does not reach the distant target"),Motion(0,100,2000,2200));
        TestEqual(TEXT("No damage beyond the traveled prefix"),Charge.Body(Victim).Resources.HpP2,VictimHp);
        TestTrue(TEXT("A later real swept contact lands"),Motion(100,400,2200,2800));
        TestEqual(TEXT("The courier's native contact delivers one 1.5 AD package"),Charge.Body(Victim).Resources.HpP2,VictimHp-int64(1500));
        TestEqual(TEXT("The same charge cannot damage another camp member"),Charge.Body(Peer).Resources.HpP2,PeerHp);
        TestTrue(TEXT("Repeated native contact is deduplicated per body life"),Motion(100,400,2200,2800));
        TestEqual(TEXT("No duplicate contact damage"),Charge.Body(Victim).Resources.HpP2,VictimHp-int64(1500));
    }
    {
        FFixture Shot;if(!Shot.Init(*this,true)){return false;}
        const auto Agent=Shot.AddBody(ELDVictimKind::CampMember), Peer=Shot.AddBody(ELDVictimKind::CampMember), Victim=Shot.AddBody();
        Shot.Place(Agent,0,0);Shot.Place(Peer,120,0);Shot.Place(Victim,300,0);
        auto& AgentBody=Shot.Body(Agent);AgentBody.MonsterId=TEXT("monster_agent");AgentBody.AttributeProjection.Final.ADP2=3600;
        if(!Shot.Catalog->FindMonster(TEXT("monster_agent"),Definition,Error)){AddError(Error);return false;}
        AgentBody.Action=Action;
        for(const auto& Area:LDMonsterRules::Areas(Definition->Skill,0,Shot.Foot(Agent),Shot.Foot(Victim)))
        {
            FLDMonsterStrike S;S.Key=FGuid::NewGuid();S.Area=Area;S.Token=Action.Token;S.AtMs=2000+Area.DelayMs;
            S.Payload.SourceBody=Agent;S.Payload.SourceEntity=Agent.Victim.EntityId;FAccess::MonsterStrikes(*Shot.Driver).Add(S);
        }
        Shot.Now(2000);
        if(!FAccess::MonsterRelease(*Shot.Driver,Shot.Actor(Agent),AgentBody,Action,Reject,Error)
            || Reject!=ELDCombatReject::None || !FAccess::MonsterStep(*Shot.Driver,2000,Error)){AddError(Error);return false;}
        const int64 PeerHp=Shot.Body(Peer).Resources.HpP2, VictimHp=Shot.Body(Victim).Resources.HpP2;
        Shot.Now(2500);
        if(!FAccess::AdvanceProjectiles(*Shot.Driver,Error)||!Shot.Flush(Error)){AddError(Error);return false;}
        TestEqual(TEXT("A camp projectile passes its peer without causing damage"),Shot.Body(Peer).Resources.HpP2,PeerHp);
        TestEqual(TEXT("The existing projectile path delivers the approved 1.5 AD to a contender"),Shot.Body(Victim).Resources.HpP2,VictimHp-int64(5400));
    }
    {
        FFixture Sweep;if(!Sweep.Init(*this,true)){return false;}
        const auto Auditor=Sweep.AddBody(ELDVictimKind::CampMember), Victim=Sweep.AddBody();
        Sweep.Place(Auditor,0,0);Sweep.Place(Victim,300,0);
        auto& AuditorBody=Sweep.Body(Auditor);AuditorBody.MonsterId=TEXT("monster_auditor");AuditorBody.AttributeProjection.Final.ADP2=9000;
        Sweep.Body(Victim).AttributeProjection.Final.ArmorP2=0;Sweep.Body(Victim).AttributeProjection.Final.MagicResistP2=10000;
        if(!Sweep.Catalog->FindMonster(TEXT("monster_auditor"),Definition,Error)){AddError(Error);return false;}
        AuditorBody.Action=Action;
        for(const auto& Area:LDMonsterRules::Areas(Definition->Skill,0,Sweep.Foot(Auditor),Sweep.Foot(Victim)))
        {
            FLDMonsterStrike S;S.Key=FGuid::NewGuid();S.Area=Area;S.Token=Action.Token;S.AtMs=2000+Area.DelayMs;
            S.Payload.SourceBody=Auditor;S.Payload.SourceEntity=Auditor.Victim.EntityId;FAccess::MonsterStrikes(*Sweep.Driver).Add(S);
        }
        Sweep.Now(2000);
        if(!FAccess::MonsterRelease(*Sweep.Driver,Sweep.Actor(Auditor),AuditorBody,Action,Reject,Error)
            || Reject!=ELDCombatReject::None){AddError(Error);return false;}
        const int64 Hp=Sweep.Body(Victim).Resources.HpP2;Sweep.Now(2500);
        if(!FAccess::MonsterStep(*Sweep.Driver,2500,Error)||!Sweep.Flush(Error)){AddError(Error);return false;}
        TestEqual(TEXT("The auditor's middle sweep deals its physical package despite the target's magic resistance"),Sweep.Body(Victim).Resources.HpP2,Hp-int64(6750));
    }
    return true;
}

// ---- Issue #287 P1: overdrive, perfect dodge and branch riders on Combat ----

namespace LDBuildPrimitivesTests
{
// The level-1 terms of Active with its overdrive applied by Combat for Holder.
bool OverdriveTerms(FAutomationTestBase& Test, FFixture& F, const FLDCombatBodyRef& Holder, const TCHAR* Active, FLDCombatActiveTerms& Plain,
    FLDCombatActiveTerms& Over)
{
    const FLDDemoAbilityDefinition* Ability = nullptr;
    FString Error;
    if (!ActiveTerms(Test, F, Active, Plain) || !F.Catalog->FindAbility(Active, Ability, Error)) { return false; }
    Over = Plain;
    return Test.TestTrue(FString::Printf(TEXT("%s overdrives: %s"), Active, *Error), FAccess::Overdrive(*F.Driver, F.Body(Holder), *Ability, Over, Error));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDOverdriveReleaseTest, "Lockdown.Rules.Combat.Overdrive.Release",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDOverdriveReleaseTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody();
    FLDCombatActiveTerms Plain, Over;
    if (!OverdriveTerms(*this, F, Holder, TEXT("active_elbow"), Plain, Over)) { return false; }
    const FLDDemoEffectDefinition* Base = FindEffect(Plain.Values, ELDDemoEffectOp::Damage);
    const FLDDemoEffectDefinition* Scaled = FindEffect(Over.Values, ELDDemoEffectOp::Damage);
    TestTrue(TEXT("S01's base and AD coefficient are 1.6 times"), Base && Scaled && Scaled->BaseP2 == Base->BaseP2 * 16 / 10
        && Scaled->ADRatioBp == Base->ADRatioBp * 16 / 10);
    TestTrue(TEXT("its push distance and time do not scale"), FindEffect(Over.Values, ELDDemoEffectOp::Knockback)->DistanceCm.Numerator
        == FindEffect(Plain.Values, ELDDemoEffectOp::Knockback)->DistanceCm.Numerator
        && FindEffect(Over.Values, ELDDemoEffectOp::Knockback)->DurationMs == FindEffect(Plain.Values, ELDDemoEffectOp::Knockback)->DurationMs);
    if (!OverdriveTerms(*this, F, Holder, TEXT("active_pants_down"), Plain, Over)) { return false; }
    TestEqual(TEXT("A control's duration never scales"), FindEffect(Over.Values, ELDDemoEffectOp::Root)->DurationMs,
        FindEffect(Plain.Values, ELDDemoEffectOp::Root)->DurationMs);
    if (!OverdriveTerms(*this, F, Holder, TEXT("active_not_mad"), Plain, Over)) { return false; }
    TestTrue(TEXT("A shield's base, AP and max-HP shares are 1.6 times"), FindEffect(Over.Values, ELDDemoEffectOp::Shield)->BaseP2
        == FindEffect(Plain.Values, ELDDemoEffectOp::Shield)->BaseP2 * 16 / 10
        && FindEffect(Over.Values, ELDDemoEffectOp::Shield)->MaxHPRatioBp == FindEffect(Plain.Values, ELDDemoEffectOp::Shield)->MaxHPRatioBp * 16 / 10);
    TestTrue(TEXT("Only a bar at the approved 60 momentum may overdrive"), !LDMomentum::OverdriveReady(F.Catalog->Get().Parameters.Overdrive, 5999)
        && LDMomentum::OverdriveReady(F.Catalog->Get().Parameters.Overdrive, 6000));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDOverdriveElementTest, "Lockdown.Rules.Combat.Overdrive.ElementBonus",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDOverdriveElementTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody();
    const FLDDemoOverdriveParameters& O = F.Catalog->Get().Parameters.Overdrive;
    FLDCombatActiveTerms Plain, Over;
    if (!OverdriveTerms(*this, F, Holder, TEXT("active_seethe"), Plain, Over)) { return false; }
    TestEqual(TEXT("Fire (S07) adds three burn layers"), FindEffect(Over.Values, ELDDemoEffectOp::Burn)->Count,
        FindEffect(Plain.Values, ELDDemoEffectOp::Burn)->Count + int32(O.FireBurnStacks));
    if (!OverdriveTerms(*this, F, Holder, TEXT("active_hose_down"), Plain, Over)) { return false; }
    const FLDDemoEffectDefinition* Slow = FindEffect(Over.Values, ELDDemoEffectOp::Slow);
    TestTrue(TEXT("Water (S09) slows 45% for 1.5 s"), Slow && Slow->RatioBp == O.WaterSlowBp && Slow->DurationMs == O.WaterSlowMs);
    if (!OverdriveTerms(*this, F, Holder, TEXT("active_zap"), Plain, Over)) { return false; }
    TestTrue(TEXT("Shock (S10) discharges once more: its base gains 0.5 R on top of the 1.6 scale"),
        FindEffect(Over.Values, ELDDemoEffectOp::Damage)->BaseP2 > FindEffect(Plain.Values, ELDDemoEffectOp::Damage)->BaseP2 * 16 / 10);
    if (!OverdriveTerms(*this, F, Holder, TEXT("active_sock_shot"), Plain, Over)) { return false; }
    TestEqual(TEXT("Rot (S21) lasts twice the approved rot"), FindEffect(Over.Values, ELDDemoEffectOp::Rot)->DurationMs,
        F.Catalog->Get().Parameters.Elements.RotMs * 2);
    if (!OverdriveTerms(*this, F, Holder, TEXT("active_cola_shake"), Plain, Over)) { return false; }
    const FLDDemoEffectDefinition* Root = FindEffect(Over.Values, ELDDemoEffectOp::Root);
    TestTrue(TEXT("Chill (N1) roots for 0.6 s"), Root && Root->DurationMs == O.ChillRootMs && !FindEffect(Plain.Values, ELDDemoEffectOp::Root));
    if (!OverdriveTerms(*this, F, Holder, TEXT("active_elbow"), Plain, Over)) { return false; }
    TestFalse(TEXT("A skill without an element gains only the scale"),
        FindEffect(Over.Values, ELDDemoEffectOp::Burn) || FindEffect(Over.Values, ELDDemoEffectOp::Slow));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPerfectDodgeResolveTest, "Lockdown.Rules.Combat.PerfectDodge.Resolve",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDPerfectDodgeResolveTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Caster = F.AddBody(), Dodger = F.AddBody();
    F.Place(Dodger, 600, 0);
    FLDBodyCombatState& Body = F.Body(Dodger);
    Body.Resources.MomentumP2 = 1000;
    F.Now(2000);
    // The dodger's D started at 1800 inside a 200 cm circle at the origin; it now stands 600 cm away.
    Body.DashStartedAtMs = 1800;
    Body.DashStartFootCm = FVector(50, 0, F.Foot(Dodger).Z);
    const auto InCircle = [](const FVector& Point) { return FVector::Dist2D(Point, FVector::ZeroVector) <= 200.; };
    const FGuid Effect = FGuid::NewGuid();
    FString Error;
    TestTrue(TEXT("The resolution judges"), FAccess::PerfectDodges(*F.Driver, Effect, Caster.Victim.EntityId, InCircle, 2000, Error));
    TestTrue(TEXT("A perfect dodge gains 15 momentum and is counted"), Body.Resources.MomentumP2 == 2500 && Body.PerfectDodges == 1
        && Body.LastPerfectDodgeAtMs == 2000);
    TestTrue(TEXT("The same effect never pays twice"), FAccess::PerfectDodges(*F.Driver, Effect, Caster.Victim.EntityId, InCircle, 2000, Error)
        && Body.Resources.MomentumP2 == 2500);
    TestTrue(TEXT("The caster never dodges its own effect"), FAccess::PerfectDodges(*F.Driver, FGuid::NewGuid(), Dodger.Victim.EntityId,
        InCircle, 2000, Error) && Body.Resources.MomentumP2 == 2500);
    TestTrue(TEXT("A dash older than the window is no perfect dodge"), FAccess::PerfectDodges(*F.Driver, FGuid::NewGuid(),
        Caster.Victim.EntityId, InCircle, 2301, Error) && Body.Resources.MomentumP2 == 2500);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDSkillRankRidersTest, "Lockdown.Rules.Combat.SkillRank.Riders",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDSkillRankRidersTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef Holder = F.AddBody(), Target = F.AddBody(ELDVictimKind::Contender, 10000000);
    FLDCombatHit Payload;
    if (!SnarkBolt(*this, F, Holder, Target, Payload)) { return false; }
    // S01 B2's rider on this release: the struck body takes 20% more for 3 s; S04 B2's: 10 momentum per body.
    FLDDemoEffectDefinition Vulnerable;
    Vulnerable.Opcode = uint8(ELDDemoEffectOp::Vulnerable);
    Vulnerable.RatioBp = 2000;
    Vulnerable.DurationMs = 3000;
    FLDDemoEffectDefinition Gain;
    Gain.Opcode = uint8(ELDDemoEffectOp::MomentumGain);
    Gain.BaseP2 = 1000;
    Payload.CastLedger->Riders = {Vulnerable, Gain};
    FLDBodyCombatState& Body = F.Body(Holder);
    Body.Resources.MomentumP2 = 0;
    if (!Land(*this, F, Payload)) { return false; }
    const int64 Hit = F.Catalog->Get().Parameters.Momentum.SkillHitP2;
    TestEqual(TEXT("The rider grants its momentum beside the skill hit"), Body.Resources.MomentumP2, 1000 + Hit);
    FLDCombatHit Probe;
    Probe.SourceEntity = FGuid::NewGuid();
    Probe.HitClass = ELDCombatHitClass::BasicPrimary;
    TestEqual(TEXT("The struck body is vulnerable to everyone for 3 s"), FAccess::Vulnerable(*F.Driver, F.Body(Target), Probe,
        F.Mode->GetAuthorityNowMs()), int64(2000));
    TestEqual(TEXT("and no longer once it ran out"), FAccess::Vulnerable(*F.Driver, F.Body(Target), Probe,
        F.Mode->GetAuthorityNowMs() + 3000), int64(0));
    FLDCombatHit Again = Payload;
    Again.HitId = FGuid::NewGuid();
    if (!Land(*this, F, Again)) { return false; }
    TestEqual(TEXT("Another hit of the release grants the per-hit rider again, never the skill hit"), Body.Resources.MomentumP2, 2000 + Hit);
    TestEqual(TEXT("A once-per-body rider does not stack its vulnerability"), F.Body(Target).Branch.Vulnerabilities.Num(), 1);
    return true;
}


// ---- Spec §7 P2 cards: one rule test per card (Lockdown.Rules.Combat.Cards.<code>). Each installs two copies of the card
// on its form's place in a real Combat director, checks the projection takes them and Combat collects each copy as its own
// effect under the card's factor key (copies stack), then checks the card's own compiled or committed result where Combat
// exposes it without a match (terms, projection, a committed hit).
namespace LDBuildPrimitivesTests
{
const TCHAR* const CardHost = TEXT("active_elbow");

struct FCardRule
{
    const TCHAR* Id = nullptr;
    ELDDemoEffectOp Op = ELDDemoEffectOp::Damage;
    ELDCardTargetKind Target = ELDCardTargetKind::Body;
    ELDDemoElement Element = ELDDemoElement::None;
};

// Installs Copies copies of Rule on Holder at level 6 and checks the projection and the collected effects.
bool InstallRule(FAutomationTestBase& Test, FFixture& F, const FLDCombatBodyRef& Holder, const FCardRule& Rule, int32 Copies)
{
    TArray<FCard> Rows;
    for (int32 Index = 0; Index < Copies; ++Index)
    {
        Rows.Add({Rule.Id, Rule.Target, Rule.Target == ELDCardTargetKind::Skill ? FName(CardHost) : NAME_None, Rule.Element});
    }
    if (!InstallCards(Test, F, Holder, 6, Rows)) { return false; }
    TArray<FLDPassiveEffectRef> Refs;
    FString Error;
    if (!Test.TestTrue(FString::Printf(TEXT("%s: Combat collects its effects (%s)"), Rule.Id, *Error),
        FAccess::Collect(*F.Driver, F.Body(Holder).AttributeProjection, Rule.Op, Refs, Error)))
    {
        return false;
    }
    TSet<uint16> Serials;
    for (const FLDPassiveEffectRef& Ref : Refs) { Serials.Add(Ref.Serial); }
    return Test.TestTrue(FString::Printf(TEXT("%s: each of %d copies is its own effect under the card's factor key, on its place"), Rule.Id, Copies),
        Refs.Num() == Copies && Serials.Num() == Copies && Algo::AllOf(Refs, [&Rule](const FLDPassiveEffectRef& Ref)
        {
            return Ref.Key == FName(Rule.Id) && Ref.Source.Kind == ELDCombatSourceKind::Card && Ref.Source.DefinitionId == FName(Rule.Id)
                && Ref.TargetKind == Rule.Target && (Rule.Target != ELDCardTargetKind::Skill || Ref.Active == FName(CardHost));
        }));
}

// The host skill's terms compiled with Holder's cards.
bool HostTerms(FAutomationTestBase& Test, FFixture& F, const FLDCombatBodyRef& Holder, FLDCombatActiveTerms& Out)
{
    FString Error;
    if (!ActiveTerms(Test, F, CardHost, Out)) { return false; }
    return Test.TestTrue(TEXT("The host skill compiles: ") + Error,
        FAccess::Compile(*F.Driver, F.Body(Holder).AttributeProjection, SourceOf(ELDCombatSourceKind::Learned, CardHost), Out, Error));
}

// The installed rule plus Check(F, Holder) on one copy and on two.
bool RunCardRule(FAutomationTestBase& Test, const FCardRule& Rule,
    TFunction<bool(FFixture&, const FLDCombatBodyRef&, int32)> Check = nullptr)
{
    bool bGood = true;
    for (const int32 Copies : {1, 2})
    {
        FFixture F;
        if (!F.Init(Test)) { return false; }
        const FLDCombatBodyRef Holder = F.AddBody();
        if (!InstallRule(Test, F, Holder, Rule, Copies)) { return false; }
        if (Check) { bGood &= Check(F, Holder, Copies); }
    }
    return bGood;
}
}

// The basic attack's cards (installed on the body).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleA01Test, "Lockdown.Rules.Combat.Cards.A01",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleA01Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_third_breath"), ELDDemoEffectOp::ComboMomentum, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleA02Test, "Lockdown.Rules.Combat.Cards.A02",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleA02Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_punch_think"), ELDDemoEffectOp::BasicCooldownCut, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleA03Test, "Lockdown.Rules.Combat.Cards.A03",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleA03Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_solid_punch"), ELDDemoEffectOp::ComboSkillHit, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleA04Test, "Lockdown.Rules.Combat.Cards.A04",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleA04Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_third_shove"), ELDDemoEffectOp::ComboKnockback, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleA06Test, "Lockdown.Rules.Combat.Cards.A06",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleA06Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_both_hands"), ELDDemoEffectOp::ComboDouble, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleA09Test, "Lockdown.Rules.Combat.Cards.A09",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleA09Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_quick_hands"), ELDDemoEffectOp::OvercapExtraHit, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleA11Test, "Lockdown.Rules.Combat.Cards.A11",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleA11Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_scholar_punch"), ELDDemoEffectOp::BasicApRider, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleA12Test, "Lockdown.Rules.Combat.Cards.A12",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleA12Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_heated_up"), ELDDemoEffectOp::FocusAttackSpeed, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleA14Test, "Lockdown.Rules.Combat.Cards.A14",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleA14Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_breach_fist"), ELDDemoEffectOp::Breach, ELDCardTargetKind::Body});
}
// The skill cards (on the host skill).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleK01Test, "Lockdown.Rules.Combat.Cards.K01",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleK01Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_next_up"), ELDDemoEffectOp::HitHasteOther, ELDCardTargetKind::Skill});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleK02Test, "Lockdown.Rules.Combat.Cards.K02",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleK02Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_pay_back"), ELDDemoEffectOp::SkillHitMomentum, ELDCardTargetKind::Skill});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleK04Test, "Lockdown.Rules.Combat.Cards.K04",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleK04Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_setup_line"), ELDDemoEffectOp::SetupMark, ELDCardTargetKind::Skill});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleK06Test, "Lockdown.Rules.Combat.Cards.K06",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleK06Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_encore"), ELDDemoEffectOp::AutoRecast, ELDCardTargetKind::Skill}, [this](FFixture& F, const FLDCombatBodyRef& Holder, int32 Copies)
{
    FLDCombatActiveTerms Terms;
    return HostTerms(*this, F, Holder, Terms) && TestTrue(TEXT("K06: one repeat per copy, 0.6 s later at 60%"),
        Terms.AutoRecasts == Copies && Terms.AutoRecastDelayMs == 600 && Terms.AutoRecastScaleBp == 6000);
});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleK08Test, "Lockdown.Rules.Combat.Cards.K08",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleK08Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_again_now"), ELDDemoEffectOp::CooldownCost, ELDCardTargetKind::Skill}, [this](FFixture& F, const FLDCombatBodyRef& Holder, int32 Copies)
{
    FLDCombatActiveTerms Plain, Terms;
    if (!ActiveTerms(*this, F, CardHost, Plain) || !HostTerms(*this, F, Holder, Terms)) { return false; }
    // Copies add into one multiplier: x(1 - 0.35 n) on the cooldown, x(1 + 0.5 n) on the cost.
    const int64 Cooldown = Plain.Values.CooldownMs * (10000 - 3500 * Copies) / 10000;
    const int64 Cost = Plain.Values.MomentumCostP2 * (10000 + 5000 * Copies) / 10000;
    return TestTrue(FString::Printf(TEXT("K08 x%d: cooldown %lld -> %lld (want %lld), cost %lld -> %lld (want %lld)"), Copies,
        Plain.Values.CooldownMs, Terms.Values.CooldownMs, Cooldown, Plain.Values.MomentumCostP2, Terms.Values.MomentumCostP2, Cost),
        FMath::Abs(Terms.Values.CooldownMs - Cooldown) <= 1 && FMath::Abs(Terms.Values.MomentumCostP2 - Cost) <= 1);
});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleK09Test, "Lockdown.Rules.Combat.Cards.K09",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleK09Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_big_one"), ELDDemoEffectOp::CooldownCost, ELDCardTargetKind::Skill}, [this](FFixture& F, const FLDCombatBodyRef& Holder, int32 Copies)
{
    FLDCombatActiveTerms Plain, Terms;
    if (!ActiveTerms(*this, F, CardHost, Plain) || !HostTerms(*this, F, Holder, Terms)) { return false; }
    TArray<FLDPassiveEffectRef> Factors;
    FString Error;
    FAccess::Collect(*F.Driver, F.Body(Holder).AttributeProjection, ELDDemoEffectOp::SkillDamageFactor, Factors, Error);
    const int64 Cooldown = Plain.Values.CooldownMs * (10000 + 5000 * Copies) / 10000;
    return TestTrue(TEXT("K09: cooldown +50% per copy and one x1.8 factor per copy under the card's key"),
        FMath::Abs(Terms.Values.CooldownMs - Cooldown) <= 1 && Factors.Num() == Copies
        && Algo::AllOf(Factors, [](const FLDPassiveEffectRef& Ref) { return Ref.Key == FName(TEXT("card_big_one")) && Ref.Effect.RatioBp == 8000; }));
});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleK12Test, "Lockdown.Rules.Combat.Cards.K12",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleK12Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_blood_price"), ELDDemoEffectOp::HpCost, ELDCardTargetKind::Skill}, [this](FFixture& F, const FLDCombatBodyRef& Holder, int32 Copies)
{
    FLDCombatActiveTerms Terms;
    return HostTerms(*this, F, Holder, Terms) && TestTrue(TEXT("K12: the skill pays 0.25% max HP per momentum instead"),
        Terms.HPCostBpPerMomentum == 25 * Copies);
});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleK13Test, "Lockdown.Rules.Combat.Cards.K13",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleK13Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_lend_fist"), ELDDemoEffectOp::SkillAdRider, ELDCardTargetKind::Skill});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleK15Test, "Lockdown.Rules.Combat.Cards.K15",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleK15Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_hurt_heal"), ELDDemoEffectOp::SkillLifesteal, ELDCardTargetKind::Skill});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleK16Test, "Lockdown.Rules.Combat.Cards.K16",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleK16Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_held_shot"), ELDDemoEffectOp::IdleReadyAmp, ELDCardTargetKind::Skill});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleK17Test, "Lockdown.Rules.Combat.Cards.K17",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleK17Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_practice"), ELDDemoEffectOp::SkillGrowth, ELDCardTargetKind::Skill});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleK25Test, "Lockdown.Rules.Combat.Cards.K25",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleK25Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_blast_reload"), ELDDemoEffectOp::ReactionHaste, ELDCardTargetKind::Skill});
}

// The dash cards.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleD01Test, "Lockdown.Rules.Combat.Cards.D01",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleD01Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_bump_reset"), ELDDemoEffectOp::DashContactReset, ELDCardTargetKind::Dash});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleD02Test, "Lockdown.Rules.Combat.Cards.D02",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleD02Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_bump_again"), ELDDemoEffectOp::HitHasteOther, ELDCardTargetKind::Dash});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleD03Test, "Lockdown.Rules.Combat.Cards.D03",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleD03Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_dash_punch"), ELDDemoEffectOp::EmpowerBasic, ELDCardTargetKind::Dash});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleD05Test, "Lockdown.Rules.Combat.Cards.D05",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleD05Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_double_dash"), ELDDemoEffectOp::DashSplit, ELDCardTargetKind::Dash});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleD06Test, "Lockdown.Rules.Combat.Cards.D06",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleD06Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_pass_through"), ELDDemoEffectOp::DashPhase, ELDCardTargetKind::Dash});
}
// The body cards.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY01Test, "Lockdown.Rules.Combat.Cards.Y01",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY01Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_spiky_hide"), ELDDemoEffectOp::Thorns, ELDCardTargetKind::Body}, [this](FFixture& F, const FLDCombatBodyRef& Holder, int32 Copies)
{
    // A direct hit on the holder queues one magic reflection to the attacker at 25% per copy of the damage it dealt.
    const FLDCombatBodyRef Attacker = F.AddBody();
    FString Error;
    FAccess::PendingHits(*F.Driver).Reset();
    const int64 Before = F.Body(Holder).Resources.HpP2;
    if (!TestTrue(TEXT("Y01: the hit applies: ") + Error, F.Apply(FFixture::Hit(Attacker, Holder, 20000), Error))) { return false; }
    const int64 Dealt = Before - F.Body(Holder).Resources.HpP2;
    const TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    const FLDCombatHit* Reflect = Pending.FindByPredicate([](const FLDCombatHit& Hit) { return Hit.bReflect; });
    bool bGood = TestTrue(FString::Printf(TEXT("Y01 x%d: one magic reflection of 25%% per copy to the attacker"), Copies),
        Reflect && Reflect->Target.Victim == Attacker.Victim && Reflect->DamageType == ELDDemoDamageType::Magical
        && Reflect->BaseP2 == Dealt * 2500 * Copies / 10000);
    // A reflection is never reflected back.
    if (Reflect)
    {
        const FLDCombatHit Back = *Reflect;
        FAccess::PendingHits(*F.Driver).Reset();
        bGood &= TestTrue(TEXT("Y01: the reflection applies: ") + Error, F.Apply(Back, Error))
            && TestFalse(TEXT("Y01: a reflection does not reflect"), FAccess::PendingHits(*F.Driver).ContainsByPredicate(
                [](const FLDCombatHit& Hit) { return Hit.bReflect; }));
    }
    return bGood;
});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY02Test, "Lockdown.Rules.Combat.Cards.Y02",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY02Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_takedown"), ELDDemoEffectOp::TakedownReset, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY03Test, "Lockdown.Rules.Combat.Cards.Y03",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY03Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_hang_on"), ELDDemoEffectOp::LastStand, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY04Test, "Lockdown.Rules.Combat.Cards.Y04",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY04Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_reaction_refund"), ELDDemoEffectOp::ReactionMomentum, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY05Test, "Lockdown.Rules.Combat.Cards.Y05",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY05Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_wave_after"), ELDDemoEffectOp::ReactionElement, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY07Test, "Lockdown.Rules.Combat.Cards.Y07",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY07Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_hard_skin"), ELDDemoEffectOp::TriggeredShield, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY08Test, "Lockdown.Rules.Combat.Cards.Y08",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY08Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_ball_kick"), ELDDemoEffectOp::KnockbackImpact, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY09Test, "Lockdown.Rules.Combat.Cards.Y09",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY09Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_skill_crit"), ELDDemoEffectOp::SkillCrit, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY10Test, "Lockdown.Rules.Combat.Cards.Y10",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY10Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_skill_drain"), ELDDemoEffectOp::LifestealSkills, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY13Test, "Lockdown.Rules.Combat.Cards.Y13",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY13Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_thick_hide"), ELDDemoEffectOp::ArmorToHp, ELDCardTargetKind::Body}, [this](FFixture& F, const FLDCombatBodyRef& Holder, int32 Copies)
{
    const FLDAttributeProjectionResult Plain = ProjectCards(*F.Catalog, 6, {});
    const FLDAttributeProjection& With = F.Body(Holder).AttributeProjection;
    if (!TestTrue(TEXT("Y13: the bare build projects"), Plain.IsSuccess())) { return false; }
    // Six max HP per armor point per copy (P2 units on both sides).
    const int64 Expected = Plain.Value->Final.MaxHPP2 + Plain.Value->Final.ArmorP2 * 6 * Copies;
    return TestTrue(FString::Printf(TEXT("Y13 x%d: max HP %lld -> %lld (want %lld)"), Copies, Plain.Value->Final.MaxHPP2,
        With.Final.MaxHPP2, Expected), FMath::Abs(With.Final.MaxHPP2 - Expected) <= Copies);
});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY14Test, "Lockdown.Rules.Combat.Cards.Y14",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY14Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_overfed"), ELDDemoEffectOp::OverhealShield, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY15Test, "Lockdown.Rules.Combat.Cards.Y15",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY15Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_berserk"), ELDDemoEffectOp::MissingHpAttackSpeed, ELDCardTargetKind::Body}, [this](FFixture& F, const FLDCombatBodyRef& Holder, int32 Copies)
{
    return TestEqual(TEXT("Y15: +1% attack speed per missing 1% HP per copy"),
        F.Body(Holder).AttributeProjection.MissingHPAttackSpeedBp, int64(100 * Copies));
});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY16Test, "Lockdown.Rules.Combat.Cards.Y16",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY16Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_quick_brain"), ELDDemoEffectOp::HasteMomentum, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY17Test, "Lockdown.Rules.Combat.Cards.Y17",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY17Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_punch_heal"), ELDDemoEffectOp::Lifesteal, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY19Test, "Lockdown.Rules.Combat.Cards.Y19",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY19Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_grow_fat"), ELDDemoEffectOp::HealGrowth, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY20Test, "Lockdown.Rules.Combat.Cards.Y20",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY20Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_kill_streak"), ELDDemoEffectOp::KillGrowth, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY21Test, "Lockdown.Rules.Combat.Cards.Y21",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY21Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_tough_skin"), ELDDemoEffectOp::ControlGrowth, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY22Test, "Lockdown.Rules.Combat.Cards.Y22",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY22Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_charge_up"), ELDDemoEffectOp::IdleCharge, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY25Test, "Lockdown.Rules.Combat.Cards.Y25",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY25Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    // Y25 carries the element chosen at install; the projection refuses it without one.
    bool bGood = RunCardRule(*this, {TEXT("card_live_wire"), ELDDemoEffectOp::ElementAura, ELDCardTargetKind::Body, ELDDemoElement::Water},
        [this](FFixture& F, const FLDCombatBodyRef& Holder, int32)
        {
            TArray<FLDPassiveEffectRef> Refs;
            FString Error;
            FAccess::Collect(*F.Driver, F.Body(Holder).AttributeProjection, ELDDemoEffectOp::ElementAura, Refs, Error);
            return TestTrue(TEXT("Y25: the aura carries the chosen element"),
                Algo::AllOf(Refs, [](const FLDPassiveEffectRef& Ref) { return Ref.Element == ELDDemoElement::Water; }));
        });
    FFixture F;
    if (!F.Init(*this)) { return false; }
    bGood &= TestFalse(TEXT("Y25 without a chosen element is refused"),
        ProjectCards(*F.Catalog, 6, {{TEXT("card_live_wire"), ELDCardTargetKind::Body}}).IsSuccess());
    return bGood;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY26Test, "Lockdown.Rules.Combat.Cards.Y26",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY26Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_long_soak"), ELDDemoEffectOp::ElementDuration, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY27Test, "Lockdown.Rules.Combat.Cards.Y27",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY27Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_louder_blast"), ELDDemoEffectOp::ReactionFactor, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY28Test, "Lockdown.Rules.Combat.Cards.Y28",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY28Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_endless_burn"), ELDDemoEffectOp::BurnCap, ELDCardTargetKind::Body});
}

// ---- Spec §3.7 multiplicative damage and §3.8 chain guard ----
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDamageFactorProductTest, "Lockdown.Rules.Combat.Damage.FactorProduct",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDamageFactorProductTest::RunTest(const FString& Parameters)
{
    int64 Bp = 0;
    bool bCapped = false;
    FString Error;
    // Different sources multiply: x1.8 and x1.35 is x2.43.
    bool bGood = TestTrue(TEXT("Different keys multiply"), LDTactical::DamageFactorProduct({{TEXT("card_big_one"), 8000},
        {TEXT("card_setup_line"), 3500}}, 10000000, Bp, bCapped, Error) && Bp == 24300 && !bCapped);
    bGood &= TestTrue(TEXT("No factor is x1"), LDTactical::DamageFactorProduct({}, 10000000, Bp, bCapped, Error) && Bp == 10000);
    bGood &= TestFalse(TEXT("A negative factor is refused"), LDTactical::DamageFactorProduct({{TEXT("bad"), -1}}, 10000000, Bp, bCapped, Error));
    bGood &= TestFalse(TEXT("An unnamed factor is refused"), LDTactical::DamageFactorProduct({{NAME_None, 100}}, 10000000, Bp, bCapped, Error));
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDamageSameCardAddsTest, "Lockdown.Rules.Combat.Damage.SameCardAdds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDamageSameCardAddsTest::RunTest(const FString& Parameters)
{
    int64 Bp = 0;
    bool bCapped = false;
    FString Error;
    // Two copies of one card add into one multiplier (1 + 0.8 + 0.8 = x2.6), not x1.8 x1.8 = x3.24.
    bool bGood = TestTrue(TEXT("Copies of one card add"), LDTactical::DamageFactorProduct({{TEXT("card_big_one"), 8000},
        {TEXT("card_big_one"), 8000}}, 10000000, Bp, bCapped, Error) && Bp == 26000);
    bGood &= TestTrue(TEXT("Grouping ignores order"), LDTactical::DamageFactorProduct({{TEXT("card_big_one"), 8000},
        {TEXT("card_setup_line"), 3500}, {TEXT("card_big_one"), 8000}}, 10000000, Bp, bCapped, Error) && Bp == 35100);
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDamageTechCapTest, "Lockdown.Rules.Combat.Damage.TechCap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDamageTechCapTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(*this, Catalog)) { return false; }
    const int64 Cap = Catalog->Get().Parameters.DamageFactorTechCapBp;
    bool bGood = TestEqual(TEXT("The technical cap is x1000"), Cap, int64(10000000));
    // Twenty x4 sources overflow any real hit: the product stops at the cap and reports it, never wraps.
    TArray<FLDDamageFactor> Many;
    for (int32 Index = 0; Index < 20; ++Index) { Many.Add({FName(*FString::Printf(TEXT("factor_%d"), Index)), 30000}); }
    int64 Bp = 0;
    bool bCapped = false;
    FString Error;
    bGood &= TestTrue(TEXT("An extreme product stops at the cap"), LDTactical::DamageFactorProduct(Many, Cap, Bp, bCapped, Error)
        && Bp == Cap && bCapped);
    bGood &= TestTrue(TEXT("Below the cap nothing is capped"), LDTactical::DamageFactorProduct({{TEXT("one"), 30000}}, Cap, Bp, bCapped, Error)
        && Bp == 40000 && !bCapped);
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDChainGuardTest, "Lockdown.Rules.Combat.Chain.Guard",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDChainGuardTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDDemoContentParameters& P = F.Catalog->Get().Parameters;
    const FLDCombatBodyRef Holder = F.AddBody(ELDVictimKind::Contender, 100000000);
    if (!InstallCards(*this, F, Holder, 6, {{TEXT("card_spiky_hide"), ELDCardTargetKind::Body}})) { return false; }
    TArray<FLDCombatBodyRef> Attackers;
    // Each hit on the holder derives one reflection: more derived effects than one step allows.
    TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    Pending.Reset();
    const int32 Hits = P.ChainMaxEffectsPerStep + 4;
    for (int32 Index = 0; Index < Hits; ++Index)
    {
        const FLDCombatBodyRef Attacker = F.AddBody(ELDVictimKind::Contender, 100000000);
        Pending.Add(FFixture::Hit(Attacker, Holder, 1000));
        Pending.Last().HitId = FGuid::NewGuid();
    }
    bool bGood = Drain(*this, F);
    bGood &= TestEqual(TEXT("Past CHAIN.MaxEffectsPerStep the derived effects wait for the next step"), Pending.Num(), 4);
    bGood &= Drain(*this, F) && TestEqual(TEXT("The next step takes the rest"), Pending.Num(), 0);
    // A hit already at the depth limit derives nothing more.
    const FLDCombatBodyRef Deep = F.AddBody(ELDVictimKind::Contender, 100000000);
    FLDCombatHit AtLimit = FFixture::Hit(Deep, Holder, 1000);
    AtLimit.HitId = FGuid::NewGuid();
    AtLimit.ChainDepth = uint8(P.ChainMaxDepth);
    Pending.Add(AtLimit);
    bGood &= Drain(*this, F) && TestEqual(TEXT("Past CHAIN.MaxDepth nothing is derived"), Pending.Num(), 0)
        && TestTrue(TEXT("The deep hit itself still landed"), F.Body(Holder).Resources.HpP2 < 100000000);
    return bGood;
}


namespace LDBuildPrimitivesTests
{
// Issue #287 P4: one element of Kind from From on To (fire is one burn layer), and the applier record of a body.
FLDCombatHit ElementHit(const FLDCombatBodyRef& From, const FLDCombatBodyRef& To, ELDCombatElement Kind)
{
    FLDCombatHit Hit = FFixture::Hit(From, To, 0);
    if (Kind == ELDCombatElement::Fire) { Hit.BurnStacks = 1; }
    else { Hit.NativeElements = LDCombatElements::Bit(Kind); }
    return Hit;
}

FLDElementApplier ApplierOf(FFixture& F, const FLDCombatBodyRef& Ref)
{
    FLDElementApplier Out;
    Out.SourceEntity = Ref.Victim.EntityId;
    Out.SourceBody = Ref;
    Out.CreditOwner = Ref.Victim.EntityId;
    Out.Level = 1;
    return Out;
}

const FLDGroundState* GroundOf(FFixture& F, ELDGroundStateKind Kind)
{
    return FAccess::GroundStates(*F.Driver).FindByPredicate([Kind](const FLDGroundState& Row) { return Row.Kind == Kind; });
}

const FLDCombatHit* PendingOn(FFixture& F, const FLDCombatBodyRef& Target, TFunctionRef<bool(const FLDCombatHit&)> Match)
{
    return FAccess::PendingHits(*F.Driver).FindByPredicate([&](const FLDCombatHit& Hit) { return Hit.Target == Target && Match(Hit); });
}
}

// Spec §8.2: the 5 x 5 table (either order, the same element or none never reacts) and every one of the ten reactions
// committed on a real target: its kind and consumed element, the primary hit's multiple of R (R = 30 at
// level 1 without AP), its control or status, the splash around it, the ground state it leaves and the trigger's momentum.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDElementsMatrixTest, "Lockdown.Rules.Combat.Elements.Matrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDElementsMatrixTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    using E = ELDCombatElement;
    using R = ELDCombatReaction;
    using G = ELDGroundStateKind;
    struct FCase { E Held; E Incoming; R Kind; };
    const FCase Cases[] = {
        {E::Fire, E::Water, R::Fog}, {E::Fire, E::Shock, R::Overload}, {E::Fire, E::Chill, R::Melt}, {E::Fire, E::Rot, R::Methane},
        {E::Water, E::Shock, R::Conduct}, {E::Water, E::Chill, R::Freeze}, {E::Water, E::Rot, R::Ferment},
        {E::Shock, E::Chill, R::Shatter}, {E::Shock, E::Rot, R::Corrode}, {E::Chill, E::Rot, R::Frostbite}};
    for (const FCase& Case : Cases)
    {
        TestTrue(FString::Printf(TEXT("%s either way"), LDCombatElements::ReactionName(Case.Kind)),
            LDCombatElements::ReactionOf(Case.Held, Case.Incoming) == Case.Kind
            && LDCombatElements::ReactionOf(Case.Incoming, Case.Held) == Case.Kind);
    }
    TestTrue(TEXT("The same element or none never reacts"), LDCombatElements::ReactionOf(E::Rot, E::Rot) == R::None
        && LDCombatElements::ReactionOf(E::None, E::Chill) == R::None);
    for (const FCase& Case : Cases)
    {
        const FString Name = LDCombatElements::ReactionName(Case.Kind);
        FFixture F;
        if (!F.Init(*this, true)) { return false; }
        const FLDCombatBodyRef A = F.AddBody(), T = F.AddBody(), O = F.AddBody();
        F.Place(A, -400, 0);
        F.Place(T, 0, 0);
        F.Place(O, 150, 0);
        FString Error;
        if (!TestTrue(Name + TEXT(": the held element lands: ") + Error, F.Apply(ElementHit(A, T, Case.Held), Error))) { return false; }
        if (Case.Kind == R::Conduct && !TestTrue(TEXT("The bystander is wet"), F.Apply(ElementHit(A, O, E::Water), Error))) { return false; }
        FAccess::PendingHits(*F.Driver).Reset();
        FAccess::Reactions(*F.Driver).Reset();
        const int64 MomentumBefore = F.Body(A).Resources.MomentumP2;
        if (!TestTrue(Name + TEXT(": the incoming element lands: ") + Error, F.Apply(ElementHit(A, T, Case.Incoming), Error))) { return false; }
        const TArray<FLDCombatReactionFact>& Facts = FAccess::Reactions(*F.Driver);
        if (!TestTrue(Name + TEXT(": one reaction of this kind, consuming the held element, triggered by A"), Facts.Num() == 1
            && Facts[0].Kind == Case.Kind && Facts[0].Consumed == Case.Held && Facts[0].TriggerEntity == A.Victim.EntityId)) { continue; }
        TestEqual(Name + TEXT(": the trigger gains MOMENTUM.Reaction"), F.Body(A).Resources.MomentumP2 - MomentumBefore,
            F.Catalog->Get().Parameters.Momentum.ReactionP2);
        const FLDCombatHit* Main = PendingOn(F, T, [&Case](const FLDCombatHit& Hit) { return Hit.ReactionKind == Case.Kind && Hit.ReactionConsumed == Case.Held; });
        if (!TestNotNull(Name + TEXT(": the primary reaction hit"), Main)) { continue; }
        const FLDCombatHit* Splash = PendingOn(F, O, [&Case](const FLDCombatHit& Hit) { return Hit.ReactionKind == Case.Kind; });
        const FLDBodyCombatState& Body = F.Body(T);
        const int64 Now = F.Mode->GetAuthorityNowMs();
        switch (Case.Kind)
        {
        case R::Fog: TestTrue(Name + TEXT(": R and a fog"), Main->BaseP2 == 3000 && FAccess::Fogs(*F.Driver).Num() == 1); break;
        case R::Overload: TestTrue(Name + TEXT(": 1.5 R, two breach, R on the bystander"), Main->BaseP2 == 4500 && Main->BreachStacks == 2
            && Splash && Splash->BaseP2 == 3000); break;
        case R::Melt: TestTrue(Name + TEXT(": 2 R and a puddle at the foot"), Main->BaseP2 == 6000 && GroundOf(F, G::Puddle)
            && LDCombatElements::GroundContains(*GroundOf(F, G::Puddle), F.Foot(T))); break;
        case R::Methane: TestTrue(Name + TEXT(": 1.5 R, two new burn layers, R on the bystander"), Main->BaseP2 == 4500
            && Body.Burn.Layers.Num() == 2 && Splash && Splash->BaseP2 == 3000); break;
        case R::Conduct: TestTrue(Name + TEXT(": 1.5 R and a slow; the wet bystander pays R with its wet"), Main->BaseP2 == 4500
            && Main->SlowBp == 3000 && Main->SlowDurationMs == 1500 && Splash && Splash->BaseP2 == 3000
            && F.Body(O).ElementMark.Kind == E::None); break;
        case R::Freeze: TestTrue(Name + TEXT(": R and a 1.2 s root"), Main->RootDurationMs == 1200 && Main->BaseP2 == 3000); break;
        case R::Ferment: TestTrue(Name + TEXT(": R and an 8 s rot cutting 60%"), Main->BaseP2 == 3000 && Body.ElementMark.Kind == E::Rot
            && Body.ElementMark.HealCutBp == 6000 && Body.ElementMark.ExpiresAtMs == Now + 8000); break;
        case R::Shatter: TestTrue(Name + TEXT(": R and the next skill hit takes +50% within 4 s"), Main->BaseP2 == 3000
            && Body.ShatterBp == 5000 && Body.ShatterUntilMs == Now + 4000); break;
        case R::Corrode: TestTrue(Name + TEXT(": R and five breach for 6 s"), Main->BaseP2 == 3000 && Main->BreachStacks == 5
            && Main->BreachMs == 6000); break;
        case R::Frostbite: TestTrue(Name + TEXT(": R and attack speed -40% for 4 s"), Main->BaseP2 == 3000 && Main->AttackSlowBp == 4000
            && Main->AttackSlowMs == 4000); break;
        default: break;
        }
        if (Case.Kind != R::Overload && Case.Kind != R::Methane && Case.Kind != R::Conduct)
        { TestNull(Name + TEXT(": no splash"), Splash); }
    }
    return true;
}

// Spec §8.1 shock: the holder's every third direct hit strikes 0.5 R on it and on the applier's other enemies within 350 cm;
// status damage and the strikes themselves never count.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDElementsShockTest, "Lockdown.Rules.Combat.Elements.Shock",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDElementsShockTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef A = F.AddBody(), T = F.AddBody(), O = F.AddBody(), B = F.AddBody();
    F.Place(A, -400, 0);
    F.Place(T, 0, 0);
    F.Place(O, 150, 0);
    F.Place(B, 0, 600);
    FString Error;
    FLDBodyCombatState& Body = F.Body(T);
    TestTrue(TEXT("A shocks T for 4 s"), F.Apply(ElementHit(A, T, ELDCombatElement::Shock), Error)
        && Body.ElementMark.Kind == ELDCombatElement::Shock && Body.ElementMark.ExpiresAtMs == 5000 && Body.ElementMark.ShockHits == 0);
    TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    Pending.Reset();
    const auto Strikes = [&Pending]() { return Pending.FilterByPredicate([](const FLDCombatHit& Hit) { return Hit.bOverchargeDischarge; }); };
    TestTrue(TEXT("Two direct hits only count"), F.Apply(FFixture::Hit(B, T, 1000), Error) && F.Apply(FFixture::Hit(B, T, 1000), Error)
        && Strikes().IsEmpty() && Body.ElementMark.ShockHits == 2);
    FLDCombatHit Settle = FFixture::Hit(B, T, 1000);
    Settle.bStatusSettle = true;
    TestTrue(TEXT("Status damage never counts"), F.Apply(Settle, Error) && Body.ElementMark.ShockHits == 2 && Strikes().IsEmpty());
    TestTrue(TEXT("The third direct hit strikes"), F.Apply(FFixture::Hit(B, T, 1000), Error));
    const TArray<FLDCombatHit> Struck = Strikes();
    TestTrue(TEXT("0.5 R on the holder and on A's other enemy within 350 cm, both A's status damage; the far enemy is spared"),
        Struck.Num() == 2 && Algo::AllOf(Struck, [&A](const FLDCombatHit& Hit)
        { return Hit.BaseP2 == 1500 && Hit.SourceEntity == A.Victim.EntityId && Hit.bStatusSettle; })
        && Struck.ContainsByPredicate([&T](const FLDCombatHit& Hit) { return Hit.Target == T; })
        && Struck.ContainsByPredicate([&O](const FLDCombatHit& Hit) { return Hit.Target == O; }));
    TestEqual(TEXT("The count starts again"), Body.ElementMark.ShockHits, 0);
    TestTrue(TEXT("The strikes land without counting"), F.Flush(Error) && Body.ElementMark.ShockHits == 0);
    Body.Branch.OverchargeUntilMs = 3000;
    TestTrue(TEXT("While overcharged (S10 B1) the count stands still"), F.Apply(FFixture::Hit(A, T, 0), Error) && Body.ElementMark.ShockHits == 0);
    return true;
}

// Spec §8.1 rot: new heals and shields are cut 25%; it ticks (3 + 0.03 AP) x 0.5 every 0.5 s for 4 s, credited to its
// applier; a refresh keeps the stronger cut and its rhythm; S21 carries rot instead of the retired grievous.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDElementsRotTest, "Lockdown.Rules.Combat.Elements.Rot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDElementsRotTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    const FLDCombatBodyRef A = F.AddBody(), T = F.AddBody(ELDVictimKind::Contender, 1000000);
    FLDBodyCombatState& Body = F.Body(T);
    FString Error;
    TestTrue(TEXT("A rots T: a 25% cut for 4 s, its first tick 0.5 s later"), F.Apply(ElementHit(A, T, ELDCombatElement::Rot), Error)
        && Body.ElementMark.Kind == ELDCombatElement::Rot && Body.ElementMark.HealCutBp == 2500 && Body.ElementMark.ExpiresAtMs == 5000
        && Body.ElementMark.NextTickMs == 1500);
    TArray<FLDObservedStatus> Statuses;
    FAccess::KeywordStatuses(*F.Driver, Body, 1000, Statuses);
    TestNotNull(TEXT("Observers see the rot"), FFixture::FindStatus(Statuses, ELDDemoStatusId::Rotten));
    TestTrue(TEXT("A new 100 shield arrives as 75"), FAccess::GrantShield(*F.Driver, Body, 10000, 1000, Error) && Body.Resources.ShieldP2 == 7500);
    Body.Resources.HpP2 = 500000;
    TestTrue(TEXT("A 100 heal arrives as 75"), FAccess::Heal(*F.Driver, Body, 10000) && Body.Resources.HpP2 == 507500);
    TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
    Pending.Reset();
    F.Now(1500);
    TestTrue(TEXT("The first tick: 1.5 of A's periodic status damage"), FAccess::AdvanceElements(*F.Driver, 1500, Error)
        && Pending.Num() == 1 && Pending[0].BaseP2 == 150 && Pending[0].bPeriodic && Pending[0].bStatusSettle
        && Pending[0].SourceEntity == A.Victim.EntityId);
    FLDCombatHit Stronger = ElementHit(A, T, ELDCombatElement::Rot);
    Stronger.RotCutBp = 5000;
    TestTrue(TEXT("A stronger rot (S21 A1) keeps the cut and the rhythm"), F.Apply(Stronger, Error) && Body.ElementMark.HealCutBp == 5000
        && Body.ElementMark.NextTickMs == 2000 && Body.ElementMark.ExpiresAtMs == 5500);
    F.Now(5500);
    TestTrue(TEXT("Eight more ticks through its end, then it lapses"), FAccess::AdvanceElements(*F.Driver, 5500, Error)
        && Pending.Num() == 1 + 8 && Body.ElementMark.Kind == ELDCombatElement::None);
    const FLDDemoAbilityDefinition* Sock = nullptr;
    TestTrue(TEXT("S21 carries rot, and its A2 branch leaves miasma"), F.Catalog->FindAbility(TEXT("active_sock_shot"), Sock, Error)
        && Sock->Values.Effects.ContainsByPredicate([](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp() == ELDDemoEffectOp::Rot; })
        && Sock->Branches.ContainsByPredicate([](const FLDDemoAbilityBranch& Branch)
        {
            return Branch.Path == ELDSkillBranch::A2 && Branch.Values.Effects.ContainsByPredicate([](const FLDDemoEffectDefinition& Effect)
            { return Effect.GetOp() == ELDDemoEffectOp::GroundState && Effect.GroundKind == uint8(ELDGroundStateKind::Miasma); });
        }));
    return true;
}

// Spec §8.3 "再加元素" and the standing effects: every conversion of the table (and two non-conversions), a conversion by an
// element hit on a body standing inside, each kind's tick on an enemy inside (never its owner) and ice as the authoritative
// movement parameter of an enemy on it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDGroundStateTransitionsTest, "Lockdown.Rules.Combat.GroundState.Transitions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDGroundStateTransitionsTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    using E = ELDCombatElement;
    using G = ELDGroundStateKind;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDCombatBodyRef A = F.AddBody(), B = F.AddBody(), Enemy = F.AddBody();
    F.Place(A, -500, -500);
    F.Place(B, -500, 500);
    F.Place(Enemy, 0, 0);
    const FVector At = F.Foot(Enemy);
    FString Error;
    TArray<FLDGroundState>& Live = FAccess::GroundStates(*F.Driver);
    const auto Reset = [&]()
    {
        Live.Reset();
        FAccess::Fogs(*F.Driver).Reset();
        FAccess::PendingHits(*F.Driver).Reset();
        F.Body(Enemy).ElementMark = FLDCombatElementMark();
        F.Body(Enemy).Burn = FLDCombatBurn();
    };
    struct FConversion { G From; E Element; G Into; bool bGone; bool bFog; int64 Burst; };
    const FConversion Conversions[] = {
        {G::Puddle, E::Shock, G::Field, false, false, 0}, {G::Puddle, E::Chill, G::Ice, false, false, 0},
        {G::Puddle, E::Fire, G::None, true, true, 0}, {G::Ice, E::Fire, G::Puddle, false, false, 0},
        {G::Fire, E::Water, G::None, true, true, 0}, {G::Miasma, E::Fire, G::None, true, false, 4500},
        {G::Puddle, E::Water, G::Puddle, false, false, 0}, {G::Field, E::Rot, G::Field, false, false, 0}};
    for (const FConversion& Row : Conversions)
    {
        Reset();
        const FString Name = FString::Printf(TEXT("%s + %s"), LDCombatElements::GroundName(Row.From), LDCombatElements::ElementName(Row.Element));
        if (!TestTrue(Name + TEXT(": A's state spawns: ") + Error, FAccess::SpawnGround(*F.Driver, Row.From, ApplierOf(F, A), At, 1000, Error))
            || !TestTrue(Name + TEXT(": B's element lands inside: ") + Error,
                FAccess::ConvertGround(*F.Driver, At, Row.Element, ApplierOf(F, B), 1000, Error))) { return false; }
        if (Row.bGone) { TestEqual(Name + TEXT(": it is gone"), Live.Num(), 0); }
        else if (TestEqual(Name + TEXT(": it stays"), Live.Num(), 1))
        {
            const bool bConverted = Row.Into != Row.From;
            TestTrue(Name + TEXT(": its kind and owner"), Live[0].Kind == Row.Into
                && Live[0].Owner.SourceEntity == (bConverted ? B : A).Victim.EntityId);
            TestTrue(Name + TEXT(": where it is"), Live[0].CentreCm.Equals(At, 0.01));
        }
        TestEqual(Name + TEXT(": a fog rises"), FAccess::Fogs(*F.Driver).Num(), Row.bFog ? 1 : 0);
        const FLDCombatHit* Burst = PendingOn(F, Enemy, [](const FLDCombatHit& Hit) { return true; });
        if (Row.Burst > 0) { TestTrue(Name + TEXT(": its burst strikes the enemy inside as B's damage"), Burst && Burst->BaseP2 == Row.Burst
            && Burst->SourceEntity == B.Victim.EntityId); }
        else { TestNull(Name + TEXT(": no burst"), Burst); }
    }
    // An element hit on a body standing in a puddle converts it.
    Reset();
    TestTrue(TEXT("A puddle under the enemy, then B shocks the enemy"), FAccess::SpawnGround(*F.Driver, G::Puddle, ApplierOf(F, A), At, 1000, Error)
        && F.Apply(ElementHit(B, Enemy, E::Shock), Error) && Live.Num() == 1 && Live[0].Kind == G::Field && Live[0].Owner.SourceEntity == B.Victim.EntityId);
    // Standing effects at the first tick, on enemies of the owner only.
    struct FTick { G Kind; int64 TickMs; E Element; int64 Damage; };
    const FTick Ticks[] = {{G::Puddle, 500, E::Water, 0}, {G::Fire, 1000, E::Fire, 0}, {G::Miasma, 500, E::Rot, 0},
        {G::Field, 500, E::Shock, 900}};
    for (const FTick& Tick : Ticks)
    {
        Reset();
        const FString Name = LDCombatElements::GroundName(Tick.Kind);
        FAccess::SpawnGround(*F.Driver, Tick.Kind, ApplierOf(F, A), At, 1000, Error);
        FAccess::SpawnGround(*F.Driver, Tick.Kind, ApplierOf(F, A), F.Foot(A), 1000, Error);
        F.Now(1000 + Tick.TickMs);
        if (!TestTrue(Name + TEXT(": the tick runs: ") + Error, FAccess::AdvanceGround(*F.Driver, 1000 + Tick.TickMs, Error))) { return false; }
        const E Taken = F.Body(Enemy).ElementMark.Kind == E::None && F.Body(Enemy).Burn.Layers.Num() > 0 ? E::Fire : F.Body(Enemy).ElementMark.Kind;
        TestTrue(Name + TEXT(": the enemy inside takes its element"), Taken == Tick.Element);
        TestTrue(Name + TEXT(": the owner standing in its own state takes nothing"), F.Body(A).ElementMark.Kind == E::None
            && F.Body(A).Burn.Layers.IsEmpty());
        if (Tick.Damage > 0)
        { TestNotNull(Name + TEXT(": its damage"), PendingOn(F, Enemy, [&Tick](const FLDCombatHit& Hit) { return Hit.BaseP2 == Tick.Damage; })); }
        F.Now(1000);
    }
    // Ice: an enemy on it accelerates and brakes at half; its owner on its own ice does not.
    Reset();
    FAccess::SpawnGround(*F.Driver, G::Ice, ApplierOf(F, A), At, 1000, Error);
    FAccess::SpawnGround(*F.Driver, G::Ice, ApplierOf(F, A), F.Foot(A), 1000, Error);
    TestTrue(TEXT("Ice contact is read at the advance"), FAccess::AdvanceGround(*F.Driver, 1000, Error));
    TestTrue(TEXT("The enemy on A's ice has the inertia, A on its own ice does not"), F.Body(Enemy).bOnEnemyIce && !F.Body(A).bOnEnemyIce
        && FMath::IsNearlyEqual(F.Driver->ReadMoveAccelScale(F.Actor(Enemy)), .5f) && F.Driver->ReadMoveAccelScale(F.Actor(A)) == 1.f);
    // Spec §11.1 cold day: a puddle turns to ice after DAILY.ColdFreezeSec, or at the end of its own life when that is sooner;
    // it never dries up unfrozen.
    Reset();
    FAccess::SetDaily(*F.Mode, {ELDDailyModifier::Cold});
    ON_SCOPE_EXIT { FAccess::SetDaily(*F.Mode, {}); };
    FAccess::SpawnGround(*F.Driver, G::Puddle, ApplierOf(F, A), At, 1000, Error);
    if (!TestEqual(TEXT("Cold day: one puddle"), Live.Num(), 1)) { return false; }
    const int64 PuddleEnd = Live[0].ExpiresAtMs;
    const int64 FreezeAt = FMath::Min<int64>(1000 + F.Catalog->Get().Parameters.Daily.ColdFreezeMs, PuddleEnd);
    // Walk the ground's own due instants: the freeze must be one of them, never skipped by an expiry.
    int64 At2 = 1000;
    for (int32 Guard = 0; Guard < 256 && At2 < FreezeAt; ++Guard)
    {
        At2 = FAccess::NextGroundDueMs(*F.Driver, At2);
        if (At2 > FreezeAt) { break; }
        F.Now(At2);
        if (!TestTrue(TEXT("Cold day: the ground advances: ") + Error, FAccess::AdvanceGround(*F.Driver, At2, Error))) { return false; }
        TestTrue(TEXT("Cold day: the puddle never dries up before it freezes"), Live.Num() == 1);
    }
    TestEqual(TEXT("Cold day: the freeze is a ground due instant"), At2, FreezeAt);
    TestTrue(TEXT("Cold day: the puddle became ice in place instead of drying up"), Live.Num() == 1 && Live[0].Kind == G::Ice
        && Live[0].CentreCm.Equals(At, 0.01) && Live[0].ExpiresAtMs > FreezeAt);
    return true;
}

// Spec §8.3 protection and §14 observation: at most 6 live states per owner and 40 per map (the oldest go first); a skill
// keeps one live instance of its own state; every live state is a public fact with its owner's seat, and its wire record
// is 16 bytes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDGroundStateCapsTest, "Lockdown.Rules.Combat.GroundState.Caps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDGroundStateCapsTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this)) { return false; }
    TArray<FLDCombatBodyRef> Owners;
    for (int32 Index = 0; Index < 7; ++Index) { Owners.Add(F.AddBody()); }
    FString Error;
    TArray<FLDGroundState>& Live = FAccess::GroundStates(*F.Driver);
    for (int32 Index = 0; Index < 7; ++Index)
    { FAccess::SpawnGround(*F.Driver, ELDGroundStateKind::Puddle, ApplierOf(F, Owners[0]), FVector(Index * 10., 0, 0), 1000, Error); }
    TestTrue(TEXT("Six per owner: the seventh evicts the oldest"), Live.Num() == 6
        && !Live.ContainsByPredicate([](const FLDGroundState& Row) { return Row.CentreCm.X == 0; }));
    for (int32 Owner = 1; Owner < 7; ++Owner)
    {
        for (int32 Index = 0; Index < 6; ++Index)
        { FAccess::SpawnGround(*F.Driver, ELDGroundStateKind::Fire, ApplierOf(F, Owners[Owner]), FVector(Index * 10., Owner * 100., 0), 1000, Error); }
    }
    TestEqual(TEXT("Forty on the map at most"), Live.Num(), 40);
    Live.Reset();
    FLDElementApplier Skill = ApplierOf(F, Owners[0]);
    Skill.Source.Kind = ELDCombatSourceKind::Learned;
    Skill.Source.DefinitionId = FName(TEXT("active_stale_takeout"));
    Skill.EffectId = FGuid::NewGuid();
    FAccess::SpawnGround(*F.Driver, ELDGroundStateKind::Miasma, Skill, FVector::ZeroVector, 1000, Error);
    Skill.EffectId = FGuid::NewGuid();
    FAccess::SpawnGround(*F.Driver, ELDGroundStateKind::Miasma, Skill, FVector(500, 0, 0), 1000, Error);
    TestTrue(TEXT("A skill keeps one live instance of its own state (the newer)"), Live.Num() == 1 && Live[0].CentreCm.X == 500);
    TArray<FLDObservedGroundFact> Facts;
    TestTrue(TEXT("Every live state is a public fact"), F.Driver->ReadObservedGround(Facts, Error) && Facts.Num() == 1
        && Facts[0].Kind == uint8(ELDGroundStateKind::Miasma) && Facts[0].RemainingMs == 4000 && Facts[0].LifeMs == 4000);
    const FLDObservedGroundState View = LDObservation::GroundView(Facts[0]);
    TestTrue(TEXT("Its wire view is valid"), LDObservation::ValidateGround(View, Error));
    TestEqual(TEXT("Each ground state is sixteen bytes on the wire"), LDObservation::GroundStateWireBytes, 16);
    return true;
}

// Spec §5 N4 占座书包: a bag of 150 + 20 x (L - 1) life stands at its point; it stops enemy flights (not the owner's),
// takes their raw amount, and blows over its radius when it runs out or is knocked out; one skill keeps one live bag (two
// with A), and A1 joins two bags with a wall; bags and the wall are public obstacles.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDSkillN4BagTest, "Lockdown.Rules.Combat.Skills.N4Bag",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDSkillN4BagTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if (!F.Init(*this, true)) { return false; }
    const FLDDemoAbilityDefinition* Ability = nullptr;
    FString Error;
    if (!TestTrue(TEXT("N4 is approved"), F.Catalog->FindAbility(TEXT("active_school_bag"), Ability, Error) && Ability)) { return false; }
    const FLDDemoAbilityValues* Values = F.Catalog->FindValues(*Ability, 1, ELDSkillBranch::None);
    const FLDDemoEffectDefinition* BagDef = Values ? Values->Effects.FindByPredicate(
        [](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp() == ELDDemoEffectOp::Bag; }) : nullptr;
    if (!TestNotNull(TEXT("N4 carries its bag"), BagDef)) { return false; }
    const FLDCombatBodyRef Owner = F.AddBody(), Enemy = F.AddBody(), Far = F.AddBody();
    F.Place(Owner, 0, 0);
    F.Place(Enemy, 150, 0);
    F.Place(Far, 2000, 0);
    FLDCombatHit Payload = FFixture::Hit(Owner, Owner, 8000);
    Payload.Source.Kind = ELDCombatSourceKind::Learned;
    Payload.Source.DefinitionId = FName(TEXT("active_school_bag"));
    TArray<FLDCombatBag>& Bags = FAccess::Bags(*F.Driver);
    const FVector Point = F.Foot(Owner) + FVector(100, 0, 0);
    TestTrue(TEXT("A bag is placed: ") + Error, FAccess::PlaceBag(*F.Driver, F.Body(Owner), Payload, *BagDef, Point, 1000, Error));
    TestTrue(TEXT("It has 150 life at level 1 and lasts four seconds"), Bags.Num() == 1 && Bags[0].HpP2 == 15000
        && Bags[0].ExpiresAtMs == 5000 && Bags[0].BlastRadiusCm == 200 && Bags[0].Blocker.IsValid());
    double At = 0;
    TestTrue(TEXT("It stops an enemy's flight through it"),
        FAccess::BagOnSegment(*F.Driver, Enemy.Victim.EntityId, Point - FVector(500, 0, 0), Point + FVector(500, 0, 0), At) == 0 && At > 0 && At < .5);
    TestTrue(TEXT("The owner's own flight passes it"),
        FAccess::BagOnSegment(*F.Driver, Owner.Victim.EntityId, Point - FVector(500, 0, 0), Point + FVector(500, 0, 0), At) == INDEX_NONE);
    TArray<FLDObservedProjectileFact> Facts;
    TestTrue(TEXT("The bag is a public obstacle"), F.Driver->ReadObservedProjectiles(Facts, Error)
        && Facts.ContainsByPredicate([](const FLDObservedProjectileFact& Fact) { return Fact.Look == ELDObservedProjectileLook::Bag && Fact.RadiusCm == 40; }));
    // An enemy blast of raw 100 takes 100 of its life; the owner's own does nothing.
    const FLDCombatHit EnemyBlast = FFixture::Hit(Enemy, Owner, 10000);
    FAccess::HitBags(*F.Driver, Point, 50, EnemyBlast, 1500);
    FAccess::HitBags(*F.Driver, Point, 50, Payload, 1500);
    TestEqual(TEXT("Only enemies damage it, by their raw amount"), Bags[0].HpP2, int64(5000));
    // It runs out: the blast hits the enemy within 200 cm and not the far body or the owner.
    const int64 EnemyBefore = F.Body(Enemy).Resources.HpP2, FarBefore = F.Body(Far).Resources.HpP2, OwnerBefore = F.Body(Owner).Resources.HpP2;
    F.Now(5000);
    TestTrue(TEXT("It blows when it runs out: ") + Error, FAccess::AdvanceBags(*F.Driver, 5000, Error) && Bags.IsEmpty() && F.Flush(Error));
    TestTrue(TEXT("The blast hurts the enemy in reach only"), F.Body(Enemy).Resources.HpP2 < EnemyBefore
        && F.Body(Far).Resources.HpP2 == FarBefore && F.Body(Owner).Resources.HpP2 == OwnerBefore);
    // Knocked out: it blows at once.
    F.Now(6000);
    TestTrue(TEXT("A second bag"), FAccess::PlaceBag(*F.Driver, F.Body(Owner), Payload, *BagDef, Point, 6000, Error));
    const FLDCombatHit Big = FFixture::Hit(Enemy, Owner, 100000);
    FAccess::HitBags(*F.Driver, Point, 50, Big, 6100);
    TestTrue(TEXT("Knocked out, it blows before its time"), FAccess::AdvanceBags(*F.Driver, 6100, Error) && Bags.IsEmpty());
    F.Flush(Error);
    // One live bag per skill; with A two, and A1 joins them with a wall.
    FAccess::PlaceBag(*F.Driver, F.Body(Owner), Payload, *BagDef, Point, 7000, Error);
    FAccess::PlaceBag(*F.Driver, F.Body(Owner), Payload, *BagDef, Point + FVector(0, 300, 0), 7000, Error);
    TestTrue(TEXT("A skill keeps one live bag (the newer)"), Bags.Num() == 1 && Bags[0].CentreCm.Y > Point.Y + 200);
    FLDDemoEffectDefinition Two = *BagDef;
    Two.Count = 2;
    Two.ExtraDistanceCm.Numerator = 60;
    Two.ExtraDistanceCm.Denominator = 1;
    FAccess::PlaceBag(*F.Driver, F.Body(Owner), Payload, Two, Point - FVector(0, 300, 0), 7000, Error);
    TestTrue(TEXT("With A two bags stand, and A1 walls them together"), Bags.Num() == 2 && Bags[1].WallKey.IsValid()
        && Bags[1].WallBlocker.IsValid() && Bags[1].WallHalfWidthCm == 30);
    Facts.Reset();
    TestTrue(TEXT("The wall is a public obstacle too"), F.Driver->ReadObservedProjectiles(Facts, Error)
        && Facts.ContainsByPredicate([](const FLDObservedProjectileFact& Fact) { return Fact.Look == ELDObservedProjectileLook::BagWall && Fact.LengthCm > 500; }));
    for (const FLDObservedProjectileFact& Fact : Facts)
    {
        if (Fact.Look != ELDObservedProjectileLook::Bag && Fact.Look != ELDObservedProjectileLook::BagWall) { continue; }
        FLDObservedProjectileView View;
        View.Handle = FGuid::NewGuid();
        View.PositionCm = Fact.PositionCm;
        View.Look = Fact.Look;
        View.RadiusCm = Fact.RadiusCm;
        View.LengthCm = Fact.LengthCm;
        TestTrue(TEXT("A bag record validates: ") + Error, LDObservation::ValidateProjectile(View, Error));
    }
    FAccess::HitBags(*F.Driver, Bags[0].CentreCm, 10, Big, 7100);
    TestTrue(TEXT("A wall goes with either of its bags"), FAccess::AdvanceBags(*F.Driver, 7100, Error) && Bags.Num() == 1
        && !Bags[0].WallKey.IsValid() && !Bags[0].WallBlocker.IsValid());
    return true;
}

// Spec §9 1v1: with nobody else near, the S10 bolt bounces back to its first target (70%, then 49%); with another enemy near
// it bounces there first and then back.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDOneVsOneBounceTest, "Lockdown.Rules.Combat.OneVsOne.Bounce",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDOneVsOneBounceTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FString Error;
    for (const bool bCrowd : {false, true})
    {
        FFixture F;
        if (!F.Init(*this, true)) { return false; }
        const FLDCombatBodyRef A = F.AddBody(), T = F.AddBody(), O = F.AddBody();
        F.Place(A, -400, 0);
        F.Place(T, 0, 0);
        F.Place(O, bCrowd ? 150 : 2000, 0);
        const FLDDemoAbilityDefinition* Zap = nullptr;
        if (!TestTrue(TEXT("S10 exists"), F.Catalog->FindAbility(TEXT("active_zap"), Zap, Error))) { return false; }
        const FLDDemoEffectDefinition* Bounce = Zap->Values.Effects.FindByPredicate([](const FLDDemoEffectDefinition& Effect)
        { return Effect.GetOp() == ELDDemoEffectOp::ChainBounce; });
        if (!TestTrue(TEXT("S10 bounces twice at 70% every 0.15 s"), Bounce && Bounce->Count == 2 && Bounce->RatioBp == 7000
            && Bounce->IntervalMs == 150)) { return false; }
        FLDCombatHit Payload = FFixture::Hit(A, T, 10000);
        if (!TestTrue(TEXT("The chain starts: ") + Error, FAccess::StartChain(*F.Driver, Payload, T, F.Foot(T), *Bounce, 1000, Error))) { return false; }
        TArray<FLDCombatHit>& Pending = FAccess::PendingHits(*F.Driver);
        F.Now(1150);
        TestTrue(TEXT("The first bounce"), FAccess::AdvanceElements(*F.Driver, 1150, Error) && Pending.Num() == 1
            && Pending[0].Target == (bCrowd ? O : T) && Pending[0].DeliveryScaleBp == 7000);
        F.Now(1300);
        TestTrue(TEXT("The second bounce returns to the first target"), FAccess::AdvanceElements(*F.Driver, 1300, Error) && Pending.Num() == 2
            && Pending[1].Target == T && Pending[1].DeliveryScaleBp == 4900 && FAccess::Chains(*F.Driver).IsEmpty());
    }
    return true;
}

// Spec §7 P4 cards (Lockdown.Rules.Combat.Cards.<code>), installed as one and two copies like the P2 cards.
namespace LDBuildPrimitivesTests
{
// A native direct hit of the card host skill from Holder on a fresh target 300 cm east of it.
bool HostHit(FAutomationTestBase& Test, FFixture& F, const FLDCombatBodyRef& Holder, FLDCombatBodyRef& OutTarget)
{
    OutTarget = F.AddBody();
    F.Place(Holder, 0, 0);
    F.Place(OutTarget, 300, 0);
    FLDCombatHit Hit = FFixture::Hit(Holder, OutTarget, 1000);
    Hit.Source.Kind = ELDCombatSourceKind::Learned;
    Hit.Source.DefinitionId = FName(CardHost);
    Hit.bDirectAbilityDamage = true;
    FString Error;
    return Test.TestTrue(TEXT("The host skill hits: ") + Error, F.Apply(Hit, Error));
}
}

// The element medals: in a skill's element slot the host's hits carry the element; in the body's, the third basic step does.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleMedalSkillTest, "Lockdown.Rules.Combat.Cards.MedalSkill",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleMedalSkillTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    bool bGood = RunCardRule(*this, {TEXT("medal_chill"), ELDDemoEffectOp::SkillElement, ELDCardTargetKind::Skill},
        [this](FFixture& F, const FLDCombatBodyRef& Holder, int32)
        {
            FLDCombatBodyRef Target;
            return HostHit(*this, F, Holder, Target) && TestTrue(TEXT("The host's hit chills"), F.Body(Target).ElementMark.Kind == ELDCombatElement::Chill);
        });
    bGood &= RunCardRule(*this, {TEXT("medal_fire"), ELDDemoEffectOp::SkillElement, ELDCardTargetKind::Skill},
        [this](FFixture& F, const FLDCombatBodyRef& Holder, int32)
        {
            FLDCombatBodyRef Target;
            return HostHit(*this, F, Holder, Target) && TestTrue(TEXT("The host's hit burns"), F.Body(Target).Burn.Layers.Num() >= 2);
        });
    return bGood;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleMedalBodyTest, "Lockdown.Rules.Combat.Cards.MedalBody",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleMedalBodyTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("medal_rot"), ELDDemoEffectOp::SkillElement, ELDCardTargetKind::Body},
        [this](FFixture& F, const FLDCombatBodyRef& Holder, int32)
        {
            const FLDCombatBodyRef Target = F.AddBody();
            F.Place(Holder, 0, 0);
            F.Place(Target, 150, 0);
            FLDCombatHit Basic = FFixture::Hit(Holder, Target, 1000);
            Basic.HitClass = ELDCombatHitClass::BasicPrimary;
            Basic.DamageType = ELDDemoDamageType::Physical;
            FString Error;
            // The medal rides the last combo step only: the first step leaves the target unmarked.
            Basic.ComboStep = 1;
            const bool bEarly = F.Apply(Basic, Error) && F.Body(Target).ElementMark.Kind == ELDCombatElement::None;
            Basic.HitId = FGuid::NewGuid();
            Basic.ComboStep = 3;
            const bool bApplied = F.Apply(Basic, Error);
            return TestTrue(TEXT("A first-step basic applies no element"), bEarly) && TestTrue(TEXT("A primary basic lands: ") + Error, bApplied)
                && TestTrue(TEXT("A third-step basic rots its target"), bApplied && F.Body(Target).ElementMark.Kind == ELDCombatElement::Rot
                && F.Body(Target).ElementMark.Applier.Source.DefinitionId == FName(TEXT("medal_rot")));
        });
}
namespace LDBuildPrimitivesTests
{
// Holder's D dash from 400 cm west of its foot completes now.
bool CompleteDash(FAutomationTestBase& Test, FFixture& F, const FLDCombatBodyRef& Holder)
{
    FLDCombatVoluntaryDash Dash;
    Dash.EffectId = FGuid::NewGuid();
    Dash.Source.Kind = ELDCombatSourceKind::Innate;
    Dash.Source.DefinitionId = LDCombatSourceRules::InnateDashId();
    Dash.StartFootCm = F.Foot(Holder) - FVector(400, 0, 0);
    Dash.EndMs = F.Mode->GetAuthorityNowMs();
    F.Body(Holder).VoluntaryDash = Dash;
    FString Error;
    return Test.TestTrue(TEXT("The dash completes: ") + Error, FAccess::ResolveDashCompletions(*F.Driver, Dash.EndMs, Error));
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleD10Test, "Lockdown.Rules.Combat.Cards.D10",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleD10Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_ice_trail"), ELDDemoEffectOp::DashIce, ELDCardTargetKind::Dash},
        [this](FFixture& F, const FLDCombatBodyRef& Holder, int32 Copies)
        {
            if (!CompleteDash(*this, F, Holder)) { return false; }
            const TArray<FLDGroundState>& Live = FAccess::GroundStates(*F.Driver);
            return TestTrue(TEXT("Each copy leaves a 160 cm wide, 5 s ice band along the path"), Live.Num() == Copies
                && Algo::AllOf(Live, [&](const FLDGroundState& Row)
                {
                    return Row.Kind == ELDGroundStateKind::Ice && FMath::IsNearlyEqual(Row.RadiusCm, 80.) && FMath::IsNearlyEqual(Row.HalfLengthCm, 200.)
                        && Row.ExpiresAtMs - Row.StartMs == 5000 && Row.Owner.SourceEntity == Holder.Victim.EntityId;
                }));
        });
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleD11Test, "Lockdown.Rules.Combat.Cards.D11",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleD11Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_move_turf"), ELDDemoEffectOp::GroundRelocate, ELDCardTargetKind::Dash},
        [this](FFixture& F, const FLDCombatBodyRef& Holder, int32)
        {
            FString Error;
            FAccess::SpawnGround(*F.Driver, ELDGroundStateKind::Puddle, ApplierOf(F, Holder), FVector(2000, 0, 0), 1000, Error);
            if (!CompleteDash(*this, F, Holder)) { return false; }
            const TArray<FLDGroundState>& Live = FAccess::GroundStates(*F.Driver);
            const bool bMoved = Live.Num() == 1 && Live[0].CentreCm.Equals(F.Foot(Holder), 0.01) && Live[0].bRelocated;
            F.Place(Holder, 900, 0);
            return TestTrue(TEXT("The newest ground state moves to the dash end"), bMoved) && CompleteDash(*this, F, Holder)
                && TestFalse(TEXT("and never twice"), Live[0].CentreCm.Equals(F.Foot(Holder), 0.01));
        });
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY12Test, "Lockdown.Rules.Combat.Cards.Y12",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY12Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_double_stall"), ELDDemoEffectOp::DoubleStall, ELDCardTargetKind::Body},
        [this](FFixture& F, const FLDCombatBodyRef& Holder, int32 Copies)
        {
            FString Error;
            FLDElementApplier Skill = ApplierOf(F, Holder);
            Skill.Source.Kind = ELDCombatSourceKind::Learned;
            Skill.Source.DefinitionId = FName(TEXT("active_stale_takeout"));
            for (int32 Index = 0; Index < 4; ++Index)
            {
                Skill.EffectId = FGuid::NewGuid();
                FAccess::SpawnGround(*F.Driver, ELDGroundStateKind::Miasma, Skill, FVector(Index * 300., 0, 0), 1000, Error);
            }
            return TestEqual(TEXT("A skill keeps one more live state per copy"), FAccess::GroundStates(*F.Driver).Num(), 1 + Copies);
        });
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY23Test, "Lockdown.Rules.Combat.Cards.Y23",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY23Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_turf_growth"), ELDDemoEffectOp::GroundGrowth, ELDCardTargetKind::Body},
        [this](FFixture& F, const FLDCombatBodyRef& Holder, int32 Copies)
        {
            FString Error;
            const FLDCombatBodyRef Target = F.AddBody();
            FAccess::SpawnGround(*F.Driver, ELDGroundStateKind::Puddle, ApplierOf(F, Holder), F.Foot(Holder), 1000, Error);
            if (!TestTrue(TEXT("Standing starts: ") + Error, FAccess::AdvanceGround(*F.Driver, 1000, Error))) { return false; }
            F.Now(3500);
            TArray<FLDDamageFactor> Factors;
            FAccess::GroundFactors(*F.Driver, F.Body(Holder), F.Body(Target), Factors);
            int64 Sum = 0;
            for (const FLDDamageFactor& Row : Factors) { if (Row.Key == FName(TEXT("card_turf_growth"))) { Sum += Row.Bp; } }
            const bool bGrown = Sum == 2 * 300 * Copies;
            FAccess::GroundStates(*F.Driver).Reset();
            return TestTrue(TEXT("Two full seconds on its own ground: +3% each, per copy"), bGrown)
                && TestTrue(TEXT("Leaving resets it"), FAccess::AdvanceGround(*F.Driver, 3500, Error) && F.Body(Holder).TurfSinceMs == 0);
        });
}


// Issue #292 P5 cards (Lockdown.Rules.Combat.Cards.<code>), installed as one and two copies like the P2 and P4 cards; the ones
// whose result Combat compiles without a match also check it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleA07Test, "Lockdown.Rules.Combat.Cards.A07",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleA07Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_wide_swing"), ELDDemoEffectOp::ComboSweep, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleA08Test, "Lockdown.Rules.Combat.Cards.A08",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleA08Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_fist_skill"), ELDDemoEffectOp::ComboCast, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleA10Test, "Lockdown.Rules.Combat.Cards.A10",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleA10Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_slow_craft"), ELDDemoEffectOp::SpeedFactor, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleA13Test, "Lockdown.Rules.Combat.Cards.A13",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleA13Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_muscle_memory"), ELDDemoEffectOp::HitGrowth, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleK03Test, "Lockdown.Rules.Combat.Cards.K03",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleK03Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_quick_takedown"), ELDDemoEffectOp::TakedownReset, ELDCardTargetKind::Skill});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleK05Test, "Lockdown.Rules.Combat.Cards.K05",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleK05Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_pin_amp"), ELDDemoEffectOp::ControlAmp, ELDCardTargetKind::Skill});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleK07Test, "Lockdown.Rules.Combat.Cards.K07",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleK07Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_crowd_hit"), ELDDemoEffectOp::Reshape, ELDCardTargetKind::Skill});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleK11Test, "Lockdown.Rules.Combat.Cards.K11",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleK11Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_hold_longer"), ELDDemoEffectOp::Reshape, ELDCardTargetKind::Skill});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleK14Test, "Lockdown.Rules.Combat.Cards.K14",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleK14Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_held_breath"), ELDDemoEffectOp::ShieldSpend, ELDCardTargetKind::Skill});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleK18Test, "Lockdown.Rules.Combat.Cards.K18",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleK18Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_combo_chain"), ELDDemoEffectOp::RecentCastAmp, ELDCardTargetKind::Skill});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleD04Test, "Lockdown.Rules.Combat.Cards.D04",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleD04Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_dodge_strike"), ELDDemoEffectOp::DodgeFreeCast, ELDCardTargetKind::Dash});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleD09Test, "Lockdown.Rules.Combat.Cards.D09",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleD09Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_slippery"), ELDDemoEffectOp::HitGrowth, ELDCardTargetKind::Dash});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY06Test, "Lockdown.Rules.Combat.Cards.Y06",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY06Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_shell_burst"), ELDDemoEffectOp::ShieldBurstBreak, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY11Test, "Lockdown.Rules.Combat.Cards.Y11",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY11Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_short_breath"), ELDDemoEffectOp::OverdriveShort, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY18Test, "Lockdown.Rules.Combat.Cards.Y18",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY18Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_run_hit"), ELDDemoEffectOp::SpeedFactor, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY24Test, "Lockdown.Rules.Combat.Cards.Y24",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY24Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_tally"), ELDDemoEffectOp::Tally, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleY29Test, "Lockdown.Rules.Combat.Cards.Y29",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleY29Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_burn_guard"), ELDDemoEffectOp::OverhealShield, ELDCardTargetKind::Body});
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleA05Test, "Lockdown.Rules.Combat.Cards.A05",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleA05Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_long_arm"), ELDDemoEffectOp::BasicProjectile, ELDCardTargetKind::Body},
        [this](FFixture& F, const FLDCombatBodyRef& Holder, int32 Copies)
        {
            // Each copy adds its 330 cm to the basic range (the flight itself starts at the landing).
            const FLDDemoRational& Range = F.Body(Holder).AttributeProjection.Final.AttackRangeCm;
            const double Cm = Range.Denominator > 0 ? double(Range.Numerator) / double(Range.Denominator) : 0.;
            const FLDDemoRational& Approved = F.Catalog->Get().Parameters.AttackRangeCm.Base;
            const double Base = double(Approved.Numerator) / double(FMath::Max<int64>(Approved.Denominator, 1));
            return TestTrue(FString::Printf(TEXT("A05 x%d: range %.0f (base %.0f)"), Copies, Cm, Base),
                FMath::IsNearlyEqual(Cm, FMath::Min(Base + 330. * Copies, 1000.), 0.5));
        });
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleK10Test, "Lockdown.Rules.Combat.Cards.K10",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleK10Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_long_reach"), ELDDemoEffectOp::Reshape, ELDCardTargetKind::Skill},
        [this](FFixture& F, const FLDCombatBodyRef& Holder, int32 Copies)
        {
            FLDCombatActiveTerms Plain, Terms;
            if (!ActiveTerms(*this, F, CardHost, Plain) || !HostTerms(*this, F, Holder, Terms)) { return false; }
            const auto Cm = [](const FLDDemoRational& V) { return V.Denominator > 0 ? double(V.Numerator) / double(V.Denominator) : 0.; };
            // Copies add +50% each to the cast range of a unit host.
            return TestTrue(FString::Printf(TEXT("K10 x%d: range %.1f -> %.1f"), Copies, Cm(Plain.Target.RangeCm), Cm(Terms.Target.RangeCm)),
                FMath::IsNearlyEqual(Cm(Terms.Target.RangeCm), Cm(Plain.Target.RangeCm) * (1. + .5 * Copies), .01));
        });
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleD07Test, "Lockdown.Rules.Combat.Cards.D07",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleD07Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_head_down"), ELDDemoEffectOp::DashRam, ELDCardTargetKind::Dash},
        [this](FFixture& F, const FLDCombatBodyRef& Holder, int32 Copies)
        {
            FLDCombatActiveTerms Terms;
            FString Error;
            if (!TestTrue(TEXT("The D dash compiles: ") + Error, FAccess::Compile(*F.Driver, F.Body(Holder).AttributeProjection,
                SourceOf(ELDCombatSourceKind::Innate, TEXT("innate_dash")), Terms, Error))) { return false; }
            return TestEqual(FString::Printf(TEXT("D07 x%d: the dash runs +60%% per copy"), Copies), Terms.DashScaleBp, int64(10000 + 6000 * Copies));
        });
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCardRuleD08Test, "Lockdown.Rules.Combat.Cards.D08",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCardRuleD08Test::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    return RunCardRule(*this, {TEXT("card_paid_dash"), ELDDemoEffectOp::CooldownCost, ELDCardTargetKind::Dash},
        [this](FFixture& F, const FLDCombatBodyRef& Holder, int32 Copies)
        {
            FLDCombatActiveTerms Terms;
            FString Error;
            if (!TestTrue(TEXT("The D dash compiles: ") + Error, FAccess::Compile(*F.Driver, F.Body(Holder).AttributeProjection,
                SourceOf(ELDCombatSourceKind::Innate, TEXT("innate_dash")), Terms, Error))) { return false; }
            // Copies add their momentum and their cooldown deltas (x(1 - 0.6 n), never below zero).
            return TestTrue(FString::Printf(TEXT("D08 x%d: the D pays 15 momentum per copy, cooldown scale %lld"), Copies, Terms.CooldownScaleBp),
                Terms.DashMomentumCostP2 == 1500 * Copies && Terms.CooldownScaleBp == FMath::Max<int64>(10000 - 6000 * Copies, 0));
        });
}


#endif
