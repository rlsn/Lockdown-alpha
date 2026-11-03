#include "LDDemoArtImport.h"
#include "Art/LDArtCatalog.h"
#include "Art/LDArtPlacement.h"
#include "Animation/AnimSequence.h"
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
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "PhysicsEngine/BodySetup.h"
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
    const TCHAR* const ArtTextureRoot = TEXT("/Game/Lockdown/Art/Textures/");
    const TCHAR* const ArtCharacterRoot = TEXT("/Game/Lockdown/Art/Characters/");
    const TCHAR* const ArtPropRoot = TEXT("/Game/Lockdown/Art/Props/");
    const TCHAR* const ArtTextureParameter = TEXT("BaseTexture");
    // Vertices within this height of the lowest point form the footprint whose centre becomes a prop's pivot.
    constexpr double ArtFootprintSliceCm = 5.;

    struct FArtClipSpec
    {
        ELDArtAnim State = ELDArtAnim::Count;
        FString Clip, Substitute;
        float PlayRate = 1.f, ImpactSec = 0.f;
    };

    struct FArtCharacterSpec
    {
        FString Id, Source;
        float HeightCm = 0.f, RunSpeedCmPerSec = 0.f;
        FName HandBone;
        TArray<FName> ForwardBones, TintSlots;
        TMap<FString, FLinearColor> Colors;
        TArray<FArtClipSpec> Clips;
    };

    struct FArtPropSpec
    {
        FString Id, Source;
        float Scale = 1.f;
        bool bFadeable = false;
    };

    struct FArtDecorationSpec
    {
        FString Prop;
        ELDArtZone Zone = ELDArtZone::Count;
        FVector Location = FVector::ZeroVector;
        float Yaw = 0.f, Scale = 1.f;
    };

    struct FArtManifest
    {
        TArray<FArtCharacterSpec> Characters;
        TArray<FString> Seats;
        TArray<TPair<uint8, FString>> Monsters;
        TArray<FArtPropSpec> Props;
        TArray<FArtDecorationSpec> Decorations;
        TSet<FString> Files;
    };

    // Colour and base-colour image of one source material, read from the glTF itself.
    struct FArtSourceMaterial
    {
        FLinearColor Color = FLinearColor::White;
        FString Image;
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
                || !ArtSafeRelative(LicenseFile) || !Url.StartsWith(TEXT("https://")) || !Page.StartsWith(TEXT("https://")))
            {
                return ArtFail(Error, TEXT("Every art pack needs a unique id, https page and download URLs, a date, license, license_file and license_sha256."));
            }
            if (!License.Contains(TEXT("CC0"))) { return ArtFail(Error, FString::Printf(TEXT("Pack %s is not declared CC0."), *Id)); }
            if (!ArtCheckHash(LicenseFile, LicenseHash, Error)) { return false; }
            FString LicenseText;
            FFileHelper::LoadFileToString(LicenseText, *ArtAbsolute(LicenseFile));
            if (!LicenseText.Contains(TEXT("CC0")) && !LicenseText.Contains(TEXT("Creative Commons Zero")))
            {
                return ArtFail(Error, FString::Printf(TEXT("The license file of pack %s does not state CC0."), *Id));
            }
            PackIds.Add(Id);
            Listed.Add(LicenseFile);
        }
        return true;
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
                || !(Path.EndsWith(TEXT(".gltf")) || Path.EndsWith(TEXT(".glb")) || Path.EndsWith(TEXT(".png"))))
            {
                return ArtFail(Error, TEXT("Every art file needs a unique .gltf/.glb/.png path, a declared pack, its pack_path, sha256 and purpose: ") + Path);
            }
            if (!ArtCheckHash(Path, Hash, Error)) { return false; }
            Listed.Add(Path);
            Out.Files.Add(Path);
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
            FString Hand;
            const TArray<TSharedPtr<FJsonValue>>* Clips = nullptr;
            if (!Value->TryGetObject(Object) || !(*Object)->TryGetStringField(TEXT("id"), Spec.Id) || !LDArt::IsValidId(Spec.Id)
                || Out.Characters.ContainsByPredicate([&Spec](const FArtCharacterSpec& Other) { return Other.Id == Spec.Id; })
                || !(*Object)->TryGetStringField(TEXT("source"), Spec.Source) || !Out.Files.Contains(Spec.Source)
                || !ArtNumberField(*Object, TEXT("height_cm"), 120., 260., Spec.HeightCm)
                || !ArtNumberField(*Object, TEXT("run_speed_cm_per_sec"), 50., 2000., Spec.RunSpeedCmPerSec)
                || !(*Object)->TryGetStringField(TEXT("hand_bone"), Hand) || Hand.IsEmpty()
                || !ArtNameArray(*Object, TEXT("forward_bones"), Spec.ForwardBones) || !ArtNameArray(*Object, TEXT("tint_slots"), Spec.TintSlots)
                || !(*Object)->TryGetArrayField(TEXT("animations"), Clips))
            {
                return ArtFail(Error, TEXT("Every character needs a unique id, a listed source, height_cm, run_speed_cm_per_sec, hand_bone, forward_bones, tint_slots and animations."));
            }
            Spec.HandBone = FName(*Hand);
            const TSharedPtr<FJsonObject>* Colors = nullptr;
            if ((*Object)->TryGetObjectField(TEXT("colors"), Colors))
            {
                for (const TPair<FString, TSharedPtr<FJsonValue>>& Color : (*Colors)->Values)
                {
                    FLinearColor Linear;
                    if (!ArtReadColor(Color.Value, Linear)) { return ArtFail(Error, FString::Printf(TEXT("Character %s colour %s is not a linear RGB triple."), *Spec.Id, *Color.Key)); }
                    Spec.Colors.Add(Color.Key, Linear);
                }
            }
            for (int32 Index = 0; Index < Clips->Num(); ++Index)
            {
                const TSharedPtr<FJsonObject>* Clip = nullptr;
                FArtClipSpec ClipSpec;
                FString State;
                if (!(*Clips)[Index]->TryGetObject(Clip) || !(*Clip)->TryGetStringField(TEXT("state"), State) || !LDArt::ParseAnimId(State, ClipSpec.State)
                    || ClipSpec.State != ELDArtAnim(Index) || !(*Clip)->TryGetStringField(TEXT("clip"), ClipSpec.Clip) || ClipSpec.Clip.IsEmpty()
                    || !ArtNumberField(*Clip, TEXT("play_rate"), .1, 10., ClipSpec.PlayRate))
                {
                    return ArtFail(Error, FString::Printf(TEXT("Character %s animation %d must be state %s with a clip and a play_rate."), *Spec.Id, Index, *LDArt::AnimId(ELDArtAnim(Index))));
                }
                if (LDArt::HasImpact(ClipSpec.State) && !ArtNumberField(*Clip, TEXT("impact_sec"), .001, 30., ClipSpec.ImpactSec))
                {
                    return ArtFail(Error, FString::Printf(TEXT("Character %s %s needs impact_sec."), *Spec.Id, *State));
                }
                (*Clip)->TryGetStringField(TEXT("substitute"), ClipSpec.Substitute);
                Spec.Clips.Add(MoveTemp(ClipSpec));
            }
            Out.Characters.Add(MoveTemp(Spec));
        }
        return true;
    }

    bool ArtLoadLooks(const TSharedPtr<FJsonObject>& Json, FArtManifest& Out, FString& Error)
    {
        const TArray<TSharedPtr<FJsonValue>>* Seats = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Monsters = nullptr;
        if (!Json->TryGetArrayField(TEXT("seats"), Seats) || Seats->Num() != LDArt::SeatCount
            || !Json->TryGetArrayField(TEXT("monsters"), Monsters) || Monsters->Num() != 2)
        {
            return ArtFail(Error, TEXT("art_assets.json needs six seats and the two camp tiers."));
        }
        for (const TSharedPtr<FJsonValue>& Seat : *Seats)
        {
            FString Character;
            if (!Seat->TryGetString(Character)) { return ArtFail(Error, TEXT("Every seat names a character.")); }
            Out.Seats.Add(Character);
        }
        const TPair<const TCHAR*, ELDDemoMonsterId> Tiers[] = {
            {TEXT("monster_scavenger"), ELDDemoMonsterId::MonsterScavenger}, {TEXT("monster_guardian"), ELDDemoMonsterId::MonsterGuardian} };
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
        const TArray<TSharedPtr<FJsonValue>>* Decorations = nullptr;
        if (!Json->TryGetArrayField(TEXT("props"), Props) || Props->IsEmpty() || !Json->TryGetArrayField(TEXT("decorations"), Decorations) || Decorations->IsEmpty())
        {
            return ArtFail(Error, TEXT("art_assets.json needs props and decorations."));
        }
        for (const TSharedPtr<FJsonValue>& Value : *Props)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            FArtPropSpec Spec;
            if (!Value->TryGetObject(Object) || !(*Object)->TryGetStringField(TEXT("id"), Spec.Id) || !LDArt::IsValidId(Spec.Id)
                || Out.Props.ContainsByPredicate([&Spec](const FArtPropSpec& Other) { return Other.Id == Spec.Id; })
                || !(*Object)->TryGetStringField(TEXT("source"), Spec.Source) || !Out.Files.Contains(Spec.Source)
                || !Spec.Source.EndsWith(TEXT(".glb")) || !ArtNumberField(*Object, TEXT("scale"), .01, 100., Spec.Scale))
            {
                return ArtFail(Error, TEXT("Every prop needs a unique id, a listed .glb source and a scale."));
            }
            if ((*Object)->HasField(TEXT("fadeable")) && !(*Object)->TryGetBoolField(TEXT("fadeable"), Spec.bFadeable))
            {
                return ArtFail(Error, TEXT("Prop fadeable must be a boolean: ") + Spec.Id);
            }
            Out.Props.Add(MoveTemp(Spec));
        }
        for (int32 Index = 0; Index < Decorations->Num(); ++Index)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            FArtDecorationSpec Spec;
            FString Zone;
            float X = 0.f, Y = 0.f;
            if (!(*Decorations)[Index]->TryGetObject(Object) || !(*Object)->TryGetStringField(TEXT("prop"), Spec.Prop)
                || !Out.Props.ContainsByPredicate([&Spec](const FArtPropSpec& Prop) { return Prop.Id == Spec.Prop; })
                || !(*Object)->TryGetStringField(TEXT("zone"), Zone) || !LDArt::ParseZoneId(Zone, Spec.Zone)
                || !ArtNumberField(*Object, TEXT("x"), -20000., 20000., X) || !ArtNumberField(*Object, TEXT("y"), -20000., 20000., Y)
                || !ArtNumberField(*Object, TEXT("yaw"), -360., 360., Spec.Yaw))
            {
                return ArtFail(Error, FString::Printf(TEXT("Decoration %d needs a declared prop, a zone, x, y and yaw."), Index));
            }
            if ((*Object)->HasField(TEXT("scale")) && !ArtNumberField(*Object, TEXT("scale"), .1, 10., Spec.Scale))
            {
                return ArtFail(Error, FString::Printf(TEXT("Decoration %d scale must be 0.1..10."), Index));
            }
            Spec.Location = FVector(X, Y, 0.);
            Out.Decorations.Add(MoveTemp(Spec));
        }
        return true;
    }

    // The manifest is the only art source list: CC0 packs proven by their own license text, one hashed file per use.
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
        TSet<FString> PackIds, Listed;
        if (!ArtLoadPacks(Json, PackIds, Listed, Error) || !ArtLoadFiles(Json, PackIds, Listed, Out, Error) || !ArtLoadCharacters(Json, Out, Error)
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
            const TSharedPtr<FJsonObject>* Pbr = nullptr;
            if (!(*Material)->TryGetObjectField(TEXT("pbrMetallicRoughness"), Pbr)) { continue; }
            if ((*Pbr)->HasField(TEXT("baseColorFactor")) && !ArtReadColor((*Pbr)->TryGetField(TEXT("baseColorFactor")), Source.Color))
            {
                return ArtFail(Error, FString::Printf(TEXT("%s material %s has an invalid base colour."), *Relative, *Name));
            }
            const TSharedPtr<FJsonObject>* ColorTexture = nullptr;
            int32 TextureIndex = 0, ImageIndex = 0;
            if (!(*Pbr)->TryGetObjectField(TEXT("baseColorTexture"), ColorTexture)) { continue; }
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
        Expression->UpdateMaterialExpressionGuid(true, false);
        Material.GetExpressionCollection().AddExpression(Expression);
        return Expression;
    }

    // One opaque two-sided surface for every art mesh: base colour = BaseTexture x Tint, emissive = Flash.
    // An existing master is reused untouched: re-saving a loaded material from this commandlet drops its graph.
    // The fade variant is masked with a pixel dither on 1 - 0.7 x LDFade, so it fades without translucency sorting.
    UMaterial* ArtMasterMaterial(const TCHAR* PackageName, bool bFade, bool& bCreated, FString& Error)
    {
        bCreated = !FPackageName::DoesPackageExist(PackageName);
        if (!bCreated) { return ArtLoadOrCreate<UMaterial>(PackageName, false); }
        UMaterial* Material = ArtLoadOrCreate<UMaterial>(PackageName, true);
        auto* White = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
        if (!Material || !White) { ArtFail(Error, FString(TEXT("Cannot create the art master material ")) + PackageName); return nullptr; }
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
        for (UMaterialExpression* Parameter : TArray<UMaterialExpression*>{Tint, Flash, Texture, Roughness}) { Parameter->UpdateParameterGuid(true, false); }
        UMaterialEditorOnlyData* Inputs = Material->GetEditorOnlyData();
        Inputs->BaseColor.Connect(0, Multiply);
        Inputs->Roughness.Connect(0, Roughness);
        Inputs->EmissiveColor.Connect(0, Flash);
        if (bFade)
        {
            auto* Fade = ArtAddExpression<UMaterialExpressionScalarParameter>(*Material);
            Fade->ParameterName = ULDArtCatalog::FadeParameter;
            Fade->DefaultValue = 0.f;
            Fade->UpdateParameterGuid(true, false);
            auto* Strength = ArtAddExpression<UMaterialExpressionMultiply>(*Material);
            Strength->A.Connect(0, Fade);
            Strength->ConstB = .7f;
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
            Material->BlendMode = BLEND_Masked;
            Material->OpacityMaskClipValue = .5f;
        }
        Material->TwoSided = true;
        Material->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
        Material->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
        Material->PostEditChange();
        Material->MarkPackageDirty();
        return Material;
    }

    // Kenney palette atlases: nearest filtering without mips keeps every swatch a flat colour at any distance.
    UTexture2D* ArtImportTexture(const FString& Relative, TArray<UObject*>& Assets, FString& Error)
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
        Texture->Filter = TF_Nearest;
        Texture->MipGenSettings = TMGS_NoMipmaps;
        Texture->CompressionSettings = TC_EditorIcon;
        Texture->SRGB = true;
        Texture->NeverStream = true;
        Texture->PostEditChange();
        Texture->MarkPackageDirty();
        Assets.AddUnique(Texture);
        return Texture;
    }

    UMaterialInstanceConstant* ArtSlotMaterial(UMaterial& Master, const FString& Folder, const FString& Owner, FName Slot, const FLinearColor& Color,
        UTexture2D* Texture, TArray<UObject*>& Assets, FString& Error)
    {
        const FString PackageName = Folder / (TEXT("MI_") + Owner + TEXT("_") + ArtAssetName(Slot.ToString()));
        auto* Instance = ArtLoadOrCreate<UMaterialInstanceConstant>(PackageName, true);
        if (!Instance) { ArtFail(Error, TEXT("Cannot create material instance ") + PackageName); return nullptr; }
        Instance->SetParentEditorOnly(&Master);
        Instance->ClearParameterValuesEditorOnly();
        Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(ULDArtCatalog::TintParameter), Color);
        if (Texture) { Instance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(ArtTextureParameter), Texture); }
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

    bool ArtDecorationTransform(const FArtDecorationSpec& Spec, const FArtPropSpec& Prop, const UStaticMesh& Mesh, FTransform& Out, FString& Error)
    {
        FVector Pivot;
        if (!ArtFootprintPivot(Mesh, Pivot, Error)) { return false; }
        Out = FTransform(-Pivot) * FTransform(FRotator(0., Spec.Yaw, 0.), Spec.Location, FVector(Prop.Scale * Spec.Scale));
        return true;
    }

    // Replaces every imported slot with an art material instance: the source colour (or the manifest override) and image.
    template<typename TSlot>
    bool ArtAssignMaterials(TArray<TSlot>& Slots, const TMap<FString, FArtSourceMaterial>& Sources, const TMap<FString, FLinearColor>& Colors,
        UMaterial& Master, const FString& Folder, const FString& Owner, TArray<UObject*>& Assets, FString& Error)
    {
        TSet<FString> Seen;
        for (TSlot& Slot : Slots)
        {
            const FString Name = Slot.ImportedMaterialSlotName.ToString();
            const FArtSourceMaterial* Source = Sources.Find(Name);
            if (!Source) { return ArtFail(Error, FString::Printf(TEXT("%s slot %s is not a source material."), *Owner, *Name)); }
            UTexture2D* Texture = nullptr;
            if (!Source->Image.IsEmpty())
            {
                Texture = ArtImportTexture(Source->Image, Assets, Error);
                if (!Texture) { return false; }
            }
            const FLinearColor* Override = Colors.Find(Name);
            UMaterialInstanceConstant* Instance = ArtSlotMaterial(Master, Folder, Owner, Slot.ImportedMaterialSlotName, Override ? *Override : Source->Color, Texture, Assets, Error);
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
        const FString& Folder, const FString& Owner, const TCHAR* MasterPackage, FString& Error)
    {
        for (const TSlot& Slot : Slots)
        {
            const FString Name = Slot.ImportedMaterialSlotName.ToString();
            const FArtSourceMaterial* Source = Sources.Find(Name);
            const auto* Instance = Cast<UMaterialInstanceConstant>(Slot.MaterialInterface);
            FLinearColor Tint;
            const FLinearColor* Override = Colors.Find(Name);
            if (!Source || !Instance || !Instance->GetPathName().StartsWith(Folder) || !Instance->Parent
                || Instance->Parent->GetOutermost()->GetName() != MasterPackage
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
            }
        }
        return true;
    }

    bool ArtImportCharacter(const FArtCharacterSpec& Spec, UMaterial& Master, ULDArtCatalog& Catalog, TArray<UObject*>& Assets, FString& Error)
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
        if (!ArtAssignMaterials(Mesh->GetMaterials(), Sources, Spec.Colors, Master, Folder, Spec.Id, Assets, Error)) { return false; }
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
        UE_LOG(LogTemp, Display, TEXT("LD_DEMO_ART_BODY %s mesh=%s skeleton=%s transform=%s"), *Spec.Id, *Mesh->GetPathName(),
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

    bool ArtImportProp(const FArtPropSpec& Spec, UMaterial& Master, UMaterial& FadeMaster, ULDArtCatalog& Catalog, TArray<UObject*>& Assets, FString& Error)
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
        if (!ArtAssignMaterials(Mesh->GetStaticMaterials(), Sources, {}, Spec.bFadeable ? FadeMaster : Master, Folder, Spec.Id, Assets, Error)) { return false; }
        Mesh->PostEditChange();
        Mesh->MarkPackageDirty();
        Assets.Add(Mesh);
        FLDArtProp& Prop = Catalog.Props.AddDefaulted_GetRef();
        Prop.Id = Spec.Id;
        Prop.bFadeable = Spec.bFadeable;
        Prop.Mesh = Mesh;
        return true;
    }
}

bool LDDemoArtImport::Import(FString& Error)
{
    FArtManifest Manifest;
    if (!ArtLoadManifest(Manifest, Error)) { return false; }
    TArray<UObject*> Assets;
    bool bNewMaster = false, bNewFadeMaster = false;
    UMaterial* Master = ArtMasterMaterial(ArtMasterPackage, false, bNewMaster, Error);
    UMaterial* FadeMaster = Master ? ArtMasterMaterial(ArtFadeMasterPackage, true, bNewFadeMaster, Error) : nullptr;
    auto* Catalog = ArtLoadOrCreate<ULDArtCatalog>(ArtCatalogPackage, true);
    if (!Master || !Catalog) { return ArtFail(Error, TEXT("The art master material or catalog cannot be created: ") + Error); }
    if (!FadeMaster) { return ArtFail(Error, TEXT("The fadeable art master material cannot be created: ") + Error); }
    if (bNewMaster) { Assets.Add(Master); }
    if (bNewFadeMaster) { Assets.Add(FadeMaster); }
    Catalog->Characters.Reset();
    Catalog->Seats.Reset();
    Catalog->Monsters.Reset();
    Catalog->Props.Reset();
    Catalog->Decorations.Reset();
    for (const FArtCharacterSpec& Spec : Manifest.Characters)
    {
        if (!ArtImportCharacter(Spec, *Master, *Catalog, Assets, Error)) { return false; }
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
        if (!ArtImportProp(Spec, *Master, *FadeMaster, *Catalog, Assets, Error)) { return false; }
    }
    for (const FArtDecorationSpec& Spec : Manifest.Decorations)
    {
        const int32 Index = Manifest.Props.IndexOfByPredicate([&Spec](const FArtPropSpec& Prop) { return Prop.Id == Spec.Prop; });
        FLDArtDecoration& Decoration = Catalog->Decorations.AddDefaulted_GetRef();
        Decoration.Prop = Spec.Prop;
        Decoration.Zone = Spec.Zone;
        if (!ArtDecorationTransform(Spec, Manifest.Props[Index], *Catalog->Props[Index].Mesh, Decoration.Transform, Error)) { return false; }
    }
    FLDArtResolved Resolved;
    if (!Catalog->Resolve(Resolved, Error)) { return ArtFail(Error, TEXT("Imported art catalog is invalid: ") + Error); }
    if (!LDArt::CheckPlacementsAgainstDemoWorld(*Catalog, Error)) { return ArtFail(Error, TEXT("Decoration placement: ") + Error); }
    Catalog->MarkPackageDirty();
    Assets.Add(Catalog);
    for (UObject* Asset : Assets)
    {
        if (!ArtSave(*Asset, Error)) { return false; }
    }
    UE_LOG(LogTemp, Display, TEXT("LD_DEMO_ART_IMPORTED %d assets saved."), Assets.Num());
    Error.Reset();
    return true;
}

bool LDDemoArtImport::Validate(FString& Error)
{
    FArtManifest Manifest;
    if (!ArtLoadManifest(Manifest, Error)) { return false; }
    const auto* Catalog = ArtLoadOrCreate<ULDArtCatalog>(ArtCatalogPackage, false);
    if (!Catalog) { return ArtFail(Error, TEXT("Art catalog DA_DemoArt is missing; run ImportDemoArt.ps1 -Mode Import.")); }
    FLDArtResolved Resolved;
    if (!Catalog->Resolve(Resolved, Error)) { return ArtFail(Error, TEXT("Art catalog is invalid: ") + Error); }
    if (Catalog->Characters.Num() != Manifest.Characters.Num() || Catalog->Props.Num() != Manifest.Props.Num()
        || Catalog->Decorations.Num() != Manifest.Decorations.Num())
    {
        return ArtFail(Error, TEXT("Art catalog entries differ from art_assets.json; run ImportDemoArt.ps1 -Mode Import."));
    }
    for (int32 Index = 0; Index < Manifest.Characters.Num(); ++Index)
    {
        const FArtCharacterSpec& Spec = Manifest.Characters[Index];
        const FLDArtCharacter& Body = Catalog->Characters[Index];
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
            || !ArtReadSourceMaterials(Spec.Source, Sources, Error) || !ArtCheckMaterials(Body.Mesh->GetMaterials(), Sources, Spec.Colors, Folder, Spec.Id, ArtMasterPackage, Error))
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
            || !ArtReadSourceMaterials(Spec.Source, Sources, Error) || !ArtCheckMaterials(Prop.Mesh->GetStaticMaterials(), Sources, {}, Folder, Spec.Id, Spec.bFadeable ? ArtFadeMasterPackage : ArtMasterPackage, Error))
        {
            return false;
        }
    }
    for (int32 Index = 0; Index < Manifest.Decorations.Num(); ++Index)
    {
        const FArtDecorationSpec& Spec = Manifest.Decorations[Index];
        const FLDArtDecoration& Have = Catalog->Decorations[Index];
        const int32 Prop = Manifest.Props.IndexOfByPredicate([&Spec](const FArtPropSpec& Candidate) { return Candidate.Id == Spec.Prop; });
        FTransform Expected;
        if (Have.Prop != Spec.Prop || Have.Zone != Spec.Zone || !ArtDecorationTransform(Spec, Manifest.Props[Prop], *Catalog->Props[Prop].Mesh, Expected, Error)
            || !Expected.Equals(Have.Transform, 1e-2))
        {
            return ArtFail(Error, FString::Printf(TEXT("Decoration %d (%s) differs from art_assets.json."), Index, *Spec.Prop));
        }
    }
    if (!LDArt::CheckPlacementsAgainstDemoWorld(*Catalog, Error)) { return ArtFail(Error, TEXT("Decoration placement: ") + Error); }
    Error.Reset();
    return true;
}
