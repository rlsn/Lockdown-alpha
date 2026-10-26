#include "Tests/LDB86Probe.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDTacticalCatalog.h"
#include "Development/LDDevelopmentDirector.h"
#include "Development/LDV08Loadout.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Economy/LDEconomyDirector.h"
#include "Economy/LDProductionDirector.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDRegionDefinition.h"
#include "UI/LDHUD.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "UI/LDHUDModel.h"
#include "UI/LDControlSettings.h"
#include "UI/LDInventoryPanel.h"
#include "UI/LDControlsPanel.h"
#include "UI/LDDevelopmentPanel.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Engine/NetConnection.h"
#include "EngineUtils.h"
#include "InputKeyEventArgs.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformMemory.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace
{
double B86Percentile(TArray<double> Values,double P)
{if(Values.IsEmpty()){return 0;}Values.Sort();return Values[FMath::Clamp(FMath::CeilToInt(Values.Num()*P)-1,0,Values.Num()-1)];}
}
ALDB86Probe::ALDB86Probe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDB86Probe::BeginPlay()
{
    Super::BeginPlay();Started=At=LastFrame=FPlatformTime::Seconds();FParse::Value(FCommandLine::Get(),TEXT("LDProbeRole="),Role);FParse::Value(FCommandLine::Get(),TEXT("LDProbeReportDir="),Directory);FParse::Value(FCommandLine::Get(),TEXT("LDB86Seconds="),Seconds);Seconds=FMath::Clamp(Seconds,60.,2100.);Visual=Role==TEXT("Visual");
    if(GetNetMode()==NM_Client){CastChecked<ALDPlayerController>(GetOwner())->ServerProbeHello(uint8(Role[0]-'A'+1));}
}
void ALDB86Probe::Check(bool Value,const FString& Text)
{Failures+=!Value;Checks.Add((Value?TEXT("PASS "):TEXT("FAIL "))+Text);UE_LOG(LogTemp,Display,TEXT("LD_B86 %s"),*Checks.Last());}
void ALDB86Probe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(Done){return;}if(FPlatformTime::Seconds()-Started>Seconds+210){Check(false,TEXT("B86 timeout"));Finish();return;}
    if(Visual){VisualTick();}else if(GetNetMode()==NM_Client){ClientTick();}else{ServerTick();}
#endif
}
void ALDB86Probe::Key(FKey Input,bool Down)
{
    auto* PC=GetNetMode()==NM_Client?CastChecked<ALDPlayerController>(GetOwner()):CastChecked<ALDPlayerController>(GetWorld()->GetFirstPlayerController());
    PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),Input,Down?IE_Pressed:IE_Released,Down?1.f:0.f,false,FPlatformTime::Cycles64()));
}
void ALDB86Probe::FillFields(bool Expiry)
{
    const double WriteAt=FPlatformTime::Seconds();auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* C=M->Combat.Get();const int64 Now=LDTactical::Milliseconds(GetWorld()->GetTimeSeconds());bool Good=true;
    for(int32 OwnerIndex=0;OwnerIndex<4;++OwnerIndex){auto* Body=CastChecked<ALDCharacter>(Owners[OwnerIndex]->GetPawn());for(int32 Chunk=0;Chunk<4;++Chunk)
    {
        auto Command=[&](LDEnvironment::EInput Input){auto Q=C->EnvironmentCommand(Body,Input,Now,Expiry&&OwnerIndex==0&&Chunk==0?500:3000);for(int32 N=0;N<16;++N){Q.Cells.Add(FIntPoint(36+N,20+OwnerIndex*4+Chunk));}return C->QueueEnvironment(Q,Now);};
        const int32 Kind=(OwnerIndex*4+Chunk)%3;
        if(Kind==1){Good&=Command(LDEnvironment::EInput::Oil);}
        Good&=Command(Kind==0?LDEnvironment::EInput::Water:Kind==1?LDEnvironment::EInput::Fire:LDEnvironment::EInput::Smoke);
    }}
    C->CommitRiver(Now);PendingWrites+=(FPlatformTime::Seconds()-WriteAt)*1000;
    ++Cycles;if(!Good){Check(false,TEXT("Diagnostic bounded field lifecycle command rejected"));Finish();}
    if(Cycles==1){Check(C->RiverCells.Num()==256&&C->HeldEnvironment()==0,TEXT("Four actual contender budgets supply256 native dynamic records"));}
}
void ALDB86Probe::ServerTick()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M||!M->Combat){return;}auto* C=M->Combat.Get();const double Wall=FPlatformTime::Seconds();
    if(!Phase)
    {
        Owners.SetNum(4);for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){auto* PC=Cast<ALDPlayerController>(It->Get());if(PC&&PC->GetPawn()&&PC->ProbeRole>=1&&PC->ProbeRole<=4){Owners[PC->ProbeRole-1]=PC;}}
        for(const auto& PC:Owners){if(!PC.IsValid()){return;}}FString Why;for(const auto& PC:Owners){M->SetLobbyChoice(PC.Get(),5,true,Why);}bool MatchStarted=false;for(const auto& PC:Owners){if(M->StartDevelopmentMatch(PC.Get(),Why)){MatchStarted=true;break;}}if(!MatchStarted){Check(false,Why);Finish();return;}
        Check(M->GetGameState<ALDGameState>()->LobbyCount==4,TEXT("Four independent owning connections occupy all four contender budgets"));
        for(int32 N=0;N<4;++N){auto* PC=Owners[N].Get();PC->ClientProbePhase(1);}
        Phase=1;At=Wall;return;
    }
    if(Phase==3){if(Wall-At>5){Finish();}return;}
    for(const auto& PC:Owners){if(!PC.IsValid()||!PC->GetPawn()){Check(false,TEXT("Contender disconnected during soak"));Finish();return;}}
    if(C->SampleSerial!=RuleSerial){RuleSerial=C->SampleSerial;PendingRules+=C->LastSampleMs;}
    if(M->Vision->SampleSerial!=ViewSerial){ViewSerial=M->Vision->SampleSerial;PendingViews+=M->Vision->LastSampleMs;}
    if(Wall-LastSample>=.1)
    {
        LastSample=Wall;
        if(Phase==1){BaselineServices.Add(PendingRules+PendingViews);}
        else if(C->RiverCells.Num()==256){++FullSamples;Rules.Add(PendingRules);Views.Add(PendingViews);Services.Add(PendingRules+PendingViews+PendingWrites);}
        PendingRules=PendingViews=PendingWrites=0;
        if(Phase==2&&C->RiverCells.Num()<=240&&C->HeldEnvironment()<=240)
        {const double ReserveAt=FPlatformTime::Seconds();const auto Lease=C->ReserveEnvironment(CastChecked<ALDCharacter>(Owners[0]->GetPawn()),16,LDTactical::Milliseconds(GetWorld()->GetTimeSeconds()),1000);if(Lease.IsValid()){++ReservationCycles;C->ReleaseEnvironment(Lease);}PendingWrites+=(FPlatformTime::Seconds()-ReserveAt)*1000;}
    }
    if(Phase==1&&Wall-At>=30){Phase=2;LastAudit=Wall-60;FillFields(true);MeasuredAt=LastFields=FPlatformTime::Seconds();for(const auto& PC:Owners){PC->ClientProbePhase(2);}return;}
    if(Phase!=2){return;}
    if(Wall-LastFields>=2){LastFields=Wall;FillFields(Cycles%5==0);}
    if(Wall-LastAudit>=60)
    {
        LastAudit=Wall;int32 Actors=0;for(TActorIterator<AActor> It(GetWorld());It;++It){++Actors;}MinActors=FMath::Min(MinActors,Actors);PeakActors=FMath::Max(PeakActors,Actors);Memory.Add(FPlatformMemory::GetStats().UsedPhysical/1048576.);
        auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("elapsed"),Wall-MeasuredAt);J->SetNumberField(TEXT("worldSeconds"),GetWorld()->GetTimeSeconds());J->SetNumberField(TEXT("records"),C->RiverCells.Num());J->SetNumberField(TEXT("actors"),Actors);J->SetNumberField(TEXT("memoryMiB"),Memory.Last());Samples.Add(MakeShared<FJsonValueObject>(J));
        UE_LOG(LogTemp,Display,TEXT("LD_B86_SOAK elapsed=%.1f cells=%d fullSamples=%d serviceP95=%.3fms"),Wall-MeasuredAt,C->RiverCells.Num(),FullSamples,B86Percentile(Services,.95));
    }
    if(Wall-MeasuredAt<Seconds){return;}
    Check(FullSamples>=Seconds*4&&Cycles>=Seconds/3&&ReservationCycles>0,TEXT("Sustained256-record samples include real expiry/reallocation and reservation release"));
    Check(PeakActors-MinActors<=128,TEXT("Authority actor population remains bounded after pool allocation"));
    bool Private=true;for(TActorIterator<ALDTacticalField> It(GetWorld());It;++It){if(It->EnvironmentPool){Private&=!It->GetIsReplicated();}}Check(Private,TEXT("Canonical environment pool never opens replicated actor channels"));
    FString Why;Check(M->Economy->Audit(Why)&&M->Economy->AuditFailures==0,TEXT("Long-session economy and physical identity audit: ")+Why);
    for(const auto& PC:Owners){PC->ClientProbePhase(3);}Phase=3;At=Wall;
}
void ALDB86Probe::ClientTick()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwner());const double Wall=FPlatformTime::Seconds();
    if(PC->ProbePhase!=Phase)
    {
        Phase=PC->ProbePhase;At=LastFrame=Wall;

        if(Phase==2){MeasuredAt=Wall;PC->ProbeRtts.Reset();if(auto* N=PC->GetNetConnection()){NetInStart=N->InTotalBytes;NetOutStart=N->OutTotalBytes;}}
        if(Phase==3){Check(OwnerVerified&&PC->OwnerState.Build.RulesVersion==8&&PC->OwnerState.Build.Actions.Num()==6&&PC->OwnerState.Inventory.Bindings.Num()==11,TEXT("Verified schema8 owner model remains current at completion"));Check(Frames.Num()>Seconds*5,TEXT("Independent client supplied sustained native frame samples"));Check(PC->OwnerState.bActive,TEXT("No hidden combat/death fixture replaces the live owner"));Check(PC->Navigation->QueryCount>=Seconds/40,TEXT("Live client repeatedly navigates through native world paths"));Finish();return;}
    }
    if(Phase==1&&!OwnerVerified)
    {
        if(PC->OwnerState.Build.RulesVersion==8&&PC->OwnerState.Build.Actions.Num()==6&&PC->OwnerState.Inventory.Bindings.Num()==11)
        {
            OwnerVerified=true;Check(true,TEXT("Independent owner receives schema8 six actions and eleven physical bindings"));
            Check(PC->Controls&&PC->Controls->Definitions().Num()==ULDControlSettings::HybridDefaults().Num(),TEXT("Native local mappings include six actions and four accessories"));
        }
        else if(Wall-At>10){Check(false,TEXT("Owner schema8 readiness timed out after reliable phase notification"));Finish();return;}
    }
    if(Phase!=2){return;}Frames.Add((Wall-LastFrame)*1000);LastFrame=Wall;
    if(Wall-LastSample>=1){LastSample=Wall;PC->ServerProbePing(Wall);}
    if(const auto* Relay=PC->LocalView();Relay&&Relay->LastView().Revision!=ViewSerial){ViewSerial=Relay->LastView().Revision;Ages.Add(FMath::Max(0.,(GetWorld()->GetGameState<ALDGameState>()->GetServerWorldTimeSeconds()-Relay->LastView().AuthoritySeconds)*1000.));}
    if(Wall-LastAudit>=60)
    {
        LastAudit=Wall;Memory.Add(FPlatformMemory::GetStats().UsedPhysical/1048576.);if(auto* N=PC->GetNetConnection()){Bytes.Add(N->InBytesPerSecond);}
        if(const auto* V=PC->LocalView())
        {Check(V->LastView().Entities.Num()<=512,TEXT("Authorized view remains within512 bounded observed tuples"));FLDObservedEntity Other;Other.Kind=ELDObservedKind::Body;Check(LDHUD::Inspector(PC->OwnerState,&Other,0).Contains(TEXT("Unknown")),TEXT("Enemy inspector still withholds private build and inventory"));}
    }
    // Ordinary native movement, not actor placement; destinations are public City positions.
    if(Wall-LastFields>=20&&PC->CanWorldCommand()){LastFields=Wall;PC->Navigation->Navigate(LDRegion::Site(GetWorld(),1)+FVector((int32((Wall-MeasuredAt)/20)%2)?100:-100,30*(Role[0]-'A'),0));}
}
void ALDB86Probe::Capture(const TCHAR* Name)
{FScreenshotRequest::RequestScreenshot(Directory/(FString(Name)+TEXT(".png")),true,false);}
void ALDB86Probe::VisualTick()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* PC=Cast<ALDPlayerController>(GetWorld()->GetFirstPlayerController());if(!PC||!PC->GetPawn()||!PC->HUDUI){return;}const double Wall=FPlatformTime::Seconds();auto* O=M->StateFor(PC);auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());
    if(!Phase)
    {
        if(Wall-Started<3){return;}FString Why;M->SetLobbyChoice(PC,5,true,Why);Check(M->StartDevelopmentMatch(PC,Why),TEXT("Native v08 visual practice starts"));PC->SetMenuOpen(false);
        Body->SetActorLocation(ALDProductionDirector::Position(GetWorld(),2)+FVector(0,40,92));Body->GetCharacterMovement()->StopMovementImmediately();O->ProtectedUntil=0;auto* S=M->Combat->StateFor(Body);S->ProtectionUntil=0;S->LastDamage=S->LastDealt=GetWorld()->GetTimeSeconds()-20;
        O->Build.Attributes.Init(16,6);const TArray<ELDSkill> Skills={ELDSkill::Frostline,ELDSkill::Counterpulse,ELDSkill::WaterJet,ELDSkill::Gust,ELDSkill::ScoutingPulse};for(auto K:Skills){O->Build.Ranks[uint8(K)]=1;}M->RefreshBuild(PC,true);
        TArray<FGuid> IDs;for(auto Kind:{ELDItemKind::ReflectiveMail,ELDItemKind::WaterFlask,ELDItemKind::ResonanceRing,ELDItemKind::PurityCharm,ELDItemKind::FrostPrism}){auto* I=GetWorld()->SpawnActor<ALDItem>();I->Kind=Kind;Check(M->Inventory->Adopt(PC,I,TEXT("DiagnosticItemCreation")),TEXT("Labelled visual fixture owns one physical source"));IDs.Add(I->Identity);}
        auto P=LDDevelopment::PlanFrom(O->Build);P.InventoryRevision=O->Inventory.Revision;P.Bindings=O->Inventory.Bindings;P.Bindings[1]=IDs[0];for(int32 N=0;N<4;++N){P.Bindings[7+N]=IDs[N+1];}for(int32 N=0;N<5;++N){P.Slots[N]=Skills[N];P.Actions[N]=FLDActionRef();P.Actions[N].Skill=Skills[N];}P.Slots[5]=ELDSkill::None;P.Actions[5]=FLDActionRef();P.Actions[5].Source=ELDActionSource::Gear;P.Actions[5].Instance=IDs[0];P.Actions[5].Definition=TEXT("reflective_mail");
        const FString PreferencesSlot=TEXT("LDB86_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);PC->Controls=ULDControlSettings::Load(PreferencesSlot,8);if(FParse::Param(FCommandLine::Get(),TEXT("LDB86LargeText"))){PC->Controls->TextScale=1.25;}Check(PC->Controls->Rebind(TEXT("Skill1"),EKeys::F6,Why),TEXT("Native action remap accepts unused F6"));const auto Before=PC->Controls->Key(TEXT("Skill2"));Check(!PC->Controls->Rebind(TEXT("Skill2"),EKeys::F6,Why)&&PC->Controls->Key(TEXT("Skill2"))==Before,TEXT("Binding collision rejects without overwriting another action"));Check(PC->Controls->Save()&&ULDControlSettings::Load(PreferencesSlot,8)->Key(TEXT("Skill1"))==EKeys::F6,TEXT("Isolated v08 preferences persist remapped native action"));UGameplayStatics::DeleteGameInSlot(PreferencesSlot+TEXT("_V08"),0);PC->RebuildV05Mappings();FTimerHandle Delay;GetWorld()->GetTimerManager().SetTimer(Delay,FTimerDelegate::CreateWeakLambda(this,[this,M,PC,P](){auto* Current=M->StateFor(PC);M->Development->Queue(PC,Current->Match,Current->Life,++Token,LDDevelopmentTransaction::Prepare,ELDSkill::None,{},P);Sent=true;}),1.f,false);Phase=1;At=Wall;return;
    }
    if(Phase==1)
    {
        M->Development->RefreshHold(PC,O->Life,Token,++Sequence,false);
        if(!Sent||Wall-At<3||O->DevelopmentKind){if(Wall-At>20){Check(false,TEXT("Held preparation did not finish: ")+PC->IntentFeedback);Finish();}return;}FString Why;Check(LDV08::Validate(O->Inventory,O->Build,Why),TEXT("Actual held preparation atomically binds seven-body/four-accessory model: ")+Why);Check(O->Build.Actions[5].Source==ELDActionSource::Gear,TEXT("Body active occupies shared slot6: ")+PC->IntentFeedback);if(O->Build.Actions[5].Source!=ELDActionSource::Gear){Finish();return;}PC->HUDUI->RefreshReadModel();Phase=2;At=Wall;return;
    }
    if(Wall-At<2){return;}
    if(Phase==2)
    {
        Check(PC->HUDUI->CommandText().Contains(TEXT("[ITEM]"))&&PC->HUDUI->CommandText().Contains(TEXT("Passive")),TEXT("Native HUD distinguishes item-sourced action and passive accessory"));Check(PC->HUDUI->CommandLayoutFits(),TEXT("Six shared/four accessory targets fit native viewport without overlap"));Capture(TEXT("HUD"));Key(EKeys::F6,true);Phase=3;At=Wall;return;
    }
    if(Phase==3){Check(PC->ArmedSlot()==0,TEXT("Remapped native Enhanced Input F6 arms learned slot1"));Key(EKeys::F6,false);PC->ClearControls();Phase=4;At=Wall;return;}
    if(Phase==4){Key(EKeys::I,true);Key(EKeys::I,false);Phase=5;At=Wall;return;}
    if(Phase==5){Check(PC->InventoryUI&&PC->InventoryUI->IsOpen(),TEXT("Native inventory input opens physical gear panel"));Capture(TEXT("Inventory"));Phase=6;At=Wall;return;}
    if(Phase==6){FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));Check(!PC->InventoryUI->IsOpen()&&PC->ArmedSlot()==-1,TEXT("Escape closes major panel without leaking world targeting"));PC->ToggleDevelopmentPanel();Phase=7;At=Wall;return;}
    if(Phase==7){Check(PC->DevelopmentUI&&PC->DevelopmentUI->IsOpen(),TEXT("Native Character panel exposes shared-source preparation"));Capture(TEXT("Character"));Phase=8;At=Wall;return;}
    if(Phase==8){if(!PC->DevelopmentUI){Check(false,TEXT("Missing Character panel"));Finish();return;}PC->DevelopmentUI->Close();PC->ToggleControlsPanel();Phase=9;At=Wall;return;}
    if(Phase==9){Check(PC->ControlsUI&&PC->ControlsUI->IsOpen(),TEXT("Native controls panel exposes all current mappings"));Capture(TEXT("Controls"));Phase=10;At=Wall;return;}
    if(Phase==10){for(const TCHAR* Name:{TEXT("HUD"),TEXT("Inventory"),TEXT("Character"),TEXT("Controls")}){Check(IFileManager::Get().FileSize(*(Directory/(FString(Name)+TEXT(".png"))))>0,TEXT("Native rendered screenshot: ")+FString(Name));}Finish();}
}
void ALDB86Probe::Finish()
{
    if(Done){return;}Done=true;auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("failures"),Failures);J->SetStringField(TEXT("role"),Role);if(Visual){int32 Width=0,Height=0;GetWorld()->GetFirstPlayerController()->GetViewportSize(Width,Height);J->SetNumberField(TEXT("viewportWidth"),Width);J->SetNumberField(TEXT("viewportHeight"),Height);J->SetNumberField(TEXT("uiScale"),UWidgetLayoutLibrary::GetViewportScale(this));}J->SetBoolField(TEXT("cooked"),FPlatformProperties::RequiresCookedData());J->SetStringField(TEXT("scope"),Visual?TEXT("Native rendered UMG and Enhanced Input engineering fixture; diagnostic knowledge/stats/items/placement, actual held preparation. No human or OS input claim."):TEXT("Four independent native clients, actual contender budgets; diagnostic256-record mixed Water/Burning/Smoke commands with expiry/reservations, normal clocks/movement/replication. Sum of observed whole authority/view samples plus diagnostic command costs in approximately100ms wall windows; transport/rendering recorded separately. Not isolated added environment cost."));
    TArray<TSharedPtr<FJsonValue>> Values;for(const auto& S:Checks){Values.Add(MakeShared<FJsonValueString>(S));}J->SetArrayField(TEXT("checks"),Values);J->SetArrayField(TEXT("minuteSamples"),Samples);J->SetNumberField(TEXT("durationSeconds"),MeasuredAt>0?FPlatformTime::Seconds()-MeasuredAt:0);J->SetNumberField(TEXT("full256Samples"),FullSamples);J->SetNumberField(TEXT("fieldCycles"),Cycles);J->SetNumberField(TEXT("reservationCycles"),ReservationCycles);J->SetNumberField(TEXT("baselineServiceP95Ms"),B86Percentile(BaselineServices,.95));J->SetNumberField(TEXT("authorityP95Ms"),B86Percentile(Rules,.95));J->SetNumberField(TEXT("viewP95Ms"),B86Percentile(Views,.95));J->SetNumberField(TEXT("serviceP95Ms"),B86Percentile(Services,.95));J->SetNumberField(TEXT("serviceP99Ms"),B86Percentile(Services,.99));J->SetNumberField(TEXT("serviceMaxMs"),B86Percentile(Services,1));J->SetNumberField(TEXT("frameP95Ms"),B86Percentile(Frames,.95));J->SetNumberField(TEXT("frameP99Ms"),B86Percentile(Frames,.99));J->SetNumberField(TEXT("frameMaxMs"),B86Percentile(Frames,1));J->SetNumberField(TEXT("meanFPS"),MeasuredAt>0?Frames.Num()/(FPlatformTime::Seconds()-MeasuredAt):0);J->SetNumberField(TEXT("memoryMinMiB"),B86Percentile(Memory,0));J->SetNumberField(TEXT("memoryMaxMiB"),B86Percentile(Memory,1));
    if(auto* PC=Cast<ALDPlayerController>(GetOwner())){RTT=PC->ProbeRtts;J->SetNumberField(TEXT("nativePathQueries"),PC->Navigation->QueryCount);if(auto* N=PC->GetNetConnection()){J->SetNumberField(TEXT("networkInBytes"),N->InTotalBytes-NetInStart);J->SetNumberField(TEXT("networkOutBytes"),N->OutTotalBytes-NetOutStart);}}
    auto Raw=[&](const TCHAR* Name,const TArray<double>& Data){TArray<TSharedPtr<FJsonValue>> Values;for(double Value:Data){Values.Add(MakeShared<FJsonValueNumber>(Value));}J->SetArrayField(Name,Values);};
    J->SetNumberField(TEXT("roundTripP95Ms"),B86Percentile(RTT,.95));J->SetNumberField(TEXT("snapshotAgeP95Ms"),B86Percentile(Ages,.95));J->SetNumberField(TEXT("snapshotAgeMaxMs"),B86Percentile(Ages,1));Raw(TEXT("roundTripsMs"),RTT);Raw(TEXT("snapshotAgesMs"),Ages);Raw(TEXT("frameIntervalsMs"),Frames);Raw(TEXT("serviceWindowsMs"),Services);Raw(TEXT("authorityWindowsMs"),Rules);Raw(TEXT("viewWindowsMs"),Views);Raw(TEXT("baselineWindowsMs"),BaselineServices);
    FString Text;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Directory/(Role+TEXT(".json"))));FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
