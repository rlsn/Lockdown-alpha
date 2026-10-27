#include "Tests/LDB90ClosureProbe.h"
#include "Framework/LDGameInstance.h"
#include "Framework/LDGameMode.h"
#include "Region/LDMapRegistry.h"
#include "Region/LDMapDefinition.h"
#include "Region/LDMapSurfaceSubsystem.h"
#include "Region/LDRegionDefinition.h"
#include "Region/LDRegionDirector.h"
#include "Character/LDCharacter.h"
#include "Economy/LDEconomyDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "World/LDWorldDirector.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

namespace
{
void Save(UWorld* W,const FLDB90ResetEvidence& E,bool Complete)
{
    auto R=MakeShared<FJsonObject>(); R->SetBoolField(TEXT("complete"),Complete); R->SetNumberField(TEXT("failures"),E.Failures); R->SetArrayField(TEXT("worlds"),E.Rows);
    R->SetStringField(TEXT("scope"),TEXT("Actual non-seamless B90 world reloads and fresh v08 transition. B90 spatial registry has no market/reward executors; B92 economy integration is not claimed."));
    FString Output,Json; FParse::Value(FCommandLine::Get(),TEXT("B90Output="),Output); FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));
    FFileHelper::SaveStringToFile(Json,*(Output/TEXT("Closure.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
FString Geometry(UWorld* W)
{
    TArray<FString> Lines;
    for(TActorIterator<AActor> It(W);It;++It)
    { if(!It->Tags.IsEmpty()) { Lines.Add(It->Tags[0].ToString()+TEXT("|")+It->GetActorTransform().ToString()); } }
    Lines.Sort(); return FString::Join(Lines,TEXT("\n"));
}
int32 OldAlive(const FLDB90ResetEvidence& E)
{ int32 N=0; for(const auto& A:E.OldActors) { N+=A.IsValid(); } return N; }
}
ALDB90ClosureProbe::ALDB90ClosureProbe()
{ PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PostUpdateWork; }
void ALDB90ClosureProbe::Tick(float Delta)
{ Super::Tick(Delta); if(!Ran&&GetWorld()->GetTimeSeconds()>2) { Ran=true; Run(); } }
void ALDB90ClosureProbe::Run()
{
    auto* GI=GetGameInstance<ULDGameInstance>(); if(!GI->B90ResetEvidence) { GI->B90ResetEvidence=MakeShared<FLDB90ResetEvidence>(); }
    auto& E=*GI->B90ResetEvidence; auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
    auto* Surface=GetWorld()->GetSubsystem<ULDMapSurfaceSubsystem>(); const auto* D=ULDMapDefinition::Get(GetWorld()); FString Error;
    if(!Registry||!Surface||!D||!Registry->Rebuild(Error)) { ++E.Failures; Save(GetWorld(),E,true); FPlatformMisc::RequestExitWithStatus(false,2); return; }
    auto R=Registry->Report(); const FString Bindings=Registry->CanonicalBindings(),Geo=Geometry(GetWorld());
    const bool Repeat=Registry->Rebuild(Error)&&Registry->CanonicalBindings()==Bindings;
    const bool Stable=E.Round==0||(E.Bindings==Bindings&&E.Geometry==Geo);
    const int32 Alive=OldAlive(E); int32 Roots=0,Starts=0,Economies=0,Items=0,Worlds=0;
    for(TActorIterator<ALDMapAnchor> It(GetWorld());It;++It) { ++Roots; }
    for(TActorIterator<APlayerStart> It(GetWorld());It;++It) { ++Starts; }
    for(TActorIterator<ALDEconomyDirector> It(GetWorld());It;++It) { ++Economies; }
    for(TActorIterator<ALDItem> It(GetWorld());It;++It) { ++Items; }
    for(TActorIterator<ALDWorldDirector> It(GetWorld());It;++It) { ++Worlds; }
    R->SetStringField(TEXT("profile"),TEXT("v09_full")); R->SetNumberField(TEXT("round"),E.Round);
    R->SetNumberField(TEXT("map_anchors"),Roots); R->SetNumberField(TEXT("spawn_actors"),Starts);
    R->SetNumberField(TEXT("economy_directors"),Economies); R->SetNumberField(TEXT("physical_items"),Items); R->SetNumberField(TEXT("legacy_world_directors"),Worlds);
    R->SetNumberField(TEXT("old_world_actors_alive"),Alive); R->SetNumberField(TEXT("fresh_surface_cells"),Surface->LiveCells());
    R->SetBoolField(TEXT("repeat_registry_identical"),Repeat); R->SetBoolField(TEXT("world_semantics_identical"),Stable);
    const bool Stale=!E.PreviousGeneration||!Registry->Resolve(TEXT("capital"),E.PreviousGeneration);
    R->SetBoolField(TEXT("stale_generation_rejected"),Stale);
    bool Good=Repeat&&Stable&&!Alive&&Roots==1&&Starts==24&&!Economies&&!Items&&!Worlds&&!Surface->LiveCells()
        &&Registry->Generation>E.PreviousGeneration&&Stale;
    auto* Imported=NewObject<ULDMapDefinition>(); const bool Import=Imported->Import(D->SourceJson,Error)&&Imported->Import(D->SourceJson,Error)&&Imported->Services.Num()==36&&Imported->POIs.Num()==96;
    R->SetBoolField(TEXT("repeat_import_identical"),Import); Good&=Import;
    auto* Body=Cast<ALDCharacter>(GetWorld()->GetFirstPlayerController()->GetPawn());
    Body->SetActorLocation(D->WetInset+FVector(0,0,92)); Body->GetCharacterMovement()->StopMovementImmediately();
    const int64 At=LDTactical::Milliseconds(GetWorld()->GetTimeSeconds());
    const bool Seed=Surface->SeedFrozenForProbe(D->WetInset,FGuid::NewGuid(),At,Error); Surface->Refresh(Body,At,true);
    const auto* Contact=Surface->Contact(Body); const bool Dirty=Seed&&Surface->LiveCells()>0&&Contact&&Contact->Frozen&&Contact->State.SurfaceSlow==30&&Contact->State.Has(LDTactical::EStatus::Wet,At);
    R->SetBoolField(TEXT("dirty_wet_frozen_fixture"),Dirty); R->SetNumberField(TEXT("dirty_surface_cells"),Surface->LiveCells()); Good&=Dirty;
    R->SetBoolField(TEXT("passed"),Good); E.Failures+=!Good; E.Rows.Add(MakeShared<FJsonValueObject>(R));
    E.Bindings=Bindings; E.Geometry=Geo; E.PreviousGeneration=Registry->Generation; E.OldActors.Reset();
    for(TActorIterator<AActor> It(GetWorld());It;++It) { E.OldActors.Add(*It); }
    ++E.Round; Save(GetWorld(),E,false);
    UE_LOG(LogTemp,Display,TEXT("LD_B90_RESET round=%d passed=%d generation=%u"),E.Round,Good,Registry->Generation);
    UGameplayStatics::OpenLevel(this,E.Round<3?TEXT("/Game/Lockdown/Generated/V09/L_V09Full"):TEXT("/Game/Lockdown/Generated/Maps/L_V08Region"),true,E.Round<3?TEXT("Profile=v09_full"):TEXT("Profile=v08_integration"));
}
bool ALDB90ClosureProbe::LegacyCompleted(UWorld* W)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("LDB90ClosureProbe"))) { return true; }
    auto* GI=W->GetGameInstance<ULDGameInstance>(); if(!GI||!GI->B90ResetEvidence) { return false; }
    auto& E=*GI->B90ResetEvidence; auto* M=W->GetAuthGameMode<ALDGameMode>();
    auto* Registry=W->GetSubsystem<ULDMapRegistry>(); auto* Surface=W->GetSubsystem<ULDMapSurfaceSubsystem>();
    const auto* Region=LDRegion::Definition(W); const int32 Alive=OldAlive(E);
    const bool Good=M&&M->IsV08Profile()&&M->RulesVersion()==8&&M->WorldRuntime&&M->WorldRuntime->bReady&&M->Economy
        &&!ULDMapDefinition::Get(W)&&Registry&&!Registry->Enabled()&&Registry->Sites().IsEmpty()&&Surface&&!Surface->Enabled()
        &&Region&&Region->Grid.Valid()&&M->Economy->Rules().Markets.Num()==3&&!Alive&&GI->AuthorityGeneration>E.PreviousGeneration;
    auto R=MakeShared<FJsonObject>(); R->SetStringField(TEXT("profile"),TEXT("v08_integration")); R->SetBoolField(TEXT("passed"),Good);
    R->SetNumberField(TEXT("generation"),GI->AuthorityGeneration); R->SetNumberField(TEXT("old_world_actors_alive"),Alive);
    R->SetBoolField(TEXT("v09_registry_disabled"),Registry&&!Registry->Enabled()&&Registry->Sites().IsEmpty());
    R->SetBoolField(TEXT("v09_contact_disabled"),Surface&&!Surface->Enabled()); R->SetBoolField(TEXT("v09_map_anchor_absent"),!ULDMapDefinition::Get(W));
    if(M&&M->Economy) { R->SetNumberField(TEXT("legacy_markets"),M->Economy->Rules().Markets.Num()); R->SetNumberField(TEXT("legacy_receipts"),M->Economy->Accounting().ReceiptCount()); }
    if(Region) { R->SetNumberField(TEXT("legacy_grid_cell_cm"),Region->Grid.CellSize); R->SetStringField(TEXT("legacy_grid_min"),Region->Grid.Min.ToString()); }
    E.Failures+=!Good; E.Rows.Add(MakeShared<FJsonValueObject>(R)); Save(W,E,true); return Good;
}
