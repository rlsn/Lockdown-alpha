#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Art/LDArtTypes.h"
#include "LDArtCatalog.generated.h"

class UAnimSequence;
class USkeletalMesh;
class UStaticMesh;

USTRUCT()
struct LOCKDOWN_API FLDArtAnimation
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere) ELDArtAnim State = ELDArtAnim::Count;
    // Hard reference: the cooked catalog carries every clip. In place, no root motion.
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAnimSequence> Sequence;
    // Idle and Run loop; every other state plays once and holds its last pose.
    UPROPERTY(VisibleAnywhere) bool bLoop = false;
    // Authored rate that makes the clip read as intended (Hit ends within 0.3 s); callers may scale it further.
    UPROPERTY(VisibleAnywhere) float PlayRate = 1.f;
    // Attack and Cast only: clip time at rate 1 when the blow or the cast lands; zero otherwise.
    UPROPERTY(VisibleAnywhere) float ImpactSec = 0.f;
    // Provenance: the clip name in the source pack, and why it stands in for a missing exact clip (empty if exact).
    UPROPERTY(VisibleAnywhere) FString SourceClip;
    UPROPERTY(VisibleAnywhere) FString SubstituteNote;
};

USTRUCT()
struct LOCKDOWN_API FLDArtCharacter
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere) FString Id;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMesh> Mesh;
    // Mesh pose relative to the body capsule centre: the yaw that makes the model face the actor's +X, a uniform scale
    // to the displayed height and the feet on the capsule bottom. Display only; capsule and gameplay sizes are unchanged.
    UPROPERTY(VisibleAnywhere) FTransform MeshTransform;
    // Displayed height from the feet to the top of the head after MeshTransform, for overhead labels.
    UPROPERTY(VisibleAnywhere) float HeightCm = 0.f;
    // Material slots that take the identity tint through ULDArtCatalog::TintParameter: the seat palette for contenders,
    // the camp tier tint for NPCs. Every other slot keeps its imported colour.
    UPROPERTY(VisibleAnywhere) TArray<FName> TintSlots;
    // Bone in the right hand for hand-anchored effects.
    UPROPERTY(VisibleAnywhere) FName HandBone;
    // Ground speed (cm/s, after MeshTransform) at which Run at rate 1 shows no foot sliding: play rate = speed / this.
    UPROPERTY(VisibleAnywhere) float RunSpeedCmPerSec = 0.f;
    // One entry per state in ELDArtAnim order; Spawn may be absent.
    UPROPERTY(VisibleAnywhere) TArray<FLDArtAnimation> Animations;
    // Null when the state has no clip (only Spawn may be missing in a resolved catalog).
    const FLDArtAnimation* Find(ELDArtAnim State) const;
};

USTRUCT()
struct LOCKDOWN_API FLDArtSeatLook
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere) uint8 Seat = 0;
    UPROPERTY(VisibleAnywhere) FString Character;
};

USTRUCT()
struct LOCKDOWN_API FLDArtMonsterLook
{
    GENERATED_BODY()
    // ELDDemoMonsterId of the camp tier.
    UPROPERTY(VisibleAnywhere) uint8 CreatureId = 0;
    UPROPERTY(VisibleAnywhere) FString Character;
};

USTRUCT()
struct LOCKDOWN_API FLDArtProp
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere) FString Id;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMesh> Mesh;
};

USTRUCT()
struct LOCKDOWN_API FLDArtDecoration
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere) FString Prop;
    UPROPERTY(VisibleAnywhere) ELDArtZone Zone = ELDArtZone::Count;
    // Demo-map world transform (cm). Decorations never collide, block sight or affect navigation.
    UPROPERTY(VisibleAnywhere) FTransform Transform;
};

// Lookups into a resolved catalog; valid while that catalog is alive.
struct LOCKDOWN_API FLDArtResolved
{
    const FLDArtCharacter* Seats[LDArt::SeatCount] = {};
    TMap<uint8, const FLDArtCharacter*> Monsters;
    TArray<const FLDArtProp*> DecorationProps;
    TArray<FTransform> DecorationTransforms;
    // Null for a seat outside the roster or an unknown camp tier.
    const FLDArtCharacter* ForSeat(uint8 Seat) const;
    const FLDArtCharacter* ForMonster(uint8 CreatureId) const;
};

// Typed Demo art table imported from SourceData\Art\art_assets.json by Scripts\ImportDemoArt.ps1: the body of every
// seat and camp tier, their clips per presentation state, and the decoration props with their placements.
// The game resolves it once at startup; any missing entry is an explicit error, never a silent default.
UCLASS()
class LOCKDOWN_API ULDArtCatalog : public UDataAsset
{
    GENERATED_BODY()
public:
    static constexpr const TCHAR* AssetPath = TEXT("/Game/Lockdown/Art/DA_DemoArt.DA_DemoArt");
    // Vector parameters of every art material: the identity tint (tint slots) and an additive emissive hit flash.
    static constexpr const TCHAR* TintParameter = TEXT("Tint");
    static constexpr const TCHAR* FlashParameter = TEXT("Flash");
    UPROPERTY(VisibleAnywhere) TArray<FLDArtCharacter> Characters;
    // Exactly one entry per seat 0..5, in seat order.
    UPROPERTY(VisibleAnywhere) TArray<FLDArtSeatLook> Seats;
    // Exactly one entry per camp tier; NPC bodies differ from every seat body and from each other.
    UPROPERTY(VisibleAnywhere) TArray<FLDArtMonsterLook> Monsters;
    UPROPERTY(VisibleAnywhere) TArray<FLDArtProp> Props;
    UPROPERTY(VisibleAnywhere) TArray<FLDArtDecoration> Decorations;
    bool Resolve(FLDArtResolved& Out, FString& Error) const;
};
