#include "Tests/LDB91IgnitionProbe.h"
#include "World/LDEnvironmentDirector.h"
#include "World/LDPortableTools.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Inventory/LDInventoryDirector.h"
#include "Navigation/LDNavigationComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ALDB91IgnitionProbe::ALDB91IgnitionProbe(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;Started=FPlatformTime::Seconds();}
void ALDB91IgnitionProbe::Check(bool Good,const FString& Text)
{auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("case"),Text);R->SetBoolField(TEXT("passed"),Good);Checks.Add(MakeShared<FJsonValueObject>(R));Failures+=!Good;UE_LOG(LogTemp,Display,TEXT("LD_B91_IGNITION %s passed=%d"),*Text,Good);}
void ALDB91IgnitionProbe::Place(ALDB91Controller* PC,FVector Point)
{PC->Navigation->Stop();auto* Body=Cast<ALDCharacter>(PC->GetPawn());Body->SetActorLocation(Point);Body->GetCharacterMovement()->StopMovementImmediately();PC->ClientSetLocation(Point,FRotator::ZeroRotator);auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M->StateFor(PC)->ProtectedUntil=0;M->Combat->StateFor(Body)->ProtectionUntil=0;M->SyncOwner(PC);}
void ALDB91IgnitionProbe::Task(uint8 Value)
{Phase=Value;At=GetWorld()->GetTimeSeconds();Objects->Publish();A->ClientB91ProbeTask(Value);B->ClientB91ProbeTask(Value);Save(false);}
bool ALDB91IgnitionProbe::Fill(int32 Target,int64 Stamp,int64 Duration)
{
    auto* C=GetWorld()->GetAuthGameMode<ALDGameMode>()->Combat.Get();int Added=0;bool Accepted=true;ALDCombatDirector::FRiverCommand Q;
    auto Begin=[&](){Q=ALDCombatDirector::FRiverCommand();Q.Input=LDEnvironment::EInput::Water;Q.State.Owner=Q.State.BudgetOwner=FGuid::NewGuid();Q.State.Life=FGuid::NewGuid();Q.State.Cast=FGuid::NewGuid();Q.State.Created=Stamp;Q.State.Until=Stamp+Duration;Q.State.Revision=++C->EnvironmentRevision;};Begin();
    for(int N=0;N<5000&&Added<Target;++N)
    {const FIntPoint Id(220+N%100,220+N/100);bool Reserved=false;for(const auto& E:C->EnvironmentLeases){Reserved|=E.Value.Command.Cells.Contains(Id);}if(C->RiverCells.Contains(Id)||Reserved){continue;}const FVector P=LDRiver::Centre(Id);FHitResult H;if(!GetWorld()->LineTraceSingleByChannel(H,P+FVector(0,0,1600),P-FVector(0,0,100),ECC_GameTraceChannel1)){continue;}Q.Cells.Add(Id);Q.Surfaces.Add(Id,H.ImpactPoint.Z);Q.Normals.Add(Id,H.ImpactNormal);++Added;
     if(Q.Cells.Num()==64||Added==Target){Accepted&=C->QueueEnvironment(Q,Stamp);C->CommitRiver(Stamp);Begin();}}
    return Accepted&&Added==Target;
}
void ALDB91IgnitionProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(Phase==99){return;}const double Now=GetWorld()->GetTimeSeconds();const int64 Time=LDTactical::Milliseconds(Now);auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M){return;}auto* C=M->Combat.Get();
    if(Failures||FPlatformTime::Seconds()-Started>280){if(!Failures){Check(false,TEXT("Ignition fixture timed out"));}Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,3);return;}
    if(!Phase)
    {
        for(FConstPlayerControllerIterator I=GetWorld()->GetPlayerControllerIterator();I;++I){if(auto* PC=Cast<ALDB91Controller>(I->Get());PC&&PC->GetPawn()){if(PC->EnvironmentProbeRole==1){A=PC;}if(PC->EnvironmentProbeRole==2){B=PC;}}}if(!A||!B||Now<7){return;}
        Objects=ALDWorldObjects::Get(GetWorld());if(!Objects||!Objects->Ready){return;}AddTickPrerequisiteActor(Objects);Tree=Objects->Find(TEXT("prop_sapling_000"));NextTree=Objects->Find(TEXT("prop_sapling_001"));TorchA=Objects->Find(TEXT("prop_torch_000"))->Physical;TorchB=Objects->Find(TEXT("prop_torch_001"))->Physical;Oil=Objects->Find(TEXT("prop_oil_flask_000"))->Physical;
        Check(A!=B&&!A->IsLocalController()&&!B->IsLocalController(),TEXT("Two independent remote ignition owners"));Check(M->Inventory->Adopt(A,TorchA.Get())&&M->Inventory->Adopt(B,TorchB.Get()),TEXT("Fixture stages two original finite Torches without creating goods"));Check(TorchA->FuelMs==180000&&TorchB->FuelMs==180000&&!TorchA->bLit&&!TorchB->bLit,TEXT("Both original fuel clocks start full and off"));
        Place(A,Tree->LastClear+FVector(-110,0,92.15));Place(B,Tree->LastClear+FVector(110,0,92.15));Phase=90;At=Now;return;
    }
    if(Phase==90){if(Now-At>1){Task(1);}return;}
    if(Phase==91){if(Now-At>1.3){SawHold=false;Task(4);}return;}
    if(Phase==92)
    {
        if(Now-At<3.4){return;}Check(Tree->Broken&&Tree->Hp==0&&!Tree->YieldIdentity.IsValid(),TEXT("Actual3s burn clears the finite sapling without Fiber"));Check(Tree->Revision==Tree->CollisionRevision&&Tree->Revision==Tree->SightRevision&&Tree->Revision==Tree->NavigationRevision,TEXT("Burn removal shares one collision/sight/navigation revision"));
        Phase=87;At=Now;return;
    }
    if(Phase==87)
    {
        const auto* Own=M->StateFor(Winner);if(!Own||!Own->bActive||!Winner->GetPawn()){return;}if(!ReturnAt){ReturnAt=Now;auto* Tool=Winner==A?TorchA.Get():TorchB.Get();Check(M->Inventory->Adopt(Winner,Tool),TEXT("Life fixture recovers the original dropped Torch without minting"));Check(M->StateFor(Winner)->ItemFamilies.FindRef(TEXT("torch_socket"))==LifeCooldown,TEXT("Returned life retains the exact family endpoint"));FString Why;Check(Objects->Initialize(Why)&&Tree->BurnReceipt.IsValid()&&Tree->Broken,TEXT("Warm same-world initialization retains burned source exhaustion"));Place(A,Tree->LastClear+FVector(-110,0,92.15));Place(B,Tree->LastClear+FVector(110,0,92.15));}
        if(Now-ReturnAt>6.3){Task(6);}return;
    }
    if(Phase==93)
    {
        if(Time<M->StateFor(B)->ItemFamilies.FindRef(TEXT("torch_socket"))+200||Now-FMath::Max3(C->StateFor(Cast<ALDCharacter>(A->GetPawn()))->LastDamage,C->StateFor(Cast<ALDCharacter>(A->GetPawn()))->LastDealt,C->StateFor(Cast<ALDCharacter>(A->GetPawn()))->QuietSince)<6){return;}
        C->RetireEnvironment(Time);Check(C->RiverCells.IsEmpty(),TEXT("Oil fire expires before capacity setup"));Place(A,NextTree->LastClear+FVector(-110,0,92.15));Place(B,NextTree->LastClear+FVector(4000,0,92.15));Check(Fill(1024,Time,10000),TEXT("Diagnostic supported fields fill the1024-cell match cap"));CapRejected=FilledLease=false;Task(8);return;
    }
    if(Phase==4&&Objects->Holds.Contains(A)&&!SawHold){SawHold=true;Check(C->HeldEnvironment()>0&&TorchA->bReserved,TEXT("Held ignition reserves both physical Torch and field capacity"));}
    if(Phase==8&&Now-At>2&&!CapRejected)
    {CapRejected=true;Check(!Objects->Holds.Contains(A)&&!TorchA->bReserved&&C->HeldEnvironment()==0&&C->RiverCells.Num()==1024,TEXT("Capacity refusal leaves no channel or leaked lease"));Check(TorchA->FuelMs==(Winner==A?179000:180000)&&TorchA->ReadyAt<Time&&M->StateFor(A)->ItemFamilies.FindRef(TEXT("torch_socket"))<Time,TEXT("Capacity refusal spends no fuel and starts no cooldown"));}
    if(Phase==8&&CapRejected&&Objects->Holds.Contains(A)&&!FilledLease)
    {
        FilledLease=true;const int Held=C->HeldEnvironment();Check(Held>=1&&Held<=4&&Fill(1024-Held,Time,6000),TEXT("Other accepted fields fill every unreserved slot during the real channel"));
        auto Q=C->EnvironmentCommand(Cast<ALDCharacter>(A->GetPawn()),LDEnvironment::EInput::Water,Time,1000);const FIntPoint Id(-450,-450);const FVector P=LDRiver::Centre(Id);FHitResult H;const bool Floor=GetWorld()->LineTraceSingleByChannel(H,P+FVector(0,0,1600),P-FVector(0,0,100),ECC_GameTraceChannel1);Q.Cells.Add(Id);Q.Surfaces.Add(Id,H.ImpactPoint.Z);Q.Normals.Add(Id,H.ImpactNormal);Check(Floor&&!C->QueueEnvironment(Q,Time),TEXT("Another allocation cannot steal the accepted ignition reservation"));
    }
    auto Read=[](ALDB91Controller* PC,uint8 Key)->TSharedPtr<FJsonObject>{TSharedPtr<FJsonObject> R;const auto* Text=PC->EnvironmentProbeReports.Find(Key);if(Text){FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(*Text),R);}return R;};const auto RA=Read(A,Phase),RB=Read(B,Phase);if(!RA||!RB){return;}
    Check(RA->GetBoolField(TEXT("completed"))&&RB->GetBoolField(TEXT("completed")),FString::Printf(TEXT("Both remote ignition phase%u reports complete"),Phase));
    if(Phase==1){Check(RA->GetIntegerField(TEXT("result"))==2&&TorchA->FuelMs==180000&&!Tree->BurnReceipt.IsValid()&&C->HeldEnvironment()==0,TEXT("Unbound Torch cannot ignite or reserve fuel/cells"));Task(2);return;}
    if(Phase==2){Check(TorchA->BindingMask==(1<<7)&&TorchB->BindingMask==(1<<7),TEXT("Both owners prepare their real accessory through quiet binding RPC"));C->ApplyStatus(Cast<ALDCharacter>(A->GetPawn()),LDTactical::EStatus::Mute,Time+1200,FGuid::NewGuid());Task(3);return;}
    if(Phase==3){Check(RA->GetIntegerField(TEXT("result"))==2&&TorchA->FuelMs==180000&&!Tree->BurnReceipt.IsValid(),TEXT("Mute rejects ignition without spending fuel or the source"));Phase=91;At=Now;return;}
    if(Phase==4){Check(SawHold&&RA->GetIntegerField(TEXT("result"))==2&&TorchA->FuelMs==180000&&!TorchA->bReserved&&C->HeldEnvironment()==0&&!Tree->BurnReceipt.IsValid(),TEXT("Release cancellation and replay retain fuel and free exact reservations"));Task(5);return;}
    if(Phase==5)
    {const int Wins=int(RA->GetIntegerField(TEXT("result"))==1)+int(RB->GetIntegerField(TEXT("result"))==1);Winner=RA->GetIntegerField(TEXT("result"))==1?A:B;Check(Wins==1&&Tree->BurnReceipt.IsValid()&&!Tree->Broken&&(Tree->Flags()&32),TEXT("Two-owner ignition race commits one public burning socket"));Check(TorchA->FuelMs+TorchB->FuelMs==359000&&C->HeldEnvironment()==0,TEXT("Race and duplicate replay spend exactly1000ms across both physical Torches"));bool Attribution=!C->RiverCells.IsEmpty()&&C->RiverCells.Num()<=4;for(const auto& P:C->RiverCells){Attribution&=P.Value.Burning.Owner==M->ContenderId(Winner)&&P.Value.Burning.Life==C->StateFor(Cast<ALDCharacter>(Winner->GetPawn()))->Identity&&P.Value.Burning.Until-P.Value.Burning.Created==3000;}Check(Attribution,TEXT("At most four Burning cells retain exact owner/life and3s lifetime"));LifeCooldown=M->StateFor(Winner)->ItemFamilies.FindRef(TEXT("torch_socket"));M->EndContenderLife(Winner,Now);auto* Tool=Winner==A?TorchA.Get():TorchB.Get();Check(Tool->State==0&&!Tool->Holder.IsValid()&&!Tool->bReserved&&Tool->FuelMs==179000,TEXT("End-life drops the same spent Torch without a reservation or refill"));Check(M->StateFor(Winner)->ItemFamilies.FindRef(TEXT("torch_socket"))==LifeCooldown,TEXT("Ignition family endpoint survives end-life"));Check(Tree->BurnReceipt.IsValid()&&!Tree->Broken&&!C->RiverCells.IsEmpty(),TEXT("Committed fire and source mutation survive caster death"));Phase=92;At=Now;return;}
    if(Phase==6)
    {Check(RA->GetIntegerField(TEXT("result"))==2&&RB->GetIntegerField(TEXT("result"))==2&&!Tree->YieldIdentity.IsValid(),TEXT("Neither returned owner nor rival can harvest the burned source"));Check(M->Inventory->Adopt(A,Oil.Get()),TEXT("Oil setup transfers one existing finite flask"));Place(A,Aim+FVector(-100,0,92.15));Place(B,Aim+FVector(100,0,92.15));Task(7);return;}
    if(Phase==7)
    {Check(!Oil.IsValid()&&RB->GetIntegerField(TEXT("result"))==1,TEXT("One owner pays for Oil; a quiet rival ignites that actual overlay"));bool OilFire=!C->RiverCells.IsEmpty()&&C->RiverCells.Num()<=4;for(const auto& P:C->RiverCells){OilFire&=P.Value.Burning.Until>Time&&P.Value.Burning.Until-P.Value.Burning.Created==3000&&P.Value.Oil.Until==0&&P.Value.Burning.Owner==M->ContenderId(B);}Check(OilFire,TEXT("Torch consumes Oil into bounded3s fire with the igniter attribution"));Check(!C->StateFor(Cast<ALDCharacter>(B->GetPawn()))->Tactical.Has(LDTactical::EStatus::Stun,Time),TEXT("Socket ignition produces no direct-hit Stun"));Phase=93;At=Now;return;}
    if(Phase==8)
    {Check(CapRejected&&FilledLease&&RA->GetIntegerField(TEXT("result"))==1&&RA->GetBoolField(TEXT("same_retry_item"))&&NextTree->BurnReceipt.IsValid(),TEXT("Same retained Torch completes its paid reservation at match saturation"));Check(C->RiverCells.Num()==1024&&C->HeldEnvironment()==0&&C->EnvironmentPool.Num()==1024&&!TorchA->bReserved,TEXT("Accepted ignition fits its reserved cells without exceeding or leaking the pool"));Check(RB->GetIntegerField(TEXT("surface_count"))==0,TEXT("Distant client receives no diagnostic or paid canonical field registry"));FString Why;int Count=0;for(TActorIterator<ALDItem> I(GetWorld());I;++I){++Count;}Check(Objects->Conserved(Why)&&Count==142&&!NextTree->YieldIdentity.IsValid(),TEXT("One Oil consume leaves142 conserved physical items and no fire reward"));Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,Failures?3:0);return;}
#endif
}
void ALDB91IgnitionProbe::Save(bool Complete)
{FString Output;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Output);if(Output.IsEmpty()){return;}auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("complete"),Complete);R->SetNumberField(TEXT("phase"),Phase);R->SetNumberField(TEXT("failures"),Failures);R->SetArrayField(TEXT("checks"),Checks);FString Why;R->SetBoolField(TEXT("conserved"),Objects&&Objects->Conserved(Why));R->SetStringField(TEXT("scope"),TEXT("Two remote headless owners, real item/world RPCs, quiet binding,1s channel,3s burn and actual life return. Existing Torches/Oil are explicitly staged; allocator saturation is diagnostic. Warm Initialize is not saved-match reload. Grass, broader fire spells, rendered/human and full-stage acceptance remain open."));FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(Output/TEXT("Ignition.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);}
