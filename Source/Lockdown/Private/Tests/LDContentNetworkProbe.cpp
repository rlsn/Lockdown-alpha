#include "Tests/LDContentNetworkProbe.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDTacticalCatalog.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Navigation/LDNavigationComponent.h"
#include "Inventory/LDInventoryDirector.h"
#include "Economy/LDEconomyDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "AIController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
ALDContentNetworkProbe::ALDContentNetworkProbe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDContentNetworkProbe::BeginPlay(){Super::BeginPlay();Started=At=FPlatformTime::Seconds();FParse::Value(FCommandLine::Get(),TEXT("LDProbeRole="),Role);if(Role.IsEmpty()){Role=TEXT("Server");}if(GetNetMode()==NM_Client){CastChecked<ALDPlayerController>(GetOwner())->ServerProbeHello(Role==TEXT("A")?1:2);}}
void ALDContentNetworkProbe::Check(bool Value,const FString& Message){Checks.Add((Value?TEXT("PASS "):TEXT("FAIL "))+Message);Failures+=!Value;UE_LOG(LogTemp,Display,TEXT("LD_B75_NET %s"),*Checks.Last());}
void ALDContentNetworkProbe::Enter(int32 Next)
{
    Phase=Next;At=FPlatformTime::Seconds();Checked=false;auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* C=M->Combat.Get();
    for(auto* PC:{A.Get(),B.Get()})
    {
        auto* O=M->StateFor(PC);auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());C->CancelTactical(Body,TEXT("B75 phase setup"));auto* S=C->StateFor(Body);S->Action=0;S->CoreRecoveryUntil=0;S->Tactical=LDTactical::FState();S->ProtectionUntil=O->ProtectedUntil=0;S->LastDamage=S->LastDealt=-1000;PC->Navigation->ResetLife();Body->GetCharacterMovement()->SetMovementMode(MOVE_None);Body->GetCharacterMovement()->StopMovementImmediately();
        Body->SetActorLocation(Next==3&&PC==A.Get()?FVector(-1750,-1050,92):PC==A.Get()?FVector(-1800,-900,92):FVector(-1600,-900,92));
        O->Build=LDDevelopment::TacticalStart(0,M->TacticalCatalog->Checksum);O->Build.Revision=100+Next;O->Build.Attributes.Init(16,6);O->Build.Ranks.Init(1,38);O->Build.Ranks[0]=0;O->Build.Slots.Init(ELDSkill::None,5);O->Build.Slots[0]=Next==1?ELDSkill::GrandExchange:Next==2?ELDSkill::GateWeave:Next==6?ELDSkill::TemporalLock:ELDSkill::Heave;O->Build.Cooldowns.Init(0,38);O->ItemFamilies.Entries.Reset();O->NullBellReadyAt=0;O->MajorProtectionReadyAt=0;O->GateReadyAt=0;S->Hp=O->Hp=O->MaxHp=LDDevelopment::MaxHP(O->Build);S->Tactical.MP=Next==7?50:LDTactical::MaxMP(O->Build);
        const auto Old=O->Inventory.Items;for(const auto& V:Old){if(auto* Item=M->Inventory->Owned(PC,V.Id)){M->Inventory->Remove(PC,Item,true,TEXT("B75NetworkCleanup"));}}
        if(PC==A.Get()&&(Next==4||Next==5||Next==7)){auto* Item=GetWorld()->SpawnActor<ALDItem>();Item->Kind=Next==7?ELDItemKind::ManaDraught:ELDItemKind::Counterglyph;Check(M->Inventory->Adopt(PC,Item,TEXT("DiagnosticItemCreation")),TEXT("Labelled native network consumable creation"));FString Why;Check(LDInventory::BindQuick(O->Inventory,Item->Identity,0,Why),TEXT("Actual quick-reference binding"));M->Inventory->Changed(PC);}
        if(PC==B.Get()&&Next==4){S->Tactical.BlockUntil=LDTactical::Milliseconds(GetWorld()->GetTimeSeconds())+10000;}
        M->Combat->Sync(Body,*S);M->SyncOwner(PC);
    }
    if(Next!=3){TArray<ALDTacticalField*> Old;for(TActorIterator<ALDTacticalField> It(GetWorld());It;++It){if(It->Kind!=ELDTacticalField::Scramble){Old.Add(*It);}}for(auto* F:Old){C->DestroyField(F);}}
    M->Vision->Tick(0);A->ClientProbePhase(Next);B->ClientProbePhase(Next);
}
void ALDContentNetworkProbe::ServerTick()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!Phase){for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){auto* PC=Cast<ALDPlayerController>(It->Get());if(PC&&PC->GetPawn()){if(PC->ProbeRole==1){A=PC;}if(PC->ProbeRole==2){B=PC;}}}if(!A.IsValid()||!B.IsValid()){return;}FString Why;M->SetLobbyChoice(A.Get(),0,true,Why);M->SetLobbyChoice(B.Get(),0,true,Why);if(!M->StartDevelopmentMatch(A.Get(),Why)&&!M->StartDevelopmentMatch(B.Get(),Why)){Check(false,TEXT("Native match start failed: ")+Why);Finish();return;}Check(true,TEXT("Native two-client tactical match starts regardless of joining host role"));for(TActorIterator<ALDResident> It(GetWorld());It;++It){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();if(auto* AI=Cast<AAIController>(It->GetController())){AI->StopMovement();}}M->Vision->SetPublicSensor(false);Enter(1);return;}
    const double Elapsed=FPlatformTime::Seconds()-At;if(Phase==8){if(Elapsed>2.5){FString Why;Check(M->Economy&&M->Economy->Audit(Why)&&!M->Economy->AuditFailures,TEXT("Native network item accounting reconciles: ")+Why);Finish();}return;}auto* C=M->Combat.Get();auto* BA=Cast<ALDCharacter>(A->GetPawn());auto* BB=Cast<ALDCharacter>(B->GetPawn());auto* SA=C->StateFor(BA);auto* SB=C->StateFor(BB);
    if(!Checked&&Elapsed>1.2&&(Phase==4||Phase==5)){Checked=true;Check(Phase==4?!SB->Tactical.BlockUntil&&!SB->Tactical.Has(LDTactical::EStatus::Mute,LDTactical::Milliseconds(GetWorld()->GetTimeSeconds())):SB->Tactical.Has(LDTactical::EStatus::Mute,LDTactical::Milliseconds(GetWorld()->GetTimeSeconds())),Phase==4?TEXT("Target block consumes one glyph packet without mute"):TEXT("Unblocked remote glyph applies actual mute"));}
    if(Elapsed<3.4){return;}
    if(Phase==1){Check(FVector::DistSquared2D(BA->GetActorLocation(),FVector(-1600,-900,92))<4&&FVector::DistSquared2D(BB->GetActorLocation(),FVector(-1800,-900,92))<4,TEXT("Remote two-target swap moves both bodies atomically"));Check(SA->Tactical.MP>234&&SA->Tactical.MP<245,TEXT("Duplicate two-target request spends90MP once"));}
    if(Phase==2){int32 Count=0;for(TActorIterator<ALDTacticalField> It(GetWorld());It;++It){Count+=It->Kind==ELDTacticalField::Gate&&It->Active(LDTactical::Milliseconds(GetWorld()->GetTimeSeconds()));}Check(Count==2,TEXT("Remote two-point request creates exactly one linked pair"));Check(SA->Tactical.MP>224&&SA->Tactical.MP<235,TEXT("Gate pair spends100MP once"));}
    if(Phase==3){Check(FVector::DistSquared2D(BA->GetActorLocation(),FVector(-1400,-1050,92))<4,TEXT("Fresh remote1s hold traverses gate once"));Check(M->StateFor(A.Get())->GateReadyAt>LDTactical::Milliseconds(GetWorld()->GetTimeSeconds()),TEXT("Arrival3s gate lock persists on owner"));}
    if(Phase==4||Phase==5){Check(M->StateFor(A.Get())->Inventory.Items.IsEmpty()&&M->StateFor(A.Get())->ItemFamilies.FindRef(TEXT("mute"))>LDTactical::Milliseconds(GetWorld()->GetTimeSeconds()),TEXT("Remote item consumed once; family endpoint retained"));}
    if(Phase==6){Check(SB->Hp==LDDevelopment::MaxHP(M->StateFor(B.Get())->Build),TEXT("Temporal Lock is control without damage"));Check(M->StateFor(A.Get())->Build.Cooldowns[34]>GetWorld()->GetTimeSeconds(),TEXT("Remote capstone releases and pays real cooldown"));}
    if(Phase==7){Check(SA->Tactical.MP>130&&SA->Tactical.MP<145&&M->StateFor(A.Get())->Inventory.Items.IsEmpty(),TEXT("Fresh held item channel restores80MP once"));}
    Enter(Phase+1);
}
void ALDContentNetworkProbe::ClientTick()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwner());const double Now=FPlatformTime::Seconds();if(Now-LastPing>.15){LastPing=Now;PC->ServerProbePing(Now);}if(Phase!=PC->ProbePhase){Phase=PC->ProbePhase;At=Now;Sent=Checked=SawCue=false;Sequence=0;LastHold=0;}if(!Phase){return;}const double Elapsed=Now-At;const bool IsA=Role==TEXT("A");const auto& O=PC->OwnerState;const uint32 Token=1000+100*Phase;auto* View=PC->LocalView();
    if(Phase==6&&View){for(const auto& E:View->LastView().Entities){if(E.Kind==ELDObservedKind::TacticalField&&E.Presentation==9){SawCue=true;}}}
    if(!Sent&&IsA&&Elapsed>.35&&O.Build.Revision==uint32(100+Phase))
    {
        FGuid Target,Gate;const FVector TargetPoint=Phase==1?FVector(-1600,-900,92):FVector(-1600,-900,92);if(View){for(const auto& E:View->LastView().Entities){if(E.Kind==ELDObservedKind::Body&&FVector::DistSquared2D(E.Position,TargetPoint)<100){Target=E.Id;}if(E.Kind==ELDObservedKind::TacticalField&&E.Presentation==11&&FVector::DistSquared2D(E.Position,FVector(-1750,-1050,0))<100){Gate=E.Id;}}}
        if((Phase==1||Phase==4||Phase==5)&&!Target.IsValid()||Phase==3&&!Gate.IsValid()){return;}
        FLDTacticalRequest R;R.Match=O.Match;R.Life=O.Life;R.Token=Token;R.BuildRevision=O.Build.Revision;R.CatalogDigest=O.Build.CatalogDigest;R.Direction=FVector::ForwardVector;
        if(Phase==1){R.TargetMode=8;R.SecondObserved=Target;PC->ServerTacticalAction(R);PC->ServerTacticalAction(R);R.Token++;R.SecondObserved=FGuid::NewGuid();PC->ServerTacticalAction(R);}
        if(Phase==2){R.TargetMode=9;R.Point=FVector(-1750,-1050,0);R.SecondPoint=FVector(-1400,-1050,0);PC->ServerTacticalAction(R);PC->ServerTacticalAction(R);}
        if(Phase==3){R.ActionKind=2;R.Observed=Gate;PC->ServerTacticalAction(R);}
        if(Phase==4||Phase==5||Phase==7){const auto* Item=O.Inventory.Find(O.Inventory.Quick[0]);if(!Item){return;}R.ActionKind=1;R.Item=Item->Id;R.ItemRevision=Item->Revision;R.InventoryRevision=O.Inventory.Revision;R.Observed=Phase==7?FGuid():Target;PC->ServerTacticalAction(R);PC->ServerTacticalAction(R);}
        if(Phase==6){R.TargetMode=3;R.Point=FVector(-1600,-900,0);PC->ServerTacticalAction(R);}
        Sent=true;
    }
    if(IsA&&Sent&&(Phase==3||Phase==7)&&Now-LastHold>.09){LastHold=Now;PC->ServerContentHold(O.Match,O.Life,Token,++Sequence,false,false);}
    if(!Checked&&Elapsed>2.5&&Phase<8){Checked=true;Check(O.Build.RulesVersion==7&&O.Build.CatalogDigest.Len()==64&&O.Build.Slots.Num()==5,TEXT("Independent owner receives bounded current build"));Check(View&&!View->IsStale()&&View->ProxyCount()>0,TEXT("Independent client maintains sanitized native world proxies"));if(Phase==1){Check(FVector::DistSquared2D(PC->GetPawn()->GetActorLocation(),IsA?FVector(-1600,-900,92):FVector(-1800,-900,92))<20,TEXT("Independent client sees reconciled swap destination"));}if(Phase==2){Check(View&&View->LastView().Entities.ContainsByPredicate([](const auto& E){return E.Kind==ELDObservedKind::TacticalField&&E.Presentation==11;}),TEXT("Native linked gate cues delivered through8-field observation"));}if(Phase==3&&IsA){Check(O.GateReadyAt>LDTactical::Milliseconds(PC->GetWorld()->GetGameState<ALDGameState>()->GetServerWorldTimeSeconds()),TEXT("Owner-only traversal lock arrives"));}if(Phase==4||Phase==5){if(IsA){Check(O.Inventory.Items.IsEmpty()&&O.ItemFamilies.FindRef(TEXT("mute"))>0,TEXT("Own consumable/family receipt reconciles"));}}if(Phase==6){Check(SawCue,TEXT("Temporal telegraph visible during remote windup"));}if(Phase==7&&IsA){Check(O.Mp>130&&O.Mp<145,TEXT("Owning client receives actual held80MP restoration"));}}
    if(Phase==8&&Elapsed>1.2){Finish();}
}
void ALDContentNetworkProbe::Tick(float Delta){Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(FPlatformTime::Seconds()-Started>65){Check(false,TEXT("B75 native network fixture timed out"));Finish();return;}if(GetNetMode()==NM_Client){ClientTick();}else{ServerTick();}
#endif
}
void ALDContentNetworkProbe::Finish()
{
    if(GetNetMode()==NM_Client){auto Samples=CastChecked<ALDPlayerController>(GetOwner())->ProbeRtts;Samples.Sort();Check(Samples.Num()>15,TEXT("Independent transport echo samples measured"));if(Samples.Num()){Check(Samples[Samples.Num()/2]>=20&&Samples[Samples.Num()/2]<350,FString::Printf(TEXT("Transport median %.1fms"),Samples[Samples.Num()/2]));}}
    auto Json=MakeShared<FJsonObject>();Json->SetNumberField(TEXT("failures"),Failures);Json->SetStringField(TEXT("scope"),TEXT("B75 controlled dedicated authority plus two independent clients, real v3 RPCs and sanitized native proxy observations; labelled build/item/clock/placement fixtures, no earned-route/human/cooked claim."));TArray<TSharedPtr<FJsonValue>> Lines;for(const auto& S:Checks){Lines.Add(MakeShared<FJsonValueString>(S));}Json->SetArrayField(TEXT("checks"),Lines);FString Directory,Output;FParse::Value(FCommandLine::Get(),TEXT("LDProbeReportDir="),Directory);FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Output));if(GetNetMode()!=NM_Client&&GetWorld()->GetAuthGameMode<ALDGameMode>()->Economy){FFileHelper::SaveStringToFile(GetWorld()->GetAuthGameMode<ALDGameMode>()->Economy->Accounting().ExportJson(TEXT("B75Network")),*(Directory/TEXT("Ledger.json")));}FFileHelper::SaveStringToFile(Output,*(Directory/(Role+TEXT(".json"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);SetActorTickEnabled(false);FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
