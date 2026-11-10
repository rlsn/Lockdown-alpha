#pragma once

#include "CoreMinimal.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Engine/DataAsset.h"
#include "UObject/ObjectKey.h"
#include "LDDemoContentCatalog.generated.h"

USTRUCT()
struct LOCKDOWN_API FLDDemoContentBundle
{
    GENERATED_BODY()

    UPROPERTY() TArray<uint8> Manifest;
    UPROPERTY() TArray<uint8> Content;
    UPROPERTY() TArray<uint8> World;
    UPROPERTY() TArray<uint8> ContentContract;
    UPROPERTY() TArray<uint8> WorldContract;
    UPROPERTY() TArray<uint8> AuthorSchema;
    UPROPERTY() TArray<uint8> NativeIds;
    UPROPERTY() TArray<uint8> NativeContract;
    UPROPERTY() TArray<uint8> WorldSource;
    UPROPERTY() TArray<uint8> WorldAuthorSchema;

    static bool ReadImportFiles(const FString& ProjectRoot, FLDDemoContentBundle& Out, FString& Error);
};

class LOCKDOWN_API FLDDemoContentSnapshot
{
public:
    FLDDemoContentSnapshot(const FLDDemoContentSnapshot&) = delete;
    FLDDemoContentSnapshot& operator=(const FLDDemoContentSnapshot&) = delete;
    const FLDDemoContentDefinitions& Get() const { return Definitions; }
    bool FindAbility(const FString& Id, const FLDDemoAbilityDefinition*& Out, FString& Error) const;
    bool FindStat(const FString& Id, const FLDDemoStatDefinition*& Out, FString& Error) const;
    // #198 B: a workshop component definition by canonical id.
    bool FindComponent(const FString& Id, const FLDDemoComponentDefinition*& Out, FString& Error) const;
    bool FindMonster(const FString& Id, const FLDDemoMonsterDefinition*& Out, FString& Error) const;
    bool FindDisplay(const FString& Id, const FLDDemoDisplayDefinition*& Out, FString& Error) const;
    // #198 skill growth: Ability's value set as its owner at Level uses it (LDSkillGrowth::GrowValues with SKILL.GrowthPerLevel).
    // Null for an ability outside this snapshot or a level outside 1..MaxLevel. Stable for the snapshot's life.
    const FLDDemoAbilityValues* FindValues(const FLDDemoAbilityDefinition& Ability, uint8 Level) const;

private:
    friend class ULDDemoContentCatalog;
    explicit FLDDemoContentSnapshot(FLDDemoContentDefinitions&& In);
    FLDDemoContentDefinitions Definitions;
    // Per ability (index into Definitions.Abilities): its value set grown for each level 1..MaxLevel; unset where growth
    // would overflow, which content validation refuses.
    TArray<TArray<TOptional<FLDDemoAbilityValues>>> GrownValues;
};

class LOCKDOWN_API FLDDemoPreparedContent
{
public:
    ~FLDDemoPreparedContent() = default;
    FLDDemoPreparedContent(const FLDDemoPreparedContent&) = delete;
    FLDDemoPreparedContent& operator=(const FLDDemoPreparedContent&) = delete;

private:
    friend class ULDDemoContentCatalog;
    FLDDemoPreparedContent() = default;
    FObjectKey Target;
    uint64 TargetGeneration = 0;
    FString ImportRoot;
    FLDDemoContentBundle Bundle;
    FLDDemoContentDefinitions Definitions;
};

UCLASS()
class LOCKDOWN_API ULDDemoContentCatalog : public UDataAsset
{
    GENERATED_BODY()

public:
    static const TCHAR* AssetPath();
    static bool PrepareImport(const ULDDemoContentCatalog& Target, const FLDDemoContentBundle& Bundle, const FLDDemoContentDigest& ExpectedRoot,
        TUniquePtr<FLDDemoPreparedContent>& Out, FString& Error);
    static bool PrepareImportFromFiles(const ULDDemoContentCatalog& Target, const FString& ProjectRoot, const FLDDemoContentDigest& ExpectedRoot,
        TUniquePtr<FLDDemoPreparedContent>& Out, FString& Error);
    bool CommitImport(TUniquePtr<FLDDemoPreparedContent>& Prepared, const FLDDemoContentDigest& CurrentExpectedRoot, FString& Error);
    bool ValidatePrepared(const FLDDemoPreparedContent& Expected, FString& Error) const;
    bool Validate(const FLDDemoContentDigest& ExpectedRoot, FString& Error) const;
    bool Read(const FLDDemoContentDigest& ExpectedRoot,
        TSharedPtr<const FLDDemoContentSnapshot>& Out, FString& Error) const;
    bool ReadRuntimeFiles(const FString& DataDirectory, const FLDDemoContentDigest& ExpectedRoot,
        TSharedPtr<const FLDDemoContentSnapshot>& Out, FString& Error) const;
    static bool Load(const FLDDemoContentDigest& ExpectedRoot,
        TSharedPtr<const FLDDemoContentSnapshot>& Out, FString& Error);
#if WITH_DEV_AUTOMATION_TESTS
    // DEV content hot reload (LDDemoEntry::IsDevContentReload). This asset must still match the compiled root; the
    // current Sync outputs under ProjectRoot are then projected with the full native importer against DiskRoot, the
    // hash of manifest.json on disk. Only the content payload, its display text and the root may differ from this
    // asset: world bytes and sources, schemas, native ids and contracts must be unchanged (SameDevReloadBase).
    bool ReadDevReloadFiles(const FString& ProjectRoot, const FLDDemoContentDigest& CompiledRoot, const FLDDemoContentDigest& DiskRoot,
        TSharedPtr<const FLDDemoContentSnapshot>& Out, FString& Error) const;
    static bool LoadDevReload(const FLDDemoContentDigest& CompiledRoot, const FLDDemoContentDigest& DiskRoot,
        TSharedPtr<const FLDDemoContentSnapshot>& Out, FString& Error);
    static bool SameDevReloadBase(const FLDDemoContentIdentity& Compiled, const FLDDemoContentIdentity& Reloaded, FString& Error);
#endif

private:
    uint64 ImportGeneration = 0;
    UPROPERTY() FLDDemoContentBundle ImportedBytes;
    UPROPERTY() FLDDemoContentDefinitions Definitions;
};
