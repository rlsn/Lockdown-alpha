#include "Framework/LDGameMode.h"

#include "Character/LDCharacter.h"
#include "Development/LDDemoContentCatalog.h"
#include "Region/LDMapRegistry.h"
#include "World/LDCampDirector.h"
#include "World/LDFieldEcology.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Evaluation/LDEvaluationSeeds.h"
#endif

bool ALDGameMode::PlanFieldSpawns(uint8 Segment, TConstArrayView<FGuid> Order,
    TMap<FGuid, FLDLegalGround>& Out, FString& Error) const
{
    using namespace LDFieldEcology;
    Out.Reset();
    const auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    FGroundSampler Sampler;
    if (!Registry || !DemoContent || !Camps || !Camps->GetState().GetRoster()
        || !Sampler.Initialize(*Registry, *DemoContent, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("Field deployment requires the camp roster and registered field."); }
        return false;
    }

    const auto& Parameters = DemoContent->Get().Parameters;
    const double CaptureDistanceCm = Centimeters(Parameters.FieldSpawnCaptureDistanceCm);
    const double PatrolMarginCm = Centimeters(Parameters.FieldSpawnPatrolMarginCm);
    const int32 SpawnPoolMinimum = int32(Parameters.FieldSpawnPoolMin);
    constexpr double CampPlacementMarginCm = 300.;
    constexpr double CandidateSeparationCm = 900.;
    constexpr double FlatClearanceCm = 40.;
    const int32 CandidateCount = FMath::Max(48, SpawnPoolMinimum);
    struct FExclusion
    {
        FVector2D Center;
        double RadiusSquared;
    };
    TArray<FExclusion> Exclusions;
    Exclusions.Add({FVector2D(Sampler.Capture().FootCm), FMath::Square(CaptureDistanceCm)});
    const FLDBossState& BossState = Camps->GetBoss();
    const FLDBossRules& Boss = BossState.Rules;
    ALDCharacter* BossActor = nullptr;
    if (BossState.Body.IsValid() && ResolveBody(MatchContext, BossState.Body, BossActor) == ELDCommitReject::None && BossActor)
    {
        Exclusions.Add({FVector2D(BossActor->GetActorLocation()), FMath::Square(Boss.AggroRadiusCm + PatrolMarginCm)});
    }
    // The peddler keeps its place through a rest; a contender cannot land inside its capsule.
    ALDCharacter* PeddlerActor = nullptr;
    if (Camps->IsPeddlerAlive() && ResolveBody(MatchContext, Camps->GetPeddlerBody(), PeddlerActor) == ELDCommitReject::None && PeddlerActor)
    {
        Exclusions.Add({FVector2D(PeddlerActor->GetActorLocation()), FMath::Square(CampPlacementMarginCm)});
    }
    for (const FVector& Waypoint : Boss.Waypoints)
    {
        Exclusions.Add({FVector2D(Waypoint), FMath::Square(Boss.AggroRadiusCm + PatrolMarginCm)});
    }
    for (const FLDCampSpec& Camp : Camps->GetState().GetRoster()->Camps)
    {
        double Radius = CampPlacementMarginCm;
        if (Camp.Tier == ELDDemoCampTier::Elite || Camp.Tier == ELDDemoCampTier::Ancient)
        {
            // Member definitions identify layout prototypes. The tier pools contain the monster definitions.
            for (const FLDCampSpeciesPool& Pool : Camp.Pools)
            {
                for (FName Species : Pool.Species)
                {
                    const FLDDemoMonsterDefinition* Monster = nullptr;
                    if (!DemoContent->FindMonsterByName(Species, Monster, Error) || !Monster) { return false; }
                    Radius = FMath::Max(Radius, Centimeters(Monster->PerceptionRadiusCm) + PatrolMarginCm);
                }
            }
        }
        for (const FLDCampMemberSpec& Member : Camp.Members)
        {
            Exclusions.Add({FVector2D(Member.HomeCm), FMath::Square(Radius)});
        }
    }

    uint32 Seed = GetTypeHash(MatchContext.LedgerMatch);
#if WITH_DEV_AUTOMATION_TESTS
    if (const auto* Seeds = LDEvaluationSeeds::Armed()) { Seed = uint32(Seeds->SpawnSeed); }
#endif
    FRandomStream Random(int32(Seed ^ Segment));
    TArray<FLDLegalGround> Pool;
    const auto Add = [&](FVector2D Point, bool bAnchor)
    {
        if (Exclusions.ContainsByPredicate([Point](const FExclusion& Zone)
            { return FVector2D::DistSquared(Point, Zone.Center) < Zone.RadiusSquared; })
            || Pool.ContainsByPredicate([Point](const FLDLegalGround& Ground)
            { return FVector2D::DistSquared(Point, FVector2D(Ground.FootCm)) < FMath::Square(CandidateSeparationCm); }))
        {
            return;
        }
        FLDLegalGround Ground;
        if (Sampler.Resolve(Point, Ground, bAnchor ? 0. : FlatClearanceCm)) { Pool.Add(MoveTemp(Ground)); }
    };

    for (FName Id : Registry->FieldSpawnAnchors())
    {
        if (const FLDMapBinding* Anchor = Registry->Resolve(Id, Registry->Generation))
        {
            Add(FVector2D(Anchor->Position), true);
        }
    }
    for (int32 Attempt = 0; Attempt < 1536 && Pool.Num() < CandidateCount; ++Attempt)
    {
        Add(Sampler.Draw(Random), false);
    }
    if (Pool.Num() < SpawnPoolMinimum)
    {
        // The random draw can miss narrow legal ground. Sweep the whole field on a fixed lattice under the same rules;
        // only a field that still lacks enough legal ground is an authority fault (map or parameter, not chance).
        const int32 Drawn = Pool.Num();
        TArray<FVector2D> Grid;
        GridPoints(Sampler.GetBounds(), CandidateSeparationCm, Grid);
        for (int32 Index = 0; Index < Grid.Num() && Pool.Num() < CandidateCount; ++Index) { Add(Grid[Index], false); }
        if (Pool.Num() < SpawnPoolMinimum)
        {
            Error = FString::Printf(TEXT("Field deployment found %d legal, distributed candidates (%d drawn, %d more from a %d-point %.0f cm grid sweep); at least %d (FIELD.SpawnPoolMin) are required: the field or its parameters leave too little legal ground."),
                Pool.Num(), Drawn, Pool.Num() - Drawn, Grid.Num(), CandidateSeparationCm, SpawnPoolMinimum);
            return false;
        }
    }

    TArray<FVector> Feet;
    for (const FLDLegalGround& Ground : Pool) { Feet.Add(Ground.FootCm); }
    TArray<TOptional<FVector>> Previous;
    for (const FGuid& Id : Order)
    {
        const FVector* Found = PreviousFieldSpawns.Find(Id);
        Previous.Add(Found ? TOptional<FVector>(*Found) : TOptional<FVector>());
    }
    TArray<int32> Chosen;
    if (ChooseFieldSpawns(Feet, Previous, Centimeters(Parameters.FieldSpawnSeparationCm), Random, Chosen))
    {
        for (int32 Index = 0; Index < Order.Num(); ++Index) { Out.Add(Order[Index], Pool[Chosen[Index]]); }
        Error.Reset();
        return true;
    }
    Error = TEXT("Field deployment could not maintain the required separation between all contenders.");
    return false;
}

bool ALDGameMode::PlanMapRecoveryPacks(uint8 Segment, TArray<FVector>& Out, FString& Error) const
{
    using namespace LDFieldEcology;
    Out.Reset();
    const auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    FGroundSampler Sampler;
    if (!Registry || !DemoContent || !Sampler.Initialize(*Registry, *DemoContent, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("Map recovery packs require registered field ground."); }
        return false;
    }
    const auto& Parameters = DemoContent->Get().Parameters;
    const int32 RecoveryPackCount = int32(Parameters.FieldMapRecoveryCount);
    const double RecoveryPackSeparationCm = Centimeters(Parameters.FieldMapRecoverySeparationCm);
    FRandomStream Random(int32(LDAirdrop::Mix(Airdrop.Schedule.Seed ^ (0x5245434FULL + Segment))));
    const double PickupRadiusSq = FMath::Square(Centimeters(Parameters.CampClearOrbRadiusCm));
    for (int32 Attempt = 0; Attempt < 1024 && Out.Num() < RecoveryPackCount; ++Attempt)
    {
        const FVector2D Point = Sampler.Draw(Random);
        if (Out.ContainsByPredicate([Point, RecoveryPackSeparationCm](const FVector& Other)
            { return FVector2D::DistSquared(Point, FVector2D(Other)) < FMath::Square(RecoveryPackSeparationCm); }))
        {
            continue;
        }
        bool bAtContender = false;
        for (const auto& Pair : PendingFieldSpawns)
        {
            if (FVector2D::DistSquared(Point, FVector2D(Pair.Value.FootCm)) <= PickupRadiusSq)
            {
                bAtContender = true;
                break;
            }
        }
        if (bAtContender) { continue; }
        FLDLegalGround Ground;
        if (Sampler.Resolve(Point, Ground)) { Out.Add(Ground.SurfaceCm); }
    }
    if (Out.Num() == RecoveryPackCount) { return true; }
    Error = FString::Printf(TEXT("Map recovery packs found %d of %d reachable positions at the required spacing."),
        Out.Num(), RecoveryPackCount);
    return false;
}
