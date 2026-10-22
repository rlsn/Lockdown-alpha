#include "Tests/LDDevelopmentProbe.h"
#include "Development/LDCharacterBuild.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameInstance.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "Vision/LDVisionDirector.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Inventory/LDInventoryDirector.h"
#include "UI/LDInventoryPanel.h"
#include "Development/LDDevelopmentDirector.h"
#include "UI/LDDevelopmentPanel.h"
#include "UI/LDControlSettings.h"
#include "UI/LDControlsPanel.h"
#include "UI/LDHUD.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "Components/EditableTextBox.h"
#include "Blueprint/WidgetTree.h"
#include "Framework/Application/SlateApplication.h"
#include "Objectives/LDObjectiveRules.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Development/LDWorldChecks.h"
#include "Region/LDRegionDefinition.h"
#include "Region/LDRegionDirector.h"

ALDDevelopmentProbe::ALDDevelopmentProbe() { bReplicates=false; PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PostUpdateWork; }
void ALDDevelopmentProbe::BeginPlay()
{
    Super::BeginPlay(); Started=PhaseAt=FPlatformTime::Seconds();
    FParse::Value(FCommandLine::Get(),TEXT("LDProbeRole="),Role);
    if (Role.IsEmpty()) { Role=TEXT("Local"); }
    if (GetNetMode()==NM_Client) { if (auto* PC=Cast<ALDPlayerController>(GetOwner())) { PC->ServerProbeHello(Role==TEXT("A")?1:2); } }
}
void ALDDevelopmentProbe::Check(bool Pass,const FString& Description)
{
    Checks.Add((Pass?TEXT("PASS "):TEXT("FAIL "))+Description); if (!Pass) { ++Failures; }
    UE_LOG(LogTemp,Display,TEXT("LD_V04_PROBE %s"),*Checks.Last());
}
namespace
{
    void AuditPanelFocus(ULDPanel* Panel,TFunction<void(bool,const FString&)> Check)
    {
        if(!Panel||!Panel->IsVisible()){return;}auto Targets=Panel->PanelFocusTargets();
        Check(Targets.Num()>=2,TEXT("Major panel exposes reachable enabled controls in one focus scope"));if(Targets.Num()<2){return;}
        bool Sized=true;for(auto* W:Targets){const auto Cached=W->GetCachedGeometry().GetLocalSize();const auto Size=Cached.IsNearlyZero()?W->GetDesiredSize():Cached;Sized&=Size.X>=39.5&&Size.Y>=39.5;}Check(Sized,TEXT("Visible panel targets measure 40 by 40 logical units; unpainted scrolled controls request at least that size"));
        auto Focused=[](const UWidget* W){return W->HasKeyboardFocus()||W->HasFocusedDescendants();};
        auto Key=[](FKey K,bool Shift=false){FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(K,FModifierKeysState(Shift,false,false,false,false,false,false,false,false),0,false,0,0));FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(K,FModifierKeysState(),0,false,0,0));};
        Targets[0]->SetKeyboardFocus();bool Ordered=Focused(Targets[0]);
        for(int32 I=1;I<=Targets.Num();++I){Key(EKeys::Tab);Ordered&=Focused(Targets[I%Targets.Num()]);}
        Check(Ordered,TEXT("Native Slate Tab visits each visible enabled control and wraps within this panel"));
        Key(EKeys::Tab,true);Check(Focused(Targets.Last()),TEXT("Native Slate Shift-Tab reverses and wraps in the same focus scope"));
        Targets[0]->SetKeyboardFocus();TArray<UWidget*> All;Panel->WidgetTree->GetAllWidgets(All);for(auto* W:All){if(auto* Scroll=Cast<UScrollBox>(W)){Scroll->ScrollWidgetIntoView(nullptr,false);Scroll->ScrollToStart();}}
    }
}
void ALDDevelopmentProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if (FPlatformTime::Seconds()-Started>(FParse::Param(FCommandLine::Get(),TEXT("LDReleaseSession"))?1620:FParse::Param(FCommandLine::Get(),TEXT("LDRegionNormal"))?620:FParse::Param(FCommandLine::Get(),TEXT("LDPreparationNormal"))?300:150)) { Check(false,TEXT("Probe timed out")); Finish(); return; }
    if (Role==TEXT("Visual"))
    {
        FString Directory; FParse::Value(FCommandLine::Get(),TEXT("LDProbeReportDir="),Directory);
        const bool ControlsVisual=FParse::Param(FCommandLine::Get(),TEXT("LDControlsVisual"));
        FString HUDScene;FParse::Value(FCommandLine::Get(),TEXT("LDHUDScene="),HUDScene);
        const bool SkillVisual=FParse::Param(FCommandLine::Get(),TEXT("LDSkillProbe"));
        const bool PrepVisual=FParse::Param(FCommandLine::Get(),TEXT("LDPreparationProbe"));
        const bool RegionVisual=FParse::Param(FCommandLine::Get(),TEXT("LDRegionProbe"));
        const bool WorldVisual=FParse::Param(FCommandLine::Get(),TEXT("LDWorldCheckProbe"));
        const FString Image=FPaths::Combine(Directory,!HUDScene.IsEmpty()?HUDScene+TEXT(".png"):ControlsVisual?TEXT("Controls.png"):RegionVisual?TEXT("Region.png"):WorldVisual?TEXT("WorldChecks.png"):PrepVisual?TEXT("Preparation.png"):SkillVisual?TEXT("SkillsHUD.png"):TEXT("Lobby.png"));
        if (!Phase && GetWorld()->GetTimeSeconds()>2)
        {
            auto* PC=CastChecked<ALDPlayerController>(GetWorld()->GetFirstPlayerController()); PC->ChooseLobbyClass(1);
            if(FParse::Param(FCommandLine::Get(),TEXT("LDHUDLargeText"))&&PC->Controls){PC->Controls->TextScale=1.25f;}
            if(RegionVisual&&HUDScene!=TEXT("Lobby")){PC->ToggleLobbyReady();PC->StartLobby();PC->GetPawn()->SetActorLocation(LDRegion::Site(GetWorld(),1)+FVector(0,210,92));GetWorld()->GetAuthGameMode<ALDGameMode>()->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()))->ProtectionUntil=0;}
            if(FParse::Param(FCommandLine::Get(),TEXT("LDSealProbe")))
            {auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M->StateFor(PC);O->Gold=100;O->Commissions=4;O->PaidJobs=4;O->Witness=FGuid::NewGuid();O->ProtectedUntil=0;PC->GetPawn()->SetActorLocation(LDRegion::Site(GetWorld(),1)+FVector(0,50,92));M->SyncOwner(PC);PC->SetFabricationHeld(true);}
            if(WorldVisual){PC->ToggleLobbyReady();PC->StartLobby();PC->GetPawn()->SetActorLocation(LDCharter::Position(3)+FVector(0,50,92));GetWorld()->GetAuthGameMode<ALDGameMode>()->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()))->ProtectionUntil=0;}
            if(SkillVisual)
            {
                PC->ToggleLobbyReady();PC->StartLobby();auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=Mode->StateFor(PC);
                O->Build.Ranks[1]=O->Build.Ranks[3]=O->Build.Ranks[4]=1;O->Build.Slots={ELDSkill::Spark,ELDSkill::Patch,ELDSkill::Step,ELDSkill::Survey};++O->Build.Revision;Mode->SyncOwner(PC);
            }
            if(PrepVisual)
            {
                PC->ToggleLobbyReady();PC->StartLobby();auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=Mode->StateFor(PC);
                Mode->AwardExperience(PC,270);O->Gold=160;PC->GetPawn()->SetActorLocation(LDObjective::Position(LDObjective::Guild)+FVector(0,50,92));Mode->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()))->ProtectionUntil=0;Mode->SyncOwner(PC);
                PC->BuyBook(ELDSkill::Spark);PC->BuyBook(ELDSkill::Patch);
            }
            Phase=1; PhaseAt=FPlatformTime::Seconds();
        }
        else if(Phase==1&&FPlatformTime::Seconds()-PhaseAt>.6&&!HUDScene.IsEmpty()&&HUDScene!=TEXT("HUD")&&HUDScene!=TEXT("Controls")&&HUDScene!=TEXT("Lobby"))
        {
            auto* PC=CastChecked<ALDPlayerController>(GetWorld()->GetFirstPlayerController());auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();
            if(HUDScene==TEXT("Development")){PC->ToggleDevelopmentPanel();}
            if(HUDScene==TEXT("Selected")){if(const auto* R=PC->LocalView()){const FLDObservedEntity* Best=nullptr;double Distance=DBL_MAX;for(const auto& V:R->LastView().Entities){const double Next=FVector::DistSquared2D(V.Position,PC->GetPawn()->GetActorLocation());if(V.Kind==ELDObservedKind::Body&&Next<Distance){Best=&V;Distance=Next;}}if(Best){PC->Inspect(Best->Id);}}}
            if(HUDScene.StartsWith(TEXT("Inventory"))||HUDScene==TEXT("QuickItems")||HUDScene==TEXT("ItemReading")||HUDScene==TEXT("DropConfirm"))
            {
                auto* O=M->StateFor(PC);const ELDSkill Stock[]={ELDSkill::Step,ELDSkill::Spark,ELDSkill::Patch,ELDSkill::Parley};while(O->Inventory.Items.Num()<23){auto* Book=GetWorld()->SpawnActor<ALDBook>();Book->State=0;Book->Skill=Stock[O->Inventory.Items.Num()%4];check(M->Inventory->Adopt(PC,Book));}auto* Seal=GetWorld()->SpawnActor<ALDSeal>();check(M->Inventory->Adopt(PC,Seal));O->Gold=160;
                FString Why;for(uint8 I=0;I<4;++I){LDInventory::BindQuick(O->Inventory,O->Inventory.Items[I].Id,I,Why);}M->Inventory->Changed(PC);
                if(HUDScene==TEXT("InventoryDead")){M->EndContenderLife(PC,GetWorld()->GetTimeSeconds());}
                if(HUDScene==TEXT("ItemReading")){PC->ClientLifeReceipt_Implementation(*O);PC->SetQuickItemHeld(1,true,0);}
                if(HUDScene!=TEXT("QuickItems")&&HUDScene!=TEXT("ItemReading")){PC->ToggleInventoryPanel();if(HUDScene!=TEXT("InventoryDead")){PC->InventoryUI->Select(HUDScene==TEXT("DropConfirm")?O->SealId:O->Inventory.Items[1].Id);if(HUDScene==TEXT("DropConfirm")){PC->InventoryUI->AskDrop();}}}
            }
            if(HUDScene.StartsWith(TEXT("Vendor")))
            {
                auto* O=M->StateFor(PC);PC->GetPawn()->SetActorLocation(LDRegion::Site(GetWorld(),1)+FVector(0,80,92),false,nullptr,ETeleportType::TeleportPhysics);M->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()))->ProtectionUntil=0;O->ProtectedUntil=0;O->Gold=80;O->Build.Ranks[uint8(ELDSkill::Spark)]=1;++O->Build.Revision;
                while(O->Inventory.Items.Num()<(HUDScene==TEXT("VendorFull")?24:5)){auto* Book=GetWorld()->SpawnActor<ALDBook>();Book->Skill=ELDSkill::Patch;Book->State=0;check(M->Inventory->Adopt(PC,Book));}M->SyncOwner(PC);PC->ClientLifeReceipt_Implementation(*O);PC->OpenVendor();PC->InventoryUI->SelectOffer(ELDSkill::Spark);
                if(HUDScene==TEXT("VendorFailed")){PC->BuyVendorBook(ELDSkill::Spark,0,40);}
            }
            if(HUDScene==TEXT("Death")){M->EndContenderLife(PC,GetWorld()->GetTimeSeconds());}
            if(HUDScene==TEXT("Result")){GetWorld()->GetGameState<ALDGameState>()->MatchDeadline=float(GetWorld()->GetTimeSeconds()+.05);}
            Phase=3;PhaseAt=FPlatformTime::Seconds();
        }
        else if (Phase==1&&FPlatformTime::Seconds()-PhaseAt>.6&&ControlsVisual)
        {
            auto* PC=CastChecked<ALDPlayerController>(GetWorld()->GetFirstPlayerController());PC->ToggleControlsPanel();
            Phase=3;PhaseAt=FPlatformTime::Seconds();
        }
        else if (Phase==1&&FPlatformTime::Seconds()-PhaseAt>.6&&PrepVisual)
        {
            auto* PC=CastChecked<ALDPlayerController>(GetWorld()->GetFirstPlayerController());PC->ToggleDevelopmentPanel();
            if(PC->DevelopmentUI&&PC->DevelopmentUI->IsOpen()){PC->DevelopmentUI->Choose(1,3);PC->DevelopmentUI->Choose(1,2);PC->DevelopmentUI->Choose(2,1);PC->DevelopmentUI->Choose(3,2);PC->DevelopmentUI->Choose(6,1);PC->DevelopmentUI->Choose(7,1);}
            Phase=3;PhaseAt=FPlatformTime::Seconds();
        }
        else if ((Phase==1||Phase==3)&&FPlatformTime::Seconds()-PhaseAt>1) { if(HUDScene.StartsWith(TEXT("Vendor"))){const auto* PC=CastChecked<ALDPlayerController>(GetWorld()->GetFirstPlayerController());Check(PC->VendorQuote.Offers.Num()==4&&PC->VendorIsOpen()&&PC->InventoryUI->VendorText().Contains(HUDScene==TEXT("VendorFailed")?TEXT("Catalog updated"):TEXT("Already learned")),TEXT("Native vendor capture uses an active authority quote and actual own state"));}if(HUDScene==TEXT("ItemReading")){const auto* PC=CastChecked<ALDPlayerController>(GetWorld()->GetFirstPlayerController());Check(PC->OwnerState.DevelopmentKind==2&&PC->HeldInputLabel()==PC->BindingLabel(TEXT("Item2")),TEXT("Rendered reading uses the real active channel and initiating item mapping"));}auto* FocusPC=CastChecked<ALDPlayerController>(GetWorld()->GetFirstPlayerController());ULDPanel* Panel=FocusPC->InventoryUI&&FocusPC->InventoryUI->IsOpen()?static_cast<ULDPanel*>(FocusPC->InventoryUI.Get()):FocusPC->ControlsUI&&FocusPC->ControlsUI->IsOpen()?static_cast<ULDPanel*>(FocusPC->ControlsUI.Get()):FocusPC->DevelopmentUI&&FocusPC->DevelopmentUI->IsOpen()?static_cast<ULDPanel*>(FocusPC->DevelopmentUI.Get()):nullptr;
            if(!Panel&&HUDScene!=TEXT("Lobby")&&HUDScene!=TEXT("Result")&&FocusPC->HUDUI){Check(FocusPC->HUDUI->CommandLayoutFits(),TEXT("Native own commands and hold progress fit the HUD band without overlapping action targets"));}
            AuditPanelFocus(Panel,[this](bool Pass,const FString& Label){Check(Pass,Label);});
            if(HUDScene==TEXT("Inventory")){auto* UI=FocusPC->InventoryUI.Get();auto Targets=UI->PanelFocusTargets();auto* Field=Targets.IsEmpty()?nullptr:Cast<UEditableTextBox>(Targets[0]);if(Field){Field->SetKeyboardFocus();FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(FocusPC->BindingKey(TEXT("Inventory")),FModifierKeysState(),0,false,0,0));Check(UI->IsOpen(),TEXT("Inventory binding typed in a focused search field does not close the major panel"));FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(FocusPC->BindingKey(TEXT("Inventory")),FModifierKeysState(),0,false,0,0));FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));Check(!UI->IsOpen()&&FocusPC->ArmedSlot()==-1,TEXT("Native Escape closes the focused editable panel without arming world commands"));FocusPC->ToggleInventoryPanel();UI->MovePanelFocus();}}
            FScreenshotRequest::RequestScreenshot(Image,true,false); Phase=2; PhaseAt=FPlatformTime::Seconds(); }
        else if (Phase==2&&FPlatformTime::Seconds()-PhaseAt>2) { Check(IFileManager::Get().FileSize(*Image)>0,ControlsVisual?TEXT("Native controls/settings panel captured; UI engineering fixture, no human evidence"):RegionVisual?TEXT("Regional HUD/map captured (location fixture)"):WorldVisual?TEXT("Native Charter/task HUD screenshot written (location fixture)"):PrepVisual?TEXT("Native preparation and book panel screenshot written (progression fixture)"):SkillVisual?TEXT("Native mixed hotbar UI screenshot written (loadout fixture)"):TEXT("Native lobby UI screenshot written")); Finish(); }
    }
    else if (Role==TEXT("Reset"))
    {
        if (GetWorld()->GetTimeSeconds()>.75&&!UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(this)) { SetActorTickEnabled(false); ResetCheck(); }
    }
    else if(FParse::Param(FCommandLine::Get(),TEXT("LDReleaseAudit")))
    {if(GetWorld()->GetTimeSeconds()>1&&!UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(this)){SetActorTickEnabled(false);ReleaseAudit();Finish();}}
    else if(FParse::Param(FCommandLine::Get(),TEXT("LDSealProbe")))
    {
        if(FParse::Param(FCommandLine::Get(),TEXT("LDRegionNormal"))){RegionNormalTick();}
        else if(GetNetMode()==NM_Client){SealClientTick();}
        else if(Role==TEXT("Region")){if(GetWorld()->GetTimeSeconds()>1&&!UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(this)){SetActorTickEnabled(false);SealCheck();Finish();}}
        else{SealServerTick();}
    }
    else if(FParse::Param(FCommandLine::Get(),TEXT("LDRegionProbe")))
    {
        if(FParse::Param(FCommandLine::Get(),TEXT("LDRegionNormal"))){RegionNormalTick();}
        else if(GetNetMode()==NM_Client){RegionClientTick();}
        else if(Role==TEXT("Region")){if(GetWorld()->GetTimeSeconds()>1&&!UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(this)){SetActorTickEnabled(false);RegionCheck();Finish();}}
        else{RegionServerTick();}
    }
    else if(FParse::Param(FCommandLine::Get(),TEXT("LDWorldCheckProbe")))
    {
        if(FParse::Param(FCommandLine::Get(),TEXT("LDWorldCheckNormal"))){WorldNormalTick();}
        else if(GetNetMode()==NM_Client){WorldClientTick();}
        else if(Role==TEXT("WorldChecks")){if(GetWorld()->GetTimeSeconds()>.75&&!UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(this)){SetActorTickEnabled(false);WorldCheck();Finish();}}
        else{WorldServerTick();}
    }
    else if(FParse::Param(FCommandLine::Get(),TEXT("LDPreparationProbe")))
    {
        if(FParse::Param(FCommandLine::Get(),TEXT("LDPreparationNormal"))){PreparationNormalTick();}
        else if(GetNetMode()==NM_Client){PreparationClientTick();}
        else if(Role==TEXT("Preparation")){if(GetWorld()->GetTimeSeconds()>.75&&!UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(this)){SetActorTickEnabled(false);PreparationCheck();Finish();}}
        else{PreparationServerTick();}
    }
    else if (FParse::Param(FCommandLine::Get(),TEXT("LDSkillProbe")))
    {
        if(GetNetMode()==NM_Client){SkillClientTick();}
        else if(Role==TEXT("Skills"))
        {if(GetWorld()->GetTimeSeconds()>.75&&!UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(this)){SetActorTickEnabled(false);SkillCheck();Finish();}}
        else{SkillServerTick();}
    }
    else if (GetNetMode()==NM_Client) { ClientTick(); }
    else if (Role==TEXT("Local"))
    {
        if (GetWorld()->GetTimeSeconds()>.75&&!UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(this)) { SetActorTickEnabled(false); LocalCheck(); Finish(); }
    }
    else { ServerTick(); }
#endif
}
void ALDDevelopmentProbe::LocalCheck()
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>(); auto* Public=GetWorld()->GetGameState<ALDGameState>();
    auto* PC=CastChecked<ALDPlayerController>(GetWorld()->GetFirstPlayerController());
    auto* State=Mode->StateFor(PC); auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());
    FString Reason;
    Check(Mode->IsDevelopmentProfile()&&!Mode->bScaffoldProfile&&Mode->CharacterCatalog(),TEXT("V04 profile loads saved catalog and disables migration fixtures"));
    Check(!Public->bMatchStarted&&Public->LobbyCount==1&&Public->ReadyCount==0&&State->bLobbyHost&&!State->Gold&&!State->Xp&&!State->SealId.IsValid(),TEXT("Fresh private lobby has no fixture resources"));
    Check(Body->GetCharacterMovement()->GetMaxSpeed()==0,TEXT("Native movement is locked before start"));
    Check(!Mode->Combat->Request(PC,0,FVector::ForwardVector,Reason),TEXT("Authority blocks basic combat in lobby"));
    Check(!Mode->StartDevelopmentMatch(PC,Reason),TEXT("Cannot start before Ready"));
    const uint32 Revision=State->Build.Revision;
    PC->ServerLobbyChoice(State->Match,State->Life,Revision+1,1,false);
    Check(State->Build.ClassId==0,TEXT("Stale build revision cannot change class"));
    Check(!Mode->SetLobbyChoice(PC,3,false,Reason)&&State->Build.ClassId==0,TEXT("Invalid class rejected without mutation"));
    const uint8 Presentation=Body->PresentationKey;
    PC->ServerLobbyChoice(State->Match,State->Life,Revision,1,false);
    Check(State->Build.ClassId==1&&State->Hp==95&&State->MaxHp==95&&Body->PresentationKey==Presentation,TEXT("Private Rogue choice applies attributes without changing body appearance"));
    PC->ServerLobbyChoice(State->Match,State->Life,State->Build.Revision,1,true);
    PC->ServerStartLobby(State->Match,State->Life);
    Check(Public->bMatchStarted&&Mode->IsPlaying()&&FMath::IsNearlyEqual(double(Public->MatchDeadline)-Mode->StartedAt(),1500.,.01),TEXT("Ready host starts one-human practice with a 1500-second deadline"));
    PC->ServerLobbyChoice(State->Match,State->Life,State->Build.Revision,2,false);
    Check(State->Build.ClassId==1&&State->bReady&&!Mode->StartDevelopmentMatch(PC,Reason),TEXT("Started roster cannot change class or restart timers"));
    FString LoginError; Mode->PreLogin(TEXT(""),TEXT("127.0.0.1"),FUniqueNetIdRepl(),LoginError);
    Check(LoginError.Contains(TEXT("Roster locked")),TEXT("New guest cannot reset finite budgets after start"));
    Check(FMath::IsNearlyEqual(Body->GetCharacterMovement()->GetMaxSpeed(),183.6f,.01f),TEXT("Native walk speed derives from the private build"));
    Body->SetSprint(true); Check(FMath::IsNearlyEqual(Body->GetCharacterMovement()->GetMaxSpeed(),306.f,.01f),TEXT("Native sprint derives from the same bounded factor")); Body->SetSprint(false);
    Mode->AwardExperience(PC,420); const auto Capped=State->Build; Mode->AwardExperience(PC,50);
    Check(State->Xp==420&&State->Build.Level==5&&State->Build.AttributePoints==8&&State->Build.SkillPoints==4&&State->Build.Revision==Capped.Revision,TEXT("Actual contender XP crosses all thresholds once and caps"));
    // Allocation is a labelled helper fixture until the held preparation transaction is integrated.
    auto Plan=LDDevelopment::PlanFrom(State->Build); Plan.Attributes[LDDevelopment::STR]+=3; Plan.Attributes[LDDevelopment::CON]+=2;
    FLDCharacterBuild Prepared; Check(LDDevelopment::Prepare(State->Build,Plan,Prepared,Reason),TEXT("Helper fixture creates an affordable allocated build"));
    State->Hp=50; State->Build=Prepared; Mode->RefreshBuild(PC);
    Check(State->Hp==50&&State->MaxHp==105&&Mode->Combat->StateFor(Body)->Hp==50,TEXT("Maximum-HP recalculation does not heal current authority HP"));
    const double Now=GetWorld()->GetTimeSeconds(); auto* Combat=Mode->Combat->StateFor(Body); Combat->ProtectionUntil=0; State->ProtectedUntil=0;
    Body->SetActorLocation(FVector(-1700,-1000,92),false,nullptr,ETeleportType::TeleportPhysics);
    auto* NearbyResident=GetWorld()->SpawnActor<ALDResident>(FVector(-1650,-1000,92),FRotator::ZeroRotator);
    NearbyResident->GetCharacterMovement()->DisableMovement();
    Check(Mode->Vision->PlaceWard(PC,FVector(-1650,-1000,0),Reason),TEXT("Nearby humanoid does not impersonate a static ward-ground blocker"));
    if (NearbyResident->GetController()) { NearbyResident->GetController()->Destroy(); } NearbyResident->Destroy();
    for (TActorIterator<ALDWard> It(GetWorld());It;++It) { It->Destroy(); }
    // The melee contract selects the nearest legal target. Isolate the damage
    // fixture so a randomly wandering resident cannot legitimately intercept it.
    for (TActorIterator<ALDResident> It(GetWorld());It;++It)
    { It->SetActorLocation(FVector(1800,1000,92),false,nullptr,ETeleportType::TeleportPhysics); It->GetCharacterMovement()->DisableMovement(); }
    auto* Victim=GetWorld()->SpawnActor<ALDCharacter>(Body->GetActorLocation()+FVector(80,0,0),FRotator::ZeroRotator);
    Victim->GetCharacterMovement()->DisableMovement(); Body->GetCharacterMovement()->DisableMovement();
    Check(Mode->Combat->Request(PC,0,FVector::ForwardVector,Reason),TEXT("Real scaled melee commits through retained combat path"));
    Mode->Combat->Sample(Now+.16);
    const double VictimHP=Mode->Combat->StateFor(Victim)->Hp;
    Check(FMath::IsNearlyEqual(VictimHP,78.5),FString::Printf(TEXT("Actual impact retains 21.5 fractional damage (victim HP %.2f)"),VictimHP)); Victim->Destroy();
    Mode->Combat->StateFor(Body)->Action=0;
    Check(!Mode->Combat->Request(PC,1,FVector::ForwardVector,Reason)&&!Mode->Combat->Request(PC,2,FVector::ForwardVector,Reason),TEXT("Legacy assassination and dash cannot bypass future equipped slots"));
    State->Build.Cooldowns[int32(ELDSkill::Step)]=Now+100; State->Gold=101; Mode->SyncOwner(PC);
    const auto Retained=State->Build; const uint32 OldLife=State->Life; const uint32 OldBody=Body->GetUniqueID();
    Mode->EndContenderLife(PC,Now); Body->Destroy(); const double ReturnAt=State->ReturnAt;
    Check(!State->bActive&&State->Gold==85&&State->LastLostGold==16&&FMath::IsNearlyEqual(ReturnAt-Now,26.,.01),TEXT("Death snapshots v0.4 level-five opening delay and ceiling gold loss"));
    Mode->EndContenderLife(PC,Now+1); Check(State->Gold==85&&State->ReturnAt==ReturnAt,TEXT("Duplicate death cannot charge loss or extend return"));
    Mode->ReturnDueContenders(ReturnAt+.01);
    Check(PC->GetPawn()&&PC->GetPawn()->GetUniqueID()!=OldBody&&State->Life==OldLife+1&&State->Hp==105&&State->Build.Attributes==Retained.Attributes&&State->Build.Cooldowns==Retained.Cooldowns&&State->Build.Ranks==Retained.Ranks&&State->Build.Slots==Retained.Slots,TEXT("New body retains build, points, knowledge and absolute cooldowns with full derived HP"));
    PC->ServerLobbyChoice(State->Match,OldLife,State->Build.Revision,0,false);
    Check(State->Build.ClassId==1,TEXT("Old-life lobby request cannot restore a fresh character"));
}
void ALDDevelopmentProbe::ResetCheck()
{
    auto* Instance=GetGameInstance<ULDGameInstance>();
    if (!Instance->DevelopmentResetEvidence) { Instance->DevelopmentResetEvidence=MakeShared<FLDDevelopmentResetEvidence>(); }
    auto& Evidence=*Instance->DevelopmentResetEvidence;
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>(); auto* Public=GetWorld()->GetGameState<ALDGameState>();
    auto* PC=CastChecked<ALDPlayerController>(GetWorld()->GetFirstPlayerController()); auto* State=Mode->StateFor(PC);
    const auto Start=LDDevelopment::StartingBuild(0,Mode->CharacterCatalog());
    Check(State->Match>Evidence.Generation&&!Public->bMatchStarted&&Public->ReadyCount==0&&Public->LobbyCount==1&&State->bLobbyHost&&!State->bReady,TEXT("New world resets lobby, ownership and generation"));
    Check(State->Build.ClassId==0&&State->Build.Attributes==Start.Attributes&&State->Build.Ranks==Start.Ranks&&State->Build.Slots==Start.Slots&&State->Build.Cooldowns==Start.Cooldowns&&!State->Build.Subclass&&!State->Build.AttributePoints&&!State->Build.SkillPoints&&State->Build.Level==1&&!State->Xp&&!State->Gold&&State->Hp==110&&State->MaxHp==110&&!State->DeathSerial,TEXT("New world clears all B41 build, currency and life fields"));
    int32 BookActors=0;for(TActorIterator<ALDBook> It(GetWorld());It;++It){++BookActors;}Check(State->Books.IsEmpty()&&!State->DevelopmentKind&&!State->DevelopmentToken&&!State->DevelopmentEndsAt&&!BookActors&&Mode->Development->QueuedCount()==0,TEXT("New world clears unread books, physical instances, development channels and queued requests"));
    Check(!State->Witness.IsValid()&&!State->TheftReceipt.IsValid()&&!State->TheftOutcome&&!State->TheftAmount&&!State->TheftRetryAt&&!State->AlertUntil&&!State->TamperSerial&&Mode->WorldChecks->DrawCount()==0&&Mode->WorldChecks->WalletCount()==0,TEXT("New world clears Witness, theft receipts, retries, alerts, wallets and RNG domain"));
    if(Mode->Region){int32 Seals=0;for(TActorIterator<ALDSeal> It(GetWorld());It;++It){++Seals;}Check(!State->FabricationReceipt.IsValid()&&!State->SealId.IsValid()&&!Seals&&!State->PaidJobs&&!State->FirstCourierTown&&!State->ContractDestination&&State->VisitedRegions.IsEmpty()&&!State->PaidSentinels&&!State->SentinelReceipt.IsValid()&&!Public->PublicSensorOfflineUntil&&!Public->PublicSensorLockoutUntil&&Public->bPublicSensorActive&&Mode->Region->SentinelBody(),TEXT("New region world clears fabrication/item, work/visit/encounter receipts and sabotage endpoints"));}
    if (Evidence.Round)
    {
        Check(!Evidence.OldActors.ContainsByPredicate([](const auto& Actor){return Actor.IsValid();}),TEXT("Prior world actors are destroyed"));
        PC->ClientLifeReceipt(Evidence.PreviousOwner);
        PC->ServerLobbyChoice(Evidence.Generation,1,State->Build.Revision,2,true);
        Check(PC->OwnerState.Match==State->Match&&State->Build.ClassId==0&&!State->bReady,TEXT("Old private receipt and lobby request cannot restore a prior build"));
    }
    Evidence.Checks.Append(Checks); Evidence.Failures+=Failures;
    if (Evidence.Round==10) { Checks=Evidence.Checks; Failures=Evidence.Failures; Finish(); return; }
    FString Reason; Mode->SetLobbyChoice(PC,1,true,Reason); Mode->StartDevelopmentMatch(PC,Reason); Mode->AwardExperience(PC,420);
    State->Build.Cooldowns[int32(ELDSkill::Step)]=GetWorld()->GetTimeSeconds()+100;
    State->Build.Subclass=3; State->Gold=101; Mode->SyncOwner(PC);
    State->Witness=FGuid::NewGuid();State->TheftReceipt=FGuid::NewGuid();State->TheftOutcome=2;State->TheftRetryAt=State->AlertUntil=GetWorld()->GetTimeSeconds()+100;State->TamperSerial=1;Mode->WorldChecks->WalletId(Cast<ALDCharacter>(PC->GetPawn()));
    auto* DirtyBody=CastChecked<ALDCharacter>(PC->GetPawn());DirtyBody->SetActorLocation(LDRegion::Site(GetWorld(),1)+FVector(0,50,92));Mode->Combat->StateFor(DirtyBody)->ProtectionUntil=0;PC->BuyBook(ELDSkill::Spark);Mode->Combat->Sample(GetWorld()->GetTimeSeconds());
    if(State->Books.Num()){PC->StartDevelopmentHold(2,State->Books[0].Id,FLDBuildPlan());Mode->Combat->Sample(GetWorld()->GetTimeSeconds());}
    PC->StopDevelopmentHold();DirtyBody->SetFacing(0);auto* Purse=GetWorld()->SpawnActor<ALDResident>(DirtyBody->GetActorLocation()+FVector(80,0,0),FRotator::ZeroRotator);Purse->SetActorTickEnabled(false);Purse->GetCharacterMovement()->DisableMovement();Purse->SetFacing(0);
    PC->ServerSkillAction(State->Match,State->Life,1000,State->Build.Revision,0,FVector::ForwardVector);Mode->Combat->Sample(GetWorld()->GetTimeSeconds());
    GetWorld()->TimeSeconds+=1.01;Mode->Combat->RefreshSkillHold(DirtyBody,1000,1,false);Mode->Combat->Sample(GetWorld()->GetTimeSeconds());
    const bool Dirty=Mode->WorldChecks->DrawCount()==1&&State->TheftReceipt.IsValid()&&State->TheftRetryAt>GetWorld()->GetTimeSeconds();Evidence.Checks.Add(Dirty?TEXT("PASS Prior world committed an actual theft draw, receipt and pair reservation before reset"):TEXT("FAIL Prior world failed to dirty theft domains before reset"));if(!Dirty){++Evidence.Failures;}
    if (Evidence.Round%2==0) { Mode->EndContenderLife(PC,GetWorld()->GetTimeSeconds()); }
    if(Mode->Region){State->PaidJobs=8;State->FirstCourierTown=43;State->ContractDestination=91;State->VisitedRegions={43,91,137};State->PaidSentinels=2;State->SentinelReceipt=FGuid::NewGuid();Public->PublicSensorOfflineUntil=GetWorld()->GetTimeSeconds()+30;Public->PublicSensorLockoutUntil=GetWorld()->GetTimeSeconds()+60;Mode->Vision->SetPublicSensor(false,false);Evidence.Checks.Add(TEXT("PASS Regional reset fixture dirtied work/visit/encounter limits and public sabotage timers"));}
    if(Mode->Region){State->FabricationReceipt=FGuid::NewGuid();State->SealId=FGuid::NewGuid();auto* DirtySeal=GetWorld()->SpawnActor<ALDSeal>();DirtySeal->Identity=State->SealId;DirtySeal->State=1;DirtySeal->Carrier=PC;Mode->SyncOwner(PC);}
    Evidence.Generation=State->Match; Evidence.PreviousOwner=PC->OwnerState;
    Evidence.OldActors.Reset(); for (TActorIterator<AActor> It(GetWorld());It;++It) { Evidence.OldActors.Add(*It); }
    ++Evidence.Round;
    UGameplayStatics::OpenLevel(this,FName(*GetWorld()->GetOutermost()->GetName()),true,TEXT("Profile=V04"));
}
void ALDDevelopmentProbe::Enter(int32 Next)
{
    Phase=Next; PhaseAt=FPlatformTime::Seconds(); A->ClientProbePhase(uint8(Next)); B->ClientProbePhase(uint8(Next));
}
void ALDDevelopmentProbe::ServerTick()
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>(); auto* Public=GetWorld()->GetGameState<ALDGameState>();
    if (!Phase)
    {
        for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        { if (auto* PC=Cast<ALDPlayerController>(It->Get());PC&&PC->GetPawn()) { if (PC->ProbeRole==1) { A=PC; } if (PC->ProbeRole==2) { B=PC; } } }
        if (A.IsValid()&&B.IsValid()) { Check(Public->LobbyCount==2&&!Public->bMatchStarted,TEXT("Separate authority has two lobby contenders")); Enter(1); }
        return;
    }
    if (Phase==4) { if (FPlatformTime::Seconds()-PhaseAt>3) { Finish(); } return; }
    if (!A.IsValid()||!B.IsValid()) { Check(false,TEXT("Client disconnected before completion")); Finish(); return; }
    const double Elapsed=FPlatformTime::Seconds()-PhaseAt;
    const auto* AS=Mode->StateFor(A.Get()); const auto* BS=Mode->StateFor(B.Get());
    if (Phase==1 && AS->bReady&&BS->bReady)
    {
        Check(AS->Build.ClassId==1&&BS->Build.ClassId==2&&Public->ReadyCount==2,TEXT("Owning RPCs choose independent private classes and Ready"));
        Check(!Public->bMatchStarted&&AS->Build.Attributes!=BS->Build.Attributes,TEXT("Choices alone do not start or merge builds")); Enter(2);
    }
    else if (Phase==2 && Public->bMatchStarted&&Elapsed>1.5)
    {
        Check(Mode->IsPlaying()&&Public->LobbyCount==2&&Public->ReadyCount==2,TEXT("Ready host starts exactly the locked two-contender roster"));
        FString Error; Mode->PreLogin(TEXT(""),TEXT("127.0.0.1"),FUniqueNetIdRepl(),Error);
        Check(Error.Contains(TEXT("Roster locked")),TEXT("Authority rejects fresh post-start guests"));
        Mode->AwardExperience(A.Get(),60); Mode->AwardExperience(B.Get(),150); Enter(3);
    }
    else if (Phase==3&&Elapsed>2)
    {
        Check(AS->Build.Level==2&&BS->Build.Level==3&&AS->Build.AttributePoints==2&&BS->Build.AttributePoints==4,TEXT("Private growth receipts remain independent on authority"));
        Enter(4);
    }
}
void ALDDevelopmentProbe::ClientTick()
{
    auto* PC=Cast<ALDPlayerController>(GetOwner()); if (!PC || !PC->OwnerState.Match) { return; }
    if (FPlatformTime::Seconds()-LastPing>.2) { LastPing=FPlatformTime::Seconds(); PC->ServerProbePing(LastPing); }
    if (PC->ProbePhase!=Phase) { Phase=PC->ProbePhase; PhaseAt=FPlatformTime::Seconds(); bChecked=false; }
    const double Elapsed=FPlatformTime::Seconds()-PhaseAt;
    const uint8 Class=Role==TEXT("A")?1:2;
    if (Phase==1)
    {
        if (!bChoiceSent) { PC->ChooseLobbyClass(Class); bChoiceSent=true; }
        if (!bReadySent&&PC->OwnerState.Build.ClassId==Class&&Elapsed>.4) { PC->ToggleLobbyReady(); bReadySent=true; }
    }
    if (Phase==2&&!bStartSent&&Elapsed>(PC->OwnerState.bLobbyHost?.8:.1)) { PC->StartLobby(); bStartSent=true; }
    if (Phase==3&&!bChecked&&Elapsed>1)
    {
        bChecked=true; const auto& O=PC->OwnerState;
        Check(O.bDevelopmentProfile&&O.Build.ClassId==Class&&O.Build.Level==(Class==1?2:3),TEXT("Owner receives own selected class and level"));
        Check(O.Build.AttributePoints==(Class==1?2:4)&&O.Build.SkillPoints==(Class==1?1:2),TEXT("Owner receives exact private point awards"));
        if (!O.bLobbyHost) { Check(PC->IntentFeedback.Contains(TEXT("Only the lobby host")),TEXT("Nonhost start RPC was rejected by authority")); }
        int32 Bodies=0,Controllers=0,PlayerStates=0; bool Own=true;
        for (TActorIterator<ALDCharacter> It(GetWorld());It;++It) { ++Bodies; Own&=*It==PC->GetPawn(); }
        for (TActorIterator<ALDPlayerController> It(GetWorld());It;++It) { ++Controllers; Own&=*It==PC; }
        for (TActorIterator<ALDPlayerState> It(GetWorld());It;++It) { ++PlayerStates; Own&=*It==PC->PlayerState; }
        Check(Bodies==1&&Controllers==1&&PlayerStates==1&&Own,TEXT("Restricted native client has no opponent canonical body, controller or private build carrier"));
        Check(PC->LocalView()&&PC->RootUI&&PC->GetPawn()->GetClass()==ALDCharacter::StaticClass(),TEXT("Selected class keeps native UI, filtered relay and the shared body class"));
    }
    if (Phase==4&&Elapsed>1) { Finish(); }
}
void ALDDevelopmentProbe::Finish()
{
    SetActorTickEnabled(false);
    TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>(); Report->SetNumberField(TEXT("failures"),Failures);
    if (GetNetMode()==NM_Client)
    {
        const auto* PC=CastChecked<ALDPlayerController>(GetOwner()); auto RTTs=PC->ProbeRtts; RTTs.Sort();
        const double Median=RTTs.IsEmpty()?0:RTTs[RTTs.Num()/2];
        Check(RTTs.Num()>=8,TEXT("Independent native client received transport echo samples"));
        Report->SetNumberField(TEXT("medianEchoRttMs"),Median); Report->SetNumberField(TEXT("echoSamples"),RTTs.Num()); Report->SetNumberField(TEXT("failures"),Failures);
    }
    Report->SetStringField(TEXT("profile"),TEXT("V04 development; skill/build grants, task callbacks and teleports in probes are engineering fixtures"));
    if(FParse::Param(FCommandLine::Get(),TEXT("LDRegionProbe"))){Report->SetStringField(TEXT("profile"),TEXT("B45 engineering fixtures: grants, teleports, controlled authority deadlines and adversarial state; not ordinary progression or human evidence"));}
    if(FParse::Param(FCommandLine::Get(),TEXT("LDRegionNormal"))){Report->SetStringField(TEXT("profile"),TEXT("B45 ordinary automated practice: native navigation/player input, original job/visit/Charter timers; no resource/build grants"));Report->SetNumberField(TEXT("nativeTravelCm"),NormalTravel);}
    if(FParse::Param(FCommandLine::Get(),TEXT("LDWorldCheckProbe"))){Report->SetStringField(TEXT("profile"),TEXT("B44 engineering fixture: setup grants/teleports, controlled authority deadlines and private fixed-roll/balance injection; not normal progression or human evidence"));}
    if(FParse::Param(FCommandLine::Get(),TEXT("LDPreparationNormal"))){Report->SetStringField(TEXT("profile"),TEXT("B43 normal-rule automated local practice; native travel and original job/book/preparation timers; no resource grants"));Report->SetNumberField(TEXT("nativeTravelCm"),NormalTravel);}
    if(FParse::Param(FCommandLine::Get(),TEXT("LDWorldCheckNormal"))){Report->SetStringField(TEXT("profile"),TEXT("B44 ordinary automated practice: native player travel and actual equipped Survey Charter hold; no XP/gold/build grants"));Report->SetNumberField(TEXT("nativeTravelCm"),NormalTravel);}
    if(FParse::Param(FCommandLine::Get(),TEXT("LDSealProbe"))){Report->SetStringField(TEXT("profile"),FParse::Param(FCommandLine::Get(),TEXT("LDRegionNormal"))?TEXT("B46 ordinary automated zero-kill acquisition: native travel, starting Survey, four paid jobs, fabrication and Heart; no grants, teleports, build overrides or clock advances"):TEXT("B46 engineering: adversarial balances, positions, clocks and creation failure; network acquisition uses real Witness/job/fabrication commits with travel and cooldown clock fixtures"));}
    if(FParse::Param(FCommandLine::Get(),TEXT("LDReleaseSession"))){Report->SetStringField(TEXT("profile"),TEXT("B47 ordinary automated 25-minute practice: original clock/deadline/rewards, native traversal, eight jobs, actual NPC healing and earned fabrication; no grants or teleports; not human evidence"));Report->SetNumberField(TEXT("wallSeconds"),FPlatformTime::Seconds()-SessionStarted);Report->SetNumberField(TEXT("nativeTravelCm"),NormalTravel);Report->SetArrayField(TEXT("audits"),SessionSamples);Report->SetNumberField(TEXT("actorMin"),SessionActorMin);Report->SetNumberField(TEXT("actorMax"),SessionActorMax);}
    FString HUDScene;if(FParse::Value(FCommandLine::Get(),TEXT("LDHUDScene="),HUDScene))
    {
        Report->SetStringField(TEXT("profile"),TEXT("B53/B54 native rendered UI; auto-start/location/selection/death/deadline fixtures; Vendor additionally grants 80 gold, known Spark and five/full bag for layout; Inventory/QuickItems/ItemReading/DropConfirm additionally grant a full bag of books/Seal and 160 gold for layout only; not progression, human or OS-scale evidence"));Report->SetStringField(TEXT("scene"),HUDScene);
        auto* PC=CastChecked<ALDPlayerController>(GetWorld()->GetFirstPlayerController());int32 W=0,H=0;PC->GetViewportSize(W,H);Report->SetNumberField(TEXT("viewportWidth"),W);Report->SetNumberField(TEXT("viewportHeight"),H);Report->SetNumberField(TEXT("effectiveViewportScale"),UWidgetLayoutLibrary::GetViewportScale(this));Report->SetNumberField(TEXT("textScale"),PC->Controls?PC->Controls->TextScale:1);
    }
    TArray<TSharedPtr<FJsonValue>> Lines; for (const auto& Line:Checks) { Lines.Add(MakeShared<FJsonValueString>(Line)); }
    Report->SetArrayField(TEXT("checks"),Lines); FString Output,Path,Directory;
    FParse::Value(FCommandLine::Get(),TEXT("LDProbeReport="),Path);
    if (Path.IsEmpty()) { FParse::Value(FCommandLine::Get(),TEXT("LDProbeReportDir="),Directory); Path=FPaths::Combine(Directory,Role+TEXT(".json")); }
    FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Output));
    if (Path.IsEmpty()||!FFileHelper::SaveStringToFile(Output,*Path)) { ++Failures; }
    FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
