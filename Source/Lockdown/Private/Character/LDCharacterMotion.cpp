#include "Character/LDCharacterMovementComponent.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Core/LDLog.h"
#include "Engine/World.h"
#include "Framework/LDDemoEntry.h"
#include "HAL/IConsoleManager.h"

namespace
{
    constexpr int32 NativeRecordCapacity=128;
#if !UE_BUILD_SHIPPING
    // DEV diagnosis: logs every write of the recorded foot and every walk bookmark beside the native position.
    int32 TraceRecordedFoot=0;
    FAutoConsoleVariableRef CVarTraceRecordedFoot(TEXT("ld.Motion.TraceRecordedFoot"),TraceRecordedFoot,
        TEXT("DEV: log every native-capture foot write with its source (0 off, 1 on)."));
#endif

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
    bool Walking=false, Falling=false, FallingHandoff=false, FallSteered=false;
    TOptional<FBookmark> WalkBookmark;
    ERevert PendingRevert=ERevert::None;
    FString Error;
    FString BodyName;
    float HalfHeight=0;

    void Fail(const TCHAR* Detail) { if (Error.IsEmpty()) { Error=Detail; } }

    void TraceFoot(const TCHAR* Source, const FVector& NativePosition) const
    {
#if !UE_BUILD_SHIPPING
        if (!TraceRecordedFoot) { return; }
        const FVector Native=NativePosition-FVector(0,0,HalfHeight);
        UE_LOG(LogLDMatch, Display, TEXT("LD_NATIVE_FOOT body=%s src=%s recorded=%s native=%s gap=%.6f safe=%d adjust=%d step=%d walking=%d falling=%d records=%d cursor=%.6f"),
            *BodyName,Source,*LastFoot.ToString(),*Native.ToString(),FVector::Dist2D(LastFoot,Native),SafeDepth,AdjustmentDepth,StepDepth,
            Walking?1:0,Falling?1:0,Batch.Records.Num(),Cursor);
#endif
    }

    bool Append(FLDNativeMotionRecord Record)
    {
        if (Batch.Records.Num()>=NativeRecordCapacity)
        { Fail(TEXT("Native motion record capacity exceeded.")); return false; }
        Batch.Records.Add(MoveTemp(Record));
        return true;
    }

    FBookmark Mark(FVector NativePosition) const
    { TraceFoot(TEXT("mark"),NativePosition); return {Batch.Records.Num(),LastFoot,NativePosition,LastSupport,Cursor,SliceEnd}; }

    void Restore(const FBookmark& Bookmark, FVector NativePosition, const FLDNativeSupportSample& Support)
    {
        if (!NativePosition.Equals(Bookmark.NativePosition,.000001) || !SameSupport(Support,Bookmark.Support))
        { Fail(TEXT("Native rollback did not restore its witnessed position and support.")); return; }
        Batch.Records.SetNum(Bookmark.Count,EAllowShrinking::No);
        LastFoot=Bookmark.Foot;
        TraceFoot(TEXT("restore"),NativePosition);
        LastSupport=Bookmark.Support;
        Cursor=Bookmark.Time;
        SliceEnd=Bookmark.SliceEnd;
        ++Batch.RevertedTrials;
    }

    // Inside CMC's own adjustment machinery (a SafeMove or a penetration resolution), a sub-centimetre unrecorded shift of
    // the body has been seen in real matches with no engine path we could witness. Such a shift is taken as a zero-time
    // adjustment record and reported; anything larger, or outside those scopes, stays an authority fault.
    static constexpr double MaxUnwitnessedAdjustmentCm=2.;
    void WitnessSmallAdjustment(const FVector& Foot, const TCHAR* Body)
    {
        if (!Error.IsEmpty() || (SafeDepth<=0 && AdjustmentDepth<=0) || Foot.ContainsNaN() || LastFoot.ContainsNaN()) { return; }
        const double Gap=FVector::Dist(Foot,LastFoot);
        if (Gap>MaxUnwitnessedAdjustmentCm) { return; }
        FLDNativeMotionRecord Record;
        Record.Kind=ELDNativeMotionRecordKind::Adjustment;
        Record.FromFootCm=LastFoot;
        Record.ToFootCm=Foot;
        Record.StartServerSeconds=Record.EndServerSeconds=Cursor;
        if (!Append(MoveTemp(Record))) { return; }
        UE_LOG(LogLDMatch, Warning, TEXT("LD_NATIVE_UNWITNESSED_ADJUSTMENT body=%s gap=%.6f safe=%d adjust=%d walking=%d"),Body,Gap,SafeDepth,
            AdjustmentDepth,Walking?1:0);
        LastFoot=Foot;
        TraceFoot(TEXT("witness"),Foot+FVector(0,0,HalfHeight));
    }

    // Once a walk has tried its ledge move and checked its fall, CMC's walking loop reverts a later ledge trial with
    // RevertMove (a SetWorldLocation) without asking either again, so no witness flags it. The body then stands exactly
    // on the walk bookmark; only that position is taken as the reverted trial, any other unrecorded move stays a fault.
    bool RestoreUnflaggedWalkRevert(const FVector& NativePosition, const FLDNativeSupportSample& Support)
    {
        if (!Walking || !WalkBookmark || PendingRevert!=ERevert::None || !Error.IsEmpty()
            || !NativePosition.Equals(WalkBookmark->NativePosition,.000001)) { return false; }
        Restore(WalkBookmark.GetValue(),NativePosition,Support);
        return Error.IsEmpty();
    }

    double StartFor(float RemainingSeconds)
    {
        if (!FMath::IsFinite(RemainingSeconds) || RemainingSeconds<0 || RemainingSeconds>NativeDuration)
        { Fail(TEXT("Native physics supplied an out-of-step remaining duration.")); return Cursor; }
        const double Start=RemainingSeconds==NativeDuration?Batch.Step.StartServerSeconds
            : Batch.Step.EndServerSeconds-double(RemainingSeconds)*NativeScale;
        if (Cursor>Start+.0000001)
        {
            // CMC refunds the unused fraction of the last walking slice when walking off an edge.
            // Preserve its witnessed path, compressing only that slice into the native-consumed interval.
            // Every other backwards time request remains an authority fault.
            if (!FallingHandoff || !Walking || !WalkBookmark || Start<WalkBookmark->Time-.0000001 || Cursor<=WalkBookmark->Time)
            { Fail(*FString::Printf(TEXT("Native physics attempted to reuse already committed simulation time. cursor=%.9f start=%.9f remaining=%.9f native=%.9f walking=%d falling=%d handoff=%d bookmark=%.9f"),Cursor,Start,RemainingSeconds,NativeDuration,Walking,Falling,FallingHandoff,WalkBookmark?WalkBookmark->Time:-1.)); return Cursor; }
            const double Begin=WalkBookmark->Time, Scale=(FMath::Max(Start,Begin)-Begin)/(Cursor-Begin);
            for (int32 Index=WalkBookmark->Count; Index<Batch.Records.Num(); ++Index)
            {
                auto& Record=Batch.Records[Index];
                Record.StartServerSeconds=Begin+(Record.StartServerSeconds-Begin)*Scale;
                Record.EndServerSeconds=Begin+(Record.EndServerSeconds-Begin)*Scale;
            }
            Cursor=FMath::Max(Start,Begin);
            SliceEnd=Cursor;
        }
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
    Capture.BodyName=GetNameSafe(GetOwner());
    Capture.HalfHeight=CharacterOwner && CharacterOwner->GetCapsuleComponent()
        ? CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f;
    Capture.TraceFoot(TEXT("begin"),UpdatedComponent->GetComponentLocation());
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
    if (!Foot.Equals(Capture.LastFoot,.000001) && !Capture.RestoreUnflaggedWalkRevert(UpdatedComponent->GetComponentLocation(),SupportOf(*this)))
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
    if (NativeCapture && !From.Equals(NativeCapture->LastFoot,.000001))
    { NativeCapture->TraceFoot(TEXT("move-start-mismatch"),UpdatedComponent->GetComponentLocation()); }
    if (NativeCapture && !From.Equals(NativeCapture->LastFoot,.000001)
        && !NativeCapture->RestoreUnflaggedWalkRevert(UpdatedComponent->GetComponentLocation(),SupportOf(*this)))
    { NativeCapture->WitnessSmallAdjustment(From,*GetNameSafe(GetOwner())); }
    int32 StepMove=0;
    if (NativeCapture && NativeCapture->StepDepth && !NativeCapture->SafeDepth && !NativeCapture->AdjustmentDepth)
    { StepMove=++NativeCapture->StepDirectMoves; }
    // #97: bodies block each other, but a dash runs through them (its path slow hits the bodies it crosses); the capsule
    // ignores other bodies only for the dash's own sweeps, so everyone else still bumps into the dashing body.
    // Issue #329 §4.1: innate dashes without card_pass_through clamp their root-motion distance to 135cm before an engaged
    // enemy in the 220cm +-30deg forward cone (LDCombatPreInput::ClampInnateDashDistance), while keeping sweep pass-through.
    // #198 F: a forced push (knockback) is blocked by bodies; C02 reads its first real block.
    const bool bPushing=IsDashing()&&bKnockbackMotion;
    const bool bGhost=GetWorld() && GetWorld()->GetTimeSeconds()<BodyPassThroughUntilSeconds;
    const bool bThroughBodies=Sweep&&UpdatedPrimitive&&!bPushing&&(IsDashing()||bGhost);
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
            KnockbackBlock->Direction=Heading;
            bWatchKnockbackBlock=false;
        }
    }
    if (!NativeCapture || !NativeCapture->Error.IsEmpty()) { return Result; }
    auto& Capture=*NativeCapture;
    const FVector To=GetActorFeetLocation();
    const bool Moved=From!=To;
    if (!From.Equals(Capture.LastFoot,.000001) || From.ContainsNaN() || To.ContainsNaN())
    {
        const UObject* Base=GetMovementBaseObject();
        Capture.Fail(*FString::Printf(TEXT("Native movement trace has a missing or nonfinite position. body=%s from=%s last=%s to=%s gap=%.6f mode=%d base=%s step=%d safe=%d adjust=%d"),
            *GetNameSafe(GetOwner()),*From.ToString(),*Capture.LastFoot.ToString(),*To.ToString(),FVector::Dist(From,Capture.LastFoot),
            int32(MovementMode.GetValue()),*GetNameSafe(Base),Capture.StepDepth,Capture.SafeDepth,Capture.AdjustmentDepth));
        return Result;
    }
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
    Capture.TraceFoot(Adjustment?TEXT("move-adjust"):Timed?TEXT("move-timed"):TEXT("move"),UpdatedComponent->GetComponentLocation());
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

void ULDCharacterMovementComponent::StartFalling(int32 Iterations, float RemainingTime, float TimeTick,
    const FVector& Delta, const FVector& SubLocation)
{
    if (!NativeCapture) { Super::StartFalling(Iterations,RemainingTime,TimeTick,Delta,SubLocation); return; }
    TGuardValue<bool> Handoff(NativeCapture->FallingHandoff,true);
    Super::StartFalling(Iterations,RemainingTime,TimeTick,Delta,SubLocation);
}

void ULDCharacterMovementComponent::PhysWalking(float DeltaSeconds, int32 Iterations)
{
    if (!NativeCapture) { Super::PhysWalking(DeltaSeconds,Iterations); return; }
    auto& Capture=*NativeCapture;
    if (Capture.Walking) { Capture.Fail(TEXT("Unclosed nested native walking interval.")); }
    TGuardValue<bool> Walking(Capture.Walking,true);
    TGuardValue<bool> Falling(Capture.Falling,false);
    Capture.Cursor=Capture.StartFor(DeltaSeconds);
    Capture.SliceEnd=Capture.Batch.Step.EndServerSeconds;
    Capture.WalkBookmark=Capture.Mark(UpdatedComponent->GetComponentLocation());
    Super::PhysWalking(DeltaSeconds,Iterations);
    if (Capture.PendingRevert!=FNativeCapture::ERevert::None)
    {
        // CMC reverts the trial before asking for a ledge retry. A retry whose delta is nearly zero ends the
        // walk without another MoveAlongFloor, leaving the body on the bookmark exactly as a failed fall check
        // does. Restore verifies that position and support; only a walk that left the ground stays a fault.
        if (!Capture.WalkBookmark || (Capture.PendingRevert==FNativeCapture::ERevert::Retry && !IsMovingOnGround()))
        { Capture.Fail(TEXT("Native walking left the ground before an expected rollback retry.")); }
        else { Capture.Restore(Capture.WalkBookmark.GetValue(),UpdatedComponent->GetComponentLocation(),SupportOf(*this)); }
        Capture.PendingRevert=FNativeCapture::ERevert::None;
    }
    // An unflagged ledge revert can be the walk's last act, with no later move to notice it: take it while the
    // bookmark is still held.
    if (!GetActorFeetLocation().Equals(Capture.LastFoot,.000001))
    { Capture.RestoreUnflaggedWalkRevert(UpdatedComponent->GetComponentLocation(),SupportOf(*this)); }
    Capture.Cursor=Capture.Batch.Step.EndServerSeconds;
    Capture.WalkBookmark.Reset();
    CaptureNativeSupport();
}

FVector ULDCharacterMovementComponent::NewFallVelocity(const FVector& InitialVelocity, const FVector& Gravity, float DeltaTime) const
{
    // After an impact CMC recomputes the fall velocity without air control, under a zeroed Acceleration, to
    // deflect the slide. That call is a probe inside the slice already open; it starts no new time.
    if (NativeCapture && NativeCapture->Falling && !(NativeCapture->FallSteered && Acceleration.IsZero()))
    {
        // PhysFalling substeps internally. Hit.Time is a fraction of this gravity-integrated slice,
        // not of all remaining frame time; a landing returns only the unconsumed native remainder.
        auto& Capture=*NativeCapture;
        const double End=Capture.Cursor+double(DeltaTime)*Capture.NativeScale;
        if (End>Capture.Batch.Step.EndServerSeconds+.0000001)
        {
            Capture.Fail(*FString::Printf(TEXT("Falling interval exceeds the current core step. body=%s cursor=%.9f end=%.9f delta=%.9f"),
                *GetNameSafe(GetOwner()),Capture.Cursor,Capture.Batch.Step.EndServerSeconds,double(DeltaTime)));
        }
        Capture.SliceEnd=FMath::Min(End,Capture.Batch.Step.EndServerSeconds);
    }
    return Super::NewFallVelocity(InitialVelocity,Gravity,DeltaTime);
}

void ULDCharacterMovementComponent::PhysFalling(float DeltaSeconds, int32 Iterations)
{
    if (!NativeCapture) { Super::PhysFalling(DeltaSeconds,Iterations); return; }
    auto& Capture=*NativeCapture;
    TGuardValue<bool> Falling(Capture.Falling,true);
    TGuardValue<bool> Steered(Capture.FallSteered,!Acceleration.IsZero());
    Capture.Cursor=Capture.StartFor(DeltaSeconds);
    TGuardValue<bool> Walking(Capture.Walking,false);
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
