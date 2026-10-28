#include "Tests/LDB91CheckpointProbe.h"
#include "World/LDWorldObjects.h"
#include "World/LDB91Checkpoint.h"
#include "World/LDEnvironmentDirector.h"
#include "World/LDPortableTools.h"
#include "Combat/LDCombatDirector.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"

ALDB91CheckpointProbe::ALDB91CheckpointProbe(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDB91CheckpointProbe::Check(bool Passed,const FString& Name)
{
    auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("case"),Name);R->SetBoolField(TEXT("passed"),Passed);Checks.Add(MakeShared<FJsonValueObject>(R));Failures+=!Passed;
    UE_LOG(LogTemp,Display,TEXT("LD_B91_CHECKPOINT_PROBE %s passed=%d"),*Name,Passed);
}
void ALDB91CheckpointProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(Done||GetWorld()->GetTimeSeconds()<2){return;}Done=true;
    FString Output,Mode,Path,Error;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Output);FParse::Value(FCommandLine::Get(),TEXT("B91CheckpointCase="),Mode);Path=Output/TEXT("World.json");
    auto* O=ALDWorldObjects::Get(GetWorld());auto* E=ALDEnvironmentDirector::Get(GetWorld());
    Check(O&&O->Ready&&E&&O->CheckpointSettled(Error),TEXT("Dedicated ownerless environment is settled"));
    if(O&&E&&!Failures)
    {
        LDCheckpoint::FData Initial;Check(O->CaptureCheckpoint(Initial,Error),TEXT("Capture complete native state"));
        if(Mode==TEXT("Write"))
        {
            Check(O->SaveCheckpoint(Output/TEXT("Baseline.json"),Error),TEXT("Pristine endowment passes semantic validation"));
            ALDWorldObject *Tree=nullptr,*Partial=nullptr,*Broken=nullptr,*Grass=nullptr,*Chest=nullptr,*Water=nullptr,*Torch=nullptr;
            for(auto W:O->Objects){auto* P=W.Get();if(P->Kind==ELDObjectKind::Sapling){if(!Tree){Tree=P;}else if(!Partial){Partial=P;}else if(!Broken){Broken=P;}}if(!Grass&&P->Kind==ELDObjectKind::TallGrass){Grass=P;}if(!Chest&&P->Kind==ELDObjectKind::Chest){Chest=P;}if(!Water&&P->Kind==ELDObjectKind::Water){Water=P;}if(!Torch&&P->Kind==ELDObjectKind::Torch){Torch=P;}}
            Check(Tree&&Partial&&Broken&&Grass&&Chest&&Water&&Torch,TEXT("Diagnostic fixture uses existing authored sources"));
            if(Tree&&Partial&&Broken&&Grass&&Chest&&Water&&Torch)
            {
                // State fixtures isolate persistence; earned channels and attacks have separate native network evidence.
                Tree->Broken=true;Tree->Hp=0;Tree->HarvestReceipt=FGuid::NewGuid();
                auto* Fiber=GetWorld()->SpawnActor<ALDItem>();Fiber->Kind=ELDItemKind::Fiber;Fiber->Institution=Tree->HarvestSource;Fiber->OriginReceipt=Tree->HarvestReceipt;Tree->YieldIdentity=Fiber->Identity;O->Finite.Add(Fiber->Identity,Fiber);O->Wrap(Fiber,Tree->LastClear+FVector(100,0,0));Tree->Apply(Tree->GetActorLocation(),Tree->Flags(),2);
                Partial->Hp=37;Partial->Apply(Partial->GetActorLocation(),Partial->Flags(),2);Broken->Broken=true;Broken->Hp=0;Broken->Apply(Broken->GetActorLocation(),Broken->Flags(),2);
                Grass->BurnReceipt=FGuid::NewGuid();Grass->Broken=true;Grass->Hp=0;Grass->Apply(Grass->GetActorLocation(),Grass->Flags(),2);
                auto* Spent=Water->Physical.Get();O->RecordConsumption(Spent);Water->Physical.Reset();Spent->State=2;Spent->Destroy();Water->Apply(Water->GetActorLocation(),Water->Flags(),2);
                auto* Tool=Torch->Physical.Get();Tool->State=1;Tool->Holder=Chest;Tool->FuelMs=170000;Tool->FuelAt=GetWorld()->GetTimeSeconds();Tool->bLit=true;Tool->ReadyAt=LDTactical::Milliseconds(GetWorld()->GetTimeSeconds())+12000;Tool->ProcReadyAt=Tool->ReadyAt+6000;++Tool->Revision;Chest->Contents.Add(Tool);Chest->Apply(Chest->GetActorLocation(),Chest->Flags(),2);Torch->Apply(Torch->GetActorLocation(),Torch->Flags(),2);
                for(auto D:E->Doors){if(D->Rear){D->Apply(5,2);++E->MutationRevision;break;}}
                bool Moved=false;for(auto P:O->Objects){if(P->Kind!=ELDObjectKind::Crate){continue;}FVector Pad;if(O->FitSource(P.Get(),P->LastClear+FVector(600,0,0),Pad)&&FVector::Dist2D(Pad,P->LastClear)>100){P->LastClear=Pad;P->Apply(Pad+FVector(0,0,P->Size().Z*.5+3),P->Flags(),P->Revision+1);Moved=true;break;}}
                Check(Moved,TEXT("Diagnostic relocation finds a legal new cargo footprint"));
                ++O->Committed;Check(O->Conserved(Error),TEXT("Diagnostic mutated fixture conserves every finite identity"));
                Check(O->SaveCheckpoint(Path,Error),TEXT("Save harvest receipt, partial HP, burnt grass, consume tombstone, storage, lit fuel and door"));
                FString Before;FFileHelper::LoadFileToString(Before,*Path);
                auto Refused=[&](const FString& Name){Error.Empty();Check(!O->SaveCheckpoint(Path,Error)&&!Error.IsEmpty(),Name);FString After;FFileHelper::LoadFileToString(After,*Path);Check(After==Before,Name+TEXT(" leaves prior file unchanged"));};
                O->Holds.Add(TWeakObjectPtr<ALDB91Controller>());Refused(TEXT("Pending channel refused"));O->Holds.Empty();
                O->Cargo.Add(TWeakObjectPtr<ALDB91Controller>());Refused(TEXT("Cargo refused"));O->Cargo.Empty();
                O->Returns.Add(TWeakObjectPtr<ALDB91Controller>());Refused(TEXT("Traversal return refused"));O->Returns.Empty();
                auto* Projectile=GetWorld()->SpawnActor<ALDArcProjectile>();Refused(TEXT("Projectile refused"));Projectile->Destroy();
                auto* Field=GetWorld()->SpawnActor<ALDTacticalField>();Field->PersistentZone=true;Field->ExpiresAt=LDTactical::Milliseconds(GetWorld()->GetTimeSeconds())+10000;Refused(TEXT("Temporary field refused"));Field->Destroy();
                Tree->bCarriedWorld=true;Refused(TEXT("Unsettled prop refused"));Tree->bCarriedWorld=false;
                ALDWorldObject *Anchor=nullptr,*Ladder=nullptr;for(auto P:O->Objects){if(P->Kind==ELDObjectKind::Anchor){Anchor=P.Get();}if(P->Kind==ELDObjectKind::Ladder){Ladder=P.Get();}}
                if(Anchor&&Ladder){Anchor->LinkedLadder=Ladder;Refused(TEXT("Deployed link refused"));Anchor->LinkedLadder.Reset();}
                auto* PC=GetWorld()->SpawnActor<ALDB91Controller>();Refused(TEXT("Connected controller refused"));PC->Destroy();
            }
        }
        else if(Mode==TEXT("Read"))
        {
            FString Text;LDCheckpoint::FData Saved;Check(FFileHelper::LoadFileToString(Text,*Path)&&LDCheckpoint::Open(Text,O->Contract,E->Contract,Saved,Error),TEXT("Read saved envelope in a different process"));
            Check(O->bCheckpointRestored&&Initial.Objects.Num()==436&&Initial.Items.Num()==143&&Initial.Consumed.Num()==1,TEXT("Startup restores exact finite census including one yield and one sink"));
            bool Objects=Initial.Objects.Num()==Saved.Objects.Num(),Items=Initial.Items.Num()==Saved.Items.Num(),Doors=Initial.Doors.Num()==Saved.Doors.Num();
            for(int N=0;Objects&&N<Initial.Objects.Num();++N){const auto& A=Initial.Objects[N];const auto& B=Saved.Objects[N];Objects=A.Id==B.Id&&A.Kind==B.Kind&&A.Position.Equals(B.Position,.01)&&A.Clear.Equals(B.Clear,.01)&&A.Revision==B.Revision&&A.Hp==B.Hp&&A.Broken==B.Broken&&A.Harvest==B.Harvest&&A.Yield==B.Yield&&A.Burn==B.Burn&&A.Physical==B.Physical&&A.Contents==B.Contents;}
            for(int N=0;Items&&N<Initial.Items.Num();++N){const auto& A=Initial.Items[N];const auto& B=Saved.Items[N];Items=A.Id==B.Id&&A.Receipt==B.Receipt&&A.Origin==B.Origin&&A.Location==B.Location&&A.Kind==B.Kind&&A.Revision==B.Revision&&A.Position.Equals(B.Position,.01)&&A.Lit==B.Lit&&A.Fuel<=B.Fuel&&A.Fuel>=B.Fuel-4000&&A.Ready<=B.Ready&&A.Ready>=B.Ready-4000&&A.Proc<=B.Proc&&A.Proc>=B.Proc-4000;}
            for(int N=0;Doors&&N<Initial.Doors.Num();++N){const auto& A=Initial.Doors[N];const auto& B=Saved.Doors[N];Doors=A.Id==B.Id&&A.Revision==B.Revision&&A.State==B.State;}
            Check(Objects,TEXT("Every source, receipt, reward identity, HP, placement and storage reference survives restart"));Check(Items,TEXT("Every live item identity, origin and location survives; lit fuel and remaining cooldowns advance without refill"));Check(Doors&&Initial.DoorMutation==Saved.DoorMutation,TEXT("All door states and revisions survive restart"));
            Check(Initial.Consumed.Num()==Saved.Consumed.Num()&&Initial.Consumed[0].Id==Saved.Consumed[0].Id&&Initial.Consumed[0].Receipt==Saved.Consumed[0].Receipt&&Initial.Consumed[0].Origin==Saved.Consumed[0].Origin,TEXT("Consumed physical identity remains a provenance-bearing tombstone"));
            bool Proxies=true;for(auto P:O->Objects){Proxies&=P->Revision==P->CollisionRevision&&P->Revision==P->SightRevision&&P->Revision==P->NavigationRevision;}for(auto D:E->Doors){Proxies&=D->Revision==D->CollisionRevision&&D->Revision==D->SightRevision&&D->Revision==D->NavigationRevision;}
            Check(Proxies&&O->Conserved(Error),TEXT("Restored geometry revisions and finite conservation agree"));Check(!O->RestoreCheckpoint(Path,Error),TEXT("A second restore cannot duplicate rewards or refill fuel"));
            Check(O->SaveCheckpoint(Output/TEXT("Restored.json"),Error),TEXT("Restored state can be checkpointed again without reminting"));
        }
        else if(Mode==TEXT("Reject"))
        {
            FString Text;LDCheckpoint::FData Saved;Check(FFileHelper::LoadFileToString(Text,*Path)&&LDCheckpoint::Open(Text,O->Contract,E->Contract,Saved,Error),TEXT("Load reference file for adversarial diagnostics"));const FString Fingerprint=O->CheckpointFingerprint(Error);
            auto Reject=[&](const FString& Name,const FString& Bad){const FString File=Output/TEXT("Rejected.json");FFileHelper::SaveStringToFile(Bad,*File);Error.Empty();Check(!O->RestoreCheckpoint(File,Error)&&!Error.IsEmpty()&&O->CheckpointFingerprint(Error)==Fingerprint,Name+TEXT(" rejected before mutation"));};
            Reject(TEXT("Malformed JSON"),TEXT("{"));Reject(TEXT("Wrong contract"),LDCheckpoint::Envelope(LDCheckpoint::Encode(Saved),TEXT("wrong"),E->Contract));
            auto Bad=Saved;const auto Duplicate=Bad.Items[0];Bad.Items.Add(Duplicate);Reject(TEXT("Duplicate item"),LDCheckpoint::Envelope(LDCheckpoint::Encode(Bad),O->Contract,E->Contract));
            Bad=Saved;Bad.Objects[0].Id=TEXT("unknown_source");Reject(TEXT("Unknown source"),LDCheckpoint::Envelope(LDCheckpoint::Encode(Bad),O->Contract,E->Contract));
            Bad=Saved;Bad.Items[0].Fuel=300001;Reject(TEXT("Excess fuel"),LDCheckpoint::Envelope(LDCheckpoint::Encode(Bad),O->Contract,E->Contract));
            Bad=Saved;Bad.Items.RemoveAt(0);Reject(TEXT("Lost finite good"),LDCheckpoint::Envelope(LDCheckpoint::Encode(Bad),O->Contract,E->Contract));
            Bad=Saved;Bad.Objects[0].Kind=15;Reject(TEXT("Changed source kind"),LDCheckpoint::Envelope(LDCheckpoint::Encode(Bad),O->Contract,E->Contract));
            Bad=Saved;Bad.Items[0].Origin=TEXT("unearned_source");Reject(TEXT("Unknown provenance"),LDCheckpoint::Envelope(LDCheckpoint::Encode(Bad),O->Contract,E->Contract));
            Bad=Saved;Bad.DoorMutation=1;Reject(TEXT("Door mutation revision regression"),LDCheckpoint::Envelope(LDCheckpoint::Encode(Bad),O->Contract,E->Contract));
            Bad=Saved;Bad.Items[0].Receipt=Bad.Items[1].Receipt;Reject(TEXT("Duplicate provenance receipt"),LDCheckpoint::Envelope(LDCheckpoint::Encode(Bad),O->Contract,E->Contract));
            int Crate=INDEX_NONE,Peer=INDEX_NONE;for(int N=0;N<Saved.Objects.Num();++N){if(Saved.Objects[N].Kind==int(ELDObjectKind::Crate)){if(Crate==INDEX_NONE){Crate=N;}else{Peer=N;break;}}}
            Check(Crate!=INDEX_NONE&&Peer!=INDEX_NONE,TEXT("Placement diagnostics address two authored crates"));
            if(Crate!=INDEX_NONE&&Peer!=INDEX_NONE)
            {
                Bad=Saved;Bad.Objects[Crate].Position.Z+=40;Bad.Objects[Crate].Clear.Z+=40;Bad.Objects[Crate].Revision=2;Reject(TEXT("Floating cargo"),LDCheckpoint::Envelope(LDCheckpoint::Encode(Bad),O->Contract,E->Contract));
                Bad=Saved;Bad.Objects[Crate].Clear.Z+=20;Reject(TEXT("Inconsistent clear pad"),LDCheckpoint::Envelope(LDCheckpoint::Encode(Bad),O->Contract,E->Contract));
                Bad=Saved;Bad.Objects[Peer].Position=Bad.Objects[Crate].Position;Bad.Objects[Peer].Clear=Bad.Objects[Crate].Clear;Bad.Objects[Peer].Revision=2;Reject(TEXT("Overlapping saved cargo"),LDCheckpoint::Envelope(LDCheckpoint::Encode(Bad),O->Contract,E->Contract));
                auto* Wall=GetWorld()->SpawnActor<AStaticMeshActor>();auto* Mesh=Wall->GetStaticMeshComponent();Mesh->SetMobility(EComponentMobility::Movable);Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Wall->SetActorLocation(Saved.Objects[Crate].Position);Mesh->SetCollisionProfileName(TEXT("BlockAll"));Mesh->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Ignore);
                Reject(TEXT("Cargo intersecting fixed geometry"),Text);Wall->Destroy();
            }
            Reject(TEXT("Wrong scope"),Text.Replace(TEXT("settled_environment"),TEXT("invalid_scope")));Reject(TEXT("Checksum mismatch"),Text.Replace(TEXT("committed"),TEXT("committeD")));
            Check(O->RestoreCheckpoint(Path,Error),TEXT("Valid restore still succeeds after all rejected files"));
        }
        else
        {Check(Mode==TEXT("Fresh")&&!O->bCheckpointRestored&&Initial.Objects.Num()==435&&Initial.Items.Num()==143&&Initial.Consumed.IsEmpty(),TEXT("No restore flag starts a fresh finite world"));bool Fresh=true;for(auto P:O->Objects){Fresh&=!P->Broken&&P->Hp==80&&!P->HarvestReceipt.IsValid()&&!P->BurnReceipt.IsValid();}Check(Fresh,TEXT("Fresh match resets source mutations explicitly"));}
    }
    if(!Error.IsEmpty()){UE_LOG(LogTemp,Display,TEXT("LD_B91_CHECKPOINT_PROBE detail=%s"),*Error);}
    auto Root=MakeShared<FJsonObject>();Root->SetStringField(TEXT("mode"),Mode);Root->SetBoolField(TEXT("completed"),true);Root->SetNumberField(TEXT("failures"),Failures);Root->SetArrayField(TEXT("checks"),Checks);FString Json;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(Output/(Mode+TEXT(".json"))));FPlatformMisc::RequestExitWithStatus(false,Failures?3:0);
#endif
}
