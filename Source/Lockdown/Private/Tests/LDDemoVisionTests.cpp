#include "Vision/LDViewTypes.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "UI/LDHUDModel.h"
#include "Character/LDFlightTrailComponent.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapDefinition.h"
#include "Framework/LDDemoEntry.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
#include "Misc/AutomationTest.h"
#include "UObject/CoreNet.h"
#include "UObject/UnrealType.h"
#include "Algo/Reverse.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
// Named scope keeps these fixtures and tests unique inside unity translation units.
namespace LDDemoVisionTests
{
namespace
{
FLDWorldRevision DemoRevision()
{
    FLDWorldRevision Revision;
    Revision.MapId = TEXT("demo_world");
    Revision.LayoutRevision = 1;
    Revision.GeometryRevision = 1;
    return Revision;
}

FLDObservedUnitView ContenderUnit(uint32 Epoch = 1)
{
    FLDObservedUnitView Unit;
    Unit.Handle = FGuid::NewGuid();
    Unit.ObservationEpoch = Epoch;
    Unit.StateRevision = 1;
    Unit.AuthorizedFieldMask = LDObservation::ContenderFields;
    Unit.Kind = ELDObservedUnitKind::Contender;
    Unit.Presentation = 2;
    Unit.FeetCm = FVector(100, -200, 2);
    Unit.Facing = 1234;
    Unit.ContenderId = FGuid::NewGuid();
    Unit.Level = 3;
    Unit.Health.CurrentP2 = 45000;
    Unit.Health.MaxP2 = 60000;
    Unit.Mana.CurrentP2 = 1000;
    Unit.Mana.MaxP2 = 30000;
    FLDObservedStatus Root;
    Root.StatusId = uint8(ELDDemoStatusId::Root);
    Root.ExpiresAtServerMs = 12345;
    Unit.Statuses.Add(Root);
    Unit.Actives.Abilities[0] = uint8(ELDDemoAbilityId::ActivePantsDown);
    Unit.Actives.Abilities[1] = uint8(ELDDemoAbilityId::ActiveSnarkBolt);
    Unit.Actives.Abilities[2] = uint8(ELDDemoAbilityId::ActiveNotMad);
    return Unit;
}

FLDObservedUnitView CampUnit(uint32 Epoch = 1)
{
    FLDObservedUnitView Unit;
    Unit.Handle = FGuid::NewGuid();
    Unit.ObservationEpoch = Epoch;
    Unit.StateRevision = 1;
    Unit.AuthorizedFieldMask = LDObservation::CampFields;
    Unit.Kind = ELDObservedUnitKind::CampMember;
    Unit.CreatureId = uint8(ELDDemoMonsterId::MonsterScavenger);
    Unit.FeetCm = FVector(-800, 900, 2);
    Unit.Health.CurrentP2 = 20000;
    Unit.Health.MaxP2 = 26000;
    FLDObservedStatus Returning;
    Returning.StatusId = uint8(ELDDemoStatusId::CampReturning);
    Unit.Statuses.Add(Returning);
    return Unit;
}

FLDWorldView ActiveFrame(uint32 Match, uint32 Generation, uint32 Revision)
{
    FLDWorldView View;
    View.Schema = LDObservation::Schema;
    View.MatchGeneration = Match;
    View.ObserverLife = 1;
    View.Generation = Generation;
    View.Revision = Revision;
    const FLDWorldRevision World = DemoRevision();
    View.WorldMapId = World.MapId;
    View.WorldLayoutRevision = World.LayoutRevision;
    View.WorldGeometryRevision = World.GeometryRevision;
    View.SampledAtServerSec = 12.5;
    View.Footprint.SourceStatus = ELDObservationSource::ActiveBody;
    View.Footprint.SourceFeetCm = FVector(0, 0, 2);
    View.Footprint.RadiusCm = 1400;
    View.Footprint.CurrentBits.Init(0, LDObservation::CoverageBytes);
    return View;
}

FLDObservedUnitTermination Death(const FLDObservedUnitView& Unit, uint32 Generation, uint32 Sequence)
{
    FLDObservedUnitTermination Termination;
    Termination.Handle = Unit.Handle;
    Termination.ObservationEpoch = Unit.ObservationEpoch;
    Termination.Generation = Generation;
    Termination.TerminationSequence = Sequence;
    Termination.OccurredAtServerMs = 4000;
    return Termination;
}

FLDObservedCatalog TestCatalog()
{
    FLDObservedCatalog Catalog;
    Catalog.MaxLevel = 16;
    Catalog.Creatures.Add(uint8(ELDDemoMonsterId::MonsterScavenger));
    Catalog.Creatures.Add(uint8(ELDDemoMonsterId::MonsterGuardian));
    for (const ELDDemoAbilityId Id : {
        ELDDemoAbilityId::ActivePantsDown, ELDDemoAbilityId::ActiveSnarkBolt, ELDDemoAbilityId::ActiveNotMad})
    {
        Catalog.Abilities.Add(uint8(Id));
    }
    return Catalog;
}

template<typename TWire>
bool Wire(TWire& Value, TWire& Out, int64* Bytes = nullptr)
{
    FNetBitWriter Writer(nullptr, 1 << 18);
    bool bOk = true;
    Value.NetSerialize(Writer, nullptr, bOk);
    if (!bOk || Writer.IsError()) { return false; }
    if (Bytes) { *Bytes = Writer.GetNumBytes(); }
    FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
    Out = TWire();
    Out.NetSerialize(Reader, nullptr, bOk);
    return bOk && !Reader.IsError();
}

template<typename TWire>
bool Encodes(TWire Value)
{
    TWire Out;
    return Wire(Value, Out);
}

FLDDemoRational Whole(int64 Value)
{
    FLDDemoRational Rational;
    Rational.Numerator = Value;
    Rational.Denominator = 1;
    return Rational;
}

FLDDemoWorldPoint Ground(int64 X, int64 Y)
{
    FLDDemoWorldPoint Point;
    Point.X = Whole(X);
    Point.Y = Whole(Y);
    Point.Z = Whole(0);
    return Point;
}

// West-east wall x[-100,100], y[-1000,1000]; low stall north of it does not block sight.
FLDDemoWorldGeometry TestGeometry()
{
    FLDDemoWorldGeometry Geometry;
    Geometry.MapId = TEXT("demo_world");
    Geometry.LayoutRevision = 1;
    Geometry.GeometryRevision = 1;
    FLDDemoWorldSurface Surface;
    Surface.Id = TEXT("demo_ground");
    Surface.MinCm = Ground(-5000, -5000);
    Surface.MaxCm = Ground(5000, 5000);
    Geometry.Surfaces.Add(Surface);
    Geometry.SightSampleHeightCm = Whole(120);
    Geometry.LevelStepCm = Whole(150);
    FLDDemoWorldBlocker Wall;
    Wall.Id = TEXT("wall");
    Wall.SurfaceId = Surface.Id;
    Wall.PolygonCm = {Ground(-100, -1000), Ground(100, -1000), Ground(100, 1000), Ground(-100, 1000)};
    Wall.HeightCm = Whole(600);
    Wall.BlocksMovement = true;
    Wall.BlocksSight = true;
    Geometry.Blockers.Add(Wall);
    FLDDemoWorldBlocker Stall;
    Stall.Id = TEXT("stall");
    Stall.SurfaceId = Surface.Id;
    Stall.PolygonCm = {Ground(-1500, 1500), Ground(-1300, 1500), Ground(-1300, 1700), Ground(-1500, 1700)};
    Stall.HeightCm = Whole(80);
    Stall.BlocksMovement = true;
    Stall.BlocksSight = false;
    Geometry.Blockers.Add(Stall);
    return Geometry;
}

FLDObservedTarget Target(const FVector& Foot, ELDVictimKind Kind = ELDVictimKind::Contender)
{
    FLDObservedTarget Out;
    Out.Body.Victim.Kind = Kind;
    Out.Body.Victim.EntityId = FGuid::NewGuid();
    Out.Body.Victim.Life = 1;
    Out.Body.BodyIdentity = FGuid::NewGuid();
    Out.FootCm = Foot;
    Out.Presented = Kind == ELDVictimKind::Contender ? ContenderUnit() : CampUnit();
    Out.Presented.Handle.Invalidate();
    Out.Presented.ObservationEpoch = 0;
    Out.Presented.StateRevision = 0;
    Out.Presented.FeetCm = Foot;
    if (Kind == ELDVictimKind::Contender) { Out.Presented.ContenderId = Out.Body.Victim.EntityId; }
    return Out;
}

FLDMoveIdentity Move(uint32 Order, uint32 Epoch = 0)
{
    FLDMoveIdentity Identity;
    Identity.Match = 7;
    Identity.Life = 1;
    Identity.Order = Order;
    Identity.Epoch = Epoch;
    return Identity;
}

bool HasReason(const TArray<FLDObservedClear>& Cleared, const FGuid& Handle, ELDObservedClearReason Reason)
{
    return Cleared.ContainsByPredicate([&Handle, Reason](const FLDObservedClear& Clear) { return Clear.Handle == Handle && Clear.Reason == Reason; });
}

// Contender units carry their public ContenderId, so fixtures follow a target without relying on frame order.
FLDObservedUnitView UnitOf(const TArray<FLDObservedUnitView>& Units, const FLDObservedTarget& Of)
{
    const FLDObservedUnitView* Found = Units.FindByPredicate([&Of](const FLDObservedUnitView& Unit) { return Unit.ContenderId == Of.Body.Victim.EntityId; });
    return Found ? *Found : FLDObservedUnitView();
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoVisionCodecTest, "Lockdown.Rules.DemoVision.Codec",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoVisionCodecTest::RunTest(const FString& Parameters)
{
    // FVector_NetQuantize10 rounds each component to 0.1 cm.
    constexpr double FeetToleranceCm = 0.05 + UE_KINDA_SMALL_NUMBER;
    FLDWorldView Full = ActiveFrame(7, 3, 41);
    Full.ObserverLife = 5;
    Full.Footprint.SourceFeetCm = FVector(-12.34, 56.78, 2.04);
    LDObservation::SetCoverageBit(Full.Footprint.CurrentBits, 0);
    LDObservation::SetCoverageBit(Full.Footprint.CurrentBits, LDObservation::CoverageCells - 1);
    for (int32 Index = 0; Index < LDObservation::MaxUnits; ++Index)
    {
        FLDObservedUnitView Unit = Index % 2 ? CampUnit(Index + 1) : ContenderUnit(Index + 1);
        // Distinct per unit, and the revision never equals the epoch, so dropped or swapped fields cannot pass.
        Unit.StateRevision = uint32(1000 + Index);
        Unit.FeetCm = FVector(100.37 + Index * 211, -200.62 - Index * 97, 2.04);
        Unit.Facing = uint16(1234 + Index);
        if (Index == 3)
        {
            Unit.AuthorizedFieldMask |= LDObservation::Mana;
            Unit.Mana.CurrentP2 = 0;
            Unit.Mana.MaxP2 = 5000;
        }
        Full.Units.Add(Unit);
    }
    for (uint32 Sequence = 1; Sequence <= uint32(LDObservation::MaxTerminations); ++Sequence)
    { Full.RecentTerminations.Add(Death(ContenderUnit(100 + Sequence), 3, Sequence)); }
    FLDWorldView Read;
    int64 Bytes = 0;
    TestTrue(TEXT("Bounded full view round-trips"), Wire(Full, Read, &Bytes));
    TestTrue(TEXT("Full view stays inside its derived capacity"), Bytes > 0 && Bytes < 8192);
    AddInfo(FString::Printf(TEXT("Maximum observation frame: %lld bytes"), Bytes));
    TestEqual(TEXT("Units complete"), Read.Units.Num(), LDObservation::MaxUnits);
    TestEqual(TEXT("Terminations complete"), Read.RecentTerminations.Num(), LDObservation::MaxTerminations);
    TestTrue(TEXT("Coverage is the exact fixed bitmap"), Read.Footprint.CurrentBits == Full.Footprint.CurrentBits
        && LDObservation::CoverageBit(Read.Footprint.CurrentBits, 0) && LDObservation::CoverageBit(Read.Footprint.CurrentBits, LDObservation::CoverageCells - 1)
        && (Read.Footprint.CurrentBits[0] & 1) != 0 && (Read.Footprint.CurrentBits[LDObservation::CoverageBytes - 1] & 0x80) != 0);
    TestTrue(TEXT("World revision identity survives"), Read.WorldMapId == DemoRevision().MapId && Read.WorldLayoutRevision == 1 && Read.WorldGeometryRevision == 1);
    TestTrue(TEXT("Frame cursor identity survives exactly"), Read.Schema == LDObservation::Schema && Read.MatchGeneration == 7 && Read.ObserverLife == 5
        && Read.Generation == 3 && Read.Revision == 41 && Read.SampledAtServerSec == Full.SampledAtServerSec);
    TestTrue(TEXT("Footprint source survives within the quantum"), Read.Footprint.SourceStatus == ELDObservationSource::ActiveBody
        && Read.Footprint.RadiusCm == Full.Footprint.RadiusCm && Read.Footprint.SourceFeetCm.Equals(Full.Footprint.SourceFeetCm, FeetToleranceCm));
    bool UnitsKeepIdentity = Read.Units.Num() == Full.Units.Num();
    for (int32 Index = 0; UnitsKeepIdentity && Index < Full.Units.Num(); ++Index)
    {
        const FLDObservedUnitView& Sent = Full.Units[Index];
        const FLDObservedUnitView& Got = Read.Units[Index];
        UnitsKeepIdentity = Got.Handle == Sent.Handle && Got.ObservationEpoch == Sent.ObservationEpoch && Got.StateRevision == Sent.StateRevision
            && Got.AuthorizedFieldMask == Sent.AuthorizedFieldMask && Got.Kind == Sent.Kind && Got.Presentation == Sent.Presentation
            && Got.Facing == Sent.Facing && Got.FeetCm.Equals(Sent.FeetCm, FeetToleranceCm);
    }
    TestTrue(TEXT("Every unit keeps its opaque handle, epoch, state revision and quantized feet"), UnitsKeepIdentity);
    bool TerminationsKeepIdentity = Read.RecentTerminations.Num() == Full.RecentTerminations.Num();
    for (int32 Index = 0; TerminationsKeepIdentity && Index < Full.RecentTerminations.Num(); ++Index)
    {
        const FLDObservedUnitTermination& Sent = Full.RecentTerminations[Index];
        const FLDObservedUnitTermination& Got = Read.RecentTerminations[Index];
        TerminationsKeepIdentity = Got.Handle == Sent.Handle && Got.ObservationEpoch == Sent.ObservationEpoch && Got.Generation == Sent.Generation
            && Got.TerminationSequence == Sent.TerminationSequence && Got.OccurredAtServerMs == Sent.OccurredAtServerMs && Got.Kind == Sent.Kind;
    }
    TestTrue(TEXT("Every termination keeps its handle, epoch, generation and sequence"), TerminationsKeepIdentity);
    {
        // Corners of the registered Demo world, not a test-local box, must survive the same quantum.
        FString WorldError;
        TSharedPtr<const FLDDemoContentSnapshot> Content;
        TSharedPtr<const FLDDemoWorldGeometry> Registered;
        FVector Low = FVector::ZeroVector, High = FVector::ZeroVector;
        auto ReadRegisteredBounds = [&]() -> bool
        {
            const auto* Definition = LoadObject<ULDMapDefinition>(nullptr, *(FString(LDDemoEntry::WorldDefinition) + TEXT(".DA_DemoWorld")));
            if (!Definition) { WorldError = TEXT("DA_DemoWorld did not load."); return false; }
            if (!LDDemoEntry::LoadRuntimeContent(Content, WorldError) || !Definition->Read(*Content, Registered, WorldError)) { return false; }
            if (Registered->Surfaces.Num() != 1) { WorldError = TEXT("The Demo world is not a single surface."); return false; }
            return Registered->Surfaces[0].MinCm.ToVector(Low, WorldError) && Registered->Surfaces[0].MaxCm.ToVector(High, WorldError);
        };
        if (!ReadRegisteredBounds())
        {
            AddError(TEXT("The registered Demo world is required for its boundary round trip: ") + WorldError);
        }
        else
        {
            const TArray<FVector> Corners = {Low, High, FVector(Low.X, High.Y, Low.Z), FVector(High.X, Low.Y, High.Z)};
            FLDWorldView Boundary = ActiveFrame(7, 3, 42);
            Boundary.Footprint.SourceFeetCm = High;
            for (int32 Index = 0; Index < Corners.Num(); ++Index)
            {
                FLDObservedUnitView Unit = Index % 2 ? CampUnit(Index + 1) : ContenderUnit(Index + 1);
                Unit.FeetCm = Corners[Index];
                Boundary.Units.Add(Unit);
            }
            FLDWorldView BoundaryRead;
            bool CornersSurvive = Wire(Boundary, BoundaryRead) && BoundaryRead.Units.Num() == Corners.Num()
                && BoundaryRead.Footprint.SourceFeetCm.Equals(High, FeetToleranceCm);
            for (int32 Index = 0; CornersSurvive && Index < Corners.Num(); ++Index)
            {
                CornersSurvive = BoundaryRead.Units[Index].Handle == Boundary.Units[Index].Handle
                    && BoundaryRead.Units[Index].FeetCm.Equals(Corners[Index], FeetToleranceCm);
            }
            TestTrue(TEXT("Registered world boundary corners round-trip within the quantum"), CornersSurvive);
        }
    }
    const FLDObservedUnitView& Contender = Read.Units[0];
    TestTrue(TEXT("Contender whitelist survives exactly"), Contender.ContenderId == Full.Units[0].ContenderId && Contender.Level == 3
        && Contender.Health.CurrentP2 == 45000 && Contender.Health.MaxP2 == 60000 && Contender.Mana.MaxP2 == 30000
        && Contender.Statuses.Num() == 1);
    TestTrue(TEXT("Camp NotApplicable fields stay absent"), Read.Units[1].Level == 0 && Read.Units[1].Mana.MaxP2 == 0
        && Read.Units[1].CreatureId == uint8(ELDDemoMonsterId::MonsterScavenger));
    TestTrue(TEXT("Defined camp mana is a closed pair"), Read.Units[3].Mana.MaxP2 == 5000);

    auto Mutated = [&](TFunctionRef<void(FLDWorldView&)> Change)
    {
        FLDWorldView Candidate = ActiveFrame(7, 3, 41);
        Candidate.Units.Add(ContenderUnit());
        Candidate.Units.Add(CampUnit());
        Change(Candidate);
        return Candidate;
    };
    TestTrue(TEXT("Baseline mutation frame encodes"), Encodes(Mutated([](FLDWorldView&) {})));
    TestFalse(TEXT("Unknown schema refused"), Encodes(Mutated([](FLDWorldView& V) { V.Schema = LDObservation::Schema + 1; })));
    TestFalse(TEXT("The retired schema 1 is refused"), Encodes(Mutated([](FLDWorldView& V) { V.Schema = 1; })));
    TestFalse(TEXT("The retired schema 2 is refused"), Encodes(Mutated([](FLDWorldView& V) { V.Schema = 2; })));
    TestFalse(TEXT("The retired schema 3 is refused"), Encodes(Mutated([](FLDWorldView& V) { V.Schema = 3; })));
    TestFalse(TEXT("The uncompressed schema 5 is refused"), Encodes(Mutated([](FLDWorldView& V) { V.Schema = 5; })));
    TestFalse(TEXT("Capacity is never truncated"), Encodes(Mutated([](FLDWorldView& V)
    { while (V.Units.Num() <= LDObservation::MaxUnits) { V.Units.Add(ContenderUnit()); } })));
    TestFalse(TEXT("Contender without level is not complete"), Encodes(Mutated([](FLDWorldView& V)
    { V.Units[0].AuthorizedFieldMask &= ~uint32(LDObservation::Level); })));
    TestFalse(TEXT("Missing level is not a default"), Encodes(Mutated([](FLDWorldView& V) { V.Units[0].Level = 0; })));
    TestFalse(TEXT("Unknown status refused"), Encodes(Mutated([](FLDWorldView& V) { V.Units[0].Statuses[0].StatusId = 200; })));
    TestFalse(TEXT("Duplicate status refused"), Encodes(Mutated([](FLDWorldView& V)
    { const FLDObservedStatus Copy = V.Units[0].Statuses[0]; V.Units[0].Statuses.Add(Copy); })));
    TestFalse(TEXT("Returning cue is camp-only"), Encodes(Mutated([](FLDWorldView& V)
    { V.Units[0].Statuses[0].StatusId = uint8(ELDDemoStatusId::CampReturning); V.Units[0].Statuses[0].ExpiresAtServerMs = 0; })));
    TestFalse(TEXT("Returning cue has no stacks"), Encodes(Mutated([](FLDWorldView& V) { V.Units[1].Statuses[0].Stacks = 2; })));
    TestFalse(TEXT("Returning cue has no expiry"), Encodes(Mutated([](FLDWorldView& V) { V.Units[1].Statuses[0].ExpiresAtServerMs = 4200; })));
    FLDObservedStatus Protection;
    Protection.StatusId = uint8(ELDDemoStatusId::RespawnProtection);
    Protection.ExpiresAtServerMs = 15000;
    TestTrue(TEXT("Respawn protection is a timed public contender status"), Encodes(Mutated([&Protection](FLDWorldView& V)
    { V.Units[0].Statuses.Add(Protection); })));
    TestFalse(TEXT("Respawn protection is contender-only"), Encodes(Mutated([&Protection](FLDWorldView& V)
    { V.Units[1].Statuses.Add(Protection); })));
    TestFalse(TEXT("Respawn protection does not stack"), Encodes(Mutated([&Protection](FLDWorldView& V)
    { FLDObservedStatus Stacked = Protection; Stacked.Stacks = 2; V.Units[0].Statuses.Add(Stacked); })));
    TestFalse(TEXT("Respawn protection is always timed"), Encodes(Mutated([&Protection](FLDWorldView& V)
    { FLDObservedStatus Untimed = Protection; Untimed.ExpiresAtServerMs = 0; V.Units[0].Statuses.Add(Untimed); })));
    TestFalse(TEXT("Resources are closed pairs"), Encodes(Mutated([](FLDWorldView& V) { V.Units[0].Health.CurrentP2 = V.Units[0].Health.MaxP2 + 1; })));
    TestFalse(TEXT("Unauthorized camp mana refused"), Encodes(Mutated([](FLDWorldView& V) { V.Units[1].Mana.MaxP2 = 10; })));
    TestFalse(TEXT("Camp level refused"), Encodes(Mutated([](FLDWorldView& V) { V.Units[1].Level = 1; })));
    TestFalse(TEXT("Unknown creature refused"), Encodes(Mutated([](FLDWorldView& V) { V.Units[1].CreatureId = 5; })));
    TestFalse(TEXT("Unknown action cue refused"), Encodes(Mutated([](FLDWorldView& V) { V.Units[0].ActionCue = static_cast<ELDObservedActionCue>(4); })));
    TestFalse(TEXT("A cue age without a cue refused"), Encodes(Mutated([](FLDWorldView& V) { V.Units[0].ActionCueAgeMs = 120; })));
    TestFalse(TEXT("Unknown unit kind refused"), Encodes(Mutated([](FLDWorldView& V) { V.Units[0].Kind = static_cast<ELDObservedUnitKind>(3); })));
    TestFalse(TEXT("Contender presentation outside the six roster seats refused"), Encodes(Mutated([](FLDWorldView& V) { V.Units[0].Presentation = LDObservation::MaxPresentations; })));
    TestFalse(TEXT("Camp members carry no contender presentation"), Encodes(Mutated([](FLDWorldView& V) { V.Units[1].Presentation = 1; })));
    TestFalse(TEXT("Positions beyond the finite observation range refused"), Encodes(Mutated([](FLDWorldView& V) { V.Units[0].FeetCm = FVector(1000001, 0, 2); })));
    TestFalse(TEXT("Duplicate handle refused"), Encodes(Mutated([](FLDWorldView& V) { V.Units[1].Handle = V.Units[0].Handle; })));
    TestFalse(TEXT("No living source cannot carry units"), Encodes(Mutated([](FLDWorldView& V)
    { V.Footprint = FLDObservedFootprint(); })));
    TestFalse(TEXT("Partial coverage refused"), Encodes(Mutated([](FLDWorldView& V) { V.Footprint.CurrentBits.SetNum(LDObservation::CoverageBytes - 1); })));
    TestFalse(TEXT("Malformed world identity refused"), Encodes(Mutated([](FLDWorldView& V) { V.WorldMapId = TEXT("demo world"); })));
    TestFalse(TEXT("Termination of another generation refused"), Encodes(Mutated([](FLDWorldView& V)
    { V.RecentTerminations.Add(Death(ContenderUnit(), 2, 1)); })));
    TestFalse(TEXT("Terminations stay ordered"), Encodes(Mutated([](FLDWorldView& V)
    { V.RecentTerminations.Add(Death(ContenderUnit(), 3, 2)); V.RecentTerminations.Add(Death(ContenderUnit(), 3, 2)); })));
    TestFalse(TEXT("A terminated handle is not also present"), Encodes(Mutated([](FLDWorldView& V)
    { V.RecentTerminations.Add(Death(V.Units[0], 3, 1)); })));
    FLDWorldView Dead = ActiveFrame(7, 4, 42);
    Dead.Footprint = FLDObservedFootprint();
    TestTrue(TEXT("NoLivingBody is an explicit empty frame"), Encodes(Dead));

    for (int32 Pattern = 0; Pattern < 4; ++Pattern)
    {
        FLDWorldView Coverage = ActiveFrame(7,3,50+Pattern);
        for (int32 Index = 0; Index < LDObservation::CoverageBytes; ++Index)
        {
            Coverage.Footprint.CurrentBits[Index] = Pattern==0 ? 0 : Pattern==1 ? 255 : Pattern==2 ? uint8(Index) : 0;
        }
        if (Pattern==3)
        {
            for (int32 Y=0;Y<LDObservation::CoverageSide;++Y)
            {
                for (int32 X=0;X<LDObservation::CoverageSide;++X)
                {
                    if (FMath::Square(X-64)+FMath::Square(Y-64)<=14*14)
                    { LDObservation::SetCoverageBit(Coverage.Footprint.CurrentBits,Y*LDObservation::CoverageSide+X); }
                }
            }
            for (uint32 Index=1;Index<=5;++Index) { Coverage.Units.Add(ContenderUnit(Index)); }
        }
        FLDWorldView Decoded;
        int64 CoverageSize=0;
        TestTrue(TEXT("Empty, filled, incompressible and circular coverage round-trip exactly"),
            Wire(Coverage,Decoded,&CoverageSize) && Decoded.Footprint.CurrentBits==Coverage.Footprint.CurrentBits);
        TestTrue(TEXT("Incompressible coverage is bounded; ordinary six-player coverage fits below a packet payload"),
            CoverageSize < (Pattern==2 ? LDObservation::CoverageBytes+128 : 1100));
        AddInfo(FString::Printf(TEXT("Coverage pattern %d observation frame: %lld bytes"),Pattern,CoverageSize));
    }

    auto InvalidCoverage = [this](uint16 EncodedBytes, int32 Pairs, uint8 RunLength)
    {
        FNetBitWriter Writer(nullptr,1<<18);
        uint32 Schema=LDObservation::Schema, Match=7, Life=1, Generation=3, Revision=60, Layout=1, Geometry=1;
        uint8 Length=10, Source=uint8(ELDObservationSource::ActiveBody);
        double Sampled=1;
        ANSICHAR MapText[]="demo_world";
        FVector_NetQuantize10 Feet(FVector(0,0,2));
        bool bFeet=true;
        float Radius=1400;
        Writer << Schema << Match << Life << Generation << Revision << Length;
        Writer.Serialize(MapText,Length);
        Writer << Layout << Geometry << Sampled << Source;
        Feet.NetSerialize(Writer,nullptr,bFeet);
        Writer << Radius << EncodedBytes;
        for (int32 Index=0;Index<Pairs;++Index) { uint8 Value=255; Writer << RunLength << Value; }
        FNetBitReader Reader(nullptr,Writer.GetData(),Writer.GetNumBits());
        FLDWorldView Decoded;
        bool bAccepted=true;
        Decoded.NetSerialize(Reader,nullptr,bAccepted);
        TestFalse(TEXT("Malformed compressed coverage is a protocol error, never a partial-success frame"),bAccepted);
        TestTrue(TEXT("Rejected compressed coverage marks the archive failed"),Reader.IsError());
    };
    InvalidCoverage(3,0,0);
    InvalidCoverage(LDObservation::CoverageBytes,0,0);
    InvalidCoverage(2,1,0);
    InvalidCoverage(18,9,255);
    TArray<uint8> TruncatedRuns, TruncatedBits;
    TruncatedRuns.Init(255,14);
    TestFalse(TEXT("A truncated coverage run list cannot fabricate the missing coverage"),
        LDObservation::DecodeCoverageRuns(TruncatedRuns,TruncatedBits));

    FNetBitWriter Hostile(nullptr, 4096);
    uint32 Schema = LDObservation::Schema, Match = 7, Life = 1, Generation = 3, Revision = 9, Layout = 1, Geometry = 1;
    double Sampled = 1;
    uint8 Length = 10, Source = uint8(ELDObservationSource::NoLivingBody), Count = uint8(LDObservation::MaxUnits + 1);
    ANSICHAR MapText[] = "demo_world";
    Hostile << Schema << Match << Life << Generation << Revision << Length;
    Hostile.Serialize(MapText, Length);
    Hostile << Layout << Geometry << Sampled << Source << Count;
    FNetBitReader HostileReader(nullptr, Hostile.GetData(), Hostile.GetNumBits());
    FLDWorldView HostileView;
    bool bHostile = true;
    HostileView.NetSerialize(HostileReader, nullptr, bHostile);
    TestFalse(TEXT("Hostile unit count is refused before allocation"), bHostile);
    TestTrue(TEXT("Refused frame allocated no units"), HostileView.Units.Num() == 0);

    FLDObservedConceal Conceal;
    Conceal.MatchGeneration = 7;
    Conceal.Generation = 3;
    Conceal.Revision = 43;
    Conceal.Removed.Add(FGuid::NewGuid());
    FLDObservedConceal ConcealRead;
    TestTrue(TEXT("Concealment round-trips"), Wire(Conceal, ConcealRead) && ConcealRead.MatchGeneration == 7 && ConcealRead.Generation == 3
        && ConcealRead.Revision == 43 && ConcealRead.Removed == Conceal.Removed);
    const FGuid RepeatedHandle = Conceal.Removed[0];
    Conceal.Removed.Add(RepeatedHandle);
    TestFalse(TEXT("Duplicate concealment refused"), Encodes(Conceal));
    Conceal.Removed.Reset();
    TestTrue(TEXT("Empty concealment is the reliable generation reset"), Encodes(Conceal));
    FLDObservedTerminationNotice Notice;
    Notice.MatchGeneration = 7;
    Notice.Revision = 44;
    Notice.Termination = Death(ContenderUnit(5), 3, 1);
    FLDObservedTerminationNotice NoticeRead;
    TestTrue(TEXT("Termination notice round-trips"), Wire(Notice, NoticeRead) && NoticeRead.MatchGeneration == 7 && NoticeRead.Revision == 44
        && NoticeRead.Termination.Handle == Notice.Termination.Handle && NoticeRead.Termination.ObservationEpoch == 5
        && NoticeRead.Termination.Generation == 3 && NoticeRead.Termination.TerminationSequence == 1 && NoticeRead.Termination.OccurredAtServerMs == 4000);
    Notice.Termination.Kind = static_cast<ELDObservedTerminationKind>(2);
    TestFalse(TEXT("Only ObservedDeath terminates"), Encodes(Notice));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoVisionPrivacyTest, "Lockdown.Rules.DemoVision.Privacy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoVisionPrivacyTest::RunTest(const FString& Parameters)
{
    auto Exact = [this](UScriptStruct* Struct, const TSet<FName>& Allowed)
    {
        int32 Count = 0;
        for (TFieldIterator<FProperty> It(Struct); It; ++It)
        {
            ++Count;
            TestTrue(*FString::Printf(TEXT("%s field %s is approved"), *Struct->GetName(), *It->GetName()), Allowed.Contains(It->GetFName()));
            TestNull(*FString::Printf(TEXT("%s has no canonical object reference"), *Struct->GetName()), CastField<FObjectPropertyBase>(*It));
            const FString Name = It->GetName();
            for (const TCHAR* Forbidden : {TEXT("LastSeen"), TEXT("Explored"), TEXT("Gold"), TEXT("Xp"), TEXT("Cooldown"), TEXT("Skill"),
                TEXT("Build"), TEXT("Inventory"), TEXT("Instance"), TEXT("Attribute"), TEXT("Body"), TEXT("Victim"), TEXT("Life")})
            {
                if (Struct != FLDWorldView::StaticStruct() || FCString::Strcmp(Forbidden, TEXT("Life")) != 0)
                { TestFalse(*FString::Printf(TEXT("%s does not carry %s"), *Name, Forbidden), Name.Contains(Forbidden, ESearchCase::CaseSensitive)); }
            }
        }
        TestEqual(*FString::Printf(TEXT("%s schema complete"), *Struct->GetName()), Count, Allowed.Num());
    };
    Exact(FLDObservedUnitView::StaticStruct(), {TEXT("Handle"), TEXT("ObservationEpoch"), TEXT("StateRevision"), TEXT("AuthorizedFieldMask"),
        TEXT("Kind"), TEXT("Presentation"), TEXT("FeetCm"), TEXT("Facing"), TEXT("ActionCue"), TEXT("ActionCueAgeMs"), TEXT("ActionAbility"), TEXT("ContenderId"), TEXT("CreatureId"),
        TEXT("Level"), TEXT("Health"), TEXT("Mana"), TEXT("Statuses"), TEXT("Actives")});
    // A flight names no caster, effect, source or target: only its handle, ground point, heading and look.
    Exact(FLDObservedProjectileView::StaticStruct(), {TEXT("Handle"), TEXT("PositionCm"), TEXT("Heading"), TEXT("Look"),
        TEXT("RadiusCm"), TEXT("InnerRadiusCm"), TEXT("AngleDegrees")});
    Exact(FLDObservedResource::StaticStruct(), {TEXT("CurrentP2"), TEXT("MaxP2")});
    Exact(FLDObservedStatus::StaticStruct(), {TEXT("StatusId"), TEXT("Stacks"), TEXT("ExpiresAtServerMs")});
    // #112 §10, #198: the Q/W/E active ids only; no rank, cooldown or mana field.
    Exact(FLDObservedActivesView::StaticStruct(), {TEXT("Abilities")});
    Exact(FLDObservedUnitTermination::StaticStruct(), {TEXT("Handle"), TEXT("ObservationEpoch"), TEXT("Generation"),
        TEXT("TerminationSequence"), TEXT("OccurredAtServerMs"), TEXT("Kind")});
    Exact(FLDObservedFootprint::StaticStruct(), {TEXT("SourceStatus"), TEXT("SourceFeetCm"), TEXT("RadiusCm"), TEXT("CurrentBits")});
    Exact(FLDWorldView::StaticStruct(), {TEXT("Schema"), TEXT("MatchGeneration"), TEXT("ObserverLife"), TEXT("Generation"), TEXT("Revision"),
        TEXT("WorldMapId"), TEXT("WorldLayoutRevision"), TEXT("WorldGeometryRevision"), TEXT("SampledAtServerSec"), TEXT("Footprint"),
        TEXT("Units"), TEXT("RecentTerminations"), TEXT("Projectiles")});
    Exact(FLDObservedConceal::StaticStruct(), {TEXT("MatchGeneration"), TEXT("Generation"), TEXT("Revision"), TEXT("Removed")});
    Exact(FLDObservedTerminationNotice::StaticStruct(), {TEXT("MatchGeneration"), TEXT("Revision"), TEXT("Termination")});

    FLDObserverLedger Ledger;
    FString Error;
    bool bReset = false;
    TestTrue(TEXT("Ledger begins"), Ledger.Begin(7, Error) && Ledger.Bind(ELDLifePhase::Alive, 1, bReset, Error));
    FLDObservedTarget Visible = Target(FVector(300, 0, 2));
    TArray<FGuid> Removed;
    TArray<FLDObservedUnitView> Units;
    TestTrue(TEXT("Visible contender projects"), Ledger.Sample({&Visible}, Removed, Units, Error) && Units.Num() == 1);
    TestTrue(TEXT("Opaque handle is not the canonical body or life"), Units.Num() == 1 && Units[0].Handle != Visible.Body.BodyIdentity
        && Units[0].Handle != Visible.Body.Victim.EntityId);
    if (Units.Num() == 1)
    {
        // Only the observer's own current handle and epoch resolve; canonical or foreign identities never do.
        const FGuid Handle = Units[0].Handle;
        const uint32 Epoch = Units[0].ObservationEpoch;
        FLDCombatBodyRef Resolved;
        TestTrue(TEXT("The observer's authorized handle resolves to the body"), Ledger.FindBinding(Handle, Epoch, Resolved) && Resolved == Visible.Body);
        TestFalse(TEXT("A canonical body identity cannot stand in for a handle"), Ledger.FindBinding(Visible.Body.BodyIdentity, Epoch, Resolved));
        TestFalse(TEXT("The public contender identity cannot stand in for a handle"), Ledger.FindBinding(Units[0].ContenderId, Epoch, Resolved));
        TestFalse(TEXT("An item or other foreign identity cannot stand in for a handle"), Ledger.FindBinding(FGuid::NewGuid(), Epoch, Resolved));
        TestFalse(TEXT("A handle resolves only with its own epoch"), Ledger.FindBinding(Handle, Epoch + 1, Resolved));
        TestFalse(TEXT("A canonical identity cannot pin an order"), Ledger.BindOrder(Move(1), Units[0].ContenderId, Epoch, Error));
        FLDObserverLedger Other;
        TArray<FLDObservedUnitView> OtherUnits;
        TestTrue(TEXT("A second observer sees the same body"), Other.Begin(7, Error) && Other.Bind(ELDLifePhase::Alive, 1, bReset, Error)
            && Other.Sample({&Visible}, Removed, OtherUnits, Error) && OtherUnits.Num() == 1);
        TestTrue(TEXT("Observers receive unrelated handles for one body"), OtherUnits.Num() == 1 && OtherUnits[0].Handle != Handle);
        TestFalse(TEXT("One observer's handle never resolves for another"), Other.FindBinding(Handle, Epoch, Resolved));
        TestFalse(TEXT("One observer's handle cannot pin another observer's order"), Other.BindOrder(Move(1), Handle, Epoch, Error));
    }
    FLDObservedTarget Incomplete = Target(FVector(300, 0, 2));
    Incomplete.Presented.Level = 0;
    TestFalse(TEXT("A provider gap cannot publish a default level"), Ledger.Sample({&Incomplete}, Removed, Units, Error));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoVisionOrderingTest, "Lockdown.Rules.DemoVision.Ordering",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoVisionOrderingTest::RunTest(const FString& Parameters)
{
    using EResult = FLDObservedFrameReceiver::EResult;
    const FLDObservedCatalog Catalog = TestCatalog();
    const FLDWorldRevision World = DemoRevision();
    FLDObservedFrameReceiver Receiver;
    TArray<FLDObservedClear> Cleared;
    FString Error;
    FLDObservedUnitView Seen;
    auto Full = [&](uint32 Match, uint32 Generation, uint32 Revision, const TArray<FLDObservedUnitView>& Units, double Now = 10)
    {
        FLDWorldView View = ActiveFrame(Match, Generation, Revision);
        View.Units = Units;
        Cleared.Reset();
        return Receiver.AcceptFull(View, World, Catalog, Now, Cleared, Error);
    };
    auto Conceal = [&](uint32 Match, uint32 Generation, uint32 Revision, const TArray<FGuid>& Removed)
    {
        FLDObservedConceal Notice;
        Notice.MatchGeneration = Match;
        Notice.Generation = Generation;
        Notice.Revision = Revision;
        Notice.Removed = Removed;
        Cleared.Reset();
        return Receiver.AcceptConceal(Notice, Cleared, Error);
    };
    const FLDObservedUnitView A = ContenderUnit(1);
    TestTrue(TEXT("First full view applies"), Full(7, 1, 2, {A}) == EResult::Applied && Receiver.FindUnit(A.Handle, 1, Seen));
    const uint64 SerialBeforeRepeat = Receiver.Read().LocalSerial;
    TestTrue(TEXT("A repeated cursor is refused as a duplicate and changes nothing"), Full(7, 1, 2, {A}) == EResult::Stale
        && Receiver.FindUnit(A.Handle, 1, Seen) && Receiver.Read().LocalSerial == SerialBeforeRepeat);
    TestTrue(TEXT("Reliable concealment removes immediately"), Conceal(7, 1, 3, {A.Handle}) == EResult::Applied
        && HasReason(Cleared, A.Handle, ELDObservedClearReason::Conceal) && !Receiver.FindUnit(A.Handle, 1, Seen));
    TestTrue(TEXT("Late older full cannot resurrect a concealed unit"), Full(7, 1, 2, {A}) == EResult::Stale && !Receiver.FindUnit(A.Handle, 1, Seen));
    const FLDObservedUnitView Again = ContenderUnit(2);
    TestTrue(TEXT("Reappearance is a fresh handle and epoch"), Full(7, 1, 5, {Again}) == EResult::Applied
        && Receiver.FindUnit(Again.Handle, 2, Seen) && !Receiver.FindUnit(A.Handle, 1, Seen));
    TestTrue(TEXT("Delayed older concealment cannot erase the newer reappearance"), Conceal(7, 1, 4, {Again.Handle}) == EResult::Stale
        && Receiver.FindUnit(Again.Handle, 2, Seen));
    TestTrue(TEXT("Full-set removal clears without a death reason"), Full(7, 1, 6, {}) == EResult::Applied
        && HasReason(Cleared, Again.Handle, ELDObservedClearReason::FullSetRemoved)
        && !HasReason(Cleared, Again.Handle, ELDObservedClearReason::ObservedDeath));
    const FLDObservedUnitView B = ContenderUnit(1);
    TestTrue(TEXT("Same-generation view applies"), Full(7, 1, 7, {B}) == EResult::Applied);
    TestTrue(TEXT("Observer life change resets every handle"), Conceal(7, 2, 8, {}) == EResult::Applied
        && HasReason(Cleared, B.Handle, ELDObservedClearReason::ObserverReset) && Receiver.Read().Status == ELDObservedViewStatus::Stale);
    TestTrue(TEXT("Old generation cannot repopulate"), Full(7, 1, 100, {B}) == EResult::Stale && !Receiver.FindUnit(B.Handle, 1, Seen));
    const FLDObservedUnitView C = ContenderUnit(1);
    TestTrue(TEXT("New generation applies"), Full(7, 2, 9, {C}) == EResult::Applied && Receiver.Read().Status == ELDObservedViewStatus::Current);
    TestTrue(TEXT("New match resets the observer"), Full(8, 1, 1, {}) == EResult::Applied && HasReason(Cleared, C.Handle, ELDObservedClearReason::MatchReset));
    TestTrue(TEXT("Old match cannot mutate the new one"), Full(7, 9, 9, {C}) == EResult::Stale);

    const FLDObservedUnitView D = ContenderUnit(1);
    TestTrue(TEXT("Visible before timeout"), Full(8, 1, 2, {D}, 20.0) == EResult::Applied);
    Cleared.Reset();
    TestFalse(TEXT("No timeout inside the receive window"), Receiver.Tick(20.0 + FLDObservedFrameReceiver::StaleAfterSeconds, Cleared));
    TestTrue(TEXT("Timeout clears units instead of extrapolating"), Receiver.Tick(20.0 + FLDObservedFrameReceiver::StaleAfterSeconds + .01, Cleared)
        && HasReason(Cleared, D.Handle, ELDObservedClearReason::Timeout));
    const FLDObservedFrameRead Stale = Receiver.Read();
    TestTrue(TEXT("Stale frame keeps no LastSeen unit or coverage"), Stale.Status == ELDObservedViewStatus::Stale && Stale.Frame
        && Stale.Frame->Units.Num() == 0 && Stale.Frame->Footprint.CurrentBits.Num() == 0 && !Receiver.FindUnit(D.Handle, 1, Seen));

    FLDWorldView Foreign = ActiveFrame(8, 1, 3);
    Foreign.WorldGeometryRevision = 2;
    Cleared.Reset();
    TestTrue(TEXT("Another world revision is rejected"), Receiver.AcceptFull(Foreign, World, Catalog, 30, Cleared, Error) == EResult::Rejected);
    FLDObservedUnitView TooHigh = ContenderUnit(3);
    TooHigh.Level = 17;
    TestTrue(TEXT("Levels above the approved maximum are rejected"), Full(8, 1, 3, {TooHigh}) == EResult::Rejected);

    FLDObservedFrameReceiver Deaths;
    FLDObservedUnitView E = ContenderUnit(1), F = ContenderUnit(2), G = ContenderUnit(3);
    FLDWorldView First = ActiveFrame(9, 1, 2);
    First.Units = {E, F, G};
    Cleared.Reset();
    TestTrue(TEXT("Termination fixture applies"), Deaths.AcceptFull(First, World, Catalog, 1, Cleared, Error) == EResult::Applied);
    FLDObservedTerminationNotice Notice;
    Notice.MatchGeneration = 9;
    Notice.Revision = 3;
    Notice.Termination = Death(E, 1, 1);
    Cleared.Reset();
    TestTrue(TEXT("Reliable termination reports ObservedDeath"), Deaths.AcceptTermination(Notice, Cleared, Error) == EResult::Applied
        && HasReason(Cleared, E.Handle, ELDObservedClearReason::ObservedDeath));
    FLDWorldView Recovery = ActiveFrame(9, 1, 5);
    Recovery.Units = {G};
    Recovery.RecentTerminations = {Death(E, 1, 1), Death(F, 1, 2)};
    Cleared.Reset();
    TestTrue(TEXT("A full view recovers a termination that overtook its notice"), Deaths.AcceptFull(Recovery, World, Catalog, 1.1, Cleared, Error) == EResult::Applied
        && HasReason(Cleared, F.Handle, ELDObservedClearReason::ObservedDeath) && !HasReason(Cleared, F.Handle, ELDObservedClearReason::FullSetRemoved)
        && !HasReason(Cleared, E.Handle, ELDObservedClearReason::ObservedDeath));
    Notice.Revision = 4;
    Notice.Termination = Death(F, 1, 2);
    Cleared.Reset();
    TestTrue(TEXT("The delayed notice is deduplicated"), Deaths.AcceptTermination(Notice, Cleared, Error) == EResult::Stale && Cleared.Num() == 0);
    Notice.Revision = 6;
    Notice.Termination = Death(G, 1, 3);
    Notice.Termination.ObservationEpoch = 99;
    Cleared.Reset();
    TestTrue(TEXT("A termination of another epoch cannot remove the current interval"), Deaths.AcceptTermination(Notice, Cleared, Error) == EResult::Applied
        && Cleared.Num() == 0 && Deaths.FindUnit(G.Handle, 3, Seen));
    Cleared.Reset();
    Deaths.Reset(ELDObservedClearReason::MatchEnded, Cleared);
    TestTrue(TEXT("Match end clears presentation without a death reason"), HasReason(Cleared, G.Handle, ELDObservedClearReason::MatchEnded)
        && Deaths.Read().Status == ELDObservedViewStatus::Unavailable && Deaths.Read().Frame == nullptr);

    // Server cadence behind these frames: the core-clock sample schedule of PublishAuthority.
    int64 DueMs = 0;
    bool bStarted = false;
    int32 Samples = 0;
    for (int32 Frame = 0; Frame < 620; ++Frame)
    {
        const int64 NowMs = int64(Frame) * 1000 / 62;
        if (!bStarted || NowMs >= DueMs)
        {
            ++Samples;
            DueMs = LDObservation::NextSampleDue(NowMs, DueMs, ALDVisionDirector::SamplePeriodMs, bStarted);
            bStarted = true;
        }
    }
    TestTrue(TEXT("62 FPS frame quantization keeps twenty observation samples per second"), Samples >= 199 && Samples <= 201);
    TestEqual(TEXT("A stalled tick skips old intervals without an immediate replay"), LDObservation::NextSampleDue(5000, 1000, 50, true), int64(5050));
    TestEqual(TEXT("A late ordinary frame keeps the scheduled phase"), LDObservation::NextSampleDue(64, 50, 50, true), int64(100));
    TestEqual(TEXT("The first sample starts the schedule at the current time"), LDObservation::NextSampleDue(7, 0, 50, false), int64(57));

    // A stream that timed out recovers with the next newer full frame; nothing from before the gap comes back.
    FLDObservedFrameReceiver Resumed;
    const FLDObservedUnitView PreGap = ContenderUnit(1), PostGap = ContenderUnit(2);
    FLDWorldView GapFirst = ActiveFrame(9, 1, 1), GapSecond = ActiveFrame(9, 1, 2);
    GapFirst.Units = {PreGap};
    GapSecond.Units = {PostGap};
    Cleared.Reset();
    TestTrue(TEXT("The resumed stream starts current"), Resumed.AcceptFull(GapFirst, World, Catalog, 40.0, Cleared, Error) == EResult::Applied);
    TestTrue(TEXT("The resumed stream times out"), Resumed.Tick(40.0 + FLDObservedFrameReceiver::StaleAfterSeconds + .01, Cleared)
        && Resumed.Read().Status == ELDObservedViewStatus::Stale);
    Cleared.Reset();
    TestTrue(TEXT("The next full frame after a timeout restores current sight without pre-gap units"),
        Resumed.AcceptFull(GapSecond, World, Catalog, 41.0, Cleared, Error) == EResult::Applied
        && Resumed.Read().Status == ELDObservedViewStatus::Current && Resumed.FindUnit(PostGap.Handle, 2, Seen) && !Resumed.FindUnit(PreGap.Handle, 1, Seen));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoVisionHandlesTest, "Lockdown.Rules.DemoVision.Handles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoVisionHandlesTest::RunTest(const FString& Parameters)
{
    FLDObserverLedger Ledger;
    FString Error;
    bool bReset = false;
    TArray<FGuid> Removed;
    TArray<FLDObservedUnitView> Units;
    TestFalse(TEXT("A ledger needs a match"), Ledger.Begin(0, Error));
    TestTrue(TEXT("Ledger begins unspawned"), Ledger.Begin(7, Error) && Ledger.Generation() == 1 && Ledger.Phase() == ELDLifePhase::Unspawned);
    FLDObservedTarget One = Target(FVector(300, 0, 2));
    TestFalse(TEXT("No living source authorizes nothing"), Ledger.Sample({&One}, Removed, Units, Error));
    TestTrue(TEXT("Spawning rotates the generation"), Ledger.Bind(ELDLifePhase::Alive, 1, bReset, Error) && bReset && Ledger.Generation() == 2);
    TestTrue(TEXT("Same life does not rotate"), Ledger.Bind(ELDLifePhase::Alive, 1, bReset, Error) && !bReset && Ledger.Generation() == 2);
    TestTrue(TEXT("First visibility opens an interval"), Ledger.Sample({&One}, Removed, Units, Error) && Units.Num() == 1 && Removed.Num() == 0);
    const FGuid FirstHandle = Units[0].Handle;
    const uint32 FirstEpoch = Units[0].ObservationEpoch;
    TestTrue(TEXT("Interval starts at state revision one"), Units[0].StateRevision == 1 && FirstEpoch == 1);
    One.Presented.FeetCm = FVector(350, 10, 2);
    One.FootCm = FVector(350, 10, 2);
    One.Presented.Facing = 99;
    TestTrue(TEXT("Continuous sight keeps the handle; pose is not a state revision"), Ledger.Sample({&One}, Removed, Units, Error)
        && Units.Num() == 1 && Units[0].Handle == FirstHandle && Units[0].ObservationEpoch == FirstEpoch && Units[0].StateRevision == 1);
    One.Presented.Health.CurrentP2 -= 100;
    TestTrue(TEXT("Presented field change advances only the state revision"), Ledger.Sample({&One}, Removed, Units, Error)
        && Units[0].Handle == FirstHandle && Units[0].StateRevision == 2);
    TestTrue(TEXT("Losing sight conceals the handle"), Ledger.Sample({}, Removed, Units, Error) && Removed.Num() == 1
        && Removed[0] == FirstHandle && Units.Num() == 0);
    FLDCombatBodyRef Bound;
    TestFalse(TEXT("No LastSeen binding survives concealment"), Ledger.FindBinding(FirstHandle, FirstEpoch, Bound));
    TestTrue(TEXT("Reappearance never reuses the hidden interval"), Ledger.Sample({&One}, Removed, Units, Error)
        && Units[0].Handle != FirstHandle && Units[0].ObservationEpoch > FirstEpoch);
    const FGuid SecondHandle = Units[0].Handle;
    FLDObservedTarget Reborn = One;
    Reborn.Body.Victim.Life = 2;
    Reborn.Body.BodyIdentity = FGuid::NewGuid();
    TestTrue(TEXT("A target's new life is a new interval"), Ledger.Sample({&Reborn}, Removed, Units, Error)
        && Removed.Contains(SecondHandle) && Units[0].Handle != SecondHandle);
    uint32 Before = 0, After = 0;
    TestTrue(TEXT("Revisions are strictly increasing"), Ledger.AllocateRevision(Before, Error) && Ledger.AllocateRevision(After, Error) && After == Before + 1);
    TestTrue(TEXT("Observer death rotates and clears"), Ledger.Bind(ELDLifePhase::Dead, 1, bReset, Error) && bReset && Ledger.Generation() == 3
        && !Ledger.IsBound(Reborn.Body));
    TestFalse(TEXT("A dead observer has no living source"), Ledger.Sample({&Reborn}, Removed, Units, Error));
    TestTrue(TEXT("Respawn rotates again"), Ledger.Bind(ELDLifePhase::Alive, 2, bReset, Error) && bReset && Ledger.Generation() == 4);
    TArray<FLDObservedTarget> Crowd;
    Crowd.SetNum(LDObservation::MaxUnits + 1);
    TArray<const FLDObservedTarget*> All;
    for (int32 Index = 0; Index < Crowd.Num(); ++Index)
    {
        Crowd[Index] = Target(FVector(10 * Index, 0, 2));
        All.Add(&Crowd[Index]);
    }
    TestFalse(TEXT("Visible capacity overflow fails instead of truncating"), Ledger.Sample(All, Removed, Units, Error));
    TestTrue(TEXT("Overflow reports CapacityExceeded"), Error.Contains(TEXT("CapacityExceeded")) && Units.Num() == 0);
    All.Pop();
    TestTrue(TEXT("Exact capacity is accepted"), Ledger.Sample(All, Removed, Units, Error) && Units.Num() == LDObservation::MaxUnits);

    // Frames and concealments list units by observer-local handle. Neither frame position nor the epochs of
    // intervals opened together may follow canonical body order, or a client could follow a unit across intervals.
    FLDObserverLedger Ordering;
    TestTrue(TEXT("Ordering ledger alive"), Ordering.Begin(7, Error) && Ordering.Bind(ELDLifePhase::Alive, 1, bReset, Error));
    TArray<FLDObservedTarget> Pair = {Target(FVector(100, 0, 2)), Target(FVector(200, 0, 2))};
    if (Pair[1].Body.Victim.EntityId < Pair[0].Body.Victim.EntityId) { Pair.Swap(0, 1); }
    constexpr int32 Cycles = 64;
    bool FramesByHandle = true, ConcealsByHandle = true, EpochsByHandle = true;
    int32 CanonicalFirst = 0, CanonicalLowerEpoch = 0;
    for (int32 Cycle = 0; Cycle < Cycles; ++Cycle)
    {
        if (!Ordering.Sample({&Pair[0], &Pair[1]}, Removed, Units, Error) || Units.Num() != 2) { FramesByHandle = false; break; }
        FramesByHandle &= Units[0].Handle < Units[1].Handle;
        EpochsByHandle &= Units[0].ObservationEpoch < Units[1].ObservationEpoch;
        CanonicalFirst += Units[0].ContenderId == Pair[0].Body.Victim.EntityId ? 1 : 0;
        CanonicalLowerEpoch += UnitOf(Units, Pair[0]).ObservationEpoch < UnitOf(Units, Pair[1]).ObservationEpoch ? 1 : 0;
        if (!Ordering.Sample({}, Removed, Units, Error) || Removed.Num() != 2) { ConcealsByHandle = false; break; }
        ConcealsByHandle &= Removed[0] < Removed[1];
    }
    TestTrue(TEXT("Frames list units by observer-local handle"), FramesByHandle);
    TestTrue(TEXT("Concealments list handles in handle order"), ConcealsByHandle);
    TestTrue(TEXT("Intervals opened together take epochs in handle order"), EpochsByHandle);
    // With fresh random handles, each check below fails with probability 2^-63.
    TestTrue(TEXT("Frame position does not follow canonical body order"), CanonicalFirst > 0 && CanonicalFirst < Cycles);
    TestTrue(TEXT("Epoch order does not follow canonical body order"), CanonicalLowerEpoch > 0 && CanonicalLowerEpoch < Cycles);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoVisionTerminationTest, "Lockdown.Rules.DemoVision.Termination",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoVisionTerminationTest::RunTest(const FString& Parameters)
{
    using ETerminate = FLDObserverLedger::ETerminate;
    FLDObserverLedger Ledger;
    FString Error;
    bool bReset = false;
    TArray<FGuid> Removed;
    TArray<FLDObservedUnitView> Units;
    FLDMatchContext Match;
    Match.Generation = 7;
    Match.LedgerMatch = FGuid::NewGuid();
    TestTrue(TEXT("Ledger alive"), Ledger.Begin(7, Error) && Ledger.Bind(ELDLifePhase::Alive, 1, bReset, Error));
    FLDObservedTarget Seen = Target(FVector(100, 0, 2)), Pinned = Target(FVector(200, 0, 2)), Hidden = Target(FVector(300, 0, 2));
    TestTrue(TEXT("Two targets visible"), Ledger.Sample({&Seen, &Pinned}, Removed, Units, Error) && Units.Num() == 2
        && UnitOf(Units, Seen).Handle.IsValid() && UnitOf(Units, Pinned).Handle.IsValid());
    const FGuid SeenHandle = UnitOf(Units, Seen).Handle, PinnedHandle = UnitOf(Units, Pinned).Handle;
    const uint32 SeenEpoch = UnitOf(Units, Seen).ObservationEpoch, PinnedEpoch = UnitOf(Units, Pinned).ObservationEpoch;
    FLDObservedTerminationFact Fact;
    TestTrue(TEXT("An unwitnessed hidden death yields no termination"),
        Ledger.Terminate(Hidden.Body, Match, 4.0, 4000, Fact, Error) == ETerminate::NotBound);
    TestTrue(TEXT("A bound visible death terminates once"), Ledger.Terminate(Seen.Body, Match, 4.0, 4000, Fact, Error) == ETerminate::Terminated
        && Fact.Wire.Handle == SeenHandle && Fact.Wire.ObservationEpoch == SeenEpoch && Fact.Wire.TerminationSequence == 1
        && Fact.Wire.Generation == Ledger.Generation() && Fact.Victim == Seen.Body.Victim && Fact.OccurredAtSeconds == 4.0);
    TestTrue(TEXT("Termination is not repeated"), Ledger.Terminate(Seen.Body, Match, 4.0, 4000, Fact, Error) == ETerminate::NotBound);
    TestTrue(TEXT("The fact stays queryable by handle and epoch"), Ledger.FindTermination(SeenHandle, SeenEpoch) != nullptr);
    TestTrue(TEXT("A terminated interval is not concealed again"), Ledger.Sample({&Pinned}, Removed, Units, Error) && !Removed.Contains(SeenHandle));
    TArray<FLDObservedUnitTermination> Recent;
    Ledger.RecentTerminations(Recent);
    TestTrue(TEXT("Full views repeat the retained fact"), Recent.Num() == 1 && Recent[0].Handle == SeenHandle);

    TestFalse(TEXT("Orders cannot pin hidden handles"), Ledger.BindOrder(Move(5), SeenHandle, SeenEpoch, Error));
    TestTrue(TEXT("The current order pins a visible target"), Ledger.BindOrder(Move(5), PinnedHandle, PinnedEpoch, Error) && Ledger.HasOrderPin());
    TestTrue(TEXT("Rebinding the same terms is idempotent"), Ledger.BindOrder(Move(5), PinnedHandle, PinnedEpoch, Error));
    TestFalse(TEXT("An older order cannot replace the pin"), Ledger.BindOrder(Move(4), PinnedHandle, PinnedEpoch, Error));
    TestFalse(TEXT("An older release cannot clear the newer pin"), Ledger.ReleaseOrder(Move(4), Error) || !Ledger.HasOrderPin());
    FLDMoveIdentity OtherLife = Move(6);
    OtherLife.Life = 2;
    TestFalse(TEXT("Another life cannot pin"), Ledger.BindOrder(OtherLife, PinnedHandle, PinnedEpoch, Error));
    FLDObservedTarget Next = Target(FVector(400, 0, 2));
    TestTrue(TEXT("Replacement candidate visible"), Ledger.Sample({&Pinned, &Next}, Removed, Units, Error) && Units.Num() == 2
        && UnitOf(Units, Next).Handle.IsValid());
    const FGuid NextHandle = UnitOf(Units, Next).Handle;
    const uint32 NextEpoch = UnitOf(Units, Next).ObservationEpoch;
    TestFalse(TEXT("The same order cannot switch away from a living pinned target"), Ledger.BindOrder(Move(5), NextHandle, NextEpoch, Error));
    TestTrue(TEXT("The pinned target's witnessed death is retained for its order"),
        Ledger.Terminate(Pinned.Body, Match, 5.0, 5000, Fact, Error) == ETerminate::Terminated && Fact.Wire.TerminationSequence == 2);

    // Two retained facts plus fifteen more forces exactly one replacement at the sixteen-fact bound.
    TArray<FLDObservedTarget> Wave;
    Wave.SetNum(LDObservation::MaxTerminations - 1);
    TArray<const FLDObservedTarget*> WaveVisible = {&Next};
    for (int32 Index = 0; Index < Wave.Num(); ++Index)
    {
        Wave[Index] = Target(FVector(500 + 10 * Index, 0, 2));
        WaveVisible.Add(&Wave[Index]);
    }
    TestTrue(TEXT("A wave of visible targets"), Ledger.Sample(WaveVisible, Removed, Units, Error) && Units.Num() == LDObservation::MaxTerminations
        && !Removed.Contains(NextHandle));
    bool AllTerminated = true;
    for (const FLDObservedTarget& Victim : Wave)
    { AllTerminated &= Ledger.Terminate(Victim.Body, Match, 6.0, 6000, Fact, Error) == ETerminate::Terminated; }
    TestTrue(TEXT("Unbound presentation facts are replaced at capacity"), AllTerminated);
    TestTrue(TEXT("The order-bound fact is never silently dropped"), Ledger.FindTermination(PinnedHandle, PinnedEpoch) != nullptr);
    TestTrue(TEXT("The oldest unbound fact made room"), Ledger.FindTermination(SeenHandle, SeenEpoch) == nullptr);
    Ledger.RecentTerminations(Recent);
    TestEqual(TEXT("Retention stays bounded"), Recent.Num(), LDObservation::MaxTerminations);
    TestTrue(TEXT("After its pinned target died, the same order may switch targets"), Ledger.BindOrder(Move(5), NextHandle, NextEpoch, Error));
    TestTrue(TEXT("A newer order replaces the pin"), Ledger.BindOrder(Move(6), NextHandle, NextEpoch, Error));
    TestTrue(TEXT("Releasing the current order clears the pin"), Ledger.ReleaseOrder(Move(6), Error) && !Ledger.HasOrderPin());
    TestTrue(TEXT("Release without a pin is idempotent"), Ledger.ReleaseOrder(Move(6), Error));
    TestTrue(TEXT("Observer generation change drops facts and pins"), Ledger.Bind(ELDLifePhase::Dead, 1, bReset, Error) && bReset
        && Ledger.FindTermination(PinnedHandle, PinnedEpoch) == nullptr && !Ledger.HasOrderPin());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoVisionSightTest, "Lockdown.Rules.DemoVision.Sight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoVisionSightTest::RunTest(const FString& Parameters)
{
    FLDSightGeometry Sight;
    FString Error;
    const FLDWorldRevision World = DemoRevision();
    TestTrue(TEXT("Approved geometry builds"), Sight.Build(TestGeometry(), World, Whole(1400), Error));
    const FVector West(-1000, 0, 2);
    TestTrue(TEXT("A tall sight blocker occludes"), Sight.Trace(West, FVector(300, 0, 2)) == ELDSightResult::Blocked);
    TestTrue(TEXT("Open ground is visible"), Sight.Trace(West, FVector(-1000, 1300, 2)) == ELDSightResult::Visible);
    TestTrue(TEXT("Touching a blocker corner occludes"), Sight.Trace(FVector(0, 1100, 2), FVector(200, 900, 2)) == ELDSightResult::Blocked);
    TestTrue(TEXT("The low stall blocks walking but not sight"), Sight.Trace(FVector(-1400, 1300, 2), FVector(-1400, 1900, 2)) == ELDSightResult::Visible);
    TestTrue(TEXT("The radius boundary is inclusive"), Sight.Trace(West, FVector(-1000, -1400, 2)) == ELDSightResult::Visible);
    TestTrue(TEXT("Beyond the radius is out of range"), Sight.Trace(West, FVector(-1000, -1400.01, 2)) == ELDSightResult::OutOfRange);
    TestTrue(TEXT("Points off the registered ground are invalid"), Sight.Trace(West, FVector(-5000.5, 0, 2)) == ELDSightResult::InvalidPoint);
    TestTrue(TEXT("Nonfinite points are invalid"), Sight.Trace(FVector(std::numeric_limits<double>::quiet_NaN(), 0, 2), West) == ELDSightResult::InvalidPoint);
    // One classification serves sampling, resolve, order pins and death witnesses.
    TestTrue(TEXT("Only visible sight resolves a target"), LDObservation::ResolveFromSight(ELDSightResult::Visible) == ELDObservedResolve::Resolved);
    TestTrue(TEXT("Range and blockers are ordinary unavailability"),
        LDObservation::ResolveFromSight(ELDSightResult::OutOfRange) == ELDObservedResolve::TargetUnavailable
        && LDObservation::ResolveFromSight(ELDSightResult::Blocked) == ELDObservedResolve::TargetUnavailable);
    TestTrue(TEXT("Invalid points and a stale world are authority faults, never a missing target"),
        LDObservation::ResolveFromSight(ELDSightResult::InvalidPoint) == ELDObservedResolve::AuthorityFault
        && LDObservation::ResolveFromSight(ELDSightResult::StaleWorld) == ELDObservedResolve::AuthorityFault);
    TestTrue(TEXT("A body off the registered ground faults instead of hiding"),
        LDObservation::ResolveFromSight(Sight.Trace(West, FVector(-5000.5, 0, 2))) == ELDObservedResolve::AuthorityFault);
    TestTrue(TEXT("Unbuilt sight geometry reports a stale world"), FLDSightGeometry().Trace(West, West) == ELDSightResult::StaleWorld);

    TArray<uint8> Bits;
    Sight.Coverage(West, Bits);
    const int32 Side = LDObservation::CoverageSide;
    auto CellOf = [Side](double X, double Y)
    { return FMath::FloorToInt32((Y + 5000.0) / (10000.0 / Side)) * Side + FMath::FloorToInt32((X + 5000.0) / (10000.0 / Side)); };
    const int32 Own = CellOf(-1000, 0);
    TestEqual(TEXT("Coverage is the fixed 128 x 128 bitmap"), Bits.Num(), LDObservation::CoverageBytes);
    TestTrue(TEXT("Coverage indexes y * 128 + x, low bit first"), LDObservation::CoverageBit(Bits, Own) && (Bits[Own / 8] & (1 << (Own % 8))) != 0);
    TestFalse(TEXT("Shadowed cells stay dark"), LDObservation::CoverageBit(Bits, CellOf(500, 0)));
    TestFalse(TEXT("Cells beyond the radius stay dark"), LDObservation::CoverageBit(Bits, CellOf(-1000, 3000)));
    const FVector2D Centre = Sight.CellCentre(Own);
    TestTrue(TEXT("Cell centres follow the registered ground bounds"), FMath::IsNearlyEqual(Centre.X, -976.5625, 1e-9) && FMath::IsNearlyEqual(Centre.Y, 39.0625, 1e-9));
    // Two observers on either side of the wall see the same target differently, in traces and coverage alike.
    const FVector East(1000, 0, 2), Between(300, 0, 2);
    TArray<uint8> EastBits;
    Sight.Coverage(East, EastBits);
    TestTrue(TEXT("Observers on either side of a wall see the same target differently"),
        Sight.Trace(West, Between) == ELDSightResult::Blocked && Sight.Trace(East, Between) == ELDSightResult::Visible
        && !LDObservation::CoverageBit(Bits, CellOf(300, 0)) && LDObservation::CoverageBit(EastBits, CellOf(300, 0)));

    FLDSightGeometry Wide;
    TestTrue(TEXT("Wide test radius builds"), Wide.Build(TestGeometry(), World, Whole(3000), Error));
    TArray<uint8> WideBits;
    Wide.Coverage(West, WideBits);
    const FVector Edge(470, 1640, 2);
    TestTrue(TEXT("A unit sample can be visible inside a shadowed display cell"), Wide.Trace(West, Edge) == ELDSightResult::Visible
        && Wide.Trace(West, FVector(Wide.CellCentre(CellOf(Edge.X, Edge.Y)), 2)) == ELDSightResult::Blocked
        && !LDObservation::CoverageBit(WideBits, CellOf(Edge.X, Edge.Y)));

    FLDDemoWorldGeometry Short = TestGeometry();
    Short.Blockers[0].HeightCm = Whole(100);
    TestFalse(TEXT("A sight blocker below the sample height is rejected"), FLDSightGeometry().Build(Short, World, Whole(1400), Error));
    FLDDemoWorldGeometry Clockwise = TestGeometry();
    Algo::Reverse(Clockwise.Blockers[0].PolygonCm);
    TestFalse(TEXT("A non-counterclockwise sight blocker is rejected"), FLDSightGeometry().Build(Clockwise, World, Whole(1400), Error));
    FLDDemoWorldGeometry Layers = TestGeometry();
    const FLDDemoWorldSurface SecondLayer = Layers.Surfaces[0];
    Layers.Surfaces.Add(SecondLayer);
    TestFalse(TEXT("Observation is single-layer only"), FLDSightGeometry().Build(Layers, World, Whole(1400), Error));
    TestFalse(TEXT("A radius is required"), FLDSightGeometry().Build(TestGeometry(), World, Whole(0), Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoVisionCoveragePublicationTest, "Lockdown.Rules.DemoVision.CoveragePublication",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoVisionCoveragePublicationTest::RunTest(const FString& Parameters)
{
    FString Error;
    FLDSightGeometry Sight;
    FLDWorldRevision World = DemoRevision();
    if (!TestTrue(TEXT("Publication geometry builds"), Sight.Build(TestGeometry(), World, Whole(1400), Error))) { return false; }
    const FVector West(-1000, 0, 2), East(1000, 0, 2);
    FLDObserverCoverage Human, AI;
    TArray<uint8> Expected, Actual;
    Sight.Coverage(West, Expected);
    FLDWorldView View = ActiveFrame(7, 3, 1);
    View.Units = {ContenderUnit(), CampUnit()};
    const auto DarkValidFrame = [&]()
    {
        View.Footprint.CurrentBits = Actual;
        return Actual.Num() == LDObservation::CoverageBytes
            && !Actual.ContainsByPredicate([](uint8 Byte) { return Byte != 0; }) && LDObservation::ValidateView(View, Error);
    };
    TestFalse(TEXT("A human's non-publication sample performs no coverage"), Human.Sample(Sight, West, false, true, Actual));
    TestTrue(TEXT("A non-publication frame remains codec-valid, with a zero bitmap"), DarkValidFrame());
    TestFalse(TEXT("An AI never computes coverage, even at a publication boundary"), AI.Sample(Sight, East, true, false, Actual));
    TestTrue(TEXT("An AI frame remains codec-valid, with its own units unchanged"), DarkValidFrame());
    TestTrue(TEXT("The first human publication computes coverage"), Human.Sample(Sight, West, true, true, Actual));
    TestTrue(TEXT("The published human bitmap matches all 2048 reference bytes"), Actual == Expected);
    TestFalse(TEXT("An unchanged foot and world reuse the prior coverage"), Human.Sample(Sight, West, true, true, Actual));
    TestTrue(TEXT("Reused coverage is byte-identical"), Actual == Expected);
    TestFalse(TEXT("Movement at a non-publication sample computes nothing"), Human.Sample(Sight, East, false, true, Actual));
    TestTrue(TEXT("It does not copy an obsolete human bitmap into the internal frame"), DarkValidFrame());
    TestFalse(TEXT("A non-publication sample did not replace the stationary cache"), Human.Sample(Sight, West, true, true, Actual));
    TestTrue(TEXT("The next stationary publication still carries the authorized bitmap"), Actual == Expected);
    TestTrue(TEXT("Sub-millimetre movement invalidates, without tolerance"), Human.Sample(Sight, West + FVector(.000001, 0, 0), true, true, Actual));
    TestTrue(TEXT("A changed foot height also invalidates"), Human.Sample(Sight, West + FVector(.000001, 0, 1), true, true, Actual));
    TestTrue(TEXT("A moved publication recomputes"), Human.Sample(Sight, East, true, true, Actual));
    Sight.Coverage(East, Expected);
    TestTrue(TEXT("Moved coverage matches that observer, not the other side of the wall"), Actual == Expected);
    TestTrue(TEXT("A second observer cannot borrow the human's cached coverage"), AI.Sample(Sight, West, true, true, Actual));
    Sight.Coverage(West, Expected);
    TestTrue(TEXT("Its first eligible publication uses its own source"), Actual == Expected);
    ++World.GeometryRevision;
    if (!TestTrue(TEXT("A new world revision builds"), Sight.Build(TestGeometry(), World, Whole(1400), Error))) { return false; }
    TestTrue(TEXT("A changed world revision invalidates stationary coverage"), AI.Sample(Sight, West, true, true, Actual));
    AI.Reset();
    TestTrue(TEXT("A life/generation reset invalidates even an identical source and world"), AI.Sample(Sight, West, true, true, Actual));
    View.Footprint.CurrentBits = Actual;
    FLDWorldView Read;
    TestTrue(TEXT("Publication and AI-frame coverage selection never changes the unit fields"),
        Wire(View, Read) && Read.Units.Num() == 2 && Read.Units[0].Handle == View.Units[0].Handle
        && Read.Units[1].Handle == View.Units[1].Handle && Read.Footprint.CurrentBits == Actual);
    return true;
}

// #97 tiers: a level-1 plateau east of the wall with a level-2 plateau on it, a ramp from the ground onto its west edge
// and a tree standing on level 1.
FLDDemoWorldGeometry TieredGeometry()
{
    const auto At = [](int64 X, int64 Y, int64 Z) { FLDDemoWorldPoint Point = Ground(X, Y); Point.Z = Whole(Z); return Point; };
    FLDDemoWorldGeometry Geometry = TestGeometry();
    FLDDemoWorldPlateau Low;
    Low.Id = TEXT("hill_1");
    Low.Level = 1;
    Low.PolygonCm = {At(2000, -500, 150), At(3000, -500, 150), At(3000, 500, 150), At(2000, 500, 150)};
    FLDDemoWorldPlateau High;
    High.Id = TEXT("hill_2");
    High.Level = 2;
    High.PolygonCm = {At(2400, -200, 300), At(2800, -200, 300), At(2800, 200, 300), At(2400, 200, 300)};
    Geometry.Plateaus = {Low, High};
    FLDDemoWorldRamp Ramp;
    Ramp.Id = TEXT("ramp_0");
    Ramp.LowerLevel = 0;
    Ramp.PolygonCm = {At(1700, 100, 0), At(1700, -100, 0), At(2000, -100, 150), At(2000, 100, 150)};
    Geometry.Ramps = {Ramp};
    FLDDemoWorldBlocker Tree;
    Tree.Id = TEXT("tree_1");
    Tree.SurfaceId = Geometry.Surfaces[0].Id;
    Tree.Shape = ELDDemoWorldBlockerShape::Cylinder;
    Tree.CenterCm = At(2200, 300, 150);
    Tree.RadiusCm = Whole(50);
    Tree.HeightCm = Whole(200);
    Tree.BlocksMovement = true;
    Tree.BlocksSight = true;
    Geometry.Blockers.Add(Tree);
    return Geometry;
}

// #97: high ground sees low ground, never the reverse; a ramp sees its lower level and itself; obstacles hide only
// from observers at or below their level.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoVisionTieredSightTest, "Lockdown.Rules.DemoVision.TieredSight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoVisionTieredSightTest::RunTest(const FString& Parameters)
{
    FLDSightGeometry Sight, Wide;
    FString Error;
    if (!TestTrue(TEXT("Tiered geometry builds"), Sight.Build(TieredGeometry(), DemoRevision(), Whole(1400), Error))
        || !TestTrue(TEXT("Wide tiered geometry builds"), Wide.Build(TieredGeometry(), DemoRevision(), Whole(3000), Error))) { return false; }
    const FVector GroundEye(1000, 0, 0), RampEye(1850, 0, 75), LowEye(2100, 0, 150), HighEye(2600, 0, 300);
    TestTrue(TEXT("Low ground does not see high ground"), Sight.Trace(GroundEye, LowEye) == ELDSightResult::Blocked);
    TestTrue(TEXT("High ground sees low ground"), Sight.Trace(LowEye, GroundEye) == ELDSightResult::Visible);
    TestTrue(TEXT("A ramp sees the low ground"), Sight.Trace(RampEye, GroundEye) == ELDSightResult::Visible);
    TestTrue(TEXT("Low ground does not see onto a ramp"), Sight.Trace(GroundEye, RampEye) == ELDSightResult::Blocked);
    TestTrue(TEXT("A ramp does not see the plateau it climbs to"), Sight.Trace(RampEye, LowEye) == ELDSightResult::Blocked);
    TestTrue(TEXT("The plateau sees down its ramp"), Sight.Trace(LowEye, RampEye) == ELDSightResult::Visible);
    TestTrue(TEXT("Level 1 does not see level 2, level 2 sees level 1"),
        Sight.Trace(LowEye, HighEye) == ELDSightResult::Blocked && Sight.Trace(HighEye, LowEye) == ELDSightResult::Visible);
    TestTrue(TEXT("Level 2 sees the ground over level 1"), Sight.Trace(HighEye, FVector(1500, 0, 0)) == ELDSightResult::Visible);
    TestTrue(TEXT("A plateau between two ground points hides them from each other"),
        Wide.Trace(FVector(1500, 0, 0), FVector(3500, 0, 0)) == ELDSightResult::Blocked
        && Wide.Trace(FVector(1500, 700, 0), FVector(3500, 700, 0)) == ELDSightResult::Visible);
    TestTrue(TEXT("A ground wall hides the ground behind it from the ground, not from level 1"),
        Wide.Trace(GroundEye, FVector(-300, 0, 0)) == ELDSightResult::Blocked
        && Wide.Trace(LowEye, FVector(-300, 0, 0)) == ELDSightResult::Visible);
    TestTrue(TEXT("A level-1 tree hides level 1 from level 1, not from level 2"),
        Sight.Trace(FVector(2050, 300, 150), FVector(2350, 300, 150)) == ELDSightResult::Blocked
        && Sight.Trace(FVector(2500, 150, 300), FVector(2100, 380, 150)) == ELDSightResult::Visible);
    TArray<uint8> GroundBits, LowBits;
    Sight.Coverage(GroundEye, GroundBits);
    Sight.Coverage(LowEye, LowBits);
    const int32 Side = LDObservation::CoverageSide;
    auto CellOf = [Side](double X, double Y)
    { return FMath::FloorToInt32((Y + 5000.0) / (10000.0 / Side)) * Side + FMath::FloorToInt32((X + 5000.0) / (10000.0 / Side)); };
    TestTrue(TEXT("Ground coverage lights the ground and leaves the plateau dark"),
        LDObservation::CoverageBit(GroundBits, CellOf(1500, 300)) && !LDObservation::CoverageBit(GroundBits, CellOf(2200, -300)));
    TestTrue(TEXT("Plateau coverage lights the ground below it"), LDObservation::CoverageBit(LowBits, CellOf(1300, 300)));
    FLDDemoWorldGeometry Floating = TieredGeometry();
    Floating.Blockers.Last().CenterCm.Z = Whole(75);
    TestFalse(TEXT("A sight blocker off every flat level is rejected"), FLDSightGeometry().Build(Floating, DemoRevision(), Whole(1400), Error));
    return true;
}

// #97: a tree is an upright cylinder; its closed disc blocks sight like a polygon blocker's closed outline.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoVisionTreeSightTest, "Lockdown.Rules.DemoVision.TreeSight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoVisionTreeSightTest::RunTest(const FString& Parameters)
{
    FLDDemoWorldGeometry Geometry = TestGeometry();
    FLDDemoWorldBlocker Tree;
    Tree.Id = TEXT("tree");
    Tree.SurfaceId = Geometry.Surfaces[0].Id;
    Tree.Shape = ELDDemoWorldBlockerShape::Cylinder;
    Tree.CenterCm = Ground(-2000, -2000);
    Tree.RadiusCm = Whole(50);
    Tree.HeightCm = Whole(200);
    Tree.BlocksMovement = true;
    Tree.BlocksSight = true;
    Geometry.Blockers.Add(Tree);
    FLDSightGeometry Sight;
    FString Error;
    if (!TestTrue(TEXT("Geometry with a tree builds"), Sight.Build(Geometry, DemoRevision(), Whole(1400), Error))) { return false; }
    TestTrue(TEXT("A sight line through the trunk is blocked"),
        Sight.Trace(FVector(-2500, -2000, 2), FVector(-1500, -2000, 2)) == ELDSightResult::Blocked);
    TestTrue(TEXT("A sight line touching the trunk's edge is blocked"),
        Sight.Trace(FVector(-2500, -1950, 2), FVector(-1500, -1950, 2)) == ELDSightResult::Blocked);
    TestTrue(TEXT("A sight line just past the trunk is visible"),
        Sight.Trace(FVector(-2500, -1949, 2), FVector(-1500, -1949, 2)) == ELDSightResult::Visible);
    TestTrue(TEXT("A target behind the trunk is hidden, beside it visible"),
        Sight.Trace(FVector(-2600, -2000, 2), FVector(-1900, -2000, 2)) == ELDSightResult::Blocked
        && Sight.Trace(FVector(-2600, -2000, 2), FVector(-1900, -1800, 2)) == ELDSightResult::Visible);
    TArray<uint8> Bits;
    Sight.Coverage(FVector(-2600, -2000, 2), Bits);
    const int32 Side = LDObservation::CoverageSide;
    auto CellOf = [Side](double X, double Y)
    { return FMath::FloorToInt32((Y + 5000.0) / (10000.0 / Side)) * Side + FMath::FloorToInt32((X + 5000.0) / (10000.0 / Side)); };
    TestFalse(TEXT("Coverage leaves the trunk's shadow dark"), LDObservation::CoverageBit(Bits, CellOf(-1700, -2000)));
    TestTrue(TEXT("Coverage lights the open ground beside the shadow"), LDObservation::CoverageBit(Bits, CellOf(-1700, -1600)));
    FLDDemoWorldGeometry Low = Geometry;
    Low.Blockers.Last().HeightCm = Whole(100);
    TestFalse(TEXT("A sight-blocking tree below the sample height is rejected"), FLDSightGeometry().Build(Low, DemoRevision(), Whole(1400), Error));
    FLDDemoWorldGeometry Flat = Geometry;
    Flat.Blockers.Last().RadiusCm = Whole(0);
    TestFalse(TEXT("A tree without a radius is rejected"), FLDSightGeometry().Build(Flat, DemoRevision(), Whole(1400), Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoVisionActionCueTest, "Lockdown.Rules.DemoVision.ActionCues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoVisionActionCueTest::RunTest(const FString& Parameters)
{
    FLDWorldView View = ActiveFrame(7, 3, 41);
    View.Units = {ContenderUnit(1), CampUnit(2)};
    View.Units[0].ActionCue = ELDObservedActionCue::Cast;
    View.Units[0].ActionCueAgeMs = 180;
    View.Units[1].ActionCue = ELDObservedActionCue::Hit;
    View.Units[1].ActionCueAgeMs = 40;
    View.Units[0].ActionAbility = 7;
    FLDWorldView Read;
    TestTrue(TEXT("Cues and their ages round-trip"), Wire(View, Read) && Read.Units.Num() == 2
        && Read.Units[0].ActionCue == ELDObservedActionCue::Cast && Read.Units[0].ActionCueAgeMs == 180 && Read.Units[0].ActionAbility == 7
        && Read.Units[1].ActionCue == ELDObservedActionCue::Hit && Read.Units[1].ActionCueAgeMs == 40);
    FString Error;
    for (const ELDObservedActionCue Cue : {ELDObservedActionCue::None, ELDObservedActionCue::Attack, ELDObservedActionCue::Cast, ELDObservedActionCue::Hit})
    {
        FLDObservedUnitView Unit = ContenderUnit();
        Unit.ActionCue = Cue;
        Unit.ActionCueAgeMs = Cue == ELDObservedActionCue::None ? 0 : 250;
        TestTrue(*FString::Printf(TEXT("Cue %d is a public cue"), int32(Cue)), LDObservation::ValidateUnit(Unit, Error));
    }
    FLDObservedUnitView Unknown = ContenderUnit();
    Unknown.ActionCue = static_cast<ELDObservedActionCue>(4);
    TestFalse(TEXT("Cue 4 is not defined"), LDObservation::ValidateUnit(Unknown, Error));
    FLDObservedUnitView Aged = ContenderUnit();
    Aged.ActionCueAgeMs = 1;
    TestFalse(TEXT("No cue carries no age"), LDObservation::ValidateUnit(Aged, Error));
    // A cast may name its public ability at that moment (user-approved CB-P10 amendment); nothing else may.
    FLDObservedUnitView Named = ContenderUnit();
    Named.ActionCue = ELDObservedActionCue::Cast;
    Named.ActionCueAgeMs = 30;
    Named.ActionAbility = 7;
    TestTrue(TEXT("A cast may name its ability"), LDObservation::ValidateUnit(Named, Error));
    Named.ActionCue = ELDObservedActionCue::Attack;
    TestFalse(TEXT("Only a cast names an ability"), LDObservation::ValidateUnit(Named, Error));
    for (uint8 Seat = 0; Seat < LDObservation::MaxPresentations; ++Seat)
    {
        FLDObservedUnitView Unit = ContenderUnit();
        Unit.Presentation = Seat;
        TestTrue(*FString::Printf(TEXT("Roster seat %d is a contender presentation"), Seat), LDObservation::ValidateUnit(Unit, Error));
    }
    // One distinct body tint per seat; camp NPCs never wear a contender tint and the two tiers differ.
    for (uint8 A = 0; A < LDObservation::MaxPresentations; ++A)
    {
        for (uint8 B = A + 1; B < LDObservation::MaxPresentations; ++B)
        {
            TestFalse(*FString::Printf(TEXT("Seats %d and %d have different tints"), A, B),
                ULDHumanoidVisualComponent::ContenderTint(A).Equals(ULDHumanoidVisualComponent::ContenderTint(B), .05f));
        }
        for (const ELDDemoMonsterId Monster : {ELDDemoMonsterId::MonsterScavenger, ELDDemoMonsterId::MonsterGuardian})
        {
            TestFalse(TEXT("Camp NPCs never wear a contender tint"),
                ULDHumanoidVisualComponent::MonsterTint(uint8(Monster)).Equals(ULDHumanoidVisualComponent::ContenderTint(A), .05f));
        }
    }
    TestFalse(TEXT("The two camp tiers look different"), ULDHumanoidVisualComponent::MonsterTint(uint8(ELDDemoMonsterId::MonsterScavenger))
        .Equals(ULDHumanoidVisualComponent::MonsterTint(uint8(ELDDemoMonsterId::MonsterGuardian)), .05f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoVisionProjectileTest, "Lockdown.Rules.DemoVision.Projectiles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoVisionProjectileTest::RunTest(const FString& Parameters)
{
    constexpr double PositionToleranceCm = 0.05 + UE_KINDA_SMALL_NUMBER;
    auto Flight = [](const FVector& Position, uint16 Heading = 0)
    {
        FLDObservedProjectileView Projectile;
        Projectile.Handle = FGuid::NewGuid();
        Projectile.PositionCm = Position;
        Projectile.Heading = Heading;
        Projectile.Look = ELDObservedProjectileLook::Chalk;
        return Projectile;
    };
    FLDWorldView Full = ActiveFrame(7, 3, 41);
    Full.Units = {ContenderUnit(1)};
    for (int32 Index = 0; Index < LDObservation::MaxProjectiles; ++Index)
    { Full.Projectiles.Add(Flight(FVector(10.37 + Index * 50, -20.62, 2.04), uint16(4000 * Index))); }
    FLDWorldView Read;
    TestTrue(TEXT("A full set of flights round-trips"), Wire(Full, Read) && Read.Projectiles.Num() == LDObservation::MaxProjectiles);
    bool bSame = Read.Projectiles.Num() == Full.Projectiles.Num();
    for (int32 Index = 0; bSame && Index < Full.Projectiles.Num(); ++Index)
    {
        const FLDObservedProjectileView& Sent = Full.Projectiles[Index];
        const FLDObservedProjectileView& Got = Read.Projectiles[Index];
        bSame = Got.Handle == Sent.Handle && Got.Heading == Sent.Heading && Got.Look == Sent.Look && Got.PositionCm.Equals(Sent.PositionCm, PositionToleranceCm);
    }
    TestTrue(TEXT("Every flight keeps its handle, heading, look and quantized position"), bSame);
    for (const auto Look : {ELDObservedProjectileLook::Sock, ELDObservedProjectileLook::BookShadow,
        ELDObservedProjectileLook::FlyerArming, ELDObservedProjectileLook::FlyerArmed, ELDObservedProjectileLook::SpeechCone})
    {
        FLDWorldView Effects=ActiveFrame(7,3,42);
        auto Effect=Flight(FVector(20,30,2));
        Effect.Look=Look;
        if (Look!=ELDObservedProjectileLook::Sock)
        {
            Effect.RadiusCm=Look==ELDObservedProjectileLook::SpeechCone ? 450.f : 180.f;
            Effect.InnerRadiusCm=Look==ELDObservedProjectileLook::SpeechCone ? 0.f : 80.f;
            Effect.AngleDegrees=Look==ELDObservedProjectileLook::SpeechCone ? 70.f : 0.f;
        }
        Effects.Projectiles.Add(Effect);
        FLDWorldView RoundTrip;
        TestTrue(TEXT("Approved skill geometry round-trips without private caster or build fields"),
            Wire(Effects,RoundTrip) && RoundTrip.Projectiles.Num()==1
            && RoundTrip.Projectiles[0].Look==Look && RoundTrip.Projectiles[0].RadiusCm==Effect.RadiusCm
            && RoundTrip.Projectiles[0].InnerRadiusCm==Effect.InnerRadiusCm
            && RoundTrip.Projectiles[0].AngleDegrees==Effect.AngleDegrees);
    }
    auto With = [&Full](TFunction<void(FLDWorldView&)> Change) { FLDWorldView View = Full; Change(View); return View; };
    TestFalse(TEXT("Flights beyond the bound refused"), Encodes(With([&Flight](FLDWorldView& V) { V.Projectiles.Add(Flight(FVector(0, 0, 2))); })));
    TestFalse(TEXT("A flight cannot reuse a unit handle"), Encodes(With([](FLDWorldView& V) { V.Projectiles[0].Handle = V.Units[0].Handle; })));
    TestFalse(TEXT("Flights have unique handles"), Encodes(With([](FLDWorldView& V) { V.Projectiles[1].Handle = V.Projectiles[0].Handle; })));
    TestFalse(TEXT("A flight requires a handle"), Encodes(With([](FLDWorldView& V) { V.Projectiles[0].Handle.Invalidate(); })));
    TestFalse(TEXT("Unknown flight look refused"), Encodes(With([](FLDWorldView& V) { V.Projectiles[0].Look = static_cast<ELDObservedProjectileLook>(255); })));
    TestFalse(TEXT("A flight cannot smuggle another effect's geometry"),
        Encodes(With([](FLDWorldView& V) { V.Projectiles[0].RadiusCm=180; })));
    TestFalse(TEXT("A trap must declare its visible radii"),
        Encodes(With([](FLDWorldView& V) { V.Projectiles[0].Look=ELDObservedProjectileLook::FlyerArmed; })));
    TestFalse(TEXT("Flights beyond the finite range refused"), Encodes(With([](FLDWorldView& V) { V.Projectiles[0].PositionCm = FVector(1000001, 0, 2); })));
    TestFalse(TEXT("A frame without a living source carries no flight"), Encodes(With([](FLDWorldView& V)
    { V.Footprint = FLDObservedFootprint(); V.Units.Reset(); })));

    // Selection uses the unit sight rule, nearest first and bounded; a flight beyond the ground is merely out of sight.
    FString Error;
    FLDSightGeometry Sight;
    TestTrue(TEXT("Sight builds"), Sight.Build(TestGeometry(), DemoRevision(), Whole(1400), Error));
    const FVector West(-600, 0, 2);
    auto Fact = [](const FVector& Position, const FVector2D& Heading = FVector2D(1, 0))
    {
        FLDObservedProjectileFact Out;
        Out.Key = FGuid::NewGuid();
        Out.PositionCm = Position;
        Out.Heading = Heading;
        return Out;
    };
    const TArray<FLDObservedProjectileFact> Facts = {Fact(FVector(-900, 0, 2)), Fact(FVector(300, 0, 2)), Fact(FVector(-600, 2000, 2)),
        Fact(FVector(-650, 50, 2)), Fact(FVector(9000, 0, 2))};
    TArray<const FLDObservedProjectileFact*> Visible;
    TestTrue(TEXT("Only flights in sight are selected, nearest first (wall, range and ground edge exclude the rest)"),
        LDObservation::SelectVisibleProjectiles(Sight, West, Facts, Visible, Error) && Visible.Num() == 2 && Visible[0] == &Facts[3] && Visible[1] == &Facts[0]);
    TArray<FLDObservedProjectileFact> Many;
    for (int32 Index = 0; Index < LDObservation::MaxProjectiles + 4; ++Index) { Many.Add(Fact(FVector(-600 + Index * 10, 100, 2))); }
    TestTrue(TEXT("Selection keeps the nearest bounded set"), LDObservation::SelectVisibleProjectiles(Sight, West, Many, Visible, Error)
        && Visible.Num() == LDObservation::MaxProjectiles && Visible[0] == &Many[0] && Visible.Last() == &Many[LDObservation::MaxProjectiles - 1]);
    const TArray<FLDObservedProjectileFact> Broken = {Fact(FVector(std::numeric_limits<double>::quiet_NaN(), 0, 2))};
    TestFalse(TEXT("A non-finite flight is a fault, not a hidden one"), LDObservation::SelectVisibleProjectiles(Sight, West, Broken, Visible, Error));

    // Observer-local handles: fresh per visibility interval, never the flight key, dropped with the observer generation.
    FLDObserverLedger Ledger;
    bool bReset = false;
    TestTrue(TEXT("Ledger begins alive"), Ledger.Begin(7, Error) && Ledger.Bind(ELDLifePhase::Alive, 1, bReset, Error));
    const FLDObservedProjectileFact A = Fact(FVector(-700, 0, 2)), B = Fact(FVector(-800, 0, 2), FVector2D(0, 1));
    auto HandleAt = [](const TArray<FLDObservedProjectileView>& Views, const FVector& Position)
    {
        const FLDObservedProjectileView* Found = Views.FindByPredicate([&Position](const FLDObservedProjectileView& View)
        { return FVector(View.PositionCm).Equals(Position, 1.0); });
        return Found ? Found->Handle : FGuid();
    };
    TArray<FGuid> Removed;
    TArray<FLDObservedProjectileView> Out;
    TestTrue(TEXT("Two flights open"), Ledger.SampleProjectiles({&A, &B}, Removed, Out, Error) && Out.Num() == 2 && Removed.IsEmpty());
    const FGuid FirstA = HandleAt(Out, A.PositionCm), FirstB = HandleAt(Out, B.PositionCm);
    const FLDObservedProjectileView* Northward = Out.FindByPredicate([&FirstB](const FLDObservedProjectileView& View) { return View.Handle == FirstB; });
    TestTrue(TEXT("Handles are fresh opaque GUIDs, never the flight key"), FirstA.IsValid() && FirstB.IsValid() && FirstA != A.Key && FirstB != B.Key);
    TestTrue(TEXT("Heading is the quantized planar yaw"), Northward && Northward->Heading == 16384);
    TestTrue(TEXT("A flight keeps its handle while in sight; one that left is removed"), Ledger.SampleProjectiles({&A}, Removed, Out, Error)
        && Out.Num() == 1 && Out[0].Handle == FirstA && Removed.Num() == 1 && Removed[0] == FirstB);
    TestTrue(TEXT("An ended flight is removed"), Ledger.SampleProjectiles({}, Removed, Out, Error) && Out.IsEmpty() && Removed.Num() == 1 && Removed[0] == FirstA);
    TestTrue(TEXT("Re-entering sight opens a fresh handle"), Ledger.SampleProjectiles({&A}, Removed, Out, Error) && Out.Num() == 1 && Out[0].Handle != FirstA);
    TestFalse(TEXT("Visible flights need unique keys"), Ledger.SampleProjectiles({&A, &A}, Removed, Out, Error));
    TArray<const FLDObservedProjectileFact*> TooMany;
    for (int32 Index = 0; Index <= LDObservation::MaxProjectiles; ++Index) { TooMany.Add(&Many[Index]); }
    TestFalse(TEXT("Flights beyond the bound are a capacity fault"), Ledger.SampleProjectiles(TooMany, Removed, Out, Error));
    TestTrue(TEXT("A new observer generation drops every flight binding"), Ledger.Bind(ELDLifePhase::Dead, 1, bReset, Error) && bReset
        && Ledger.SampleProjectiles({}, Removed, Out, Error) && Removed.IsEmpty() && Out.IsEmpty());
    TestFalse(TEXT("A dead observer authorizes no flight"), Ledger.SampleProjectiles({&A}, Removed, Out, Error));

    // The receiver drops a concealed flight at once and every flight on a receive timeout.
    FLDObservedFrameReceiver Receiver;
    TArray<FLDObservedClear> Cleared;
    FLDWorldView Frame = ActiveFrame(7, 3, 41);
    Frame.Projectiles = {Flight(FVector(1, 2, 2)), Flight(FVector(3, 4, 2))};
    TestTrue(TEXT("A frame with flights applies"), Receiver.AcceptFull(Frame, DemoRevision(), TestCatalog(), 20.0, Cleared, Error)
        == FLDObservedFrameReceiver::EResult::Applied && Receiver.Read().Frame && Receiver.Read().Frame->Projectiles.Num() == 2);
    FLDObservedConceal Conceal;
    Conceal.MatchGeneration = 7;
    Conceal.Generation = 3;
    Conceal.Revision = 42;
    Conceal.Removed = {Frame.Projectiles[0].Handle};
    TestTrue(TEXT("A concealed flight stops being drawn at once"), Receiver.AcceptConceal(Conceal, Cleared, Error) == FLDObservedFrameReceiver::EResult::Applied
        && Receiver.Read().Frame->Projectiles.Num() == 1 && Receiver.Read().Frame->Projectiles[0].Handle == Frame.Projectiles[1].Handle);
    TestTrue(TEXT("A receive timeout drops every flight"), Receiver.Tick(20.0 + FLDObservedFrameReceiver::StaleAfterSeconds + .01, Cleared)
        && Receiver.Read().Frame && Receiver.Read().Frame->Projectiles.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoVisionPresentationTest, "Lockdown.Rules.DemoVision.Presentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoVisionPresentationTest::RunTest(const FString& Parameters)
{
    namespace P = LDObservedPresentation;
    auto Point = [](double At, const FVector& Location, float Yaw, bool bSnap = false)
    {
        P::FPoint Out;
        Out.At = At;
        Out.Location = Location;
        Out.Yaw = Yaw;
        Out.bSnap = bSnap;
        return Out;
    };
    const TArray<P::FPoint> Path = {Point(10.0, FVector(0, 0, 90), 350.f, true), Point(10.1, FVector(50, 0, 90), 10.f),
        Point(10.2, FVector(100, 20, 90), 10.f), Point(10.3, FVector(900, 20, 90), 90.f, true)};
    P::FPose Pose = P::Sample(Path, 9.9);
    TestTrue(TEXT("Before the first sample the pose waits at it"), Pose.Reached == INDEX_NONE && Pose.Location.Equals(FVector(0, 0, 90)) && Pose.Velocity.IsZero());
    Pose = P::Sample(Path, 10.05);
    TestTrue(TEXT("Between two samples the pose is linear and moves at their speed"), Pose.Reached == 0
        && Pose.Location.Equals(FVector(25, 0, 90), .001) && Pose.Velocity.Equals(FVector(500, 0, 0), .001));
    TestTrue(TEXT("Yaw takes the shorter arc across north"), FMath::IsNearlyEqual(FRotator::NormalizeAxis(double(Pose.Yaw)), 0.0, 0.01));
    Pose = P::Sample(Path, 10.25);
    TestTrue(TEXT("A snap sample is reached by a jump, never by a sweep"), Pose.Reached == 2 && Pose.Location.Equals(FVector(100, 20, 90)) && Pose.Velocity.IsZero());
    Pose = P::Sample(Path, 10.3);
    TestTrue(TEXT("The snap sample shows once the clock reaches it"), Pose.Reached == 3 && Pose.Location.Equals(FVector(900, 20, 90)));
    Pose = P::Sample(Path, 12.0);
    TestTrue(TEXT("After the newest sample the pose holds it: nothing is extrapolated"), Pose.Reached == 3
        && Pose.Location.Equals(FVector(900, 20, 90)) && Pose.Velocity.IsZero() && FMath::IsNearlyEqual(Pose.Yaw, 90.f));
    TestTrue(TEXT("No sample, no pose"), P::Sample(TArray<P::FPoint>(), 10.0).Reached == INDEX_NONE);

    TestTrue(TEXT("The first arrival seeds the offset"), FMath::IsNearlyEqual(P::TrackOffset(false, 0, 0, 105.0, 100.0), 5.0));
    TestTrue(TEXT("An earlier arrival lowers the offset at once"), FMath::IsNearlyEqual(P::TrackOffset(true, 5.0, .1, 105.08, 100.1), 4.98));
    TestTrue(TEXT("A late arrival raises it only by the bounded growth"),
        FMath::IsNearlyEqual(P::TrackOffset(true, 5.0, .1, 105.2, 100.1), 5.0 + .1 * P::OffsetGrowthPerSecond));
    TestTrue(TEXT("Growth never passes the arrival itself"), FMath::IsNearlyEqual(P::TrackOffset(true, 5.0, 100.0, 105.11, 100.1), 5.01));
    TestTrue(TEXT("A jump beyond the reseed bound adopts the new clock"), FMath::IsNearlyEqual(P::TrackOffset(true, 5.0, .1, 200.0, 10.0), 190.0));
    TestTrue(TEXT("The clock trails the newest sample by more than one publication"), P::DelaySeconds > .1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoVisionPropFadeTest, "Lockdown.Rules.DemoVision.PropFade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoVisionPropFadeTest::RunTest(const FString& Parameters)
{
    // A thin pole at the origin, 600 cm tall; the camera looks down from behind it at -60 degrees.
    const FBox Pole(FVector(-15, -15, 0), FVector(15, 15, 600));
    const FVector Camera(-1000, 0, 1500);
    TestTrue(TEXT("A pole between the camera and a body's chest occludes it"), LDPropFade::Occludes(Pole, Camera, FVector(300, 0, 90)));
    TestFalse(TEXT("A pole beside the line does not"), LDPropFade::Occludes(Pole, Camera, FVector(300, 600, 90)));
    TestFalse(TEXT("A body in front of the pole is not hidden by it"), LDPropFade::Occludes(Pole, Camera, FVector(-400, 0, 90)));
    // Decorations have no collision: a body can stand in the pole, or against it on the camera's side.
    TestTrue(TEXT("A body standing inside the pole is run through by it"), LDPropFade::Occludes(Pole, Camera, FVector(5, 0, 90)));
    TestTrue(TEXT("A body touching the pole on the camera's side is run through by it"), LDPropFade::Occludes(Pole, Camera, FVector(-15 - LDPropFade::PierceRadiusCm + 1, 0, 160)));
    TestFalse(TEXT("A body clear of the pole on the camera's side is not"), LDPropFade::Occludes(Pole, Camera, FVector(-15 - LDPropFade::PierceRadiusCm - 5, 0, 90)));
    TestFalse(TEXT("A target above the prop is not run through by it"), LDPropFade::Occludes(Pole, Camera, FVector(-40, 0, 700)));
    TestFalse(TEXT("An unset prop never occludes"), LDPropFade::Occludes(FBox(ForceInit), Camera, FVector(300, 0, 90)));
    float Fade = 0.f;
    Fade = LDPropFade::Step(Fade, true, .1f);
    TestTrue(TEXT("Fading in is gradual"), Fade > 0.f && Fade < 1.f);
    Fade = LDPropFade::Step(Fade, true, 1.f);
    TestTrue(TEXT("Fading in stops at full"), Fade == 1.f);
    Fade = LDPropFade::Step(Fade, false, 1.f);
    TestTrue(TEXT("Clearing the line fades back to opaque"), Fade == 0.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoVisionFlightTrailTest, "Lockdown.Rules.DemoVision.FlightTrail",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoVisionFlightTrailTest::RunTest(const FString& Parameters)
{
    using namespace LDFlightTrail;
    TArray<FPuff> Puffs;
    Layout(0.f, Puffs);
    TestTrue(TEXT("A chalk standing still has no trail"), Puffs.IsEmpty());
    Layout(MinSpacingCm / SpacingPerSpeed - 1.f, Puffs);
    TestTrue(TEXT("A drift below the threshold shows no trail"), Puffs.IsEmpty());
    // The approved active_snark_bolt flies at 1600 cm/s; the proxy reaches about 70 % of it in the first frames.
    for (const float Speed : {1120.f, 1600.f})
    {
        Layout(Speed, Puffs);
        if (!TestEqual(FString::Printf(TEXT("A flying chalk draws every puff (%.0f cm/s)"), Speed), Puffs.Num(), PuffCount)) { return false; }
        TestTrue(TEXT("The trail starts at the chalk's tail"), Puffs[0].OffsetCm - Puffs[0].DiameterCm * .5f <= 0.f);
        const FPuff& Last = Puffs.Last();
        TestTrue(FString::Printf(TEXT("The trail is at least twice the 50 cm chalk (%.0f cm/s)"), Speed), Last.OffsetCm + Last.DiameterCm * .5f >= 100.f);
        TestTrue(TEXT("The head is about as thick as the 22 cm chalk"), Puffs[0].DiameterCm >= 18.f);
        bool bTaper = true, bStreak = true;
        for (int32 Index = 1; Index < Puffs.Num(); ++Index)
        {
            bTaper &= Puffs[Index].DiameterCm < Puffs[Index - 1].DiameterCm && Puffs[Index].OffsetCm > Puffs[Index - 1].OffsetCm;
            if (Index <= PuffCount / 2)
            {
                bStreak &= Puffs[Index].OffsetCm - Puffs[Index - 1].OffsetCm <= (Puffs[Index].DiameterCm + Puffs[Index - 1].DiameterCm) * .5f;
            }
        }
        TestTrue(TEXT("Puffs thin away from the chalk"), bTaper);
        TestTrue(FString::Printf(TEXT("The thick half overlaps into one streak (%.0f cm/s)"), Speed), bStreak);
    }
    return true;
}
// A visible contender shows its Q/W/E actives through the same authorized frame; the camp carries none; malformed or
// unapproved actives are refused; the card shows no cooldown.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoVisionOpponentActivesTest, "Lockdown.Rules.DemoVision.OpponentActives",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoVisionOpponentActivesTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("Q/W/E actives are part of the complete contender field set, not of the camp set"),
        (LDObservation::ContenderFields & LDObservation::Actives) != 0 && (LDObservation::CampFields & LDObservation::Actives) == 0);
    FLDWorldView Frame = ActiveFrame(7, 3, 42);
    Frame.Units.Add(ContenderUnit());
    Frame.Units.Add(CampUnit());
    FLDWorldView Read;
    if (!TestTrue(TEXT("A frame with Q/W/E actives round-trips"), Wire(Frame, Read) && Read.Units.Num() == 2)) { return false; }
    const FLDObservedActivesView& Got = Read.Units[0].Actives;
    TestTrue(TEXT("All three slots keep their ability"), Got.Abilities[0] == uint8(ELDDemoAbilityId::ActivePantsDown)
        && Got.Abilities[1] == uint8(ELDDemoAbilityId::ActiveSnarkBolt) && Got.Abilities[2] == uint8(ELDDemoAbilityId::ActiveNotMad));
    TestTrue(TEXT("The camp member carries no actives"), Read.Units[1].Actives.Abilities[0] == 0 && Read.Units[1].Actives.Abilities[1] == 0
        && Read.Units[1].Actives.Abilities[2] == 0);
    auto Refused = [&](const TCHAR* What, TFunctionRef<void(FLDObservedUnitView&, FLDObservedUnitView&)> Change)
    {
        FLDWorldView Candidate = ActiveFrame(7, 3, 43);
        Candidate.Units.Add(ContenderUnit());
        Candidate.Units.Add(CampUnit());
        Change(Candidate.Units[0], Candidate.Units[1]);
        TestFalse(What, Encodes(Candidate));
    };
    Refused(TEXT("The same ability in two slots is refused"), [](FLDObservedUnitView& C, FLDObservedUnitView&) { C.Actives.Abilities[1] = C.Actives.Abilities[0]; });
    Refused(TEXT("A camp member with actives is refused"), [](FLDObservedUnitView&, FLDObservedUnitView& M)
    { M.Actives.Abilities[0] = uint8(ELDDemoAbilityId::ActiveSnarkBolt); });
    Refused(TEXT("A contender without the actives field is not complete"), [](FLDObservedUnitView& C, FLDObservedUnitView&)
    { C.AuthorizedFieldMask &= ~uint32(LDObservation::Actives); });
    FLDObservedUnitView Empty = ContenderUnit();
    Empty.Actives = FLDObservedActivesView();
    FString Error;
    TestTrue(TEXT("Three empty slots are a valid contender"), LDObservation::ValidateUnit(Empty, Error));

    const FLDObservedCatalog Catalog = TestCatalog();
    TestTrue(TEXT("Approved actives are accepted"), Catalog.Accepts(ContenderUnit(), Error));
    FLDObservedUnitView Unapproved = ContenderUnit();
    Unapproved.Actives.Abilities[1] = uint8(ELDDemoAbilityId::ActiveStareDown);
    TestFalse(TEXT("An active outside the approved catalog is refused"), Catalog.Accepts(Unapproved, Error));

    const LDHUD::FObservedCard Card = LDHUD::ObservedCard(ContenderUnit(), TEXT("对手"), nullptr, 0.);
    TestTrue(TEXT("The target card shows all Q/W/E tiles"), Card.bActives && Card.Actives[0].Key == TEXT("Q")
        && Card.Actives[1].Key == TEXT("W") && Card.Actives[2].Key == TEXT("E")
        && !Card.Actives[0].bEmpty && !Card.Actives[1].bEmpty && !Card.Actives[2].bEmpty);
    const FString Text = LDHUD::ObservedUnit(ContenderUnit(), TEXT("对手"), nullptr, 0.);
    TestTrue(TEXT("The card text never names a cooldown"), !Text.Contains(TEXT("冷却")));
    const LDHUD::FObservedCard CampCard = LDHUD::ObservedCard(CampUnit(), FString(), nullptr, 0.);
    TestFalse(TEXT("A camp member's card has no Q/W/E tiles"), CampCard.bActives);
    return true;
}
// #147: every public status a unit can carry at once (all combat cues plus its kind's marker) fits the bound, so a fully
// debuffed body never stops the observation; one more than the bound, or a repeated status, is still refused.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoVisionFullStatusBoundTest, "Lockdown.Rules.DemoVision.FullStatusBound",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoVisionFullStatusBoundTest::RunTest(const FString& Parameters)
{
    // Each kind's accepted status ids, probed one at a time with every stacks/expiry shape a status may use.
    const auto AcceptedAlone = [](FLDObservedUnitView Unit)
    {
        TArray<FLDObservedStatus> Accepted;
        for (int32 Id = 1; Id < 256; ++Id)
        {
            for (const FLDObservedStatus Shape : {FLDObservedStatus{uint8(Id), 1, 0}, FLDObservedStatus{uint8(Id), 1, 5000}})
            {
                Unit.Statuses = {Shape};
                FString Error;
                if (LDObservation::ValidateUnit(Unit, Error)) { Accepted.Add(Shape); break; }
            }
        }
        return Accepted;
    };
    const TArray<FLDObservedStatus> ContenderAll = AcceptedAlone(ContenderUnit());
    const TArray<FLDObservedStatus> CampAll = AcceptedAlone(CampUnit());
    const auto Has = [](const TArray<FLDObservedStatus>& Rows, ELDDemoStatusId Id)
    { return Rows.ContainsByPredicate([Id](const FLDObservedStatus& Row) { return Row.StatusId == uint8(Id); }); };
    TestTrue(TEXT("Silence and burn are public on both kinds"), Has(ContenderAll, ELDDemoStatusId::Silence) && Has(ContenderAll, ELDDemoStatusId::Burn)
        && Has(CampAll, ELDDemoStatusId::Silence) && Has(CampAll, ELDDemoStatusId::Burn));
    TestEqual(TEXT("The bound is exactly the most public statuses one unit can show at once"),
        FMath::Max(ContenderAll.Num(), CampAll.Num()), LDObservation::MaxStatuses);

    FLDWorldView Frame = ActiveFrame(7, 3, 44);
    for (int32 Index = 0; Index < LDObservation::MaxUnits; ++Index)
    {
        FLDObservedUnitView Unit = Index % 2 ? CampUnit(Index + 1) : ContenderUnit(Index + 1);
        Unit.Statuses = Index % 2 ? CampAll : ContenderAll;
        for (FLDObservedStatus& Status : Unit.Statuses) { if (Status.StatusId == uint8(ELDDemoStatusId::Burn)) { Status.Stacks = 5; } }
        FString Error;
        TestTrue(TEXT("A unit with every public status of its kind is valid"), LDObservation::ValidateUnit(Unit, Error));
        Frame.Units.Add(Unit);
    }
    FLDWorldView Read;
    int64 Bytes = 0;
    if (!TestTrue(TEXT("A frame of fully debuffed units round-trips"), Wire(Frame, Read, &Bytes) && Read.Units.Num() == LDObservation::MaxUnits))
    { return false; }
    AddInfo(FString::Printf(TEXT("Fully debuffed observation frame: %lld bytes"), Bytes));
    // #97: with 58 units every one fully debuffed the frame is about 13 KB; the unreliable full view still travels as
    // partial bunches, so the bound is twice the old 8 KB rather than a cap the runtime enforces.
    TestTrue(TEXT("and stays inside the frame capacity"), Bytes < 16384);
    TestTrue(TEXT("Every status and its stacks survive"), Read.Units[0].Statuses.Num() == ContenderAll.Num()
        && Read.Units[1].Statuses.Num() == CampAll.Num()
        && Read.Units[0].Statuses.ContainsByPredicate([](const FLDObservedStatus& Row)
        { return Row.StatusId == uint8(ELDDemoStatusId::Burn) && Row.Stacks == 5; }));

    FLDObservedUnitView Over = ContenderUnit();
    Over.Statuses = ContenderAll;
    Over.Statuses.Add(ContenderAll[0]);
    FString Error;
    TestFalse(TEXT("One more than the bound is refused"), LDObservation::ValidateUnit(Over, Error));
    Over.Statuses.Pop();
    Over.Statuses.Last() = Over.Statuses[0];
    TestFalse(TEXT("A repeated status is refused within the bound"), LDObservation::ValidateUnit(Over, Error));
    return true;
}
} // namespace LDDemoVisionTests
#endif
