#include "Tests/LDB91EncounterProbe.h"
#include "World/LDEnvironmentDirector.h"
#include "World/LDB91Checkpoint.h"
#include "World/LDB91Forest.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Inventory/LDInventoryDirector.h"
#include "Region/LDMapDefinition.h"
#include "Navigation/LDNavigationComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
ALDB91EncounterProbe::ALDB91EncounterProbe(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;Started=FPlatformTime::Seconds();}
void ALDB91EncounterProbe::Check(bool Good,const FString& Text){auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("case"),Text);R->SetBoolField(TEXT("passed"),Good);Checks.Add(MakeShared<FJsonValueObject>(R));Failures+=!Good;UE_LOG(LogTemp,Display,TEXT("LD_B91_ENCOUNTER %s passed=%d"),*Text,Good);}
void ALDB91EncounterProbe::Task(uint8 Value,FVector Point){Phase=Value;At=GetWorld()->GetTimeSeconds();Objects->Publish();ALDEnvironmentDirector::Get(GetWorld())->Publish();A->ClientB91ReferencePoint(Point);B->ClientB91ReferencePoint(Point);A->ClientB91ProbeTask(Value);B->ClientB91ProbeTask(Value);Save(false);}
void ALDB91EncounterProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(Phase==99){return;}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* E=ALDEnvironmentDirector::Get(GetWorld());if(!M||!E){return;}const double Now=GetWorld()->GetTimeSeconds();FString Out,Why;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Out);
    if(Failures||FPlatformTime::Seconds()-Started>300){if(!Failures){Check(false,TEXT("Reference encounter timed out"));}Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,3);return;}
    Objects=ALDWorldObjects::Get(GetWorld());if(!Objects||!Objects->Ready||Now<8){return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("LDB91ForestRoutes")))
    {
        if(UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld())){return;}auto* Map=ULDMapDefinition::Get(GetWorld());const int Total=Map->Spawns.Num()*Map->Services.Num();
        for(int Batch=0;Batch<24&&RouteIndex<Total;++Batch,++RouteIndex)
        {const auto& S=Map->Spawns[RouteIndex/Map->Services.Num()];const auto& D=Map->Services[RouteIndex%Map->Services.Num()];const FVector Goal=D.Position+FVector(0,-1150,0);const auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),S.Position+FVector(0,0,2.15),Goal);const bool Good=Path&&Path->IsValid()&&!Path->IsPartial()&&FVector::Dist2D(Path->PathPoints.Last(),Goal)<100;auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("spawn"),S.Id.ToString());R->SetStringField(TEXT("service_front"),D.Id.ToString());R->SetBoolField(TEXT("passed"),Good);R->SetNumberField(TEXT("length_cm"),Good?Path->GetPathLength():0);RouteRows.Add(MakeShared<FJsonValueObject>(R));if(!Good){Check(false,TEXT("Forest route failed: ")+S.Id.ToString()+TEXT(" to ")+D.Id.ToString());}}
        if(RouteIndex<Total){return;}Check(Total==864,TEXT("All24 spawn pads retain native paths to all36 public service front approaches with3000 trunks present"));Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,Failures?3:0);return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("LDB91EncounterResume")))
    {
        LDCheckpoint::FData Saved,Actual;FString Text;Check(Objects->bCheckpointRestored&&FFileHelper::LoadFileToString(Text,*(Out/TEXT("EarnedWorld.json")))&&LDCheckpoint::Open(Text,Objects->Contract,E->Contract,Saved,Why)&&Objects->CaptureCheckpoint(Actual,Why),TEXT("New ownerless process restores the earned encounter checkpoint"));
        bool Exact=Saved.Objects.Num()==Actual.Objects.Num()&&Saved.Items.Num()==Actual.Items.Num()&&Saved.Consumed.Num()==Actual.Consumed.Num();
        for(const auto& R:Saved.Objects){const auto* P=Actual.Objects.FindByPredicate([&](const auto& V){return V.Id==R.Id;});Exact&=P&&P->Position.Equals(R.Position,.1)&&P->Hp==R.Hp&&P->Broken==R.Broken&&P->Harvest==R.Harvest&&P->Yield==R.Yield&&P->Burn==R.Burn&&P->Contents==R.Contents&&P->Physical==R.Physical;}
        for(const auto& I:Saved.Items){const auto* P=Actual.Items.FindByPredicate([&](const auto& V){return V.Id==I.Id;});Exact&=P&&P->Receipt==I.Receipt&&P->Origin==I.Origin&&P->Kind==I.Kind&&P->Fuel==I.Fuel&&P->Location==I.Location;}
        Check(Exact&&Objects->Conserved(Why),TEXT("Every original identity, receipt, source consequence, stored item and spent fuel survives restart"));Check(!Objects->RestoreCheckpoint(Out/TEXT("EarnedWorld.json"),Why),TEXT("Earned checkpoint cannot be applied twice"));Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,Failures?3:0);return;
    }
    if(!Phase)
    {
        for(FConstPlayerControllerIterator I=GetWorld()->GetPlayerControllerIterator();I;++I){if(auto* PC=Cast<ALDB91Controller>(I->Get());PC&&PC->GetPawn()){if(PC->EnvironmentProbeRole==1){A=PC;}if(PC->EnvironmentProbeRole==2){B=PC;}}}if(!A||!B){return;}
        AddTickPrerequisiteActor(Objects);Base=ULDMapDefinition::Get(GetWorld())->Buildings.FindByPredicate([](const auto& V){return V.Id==TEXT("building_riverport_00");})->Position;Storage=Objects->Find(TEXT("prop_crate_002"));Sapling=Objects->Find(TEXT("prop_sapling_056"));Grass=Objects->Find(TEXT("prop_tall_grass_041"));Anchor=Objects->Find(TEXT("roof_anchor_06"));Knife=Objects->Find(TEXT("prop_forestry_knife_000"))?Objects->Find(TEXT("prop_forestry_knife_000"))->Physical.Get():nullptr;
        if(!Knife){for(auto W:Objects->Objects){if(W->Kind==ELDObjectKind::ForestryKnife){Knife=W->Physical.Get();break;}}}Torch=Objects->Find(TEXT("prop_torch_002"))->Physical.Get();Rope=Objects->Find(TEXT("prop_rope_002"))->Physical.Get();
        Check(Storage&&Sapling&&Grass&&Anchor&&Knife&&Torch&&Rope,TEXT("Reference uses named finite storage, foliage, grass, roof anchor and existing tools"));if(Failures){return;}
        Check(Objects->InteriorStoragePad(Storage,Storage->LastClear)&&Storage->Contents.IsEmpty(),TEXT("Riverport warehouse crate is physically indoors with its original empty storage"));
        Check(M->Inventory->Adopt(A,Knife)&&M->Inventory->Adopt(A,Torch)&&M->Inventory->Adopt(A,Rope),TEXT("Labelled starting-tool fixture transfers three existing instances without minting"));
        for(auto* PC:{A,B}){M->StateFor(PC)->ProtectedUntil=0;M->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()))->ProtectionUntil=0;M->SyncOwner(PC);PC->Navigation->ResetLife();auto* Body=Cast<ALDCharacter>(PC->GetPawn());Body->GetCharacterMovement()->StopMovementImmediately();const FVector P=PC==A?Base+FVector(0,-1150,92.15):Grass->LastClear+FVector(500,0,92.15);Body->SetActorLocation(P);Body->GetCharacterMovement()->SetMovementMode(PC==A?MOVE_Walking:MOVE_None);PC->ClientSetLocation(P,FRotator::ZeroRotator);}
        Last=A->GetPawn()->GetActorLocation();Phase=90;At=Now;return;
    }
    if(Phase==90){if(Now-At>1){Task(1,Base+FVector(-1050,-1150,0));}return;}
    if(Phase==91)
    {if(Now-At<2){return;}if(!Objects->CheckpointSettled(Why)){if(Now-At>20){Check(false,TEXT("Retired encounter settles: ")+Why);}return;}Check(Objects->SaveCheckpoint(Out/TEXT("EarnedWorld.json"),Why),TEXT("Save actual harvested reward, burnt grass, retrieved rope, source items and open rear after normal retirement: ")+Why);Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,Failures?3:0);return;}
    if(!IsValid(A)||!IsValid(B)||!A->GetPawn()||!B->GetPawn()){Check(false,TEXT("Reference owner disconnected before completion"));Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,3);return;}
    auto* Body=Cast<ALDCharacter>(A->GetPawn());const FVector P=Body->GetActorLocation();Distance+=FVector::Dist(P,Last);Last=P;if(Body->GetCharacterMovement()->IsFalling()){Falling+=Delta;}Trace+=FString::Printf(TEXT("%u,%.6f,%.3f,%.3f,%.3f\n"),Phase,Now,P.X,P.Y,P.Z);
    if(Phase==22&&Objects->Holds.Contains(A)){SawChannel=true;}
    auto Read=[](ALDB91Controller* PC,uint8 N){TSharedPtr<FJsonObject> R;if(const auto* S=PC->EnvironmentProbeReports.Find(N)){FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(*S),R);}return R;};auto RA=Read(A,Phase),RB=Read(B,Phase);if(!RA||!RB){return;}Check(RA->GetBoolField(TEXT("completed"))&&RB->GetBoolField(TEXT("completed")),FString::Printf(TEXT("Actual remote reference phase%u completes"),Phase));if(Failures){return;}
    switch(Phase)
    {
    case 1:Task(2,Base+FVector(-1050,1150,0));break;
    case 2:Task(3,Base+FVector(0,980,0));break;
    case 3:Task(4);break;
    case 4:Check(E->Door(TEXT("building_riverport_00_door_north"))->Open,TEXT("One RMB order opens the rear after walking from public frontage"));Task(5,Storage->LastClear+FVector(0,180,0));break;
    case 5:Task(6,Storage->LastClear);break;
    case 6:Task(7);break;
    case 7:Check(Knife->Holder==Storage,TEXT("Real storage transaction places the original Knife inside the warehouse crate"));Task(8,Storage->LastClear);break;
    case 8:Task(9);break;
    case 9:Check(Knife->Holder==A,TEXT("Current private container quote recovers exactly the stored Knife identity"));Task(10);break;
    case 10:Task(11,Base+FVector(0,1150,0));break;
    case 11:Task(12,Anchor->Lower);break;
    case 12:Task(13,Anchor->Lower);break;
    case 13:Check(Anchor->LinkInstalled()&&Rope->Holder==Anchor,TEXT("Prepared original Rope installs at the approved Riverport roof pair"));Task(14,Anchor->Lower);break;
    case 14:Check(FVector::Dist(P,Anchor->Upper+FVector(0,0,92.15))<5,TEXT("Actual optional link takes the same owner onto the supported roof landing"));Task(15,Base+FVector(0,740,400));break;
    case 15:Task(16,Base+FVector(430,-750,0));break;
    case 16:Task(17,Base+FVector(0,1150,0));break;
    case 17:Task(18,Anchor->Lower);break;
    case 18:Task(19,Anchor->Lower);break;
    case 19:Check(!Anchor->LinkInstalled()&&Rope->Holder==A,TEXT("Deployer recovers the same portable Rope after a permanent-stair return"));Task(20,Sapling->LastClear+FVector(0,600,0));break;
    case 20:Task(21);break;
    case 21:Check(FVector::Dist2D(P,Sapling->LastClear)>300,TEXT("Harvest confirmation begins outside item reach"));Task(22,Sapling->LastClear);break;
    case 22:Check(SawChannel&&Sapling->Broken&&Sapling->HarvestReceipt.IsValid()&&Sapling->YieldIdentity.IsValid()&&M->StateFor(A)->Inventory.Find(Sapling->YieldIdentity),TEXT("One item confirmation walks into reach then channels4s and yields one source-backed Fiber"));Task(23,Grass->LastClear);break;
    case 23:Task(24);break;
    case 24:Check(Objects->InsideGrass(Body)&&M->Combat->Concealed(Body)&&!E->Visible(B,Body)&&!RB->GetBoolField(TEXT("other_body_seen")),TEXT("Continuous frontage, rear, warehouse, roof and grass journey ends concealed from the remote observer"));Check(Distance>10000&&Falling<.1,TEXT("Reference journey records actual movement without falling or restaging"));Task(25);break;
    case 25:Task(26,Grass->LastClear);break;
    case 26:Task(27);break;
    case 27:
        Check(Grass->Broken&&Grass->BurnReceipt.IsValid()&&Torch->FuelMs==179000,TEXT("Same owner pays retained Torch fuel and permanently clears the escape grass"));{int Count=0;for(TActorIterator<ALDB91Forest> I(GetWorld());I;++I){Count+=I->Trunks->GetInstanceCount();}Check(Count==3000,TEXT("Paid functional grass ignition leaves all3000 scenery trees intact"));}
        Check(Objects->Conserved(Why),TEXT("Journey conserves all initial goods plus exactly one finite harvest reward"));
        for(auto* PC:{A,B}){M->Logout(PC);if(auto* Pawn=PC->GetPawn().Get()){Pawn->Destroy();}PC->Destroy();}A=B=nullptr;Phase=91;At=Now;break;
    }
#endif
}
void ALDB91EncounterProbe::Save(bool Complete){FString Out;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Out);const bool Resume=FParse::Param(FCommandLine::Get(),TEXT("LDB91EncounterResume"));auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("complete"),Complete);R->SetNumberField(TEXT("phase"),Phase);R->SetNumberField(TEXT("failures"),Failures);R->SetArrayField(TEXT("checks"),Checks);R->SetArrayField(TEXT("routes"),RouteRows);R->SetNumberField(TEXT("walked_cm"),Distance);R->SetNumberField(TEXT("falling_s"),Falling);R->SetStringField(TEXT("scope"),TEXT("Named existing tools are staged in the initial inventory; one remote owner then walks and uses real pointer/item/storage/traversal/channel commands continuously. Peer watches grass. Settled ownerless checkpoint preserves world consequences; account/economy resume and normal item acquisition are later-stage."));FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(Out/(FParse::Param(FCommandLine::Get(),TEXT("LDB91ForestRoutes"))?TEXT("ForestRoutes.json"):Resume?TEXT("Resume.json"):TEXT("Encounter.json"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);if(!Resume){FFileHelper::SaveStringToFile(Trace,*(Out/TEXT("EncounterMotion.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);}}
