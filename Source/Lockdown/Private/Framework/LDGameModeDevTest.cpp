#include "Framework/LDGameMode.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Combat/LDCombatDirector.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Framework/LDPlayerController.h"
#include "Navigation/LDNavigationComponent.h"
#include "World/LDNativeGround.h"
#include "Engine/World.h"

void ALDGameMode::EnableRuntimeDevTest()
{
    if (!HasAuthority() || !IsDemoRoom() || bRuntimeDevTest) { return; }
    bRuntimeDevTest = true;
    PublishLobby();
}

bool ALDGameMode::DevSetPeaceful(const FGuid& Contender, bool bEnabled, const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None || !FindContender(Contender))
    { Error = TEXT("和平模式需要当前参赛者与有效对局"); return false; }
    if (bEnabled) { DevPeaceful.Add(Contender); }
    else { DevPeaceful.Remove(Contender); }
    if (auto* PC = FindContenderController(Contender)) { PC->bDevPeaceful = bEnabled; PC->ForceNetUpdate(); }
    Error = bEnabled ? TEXT("和平模式已开启") : TEXT("和平模式已关闭");
    return true;
}

bool ALDGameMode::DevSkipPreparation(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None)
    { Error = TEXT("跳过准备需要有效对局"); return false; }
    TArray<FLDTimelineEvent> Events;
    if (!Timeline.DevFinishPreparation(Permit.GetBoundary().AtMs, Events, Error)) { return false; }
    RestSignups.Reset();
    LockedEntrants.Reset();
    if (!ApplyTimelineEvents(Events, Permit, Error)) { return false; }
    PublishTimeline();
    Error = TEXT("准备已跳过，正在进入场景");
    return true;
}

bool ALDGameMode::DevTeleport(const FGuid& Contender, const FVector& CameraPosition,
    const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Life = Lives.Find(Contender);
    ALDCharacter* Body = Life ? Life->Actor.Get() : nullptr;
    if (ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None || !Life
        || Life->Phase != ELDLifePhase::Alive || !Body || CameraPosition.ContainsNaN())
    { Error = TEXT("只有存活角色可以传送"); return false; }
    auto* Movement = Cast<ULDCharacterMovementComponent>(Body->GetCharacterMovement());
    if (!Movement || Movement->IsDashing()) { Error = TEXT("请等待位移结束后再传送"); return false; }
    const auto* Capsule = Body->GetCapsuleComponent();
    const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
    const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), HalfHeight);
    auto Query = LDNativeGround::Query(*GetWorld());
    Query.AddIgnoredActor(Body);
    FHitResult Hit;
    // Native Pawn collision over the whole world. Static mesh components may use any object channel or mobility.
    const FVector Start = CameraPosition + FVector(0, 0, HalfHeight);
    const FVector End(CameraPosition.X, CameraPosition.Y, -HALF_WORLD_MAX);
    if (!GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Pawn, Shape, Query)
        || Hit.bStartPenetrating || !Hit.GetComponent()
        || (!Hit.GetComponent()->IsA<UStaticMeshComponent>() && !LDNativeGround::IsSupport(*Hit.GetComponent()))
        || !Movement->IsWalkable(Hit))
    { Error = TEXT("镜头下方没有可站立的静态表面"); return false; }
    const float FloorGap = (UCharacterMovementComponent::MIN_FLOOR_DIST + UCharacterMovementComponent::MAX_FLOOR_DIST) * .5f;
    const FVector Origin = Hit.Location + FVector(0, 0, FloorGap);
    if (GetWorld()->OverlapBlockingTestByChannel(Origin, FQuat::Identity, ECC_Pawn, Shape, Query))
    { Error = TEXT("落点空间不足，无法放下角色"); return false; }
    if (!Body->TeleportTo(Origin, Body->GetActorRotation(), false, true))
    { Error = TEXT("传送失败，角色仍在原位置"); return false; }
    if (auto* Controller = FindContenderController(Contender))
    {
        Controller->Navigation->EndLifeOrders(ELDOrderCancelReason::Stop);
    }
    Movement->StopMovementImmediately();
    Movement->SetMovementMode(MOVE_Walking);
    Movement->FindFloor(Origin, Movement->CurrentFloor, false);
    Movement->SetBaseFromFloor(Movement->CurrentFloor);
    // Keep the current life's native sequence and retained acknowledgements. Only the discontinuity's pose changes.
    Life->MotionLastFoot = Movement->GetActorFeetLocation();
    Life->MotionGrounded = Movement->CurrentFloor.IsWalkableFloor();
    Life->MotionFloor = Movement->CurrentFloor.HitResult.GetComponent();
    // Teleport is a discontinuity: update presence at this boundary, never sweep through intermediate circles.
    if (auto* Track = CaptureTracks.Find(Contender); Track && CaptureReducer.IsActive())
    {
        const bool Inside = CaptureCircle.Contains(Life->MotionLastFoot, {Life->MotionGrounded, Life->MotionFloor});
        if (Track->bInside != Inside)
        {
            FLDCaptureEvent Fact;
            Fact.Kind = ELDCaptureEventKind::Presence;
            Fact.Body = Life->Body;
            Fact.Phase = ELDCommitPhase::Transactions;
            Fact.bInside = Inside;
            Fact.Time.Seconds = AuthorityStep.EndServerSeconds;
            Fact.Time.BucketMs = Permit.GetBoundary().AtMs;
            if (!CaptureReducer.Enqueue(Fact, Error) || (Combat && !Combat->NotifyFinalPresence(Fact, Error)))
            { FaultAuthority(Error); return false; }
            Track->bInside = Inside;
        }
    }
    Body->ForceNetUpdate();
    SyncOwner(FindContenderController(Contender));
    Error = TEXT("已传送到镜头下方的静态表面");
    return true;
}
#endif
