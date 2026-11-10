#include "Character/LDCharacterMovementComponent.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Framework/LDDemoEntry.h"

namespace
{
    constexpr int32 NativeRecordCapacity=128;

    FLDNativeSupportSample SupportOf(const UCharacterMovementComponent& Move)
    {
        FLDNativeSupportSample Result;
        Result.Grounded=Move.IsMovingOnGround() && Move.CurrentFloor.IsWalkableFloor();
        if (Result.Grounded) { Result.Floor=Move.CurrentFloor.HitResult.GetComponent(); }
        return Result;
    }

    bool SameSupport(const FLDNativeSupportSample& A, const FLDNativeSupportSample& B)
    { return A.Grounded==B.Grounded && A.Floor==B.Floor; }
}

struct ULDCharacterMovementComponent::FNativeCapture
{
    struct FBookmark
    {
        int32 Count=0;
        FVector Foot=FVector::ZeroVector;
        FVector NativePosition=FVector::ZeroVector;
        FLDNativeSupportSample Support;
        double Time=0;
        double SliceEnd=0;
    };
    enum class ERevert : uint8 { None, Retry, Failure };

    FLDNativeMotionBatch Batch;
    FLDNativeSupportSample LastSupport;
    FVector LastFoot=FVector::ZeroVector;
    double Cursor=0;
    double SliceEnd=0;
    double NativeScale=1;
    float NativeDuration=0;
    int32 SafeDepth=0, AdjustmentDepth=0, StepDepth=0, StepDirectMoves=0;
    bool Walking=false, Falling=false;
    TOptional<FBookmark> WalkBookmark;
    ERevert PendingRevert=ERevert::None;
    FString Error;

    void Fail(const TCHAR* Detail) { if (Error.IsEmpty()) { Error=Detail; } }

    bool Append(FLDNativeMotionRecord Record)
    {
        if (Batch.Records.Num()>=NativeRecordCapacity)
        { Fail(TEXT("Native motion record capacity exceeded.")); return false; }
        Batch.Records.Add(MoveTemp(Record));
        return true;
    }

    FBookmark Mark(FVector NativePosition) const
    { return {Batch.Records.Num(),LastFoot,NativePosition,LastSupport,Cursor,SliceEnd}; }

    void Restore(const FBookmark& Bookmark, FVector NativePosition, const FLDNativeSupportSample& Support)
    {
        if (!NativePosition.Equals(Bookmark.NativePosition,.000001) || !SameSupport(Support,Bookmark.Support))
        { Fail(TEXT("Native rollback did not restore its witnessed position and support.")); return; }
        Batch.Records.SetNum(Bookmark.Count,EAllowShrinking::No);
        LastFoot=Bookmark.Foot;
        LastSupport=Bookmark.Support;
        Cursor=Bookmark.Time;
        SliceEnd=Bookmark.SliceEnd;
        ++Batch.RevertedTrials;
    }

    double StartFor(float RemainingSeconds)
    {
        if (!FMath::IsFinite(RemainingSeconds) || RemainingSeconds<0 || RemainingSeconds>NativeDuration)
        { Fail(TEXT("Native physics supplied an out-of-step remaining duration.")); return Cursor; }
        const double Start=RemainingSeconds==NativeDuration?Batch.Step.StartServerSeconds
            : Batch.Step.EndServerSeconds-double(RemainingSeconds)*NativeScale;
        if (Cursor>Start+.0000001)
        { Fail(TEXT("Native physics attempted to reuse already committed simulation time.")); return Cursor; }
        return FMath::Max(Cursor,Start);
    }
};

bool ULDCharacterMovementComponent::BeginNativeMotionCapture(const FLDAuthorityStep& Step, const FLDCombatBodyRef& Body,
    const FGuid& Stream, const FLDWorldRevision& World, FString& Error)
{
    const double Seconds=Step.EndServerSeconds-Step.StartServerSeconds;
    if (NativeCapture || !UpdatedComponent || !Step.Match.IsValid() || !Step.StepSerial || !Body.IsValid()
        || !Stream.IsValid() || !World.IsValid() || !FMath::IsFinite(Seconds) || Seconds<=0
        || !FMath::IsFinite(Step.StartServerSeconds) || Step.StartServerSeconds<0 || Seconds>MAX_flt)
    {
        Error=TEXT("Native capture requires a unique current body, world and finite step.");
        return false;
    }
    NativeCapture=MakeShared<FNativeCapture>();
    auto& Capture=*NativeCapture;
    Capture.Batch.Step=Step;
    Capture.Batch.Body=Body;
    Capture.Batch.StreamId=Stream;
    Capture.Batch.World=World;
    Capture.Batch.PreviousSequence=NativeRecordSequence;
    Capture.Batch.StartFootCm=Capture.LastFoot=GetActorFeetLocation();
    Capture.Batch.InitialSupport=Capture.LastSupport=SupportOf(*this);
    Capture.Cursor=Step.StartServerSeconds;
    Capture.SliceEnd=Step.EndServerSeconds;
    Capture.NativeDuration=float(Seconds);
    Capture.NativeScale=Seconds/double(Capture.NativeDuration);
    Error.Reset();
    return true;
}

void ULDCharacterMovementComponent::CaptureNativeSupport() const
{
    if (!NativeCapture || !NativeCapture->Error.IsEmpty() || NativeCapture->PendingRevert!=FNativeCapture::ERevert::None) { return; }
    auto& Capture=*NativeCapture;
    const FVector Foot=GetActorFeetLocation();
    if (!Foot.Equals(Capture.LastFoot,.000001))
    { Capture.Fail(TEXT("Native support changed after an unrecorded position update.")); return; }
    const auto Support=SupportOf(*this);
    if (Support.Grounded && !Support.Floor.IsValid())
    { Capture.Fail(TEXT("Native grounded support has no actual floor component.")); return; }
    if (SameSupport(Support,Capture.LastSupport)) { return; }
    FLDNativeMotionRecord Record;
    Record.Kind=ELDNativeMotionRecordKind::Support;
    Record.FromFootCm=Record.ToFootCm=Foot;
    Record.StartServerSeconds=Record.EndServerSeconds=Capture.Cursor;
    Record.Support=Support;
    if (Capture.Append(MoveTemp(Record))) { Capture.LastSupport=Support; }
}

bool ULDCharacterMovementComponent::FinishNativeMotionCapture(TSharedPtr<const FLDNativeMotionBatch>& Out, FString& Error)
{
    Out.Reset();
    if (!NativeCapture)
    { Error=TEXT("No native capture is active."); return false; }
    CaptureNativeSupport();
    auto Capture=MoveTemp(NativeCapture);
    if (Capture->PendingRevert!=FNativeCapture::ERevert::None)
    { Capture->Fail(TEXT("Native capture ended with an unresolved rollback witness.")); }
    if (Capture->Cursor>Capture->Batch.Step.EndServerSeconds
        || !GetActorFeetLocation().Equals(Capture->LastFoot,.000001))
    { Capture->Fail(TEXT("Native capture did not cover its final pose within the issued step.")); }
    if (!Capture->Error.IsEmpty()) { Error=Capture->Error; return false; }
    if (uint64(Capture->Batch.Records.Num())>MAX_uint64-NativeRecordSequence)
    { Error=TEXT("Native record sequence exhausted."); return false; }
    for (auto& Record : Capture->Batch.Records) { Record.Sequence=++NativeRecordSequence; }
    Capture->Batch.ThroughSequence=NativeRecordSequence;
    Capture->Batch.EndFootCm=Capture->LastFoot;
    Capture->Batch.FinalSupport=Capture->LastSupport;
    Out=MakeShared<const FLDNativeMotionBatch>(MoveTemp(Capture->Batch));
    Error.Reset();
    return true;
}

bool ULDCharacterMovementComponent::MoveUpdatedComponentImpl(const FVector& Delta, const FQuat& Rotation,
    bool Sweep, FHitResult* Hit, ETeleportType Teleport)
{
    const FVector From=NativeCapture?GetActorFeetLocation():FVector::ZeroVector;
    int32 StepMove=0;
    if (NativeCapture && NativeCapture->StepDepth && !NativeCapture->SafeDepth && !NativeCapture->AdjustmentDepth)
    { StepMove=++NativeCapture->StepDirectMoves; }
    // #97: bodies block each other, but a dash runs through them (its path slow hits the bodies it crosses); the capsule
    // ignores other bodies only for the dash's own sweeps, so everyone else still bumps into the dashing body.
    // #198 F: a forced push (knockback) is blocked by bodies; C02 reads its first real block.
    const bool bPushing=IsDashing()&&bKnockbackMotion;
    const bool bThroughBodies=Sweep&&UpdatedPrimitive&&IsDashing()&&!bKnockbackMotion;
    if (bThroughBodies) { UpdatedPrimitive->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore); }
    const bool Result=Super::MoveUpdatedComponentImpl(Delta,Rotation,Sweep,Hit,Teleport);
    if (bThroughBodies) { UpdatedPrimitive->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block); }
    if (bPushing && bWatchKnockbackBlock && Sweep && Hit && Hit->bBlockingHit && !Hit->bStartPenetrating && !IsWalkable(*Hit))
    {
        const FVector Heading=FVector(Delta.X,Delta.Y,0).GetSafeNormal();
        const UPrimitiveComponent* Blocking=Hit->GetComponent();
        AActor* Blocker=Hit->GetActor();
        const bool bBody=Cast<ALDCharacter>(Blocker)!=nullptr;
        const bool bStatic=Blocking && Blocking->Mobility==EComponentMobility::Static;
        // Only a surface facing the push stops it; a grazing contact lets the native movement slide on.
        if (!Heading.IsNearlyZero() && (bBody || bStatic)
            && FVector::DotProduct(FVector(Hit->Normal.X,Hit->Normal.Y,0).GetSafeNormal(),Heading)<=-.1)
        {
            KnockbackBlock.Emplace();
            KnockbackBlock->bBody=bBody;
            KnockbackBlock->Blocker=Blocker;
            bWatchKnockbackBlock=false;
        }
    }
    if (!NativeCapture || !NativeCapture->Error.IsEmpty()) { return Result; }
    auto& Capture=*NativeCapture;
    const FVector To=GetActorFeetLocation();
    const bool Moved=From!=To;
    if (!From.Equals(Capture.LastFoot,.000001) || From.ContainsNaN() || To.ContainsNaN())
    { Capture.Fail(TEXT("Native movement trace has a missing or nonfinite position.")); return Result; }
    const bool ArtificialStepMove=StepMove==1 || StepMove==3;
    const bool Adjustment=Capture.AdjustmentDepth>0 || ArtificialStepMove;
    const bool Timed=!Adjustment && ((Capture.SafeDepth>0 && (Capture.Walking || Capture.Falling)) || StepMove==2);
    if (!Timed && !Adjustment && Moved)
    { Capture.Fail(TEXT("Native movement has no witnessed time-bearing or adjustment scope.")); return Result; }
    const double Begin=Capture.Cursor;
    if (Timed)
    {
        if (!Hit || !FMath::IsFinite(Hit->Time) || Hit->Time<0 || Hit->Time>1 || Capture.SliceEnd<Capture.Cursor)
        { Capture.Fail(TEXT("Native sweep supplied no valid remaining-time fraction.")); return Result; }
        if (!Hit->bStartPenetrating)
        {
            Capture.Cursor=Hit->Time==1?Capture.SliceEnd
                : Capture.Cursor+(Capture.SliceEnd-Capture.Cursor)*double(Hit->Time);
        }
    }
    if (Moved)
    {
        FLDNativeMotionRecord Record;
        Record.Kind=Adjustment?ELDNativeMotionRecordKind::Adjustment:ELDNativeMotionRecordKind::Movement;
        Record.FromFootCm=From;
        Record.ToFootCm=To;
        Record.StartServerSeconds=Begin;
        Record.EndServerSeconds=Capture.Cursor;
        Record.RootMotion=HasRootMotionSources() || HasAnimRootMotion();
        Capture.Append(MoveTemp(Record));
    }
    Capture.LastFoot=To;
    return Result;
}

bool ULDCharacterMovementComponent::SafeMoveUpdatedComponent(const FVector& Delta, const FQuat& Rotation,
    bool Sweep, FHitResult& Hit, ETeleportType Teleport)
{
    if (!NativeCapture) { return Super::SafeMoveUpdatedComponent(Delta,Rotation,Sweep,Hit,Teleport); }
    TGuardValue<int32> Scope(NativeCapture->SafeDepth,NativeCapture->SafeDepth+1);
    return Super::SafeMoveUpdatedComponent(Delta,Rotation,Sweep,Hit,Teleport);
}

bool ULDCharacterMovementComponent::ResolvePenetrationImpl(const FVector& Adjustment, const FHitResult& Hit, const FQuat& Rotation)
{
    if (!NativeCapture) { return Super::ResolvePenetrationImpl(Adjustment,Hit,Rotation); }
    TGuardValue<int32> Scope(NativeCapture->AdjustmentDepth,NativeCapture->AdjustmentDepth+1);
    return Super::ResolvePenetrationImpl(Adjustment,Hit,Rotation);
}

void ULDCharacterMovementComponent::AdjustFloorHeight()
{
    if (!NativeCapture) { Super::AdjustFloorHeight(); return; }
    TGuardValue<int32> Scope(NativeCapture->AdjustmentDepth,NativeCapture->AdjustmentDepth+1);
    Super::AdjustFloorHeight();
}

void ULDCharacterMovementComponent::MoveAlongFloor(const FVector& InVelocity, float DeltaSeconds, FStepDownResult* OutStepDownResult)
{
    if (!NativeCapture) { Super::MoveAlongFloor(InVelocity,DeltaSeconds,OutStepDownResult); return; }
    auto& Capture=*NativeCapture;
    if (Capture.PendingRevert!=FNativeCapture::ERevert::None)
    {
        if (!Capture.WalkBookmark || Capture.PendingRevert!=FNativeCapture::ERevert::Retry)
        { Capture.Fail(TEXT("Native walking retried without the expected ledge rollback witness.")); }
        else { Capture.Restore(Capture.WalkBookmark.GetValue(),UpdatedComponent->GetComponentLocation(),SupportOf(*this)); }
        Capture.PendingRevert=FNativeCapture::ERevert::None;
    }
    Capture.WalkBookmark=Capture.Mark(UpdatedComponent->GetComponentLocation());
    const double NativeEnd=Capture.Cursor+double(DeltaSeconds)*Capture.NativeScale;
    if (NativeEnd>Capture.Batch.Step.EndServerSeconds+.0000001)
    { Capture.Fail(TEXT("Walking interval exceeds the current core step.")); }
    Capture.SliceEnd=FMath::Min(NativeEnd,Capture.Batch.Step.EndServerSeconds);
    Super::MoveAlongFloor(InVelocity,DeltaSeconds,OutStepDownResult);
    // Any unused native movement time is a stationary remainder, not a chord.
    Capture.Cursor=Capture.SliceEnd;
}

bool ULDCharacterMovementComponent::StepUp(const FVector& GravityVector, const FVector& Delta,
    const FHitResult& Hit, FStepDownResult* OutStepDownResult)
{
    if (!NativeCapture) { return Super::StepUp(GravityVector,Delta,Hit,OutStepDownResult); }
    auto& Capture=*NativeCapture;
    const auto Bookmark=Capture.Mark(UpdatedComponent->GetComponentLocation());
    TGuardValue<int32> Depth(Capture.StepDepth,Capture.StepDepth+1);
    TGuardValue<int32> MoveIndex(Capture.StepDirectMoves,0);
    const bool Result=Super::StepUp(GravityVector,Delta,Hit,OutStepDownResult);
    if (!Result) { Capture.Restore(Bookmark,UpdatedComponent->GetComponentLocation(),SupportOf(*this)); }
    return Result;
}

FVector ULDCharacterMovementComponent::GetLedgeMove(const FVector& OldLocation, const FVector& Delta,
    const FFindFloorResult& OldFloor) const
{
    const FVector Result=Super::GetLedgeMove(OldLocation,Delta,OldFloor);
    if (NativeCapture && NativeCapture->Walking && !Result.IsZero())
    { NativeCapture->PendingRevert=FNativeCapture::ERevert::Retry; }
    return Result;
}

bool ULDCharacterMovementComponent::CheckFall(const FFindFloorResult& OldFloor, const FHitResult& Hit,
    const FVector& Delta, const FVector& OldLocation, float RemainingTime, float TimeTick, int32 Iterations, bool MustJump)
{
    const bool Result=Super::CheckFall(OldFloor,Hit,Delta,OldLocation,RemainingTime,TimeTick,Iterations,MustJump);
    if (NativeCapture && NativeCapture->Walking && !Result)
    { NativeCapture->PendingRevert=FNativeCapture::ERevert::Failure; }
    return Result;
}

void ULDCharacterMovementComponent::PhysWalking(float DeltaSeconds, int32 Iterations)
{
    if (!NativeCapture) { Super::PhysWalking(DeltaSeconds,Iterations); return; }
    auto& Capture=*NativeCapture;
    if (Capture.Walking) { Capture.Fail(TEXT("Unclosed nested native walking interval.")); }
    TGuardValue<bool> Walking(Capture.Walking,true);
    Capture.Cursor=Capture.StartFor(DeltaSeconds);
    Capture.SliceEnd=Capture.Batch.Step.EndServerSeconds;
    Capture.WalkBookmark=Capture.Mark(UpdatedComponent->GetComponentLocation());
    Super::PhysWalking(DeltaSeconds,Iterations);
    if (Capture.PendingRevert!=FNativeCapture::ERevert::None)
    {
        if (!Capture.WalkBookmark || Capture.PendingRevert!=FNativeCapture::ERevert::Failure)
        { Capture.Fail(TEXT("Native walking ended before an expected rollback retry.")); }
        else { Capture.Restore(Capture.WalkBookmark.GetValue(),UpdatedComponent->GetComponentLocation(),SupportOf(*this)); }
        Capture.PendingRevert=FNativeCapture::ERevert::None;
    }
    Capture.Cursor=Capture.Batch.Step.EndServerSeconds;
    Capture.WalkBookmark.Reset();
    CaptureNativeSupport();
}

void ULDCharacterMovementComponent::PhysFalling(float DeltaSeconds, int32 Iterations)
{
    if (!NativeCapture) { Super::PhysFalling(DeltaSeconds,Iterations); return; }
    auto& Capture=*NativeCapture;
    TGuardValue<bool> Falling(Capture.Falling,true);
    Capture.Cursor=Capture.StartFor(DeltaSeconds);
    Capture.SliceEnd=Capture.Batch.Step.EndServerSeconds;
    Super::PhysFalling(DeltaSeconds,Iterations);
    Capture.Cursor=Capture.Batch.Step.EndServerSeconds;
    CaptureNativeSupport();
}

void ULDCharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
    Super::OnMovementModeChanged(PreviousMovementMode,PreviousCustomMode);
    CaptureNativeSupport();
}

void ULDCharacterMovementComponent::FindFloor(const FVector& CapsuleLocation, FFindFloorResult& OutFloorResult,
    bool UseCachedLocation, const FHitResult* DownwardSweepResult) const
{
    Super::FindFloor(CapsuleLocation,OutFloorResult,UseCachedLocation,DownwardSweepResult);
    if (&OutFloorResult==&CurrentFloor) { CaptureNativeSupport(); }
}

#if WITH_DEV_AUTOMATION_TESTS
bool ULDCharacterMovementComponent::BeginIsolatedNativeMotionProbe(float DeltaSeconds, FString& Error)
{
    if (!GetWorld() || GetWorld()->GetAuthGameMode() || !CharacterOwner || !CharacterOwner->HasAuthority()
        || UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName())==LDDemoEntry::WorldMap)
    { Error=TEXT("Native instrumentation is restricted to an isolated engine test world."); return false; }
    const FLDMatchContext Match{1,FGuid::NewGuid()};
    const FLDCombatBodyRef Body{{ELDVictimKind::Contender,FGuid::NewGuid(),1},FGuid::NewGuid()};
    const FLDAuthorityStep Step{Match,1,0,double(DeltaSeconds)};
    return BeginNativeMotionCapture(Step,Body,FGuid::NewGuid(),{TEXT("native_probe"),1,1},Error);
}

bool ULDCharacterMovementComponent::EndIsolatedNativeMotionProbe(TSharedPtr<const FLDNativeMotionBatch>& Out, FString& Error)
{ return FinishNativeMotionCapture(Out,Error); }
#endif
