#include "Tests/LDB81Probe.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDTacticalCatalog.h"
#include "Development/LDV08Loadout.h"
#include "Development/LDDevelopmentDirector.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDAcquisitionDirector.h"
#include "Inventory/LDAcquisitionCatalog.h"
#include "Economy/LDEconomyDirector.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDRegionDefinition.h"
#include "Objectives/LDObjectiveRules.h"
#include "UI/LDControlSettings.h"
#include "UI/LDDevelopmentPanel.h"
#include "Vision/LDWorldViewRelay.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

ALDB81Probe::ALDB81Probe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDB81Probe::BeginPlay()
{Super::BeginPlay();Started=At=FPlatformTime::Seconds();FParse::Value(FCommandLine::Get(),TEXT("LDProbeRole="),Role);if(Role.IsEmpty()){Role=TEXT("Server");}if(GetNetMode()==NM_Client){CastChecked<ALDPlayerController>(GetOwner())->ServerProbeHello(Role==TEXT("A")?1:2);}}
void ALDB81Probe::Check(bool Value,const FString& Message)
{Checks.Add((Value?TEXT("PASS "):TEXT("FAIL "))+Message);if(!Value){++Failures;}UE_LOG(LogTemp,Display,TEXT("LD_B81 %s"),*Checks.Last());}
void ALDB81Probe::Enter(int32 Next)
{
    Phase=Next;At=FPlatformTime::Seconds();auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const double Now=GetWorld()->GetTimeSeconds();
    auto* O=M->StateFor(A.Get());auto* Body=CastChecked<ALDCharacter>(A->GetPawn());auto* S=M->Combat->StateFor(Body);S->LastDamage=S->LastDealt=Now-20;S->Tactical.Statuses.Reset();S->CoreRecoveryUntil=0;S->ProtectionUntil=0;O->ProtectedUntil=0;MP=S->Tactical.MP;
    // Controlled fixture isolates permissions from the independent protection-family lock.
    if(Next==2){M->Combat->ApplyStatus(Body,LDTactical::EStatus::Silence,LDTactical::Milliseconds(Now)+10000,FGuid::NewGuid());}
    if(Next==3){M->Combat->ApplyStatus(Body,LDTactical::EStatus::Mute,LDTactical::Milliseconds(Now)+10000,FGuid::NewGuid());}
    if(Next==7)
    {OldLife=O->Life;S->Hp=0;M->EndContenderLife(A.Get(),Now);Check(O->Inventory.Find(Mail)&&O->Inventory.Find(Helm),TEXT("Death retains ordinary bound body gear"));Check(!O->Inventory.Find(Charm)&&!O->Inventory.Find(Spare),TEXT("Bound consumable and unbound spare drop with original identities"));const double Return=O->ReturnAt;Body->Destroy();GetWorld()->TimeSeconds=Return+.01;M->ReturnDueContenders(Return+.01);Check(O->Life==OldLife+1&&O->Build.Preset==3,TEXT("Native return keeps immutable class trait and increments life"));Check(O->Inventory.Find(Mail)->ReadyAt==MailReady,TEXT("Instance cooldown endpoint persists through death/return"));}
    M->SyncOwner(A.Get());M->SyncOwner(B.Get());A->ClientProbePhase(Next);B->ClientProbePhase(Next);
}
void ALDB81Probe::ServerTick()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M||!M->Combat){return;}
    if(!Phase)
    {
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){auto* PC=Cast<ALDPlayerController>(It->Get());if(PC&&PC->GetPawn()){if(PC->ProbeRole==1){A=PC;}if(PC->ProbeRole==2){B=PC;}}}if(!A.IsValid()||!B.IsValid()){return;}
        FString Why;Check(M->IsV08Profile(),TEXT("Authority selected isolated v08 development profile"));Check(M->SetLobbyChoice(A.Get(),5,false,Why),TEXT("B82 Arcanist kit is available while B81 loadout contracts persist"));M->SetLobbyChoice(A.Get(),3,true,Why);M->SetLobbyChoice(B.Get(),0,true,Why);if(!M->StartDevelopmentMatch(A.Get(),Why)&&!M->StartDevelopmentMatch(B.Get(),Why)){Check(false,Why);Finish();return;}
        for(TActorIterator<ALDResident> It(GetWorld());It;++It){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();if(auto* AI=Cast<AAIController>(It->GetController())){AI->StopMovement();}}
        const FVector Guild=LDRegion::Site(GetWorld(),LDObjective::Guild);for(auto* PC:{A.Get(),B.Get()}){PC->Navigation->ResetLife();auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());Body->SetActorLocation(Guild+FVector(PC==A.Get()?30:170,0,92));Body->GetCharacterMovement()->SetMovementMode(MOVE_None);Body->GetCharacterMovement()->StopMovementImmediately();}
        Check(M->StateFor(A.Get())->Inventory.Items.IsEmpty(),TEXT("New profile starts without free gear or consumables"));
        auto Grant=[&](ELDItemKind K){auto* Item=GetWorld()->SpawnActor<ALDItem>();Item->Kind=K;Check(M->Inventory->Adopt(A.Get(),Item,TEXT("DiagnosticItemCreation")),TEXT("Labelled fixture identity enters physical source/sink ledger"));return Item->Identity;};
        Mail=Grant(ELDItemKind::ReflectiveMail);Charm=Grant(ELDItemKind::PurityCharm);Salve=Grant(ELDItemKind::HealingSalve);Spare=Grant(ELDItemKind::HealingSalve);Helm=Grant(ELDItemKind::IronHelm);
        Check(M->StateFor(A.Get())->Inventory.Quick.IsEmpty(),TEXT("No independent quick bar or starting equipment grant"));Enter(1);return;
    }
    if(FPlatformTime::Seconds()-At<3.4){return;}if(Phase==8){Finish();return;}if(!A.IsValid()||!B.IsValid()){Check(false,TEXT("Unexpected disconnect during fixture"));Finish();return;}auto* O=M->StateFor(A.Get());auto* S=M->Combat->StateFor(Cast<ALDCharacter>(A->GetPawn()));
    if(Phase==1){Check(O->Build.Actions[5].Source==ELDActionSource::Gear&&O->Build.Actions[5].Instance==Mail,TEXT("Remote atomic draft allocates body source to shared action6"));Check(O->Inventory.Bindings[7]==Charm&&O->Inventory.Bindings[8]==Salve&&O->Inventory.Find(Salve)->BindingMask==256,TEXT("Remote accessory2 survives widened mask and physical binding projection"));
        const int32 Price=ULDEquipmentCatalog::Load(8)->Find(ELDItemKind::IronHelm)->Price;const int32 Count=O->Inventory.Items.Num();O->Gold+=Price;M->RecordEconomyGold(TEXT("CourierReward"),Price);const auto Gold=O->Gold;M->Acquisition->Purchase(A.Get(),O->Match,O->Life,900001,2,ELDItemKind::IronHelm,ELDSkill::None,O->Inventory.Revision,ULDAcquisitionCatalog::Load()->Checksum);Check(O->Gold==Gold-Price&&O->Inventory.Items.Num()==Count+1,TEXT("Authored new helmet price burns exactly once through native paid supply; diagnostic wallet funding only"));}
    if(Phase==2){MailReady=O->Inventory.Find(Mail)->ReadyAt;Check(MailReady>LDTactical::Milliseconds(GetWorld()->GetTimeSeconds())&&S->Tactical.ReflectUntil>0,TEXT("Silenced client activates body reflection through shared action6"));Check(S->Tactical.MP>=MP,TEXT("Body item action spends no learned MP"));const auto Saved=O->Build;O->Build.Ranks[uint8(ELDSkill::Step)]=1;O->Build.Slots[4]=ELDSkill::Step;O->Build.Actions[4].Skill=ELDSkill::Step;FString Why;Check(!M->Combat->RequestEquipped(Cast<ALDCharacter>(A->GetPawn()),O->Build.Revision,4,800001,FVector::ForwardVector,Why)&&Why.Contains(TEXT("Silenced")),TEXT("Legacy learned Step is Silence-blocked before movement validation"));O->Build=Saved;}
    if(Phase==3){Check(S->Tactical.Barrier>0&&S->Tactical.MP<MP,TEXT("Muted actor can cast learned Brace Wall normally"));Check(O->Inventory.Find(Salve)&&O->Inventory.Find(Salve)->ReadyAt==0&&O->Inventory.Find(Mail)->ReadyAt==MailReady,TEXT("Mute rejects accessory/body requests without consuming identity or changing cooldown"));}
    if(Phase==4){Check(!O->Inventory.Find(Salve)&&!O->Inventory.Bindings[8].IsValid()&&O->Inventory.Find(Spare),TEXT("Consume exactly one bound accessory, clear binding, no bag auto-refill"));}
    if(Phase==5){Check(!O->Inventory.Bindings[1].IsValid()&&O->Build.Actions[5].Source==ELDActionSource::Learned,TEXT("Quiet unbind invalidates dependent shared action atomically"));Check(O->Inventory.Find(Mail)->ReadyAt==MailReady,TEXT("Unbind and stale-source request preserve instance cooldown"));}
    if(Phase==6){Check(O->Inventory.Bindings[1]==Mail&&O->Build.Actions[5].Instance==Mail,TEXT("Rebind commits source and slot together"));Check(O->Inventory.Find(Mail)->ReadyAt==MailReady,TEXT("Rebinding never resets absolute cooldown"));}
    if(Phase==7){FString Why;Check(O->Hp==O->MaxHp&&O->Mp==O->MaxMp,TEXT("Only native return explicitly refills HP/MP"));Check(M->Economy->Audit(Why)&&M->Economy->AuditFailures==0,TEXT("Bound/consumed/dropped original identities reconcile in native ledger"));Enter(8);return;}
    if(Phase==8){Finish();return;}Enter(Phase+1);
}
void ALDB81Probe::ClientTick()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwner());const double Now=FPlatformTime::Seconds();if(Now-PingAt>.15){PingAt=Now;PC->ServerProbePing(Now);}
    if(PC->ProbePhase!=Phase){Phase=PC->ProbePhase;At=Now;Sent=Checked=false;Sequence=0;}if(!Phase){return;}const auto& O=PC->OwnerState;const uint32 Token=100000+Phase*100;const double E=Now-At;
    auto Item=[&](ELDItemKind K){return O.Inventory.Items.FindByPredicate([&](const auto& I){return I.Kind==K;});};
    if(Role==TEXT("A")&&!Sent&&E>.45)
    {
        const auto* I=Item(ELDItemKind::ReflectiveMail);
        if(Phase==1||Phase==6)
        {if(!I){return;}auto P=LDDevelopment::PlanFrom(O.Build);P.InventoryRevision=O.Inventory.Revision;P.Bindings=O.Inventory.Bindings;P.Bindings[1]=I->Id;P.Actions[5].Source=ELDActionSource::Gear;P.Actions[5].Instance=I->Id;P.Actions[5].Definition=TEXT("reflective_mail");P.Slots[5]=ELDSkill::None;if(Phase==1){const auto* H=Item(ELDItemKind::IronHelm);const auto* C=Item(ELDItemKind::PurityCharm);const auto* V=Item(ELDItemKind::HealingSalve);if(!H||!C||!V){return;}P.Bindings[0]=H->Id;P.Bindings[7]=C->Id;P.Bindings[8]=V->Id;}PC->ServerDevelopmentRequest(O.Match,O.Life,Token,LDDevelopmentTransaction::Prepare,ELDSkill::None,FGuid(),P);PC->ServerDevelopmentRequest(O.Match,O.Life,Token,LDDevelopmentTransaction::Prepare,ELDSkill::None,FGuid(),P);}
        if(Phase==2&&I){PC->SendItemAction(I->Id,FGuid(),FVector::ZeroVector);}
        if(Phase==3){const auto* V=Item(ELDItemKind::HealingSalve);if(V){PC->SendItemAction(V->Id,FGuid(),FVector::ZeroVector);}if(I){PC->SendItemAction(I->Id,FGuid(),FVector::ZeroVector);}PC->SetSkillHeld(1,true);}
        if(Phase==4){const auto* V=O.Inventory.Find(O.Inventory.Bindings[8]);if(V){PC->SendItemAction(V->Id,FGuid(),FVector::ZeroVector);}}
        if(Phase==5&&I){FLDLoadoutRequest R;R.Version=2;R.Match=O.Match;R.Life=O.Life;R.Token=Token;R.BuildRevision=O.Build.Revision;R.InventoryRevision=O.Inventory.Revision;R.ItemRevision=I->Revision;R.Item=I->Id;R.Command=LDInventory::Unequip;R.EquipmentDigest=ULDEquipmentCatalog::Load(8)->Checksum;PC->ServerLoadoutRequest(R);}
        if(Phase==7){PC->ServerInventoryRequest(O.Match,O.Life-1,Token,O.Inventory.Revision,LDInventory::Use,FGuid(),1,0,FVector::ZeroVector);}
        Sent=true;
    }
    if(Role==TEXT("A")&&Sent&&(Phase==1||Phase==5||Phase==6)&&Now-HoldAt>.1){HoldAt=Now;PC->ServerDevelopmentHold(O.Match,O.Life,Token,++Sequence,false);}
    if(!Checked&&E>2.8){Checked=true;Check(O.Build.RulesVersion==8&&O.Build.Actions.Num()==6&&O.Inventory.Bindings.Num()==11&&O.Inventory.Quick.IsEmpty(),TEXT("Independent owner receives typed six/eleven state without quick references"));Check(PC->Controls&&PC->Controls->Profile==8&&PC->Controls->Bindings.Contains(TEXT("Skill6"))&&PC->Controls->Bindings.Contains(TEXT("Item4")),TEXT("Native input exposes sixth action and fourth physical accessory"));if(Phase==5&&Role==TEXT("A")){const auto* I=Item(ELDItemKind::ReflectiveMail);if(I){FLDTacticalRequest R;R.Schema=8;R.WireVersion=4;R.SourceKind=1;R.SourceDefinition=TEXT("reflective_mail");R.Item=I->Id;R.ItemRevision=I->Revision;R.ActionKind=0;R.Slot=5;R.Match=O.Match;R.Life=O.Life;R.Token=Token+10;R.BuildRevision=O.Build.Revision;R.InventoryRevision=O.Inventory.Revision;R.CatalogDigest=O.Build.CatalogDigest;PC->ServerTacticalAction(R);PC->ServerInventoryRequest(O.Match,O.Life,Token+20,O.Inventory.Revision,LDInventory::Use,I->Id,I->Revision,0,FVector::ZeroVector);}}}
    if(Role==TEXT("A")&&Phase==1&&E>2.8&&PC->DevelopmentUI==nullptr){PC->ToggleDevelopmentPanel();Check(PC->DevelopmentUI&&PC->DevelopmentUI->IsOpen()&&PC->DevelopmentUI->Preview().Actions.Num()==6&&PC->DevelopmentUI->Preview().Bindings.Num()==11,TEXT("Native preparation panel builds full typed owner draft with eleven-binding preview"));}
    if(Role==TEXT("A")&&Phase==1&&E>3.1&&PC->DevelopmentUI&&PC->DevelopmentUI->IsOpen()){PC->ToggleDevelopmentPanel();}
    if(Phase==8&&E>2){Finish();}
}
void ALDB81Probe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(Done){return;}if(FPlatformTime::Seconds()-Started>85){Check(false,TEXT("B81 fixture timeout"));Finish();return;}if(GetNetMode()==NM_Client){ClientTick();}else{ServerTick();}
#endif
}
void ALDB81Probe::Finish()
{
    if(Done){return;}Done=true;FString Directory;FParse::Value(FCommandLine::Get(),TEXT("LDProbeReportDir="),Directory);
    if(GetNetMode()==NM_Client){auto* PC=CastChecked<ALDPlayerController>(GetOwner());auto Samples=PC->ProbeRtts;Samples.Sort();Check(Samples.Num()>=10,TEXT("Independent client transport echo measured"));if(Samples.Num()){Check(Samples[Samples.Num()/2]>20&&Samples[Samples.Num()/2]<350,FString::Printf(TEXT("Echo median %.1fms"),Samples[Samples.Num()/2]));}}
    else{auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(M&&M->Economy){FFileHelper::SaveStringToFile(M->Economy->Accounting().ExportJson(TEXT("B81Fixture")),*(Directory/TEXT("Ledger.json")));}}
    auto Json=MakeShared<FJsonObject>();Json->SetNumberField(TEXT("failures"),Failures);Json->SetStringField(TEXT("scope"),TEXT("B81 controlled native two-client fixture, real RPC/holds/transactions; diagnostic item grants, wallet funding, quiet/placement/control and return clock; not normal earned acquisition or human review"));TArray<TSharedPtr<FJsonValue>> Values;for(const auto& S:Checks){Values.Add(MakeShared<FJsonValueString>(S));}Json->SetArrayField(TEXT("checks"),Values);FString Output;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Output));if(!FFileHelper::SaveStringToFile(Output,*(Directory/(Role+TEXT(".json"))))){++Failures;}FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
