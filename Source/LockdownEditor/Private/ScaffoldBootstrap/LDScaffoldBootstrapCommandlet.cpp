#include "LDScaffoldBootstrapCommandlet.h"
#include "Core/LDPrototypeTuning.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerStart.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Misc/SecureHash.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
    constexpr TCHAR GeneratedRoot[] = TEXT("/Game/Lockdown/Generated/");
    constexpr TCHAR SignatureKey[] = TEXT("LockdownBootstrapSignature");
    const TArray<FString> MapNames = {TEXT("L_Frontend"), TEXT("L_MigrationSandbox"), TEXT("L_FunctionalTests")};

    bool Exists(const FString& Path) { return FPackageName::DoesPackageExist(Path); }

    void Stamp(UObject* Object, const FString& Signature)
    {
        Object->GetOutermost()->GetMetaData().SetValue(Object, SignatureKey, *Signature);
    }

    bool HasSignature(UObject* Object, const FString& Signature)
    {
        return Object && Object->GetOutermost()->GetMetaData().GetValue(Object, SignatureKey) == Signature;
    }

    bool SaveAsset(UObject* Object, const FString& Path, const FString& Signature)
    {
        Stamp(Object, Signature);
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        Args.SaveFlags = SAVE_NoError;
        const FString File = FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension());
        if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true)) { return false; }
        return UPackage::SavePackage(Object->GetOutermost(), Object, *File, Args);
    }

    bool ReadVector(const TSharedPtr<FJsonObject>& Json, const TCHAR* Key, FVector& Result)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!Json->TryGetArrayField(Key, Values) || Values->Num() != 3) { return false; }
        double X, Y, Z;
        if (!(*Values)[0]->TryGetNumber(X) || !(*Values)[1]->TryGetNumber(Y) || !(*Values)[2]->TryGetNumber(Z)) { return false; }
        Result = FVector(X, Y, Z);
        return !Result.ContainsNaN();
    }
}

ULDScaffoldBootstrapCommandlet::ULDScaffoldBootstrapCommandlet()
{
    IsClient = false;
    IsEditor = true;
    LogToConsole = true;
}

int32 ULDScaffoldBootstrapCommandlet::Main(const FString& Params)
{
    const bool bValidate = FParse::Param(*Params, TEXT("Validate"));
    const bool bRegenerate = FParse::Param(*Params, TEXT("Regenerate"));
    if (bValidate && bRegenerate) { UE_LOG(LogTemp, Error, TEXT("Choose Validate or Regenerate, not both")); return 1; }
    FString LayoutText;
    TSharedPtr<FJsonObject> Layout;
    if (!FFileHelper::LoadFileToString(LayoutText, *(FPaths::ProjectConfigDir() / TEXT("BootstrapLayout.json")))
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(LayoutText), Layout) || !Layout.IsValid()) { return 2; }
    const TArray<TSharedPtr<FJsonValue>>* Geometry = nullptr;
    double Schema = 0;
    FString Version;
    if (!Layout->TryGetNumberField(TEXT("schemaVersion"), Schema) || Schema != 1
        || !Layout->TryGetStringField(TEXT("generatorVersion"), Version) || !Layout->TryGetArrayField(TEXT("geometry"), Geometry)) { return 2; }
    // Source hash records authoring changes; validate additionally checks saved geometry.
    const FString Signature = Version + TEXT(":") + FMD5::HashAnsiString(*LayoutText);
    for (const TSharedPtr<FJsonValue>& Value : *Geometry)
    {
        const TSharedPtr<FJsonObject> Item = Value->AsObject();
        FString Label, Mesh;
        FVector Location, Size;
        if (!Item || !Item->TryGetStringField(TEXT("label"), Label) || Label.IsEmpty()
            || !Item->TryGetStringField(TEXT("mesh"), Mesh) || (Mesh != TEXT("Cube") && Mesh != TEXT("Cylinder"))
            || !ReadVector(Item, TEXT("location"), Location) || !ReadVector(Item, TEXT("size"), Size)
            || Size.GetMin() <= 0) { UE_LOG(LogTemp, Error, TEXT("Invalid bootstrap geometry")); return 2; }
    }

    const FString TuningPath = FString(GeneratedRoot) + TEXT("Data/DA_PrototypeTuning");
    if (bRegenerate || (!bValidate && !Exists(TuningPath)))
    {
        UPackage* Package = CreatePackage(*TuningPath);
        ULDPrototypeTuning* Tuning = NewObject<ULDPrototypeTuning>(Package, TEXT("DA_PrototypeTuning"), RF_Public | RF_Standalone);
        if (!SaveAsset(Tuning, TuningPath, Signature)) { return 3; }
    }
    ULDPrototypeTuning* Tuning = LoadObject<ULDPrototypeTuning>(nullptr, *(TuningPath + TEXT(".DA_PrototypeTuning")));
    FString TuningError;
    if (!HasSignature(Tuning, Signature) || !Tuning->Validate(TuningError)) { UE_LOG(LogTemp, Error, TEXT("Missing/stale tuning: %s"), *TuningError); return 3; }

    UInputMappingContext* Context = nullptr;
    const FString ContextPath = FString(GeneratedRoot) + TEXT("Input/IMC_Gameplay");
    const bool bWriteContext = bRegenerate || (!bValidate && !Exists(ContextPath));
    if (bWriteContext) { Context = NewObject<UInputMappingContext>(CreatePackage(*ContextPath), TEXT("IMC_Gameplay"), RF_Public | RF_Standalone); }
    else { Context = LoadObject<UInputMappingContext>(nullptr, *(ContextPath + TEXT(".IMC_Gameplay"))); }
    if (!Context) { UE_LOG(LogTemp, Error, TEXT("Missing input context")); return 4; }
    struct FInputSpec { const TCHAR* Name; EInputActionValueType Type; FKey Key; };
    const TArray<FInputSpec> Inputs = {
        {TEXT("Move"), EInputActionValueType::Axis2D, EKeys::Invalid},
        {TEXT("Aim"), EInputActionValueType::Axis2D, EKeys::Invalid},
        {TEXT("Sprint"), EInputActionValueType::Boolean, EKeys::LeftShift},
        {TEXT("Attack"), EInputActionValueType::Boolean, EKeys::LeftMouseButton},
        {TEXT("Dash"), EInputActionValueType::Boolean, EKeys::SpaceBar},
        {TEXT("Assassinate"), EInputActionValueType::Boolean, EKeys::Q},
        {TEXT("Interact"), EInputActionValueType::Boolean, EKeys::E},
        {TEXT("Ward"), EInputActionValueType::Boolean, EKeys::F},
        {TEXT("Help"), EInputActionValueType::Boolean, EKeys::H},
        {TEXT("Menu"), EInputActionValueType::Boolean, EKeys::Escape}
    };
    for (const FInputSpec& Spec : Inputs)
    {
        const FString Name = FString(TEXT("IA_")) + Spec.Name;
        const FString Path = FString(GeneratedRoot) + TEXT("Input/") + Name;
        UInputAction* Action = nullptr;
        if (bRegenerate || (!bValidate && !Exists(Path)))
        {
            Action = NewObject<UInputAction>(CreatePackage(*Path), *Name, RF_Public | RF_Standalone);
            Action->ValueType = Spec.Type;
            if (!SaveAsset(Action, Path, Signature)) { return 4; }
        }
        else { Action = LoadObject<UInputAction>(nullptr, *(Path + TEXT(".") + Name)); }
        if (!HasSignature(Action, Signature) || Action->ValueType != Spec.Type) { UE_LOG(LogTemp, Error, TEXT("Missing/stale input %s"), *Name); return 4; }
        if (bWriteContext && Spec.Key.IsValid()) { Context->MapKey(Action, Spec.Key); }
    }
    // Movement modifiers and cursor aiming are added with the B32 controller.
    if (bWriteContext && !SaveAsset(Context, ContextPath, Signature)) { return 4; }
    if (!HasSignature(Context, Signature)) { return 4; }

    for (const FString& Name : MapNames)
    {
        const FString Path = FString(GeneratedRoot) + TEXT("Maps/") + Name;
        UWorld* World = nullptr;
        if (bRegenerate || (!bValidate && !Exists(Path)))
        {
            World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
            if (!World) { return 5; }
            for (const TSharedPtr<FJsonValue>& Value : *Geometry)
            {
                const TSharedPtr<FJsonObject> Item = Value->AsObject();
                FVector Location, Size;
                ReadVector(Item, TEXT("location"), Location);
                ReadVector(Item, TEXT("size"), Size);
                double Pitch = 0;
                Item->TryGetNumberField(TEXT("pitch"), Pitch);
                AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Location, FRotator(Pitch, 0, 0));
                if (!Actor) { return 5; }
                Actor->SetActorLabel(Item->GetStringField(TEXT("label")));
                UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *(TEXT("/Engine/BasicShapes/") + Item->GetStringField(TEXT("mesh"))));
                if (!Mesh) { return 5; }
                Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
                Actor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
                Actor->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
                Actor->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block);
                if (Item->GetStringField(TEXT("label")) == TEXT("Ground") || Item->GetStringField(TEXT("label")) == TEXT("Ramp") || Item->GetStringField(TEXT("label")) == TEXT("Plateau"))
                {
                    Actor->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
                    Actor->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore);
                }
                Actor->SetActorScale3D(Size / 100.0);
            }
            APlayerStart* Start = World->SpawnActor<APlayerStart>(FVector(-400, 0, 100), FRotator::ZeroRotator);
            ADirectionalLight* Light = World->SpawnActor<ADirectionalLight>(FVector(0, 0, 1000), FRotator(-60, -35, 0));
            if (!Start || !Light) { return 5; }
            Start->SetActorLabel(TEXT("Spawn_Initial"));
            Light->SetActorLabel(TEXT("Sun"));
            Light->GetLightComponent()->SetIntensity(5.0f);
            Stamp(World, Signature);
            const FString MapFile = FPackageName::LongPackageNameToFilename(Path, FPackageName::GetMapPackageExtension());
            if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(MapFile), true)) { return 5; }
            if (!UEditorLoadingAndSavingUtils::SaveMap(World, Path)) { return 5; }
        }
        else { World = LoadObject<UWorld>(nullptr, *(Path + TEXT(".") + Name)); }
        if (!HasSignature(World, Signature)) { UE_LOG(LogTemp, Error, TEXT("Missing/stale map %s"), *Path); return 5; }
        int32 FoundCount = 0;
        for (const TSharedPtr<FJsonValue>& Value : *Geometry)
        {
            const TSharedPtr<FJsonObject> Item = Value->AsObject();
            FVector Location, Size;
            ReadVector(Item, TEXT("location"), Location);
            ReadVector(Item, TEXT("size"), Size);
            double Pitch = 0;
            Item->TryGetNumberField(TEXT("pitch"), Pitch);
            int32 Matches = 0;
            for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
            {
                if (It->GetActorLabel() != Item->GetStringField(TEXT("label"))) { continue; }
                const UStaticMesh* Mesh = It->GetStaticMeshComponent()->GetStaticMesh();
                if (!It->GetActorLocation().Equals(Location, 0.1) || !It->GetActorScale3D().Equals(Size / 100.0, 0.001)
                    || !It->GetActorRotation().Equals(FRotator(Pitch, 0, 0), 0.01) || !Mesh || Mesh->GetName() != Item->GetStringField(TEXT("mesh"))) { return 5; }
                ++Matches;
            }
            if (Matches != 1) { UE_LOG(LogTemp, Error, TEXT("Missing or duplicated generated geometry")); return 5; }
            FoundCount += Matches;
        }
        UE_LOG(LogTemp, Display, TEXT("Validated %s: %d generated meshes"), *Path, FoundCount);
    }
    UE_LOG(LogTemp, Display, TEXT("LD_BOOTSTRAP_SUCCESS %s (B31 foundation only; gameplay/UI not generated)"), *Signature);
    return 0;
}
