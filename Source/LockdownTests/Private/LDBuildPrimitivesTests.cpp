#include "Combat/LDMomentum.h"
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
#include "PK/LDPKDirector.h"
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
    static bool FinalStep(ALDCombatDirector& Driver,int64 NowMs,FString& Error){return Driver.AdvanceFinalMods(NowMs,Error);}
    static const auto& FinalTraffic(const ALDCombatDirector& Driver){return Driver.FinalTraffic;}
    static bool FinalCost(const ALDCombatDirector& Driver,const FLDCombatEntityRecord& Record,const FLDCombatSourceRef& Source,
        int64& Cooldown,FString& Error){return Driver.FinalCooldownCost(Record,Source,Cooldown,Error);}
    static bool FinalSplit(ALDCombatDirector& Driver,FLDCombatZone& Zone,const FLDCombatAction& Action,int64 NowMs,
        bool& Handled,FString& Error){return Driver.SplitFinalZone(Zone,Action.Direction,NowMs,Handled,Error);}
    static bool FinalNotice(ALDCombatDirector& Driver,const FLDCombatHit& Hit,FLDBodyCombatState& Target,FLDBodyCombatState& Source,
        int64 NowMs,FString& Error){return Driver.OnFinalHit(Hit,Target,&Source,true,false,NowMs,Error);}
    static FLDCaptureReducer& Capture(ALDGameMode& Mode){return Mode.CaptureReducer;}
    static void FinalCentre(ALDGameMode& Mode,const FVector& Centre){Mode.CaptureCircle.CenterCm=FVector2D(Centre.X,Centre.Y);}
    static bool Showdown(ALDGameMode& Mode,const FLDDemoContentDefinitions& Content,FString& Error)
    {
        FLDTimelineParameters Parameters;
        if(!FLDTimelineParameters::FromContent(Content.Parameters,Parameters,Error)||!Mode.Timeline.Begin(Parameters,0,Error)){return false;}
        for(int32 Index=0;Index<32&&!Mode.IsShowdown();++Index)
        {
            TArray<FLDTimelineEvent> Events;
            if(!Mode.Timeline.Advance(Mode.Timeline.NextDueMs(),0,true,true,Events,Error)){return false;}
        }
        if(!Mode.IsShowdown()){Error=TEXT("Fixture did not reach the real final field stage.");return false;}
        return true;
    }
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
        return Driver.ReleaseMechanics(Body, Source, Terms, Origin, Point, (Point-Origin).GetSafeNormal2D(), FGuid::NewGuid(), 0, 0, MakeShared<FLDCombatCastLedger>(), Error);
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
    static TArray<FLDCombatLockedLedger>& Locked(ALDCombatDirector& Driver) { return Driver.LockedLedgers; }
    static bool Watch(ALDCombatDirector& Driver, ALDCharacter& Target, FLDBodyCombatState& Body, const FLDCombatHit& Hit, int64 NowMs,
        FString& Error)
    { return Driver.WatchKnockbackImpact(Target, Body, Hit, FVector::ForwardVector, NowMs, Error); }
    static bool Impact(ALDCombatDirector& Driver, FLDBodyCombatState& Body, const FLDCombatKnockbackImpact& Impact, bool bBody,
        const ALDCharacter* Blocker, int64 NowMs, FString& Error)
    { return Driver.ResolveKnockbackImpact(Body, Impact, bBody, Blocker, NowMs, Error); }
    static bool FinalImpact(ALDCombatDirector& Driver, const FLDCombatKnockbackImpact& Impact,
        const ALDCharacter& Blocker, const FVector& Direction, int64 NowMs, FString& Error)
    { return Driver.ResolveFinalBowlingImpact(Impact, &Blocker, Direction, NowMs, Error); }
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
    static void ReactionPK(ALDGameMode& Mode, ALDPKDirector& PK, const FLDCombatBodyRef& Setter,
        const FLDCombatBodyRef& Mate, ALDCharacter& MateActor, const FLDCombatBodyRef& Target,
        const FLDCombatBodyRef& Trigger, const FVector& MateFoot)
    {
        Mode.PKDirector = &PK;
        PK.Mode = &Mode;
        PK.bArenaActive = true;
        PK.Entrants = {Setter.Victim.EntityId, Mate.Victim.EntityId, Target.Victim.EntityId, Trigger.Victim.EntityId};
        PK.Drawn.SideOfSeat = {1, 1, 2, 2};
        for (const FGuid& Entrant : PK.Entrants) { Mode.PKLives.Add(Entrant); }
        auto& Life = Mode.Lives.Add(Mate.Victim.EntityId);
        Life.Kind = ELDVictimKind::Contender;
        Life.Phase = ELDLifePhase::Alive;
        Life.Life = Mate.Victim.Life;
        Life.Body = Mate;
        Life.Actor = &MateActor;
        Life.MotionLastFoot = MateFoot;
    }
    static void MoveReactionTeammate(ALDGameMode& Mode, const FLDCombatBodyRef& Mate, const FVector& Foot)
    { Mode.Lives.FindChecked(Mate.Victim.EntityId).MotionLastFoot = Foot; }
    static void SetReactionSpectator(ALDGameMode& Mode, const FLDCombatBodyRef& Setter)
    { Mode.PKLives.Remove(Setter.Victim.EntityId); }
    static void EndReactionPK(ALDPKDirector& PK) { PK.bArenaActive = false; }
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
    const FLDCombatBodyRef Mate = F.AddBody();
    F.Place(Mate, 100, 0);
    auto* PK = F.World->SpawnActor<ALDPKDirector>();
    if (!TestNotNull(TEXT("The PK director exists"), PK)) { return false; }
    FAccess::ReactionPK(*F.Mode, *PK, Setter, Mate, F.Actor(Mate), Target, Trigger, F.Foot(Mate));
    F.Actor(Setter).SetActorLocation(FarActor);
    TestTrue(TEXT("A PK teammate's live sight authorizes the target and trigger"),
        FAccess::SendReaction(*F.Mode, Setter, Target, Trigger, Sent, Error) == ELDReactionAssistDelivery::Sent
        && Sent.Trigger == Trigger.Victim.EntityId);
    FAccess::MoveReactionTeammate(*F.Mode, Mate, FarFoot);
    TestTrue(TEXT("A stale teammate observation cannot disclose a reaction after sight is lost"),
        FAccess::SendReaction(*F.Mode, Setter, Target, Trigger, Sent, Error) == ELDReactionAssistDelivery::Unavailable
        && Error.IsEmpty());
    F.Body(Target).ExposedUntilMs = 1001;
    TestTrue(TEXT("An exposed PK target is presentable without disclosing an unseen trigger"),
        FAccess::SendReaction(*F.Mode, Setter, Target, Trigger, Sent, Error) == ELDReactionAssistDelivery::Sent
        && !Sent.Trigger.IsValid());
    F.Body(Trigger).ExposedUntilMs = 1001;
    TestTrue(TEXT("An exposed PK trigger may be identified"),
        FAccess::SendReaction(*F.Mode, Setter, Target, Trigger, Sent, Error) == ELDReactionAssistDelivery::Sent
        && Sent.Trigger == Trigger.Victim.EntityId);
    F.Body(Target).ExposedUntilMs = F.Body(Trigger).ExposedUntilMs = 0;
    FAccess::SetReactionSpectator(*F.Mode, Setter);
    TestTrue(TEXT("A PK spectator may see the arena's currently registered bodies"),
        FAccess::SendReaction(*F.Mode, Setter, Target, Trigger, Sent, Error) == ELDReactionAssistDelivery::Sent
        && Sent.Trigger == Trigger.Victim.EntityId);
    FAccess::EndReactionPK(*PK);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDFinalGroundBandTest,"Lockdown.Rules.DemoFinalMod.GroundBandTime",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDFinalGroundBandTest::RunTest(const FString& Parameters)
{
    using namespace LDDashContactRules;
    const FGroundBand Band{FVector(0,0,0),FVector(300,0,0),1.,2.,2.5};
    TOptional<FGroundBandShape> Shape;
    TArray<FContactInterval> Contacts;
    FString Error;
    TestTrue(TEXT("Only the part already walked exists"),SampleGroundBand(Band,1.5,Shape,Error)&&Shape.IsSet()
        &&Shape->FromCm.Equals(FVector(0,0,0))&&Shape->ToCm.Equals(FVector(150,0,0)));
    TestTrue(TEXT("Each old part expires without extending the entire trail"),SampleGroundBand(Band,3.75,Shape,Error)
        &&Shape.IsSet()&&Shape->FromCm.Equals(FVector(75,0,0))&&Shape->ToCm.Equals(FVector(300,0,0)));
    TestTrue(TEXT("The final expiry is half-open"),SampleGroundBand(Band,4.5,Shape,Error)&&!Shape.IsSet());
    TestTrue(TEXT("Crossing a future segment does not count"),GroundBandContacts(FVector(200,-100,0),FVector(200,100,0),
        1.,1.4,Band,45,Contacts,Error)&&Contacts.IsEmpty());
    TestTrue(TEXT("A full crossing between endpoints is retained"),GroundBandContacts(FVector(200,-100,0),FVector(200,100,0),
        2.1,2.3,Band,45,Contacts,Error)&&!Contacts.IsEmpty()
        &&FMath::IsNearlyEqual(Contacts[0].FromSeconds,2.155,.000001));
    TestTrue(TEXT("A stationary enemy can be reached by new ground"),GroundBandContacts(FVector(200,0,0),FVector(200,0,0),
        1.,2.,Band,45,Contacts,Error)&&!Contacts.IsEmpty()
        &&FMath::IsNearlyEqual(Contacts[0].FromSeconds,1.+155./300.,.000001));
    TestTrue(TEXT("Walking across an expired tail does not count"),GroundBandContacts(FVector(0,-100,0),FVector(0,100,0),
        3.9,4.3,Band,45,Contacts,Error)&&Contacts.IsEmpty());
    TestTrue(TEXT("Touching only at the final expiry cannot enter"),GroundBandContacts(FVector(400,0,0),FVector(345,0,0),
        4.4,4.5,Band,45,Contacts,Error)&&Contacts.IsEmpty());
    TestFalse(TEXT("Reversed native time is an explicit error"),GroundBandContacts(FVector::ZeroVector,FVector::ZeroVector,
        2.,1.,Band,45,Contacts,Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDFinalUniversalCombatTest,"Lockdown.Rules.DemoFinalMod.UniversalCombat",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDFinalUniversalCombatTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if(!F.Init(*this,true)){return false;}
    FString Error;
    if(!FAccess::Showdown(*F.Mode,F.Catalog->Get(),Error)){AddError(Error);return false;}
    const auto A=F.AddBody(),B=F.AddBody(ELDVictimKind::Contender,1000000);
    F.Actor(A).SetActorLocation(F.Origin+FVector(0,0,88));
    F.Actor(B).SetActorLocation(F.Origin+FVector(100,0,88));
    FLDCombatEntityRecord& Record=FAccess::Records(*F.Driver).FindChecked(A.Victim.EntityId);
    Record.Build.FinalMod.DefinitionId=TEXT("final_open_mic");
    const auto Basic=[&]()
    {
        auto Hit=FFixture::Hit(A,B,10000);
        Hit.Source.Kind=ELDCombatSourceKind::Basic;Hit.HitClass=ELDCombatHitClass::BasicPrimary;
        Hit.DamageType=ELDDemoDamageType::Physical;
        return Hit;
    };
    const auto First=Basic();
    F.Now(1000);
    TestTrue(TEXT("First independent hit lands"),F.Apply(First,Error)&&FAccess::Zones(*F.Driver).IsEmpty());
    TestTrue(TEXT("Repeated notification cannot manufacture a second hit"),FAccess::FinalNotice(*F.Driver,First,F.Body(B),F.Body(A),1100,Error)
        &&FAccess::Zones(*F.Driver).IsEmpty());
    F.Now(1200);
    TestTrue(TEXT("Two real hits create the moving voice wave"),F.Apply(Basic(),Error)&&FAccess::Zones(*F.Driver).Num()==1);
    if(FAccess::Zones(*F.Driver).Num()!=1){AddError(Error);return false;}
    TestTrue(TEXT("The first wave has one immediate pulse and an owner movement cost"),
        FAccess::Zones(*F.Driver)[0].Pulsed==1&&!F.Body(A).TimedBonuses.IsEmpty());
    TestTrue(TEXT("Derived wave hits do not recursively create more waves"),F.Flush(Error)&&FAccess::Zones(*F.Driver).Num()==1);
    const FGuid Old=FAccess::Zones(*F.Driver)[0].EffectId;
    F.Now(1300);
    TestTrue(TEXT("An already consumed hit is not paired with the third"),F.Apply(Basic(),Error)
        &&FAccess::Zones(*F.Driver)[0].EffectId==Old);
    F.Now(1400);
    TestTrue(TEXT("The next pair replaces rather than stacks or adds an immediate pulse"),F.Apply(Basic(),Error)
        &&FAccess::Zones(*F.Driver).Num()==1&&FAccess::Zones(*F.Driver)[0].EffectId!=Old
        &&FAccess::Zones(*F.Driver)[0].Pulsed==0&&FAccess::Zones(*F.Driver)[0].DelayMs==800);
    FAccess::Zones(*F.Driver).Reset();
    Record.Build.FinalMod.DefinitionId=TEXT("final_landlord");
    F.Body(A).AttributeProjection.Final.ADP2=10000;
    FAccess::FinalCentre(*F.Mode,F.Origin);
    auto& Capture=FAccess::Capture(*F.Mode);
    TestTrue(TEXT("Capture begins"),Capture.Begin(0,3500,20000,Error));
    for(const auto Ref:{A,B})
    {
        FLDCaptureEvent Join;Join.Kind=ELDCaptureEventKind::Joined;Join.Body=Ref;Join.bInside=Ref==A;
        Join.Time.Seconds=0.;
        TestTrue(TEXT("Current lives join capture"),Capture.Enqueue(Join,Error));
    }
    TOptional<FLDCaptureProposal> Proposal;
    TestTrue(TEXT("The real spray reducer establishes the landlord"),Capture.CloseBoundary(3500,Proposal,Error)
        &&Capture.GetGraffitiOwner()==A.Victim.EntityId);
    F.Now(4000);
    const int64 Before=F.Body(B).Resources.HpP2;
    TestTrue(TEXT("Holding the flag reduces the actual main strike"),F.Apply(Basic(),Error)
        &&F.Body(B).Resources.HpP2==Before-8000);
    TestTrue(TEXT("Successful strike starts the delayed flag-centred shock"),FAccess::Zones(*F.Driver).Num()==1
        &&FAccess::Zones(*F.Driver)[0].DelayMs==700&&FAccess::Zones(*F.Driver)[0].Pulsed==0);
    F.Now(4700);
    TestTrue(TEXT("The shock damages at its real delayed instant"),FAccess::AdvanceMechanics(*F.Driver,4700,Error)&&F.Flush(Error)
        &&F.Body(B).Resources.HpP2==Before-8000-9000);
    F.Now(4800);
    TestTrue(TEXT("Another basic cannot bypass the six-second trigger interval"),F.Apply(Basic(),Error)
        &&!FAccess::Zones(*F.Driver).ContainsByPredicate([](const FLDCombatZone& Zone){return Zone.StartMs==4800;}));
    auto& Challenger=FAccess::Records(*F.Driver).FindChecked(B.Victim.EntityId);
    Challenger.Build.FinalMod.DefinitionId=TEXT("final_comeback");
    FLDCaptureEvent Enter;Enter.Body=B;Enter.Kind=ELDCaptureEventKind::Joined;Enter.Time.Seconds=4.8;
    TestTrue(TEXT("The challenger begins outside"),F.Driver->NotifyFinalPresence(Enter,Error));
    Enter.Kind=ELDCaptureEventKind::Presence;Enter.bInside=true;Enter.Time.Seconds=5.;
    TestTrue(TEXT("Entering another live owner's core arms comeback"),F.Driver->NotifyFinalPresence(Enter,Error)
        &&F.Body(B).FinalMod.ArmedUntilMs==9000);
    F.Now(5000);
    auto ReturnHit=FFixture::Hit(B,A,10000);
    ReturnHit.Source.Kind=ELDCombatSourceKind::Basic;ReturnHit.HitClass=ELDCombatHitClass::BasicPrimary;
    ReturnHit.DamageType=ELDDemoDamageType::Physical;
    const int64 OwnerHp=F.Body(A).Resources.HpP2;
    TestTrue(TEXT("Comeback charges the triggering strike and creates its warning zone"),F.Apply(ReturnHit,Error)
        &&F.Body(A).Resources.HpP2==OwnerHp-8000&&Challenger.FinalModReadyAtMs==15000);
    TestTrue(TEXT("Comeback's first true pulse lands at the hit point"),F.Flush(Error)
        &&F.Body(A).Resources.HpP2==OwnerHp-8000-3000);
    F.Now(6000);
    TestTrue(TEXT("Comeback's second pulse lands one second later"),FAccess::AdvanceMechanics(*F.Driver,6000,Error)
        &&F.Flush(Error)&&F.Body(A).Resources.HpP2==OwnerHp-8000-6000
        &&Capture.GetGraffitiOwner()==A.Victim.EntityId);
    AddInfo(Error);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDFinalMechanicContractTest,"Lockdown.Rules.DemoFinalMod.MechanicContracts",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDFinalMechanicContractTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if(!F.Init(*this)){return false;}
    FString Error;
    if(!FAccess::Showdown(*F.Mode,F.Catalog->Get(),Error)){AddError(Error);return false;}
    const auto A=F.AddBody(),B=F.AddBody();
    auto& Record=FAccess::Records(*F.Driver).FindChecked(A.Victim.EntityId);
    Record.Build.FinalMod={TEXT("final_traffic"),ELDFinalModTargetKind::Dash,NAME_None};
    FLDCombatSourceRef Dash;Dash.Kind=ELDCombatSourceKind::Innate;Dash.DefinitionId=LDCombatSourceRules::InnateDashId();
    int64 Cooldown=7000;
    TestTrue(TEXT("Traffic adds its cost to the bound base cooldown"),FAccess::FinalCost(*F.Driver,Record,Dash,Cooldown,Error)&&Cooldown==11000);
    auto& Travel=F.Body(A).VoluntaryDash.Emplace();
    Travel.Source=Dash;Travel.EffectId=FGuid::NewGuid();Travel.EndMs=2000;
    auto Motion=MakeShared<FLDNativeMotionBatch>();
    Motion->Body=A;
    Motion->Step.StartServerSeconds=1.;Motion->Step.EndServerSeconds=1.1;
    Motion->StartFootCm=F.Foot(A);Motion->EndFootCm=Motion->StartFootCm+FVector(100,0,0);
    Motion->InitialSupport.Grounded=Motion->FinalSupport.Grounded=true;
    auto& Move=Motion->Records.AddDefaulted_GetRef();
    Move.Kind=ELDNativeMotionRecordKind::Movement;Move.RootMotion=true;
    Move.FromFootCm=Motion->StartFootCm;Move.ToFootCm=Motion->EndFootCm;
    Move.StartServerSeconds=1.;Move.EndServerSeconds=1.1;
    FAccess::CaptureSkillMotion(*F.Driver,Motion);
    F.Now(1100);
    TestTrue(TEXT("Native movement inherits grounded support without a redundant support payload"),
        FAccess::FinalStep(*F.Driver,1100,Error)&&FAccess::FinalTraffic(*F.Driver).Num()==1);
    auto Crossing=MakeShared<FLDNativeMotionBatch>();
    Crossing->Body=B;
    Crossing->Step.StartServerSeconds=1.1;Crossing->Step.EndServerSeconds=1.2;
    Crossing->StartFootCm=Motion->StartFootCm+FVector(50,-100,0);
    Crossing->EndFootCm=Motion->StartFootCm+FVector(50,100,0);
    Crossing->InitialSupport.Grounded=Crossing->FinalSupport.Grounded=true;
    auto& CrossMove=Crossing->Records.AddDefaulted_GetRef();
    CrossMove.Kind=ELDNativeMotionRecordKind::Movement;
    CrossMove.FromFootCm=Crossing->StartFootCm;CrossMove.ToFootCm=Crossing->EndFootCm;
    CrossMove.StartServerSeconds=1.1;CrossMove.EndServerSeconds=1.2;
    FAccess::CaptureSkillMotion(*F.Driver,Crossing);
    F.Now(1200);
    TestTrue(TEXT("A grounded enemy crossing the whole band receives its slow even when both endpoints are outside"),
        FAccess::FinalStep(*F.Driver,1200,Error)&&!F.Body(B).Slows.IsEmpty());
    auto Airborne=MakeShared<FLDNativeMotionBatch>(*Motion);
    Airborne->Step.StartServerSeconds=1.2;Airborne->Step.EndServerSeconds=1.3;
    Airborne->Records.Reset();
    auto& Takeoff=Airborne->Records.AddDefaulted_GetRef();
    Takeoff.Kind=ELDNativeMotionRecordKind::Support;
    Takeoff.FromFootCm=Takeoff.ToFootCm=Airborne->StartFootCm;
    Takeoff.StartServerSeconds=Takeoff.EndServerSeconds=1.2;
    auto& AirMove=Airborne->Records.AddDefaulted_GetRef();
    AirMove=Move;AirMove.StartServerSeconds=1.2;AirMove.EndServerSeconds=1.3;
    Airborne->FinalSupport.Grounded=false;
    FAccess::CaptureSkillMotion(*F.Driver,Airborne);
    F.Now(1300);
    TestTrue(TEXT("A support transition to airborne does not lay another traffic segment"),
        FAccess::FinalStep(*F.Driver,1300,Error)&&FAccess::FinalTraffic(*F.Driver).Num()==1
        &&FAccess::FinalTraffic(*F.Driver)[0].Segments.Num()==1);
    Record.Build.FinalMod={TEXT("final_breach"),ELDFinalModTargetKind::None,NAME_None};
    Cooldown=7000;
    TestTrue(TEXT("Breach adds the separate D cost"),FAccess::FinalCost(*F.Driver,Record,Dash,Cooldown,Error)&&Cooldown==10000);
    FLDCaptureEvent Presence;Presence.Kind=ELDCaptureEventKind::Joined;Presence.Body=A;Presence.Time.Seconds=1.;
    TestTrue(TEXT("Breach starts with actual outside presence"),F.Driver->NotifyFinalPresence(Presence,Error));
    Presence.Kind=ELDCaptureEventKind::Presence;Presence.bInside=true;Presence.Time.Seconds=4.;
    TestTrue(TEXT("Three continuous outside seconds arm the next basic window"),F.Driver->NotifyFinalPresence(Presence,Error)
        &&F.Body(A).FinalMod.ArmedUntilMs==7000);
    Record.Build.FinalMod={TEXT("final_split"),ELDFinalModTargetKind::Active,TEXT("active_hose_down")};
    FLDCombatZone Zone;Zone.SourceBody=A;Zone.SourceEntity=Zone.CreditOwner=A.Victim.EntityId;
    Zone.Source.Kind=ELDCombatSourceKind::Learned;Zone.Source.DefinitionId=TEXT("active_hose_down");
    Zone.StartMs=1000;Zone.EndMs=5000;Zone.IntervalMs=1000;Zone.Count=4;Zone.RadiusCm=200;
    Zone.BaseP2=10000;Zone.APRatioBp=2500;Zone.SourceAPP2=20000;Zone.EffectId=FGuid::NewGuid();
    FLDCombatAction Action;Action.Direction=FVector(1,0,0);
    bool Handled=false;
    TestTrue(TEXT("A placed zone becomes two independently observed zones"),FAccess::FinalSplit(*F.Driver,Zone,Action,1000,Handled,Error)
        &&Handled&&FAccess::Zones(*F.Driver).Num()==2);
    if(FAccess::Zones(*F.Driver).Num()!=2){AddError(Error);return false;}
    const auto& Left=FAccess::Zones(*F.Driver)[0];
    const auto& Right=FAccess::Zones(*F.Driver)[1];
    TestEqual(TEXT("Split reduces only fixed damage"),Left.BaseP2,int64(6000));
    TestEqual(TEXT("Split preserves the AP coefficient"),Left.APRatioBp,int64(2500));
    TestEqual(TEXT("Split preserves release-time AP"),Left.SourceAPP2,int64(20000));
    TestTrue(TEXT("Split scales the radius to 110 cm"),FMath::IsNearlyEqual(Left.RadiusCm,110.,1.e-9));
    TestEqual(TEXT("Split removes one second of lifetime"),Left.EndMs,int64(4000));
    TestEqual(TEXT("Split retains only pulses inside the shortened lifetime"),Left.Count,3);
    TestTrue(TEXT("The two zones have independent observation keys"),Left.ObservedKey!=Right.ObservedKey);
    TestTrue(TEXT("The two zones lie on opposite sides of the release direction"),
        Left.CentreCm.Equals(FVector(0,-140,0),1.e-9)&&Right.CentreCm.Equals(FVector(0,140,0),1.e-9));
    TestTrue(TEXT("A new release replaces the previous pair"),FAccess::FinalSplit(*F.Driver,Zone,Action,1000,Handled,Error)
        &&FAccess::Zones(*F.Driver).Num()==2);
    AddInfo(Error);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDFinalControlAndImpactTest,"Lockdown.Rules.DemoFinalMod.ControlAndImpact",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDFinalControlAndImpactTest::RunTest(const FString& Parameters)
{
    using namespace LDBuildPrimitivesTests;
    FFixture F;
    if(!F.Init(*this,true)){return false;}
    FString Error;
    if(!FAccess::Showdown(*F.Mode,F.Catalog->Get(),Error)){AddError(Error);return false;}
    const auto A=F.AddBody(),B=F.AddBody(),C=F.AddBody();
    auto& Record=FAccess::Records(*F.Driver).FindChecked(A.Victim.EntityId);
    Record.Build.FinalMod={TEXT("final_forced_exit"),ELDFinalModTargetKind::Active,TEXT("active_pants_down")};
    FAccess::FinalCentre(*F.Mode,F.Origin);
    auto& Capture=FAccess::Capture(*F.Mode);
    if(!Capture.Begin(0,3500,20000,Error)){AddError(Error);return false;}
    for(const auto Ref:{A,B,C})
    {
        FLDCaptureEvent Join;Join.Kind=ELDCaptureEventKind::Joined;Join.Body=Ref;Join.bInside=Ref==B;
        if(!Capture.Enqueue(Join,Error)){AddError(Error);return false;}
    }
    TOptional<FLDCaptureProposal> Proposal;
    if(!Capture.CloseBoundary(1000,Proposal,Error)){AddError(Error);return false;}
    F.Now(1000);
    FLDControlApplication Root;
    Root.Kind=ELDControlKind::Root;Root.DurationMs=2000;Root.EffectId=FGuid::NewGuid();
    Root.SourceBody=A;Root.SourceEntity=Root.CreditOwner=A.Victim.EntityId;
    Root.Source.Kind=ELDCombatSourceKind::Learned;Root.Source.DefinitionId=TEXT("active_pants_down");
    bool Applied=false;
    TestTrue(TEXT("The bound native control pays its 30% duration cost before DR"),
        F.Driver->CommitControl(F.Body(B),Root,1000,Applied,Error)&&Applied
        &&F.Body(B).Roots.Num()==1&&F.Body(B).Roots[0].ExpiresAtMs==2400);
    TestTrue(TEXT("Hard control never removes the target's valid core presence"),Capture.IsCommittedInside(B));
    TestTrue(TEXT("The exit cannot trigger before actual control expiry plus 250 ms"),
        FAccess::FinalStep(*F.Driver,2649,Error)&&FAccess::PendingHits(*F.Driver).IsEmpty());
    TestTrue(TEXT("A still-present target gets the delayed authoritative exit"),
        FAccess::FinalStep(*F.Driver,2650,Error)&&FAccess::PendingHits(*F.Driver).Num()==1);
    if(FAccess::PendingHits(*F.Driver).Num()!=1){AddError(Error);return false;}
    const auto Exit=FAccess::PendingHits(*F.Driver)[0];
    TestTrue(TEXT("The exit is a zero-damage outward push on the original life"),
        Exit.Target==B&&Exit.KnockbackCm==180.&&Exit.bFinalModDerived&&Exit.BaseP2==0
        &&Exit.Source.DefinitionId==TEXT("final_forced_exit"));
    TestTrue(TEXT("The target's eight-second cooldown is committed once"),
        Record.FinalModTargetCooldowns.Num()==1&&Record.FinalModTargetCooldowns[0].ReadyAtMs==10650);
    FAccess::PendingHits(*F.Driver).Reset();
    Record.Build.FinalMod={TEXT("final_bowling"),ELDFinalModTargetKind::Active,TEXT("active_elbow")};
    F.Body(A).AttributeProjection.Final.APP2=20000;
    auto Push=FFixture::Hit(A,B,0);
    Push.KnockbackCm=300;Push.KnockbackMs=250;
    Push.KnockbackSource.Kind=ELDCombatSourceKind::Learned;Push.KnockbackSource.DefinitionId=TEXT("active_elbow");
    TestTrue(TEXT("The real bound displacement arms its native body-impact watch"),
        FAccess::Watch(*F.Driver,F.Actor(B),F.Body(B),Push,3000,Error)&&F.Body(B).PendingImpact.IsSet());
    if(!F.Body(B).PendingImpact.IsSet()){AddError(Error);return false;}
    const auto Impact=F.Body(B).PendingImpact.GetValue();
    TestTrue(TEXT("A real impact direction emits bowling's damage and second push"),
        FAccess::FinalImpact(*F.Driver,Impact,F.Actor(C),FVector(0,1,0),3100,Error)
        &&FAccess::PendingHits(*F.Driver).Num()==1);
    if(FAccess::PendingHits(*F.Driver).Num()!=1){AddError(Error);return false;}
    const auto Bowled=FAccess::PendingHits(*F.Driver)[0];
    TestTrue(TEXT("Bowling preserves its AP term and the ordinary push source without recursive FIN"),
        Bowled.Target==C&&Bowled.BaseP2==4500&&Bowled.APRatioBp==3500&&Bowled.SourceAPP2==20000
        &&Bowled.DamageType==ELDDemoDamageType::Magical&&Bowled.KnockbackCm==140.
        &&Bowled.bFinalModDerived&&Bowled.KnockbackSource==Push.KnockbackSource
        &&Bowled.KnockbackAnchorCm.Equals(F.Foot(C)-FVector(0,1,0),1.e-9));
    AddInfo(Error);
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
    TestTrue(TEXT("The new fire's whole budget (six ticks of 2.25) settles at once as B's fire, then R = 20 from B"),
        Pending.Num() == 2 && Pending[0].bBurnTick && Pending[0].BaseP2 == 1350 && Pending[0].SourceEntity == B.Victim.EntityId
        && Pending[1].bStatusSettle && !Pending[1].bBurnTick && Pending[1].BaseP2 == 2000 && Pending[1].SourceEntity == B.Victim.EntityId
        && Pending[1].Target == Target);
    TestTrue(TEXT("B owns one fog, its next pulse one second later, and the reaction is reported with B as trigger"),
        FAccess::Fogs(*F.Driver).Num() == 1 && FAccess::Fogs(*F.Driver)[0].Owner.SourceEntity == B.Victim.EntityId
        && FAccess::Fogs(*F.Driver)[0].NextPulseMs == 2500 && FAccess::Reactions(*F.Driver).Num() == 1
        && FAccess::Reactions(*F.Driver)[0].Kind == ELDCombatReaction::Fog
        && FAccess::Reactions(*F.Driver)[0].TriggerEntity == B.Victim.EntityId);
    TestTrue(TEXT("The settles land"), F.Flush(Error) && Body.Resources.HpP2 == 100000 - 3350);

    FLDBodyCombatState& Other = F.Body(Second);
    FLDCombatHit TwoLayers = FFixture::Hit(A, Second, 0);
    TwoLayers.BurnStacks = 2;
    TestTrue(TEXT("Two fire layers of A"), F.Apply(TwoLayers, Error) && Other.Burn.Layers.Num() == 2);
    F.Now(2000);
    FLDCombatHit Water = FFixture::Hit(B, Second, 0);
    Water.bApplyWet = true;
    TestTrue(FString::Printf(TEXT("Water on fire: %s"), *Error), F.Apply(Water, Error));
    TestTrue(TEXT("A's held layers settle their remaining six ticks each to A; B's R follows; the new fog wets the target"),
        Pending.Num() == 2 && Pending[0].SourceEntity == A.Victim.EntityId && Pending[0].BaseP2 == 2700 && Pending[0].bBurnTick
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
                { TestTrue(TEXT("S21 applies attack slow and grievous"), !F.Body(Target).AttackSlows.IsEmpty()
                    && F.Body(Target).Grievous.RatioBp == 3500); }
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
const FLDCombatEffectBudget* BudgetOf(const FLDBodyCombatState& Body, const FGuid& EffectId, const TCHAR* Component)
{
    return Body.EffectBudgets.FindByPredicate([&](const FLDCombatEffectBudget& Row)
    { return Row.EffectId == EffectId && Row.Component == FName(Component); });
}
}

// Two C11 roots that feed one C08 store before it fires merge into one counter, spent once under the latest root;
// no second counter and no fresh root identity appear.
// C11's 30G per root is consumed by the root and its derived same-effect hits; the C08 counter it feeds spends no
// C11 budget and refills nothing, so the loop cannot reopen healing.
// R11 (spec §12.1): M01 spreads S21's damage around the struck body; the cut of heals and shields and the attack slow stay on it.
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
    TestTrue(TEXT("Only a full bar may overdrive"), !LDMomentum::OverdriveReady(F.Catalog->Get().Parameters.Overdrive, 9999)
        && LDMomentum::OverdriveReady(F.Catalog->Get().Parameters.Overdrive, 10000));
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
    TestEqual(TEXT("Rot (S21) doubles the cut's duration"), FindEffect(Over.Values, ELDDemoEffectOp::Grievous)->DurationMs,
        FindEffect(Plain.Values, ELDDemoEffectOp::Grievous)->DurationMs * 2);
    if (!OverdriveTerms(*this, F, Holder, TEXT("active_open_mic"), Plain, Over)) { return false; }
    TestTrue(TEXT("Gust (S22) pushes 300 cm on the last wave"), Over.FinalKnockbackTerms.IsSet()
        && Over.FinalKnockbackTerms->DistanceCm.Numerator == 300 * Over.FinalKnockbackTerms->DistanceCm.Denominator);
    if (!OverdriveTerms(*this, F, Holder, TEXT("active_elbow"), Plain, Over)) { return false; }
    TestFalse(TEXT("A skill without an element gains only the scale"), Over.FinalKnockbackTerms.IsSet()
        || FindEffect(Over.Values, ELDDemoEffectOp::Burn) || FindEffect(Over.Values, ELDDemoEffectOp::Slow));
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

#endif
