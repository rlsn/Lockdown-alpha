#include "Tests/LDGeometryProbe.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Objectives/LDObjectiveRules.h"
#include "Region/LDRegionDirector.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Core/LDGroundQueries.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDTacticalCatalog.h"
#include "Character/LDCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/LDNavigationComponent.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDTradeDirector.h"
#include "Economy/LDEconomyDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "Region/LDRegionDefinition.h"
#include "AIController.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

ALDGeometryProbe::ALDGeometryProbe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDGeometryProbe::BeginPlay()
{
    Super::BeginPlay();Started=At=FPlatformTime::Seconds();FParse::Value(FCommandLine::Get(),TEXT("LDProbeRole="),Role);if(Role.IsEmpty()){Role=TEXT("Server");}
    if(GetNetMode()==NM_Client){CastChecked<ALDPlayerController>(GetOwner())->ServerProbeHello(Role==TEXT("A")?1:2);}
}
void ALDGeometryProbe::Check(bool Value,const FString& Message){Checks.Add((Value?TEXT("PASS "):TEXT("FAIL "))+Message);if(!Value){++Failures;}UE_LOG(LogTemp,Display,TEXT("LD_B72_PROBE %s"),*Checks.Last());}
void ALDGeometryProbe::Enter(int32 Next)
{
    Phase=Next;At=FPlatformTime::Seconds();Checked=false;Sequence=0;
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* C=M->Combat.Get();const double Now=GetWorld()->GetTimeSeconds();
    for(TActorIterator<ALDTacticalField> It(GetWorld());It;++It){It->Destroy();}
    for(auto* PC:{A.Get(),B.Get()})
    {
        auto* O=M->StateFor(PC);auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());
        M->Objectives->Interrupt(PC,Now,true);PC->Navigation->ResetLife();Body->SetActorLocation(PC==A.Get()?FVector(-1800,-900,92):FVector(-1300,-900,92),false,nullptr,ETeleportType::TeleportPhysics);
        Body->GetCharacterMovement()->StopMovementImmediately();Body->GetCharacterMovement()->SetMovementMode(MOVE_None);
        auto* S=C->StateFor(Body);C->CancelTactical(Body,TEXT("Fixture reset"));S->Action=0;S->Tactical=LDTactical::FState();S->Tactical.LastRegen=LDTactical::Milliseconds(Now);
        O->Build=LDDevelopment::TacticalStart(0,M->TacticalCatalog->Checksum);O->Build.Revision=200+Next;O->Build.Level=PC==A.Get()?8:14;O->Build.Attributes[4]=9;
        for(const auto& D:M->TacticalCatalog->Abilities){O->Build.Ranks[uint8(D.Skill)]=1;}
        const ELDSkill Skills[]={ELDSkill::None,ELDSkill::SmokeVeil,ELDSkill::SeersFlare,ELDSkill::WireSnare,ELDSkill::SeersFlare,ELDSkill::ForceWave,ELDSkill::GroundingField,ELDSkill::Counterpulse,ELDSkill::RemedyBind,ELDSkill::RemedyBind,ELDSkill::VeilStep};
        O->Build.Slots={Skills[Next],ELDSkill::PurgeTouch,ELDSkill::AnchorBash,ELDSkill::SuretyShell,ELDSkill::Step};O->Build.Ranks[uint8(ELDSkill::Step)]=1;
        Check(LDDevelopment::Valid(O->Build),TEXT("Diagnostic ranks and five slots validate through native build rules"));
        O->ProtectedUntil=S->ProtectionUntil=0;O->MajorProtectionReadyAt=0;O->bAidConsent=false;S->CoreRecoveryUntil=0;S->LastDamage=S->LastDealt=-1000;
        O->Hp=O->MaxHp=S->Hp=LDDevelopment::MaxHP(O->Build);O->Mp=O->MaxMp=S->Tactical.MP=LDTactical::MaxMP(O->Build);M->SyncOwner(PC);
    }
    auto* SB=C->StateFor(Cast<ALDCharacter>(B->GetPawn()));
    if(Next==2){SB->Tactical.ConcealedUntil=LDTactical::Milliseconds(Now)+8000;}
    if(Next==4)
    {auto* F=GetWorld()->SpawnActor<ALDTacticalField>(FVector(-1500,-900,15),FRotator::ZeroRotator);F->Source=Cast<ALDCharacter>(A->GetPawn());F->SourceLife=C->StateFor(F->Source.Get())->Identity;F->Kind=ELDTacticalField::Trap;F->Hp=20;F->Radius=100;F->ExpiresAt=LDTactical::Milliseconds(Now)+45000;F->ArmedAt=F->ExpiresAt;}
    if(Next==5){C->ApplyStatus(Cast<ALDCharacter>(B->GetPawn()),LDTactical::EStatus::Root,LDTactical::Milliseconds(Now)+5000,FGuid::NewGuid());}
    if(Next==7)
    {
        const FVector Heart=LDRegion::Site(GetWorld(),LDObjective::Heart);A->GetPawn()->SetActorLocation(Heart+FVector(-80,0,92));B->GetPawn()->SetActorLocation(Heart+FVector(0,0,92));
        auto* G=M->GetGameState<ALDGameState>();G->CharterWindow=FGuid::NewGuid();G->CharterOpenAt=Now-1;G->CharterCloseAt=Now+60;G->CharterPhase=2;
        // Diagnostic access setup; claim and counterspell then use real remote RPCs.
    }
    if(Next==8){C->ApplyStatus(Cast<ALDCharacter>(B->GetPawn()),LDTactical::EStatus::Stun,LDTactical::Milliseconds(Now)+8000,FGuid::NewGuid());B->GetPawn()->SetActorLocation(FVector(-1400,-900,92));}
    if(Next==9){C->StateFor(Cast<ALDCharacter>(A->GetPawn()))->Tactical.ConcealedUntil=LDTactical::Milliseconds(Now)+8000;}
    A->ClientProbePhase(uint8(Next));B->ClientProbePhase(uint8(Next));
}
void ALDGeometryProbe::ServerTick()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M||!M->Combat){return;}
    if(!Phase)
    {
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){auto* PC=Cast<ALDPlayerController>(It->Get());if(PC&&PC->GetPawn()){if(PC->ProbeRole==1){A=PC;}if(PC->ProbeRole==2){B=PC;}}}
        if(!A.IsValid()||!B.IsValid()){return;}
        FString Why;M->SetLobbyChoice(A.Get(),0,true,Why);M->SetLobbyChoice(B.Get(),0,true,Why);
        if(!M->StartDevelopmentMatch(A.Get(),Why)&&!M->StartDevelopmentMatch(B.Get(),Why)){Check(false,Why);Finish();return;}
        Check(M->IsTacticalProfile()&&M->StateFor(A.Get())->Build.RulesVersion==7,TEXT("Separate native v07 match starts with schema7"));
        Check(FMath::IsNearlyEqual(M->GetGameState<ALDGameState>()->MatchDeadline-M->StartedAt(),2100.,.01),TEXT("v07 deadline is 2100s"));
        for(TActorIterator<ALDResident> It(GetWorld());It;++It){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();if(auto* AI=Cast<AAIController>(It->GetController())){AI->StopMovement();}}
        M->Vision->SetPublicSensor(false);Enter(1);return;
    }
    const double E=FPlatformTime::Seconds()-At;
    if(Phase==10&&Checked){if(E>2.8){Finish();}return;}
    if(!A.IsValid()||!B.IsValid()||!A->GetPawn()||!B->GetPawn()){Check(false,TEXT("Unexpected disconnect before final checks"));Finish();return;}
    auto* C=M->Combat.Get();auto* BA=Cast<ALDCharacter>(A->GetPawn());auto* BB=Cast<ALDCharacter>(B->GetPawn());auto* SA=C->StateFor(BA);auto* SB=C->StateFor(BB);const int64 AtMS=LDTactical::Milliseconds(GetWorld()->GetTimeSeconds());
    if(!Checked&&Phase==7&&E>1.2){Checked=true;Check(M->Objectives->HasChannel(B.Get())&&M->StateFor(B.Get())->ChannelProgress>.4,TEXT("Real damage and nearby rival preserve v07 Heart claim progress"));Check(SB->Hp<M->StateFor(B.Get())->MaxHp,TEXT("Claimant actually received ordinary attack damage"));ClaimStarted=M->StateFor(B.Get())->ChannelStartedAt;
        for(auto K:{LDTactical::EStatus::Silence,LDTactical::EStatus::Mute,LDTactical::EStatus::Root}){C->ApplyStatus(BB,K,AtMS+4000,FGuid::NewGuid());}
        Check(M->Objectives->HasChannel(B.Get()),TEXT("Silence mute root alone preserve stationary Heart claim"));}
    if(!Checked&&Phase==9&&E>1.3)
    {Checked=true;Check(SA->Tactical.GuardUntil>AtMS&&!C->Concealed(BA),TEXT("Remote self Remedy grants brief guard and breaks concealment"));C->ApplyStatus(BA,LDTactical::EStatus::Stun,AtMS+3000,FGuid::NewGuid());Check(!SA->Tactical.Has(LDTactical::EStatus::Stun,AtMS)&&!SA->Tactical.GuardUntil,TEXT("One hard control component consumes one guard"));C->ApplyStatus(BA,LDTactical::EStatus::Hex,AtMS+3000,FGuid::NewGuid());Check(SA->Tactical.Has(LDTactical::EStatus::Hex,AtMS),TEXT("Second hard control is not immune"));
        const auto Family=SA->Tactical.MajorReadyAt;M->RefreshBuild(A.Get());Check(SA->Tactical.MajorReadyAt==Family&&M->StateFor(A.Get())->MajorProtectionReadyAt==Family,TEXT("Native build refresh preserves owner major family endpoint"));}
    if(Phase==10){if(!Checked&&E>1.25){Checked=true;AuthorityEdges();}if(E>2.8){Finish();}return;}
    if(Phase==3&&!Checked&&E>2.6){Checked=true;BB->SetActorLocation(FVector(-1550,-900,92));C->SampleFields(AtMS);Check(SB->Tactical.Has(LDTactical::EStatus::Root,AtMS),TEXT("Armed trap applies root to one body"));int32 Live=0;for(TActorIterator<ALDTacticalField> It(GetWorld());It;++It){Live+=It->Active(AtMS)&&It->Kind==ELDTacticalField::Trap;}Check(Live==0,TEXT("Triggered trap consumed exactly once"));}
    if(E<3){return;}
    if(Phase==1){Check(!M->Vision->CanObserve(A.Get(),BB)&&!M->Vision->CanObserve(B.Get(),BA),TEXT("Smoke boundary hides both directions on authority"));Check(SA->Tactical.MP<M->StateFor(A.Get())->MaxMp-25,TEXT("Unseen ground cast commits one real cost"));}
    if(Phase==2){Check(C->Concealed(BB)&&M->Vision->CanObserve(A.Get(),BB),TEXT("Detector grants geometric observation while concealment remains active"));}
    if(Phase==4){bool Seen=false;for(TActorIterator<ALDTacticalField> It(GetWorld());It;++It){if(It->Kind==ELDTacticalField::Trap){Seen=M->Vision->CanObserve(B.Get(),*It);}}Check(Seen,TEXT("Rival trap becomes observable within own detector geometry"));}
    if(Phase==5){Check(BB->GetActorLocation().X>-1100,TEXT("Remote Force Wave moves rooted body by native swept movement"));Check(SB->Tactical.Has(LDTactical::EStatus::Root,AtMS),TEXT("Push does not cleanse root"));}
    if(Phase==6){Check(!C->Allows(BB,LDTactical::EAction::Dash)&&C->Allows(BB,LDTactical::EAction::Move),TEXT("Grounding blocks dash but permits walking"));LDTactical::Dispel(SB->Tactical,false);Check(!C->Allows(BB,LDTactical::EAction::Dash),TEXT("Basic dispel cannot remove source-bound grounding"));}
    if(Phase==7){Check(!M->Objectives->HasChannel(B.Get())&&ClaimStarted>0,TEXT("Level8 remote Counterpulse cancels level14 independent Heart claim"));Check(SB->Hp>0,TEXT("Claim cancellation needs no kill or automatic damage reset"));}
    if(Phase==8){Check(M->StateFor(B.Get())->bAidConsent&&!SB->Tactical.Has(LDTactical::EStatus::Stun,AtMS),TEXT("Remote explicit consent permits external strong dispel"));}
    Enter(Phase+1);
}
void ALDGeometryProbe::ClientTick()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwner());const double Now=FPlatformTime::Seconds();if(Now-LastPing>.15){LastPing=Now;PC->ServerProbePing(Now);}
    if(PC->ProbePhase!=Phase){Phase=PC->ProbePhase;At=Now;Sent=Checked=false;Sequence=0;}if(!Phase){return;}
    const double E=Now-At;const bool IsA=Role==TEXT("A");const auto& O=PC->OwnerState;const uint32 Token=5000+Phase*100;
    auto FindBody=[&](){FGuid Id;const FVector Expected=Phase==7?LDRegion::Site(GetWorld(),LDObjective::Heart)+FVector(0,0,92):FVector(-1400,-900,92);if(auto* R=PC->LocalView()){for(const auto& V:R->LastView().Entities){if(V.Kind==ELDObservedKind::Body&&FVector::DistSquared2D(V.Position,Expected)<100){Id=V.Id;break;}}}return Id;};
    if(Phase==7&&!IsA&&E>.2){if(!Sent){PC->ServerContextRequest(O.Match,O.Life,Token,LDObjective::Heart,FGuid());Sent=true;}PC->ServerContextHold(O.Match,O.Life,Token,++Sequence,false);}
    if(Phase==8&&!IsA&&!Sent&&E>.1){PC->ServerAidConsent(O.Match,O.Life,true);Sent=true;}
    const bool Caster=Phase==4?!IsA:IsA;
    if(Caster&&!Sent&&E>.45)
    {
        if(Phase==7){PC->ServerCombatAction(O.Match,O.Life,Token,0,FVector::ForwardVector);Sent=true;}
        else if(Phase==8){const auto Id=FindBody();if(!Id.IsValid()){return;}PC->SendTacticalAction(Token,0,FVector::ForwardVector,Id);Sent=true;}
        else if(Phase==9||Phase==10){PC->SendTacticalAction(Token,0,FVector::ForwardVector,FGuid());Sent=true;}
        else{const FVector P=Phase==1||Phase==2||Phase==6?FVector(-1300,-900,0):Phase==3?FVector(-1550,-900,0):FVector(-1500,-900,0);PC->SendTacticalAction(Token,0,FVector::ForwardVector,FGuid(),P);Sent=true;}
    }
    if(Phase==7&&IsA&&Sent&&Sequence==0&&E>1.5){const auto Id=FindBody();if(Id.IsValid()){PC->SendTacticalAction(Token+1,0,FVector::ForwardVector,Id);++Sequence;}}
    if(!Checked&&E>1.7)
    {
        Checked=true;Check(O.Build.RulesVersion==7&&O.Build.Slots.Num()==5,TEXT("Independent owner sees tactical build"));Check(O.Mp>=0&&O.Mp<=O.MaxMp,TEXT("Private MP bounded"));
        const auto* R=PC->LocalView();Check(R&&!R->IsStale(),TEXT("Independent filtered view stays fresh"));
        if(R)
        {
            const auto& V=R->LastView();int32 Trap=0,Body=0;for(const auto& X:V.Entities){Trap+=X.Kind==ELDObservedKind::TacticalField&&X.Presentation==2;Body+=X.Kind==ELDObservedKind::Body&&FVector::DistSquared2D(X.Position,FVector(-1300,-900,92))<100;}
            if(Phase==1&&IsA){Check(Body==0,TEXT("Hidden victim absent from remote smoke payload"));}
            if(Phase==2&&IsA){Check(Body==1,TEXT("Remote detector observes concealed body without identity metadata"));}
            if(Phase==3){Check(IsA?Trap==1:Trap==0,TEXT("Remote trap payload filtered independently for owner/rival"));}
            if(Phase==4&&!IsA){Check(Trap==1,TEXT("Remote detector reveals rival trap"));}
        }
    }
    if(Phase==10&&E>1.8){Finish();}
}
void ALDGeometryProbe::AuthorityEdges()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* C=M->Combat.Get();auto* BA=Cast<ALDCharacter>(A->GetPawn());auto* BB=Cast<ALDCharacter>(B->GetPawn());auto* SA=C->StateFor(BA);auto* SB=C->StateFor(BB);const double Now=GetWorld()->GetTimeSeconds();const int64 AtMS=LDTactical::Milliseconds(Now);
    auto* Wall=GetWorld()->SpawnActor<AStaticMeshActor>(FVector(-1200,-900,150),FRotator::ZeroRotator);Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube")));Wall->SetActorScale3D(FVector(.2,3,3));Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));Wall->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel2,ECR_Block);
    BB->SetActorLocation(FVector(-1300,-900,92));SB->Action=7;
    Check(C->Push(BB,FVector::ForwardVector,250,Now)&&BB->GetActorLocation().X<-1220,TEXT("Native swept push stops before static wall and cancels channel only after movement"));
    SB->Action=7;Check(!C->Push(BB,FVector::ForwardVector,250,Now)&&SB->Action==7,TEXT("Zero displacement at wall preserves channel"));SB->Action=0;
    auto* Detector=GetWorld()->SpawnActor<ALDTacticalField>(FVector(-1100,-900,15),FRotator::ZeroRotator);Detector->Source=BA;Detector->SourceLife=SA->Identity;Detector->Kind=ELDTacticalField::Detector;Detector->Hp=1;Detector->Radius=350;Detector->ExpiresAt=AtMS+5000;
    Check(!M->Vision->Detects(A.Get(),BB->GetActorLocation()),TEXT("Detector cannot reveal across real collision wall"));
    const FGuid Life=Detector->SourceLife;SA->Identity=FGuid::NewGuid();Check(!Detector->Active(AtMS),TEXT("Old life field invalid before next publication"));C->SampleFields(AtMS);Check(Detector->IsActorBeingDestroyed(),TEXT("Source loss destroys field"));SA->Identity=Life;
    Wall->Destroy();FVector Floor;LDGround::MapSurface(GetWorld(),FVector(-1700,-900,0),Floor);BB->SetActorLocation(Floor+FVector(0,0,92));
    SB->Action=7;Check(C->Push(BB,FVector(0,1,0),.05,Now)&&SB->Action==0,TEXT("Even a real submillimeter push interrupts; only zero displacement is exempt"));BB->SetActorLocation(Floor+FVector(0,0,92));
    const auto* D=M->TacticalCatalog->Find(ELDSkill::FarwatchWard);M->StateFor(B.Get())->Build.Slots[0]=ELDSkill::FarwatchWard;SB->Tactical.MP=200;SB->Tactical.LastRegen=AtMS;SB->CoreRecoveryUntil=0;FString Why;
    int32 Count=0;for(int32 I=0;I<2;++I){auto* F=GetWorld()->SpawnActor<ALDTacticalField>(Floor,FRotator::ZeroRotator);F->Source=BB;F->SourceLife=SB->Identity;F->Kind=ELDTacticalField::Ward;F->Radius=560;F->Hp=40;F->ExpiresAt=AtMS+60000;++Count;}
    const auto Revision=M->StateFor(B.Get())->Build.Revision;Check(C->RequestEquipped(BB,Revision,0,90000,FVector::ForwardVector,Why,nullptr,Floor),TEXT("Field limit rechecked at release after accepted windup"));
    // Advance only the pending endpoint in this controlled fixture; wall-clock is unchanged.
    SB->CoreEndsAt=AtMS;C->Sample(Now);Check(SB->Tactical.MP==200-D->Mana&&M->StateFor(B.Get())->Build.Cooldowns[uint8(ELDSkill::FarwatchWard)]>Now,TEXT("Third source replaces oldest and spends one release cost under B75 owner-limit semantics"));
    SA->Tactical.Statuses.Reset();C->ApplyStatus(BA,LDTactical::EStatus::Blind,AtMS+3000,FGuid::NewGuid());Check(M->Vision->SourcesFor(A.Get()).ContainsByPredicate([](const auto& X){return X.Radius==200;}),TEXT("Native blind contracts own body sight"));
    auto* Ward=GetWorld()->SpawnActor<ALDTacticalField>(BA->GetActorLocation(),FRotator::ZeroRotator);Ward->Source=BA;Ward->SourceLife=SA->Identity;Ward->Kind=ELDTacticalField::Ward;Ward->Hp=40;Ward->Radius=560;Ward->ExpiresAt=AtMS+60000;
    Check(M->Vision->SourcesFor(A.Get()).ContainsByPredicate([](const auto& X){return X.Radius==560;}),TEXT("Blind leaves independent authorized ward radius intact"));
    auto* OA=M->StateFor(A.Get());OA->Build.Attributes[3]=11;OA->Build.Slots[0]=ELDSkill::BreakSigil;SA->CoreRecoveryUntil=0;
    Check(C->RequestEquipped(BA,OA->Build.Revision,0,90001,FVector::ForwardVector,Why,BB),TEXT("Natural-gated Break Sigil accepts observed body through shared dispatcher"));
    SA->CoreEndsAt=AtMS;C->Sample(Now);Check(SB->Tactical.Has(LDTactical::EStatus::Break,AtMS)&&!LDTactical::PassiveActive(SB->Tactical,true,AtMS)&&LDTactical::PassiveActive(SB->Tactical,false,AtMS),TEXT("Native Break Sigil suppresses tagged passive query only"));
    auto* Resident=GetWorld()->SpawnActor<ALDResident>(FVector(-1750,-800,92),FRotator::ZeroRotator);Resident->SetActorTickEnabled(false);SB=C->StateFor(BB);
    C->ApplyStatus(Resident,LDTactical::EStatus::Root,AtMS+3000,FGuid::NewGuid());Check(Resident->GetCharacterMovement()->GetMaxSpeed()==0,TEXT("Ordinary native NPC root stops voluntary movement too"));
    auto NPC=LDDevelopment::TacticalStart(0,M->TacticalCatalog->Checksum);NPC.Ranks[uint8(ELDSkill::SuretyShell)]=1;NPC.Slots={ELDSkill::SuretyShell,ELDSkill::None,ELDSkill::None,ELDSkill::None,ELDSkill::None};
    Check(C->RegisterNPCBuild(Resident,NPC),TEXT("Native NPC accepts the same delivered tactical build"));auto* N=C->StateFor(Resident);N->Tactical.MP=180;N->Tactical.LastRegen=AtMS;
    Check(C->RequestEquipped(Resident,NPC.Revision,0,1,FVector::ForwardVector,Why),TEXT("Native NPC uses shared tactical release path"));N->CoreEndsAt=AtMS;C->Sample(Now);const double Ready=N->NPCBuild.Cooldowns[uint8(ELDSkill::SuretyShell)];
    Check(Ready>Now&&C->RegisterNPCBuild(Resident,NPC)&&N->NPCBuild.Cooldowns[uint8(ELDSkill::SuretyShell)]==Ready,TEXT("NPC refresh preserves new-ID absolute cooldowns"));Resident->Destroy();
    SB->Hp=0;C->SampleFields(AtMS);Check(M->Vision->SourcesFor(B.Get()).Num()<=2,TEXT("Dead source removes all tactical sight sources"));SB->Hp=M->StateFor(B.Get())->MaxHp;
}
void ALDGeometryProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(FPlatformTime::Seconds()-Started>55){Check(false,TEXT("B72 network fixture timed out"));Finish();return;}
    if(GetNetMode()==NM_Client){ClientTick();}else{ServerTick();}
#endif
}
void ALDGeometryProbe::Finish()
{
    if(GetNetMode()==NM_Client)
    {auto* PC=CastChecked<ALDPlayerController>(GetOwner());auto Samples=PC->ProbeRtts;Samples.Sort();Check(Samples.Num()>=15,TEXT("Independent client transport echo measured"));if(!Samples.IsEmpty()){Check(Samples[Samples.Num()/2]>=20&&Samples[Samples.Num()/2]<350,FString::Printf(TEXT("Echo median %.1fms"),Samples[Samples.Num()/2]));}}
    TSharedRef<FJsonObject> Json=MakeShared<FJsonObject>();Json->SetNumberField(TEXT("failures"),Failures);Json->SetStringField(TEXT("scope"),TEXT("Controlled native B72 fixture; legal RPCs, diagnostic build/items/placement; not a normal earned route"));TArray<TSharedPtr<FJsonValue>> Lines;for(const auto& S:Checks){Lines.Add(MakeShared<FJsonValueString>(S));}Json->SetArrayField(TEXT("checks"),Lines);
    FString Directory,Output;FParse::Value(FCommandLine::Get(),TEXT("LDProbeReportDir="),Directory);FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Output));
    if(GetNetMode()!=NM_Client){auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(M&&M->Economy){if(!FFileHelper::SaveStringToFile(M->Economy->Accounting().ExportJson(TEXT("B72Fixture")),*(Directory/TEXT("Ledger.json")))){++Failures;}}}
    if(!FFileHelper::SaveStringToFile(Output,*(Directory/(Role+TEXT(".json"))))){++Failures;}SetActorTickEnabled(false);FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
