#include "Tests/LDB91CombatProbe.h"
#include "World/LDEnvironmentDirector.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Inventory/LDInventoryDirector.h"
#include "Region/LDMapDefinition.h"
#include "Core/LDGroundQueries.h"
#include "Navigation/LDNavigationComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Vision/LDVisionDirector.h"
ALDB91CombatProbe::ALDB91CombatProbe(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;Started=FPlatformTime::Seconds();}
void ALDB91CombatProbe::Check(bool Good,const FString& Text){auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("case"),Text);R->SetBoolField(TEXT("passed"),Good);Checks.Add(MakeShared<FJsonValueObject>(R));Failures+=!Good;UE_LOG(LogTemp,Display,TEXT("LD_B91_COMBAT %s passed=%d"),*Text,Good);}
void ALDB91CombatProbe::Place(ALDB91Controller* PC,FVector Floor){PC->Navigation->ResetLife();auto* Body=Cast<ALDCharacter>(PC->GetPawn());Body->GetCharacterMovement()->StopMovementImmediately();Body->SetActorLocation(Floor+FVector(0,0,92.15));Body->GetCharacterMovement()->SetMovementMode(MOVE_None);PC->ClientSetLocation(Body->GetActorLocation(),FRotator::ZeroRotator);}
void ALDB91CombatProbe::Task(uint8 Value){Phase=Value;At=GetWorld()->GetTimeSeconds();Stunned=false;ALDEnvironmentDirector::Get(GetWorld())->Publish();A->ClientB91ProbeTask(Value);B->ClientB91ProbeTask(Value);Save(false);}
void ALDB91CombatProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(Phase==99){return;}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M){return;}auto* C=M->Combat.Get();auto* E=ALDEnvironmentDirector::Get(GetWorld());const double Now=GetWorld()->GetTimeSeconds();const int64 Time=LDTactical::Milliseconds(Now);
    if(Failures||FPlatformTime::Seconds()-Started>300){if(!Failures){Check(false,TEXT("Combat fixture timed out"));}Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,3);return;}
    if(!Phase)
    {
        for(FConstPlayerControllerIterator I=GetWorld()->GetPlayerControllerIterator();I;++I){if(auto* PC=Cast<ALDB91Controller>(I->Get());PC&&PC->GetPawn()){if(PC->EnvironmentProbeRole==1){A=PC;}if(PC->EnvironmentProbeRole==2){B=PC;}}}if(!A||!B||Now<8){return;}AddTickPrerequisiteActor(C);Check(A!=B&&!A->IsLocalController()&&!B->IsLocalController(),TEXT("Two remote owners use full-map combat sources"));
        auto* Map=ULDMapDefinition::Get(GetWorld());bool Found=false;
        for(const auto& River:Map->Rivers){for(int N=1;N<River.Points.Num()&&!Found;++N){const FVector P=FMath::Lerp(River.Points[N-1],River.Points[N],.5);FVector G,Left;FVector Hint=P;Hint.Z=Map->Height(FVector2D(P));if(LDGround::MapSurface(GetWorld(),Hint,G)&&Map->RiverAt(G)&&LDGround::MapSurface(GetWorld(),G-FVector(300,0,0),Left)&&FMath::Abs(Left.Z-G.Z)<5&&LDGround::ClearSegment(GetWorld(),Left+FVector(0,0,92.15),G+FVector(0,0,92.15))){Arena=G;Found=true;}}if(Found){break;}}
        bool Ledge=false;FVector Lower,Upper;
        for(const auto& P:Map->Plateaus){for(int I=0;I<360&&!Ledge;++I){const double Angle=I*PI/180;const FVector Dir(FMath::Cos(Angle),FMath::Sin(Angle),0);Lower=P.Position+Dir*(P.Radius+3700);Upper=P.Position+Dir*(P.Radius+2300);Lower.Z=Map->Height(FVector2D(Lower))+92.15;Upper.Z=Map->Height(FVector2D(Upper))+92.15;if(Map->UpwardLedge(Lower,Upper)&&E->Ray(Upper,Lower)&&!E->Ray(Lower,Upper)){Ledge=true;}}if(Ledge){break;}}
        UE_LOG(LogTemp,Display,TEXT("LD_B91_LEDGE lower=%s upper=%s"),*Lower.ToString(),*Upper.ToString());
        Check(Ledge,TEXT("Tagged plateau side edge blocks only uphill sight with clear physical downslope ray"));
        Check(!Map->UpwardLedge(Arena+FVector(0,0,92.15),Arena+FVector(300,0,92.15)),TEXT("Same-band river sight retains ordinary geometry"));
        Check(Found,TEXT("Fixture selects a clear physical river floor beyond legacy bounds"));if(!Found){return;}
        for(auto* PC:{A,B}){auto* O=M->StateFor(PC);O->ProtectedUntil=0;O->Build.Attributes={16,16,16,16,16,16};auto* S=C->StateFor(Cast<ALDCharacter>(PC->GetPawn()));S->ProtectionUntil=0;S->Tactical.MP=300;S->Hp=LDDevelopment::MaxHP(O->Build);O->Hp=S->Hp;O->MaxHp=S->Hp;S->LastDamage=S->LastDealt=Now-20;}
        auto Grant=[&](ALDB91Controller* PC,ELDItemKind Kind,int Slot){auto* Item=GetWorld()->SpawnActor<ALDItem>();Item->Kind=Kind;Check(M->Inventory->Adopt(PC,Item,TEXT("DiagnosticItemCreation")),TEXT("Labelled combat fixture creates one physical catalog item"));auto* O=M->StateFor(PC);O->Inventory.Bindings[7+Slot]=Item->Identity;Item->BindingMask=1<<(7+Slot);if(auto* Entry=const_cast<FLDInventoryItem*>(O->Inventory.Find(Item->Identity))){Item->Project(*Entry);}++O->Inventory.Revision;};
        Grant(A,ELDItemKind::FrostPrism,0);Grant(B,ELDItemKind::FrostCapsule,0);Grant(A,ELDItemKind::GustVial,1);Grant(A,ELDItemKind::SmokeFlask,2);Grant(A,ELDItemKind::SeersDust,3);
        auto& Build=M->StateFor(A)->Build;int Slot=0;for(auto Skill:{ELDSkill::Frostline,ELDSkill::WaterJet,ELDSkill::Firebolt,ELDSkill::Gust}){Build.Ranks[uint8(Skill)]=1;Build.Slots[Slot]=Skill;Build.Actions[Slot]=FLDActionRef();Build.Actions[Slot++].Skill=Skill;}++Build.Revision;
        Place(A,Arena-FVector(300,0,0));Place(B,Arena);for(auto* PC:{A,B}){M->SyncOwner(PC);}MP=C->StateFor(Cast<ALDCharacter>(A->GetPawn()))->Tactical.MP;Phase=90;At=Now;return;
    }
    auto* BA=Cast<ALDCharacter>(A->GetPawn());auto* BB=Cast<ALDCharacter>(B->GetPawn());auto* SA=C->StateFor(BA);auto* SB=C->StateFor(BB);
    if(Phase==90){if(Now-At>1){Check(SB->Tactical.Has(LDTactical::EStatus::Wet,Time),TEXT("Actual full-map river contact applies Wet before casting"));Task(1);}return;}
    if((Phase==1||Phase==4||Phase==16||Phase==17)&&SB&&SB->Tactical.Has(LDTactical::EStatus::Stun,Time)){Stunned=true;if(Phase==1){for(const auto& Status:SB->Tactical.Statuses){if(Status.Kind==LDTactical::EStatus::Stun){GuardLock=Status.Until-LDTactical::Milliseconds(SB->LastDamage);}}}}
    if(Phase==91){if(Time>M->StateFor(B)->WetFrostReadyAt+100&&M->StateFor(A)->Build.Cooldowns[uint8(ELDSkill::Frostline)]<Now){LDTactical::ActivateGuard(SB->Tactical,Time);Task(4);}return;}
    if(Phase==92){if(!M->StateFor(B)->bActive||!BB){return;}Check(M->StateFor(B)->WetFrostReadyAt==GuardLock,TEXT("Actual life return retains the victim reaction endpoint"));Place(A,Arena-FVector(300,0,0));Place(B,Arena);SA->ProtectionUntil=SB->ProtectionUntil=0;M->StateFor(A)->ProtectedUntil=M->StateFor(B)->ProtectedUntil=0;for(auto* PC:{A,B}){M->SyncOwner(PC);}Task(6);return;}
    if(Phase==93){if(Now-At>1.5){Check(C->Concealed(BB)&&!E->Visible(A,BB),TEXT("Real grass outside proximity hides before the paid detector"));Task(10);}return;}
    if(Phase==94){if(Now-At>6.5){Task(12);}return;}
    if(Phase==95){if(Now-At>6&&M->StateFor(A)->Build.Cooldowns[uint8(ELDSkill::Frostline)]<Now){Check(!SB->Tactical.Has(LDTactical::EStatus::Wet,Time)&&!ULDMapDefinition::Get(GetWorld())->RiverAt(Arena),TEXT("Dry Heart fixture waits for actual Wet and floor-field expiry"));DryHP=SB->Hp;MP=SA->Tactical.MP;GuardLock=M->StateFor(B)->WetFrostReadyAt;Task(17);}return;}
    auto Read=[](ALDB91Controller* PC,uint8 P){TSharedPtr<FJsonObject> R;if(const auto* S=PC->EnvironmentProbeReports.Find(P)){FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(*S),R);}return R;};auto RA=Read(A,Phase),RB=Read(B,Phase);if(!RA||!RB){return;}
    if(!C->RiverCommands.IsEmpty()){return;}
    Check(RA->GetBoolField(TEXT("completed"))&&RB->GetBoolField(TEXT("completed")),FString::Printf(TEXT("Both restricted combat phase%u snapshots complete"),Phase));
    if(Phase==1){Lock=M->StateFor(B)->WetFrostReadyAt;Check(Stunned&&GuardLock==600&&Lock-LDTactical::Milliseconds(SB->LastDamage)==6000,TEXT("Direct Frost on river Wet attempts600ms Stun with6000ms victim lock"));Check(SA->Tactical.MP<MP&&M->StateFor(A)->Build.Cooldowns[uint8(ELDSkill::Frostline)]>Now,TEXT("Prepared Frostline spends real MP and starts cooldown"));Task(2);return;}
    if(Phase==2){bool Ready=false;for(const auto& I:M->StateFor(A)->Inventory.Items){if(I.Kind==ELDItemKind::FrostPrism){const auto* P=M->Inventory->Owned(A,I.Id);Ready=P&&P->ReadyAt>Time;}}Check(Ready&&M->StateFor(B)->WetFrostReadyAt==Lock&&!SB->Tactical.Has(LDTactical::EStatus::Stun,Time),TEXT("Bound Frost Prism cannot repeat the victim reaction inside its lock"));Task(3);return;}
    if(Phase==3){Check(!M->StateFor(B)->Inventory.Items.ContainsByPredicate([](const auto& I){return I.Kind==ELDItemKind::FrostCapsule;})&&SB->Tactical.SurfaceSlow==30&&!SB->Tactical.Has(LDTactical::EStatus::Stun,Time)&&M->StateFor(B)->WetFrostReadyAt==Lock,TEXT("Consumed Frost Capsule adds frozen-floor slow without direct Stun or lock refresh"));Phase=91;At=Now;return;}
    if(Phase==4){GuardLock=M->StateFor(B)->WetFrostReadyAt;Check(!Stunned&&GuardLock>Lock,TEXT("Diagnostic Control Guard blocks Stun while consuming the next reaction attempt"));OldBody=SB->Identity;M->EndContenderLife(B,Now);Check(M->StateFor(B)->WetFrostReadyAt==GuardLock,TEXT("Reaction endpoint is stored outside the destroyed body"));Phase=92;At=Now;return;}
    if(Phase==6){Check(SB->Tactical.Has(LDTactical::EStatus::Wet,Time)&&M->StateFor(A)->Build.Cooldowns[uint8(ELDSkill::WaterJet)]>Now,TEXT("Paid Water Jet refreshes actual full-map Wet"));Task(7);return;}
    if(Phase==7){bool Steam=false;for(const auto& P:C->RiverCells){Steam|=P.Value.Obscured.Until>Time;}Check(Steam&&!E->Visible(A,BB)&&!E->Visible(B,BA),TEXT("Paid direct Fire on Wet creates symmetric Steam union occlusion"));MP=SA->Tactical.MP;Task(8);return;}
    if(Phase==8){Check(SA->Tactical.MP>=MP&&RA->GetStringField(TEXT("reason")).Contains(TEXT("no longer observable"))&&!RA->GetBoolField(TEXT("old_handle_resolved"))&&RA->GetIntegerField(TEXT("body_count"))==0,TEXT("Stale unit handle across Steam spends no MP and has no hidden body payload"));Task(9);return;}
    if(Phase==9)
    {
        Check(E->Visible(A,BB)&&E->Visible(B,BA),TEXT("Prepared Gust clears the selected Steam corridor and recalculates authorization"));
        auto* O=ALDWorldObjects::Get(GetWorld());for(auto W:O->Objects){if(W->Kind!=ELDObjectKind::TallGrass){continue;}FVector Pad;if(LDGround::MapSurface(GetWorld(),W->LastClear+FVector(275,0,0),Pad)&&FMath::Abs(Pad.Z-W->LastClear.Z)<1&&E->Ray(Pad+FVector(0,0,92.15),W->LastClear+FVector(0,0,92.15),W.Get())){Grass=W.Get();break;}}
        Check(Grass!=nullptr,TEXT("Paid detector fixture selects fitted flat grass"));if(Grass){Place(A,Grass->LastClear+FVector(275,0,0));Place(B,Grass->LastClear);Phase=93;At=Now;}return;
    }
    if(Phase==10)
    {
        Check(!M->StateFor(A)->Inventory.Items.ContainsByPredicate([](const auto& I){return I.Kind==ELDItemKind::SeersDust;})&&C->Concealed(BB)&&M->Vision->Detects(A,BB->GetActorLocation())&&E->Visible(A,BB)&&RA->GetIntegerField(TEXT("body_count"))>0,TEXT("Bound Seers Dust consumes once and its real catalog detector reveals grass beyond200cm"));
        auto* W=GetWorld()->SpawnActor<AStaticMeshActor>();auto* Mesh=W->GetStaticMeshComponent();Mesh->SetMobility(EComponentMobility::Movable);Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));W->SetActorLocation(Grass->LastClear+FVector(137.5,0,150));W->SetActorScale3D(FVector(.2,6,3));Mesh->SetCollisionProfileName(TEXT("BlockAll"));Mesh->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Ignore);Mesh->SetCollisionResponseToChannel(ECC_GameTraceChannel2,ECR_Block);Mesh->SetCanEverAffectNavigation(false);Wall=W;Task(11);return;
    }
    if(Phase==11){Check(!M->Vision->Detects(A,BB->GetActorLocation())&&!E->Visible(A,BB)&&RA->GetIntegerField(TEXT("body_count"))==0,TEXT("Physical wall blocks the paid detector and suppresses restricted body payload"));Wall->Destroy();Phase=94;At=Now;return;}
    if(Phase==12){Check(C->Concealed(BB)&&!M->Vision->Detects(A,BB->GetActorLocation())&&!E->Visible(A,BB),TEXT("Actual detector expiry revokes grass authorization without manual field deletion"));Task(13);return;}
    if(Phase==13)
    {
        Check(!C->Concealed(BB)&&E->Visible(A,BB)&&RA->GetIntegerField(TEXT("body_count"))>0,TEXT("Ordinary remote basic attack reveals grass after the real detector has expired"));
        const auto* Map=ULDMapDefinition::Get(GetWorld());bool Found=false;for(int Side:{1,-1}){FVector Want=Map->WetInset+FVector(Side*(Map->WetSize.X*.5+1500),0,0),Floor;Want.Z=Map->Height(FVector2D(Want));if(LDGround::MapSurface(GetWorld(),Want,Floor)&&!Map->RiverAt(Floor)&&E->Ray(Floor+FVector(-300,0,92.15),Floor+FVector(0,0,92.15))){Arena=DryArena=Floor;Found=true;break;}}
        Check(Found,TEXT("Native dry Heart approach has supported clear combat geometry"));Place(A,Arena-FVector(300,0,0));Place(B,Arena);Task(14);return;
    }
    if(Phase==14){Check(!ULDMapDefinition::Get(GetWorld())->RiverAt(Arena)&&SB->Tactical.Has(LDTactical::EStatus::Wet,Time),TEXT("Prepared Water Jet pays for Wet on the dry Heart approach"));Task(15);return;}
    if(Phase==15){Check(!E->Visible(A,BB)&&!E->Visible(B,BA),TEXT("Paid Fire contact creates Steam occlusion on the dry Heart approach"));FVector Floor;const bool Wet=LDGround::MapSurface(GetWorld(),ULDMapDefinition::Get(GetWorld())->WetInset,Floor)&&ULDMapDefinition::Get(GetWorld())->RiverAt(Floor);Check(Wet,TEXT("Native wet Heart approach retains its authored water floor"));Arena=Floor;Place(A,Arena-FVector(300,0,0));Place(B,Arena);Task(16);return;}
    if(Phase==16){Check(Stunned&&M->StateFor(B)->WetFrostReadyAt>Time,TEXT("Actual direct Frost on the wet Heart approach applies the same guarded reaction contract"));Arena=DryArena;Place(A,Arena-FVector(300,0,0));Place(B,Arena);Phase=95;At=Now;return;}
    if(Phase==17){const auto* Floor=C->RiverCells.Find(LDRiver::Cell(Arena));Check((!Floor||Floor->Frozen.Until<=Time)&&SB->Hp<DryHP&&SA->Tactical.MP<MP&&!Stunned&&M->StateFor(B)->WetFrostReadyAt==GuardLock,TEXT("Supported dry-floor Frostline spends normally and damages without invented ice, Stun or lock refresh"));Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,Failures?3:0);return;}
#endif
}
void ALDB91CombatProbe::Save(bool Complete){FString Out;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Out);auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("complete"),Complete);R->SetNumberField(TEXT("phase"),Phase);R->SetNumberField(TEXT("failures"),Failures);R->SetArrayField(TEXT("checks"),Checks);R->SetStringField(TEXT("scope"),TEXT("Diagnostic catalog items, learned build and bindings; actual remote requests, MP/cooldowns, consumption, physical river and life return. Normal acquisition is B92."));FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(Out/TEXT("Combat.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);}
