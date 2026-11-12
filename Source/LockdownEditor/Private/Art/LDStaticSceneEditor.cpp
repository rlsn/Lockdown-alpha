#include "LDStaticSceneEditor.h"
#include "Art/LDStaticScene.h"
#include "Art/LDArtPlacement.h"
#include "AssetCompilingManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Engine/TargetPoint.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Framework/LDDemoEntry.h"
#include "Misc/PackageName.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapDefinition.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "ScaffoldBootstrap/LDDemoWorldBuilder.h"
#include "UObject/StrongObjectPtr.h"

bool LDStaticSceneEditor::RefreshIndex(UWorld& World, FString& Error)
{
    auto* Catalog = LoadObject<ULDArtCatalog>(nullptr, ULDArtCatalog::AssetPath);
    TArray<FLDArtDecoration> Placements;
    TArray<FString> Hidden;
    if (!Catalog) { Error = TEXT("The saved art catalog is missing."); return false; }
    if (!LDStaticScene::Read(World, *Catalog, Placements, Hidden, Error)) { return false; }
    Catalog->Decorations = MoveTemp(Placements);
    Catalog->HiddenBlockers = MoveTemp(Hidden);
    if (!LDArt::CheckPlacementsAgainstDemoWorld(*Catalog, Error)) { return false; }
    Catalog->MarkPackageDirty();
    return LDDemoWorldBuilder::SaveAsset(*Catalog, TEXT("/Game/Lockdown/Art/DA_DemoArt"), Error);
}

bool LDStaticSceneEditor::NormalizeCopies(UWorld& World, const FLDDemoWorldGeometry& Base, FString& Error)
{
    TSet<FString> Seen;
    for (TActorIterator<AStaticMeshActor> It(&World); It; ++It)
    {
        if (!It->ActorHasTag(LDDemoWorld::StaticGeometryTag)) { continue; }
        const FString Id = It->Tags[0].ToString();
        if (Seen.Contains(Id))
        {
            const auto* Prototype = Base.Blockers.FindByPredicate([&](const FLDDemoWorldBlocker& B) { return B.Id == Id; });
            if (!Prototype) { Error = TEXT("A ground/terrain identity cannot be duplicated: ") + Id; return false; }
            const FString NewId = (Prototype->Shape == ELDDemoWorldBlockerShape::Cylinder ? TEXT("tree_edit_") : TEXT("block_edit_"))
                + It->GetActorGuid().ToString(EGuidFormats::Digits).ToLower();
            It->Tags[0] = FName(*NewId);
            It->Tags.Add(FName(*(TEXT("LDScenePrototype:") + Id)));
            Seen.Add(NewId);
        }
        else { Seen.Add(Id); }
    }
    const auto* Catalog = LoadObject<ULDArtCatalog>(nullptr, ULDArtCatalog::AssetPath);
    for (auto* Part : LDStaticScene::Visuals(World))
    {
        const auto* Prop = Catalog ? Catalog->Props.FindByPredicate([&](const FLDArtProp& P) { return P.Mesh == Part->GetStaticMesh(); }) : nullptr;
        if (Prop && Prop->bFadeable) { Part->ComponentTags.AddUnique(ULDArtCatalog::FadeableTag); }
        else { Part->ComponentTags.Remove(ULDArtCatalog::FadeableTag); }
    }
    return true;
}

bool LDStaticSceneEditor::Export(const FString& Filename, FString& Error)
{
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(FPackageName::LongPackageNameToFilename(
        LDDemoEntry::WorldMap, FPackageName::GetMapPackageExtension()));
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TSharedPtr<const FLDDemoWorldGeometry> Base;
    if (!World || !LDArt::LoadDemoWorld(Content, Base, Error)) { return false; }
    if (!NormalizeCopies(*World, *Base, Error)) { return false; }
    FLDDemoWorldGeometry Geometry;
    if (!LDStaticScene::ProjectGeometry(*World, *Base, Geometry, Error)) { return false; }
    const auto Number = [](const FLDDemoRational& V) { return double(V.Numerator) / V.Denominator; };
    const auto Point = [&](const FLDDemoWorldPoint& P)
    {
        auto J = MakeShared<FJsonObject>();
        J->SetNumberField(TEXT("x"), Number(P.X)); J->SetNumberField(TEXT("y"), Number(P.Y)); J->SetNumberField(TEXT("z"), Number(P.Z));
        return MakeShared<FJsonValueObject>(J);
    };
    const auto Polygon = [&](const TArray<FLDDemoWorldPoint>& Ring)
    { TArray<TSharedPtr<FJsonValue>> Values; for (const auto& P : Ring) { Values.Add(Point(P)); } return Values; };
    auto Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schema_version"), 1);
    Root->SetStringField(TEXT("source_sha256"), Base->SourceDigest.ToHex());
    TArray<TSharedPtr<FJsonValue>> Surfaces, Plateaus, Ramps, Blockers, Anchors;
    for (const auto& S : Geometry.Surfaces)
    {
        auto J = MakeShared<FJsonObject>(); J->SetStringField(TEXT("id"), S.Id);
        J->SetField(TEXT("min_cm"), Point(S.MinCm)); J->SetField(TEXT("max_cm"), Point(S.MaxCm)); Surfaces.Add(MakeShared<FJsonValueObject>(J));
    }
    for (const auto& P : Geometry.Plateaus)
    {
        auto J = MakeShared<FJsonObject>(); J->SetStringField(TEXT("id"), P.Id);
        J->SetArrayField(TEXT("polygon_cm"), Polygon(P.PolygonCm)); Plateaus.Add(MakeShared<FJsonValueObject>(J));
    }
    for (const auto& P : Geometry.Ramps)
    {
        auto J = MakeShared<FJsonObject>(); J->SetStringField(TEXT("id"), P.Id);
        J->SetArrayField(TEXT("polygon_cm"), Polygon(P.PolygonCm)); Ramps.Add(MakeShared<FJsonValueObject>(J));
    }
    for (const auto& B : Geometry.Blockers)
    {
        auto J = MakeShared<FJsonObject>(); J->SetStringField(TEXT("id"), B.Id);
        J->SetStringField(TEXT("shape"), B.Shape == ELDDemoWorldBlockerShape::Cylinder ? TEXT("cylinder") : TEXT("polygon"));
        J->SetStringField(TEXT("surface_id"), B.SurfaceId);
        J->SetNumberField(TEXT("height_cm"), Number(B.HeightCm));
        J->SetBoolField(TEXT("blocks_movement"), B.BlocksMovement); J->SetBoolField(TEXT("blocks_sight"), B.BlocksSight);
        if (B.Shape == ELDDemoWorldBlockerShape::Cylinder)
        { J->SetField(TEXT("center_cm"), Point(B.CenterCm)); J->SetNumberField(TEXT("radius_cm"), Number(B.RadiusCm)); }
        else { J->SetArrayField(TEXT("polygon_cm"), Polygon(B.PolygonCm)); }
        Blockers.Add(MakeShared<FJsonValueObject>(J));
    }
    for (const auto& A : Geometry.Anchors)
    {
        auto J = MakeShared<FJsonObject>(); J->SetStringField(TEXT("id"), A.Id); J->SetField(TEXT("position_cm"), Point(A.PositionCm));
        TArray<TSharedPtr<FJsonValue>> Members;
        for (const auto& M : A.Members)
        { auto V = MakeShared<FJsonObject>(); V->SetStringField(TEXT("id"), M.Id); V->SetField(TEXT("position_cm"), Point(M.PositionCm)); Members.Add(MakeShared<FJsonValueObject>(V)); }
        J->SetArrayField(TEXT("members"), Members); Anchors.Add(MakeShared<FJsonValueObject>(J));
    }
    Root->SetArrayField(TEXT("surfaces"), Surfaces); Root->SetArrayField(TEXT("plateaus"), Plateaus);
    Root->SetArrayField(TEXT("ramps"), Ramps); Root->SetArrayField(TEXT("blockers"), Blockers); Root->SetArrayField(TEXT("anchors"), Anchors);
    FString Json;
    if (!FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json))
        || !FFileHelper::SaveStringToFile(Json, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        || !UEditorLoadingAndSavingUtils::SaveMap(World, LDDemoEntry::WorldMap))
    { Error = TEXT("Could not save the scene export and normalized object identities."); return false; }
    return true;
}
