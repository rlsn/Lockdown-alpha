#include "Tests/LDB91GrassProbe.h"
#include "World/LDEnvironmentDirector.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Inventory/LDInventoryDirector.h"
#include "Navigation/LDNavigationComponent.h"
#include "Vision/LDVisionDirector.h"
#include "Core/LDGroundQueries.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ALDB91GrassProbe::ALDB91GrassProbe(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;Started=FPlatformTime::Seconds();}
void ALDB91GrassProbe::Check(bool Good,const FString& Text)
{auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("case"),Text);R->SetBoolField(TEXT("passed"),Good);Checks.Add(MakeShared<FJsonValueObject>(R));Failures+=!Good;UE_LOG(LogTemp,Display,TEXT("LD_B91_GRASS %s passed=%d"),*Text,Good);}
void ALDB91GrassProbe::Place(ALDB91Controller* PC,FVector Point)
{PC->Navigation->ResetLife();auto* Body=Cast<ALDCharacter>(PC->GetPawn());Body->SetActorLocation(Point);Body->GetCharacterMovement()->StopMovementImmediately();PC->ClientSetLocation(Point,FRotator::ZeroRotator);}
void ALDB91GrassProbe::Task(uint8 Value)
{Phase=Value;At=GetWorld()->GetTimeSeconds();ALDEnvironmentDirector::Get(GetWorld())->Publish();Objects->Publish();A->ClientB91ProbeTask(Value);B->ClientB91ProbeTask(Value);Save(false);}
void ALDB91GrassProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(Phase==99){return;}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M){return;}auto* C=M->Combat.Get();auto* E=ALDEnvironmentDirector::Get(GetWorld());const double Now=GetWorld()->GetTimeSeconds();
    if(Failures||FPlatformTime::Seconds()-Started>220){if(!Failures){Check(false,TEXT("Grass fixture timed out"));}Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,3);return;}
    if(!Phase)
    {
        for(FConstPlayerControllerIterator I=GetWorld()->GetPlayerControllerIterator();I;++I){if(auto* PC=Cast<ALDB91Controller>(I->Get());PC&&PC->GetPawn()){if(PC->EnvironmentProbeRole==1){A=PC;}if(PC->EnvironmentProbeRole==2){B=PC;}}}if(!A||!B||Now<7){return;}Objects=ALDWorldObjects::Get(GetWorld());if(!Objects||!Objects->Ready){return;}AddTickPrerequisiteActor(Objects);
        Check(A!=B&&!A->IsLocalController()&&!B->IsLocalController(),TEXT("Two independent remote grass owners"));int Count=0;for(TActorIterator<ALDItem> I(GetWorld());I;++I){++Count;}Check(Objects->Objects.Num()==435&&Count==143,TEXT("96 grass patches add no physical goods to435 records and143 items"));
        int Patches=0;bool Fit=true;for(auto W:Objects->Objects){auto* O=W.Get();if(!O||O->Kind!=ELDObjectKind::TallGrass){continue;}++Patches;FVector Floor;const bool Supported=Objects->Placement(O,O->LastClear,Floor);Fit&=Supported&&!O->BlocksWalk()&&!O->Container()&&!O->Heavy()&&!O->Broken&&!O->Physical.IsValid()&&O->Proxy->GetCollisionEnabled()==ECollisionEnabled::NoCollision&&!O->Proxy->CanEverAffectNavigation();auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("id"),O->SourceId.ToString());R->SetStringField(TEXT("source"),O->SourcePosition.ToString());R->SetStringField(TEXT("fitted"),O->LastClear.ToString());R->SetBoolField(TEXT("supported"),Supported);Manifest.Add(MakeShared<FJsonValueObject>(R));}
        Check(Patches==96&&Fit,TEXT("All96 stable grass disks are fitted, non-solid and resource-free"));for(auto O:Objects->Objects){if(!O.IsValid()||O->Kind!=ELDObjectKind::TallGrass){continue;}FVector Floor;const FVector P=O->LastClear;if(LDGround::MapSurface(GetWorld(),P+FVector(500,0,0),Floor)&&FMath::Abs(Floor.Z-P.Z)<1&&E->Ray(P+FVector(0,0,92.15),P+FVector(500,0,92.15),O.Get())){Grass=O.Get();break;}}Check(Grass!=nullptr,TEXT("Reference fixture chooses an authored flat grass patch with clear observation geometry"));if(!Grass){return;}TorchA=Objects->Find(TEXT("prop_torch_000"))->Physical;TorchB=Objects->Find(TEXT("prop_torch_001"))->Physical;Check(M->Inventory->Adopt(A,TorchA.Get())&&M->Inventory->Adopt(B,TorchB.Get()),TEXT("Fixture moves two existing finite Torches without minting"));
        for(auto* PC:{A,B}){M->StateFor(PC)->ProtectedUntil=0;C->StateFor(Cast<ALDCharacter>(PC->GetPawn()))->ProtectionUntil=0;}Place(A,Grass->LastClear+FVector(0,0,92.15));Place(B,Grass->LastClear+FVector(500,0,92.15));Phase=80;At=Now;return;
    }
    auto* BA=Cast<ALDCharacter>(A->GetPawn());auto* BB=Cast<ALDCharacter>(B->GetPawn());
    if(Phase==80){if(Now-At>1.3){Task(1);}return;}
    if(Phase==81){if(Now-At>1.3){Task(13);}return;}
    if(Phase==90){const auto* O=M->StateFor(A);if(O->bActive&&BA){Place(A,Grass->LastClear+FVector(0,0,92.15));Task(16);}return;}
    auto Read=[](ALDB91Controller* PC,uint8 Value)->TSharedPtr<FJsonObject>{TSharedPtr<FJsonObject> R;const auto* S=PC->EnvironmentProbeReports.Find(Value);if(S){FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(*S),R);}return R;};const auto RA=Read(A,Phase),RB=Read(B,Phase);if(!RA||!RB){return;}
    Check(RA->GetBoolField(TEXT("completed"))&&RB->GetBoolField(TEXT("completed")),FString::Printf(TEXT("Both restricted grass phase%u snapshots complete"),Phase));
    auto Hidden=[&](){return C->Concealed(BA)&&!E->Visible(B,BA)&&!RB->GetBoolField(TEXT("body_seen"));};
    auto Seen=[&](){return E->Visible(B,BA)&&RB->GetBoolField(TEXT("body_seen"));};
    if(Phase==1){Check(Hidden()&&RA->GetBoolField(TEXT("body_seen")),TEXT("Grass hides its occupant from a distant observer without becoming a sight wall"));Place(B,Grass->LastClear+FVector(150,0,92.15));Task(2);return;}
    if(Phase==2){Check(C->Concealed(BA)&&Seen(),TEXT("Ordinary clear geometry within200cm reveals grass without deleting concealment"));C->StateFor(BA)->Tactical.ConcealedUntil=LDTactical::Milliseconds(Now)+500;Check(!E->Visible(B,BA),TEXT("Diagnostic timed invisibility is not revealed by the grass proximity exception"));C->StateFor(BA)->Tactical.ConcealedUntil=0;auto* W=GetWorld()->SpawnActor<AStaticMeshActor>();auto* Mesh=W->GetStaticMeshComponent();Mesh->SetMobility(EComponentMobility::Movable);Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));W->SetActorLocation(Grass->LastClear+FVector(75,0,150));W->SetActorScale3D(FVector(.2,6,3));Mesh->SetCollisionProfileName(TEXT("BlockAll"));Mesh->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Ignore);Mesh->SetCollisionResponseToChannel(ECC_GameTraceChannel2,ECR_Block);Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Mesh->SetCanEverAffectNavigation(false);Wall=W;Detector=C->CreateField(BB,ELDTacticalField::Detector,BB->GetActorLocation(),LDTactical::Milliseconds(Now),30000,1,700);Task(3);return;}
    if(Phase==3){Check(C->Concealed(BA)&&!E->Visible(B,BA)&&!RB->GetBoolField(TEXT("body_seen"))&&!M->Vision->Detects(B,BA->GetActorLocation()),TEXT("Diagnostic wall blocks both proximity and detector authorization"));if(Wall.IsValid()){Wall->Destroy();}if(Detector.IsValid()){C->DestroyField(Detector.Get());}Place(B,Grass->LastClear+FVector(500,0,92.15));Detector=C->CreateField(BB,ELDTacticalField::Detector,BB->GetActorLocation(),LDTactical::Milliseconds(Now),30000,1,700);Task(4);return;}
    if(Phase==4){Check(Detector.IsValid()&&C->Concealed(BA)&&Seen()&&M->Vision->Detects(B,BA->GetActorLocation()),TEXT("Diagnostic authorized detector reveals distant grass through clear geometry only"));FGuid::Parse(RB->GetStringField(TEXT("body_handle")),SeenBody);if(Detector.IsValid()){C->DestroyField(Detector.Get());}Task(5);return;}
    if(Phase==5){Check(!A->Navigation->HasMoveOrder()&&FVector::Dist2D(BA->GetActorLocation(),Grass->LastClear)>50&&FVector::Dist2D(BA->GetActorLocation(),Grass->LastClear)<150,TEXT("Real navigation walks inside grass without requiring stationary cover"));Check(Hidden()&&SeenBody.IsValid()&&!E->Resolve(B,SeenBody),TEXT("Detector loss hides the body and revokes its old target handle"));Task(6);return;}
    if(Phase==6){Check(!C->Concealed(BA)&&Seen(),TEXT("Ordinary remote basic attack reveals the grass occupant"));Task(7);return;}
    if(Phase==7){const auto* G=Objects->GrassBodies.Find(BA);Check(G&&Now>=G->RevealUntil&&Now<G->ReadyAt&&!C->Concealed(BA)&&Seen(),TEXT("After the2s offense reveal, one further rearm second remains visible"));Task(8);return;}
    if(Phase==8){Check(Hidden(),TEXT("No new offense permits concealment after the full rearm endpoint"));Place(A,Grass->LastClear+FVector(300,0,92.15));Task(9);return;}
    if(Phase==9){Check(!Objects->InsideGrass(BA)&&!C->Concealed(BA)&&Seen(),TEXT("Leaving the250cm feet-point disk immediately removes grass concealment"));Place(A,Grass->LastClear+FVector(0,0,92.15));Task(10);return;}
    if(Phase==10){Check(Objects->InsideGrass(BA)&&!C->Concealed(BA)&&Seen(),TEXT("Re-entry does not conceal before one quiet second"));Task(11);return;}
    if(Phase==11){Check(Hidden(),TEXT("Re-entry rearms after one real quiet second"));M->StateFor(A)->ChannelKind=3;M->SyncOwner(A);Task(12);return;}
    if(Phase==12){Check(!C->Concealed(BA)&&Seen(),TEXT("Diagnostic active-claim owner state cannot receive grass concealment"));C->StateFor(BA)->Action=10;Objects->TickGrass(Now);C->StateFor(BA)->Action=0;const auto* G=Objects->GrassBodies.Find(BA);Check(G&&G->RevealUntil>=Now+2&&G->ReadyAt>=Now+3&&!C->Concealed(BA),TEXT("Diagnostic sustained casting retains full reveal and rearm after it stops"));M->StateFor(A)->ChannelKind=0;M->SyncOwner(A);Phase=81;At=Now;return;}
    if(Phase==13){Check(TorchA->BindingMask==(1<<7)&&TorchB->BindingMask==(1<<7)&&TorchA->FuelMs==180000&&TorchB->FuelMs==180000,TEXT("Both clients complete real quiet2s accessory binding of original Torches"));Place(A,Grass->LastClear+FVector(-100,0,92.15));Place(B,Grass->LastClear+FVector(100,0,92.15));Task(14);return;}
    if(Phase==14){const int Wins=(RA->GetIntegerField(TEXT("result"))==1)+(RB->GetIntegerField(TEXT("result"))==1);Check(Wins==1&&Grass->BurnReceipt.IsValid()&&!Grass->Broken&&!Objects->InsideGrass(BA),TEXT("One paid Torch wins the same grass socket and disables its cover immediately"));Check(TorchA->FuelMs+TorchB->FuelMs==359000&&!TorchA->bReserved&&!TorchB->bReserved&&C->HeldEnvironment()==0,TEXT("Race and replay spend exactly1s fuel and release all reservations"));bool Good=!C->RiverCells.IsEmpty()&&C->RiverCells.Num()<=4;for(const auto& P:C->RiverCells){Good&=P.Value.Burning.Until-P.Value.Burning.Created==3000;}Check(Good,TEXT("Grass ignition retains bounded3s Burning cells without spreading"));Place(B,Grass->LastClear+FVector(500,0,92.15));Place(A,Grass->LastClear+FVector(0,0,92.15));Task(15);return;}
    if(Phase==15){Check(Grass->Broken&&!C->Concealed(BA)&&Seen()&&Grass->Proxy->GetCollisionEnabled()==ECollisionEnabled::NoCollision&&Grass->Revision==Grass->SightRevision&&Grass->Revision==Grass->NavigationRevision,TEXT("Burnt cover stays cleared with synchronized nonblocking proxy revisions"));int Burnt=0;for(auto O:Objects->Objects){Burnt+=O.IsValid()&&O->Kind==ELDObjectKind::TallGrass&&O->Broken;}FString Why;Check(Burnt==1&&!Grass->YieldIdentity.IsValid()&&Objects->Initialize(Why)&&Grass->Broken&&Objects->Conserved(Why),TEXT("Only the named grass burns; warm initialization creates no regrowth or reward"));M->EndContenderLife(A,Now);Phase=90;At=Now;return;}
    if(Phase==16){int Count=0;for(TActorIterator<ALDItem> I(GetWorld());I;++I){++Count;}FString Why;Check(M->StateFor(A)->Life>1&&Grass->Broken&&!Objects->InsideGrass(BA)&&!C->Concealed(BA)&&Seen(),TEXT("Actual life return cannot restore the burnt patch"));Check(Count==143&&Objects->Conserved(Why),TEXT("Grass, burn and real life return conserve all143 physical items"));Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,Failures?3:0);return;}
#endif
}
void ALDB91GrassProbe::Save(bool Complete)
{FString Output;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Output);if(Output.IsEmpty()){return;}auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("complete"),Complete);R->SetNumberField(TEXT("phase"),Phase);R->SetNumberField(TEXT("failures"),Failures);R->SetArrayField(TEXT("checks"),Checks);R->SetArrayField(TEXT("manifest"),Manifest);R->SetStringField(TEXT("contract"),Objects?Objects->Contract:TEXT(""));R->SetStringField(TEXT("scope"),TEXT("Two remote owners with real clocks, basic attack, quiet Torch binding and paid ignition. Existing Torches and body positions are fixture staging; wall, detector fields, sustained-cast state and claim-state gate are diagnostic, not earned acquisition/full-map Heart admission. Warm initialization and real life return are tested; saved-match restoration, paid detector acquisition, broad fire spells and full B91/human/performance acceptance remain open."));FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(Output/TEXT("Grass.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);}
