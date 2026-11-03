#pragma once
#include "CoreMinimal.h"
#include "LDArtTypes.generated.h"

// Presentation states a body can play. Spawn is optional per body; every other state is required.
UENUM()
enum class ELDArtAnim : uint8 { Idle, Run, Attack, Cast, Hit, Death, Spawn, Count UMETA(Hidden) };

// Where a decoration may stand so that it never reads as an obstacle or hides a gameplay place (LDArt::CheckPlacements):
// Outside the arena floor, low along the inside of an outer wall, or as the thin landmark of the capture or shop anchor.
UENUM()
enum class ELDArtZone : uint8 { Outside, WallBase, Capture, Shop, Count UMETA(Hidden) };

namespace LDArt
{
    // Locked roster seats 0..5 (APP_SPEC: at most six contenders).
    constexpr int32 SeatCount = 6;
    constexpr int32 AnimCount = int32(ELDArtAnim::Count);
    // Literal ids instead of reflected enum names: non-editor builds do not preserve FName casing.
    LOCKDOWN_API FString AnimId(ELDArtAnim Anim);
    LOCKDOWN_API bool ParseAnimId(const FString& Id, ELDArtAnim& Out);
    LOCKDOWN_API FString ZoneId(ELDArtZone Zone);
    LOCKDOWN_API bool ParseZoneId(const FString& Id, ELDArtZone& Out);
    inline bool IsRequiredAnim(ELDArtAnim Anim) { return Anim != ELDArtAnim::Spawn && Anim != ELDArtAnim::Count; }
    inline bool IsLoopingAnim(ELDArtAnim Anim) { return Anim == ELDArtAnim::Idle || Anim == ELDArtAnim::Run; }
    // Attack and Cast carry the clip time at which the blow or the cast visibly lands.
    inline bool HasImpact(ELDArtAnim Anim) { return Anim == ELDArtAnim::Attack || Anim == ELDArtAnim::Cast; }
    // Catalog ids are lowercase ASCII words joined by underscores, compared byte-exact.
    LOCKDOWN_API bool IsValidId(const FString& Id);
}
