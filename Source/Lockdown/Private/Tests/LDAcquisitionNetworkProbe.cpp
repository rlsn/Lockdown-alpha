#include "Tests/LDAcquisitionNetworkProbe.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Inventory/LDAcquisitionCatalog.h"
#include "Inventory/LDAcquisitionDirector.h"
#include "Economy/LDProductionDirector.h"
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

ALDAcquisitionNetworkProbe::ALDAcquisitionNetworkProbe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDAcquisitionNetworkProbe::BeginPlay()
{
    Super::BeginPlay();Started=At=FPlatformTime::Seconds();FParse::Value(FCommandLine::Get(),TEXT("LDProbeRole="),Role);if(Role.IsEmpty()){Role=TEXT("Server");}
    if(GetNetMode()==NM_Client){CastChecked<ALDPlayerController>(GetOwner())->ServerProbeHello(Role==TEXT("A")?1:2);}
}
void ALDAcquisitionNetworkProbe::Check(bool Value,const FString& Message){Checks.Add((Value?TEXT("PASS "):TEXT("FAIL "))+Message);if(!Value){++Failures;}UE_LOG(LogTemp,Display,TEXT("LD_B74_NET %s"),*Checks.Last());}
void ALDAcquisitionNetworkProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(FPlatformTime::Seconds()-Started>100){Check(false,TEXT("B74 network fixture timed out"));Finish();return;}
    if(GetNetMode()==NM_Client){ClientTick();}else{ServerTick();}
#endif
}
void ALDAcquisitionNetworkProbe::Finish()
{
    if(GetNetMode()==NM_Client)
    {auto* PC=CastChecked<ALDPlayerController>(GetOwner());auto Samples=PC->ProbeRtts;Samples.Sort();Check(Samples.Num()>=15,TEXT("Independent client transport echo measured"));if(!Samples.IsEmpty()){Check(Samples[Samples.Num()/2]>=20&&Samples[Samples.Num()/2]<350,FString::Printf(TEXT("Echo median %.1fms"),Samples[Samples.Num()/2]));}}
    TSharedRef<FJsonObject> Json=MakeShared<FJsonObject>();Json->SetNumberField(TEXT("failures"),Failures);Json->SetStringField(TEXT("scope"),TEXT("B74 native authority and two remote clients. Diagnostic gold/Toolkit funding, quiet and placement; actual bounded RPC buys, stock contention, work orders, cancelled/committed quality and shared-cache race. Separate earned journey covers normal acquisition"));TArray<TSharedPtr<FJsonValue>> Lines;for(const auto& S:Checks){Lines.Add(MakeShared<FJsonValueString>(S));}Json->SetArrayField(TEXT("checks"),Lines);
    FString Directory,Output;FParse::Value(FCommandLine::Get(),TEXT("LDProbeReportDir="),Directory);FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Output));
    if(GetNetMode()!=NM_Client){auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(M&&M->Economy){if(!FFileHelper::SaveStringToFile(M->Economy->Accounting().ExportJson(TEXT("B74Network")),*(Directory/TEXT("Ledger.json")))){++Failures;}}}
    if(!FFileHelper::SaveStringToFile(Output,*(Directory/(Role+TEXT(".json"))))){++Failures;}SetActorTickEnabled(false);FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
void ALDAcquisitionNetworkProbe::Enter(int32 Next)
{
    Phase=Next;At=FPlatformTime::Seconds();Checked=false;auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const double Now=GetWorld()->GetTimeSeconds();
    if(Next==4){auto* I=GetWorld()->SpawnActor<ALDItem>();I->Kind=ELDItemKind::Toolkit;Check(M->Inventory->Adopt(A.Get(),I,TEXT("DiagnosticItemCreation")),TEXT("Labelled diagnostic Toolkit for cancellation/quality boundary"));}
    for(auto* PC:{A.Get(),B.Get()}){auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());PC->Navigation->ResetLife();Body->GetCharacterMovement()->SetMovementMode(MOVE_None);Body->GetCharacterMovement()->StopMovementImmediately();const uint8 Site=Next==2||Next==3||Next==7||Next==10?4:Next>=8?6:2;Body->SetActorLocation(ALDProductionDirector::Position(GetWorld(),Site)+FVector(0,PC==A.Get()?40:70,92));auto* S=M->Combat->StateFor(Body);S->ProtectionUntil=0;S->LastDamage=S->LastDealt=Now-20;M->StateFor(PC)->ProtectedUntil=0;M->SyncOwner(PC);PC->ClientProbePhase(Next);}
    BeforeGold=M->StateFor(A.Get())->Gold;
}
void ALDAcquisitionNetworkProbe::ServerTick()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M||!M->Combat){return;}
    if(!Phase){for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){auto* P=Cast<ALDPlayerController>(It->Get());if(P&&P->GetPawn()){if(P->ProbeRole==1){A=P;}if(P->ProbeRole==2){B=P;}}}if(!A.IsValid()||!B.IsValid()){return;}FString Why;M->SetLobbyChoice(A.Get(),0,true,Why);M->SetLobbyChoice(B.Get(),1,true,Why);if(!M->StartDevelopmentMatch(A.Get(),Why)&&!M->StartDevelopmentMatch(B.Get(),Why)){Check(false,Why);Finish();return;}
        for(TActorIterator<ALDResident> It(GetWorld());It;++It){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();}
        for(auto* P:{A.Get(),B.Get()}){M->StateFor(P)->Gold+=1000;M->RecordEconomyGold(TEXT("DiagnosticReward"),1000);}Check(true,TEXT("Labelled diagnostic wallet funding; no normal income claim in network boundary lane"));Enter(1);return;}
    const double Elapsed=FPlatformTime::Seconds()-At;if(Phase==11){if(Elapsed>3){Finish();}return;}if(!A.IsValid()||!B.IsValid()){Check(false,TEXT("Unexpected disconnect"));Finish();return;}if(Elapsed<(Phase==1||Phase==2||Phase==3||Phase==7?2.5:Phase==4||Phase==6?3.8:5.6)){return;}
    auto* O=M->StateFor(A.Get());auto* Other=M->StateFor(B.Get());auto Find=[&](ELDItemKind K){return O->Inventory.Items.FindByPredicate([&](const auto& I){return I.Kind==K;});};
    if(Phase==1){Check(O->Gold==982&&Other->Gold==982,TEXT("Independent bounded supply RPCs burn fixed18 once, including request replay"));Check(Find(ELDItemKind::SimpleStaff)!=nullptr,TEXT("Official supply creates exact physical staff"));}
    if(Phase==2){Check(M->Economy->Accounting().Remaining(TEXT("B74Book32"))==0,TEXT("Concurrent remote CircleIII purchases consume exactly two global copies"));Check(O->Gold==892&&Other->Gold==892,TEXT("Each successful book purchase burns exactly90"));}
    if(Phase==3){Check(O->Gold==BeforeGold&&O->Gold>=90,TEXT("Third archive purchase rejects despite sufficient funds; no spend"));}
    if(Phase==4){const auto* V=Find(ELDItemKind::SimpleStaff);Check(V&&V->Quality==0&&!V->bReserved&&Find(ELDItemKind::Toolkit)&&O->Gold==BeforeGold,TEXT("Remote release restores exact quality inputs; zero fee and no upgrade"));}
    if(Phase==5){const auto* V=Find(ELDItemKind::SimpleStaff);Check(V&&V->Quality==1&&!Find(ELDItemKind::Toolkit)&&O->Gold==BeforeGold-6,TEXT("Remote4s quality commit consumes Toolkit and changes same staff to Fine"));}
    if(Phase==6){const auto* V=Find(ELDItemKind::SimpleStaff);Check(V&&O->Inventory.Bindings[0]==V->Id&&O->Build.Gear.WeaponBase==34,TEXT("Remote equipment uses floored Fine weapon magnitude"));}
    if(Phase==7){Check(O->Gold==BeforeGold&&Find(ELDItemKind::SimpleStaff)->Quality==1&&!O->DevelopmentKind,TEXT("Equipped quality input rejects without starting a craft"));}
    if(Phase==8){Check(M->Economy->Accounting().Remaining(TEXT("B74Cache6"))==0,TEXT("Concurrent native cache opens debit one fixed opening budget"));}
    if(Phase==9){int32 Keys=0;for(auto* Own:{O,Other}){for(const auto& V:Own->Inventory.Items){Keys+=V.Kind==ELDItemKind::AncientKeyA;}}Check(Keys==1,TEXT("Two real3s pickup RPCs racing one cache key yield one physical owner"));FString Why;Check(M->Economy->Audit(Why)&&M->Economy->AuditFailures==0,TEXT("Network stock/cancellation/quality/cache receipts reconcile"));}
    if(Phase==10){const auto* V=Find(ELDItemKind::HarpoonHalberd);Check(V&&O->Gold==BeforeGold-67,TEXT("Real East work-order RPC burns24+35+8 and creates one Harpoon cargo output"));if(V){const auto* P=M->Inventory->Owned(A.Get(),V->Id);Check(P&&P->ParentIds.Num()==2,TEXT("Remote completed order records two transient officially purchased parents"));}FString Why;Check(M->Economy->Audit(Why),TEXT("Final work-order receipt reconciles against physical world"));}
    Enter(Phase+1);
}
void ALDAcquisitionNetworkProbe::ClientTick()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwner());const double Now=FPlatformTime::Seconds();if(Now-LastPing>.15){LastPing=Now;PC->ServerProbePing(Now);}if(PC->ProbePhase!=Phase){Phase=PC->ProbePhase;At=Now;Sent=Checked=false;Sequence=0;HoldSent=0;}if(!Phase){return;}const double E=Now-At;const auto& O=PC->OwnerState;const bool IsA=Role==TEXT("A");const uint32 Token=10000+Phase*100;const auto* Catalog=ULDAcquisitionCatalog::Load();
    if(!Sent&&E>.5)
    {
        if(Phase==1||Phase==2||(Phase==3&&IsA)){FLDSupplyRequest R;R.Match=O.Match;R.Life=O.Life;R.Token=Token;R.InventoryRevision=O.Inventory.Revision;R.Site=Phase==1?2:4;R.Kind=Phase==1?ELDItemKind::SimpleStaff:ELDItemKind::Book;R.Skill=Phase==1?ELDSkill::None:ELDSkill::BreakSigil;R.Digest=Catalog->Checksum;PC->ServerAcquisitionPurchase(R);if(Phase==1){PC->ServerAcquisitionPurchase(R);}}
        if(IsA&&(Phase==4||Phase==5||Phase==7))
        {const auto* Staff=O.Inventory.Items.FindByPredicate([](const auto& V){return V.Kind==ELDItemKind::SimpleStaff;});if(!Staff){return;}FLDProductionPlan P;FString Why;const bool Valid=ALDAcquisitionDirector::Preview(O.Inventory,O.Build,Phase==7?33:32,0,2,Staff->Id,P,Why);if(Phase==7){P.Site=4;P.Recipe=33;P.Target=Staff->Id;P.CatalogDigest=Catalog->Checksum;P.BuildRevision=O.Build.Revision;P.InventoryRevision=O.Inventory.Revision;P.Fee=12;P.Inputs.Add(*Staff);}Check(Valid||Phase==7,TEXT("Owner creates bounded preview for quality request"));PC->ServerProductionRequest(O.Match,O.Life,Token,P);}
        if(IsA&&Phase==6){const auto* V=O.Inventory.Items.FindByPredicate([](const auto& I){return I.Kind==ELDItemKind::SimpleStaff;});if(!V){return;}FLDLoadoutRequest R;R.Match=O.Match;R.Life=O.Life;R.Token=Token;R.InventoryRevision=O.Inventory.Revision;R.BuildRevision=O.Build.Revision;R.ItemRevision=V->Revision;R.Item=V->Id;R.EquipmentDigest=ULDEquipmentCatalog::Load()->Checksum;R.Command=LDInventory::Equip;R.Binding=0;PC->ServerLoadoutRequest(R);}
        if(Phase==10&&IsA){FLDProductionPlan P;FString Why;Check(ALDAcquisitionDirector::Preview(O.Inventory,O.Build,20,1,4,FGuid(),P,Why),TEXT("Owner previews missing official immediate inputs for completed work order"));PC->ServerProductionRequest(O.Match,O.Life,Token,P);}
        if(Phase==8||Phase==9){FGuid Target;if(Phase==9){for(const auto& I:PC->CacheItems){if(I.Kind==ELDItemKind::AncientKeyA){Target=I.Id;}}if(!Target.IsValid()){return;}}FLDProductionPlan P;FString Why;if(!ALDAcquisitionDirector::Preview(O.Inventory,O.Build,Phase==8?34:35,0,6,Target,P,Why)){return;}PC->ServerProductionRequest(O.Match,O.Life,Token,P);}
        Sent=true;
    }
    if(Sent&&Now-HoldSent>.09){HoldSent=Now;if(Phase==4&&IsA){if(E>1.5){PC->ServerProductionRelease(O.Match,O.Life,Token);}else{PC->ServerProductionHold(O.Match,O.Life,Token,++Sequence,false);}}if((Phase==5||Phase==7||Phase==10)&&IsA||Phase==8||Phase==9){PC->ServerProductionHold(O.Match,O.Life,Token,++Sequence,false);}if(Phase==6&&IsA){PC->ServerDevelopmentHold(O.Match,O.Life,Token,++Sequence,false);}}
    if(!Checked&&E>1.6){Checked=true;if(Phase==8){const auto* V=PC->LocalView();bool Seen=false;if(V){for(const auto& Entry:V->LastView().Entities){Seen|=Entry.Kind==ELDObservedKind::Production&&Entry.Presentation==6;}}Check(Seen,TEXT("Actual sanitized world view serializes new ruin service presentation"));}Check(O.Inventory.RulesVersion==7,TEXT("Remote owner retains tactical inventory profile"));Check(O.Build.RulesVersion==7,TEXT("Remote owner receives owner-only derived gear state"));}
    if(Phase==11&&E>2.2){Finish();}
}
