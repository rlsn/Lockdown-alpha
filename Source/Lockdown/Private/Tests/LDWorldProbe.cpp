#include "Tests/LDWorldProbe.h"
#include "Tests/LDB90ClosureProbe.h"
#include "UI/LDInventoryPanel.h"
#include "World/LDWorldDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Combat/LDCombatDirector.h"
#include "Economy/LDEconomyDirector.h"
#include "Economy/LDCommerceDirector.h"
#include "Economy/LDBankDirector.h"
#include "Economy/LDProductionDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDAcquisitionDirector.h"
#include "Inventory/LDAcquisitionCatalog.h"
#include "Development/LDDevelopmentDirector.h"
#include "Development/LDWorldChecks.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Region/LDRegionDefinition.h"
#include "Navigation/LDNavigationComponent.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "Core/LDGroundQueries.h"
#include "Character/LDCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Algo/Reverse.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
namespace LDWorldDemo
{
ALDCharacter* Body(ALDPlayerController* PC){return PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;}
FGuid Find(ALDGameMode* M,ALDPlayerController* PC,ELDItemKind K){const auto* O=M->StateFor(PC);const auto* I=O?O->Inventory.Items.FindByPredicate([&](const auto& V){return V.Kind==K;}):nullptr;return I?I->Id:FGuid();}
FGuid Observe(ALDGameMode* M,ALDPlayerController* PC,AActor* Actor){M->Vision->Tick(0);if(const auto* V=M->Vision->AuthorityViewFor(PC)){for(const auto& E:V->Entities){if(M->Vision->ResolveObserved(PC,E.Id)==Actor){return E.Id;}}}return {};}
FLDProductionPlan Commodity(ALDGameMode* M,ALDPlayerController* PC,uint8 Recipe)
{const auto* O=M->StateFor(PC);FLDProductionPlan P;P.Site=Recipe==7?3:2;P.Recipe=Recipe;P.InventoryRevision=O->Inventory.Revision;P.BuildRevision=O->Build.Revision;P.Fee=Recipe==1?4:Recipe==2?6:0;int32 N=0;const auto First=Recipe==1?ELDItemKind::Fiber:Recipe==6?ELDItemKind::Core:ELDItemKind::Toolkit;for(const auto& I:O->Inventory.Items){if(I.Kind==First&&N++<(Recipe==6?1:2)){P.Inputs.Add(I);}}if(Recipe<=2){if(const auto* I=O->Inventory.Find(Find(M,PC,Recipe==1?ELDItemKind::Ore:ELDItemKind::Crystal))){P.Inputs.Add(*I);}}return P;}
}

ALDWorldProbe::ALDWorldProbe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDWorldProbe::Check(bool Good,const FString& Message){Failures+=!Good;Checks.Add((Good?TEXT("PASS "):TEXT("FAIL "))+Message);UE_LOG(LogTemp,Display,TEXT("LD_B76_DEMO %s"),*Checks.Last());}
void ALDWorldProbe::Add(const FString& Name,double Timeout,TFunction<bool()> Poll){Steps.Add({Name,Timeout,MoveTemp(Poll)});}
void ALDWorldProbe::Wait(double Seconds){Add(FString::Printf(TEXT("Wait %.2fs on native world clock"),Seconds),Seconds+5,[this,Seconds](){return GetWorld()->GetTimeSeconds()-StepAt>=Seconds;});}
void ALDWorldProbe::Tick(float Delta)
{
#if !UE_BUILD_SHIPPING
    if(Done){return;}if(FParse::Param(FCommandLine::Get(),TEXT("LDWorldNetProbe"))){Network();return;}if(!Initialized){Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();A=Cast<ALDPlayerController>(GetWorld()->GetFirstPlayerController());if(!A||!A->GetPawn()){return;}Initialized=true;FParse::Value(FCommandLine::Get(),TEXT("LDWorldRoute="),Route);if(Route==TEXT("Boundary")){Boundary();return;}B=Cast<ALDPlayerController>(UGameplayStatics::CreatePlayer(this,1,true));FString Why;if(!B){Check(false,TEXT("Second normal local contender"));Finish();return;}Mode->SetLobbyChoice(A,Route==TEXT("Charter")?0:4,true,Why);Mode->SetLobbyChoice(B,Mode->IsV08Profile()?5:0,true,Why);if(!Mode->StartDevelopmentMatch(A,Why)||!Mode->WorldRuntime){Check(false,Why);Finish();return;}for(auto* PC:{A.Get(),B.Get()}){PC->bControlsSuppressed=false;PC->bFreshInputRequired=false;PC->NextRequest=Token;}Check(!Mode->Economy->AllowDiagnosticItems()&&Mode->StateFor(A)->Gold==0&&(Mode->StateFor(A)->Inventory.Items.Num()==(!Mode->IsV08Profile()&&Route==TEXT("Charter")?1:0)),TEXT("Native roster begins with zero money and declared starter endowment only; diagnostic grants disabled"));Build();}
    if(Cursor>=Steps.Num()){Finish();return;}if(!Mode->StateFor(A)->bActive){Check(false,TEXT("Normal route actor died; no scripted resurrection or refund"));Finish();return;}
    const double Now=GetWorld()->GetTimeSeconds();if(StepAt<0){StepAt=Now;UE_LOG(LogTemp,Display,TEXT("LD_B76_STEP route=%s step=%d time=%.3f %s"),*Route,Cursor,Now,*Steps[Cursor].Name);}
    if(Steps[Cursor].Poll()){Check(true,Steps[Cursor].Name);++Cursor;StepAt=-1;}else if(Now-StepAt>Steps[Cursor].Timeout){Check(false,TEXT("Timed out: ")+Steps[Cursor].Name+TEXT(" · ")+A->IntentFeedback);Finish();}
#endif
}
void ALDWorldProbe::Move(ALDPlayerController* PC,TFunction<FVector()> Point,const FString& Name)
{
    Add(TEXT("Native traversal: ")+Name,90,[this,PC,Point=MoveTemp(Point),Begun=false,From=FVector::ZeroVector,Waypoints=TArray<FVector>(),Waypoint=0]() mutable {const FVector Goal=Point();if(!Begun){FVector Ground;if(!LDGround::MapSurface(GetWorld(),Goal,Ground)){return false;}Begun=true;From=LDWorldDemo::Body(PC)->GetActorLocation();const auto* D=LDRegion::Definition(GetWorld());const FVector City=LDRegion::Site(GetWorld(),1),East=LDRegion::Site(GetWorld(),6);const bool ToEast=FVector::Dist2D(From,City)<500&&FVector::Dist2D(Goal,East)<150,ToCity=FVector::Dist2D(From,East)<500&&FVector::Dist2D(Goal,City)<150;if(ToEast||ToCity){for(const auto& R:D->Routes){if(R.Covered&&R.From==D->CityRegion&&R.To==D->Towns()[1]){Waypoints=R.Points;if(ToCity){Algo::Reverse(Waypoints);}break;}}}if(Waypoints.Num()>1){Waypoint=1;Waypoints.Last()=Goal;}else{Waypoints={Goal};}PC->bFreshInputRequired=false;PC->NextRequest=Token;PC->Navigation->Navigate(Waypoints[Waypoint]);}
        if(Waypoint+1<Waypoints.Num()&&!PC->Navigation->IsFollowing()&&FVector::Dist2D(LDWorldDemo::Body(PC)->GetActorLocation(),Waypoints[Waypoint])<=90){++Waypoint;PC->bFreshInputRequired=false;PC->Navigation->Navigate(Waypoints[Waypoint]);}
        if(FVector::DistSquared2D(LDWorldDemo::Body(PC)->GetActorLocation(),Goal)>90*90||PC->Navigation->IsFollowing()||LDWorldDemo::Body(PC)->GetVelocity().SizeSquared2D()>1){return false;}auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),Steps[Cursor].Name);Row->SetNumberField(TEXT("seconds"),GetWorld()->GetTimeSeconds()-StepAt);Row->SetNumberField(TEXT("straightCm"),FVector::Dist2D(From,Goal));Row->SetStringField(TEXT("from"),From.ToString());Row->SetStringField(TEXT("to"),Goal.ToString());Row->SetNumberField(TEXT("nativePathQueries"),PC->Navigation->QueryCount);Travel.Add(MakeShared<FJsonValueObject>(Row));return true;});
}
void ALDWorldProbe::Hold(ALDPlayerController* PC,uint8 Target,double Seconds)
{
    Add(FString::Printf(TEXT("Normal held context target%u /%.1fs"),Target,Seconds),Seconds+3,[this,PC,Target,Seconds,Id=uint32(0),Seq=uint32(0)]() mutable {const double Now=GetWorld()->GetTimeSeconds();auto* O=Mode->StateFor(PC);if(!Id){PC->Navigation->Stop();Id=++Token;Mode->Objectives->Queue(PC,O->Match,O->Life,Id,Target,{});return false;}Mode->Objectives->RefreshHold(PC,O->Life,Id,++Seq,false,Now);if(Now-StepAt<Seconds+.15){return false;}Mode->Objectives->Release(PC,O->Life,Id);return Target==3?Mode->GetGameState<ALDGameState>()->EndReason==1:Target>=10?(Target>=12?O->ShrineReadyAt>Now:O->BreachRoute==Target-5):Target==1?(O->ContractStage==1||O->ContractStage==0):O->ContractStage==2;});
}
void ALDWorldProbe::Produce(ALDPlayerController* PC,TFunction<FLDProductionPlan()> Plan,double Seconds)
{
    Add(TEXT("Normal exact physical production / ")+FString::SanitizeFloat(Seconds),Seconds+4,[this,PC,Plan=MoveTemp(Plan),Seconds,Id=uint32(0),Seq=uint32(0),Accepted=false]() mutable {const double Now=GetWorld()->GetTimeSeconds();auto* O=Mode->StateFor(PC);if(!Id){PC->Navigation->Stop();Id=++Token;Mode->Production->Queue(PC,O->Match,O->Life,Id,Plan());return false;}Accepted|=O->DevelopmentKind!=0;Mode->Production->RefreshHold(PC,O->Life,Id,++Seq,false);return Accepted&&Now-StepAt>=Seconds+.15&&!O->DevelopmentKind;});
}
void ALDWorldProbe::Couriers(int32 Count)
{
    for(int32 N=0;N<Count;++N){Move(A,[this](){return LDRegion::Site(GetWorld(),1);},TEXT("City Guild"));Add(TEXT("Courier cooldown elapsed"),25,[this](){return GetWorld()->GetTimeSeconds()>=Mode->StateFor(A)->WorkReadyAt;});Hold(A,1,.2);Move(A,[this](){return LDRegion::Definition(GetWorld())->Node(Mode->StateFor(A)->ContractDestination)->Position;},TEXT("Assigned town"));Add(TEXT("Collect at native assigned town"),4,[this,Id=uint32(0),Seq=uint32(0)]() mutable {auto* O=Mode->StateFor(A);if(!Id){Id=++Token;Mode->Objectives->Queue(A,O->Match,O->Life,Id,LDRegion::Definition(GetWorld())->Node(O->ContractDestination)->Site,{});}Mode->Objectives->RefreshHold(A,O->Life,Id,++Seq,false,GetWorld()->GetTimeSeconds());return O->ContractStage==2;});Move(A,[this](){return LDRegion::Site(GetWorld(),1);},TEXT("Town to City"));Hold(A,1,3);}
    Add(TEXT("Eight earned courier payments /200g, no grants"),1,[this,Count](){return Mode->StateFor(A)->PaidJobs==Count&&Mode->StateFor(A)->Gold==25*Count;});
}
void ALDWorldProbe::Buy(ELDItemKind Kind,uint8 Service)
{
    Move(A,[this,Service](){return ALDCommerceDirector::Position(GetWorld(),Service);},TEXT("Official input counter"));Wait(.4);Add(TEXT("Paid official component: ")+LDInventory::Name(Kind,ELDSkill::None),2,[this,Kind,Service,Before=-1]() mutable {auto* O=Mode->StateFor(A);if(Before<0){Before=O->Inventory.Items.Num();A->NextRequest=Token;A->OpenCommerce(Service);Token=FMath::Max(Token,A->NextRequest);const auto Q=A->CommerceView;const auto* Offer=Q.Offers.FindByPredicate([&](const auto& V){return V.Kind==Kind;});if(!Offer){return false;}Mode->Commerce->Queue(A,O->Match,O->Life,++Token,Q.Session,Q.Revision,1,Kind,{},0,O->Inventory.Revision,Offer->Ask);}if(O->Inventory.Items.Num()!=Before+1){return false;}A->CloseCommerce();if(A->InventoryUI&&A->InventoryUI->IsOpen()){A->InventoryUI->Close();}Token=FMath::Max(Token,A->NextRequest);return true;});
}
void ALDWorldProbe::Toolkit(){Buy(ELDItemKind::Fiber);Buy(ELDItemKind::Fiber);Buy(ELDItemKind::Ore);Move(A,[this](){return ALDProductionDirector::Position(GetWorld(),2);},TEXT("City workshop"));Produce(A,[this](){return LDWorldDemo::Commodity(Mode,A,1);},4);}
void ALDWorldProbe::Core(){Toolkit();Toolkit();Buy(ELDItemKind::Crystal,1);Move(A,[this](){return ALDProductionDirector::Position(GetWorld(),2);},TEXT("City Core workshop"));Produce(A,[this](){return LDWorldDemo::Commodity(Mode,A,2);},6);}
void ALDWorldProbe::Cache(uint8 Site,ELDItemKind Kind)
{
    Move(A,[this,Site](){return LDRegion::Site(GetWorld(),Site==6?2:6);},TEXT("Town before ruin discovery"));Move(A,[this,Site](){return ALDProductionDirector::Position(GetWorld(),Site);},TEXT("Town / ruin approach"));Wait(6.1);Produce(A,[this,Site](){FLDProductionPlan P;FString Why;Mode->Acquisition->Preview(Mode->StateFor(A)->Inventory,Mode->StateFor(A)->Build,34,0,Site,{},P,Why);return P;},4);
    Produce(A,[this,Site,Kind](){FLDProductionPlan P;FString Why;const auto* I=A->CacheItems.FindByPredicate([&](const auto& V){return V.Kind==Kind;});Mode->Acquisition->Preview(Mode->StateFor(A)->Inventory,Mode->StateFor(A)->Build,35,0,Site,I?I->Id:FGuid(),P,Why);return P;},Kind>=ELDItemKind::AncientKeyA&&Kind<=ELDItemKind::AncientKeyC?3:.05);
    Add(TEXT("Exact scarce cache item owned"),1,[this,Kind](){return LDWorldDemo::Find(Mode,A,Kind).IsValid();});
}
void ALDWorldProbe::Fight(uint8 Kind,uint8 Index)
{
    const FString Label=Kind==1?TEXT("Guardian"):Kind==0?TEXT("Camp"):FString::Printf(TEXT("Grand tower %u"),Index);
    Move(A,[this,Kind,Index](){const FString Name=Kind==1?TEXT("BossLair"):(Kind==0?FString::Printf(TEXT("Camp%c"),'A'+Index):FString::Printf(TEXT("GrandTower%c"),'A'+Index));return Mode->WorldRuntime->Position(Name)+FVector(Kind==2?(Index==0?-550:550):700,0,0);},Label+TEXT(" observed approach"));
    Add(TEXT("Native legal bow combat: ")+Label,500,[this,Kind,Index,Target=TWeakObjectPtr<ALDWorldPawn>(),Found=false,AttackAt=-100.,MoveAt=-100.,ShotAt=-100.,LogAt=-100.,Lane=0]() mutable {
        const double Now=GetWorld()->GetTimeSeconds();if(!Found){for(const auto& W:Mode->WorldRuntime->Pawns){if(W.IsValid()&&W->Kind==Kind&&W->Index==Index&&!W->Paid){Target=W;Found=true;break;}}}auto* P=Target.Get();if(Found&&(!P||P->Paid)){A->Navigation->Stop();return true;}if(!P){return false;}
        // Policy only issues attacks from current observation and movement from the public lair/tower coordinate.
        // It does not read HP/cooldowns or modify encounter state.
        const FVector Here=LDWorldDemo::Body(A)->GetActorLocation(),Home=P->Home;const double Radius=Kind==2?550:950;FVector Radial=(Here-Home).GetSafeNormal2D();if(Radial.IsNearlyZero()){Radial=FVector::ForwardVector;}const FVector Tangent(-Radial.Y,Radial.X,0);FVector Next=Home+(Radial*.6+Tangent*.8).GetSafeNormal2D()*Radius;if(Kind==2){const FVector One=Home+(Index!=0?FVector(-180,550,0):FVector(Index==0?-550:550,-180,0));const FVector Two=Home+(Index!=0?FVector(180,550,0):FVector(Index==0?-550:550,180,0));if(FVector::Dist2D(Here,Lane?Two:One)<70){Lane=1-Lane;}Next=Lane?Two:One;}
        for(TActorIterator<ALDArcProjectile> It(GetWorld());It;++It){if(Mode->Vision->CanObserve(A,*It)&&FVector::DistSquared2D(It->GetActorLocation(),P->GetActorLocation())<80*80){ShotAt=Now;}}
        auto* S=Mode->Combat->StateFor(LDWorldDemo::Body(A));if(Now-LogAt>=1){FVector Ground;const bool Surface=LDGround::MapSurface(GetWorld(),Next,Ground);UE_LOG(LogTemp,Display,TEXT("LD_B76_FIGHT t=%.2f here=%s velocity=%s next=%s surface=%d can=%d fresh=%d suppressed=%d following=%d action=%d observed=%d"),Now,*Here.ToString(),*LDWorldDemo::Body(A)->GetVelocity().ToString(),*Next.ToString(),Surface,A->CanWorldCommand(),A->bFreshInputRequired,A->bControlsSuppressed,A->Navigation->IsFollowing(),S->Action,LDWorldDemo::Observe(Mode,A,P).IsValid());LogAt=Now;}if(S->Action==1){return false;}if(Now-AttackAt<.4){return false;}
        const bool Time=Kind==2?(Now-ShotAt>=.6&&Now-ShotAt<.85):Now-AttackAt>=1.8;
        if(Time&&FVector::DistSquared2D(Here,P->GetActorLocation())<580*580&&(Kind==2||FVector::DistSquared2D(Here,P->GetActorLocation())>450*450)){const FGuid Seen=LDWorldDemo::Observe(Mode,A,P);if(Seen.IsValid()){A->Navigation->Stop();FLDTacticalRequest R;R.ActionKind=4;R.Match=A->OwnerState.Match;R.Life=A->OwnerState.Life;R.Token=++Token;A->NextRequest=Token;R.BuildRevision=A->OwnerState.Build.Revision;R.CatalogDigest=A->OwnerState.Build.CatalogDigest;R.Direction=(P->GetActorLocation()-Here).GetSafeNormal2D();R.Observed=Seen;if(Mode->IsV08Profile()){R.Schema=8;R.WireVersion=4;R.SourceKind=3;R.InventoryRevision=A->OwnerState.Inventory.Revision;}A->ServerTacticalAction(R);AttackAt=Now;return false;}}
        if(Now-MoveAt>=(Kind==2?.5:2.5)||!A->Navigation->IsFollowing()){A->NextRequest=Token;A->bFreshInputRequired=false;FVector Ground;for(int32 Candidate=0;Candidate<8;++Candidate){const FVector Point=Kind==2?Next:Home+Radial.RotateAngleAxis(53.+Candidate*15.,FVector::UpVector)*Radius;if(LDGround::MapSurface(GetWorld(),Point,Ground)){A->Navigation->Navigate(Ground);MoveAt=Now;break;}}}return false;});
}
void ALDWorldProbe::Build()
{
    Wait(7);const bool Keys=Route==TEXT("Keys"),Artifact=Route==TEXT("Artifact"),Siege=Route==TEXT("Siege"),Charter=Route==TEXT("Charter");Check(Keys||Artifact||Siege||Charter,TEXT("Recognized normal route"));if(Failures){return;}
    if(!Keys){Couriers(8);}
    if(Siege||Artifact){Move(A,[this](){return ALDProductionDirector::Position(GetWorld(),2);},TEXT("City equipment supply"));Wait(6.1);Add(TEXT("Earned Surveyor Bow purchase50g"),2,[this,Begun=false]() mutable {auto* O=Mode->StateFor(A);if(!Begun){Begun=true;Mode->Acquisition->Purchase(A,O->Match,O->Life,++Token,2,ELDItemKind::SurveyorBow,ELDSkill::None,O->Inventory.Revision,ULDAcquisitionCatalog::Load(Mode->RulesVersion())->Checksum);}return LDWorldDemo::Find(Mode,A,ELDItemKind::SurveyorBow).IsValid();});Add(TEXT("Quiet native weapon binding2s"),4,[this,Id=uint32(0),Seq=uint32(0)]() mutable {auto* O=Mode->StateFor(A);const auto* I=O->Inventory.Find(LDWorldDemo::Find(Mode,A,ELDItemKind::SurveyorBow));if(!I){return false;}if(!Id){Id=++Token;Mode->Development->QueueLoadout(A,O->Match,O->Life,Id,O->Inventory.Revision,I->Revision,LDInventory::Equip,I->Id,Mode->IsV08Profile()?5:0);}Mode->Development->RefreshHold(A,O->Life,Id,++Seq,false);return I->BindingMask!=0&&O->Build.Gear.Ranged;});}
    if(Keys){for(uint8 N=0;N<3;++N){Cache(6+N,ELDItemKind(uint8(ELDItemKind::AncientKeyA)+N));}}
    if(Siege){Fight(0,0);for(uint8 Order=0;Order<3;++Order){const uint8 N=Order==1?2:Order==2?1:0;Fight(2,N);Move(A,[this,N](){return Mode->WorldRuntime->Position(N?TEXT("ShrineEast"):TEXT("ShrineWest"));},TEXT("Shrine outside tower range"));Wait(6.1);Hold(A,N?13:12,4);}Add(TEXT("Three independent tower deaths grant permanent global siege"),1,[this](){return Mode->GetGameState<ALDGameState>()->DefensiveFlags==0;});}
    if(Artifact){Core();Core();Cache(8,ELDItemKind::ArcaneCatalyst);Fight(1,0);Move(A,[this](){for(TActorIterator<ALDItem> It(GetWorld());It;++It){if(It->State==0&&It->Kind==ELDItemKind::HeartFragment){return It->GetActorLocation();}}return FVector(100000,0,0);},TEXT("Physical guardian Fragment"));Add(TEXT("Observed capacity-checked Fragment pickup"),2,[this,Begun=false]() mutable {auto* O=Mode->StateFor(A);if(!Begun){for(TActorIterator<ALDItem> It(GetWorld());It;++It){if(It->State==0&&It->Kind==ELDItemKind::HeartFragment){const FGuid Seen=LDWorldDemo::Observe(Mode,A,*It);if(Seen.IsValid()){Mode->Inventory->Queue(A,O->Match,O->Life,++Token,O->Inventory.Revision,LDInventory::Pickup,Seen,It->Revision,0,{});Begun=true;}break;}}}return LDWorldDemo::Find(Mode,A,ELDItemKind::HeartFragment).IsValid();});Move(A,[this](){return ALDProductionDirector::Position(GetWorld(),5);},TEXT("Guardian to HeartForge"));Wait(6.1);Produce(A,[this](){FLDProductionPlan P;FString Why;Check(Mode->Acquisition->Preview(Mode->StateFor(A)->Inventory,Mode->StateFor(A)->Build,31,0,5,{},P,Why),TEXT("Actual Heartbreaker exact recipe preview: ")+Why);return P;},8);Add(TEXT("Four physical ingredients assemble Standard Heartbreaker"),1,[this](){return LDWorldDemo::Find(Mode,A,ELDItemKind::Heartbreaker).IsValid();});}
    if(Charter){Toolkit();Toolkit();Move(A,[this](){return ALDProductionDirector::Position(GetWorld(),3);},TEXT("West Supply"));Produce(A,[this](){return LDWorldDemo::Commodity(Mode,A,7);},3);Core();Move(A,[this](){return ALDProductionDirector::Position(GetWorld(),2);},TEXT("City industrial procurement"));Produce(A,[this](){return LDWorldDemo::Commodity(Mode,A,6);},3);
        Add(TEXT("Earned natural STR12 preparation /2s"),4,[this,Id=uint32(0),Seq=uint32(0)]() mutable {auto* O=Mode->StateFor(A);if(!Id){auto P=LDDevelopment::PlanFrom(O->Build);if(Mode->IsV08Profile()){P.InventoryRevision=O->Inventory.Revision;P.Bindings=O->Inventory.Bindings;}++P.Attributes[0];Id=++Token;Mode->Development->Queue(A,O->Match,O->Life,Id,LDDevelopmentTransaction::Prepare,ELDSkill::None,{},P);}Mode->Development->RefreshHold(A,O->Life,Id,++Seq,false);return O->Build.Attributes[0]>=12&&!O->DevelopmentKind;});
        Move(A,[this](){for(TActorIterator<ALDCharterTask> It(GetWorld());It;++It){if(It->Service==0){return It->GetActorLocation();}}return FVector(100000,0,0);},TEXT("Actual equipped Heave service"));Add(TEXT("Equipped Witness channel with natural gate"),8,[this,Id=uint32(0),Seq=uint32(0)]() mutable {auto* O=Mode->StateFor(A);if(!Id){for(TActorIterator<ALDCharterTask> It(GetWorld());It;++It){if(It->Service==0){FString Why;Id=++Token;Check(Mode->Combat->RequestEquipped(LDWorldDemo::Body(A),O->Build.Revision,uint8(O->Build.Slots.Find(ELDSkill::Heave)),Id,FVector::ForwardVector,Why,*It),TEXT("Normal equipped Heave accepted: ")+Why);break;}}}Mode->Combat->RefreshSkillHold(LDWorldDemo::Body(A),Id,++Seq,false);return O->Witness.IsValid();});Move(A,[this](){return LDRegion::Site(GetWorld(),1);},TEXT("Earned Treasury checkout"));Wait(6.1);Add(TEXT("100g registered Charter checkout with both earned endorsements"),2,[this,Begun=false]() mutable {auto* O=Mode->StateFor(A);if(!Begun){Begun=true;A->NextRequest=Token;A->OpenBank();Token=FMath::Max(Token,A->NextRequest);const auto Q=A->BankView;Mode->Bank->Queue(A,O->Match,O->Life,++Token,Q.Session,100,false,Q.WalletRevision,Q.BankRevision,true);}if(!O->CharterPurchase.IsValid()){return false;}A->CloseBank();if(A->InventoryUI&&A->InventoryUI->IsOpen()){A->InventoryUI->Close();}Token=FMath::Max(Token,A->NextRequest);return true;});Produce(A,[this](){return LDWorldDemo::Commodity(Mode,A,8);},4);Move(B,[this](){return LDRegion::Site(GetWorld(),3);},TEXT("Public nonowner response City to Heart"));Add(TEXT("Public warning elapsed, rival has no Charter registration"),35,[this](){return GetWorld()->GetTimeSeconds()>=Mode->GetGameState<ALDGameState>()->CharterOpenAt&&!Mode->StateFor(B)->CharterPurchase.IsValid();});Hold(B,3,10);}
    else{if(Keys||Artifact){Move(A,[this](){return Mode->WorldRuntime->Position(TEXT("HeartAntechamber"));},TEXT("Ruin / forge to antechamber"));Wait(6.1);Hold(A,Artifact?11:10,Artifact?6:4);Add(TEXT("Public30s warning, owner60s lease and source retention/consume"),1,[this,Artifact](){const auto* O=Mode->StateFor(A);return O->BreachRoute==(Artifact?6:5)&&FMath::IsNearlyEqual(O->BreachCloseAt-O->BreachOpenAt,60.)&&(Artifact?!LDWorldDemo::Find(Mode,A,ELDItemKind::Heartbreaker).IsValid():LDWorldDemo::Find(Mode,A,ELDItemKind::AncientKeyA).IsValid());});}
        Move(A,[this](){return LDRegion::Site(GetWorld(),3);},TEXT("Antechamber / shrine to claim circle"));if(!Siege){Add(TEXT("Owner warning elapsed through native time"),35,[this](){return GetWorld()->GetTimeSeconds()>=Mode->StateFor(A)->BreachOpenAt;});}
        if(Keys&&Mode->IsV08Profile())
        {
            Move(B,[this](){return LDRegion::Site(GetWorld(),3)+FVector(0,190,0);},TEXT("Level-one defender responds to public breach"));
            Add(TEXT("Level-one paid Counterpulse interrupts live Heart; no damage, grants or progression setters"),7,[this,Claim=uint32(0),Seq=uint32(0),Cast=false,MP=0.]() mutable {
                auto* O=Mode->StateFor(A);auto* D=Mode->StateFor(B);auto* C=Mode->Combat.Get();const double Now=GetWorld()->GetTimeSeconds();
                if(!Claim){Claim=++Token;Mode->Objectives->Queue(A,O->Match,O->Life,Claim,3,{});return false;}
                Mode->Objectives->RefreshHold(A,O->Life,Claim,++Seq,false,Now);
                if(!Cast&&Now-StepAt>1.5&&Mode->Objectives->HasChannel(A))
                {
                    const FGuid Seen=LDWorldDemo::Observe(Mode,B,LDWorldDemo::Body(A));if(!Seen.IsValid()){return false;}
                    FString Why;MP=C->StateFor(LDWorldDemo::Body(B))->Tactical.MP;
                    Cast=C->RequestEquipped(LDWorldDemo::Body(B),D->Build.Revision,uint8(D->Build.Slots.Find(ELDSkill::Counterpulse)),++Token,(LDWorldDemo::Body(A)->GetActorLocation()-LDWorldDemo::Body(B)->GetActorLocation()).GetSafeNormal2D(),Why,LDWorldDemo::Body(A));
                    Check(Cast,TEXT("Observed starter Counterpulse accepted: ")+Why);
                }
                if(!Cast||Now-StepAt<2.3||Mode->Objectives->HasChannel(A)){return false;}
                Check(D->Build.Level==1&&D->Xp==0&&D->Gold==0&&D->Inventory.Items.IsEmpty(),TEXT("Defender uses unchanged level-one kit, zero wallet and no equipment"));
                Check(C->StateFor(LDWorldDemo::Body(B))->Tactical.MP<MP-25&&D->Build.Cooldowns[uint8(ELDSkill::Counterpulse)]>Now,TEXT("Interrupt pays live32MP and real cooldown"));
                Check(!Mode->IsFinished()&&O->bActive&&Mode->StateFor(A)->BreachRoute==5,TEXT("Actual interrupt clears claim while earned key authorization survives"));
                Mode->Objectives->Release(A,O->Life,Claim);return true;
            });
        }
        Hold(A,3,10);}
    Add(TEXT("Single terminal winner and finite-source ledger reconcile"),1,[this,Charter](){FString Why;const auto* G=Mode->GetGameState<ALDGameState>();return G->EndReason==1&&G->ResultSerial==1&&Mode->StateFor(Charter?B:A)->bWinner&&Mode->Economy->Audit(Why)&&Mode->Economy->AuditFailures==0;});
}
void ALDWorldProbe::Finish()
{
    if(!Done&&!ALDB90ClosureProbe::LegacyCompleted(GetWorld())) { Check(false,TEXT("B90-to-v08 profile isolation")); }
    if(Done){return;}Done=true;auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("failures"),Failures);R->SetStringField(TEXT("route"),Route);R->SetNumberField(TEXT("schema"),Mode?Mode->RulesVersion():0);R->SetStringField(TEXT("catalogDigest"),Mode&&Mode->StateFor(A)?Mode->StateFor(A)->Build.CatalogDigest:TEXT(""));R->SetNumberField(TEXT("seconds"),GetWorld()->GetTimeSeconds());TArray<TSharedPtr<FJsonValue>> Values;for(const auto& S:Checks){Values.Add(MakeShared<FJsonValueString>(S));}R->SetArrayField(TEXT("checks"),Values);R->SetArrayField(TEXT("travel"),Travel);R->SetStringField(TEXT("scope"),Route==TEXT("Boundary")||Route.StartsWith(TEXT("Network"))?TEXT("Explicit boundary or dedicated-server/two-client fixtures; labelled physical sources and placement. Never normal route evidence; remote channels and public/owner observations are real."):TEXT("Two native local contenders, benchmark fixed delta20Hz. Normal native navigation/movement/AI/combat/services and elapsed quiet/protection/holds. No teleports, gold/XP/items/HP/gates/endpoints setters in this lane. Automated commands, not human/remote/package acceptance."));FString Path,Json;FParse::Value(FCommandLine::Get(),TEXT("LDProbeReport="),Path);FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*Path);if(Mode&&Mode->Economy){FFileHelper::SaveStringToFile(Mode->Economy->Accounting().ExportJson(FName(*((Mode->IsV08Profile()?TEXT("B85"):TEXT("B76"))+Route))),*(FPaths::GetPath(Path)/TEXT("Ledger.json")));}FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
