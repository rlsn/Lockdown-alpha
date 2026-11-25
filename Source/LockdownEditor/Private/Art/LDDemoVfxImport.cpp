#include "LDDemoVfxImport.h"
#include "LDDemoVfxNiagara.h"
#include "LDEditorLog.h"
#include "Character/LDHitVfx.h"
#include "Framework/LDDemoEntry.h"
#include "Dom/JsonObject.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/Texture2D.h"
#include "Factories/TextureFactory.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Engine/StaticMesh.h"
#include "InterchangeGenericAnimationPipeline.h"
#include "InterchangeGenericAssetsPipeline.h"
#include "InterchangeGenericAssetsPipelineSharedSettings.h"
#include "InterchangeGenericMaterialPipeline.h"
#include "InterchangeGenericMeshPipeline.h"
#include "InterchangeGenericTexturePipeline.h"
#include "InterchangeManager.h"
#include "InterchangeSourceData.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
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
        Error.Reset();
        return true;
    }

    // One slice asset of vfx_slice_assets.json: a texture or flipbook (Folder/T_<id>) or a slash mesh (Folder/<id>).
    struct FSliceSpec
    {
        FString Id;
        FString File;       // relative to SourceData/VFX ("Slice/fb_hit_basic.png")
        FString Package;
        bool bMesh = false;
        int32 Width = 0;
        int32 Height = 0;
        LDSliceVfx::ETex Tex = LDSliceVfx::ETex::Count;
        LDSliceVfx::EMesh Mesh = LDSliceVfx::EMesh::Count;
    };
    constexpr TCHAR SliceManifest[] = TEXT("vfx_slice_assets.json");
    constexpr TCHAR SliceFolder[] = TEXT("Slice");

    // The slice manifest lists exactly the textures and meshes the runtime loads (LDSliceVfx::ETex and EMesh), each with
    // the SHA-256 of its committed bytes, and the SHA-256 of every generator and tool script that made or checked them.
    bool SliceLoadManifest(TArray<FSliceSpec>& Out, TSet<FString>& Scripts, FString& Error)
    {
        Out.Reset();
        Scripts.Reset();
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *(VfxRoot() / SliceManifest))) { return VfxFail(Error, TEXT("SourceData/VFX/vfx_slice_assets.json is missing.")); }
        TSharedPtr<FJsonObject> Json;
        int32 Schema = 0;
        FString Provenance;
        const TSharedPtr<FJsonObject>* Generators = nullptr;
        const TSharedPtr<FJsonObject>* Tools = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Assets = nullptr;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()
            || !Json->TryGetNumberField(TEXT("schema"), Schema) || Schema != 1 || !Json->TryGetStringField(TEXT("provenance"), Provenance)
            || Provenance.IsEmpty() || !Json->TryGetObjectField(TEXT("generators"), Generators) || !Json->TryGetObjectField(TEXT("tools"), Tools)
            || !Json->TryGetArrayField(TEXT("assets"), Assets))
        {
            return VfxFail(Error, TEXT("vfx_slice_assets.json must be valid JSON with schema 1, provenance, generators, tools and an assets list."));
        }
        FString HashError;
        for (const TSharedPtr<FJsonObject>* Group : {Generators, Tools})
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Script : (*Group)->Values)
            {
                FString Hash;
                if (!Script.Key.StartsWith(FString(GeneratorFolder) + TEXT("/"), ESearchCase::CaseSensitive) || !Script.Key.EndsWith(TEXT(".py"), ESearchCase::CaseSensitive)
                    || Script.Key.Mid(FCString::Strlen(GeneratorFolder) + 1).Contains(TEXT("/")) || !Script.Value->TryGetString(Hash))
                {
                    return VfxFail(Error, TEXT("vfx_slice_assets.json lists a script outside SourceData/VFX/Blender: ") + Script.Key);
                }
                if (!LDDemoEntry::VerifyFileHash(VfxRoot() / Script.Key, Hash, HashError))
                {
                    return VfxFail(Error, FString::Printf(TEXT("SourceData/VFX/%s is missing or differs from its slice manifest SHA-256; rerun slice_manifest.py."), *Script.Key));
                }
                Scripts.Add(FPaths::GetCleanFilename(Script.Key));
            }
        }
        TMap<FString, LDSliceVfx::ETex> Textures;
        TMap<FString, LDSliceVfx::EMesh> Meshes;
        for (int32 Tex = 0; Tex < int32(LDSliceVfx::ETex::Count); ++Tex) { Textures.Add(LDSliceVfx::TexId(LDSliceVfx::ETex(Tex)), LDSliceVfx::ETex(Tex)); }
        for (int32 Mesh = 0; Mesh < int32(LDSliceVfx::EMesh::Count); ++Mesh) { Meshes.Add(LDSliceVfx::MeshId(LDSliceVfx::EMesh(Mesh)), LDSliceVfx::EMesh(Mesh)); }
        TSet<FString> Listed;
        for (const TSharedPtr<FJsonValue>& Value : *Assets)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            FSliceSpec Spec;
            FString Kind, Hash, Purpose;
            if (!Value->TryGetObject(Object) || !(*Object)->TryGetStringField(TEXT("id"), Spec.Id) || Listed.Contains(Spec.Id)
                || !(*Object)->TryGetStringField(TEXT("kind"), Kind) || !(*Object)->TryGetStringField(TEXT("file"), Spec.File)
                || !(*Object)->TryGetStringField(TEXT("sha256"), Hash) || !(*Object)->TryGetStringField(TEXT("purpose"), Purpose) || Purpose.IsEmpty())
            {
                return VfxFail(Error, TEXT("Every slice asset needs a unique id, kind, file, sha256 and purpose."));
            }
            Spec.bMesh = Kind == TEXT("static_mesh");
            if (Spec.bMesh)
            {
                if (!Meshes.Contains(Spec.Id) || Spec.File != FString(SliceFolder) / Spec.Id + TEXT(".fbx"))
                { return VfxFail(Error, TEXT("vfx_slice_assets.json lists a mesh the game does not use or at another path: ") + Spec.Id); }
                Spec.Mesh = Meshes[Spec.Id];
                Spec.Package = LDSliceVfx::MeshPackage(Spec.Mesh);
            }
            else
            {
                if ((Kind != TEXT("texture") && Kind != TEXT("flipbook")) || !Textures.Contains(Spec.Id) || Spec.File != FString(SliceFolder) / Spec.Id + TEXT(".png")
                    || (Kind == TEXT("flipbook")) != LDSliceVfx::IsFlipbook(Textures[Spec.Id]))
                { return VfxFail(Error, TEXT("vfx_slice_assets.json lists a texture the game does not use, of another kind or at another path: ") + Spec.Id); }
                if (!(*Object)->TryGetNumberField(TEXT("width"), Spec.Width) || !(*Object)->TryGetNumberField(TEXT("height"), Spec.Height)
                    || Spec.Width <= 0 || Spec.Height <= 0)
                { return VfxFail(Error, TEXT("A slice texture needs its width and height: ") + Spec.Id); }
                Spec.Tex = Textures[Spec.Id];
                Spec.Package = LDSliceVfx::TexturePackage(Spec.Tex);
            }
            if (!LDDemoEntry::VerifyFileHash(VfxRoot() / Spec.File, Hash, HashError))
            {
                return VfxFail(Error, FString::Printf(TEXT("SourceData/VFX/%s is missing or differs from its slice manifest SHA-256."), *Spec.File));
            }
            Listed.Add(Spec.Id);
            Out.Add(MoveTemp(Spec));
        }
        for (const TPair<FString, LDSliceVfx::ETex>& Tex : Textures) { if (!Listed.Contains(Tex.Key)) { return VfxFail(Error, TEXT("vfx_slice_assets.json does not list ") + Tex.Key); } }
        for (const TPair<FString, LDSliceVfx::EMesh>& Mesh : Meshes) { if (!Listed.Contains(Mesh.Key)) { return VfxFail(Error, TEXT("vfx_slice_assets.json does not list ") + Mesh.Key); } }
        Error.Reset();
        return true;
    }

    // SourceData/VFX holds the two manifests, the listed hit masks, the Blender folder (hit_textures.py and the listed
    // slice scripts) and the Slice folder (the listed slice assets), and nothing else.
    bool VfxCheckFiles(const TArray<FVfxSpec>& Hits, const TArray<FSliceSpec>& Slices, const TSet<FString>& SliceScripts, FString& Error)
    {
        TSet<FString> Masks, SliceFiles;
        for (const FVfxSpec& Spec : Hits) { Masks.Add(Spec.Id + TEXT(".png")); }
        for (const FSliceSpec& Spec : Slices) { SliceFiles.Add(FPaths::GetCleanFilename(Spec.File)); }
        const auto Only = [&Error](const FString& Folder, bool bFiles, TFunctionRef<bool(const FString&)> Allowed, const TCHAR* What)
        {
            TArray<FString> Found;
            IFileManager::Get().FindFiles(Found, *(Folder / TEXT("*")), bFiles, !bFiles);
            for (const FString& Name : Found)
            {
                if (!Allowed(Name)) { return VfxFail(Error, FString::Printf(TEXT("%s: %s"), What, *Name)); }
            }
            return true;
        };
        return Only(VfxRoot(), true, [&Masks](const FString& Name) { return Name == Manifest || Name == SliceManifest || Masks.Contains(Name); },
                TEXT("Unlisted file under SourceData/VFX"))
            && Only(VfxRoot(), false, [](const FString& Name) { return Name == GeneratorFolder || Name == SliceFolder; },
                TEXT("SourceData/VFX holds only the Blender and Slice folders"))
            && Only(VfxRoot() / GeneratorFolder, true, [&SliceScripts](const FString& Name) { return Name == TEXT("hit_textures.py") || SliceScripts.Contains(Name); },
                TEXT("SourceData/VFX/Blender holds only hit_textures.py and the scripts vfx_slice_assets.json lists"))
            && Only(VfxRoot() / GeneratorFolder, false, [](const FString&) { return false; }, TEXT("SourceData/VFX/Blender holds no folders"))
            && Only(VfxRoot() / SliceFolder, true, [&SliceFiles](const FString& Name) { return SliceFiles.Contains(Name); },
                TEXT("Unlisted file under SourceData/VFX/Slice"))
            && Only(VfxRoot() / SliceFolder, false, [](const FString&) { return false; }, TEXT("SourceData/VFX/Slice holds no folders"));
    }

    bool VfxLoadAll(TArray<FVfxSpec>& Hits, TArray<FSliceSpec>& Slices, FString& Error)
    {
        TSet<FString> Scripts;
        return VfxLoadManifest(Hits, Error) && SliceLoadManifest(Slices, Scripts, Error) && VfxCheckFiles(Hits, Slices, Scripts, Error);
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

    enum class EVfxMaterial : uint8 { Layer, Shell, Ghost };

    const TCHAR* VfxPackageOf(EVfxMaterial Kind)
    {
        return Kind == EVfxMaterial::Layer ? LDHitVfx::LayerMaterial : Kind == EVfxMaterial::Shell ? LDHitVfx::ShellMaterial : LDHitVfx::GhostMaterial;
    }

    // Layer: translucent unlit, emissive = Glow x Mask.r + Ink, opacity = Mask.a x Fade (two-sided sprites and marks).
    // Shell: additive unlit sphere, emissive = Glow x Fade x (tiled Mask.r + fresnel rim).
    // Ghost: translucent unlit skeletal afterimage, emissive = Glow x (rim + 0.3), opacity = Fade x (0.25 + 0.75 rim).
    // Scratch: build the same graph into a transient material instead (the reference VfxCheckMaterial compares with).
    UMaterial* VfxBuildMaterial(EVfxMaterial Kind, UTexture2D* DefaultMask, FString& Error, UMaterial* Scratch = nullptr)
    {
        const TCHAR* PackageName = VfxPackageOf(Kind);
        const bool bShell = Kind == EVfxMaterial::Shell;
        const FString Name = FPackageName::GetShortName(PackageName);
        UMaterial* Material = Scratch ? Scratch : VfxLoad<UMaterial>(PackageName);
        if (!Material) { Material = NewObject<UMaterial>(CreatePackage(PackageName), *Name, RF_Public | RF_Standalone); }
        if (!Material || !DefaultMask) { VfxFail(Error, FString(TEXT("Cannot create the effect material ")) + PackageName); return nullptr; }
        if (!Scratch) { Material->PreEditChange(nullptr); }
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
        if (Kind == EVfxMaterial::Ghost)
        {
            auto* GhostGlow = VfxAdd<UMaterialExpressionVectorParameter>(*Material);
            GhostGlow->ParameterName = LDHitVfx::GlowParameter;
            GhostGlow->DefaultValue = FLinearColor::White;
            auto* GhostFade = VfxAdd<UMaterialExpressionScalarParameter>(*Material);
            GhostFade->ParameterName = LDHitVfx::FadeParameter;
            GhostFade->DefaultValue = 1.f;
            auto* Rim = VfxAdd<UMaterialExpressionFresnel>(*Material);
            Rim->Exponent = 2.5f;
            Rim->BaseReflectFraction = .04f;
            auto* Lit = VfxAdd<UMaterialExpressionAdd>(*Material);
            Lit->A.Connect(0, Rim);
            Lit->ConstB = .3f;
            auto* Emissive = VfxAdd<UMaterialExpressionMultiply>(*Material);
            Emissive->A.Connect(0, Lit);
            Emissive->B.Connect(0, GhostGlow);
            auto* Edge = VfxAdd<UMaterialExpressionMultiply>(*Material);
            Edge->A.Connect(0, Rim);
            Edge->ConstB = .75f;
            auto* Body = VfxAdd<UMaterialExpressionAdd>(*Material);
            Body->A.Connect(0, Edge);
            Body->ConstB = .25f;
            auto* Opacity = VfxAdd<UMaterialExpressionMultiply>(*Material);
            Opacity->A.Connect(0, Body);
            Opacity->B.Connect(0, GhostFade);
            Inputs->EmissiveColor.Connect(0, Emissive);
            Inputs->Opacity.Connect(0, Opacity);
            Material->BlendMode = BLEND_Translucent;
            Material->TwoSided = false;
            Material->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
        }
        else
        {
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
        if (Scratch) { return Material; }
        Material->PostEditChange();
        Material->MarkPackageDirty();
        return Material;
    }

    // Everything the generated graph fixes: settings, each expression's type, parameter, constants and input links,
    // and the material's own inputs. Two materials with the same signature render the same graph.
    FString VfxSignature(const UMaterial& Material)
    {
        FString Out = FString::Printf(TEXT("blend=%d two=%d unlit=%d|"), int32(Material.BlendMode), Material.TwoSided ? 1 : 0,
            Material.GetShadingModels().HasOnlyShadingModel(MSM_Unlit) ? 1 : 0);
        const auto Expressions = Material.GetExpressions();
        const auto IndexOf = [&Expressions](const UMaterialExpression* Wanted)
        {
            for (int32 Index = 0; Index < Expressions.Num(); ++Index) { if (Expressions[Index] == Wanted) { return Index; } }
            return -1;
        };
        const auto Link = [&IndexOf](const FExpressionInput* Input)
        { return Input && Input->Expression ? FString::Printf(TEXT("<%d.%d"), IndexOf(Input->Expression), Input->OutputIndex) : FString(TEXT("<-")); };
        for (const UMaterialExpression* Expression : Expressions)
        {
            if (!Expression) { Out += TEXT("null|"); continue; }
            Out += Expression->GetClass()->GetName() + TEXT(":") + Expression->GetParameterName().ToString();
            if (const auto* Add = Cast<UMaterialExpressionAdd>(Expression)) { Out += FString::Printf(TEXT(" a=%.3f b=%.3f"), Add->ConstA, Add->ConstB); }
            if (const auto* Multiply = Cast<UMaterialExpressionMultiply>(Expression)) { Out += FString::Printf(TEXT(" a=%.3f b=%.3f"), Multiply->ConstA, Multiply->ConstB); }
            if (const auto* Rim = Cast<UMaterialExpressionFresnel>(Expression)) { Out += FString::Printf(TEXT(" e=%.3f r=%.3f"), Rim->Exponent, Rim->BaseReflectFraction); }
            if (const auto* Tiling = Cast<UMaterialExpressionTextureCoordinate>(Expression)) { Out += FString::Printf(TEXT(" u=%.3f v=%.3f"), Tiling->UTiling, Tiling->VTiling); }
            if (const auto* Scalar = Cast<UMaterialExpressionScalarParameter>(Expression)) { Out += FString::Printf(TEXT(" d=%.3f"), Scalar->DefaultValue); }
            if (const auto* Vector = Cast<UMaterialExpressionVectorParameter>(Expression)) { Out += TEXT(" d=") + Vector->DefaultValue.ToString(); }
            if (const auto* Sample = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression)) { Out += FString::Printf(TEXT(" s=%d"), int32(Sample->SamplerType)); }
            for (int32 Index = 0; ; ++Index)
            {
                const FExpressionInput* Input = const_cast<UMaterialExpression*>(Expression)->GetInput(Index);
                if (!Input) { break; }
                Out += Link(Input);
            }
            Out += TEXT("|");
        }
        for (const EMaterialProperty Property : {MP_EmissiveColor, MP_Opacity, MP_OpacityMask, MP_BaseColor})
        { Out += Link(const_cast<UMaterial&>(Material).GetExpressionInputForProperty(Property)); }
        return Out;
    }

    bool VfxCheckMaterial(const UMaterial* Material, EVfxMaterial Kind, FString& Error)
    {
        const TCHAR* PackageName = VfxPackageOf(Kind);
        const bool bShell = Kind == EVfxMaterial::Shell, bGhost = Kind == EVfxMaterial::Ghost;
        if (!Material) { return VfxFail(Error, FString(PackageName) + TEXT(" is missing; run ImportDemoArt.ps1 -Mode ImportVFX.")); }
        TArray<FName> Parameters;
        for (const UMaterialExpression* Expression : Material->GetExpressions())
        {
            if (Expression && !Expression->GetParameterName().IsNone()) { Parameters.Add(Expression->GetParameterName()); }
        }
        TArray<FName> Expected{LDHitVfx::GlowParameter, LDHitVfx::FadeParameter};
        if (!bGhost) { Expected.Add(LDHitVfx::MaskParameter); }
        if (Kind == EVfxMaterial::Layer) { Expected.Add(LDHitVfx::InkParameter); }
        Parameters.Sort(FNameLexicalLess());
        Expected.Sort(FNameLexicalLess());
        const UMaterialEditorOnlyData* Inputs = Material->GetEditorOnlyData();
        const bool bGraph = Inputs && Parameters == Expected && Material->GetExpressions().Num() == (bGhost ? 8 : bShell ? 8 : 7)
            && Inputs->EmissiveColor.IsConnected() && Inputs->Opacity.IsConnected() == !bShell
            && Material->BlendMode == (bShell ? BLEND_Additive : BLEND_Translucent) && Material->GetShadingModels().HasOnlyShadingModel(MSM_Unlit)
            && (!bGhost || Material->GetUsageByFlag(MATUSAGE_SkeletalMesh));
        // The whole graph must equal a freshly generated one: a rewired link or a changed constant fails and is rebuilt.
        if (bGraph)
        {
            UTexture2D* Star = VfxLoad<UTexture2D>(LDHitVfx::TexturePackage(LDHitVfx::MaskId(LDHitVfx::EMask::Star)));
            UMaterial* Reference = NewObject<UMaterial>(GetTransientPackage());
            FString Ignored;
            if (!Star || !VfxBuildMaterial(Kind, Star, Ignored, Reference) || VfxSignature(*Material) != VfxSignature(*Reference))
            {
                return VfxFail(Error, FString::Printf(TEXT("%s differs from the importer's graph (links, constants or settings); ")
                    TEXT("run ImportDemoArt.ps1 -Mode ImportVFX to rebuild it."), PackageName));
            }
        }
        return bGraph || VfxFail(Error, FString::Printf(TEXT("%s does not hold the importer's graph (%d expressions, %d parameters); ")
            TEXT("run ImportDemoArt.ps1 -Mode ImportVFX to rebuild it."), PackageName, Material->GetExpressions().Num(), Parameters.Num()));
    }

    // A dedicated-server load drops material graphs (see LDDemoArtImport).
    bool VfxRequireClient(FString& Error)
    {
        return GIsClient || VfxFail(Error, TEXT("Effect import and validation need a client process; a dedicated-server load drops material graphs."));
    }

    // ---------------------------------------------------------------- slice textures, meshes and material

    // Thin-line sheets keep their edges with BC7; the dissolve and chain energy noises are plain masks; the wet decal is a
    // soft BC3 mask.
    TextureCompressionSettings SliceCompression(LDSliceVfx::ETex Tex)
    {
        switch (Tex)
        {
        case LDSliceVfx::ETex::DissolveNoise: case LDSliceVfx::ETex::ChainAEnergyNoise: return TC_Masks;
        case LDSliceVfx::ETex::WetDecal: return TC_Default;
        default: return TC_BC7;
        }
    }

    void SliceApplySettings(UTexture2D& Texture, LDSliceVfx::ETex Tex)
    {
        Texture.LODGroup = TEXTUREGROUP_Effects;
        Texture.CompressionSettings = SliceCompression(Tex);
        Texture.MipGenSettings = TMGS_FromTextureGroup;
        Texture.SRGB = false;
        Texture.NeverStream = true;
    }

    bool SliceSettingsMatch(const UTexture2D& Texture, LDSliceVfx::ETex Tex)
    {
        return Texture.LODGroup == TEXTUREGROUP_Effects && Texture.CompressionSettings == SliceCompression(Tex)
            && Texture.MipGenSettings == TMGS_FromTextureGroup && !Texture.SRGB && Texture.NeverStream;
    }

    bool SliceImportedFromCurrent(const UAssetImportData* Data, const FSliceSpec& Spec)
    {
        const FMD5Hash Current = FMD5Hash::HashFile(*(VfxRoot() / Spec.File));
        return Data && !Data->SourceData.SourceFiles.IsEmpty() && Current.IsValid() && Data->SourceData.SourceFiles[0].FileHash == Current
            && FPaths::GetCleanFilename(Data->SourceData.SourceFiles[0].RelativeFilename).Equals(FPaths::GetCleanFilename(Spec.File), ESearchCase::CaseSensitive);
    }

    UTexture2D* SliceImportTexture(const FSliceSpec& Spec, FString& Error)
    {
        UPackage* Package = CreatePackage(*Spec.Package);
        Package->FullyLoad();
        auto* Factory = NewObject<UTextureFactory>(GetTransientPackage());
        Factory->AddToRoot();
        UTextureFactory::SuppressImportOverwriteDialog();
        bool bCancelled = false;
        UObject* Imported = UFactory::StaticImportObject(UTexture2D::StaticClass(), Package, FName(*FPackageName::GetShortName(Spec.Package)),
            RF_Public | RF_Standalone, bCancelled, *(VfxRoot() / Spec.File), nullptr, Factory);
        Factory->RemoveFromRoot();
        auto* Texture = Cast<UTexture2D>(Imported);
        if (!Texture || bCancelled) { VfxFail(Error, TEXT("Slice texture import failed: ") + Spec.Id); return nullptr; }
        SliceApplySettings(*Texture, Spec.Tex);
        Texture->PostEditChange();
        Texture->MarkPackageDirty();
        if (!VfxSave(*Texture, Error)) { return nullptr; }
        UE_LOG(LogLDEditor, Display, TEXT("LD_DEMO_VFX_TEXTURE_IMPORTED %s <- SourceData/VFX/%s"), *Spec.Package, *Spec.File);
        return Texture;
    }

    // The slash ribbon as Unreal reads it: the outer-edge vertex (V >= 0.95) with the lowest U is the head of the swing.
    bool SliceHeadOf(const UStaticMesh& Mesh, FVector& Out)
    {
        const FMeshDescription* Description = Mesh.GetMeshDescription(0);
        if (!Description || Description->VertexInstances().Num() == 0) { return false; }
        FStaticMeshConstAttributes Attributes(*Description);
        const auto Positions = Attributes.GetVertexPositions();
        const auto UVs = Attributes.GetVertexInstanceUVs();
        if (UVs.GetNumChannels() < 1) { return false; }
        float Best = TNumericLimits<float>::Max();
        for (const FVertexInstanceID Instance : Description->VertexInstances().GetElementIDs())
        {
            const FVector2f UV = UVs.Get(Instance, 0);
            if (UV.Y >= .95f && UV.X < Best)
            {
                Best = UV.X;
                Out = FVector(Positions[Description->GetVertexInstanceVertex(Instance)]);
            }
        }
        return Best < .05f;
    }

    // Every slash mesh must arrive in the orientation the runtime places it in (LDSliceVfx::SlashHead): forward +X, its
    // head on the side the combo stage swings to, the swing centre at the origin.
    bool SliceCheckMesh(const UStaticMesh* Mesh, const FSliceSpec& Spec, bool bSourceCheck, FString& Error)
    {
        if (!Mesh) { return VfxFail(Error, Spec.Package + TEXT(" is missing; run ImportDemoArt.ps1 -Mode ImportVFX.")); }
        if (bSourceCheck && !SliceImportedFromCurrent(Mesh->GetAssetImportData(), Spec))
        {
            return VfxFail(Error, Spec.Package + TEXT(" was not imported from the current bytes of SourceData/VFX/") + Spec.File + TEXT("; run ImportDemoArt.ps1 -Mode ImportVFX."));
        }
        FVector Head;
        if (!SliceHeadOf(*Mesh, Head)) { return VfxFail(Error, Spec.Package + TEXT(" has no slash head (outer edge vertex at U = 0) in its first UV channel.")); }
        const FVector Expected = LDSliceVfx::SlashHead(Spec.Mesh);
        if (!Head.Equals(Expected, 3.))
        {
            return VfxFail(Error, FString::Printf(TEXT("%s imported its head at %s, the runtime expects %s (forward +X, head on the swing's side)."),
                *Spec.Package, *Head.ToCompactString(), *Expected.ToCompactString()));
        }
        return true;
    }

    UStaticMesh* SliceImportMesh(const FSliceSpec& Spec, FString& Error)
    {
        UInterchangeManager& Manager = UInterchangeManager::GetInterchangeManager();
        UInterchangeSourceData* Source = UInterchangeManager::CreateSourceData(VfxRoot() / Spec.File);
        auto* Pipeline = NewObject<UInterchangeGenericAssetsPipeline>(GetTransientPackage());
        if (!Source || !Pipeline || !Pipeline->CommonMeshesProperties || !Pipeline->MeshPipeline || !Pipeline->MaterialPipeline)
        {
            VfxFail(Error, TEXT("Interchange pipeline is unavailable for ") + Spec.File);
            return nullptr;
        }
        Pipeline->bUseSourceNameForAsset = true;
        auto* Common = Pipeline->CommonMeshesProperties.Get();
        Common->ForceAllMeshAsType = EInterchangeForceMeshType::IFMT_StaticMesh;
        Common->bImportLods = false;
        Common->bBakeMeshes = true;
        Common->bRecomputeNormals = false;
        Common->VertexColorImportOption = EInterchangeVertexColorImportOption::IVCIO_Ignore;
        auto* Meshes = Pipeline->MeshPipeline.Get();
        Meshes->bImportStaticMeshes = true;
        Meshes->bImportSkeletalMeshes = false;
        Meshes->CombineStaticMeshesBehavior = EInterchangeCombineStaticMeshesBehavior::All;
        Meshes->bCollision = false;
        Meshes->bImportCollisionAccordingToMeshName = false;
        Meshes->bBuildNanite = false;
        Meshes->bGenerateLightmapUVs = false;
        Meshes->bImportMorphTargets = false;
        Meshes->bImportGeometryCaches = false;
        if (Pipeline->AnimationPipeline) { Pipeline->AnimationPipeline->bImportAnimations = false; }
        Pipeline->MaterialPipeline->bImportMaterials = false;
        if (Pipeline->MaterialPipeline->TexturePipeline) { Pipeline->MaterialPipeline->TexturePipeline->bImportTextures = false; }
        FImportAssetParameters Parameters;
        Parameters.bIsAutomated = true;
        Parameters.bReplaceExisting = true;
        Parameters.OverridePipelines.Add(FSoftObjectPath(Pipeline));
        TArray<UObject*> Imported;
        if (!Manager.ImportAsset(FString(LDSliceVfx::Folder) + TEXT("/"), Source, Parameters, Imported) || Imported.IsEmpty())
        {
            VfxFail(Error, TEXT("Interchange import failed: ") + Spec.File);
            return nullptr;
        }
        UStaticMesh* Mesh = nullptr;
        for (UObject* Object : Imported) { if (auto* Found = Cast<UStaticMesh>(Object)) { Mesh = Found; } }
        if (!Mesh || Mesh->GetOutermost()->GetName() != Spec.Package)
        {
            VfxFail(Error, FString::Printf(TEXT("%s did not import as %s (got %s)."), *Spec.File, *Spec.Package, Mesh ? *Mesh->GetOutermost()->GetName() : TEXT("no static mesh")));
            return nullptr;
        }
        Mesh->GetStaticMaterials().Reset();
        Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("Slash")));
        Mesh->PostEditChange();
        Mesh->MarkPackageDirty();
        if (!VfxSave(*Mesh, Error)) { return nullptr; }
        UE_LOG(LogLDEditor, Display, TEXT("LD_DEMO_VFX_MESH_IMPORTED %s <- SourceData/VFX/%s"), *Spec.Package, *Spec.File);
        return Mesh;
    }

    // The unit quad every slice sprite, strip and ground mark is drawn on: 100 x 100 cm in XY facing +Z, U = (X + 50) / 100
    // and V = (Y + 50) / 100, so a texture's +U runs along the quad's +X wherever it is turned.
    constexpr TCHAR SliceQuadName[] = TEXT("SM_LDSliceQuad");
    FString SliceQuadPackage() { return FString(LDSliceVfx::Folder) / SliceQuadName; }

    bool SliceQuadMatches(const UStaticMesh& Mesh)
    {
        const FMeshDescription* Description = Mesh.GetMeshDescription(0);
        if (!Description || Description->Vertices().Num() != 4 || Description->Triangles().Num() != 2) { return false; }
        FStaticMeshConstAttributes Attributes(*Description);
        const auto Positions = Attributes.GetVertexPositions();
        const auto UVs = Attributes.GetVertexInstanceUVs();
        for (const FVertexInstanceID Instance : Description->VertexInstances().GetElementIDs())
        {
            const FVector3f P = Positions[Description->GetVertexInstanceVertex(Instance)];
            const FVector2f UV = UVs.Get(Instance, 0);
            if (FMath::Abs(FMath::Abs(P.X) - 50.f) > .01f || FMath::Abs(FMath::Abs(P.Y) - 50.f) > .01f || P.Z != 0.f
                || !FMath::IsNearlyEqual(UV.X, (P.X + 50.f) / 100.f) || !FMath::IsNearlyEqual(UV.Y, (P.Y + 50.f) / 100.f))
            { return false; }
        }
        return true;
    }

    UStaticMesh* SliceBuildQuad(FString& Error)
    {
        const FString PackageName = SliceQuadPackage();
        UStaticMesh* Mesh = VfxLoad<UStaticMesh>(PackageName);
        if (Mesh && SliceQuadMatches(*Mesh)) { return Mesh; }
        if (!Mesh) { Mesh = NewObject<UStaticMesh>(CreatePackage(*PackageName), SliceQuadName, RF_Public | RF_Standalone); }
        Mesh->GetOutermost()->FullyLoad();
        Mesh->SetNumSourceModels(1);
        FMeshDescription Description;
        FStaticMeshAttributes Attributes(Description);
        Attributes.Register();
        auto Positions = Attributes.GetVertexPositions();
        auto UVs = Attributes.GetVertexInstanceUVs();
        UVs.SetNumChannels(1);
        const FPolygonGroupID Group = Description.CreatePolygonGroup();
        Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Slice");
        const FVector3f Corners[] = { {-50.f, -50.f, 0.f}, {50.f, -50.f, 0.f}, {50.f, 50.f, 0.f}, {-50.f, 50.f, 0.f} };
        TArray<FVertexID> Vertices;
        for (const FVector3f& Corner : Corners)
        {
            const FVertexID Vertex = Description.CreateVertex();
            Positions[Vertex] = Corner;
            Vertices.Add(Vertex);
        }
        // Unreal reads a triangle's front from Cross(P2 - P0, P1 - P0): listed this way both triangles face +Z.
        for (const TArray<int32>& Triangle : { TArray<int32>{0, 2, 1}, TArray<int32>{0, 3, 2} })
        {
            TArray<FVertexInstanceID> Instances;
            for (const int32 Index : Triangle)
            {
                const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertices[Index]);
                UVs.Set(Instance, 0, FVector2f((Corners[Index].X + 50.f) / 100.f, (Corners[Index].Y + 50.f) / 100.f));
                Instances.Add(Instance);
            }
            Description.CreatePolygon(Group, Instances);
        }
        auto& Settings = Mesh->GetSourceModel(0).BuildSettings;
        Settings.bRecomputeNormals = true;
        Settings.bRecomputeTangents = true;
        Mesh->CreateMeshDescription(0, MoveTemp(Description));
        Mesh->CommitMeshDescription(0);
        Mesh->GetStaticMaterials().Reset();
        Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("Slice")));
        Mesh->Build(false);
        Mesh->PostEditChange();
        Mesh->MarkPackageDirty();
        if (!VfxSave(*Mesh, Error)) { return nullptr; }
        UE_LOG(LogLDEditor, Display, TEXT("LD_DEMO_VFX_MESH_BUILT %s"), *PackageName);
        return Mesh;
    }

    // The slice material: one Custom node reads the atlas cell (flipbook frame or strip variant: Flip = columns, rows,
    // frame, U repeats; Shape = U window start and end, dissolve, U scroll; U repeats and scroll
    // with gradients taken before the wrap so mips stay continuous), remaps the glow (ink 0, body 0.6, core 1) and
    // multiplies the coverage by Fade, a soft U window and a noise erosion.
    const TCHAR* SliceCode()
    {
        return TEXT("float Cols = max(Flip.x, 1.0);\r\n")
            TEXT("float Rows = max(Flip.y, 1.0);\r\n")
            TEXT("float Frame = floor(Flip.z + 0.5);\r\n")
            TEXT("float Row = floor(Frame / Cols);\r\n")
            TEXT("float Col = Frame - Cols * Row;\r\n")
            TEXT("float2 Along = float2(UV.x * max(FlipW, 0.001) + ShapeW, UV.y);\r\n")
            TEXT("float2 Cell = float2((frac(Along.x) + Col) / Cols, (saturate(Along.y) + Row) / Rows);\r\n")
            TEXT("float2 Scale = float2(1.0 / Cols, 1.0 / Rows);\r\n")
            TEXT("float4 M = Texture2DSampleGrad(Mask, MaskSampler, Cell, ddx(Along) * Scale, ddy(Along) * Scale);\r\n")
            TEXT("float N = Texture2DSample(Noise, NoiseSampler, UV * float2(2.0, 1.0)).r;\r\n")
            TEXT("float Window = saturate((UV.x - Shape.x) * 24.0) * saturate((Shape.y - UV.x) * 24.0);\r\n")
            TEXT("float Erode = saturate((N - Shape.z * 1.25 + 0.25) * 5.0);\r\n")
            TEXT("float G = M.r;\r\n")
            TEXT("float3 Body = Glow * saturate(G / 0.6);\r\n")
            TEXT("float3 Hot = lerp(Body, Core, smoothstep(0.7, 1.0, G));\r\n")
            TEXT("float3 Colour = lerp(Ink, Hot, saturate(G * 5.0));\r\n")
            TEXT("return float4(Colour, M.a * Fade * Window * Erode);");
    }

    UMaterial* SliceBuildMaterial(UTexture2D* DefaultMask, UTexture2D* DefaultNoise, FString& Error, UMaterial* Scratch = nullptr)
    {
        const FString Name = FPackageName::GetShortName(LDSliceVfx::Material);
        UMaterial* Material = Scratch ? Scratch : VfxLoad<UMaterial>(LDSliceVfx::Material);
        if (!Material) { Material = NewObject<UMaterial>(CreatePackage(LDSliceVfx::Material), *Name, RF_Public | RF_Standalone); }
        if (!Material || !DefaultMask || !DefaultNoise) { VfxFail(Error, FString(TEXT("Cannot create the slice material ")) + LDSliceVfx::Material); return nullptr; }
        if (!Scratch) { Material->PreEditChange(nullptr); }
        FMaterialExpressionCollection& Collection = Material->GetExpressionCollection();
        TArray<UObject*> Previous(Collection.Expressions);
        Previous.Append(Collection.EditorComments);
        for (UObject* Old : Previous) { if (Old) { Old->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional); } }
        Collection.Empty();
        for (int32 Property = 0; Property < MP_MAX; ++Property)
        {
            if (FExpressionInput* Input = Material->GetExpressionInputForProperty(EMaterialProperty(Property))) { Input->Expression = nullptr; }
        }
        UMaterialEditorOnlyData* Inputs = Material->GetEditorOnlyData();
        auto* UV = VfxAdd<UMaterialExpressionTextureCoordinate>(*Material);
        auto* Mask = VfxAdd<UMaterialExpressionTextureObjectParameter>(*Material);
        Mask->ParameterName = LDSliceVfx::MaskParameter;
        Mask->Texture = DefaultMask;
        Mask->SamplerType = SAMPLERTYPE_LinearColor;
        auto* Noise = VfxAdd<UMaterialExpressionTextureObjectParameter>(*Material);
        Noise->ParameterName = LDSliceVfx::NoiseParameter;
        Noise->Texture = DefaultNoise;
        Noise->SamplerType = SAMPLERTYPE_Masks;
        const auto Vector = [Material](const TCHAR* Parameter, const FLinearColor& Default)
        {
            auto* Expression = VfxAdd<UMaterialExpressionVectorParameter>(*Material);
            Expression->ParameterName = Parameter;
            Expression->DefaultValue = Default;
            return Expression;
        };
        auto* Glow = Vector(LDSliceVfx::GlowParameter, FLinearColor::White);
        auto* Ink = Vector(LDSliceVfx::InkParameter, FLinearColor::Black);
        auto* Core = Vector(LDSliceVfx::CoreParameter, FLinearColor::White);
        auto* Flip = Vector(LDSliceVfx::FlipParameter, FLinearColor(1.f, 1.f, 0.f, 1.f));
        auto* Shape = Vector(LDSliceVfx::ShapeParameter, FLinearColor(-1.f, 2.f, 0.f, 0.f));
        auto* Fade = VfxAdd<UMaterialExpressionScalarParameter>(*Material);
        Fade->ParameterName = LDSliceVfx::FadeParameter;
        Fade->DefaultValue = 1.f;
        auto* Custom = VfxAdd<UMaterialExpressionCustom>(*Material);
        Custom->OutputType = CMOT_Float4;
        Custom->Description = TEXT("LDSlice");
        Custom->Code = SliceCode();
        Custom->Inputs.Reset();
        const auto Bind = [Custom](const TCHAR* Name, UMaterialExpression* Source, int32 Output)
        {
            FCustomInput& Input = Custom->Inputs.AddDefaulted_GetRef();
            Input.InputName = Name;
            Input.Input.Connect(Output, Source);
        };
        Bind(TEXT("UV"), UV, 0);
        Bind(TEXT("Mask"), Mask, 0);
        Bind(TEXT("Noise"), Noise, 0);
        Bind(TEXT("Glow"), Glow, 0);
        Bind(TEXT("Ink"), Ink, 0);
        Bind(TEXT("Core"), Core, 0);
        // A vector parameter's first output is its RGB; the fourth channel (output 4) comes in on its own.
        Bind(TEXT("Flip"), Flip, 0);
        Bind(TEXT("FlipW"), Flip, 4);
        Bind(TEXT("Shape"), Shape, 0);
        Bind(TEXT("ShapeW"), Shape, 4);
        Bind(TEXT("Fade"), Fade, 0);
        auto* Colour = VfxAdd<UMaterialExpressionComponentMask>(*Material);
        Colour->Input.Connect(0, Custom);
        Colour->R = Colour->G = Colour->B = 1;
        Colour->A = 0;
        auto* Coverage = VfxAdd<UMaterialExpressionComponentMask>(*Material);
        Coverage->Input.Connect(0, Custom);
        Coverage->R = Coverage->G = Coverage->B = 0;
        Coverage->A = 1;
        Inputs->EmissiveColor.Connect(0, Colour);
        Inputs->Opacity.Connect(0, Coverage);
        Material->BlendMode = BLEND_Translucent;
        Material->TwoSided = true;
        Material->SetShadingModel(MSM_Unlit);
        Material->bUseEmissiveForDynamicAreaLighting = false;
        Material->SetUsageByFlag(MATUSAGE_StaticLighting, false);
        const uint32 Seed = FCrc::StrCrc32(LDSliceVfx::Material);
        for (int32 Index = 0; Index < Collection.Expressions.Num(); ++Index)
        {
            UMaterialExpression* Expression = Collection.Expressions[Index];
            Expression->MaterialExpressionGuid = FGuid(Seed, uint32(Index), 0x4C44u, 3u);
            if (Expression->HasAParameterName()) { Expression->GetParameterExpressionId() = FGuid(Seed, uint32(Index), 0x4C44u, 4u); }
        }
        if (Scratch) { return Material; }
        Material->PostEditChange();
        Material->MarkPackageDirty();
        return Material;
    }

    FString SliceSignature(const UMaterial& Material)
    {
        FString Out = VfxSignature(Material);
        for (const UMaterialExpression* Expression : Material.GetExpressions())
        {
            if (const auto* Custom = Cast<UMaterialExpressionCustom>(Expression))
            {
                Out += FString::Printf(TEXT("|code=%08x out=%d"), FCrc::StrCrc32(*Custom->Code), int32(Custom->OutputType));
                for (const FCustomInput& Input : Custom->Inputs) { Out += TEXT(" ") + Input.InputName.ToString(); }
            }
            if (const auto* Mask = Cast<UMaterialExpressionComponentMask>(Expression))
            { Out += FString::Printf(TEXT("|mask=%d%d%d%d"), Mask->R, Mask->G, Mask->B, Mask->A); }
            if (const auto* Object = Cast<UMaterialExpressionTextureObjectParameter>(Expression))
            { Out += FString::Printf(TEXT("|object=%s s=%d"), *Object->GetParameterName().ToString(), int32(Object->SamplerType)); }
        }
        return Out;
    }

    bool SliceCheckMaterial(const UMaterial* Material, UTexture2D* DefaultMask, UTexture2D* DefaultNoise, FString& Error)
    {
        if (!Material) { return VfxFail(Error, FString(LDSliceVfx::Material) + TEXT(" is missing; run ImportDemoArt.ps1 -Mode ImportVFX.")); }
        UMaterial* Reference = NewObject<UMaterial>(GetTransientPackage());
        FString Ignored;
        if (!DefaultMask || !DefaultNoise || !SliceBuildMaterial(DefaultMask, DefaultNoise, Ignored, Reference)
            || SliceSignature(*Material) != SliceSignature(*Reference) || Material->BlendMode != BLEND_Translucent
            || !Material->GetShadingModels().HasOnlyShadingModel(MSM_Unlit) || !Material->TwoSided)
        {
            return VfxFail(Error, FString(LDSliceVfx::Material) + TEXT(" differs from the importer's graph; run ImportDemoArt.ps1 -Mode ImportVFX to rebuild it."));
        }
        return true;
    }

    // Imports or checks every slice asset; with bImport an asset that is missing, unsettled or from older bytes is
    // (re)imported and the material and quad are rebuilt when their graph or geometry differs.
    bool SliceSync(const TArray<FSliceSpec>& Specs, bool bImport, FString& Error)
    {
        UTexture2D* DefaultMask = nullptr;
        UTexture2D* DefaultNoise = nullptr;
        for (const FSliceSpec& Spec : Specs)
        {
            if (Spec.bMesh)
            {
                UStaticMesh* Mesh = VfxLoad<UStaticMesh>(Spec.Package);
                FString Why;
                if (!SliceCheckMesh(Mesh, Spec, true, Why))
                {
                    if (!bImport) { return VfxFail(Error, Why); }
                    Mesh = SliceImportMesh(Spec, Error);
                    if (!Mesh || !SliceCheckMesh(Mesh, Spec, true, Error)) { return false; }
                }
                continue;
            }
            UTexture2D* Texture = VfxLoad<UTexture2D>(Spec.Package);
            const bool bCurrent = Texture && SliceSettingsMatch(*Texture, Spec.Tex) && Texture->Source.GetSizeX() == Spec.Width
                && Texture->Source.GetSizeY() == Spec.Height && SliceImportedFromCurrent(Texture->AssetImportData, Spec);
            if (!bCurrent)
            {
                if (!bImport)
                {
                    return VfxFail(Error, Spec.Package + TEXT(" is missing, lost its slice settings or was not imported from the current bytes of SourceData/VFX/")
                        + Spec.File + TEXT("; run ImportDemoArt.ps1 -Mode ImportVFX."));
                }
                Texture = SliceImportTexture(Spec, Error);
                if (!Texture) { return false; }
            }
            if (Spec.Tex == LDSliceVfx::ETex::HitSkill) { DefaultMask = Texture; }
            if (Spec.Tex == LDSliceVfx::ETex::DissolveNoise) { DefaultNoise = Texture; }
        }
        UStaticMesh* Quad = VfxLoad<UStaticMesh>(SliceQuadPackage());
        if (!Quad || !SliceQuadMatches(*Quad))
        {
            if (!bImport) { return VfxFail(Error, SliceQuadPackage() + TEXT(" is missing or not the importer's quad; run ImportDemoArt.ps1 -Mode ImportVFX.")); }
            if (!SliceBuildQuad(Error)) { return false; }
        }
        FString Why;
        if (!SliceCheckMaterial(VfxLoad<UMaterial>(LDSliceVfx::Material), DefaultMask, DefaultNoise, Why))
        {
            if (!bImport) { return VfxFail(Error, Why); }
            UMaterial* Material = SliceBuildMaterial(DefaultMask, DefaultNoise, Error);
            if (!Material || !SliceCheckMaterial(Material, DefaultMask, DefaultNoise, Error) || !VfxSave(*Material, Error)) { return false; }
            UE_LOG(LogLDEditor, Display, TEXT("LD_DEMO_VFX_MATERIAL_BUILT %s"), LDSliceVfx::Material);
        }
        return true;
    }
}

bool LDDemoVfxImport::Import(FString& Error)
{
    TArray<FVfxSpec> Specs;
    TArray<FSliceSpec> Slices;
    if (!VfxRequireClient(Error) || !VfxLoadAll(Specs, Slices, Error)) { return false; }
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
    for (const EVfxMaterial Kind : {EVfxMaterial::Layer, EVfxMaterial::Shell, EVfxMaterial::Ghost})
    {
        // A material already holding the importer's graph is left as it is, so a re-import does not churn its bytes.
        FString Ignored;
        if (VfxCheckMaterial(VfxLoad<UMaterial>(VfxPackageOf(Kind)), Kind, Ignored)) { continue; }
        UMaterial* Material = VfxBuildMaterial(Kind, Star, Error);
        if (!Material || !VfxCheckMaterial(Material, Kind, Error) || !VfxSave(*Material, Error)) { return false; }
        UE_LOG(LogLDEditor, Display, TEXT("LD_DEMO_VFX_MATERIAL_BUILT %s"), VfxPackageOf(Kind));
    }
    if (!SliceSync(Slices, true, Error) || !LDDemoVfxNiagara::Build(Error)) { return false; }
    Error.Reset();
    return true;
}

bool LDDemoVfxImport::Validate(FString& Error)
{
    TArray<FVfxSpec> Specs;
    TArray<FSliceSpec> Slices;
    if (!VfxRequireClient(Error) || !VfxLoadAll(Specs, Slices, Error)) { return false; }
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
    for (const EVfxMaterial Kind : {EVfxMaterial::Layer, EVfxMaterial::Shell, EVfxMaterial::Ghost})
    {
        if (!VfxCheckMaterial(VfxLoad<UMaterial>(VfxPackageOf(Kind)), Kind, Error)) { return false; }
    }
    return SliceSync(Slices, false, Error) && LDDemoVfxNiagara::Validate(Error);
}
