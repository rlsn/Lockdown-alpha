#include "LDDemoVfxImport.h"
#include "LDEditorLog.h"
#include "Character/LDHitVfx.h"
#include "Framework/LDDemoEntry.h"
#include "Dom/JsonObject.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/Texture2D.h"
#include "Factories/TextureFactory.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
    struct FVfxSpec
    {
        FString Id;
        FString Package;
        int32 Width = 0;
        int32 Height = 0;
    };

    constexpr TCHAR GeneratorFolder[] = TEXT("Blender");
    constexpr TCHAR Manifest[] = TEXT("vfx_textures.json");

    bool VfxFail(FString& Error, const FString& Message) { Error = Message; return false; }

    FString VfxRoot() { return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("SourceData"), TEXT("VFX"))); }

    FString VfxFile(const FVfxSpec& Spec) { return VfxRoot() / (Spec.Id + TEXT(".png")); }

    // Every mask the runtime loads (LDHitVfx::EMask) must be listed, and nothing else.
    bool VfxLoadManifest(TArray<FVfxSpec>& Out, FString& Error)
    {
        Out.Reset();
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *(VfxRoot() / Manifest))) { return VfxFail(Error, TEXT("SourceData/VFX/vfx_textures.json is missing.")); }
        TSharedPtr<FJsonObject> Json;
        int32 Schema = 0;
        FString Generator, GeneratorHash, Provenance;
        const TArray<TSharedPtr<FJsonValue>>* Textures = nullptr;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()
            || !Json->TryGetNumberField(TEXT("schema"), Schema) || Schema != 1 || !Json->TryGetStringField(TEXT("generator"), Generator)
            || Generator != TEXT("Blender/hit_textures.py") || !Json->TryGetStringField(TEXT("generator_sha256"), GeneratorHash)
            || !Json->TryGetStringField(TEXT("provenance"), Provenance) || Provenance.IsEmpty() || !Json->TryGetArrayField(TEXT("textures"), Textures))
        {
            return VfxFail(Error, TEXT("vfx_textures.json must be valid JSON with schema 1, generator Blender/hit_textures.py, generator_sha256, provenance and a textures list."));
        }
        FString HashError;
        if (!LDDemoEntry::VerifyFileHash(VfxRoot() / Generator, GeneratorHash, HashError))
        {
            return VfxFail(Error, TEXT("SourceData/VFX/Blender/hit_textures.py is missing or differs from its manifest SHA-256; rerun it and update the manifest."));
        }
        TSet<FString> Known, Listed;
        for (int32 Mask = 0; Mask < int32(LDHitVfx::EMask::Count); ++Mask) { Known.Add(LDHitVfx::MaskId(LDHitVfx::EMask(Mask))); }
        for (const TSharedPtr<FJsonValue>& Value : *Textures)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            FVfxSpec Spec;
            FString Hash, Purpose;
            if (!Value->TryGetObject(Object) || !(*Object)->TryGetStringField(TEXT("id"), Spec.Id) || Listed.Contains(Spec.Id)
                || !(*Object)->TryGetStringField(TEXT("sha256"), Hash) || !(*Object)->TryGetNumberField(TEXT("width"), Spec.Width)
                || !(*Object)->TryGetNumberField(TEXT("height"), Spec.Height) || Spec.Width <= 0 || Spec.Height <= 0
                || !(*Object)->TryGetStringField(TEXT("purpose"), Purpose) || Purpose.IsEmpty())
            {
                return VfxFail(Error, TEXT("Every effect texture needs a unique id, sha256, width, height and purpose."));
            }
            if (!Known.Contains(Spec.Id)) { return VfxFail(Error, TEXT("vfx_textures.json lists a mask the game does not use: ") + Spec.Id); }
            if (!LDDemoEntry::VerifyFileHash(VfxFile(Spec), Hash, HashError))
            {
                return VfxFail(Error, FString::Printf(TEXT("SourceData/VFX/%s.png is missing or differs from its manifest SHA-256."), *Spec.Id));
            }
            Spec.Package = LDHitVfx::TexturePackage(*Spec.Id);
            Listed.Add(Spec.Id);
            Out.Add(MoveTemp(Spec));
        }
        for (const FString& Id : Known)
        {
            if (!Listed.Contains(Id)) { return VfxFail(Error, TEXT("vfx_textures.json does not list the mask ") + Id); }
        }
        TArray<FString> Found;
        IFileManager::Get().FindFiles(Found, *(VfxRoot() / TEXT("*")), true, false);
        for (const FString& File : Found)
        {
            if (File != Manifest && !(File.EndsWith(TEXT(".png"), ESearchCase::CaseSensitive) && Listed.Contains(FPaths::GetBaseFilename(File))))
            {
                return VfxFail(Error, TEXT("Unlisted file under SourceData/VFX: ") + File);
            }
        }
        TArray<FString> Folders;
        IFileManager::Get().FindFiles(Folders, *(VfxRoot() / TEXT("*")), false, true);
        for (const FString& Folder : Folders)
        {
            if (Folder != GeneratorFolder) { return VfxFail(Error, TEXT("SourceData/VFX holds only the Blender folder: ") + Folder); }
        }
        TArray<FString> Scripts;
        IFileManager::Get().FindFiles(Scripts, *(VfxRoot() / GeneratorFolder / TEXT("*")), true, false);
        for (const FString& File : Scripts)
        {
            if (File != TEXT("hit_textures.py")) { return VfxFail(Error, TEXT("SourceData/VFX/Blender holds only hit_textures.py: ") + File); }
        }
        Error.Reset();
        return true;
    }

    // Masks are linear data (RGB glow, A coverage): filtered with mips, never streamed out.
    void VfxApplySettings(UTexture2D& Texture)
    {
        Texture.LODGroup = TEXTUREGROUP_Effects;
        Texture.CompressionSettings = TC_Default;
        Texture.MipGenSettings = TMGS_FromTextureGroup;
        Texture.SRGB = false;
        Texture.NeverStream = true;
    }

    bool VfxSettingsMatch(const UTexture2D& Texture)
    {
        return Texture.LODGroup == TEXTUREGROUP_Effects && Texture.CompressionSettings == TC_Default
            && Texture.MipGenSettings == TMGS_FromTextureGroup && !Texture.SRGB && Texture.NeverStream;
    }

    template<typename T>
    T* VfxLoad(const FString& Package)
    {
        return FPackageName::DoesPackageExist(Package) ? LoadObject<T>(nullptr, *(Package + TEXT(".") + FPackageName::GetShortName(Package))) : nullptr;
    }

    bool VfxImportedFromCurrent(const UTexture2D& Texture, const FVfxSpec& Spec)
    {
        const UAssetImportData* Data = Texture.AssetImportData;
        const FMD5Hash Current = FMD5Hash::HashFile(*VfxFile(Spec));
        return Data && !Data->SourceData.SourceFiles.IsEmpty() && Current.IsValid() && Data->SourceData.SourceFiles[0].FileHash == Current
            && FPaths::GetCleanFilename(Data->SourceData.SourceFiles[0].RelativeFilename).Equals(Spec.Id + TEXT(".png"), ESearchCase::CaseSensitive);
    }

    bool VfxSave(UObject& Asset, FString& Error)
    {
        const FString File = FPackageName::LongPackageNameToFilename(Asset.GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true)) { return VfxFail(Error, TEXT("Cannot create the effect asset folder: ") + File); }
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        return UPackage::SavePackage(Asset.GetOutermost(), &Asset, *File, Args) || VfxFail(Error, TEXT("Effect asset could not be saved: ") + File);
    }

    template<typename T>
    T* VfxAdd(UMaterial& Material)
    {
        T* Expression = NewObject<T>(&Material);
        Expression->Material = &Material;
        Material.GetExpressionCollection().AddExpression(Expression);
        return Expression;
    }

    // Layer: translucent unlit, emissive = Glow x Mask.r + Ink, opacity = Mask.a x Fade (two-sided sprites and marks).
    // Shell: additive unlit sphere, emissive = Glow x Fade x (tiled Mask.r + fresnel rim).
    UMaterial* VfxBuildMaterial(const TCHAR* PackageName, bool bShell, UTexture2D* DefaultMask, FString& Error)
    {
        const FString Name = FPackageName::GetShortName(PackageName);
        UMaterial* Material = VfxLoad<UMaterial>(PackageName);
        if (!Material) { Material = NewObject<UMaterial>(CreatePackage(PackageName), *Name, RF_Public | RF_Standalone); }
        if (!Material || !DefaultMask) { VfxFail(Error, FString(TEXT("Cannot create the effect material ")) + PackageName); return nullptr; }
        Material->PreEditChange(nullptr);
        FMaterialExpressionCollection& Collection = Material->GetExpressionCollection();
        TArray<UObject*> Previous(Collection.Expressions);
        Previous.Append(Collection.EditorComments);
        for (UObject* Old : Previous)
        {
            if (Old) { Old->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional); }
        }
        Collection.Empty();
        for (int32 Property = 0; Property < MP_MAX; ++Property)
        {
            if (FExpressionInput* Input = Material->GetExpressionInputForProperty(EMaterialProperty(Property))) { Input->Expression = nullptr; }
        }
        UMaterialEditorOnlyData* Inputs = Material->GetEditorOnlyData();
        auto* Mask = VfxAdd<UMaterialExpressionTextureSampleParameter2D>(*Material);
        Mask->ParameterName = LDHitVfx::MaskParameter;
        Mask->Texture = DefaultMask;
        Mask->SamplerType = SAMPLERTYPE_LinearColor;
        auto* Glow = VfxAdd<UMaterialExpressionVectorParameter>(*Material);
        Glow->ParameterName = LDHitVfx::GlowParameter;
        Glow->DefaultValue = FLinearColor::White;
        auto* Fade = VfxAdd<UMaterialExpressionScalarParameter>(*Material);
        Fade->ParameterName = LDHitVfx::FadeParameter;
        Fade->DefaultValue = 1.f;
        if (!bShell)
        {
            auto* Ink = VfxAdd<UMaterialExpressionVectorParameter>(*Material);
            Ink->ParameterName = LDHitVfx::InkParameter;
            Ink->DefaultValue = FLinearColor::Black;
            auto* Hot = VfxAdd<UMaterialExpressionMultiply>(*Material);
            Hot->A.Connect(0, Glow);
            Hot->B.Connect(1, Mask);
            auto* Emissive = VfxAdd<UMaterialExpressionAdd>(*Material);
            Emissive->A.Connect(0, Hot);
            Emissive->B.Connect(0, Ink);
            auto* Opacity = VfxAdd<UMaterialExpressionMultiply>(*Material);
            Opacity->A.Connect(4, Mask);
            Opacity->B.Connect(0, Fade);
            Inputs->EmissiveColor.Connect(0, Emissive);
            Inputs->Opacity.Connect(0, Opacity);
            Material->BlendMode = BLEND_Translucent;
            Material->TwoSided = true;
        }
        else
        {
            auto* Tiling = VfxAdd<UMaterialExpressionTextureCoordinate>(*Material);
            Tiling->UTiling = 8.f;
            Tiling->VTiling = 4.f;
            Mask->Coordinates.Connect(0, Tiling);
            auto* Rim = VfxAdd<UMaterialExpressionFresnel>(*Material);
            Rim->Exponent = 3.f;
            Rim->BaseReflectFraction = .04f;
            auto* Lattice = VfxAdd<UMaterialExpressionAdd>(*Material);
            Lattice->A.Connect(1, Mask);
            Lattice->B.Connect(0, Rim);
            auto* Lit = VfxAdd<UMaterialExpressionMultiply>(*Material);
            Lit->A.Connect(0, Lattice);
            Lit->B.Connect(0, Glow);
            auto* Faded = VfxAdd<UMaterialExpressionMultiply>(*Material);
            Faded->A.Connect(0, Lit);
            Faded->B.Connect(0, Fade);
            Inputs->EmissiveColor.Connect(0, Faded);
            Material->BlendMode = BLEND_Additive;
            Material->TwoSided = false;
        }
        Material->SetShadingModel(MSM_Unlit);
        Material->bUseEmissiveForDynamicAreaLighting = false;
        // Identities derive from the package and graph position, so a rebuild of the same code keeps every GUID.
        const uint32 Seed = FCrc::StrCrc32(PackageName);
        for (int32 Index = 0; Index < Collection.Expressions.Num(); ++Index)
        {
            UMaterialExpression* Expression = Collection.Expressions[Index];
            Expression->MaterialExpressionGuid = FGuid(Seed, uint32(Index), 0x4C44u, 3u);
            if (Expression->HasAParameterName()) { Expression->GetParameterExpressionId() = FGuid(Seed, uint32(Index), 0x4C44u, 4u); }
        }
        Material->PostEditChange();
        Material->MarkPackageDirty();
        return Material;
    }

    bool VfxCheckMaterial(const UMaterial* Material, const TCHAR* PackageName, bool bShell, FString& Error)
    {
        if (!Material) { return VfxFail(Error, FString(PackageName) + TEXT(" is missing; run ImportDemoArt.ps1 -Mode ImportVFX.")); }
        TArray<FName> Parameters;
        for (const UMaterialExpression* Expression : Material->GetExpressions())
        {
            if (Expression && !Expression->GetParameterName().IsNone()) { Parameters.Add(Expression->GetParameterName()); }
        }
        TArray<FName> Expected{LDHitVfx::MaskParameter, LDHitVfx::GlowParameter, LDHitVfx::FadeParameter};
        if (!bShell) { Expected.Add(LDHitVfx::InkParameter); }
        Parameters.Sort(FNameLexicalLess());
        Expected.Sort(FNameLexicalLess());
        const UMaterialEditorOnlyData* Inputs = Material->GetEditorOnlyData();
        const bool bGraph = Inputs && Parameters == Expected && Material->GetExpressions().Num() == (bShell ? 8 : 7)
            && Inputs->EmissiveColor.IsConnected() && Inputs->Opacity.IsConnected() == !bShell
            && Material->BlendMode == (bShell ? BLEND_Additive : BLEND_Translucent) && Material->GetShadingModels().HasOnlyShadingModel(MSM_Unlit);
        return bGraph || VfxFail(Error, FString::Printf(TEXT("%s does not hold the importer's graph (%d expressions, %d parameters); ")
            TEXT("run ImportDemoArt.ps1 -Mode ImportVFX to rebuild it."), PackageName, Material->GetExpressions().Num(), Parameters.Num()));
    }

    // A dedicated-server load drops material graphs (see LDDemoArtImport).
    bool VfxRequireClient(FString& Error)
    {
        return GIsClient || VfxFail(Error, TEXT("Effect import and validation need a client process; a dedicated-server load drops material graphs."));
    }
}

bool LDDemoVfxImport::Import(FString& Error)
{
    TArray<FVfxSpec> Specs;
    if (!VfxRequireClient(Error) || !VfxLoadManifest(Specs, Error)) { return false; }
    UTexture2D* Star = nullptr;
    for (const FVfxSpec& Spec : Specs)
    {
        UTexture2D* Texture = VfxLoad<UTexture2D>(Spec.Package);
        if (!Texture || !VfxSettingsMatch(*Texture) || Texture->Source.GetSizeX() != Spec.Width || Texture->Source.GetSizeY() != Spec.Height
            || !VfxImportedFromCurrent(*Texture, Spec))
        {
            UPackage* Package = CreatePackage(*Spec.Package);
            Package->FullyLoad();
            auto* Factory = NewObject<UTextureFactory>(GetTransientPackage());
            Factory->AddToRoot();
            UTextureFactory::SuppressImportOverwriteDialog();
            bool bCancelled = false;
            UObject* Imported = UFactory::StaticImportObject(UTexture2D::StaticClass(), Package, FName(*FPackageName::GetShortName(Spec.Package)),
                RF_Public | RF_Standalone, bCancelled, *VfxFile(Spec), nullptr, Factory);
            Factory->RemoveFromRoot();
            Texture = Cast<UTexture2D>(Imported);
            if (!Texture || bCancelled) { return VfxFail(Error, TEXT("Effect texture import failed: ") + Spec.Id); }
            VfxApplySettings(*Texture);
            Texture->PostEditChange();
            Texture->MarkPackageDirty();
            if (!VfxSave(*Texture, Error)) { return false; }
            UE_LOG(LogLDEditor, Display, TEXT("LD_DEMO_VFX_TEXTURE_IMPORTED %s <- SourceData/VFX/%s.png"), *Spec.Package, *Spec.Id);
        }
        if (Spec.Id == LDHitVfx::MaskId(LDHitVfx::EMask::Star)) { Star = Texture; }
    }
    for (const bool bShell : {false, true})
    {
        const TCHAR* Package = bShell ? LDHitVfx::ShellMaterial : LDHitVfx::LayerMaterial;
        UMaterial* Material = VfxBuildMaterial(Package, bShell, Star, Error);
        if (!Material || !VfxCheckMaterial(Material, Package, bShell, Error) || !VfxSave(*Material, Error)) { return false; }
        UE_LOG(LogLDEditor, Display, TEXT("LD_DEMO_VFX_MATERIAL_BUILT %s"), Package);
    }
    Error.Reset();
    return true;
}

bool LDDemoVfxImport::Validate(FString& Error)
{
    TArray<FVfxSpec> Specs;
    if (!VfxRequireClient(Error) || !VfxLoadManifest(Specs, Error)) { return false; }
    for (const FVfxSpec& Spec : Specs)
    {
        const UTexture2D* Texture = VfxLoad<UTexture2D>(Spec.Package);
        if (!Texture) { return VfxFail(Error, Spec.Package + TEXT(" is missing; run ImportDemoArt.ps1 -Mode ImportVFX.")); }
        if (!VfxSettingsMatch(*Texture)) { return VfxFail(Error, Spec.Package + TEXT(" lost its effect texture settings; run ImportDemoArt.ps1 -Mode ImportVFX.")); }
        if (Texture->Source.GetSizeX() != Spec.Width || Texture->Source.GetSizeY() != Spec.Height)
        {
            return VfxFail(Error, FString::Printf(TEXT("%s is %lldx%lld, the manifest says %dx%d."), *Spec.Package,
                Texture->Source.GetSizeX(), Texture->Source.GetSizeY(), Spec.Width, Spec.Height));
        }
        if (!VfxImportedFromCurrent(*Texture, Spec))
        {
            return VfxFail(Error, Spec.Package + TEXT(" was not imported from the current bytes of SourceData/VFX/") + Spec.Id
                + TEXT(".png; run ImportDemoArt.ps1 -Mode ImportVFX."));
        }
    }
    return VfxCheckMaterial(VfxLoad<UMaterial>(LDHitVfx::LayerMaterial), LDHitVfx::LayerMaterial, false, Error)
        && VfxCheckMaterial(VfxLoad<UMaterial>(LDHitVfx::ShellMaterial), LDHitVfx::ShellMaterial, true, Error);
}
