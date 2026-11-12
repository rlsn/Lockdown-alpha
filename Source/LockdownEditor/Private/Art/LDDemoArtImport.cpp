#include "LDDemoArtImport.h"
#include "LDEditorLog.h"
#include "Art/LDArtCatalog.h"
#include "Art/LDArtPlacement.h"
#include "Art/LDStaticScene.h"
#include "Engine/World.h"
#include "Animation/AnimSequence.h"
#include "AnimPose.h"
#include "Animation/Skeleton.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Development/LDDemoContentIds.h"
#include "Dom/JsonObject.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Factories/TextureFactory.h"
#include "Framework/LDDemoEntry.h"
#include "HAL/FileManager.h"
#include "InterchangeGenericAnimationPipeline.h"
#include "InterchangeGenericAssetsPipeline.h"
#include "InterchangeGenericAssetsPipelineSharedSettings.h"
#include "InterchangeGenericMaterialPipeline.h"
#include "InterchangeGenericMeshPipeline.h"
#include "InterchangeGenericTexturePipeline.h"
#include "InterchangeManager.h"
#include "InterchangeSourceData.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionMax.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "PhysicsEngine/BodySetup.h"
#include "Region/LDMapGeometry.h"
#include "ReferenceSkeleton.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
    const TCHAR* const ArtCatalogPackage = TEXT("/Game/Lockdown/Art/DA_DemoArt");
    const TCHAR* const ArtMasterPackage = TEXT("/Game/Lockdown/Art/Materials/M_ArtSurface");
    const TCHAR* const ArtFadeMasterPackage = TEXT("/Game/Lockdown/Art/Materials/M_ArtSurfaceFade");
    const TCHAR* const ArtMaskedMasterPackage = TEXT("/Game/Lockdown/Art/Materials/M_ArtSurfaceMasked");
    const TCHAR* const ArtTextureRoot = TEXT("/Game/Lockdown/Art/Textures/");
    const TCHAR* const ArtCharacterRoot = TEXT("/Game/Lockdown/Art/Characters/");
    const TCHAR* const ArtPropRoot = TEXT("/Game/Lockdown/Art/Props/");
    const TCHAR* const ArtTextureParameter = TEXT("BaseTexture");
    // Masked surfaces: a pixel stays where the base image's alpha reaches the source material's glTF alphaCutoff.
    const TCHAR* const ArtAlphaCutoffParameter = TEXT("AlphaCutoff");
    // glTF sampler magFilter LINEAR: a painted image sampled smoothly (#97 foliage and bark). Without it the image is a
    // palette atlas whose swatches must stay flat (Kenney kits declare NEAREST or leave the filter unset).
    constexpr int32 ArtGltfLinear = 9729;

    // The three art master materials: opaque, opaque with the camera fade dither, and alpha-cut (glTF MASK, #97 foliage).
    enum class EArtSurface : uint8 { Opaque, Fade, Masked };

    const TCHAR* ArtMasterPackageOf(EArtSurface Surface)
    {
        return Surface == EArtSurface::Fade ? ArtFadeMasterPackage : Surface == EArtSurface::Masked ? ArtMaskedMasterPackage : ArtMasterPackage;
    }
    // Vertices within this height of the lowest point form the footprint whose centre becomes a prop's pivot.
    constexpr double ArtFootprintSliceCm = 5.;

    struct FArtClipSpec
    {
        ELDArtAnim State = ELDArtAnim::Count;
        // glTF bodies: Clip is the clip name in the source file. Authored bodies: Clip is the provenance text and Asset
        // the animation asset in the body folder.
        FString Clip, Asset, Substitute;
        float PlayRate = 1.f, ImpactSec = 0.f;
    };

    // A worn part of an authored body (FLDArtAttachment): an asset in the body folder.
    struct FArtAttachmentSpec
    {
        FString Id, Asset;
        bool bSkinned = false;
        FName Socket;
        TArray<FName> TintSlots;
    };

    struct FArtCharacterSpec
    {
        // Source: the glTF of an imported body; for an authored body the listed file its mesh was imported from, or
        // empty when that file is not in the repository (the pack records where it lives).
        FString Id, Source;
        // Authored bodies (finished assets in /Game/Lockdown/Art/Characters/<id>): their pack and mesh asset name.
        bool bAuthored = false;
        FString Pack, Mesh;
        FName HipBone, FootBone;
        float HeightCm = 0.f, RunSpeedCmPerSec = 0.f;
        FName HandBone;
        TArray<FName> ForwardBones, TintSlots;
        TMap<FString, FLinearColor> Colors;
        TArray<FArtClipSpec> Clips;
        TArray<FArtAttachmentSpec> Attachments;
    };

    struct FArtPropSpec
    {
        FString Id, Source;
        float Scale = 1.f;
        bool bFadeable = false;
    };

    struct FArtManifest
    {
        TArray<FArtCharacterSpec> Characters;
        TArray<FString> Seats;
        TArray<TPair<uint8, FString>> Monsters;
        TArray<FArtPropSpec> Props;
        TSet<FString> Files, Packs;
        FString AirdropProp;
    };

    // Colour and base-colour image of one source material, read from the glTF itself: its alpha mode and cutoff, and
    // whether its image is sampled smoothly (mips, linear) or as a flat-swatch palette.
    struct FArtSourceMaterial
    {
        FLinearColor Color = FLinearColor::White;
        FString Image;
        bool bMasked = false, bSmooth = false;
        float AlphaCutoff = .5f;
    };

    bool ArtFail(FString& Error, const FString& Message) { Error = Message; return false; }

    FString ArtSourceRoot() { return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("SourceData"), TEXT("Art"))); }

    FString ArtAbsolute(const FString& Relative) { return FPaths::Combine(ArtSourceRoot(), Relative); }

    bool ArtSafeRelative(const FString& Path)
    {
        return !Path.IsEmpty() && !Path.Contains(TEXT("\\")) && !Path.Contains(TEXT("..")) && !Path.StartsWith(TEXT("/")) && !Path.Contains(TEXT(":"));
    }

    // Stable asset name from a manifest path: letters and digits, everything else an underscore.
    FString ArtAssetName(const FString& Text)
    {
        FString Out;
        for (const TCHAR Char : FPaths::ChangeExtension(Text, TEXT("")))
        {
            Out.AppendChar(FChar::IsAlnum(Char) ? Char : TCHAR('_'));
        }
        return Out;
    }

    // An asset short name in a body folder: letters, digits and underscores.
    bool ArtIsAssetName(const FString& Name)
    {
        if (Name.IsEmpty() || Name.Len() > 96) { return false; }
        for (const TCHAR Char : Name)
        {
            if (!FChar::IsAlnum(Char) && Char != TCHAR('_')) { return false; }
        }
        return true;
    }

    bool ArtCheckHash(const FString& Relative, const FString& Sha256, FString& Error)
    {
        FString HashError;
        return LDDemoEntry::VerifyFileHash(ArtAbsolute(Relative), Sha256, HashError)
            || ArtFail(Error, FString::Printf(TEXT("%s is missing or differs from its manifest SHA-256."), *Relative));
    }

    bool ArtReadColor(const TSharedPtr<FJsonValue>& Value, FLinearColor& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
        if (!Value.IsValid() || !Value->TryGetArray(Items) || Items->Num() < 3) { return false; }
        double Channel[3];
        for (int32 Index = 0; Index < 3; ++Index)
        {
            if (!(*Items)[Index]->TryGetNumber(Channel[Index]) || !FMath::IsFinite(Channel[Index]) || Channel[Index] < 0. || Channel[Index] > 1.) { return false; }
        }
        Out = FLinearColor(float(Channel[0]), float(Channel[1]), float(Channel[2]));
        return true;
    }

    bool ArtNumberField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double Min, double Max, float& Out)
    {
        double Value = 0.;
        if (!Object->TryGetNumberField(Field, Value) || !FMath::IsFinite(Value) || Value < Min || Value > Max) { return false; }
        Out = float(Value);
        return true;
    }

    bool ArtNameArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, TArray<FName>& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
        if (!Object->TryGetArrayField(Field, Items) || Items->IsEmpty()) { return false; }
        for (const TSharedPtr<FJsonValue>& Item : *Items)
        {
            FString Text;
            if (!Item->TryGetString(Text) || Text.IsEmpty()) { return false; }
            Out.Add(FName(*Text));
        }
        return true;
    }
}

namespace
{
    // CC0 packs prove their dedication by their own license text. Project-owned or otherwise licensed sources (approved
    // by the project owner on 2026-11-11) quote their declared license verbatim in their license file.
    bool ArtLoadPacks(const TSharedPtr<FJsonObject>& Json, TSet<FString>& PackIds, TSet<FString>& Listed, FString& Error)
    {
        const TArray<TSharedPtr<FJsonValue>>* Packs = nullptr;
        if (!Json->TryGetArrayField(TEXT("packs"), Packs) || Packs->IsEmpty()) { return ArtFail(Error, TEXT("art_assets.json lists no packs.")); }
        for (const TSharedPtr<FJsonValue>& Value : *Packs)
        {
            const TSharedPtr<FJsonObject>* Pack = nullptr;
            FString Id, License, LicenseFile, LicenseHash, Url, Page, Date;
            if (!Value->TryGetObject(Pack) || !(*Pack)->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty() || PackIds.Contains(Id)
                || !(*Pack)->TryGetStringField(TEXT("license"), License) || !(*Pack)->TryGetStringField(TEXT("download_url"), Url)
                || !(*Pack)->TryGetStringField(TEXT("page_url"), Page) || !(*Pack)->TryGetStringField(TEXT("downloaded"), Date)
                || !(*Pack)->TryGetStringField(TEXT("license_file"), LicenseFile) || !(*Pack)->TryGetStringField(TEXT("license_sha256"), LicenseHash)
                || !ArtSafeRelative(LicenseFile) || License.IsEmpty() || Date.IsEmpty())
            {
                return ArtFail(Error, TEXT("Every art pack needs a unique id, page and download URLs, a date, license, license_file and license_sha256."));
            }
            const bool bCC0 = License.Contains(TEXT("CC0"));
            if (bCC0 && (!Url.StartsWith(TEXT("https://")) || !Page.StartsWith(TEXT("https://"))))
            {
                return ArtFail(Error, FString::Printf(TEXT("CC0 pack %s needs https page and download URLs."), *Id));
            }
            if (!ArtCheckHash(LicenseFile, LicenseHash, Error)) { return false; }
            FString LicenseText;
            FFileHelper::LoadFileToString(LicenseText, *ArtAbsolute(LicenseFile));
            if (bCC0 ? !LicenseText.Contains(TEXT("CC0")) && !LicenseText.Contains(TEXT("Creative Commons Zero")) : !LicenseText.Contains(License))
            {
                return ArtFail(Error, FString::Printf(TEXT("The license file of pack %s does not state its declared license: %s"), *Id, *License));
            }
            PackIds.Add(Id);
            Listed.Add(LicenseFile);
        }
        return true;
    }

    // Model and image sources; anything else under SourceData\Art (besides license files) is rejected.
    bool ArtListedExtension(const FString& Path)
    {
        static const TCHAR* const Allowed[] = { TEXT("gltf"), TEXT("glb"), TEXT("bin"), TEXT("png"), TEXT("jpg"), TEXT("jpeg"), TEXT("fbx") };
        const FString Extension = FPaths::GetExtension(Path).ToLower();
        for (const TCHAR* Candidate : Allowed)
        {
            if (Extension == Candidate) { return true; }
        }
        return false;
    }

    bool ArtLoadFiles(const TSharedPtr<FJsonObject>& Json, const TSet<FString>& PackIds, TSet<FString>& Listed, FArtManifest& Out, FString& Error)
    {
        const TArray<TSharedPtr<FJsonValue>>* Files = nullptr;
        if (!Json->TryGetArrayField(TEXT("files"), Files) || Files->IsEmpty()) { return ArtFail(Error, TEXT("art_assets.json lists no files.")); }
        for (const TSharedPtr<FJsonValue>& Value : *Files)
        {
            const TSharedPtr<FJsonObject>* File = nullptr;
            FString Path, Pack, PackPath, Hash, Purpose;
            if (!Value->TryGetObject(File) || !(*File)->TryGetStringField(TEXT("path"), Path) || !ArtSafeRelative(Path) || Listed.Contains(Path)
                || !(*File)->TryGetStringField(TEXT("pack"), Pack) || !PackIds.Contains(Pack) || !(*File)->TryGetStringField(TEXT("pack_path"), PackPath)
                || PackPath.IsEmpty() || !(*File)->TryGetStringField(TEXT("sha256"), Hash) || !(*File)->TryGetStringField(TEXT("purpose"), Purpose) || Purpose.IsEmpty()
                || !ArtListedExtension(Path))
            {
                return ArtFail(Error, TEXT("Every art file needs a unique model or image source path (glTF, FBX, PNG, JPEG), a declared pack, its pack_path, sha256 and purpose: ") + Path);
            }
            if (!ArtCheckHash(Path, Hash, Error)) { return false; }
            Listed.Add(Path);
            Out.Files.Add(Path);
        }
        return true;
    }

    bool ArtLoadClips(const TSharedPtr<FJsonObject>& Object, bool bAuthored, FArtCharacterSpec& Spec, FString& Error)
    {
        const TArray<TSharedPtr<FJsonValue>>* Clips = nullptr;
        if (!Object->TryGetArrayField(TEXT("animations"), Clips)) { return ArtFail(Error, FString::Printf(TEXT("Character %s lists no animations."), *Spec.Id)); }
        for (int32 Index = 0; Index < Clips->Num(); ++Index)
        {
            const TSharedPtr<FJsonObject>* Clip = nullptr;
            FArtClipSpec ClipSpec;
            FString State;
            const TCHAR* const ClipField = bAuthored ? TEXT("origin") : TEXT("clip");
            if (!(*Clips)[Index]->TryGetObject(Clip) || !(*Clip)->TryGetStringField(TEXT("state"), State) || !LDArt::ParseAnimId(State, ClipSpec.State)
                || ClipSpec.State != ELDArtAnim(Index) || !(*Clip)->TryGetStringField(ClipField, ClipSpec.Clip) || ClipSpec.Clip.IsEmpty()
                || (bAuthored && (!(*Clip)->TryGetStringField(TEXT("asset"), ClipSpec.Asset) || !ArtIsAssetName(ClipSpec.Asset)))
                || !ArtNumberField(*Clip, TEXT("play_rate"), .1, 10., ClipSpec.PlayRate))
            {
                return ArtFail(Error, FString::Printf(TEXT("Character %s animation %d must be state %s with %s and a play_rate%s."), *Spec.Id, Index,
                    *LDArt::AnimId(ELDArtAnim(Index)), ClipField, bAuthored ? TEXT(" and an asset name") : TEXT("")));
            }
            if (LDArt::HasImpact(ClipSpec.State) && !ArtNumberField(*Clip, TEXT("impact_sec"), .001, 30., ClipSpec.ImpactSec))
            {
                return ArtFail(Error, FString::Printf(TEXT("Character %s %s needs impact_sec."), *Spec.Id, *State));
            }
            (*Clip)->TryGetStringField(TEXT("substitute"), ClipSpec.Substitute);
            Spec.Clips.Add(MoveTemp(ClipSpec));
        }
        return true;
    }

    bool ArtLoadAttachments(const TSharedPtr<FJsonObject>& Object, FArtCharacterSpec& Spec, FString& Error)
    {
        const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
        if (!Object->TryGetArrayField(TEXT("attachments"), Parts)) { return true; }
        for (const TSharedPtr<FJsonValue>& Value : *Parts)
        {
            const TSharedPtr<FJsonObject>* Part = nullptr;
            FArtAttachmentSpec Attachment;
            FString Kind, Socket;
            if (!Value->TryGetObject(Part) || !(*Part)->TryGetStringField(TEXT("id"), Attachment.Id) || !LDArt::IsValidId(Attachment.Id)
                || !(*Part)->TryGetStringField(TEXT("asset"), Attachment.Asset) || !ArtIsAssetName(Attachment.Asset)
                || !(*Part)->TryGetStringField(TEXT("kind"), Kind) || !(Kind == TEXT("static") || Kind == TEXT("skinned")))
            {
                return ArtFail(Error, FString::Printf(TEXT("Character %s attachments need an id, an asset name and kind static or skinned."), *Spec.Id));
            }
            Attachment.bSkinned = Kind == TEXT("skinned");
            if ((*Part)->TryGetStringField(TEXT("socket"), Socket) && !Socket.IsEmpty()) { Attachment.Socket = FName(*Socket); }
            if ((*Part)->HasField(TEXT("tint_slots")) && !ArtNameArray(*Part, TEXT("tint_slots"), Attachment.TintSlots))
            {
                return ArtFail(Error, FString::Printf(TEXT("Character %s part %s tint_slots must name material slots."), *Spec.Id, *Attachment.Id));
            }
            Spec.Attachments.Add(MoveTemp(Attachment));
        }
        return true;
    }

    bool ArtLoadCharacters(const TSharedPtr<FJsonObject>& Json, FArtManifest& Out, FString& Error)
    {
        const TArray<TSharedPtr<FJsonValue>>* Characters = nullptr;
        if (!Json->TryGetArrayField(TEXT("characters"), Characters) || Characters->IsEmpty()) { return ArtFail(Error, TEXT("art_assets.json lists no characters.")); }
        for (const TSharedPtr<FJsonValue>& Value : *Characters)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            FArtCharacterSpec Spec;
            FString Hand, Kind, Hip, Foot;
            if (!Value->TryGetObject(Object) || !(*Object)->TryGetStringField(TEXT("id"), Spec.Id) || !LDArt::IsValidId(Spec.Id)
                || Out.Characters.ContainsByPredicate([&Spec](const FArtCharacterSpec& Other) { return Other.Id == Spec.Id; }))
            {
                return ArtFail(Error, TEXT("Every character needs a unique lowercase id."));
            }
            (*Object)->TryGetStringField(TEXT("kind"), Kind);
            Spec.bAuthored = Kind == TEXT("authored");
            if (!Kind.IsEmpty() && !Spec.bAuthored) { return ArtFail(Error, FString::Printf(TEXT("Character %s kind must be authored or absent (glTF)."), *Spec.Id)); }
            const bool bSource = (*Object)->TryGetStringField(TEXT("source"), Spec.Source) && !Spec.Source.IsEmpty();
            if (Spec.bAuthored
                ? (!(*Object)->TryGetStringField(TEXT("pack"), Spec.Pack) || !Out.Packs.Contains(Spec.Pack) || !(*Object)->TryGetStringField(TEXT("mesh"), Spec.Mesh)
                    || !ArtIsAssetName(Spec.Mesh) || (bSource && !Out.Files.Contains(Spec.Source)) || !(*Object)->TryGetStringField(TEXT("hip_bone"), Hip) || Hip.IsEmpty()
                    || !(*Object)->TryGetStringField(TEXT("foot_bone"), Foot) || Foot.IsEmpty())
                : (!bSource || !Out.Files.Contains(Spec.Source)))
            {
                return ArtFail(Error, FString::Printf(TEXT("Character %s needs a listed glTF source, or as an authored body a declared pack, a mesh asset name, hip_bone, foot_bone and at most a listed source."), *Spec.Id));
            }
            const TArray<TSharedPtr<FJsonValue>>* Tints = nullptr;
            if (!ArtNumberField(*Object, TEXT("height_cm"), 120., 260., Spec.HeightCm)
                || !ArtNumberField(*Object, TEXT("run_speed_cm_per_sec"), 50., 2000., Spec.RunSpeedCmPerSec)
                || !(*Object)->TryGetStringField(TEXT("hand_bone"), Hand) || Hand.IsEmpty() || !ArtNameArray(*Object, TEXT("forward_bones"), Spec.ForwardBones)
                || !(*Object)->TryGetArrayField(TEXT("tint_slots"), Tints) || (!Tints->IsEmpty() && !ArtNameArray(*Object, TEXT("tint_slots"), Spec.TintSlots)))
            {
                return ArtFail(Error, FString::Printf(TEXT("Character %s needs height_cm, run_speed_cm_per_sec, hand_bone, forward_bones and tint_slots."), *Spec.Id));
            }
            Spec.HandBone = FName(*Hand);
            Spec.HipBone = FName(*Hip);
            Spec.FootBone = FName(*Foot);
            const TSharedPtr<FJsonObject>* Colors = nullptr;
            if ((*Object)->TryGetObjectField(TEXT("colors"), Colors))
            {
                if (Spec.bAuthored) { return ArtFail(Error, FString::Printf(TEXT("Authored character %s takes its colours from its own materials."), *Spec.Id)); }
                for (const TPair<FString, TSharedPtr<FJsonValue>>& Color : (*Colors)->Values)
                {
                    FLinearColor Linear;
                    if (!ArtReadColor(Color.Value, Linear)) { return ArtFail(Error, FString::Printf(TEXT("Character %s colour %s is not a linear RGB triple."), *Spec.Id, *Color.Key)); }
                    Spec.Colors.Add(Color.Key, Linear);
                }
            }
            if (!ArtLoadClips(*Object, Spec.bAuthored, Spec, Error)) { return false; }
            if ((*Object)->HasField(TEXT("attachments")) && !Spec.bAuthored)
            {
                return ArtFail(Error, FString::Printf(TEXT("Only authored characters wear attachments: %s"), *Spec.Id));
            }
            if (!ArtLoadAttachments(*Object, Spec, Error)) { return false; }
            Out.Characters.Add(MoveTemp(Spec));
        }
        return true;
    }

    bool ArtLoadLooks(const TSharedPtr<FJsonObject>& Json, FArtManifest& Out, FString& Error)
    {
        const TArray<TSharedPtr<FJsonValue>>* Seats = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Monsters = nullptr;
        if (!Json->TryGetArrayField(TEXT("seats"), Seats) || Seats->Num() != LDArt::SeatCount
            || !Json->TryGetArrayField(TEXT("monsters"), Monsters) || Monsters->Num() != 4)
        {
            return ArtFail(Error, TEXT("art_assets.json needs six seats, the Low and High camp tiers, the Boss and the Elite tier."));
        }
        for (const TSharedPtr<FJsonValue>& Seat : *Seats)
        {
            FString Character;
            if (!Seat->TryGetString(Character)) { return ArtFail(Error, TEXT("Every seat names a character.")); }
            Out.Seats.Add(Character);
        }
        const TPair<const TCHAR*, ELDDemoMonsterId> Tiers[] = {
            {TEXT("monster_scavenger"), ELDDemoMonsterId::MonsterScavenger}, {TEXT("monster_guardian"), ELDDemoMonsterId::MonsterGuardian},
            {TEXT("monster_dean"), ELDDemoMonsterId::MonsterDean}, {TEXT("monster_advisor"), ELDDemoMonsterId::MonsterAdvisor} };
        for (int32 Index = 0; Index < Monsters->Num(); ++Index)
        {
            const TSharedPtr<FJsonObject>* Look = nullptr;
            FString Creature, Character;
            if (!(*Monsters)[Index]->TryGetObject(Look) || !(*Look)->TryGetStringField(TEXT("creature"), Creature)
                || !Creature.Equals(Tiers[Index].Key, ESearchCase::CaseSensitive) || !(*Look)->TryGetStringField(TEXT("character"), Character))
            {
                return ArtFail(Error, FString::Printf(TEXT("Camp tier look %d must be %s with a character."), Index, Tiers[Index].Key));
            }
            Out.Monsters.Add({uint8(Tiers[Index].Value), Character});
        }
        return true;
    }

    bool ArtLoadProps(const TSharedPtr<FJsonObject>& Json, FArtManifest& Out, FString& Error)
    {
        const TArray<TSharedPtr<FJsonValue>>* Props = nullptr;
        if (Json->HasField(TEXT("decorations")))
        { return ArtFail(Error, TEXT("Decoration coordinates belong to the saved Demo map; remove the retired decorations input.")); }
        if (!Json->TryGetArrayField(TEXT("props"), Props) || Props->IsEmpty())
        {
            return ArtFail(Error, TEXT("art_assets.json needs props."));
        }
        for (const TSharedPtr<FJsonValue>& Value : *Props)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            FArtPropSpec Spec;
            if (!Value->TryGetObject(Object) || !(*Object)->TryGetStringField(TEXT("id"), Spec.Id) || !LDArt::IsValidId(Spec.Id)
                || Out.Props.ContainsByPredicate([&Spec](const FArtPropSpec& Other) { return Other.Id == Spec.Id; })
                || !(*Object)->TryGetStringField(TEXT("source"), Spec.Source) || !Out.Files.Contains(Spec.Source)
                || !(Spec.Source.EndsWith(TEXT(".glb")) || Spec.Source.EndsWith(TEXT(".gltf"))) || !ArtNumberField(*Object, TEXT("scale"), .01, 100., Spec.Scale))
            {
                return ArtFail(Error, TEXT("Every prop needs a unique id, a listed .glb/.gltf source and a scale."));
            }
            if ((*Object)->HasField(TEXT("fadeable")) && !(*Object)->TryGetBoolField(TEXT("fadeable"), Spec.bFadeable))
            {
                return ArtFail(Error, TEXT("Prop fadeable must be a boolean: ") + Spec.Id);
            }
            Out.Props.Add(MoveTemp(Spec));
        }
        const TSharedPtr<FJsonObject>* Dressing = nullptr;
        const auto KnownProp = [&Out](const FString& Id) { return Out.Props.ContainsByPredicate([&Id](const FArtPropSpec& Prop) { return Prop.Id == Id; }); };
        if (!Json->TryGetObjectField(TEXT("dressing"), Dressing) || !(*Dressing)->TryGetStringField(TEXT("airdrop_prop"), Out.AirdropProp) || !KnownProp(Out.AirdropProp))
        {
            return ArtFail(Error, TEXT("art_assets.json dressing needs the airdrop_prop of a declared prop."));
        }
        if ((*Dressing)->HasField(TEXT("tree_props")) || (*Dressing)->HasField(TEXT("hidden_blockers")))
        { return ArtFail(Error, TEXT("Tree looks and hidden boundaries belong to the saved Demo map, not procedural dressing input.")); }
        return true;
    }

    // The manifest is the only art source list: packs proven by their own license text, one hashed file per use.
    bool ArtLoadManifest(FArtManifest& Out, FString& Error)
    {
        Out = FArtManifest();
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *ArtAbsolute(TEXT("art_assets.json")))) { return ArtFail(Error, TEXT("SourceData/Art/art_assets.json is missing.")); }
        TSharedPtr<FJsonObject> Json;
        int32 Schema = 0;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid() || !Json->TryGetNumberField(TEXT("schema"), Schema) || Schema != 1)
        {
            return ArtFail(Error, TEXT("art_assets.json must be valid JSON with schema 1."));
        }
        TSet<FString> Listed;
        if (!ArtLoadPacks(Json, Out.Packs, Listed, Error) || !ArtLoadFiles(Json, Out.Packs, Listed, Out, Error) || !ArtLoadCharacters(Json, Out, Error)
            || !ArtLoadLooks(Json, Out, Error) || !ArtLoadProps(Json, Out, Error))
        {
            return false;
        }
        // Only selected files ship: anything else under SourceData\Art is an unlisted third-party file.
        TArray<FString> Found;
        const FString Root = ArtSourceRoot();
        IFileManager::Get().FindFilesRecursive(Found, *Root, TEXT("*"), true, false);
        for (const FString& File : Found)
        {
            FString Relative = File;
            FPaths::MakePathRelativeTo(Relative, *(Root + TEXT("/")));
            Relative.ReplaceInline(TEXT("\\"), TEXT("/"));
            if (Relative != TEXT("art_assets.json") && !Listed.Contains(Relative)) { return ArtFail(Error, TEXT("Unlisted file under SourceData/Art: ") + Relative); }
        }
        Error.Reset();
        return true;
    }
}

namespace
{
    // Reads the glTF JSON (from .gltf text or the first .glb chunk) and returns each material's colour and image.
    bool ArtReadSourceMaterials(const FString& Relative, TMap<FString, FArtSourceMaterial>& Out, FString& Error)
    {
        Out.Reset();
        TArray<uint8> Bytes;
        if (!FFileHelper::LoadFileToArray(Bytes, *ArtAbsolute(Relative))) { return ArtFail(Error, TEXT("Cannot read ") + Relative); }
        FString Text;
        if (Relative.EndsWith(TEXT(".glb")))
        {
            uint32 Header[5] = {};
            if (Bytes.Num() < 20) { return ArtFail(Error, TEXT("Truncated GLB: ") + Relative); }
            FMemory::Memcpy(Header, Bytes.GetData(), sizeof(Header));
            if (Header[0] != 0x46546C67u || Header[1] != 2u || Header[4] != 0x4E4F534Au || 20u + Header[3] > uint32(Bytes.Num()))
            {
                return ArtFail(Error, TEXT("Not a glTF 2.0 binary with a JSON chunk: ") + Relative);
            }
            const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData() + 20), int32(Header[3]));
            Text = FString(Converted.Length(), Converted.Get());
        }
        else
        {
            FFileHelper::BufferToString(Text, Bytes.GetData(), Bytes.Num());
        }
        TSharedPtr<FJsonObject> Json;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()) { return ArtFail(Error, TEXT("Invalid glTF JSON: ") + Relative); }
        const TArray<TSharedPtr<FJsonValue>>* Materials = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Textures = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Images = nullptr;
        if (!Json->TryGetArrayField(TEXT("materials"), Materials)) { return ArtFail(Error, TEXT("glTF without materials: ") + Relative); }
        Json->TryGetArrayField(TEXT("textures"), Textures);
        Json->TryGetArrayField(TEXT("images"), Images);
        for (const TSharedPtr<FJsonValue>& Value : *Materials)
        {
            const TSharedPtr<FJsonObject>* Material = nullptr;
            FString Name;
            if (!Value->TryGetObject(Material) || !(*Material)->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty() || Out.Contains(Name))
            {
                return ArtFail(Error, TEXT("Every glTF material needs a unique name: ") + Relative);
            }
            FArtSourceMaterial& Source = Out.Add(Name);
            FString AlphaMode;
            if ((*Material)->TryGetStringField(TEXT("alphaMode"), AlphaMode) && AlphaMode != TEXT("OPAQUE"))
            {
                double Cutoff = .5;
                if (AlphaMode != TEXT("MASK") || ((*Material)->HasField(TEXT("alphaCutoff")) && (!(*Material)->TryGetNumberField(TEXT("alphaCutoff"), Cutoff) || !(Cutoff > 0. && Cutoff < 1.))))
                {
                    return ArtFail(Error, FString::Printf(TEXT("%s material %s must be OPAQUE or MASK with a cutoff in (0, 1)."), *Relative, *Name));
                }
                Source.bMasked = true;
                Source.AlphaCutoff = float(Cutoff);
            }
            const TSharedPtr<FJsonObject>* Pbr = nullptr;
            if (!(*Material)->TryGetObjectField(TEXT("pbrMetallicRoughness"), Pbr)) { continue; }
            if ((*Pbr)->HasField(TEXT("baseColorFactor")) && !ArtReadColor((*Pbr)->TryGetField(TEXT("baseColorFactor")), Source.Color))
            {
                return ArtFail(Error, FString::Printf(TEXT("%s material %s has an invalid base colour."), *Relative, *Name));
            }
            const TSharedPtr<FJsonObject>* ColorTexture = nullptr;
            int32 TextureIndex = 0, ImageIndex = 0;
            if (!(*Pbr)->TryGetObjectField(TEXT("baseColorTexture"), ColorTexture))
            {
                if (Source.bMasked) { return ArtFail(Error, FString::Printf(TEXT("%s material %s is masked without a base colour image."), *Relative, *Name)); }
                continue;
            }
            const TSharedPtr<FJsonObject>* TextureObject = nullptr;
            const TSharedPtr<FJsonObject>* ImageObject = nullptr;
            FString Uri;
            if (!(*ColorTexture)->TryGetNumberField(TEXT("index"), TextureIndex) || !Textures || !Textures->IsValidIndex(TextureIndex)
                || !(*Textures)[TextureIndex]->TryGetObject(TextureObject) || !(*TextureObject)->TryGetNumberField(TEXT("source"), ImageIndex)
                || !Images || !Images->IsValidIndex(ImageIndex) || !(*Images)[ImageIndex]->TryGetObject(ImageObject)
                || !(*ImageObject)->TryGetStringField(TEXT("uri"), Uri) || Uri.StartsWith(TEXT("data:")) || !ArtSafeRelative(Uri))
            {
                return ArtFail(Error, FString::Printf(TEXT("%s material %s must use an external relative base colour image."), *Relative, *Name));
            }
            Source.Image = FPaths::GetPath(Relative) / Uri;
            int32 SamplerIndex = 0, MagFilter = 0;
            const TArray<TSharedPtr<FJsonValue>>* Samplers = nullptr;
            const TSharedPtr<FJsonObject>* Sampler = nullptr;
            Source.bSmooth = (*TextureObject)->TryGetNumberField(TEXT("sampler"), SamplerIndex) && Json->TryGetArrayField(TEXT("samplers"), Samplers)
                && Samplers->IsValidIndex(SamplerIndex) && (*Samplers)[SamplerIndex]->TryGetObject(Sampler)
                && (*Sampler)->TryGetNumberField(TEXT("magFilter"), MagFilter) && MagFilter == ArtGltfLinear;
        }
        return true;
    }

    template<typename T>
    T* ArtLoadOrCreate(const FString& PackageName, bool bCreate)
    {
        const FString Name = FPackageName::GetShortName(PackageName);
        if (FPackageName::DoesPackageExist(PackageName)) { return LoadObject<T>(nullptr, *(PackageName + TEXT(".") + Name)); }
        return bCreate ? NewObject<T>(CreatePackage(*PackageName), *Name, RF_Public | RF_Standalone) : nullptr;
    }

    bool ArtSave(UObject& Asset, FString& Error)
    {
        const FString File = FPackageName::LongPackageNameToFilename(Asset.GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true)) { return ArtFail(Error, TEXT("Cannot create art asset directory: ") + File); }
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        return UPackage::SavePackage(Asset.GetOutermost(), &Asset, *File, Args) || ArtFail(Error, TEXT("Art asset could not be saved: ") + File);
    }

    template<typename T>
    T* ArtAddExpression(UMaterial& Material)
    {
        T* Expression = NewObject<T>(&Material);
        Expression->Material = &Material;
        Material.GetExpressionCollection().AddExpression(Expression);
        return Expression;
    }

    // The master graph is code: every import rebuilds it in place, so editing ArtMasterMaterial and running
    // ImportDemoArt -Mode Import is the only way to change it; edits made to the asset are replaced.
    // Base: one opaque two-sided surface for every art mesh: base colour = BaseTexture x Tint, emissive = Flash.
    // The fade variant is masked with a pixel dither on 1 - LDFadeStrength x max(LDFade, per-instance fade), so it fades
    // without translucency sorting, a whole component or one instance at a time.
    // The masked variant cuts where BaseTexture alpha is below AlphaCutoff (glTF MASK foliage, #97).
    UMaterial* ArtMasterMaterial(EArtSurface Surface, FString& Error)
    {
        const TCHAR* PackageName = ArtMasterPackageOf(Surface);
        const bool bFade = Surface == EArtSurface::Fade;
        UMaterial* Material = ArtLoadOrCreate<UMaterial>(PackageName, true);
        auto* White = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
        if (!Material || !White) { ArtFail(Error, FString(TEXT("Cannot create the art master material ")) + PackageName); return nullptr; }
        Material->PreEditChange(nullptr);
        // Drop whatever graph the asset held, so none of it is saved again.
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
        auto* Tint = ArtAddExpression<UMaterialExpressionVectorParameter>(*Material);
        Tint->ParameterName = ULDArtCatalog::TintParameter;
        Tint->DefaultValue = FLinearColor::White;
        auto* Flash = ArtAddExpression<UMaterialExpressionVectorParameter>(*Material);
        Flash->ParameterName = ULDArtCatalog::FlashParameter;
        Flash->DefaultValue = FLinearColor::Black;
        auto* Texture = ArtAddExpression<UMaterialExpressionTextureSampleParameter2D>(*Material);
        Texture->ParameterName = ArtTextureParameter;
        Texture->Texture = White;
        Texture->SamplerType = SAMPLERTYPE_Color;
        auto* Roughness = ArtAddExpression<UMaterialExpressionScalarParameter>(*Material);
        Roughness->ParameterName = TEXT("Roughness");
        Roughness->DefaultValue = .85f;
        auto* Multiply = ArtAddExpression<UMaterialExpressionMultiply>(*Material);
        Multiply->A.Connect(0, Texture);
        Multiply->B.Connect(0, Tint);
        UMaterialEditorOnlyData* Inputs = Material->GetEditorOnlyData();
        Inputs->BaseColor.Connect(0, Multiply);
        Inputs->Roughness.Connect(0, Roughness);
        Inputs->EmissiveColor.Connect(0, Flash);
        if (bFade)
        {
            auto* Fade = ArtAddExpression<UMaterialExpressionScalarParameter>(*Material);
            Fade->ParameterName = ULDArtCatalog::FadeParameter;
            Fade->DefaultValue = 0.f;
            auto* Strength = ArtAddExpression<UMaterialExpressionMultiply>(*Material);
            auto* Reach = ArtAddExpression<UMaterialExpressionScalarParameter>(*Material);
            Reach->ParameterName = ULDArtCatalog::FadeStrengthParameter;
            Reach->DefaultValue = .9f;
            // #97: an instanced prop fades one instance at a time through its first per-instance custom float.
            auto* Instance = ArtAddExpression<UMaterialExpressionPerInstanceCustomData>(*Material);
            Instance->DataIndex = 0;
            Instance->ConstDefaultValue = 0.f;
            auto* Either = ArtAddExpression<UMaterialExpressionMax>(*Material);
            Either->A.Connect(0, Fade);
            Either->B.Connect(0, Instance);
            Strength->A.Connect(0, Either);
            Strength->B.Connect(0, Reach);
            auto* Opacity = ArtAddExpression<UMaterialExpressionOneMinus>(*Material);
            Opacity->Input.Connect(0, Strength);
            auto* Dithered = ArtAddExpression<UMaterialExpressionCustom>(*Material);
            Dithered->OutputType = CMOT_Float1;
            Dithered->Description = TEXT("LDFadeDither");
            // Interleaved gradient noise on the pixel position: a pixel stays when the opacity exceeds its threshold.
            Dithered->Code = TEXT("float2 P = Parameters.SvPosition.xy;\r\nfloat N = frac(52.9829189 * frac(dot(P, float2(0.06711056, 0.00583715))));\r\nreturn Opacity - N + 0.5;");
            Dithered->Inputs.SetNum(1);
            Dithered->Inputs[0].InputName = TEXT("Opacity");
            Dithered->Inputs[0].Input.Connect(0, Opacity);
            Inputs->OpacityMask.Connect(0, Dithered);
        }
        if (Surface == EArtSurface::Masked)
        {
            auto* Cutoff = ArtAddExpression<UMaterialExpressionScalarParameter>(*Material);
            Cutoff->ParameterName = ArtAlphaCutoffParameter;
            Cutoff->DefaultValue = .5f;
            auto* Below = ArtAddExpression<UMaterialExpressionSubtract>(*Material);
            Below->A.Connect(4, Texture);
            Below->B.Connect(0, Cutoff);
            // alpha - cutoff + 0.5 against the 0.5 clip keeps exactly the pixels whose alpha reaches the cutoff.
            auto* Centred = ArtAddExpression<UMaterialExpressionAdd>(*Material);
            Centred->A.Connect(0, Below);
            Centred->ConstB = .5f;
            Inputs->OpacityMask.Connect(0, Centred);
        }
        Material->BlendMode = Surface == EArtSurface::Opaque ? BLEND_Opaque : BLEND_Masked;
        Material->OpacityMaskClipValue = .5f;
        Material->TwoSided = true;
        Material->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
        Material->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
        // Identities derive from the package and graph position, so a rebuild of the same code keeps every GUID.
        const uint32 Seed = FCrc::StrCrc32(PackageName);
        for (int32 Index = 0; Index < Collection.Expressions.Num(); ++Index)
        {
            UMaterialExpression* Expression = Collection.Expressions[Index];
            Expression->MaterialExpressionGuid = FGuid(Seed, uint32(Index), 0x4C44u, 1u);
            if (Expression->HasAParameterName()) { Expression->GetParameterExpressionId() = FGuid(Seed, uint32(Index), 0x4C44u, 2u); }
        }
        Material->PostEditChange();
        Material->MarkPackageDirty();
        return Material;
    }

    // The graph ArtMasterMaterial builds, checked after every build and load: an empty or partial graph renders black.
    bool ArtCheckMasterGraph(const UMaterial& Material, EArtSurface Surface, FString& Error)
    {
        const bool bFade = Surface == EArtSurface::Fade, bMasked = Surface == EArtSurface::Masked;
        TArray<FName> Parameters;
        for (const UMaterialExpression* Expression : Material.GetExpressions())
        {
            if (Expression && !Expression->GetParameterName().IsNone()) { Parameters.Add(Expression->GetParameterName()); }
        }
        TArray<FName> Expected{ULDArtCatalog::TintParameter, ULDArtCatalog::FlashParameter, ArtTextureParameter, TEXT("Roughness")};
        if (bFade) { Expected.Append({ULDArtCatalog::FadeParameter, ULDArtCatalog::FadeStrengthParameter}); }
        if (bMasked) { Expected.Add(ArtAlphaCutoffParameter); }
        Parameters.Sort(FNameLexicalLess());
        Expected.Sort(FNameLexicalLess());
        const UMaterialEditorOnlyData* Inputs = Material.GetEditorOnlyData();
        const bool bGraph = Inputs && Material.GetExpressions().Num() == (bFade ? 12 : bMasked ? 8 : 5) && Parameters == Expected
            && Inputs->BaseColor.IsConnected() && Inputs->Roughness.IsConnected() && Inputs->EmissiveColor.IsConnected()
            && Inputs->OpacityMask.IsConnected() == (bFade || bMasked) && Material.BlendMode == (bFade || bMasked ? BLEND_Masked : BLEND_Opaque) && Material.TwoSided;
        return bGraph || ArtFail(Error, FString::Printf(TEXT("%s does not hold the importer's graph (%d expressions, %d parameters); ")
            TEXT("run ImportDemoArt.ps1 -Mode Import to rebuild it."), *Material.GetPathName(), Material.GetExpressions().Num(), Parameters.Num()));
    }

    // A process without a client loads packages as a dedicated server, whose linker skips exports that are not
    // always loaded in an editor game, such as material expressions: every master would load with an empty graph.
    bool ArtRequireClient(FString& Error)
    {
        return GIsClient || ArtFail(Error, TEXT("Art import and validation need a client process (UCommandlet::IsClient); ")
            TEXT("a dedicated-server load drops material graphs."));
    }

    // Kenney palette atlases: nearest filtering without mips keeps every swatch a flat colour at any distance.
    // Smooth images (the glTF asks for linear filtering, #97 foliage and bark) keep mips and default compression.
    void ArtTextureSettings(UTexture2D& Texture, bool bSmooth)
    {
        Texture.Filter = bSmooth ? TF_Default : TF_Nearest;
        Texture.MipGenSettings = bSmooth ? TMGS_FromTextureGroup : TMGS_NoMipmaps;
        Texture.CompressionSettings = bSmooth ? TC_Default : TC_EditorIcon;
        Texture.LODGroup = TEXTUREGROUP_World;
    }

    bool ArtTextureMatches(const UTexture2D& Texture, bool bSmooth)
    {
        return Texture.Filter == (bSmooth ? TF_Default : TF_Nearest) && Texture.MipGenSettings == (bSmooth ? TMGS_FromTextureGroup : TMGS_NoMipmaps)
            && Texture.CompressionSettings == (bSmooth ? TC_Default : TC_EditorIcon) && Texture.SRGB;
    }

    UTexture2D* ArtImportTexture(const FString& Relative, bool bSmooth, TArray<UObject*>& Assets, FString& Error)
    {
        const FString Name = TEXT("T_") + ArtAssetName(Relative);
        UPackage* Package = CreatePackage(*(FString(ArtTextureRoot) + Name));
        Package->FullyLoad();
        auto* Factory = NewObject<UTextureFactory>(GetTransientPackage());
        Factory->AddToRoot();
        UTextureFactory::SuppressImportOverwriteDialog();
        bool bCancelled = false;
        UObject* Imported = UFactory::StaticImportObject(UTexture2D::StaticClass(), Package, FName(*Name), RF_Public | RF_Standalone, bCancelled,
            *ArtAbsolute(Relative), nullptr, Factory);
        Factory->RemoveFromRoot();
        auto* Texture = Cast<UTexture2D>(Imported);
        if (!Texture || bCancelled) { ArtFail(Error, TEXT("Texture import failed: ") + Relative); return nullptr; }
        ArtTextureSettings(*Texture, bSmooth);
        Texture->SRGB = true;
        Texture->NeverStream = true;
        Texture->PostEditChange();
        Texture->MarkPackageDirty();
        Assets.AddUnique(Texture);
        return Texture;
    }

    UMaterialInstanceConstant* ArtSlotMaterial(UMaterial& Master, const FString& Folder, const FString& Owner, FName Slot, const FLinearColor& Color,
        UTexture2D* Texture, const FArtSourceMaterial& Source, TArray<UObject*>& Assets, FString& Error)
    {
        const FString PackageName = Folder / (TEXT("MI_") + Owner + TEXT("_") + ArtAssetName(Slot.ToString()));
        auto* Instance = ArtLoadOrCreate<UMaterialInstanceConstant>(PackageName, true);
        if (!Instance) { ArtFail(Error, TEXT("Cannot create material instance ") + PackageName); return nullptr; }
        Instance->SetParentEditorOnly(&Master);
        Instance->ClearParameterValuesEditorOnly();
        Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(ULDArtCatalog::TintParameter), Color);
        if (Texture) { Instance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(ArtTextureParameter), Texture); }
        if (Source.bMasked) { Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(ArtAlphaCutoffParameter), Source.AlphaCutoff); }
        Instance->PostEditChange();
        Instance->MarkPackageDirty();
        Assets.AddUnique(Instance);
        return Instance;
    }

    bool ArtInterchange(const FString& Relative, const FString& ContentPath, bool bSkeletal, TArray<UObject*>& Out, FString& Error)
    {
        UInterchangeManager& Manager = UInterchangeManager::GetInterchangeManager();
        UInterchangeSourceData* Source = UInterchangeManager::CreateSourceData(ArtAbsolute(Relative));
        auto* Pipeline = NewObject<UInterchangeGenericAssetsPipeline>(GetTransientPackage());
        if (!Source || !Pipeline || !Pipeline->CommonMeshesProperties || !Pipeline->CommonSkeletalMeshesAndAnimationsProperties || !Pipeline->MeshPipeline
            || !Pipeline->AnimationPipeline || !Pipeline->MaterialPipeline)
        {
            return ArtFail(Error, TEXT("Interchange pipeline is unavailable for ") + Relative);
        }
        Pipeline->bUseSourceNameForAsset = true;
        auto* Common = Pipeline->CommonMeshesProperties.Get();
        Common->ForceAllMeshAsType = bSkeletal ? EInterchangeForceMeshType::IFMT_None : EInterchangeForceMeshType::IFMT_StaticMesh;
        Common->bImportLods = false;
        Common->bBakeMeshes = true;
        Common->bRecomputeNormals = false;
        Common->VertexColorImportOption = EInterchangeVertexColorImportOption::IVCIO_Ignore;
        Pipeline->CommonSkeletalMeshesAndAnimationsProperties->bImportOnlyAnimations = false;
        auto* Mesh = Pipeline->MeshPipeline.Get();
        Mesh->bImportStaticMeshes = !bSkeletal;
        Mesh->bImportSkeletalMeshes = bSkeletal;
        Mesh->CombineStaticMeshesBehavior = EInterchangeCombineStaticMeshesBehavior::All;
        Mesh->bCollision = false;
        Mesh->bImportCollisionAccordingToMeshName = false;
        Mesh->bBuildNanite = false;
        Mesh->bGenerateLightmapUVs = false;
        Mesh->bCreatePhysicsAsset = false;
        Mesh->bImportMorphTargets = false;
        Mesh->bImportGeometryCaches = false;
        Pipeline->AnimationPipeline->bImportAnimations = bSkeletal;
        Pipeline->MaterialPipeline->bImportMaterials = false;
        if (Pipeline->MaterialPipeline->TexturePipeline) { Pipeline->MaterialPipeline->TexturePipeline->bImportTextures = false; }
        FImportAssetParameters Parameters;
        Parameters.bIsAutomated = true;
        Parameters.bReplaceExisting = true;
        Parameters.OverridePipelines.Add(FSoftObjectPath(Pipeline));
        Out.Reset();
        if (!Manager.ImportAsset(ContentPath, Source, Parameters, Out) || Out.IsEmpty())
        {
            return ArtFail(Error, TEXT("Interchange import failed: ") + Relative);
        }
        return true;
    }

    bool ArtImportedFromCurrentBytes(const UAssetImportData* Data, const FString& Relative, const FString& Asset, FString& Error)
    {
        if (!Data || Data->SourceData.SourceFiles.IsEmpty())
        {
            return ArtFail(Error, Asset + TEXT(" has no import source; run ImportDemoArt.ps1 -Mode Import."));
        }
        const FMD5Hash Current = FMD5Hash::HashFile(*ArtAbsolute(Relative));
        const auto& Imported = Data->SourceData.SourceFiles[0];
        if (!Current.IsValid() || Imported.FileHash != Current
            || !FPaths::GetCleanFilename(Imported.RelativeFilename).Equals(FPaths::GetCleanFilename(Relative), ESearchCase::CaseSensitive))
        {
            return ArtFail(Error, Asset + TEXT(" was not imported from the current bytes of ") + Relative + TEXT("; run ImportDemoArt.ps1 -Mode Import."));
        }
        return true;
    }
}

namespace
{
    // Mesh pose on the capsule: face the actor's +X (forward bones in front of the root), scale the bind height to the
    // declared display height and stand the lowest vertex on the capsule bottom.
    bool ArtBodyTransform(const USkeletalMesh& Mesh, const FArtCharacterSpec& Spec, FTransform& Out, FString& Error)
    {
        const FReferenceSkeleton& Reference = Mesh.GetRefSkeleton();
        TArray<FTransform> Space;
        Space.SetNum(Reference.GetNum());
        for (int32 Bone = 0; Bone < Reference.GetNum(); ++Bone)
        {
            const int32 Parent = Reference.GetParentIndex(Bone);
            Space[Bone] = Parent == INDEX_NONE ? Reference.GetRefBonePose()[Bone] : Reference.GetRefBonePose()[Bone] * Space[Parent];
        }
        FVector Forward = FVector::ZeroVector;
        for (const FName Name : Spec.ForwardBones)
        {
            const int32 Bone = Reference.FindBoneIndex(Name);
            if (Bone == INDEX_NONE)
            {
                FString Bones;
                for (int32 Index = 0; Index < Reference.GetNum(); ++Index) { Bones += Reference.GetBoneName(Index).ToString() + TEXT(" "); }
                return ArtFail(Error, FString::Printf(TEXT("Character %s has no forward bone %s; bones: %s"), *Spec.Id, *Name.ToString(), *Bones));
            }
            Forward += Space[Bone].GetLocation() / Spec.ForwardBones.Num();
        }
        Forward -= Space[0].GetLocation();
        Forward.Z = 0.;
        const FBox Bounds = Mesh.GetImportedBounds().GetBox();
        const double Height = Bounds.Max.Z - Bounds.Min.Z;
        if (Forward.Size2D() < 1. || Height < 50.)
        {
            return ArtFail(Error, FString::Printf(TEXT("Character %s has no usable forward direction or bind height."), *Spec.Id));
        }
        const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(Forward.Y, Forward.X));
        const double Scale = Spec.HeightCm / Height;
        const double HalfHeight = GetDefault<ALDCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
        Out = FTransform(FRotator(0., -Yaw, 0.), FVector(0., 0., -HalfHeight - Bounds.Min.Z * Scale), FVector(Scale));
        return true;
    }

    // Interchange names a clip after its mesh: "<Mesh><Clip>"; an exact match avoids Idle also matching Idle_Neutral.
    UAnimSequence* ArtFindClip(const TArray<UAnimSequence*>& Sequences, const FString& MeshName, const FString& Clip)
    {
        const FString Wanted = ArtAssetName(Clip);
        UAnimSequence* Found = nullptr;
        for (UAnimSequence* Sequence : Sequences)
        {
            const FString Name = Sequence->GetName();
            if (Name.Equals(MeshName + Wanted, ESearchCase::CaseSensitive) || Name.Equals(MeshName + TEXT("_") + Wanted, ESearchCase::CaseSensitive))
            {
                if (Found) { return nullptr; }
                Found = Sequence;
            }
        }
        return Found;
    }

    // The prop pivot is the centre of its footprint (the vertices next to its lowest point), on the ground.
    bool ArtFootprintPivot(const UStaticMesh& Mesh, FVector& Out, FString& Error)
    {
        const FMeshDescription* Description = Mesh.GetMeshDescription(0);
        if (!Description || Description->Vertices().Num() == 0) { return ArtFail(Error, TEXT("Prop mesh has no source geometry: ") + Mesh.GetPathName()); }
        FStaticMeshConstAttributes Attributes(*Description);
        const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
        double Lowest = TNumericLimits<double>::Max();
        for (const FVertexID Vertex : Description->Vertices().GetElementIDs()) { Lowest = FMath::Min(Lowest, double(Positions[Vertex].Z)); }
        FBox Footprint(ForceInit);
        for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
        {
            if (Positions[Vertex].Z <= Lowest + ArtFootprintSliceCm) { Footprint += FVector(Positions[Vertex]); }
        }
        Out = FVector(Footprint.GetCenter().X, Footprint.GetCenter().Y, Lowest);
        return true;
    }

    // The master of one slot: masked sources cut on their alpha; a fadeable prop must be wholly opaque.
    bool ArtSlotSurface(const FArtSourceMaterial& Source, bool bFadeable, const FString& Owner, const FString& Slot, EArtSurface& Out, FString& Error)
    {
        if (Source.bMasked && bFadeable) { return ArtFail(Error, FString::Printf(TEXT("%s slot %s is masked; a fadeable prop must be opaque."), *Owner, *Slot)); }
        Out = Source.bMasked ? EArtSurface::Masked : bFadeable ? EArtSurface::Fade : EArtSurface::Opaque;
        return true;
    }

    // Replaces every imported slot with an art material instance: the source colour (or the manifest override) and image.
    template<typename TSlot>
    bool ArtAssignMaterials(TArray<TSlot>& Slots, const TMap<FString, FArtSourceMaterial>& Sources, const TMap<FString, FLinearColor>& Colors,
        const TMap<EArtSurface, UMaterial*>& Masters, bool bFadeable, const FString& Folder, const FString& Owner, TArray<UObject*>& Assets, FString& Error)
    {
        TSet<FString> Seen;
        for (TSlot& Slot : Slots)
        {
            const FString Name = Slot.ImportedMaterialSlotName.ToString();
            const FArtSourceMaterial* Source = Sources.Find(Name);
            if (!Source) { return ArtFail(Error, FString::Printf(TEXT("%s slot %s is not a source material."), *Owner, *Name)); }
            EArtSurface Surface;
            if (!ArtSlotSurface(*Source, bFadeable, Owner, Name, Surface, Error)) { return false; }
            UTexture2D* Texture = nullptr;
            if (!Source->Image.IsEmpty())
            {
                Texture = ArtImportTexture(Source->Image, Source->bSmooth, Assets, Error);
                if (!Texture) { return false; }
            }
            const FLinearColor* Override = Colors.Find(Name);
            UMaterialInstanceConstant* Instance = ArtSlotMaterial(*Masters[Surface], Folder, Owner, Slot.ImportedMaterialSlotName, Override ? *Override : Source->Color,
                Texture, *Source, Assets, Error);
            if (!Instance) { return false; }
            Slot.MaterialInterface = Instance;
            Seen.Add(Name);
        }
        for (const TPair<FString, FLinearColor>& Color : Colors)
        {
            if (!Seen.Contains(Color.Key)) { return ArtFail(Error, FString::Printf(TEXT("%s colour override %s names no material slot."), *Owner, *Color.Key)); }
        }
        return true;
    }

    template<typename TSlot>
    bool ArtCheckMaterials(const TArray<TSlot>& Slots, const TMap<FString, FArtSourceMaterial>& Sources, const TMap<FString, FLinearColor>& Colors,
        const FString& Folder, const FString& Owner, bool bFadeable, FString& Error)
    {
        for (const TSlot& Slot : Slots)
        {
            const FString Name = Slot.ImportedMaterialSlotName.ToString();
            const FArtSourceMaterial* Source = Sources.Find(Name);
            const auto* Instance = Cast<UMaterialInstanceConstant>(Slot.MaterialInterface);
            FLinearColor Tint;
            const FLinearColor* Override = Colors.Find(Name);
            EArtSurface Surface = EArtSurface::Opaque;
            float Cutoff = 0.f;
            if (Source && !ArtSlotSurface(*Source, bFadeable, Owner, Name, Surface, Error)) { return false; }
            if (Source && Source->bMasked && (!Instance || !Instance->GetScalarParameterValue(FHashedMaterialParameterInfo(ArtAlphaCutoffParameter), Cutoff)
                || !FMath::IsNearlyEqual(Cutoff, Source->AlphaCutoff, 1e-4f)))
            {
                return ArtFail(Error, FString::Printf(TEXT("%s slot %s does not cut at its source alpha cutoff."), *Owner, *Name));
            }
            if (!Source || !Instance || !Instance->GetPathName().StartsWith(Folder) || !Instance->Parent
                || Instance->Parent->GetOutermost()->GetName() != ArtMasterPackageOf(Surface)
                || !Instance->GetVectorParameterValue(FHashedMaterialParameterInfo(ULDArtCatalog::TintParameter), Tint)
                || !Tint.Equals(Override ? *Override : Source->Color, 1e-4f))
            {
                return ArtFail(Error, FString::Printf(TEXT("%s slot %s does not use its art material and source colour."), *Owner, *Name));
            }
            if (!Source->Image.IsEmpty())
            {
                UTexture* Texture = nullptr;
                if (!Instance->GetTextureParameterValue(FHashedMaterialParameterInfo(ArtTextureParameter), Texture) || !Texture
                    || !ArtImportedFromCurrentBytes(Texture->AssetImportData, Source->Image, Texture->GetPathName(), Error))
                {
                    return Error.IsEmpty() ? ArtFail(Error, Owner + TEXT(" slot ") + Name + TEXT(" lost its source image.")) : false;
                }
                const auto* Image = Cast<UTexture2D>(Texture);
                if (!Image || !ArtTextureMatches(*Image, Source->bSmooth))
                {
                    return ArtFail(Error, FString::Printf(TEXT("%s slot %s image is not sampled as its glTF asks (%s)."), *Owner, *Name, Source->bSmooth ? TEXT("smooth") : TEXT("palette")));
                }
            }
        }
        return true;
    }

    bool ArtImportCharacter(const FArtCharacterSpec& Spec, const TMap<EArtSurface, UMaterial*>& Masters, ULDArtCatalog& Catalog, TArray<UObject*>& Assets, FString& Error)
    {
        TMap<FString, FArtSourceMaterial> Sources;
        TArray<UObject*> Imported;
        const FString Folder = FString(ArtCharacterRoot) + Spec.Id;
        if (!ArtReadSourceMaterials(Spec.Source, Sources, Error) || !ArtInterchange(Spec.Source, Folder, true, Imported, Error)) { return false; }
        USkeletalMesh* Mesh = nullptr;
        TArray<UAnimSequence*> Sequences;
        for (UObject* Object : Imported)
        {
            if (auto* Skinned = Cast<USkeletalMesh>(Object))
            {
                if (Mesh) { return ArtFail(Error, TEXT("More than one skeletal mesh imported from ") + Spec.Source); }
                Mesh = Skinned;
            }
            else if (auto* Sequence = Cast<UAnimSequence>(Object)) { Sequences.Add(Sequence); }
        }
        if (!Mesh || !Mesh->GetSkeleton()) { return ArtFail(Error, TEXT("No skinned mesh and skeleton imported from ") + Spec.Source); }
        if (!ArtAssignMaterials(Mesh->GetMaterials(), Sources, Spec.Colors, Masters, false, Folder, Spec.Id, Assets, Error)) { return false; }
        for (int32 LOD=0; LOD<Mesh->GetLODNum(); ++LOD) { Mesh->GetLODInfo(LOD)->bAllowCPUAccess = true; }
        Mesh->PostEditChange();
        Mesh->MarkPackageDirty();
        Assets.Add(Mesh);
        Assets.Add(Mesh->GetSkeleton());
        FLDArtCharacter& Body = Catalog.Characters.AddDefaulted_GetRef();
        Body.Id = Spec.Id;
        Body.Mesh = Mesh;
        Body.HeightCm = Spec.HeightCm;
        Body.TintSlots = Spec.TintSlots;
        Body.HandBone = Spec.HandBone;
        Body.RunSpeedCmPerSec = Spec.RunSpeedCmPerSec;
        if (!ArtBodyTransform(*Mesh, Spec, Body.MeshTransform, Error)) { return false; }
        UE_LOG(LogLDEditor, Display, TEXT("LD_DEMO_ART_BODY %s mesh=%s skeleton=%s transform=%s"), *Spec.Id, *Mesh->GetPathName(),
            *Mesh->GetSkeleton()->GetPathName(), *Body.MeshTransform.ToHumanReadableString());
        for (const FArtClipSpec& Spec2 : Spec.Clips)
        {
            UAnimSequence* Sequence = ArtFindClip(Sequences, Mesh->GetName(), Spec2.Clip);
            if (!Sequence)
            {
                // A re-import replaces existing clips in place without listing them; load them by their package path.
                const FString Name = Mesh->GetName() + ArtAssetName(Spec2.Clip);
                Sequence = ArtLoadOrCreate<UAnimSequence>(Folder / Name, false);
                if (Sequence && Sequence->GetSkeleton() != Mesh->GetSkeleton()) { Sequence = nullptr; }
            }
            if (!Sequence)
            {
                FString Names;
                for (const UAnimSequence* Candidate : Sequences) { Names += Candidate->GetName() + TEXT(" "); }
                return ArtFail(Error, FString::Printf(TEXT("Character %s has no single clip %s among: %s"), *Spec.Id, *Spec2.Clip, *Names));
            }
            Sequence->bEnableRootMotion = false;
            Sequence->MarkPackageDirty();
            Assets.Add(Sequence);
            FLDArtAnimation& Clip = Body.Animations.AddDefaulted_GetRef();
            Clip.State = Spec2.State;
            Clip.Sequence = Sequence;
            Clip.bLoop = LDArt::IsLoopingAnim(Spec2.State);
            Clip.PlayRate = Spec2.PlayRate;
            Clip.ImpactSec = LDArt::HasImpact(Spec2.State) ? Spec2.ImpactSec : 0.f;
            Clip.SourceClip = Spec2.Clip;
            Clip.SubstituteNote = Spec2.Substitute;
        }
        return true;
    }

    bool ArtImportProp(const FArtPropSpec& Spec, const TMap<EArtSurface, UMaterial*>& Masters, ULDArtCatalog& Catalog, TArray<UObject*>& Assets, FString& Error)
    {
        TMap<FString, FArtSourceMaterial> Sources;
        TArray<UObject*> Imported;
        const FString Folder = FString(ArtPropRoot) + Spec.Id;
        if (!ArtReadSourceMaterials(Spec.Source, Sources, Error) || !ArtInterchange(Spec.Source, Folder, false, Imported, Error)) { return false; }
        TArray<UStaticMesh*> Meshes;
        for (UObject* Object : Imported) { if (auto* Mesh = Cast<UStaticMesh>(Object)) { Meshes.Add(Mesh); } }
        if (Meshes.Num() != 1) { return ArtFail(Error, FString::Printf(TEXT("%s imported %d static meshes; one combined mesh is required."), *Spec.Source, Meshes.Num())); }
        UStaticMesh* Mesh = Meshes[0];
        // Display only even if a component were misconfigured: no simple collision and no navigation geometry.
        if (UBodySetup* Body = Mesh->GetBodySetup())
        {
            Body->RemoveSimpleCollision();
            Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;
        }
        Mesh->bHasNavigationData = false;
        if (!ArtAssignMaterials(Mesh->GetStaticMaterials(), Sources, {}, Masters, Spec.bFadeable, Folder, Spec.Id, Assets, Error)) { return false; }
        Mesh->PostEditChange();
        Mesh->MarkPackageDirty();
        Assets.Add(Mesh);
        FVector Pivot;
        if (!ArtFootprintPivot(*Mesh, Pivot, Error)) { return false; }
        UE_LOG(LogLDEditor, Display, TEXT("LD_DEMO_ART_PROP %s bounds=%s pivot=%s"), *Spec.Id, *Mesh->GetBoundingBox().ToString(), *Pivot.ToString());
        FLDArtProp& Prop = Catalog.Props.AddDefaulted_GetRef();
        Prop.Id = Spec.Id;
        Prop.bFadeable = Spec.bFadeable;
        Prop.Mesh = Mesh;
        return true;
    }
}

namespace
{
    bool ArtReadSavedScene(const ULDArtCatalog& Catalog, TArray<FLDArtDecoration>& Placements, TArray<FString>& Hidden, FString& Error)
    {
        UPackage* Package = LoadPackage(nullptr, LDDemoEntry::WorldMap, LOAD_None);
        UWorld* Map = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
        if (!Map) { return ArtFail(Error, TEXT("The saved editable Demo map is missing.")); }
        return LDStaticScene::Read(*Map, Catalog, Placements, Hidden, Error);
    }

    // The crate stands with its footprint centre on the drop point at its manifest scale.
    bool ArtAirdropTransform(const FArtManifest& Manifest, const ULDArtCatalog& Catalog, FTransform& Out, FString& Error)
    {
        const int32 Index = Manifest.Props.IndexOfByPredicate([&Manifest](const FArtPropSpec& Prop) { return Prop.Id == Manifest.AirdropProp; });
        if (Index == INDEX_NONE || !Catalog.Props.IsValidIndex(Index) || !Catalog.Props[Index].Mesh) { return ArtFail(Error, TEXT("The airdrop prop is not imported.")); }
        FVector Pivot;
        if (!ArtFootprintPivot(*Catalog.Props[Index].Mesh, Pivot, Error)) { return false; }
        Out = FTransform(-Pivot) * FTransform(FQuat::Identity, FVector::ZeroVector, FVector(Manifest.Props[Index].Scale));
        return true;
    }

    bool ArtLoadMasters(bool bCreate, TMap<EArtSurface, UMaterial*>& Out, FString& Error)
    {
        for (const EArtSurface Surface : { EArtSurface::Opaque, EArtSurface::Fade, EArtSurface::Masked })
        {
            UMaterial* Master = bCreate ? ArtMasterMaterial(Surface, Error) : ArtLoadOrCreate<UMaterial>(ArtMasterPackageOf(Surface), false);
            if (!Master) { return ArtFail(Error, FString::Printf(TEXT("Art master material %s is missing; run ImportDemoArt.ps1 -Mode Import. %s"), ArtMasterPackageOf(Surface), *Error)); }
            if (!ArtCheckMasterGraph(*Master, Surface, Error)) { return false; }
            Out.Add(Surface, Master);
        }
        return true;
    }
}

namespace
{
    // Authored bodies live as finished assets in their body folder (no importer rebuilds them); Import reads them into
    // the catalog and Validate checks them against the manifest. Their numbers are measured from the clips as
    // MeasureDemoArt.py measures the glTF bodies: the same sample rate, impact rule and planted-foot run speed.
    constexpr double ArtSampleHz = 240.;
    // Largest horizontal root travel (displayed cm) an in-place clip may show.
    constexpr double ArtRootDriftCm = 2.;

    template<typename T>
    T* ArtLoadAuthored(const FString& Folder, const FString& Name)
    {
        return LoadObject<T>(nullptr, *(Folder / Name + TEXT(".") + Name));
    }

    // Clip times sampled at ArtSampleHz from 0 to the clip end, and the poses at those times (rate 1, no root motion).
    void ArtSamplePoses(const UAnimSequence& Sequence, const USkeletalMesh& Mesh, TArray<double>& Times, TArray<FAnimPose>& Poses)
    {
        const double Length = Sequence.GetPlayLength();
        const int32 Count = FMath::FloorToInt32(Length * ArtSampleHz) + 1;
        Times.Reset(Count);
        for (int32 Index = 0; Index < Count; ++Index) { Times.Add(Length * Index / FMath::Max(Count - 1, 1)); }
        FAnimPoseEvaluationOptions Options;
        Options.bExtractRootMotion = false;
        Options.bShouldRetarget = false;
        Options.OptionalSkeletalMesh = const_cast<USkeletalMesh*>(&Mesh);
        Poses.Reset();
        UAnimPoseExtensions::GetAnimPoseAtTimeIntervals(&Sequence, Times, Options, Poses);
    }

    struct FArtMeasured
    {
        float RunSpeedCmPerSec = 0.f;
        TMap<ELDArtAnim, float> ImpactSec;
    };

    float ArtRound(double Value, double Step) { return float(FMath::RoundToDouble(Value / Step) * Step); }

    bool ArtMeasureAuthored(const FArtCharacterSpec& Spec, const FLDArtCharacter& Body, FArtMeasured& Out, FString& Error)
    {
        const USkeletalMesh& Mesh = *Body.Mesh;
        // The mesh's own forward: the catalog transform turns it onto the actor's +X.
        const FVector Forward = Body.MeshTransform.GetRotation().Inverse().RotateVector(FVector::XAxisVector).GetSafeNormal2D();
        const double Scale = Body.MeshTransform.GetScale3D().X;
        TArray<double> Times;
        TArray<FAnimPose> Poses;
        const FName Root = Mesh.GetRefSkeleton().GetBoneName(0);
        for (const FLDArtAnimation& Clip : Body.Animations)
        {
            ArtSamplePoses(*Clip.Sequence, Mesh, Times, Poses);
            // In place: a body moves with its capsule only, so the root bone stays where the clip starts it.
            double Drift = 0.;
            for (const FAnimPose& Pose : Poses)
            {
                Drift = FMath::Max(Drift, FVector::Dist2D(UAnimPoseExtensions::GetBonePose(Pose, Root, EAnimPoseSpaces::World).GetLocation(),
                    UAnimPoseExtensions::GetBonePose(Poses[0], Root, EAnimPoseSpaces::World).GetLocation()) * Scale);
            }
            if (Drift > ArtRootDriftCm)
            {
                return ArtFail(Error, FString::Printf(TEXT("Character %s %s clip moves its root %.1f cm; author it in place."), *Spec.Id, *LDArt::AnimId(Clip.State), Drift));
            }
            if (!LDArt::HasImpact(Clip.State)) { continue; }
            int32 Best = INDEX_NONE;
            double Reach = -TNumericLimits<double>::Max();
            for (int32 Index = 0; Index < Poses.Num(); ++Index)
            {
                const FVector Hand = UAnimPoseExtensions::GetBonePose(Poses[Index], Spec.HandBone, EAnimPoseSpaces::World).GetLocation();
                const FVector Hip = UAnimPoseExtensions::GetBonePose(Poses[Index], Spec.HipBone, EAnimPoseSpaces::World).GetLocation();
                const double Ahead = FVector::DotProduct(Hand - Hip, Forward);
                if (Ahead > Reach) { Reach = Ahead; Best = Index; }
            }
            if (Best == INDEX_NONE) { return ArtFail(Error, FString::Printf(TEXT("Character %s %s clip has no poses to measure."), *Spec.Id, *LDArt::AnimId(Clip.State))); }
            Out.ImpactSec.Add(Clip.State, ArtRound(Times[Best], .001));
        }
        const FLDArtAnimation* Run = Body.Find(ELDArtAnim::Run);
        ArtSamplePoses(*Run->Sequence, Mesh, Times, Poses);
        TArray<double> Height, Along;
        for (const FAnimPose& Pose : Poses)
        {
            const FVector Foot = UAnimPoseExtensions::GetBonePose(Pose, Spec.FootBone, EAnimPoseSpaces::World).GetLocation();
            Height.Add(Foot.Z);
            Along.Add(FVector::DotProduct(Foot, Forward));
        }
        if (Height.Num() < 3) { return ArtFail(Error, FString::Printf(TEXT("Character %s Run clip is too short to measure."), *Spec.Id)); }
        // While the foot is planted (within 2 displayed cm of its lowest point) it slides back at the body's ground speed.
        const double Lowest = FMath::Min(Height);
        TArray<double> Speeds;
        for (int32 Index = 0; Index < Height.Num(); ++Index)
        {
            if (Height[Index] > Lowest + 2. / Scale) { continue; }
            const int32 A = FMath::Max(Index - 1, 0), B = FMath::Min(Index + 1, Height.Num() - 1);
            Speeds.Add(-(Along[B] - Along[A]) / (Times[B] - Times[A]) * Scale);
        }
        Speeds.Sort();
        const int32 Mid = Speeds.Num() / 2;
        Out.RunSpeedCmPerSec = ArtRound(Speeds.Num() % 2 ? Speeds[Mid] : .5 * (Speeds[Mid - 1] + Speeds[Mid]), .1);
        return true;
    }

    // The manifest's numbers must be the measured ones; the message gives the measured values to copy.
    bool ArtCheckMeasured(const FArtCharacterSpec& Spec, const FLDArtCharacter& Body, FString& Error)
    {
        FArtMeasured Measured;
        if (!ArtMeasureAuthored(Spec, Body, Measured, Error)) { return false; }
        TArray<FString> Wrong;
        if (FMath::Abs(Measured.RunSpeedCmPerSec - Spec.RunSpeedCmPerSec) > .5f)
        {
            Wrong.Add(FString::Printf(TEXT("run_speed_cm_per_sec %.1f (manifest %.1f)"), Measured.RunSpeedCmPerSec, Spec.RunSpeedCmPerSec));
        }
        for (const FArtClipSpec& Clip : Spec.Clips)
        {
            const float* Impact = Measured.ImpactSec.Find(Clip.State);
            if (Impact && FMath::Abs(*Impact - Clip.ImpactSec) > .0015f)
            {
                Wrong.Add(FString::Printf(TEXT("%s impact_sec %.3f (manifest %.3f)"), *LDArt::AnimId(Clip.State), *Impact, Clip.ImpactSec));
            }
        }
        return Wrong.IsEmpty() || ArtFail(Error, FString::Printf(TEXT("Character %s measures %s."), *Spec.Id, *FString::Join(Wrong, TEXT(", "))));
    }

    // Every material the body component paints exposes the identity tint and the hit flash, and lives in the body folder.
    template<typename TSlot>
    bool ArtCheckAuthoredMaterials(const TArray<TSlot>& Slots, const FString& Folder, const FString& Owner, FString& Error)
    {
        for (const TSlot& Slot : Slots)
        {
            FLinearColor Value;
            const UMaterialInterface* Material = Slot.MaterialInterface;
            const UMaterial* Base = Material ? Material->GetMaterial() : nullptr;
            if (!Material || !Base || !Material->GetPathName().StartsWith(Folder) || !Base->GetPathName().StartsWith(Folder)
                || !Material->GetVectorParameterValue(FHashedMaterialParameterInfo(ULDArtCatalog::TintParameter), Value)
                || !Material->GetVectorParameterValue(FHashedMaterialParameterInfo(ULDArtCatalog::FlashParameter), Value))
            {
                return ArtFail(Error, FString::Printf(TEXT("%s slot %s needs a material of its body folder with the Tint and Flash parameters."),
                    *Owner, *Slot.MaterialSlotName.ToString()));
            }
        }
        return true;
    }

    bool ArtReadAuthored(const FArtCharacterSpec& Spec, FLDArtCharacter& Body, TArray<UObject*>* Assets, FString& Error)
    {
        const FString Folder = FString(ArtCharacterRoot) + Spec.Id;
        const FString Hint = TEXT(" in its body folder.");
        USkeletalMesh* Mesh = ArtLoadAuthored<USkeletalMesh>(Folder, Spec.Mesh);
        if (!Mesh || !Mesh->GetSkeleton()) { return ArtFail(Error, FString::Printf(TEXT("Authored body %s has no mesh %s with a skeleton%s"), *Spec.Id, *Spec.Mesh, *Hint)); }
        if (!Spec.Source.IsEmpty() && !ArtImportedFromCurrentBytes(Mesh->GetAssetImportData(), Spec.Source, Mesh->GetPathName(), Error)) { return false; }
        if (!ArtCheckAuthoredMaterials(Mesh->GetMaterials(), Folder, Spec.Id, Error)) { return false; }
        if (Assets)
        {
            for (int32 LOD = 0; LOD < Mesh->GetLODNum(); ++LOD) { Mesh->GetLODInfo(LOD)->bAllowCPUAccess = true; }
            Mesh->PostEditChange();
            Mesh->MarkPackageDirty();
            Assets->Add(Mesh);
        }
        Body.Id = Spec.Id;
        Body.Mesh = Mesh;
        Body.HeightCm = Spec.HeightCm;
        Body.TintSlots = Spec.TintSlots;
        Body.HandBone = Spec.HandBone;
        Body.RunSpeedCmPerSec = Spec.RunSpeedCmPerSec;
        if (!ArtBodyTransform(*Mesh, Spec, Body.MeshTransform, Error)) { return false; }
        Body.Animations.Reset();
        for (const FArtClipSpec& Want : Spec.Clips)
        {
            UAnimSequence* Sequence = ArtLoadAuthored<UAnimSequence>(Folder, Want.Asset);
            if (!Sequence) { return ArtFail(Error, FString::Printf(TEXT("Authored body %s has no clip %s%s"), *Spec.Id, *Want.Asset, *Hint)); }
            if (Assets)
            {
                Sequence->bEnableRootMotion = false;
                Sequence->MarkPackageDirty();
                Assets->Add(Sequence);
            }
            FLDArtAnimation& Clip = Body.Animations.AddDefaulted_GetRef();
            Clip.State = Want.State;
            Clip.Sequence = Sequence;
            Clip.bLoop = LDArt::IsLoopingAnim(Want.State);
            Clip.PlayRate = Want.PlayRate;
            Clip.ImpactSec = LDArt::HasImpact(Want.State) ? Want.ImpactSec : 0.f;
            Clip.SourceClip = Want.Clip;
            Clip.SubstituteNote = Want.Substitute;
        }
        Body.Attachments.Reset();
        for (const FArtAttachmentSpec& Want : Spec.Attachments)
        {
            FLDArtAttachment& Part = Body.Attachments.AddDefaulted_GetRef();
            Part.Id = Want.Id;
            Part.Socket = Want.Socket;
            Part.TintSlots = Want.TintSlots;
            const FString Owner = Spec.Id + TEXT(".") + Want.Id;
            if (Want.bSkinned)
            {
                Part.SkinnedMesh = ArtLoadAuthored<USkeletalMesh>(Folder, Want.Asset);
                if (!Part.SkinnedMesh || !ArtCheckAuthoredMaterials(Part.SkinnedMesh->GetMaterials(), Folder, Owner, Error))
                {
                    return Error.IsEmpty() ? ArtFail(Error, FString::Printf(TEXT("Authored body %s has no skinned part %s%s"), *Spec.Id, *Want.Asset, *Hint)) : false;
                }
            }
            else
            {
                Part.StaticMesh = ArtLoadAuthored<UStaticMesh>(Folder, Want.Asset);
                if (!Part.StaticMesh || !ArtCheckAuthoredMaterials(Part.StaticMesh->GetStaticMaterials(), Folder, Owner, Error))
                {
                    return Error.IsEmpty() ? ArtFail(Error, FString::Printf(TEXT("Authored body %s has no static part %s%s"), *Spec.Id, *Want.Asset, *Hint)) : false;
                }
            }
        }
        return true;
    }

    bool ArtImportAuthored(const FArtCharacterSpec& Spec, ULDArtCatalog& Catalog, TArray<UObject*>& Assets, FString& Error)
    {
        FLDArtCharacter& Body = Catalog.Characters.AddDefaulted_GetRef();
        if (!ArtReadAuthored(Spec, Body, &Assets, Error)) { return false; }
        UE_LOG(LogLDEditor, Display, TEXT("LD_DEMO_ART_BODY %s authored mesh=%s skeleton=%s transform=%s parts=%d"), *Spec.Id, *Body.Mesh->GetPathName(),
            *Body.Mesh->GetSkeleton()->GetPathName(), *Body.MeshTransform.ToHumanReadableString(), Body.Attachments.Num());
        return ArtCheckMeasured(Spec, Body, Error);
    }

    bool ArtValidateAuthored(const FArtCharacterSpec& Spec, const FLDArtCharacter& Have, FString& Error)
    {
        FLDArtCharacter Want;
        if (!ArtReadAuthored(Spec, Want, nullptr, Error)) { return false; }
        bool bSame = Have.Id == Want.Id && Have.Mesh == Want.Mesh && Have.HeightCm == Want.HeightCm && Have.TintSlots == Want.TintSlots
            && Have.HandBone == Want.HandBone && Have.RunSpeedCmPerSec == Want.RunSpeedCmPerSec && Have.MeshTransform.Equals(Want.MeshTransform, 1e-3)
            && Have.Animations.Num() == Want.Animations.Num() && Have.Attachments.Num() == Want.Attachments.Num();
        for (int32 Index = 0; bSame && Index < Want.Animations.Num(); ++Index)
        {
            const FLDArtAnimation& A = Have.Animations[Index];
            const FLDArtAnimation& B = Want.Animations[Index];
            bSame = A.State == B.State && A.Sequence == B.Sequence && A.bLoop == B.bLoop && A.PlayRate == B.PlayRate && A.ImpactSec == B.ImpactSec
                && A.SourceClip == B.SourceClip && A.SubstituteNote == B.SubstituteNote && !A.Sequence->bEnableRootMotion;
        }
        for (int32 Index = 0; bSame && Index < Want.Attachments.Num(); ++Index)
        {
            const FLDArtAttachment& A = Have.Attachments[Index];
            const FLDArtAttachment& B = Want.Attachments[Index];
            bSame = A.Id == B.Id && A.StaticMesh == B.StaticMesh && A.SkinnedMesh == B.SkinnedMesh && A.Socket == B.Socket && A.TintSlots == B.TintSlots;
        }
        bSame = bSame && Have.Mesh->GetLODInfo(0) && Have.Mesh->GetLODInfo(0)->bAllowCPUAccess;
        if (!bSame) { return ArtFail(Error, TEXT("Authored character differs from art_assets.json: ") + Spec.Id + TEXT("; run ImportDemoArt.ps1 -Mode Import.")); }
        return ArtCheckMeasured(Spec, Have, Error);
    }
}

bool LDDemoArtImport::Import(FString& Error)
{
    if (!ArtRequireClient(Error)) { return false; }
    FArtManifest Manifest;
    if (!ArtLoadManifest(Manifest, Error)) { return false; }
    TArray<UObject*> Assets;
    TMap<EArtSurface, UMaterial*> Masters;
    if (!ArtLoadMasters(true, Masters, Error)) { return false; }
    auto* Catalog = ArtLoadOrCreate<ULDArtCatalog>(ArtCatalogPackage, true);
    if (!Catalog) { return ArtFail(Error, TEXT("The art catalog cannot be created.")); }
    for (const TPair<EArtSurface, UMaterial*>& Master : Masters) { Assets.Add(Master.Value); }
    Catalog->Characters.Reset();
    Catalog->Seats.Reset();
    Catalog->Monsters.Reset();
    Catalog->Props.Reset();
    Catalog->Decorations.Reset();
    Catalog->HiddenBlockers.Reset();
    for (const FArtCharacterSpec& Spec : Manifest.Characters)
    {
        if (!(Spec.bAuthored ? ArtImportAuthored(Spec, *Catalog, Assets, Error) : ArtImportCharacter(Spec, Masters, *Catalog, Assets, Error))) { return false; }
    }
    for (int32 Seat = 0; Seat < Manifest.Seats.Num(); ++Seat)
    {
        FLDArtSeatLook& Look = Catalog->Seats.AddDefaulted_GetRef();
        Look.Seat = uint8(Seat);
        Look.Character = Manifest.Seats[Seat];
    }
    for (const TPair<uint8, FString>& Tier : Manifest.Monsters)
    {
        FLDArtMonsterLook& Look = Catalog->Monsters.AddDefaulted_GetRef();
        Look.CreatureId = Tier.Key;
        Look.Character = Tier.Value;
    }
    for (const FArtPropSpec& Spec : Manifest.Props)
    {
        if (!ArtImportProp(Spec, Masters, *Catalog, Assets, Error)) { return false; }
    }
    if (!ArtReadSavedScene(*Catalog, Catalog->Decorations, Catalog->HiddenBlockers, Error)) { return false; }
    Catalog->AirdropProp = Manifest.AirdropProp;
    if (!ArtAirdropTransform(Manifest, *Catalog, Catalog->AirdropTransform, Error)) { return false; }
    FLDArtResolved Resolved;
    if (!Catalog->Resolve(Resolved, Error)) { return ArtFail(Error, TEXT("Imported art catalog is invalid: ") + Error); }
    if (!LDArt::CheckPlacementsAgainstDemoWorld(*Catalog, Error)) { return ArtFail(Error, TEXT("Decoration placement: ") + Error); }
    Catalog->MarkPackageDirty();
    Assets.Add(Catalog);
    for (UObject* Asset : Assets)
    {
        if (!ArtSave(*Asset, Error)) { return false; }
    }
    UE_LOG(LogLDEditor, Display, TEXT("LD_DEMO_ART_IMPORTED %d assets saved."), Assets.Num());
    Error.Reset();
    return true;
}

bool LDDemoArtImport::Validate(FString& Error)
{
    if (!ArtRequireClient(Error)) { return false; }
    FArtManifest Manifest;
    if (!ArtLoadManifest(Manifest, Error)) { return false; }
    const auto* Catalog = ArtLoadOrCreate<ULDArtCatalog>(ArtCatalogPackage, false);
    if (!Catalog) { return ArtFail(Error, TEXT("Art catalog DA_DemoArt is missing; run ImportDemoArt.ps1 -Mode Import.")); }
    TMap<EArtSurface, UMaterial*> Masters;
    if (!ArtLoadMasters(false, Masters, Error)) { return false; }
    FLDArtResolved Resolved;
    if (!Catalog->Resolve(Resolved, Error)) { return ArtFail(Error, TEXT("Art catalog is invalid: ") + Error); }
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TSharedPtr<const FLDDemoWorldGeometry> World;
    TArray<FLDArtDecoration> Decorations;
    TArray<FString> Hidden;
    if (Catalog->Props.Num() != Manifest.Props.Num())
    {
        return ArtFail(Error, TEXT("Art catalog props differ from art_assets.json; run ImportDemoArt.ps1 -Mode Import."));
    }
    if (!ArtReadSavedScene(*Catalog, Decorations, Hidden, Error)) { return false; }
    FTransform Crate;
    if (Catalog->Characters.Num() != Manifest.Characters.Num() || Catalog->Decorations.Num() != Decorations.Num()
        || Catalog->HiddenBlockers != Hidden || Catalog->AirdropProp != Manifest.AirdropProp
        || !ArtAirdropTransform(Manifest, *Catalog, Crate, Error) || !Crate.Equals(Catalog->AirdropTransform, 1e-2))
    {
        return ArtFail(Error, TEXT("Art catalog entries differ from art_assets.json or the world's trees; run ImportDemoArt.ps1 -Mode Import."));
    }
    for (int32 Index = 0; Index < Manifest.Characters.Num(); ++Index)
    {
        const FArtCharacterSpec& Spec = Manifest.Characters[Index];
        const FLDArtCharacter& Body = Catalog->Characters[Index];
        if (Spec.bAuthored)
        {
            if (!ArtValidateAuthored(Spec, Body, Error)) { return false; }
            continue;
        }
        const FString Folder = FString(ArtCharacterRoot) + Spec.Id + TEXT("/");
        FTransform Expected;
        TMap<FString, FArtSourceMaterial> Sources;
        if (Body.Id != Spec.Id || Body.HeightCm != Spec.HeightCm || Body.TintSlots != Spec.TintSlots || Body.HandBone != Spec.HandBone
            || Body.RunSpeedCmPerSec != Spec.RunSpeedCmPerSec || Body.Animations.Num() != Spec.Clips.Num() || !Body.Mesh->GetPathName().StartsWith(Folder)
            || !ArtBodyTransform(*Body.Mesh, Spec, Expected, Error) || !Expected.Equals(Body.MeshTransform, 1e-3))
        {
            return ArtFail(Error, TEXT("Character differs from art_assets.json: ") + Spec.Id + (Error.IsEmpty() ? FString() : TEXT(" ") + Error));
        }
        if (!ArtImportedFromCurrentBytes(Body.Mesh->GetAssetImportData(), Spec.Source, Body.Mesh->GetPathName(), Error)
            || !ArtReadSourceMaterials(Spec.Source, Sources, Error) || !ArtCheckMaterials(Body.Mesh->GetMaterials(), Sources, Spec.Colors, Folder, Spec.Id, false, Error))
        {
            return false;
        }
        for (int32 Clip = 0; Clip < Spec.Clips.Num(); ++Clip)
        {
            const FArtClipSpec& Want = Spec.Clips[Clip];
            const FLDArtAnimation& Have = Body.Animations[Clip];
            if (Have.State != Want.State || Have.SourceClip != Want.Clip || Have.PlayRate != Want.PlayRate || Have.SubstituteNote != Want.Substitute
                || Have.ImpactSec != (LDArt::HasImpact(Want.State) ? Want.ImpactSec : 0.f)
                || ArtFindClip({Have.Sequence.Get()}, Body.Mesh->GetName(), Want.Clip) != Have.Sequence
                || !Have.Sequence->GetPathName().StartsWith(Folder))
            {
                return ArtFail(Error, FString::Printf(TEXT("Character %s %s clip differs from art_assets.json."), *Spec.Id, *LDArt::AnimId(Want.State)));
            }
            if (!ArtImportedFromCurrentBytes(Have.Sequence->AssetImportData, Spec.Source, Have.Sequence->GetPathName(), Error)) { return false; }
        }
    }
    for (int32 Seat = 0; Seat < LDArt::SeatCount; ++Seat)
    {
        if (Catalog->Seats[Seat].Character != Manifest.Seats[Seat]) { return ArtFail(Error, FString::Printf(TEXT("Seat %d look differs from art_assets.json."), Seat)); }
    }
    for (int32 Tier = 0; Tier < Manifest.Monsters.Num(); ++Tier)
    {
        if (Catalog->Monsters[Tier].CreatureId != Manifest.Monsters[Tier].Key || Catalog->Monsters[Tier].Character != Manifest.Monsters[Tier].Value)
        {
            return ArtFail(Error, FString::Printf(TEXT("Camp tier look %d differs from art_assets.json."), Tier));
        }
    }
    for (int32 Index = 0; Index < Manifest.Props.Num(); ++Index)
    {
        const FArtPropSpec& Spec = Manifest.Props[Index];
        const FLDArtProp& Prop = Catalog->Props[Index];
        const FString Folder = FString(ArtPropRoot) + Spec.Id + TEXT("/");
        TMap<FString, FArtSourceMaterial> Sources;
        if (Prop.Id != Spec.Id || Prop.bFadeable != Spec.bFadeable || !Prop.Mesh->GetPathName().StartsWith(Folder) || Prop.Mesh->bHasNavigationData
            || (Prop.Mesh->GetBodySetup() && Prop.Mesh->GetBodySetup()->AggGeom.GetElementCount() > 0))
        {
            return ArtFail(Error, TEXT("Prop differs from art_assets.json or carries collision/navigation data: ") + Spec.Id);
        }
        if (!ArtImportedFromCurrentBytes(Prop.Mesh->GetAssetImportData(), Spec.Source, Prop.Mesh->GetPathName(), Error)
            || !ArtReadSourceMaterials(Spec.Source, Sources, Error) || !ArtCheckMaterials(Prop.Mesh->GetStaticMaterials(), Sources, {}, Folder, Spec.Id, Spec.bFadeable, Error))
        {
            return false;
        }
    }
    for (int32 Index = 0; Index < Decorations.Num(); ++Index)
    {
        const auto& Expected = Decorations[Index];
        const auto& Have = Catalog->Decorations[Index];
        if (Have.Prop != Expected.Prop || Have.Zone != Expected.Zone || Have.Blocker != Expected.Blocker
            || !Expected.Transform.Equals(Have.Transform, 1e-2))
        { return ArtFail(Error, TEXT("Saved scene differs from its derived art index; run SyncDemoMap.ps1.")); }
    }
    if (!LDArt::CheckPlacementsAgainstDemoWorld(*Catalog, Error)) { return ArtFail(Error, TEXT("Decoration placement: ") + Error); }
    Error.Reset();
    return true;
}
