#include "LDScaffoldBootstrapCommandlet.h"
#include "LDRegionBootstrap.h"
#include "Core/LDPrototypeTuning.h"
#include "Development/LDCharacterBuild.h"
#include "Combat/LDTacticalCatalog.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Inventory/LDAcquisitionCatalog.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerStart.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"
#include "InputModifiers.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Engine/Texture2D.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationSystem.h"
#include "Builders/CubeBuilder.h"
#include "Components/BrushComponent.h"
#include "Model.h"
#include "UI/LDUIRoot.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
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

    UPackage* EditablePackage(const FString& Path)
    {
        UPackage* Package = CreatePackage(*Path);
        Package->FullyLoad();
        return Package;
    }

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
        Args.SaveFlags = SAVE_None;
        const FString File = FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension());
        if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true)) { return false; }
        const bool bSaved = UPackage::SavePackage(Object->GetOutermost(), Object, *File, Args);
        if (!bSaved) { UE_LOG(LogTemp,Error,TEXT("Package save failed: %s"),*Path); }
        return bSaved;
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
    if (bValidate && (bRegenerate || FParse::Param(*Params,TEXT("RebuildRegionMaps")))) { UE_LOG(LogTemp, Error, TEXT("Validate cannot regenerate assets or rebuild region maps")); return 1; }
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

    const TArray<TPair<FString,FLinearColor>> Materials = {
        {TEXT("M_Character"),FLinearColor(.2f,.35f,.32f)}, {TEXT("M_Ground"),FLinearColor(.13f,.17f,.15f)},
        {TEXT("M_Stone"),FLinearColor(.4f,.38f,.3f)}, {TEXT("M_Tree"),FLinearColor(.12f,.22f,.15f)}
    };
    for (const auto& Entry : Materials)
    {
        const FString Path = FString(GeneratedRoot) + TEXT("Materials/") + Entry.Key;
        if (bRegenerate || (!bValidate && !Exists(Path)))
        {
            UPackage* Package = EditablePackage(Path);
            UMaterial* Material = FindObject<UMaterial>(Package,*Entry.Key);
            if (!Material) { Material = NewObject<UMaterial>(Package, *Entry.Key, RF_Public | RF_Standalone); }
            Material->GetExpressionCollection() = FMaterialExpressionCollection();
            UMaterialExpressionVectorParameter* Tint = NewObject<UMaterialExpressionVectorParameter>(Material);
            Tint->ParameterName = TEXT("Tint");
            Tint->DefaultValue = Entry.Value;
            Material->GetExpressionCollection().AddExpression(Tint);
            Material->GetExpressionInputForProperty(MP_BaseColor)->Expression = Tint;
            UMaterialExpressionConstant* Roughness = NewObject<UMaterialExpressionConstant>(Material);
            Roughness->R = .85f;
            Material->GetExpressionCollection().AddExpression(Roughness);
            Material->GetExpressionInputForProperty(MP_Roughness)->Expression = Roughness;
            Material->PostEditChange();
            if (!SaveAsset(Material,Path,Signature)) { return 3; }
        }
        if (!HasSignature(LoadObject<UMaterial>(nullptr, *(Path + TEXT(".") + Entry.Key)), Signature)) { UE_LOG(LogTemp,Error,TEXT("Missing/stale material %s"),*Path); return 3; }
    }
    const FString TexturePath=FString(GeneratedRoot)+TEXT("Materials/T_FogClosed");
    if (bRegenerate || (!bValidate && !Exists(TexturePath)))
    {
        UPackage* Package=EditablePackage(TexturePath);
        UTexture2D* Texture=FindObject<UTexture2D>(Package,TEXT("T_FogClosed"));
        if (!Texture) { Texture=NewObject<UTexture2D>(Package,TEXT("T_FogClosed"),RF_Public|RF_Standalone); }
        TArray<FColor> Pixels; Pixels.Init(FColor::Black,60*40);
        Texture->Source.Init(60,40,1,1,TSF_BGRA8,reinterpret_cast<const uint8*>(Pixels.GetData()));
        Texture->SRGB=false; Texture->CompressionSettings=TC_VectorDisplacementmap; Texture->Filter=TF_Nearest;
        Texture->MipGenSettings=TMGS_NoMipmaps; Texture->AddressX=TA_Clamp; Texture->AddressY=TA_Clamp;
        Texture->PostEditChange();
        if (!SaveAsset(Texture,TexturePath,Signature)) { return 3; }
    }
    UTexture2D* Closed=LoadObject<UTexture2D>(nullptr,*(TexturePath+TEXT(".T_FogClosed")));
    if (!HasSignature(Closed,Signature)) { return 3; }
    const FString FogPath=FString(GeneratedRoot)+TEXT("Materials/M_Fog");
    const FString FogSignature=Signature+TEXT(":b45-grid");
    if (bRegenerate || (!bValidate && (!Exists(FogPath)||!HasSignature(LoadObject<UMaterial>(nullptr,*(FogPath+TEXT(".M_Fog"))),FogSignature))))
    {
        UPackage* Package=EditablePackage(FogPath);
        UMaterial* Fog=FindObject<UMaterial>(Package,TEXT("M_Fog"));
        if (!Fog) { Fog=NewObject<UMaterial>(Package,TEXT("M_Fog"),RF_Public|RF_Standalone); }
        Fog->MaterialDomain=MD_PostProcess;
        Fog->GetExpressionCollection()=FMaterialExpressionCollection();
        auto Add=[Fog](UMaterialExpression* Expression){Fog->GetExpressionCollection().AddExpression(Expression);};
        auto* World=NewObject<UMaterialExpressionWorldPosition>(Fog); Add(World);
        auto* XY=NewObject<UMaterialExpressionComponentMask>(Fog); XY->R=true; XY->G=true; XY->Input.Expression=World; Add(XY);
        auto* Scale=NewObject<UMaterialExpressionVectorParameter>(Fog); Scale->ParameterName=TEXT("WorldScale"); Scale->DefaultValue=FLinearColor(1.f/4800,1.f/3200,0,0); Add(Scale);
        auto* ScaleXY=NewObject<UMaterialExpressionComponentMask>(Fog); ScaleXY->R=true; ScaleXY->G=true; ScaleXY->Input.Expression=Scale; Add(ScaleXY);
        auto* UV=NewObject<UMaterialExpressionMultiply>(Fog); UV->A.Expression=XY; UV->B.Expression=ScaleXY; Add(UV);
        auto* WorldOffset=NewObject<UMaterialExpressionVectorParameter>(Fog);WorldOffset->ParameterName=TEXT("WorldOffset");WorldOffset->DefaultValue=FLinearColor(.5f,.5f,0,0);Add(WorldOffset);
        auto* OffsetXY=NewObject<UMaterialExpressionComponentMask>(Fog);OffsetXY->R=true;OffsetXY->G=true;OffsetXY->Input.Expression=WorldOffset;Add(OffsetXY);
        auto* Offset=NewObject<UMaterialExpressionAdd>(Fog); Offset->A.Expression=UV;Offset->B.Expression=OffsetXY;Add(Offset);
        auto* Mask=NewObject<UMaterialExpressionTextureSampleParameter2D>(Fog); Mask->ParameterName=TEXT("FogMask"); Mask->Texture=Closed; Mask->SamplerType=SAMPLERTYPE_LinearColor; Mask->Coordinates.Expression=Offset; Add(Mask);
        auto* Red=NewObject<UMaterialExpressionComponentMask>(Fog); Red->R=true; Red->Input.Expression=Mask; Add(Red);
        auto* Factor=NewObject<UMaterialExpressionMultiply>(Fog); Factor->A.Expression=Red; Factor->ConstB=.96f; Add(Factor);
        auto* Floor=NewObject<UMaterialExpressionAdd>(Fog); Floor->A.Expression=Factor; Floor->ConstB=.04f; Add(Floor);
        auto* Scene=NewObject<UMaterialExpressionSceneTexture>(Fog); Scene->SceneTextureId=PPI_PostProcessInput0; Add(Scene);
        auto* Result=NewObject<UMaterialExpressionMultiply>(Fog); Result->A.Expression=Scene; Result->B.Expression=Floor; Add(Result);
        Fog->GetExpressionInputForProperty(MP_EmissiveColor)->Expression=Result;
        Fog->PostEditChange();
        if (!SaveAsset(Fog,FogPath,FogSignature)) { return 3; }
    }
    UMaterial* Fog=LoadObject<UMaterial>(nullptr,*(FogPath+TEXT(".M_Fog")));
    if (!HasSignature(Fog,FogSignature) || Fog->MaterialDomain!=MD_PostProcess) { return 3; }
    const FString WidgetPath = FString(GeneratedRoot) + TEXT("UI/WBP_UIRoot");
    if (bRegenerate || (!bValidate && !Exists(WidgetPath)))
    {
        UPackage* Package = EditablePackage(WidgetPath);
        UBlueprint* Blueprint = FindObject<UWidgetBlueprint>(Package,TEXT("WBP_UIRoot"));
        if (!Blueprint) { Blueprint = FKismetEditorUtilities::CreateBlueprint(ULDUIRoot::StaticClass(),Package,TEXT("WBP_UIRoot"),BPTYPE_Normal,UWidgetBlueprint::StaticClass(),UWidgetBlueprintGeneratedClass::StaticClass()); }
        if (!Blueprint) { return 3; }
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        if (!Blueprint->GeneratedClass || !SaveAsset(Blueprint,WidgetPath,Signature)) { return 3; }
    }
    UWidgetBlueprint* SavedWidget = LoadObject<UWidgetBlueprint>(nullptr, *(WidgetPath + TEXT(".WBP_UIRoot")));
    if (!HasSignature(SavedWidget, Signature))
    {
        UE_LOG(LogTemp,Error,TEXT("Widget signature mismatch: %s; actual '%s'; expected '%s'"),*GetPathNameSafe(SavedWidget),
            SavedWidget ? *SavedWidget->GetOutermost()->GetMetaData().GetValue(SavedWidget,SignatureKey) : TEXT("missing"),*Signature);
        return 3;
    }

    const FString TuningPath = FString(GeneratedRoot) + TEXT("Data/DA_PrototypeTuning");
    if (bRegenerate || (!bValidate && !Exists(TuningPath)))
    {
        UPackage* Package = EditablePackage(TuningPath);
        ULDPrototypeTuning* Tuning = NewObject<ULDPrototypeTuning>(Package, TEXT("DA_PrototypeTuning"), RF_Public | RF_Standalone);
        if (!SaveAsset(Tuning, TuningPath, Signature)) { return 3; }
    }
    ULDPrototypeTuning* Tuning = LoadObject<ULDPrototypeTuning>(nullptr, *(TuningPath + TEXT(".DA_PrototypeTuning")));
    FString TuningError;
    if (!HasSignature(Tuning, Signature) || !Tuning->Validate(TuningError)) { UE_LOG(LogTemp, Error, TEXT("Missing/stale tuning: %s"), *TuningError); return 3; }

    FString CharacterJson,CharacterError;
    if (!FFileHelper::LoadFileToString(CharacterJson,*(FPaths::ProjectDir()/TEXT("docs/v04/data/prototype_rules.json")))) { return 3; }
    ULDCharacterCatalog* ExpectedCatalog=NewObject<ULDCharacterCatalog>();
    if (!ExpectedCatalog->Import(CharacterJson,CharacterError)) { UE_LOG(LogTemp,Error,TEXT("Invalid character source: %s"),*CharacterError); return 3; }
    const FString CatalogPath=FString(GeneratedRoot)+TEXT("Data/DA_CharacterCatalog");
    if (bRegenerate || (!bValidate && !Exists(CatalogPath)))
    {
        auto* Catalog=NewObject<ULDCharacterCatalog>(EditablePackage(CatalogPath),TEXT("DA_CharacterCatalog"),RF_Public|RF_Standalone);
        if (!Catalog->Import(CharacterJson,CharacterError) || !SaveAsset(Catalog,CatalogPath,ExpectedCatalog->SourceChecksum)) { return 3; }
    }
    const auto* Catalog=ULDCharacterCatalog::Load();
    if (!Catalog || !Catalog->Validate(CharacterError) || Catalog->SourceChecksum!=ExpectedCatalog->SourceChecksum || !HasSignature(const_cast<ULDCharacterCatalog*>(Catalog),ExpectedCatalog->SourceChecksum))
    { UE_LOG(LogTemp,Error,TEXT("Missing/stale character catalog: %s"),*CharacterError); return 3; }
    UE_LOG(LogTemp,Display,TEXT("LD_CHARACTER_CATALOG schema=%d sha256=%s"),Catalog->SchemaVersion,*Catalog->SourceChecksum);

    FString CombatJson,CombatError;
    if(!FFileHelper::LoadFileToString(CombatJson,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/V07/combat.json")))){return 3;}
    auto* ExpectedCombat=NewObject<ULDTacticalCatalog>();if(!ExpectedCombat->Import(CombatJson,CombatError)){UE_LOG(LogTemp,Error,TEXT("%s"),*CombatError);return 3;}
    const FString CombatPath=FString(GeneratedRoot)+TEXT("V07/DA_CombatCatalog");
    if(bRegenerate||(!bValidate&&(!Exists(CombatPath)||!ULDTacticalCatalog::Load()||ULDTacticalCatalog::Load()->Checksum!=ExpectedCombat->Checksum))){auto* Combat=NewObject<ULDTacticalCatalog>(EditablePackage(CombatPath),TEXT("DA_CombatCatalog"),RF_Public|RF_Standalone);if(!Combat->Import(CombatJson,CombatError)||!SaveAsset(Combat,CombatPath,ExpectedCombat->Checksum)){return 3;}}
    FString GearJson,GearError;if(!FFileHelper::LoadFileToString(GearJson,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/V07/equipment.json")))){return 3;}
    auto* ExpectedGear=NewObject<ULDEquipmentCatalog>();if(!ExpectedGear->Import(GearJson,GearError)){UE_LOG(LogTemp,Error,TEXT("%s"),*GearError);return 3;}
    const FString GearPath=TEXT("/Game/Lockdown/Generated/V07/DA_EquipmentCatalog");auto* ExistingGear=ULDEquipmentCatalog::Load();
    if(bRegenerate||(!bValidate&&(!ExistingGear||!ExistingGear->Validate(GearError)||ExistingGear->Checksum!=ExpectedGear->Checksum)))
    {auto* Gear=NewObject<ULDEquipmentCatalog>(EditablePackage(GearPath),TEXT("DA_EquipmentCatalog"),RF_Public|RF_Standalone);if(!Gear->Import(GearJson,GearError)||!SaveAsset(Gear,GearPath,ExpectedGear->Checksum)){return 3;}}
    auto* Gear=ULDEquipmentCatalog::Load();if(!Gear||!Gear->Validate(GearError)||Gear->Checksum!=ExpectedGear->Checksum||!HasSignature(Gear,ExpectedGear->Checksum)){UE_LOG(LogTemp,Error,TEXT("Equipment catalog: %s"),*GearError);return 3;}
    FString AcqJson,AcqError;if(!FFileHelper::LoadFileToString(AcqJson,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/V07/acquisition.json")))){return 3;}
    auto* ExpectedAcq=NewObject<ULDAcquisitionCatalog>();if(!ExpectedAcq->Import(AcqJson,AcqError)){UE_LOG(LogTemp,Error,TEXT("Acquisition import: %s"),*AcqError);return 3;}
    const FString AcqPath=TEXT("/Game/Lockdown/Generated/V07/DA_AcquisitionCatalog");auto* ExistingAcq=ULDAcquisitionCatalog::Load();
    if(bRegenerate||(!bValidate&&(!Exists(AcqPath)||!ExistingAcq||ExistingAcq->Checksum!=ExpectedAcq->Checksum))){auto* Acq=NewObject<ULDAcquisitionCatalog>(EditablePackage(AcqPath),TEXT("DA_AcquisitionCatalog"),RF_Public|RF_Standalone);if(!Acq->Import(AcqJson,AcqError)||!SaveAsset(Acq,AcqPath,ExpectedAcq->Checksum)){return 3;}}
    auto* Acq=ULDAcquisitionCatalog::Load();if(!Acq||!Acq->Validate(AcqError)||Acq->Checksum!=ExpectedAcq->Checksum||!HasSignature(Acq,ExpectedAcq->Checksum)){UE_LOG(LogTemp,Error,TEXT("Acquisition catalog: %s"),*AcqError);return 3;}
    const auto* Combat=ULDTacticalCatalog::Load();if(!Combat||!Combat->Validate(CombatError)||Combat->Checksum!=ExpectedCombat->Checksum||!HasSignature(const_cast<ULDTacticalCatalog*>(Combat),ExpectedCombat->Checksum)){UE_LOG(LogTemp,Error,TEXT("Missing/stale B71 catalog %s"),*CombatError);return 3;}
    UE_LOG(LogTemp,Display,TEXT("LD_B71_CATALOG schema=7 sha256=%s"),*Combat->Checksum);
    UInputMappingContext* Context = nullptr;
    const FString ContextPath = FString(GeneratedRoot) + TEXT("Input/IMC_Gameplay");
    const bool bWriteContext = bRegenerate || (!bValidate && !Exists(ContextPath));
    if (bWriteContext) { Context = NewObject<UInputMappingContext>(EditablePackage(ContextPath), TEXT("IMC_Gameplay"), RF_Public | RF_Standalone); }
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
        {TEXT("Menu"), EInputActionValueType::Boolean, EKeys::Escape},
        {TEXT("Zoom"), EInputActionValueType::Axis1D, EKeys::MouseWheelAxis}
    };
    for (const FInputSpec& Spec : Inputs)
    {
        const FString Name = FString(TEXT("IA_")) + Spec.Name;
        const FString Path = FString(GeneratedRoot) + TEXT("Input/") + Name;
        UInputAction* Action = nullptr;
        if (bRegenerate || (!bValidate && !Exists(Path)))
        {
            Action = NewObject<UInputAction>(EditablePackage(Path), *Name, RF_Public | RF_Standalone);
            Action->ValueType = Spec.Type;
            if (!SaveAsset(Action, Path, Signature)) { return 4; }
        }
        else { Action = LoadObject<UInputAction>(nullptr, *(Path + TEXT(".") + Name)); }
        if (!HasSignature(Action, Signature) || Action->ValueType != Spec.Type) { UE_LOG(LogTemp, Error, TEXT("Missing/stale input %s"), *Name); return 4; }
        if (bWriteContext && Spec.Key.IsValid()) { Context->MapKey(Action, Spec.Key); }
        if (bWriteContext && FString(Spec.Name) == TEXT("Move"))
        {
            auto MapMovement = [Context,Action](FKey Key, bool bY, bool bNegative)
            {
                FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action,Key);
                if (bNegative) { Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context)); }
                if (bY)
                {
                    UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Context);
                    Swizzle->Order = EInputAxisSwizzle::YXZ;
                    Mapping.Modifiers.Add(Swizzle);
                }
            };
            MapMovement(EKeys::W,true,false); MapMovement(EKeys::S,true,true);
            MapMovement(EKeys::A,false,true); MapMovement(EKeys::D,false,false);
        }
    }
    if (Context->GetMappings().Num() != 13) { UE_LOG(LogTemp,Error,TEXT("Expected 13 gameplay key mappings")); return 4; }
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
                if (Item->GetStringField(TEXT("label"))==TEXT("Plateau")) { Actor->GetStaticMeshComponent()->ComponentTags.Add(TEXT("LDHighGround")); }
                if (Item->GetStringField(TEXT("label"))==TEXT("PublicSensorBase")) { Actor->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel2,ECR_Ignore); }
                if (Item->GetStringField(TEXT("label")) == TEXT("Ground") || Item->GetStringField(TEXT("label")) == TEXT("Ramp") || Item->GetStringField(TEXT("label")) == TEXT("Plateau"))
                {
                    Actor->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
                    Actor->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore);
                }
                Actor->SetActorScale3D(Size / 100.0);
                const FString MaterialName = Item->GetStringField(TEXT("label")) == TEXT("Ground") ? TEXT("M_Ground")
                    : (Item->GetStringField(TEXT("mesh")) == TEXT("Cylinder") ? TEXT("M_Tree") : TEXT("M_Stone"));
                Actor->GetStaticMeshComponent()->SetMaterial(0,LoadObject<UMaterial>(nullptr,*(FString(GeneratedRoot)+TEXT("Materials/")+MaterialName+TEXT(".")+MaterialName)));
            }
            APlayerStart* Start = World->SpawnActor<APlayerStart>(FVector(-400, 0, 100), FRotator::ZeroRotator);
            ADirectionalLight* Light = World->SpawnActor<ADirectionalLight>(FVector(0, 0, 1000), FRotator(-60, -35, 0));
            if (!Start || !Light) { return 5; }
            Start->SetActorLabel(TEXT("Spawn_Initial"));
            Light->SetActorLabel(TEXT("Sun"));
            Light->GetLightComponent()->SetIntensity(5.0f);
            CastChecked<UDirectionalLightComponent>(Light->GetLightComponent())->SetForwardShadingPriority(2);
            ADirectionalLight* Fill = World->SpawnActor<ADirectionalLight>(FVector(0,0,1000),FRotator(-60,145,0));
            Fill->SetActorLabel(TEXT("Fill"));
            Fill->GetLightComponent()->SetIntensity(1.0f);
            Fill->GetLightComponent()->SetCastShadows(false);
            CastChecked<UDirectionalLightComponent>(Fill->GetLightComponent())->SetForwardShadingPriority(1);
            ANavMeshBoundsVolume* Bounds = World->SpawnActor<ANavMeshBoundsVolume>(FVector(0,0,200),FRotator::ZeroRotator);
            if (!Bounds) { return 5; }
            Bounds->SetActorLabel(TEXT("NavigationBounds"));
            Bounds->Brush = NewObject<UModel>(Bounds,NAME_None,RF_Transactional);
            Bounds->Brush->Initialize(Bounds,false);
            Bounds->GetBrushComponent()->Brush = Bounds->Brush;
            UCubeBuilder* Builder = NewObject<UCubeBuilder>(Bounds,NAME_None,RF_Transactional);
            Bounds->BrushBuilder = Builder;
            Builder->X = 4800; Builder->Y = 3200; Builder->Z = 800;
            if (!Builder->Build(World,Bounds)) { return 5; }
            Bounds->Brush->BuildBound();
            Bounds->GetBrushComponent()->BuildSimpleBrushCollision();
            Bounds->GetBrushComponent()->UpdateBounds();
            Bounds->PostEditChange();
            Bounds->PostEditMove(true);
            Bounds->GetBrushComponent()->SetCanEverAffectNavigation(false);
            if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)) { Nav->OnNavigationBoundsUpdated(Bounds); }
            Stamp(World, Signature);
            const FString MapFile = FPackageName::LongPackageNameToFilename(Path, FPackageName::GetMapPackageExtension());
            if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(MapFile), true)) { return 5; }
            if (!UEditorLoadingAndSavingUtils::SaveMap(World, Path)) { return 5; }
        }
        else
        {
            // Initialize the saved editor world so scene-component world transforms are valid.
            const FString MapFile = FPackageName::LongPackageNameToFilename(Path, FPackageName::GetMapPackageExtension());
            World = UEditorLoadingAndSavingUtils::LoadMap(MapFile);
        }
        if (!HasSignature(World, Signature)) { UE_LOG(LogTemp, Error, TEXT("Missing/stale map %s"), *Path); return 5; }
        int32 FoundCount = 0;
        int32 NavigationVolumes = 0;
        for (AActor* Actor : World->PersistentLevel->Actors)
        {
            if (ANavMeshBoundsVolume* Bounds = Cast<ANavMeshBoundsVolume>(Actor))
            {
                const FVector Size = Bounds->GetComponentsBoundingBox(true).GetSize();
                if (!Size.Equals(FVector(4800,3200,800),1.0)) { UE_LOG(LogTemp,Error,TEXT("Invalid navigation bounds: %s"),*Size.ToString()); return 5; }
                ++NavigationVolumes;
            }
        }
        if (NavigationVolumes != 1) { UE_LOG(LogTemp,Error,TEXT("Expected one navigation volume")); return 5; }
        for (const TSharedPtr<FJsonValue>& Value : *Geometry)
        {
            const TSharedPtr<FJsonObject> Item = Value->AsObject();
            FVector Location, Size;
            ReadVector(Item, TEXT("location"), Location);
            ReadVector(Item, TEXT("size"), Size);
            double Pitch = 0;
            Item->TryGetNumberField(TEXT("pitch"), Pitch);
            int32 Matches = 0;
            if (!World->PersistentLevel) { return 5; }
            // A package-loaded world has no initialized level collection for TActorIterator.
            // Inspect its serialized persistent actors so Validate also works in a fresh process.
            for (AActor* SavedActor : World->PersistentLevel->Actors)
            {
                const AStaticMeshActor* Actor = Cast<AStaticMeshActor>(SavedActor);
                if (!Actor || Actor->GetActorLabel() != Item->GetStringField(TEXT("label"))) { continue; }
                const UStaticMesh* Mesh = Actor->GetStaticMeshComponent()->GetStaticMesh();
                const FString Label=Item->GetStringField(TEXT("label"));
                const bool bGround=Label==TEXT("Ground") || Label==TEXT("Ramp") || Label==TEXT("Plateau");
                if (Actor->GetStaticMeshComponent()->GetCollisionResponseToChannel(ECC_GameTraceChannel1)!=(bGround ? ECR_Block : ECR_Ignore)
                    || Actor->GetStaticMeshComponent()->GetCollisionResponseToChannel(ECC_GameTraceChannel2)!=((bGround || Label==TEXT("PublicSensorBase")) ? ECR_Ignore : ECR_Block)
                    || Actor->GetStaticMeshComponent()->ComponentHasTag(TEXT("LDHighGround"))!=(Label==TEXT("Plateau")))
                { UE_LOG(LogTemp,Error,TEXT("Aim/vision collision or elevation tag mismatch in %s for %s"),*Path,*Label); return 5; }
                if (!Actor->GetActorLocation().Equals(Location, 0.1) || !Actor->GetActorScale3D().Equals(Size / 100.0, 0.001)
                    || !Actor->GetActorRotation().Equals(FRotator(Pitch, 0, 0), 0.01) || !Mesh || Mesh->GetName() != Item->GetStringField(TEXT("mesh")))
                {
                    UE_LOG(LogTemp, Error, TEXT("Geometry mismatch in %s for %s: location %s expected %s; scale %s expected %s; rotation %s; mesh %s"),
                        *Path, *Actor->GetActorLabel(), *Actor->GetActorLocation().ToString(), *Location.ToString(),
                        *Actor->GetActorScale3D().ToString(), *(Size / 100.0).ToString(), *Actor->GetActorRotation().ToString(), *GetNameSafe(Mesh));
                    return 5;
                }
                ++Matches;
            }
            if (Matches != 1) { UE_LOG(LogTemp, Error, TEXT("Expected one %s in %s, found %d"), *Item->GetStringField(TEXT("label")), *Path, Matches); return 5; }
            FoundCount += Matches;
        }
        UE_LOG(LogTemp, Display, TEXT("Validated %s: %d generated meshes"), *Path, FoundCount);
    }
    if(!LDRegionBootstrap::Run(bValidate,bRegenerate,FParse::Param(*Params,TEXT("RebuildRegionMaps")))){return 6;}
    UE_LOG(LogTemp,Display,TEXT("LD_BOOTSTRAP_SUCCESS %s (migration plus two regional topologies; 28 packages)"),*Signature);
    return 0;
}
