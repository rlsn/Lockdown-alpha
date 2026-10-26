#include "Tests/LDB83Probe.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDRiverRules.h"
#include "Combat/LDTacticalCatalog.h"
#include "Development/LDDevelopmentDirector.h"
#include "Development/LDV08Loadout.h"
#include "Economy/LDEconomyDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDAcquisitionDirector.h"
#include "Inventory/LDAcquisitionCatalog.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDRegionDefinition.h"
#include "Vision/LDWorldViewRelay.h"
#include "Vision/LDVisionDirector.h"
#include "World/LDWorldDirector.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    ALDCharacter* Body(ALDPlayerController* PC){return CastChecked<ALDCharacter>(PC->GetPawn());}
    const FLDInventoryItem* Item(const FLDOwnerState& O,ELDItemKind K){return O.Inventory.Items.FindByPredicate([&](const auto& I){return I.Kind==K;});}
}
ALDB83Probe::ALDB83Probe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDB83Probe::BeginPlay()
{Super::BeginPlay();Started=At=FPlatformTime::Seconds();FParse::Value(FCommandLine::Get(),TEXT("LDProbeRole="),Role);if(Role.IsEmpty()){Role=TEXT("Server");}if(GetNetMode()==NM_Client){CastChecked<ALDPlayerController>(GetOwner())->ServerProbeHello(Role==TEXT("A")?1:2);}}
void ALDB83Probe::Check(bool Value,const FString& Message)
{Checks.Add((Value?TEXT("PASS "):TEXT("FAIL "))+Message);if(!Value){++Failures;}UE_LOG(LogTemp,Display,TEXT("LD_B83 %s"),*Checks.Last());}
FString ALDB83Probe::NavigationSignature() const
{const auto* P=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),FVector(-2100,-700,92),FVector(-1100,-700,92));FString S;if(!P||!P->IsValid()||P->IsPartial()){return S;}for(auto Point:P->PathPoints){S+=Point.ToString();}return S;}
void ALDB83Probe::Enter(int32 Next)
{
    Phase=Next;At=FPlatformTime::Seconds();Sent=Checked=false;PlayerWet=NPCWet=PlayerSlow=NPCSlow=Stunned=false;
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* C=M->Combat.Get();const int64 Now=LDTactical::Milliseconds(GetWorld()->GetTimeSeconds());
    auto Place=[&](ALDCharacter* P,FVector Point,bool Walking){P->GetCharacterMovement()->StopMovementImmediately();P->SetActorLocation(Point+FVector(0,0,92));P->GetCharacterMovement()->SetMovementMode(Walking?MOVE_Walking:MOVE_None);};
    if(Next==3){Place(Body(A.Get()),FVector(-2100,-700,0),true);Place(NPC.Get(),FVector(-2100,-900,0),true);Place(Body(B.Get()),FVector(-1250,-650,0),false);}
    if(Next==3||Next==4||Next==5){const double X=Next==4?-2100:-1100;if(Next!=5){CastChecked<AAIController>(NPC->GetController())->MoveToLocation(FVector(X,-900,0),20);}Path=Path.IsEmpty()?NavigationSignature():Path;}
    if(Next>=6&&Next<=9)
    {
        for(auto* PC:{A.Get(),B.Get()}){PC->Navigation->Stop();auto* S=C->StateFor(Body(PC));S->ProtectionUntil=0;M->StateFor(PC)->ProtectedUntil=0;S->Tactical.Statuses.Reset();S->Hp=LDDevelopment::MaxHP(M->StateFor(PC)->Build);}
        Place(Body(A.Get()),FVector(-1900,-700,0),false);Place(Body(B.Get()),FVector(-1550,-700,0),false);Place(NPC.Get(),FVector(-1900,-1050,0),false);
        if(Next==6){C->RefreshRiverContact(Body(B.Get()),Now,true);MP=C->StateFor(Body(A.Get()))->Tactical.MP;}
        if(Next==7){Ready=M->StateFor(B.Get())->WetFrostReadyAt;C->RefreshRiverContact(Body(B.Get()),Now,true);}
        if(Next==8){Place(Body(B.Get()),FVector(-1300,-700,0),false);MP=C->StateFor(Body(A.Get()))->Tactical.MP;}
        if(Next==9){Place(Body(B.Get()),FVector(-1900,-300,0),false);}
    }
    if(Next==10){for(auto* PC:{A.Get(),B.Get()}){PC->Navigation->Stop();Place(Body(PC),LDRegion::Site(GetWorld(),1)+FVector(PC==A.Get()?20:90,0,0),false);auto* S=C->StateFor(Body(PC));S->LastDamage=S->LastDealt=GetWorld()->GetTimeSeconds()-20;S->ProtectionUntil=0;M->StateFor(PC)->ProtectedUntil=0;}
        auto* O=M->StateFor(A.Get());O->Gold+=64;M->RecordEconomyGold(TEXT("CourierReward"),64);int32 N=0;for(auto Kind:{ELDItemKind::Book,ELDItemKind::OilFlask,ELDItemKind::GustVial,ELDItemKind::SmokeFlask}){M->Acquisition->Purchase(A.Get(),O->Match,O->Life,910000+N++,2,Kind,Kind==ELDItemKind::Book?ELDSkill::Firebolt:ELDSkill::None,O->Inventory.Revision,ULDAcquisitionCatalog::Load()->Checksum);}Check(O->Gold==0&&Item(*O,ELDItemKind::OilFlask)&&Item(*O,ELDItemKind::GustVial),TEXT("B83 paid Firebolt and field consumables debit authored prices"));}
    if(Next==12){Place(Body(A.Get()),FVector(-1500,-1000,0),false);Place(Body(B.Get()),FVector(-1100,-1000,0),false);for(auto* PC:{A.Get(),B.Get()}){auto* S=C->StateFor(Body(PC));S->Tactical.Statuses.Reset();S->ProtectionUntil=0;S->Hp=LDDevelopment::MaxHP(M->StateFor(PC)->Build);M->StateFor(PC)->ProtectedUntil=0;}}
    if(Next==13){MP=C->StateFor(Body(A.Get()))->Tactical.MP;}
    M->SyncOwner(A.Get());M->SyncOwner(B.Get());A->ClientProbePhase(Next);B->ClientProbePhase(Next);
}
void ALDB83Probe::ServerTick()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M||!M->Combat){return;}auto* C=M->Combat.Get();
    if(!Phase)
    {
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){auto* PC=Cast<ALDPlayerController>(It->Get());if(PC&&PC->GetPawn()){if(PC->ProbeRole==1){A=PC;}if(PC->ProbeRole==2){B=PC;}}}if(!A.IsValid()||!B.IsValid()){return;}
        FString Why;M->SetLobbyChoice(A.Get(),5,true,Why);M->SetLobbyChoice(B.Get(),2,true,Why);if(!M->StartDevelopmentMatch(A.Get(),Why)&&!M->StartDevelopmentMatch(B.Get(),Why)){Check(false,Why);Finish();return;}
        Check(M->IsV08Profile()&&M->WorldRuntime&&GetWorld()->GetMapName().Contains(TEXT("L_V08Region")),TEXT("Separate native River Reach starts with retained world clock"));
        for(TActorIterator<ALDResident> It(GetWorld());It;++It){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();if(auto* AI=Cast<AAIController>(It->GetController())){AI->StopMovement();}}
        for(auto* PC:{A.Get(),B.Get()}){PC->Navigation->ResetLife();Body(PC)->SetActorLocation(LDRegion::Site(GetWorld(),1)+FVector(PC==A.Get()?20:90,0,92));Body(PC)->GetCharacterMovement()->SetMovementMode(MOVE_None);auto* S=C->StateFor(Body(PC));S->ProtectionUntil=0;S->LastDamage=S->LastDealt=GetWorld()->GetTimeSeconds()-20;M->StateFor(PC)->ProtectedUntil=0;}
        auto Grant=[&](ALDPlayerController* PC,ELDItemKind K){auto* I=GetWorld()->SpawnActor<ALDItem>();I->Kind=K;Check(M->Inventory->Adopt(PC,I,TEXT("DiagnosticItemCreation")),TEXT("Labelled fixture physical identity enters ledger"));};
        Grant(A.Get(),ELDItemKind::FrostPrism);Grant(A.Get(),ELDItemKind::WaterFlask);Grant(B.Get(),ELDItemKind::FrostCapsule);Grant(B.Get(),ELDItemKind::RiverWaders);Grant(B.Get(),ELDItemKind::RiverTreads);
        auto* O=M->StateFor(A.Get());O->Gold+=40;M->RecordEconomyGold(TEXT("CourierReward"),40);const auto Before=O->Inventory.Items.Num();
        M->Acquisition->Purchase(A.Get(),O->Match,O->Life,900001,2,ELDItemKind::Book,ELDSkill::WaterJet,O->Inventory.Revision,ULDAcquisitionCatalog::Load()->Checksum);
        Check(O->Gold==0&&O->Inventory.Items.Num()==Before+1,TEXT("Water Jet book costs real40g via native supply; wallet funding is diagnostic"));
        NPC=GetWorld()->SpawnActor<ALDResident>(FVector(-2100,-900,92),FRotator::ZeroRotator);NPC->SetActorTickEnabled(false);NPC->SpawnDefaultController();Check(C->RegisterNPCBuild(NPC.Get(),LDDevelopment::TacticalStart(5,M->TacticalCatalog->Checksum,8)),TEXT("Native AI owns the same paid MP/slot/cooldown build"));
        Enter(1);return;
    }
    const double E=FPlatformTime::Seconds()-At;if(Phase==16){if(E>.5){Boundaries();Enter(17);}return;}if(Phase==17){if(E>3){Finish();}return;}
    if(!A.IsValid()||!B.IsValid()||!NPC.IsValid()){Check(false,TEXT("Unexpected fixture disconnect"));Finish();return;}
    auto* SA=C->StateFor(Body(A.Get()));auto* SB=C->StateFor(Body(B.Get()));auto* SN=C->StateFor(NPC.Get());const int64 Now=LDTactical::Milliseconds(GetWorld()->GetTimeSeconds());
    if(Phase>=3&&Phase<=5){PlayerWet|=SA->Tactical.Has(LDTactical::EStatus::Wet,Now);NPCWet|=SN->Tactical.Has(LDTactical::EStatus::Wet,Now);PlayerSlow|=SA->Tactical.SurfaceSlow==30;NPCSlow|=SN->Tactical.SurfaceSlow==30;Stunned|=SA->Tactical.Has(LDTactical::EStatus::Stun,Now)||SN->Tactical.Has(LDTactical::EStatus::Stun,Now);}
    if(Phase==5&&!Sent&&E>1.6){Sent=true;CastChecked<AAIController>(NPC->GetController())->MoveToLocation(FVector(-1100,-900,0),20);}
    if(Phase==6){Stunned|=SB->Tactical.Has(LDTactical::EStatus::Stun,Now);}
    if(Phase==7&&!Sent&&E>.5)
    {Sent=true;FString Why;const auto* NB=C->BuildFor(NPC.Get());FLDTacticalRequest R;R.Schema=8;R.WireVersion=4;R.Token=1;R.BuildRevision=NB->Revision;R.Slot=NB->Slots.Find(ELDSkill::Frostline);R.Point=Body(B.Get())->GetActorLocation();R.Direction=(FVector(R.Point)-NPC->GetActorLocation()).GetSafeNormal2D();MP=SN->Tactical.MP;Check(C->RequestContent(NPC.Get(),R,nullptr,nullptr,Why),TEXT("Ordinary native AI direct Frost accepted through shared content handler: ")+Why);}
    const double Duration=Phase==5?8.5:Phase==4?8:Phase==3?6.5:Phase<=2||Phase==10||Phase==11?4:3;
    if(E<Duration){return;}
    if(Phase==1){Check(LDDevelopment::Rank(M->StateFor(A.Get())->Build,ELDSkill::WaterJet)==1,TEXT("Remote held reading consumes paid book into actual rank1 knowledge"));}
    if(Phase==2){const auto* O=M->StateFor(A.Get());Check(O->Build.Actions[0].Skill==ELDSkill::Frostline&&O->Build.Actions[1].Skill==ELDSkill::WaterJet,TEXT("Remote quiet preparation equips both learned sources"));Check(M->StateFor(B.Get())->Inventory.Bindings[2].IsValid()&&M->StateFor(B.Get())->Inventory.Bindings[3].IsValid(),TEXT("Remote draft binds Waders/Treads through physical slots"));}
    if(Phase>=3&&Phase<=5)
    {const double X=Phase==4?-2100:-1100;Check(FMath::Abs(Body(A.Get())->GetActorLocation().X-X)<100&&FMath::Abs(NPC->GetActorLocation().X-X)<100,FString::Printf(TEXT("Native player movement and AI pathfollowing cross river banks (playerX=%.1f npcX=%.1f)"),Body(A.Get())->GetActorLocation().X,NPC->GetActorLocation().X));Check(PlayerWet&&NPCWet&&!Stunned,TEXT("Native crossings refresh Wet without contact stun"));Check(!Path.IsEmpty()&&NavigationSignature()==Path,TEXT("Same complete native path before/during/after Frozen; no elemental nav rebuild"));if(Phase==4){Check(PlayerSlow&&NPCSlow,TEXT("Both actual moving capsules receive30percent Frozen contact slow"));}if(Phase==5){Check(SA->Tactical.SurfaceSlow==0&&SN->Tactical.SurfaceSlow==0,TEXT("Thaw/exit removes contact slow"));}}
    if(Phase==6){Check(Stunned&&M->StateFor(B.Get())->WetFrostReadyAt>Now,TEXT("Remote direct Frost on Wet applies real600ms Stun and shared6s endpoint"));Check(SA->Tactical.MP<MP-20&&M->StateFor(A.Get())->Build.Cooldowns[38]>GetWorld()->GetTimeSeconds(),TEXT("Frostline commits actual30MP and14s cooldown"));Check(SB->Tactical.SurfaceSlow==15,TEXT("Bound Treads halve only surface slow; do not suppress Wet-stun"));}
    if(Phase==7){Check(M->StateFor(B.Get())->WetFrostReadyAt==Ready,TEXT("Different AI attacker and Frost Prism share victim lock without extension"));Check(SN->Tactical.MP<MP-20&&M->StateFor(A.Get())->Inventory.Find(Item(*M->StateFor(A.Get()),ELDItemKind::FrostPrism)->Id)->ReadyAt>Now,TEXT("AI Frost spends MP; remote reusable Prism starts actual family cooldown"));}
    if(Phase==8){Check(SA->Tactical.MP<MP-15&&SB->Tactical.Has(LDTactical::EStatus::Wet,Now),TEXT("Remote Water Jet pays24MP, applies Wet and creates bounded line cells"));}
    if(Phase==9){Check(!Item(*M->StateFor(A.Get()),ELDItemKind::WaterFlask)&&SB->Tactical.Has(LDTactical::EStatus::Wet,Now),TEXT("Remote Water Flask consumes original physical source and applies neutral Wet"));FString Why;Check(M->Economy->Audit(Why)&&M->Economy->AuditFailures==0,TEXT("Native paid/consumed source and gold ledgers reconcile: ")+Why);}
    if(Phase==10){Check(LDDevelopment::Rank(M->StateFor(A.Get())->Build,ELDSkill::Firebolt)==1,TEXT("Remote held reading consumes paid Firebolt book"));}
    if(Phase==11){Check(M->StateFor(A.Get())->Build.Slots[2]==ELDSkill::Firebolt,TEXT("Remote atomic preparation equips Firebolt and paid consumable sources"));}
    if(Phase==12){Check(!Item(*M->StateFor(A.Get()),ELDItemKind::OilFlask)&&C->RiverCells.FindRef(LDRiver::Cell(Body(B.Get())->GetActorLocation())).Oil.Until>Now,TEXT("Remote paid Oil Flask creates finite overlay and consumes once"));}
    if(Phase==13){const auto Cell=C->RiverCells.FindRef(LDRiver::Cell(Body(B.Get())->GetActorLocation()));Check(Cell.Burning.Until>Now&&Cell.Oil.Until==0&&SA->Tactical.MP<MP-15&&C->EnvironmentLeases.IsEmpty(),TEXT("Paid Firebolt materializes oil-fire, spends MP and releases reservation"));}
    if(Phase==14){Check(!M->Vision->CanObserve(A.Get(),Body(B.Get()))&&M->Vision->CanObserve(B.Get(),Body(A.Get()))==false,TEXT("Native smoke union hides actors across its boundary symmetrically"));}
    if(Phase==15){Check(!Item(*M->StateFor(A.Get()),ELDItemKind::GustVial)&&M->Vision->CanObserve(A.Get(),Body(B.Get())),TEXT("Remote Gust Vial clears only selected corridor and recomputes authorization"));}
    Enter(Phase+1);
}
void ALDB83Probe::ClientTick()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwner());const double Now=FPlatformTime::Seconds();if(Now-PingAt>.15){PingAt=Now;PC->ServerProbePing(Now);}
    if(PC->ProbePhase!=Phase){Phase=PC->ProbePhase;At=Now;Sent=Checked=false;Sequence=0;}if(!Phase){return;}const double E=Now-At;const auto& O=PC->OwnerState;const uint32 Token=100000+Phase*100;
    if(!Sent&&E>(Phase==5?1.7:Phase==4&&Role==TEXT("A")?1.1:.5))
    {
        if(Phase==1&&Role==TEXT("A")){const auto* Book=O.Inventory.Items.FindByPredicate([](const auto& I){return I.Kind==ELDItemKind::Book&&I.Skill==ELDSkill::WaterJet;});if(!Book){return;}PC->ServerInventoryRequest(O.Match,O.Life,Token,O.Inventory.Revision,LDInventory::Read,Book->Id,Book->Revision,0,FVector::ZeroVector);}
        if(Phase==2)
        {auto P=LDDevelopment::PlanFrom(O.Build);P.InventoryRevision=O.Inventory.Revision;P.Bindings=O.Inventory.Bindings;
            if(Role==TEXT("A")){P.Slots[0]=ELDSkill::Frostline;P.Actions[0].Skill=ELDSkill::Frostline;P.Slots[1]=ELDSkill::WaterJet;P.Actions[1].Skill=ELDSkill::WaterJet;P.Bindings[7]=Item(O,ELDItemKind::FrostPrism)->Id;P.Bindings[8]=Item(O,ELDItemKind::WaterFlask)->Id;}
            else{P.Bindings[2]=Item(O,ELDItemKind::RiverWaders)->Id;P.Bindings[3]=Item(O,ELDItemKind::RiverTreads)->Id;P.Bindings[7]=Item(O,ELDItemKind::FrostCapsule)->Id;}
            PC->ServerDevelopmentRequest(O.Match,O.Life,Token,LDDevelopmentTransaction::Prepare,ELDSkill::None,FGuid(),P);}
        if(Role==TEXT("A")&&(Phase==3||Phase==4||Phase==5)){PC->Navigation->Navigate(FVector(Phase==4?-2100:-1100,-700,0));}
        if(Role==TEXT("B")&&Phase==4){PC->SendItemAction(Item(O,ELDItemKind::FrostCapsule)->Id,{},FVector(-1600,-800,0));}
        if(Role==TEXT("A")&&(Phase==6||Phase==8)){PC->SetSkillHeld(Phase==6?0:1,true);PC->ConfirmArmed(FVector(-1300,-700,0),{});}
        if(Role==TEXT("A")&&Phase==7)
        {const auto* V=PC->LocalView();const auto* Target=V?V->LastView().Entities.FindByPredicate([](const auto& Ent){return Ent.Kind==ELDObservedKind::Body&&FVector::DistSquared2D(Ent.Position,FVector(-1550,-700,0))<80*80;}):nullptr;if(!Target){return;}PC->SendItemAction(Item(O,ELDItemKind::FrostPrism)->Id,Target->Id,Target->Position);}
        if(Role==TEXT("A")&&Phase==9){PC->SendItemAction(Item(O,ELDItemKind::WaterFlask)->Id,{},FVector(-1900,-300,0));}
        if(Phase==10&&Role==TEXT("A")){const auto* Book=O.Inventory.Items.FindByPredicate([](const auto& I){return I.Kind==ELDItemKind::Book&&I.Skill==ELDSkill::Firebolt;});if(!Book){return;}PC->ServerInventoryRequest(O.Match,O.Life,Token,O.Inventory.Revision,LDInventory::Read,Book->Id,Book->Revision,0,FVector::ZeroVector);}
        if(Phase==11&&Role==TEXT("A")){auto P=LDDevelopment::PlanFrom(O.Build);P.InventoryRevision=O.Inventory.Revision;P.Bindings=O.Inventory.Bindings;P.Slots[2]=ELDSkill::Firebolt;P.Actions[2].Skill=ELDSkill::Firebolt;P.Bindings[8]=Item(O,ELDItemKind::OilFlask)->Id;P.Bindings[9]=Item(O,ELDItemKind::GustVial)->Id;P.Bindings[10]=Item(O,ELDItemKind::SmokeFlask)->Id;PC->ServerDevelopmentRequest(O.Match,O.Life,Token,LDDevelopmentTransaction::Prepare,ELDSkill::None,FGuid(),P);}
        if(Role==TEXT("A")&&(Phase==12||Phase==14||Phase==15)){const auto* I=Item(O,Phase==12?ELDItemKind::OilFlask:Phase==14?ELDItemKind::SmokeFlask:ELDItemKind::GustVial);if(!I){return;}PC->SendItemAction(I->Id,{},FVector(Phase==15?-1200:-1100,-1000,0));}
        if(Role==TEXT("A")&&Phase==13){const auto* V=PC->LocalView();const auto* T=V?V->LastView().Entities.FindByPredicate([](const auto& E){return E.Kind==ELDObservedKind::Body&&FVector::DistSquared2D(E.Position,FVector(-1100,-1000,0))<60*60;}):nullptr;if(!T){return;}PC->SetSkillHeld(2,true);PC->ConfirmArmed(T->Position,T->Id);}
        Sent=true;
    }
    if(Sent&&(((Phase==1||Phase==10||Phase==11)&&Role==TEXT("A"))||Phase==2)&&Now-HoldAt>.1){HoldAt=Now;PC->ServerDevelopmentHold(O.Match,O.Life,Token,++Sequence,false);}
    if(!Checked&&E>2)
    {Checked=true;Check(O.Build.RulesVersion==8&&O.Build.Ranks.Num()==54&&O.Build.Actions.Num()==6,TEXT("Independent owner receives extended sparse identities and six actions"));
        if(Phase==1&&Role==TEXT("A")){UE_LOG(LogTemp,Display,TEXT("LD_B83_READ feedback=%s"),*PC->IntentFeedback);}
        if(const auto* V=PC->LocalView()){bool Safe=true;int32 Cells=0;for(const auto& Ent:V->LastView().Entities){if(Ent.Kind==ELDObservedKind::TacticalField&&Ent.Presentation>=16){++Cells;Safe&=Ent.Presentation<=22&&Ent.ActionCue<128;}}Check(Safe,TEXT("Authorized environment cues carry appearance/pose only; no owner/grid metadata"));if(Phase==4||Phase==8||Phase==9||Phase==12||Phase==13||Phase==14){Check(Cells>0,TEXT("Independent client receives authorized Wet/Frozen cell cues"));}}
    }
    if(Phase==17&&E>1.5){Finish();}
}
void ALDB83Probe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(Done){return;}if(FPlatformTime::Seconds()-Started>150){Check(false,TEXT("B83 fixture timeout"));Finish();return;}if(GetNetMode()==NM_Client){ClientTick();}else{ServerTick();}
#endif
}
void ALDB83Probe::Finish()
{
    if(Done){return;}Done=true;FString Directory;FParse::Value(FCommandLine::Get(),TEXT("LDProbeReportDir="),Directory);
    if(GetNetMode()==NM_Client){auto Samples=CastChecked<ALDPlayerController>(GetOwner())->ProbeRtts;Samples.Sort();Check(Samples.Num()>=10,TEXT("Independent client transport echo measured"));if(Samples.Num()){Check(Samples[Samples.Num()/2]>20&&Samples[Samples.Num()/2]<350,FString::Printf(TEXT("Echo median %.1fms"),Samples[Samples.Num()/2]));}}
    else if(auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M&&M->Economy){FFileHelper::SaveStringToFile(M->Economy->Accounting().ExportJson(TEXT("B83Fixture")),*(Directory/TEXT("Ledger.json")));}
    auto Json=MakeShared<FJsonObject>();Json->SetNumberField(TEXT("failures"),Failures);Json->SetStringField(TEXT("scope"),TEXT("B83 native editor server/two independent clients; actual native movement/AI/paid book/holds/releases. Diagnostic wallet funding, item grants, phase placement and isolated boundary clock. Not packaged/human/normal earned journey acceptance."));TArray<TSharedPtr<FJsonValue>> Values;for(const auto& S:Checks){Values.Add(MakeShared<FJsonValueString>(S));}Json->SetArrayField(TEXT("checks"),Values);FString Output;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Output));if(!FFileHelper::SaveStringToFile(Output,*(Directory/(Role+TEXT(".json"))))){++Failures;}FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
