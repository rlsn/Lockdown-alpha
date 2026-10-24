#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "LDCharacterBuild.generated.h"

UENUM()
enum class ELDSkill : uint8 { None, Step, Ambush, Spark, Patch, Pickpocket, Parley, Analyze, Survey, Heave, AnchorBash, BraceWall, VeilStep, Hamstring, ArcBolt, Counterpulse, FarwatchWard, WireSnare, LedgerMark, SuretyShell, FalseTrail, SmokeVeil, PurgeTouch, SeersFlare, EmberLine, Grapple, NullSeal, GroundingField, ForceWave, GuardSwap, Slipstream, RemedyBind, BreakSigil, ArcanePrison, TemporalLock, SovereignWard, GrandExchange, GateWeave };

// This record is private to its owning contender. It is never a remote-body property.
USTRUCT()
struct LOCKDOWN_API FLDCharacterBuild
{
    GENERATED_BODY()
    UPROPERTY() uint8 RulesVersion=6;
    UPROPERTY() FString CatalogDigest;
    UPROPERTY() uint8 ClassId = 0;
    UPROPERTY() TArray<int32> Attributes = {12,9,12,8,10,9}; // STR DEX CON INT WIS CHA
    UPROPERTY() int32 Level = 1;
    UPROPERTY() int32 AttributePoints = 0;
    UPROPERTY() int32 SkillPoints = 0;
    UPROPERTY() uint8 Subclass = 0;
    UPROPERTY() uint32 Revision = 1;
    UPROPERTY() TArray<uint8> Ranks;
    UPROPERTY() TArray<ELDSkill> Slots;
    UPROPERTY() TArray<double> Cooldowns;
    FLDCharacterBuild();
};

USTRUCT()
struct LOCKDOWN_API FLDBuildPlan
{
    GENERATED_BODY()
    UPROPERTY() uint8 RulesVersion=6;
    UPROPERTY() FString CatalogDigest;
    UPROPERTY() uint32 ExpectedRevision = 0;
    UPROPERTY() TArray<int32> Attributes;
    UPROPERTY() TArray<uint8> Ranks;
    UPROPERTY() TArray<ELDSkill> Slots;
    UPROPERTY() uint8 Subclass = 0;
    bool NetSerialize(FArchive& Ar,class UPackageMap* Map,bool& Success);
};
template<> struct TStructOpsTypeTraits<FLDBuildPlan> : TStructOpsTypeTraitsBase2<FLDBuildPlan>
{enum {WithNetSerializer=true};};

USTRUCT()
struct LOCKDOWN_API FLDOwnedBook
{
    GENERATED_BODY()
    UPROPERTY() FGuid Id;
    UPROPERTY() ELDSkill Skill=ELDSkill::None;
};

USTRUCT()
struct LOCKDOWN_API FLDClassDefinition
{
    GENERATED_BODY()
    UPROPERTY() FString Id;
    UPROPERTY() TArray<int32> Attributes;
    UPROPERTY() TArray<ELDSkill> StartingSkills;
    UPROPERTY() TArray<ELDSkill> ClassLearning;
};

// Saved by the editor bootstrap from the checked specification companion.
// Runtime never reaches into docs/ or depends on an editor-only JSON file.
UCLASS()
class LOCKDOWN_API ULDCharacterCatalog : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere) int32 SchemaVersion = 1;
    UPROPERTY(VisibleAnywhere) FString SourceChecksum;
    UPROPERTY(VisibleAnywhere) FString SourceJson;
    UPROPERTY(VisibleAnywhere) TArray<FLDClassDefinition> Classes;
    bool Import(const FString& Text, FString& Error);
    bool Validate(FString& Error) const;
    static ULDCharacterCatalog* Load();
};

namespace LDDevelopment
{
    constexpr int32 SkillCount = 10, SlotCount = 4, V07SkillCount=38;
    inline int32 Count(const FLDCharacterBuild& B){return B.RulesVersion==7?V07SkillCount:SkillCount;}
    inline int32 Slots(const FLDCharacterBuild& B){return B.RulesVersion==7?5:4;}
    inline bool Held(ELDSkill S){return uint8(S)>=4&&uint8(S)<=9;}
    inline bool Implemented(ELDSkill S){return uint8(S)<10||S==ELDSkill::AnchorBash||S==ELDSkill::ArcBolt||S==ELDSkill::PurgeTouch||S==ELDSkill::SuretyShell;}
    LOCKDOWN_API FLDCharacterBuild TacticalStart(uint8 ClassId,const FString& Digest);
    constexpr double DeadlineSeconds = 1500;
    enum EAttribute : uint8 { STR, DEX, CON, INT, WIS, CHA };
    LOCKDOWN_API const TCHAR* SkillName(ELDSkill Skill);
    LOCKDOWN_API ELDSkill SkillFromName(const FString& Name);
    LOCKDOWN_API const TCHAR* ClassName(uint8 ClassId);
    LOCKDOWN_API const TCHAR* SubclassName(uint8 Subclass);
    LOCKDOWN_API FLDCharacterBuild StartingBuild(uint8 ClassId, const ULDCharacterCatalog* Catalog = nullptr);
    LOCKDOWN_API bool Valid(const FLDCharacterBuild& Build);
    LOCKDOWN_API bool CanTrain(uint8 ClassId, ELDSkill Skill);
    LOCKDOWN_API int32 Rank(const FLDCharacterBuild& Build, ELDSkill Skill);
    LOCKDOWN_API bool Equipped(const FLDCharacterBuild& Build, ELDSkill Skill);
    LOCKDOWN_API double MaxHP(const FLDCharacterBuild& Build);
    LOCKDOWN_API double BasicDamage(const FLDCharacterBuild& Build);
    LOCKDOWN_API double SpeedFactor(const FLDCharacterBuild& Build);
    LOCKDOWN_API double SkillValue(const FLDCharacterBuild& Build, ELDSkill Skill);
    LOCKDOWN_API double Cooldown(ELDSkill Skill);
    LOCKDOWN_API double ChannelSeconds(ELDSkill Skill);
    LOCKDOWN_API int32 LevelForXP(int64 XP);
    LOCKDOWN_API void AwardXP(FLDCharacterBuild& Build, int64& XP, int64 Amount);
    LOCKDOWN_API double DeathDelay(int32 Level, double Elapsed);
    LOCKDOWN_API int64 DeathLoss(int64 Gold);
    LOCKDOWN_API FLDBuildPlan PlanFrom(const FLDCharacterBuild& Build);
    LOCKDOWN_API bool Prepare(const FLDCharacterBuild& Current, const FLDBuildPlan& Plan, FLDCharacterBuild& Result, FString& Error);
    LOCKDOWN_API bool TheftSucceeds(int32 Roll, int32 DEXValue, int32 Rank, bool Cutpurse, int32 TargetWIS, bool Alert);
}
