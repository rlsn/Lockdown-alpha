#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "UI/LDUIRoot.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Framework/LDGameMode.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Tests/LDViewNetworkProbe.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Tests/LDCombatNetworkProbe.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Objectives/LDObjectiveRules.h"
#include "Tests/LDObjectiveNetworkProbe.h"
#include "Tests/LDSoakProbe.h"
#include "Tests/LDDevelopmentProbe.h"
#include "Development/LDDevelopmentDirector.h"
#include "Development/LDWorldChecks.h"
#include "Region/LDRegionDefinition.h"
#include "Navigation/LDNavigationComponent.h"
#include "Tests/LDNavigationProbe.h"
#include "Tests/LDInputProbe.h"
#include "UI/LDControlSettings.h"
#include "UI/LDControlsPanel.h"

namespace
{
    bool LDNetworkProbeEnabled()
    {return FParse::Param(FCommandLine::Get(),TEXT("LDViewProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDCombatNetProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDObjectiveNetProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDSoakProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDDevelopmentProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDNavigationProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDInputProbe"));}
}

ALDPlayerController::ALDPlayerController(){Navigation=CreateDefaultSubobject<ULDNavigationComponent>(TEXT("NavigationCommands"));}
bool ALDPlayerController::CanWorldCommand() const
{const auto* G=GetWorld()->GetGameState<ALDGameState>();return !bControlsSuppressed&&!bFreshInputRequired&&OwnerState.bActive&&GetPawn()&&G&&G->bMatchStarted&&!G->EndReason;}

bool ALDPlayerController::IsFrontend() const { return GetWorld()->GetMapName().Contains(TEXT("L_Frontend")); }

void ALDPlayerController::ChooseLobbyClass(uint8 ClassId)
{ ServerLobbyChoice(OwnerState.Match,OwnerState.Life,OwnerState.Build.Revision,ClassId,false); }
void ALDPlayerController::ToggleLobbyReady()
{ ServerLobbyChoice(OwnerState.Match,OwnerState.Life,OwnerState.Build.Revision,OwnerState.Build.ClassId,!OwnerState.bReady); }
void ALDPlayerController::StartLobby() { ServerStartLobby(OwnerState.Match,OwnerState.Life); }
void ALDPlayerController::ServerLobbyChoice_Implementation(uint32 Match,uint32 Life,uint32 BuildRevision,uint8 ClassId,bool Ready)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>(); const auto* State=Mode?Mode->StateFor(this):nullptr; FString Reason;
    if (!State || State->Match!=Match || State->Life!=Life || State->Build.Revision!=BuildRevision) { Reason=TEXT("Stale lobby choice."); }
    else { Mode->SetLobbyChoice(this,ClassId,Ready,Reason); }
    ClientIntentResult(0,Reason);
}
void ALDPlayerController::ServerStartLobby_Implementation(uint32 Match,uint32 Life)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>(); const auto* State=Mode?Mode->StateFor(this):nullptr; FString Reason;
    if (!State || State->Match!=Match || State->Life!=Life) { Reason=TEXT("Stale start request."); }
    else { Mode->StartDevelopmentMatch(this,Reason); }
    ClientIntentResult(0,Reason);
}

void ALDPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController()) { return; }
    Controls=ULDControlSettings::Load();
    bShowMouseCursor = true;
    if (ULocalPlayer* Local = GetLocalPlayer())
    {
        UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Lockdown/Generated/Input/IMC_Gameplay.IMC_Gameplay"));
        if (ensure(Context)) { Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()->AddMappingContext(Context, 0); }
    }
    const TSubclassOf<ULDUIRoot> RootClass = LoadClass<ULDUIRoot>(nullptr,TEXT("/Game/Lockdown/Generated/UI/WBP_UIRoot.WBP_UIRoot_C"));
    checkf(RootClass,TEXT("Missing UI root; run asset bootstrap"));
    RootUI = CreateWidget<ULDUIRoot>(this, RootClass);
    RootUI->AddToViewport();
    SetMenuOpen(IsFrontend());
#if !UE_BUILD_SHIPPING
    if(GetNetMode()==NM_Client)
    {
    if(!IsFrontend()&&FParse::Param(FCommandLine::Get(),TEXT("LDInputProbe")))
    {FActorSpawnParameters Params;Params.Owner=this;GetWorld()->SpawnActor<ALDInputProbe>(ALDInputProbe::StaticClass(),FTransform::Identity,Params);}
    if(!IsFrontend()&&FParse::Param(FCommandLine::Get(),TEXT("LDNavigationProbe")))
    {FActorSpawnParameters Params;Params.Owner=this;GetWorld()->SpawnActor<ALDNavigationProbe>(ALDNavigationProbe::StaticClass(),FTransform::Identity,Params);}
    if(!IsFrontend()&&FParse::Param(FCommandLine::Get(),TEXT("LDDevelopmentProbe")))
    {FActorSpawnParameters Params;Params.Owner=this;GetWorld()->SpawnActor<ALDDevelopmentProbe>(ALDDevelopmentProbe::StaticClass(),FTransform::Identity,Params);}
    if (!IsFrontend() && FParse::Param(FCommandLine::Get(),TEXT("LDViewProbe")))
    { FActorSpawnParameters Params; Params.Owner=this; GetWorld()->SpawnActor<ALDViewNetworkProbe>(ALDViewNetworkProbe::StaticClass(),FTransform::Identity,Params); }
    if(!IsFrontend()&&FParse::Param(FCommandLine::Get(),TEXT("LDCombatNetProbe")))
    {FActorSpawnParameters Params;Params.Owner=this;GetWorld()->SpawnActor<ALDCombatNetworkProbe>(ALDCombatNetworkProbe::StaticClass(),FTransform::Identity,Params);}
    if(!IsFrontend()&&FParse::Param(FCommandLine::Get(),TEXT("LDObjectiveNetProbe")))
    {FActorSpawnParameters Params;Params.Owner=this;GetWorld()->SpawnActor<ALDObjectiveNetworkProbe>(ALDObjectiveNetworkProbe::StaticClass(),FTransform::Identity,Params);}
    if(!IsFrontend()&&FParse::Param(FCommandLine::Get(),TEXT("LDSoakProbe")))
    {FActorSpawnParameters Params;Params.Owner=this;GetWorld()->SpawnActor<ALDSoakProbe>(ALDSoakProbe::StaticClass(),FTransform::Identity,Params);}
    }
#endif
}

void ALDPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    UEnhancedInputComponent* Input = CastChecked<UEnhancedInputComponent>(InputComponent);
    auto Action = [](const TCHAR* Name)
    {
        return LoadObject<UInputAction>(nullptr, *(FString(TEXT("/Game/Lockdown/Generated/Input/IA_")) + Name + TEXT(".IA_") + Name));
    };
    Input->BindAction(Action(TEXT("Move")), ETriggerEvent::Triggered, this, &ALDPlayerController::Move);
    Input->BindAction(Action(TEXT("Sprint")), ETriggerEvent::Started, this, &ALDPlayerController::SprintStarted);
    Input->BindAction(Action(TEXT("Sprint")), ETriggerEvent::Completed, this, &ALDPlayerController::SprintStopped);
    Input->BindAction(Action(TEXT("Sprint")), ETriggerEvent::Canceled, this, &ALDPlayerController::SprintStopped);
    Input->BindAction(Action(TEXT("Zoom")), ETriggerEvent::Triggered, this, &ALDPlayerController::Zoom);
    Input->BindAction(Action(TEXT("Menu")), ETriggerEvent::Started, this, &ALDPlayerController::ToggleMenu);
    Input->BindAction(Action(TEXT("Help")), ETriggerEvent::Started, this, &ALDPlayerController::ToggleMenu);
    Input->BindAction(Action(TEXT("Ward")), ETriggerEvent::Started, this, &ALDPlayerController::Ward);
    Input->BindAction(Action(TEXT("Attack")),ETriggerEvent::Started,this,&ALDPlayerController::Attack);
    Input->BindAction(Action(TEXT("Assassinate")),ETriggerEvent::Started,this,&ALDPlayerController::Assassinate);
    Input->BindAction(Action(TEXT("Dash")),ETriggerEvent::Started,this,&ALDPlayerController::Dash);
    Input->BindAction(Action(TEXT("Dash")),ETriggerEvent::Completed,this,&ALDPlayerController::RecenterEnded);
    Input->BindAction(Action(TEXT("Dash")),ETriggerEvent::Canceled,this,&ALDPlayerController::RecenterEnded);
    Input->BindAction(Action(TEXT("Interact")),ETriggerEvent::Started,this,&ALDPlayerController::InteractStarted);
    Input->BindAction(Action(TEXT("Interact")),ETriggerEvent::Completed,this,&ALDPlayerController::InteractStopped);
    Input->BindAction(Action(TEXT("Interact")),ETriggerEvent::Canceled,this,&ALDPlayerController::InteractStopped);
    // A native context adds four slots without changing the frozen migration assets.
    SkillInputContext=NewObject<UInputMappingContext>(this);
    const FKey Keys[]={EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four};
    for(uint8 Slot=0;Slot<LDDevelopment::SlotCount;++Slot)
    {
        auto* SkillAction=NewObject<UInputAction>(SkillInputContext);
        SkillInputContext->MapKey(SkillAction,Keys[Slot]);
        Input->BindAction(SkillAction,ETriggerEvent::Started,this,&ALDPlayerController::SkillStarted,Slot);
        Input->BindAction(SkillAction,ETriggerEvent::Completed,this,&ALDPlayerController::SkillStopped,Slot);
        Input->BindAction(SkillAction,ETriggerEvent::Canceled,this,&ALDPlayerController::SkillStopped,Slot);
    }
    auto* DevelopmentAction=NewObject<UInputAction>(SkillInputContext);SkillInputContext->MapKey(DevelopmentAction,EKeys::P);
    Input->BindAction(DevelopmentAction,ETriggerEvent::Started,this,&ALDPlayerController::ToggleDevelopmentPanel);
    NavigationInputContext=NewObject<UInputMappingContext>(this);
    auto* NavigateAction=NewObject<UInputAction>(NavigationInputContext);NavigationInputContext->MapKey(NavigateAction,EKeys::RightMouseButton);
    Input->BindAction(NavigateAction,ETriggerEvent::Started,this,&ALDPlayerController::NavigateClick);
    auto* StopAction=NewObject<UInputAction>(NavigationInputContext);NavigationInputContext->MapKey(StopAction,EKeys::S);
    Input->BindAction(StopAction,ETriggerEvent::Started,this,&ALDPlayerController::StopOrder);
    auto* FollowAction=NewObject<UInputAction>(NavigationInputContext);NavigationInputContext->MapKey(FollowAction,EKeys::Home);
    Input->BindAction(FollowAction,ETriggerEvent::Started,this,&ALDPlayerController::CameraFollow);
    if(auto* Local=GetLocalPlayer()){Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()->AddMappingContext(SkillInputContext,1);}
}

void ALDPlayerController::OnRep_OwnerState()
{
    if(OwnerState.Match<LatestOwner.Match || (OwnerState.Match==LatestOwner.Match&&OwnerState.Revision<LatestOwner.Revision)){OwnerState=LatestOwner;return;}
    if(IsLocalController()&&(OwnerState.Match!=LatestOwner.Match||OwnerState.Life!=LatestOwner.Life||!OwnerState.bActive)){PendingAimRequest=0;Inspected.Invalidate();ClearControls();}
    LatestOwner=OwnerState;
}
void ALDPlayerController::ClientLifeReceipt_Implementation(const FLDOwnerState& State)
{
    if(State.Match<OwnerState.Match || (State.Match==OwnerState.Match&&State.Revision<OwnerState.Revision)){return;}
    OwnerState=State;OnRep_OwnerState();
}
void ALDPlayerController::Attack(){CombatAction(0);}
void ALDPlayerController::Assassinate(){if(OwnerState.bDevelopmentProfile){SkillAlias(ELDSkill::Ambush);}else{CombatAction(1);}}
void ALDPlayerController::Dash(){if(Navigation->Enabled()){if(CanWorldCommand()){Navigation->Recenter(true);}return;}if(OwnerState.bDevelopmentProfile){SkillAlias(ELDSkill::Step);}else{CombatAction(2);}}
void ALDPlayerController::RecenterEnded(){Navigation->Recenter(false);}
void ALDPlayerController::NavigateClick(){if(Navigation->Enabled()){Navigation->ClickMove();}}
void ALDPlayerController::StopOrder(){if(Navigation->Enabled()&&CanWorldCommand()){Navigation->Stop();CancelInterruptibleHolds();}}
void ALDPlayerController::CameraFollow(){if(Navigation->Enabled()&&CanWorldCommand()){Navigation->ToggleFollow();}}
void ALDPlayerController::SkillAlias(ELDSkill Skill)
{
    const int32 Slot=OwnerState.Build.Slots.Find(Skill);
    if(Slot==INDEX_NONE){IntentFeedback=TEXT("Equip the skill before using this shortcut.");return;}
    SkillStarted(uint8(Slot));
}
void ALDPlayerController::SkillStarted(uint8 Slot)
{
    auto* Body=Cast<ALDCharacter>(GetPawn());
    if(bControlsSuppressed||!OwnerState.bDevelopmentProfile||!OwnerState.bActive||!Body||!OwnerState.Build.Slots.IsValidIndex(Slot)){return;}
    const auto Skill=OwnerState.Build.Slots[Slot];
    if(Skill==ELDSkill::None){IntentFeedback=TEXT("Empty skill slot.");return;}
    if(HeldSkillSlot!=255){SkillStopped(HeldSkillSlot);}
    FVector Direction=Body->GetActorForwardVector();
    if(Skill==ELDSkill::Step&&!Body->GetLastMovementInputVector().IsNearlyZero()){Direction=Body->GetLastMovementInputVector().GetSafeNormal2D();}
    const uint32 Token=++NextRequest;
    if(Navigation->Enabled()){PendingAimRequest=Token;PendingSkillSlot=Slot;}
    if(uint8(Skill)>=uint8(ELDSkill::Patch)){Navigation->Stop();}
    if(uint8(Skill)>=uint8(ELDSkill::Patch))
    {HeldSkillSlot=Slot;SkillHoldToken=Token;SkillHoldSequence=0;LastSkillHoldSent=-1000;InteractStopped();}
    ServerSkillAction(OwnerState.Match,OwnerState.Life,Token,OwnerState.Build.Revision,Slot,Direction);
}
void ALDPlayerController::SkillStopped(uint8 Slot)
{
    if(HeldSkillSlot==Slot){ServerSkillRelease(OwnerState.Match,OwnerState.Life,SkillHoldToken);HeldSkillSlot=255;SkillHoldToken=0;}
}
void ALDPlayerController::CombatAction(uint8 Action)
{
    ALDCharacter* Body=Cast<ALDCharacter>(GetPawn());
    if(bControlsSuppressed||!OwnerState.bActive||!Body){return;}
    FVector Direction=Body->GetActorForwardVector();
    if(Action==2){const FVector Move=Body->GetLastMovementInputVector();if(!Move.IsNearlyZero()){Direction=Move.GetSafeNormal2D();}}
    IntentFeedback=TEXT("Action requested; awaiting authority.");
    ServerCombatAction(OwnerState.Match,OwnerState.Life,++NextRequest,Action,Direction);
}
void ALDPlayerController::ServerCombatAction_Implementation(uint32 Match,uint32 Life,uint32 Request,uint8 Action,FVector_NetQuantizeNormal Direction)
{
    const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if(M&&M->IsUIControlProfile()&&Action!=0){ClientIntentResult(Request,TEXT("Use an equipped skill slot"));return;}
    if(Action<=2){EnqueueAction(Match,Life,Request,Action,Direction);}
}
void ALDPlayerController::ServerSkillAction_Implementation(uint32 Match,uint32 Life,uint32 Request,uint32 BuildRevision,uint8 Slot,FVector_NetQuantizeNormal Direction)
{
    if(Slot>=LDDevelopment::SlotCount){return;}
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M?M->StateFor(this):nullptr;
    if(M&&M->IsUIControlProfile()&&O&&O->Build.Slots.IsValidIndex(Slot)&&O->Build.Slots[Slot]!=ELDSkill::Step&&O->Build.Slots[Slot]!=ELDSkill::Patch)
    {ClientIntentResult(Request,TEXT("Choose an observed target"));return;}
    EnqueueAction(Match,Life,Request,uint8(3+Slot),Direction,BuildRevision);
}
void ALDPlayerController::ServerSkillTargetAction_Implementation(uint32 Match,uint32 Life,uint32 Request,uint32 Revision,uint8 Slot,FVector_NetQuantizeNormal Direction,FGuid Observed)
{if(Slot<LDDevelopment::SlotCount){EnqueueAction(Match,Life,Request,uint8(3+Slot),Direction,Revision,Observed);}}
void ALDPlayerController::ServerSkillHold_Implementation(uint32 Match,uint32 Life,uint32 Token,uint32 Sequence,bool Moving)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=Mode?Mode->StateFor(this):nullptr;
    if(O&&O->Match==Match&&O->Life==Life&&Mode->Combat){Mode->Combat->RefreshSkillHold(Cast<ALDCharacter>(GetPawn()),Token,Sequence,Moving);}
}
void ALDPlayerController::ServerSkillRelease_Implementation(uint32 Match,uint32 Life,uint32 Token)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=Mode?Mode->StateFor(this):nullptr;
    if(O&&O->Match==Match&&O->Life==Life&&Mode->Combat){Mode->Combat->ReleaseSkillHold(this,Token);}
}
void ALDPlayerController::EnqueueAction(uint32 Match,uint32 Life,uint32 Request,uint8 Action,const FVector& Direction,uint32 BuildRevision,FGuid Observed)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* State=Mode?Mode->StateFor(this):nullptr;
    if(!State||Match!=State->Match||Life!=State->Life||!Request||Action>6||Direction.ContainsNaN()||FMath::Abs(Direction.Z)>.001||Direction.SizeSquared()<.9||Direction.SizeSquared()>1.1
        ||(Action>=3&&(!State->bDevelopmentProfile||BuildRevision!=State->Build.Revision)))
    {ClientIntentResult(Request,TEXT("Invalid action or stale life."));return;}
    if(const FString* Cached=CombatReceipts.Find(Request)){ClientIntentResult(Request,*Cached);return;}
    if(Mode->IsFinished()){ClientIntentResult(Request,TEXT("Demo finished."));return;}
    if(Request<LastCombatRequest){ClientIntentResult(Request,TEXT("Expired action request."));return;}
    const double Now=GetWorld()->GetTimeSeconds();
    CombatTokens=FMath::Min(3.,CombatTokens+FMath::Max(0.,Now-LastCombatAt)*12);LastCombatAt=Now;
    if(CombatTokens<1){ClientIntentResult(Request,TEXT("Action rate limited."));return;}
    --CombatTokens;LastCombatRequest=Request;LastCombatResult=TEXT("Action queued.");
    if(CombatReceipts.Num()>=32){uint32 Oldest=MAX_uint32;for(const auto& Pair:CombatReceipts){Oldest=FMath::Min(Oldest,Pair.Key);}CombatReceipts.Remove(Oldest);}
    CombatReceipts.Add(Request,LastCombatResult);
    if(Mode->Combat){if(Action>=3){Mode->Combat->QueueSkill(this,Match,Life,Request,BuildRevision,Action-3,Direction,Observed);}else{Mode->Combat->Queue(this,Match,Life,Request,Action,Direction);}}
    else{CompleteCombatRequest(Request,TEXT("Combat unavailable."));}
}
void ALDPlayerController::CompleteCombatRequest(uint32 Request,const FString& Result)
{CombatReceipts.Add(Request,Result);if(Request==LastCombatRequest){LastCombatResult=Result;}ClientCombatResult(OwnerState,Request,Result,GetPawn()?GetPawn()->GetActorRotation().Yaw:0);}
void ALDPlayerController::ClientCombatResult_Implementation(const FLDOwnerState& State,uint32 Request,const FString& Result,float Facing)
{
    ClientLifeReceipt_Implementation(State);
    if(State.Match==OwnerState.Match&&State.Life==OwnerState.Life&&Request==PendingAimRequest)
    {PendingAimRequest=0;if(auto* Body=Cast<ALDCharacter>(GetPawn())){Body->SetFacing(Facing);}}
    ClientIntentResult_Implementation(Request,Result);
}

bool ALDPlayerController::ChooseContext(uint8& Target,FGuid& Observed) const
{
    const APawn* Body=GetPawn();if(!Body){return false;}const FVector P=Body->GetActorLocation();
    if(!OwnerState.SealId.IsValid())
    {
        double Distance=80*80+1.;FGuid Closest;
        if(const auto* Relay=LocalView()){for(const auto& E:Relay->LastView().Entities)
        {const double D=FVector::DistSquared2D(P,E.Position);if(E.Kind==ELDObservedKind::Seal&&(D<Distance||(D==Distance&&E.Id.ToString()<Closest.ToString()))){Distance=D;Closest=E.Id;}}}
        if(Closest.IsValid()){Target=LDObjective::Seal;Observed=Closest;return true;}
    }
    Observed.Invalidate();
    if(OwnerState.bDevelopmentProfile)
    {
        double Distance=80*80+1.;FGuid Closest;
        if(const auto* Relay=LocalView()){for(const auto& E:Relay->LastView().Entities){const double D=FVector::DistSquared2D(P,E.Position);if(E.Kind==ELDObservedKind::Book&&(D<Distance||(D==Distance&&E.Id.ToString()<Closest.ToString()))){Distance=D;Closest=E.Id;}}}
        if(Closest.IsValid()){Target=5;Observed=Closest;return true;}
    }
    if(FVector::DistSquared2D(P,LDRegion::Site(GetWorld(),LDObjective::Heart))<=120*120){Target=LDObjective::Heart;return true;}
    const double Guild=FVector::DistSquared2D(P,LDRegion::Site(GetWorld(),LDObjective::Guild)),Station=FVector::DistSquared2D(P,LDRegion::Site(GetWorld(),LDObjective::Station));
    if(LDRegion::Definition(GetWorld())&&FVector::DistSquared2D(P,LDRegion::Site(GetWorld(),6))<=120*120){Target=6;return true;}
    if(FMath::Min(Guild,Station)<=120*120){Target=Guild<=Station?LDObjective::Guild:LDObjective::Station;return true;}
    return false;
}
FString ALDPlayerController::LegacyContextPrompt() const
{
    const auto* GS=GetWorld()->GetGameState<ALDGameState>();
    if(GS&&GS->EndReason){return GS->EndReason==2?TEXT("No claim. Demo finished."):(OwnerState.bWinner?TEXT("Heart claimed. You won."):TEXT("Heart claimed by another contender."));}
    if(!OwnerState.bActive){return TEXT("Waiting: owned ward and shared sight only.");}
    const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    if(Now<OwnerState.ProtectedUntil){return TEXT("Return protection: gameplay actions locked.");}
    if(OwnerState.bDevelopmentProfile&&GetPawn())
    {
        const FLDObservedEntity* NearestTask=nullptr;double Distance=120*120+1.;
        if(const auto* Relay=LocalView();Relay&&!Relay->IsStale()){for(const auto& E:Relay->LastView().Entities){const double D=FVector::DistSquared2D(GetPawn()->GetActorLocation(),E.Position);if((E.Kind==ELDObservedKind::Charter||E.Kind==ELDObservedKind::WatchControl)&&D<Distance){NearestTask=&E;Distance=D;}}}
        if(NearestTask)
        {
            if(NearestTask->Kind==ELDObservedKind::WatchControl){const auto& B=OwnerState.Build;const int32 Slot=B.Slots.Find(ELDSkill::Analyze);return FString::Printf(TEXT("Watch control · Own Analyze %.0f / public 14 · %s · Hold 2s; offline 30s / global lock 60s."),LDDevelopment::SkillValue(B,ELDSkill::Analyze),Slot==INDEX_NONE?TEXT("Learn and equip at a station"):GS&&Now<GS->PublicSensorLockoutUntil?TEXT("Globally locked"):*FString::Printf(TEXT("Hold slot %d"),Slot+1));}
            const auto Skill=LDCharter::Skill(NearestTask->Presentation);const auto& Build=OwnerState.Build;const int32 Slot=Build.Slots.Find(Skill);
            if(OwnerState.Witness.IsValid()){return TEXT("Charter Witness recorded. E remains work/loot; no duplicate Charter reward.");}
            if(!LDDevelopment::Rank(Build,Skill)){return FString::Printf(TEXT("%s · %s threshold 12 · Unlearned. P: learn and prepare. E remains work/loot."),LDCharter::Name(NearestTask->Presentation),LDDevelopment::SkillName(Skill));}
            return FString::Printf(TEXT("%s · Own %s score %.0f / public 12 · %s · E remains work/loot."),LDCharter::Name(NearestTask->Presentation),LDDevelopment::SkillName(Skill),LDDevelopment::SkillValue(Build,Skill),Slot==INDEX_NONE?TEXT("Learned but unequipped; prepare at a station"):Build.Cooldowns[uint8(Skill)]>Now?TEXT("Skill cooling down"):*FString::Printf(TEXT("Hold slot %d for %.0fs"),Slot+1,LDDevelopment::ChannelSeconds(Skill)));
        }
    }
    uint8 Target=0;FGuid Observed;if(!ChooseContext(Target,Observed)){return OwnerState.bDevelopmentProfile?TEXT("1–4: equipped skills. E work/loot; P build/books. Guild: Shift+E fabricates a Seal (Witness, 4 credits, 100 gold). Conquest unavailable."):TEXT("Approach Guild [G], West station [W] or Heart [H]. E interacts within 120 cm.");}
    if(Target==LDObjective::Seal){return TEXT("E: pick up visible Concord Seal within 80 cm.");}
    if(Target==5){return TEXT("E: pick up visible unread book within 80 cm. P: inventory and reading.");}
    if(Target==LDObjective::Heart)
    {return GS&&LDRules::HasHeartAccess(OwnerState.SealId.IsValid(),GS->DefensiveFlags)?TEXT("Hold E: claim Heart for 10 seconds. Other unprotected contenders contest within 240 cm."):(OwnerState.bDevelopmentProfile?TEXT("Heart locked: earn or recover a Concord Seal. Conquest port unavailable."):TEXT("Heart locked: carry a Seal or clear all four defences through the labelled fixture."));}
    if(Target==LDObjective::Guild)
    {
        if(OwnerState.ContractStage==2){return OwnerState.bDevelopmentProfile?TEXT("Hold E: deliver courier package for 3 seconds (+25 gold, +45 XP)."):TEXT("Hold E: deliver courier package for 3 seconds (+25 gold, +15 XP).");}
        if(OwnerState.ContractStage==1){if(const auto* D=LDRegion::Definition(GetWorld())){const auto* N=D->Node(OwnerState.ContractDestination);return FString::Printf(TEXT("Courier active: collect at %s."),N?*N->Name:TEXT("assigned town"));}return TEXT("Courier active: collect at West station [W].");}
        if(LDRegion::Definition(GetWorld())&&OwnerState.PaidJobs>=8){return TEXT("Eight paid contracts completed. Unused credits remain available.");}
        if(Now<OwnerState.WorkReadyAt){return FString::Printf(TEXT("Courier acceptance ready in %.1fs."),OwnerState.WorkReadyAt-Now);}
        return LDRegion::Definition(GetWorld())?TEXT("E: accept courier contract (eight paid per match; towns alternate)."):TEXT("E: accept courier contract. Then hold E at West station [W] to collect.");
    }
    if(const auto* D=LDRegion::Definition(GetWorld())){const auto* N=D->Node(OwnerState.ContractDestination);return OwnerState.ContractStage==1&&N&&N->Site==Target?TEXT("Hold E: collect assigned package for 3 seconds."):TEXT("Town station: preparation available; collect only at your assigned town.");}
    return OwnerState.ContractStage==1?TEXT("Hold E: collect courier package for 3 seconds."):TEXT("West station: accept a courier contract at Guild [G] first.");
}
void ALDPlayerController::InteractStarted()
{StartInteraction(IsInputKeyDown(EKeys::LeftShift)||IsInputKeyDown(EKeys::RightShift));}
void ALDPlayerController::SetFabricationHeld(bool Held)
{if(Held){StartInteraction(true);}else{InteractStopped();}}
void ALDPlayerController::StartInteraction(bool Fabricate)
{
    Navigation->Stop();
    if(HeldSkillSlot!=255){SkillStopped(HeldSkillSlot);}
    if(bControlsSuppressed||!OwnerState.bActive){return;}uint8 Target=0;FGuid Observed;
    if(!ChooseContext(Target,Observed)){IntentFeedback=ContextPrompt();return;}
    if(Fabricate&&OwnerState.bDevelopmentProfile&&LDRegion::Definition(GetWorld())&&FVector::DistSquared2D(GetPawn()->GetActorLocation(),LDRegion::Site(GetWorld(),1))<=120*120)
    {Target=LDObjective::Fabricator;Observed.Invalidate();}
    bInteractHeld=true;ContextToken=++NextRequest;HoldSequence=0;LastHoldSent=-1000;
    if(Target==5){bInteractHeld=false;ServerDevelopmentRequest(OwnerState.Match,OwnerState.Life,ContextToken,LDDevelopmentTransaction::PickUp,ELDSkill::None,Observed,FLDBuildPlan());return;}
    ServerContextRequest(OwnerState.Match,OwnerState.Life,ContextToken,Target,Observed);
}
void ALDPlayerController::InteractStopped()
{
    if(bInteractHeld){ServerContextRelease(OwnerState.Match,OwnerState.Life,ContextToken);}bInteractHeld=false;
}
void ALDPlayerController::ServerContextRequest_Implementation(uint32 Match,uint32 Life,uint32 Request,uint8 Target,FGuid Observed)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=Mode?Mode->StateFor(this):nullptr;
    if(!O||O->Match!=Match||O->Life!=Life||!Request||Target<1||(Target>4&&!((Target==6||Target==LDObjective::Fabricator)&&Mode->Region))||(Target==4)!=Observed.IsValid())
    {ClientIntentResult(Request,TEXT("Invalid interaction or stale life."));return;}
    if(const auto* Receipt=ContextReceipts.Find(Request)){ClientTransactionResult(OwnerState,Request,*Receipt);return;}
    if(Mode->IsFinished()){ClientIntentResult(Request,TEXT("Demo finished."));return;}
    if(Request<LastContextRequest){ClientIntentResult(Request,TEXT("Expired interaction request."));return;}
    const double Now=GetWorld()->GetTimeSeconds();ContextTokens=FMath::Min(2.,ContextTokens+FMath::Max(0.,Now-ContextRateAt)*6);ContextRateAt=Now;
    if(ContextTokens<1){ClientIntentResult(Request,TEXT("Interaction rate limited."));return;}
    --ContextTokens;LastContextRequest=Request;
    if(ContextReceipts.Num()>=32){uint32 Oldest=MAX_uint32;for(const auto& Pair:ContextReceipts){Oldest=FMath::Min(Oldest,Pair.Key);}ContextReceipts.Remove(Oldest);}
    ContextReceipts.Add(Request,TEXT("Interaction queued."));
    if(Mode->Objectives){Mode->Objectives->Queue(this,Match,Life,Request,Target,Observed);}else{CompleteContextRequest(Request,TEXT("Interactions unavailable."));}
}
void ALDPlayerController::ServerContextHold_Implementation(uint32 Match,uint32 Life,uint32 Token,uint32 Sequence,bool Moving)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=Mode?Mode->StateFor(this):nullptr;const double Now=GetWorld()->GetTimeSeconds();
    if(!O||O->Match!=Match||O->Life!=Life||Now-LastHoldReceived<.04){return;}LastHoldReceived=Now;
    if(Mode->Objectives&&!Mode->IsFinished()){Mode->Objectives->RefreshHold(this,Life,Token,Sequence,Moving,Now);}
}
void ALDPlayerController::ServerContextRelease_Implementation(uint32 Match,uint32 Life,uint32 Token)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=Mode?Mode->StateFor(this):nullptr;
    if(O&&O->Match==Match&&O->Life==Life&&Mode->Objectives){Mode->Objectives->Release(this,Life,Token);}
}
void ALDPlayerController::CompleteContextRequest(uint32 Request,const FString& Result)
{
    ContextReceipts.Add(Request,Result);ClientTransactionResult(OwnerState,Request,Result);
}
void ALDPlayerController::ClientTransactionResult_Implementation(const FLDOwnerState& State,uint32 Request,const FString& Result)
{ClientLifeReceipt_Implementation(State);ClientIntentResult_Implementation(Request,Result);}

void ALDPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(ALDPlayerController,OwnerState); }
ALDWorldViewRelay* ALDPlayerController::LocalView() const
{
    for (TActorIterator<ALDWorldViewRelay> It(GetWorld());It;++It) { if (It->GetOwner()==this) { return *It; } }
    return nullptr;
}
void ALDPlayerController::Ward()
{
    if (bControlsSuppressed) { return; }
    FHitResult Hit;
    if (GetHitResultUnderCursor(ECC_GameTraceChannel1,false,Hit)) { ServerPlaceWard(OwnerState.Match,OwnerState.Life,++NextRequest,Hit.ImpactPoint); }
}
void ALDPlayerController::ServerPlaceWard_Implementation(uint32 Match,uint32 Life,uint32 Request,FVector_NetQuantize10 Position)
{
    const double Now=GetWorld()->GetTimeSeconds();
    if (Now-LastWardAt<.25) { return; }
    LastWardAt=Now;
    ALDGameMode* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    FLDOwnerState* State=Mode ? Mode->StateFor(this) : nullptr;
    if (!State || Match!=State->Match || Life!=State->Life || !Request)
    { ClientIntentResult(Request,TEXT("Stale session or life.")); return; }
    if (Request==LastWardRequest) { ClientIntentResult(Request,LastWardResult); return; }
    if (Request<LastWardRequest) { ClientIntentResult(Request,TEXT("Expired request.")); return; }
    LastWardRequest=Request;
    LastWardResult=TEXT("Sight system unavailable.");
    if (Mode->Vision) { Mode->Vision->PlaceWard(this,Position,LastWardResult); }
    ClientIntentResult(Request,LastWardResult);
}
void ALDPlayerController::ClientIntentResult_Implementation(uint32 Request,const FString& Reason)
{
    if(Request==PendingAimRequest){PendingAimRequest=0;}
    if(Request)
    {
        if(SeenIntentResults.ContainsByPredicate([&](const auto& Result){return Result.Key==Request&&Result.Value==Reason;})){return;}
        if(SeenIntentResults.Num()>=128){SeenIntentResults.RemoveAt(0);}
        SeenIntentResults.Emplace(Request,Reason);
    }
    ++IntentFeedbackSerial;
    IntentFeedback=Reason;
#if !UE_BUILD_SHIPPING
    if(double* Sent=ProbeRequests.Find(Request)){ProbeAckMs.Add((FPlatformTime::Seconds()-*Sent)*1000);ProbeRequests.Remove(Request);}
#endif
}
void ALDPlayerController::ClientProbePhase_Implementation(uint8 Phase)
{
#if !UE_BUILD_SHIPPING
    if (Phase<=64 && LDNetworkProbeEnabled()) { ProbePhase=Phase; }
#endif
}
void ALDPlayerController::ServerProbeHello_Implementation(uint8 ProbeIdentity)
{
#if !UE_BUILD_SHIPPING
    const ALDGameMode* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if (Mode && (Mode->bScaffoldProfile||Mode->IsDevelopmentProfile()) && !ProbeRole && ProbeIdentity>=1 && ProbeIdentity<=2 && LDNetworkProbeEnabled()) { ProbeRole=ProbeIdentity; }
#endif
}
void ALDPlayerController::ServerProbePing_Implementation(double Sent)
{
#if !UE_BUILD_SHIPPING
    if(FMath::IsFinite(Sent)&&LDNetworkProbeEnabled()){ClientProbePong(Sent);}
#endif
}
void ALDPlayerController::ClientProbePong_Implementation(double Sent)
{
#if !UE_BUILD_SHIPPING
    if(FMath::IsFinite(Sent)&&LDNetworkProbeEnabled()){ProbeRtts.Add((FPlatformTime::Seconds()-Sent)*1000);}
#endif
}
void ALDPlayerController::TogglePublicSensorFixture()
{
#if !UE_BUILD_SHIPPING
    ALDGameMode* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if (HasAuthority() && IsLocalController() && Mode && Mode->bScaffoldProfile && Mode->Vision&&!Mode->IsFinished())
    { Mode->Vision->SetPublicSensor(!Mode->Vision->IsPublicSensorActive()); }
#endif
}
void ALDPlayerController::ToggleDefenceFixture(uint8 Index)
{
#if !UE_BUILD_SHIPPING
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* GS=GetWorld()->GetGameState<ALDGameState>();
    if(HasAuthority()&&IsLocalController()&&Mode&&Mode->Objectives&&Index<4){Mode->Objectives->SetDefencesFixture(GS->DefensiveFlags^(1<<Index));}
#endif
}
void ALDPlayerController::SeedSealFixture()
{
#if !UE_BUILD_SHIPPING
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if(HasAuthority()&&IsLocalController()&&Mode&&Mode->Objectives)
    {IntentFeedback=Mode->Objectives->SeedFixtureSeal()?TEXT("Fixture: one dropped Seal seeded at Guild. Pickup/drop/claim are real."):TEXT("Fixture seed unavailable: already seeded or demo finished.");}
#endif
}

void ALDPlayerController::Move(const FInputActionValue& Value)
{
    if(Navigation->Enabled()){return;}
    if (bControlsSuppressed || !OwnerState.bActive || (GetWorld()->GetGameState<ALDGameState>()&&GetWorld()->GetGameState<ALDGameState>()->EndReason)) { return; }
    if (APawn* ControlledPawn = GetPawn())
    {
        const FVector2D Axis = Value.Get<FVector2D>().GetClampedToMaxSize(1);
        const FRotator CameraYaw(0,-45,0);
        const FVector Direction = CameraYaw.RotateVector(FVector(Axis.Y,Axis.X,0));
        ControlledPawn->AddMovementInput(Direction.GetSafeNormal(), float(Direction.Size()));
    }
}

void ALDPlayerController::SprintStarted() { if (!bControlsSuppressed) { if (ALDCharacter* Body = Cast<ALDCharacter>(GetPawn())) { Body->SetSprint(true); } } }
void ALDPlayerController::SprintStopped() { if (ALDCharacter* Body = Cast<ALDCharacter>(GetPawn())) { Body->SetSprint(false); } }
void ALDPlayerController::Zoom(const FInputActionValue& Value) { if (!bControlsSuppressed) { if (ALDCharacter* Body = Cast<ALDCharacter>(GetPawn())) { Body->Zoom(Value.Get<float>()); } } }
void ALDPlayerController::ToggleMenu() { if (RootUI && !IsFrontend()) { RootUI->ToggleMenu(); } }

void ALDPlayerController::CancelInterruptibleHolds()
{
    InteractStopped();
    StopDevelopmentHold();
    if(HeldSkillSlot!=255){SkillStopped(HeldSkillSlot);}
}
void ALDPlayerController::ClearControls()
{
    CancelArmed();bPrimaryOwned=false;HeldObserved.Invalidate();
    Navigation->Stop();Navigation->Recenter(false);
    CancelInterruptibleHolds();
    SprintStopped();
    if (ALDCharacter* Body = Cast<ALDCharacter>(GetPawn()))
    {
        Body->ConsumeMovementInputVector();
        auto* Movement=Body->GetCharacterMovement();
        if(Navigation->Enabled()&&Movement->IsFalling()){Movement->Velocity.X=Movement->Velocity.Y=0;}else{Movement->StopMovementImmediately();}
    }
}

void ALDPlayerController::FlushPressedKeys() { Super::FlushPressedKeys();if(Navigation->Enabled()||IsFrontend()){bFreshInputRequired=true;}ClearControls(); }

void ALDPlayerController::SetMenuOpen(bool bOpen)
{
    if(Navigation->Enabled()||IsFrontend()){bFreshInputRequired=true;}
    if (bControlsSuppressed != bOpen)
    {
        bControlsSuppressed = bOpen;
        SetIgnoreMoveInput(bOpen);
    }
    ClearControls();
    if (bOpen)
    {
        FInputModeGameAndUI Mode;
        Mode.SetHideCursorDuringCapture(false);
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        if (RootUI) { Mode.SetWidgetToFocus(RootUI->TakeWidget()); }
        SetInputMode(Mode);
    }
    else
    {
        FInputModeGameAndUI Mode;
        Mode.SetHideCursorDuringCapture(false);
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        SetInputMode(Mode);
    }
}

void ALDPlayerController::PlayerTick(float Delta)
{
    Super::PlayerTick(Delta);
    UpdateV05Inputs(Delta);
    if(IsLocalController()&&DevelopmentHoldToken&&GetWorld()->GetTimeSeconds()-LastDevelopmentHoldSent>=.1)
    {
        LastDevelopmentHoldSent=GetWorld()->GetTimeSeconds();const APawn* HeldBody=GetPawn();
        ServerDevelopmentHold(OwnerState.Match,OwnerState.Life,DevelopmentHoldToken,++DevelopmentHoldSequence,HeldBody&&!HeldBody->GetLastMovementInputVector().IsNearlyZero());
    }
    if(IsLocalController()&&HeldSkillSlot!=255&&GetWorld()->GetTimeSeconds()-LastSkillHoldSent>=.1)
    {
        LastSkillHoldSent=GetWorld()->GetTimeSeconds();const APawn* HeldBody=GetPawn();
        ServerSkillHold(OwnerState.Match,OwnerState.Life,SkillHoldToken,++SkillHoldSequence,HeldBody&&!HeldBody->GetLastMovementInputVector().IsNearlyZero());
    }
    if(IsLocalController()&&bInteractHeld&&GetWorld()->GetTimeSeconds()-LastHoldSent>=.1)
    {
        LastHoldSent=GetWorld()->GetTimeSeconds();const APawn* Body=GetPawn();
        ServerContextHold(OwnerState.Match,OwnerState.Life,ContextToken,++HoldSequence,Body&&!Body->GetLastMovementInputVector().IsNearlyZero());
    }
    const auto* PublicState=GetWorld()->GetGameState<ALDGameState>();const bool bFinished=PublicState&&PublicState->EndReason;
    if(bFinished)
    {if(IsLocalController()){bInteractHeld=false;StopDevelopmentHold();if(HeldSkillSlot!=255){SkillStopped(HeldSkillSlot);}if(auto* Body=Cast<ALDCharacter>(GetPawn())){Body->GetCharacterMovement()->DisableMovement();}}}
    if(IsLocalController()&&!IsFrontend())
    {
        if(ALDCharacter* Active=Cast<ALDCharacter>(GetPawn()))
        {
            LastBodyPosition=Active->GetActorLocation();
            if(WaitingCamera){SetViewTarget(Active);WaitingCamera->Destroy();WaitingCamera=nullptr;}
            auto* Move=CastChecked<ULDCharacterMovementComponent>(Active->GetCharacterMovement());
            Move->bCombatWalkOnly=(OwnerState.ActionCue&15)==2||(OwnerState.ActionCue&15)==3||(OwnerState.ActionCue&15)==6;
            Move->bCanWalkOffLedges=(OwnerState.ActionCue&15)!=4;
        }
        else if(!OwnerState.bActive&&!WaitingCamera)
        {
            WaitingCamera=GetWorld()->SpawnActor<ACameraActor>();
            const FRotator Rotation(-60,-45,0);
            WaitingCamera->SetActorLocation(LastBodyPosition+FVector(0,0,20)-Rotation.Vector()*1400);
            WaitingCamera->SetActorRotation(Rotation);
            auto* Camera=WaitingCamera->GetCameraComponent();Camera->FieldOfView=55;
            Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness=true;Camera->PostProcessSettings.AutoExposureMinBrightness=0;
            Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness=true;Camera->PostProcessSettings.AutoExposureMaxBrightness=0;
            Camera->PostProcessSettings.bOverride_MotionBlurAmount=true;Camera->PostProcessSettings.MotionBlurAmount=0;
            SetViewTarget(WaitingCamera);
        }
    }
    if (bFinished || !IsLocalController() || bControlsSuppressed || !OwnerState.bActive) { return; }
    ALDCharacter* Body = Cast<ALDCharacter>(GetPawn());
    if (!Body) { return; }
    if(Navigation->Enabled()){return;}
    FHitResult Hit;
    if (GetHitResultUnderCursor(ECC_GameTraceChannel1, false, Hit) && Hit.ImpactNormal.Z >= FMath::Cos(FMath::DegreesToRadians(40.0)))
    {
        FVector Direction = Hit.ImpactPoint - Body->GetActorLocation();
        Direction.Z = 0;
        if (!Direction.IsNearlyZero() && !Direction.ContainsNaN())
        {
            const float Yaw = Direction.Rotation().Yaw;
            Body->SetFacing(Yaw);
            AimSendTime += Delta;
            if (AimSendTime >= .1f) { AimSendTime = 0; ServerSetAim(OwnerState.Match,OwnerState.Life,Yaw); }
        }
    }
}

void ALDPlayerController::ServerSetAim_Implementation(uint32 Match,uint32 Life,float Yaw)
{
    const double Now = GetWorld()->GetTimeSeconds();
    ALDGameMode* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    const FLDOwnerState* State=Mode ? Mode->StateFor(this) : nullptr;
    if(Mode&&Mode->IsUIControlProfile()){return;}
    if (!State || !State->bActive || Match!=State->Match || Life!=State->Life || !FMath::IsFinite(Yaw) || Now - LastAcceptedAimTime < .04) { return; }
    LastAcceptedAimTime = Now;
    if (ALDCharacter* Body = Cast<ALDCharacter>(GetPawn())) { Body->SetFacing(Yaw); }
}
