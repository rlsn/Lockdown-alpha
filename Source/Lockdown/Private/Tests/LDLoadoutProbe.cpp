#include "Tests/LDLoadoutProbe.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Development/LDDevelopmentDirector.h"
#include "UI/LDControlSettings.h"
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

ALDLoadoutProbe::ALDLoadoutProbe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDLoadoutProbe::BeginPlay()
{
    Super::BeginPlay();Started=At=FPlatformTime::Seconds();FParse::Value(FCommandLine::Get(),TEXT("LDProbeRole="),Role);if(Role.IsEmpty()){Role=TEXT("Server");}
    if(GetNetMode()==NM_Client){CastChecked<ALDPlayerController>(GetOwner())->ServerProbeHello(Role==TEXT("A")?1:2);}
}
void ALDLoadoutProbe::Check(bool Value,const FString& Message){Checks.Add((Value?TEXT("PASS "):TEXT("FAIL "))+Message);if(!Value){++Failures;}UE_LOG(LogTemp,Display,TEXT("LD_B73_PROBE %s"),*Checks.Last());}
void ALDLoadoutProbe::Enter(int32 Next)
{
    Phase=Next;At=FPlatformTime::Seconds();Checked=false;Sequence=0;
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const double Now=GetWorld()->GetTimeSeconds();
    if(Next==12)
    {GetWorld()->TimeSeconds=ReturnEndpoint+.01;M->ReturnDueContenders(GetWorld()->GetTimeSeconds());}
    for(auto* PC:{A.Get(),B.Get()})
    {
        if(!PC||!PC->GetPawn()){continue;}auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());PC->Navigation->ResetLife();Body->SetActorLocation(PC==A.Get()?FVector(-1800,-900,92):FVector(-1600,-900,92));Body->GetCharacterMovement()->SetMovementMode(MOVE_None);Body->GetCharacterMovement()->StopMovementImmediately();auto* S=M->Combat->StateFor(Body);
        S->LastDamage=S->LastDealt=Now-20;S->Tactical.Statuses.Reset();S->CoreRecoveryUntil=0;
        if(Next!=12){S->ProtectionUntil=0;M->StateFor(PC)->ProtectedUntil=0;}
    }
    auto* Body=Cast<ALDCharacter>(A->GetPawn());auto* S=M->Combat->StateFor(Body);auto* O=M->StateFor(A.Get());
    if(Next==2){M->Combat->ApplyStatus(Body,LDTactical::EStatus::Silence,LDTactical::Milliseconds(Now)+8000,FGuid::NewGuid());}
    if(Next==3){M->AwardExperience(A.Get(),450);Check(O->Build.Level==5&&O->Build.AttributePoints==4,TEXT("Diagnostic XP uses native level/mastery tables; no gold grant"));}
    if(Next==4){const FVector P=LDRegion::Site(GetWorld(),LDObjective::Guild);Body->SetActorLocation(P+FVector(30,0,92));}
    if(Next==7)
    {
        while(LDInventory::Weight(O->Inventory)<=LDInventory::WeightCapacity(LDDevelopment::TacticalStart(0,M->TacticalCatalog->Checksum)))
        {auto* I=GetWorld()->SpawnActor<ALDItem>();I->Kind=ELDItemKind::Core;Check(M->Inventory->Adopt(A.Get(),I,TEXT("DiagnosticItemCreation")),TEXT("Journalled diagnostic cargo fits only equipped capacity"));}
    }
    if(Next==8){M->Combat->ApplyStatus(Body,LDTactical::EStatus::Mute,LDTactical::Milliseconds(Now)+8000,FGuid::NewGuid());}
    if(Next==9){const auto Entries=O->Inventory.Items;for(const auto& I:Entries){if(I.Kind==ELDItemKind::Core){auto* P=M->Inventory->Owned(A.Get(),I.Id);P->SetActorLocation(Body->GetActorLocation());M->Inventory->Remove(A.Get(),P,false);}}M->Combat->StateFor(Cast<ALDCharacter>(B->GetPawn()))->Tactical.ConcealedUntil=LDTactical::Milliseconds(Now)+10000;}
    if(Next==10){M->Combat->ApplyStatus(Body,LDTactical::EStatus::Break,LDTactical::Milliseconds(Now)+8000,FGuid::NewGuid());}
    M->SyncOwner(A.Get());A->ClientProbePhase(uint8(Next));B->ClientProbePhase(uint8(Next));
}
void ALDLoadoutProbe::ServerTick()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M||!M->Combat){return;}
    if(!Phase)
    {
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){auto* PC=Cast<ALDPlayerController>(It->Get());if(PC&&PC->GetPawn()){if(PC->ProbeRole==1){A=PC;}if(PC->ProbeRole==2){B=PC;}}}
        if(!A.IsValid()||!B.IsValid()){return;}FString Why;M->SetLobbyChoice(A.Get(),0,true,Why);M->SetLobbyChoice(B.Get(),0,true,Why);if(!M->StartDevelopmentMatch(A.Get(),Why)&&!M->StartDevelopmentMatch(B.Get(),Why)){Check(false,Why);Finish();return;}
        for(TActorIterator<ALDResident> It(GetWorld());It;++It){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();if(auto* AI=Cast<AAIController>(It->GetController())){AI->StopMovement();}}
        for(auto K:{ELDItemKind::IronHalberd,ELDItemKind::BastionPlate,ELDItemKind::MerchantHarness,ELDItemKind::ScholarsSigil,ELDItemKind::WardingEye,ELDItemKind::SimpleStaff,ELDItemKind::NullBell})
        {auto* I=GetWorld()->SpawnActor<ALDItem>();I->Kind=K;Check(M->Inventory->Adopt(A.Get(),I,TEXT("DiagnosticItemCreation")),TEXT("Labelled gear/consumable grant enters real physical ledger"));Items.Add(K,I->Identity);}
        for(auto Skill:{ELDSkill::BreakSigil,ELDSkill::Spark}){auto* I=GetWorld()->SpawnActor<ALDBook>();I->State=0;I->Skill=Skill;Check(M->Inventory->Adopt(A.Get(),I,TEXT("DiagnosticItemCreation")),TEXT("Diagnostic books are physical owned instances"));}
        M->StateFor(A.Get())->Gold+=25;M->RecordEconomyGold(TEXT("CourierReward"),25);Check(true,TEXT("Labelled diagnostic reward funds bank setup; not a normal courier journey"));
        const FGuid OwnerId=M->ContenderId(A.Get());const auto* W=M->Economy->Accounting().Account(ALDEconomyDirector::Wallet(OwnerId));const auto* Bank=M->Economy->Accounting().Account(ALDEconomyDirector::BankAccount(OwnerId));Check(M->Economy->CommitBank(A.Get(),20,true,W->Revision,Bank->Revision,Why),TEXT("Actual bank deposit retained across upcoming death"));BankBefore=M->StateFor(A.Get())->BankGold;
        M->Vision->SetPublicSensor(false);Enter(1);return;
    }
    const double E=FPlatformTime::Seconds()-At;
    if(Phase==12&&Checked){if(E>3){Finish();}return;}
    if(!A.IsValid()||!B.IsValid()){Check(false,TEXT("Unexpected disconnect"));Finish();return;}
    auto* O=M->StateFor(A.Get());auto* Body=Cast<ALDCharacter>(A->GetPawn());auto* S=M->Combat->StateFor(Body);
    if(Phase==11&&!Checked&&E>1.5)
    {
        Checked=true;const double Now=GetWorld()->GetTimeSeconds();SavedCooldown=O->Build.Cooldowns[32];SavedFamily=O->NullBellReadyAt;O->MajorProtectionReadyAt=LDTactical::Milliseconds(Now)+8000;SavedMajor=O->MajorProtectionReadyAt;OldLife=O->Life;
        const int64 Gold=O->Gold,Loss=LDDevelopment::DeathLoss(Gold);S->Hp=0;M->EndContenderLife(A.Get(),Now);Body->Destroy();ReturnEndpoint=O->ReturnAt;
        Check(O->Gold==Gold-Loss&&O->BankGold==BankBefore,TEXT("Death burns exact wallet tax and preserves bank"));Check(FMath::IsNearlyEqual(ReturnEndpoint-Now,LDDevelopment::TacticalDeathDelay(5,Now-M->StartedAt()),.001),TEXT("Respawn endpoint uses level and elapsed formula once"));M->EndContenderLife(A.Get(),Now+1);Check(O->ReturnAt==ReturnEndpoint,TEXT("Later pressure cannot extend displayed return endpoint"));
        Check(O->Inventory.Items.Num()==3&&O->Inventory.Bindings[0]==Items[ELDItemKind::IronHalberd]&&O->Inventory.Bindings[1]==Items[ELDItemKind::IronHalberd]&&O->Inventory.Bindings[2]==Items[ELDItemKind::MerchantHarness],TEXT("Equipped ordinary gear retained once; spare gear/books/cargo/consumables drop"));
        Check(O->Build.Ranks[32]==2&&O->Build.Cooldowns[32]==SavedCooldown&&O->NullBellReadyAt==SavedFamily,TEXT("Knowledge/mastery and absolute skill/family endpoints survive death"));
        int32 Drops=0;for(TActorIterator<ALDItem> It(GetWorld());It;++It){if(It->State==0&&Items.FindKey(It->Identity)){++Drops;}}Check(Drops>=3,TEXT("Unbound physical gear remains in world with original identities"));
    }
    if(Phase==12&&!Checked&&E>1.1)
    {
        Checked=true;Check(O->bActive&&O->Life==OldLife+1&&O->Hp==O->MaxHp&&O->Mp==O->MaxMp,TEXT("Native return refills only HP/MP on a fresh life"));Check(O->Build.Cooldowns[32]==SavedCooldown&&O->NullBellReadyAt==SavedFamily&&O->MajorProtectionReadyAt==SavedMajor,TEXT("Return preserves ability and both family endpoints"));Check(O->Inventory.Items.Num()==3&&O->BankGold==BankBefore&&O->Build.Attributes[3]==11,TEXT("Return grants no new supplies; bound property and natural investment retained"));
        Check(FMath::IsNearlyEqual(O->ProtectedUntil-(ReturnEndpoint+.01),3.,.002),TEXT("Respawn protection is exactly three seconds"));FString Why;Check(!M->Combat->RequestEquipped(Body,O->Build.Revision,4,99000,FVector::ForwardVector,Why,B->GetPawn()),TEXT("Protection blocks direct equipped cast"));AuthorityEdges();return;
    }
    const double Duration=Phase==3?4.7:Phase==4?3.6:Phase==11?2.3:3.5;if(E<Duration){return;}
    if(Phase==1){Check(O->Inventory.Bindings[0]==Items[ELDItemKind::IronHalberd]&&O->Inventory.Bindings[1]==Items[ELDItemKind::IronHalberd],TEXT("Remote two-hand hold commits same instance to both reserved bindings"));Check(O->Build.Attributes[0]==11&&LDDevelopment::Effective(O->Build)[0]==12,TEXT("Actual equipped STR changes effective value only"));}
    if(Phase==2){Check(O->Inventory.Bindings[2]==Items[ELDItemKind::BastionPlate]&&O->MaxHp==700&&O->Hp<660,TEXT("Silence permits equipment commit; increased max HP grants no heal"));}
    if(Phase==3){Check(O->Build.Ranks[32]==1&&O->Build.Attributes[3]==8&&!LDGear::NaturalGate(O->Build,ELDSkill::BreakSigil),TEXT("Cross-class book learns while naturally ineligible without equipping"));}
    if(Phase==4){Check(O->Build.Attributes[3]==11&&O->Build.Attributes[4]==9&&O->Build.Ranks[32]==2&&O->Build.Slots[4]==ELDSkill::BreakSigil&&O->Build.SkillPoints==3,TEXT("Atomic natural investment and cross-class mastery/equip commit spend correct points"));}
    if(Phase==5){Check(M->Combat->StateFor(Cast<ALDCharacter>(B->GetPawn()))->Tactical.Has(LDTactical::EStatus::Break,LDTactical::Milliseconds(GetWorld()->GetTimeSeconds())),TEXT("Fifth-slot gated skill releases through real tactical RPC"));}
    if(Phase==6){Check(O->Inventory.Bindings[2]==Items[ELDItemKind::MerchantHarness]&&LDInventory::WeightCapacity(O->Build)==72,TEXT("Remote replacement derives equipped carrying capacity"));}
    if(Phase==7){Check(O->Inventory.Bindings[2]==Items[ELDItemKind::MerchantHarness]&&LDInventory::Weight(O->Inventory)>62,TEXT("Overweight capacity removal rejects without changing bindings"));}
    if(Phase==8){Check(!O->Inventory.Quick[0].IsValid()&&!O->DevelopmentKind,TEXT("Mute and legacy direct bind both fail without immediate quick activation"));}
    if(Phase==9){Check(O->Inventory.Bindings[4]==Items[ELDItemKind::WardingEye]&&M->Vision->CanObserve(A.Get(),B->GetPawn()),TEXT("Bound Warding Eye supplies actual geometric concealment detection"));}
    if(Phase==10){Check(!M->Vision->CanObserve(A.Get(),B->GetPawn())&&LDDevelopment::Effective(O->Build)[4]==10,TEXT("Break suppresses only tagged detector; ordinary gear WIS retained"));}
    Enter(Phase+1);
}
void ALDLoadoutProbe::ClientTick()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwner());const double Now=FPlatformTime::Seconds();if(Now-LastPing>.15){LastPing=Now;PC->ServerProbePing(Now);}
    if(PC->ProbePhase!=Phase){Phase=PC->ProbePhase;At=Now;Sent=Checked=false;Sequence=0;HoldSent=0;}if(!Phase){return;}const double E=Now-At;const auto& O=PC->OwnerState;const bool IsA=Role==TEXT("A");const uint32 Token=10000+Phase*100;
    auto Item=[&](ELDItemKind Kind){const auto* V=O.Inventory.Items.FindByPredicate([&](const auto& I){return I.Kind==Kind;});return V;};
    if(IsA&&!Sent&&E>.4)
    {
        if(Phase==1||Phase==2||Phase==6||Phase==7||Phase==8||Phase==9||Phase==10)
        {
            const auto* I=Item(Phase==1?ELDItemKind::IronHalberd:Phase==2?ELDItemKind::BastionPlate:Phase==6||Phase==7?ELDItemKind::MerchantHarness:Phase==8||Phase==10?ELDItemKind::NullBell:ELDItemKind::WardingEye);if(!I){return;}const auto* C=ULDEquipmentCatalog::Load();FLDLoadoutRequest R;R.Match=O.Match;R.Life=O.Life;R.Token=Token;R.InventoryRevision=O.Inventory.Revision;R.BuildRevision=O.Build.Revision;R.ItemRevision=I->Revision;R.Item=I->Id;R.EquipmentDigest=C->Checksum;R.Command=Phase==7?LDInventory::Unequip:Phase==8||Phase==10?LDInventory::Bind:LDInventory::Equip;R.Binding=Phase==1||Phase==7||Phase==8||Phase==10?0:Phase==9?4:2;PC->ServerLoadoutRequest(R);if(Phase==1){PC->ServerLoadoutRequest(R);}if(Phase==8){PC->ServerInventoryRequest(O.Match,O.Life,Token+1,O.Inventory.Revision,LDInventory::Bind,I->Id,I->Revision,0,FVector::ZeroVector);}
        }
        if(Phase==3){const auto* I=O.Inventory.Items.FindByPredicate([](const auto& V){return V.Kind==ELDItemKind::Book&&V.Skill==ELDSkill::BreakSigil;});if(!I){return;}PC->ServerInventoryRequest(O.Match,O.Life,Token,O.Inventory.Revision,LDInventory::Read,I->Id,I->Revision,0,FVector::ZeroVector);}
        if(Phase==4){auto Plan=LDDevelopment::PlanFrom(O.Build);Plan.Attributes[3]=11;Plan.Attributes[4]=9;Plan.Ranks[32]=2;Plan.Slots[4]=ELDSkill::BreakSigil;PC->ServerDevelopmentRequest(O.Match,O.Life,Token,LDDevelopmentTransaction::Prepare,ELDSkill::None,FGuid(),Plan);}
        if(Phase==5){FGuid Target;if(auto* V=PC->LocalView()){for(const auto& I:V->LastView().Entities){if(I.Kind==ELDObservedKind::Body&&FVector::DistSquared2D(I.Position,FVector(-1600,-900,92))<100){Target=I.Id;}}}if(!Target.IsValid()){return;}PC->SendTacticalAction(Token,4,FVector::ForwardVector,Target);}
        if(Phase==11){const auto* I=Item(ELDItemKind::NullBell);if(I){PC->ServerInventoryRequest(O.Match,O.Life,Token,O.Inventory.Revision,LDInventory::Use,I->Id,I->Revision,0,FVector::ZeroVector);}}
        Sent=true;
    }
    if(IsA&&Sent&&(Phase==1||Phase==2||Phase==3||Phase==4||Phase==6||Phase==7||Phase==8||Phase==9||Phase==10)&&Now-HoldSent>.1){HoldSent=Now;PC->ServerDevelopmentHold(O.Match,O.Life,Token,++Sequence,false);}
    if(!Checked&&E>1.4)
    {
        Checked=true;Check(O.Inventory.RulesVersion==7&&O.Inventory.Quick.Num()==3&&O.Inventory.Bindings.Num()==6,TEXT("Independent owner receives three quick references and six bindings"));Check(PC->Controls&&PC->Controls->Profile==7&&PC->BindingKey(TEXT("Skill5"))==EKeys::D&&!PC->Controls->Bindings.Contains(TEXT("Item4")),TEXT("Live native mappings use profile7 fifth skill and remove quick4"));
        if(IsA&&Phase==4){Check(O.DevelopmentKind==LDDevelopmentTransaction::Prepare||O.Build.Attributes[3]==11,TEXT("Remote preparation uses the real held station channel"));}
    }
    if(Phase==12&&E>2.1){Finish();}
}
void ALDLoadoutProbe::AuthorityEdges()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M->StateFor(A.Get());auto* Body=Cast<ALDCharacter>(A->GetPawn());auto* S=M->Combat->StateFor(Body);const double Now=GetWorld()->GetTimeSeconds();FString Why;
    S->LastDamage=S->LastDealt=Now-20;auto* P=GetWorld()->SpawnActor<ALDArcProjectile>();P->Source=Body;Check(!M->Combat->QuietForPreparation(Body),TEXT("Owned released projectile blocks quiet preparation before any contact"));P->Destroy();Check(M->Combat->QuietForPreparation(Body),TEXT("No pending projectile restores quiet eligibility"));
    const auto Gold=O->Gold;Check(M->Economy->Audit(Why)&&M->Economy->AuditFailures==0,TEXT("Native economy reconciles bound retained and dropped original identities"));Check(Gold>=0&&O->BankGold==BankBefore,TEXT("No hidden reward or bank loss from equipment/death"));
    auto Draft=LDDevelopment::PlanFrom(O->Build);Draft.Slots[4]=ELDSkill::TemporalLock;Check(!LDDevelopment::Prepare(O->Build,Draft,O->Build,Why),TEXT("Unimplemented capstone cannot be activated through preparation"));
}
void ALDLoadoutProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(FPlatformTime::Seconds()-Started>80){Check(false,TEXT("B73 network fixture timed out"));Finish();return;}
    if(GetNetMode()==NM_Client){ClientTick();}else{ServerTick();}
#endif
}
void ALDLoadoutProbe::Finish()
{
    if(GetNetMode()==NM_Client)
    {auto* PC=CastChecked<ALDPlayerController>(GetOwner());auto Samples=PC->ProbeRtts;Samples.Sort();Check(Samples.Num()>=15,TEXT("Independent client transport echo measured"));if(!Samples.IsEmpty()){Check(Samples[Samples.Num()/2]>=20&&Samples[Samples.Num()/2]<350,FString::Printf(TEXT("Echo median %.1fms"),Samples[Samples.Num()/2]));}}
    TSharedRef<FJsonObject> Json=MakeShared<FJsonObject>();Json->SetNumberField(TEXT("failures"),Failures);Json->SetStringField(TEXT("scope"),TEXT("Controlled native B73 fixture; legal RPCs, diagnostic gear/books/XP/quiet/placement/return clock; diagnostic wallet funding; normal gear sources remain B74; not a normal earned route"));TArray<TSharedPtr<FJsonValue>> Lines;for(const auto& S:Checks){Lines.Add(MakeShared<FJsonValueString>(S));}Json->SetArrayField(TEXT("checks"),Lines);
    FString Directory,Output;FParse::Value(FCommandLine::Get(),TEXT("LDProbeReportDir="),Directory);FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Output));
    if(GetNetMode()!=NM_Client){auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(M&&M->Economy){if(!FFileHelper::SaveStringToFile(M->Economy->Accounting().ExportJson(TEXT("B73Fixture")),*(Directory/TEXT("Ledger.json")))){++Failures;}}}
    if(!FFileHelper::SaveStringToFile(Output,*(Directory/(Role+TEXT(".json"))))){++Failures;}SetActorTickEnabled(false);FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
