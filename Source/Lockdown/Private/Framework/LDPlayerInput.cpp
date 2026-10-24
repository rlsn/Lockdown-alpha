#include "Combat/LDTacticalCatalog.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "UI/LDControlSettings.h"
#include "UI/LDControlsPanel.h"
#include "UI/LDDevelopmentPanel.h"
#include "UI/LDUIRoot.h"
#include "UI/LDHUD.h"
#include "UI/LDHUDModel.h"
#include "Objectives/LDObjectiveRules.h"
#include "UI/LDInventoryPanel.h"
#include "Navigation/LDNavigationComponent.h"
#include "Character/LDCharacter.h"
#include "Vision/LDWorldViewRelay.h"
#include "Development/LDWorldChecks.h"
#include "Development/LDDevelopmentDirector.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
    bool PhysicalDown(const ALDPlayerController* PC,FKey Key)
    {
#if PLATFORM_WINDOWS
        if(!FApp::IsUnattended())
        {const uint32 *Code=nullptr,*Character=nullptr;FInputKeyManager::Get().GetCodesFromKey(Key,Code,Character);if(Code){return (::GetAsyncKeyState(int32(*Code))&0x8000)!=0;}}
#endif
        return PC->IsInputKeyDown(Key);
    }
    int32 Numbered(FName Action,const TCHAR* Prefix)
    {const FString Text=Action.ToString();const int32 Length=FCString::Strlen(Prefix);return Text.StartsWith(Prefix)&&Text.Len()==Length+1&&Text[Length]>=TEXT('1')&&Text[Length]<=TEXT('4')?Text[Length]-TEXT('1'):-1;}
}
FKey ALDPlayerController::BindingKey(FName Action) const {return Controls?Controls->Key(Action):FKey();}
FString ALDPlayerController::BindingLabel(FName Action) const {return Controls?Controls->Label(Action):Action.ToString();}
double ALDPlayerController::ArmedRange() const
{if(OwnerState.Build.RulesVersion==7&&ArmedSkill>=0){const auto* C=ULDTacticalCatalog::Load();const auto* D=C?C->Find(OwnerState.Build.Slots[ArmedSkill]):nullptr;if(D){return D->Range;}}return bAttackArmed||ArmedWard.IsValid()?96:ArmedSkill>=0&&OwnerState.Build.Slots.IsValidIndex(ArmedSkill)?(OwnerState.Build.Slots[ArmedSkill]==ELDSkill::Step?LDDevelopment::SkillValue(OwnerState.Build,ELDSkill::Step):LDHUD::Range(OwnerState.Build.Slots[ArmedSkill])):0;}
FString ALDPlayerController::HeldInputLabel() const
{
    if(OwnerState.DevelopmentKind){return HeldQuickSlot>=0?(HeldQuickGesture==2?TEXT("mouse button"):BindingLabel(FName(*FString::Printf(TEXT("Item%d"),HeldQuickSlot+1)))):BindingLabel(TEXT("Context"));}
    return HeldSkillGesture==1?BindingLabel(TEXT("Select")):HeldSkillGesture==2?TEXT("mouse button"):HeldSkillGesture==3?BindingLabel(TEXT("Context")):BindingLabel(FName(*FString::Printf(TEXT("Skill%d"),HeldSkillSlot+1)));
}
FString ALDPlayerController::ControlSummary() const
{
    auto K=[this](const TCHAR* A){return BindingLabel(FName(A));};
    return K(TEXT("Select"))+TEXT(" inspect / confirm · ")+K(TEXT("Navigate"))+TEXT(" move / cancel · ")+K(TEXT("Attack"))+TEXT(" arm attack · ")+K(TEXT("Stop"))+TEXT(" stop · ")+K(TEXT("Sprint"))+TEXT(" sprint\n")
        +K(TEXT("Skill1"))+TEXT(" / ")+K(TEXT("Skill2"))+TEXT(" / ")+K(TEXT("Skill3"))+TEXT(" / ")+K(TEXT("Skill4"))+TEXT(" equipped skills · ")+K(TEXT("Context"))+TEXT(" hold context · ")+K(TEXT("Development"))+TEXT(" character\n")
        +K(TEXT("PanUp"))+TEXT(" / ")+K(TEXT("PanDown"))+TEXT(" / ")+K(TEXT("PanLeft"))+TEXT(" / ")+K(TEXT("PanRight"))+TEXT(" camera · ")+K(TEXT("Recenter"))+TEXT(" hold recenter · ")+K(TEXT("Follow"))+TEXT(" follow · ")+K(TEXT("Help"))+TEXT(" controls · ")+K(TEXT("Back"))+TEXT(" Back");
}
void ALDPlayerController::PressBasicAttack(){if(Navigation->Enabled()){ArmAttack();}else{Attack();}}
FVector2D ALDPlayerController::CameraPanAxis() const
{return FVector2D((IsInputKeyDown(BindingKey(TEXT("PanRight")))?1:0)-(IsInputKeyDown(BindingKey(TEXT("PanLeft")))?1:0),(IsInputKeyDown(BindingKey(TEXT("PanUp")))?1:0)-(IsInputKeyDown(BindingKey(TEXT("PanDown")))?1:0));}
void ALDPlayerController::RebuildV05Mappings()
{
    if(!IsLocalController()||!Controls||(!Navigation->Enabled()&&!IsFrontend())){return;}
    auto* Local=GetLocalPlayer();auto* Input=Cast<UEnhancedInputComponent>(InputComponent);if(!Local||!Input){return;}
    auto* Subsystem=Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
    for(uint32 Handle:V05BindingHandles){Input->RemoveBindingByHandle(Handle);}V05BindingHandles.Reset();
    if(auto* Old=LoadObject<UInputMappingContext>(nullptr,TEXT("/Game/Lockdown/Generated/Input/IMC_Gameplay.IMC_Gameplay"))){Subsystem->RemoveMappingContext(Old);}
    if(SkillInputContext){Subsystem->RemoveMappingContext(SkillInputContext);}
    if(NavigationInputContext){Subsystem->RemoveMappingContext(NavigationInputContext);}
    CancelInterruptibleHolds();CancelArmed();
    NavigationInputContext=NewObject<UInputMappingContext>(this);
    for(const auto& B:ULDControlSettings::Defaults())
    {
        auto* Action=NewObject<UInputAction>(NavigationInputContext);Action->ValueType=B.Axis?EInputActionValueType::Axis1D:EInputActionValueType::Boolean;
        NavigationInputContext->MapKey(Action,Controls->Key(B.Action));
        if(B.Axis){V05BindingHandles.Add(Input->BindAction(Action,ETriggerEvent::Triggered,this,&ALDPlayerController::V05Zoom).GetHandle());}
        else
        {V05BindingHandles.Add(Input->BindAction(Action,ETriggerEvent::Started,this,&ALDPlayerController::V05Pressed,B.Action).GetHandle());V05BindingHandles.Add(Input->BindAction(Action,ETriggerEvent::Completed,this,&ALDPlayerController::V05Released,B.Action).GetHandle());V05BindingHandles.Add(Input->BindAction(Action,ETriggerEvent::Canceled,this,&ALDPlayerController::V05Released,B.Action).GetHandle());}
    }
    FModifyContextOptions Options;Options.bIgnoreAllPressedKeysUntilRelease=true;
    Subsystem->AddMappingContext(NavigationInputContext,10,Options);bNavigationContextActive=true;bFreshInputRequired=true;
    Navigation->bEdgePan=Controls->bEdgePan;
}
void ALDPlayerController::UpdateV05Inputs(float Delta)
{
    if(!IsLocalController()||(!Navigation->Enabled()&&!IsFrontend())){return;}
    if(!Controls){Controls=ULDControlSettings::Load();}
    if(Navigation->Enabled()&&!IsFrontend()&&!HUDUI){HUDUI=CreateWidget<ULDHUD>(this);HUDUI->AddToViewport(30);}
    if(!bNavigationContextActive){RebuildV05Mappings();Navigation->bFollowCamera=Controls->bStartFollowing;}
    if(bFreshInputRequired)
    {
        bool Down=PhysicalDown(this,EKeys::LeftMouseButton)||PhysicalDown(this,EKeys::RightMouseButton);
        for(const auto& B:ULDControlSettings::Defaults()){Down|=PhysicalDown(this,Controls->Key(B.Action));}
        if(!Down){bFreshInputRequired=false;}
    }
    if(GetPawn()!=PreferencesPawn.Get())
    {PreferencesPawn=GetPawn();if(auto* Body=Cast<ALDCharacter>(GetPawn())){Body->CameraArm->TargetArmLength=Controls->Zoom;}}
    if(PreferenceSaveAt>0&&GetWorld()->GetTimeSeconds()>=PreferenceSaveAt){if(!Controls->Save()){IntentFeedback=TEXT("Zoom changed for this session; preferences could not be saved.");}PreferenceSaveAt=0;}
    const auto* View=LocalView();
    auto Visible=[&](FGuid Id){return View&&!View->IsStale()&&View->LastView().Generation==OwnerState.Match&&View->LastView().Entities.ContainsByPredicate([&](const FLDObservedEntity& E){return E.Id==Id;});};
    if(Inspected.IsValid()&&(!Visible(Inspected)||InspectedGeneration!=OwnerState.Match))
    {Inspected.Invalidate();CancelArmed();if(HeldObserved.IsValid()&&HeldSkillSlot!=255){SkillStopped(HeldSkillSlot);}IntentFeedback=TEXT("Target no longer visible");}
    if(HeldObserved.IsValid()&&!Visible(HeldObserved)&&HeldSkillSlot!=255){SkillStopped(HeldSkillSlot);HeldObserved.Invalidate();}
    if(bPrimaryOwned&&!FApp::IsUnattended()&&FSlateApplication::IsInitialized())
    {
        const FKey Primary=BindingKey(TEXT("Select"));
        if(!PhysicalDown(this,Primary)){V05Released(TEXT("Select"));}
    }
    if(HeldSkillGesture==2&&HeldSkillSlot!=255&&!FApp::IsUnattended()&&!PhysicalDown(this,EKeys::LeftMouseButton)){SkillStopped(HeldSkillSlot);}
    if(HeldQuickSlot>=0&&HeldQuickGesture==2&&!FApp::IsUnattended()&&!PhysicalDown(this,EKeys::LeftMouseButton)){StopDevelopmentHold();}
    if(ArmedWard.IsValid()||ReadArmed.IsValid()){const auto* Item=OwnerState.Inventory.Find(ArmedWard.IsValid()?ArmedWard:ReadArmed);if(!Item||Item->bReserved||Item->Revision!=ArmedItemRevision){CancelArmed();IntentFeedback=TEXT("Item changed; choose it again");}}
    if(InventoryUI){InventoryUI->Refresh();}
    if(HUDUI){HUDUI->RefreshReadModel();}
}
void ALDPlayerController::CancelArmed()
{bProductionArmed=false;ArmedSkill=-1;bAttackArmed=bDraftArmed=bFabricationArmed=false;ReadArmed.Invalidate();ArmedWard.Invalidate();PreparedDraft=FLDBuildPlan();}
void ALDPlayerController::ToggleControlsPanel()
{
    if(InventoryUI&&InventoryUI->IsOpen()){InventoryUI->Close();}
    if(ControlsUI&&ControlsUI->IsOpen()){ControlsUI->Close();return;}
    if(DevelopmentUI&&DevelopmentUI->IsOpen()){DevelopmentUI->Close();}
    if(RootUI){RootUI->DismissMenu();}
    if(!Controls){Controls=ULDControlSettings::Load();}
    if(!ControlsUI){ControlsUI=CreateWidget<ULDControlsPanel>(this);ControlsUI->AddToViewport(25);}
    ControlsUI->Open();
}
void ALDPlayerController::V05Back()
{
    if(IsTargeting()||bProductionArmed||bDraftArmed||bFabricationArmed||ReadArmed.IsValid()){CancelArmed();return;}
    if(InventoryUI&&InventoryUI->IsOpen()){InventoryUI->Close();return;}
    if(ControlsUI&&ControlsUI->IsOpen()){ControlsUI->Close();return;}
    if(DevelopmentUI&&DevelopmentUI->IsOpen()){DevelopmentUI->Close();return;}
    if(RootUI&&RootUI->IsMenuOpen()){RootUI->ToggleMenu();return;}
    if(bPrimaryOwned||HeldSkillSlot!=255||bInteractHeld||DevelopmentHoldToken){ClearControls();return;}
    if(!bFreshInputRequired&&RootUI){RootUI->ToggleMenu();}
}
void ALDPlayerController::V05Pressed(FName Action)
{
    if(Action==TEXT("Back")){V05Back();return;}
    if(Action==TEXT("Help")){ToggleControlsPanel();return;}
    if(Action==TEXT("Development")&&!OwnerState.bActive){ToggleDevelopmentPanel();return;}
    if(Action==TEXT("Inventory")&&!OwnerState.bActive&&!bControlsSuppressed){ToggleInventoryPanel();return;}
    if(!CanWorldCommand()){return;}
    const int32 Skill=Numbered(Action,TEXT("Skill"));if(Skill>=0){ArmSkill(uint8(Skill));return;}
    const int32 QuickSlot=Numbered(Action,TEXT("Item"));if(QuickSlot>=0){SetQuickItemHeld(uint8(QuickSlot),true);return;}
    if(Action==TEXT("Select"))
    {
        if(RootUI&&RootUI->PointerOverUI()){return;}
        FHitResult Hit;const FGuid Target=ObservedUnderPointer();
        if(GetHitResultUnderCursor(ECC_GameTraceChannel1,false,Hit))
        {bPrimaryOwned=true;if(IsTargeting()){ConfirmArmed(Hit.ImpactPoint,Target);}else{Inspect(Target);}}
    }
    else if(Action==TEXT("Navigate")){if(RootUI&&RootUI->PointerOverUI()){return;}if(IsTargeting()||bProductionArmed||bDraftArmed||ReadArmed.IsValid()||bFabricationArmed){CancelArmed();}else{Navigation->ClickMove();}}
    else if(Action==TEXT("Attack")){ArmAttack();}
    else if(Action==TEXT("Context")){ContextV05();}
    else if(Action==TEXT("Stop")){StopOrder();CancelArmed();}
    else if(Action==TEXT("Sprint")){SprintStarted();}
    else if(Action==TEXT("Recenter")){Navigation->Recenter(true);}
    else if(Action==TEXT("Follow")){Navigation->ToggleFollow();}
    else if(Action==TEXT("Development")){ToggleDevelopmentPanel();}
    else if(Action==TEXT("Inventory")){ToggleInventoryPanel();}
    else if(Action==TEXT("Trade")){OfferTrade();}
}
void ALDPlayerController::V05Released(FName Action)
{
    if(Action==TEXT("Sprint")){SprintStopped();}
    if(Action==TEXT("Recenter")){Navigation->Recenter(false);}
    if(Action==TEXT("Select")){bPrimaryOwned=false;if(HeldSkillGesture==1&&HeldSkillSlot!=255){SkillStopped(HeldSkillSlot);}}
    if(Action==TEXT("Context")){InteractStopped();StopDevelopmentHold();StopProductionHold();if(HeldSkillGesture==3&&HeldSkillSlot!=255){SkillStopped(HeldSkillSlot);}}
    const int32 Slot=Numbered(Action,TEXT("Skill"));if(Slot>=0&&HeldSkillGesture==0){SkillStopped(uint8(Slot));}
    const int32 QuickSlot=Numbered(Action,TEXT("Item"));if(QuickSlot>=0){SetQuickItemHeld(uint8(QuickSlot),false);}
}
void ALDPlayerController::V05Zoom(const FInputActionValue& Value)
{if(CanWorldCommand()&&(!RootUI||!RootUI->PointerOverUI())){if(auto* Body=Cast<ALDCharacter>(GetPawn())){Body->Zoom(Value.Get<float>());Controls->Zoom=Body->CameraArm->TargetArmLength;PreferenceSaveAt=GetWorld()->GetTimeSeconds()+.5;}}}
void ALDPlayerController::ArmAttack(){if(CanWorldCommand()){CancelArmed();bAttackArmed=true;IntentFeedback=TEXT("Choose an attack direction. ")+BindingLabel(TEXT("Navigate"))+TEXT(" cancels.");}}
void ALDPlayerController::ArmSkill(uint8 Slot,uint8 Gesture)
{
    if(!CanWorldCommand()||!OwnerState.Build.Slots.IsValidIndex(Slot)||OwnerState.Build.Slots[Slot]==ELDSkill::None){IntentFeedback=TEXT("Empty skill slot");return;}
    const bool SelfPurge=(OwnerState.Build.Slots[Slot]==ELDSkill::PurgeTouch||OwnerState.Build.Slots[Slot]==ELDSkill::RemedyBind)&&ArmedSkill==Slot;
    CancelArmed();
    if((OwnerState.Build.Slots[Slot]==ELDSkill::PurgeTouch||OwnerState.Build.Slots[Slot]==ELDSkill::RemedyBind)&&!SelfPurge){ArmedSkill=Slot;IntentFeedback=TEXT("Confirm an observed body, or press this skill again to target yourself. External cleansing requires aid consent.");return;}
    if(OwnerState.Build.Slots[Slot]==ELDSkill::Patch||OwnerState.Build.Slots[Slot]==ELDSkill::PurgeTouch||OwnerState.Build.Slots[Slot]==ELDSkill::SuretyShell||OwnerState.Build.Slots[Slot]==ELDSkill::VeilStep||OwnerState.Build.Slots[Slot]==ELDSkill::RemedyBind){HeldSkillGesture=Gesture;SkillStarted(Slot);return;}
    if(OwnerState.Build.RulesVersion==7){const auto* C=ULDTacticalCatalog::Load();const auto* D=C?C->Find(OwnerState.Build.Slots[Slot]):nullptr;if(D&&(D->Target==ELDTacticalTarget::Ground||D->Target==ELDTacticalTarget::Cone)){ArmedSkill=Slot;IntentFeedback=FString::Printf(TEXT("%s: choose ground, range %d cm. Cancel with move/stop."),LDDevelopment::SkillName(D->Skill),D->Range);return;}}
    ArmedSkill=Slot;IntentFeedback=FString::Printf(TEXT("%s: choose a visible target or direction. %s cancels."),LDDevelopment::SkillName(OwnerState.Build.Slots[Slot]),*BindingLabel(TEXT("Navigate")));
}
FGuid ALDPlayerController::ObservedUnderPointer() const
{
    const auto* R=LocalView();float X=0,Y=0;if(!R||R->IsStale()||!GetMousePosition(X,Y)){return {};}
    FGuid Best;double Distance=32*32;
    for(const auto& E:R->LastView().Entities)
    {FVector2D Screen;if(ProjectWorldLocationToScreen(E.Position,Screen)){const double D=FVector2D::DistSquared(Screen,FVector2D(X,Y));if(D<Distance){Best=E.Id;Distance=D;}}}
    return Best;
}
void ALDPlayerController::Inspect(FGuid Observed)
{
    Inspected=Observed;InspectedGeneration=OwnerState.Match;FLDObservedEntity Entry;
    if(!ReadInspected(Entry)){Inspected.Invalidate();}
}
bool ALDPlayerController::ReadInspected(FLDObservedEntity& Out) const
{
    const auto* R=LocalView();if(!Inspected.IsValid()||!R||R->IsStale()||R->LastView().Generation!=OwnerState.Match){return false;}
    if(const auto* E=R->LastView().Entities.FindByPredicate([&](const FLDObservedEntity& V){return V.Id==Inspected;})){Out=*E;return true;}return false;
}
void ALDPlayerController::BeginTargetSkill(uint8 Slot,FGuid Target,const FVector& Aim,uint8 Gesture)
{
    const auto Skill=OwnerState.Build.Slots[Slot];
    if(Skill!=ELDSkill::Step&&!Target.IsValid()){IntentFeedback=TEXT("Choose an observed target");return;}
    if(HeldSkillSlot!=255){SkillStopped(HeldSkillSlot);}
    if(LDDevelopment::Held(Skill)){Navigation->Stop();CancelInterruptibleHolds();}
    const uint32 Token=++NextRequest;
    PendingAimRequest=Token;PendingSkillSlot=Slot;if(auto* Body=Cast<ALDCharacter>(GetPawn())){Body->SetFacing(Aim.Rotation().Yaw);}
    if(LDDevelopment::Held(Skill)){HeldSkillSlot=Slot;HeldSkillGesture=Gesture;HeldObserved=Target;SkillHoldToken=Token;SkillHoldSequence=0;LastSkillHoldSent=-1000;}
    if(OwnerState.Build.RulesVersion==7){Navigation->Stop();SendTacticalAction(Token,Slot,Aim,Target);}else{ServerSkillTargetAction(OwnerState.Match,OwnerState.Life,Token,OwnerState.Build.Revision,Slot,Aim,Target);}
}
void ALDPlayerController::ConfirmArmed(const FVector& Ground,FGuid Target)
{
    if(!CanWorldCommand()||!GetPawn()){return;}
    if(ArmedWard.IsValid()){const FGuid Item=ArmedWard;CancelArmed();InventoryCommand(LDInventory::Place,Item,0,Ground);return;}
    if(OwnerState.Build.RulesVersion==7&&ArmedSkill>=0)
    {
        const uint8 Slot=uint8(ArmedSkill);const auto* C=ULDTacticalCatalog::Load();const auto* D=C?C->Find(OwnerState.Build.Slots[Slot]):nullptr;
        if(D&&(D->Target==ELDTacticalTarget::Ground||D->Target==ELDTacticalTarget::Cone))
        {FVector Aim=(Ground-GetPawn()->GetActorLocation()).GetSafeNormal2D();if(Aim.IsNearlyZero()){Aim=FVector::ForwardVector;}CancelArmed();Navigation->Stop();SendTacticalAction(++NextRequest,Slot,Aim,FGuid(),Ground);return;}
    }
    FVector Point=Ground;
    if(Target.IsValid())
    {
        const auto* R=LocalView();const auto* E=R&&!R->IsStale()?R->LastView().Entities.FindByPredicate([&](const FLDObservedEntity& V){return V.Id==Target;}):nullptr;
        if(!E){IntentFeedback=TEXT("Target no longer visible");CancelArmed();return;}Point=E->Position;
    }
    const FVector Aim=(Point-GetPawn()->GetActorLocation()).GetSafeNormal2D();if(Aim.IsNearlyZero()){return;}
    if(bAttackArmed){CancelArmed();PendingAimRequest=++NextRequest;PendingSkillSlot=255;CastChecked<ALDCharacter>(GetPawn())->SetFacing(Aim.Rotation().Yaw);ServerCombatAction(OwnerState.Match,OwnerState.Life,PendingAimRequest,0,Aim);}
    else if(ArmedSkill>=0){const uint8 Slot=uint8(ArmedSkill);CancelArmed();BeginTargetSkill(Slot,Target,Aim,1);}
}
void ALDPlayerController::EndSkillGesture(uint8 Slot,uint8 Gesture){if(HeldSkillSlot==Slot&&HeldSkillGesture==Gesture){SkillStopped(Slot);}}
void ALDPlayerController::ArmPreparation(const FLDBuildPlan& Plan)
{
    FLDCharacterBuild Candidate;FString Error;if(!LDDevelopment::Prepare(OwnerState.Build,Plan,Candidate,Error)){IntentFeedback=Error;return;}
    const FLDBuildPlan Copy=Plan;if(DevelopmentUI&&DevelopmentUI->IsOpen()){DevelopmentUI->Close();}
    CancelArmed();PreparedDraft=Copy;bDraftArmed=true;bFreshInputRequired=true;IntentFeedback=TEXT("Hold ")+BindingLabel(TEXT("Context"))+TEXT(" at the station to apply your preview.");
}
void ALDPlayerController::ArmRead(FGuid Book)
{
    const auto* Item=OwnerState.Inventory.Find(Book);if(Navigation->Enabled()&&(!Item||Item->Kind!=ELDItemKind::Book||Item->bReserved)){IntentFeedback=TEXT("Owned book unavailable");return;}const uint32 Revision=Item?Item->Revision:0;
    if(DevelopmentUI&&DevelopmentUI->IsOpen()){DevelopmentUI->Close();}
    CancelArmed();ReadArmed=Book;ArmedItemRevision=Revision;bFreshInputRequired=true;IntentFeedback=TEXT("Hold ")+BindingLabel(TEXT("Context"))+TEXT(" to read. Closing the panel spends nothing.");
}
void ALDPlayerController::ArmFabrication(){if(const auto* G=GetWorld()->GetGameState<ALDGameState>();G&&G->bUIControlProfile){if(!InventoryUI||!InventoryUI->IsOpen()){ToggleInventoryPanel();}if(InventoryUI){InventoryUI->PreviewProduction(8);}return;}CancelArmed();bFabricationArmed=true;IntentFeedback=TEXT("Hold ")+BindingLabel(TEXT("Context"))+TEXT(" at Guild to fabricate a Seal.");}
void ALDPlayerController::ContextV05()
{
    if(bProductionArmed){StartProductionHold();return;}
    if(bDraftArmed){const auto Plan=PreparedDraft;CancelArmed();StartDevelopmentHold(LDDevelopmentTransaction::Prepare,FGuid(),Plan);return;}
    if(ReadArmed.IsValid()){const FGuid Book=ReadArmed;CancelArmed();StartDevelopmentHold(LDDevelopmentTransaction::Read,Book,FLDBuildPlan());return;}
    if(bFabricationArmed){CancelArmed();StartInteraction(true);return;}
    FLDObservedEntity Item;if(ReadInspected(Item)&&LDVision::PickupKind(Item.Kind)){BeginPickup(Item.Id);return;}
    FLDObservedEntity E;
    if(ReadInspected(E)&&E.Kind==ELDObservedKind::Production){if(E.Presentation<2){ProductionPlan=FLDProductionPlan();ProductionPlan.Site=E.Presentation;ProductionPlan.InventoryRevision=OwnerState.Inventory.Revision;StartProductionHold();}else{ToggleInventoryPanel();}return;}
    if(ReadInspected(E)&&(E.Kind==ELDObservedKind::Charter||E.Kind==ELDObservedKind::WatchControl))
    {
        const auto Skill=E.Kind==ELDObservedKind::WatchControl?ELDSkill::Analyze:LDCharter::Skill(E.Presentation);const int32 Slot=OwnerState.Build.Slots.Find(Skill);
        if(Slot==INDEX_NONE){IntentFeedback=TEXT("Equip this skill at a preparation station");return;}
        BeginTargetSkill(uint8(Slot),E.Id,(FVector(E.Position)-GetPawn()->GetActorLocation()).GetSafeNormal2D(),3);return;
    }
    for(uint8 Site=0;Site<2;++Site){if(NearProduction(Site)){ProductionPlan=FLDProductionPlan();ProductionPlan.Site=Site;ProductionPlan.InventoryRevision=OwnerState.Inventory.Revision;StartProductionHold();return;}}
    StartInteraction(false);
}
FString ALDPlayerController::ContextPrompt() const
{
    FString Text=LegacyContextPrompt();if(!Navigation->Enabled()||!Controls){return Text;}
    const FString Context=BindingLabel(TEXT("Context"));
    Text.ReplaceInline(TEXT("Shift+E"),*(FString(TEXT("Fabricate, then hold "))+Context));
    Text.ReplaceInline(TEXT("Hold E"),*(FString(TEXT("Hold "))+Context));Text.ReplaceInline(TEXT("hold E"),*(FString(TEXT("hold "))+Context));
    Text.ReplaceInline(TEXT("E:"),*(Context+TEXT(":")));Text.ReplaceInline(TEXT("E remains"),*(Context+TEXT(" remains")));Text.ReplaceInline(TEXT("E work"),*(Context+TEXT(" work")));
    Text.ReplaceInline(TEXT("P:"),*(BindingLabel(TEXT("Development"))+TEXT(":")));Text.ReplaceInline(TEXT("P build"),*(BindingLabel(TEXT("Development"))+TEXT(" build")));
    for(int32 I=0;I<4;++I){Text.ReplaceInline(*FString::Printf(TEXT("slot %d"),I+1),*BindingLabel(FName(*FString::Printf(TEXT("Skill%d"),I+1))));}
    Text.ReplaceInline(TEXT("1–4: equipped skills"),*(BindingLabel(TEXT("Skill1"))+TEXT("/")+BindingLabel(TEXT("Skill2"))+TEXT("/")+BindingLabel(TEXT("Skill3"))+TEXT("/")+BindingLabel(TEXT("Skill4"))+TEXT(": equipped skills")));
    if(bProductionArmed){return TEXT("Production preview armed. Hold ")+Context+TEXT(" at ")+ALDProductionDirector::SiteName(ProductionPlan.Site);}
    if(bDraftArmed){return TEXT("Preview armed. Hold ")+Context+TEXT(" at the station to apply; Escape cancels.");}
    if(ReadArmed.IsValid()){return TEXT("Book armed. Hold ")+Context+TEXT(" to learn; Escape cancels.");}
    if(bFabricationArmed){return TEXT("Fabrication armed. Hold ")+Context+TEXT(" at Guild; Escape cancels.");}
    if(const auto* G=GetWorld()->GetGameState<ALDGameState>();G&&G->bUIControlProfile){uint8 Target=0;FGuid Observed;if(ChooseContext(Target,Observed)&&(Target==LDObjective::Guild||Target==LDObjective::Heart||OwnerState.ContractStage)){return Text+TEXT(" · Bag: services");}return LDHUD::EconomyStep(OwnerState,*G,G->GetServerWorldTimeSeconds())+TEXT(" · Bag: services");}
    for(uint8 Site=0;Site<5;++Site){if(NearProduction(Site)){return Site<2?TEXT("Hold ")+Context+TEXT(" 6s: ")+ALDProductionDirector::SiteName(Site)+TEXT(" · finite match charges · Bag: Survey preview"):ALDProductionDirector::SiteName(Site)+TEXT(" · Bag: choose exact inputs, preview, then arm Hold ")+Context;}}
    return Text;
}
