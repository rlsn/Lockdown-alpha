#include "Tests/LDB91SaplingsProbe.h"
#include "World/LDEnvironmentDirector.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Navigation/LDNavigationComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ALDB91SaplingsProbe::ALDB91SaplingsProbe(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;Started=FPlatformTime::Seconds();}
void ALDB91SaplingsProbe::Check(bool Good,const FString& Text)
{auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("case"),Text);R->SetBoolField(TEXT("passed"),Good);Checks.Add(MakeShared<FJsonValueObject>(R));Failures+=!Good;UE_LOG(LogTemp,Display,TEXT("LD_B91_SAPLINGS %s passed=%d"),*Text,Good);}
void ALDB91SaplingsProbe::Near(int Index,bool Both)
{
    Target=Objects->Find(FName(*FString::Printf(TEXT("prop_sapling_%03d"),Index)));
    auto Place=[](ALDB91Controller* PC,FVector P){PC->Navigation->ResetLife();auto* Body=Cast<ALDCharacter>(PC->GetPawn());Body->GetCharacterMovement()->StopMovementImmediately();Body->SetActorLocation(P);PC->ClientSetLocation(P,FRotator::ZeroRotator);};
    Place(A,Target->LastClear+FVector(-94,0,92.15));Place(B,Target->LastClear+FVector(Both?94:500,0,92.15));
}
void ALDB91SaplingsProbe::Task(uint8 Value){Phase=Value;At=GetWorld()->GetTimeSeconds();Objects->Publish();A->ClientB91ProbeTask(Value);B->ClientB91ProbeTask(Value);Save(false);}
void ALDB91SaplingsProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(Phase==99){return;}const double Now=GetWorld()->GetTimeSeconds();auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M){return;}
    if(Failures||FPlatformTime::Seconds()-Started>240){if(!Failures){Check(false,TEXT("Sapling fixture timeout"));}Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,3);return;}
    auto Count=[&](){int N=0;for(TActorIterator<ALDItem> I(GetWorld());I;++I){++N;}return N;};
    auto ProxyClear=[](ALDWorldObject* O){return O->Broken&&O->Proxy->GetCollisionEnabled()==ECollisionEnabled::NoCollision&&!O->Proxy->CanEverAffectNavigation()&&O->Revision==O->CollisionRevision&&O->Revision==O->SightRevision&&O->Revision==O->NavigationRevision;};
    if(!Phase)
    {
        for(FConstPlayerControllerIterator I=GetWorld()->GetPlayerControllerIterator();I;++I){if(auto* PC=Cast<ALDB91Controller>(I->Get());PC&&PC->GetPawn()){if(PC->EnvironmentProbeRole==1){A=PC;}if(PC->EnvironmentProbeRole==2){B=PC;}}}
        if(!A||!B||Now<8){return;}Objects=ALDWorldObjects::Get(GetWorld());if(!Objects||!Objects->Ready){return;}AddTickPrerequisiteActor(Objects);
        Check(A!=B&&!A->IsLocalController()&&!B->IsLocalController(),TEXT("Two independent remote sapling owners"));Check(Objects->Objects.Num()==435&&Count()==143,TEXT("64 authored saplings add no initial physical reward"));
        int Sources=0;bool Good=true;for(auto O:Objects->Objects){if(!O.IsValid()||O->Kind!=ELDObjectKind::Sapling){continue;}++Sources;FVector Surface;Good&=Objects->Placement(O.Get(),O->LastClear,Surface)&&O->Hp==80&&!O->Container()&&!O->Heavy()&&!O->HarvestSource.IsNone();FHitResult Hit;FCollisionQueryParams Q;
         const FVector Eye=O->LastClear+FVector(0,0,90);Good&=GetWorld()->LineTraceSingleByChannel(Hit,Eye-FVector(61,0,0),Eye+FVector(61,0,0),ECC_GameTraceChannel2,Q)&&Hit.GetActor()==O.Get();
         auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("id"),O->SourceId.ToString());Row->SetStringField(TEXT("finite_source"),O->HarvestSource.ToString());Row->SetStringField(TEXT("source"),O->SourcePosition.ToString());Row->SetStringField(TEXT("fitted"),O->LastClear.ToString());Manifest.Add(MakeShared<FJsonValueObject>(Row));}
        Check(Sources==64&&Good,TEXT("All64 supported80HP saplings fit and block sight without becoming containers"));
        int ToolIndex=0;for(auto* PC:{A,B}){auto* Knife=Objects->Find(FName(*FString::Printf(TEXT("prop_forestry_knife_%03d"),ToolIndex++)))->Physical.Get();Check(M->Inventory->Adopt(PC,Knife),TEXT("Fixture transfers an existing finite knife; client must bind it through the real channel"));M->StateFor(PC)->ProtectedUntil=0;M->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()))->ProtectionUntil=0;M->SyncOwner(PC);}Near(0);
        auto* Body=Cast<ALDCharacter>(A->GetPawn());const uint32 Before=Target->Revision;
        Check(Objects->MeleeTarget(Body,FVector(1,0,0),120)==Target&&Objects->MeleeTarget(Body,FVector(1,0,0),120)==Target&&Target->Hp==80&&Target->Revision==Before&&!Target->Broken,TEXT("Melee selection leaves the shared pre-damage world unchanged"));
        Phase=80;At=Now;return;
    }
    if(Phase==83){if(Now-At>6.3){Task(15);}return;}
    if(Phase==80){if(Now-At>1){Task(1);}return;}
    if(Phase==81)
    {if(Now-At<6.3){return;}Near(4,false);for(TActorIterator<ALDItem> I(GetWorld());I&&M->StateFor(A)->Inventory.Items.Num()<24;++I){if(I->State==0&&!I->Holder.IsValid()&&!I->bReserved&&M->Inventory->Adopt(A,*I)){Staged.Add(*I);}}Check(M->StateFor(A)->Inventory.Items.Num()==24,TEXT("Capacity setup moves existing ground items into the bag without minting"));Task(6);return;}
    if(Phase==82)
    {auto* Own=M->StateFor(Winner);if(!Own||!Own->bActive||!Winner->GetPawn()){return;}if(!Returned){Returned=Now;for(auto O:Objects->Objects){if(O.IsValid()&&O->Kind==ELDObjectKind::ForestryKnife&&O->Physical.IsValid()&&O->Physical->State==0){M->Inventory->Adopt(Winner,O->Physical.Get());break;}}Own->ProtectedUntil=0;M->Combat->StateFor(Cast<ALDCharacter>(Winner->GetPawn()))->ProtectionUntil=0;M->SyncOwner(Winner);Near(0);}
     if(Now-Returned>6.3){Task(7);}return;}
    if(Phase==4){if(const auto* H=Objects->Holds.Find(A);H&&Now-H->Heartbeat>.5){SawBriefGap=true;}}
    auto Read=[](ALDB91Controller* PC,uint8 Value)->TSharedPtr<FJsonObject>{TSharedPtr<FJsonObject> R;const auto* S=PC->EnvironmentProbeReports.Find(Value);if(S){FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(*S),R);}return R;};const auto RA=Read(A,Phase),RB=Read(B,Phase);if(!RA||!RB){return;}
    Check(RA->GetBoolField(TEXT("completed"))&&RB->GetBoolField(TEXT("completed")),FString::Printf(TEXT("Both remote sapling phase%u reports complete"),Phase));
    if(Phase==1)
    {Winner=RA->GetIntegerField(TEXT("result"))==1?A:B;Check((RA->GetIntegerField(TEXT("result"))==1)+(RB->GetIntegerField(TEXT("result"))==1)==1&&Count()==144,TEXT("Harvest race awards exactly one existing source receipt and one Fiber"));Check(Target->YieldIdentity.IsValid()&&Target->HarvestReceipt.IsValid()&&ProxyClear(Target),TEXT("Harvest commits one physical reward and a shared collision/sight/nav removal"));Check(RA->GetBoolField(TEXT("duplicate_same"))&&RB->GetBoolField(TEXT("duplicate_same")),TEXT("Harvest retry replays the original winning or losing receipt"));Near(1);Task(2);return;}
    if(Phase==2)
    {Check(Target->Hp==0&&!Target->YieldIdentity.IsValid()&&Count()==144&&ProxyClear(Target),TEXT("Four-second chop clears the blocker without creating Fiber"));Near(2,false);Task(3);return;}
    if(Phase==3)
    {Check(RA->GetIntegerField(TEXT("result"))==2&&Target->Hp==80&&!Target->Broken&&Count()==144,TEXT("S stops harvest once; source and knife are retained without automatic restart"));auto* Body=Cast<ALDCharacter>(A->GetPawn());M->Combat->ApplyStatus(Body,LDTactical::EStatus::Silence,LDTactical::Milliseconds(Now)+6000,FGuid::NewGuid());Task(4);return;}
    if(Phase==4)
    {Check(SawBriefGap,TEXT("An issued harvest survives an observed heartbeat gap longer than500ms"));Check(RA->GetIntegerField(TEXT("result"))==1&&Target->YieldIdentity.IsValid()&&Count()==145,TEXT("Silence alone permits a real bound item harvest"));Check(ProxyClear(Target),TEXT("Second source removes its own proxies with one reward only"));Near(3,false);Task(5);return;}
    if(Phase==5)
    {Check(Target->Hp==0&&Target->Revision>2&&!Target->YieldIdentity.IsValid()&&Count()==145&&ProxyClear(Target),TEXT("Ordinary RPC melee windups and cooldowns clear80HP through multiple hits without harvest reward"));Phase=81;At=Now;return;}
    if(Phase==6)
    {Check(RA->GetIntegerField(TEXT("result"))==2&&Target->Hp==80&&!Target->Broken&&Count()==145,TEXT("Full inventory rejects harvest before spending the finite source"));for(auto P:Staged){M->Inventory->Remove(A,P.Get(),false);}Staged.Reset();FString Why;Check(Objects->Conserved(Why),TEXT("Capacity setup restores all original item locations without duplication"));M->EndContenderLife(Winner,Now);Phase=82;At=Now;return;}
    if(Phase==7)
    {FString Why;Check(Objects->Initialize(Why)&&Target->Broken&&Target->YieldIdentity.IsValid()&&ProxyClear(Target),TEXT("Same-world reinitialization preserves the harvested source and cleared proxies"));Check((Winner==A?RA:RB)->GetIntegerField(TEXT("result"))==2&&Count()==145,TEXT("New life cannot harvest a previously exhausted source"));Near(4,false);Task(8);return;}
    if(Phase==8)
    {FString Why;Check(RA->GetIntegerField(TEXT("result"))==1&&Target->YieldIdentity.IsValid()&&Count()==146,TEXT("After capacity recovery a fresh source awards exactly one Fiber"));Check(Objects->Conserved(Why),TEXT("All146 physical identities occupy exactly one location"));int Outputs=0,Cleared=0;TSet<FGuid> Ids,Receipts;
     for(auto O:Objects->Objects){if(O.IsValid()&&O->Kind==ELDObjectKind::Sapling){Cleared+=O->Broken;if(O->YieldIdentity.IsValid()){++Outputs;Ids.Add(O->YieldIdentity);Receipts.Add(O->HarvestReceipt);}}}
     Check(Outputs==3&&Ids.Num()==3&&Receipts.Num()==3&&Cleared==5,TEXT("Three finite harvests, one chop and one damage clearance never respawn or duplicate outputs"));Near(5,false);M->Combat->ApplyStatus(Cast<ALDCharacter>(A->GetPawn()),LDTactical::EStatus::Mute,LDTactical::Milliseconds(Now)+8000,FGuid::NewGuid());Task(9);return;}
    if(Phase==9){Check(RA->GetIntegerField(TEXT("result"))==2&&Target->Hp==80&&!Target->YieldIdentity.IsValid(),TEXT("Mute blocks the Forestry Knife and leaves its finite sapling untouched"));Task(10);return;}
    if(Phase==10){Check(RA->GetIntegerField(TEXT("result"))==2&&Target->Hp==80,TEXT("A raw source-free harvest cannot bypass the new item requirement"));Target=Objects->Find(TEXT("prop_water_flask_001"));const FVector P=Target->LastClear+FVector(-90,0,92.15);A->GetPawn()->SetActorLocation(P);A->ClientSetLocation(P,FRotator::ZeroRotator);Task(11);return;}
    if(Phase==11){FString Why;Check(Target->Broken&&!Target->Physical.IsValid()&&Objects->Conserved(Why)&&Count()==145,TEXT("One ordinary basic attack consumes the actual ground bottle and preserves finite conservation"));auto* D=ALDEnvironmentDirector::Get(GetWorld())->Door(TEXT("building_riverport_00_door_south"));D->Apply(0,++ALDEnvironmentDirector::Get(GetWorld())->MutationRevision);const FVector P=D->GetActorLocation()+FVector(0,-85,-37.85);A->GetPawn()->SetActorLocation(P);A->ClientSetLocation(P,FRotator::ZeroRotator);Task(12);return;}
    if(Phase==12){auto* D=ALDEnvironmentDirector::Get(GetWorld())->Door(TEXT("building_riverport_00_door_south"));Check(D->Open,TEXT("One right-click opens an ordinary door without a held input"));Task(13);return;}
    if(Phase==13){auto* D=ALDEnvironmentDirector::Get(GetWorld())->Door(TEXT("building_riverport_00_door_south"));Check(!D->Open,TEXT("One right-click closes the same ordinary doorway"));Target=Objects->Find(TEXT("prop_chest_000"));const FVector P=Target->LastClear+FVector(-110,0,92.15);A->GetPawn()->SetActorLocation(P);A->ClientSetLocation(P,FRotator::ZeroRotator);Task(14);return;}
    if(Phase==14){Check(RA->GetIntegerField(TEXT("result"))==1,TEXT("One left-click opens observed storage and obtains a private contents view"));Target=Objects->Find(TEXT("prop_forestry_knife_002"));const FVector P=Target->LastClear+FVector(-300,0,92.15);A->GetPawn()->SetActorLocation(P);A->ClientSetLocation(P,FRotator::ZeroRotator);Phase=83;At=Now;return;}
    if(Phase==15){Check(Target->Physical.IsValid()&&Target->Physical->Holder==A,TEXT("One right-click approaches and picks up the same physical knife"));Near(6,false);Task(16);return;}
    if(Phase==16){FString Why;Check(RA->GetIntegerField(TEXT("result"))==2&&!Objects->Holds.Contains(A)&&Target->Hp==80&&!Target->Broken&&Count()==145&&Objects->Conserved(Why),TEXT("Two seconds without command liveness cancel before harvest; late refresh cannot restart or spend its source"));Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,Failures?3:0);return;}
#endif
}
void ALDB91SaplingsProbe::Save(bool Complete)
{
    FString Output;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Output);if(Output.IsEmpty()){return;}auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("complete"),Complete);R->SetNumberField(TEXT("phase"),Phase);R->SetNumberField(TEXT("failures"),Failures);R->SetArrayField(TEXT("checks"),Checks);R->SetArrayField(TEXT("manifest"),Manifest);R->SetStringField(TEXT("contract"),Objects?Objects->Contract:TEXT(""));FString Why;R->SetBoolField(TEXT("conserved"),Objects&&Objects->Conserved(Why));R->SetBoolField(TEXT("saw_brief_gap"),SawBriefGap);R->SetStringField(TEXT("scope"),TEXT("Two remote owners issue one source command with no physical key held. Real quiet binding, automatic command heartbeats, owner progress, S stop and Mute/source refusal. Knives and full bag are staged from existing finite items. Ordinary melee uses combat RPC. End-life/return and same-world Initialize are tested; cross-process saved-match restoration, burning/socket fields, ranged/projectile or spell sapling damage, grass and full-stage acceptance remain open."));FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(Output/TEXT("Saplings.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
