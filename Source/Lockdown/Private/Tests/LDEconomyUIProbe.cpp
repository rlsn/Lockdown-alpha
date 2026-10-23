#include "Tests/LDEconomyUIProbe.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Economy/LDEconomyDirector.h"
#include "Economy/LDCommerceDirector.h"
#include "Economy/LDProductionDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDTradeDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Region/LDRegionDefinition.h"
#include "UI/LDInventoryPanel.h"
#include "UI/LDControlSettings.h"
#include "UI/LDHUD.h"
#include "UI/LDHUDModel.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Border.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputKeyEventArgs.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
namespace
{
    void SlateKey(FKey Key,bool Shift=false)
    {FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key,FModifierKeysState(Shift,false,false,false,false,false,false,false,false),0,false,0,0));FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key,FModifierKeysState(),0,false,0,0));}
    FString TextOf(UUserWidget* UI)
    {FString Text;TArray<UWidget*> Widgets;UI->WidgetTree->GetAllWidgets(Widgets);for(auto* W:Widgets){if(auto* T=Cast<UTextBlock>(W)){Text+=T->GetText().ToString()+TEXT("\n");}}return Text;}
}
ALDEconomyUIProbe::ALDEconomyUIProbe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDEconomyUIProbe::Check(bool Good,const FString& Text){Failures+=!Good;Checks.Add((Good?TEXT("PASS "):TEXT("FAIL "))+Text);UE_LOG(LogTemp,Display,TEXT("LD_ECONOMY_UI %s"),*Checks.Last());}
void ALDEconomyUIProbe::Enter(int32 Next){Phase=Next;At=FPlatformTime::Seconds();}
void ALDEconomyUIProbe::Input(FKey Key,bool Down){A->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),Key,Down?IE_Pressed:IE_Released,Down?1.f:0.f,false,FPlatformTime::Cycles64()));}
void ALDEconomyUIProbe::Capture(const FString& Name){const FString Path=Folder/(Name+TEXT(".png"));Images.Add(Path);PendingImage=Path;ShotAt=FPlatformTime::Seconds();}
bool ALDEconomyUIProbe::SnapshotReady(const FString& Name){if(ShotPhase!=Phase){ShotPhase=Phase;Capture(Name);return false;}return true;}
void ALDEconomyUIProbe::AuditPanel()
{
    auto* UI=A->InventoryUI.Get();auto Targets=UI->PanelFocusTargets();Check(Targets.Num()>10,TEXT("Visible service and owned bag expose one bounded keyboard focus scope"));if(Targets.IsEmpty()){return;}
    bool Sized=true;for(auto* W:Targets){auto Size=W->GetCachedGeometry().GetLocalSize();if(Size.IsNearlyZero()){Size=W->GetDesiredSize();}Sized&=Size.X>=39.5&&Size.Y>=39.5;}Check(Sized,TEXT("Visible or scrolled service controls request at least40x40 logical targets"));
    Targets[0]->SetKeyboardFocus();bool Ordered=true;for(int32 N=1;N<=Targets.Num();++N){SlateKey(EKeys::Tab);Ordered&=Targets[N%Targets.Num()]->HasKeyboardFocus()||Targets[N%Targets.Num()]->HasFocusedDescendants();}Check(Ordered,TEXT("Native Slate Tab visits every enabled control and wraps within visible service"));SlateKey(EKeys::Tab,true);Check(Targets.Last()->HasKeyboardFocus()||Targets.Last()->HasFocusedDescendants(),TEXT("Native Shift-Tab reverses without entering hidden service tabs"));
    const FVector2D Body=UI->Grid->GetCachedGeometry().GetLocalSize();Check(Body.X>250&&UI->ServiceWidth->GetCachedGeometry().GetLocalSize().X>=349,TEXT("Responsive owner bag and service column both retain usable width"));
    Check(!TextOf(UI).Contains(TEXT("Wallet 357"))&&!TextOf(UI).Contains(TEXT("Gold 357")),TEXT("A's service read model contains no rival wallet or private progress"));
    Targets[0]->SetKeyboardFocus();UI->ServiceScroll->ScrollWidgetIntoView(nullptr,false);UI->ServiceScroll->ScrollToStart();
}
void ALDEconomyUIProbe::Finish()
{
    for(const auto& Path:Images){Check(FPaths::FileExists(Path),TEXT("Native offscreen capture written: ")+FPaths::GetCleanFilename(Path));}
    auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("failures"),Failures);TArray<TSharedPtr<FJsonValue>> Rows;for(const auto& Text:Checks){Rows.Add(MakeShared<FJsonValueString>(Text));}R->SetArrayField(TEXT("checks"),Rows);R->SetStringField(TEXT("scope"),TEXT("Rendered UMG/Slate and actual Enhanced Input, two local owners, labelled diagnostic stock/gold/progression and public-window fixtures; no human, OS-scale or remote-client acceptance"));FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(Folder/TEXT("UI.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
void ALDEconomyUIProbe::Tick(float Delta)
{
#if !UE_BUILD_SHIPPING
    auto* W=GetWorld();auto* M=W->GetAuthGameMode<ALDGameMode>();if(!Started){Started=FPlatformTime::Seconds();FParse::Value(FCommandLine::Get(),TEXT("LDProbeReportDir="),Folder);}
    if(FPlatformTime::Seconds()-Started>120){Check(false,TEXT("Bounded native UI probe timed out"));Finish();return;}
    if(B.IsValid()&&B->InventoryUI){B->InventoryUI->SetVisibility(ESlateVisibility::Collapsed);}
    if(!PendingImage.IsEmpty()){if(FPlatformTime::Seconds()-ShotAt>=.15){if(Phase==6){Check(A->InventoryUI->IsOpen()&&A->InventoryUI->IsVisible(),FString::Printf(TEXT("Workshop preview remains visible during native capture: open%d visible%d armed%d suppressed%d"),A->InventoryUI->IsOpen(),A->InventoryUI->IsVisible(),A->bProductionArmed,A->bControlsSuppressed));}FScreenshotRequest::RequestScreenshot(PendingImage,true,false);PendingImage.Reset();CaptureUntil=FPlatformTime::Seconds()+1.2;}return;}if(FPlatformTime::Seconds()<CaptureUntil){return;}
    if(!Phase)
    {
        if(W->GetTimeSeconds()<2){return;}A=Cast<ALDPlayerController>(W->GetFirstPlayerController());if(!A.IsValid()||!A->GetPawn()){return;}B=Cast<ALDPlayerController>(UGameplayStatics::CreatePlayer(this,1,true));if(!B.IsValid()||!B->GetPawn()){Check(false,TEXT("Two local owner fixtures admitted"));Finish();return;}
        W->GetGameViewport()->SetForceDisableSplitscreen(true);FString Error;M->SetLobbyChoice(A.Get(),0,true,Error);M->SetLobbyChoice(B.Get(),1,true,Error);Check(M->StartDevelopmentMatch(A.Get(),Error),TEXT("Normal locked economy profile started"));M->Combat->SetActorTickEnabled(false);
        auto* OA=M->StateFor(A.Get());auto* OB=M->StateFor(B.Get());OA->Gold=600;OB->Gold=357;M->Economy->GoldChanged(TEXT("DiagnosticReward"),957);
        for(auto P:{A,B}){auto* O=M->StateFor(P.Get());O->ProtectedUntil=0;auto* Body=CastChecked<ALDCharacter>(P->GetPawn());Body->GetCharacterMovement()->StopMovementImmediately();Body->SetActorLocation(LDRegion::Site(W,1)+FVector(P==A?-50:50,30,92));auto* S=M->Combat->StateFor(Body);S->ProtectionUntil=0;S->LastDamage=S->LastDealt=-1000;M->SyncOwner(P.Get());}
        for(auto Kind:{ELDItemKind::Fiber,ELDItemKind::Fiber,ELDItemKind::Ore,ELDItemKind::Crystal,ELDItemKind::Toolkit,ELDItemKind::Toolkit,ELDItemKind::Core}){auto* Item=W->SpawnActor<ALDItem>();Item->Kind=Kind;Check(M->Inventory->Adopt(A.Get(),Item,TEXT("DiagnosticItemCreation")),TEXT("Labelled real physical cargo fixture adopted"));}
        A->Controls=NewObject<ULDControlSettings>(A.Get());A->Controls->ResetBindings();A->Controls->TextScale=FParse::Param(FCommandLine::Get(),TEXT("LDHUDLargeText"))?1.25f:1;Check(A->Controls->Rebind(TEXT("Context"),EKeys::K,Error),TEXT("Session-only context remapping accepted"));A->RebuildV05Mappings();A->OpenCommerce(1);if(B->HUDUI){B->HUDUI->SetVisibility(ESlateVisibility::Collapsed);}Enter(1);return;
    }
    if(B->HUDUI){B->HUDUI->SetVisibility(ESlateVisibility::Collapsed);}const double Elapsed=FPlatformTime::Seconds()-At;auto* UI=A->InventoryUI.Get();auto* OA=M->StateFor(A.Get());auto* G=W->GetGameState<ALDGameState>();FString Error;
    auto Place=[&](uint8 Site){A->CloseCommerce();auto* Body=CastChecked<ALDCharacter>(A->GetPawn());Body->GetCharacterMovement()->StopMovementImmediately();Body->SetActorLocation(LDRegion::Site(W,Site)+FVector(-50,30,92));};
    auto SelectInputs=[&](){UI->ProductionInputs.Reset();int32 Fiber=0,Ore=0;for(const auto& I:A->OwnerState.Inventory.Items){if((I.Kind==ELDItemKind::Fiber&&Fiber++<2)||(I.Kind==ELDItemKind::Ore&&Ore++<1)){UI->Select(I.Id);UI->ToggleProductionInput();}}UI->PreviewProduction(1);};
    if(Phase==1&&Elapsed>.6)
    {Check(UI->ServiceTab==2&&A->CommerceView.Session.IsValid()&&A->CommerceView.Offers.Num()==4,TEXT("Market opens beside own bag with live stock/cash/ask/bid"));AuditPanel();Capture(TEXT("Market"));Enter(2);}
    else if(Phase==2&&Elapsed>.3)
    {Gold=OA->Gold;UI->CommerceAction(1,ELDItemKind::Fiber);const int32 Pending=M->Commerce->Requests.Num();UI->CommerceAction(1,ELDItemKind::Fiber);Check(A->CommercePending&&Pending==1&&M->Commerce->Requests.Num()==1&&OA->Gold==Gold,TEXT("One explicit unit is pending until acknowledgment; double-click cannot duplicate it"));Capture(TEXT("Pending"));Enter(3);}
    else if(Phase==3&&Elapsed>.3)
    {M->Combat->Sample(W->GetTimeSeconds());Check(OA->Gold==Gold-6&&!A->CommercePending,TEXT("Acknowledged native Buy receives one actual Fiber for quoted6gold"));auto Q=M->Economy->CommerceQuote(1);FGuid Received;const auto* F=Q.Offers.FindByPredicate([](const auto& Row){return Row.Kind==ELDItemKind::Fiber;});Check(M->Economy->CommitCommerce(B.Get(),1,Q.Revision,1,ELDItemKind::Fiber,FGuid(),0,M->StateFor(B.Get())->Inventory.Revision,F->Ask,Received,Error),TEXT("Rival stock mutation is a real conserved paid purchase"));M->Commerce->Sample(W->GetTimeSeconds());Check(A->CommerceNeedsReview,TEXT("Changed quote requires a separate local review"));UI->Refresh();Capture(TEXT("QuoteChanged"));Enter(4);}
    else if(Phase==4&&Elapsed>.3)
    {auto ValidView=A->CommerceView;auto Invalid=ValidView;++Invalid.Match;Invalid.QuotedAt+=1000;A->ClientCommerceView_Implementation(A->OwnerState,Invalid,0,TEXT("Diagnostic foreign-match quote"));Check(A->CommerceView.Match==ValidView.Match&&A->LastCommerceQuotes.FindChecked(1).QuotedAt==ValidView.QuotedAt,TEXT("Foreign-match callback cannot replace a valid local quote or populate cached prices"));Invalid=ValidView;++Invalid.Life;A->ClientCommerceView_Implementation(A->OwnerState,Invalid,0,TEXT("Diagnostic stale-life quote"));Check(A->CommerceView.Life==ValidView.Life,TEXT("Wrong-life callback cannot replace current service discovery"));
        const auto Count=OA->Inventory.Items.Num();UI->CommerceAction(1,ELDItemKind::Fiber);Check(!A->CommerceNeedsReview&&!A->CommercePending&&OA->Inventory.Items.Num()==Count,TEXT("Review updated terms alone buys nothing"));const auto Cached=A->LastCommerceQuotes.FindChecked(1);Place(6);UI->SelectedMarket=1;auto Q=M->Economy->CommerceQuote(1);FGuid Received;const auto* F=Q.Offers.FindByPredicate([](const auto& Row){return Row.Kind==ELDItemKind::Fiber;});M->Economy->CommitCommerce(B.Get(),1,Q.Revision,1,ELDItemKind::Fiber,FGuid(),0,M->StateFor(B.Get())->Inventory.Revision,F->Ask,Received,Error);M->Commerce->Sample(W->GetTimeSeconds());UI->Refresh();Check(A->LastCommerceQuotes.FindChecked(1).Revision==Cached.Revision&&A->LastCommerceQuotes.FindChecked(1).QuotedAt==Cached.QuotedAt&&UI->CommerceStatus->GetText().ToString().Contains(TEXT("READ ONLY"))&&!UI->CommerceBuyButtons[0]->GetIsEnabled()&&!UI->CommerceSellButtons[0]->GetIsEnabled(),TEXT("Distant visited quote freezes timestamp/prices and disables exchange after new stock changes"));Capture(TEXT("QuoteHistory"));Enter(5);}
    else if(Phase==5&&Elapsed>.3)
    {Place(1);A->OpenCommerce(0);UI->ServiceScroll->ScrollWidgetIntoView(UI->CommerceBuyButtons[1],false);Enter(53);}
    else if(Phase==53&&Elapsed>.4)
    {if(!SnapshotReady(TEXT("OfficialSupply"))){return;}Check(A->CommerceView.Service==0&&A->CommerceView.Offers.Num()==2&&A->CommerceView.Offers[0].Stock==-1&&!UI->CommerceSellButtons[0]->IsVisible(),TEXT("Native official supply shows paid unlimited materials without fake Sell or reserve controls"));A->CloseCommerce();UI->ShowService(3);SelectInputs();Check(UI->DisplayedProduction.Inputs.Num()==3&&UI->DisplayedProduction.Fee==4&&UI->ProductionStatus->GetText().ToString().Contains(TEXT("Final bag"))&&UI->ProductionConfirm->GetIsEnabled(),TEXT("Workshop previews exact inputs, fee and final slots/weight"));UI->Search->SetKeyboardFocus();Enter(51);}
    else if(Phase==51&&Elapsed>.25)
    {Check(UI->Search->HasKeyboardFocus()||UI->Search->HasFocusedDescendants(),TEXT("Editable search receives native focus before typing shortcuts"));SlateKey(A->BindingKey(TEXT("Inventory")));FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(TCHAR('i'),FModifierKeysState(),0,false));SlateKey(EKeys::K);FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(TCHAR('k'),FModifierKeysState(),0,false));Enter(52);}
    else if(Phase==52&&Elapsed>.25)
    {Check(UI->IsOpen()&&!OA->DevelopmentKind&&!A->ProductionHoldToken&&!A->bProductionArmed,TEXT("Inventory/context keys in a focused search field cannot close panel or start world work after input dispatch"));Check(UI->Search->GetText().ToString()==TEXT("ik"),TEXT("Native character events still type letters mapped to inventory/context shortcuts"));for(int32 N=0;N<2;++N){SlateKey(EKeys::BackSpace);FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(TCHAR(8),FModifierKeysState(),0,false));}Enter(54);}
    else if(Phase==54&&Elapsed>.25)
    {Check(UI->Search->GetText().IsEmpty()&&UI->Cells[0]->GetIsEnabled(),TEXT("Native Backspace clearing restores the unfiltered owned bag"));UI->ProductionConfirm->SetKeyboardFocus();UI->ServiceScroll->ScrollWidgetIntoView(UI->ProductionConfirm,false);Enter(6);}
    else if(Phase==6&&Elapsed>.4)
    {if(!SnapshotReady(TEXT("Workshop"))){return;}Check(UI->ProductionConfirm->HasKeyboardFocus(),TEXT("Preview confirmation is reachable through native keyboard focus"));Gold=OA->Gold;SlateKey(EKeys::Enter);Check(!UI->IsOpen()&&A->bProductionArmed&&A->bFreshInputRequired&&!OA->DevelopmentKind&&OA->Gold==Gold,TEXT("Native Enter closes and arms without starting or spending"));Input(EKeys::K,true);Enter(7);}
    else if(Phase==7&&Elapsed>.2)
    {M->Combat->Sample(W->GetTimeSeconds());Check(!OA->DevelopmentKind&&A->bProductionArmed,TEXT("Key already held across panel close cannot bypass fresh-input gate"));Input(EKeys::K,false);Enter(8);}
    else if(Phase==8&&Elapsed>.25){Input(EKeys::K,true);M->Combat->SetActorTickEnabled(true);Enter(9);}
    else if(Phase==9&&Elapsed>.5)
    {if(!SnapshotReady(TEXT("HeldCraft"))){return;}Check(OA->DevelopmentKind==5&&A->ProductionHoldToken&&!A->bProductionArmed&&A->HeldInputLabel()==TEXT("K"),TEXT("Fresh remapped K passes actual Enhanced Input into held production and reserves inputs"));Input(EKeys::K,false);Enter(10);}
    else if(Phase==10&&Elapsed>.3)
    {Check(!OA->DevelopmentKind&&OA->Gold==Gold&&A->OwnerState.Inventory.Items.ContainsByPredicate([](const auto& I){return I.Kind==ELDItemKind::Fiber&&!I.bReserved;}),TEXT("Native mapped key release cancels without fee or losing reserved cargo"));A->ToggleInventoryPanel();UI->ShowService(3);SelectInputs();UI->ProductionConfirm->SetKeyboardFocus();SlateKey(EKeys::Enter);Enter(11);}
    else if(Phase==11&&Elapsed>.25){Input(EKeys::K,true);Enter(12);}
    else if(Phase==12&&Elapsed>.4)
    {Check(OA->DevelopmentKind==5,TEXT("A new explicit preview and gesture starts a second hold"));A->ToggleInventoryPanel();Input(EKeys::K,false);Enter(13);}
    else if(Phase==13&&Elapsed>.3)
    {Check(UI->IsOpen()&&!OA->DevelopmentKind&&OA->Gold==Gold,TEXT("Reopening a major panel cancels the active production hold without spend"));UI->ShowService(0);for(const auto& Item:OA->Inventory.Items){if(Item.Kind==ELDItemKind::Core){UI->Select(Item.Id);break;}}UI->Refresh();Check(!UI->UseButton->GetIsEnabled()&&!UI->Assignments[0]->GetIsEnabled()&&UI->DetailText().Contains(TEXT("Cannot be quick-used")),TEXT("Cargo detail and controls clearly reject quick use or shortcut assignment"));UI->ShowService(4);UI->ProductionInputs.Reset();for(const auto& I:OA->Inventory.Items){if(I.Kind==ELDItemKind::Core){UI->Select(I.Id);UI->ToggleProductionInput();break;}}UI->PreviewProduction(6);Check(UI->ProductionStatus->GetText().ToString().Contains(TEXT("public quota"))&&UI->ProductionStatus->GetText().ToString().Contains(TEXT("Industrial")),TEXT("Procurement preview names payout, public quota and distinct own endorsement"));UI->ServiceScroll->ScrollWidgetIntoView(UI->ProductionConfirm,false);Enter(14);}
    else if(Phase==14&&Elapsed>.3)
    {if(!SnapshotReady(TEXT("Procurement"))){return;}OA->SupplyEndorsement=FGuid::NewGuid();OA->IndustrialEndorsement=FGuid::NewGuid();OA->Witness=FGuid::NewGuid();M->SyncOwner(A.Get());A->OpenBank();UI->BankAmount->SetText(FText::FromString(TEXT("100")));UI->ReviewBank(false,true);Check(UI->bBankReviewed&&UI->bCharterReview&&!OA->CharterPurchase.IsValid(),TEXT("Treasury review exposes explicit100/0 split without purchase"));Enter(15);}
    else if(Phase==15&&Elapsed>.3)
    {if(ShotPhase!=Phase){AuditPanel();UI->ServiceScroll->ScrollWidgetIntoView(UI->BankConfirm,false);}if(!SnapshotReady(TEXT("Treasury"))){return;}UI->BankAmount->SetText(FText::FromString(TEXT("99")));UI->Refresh();Check(!UI->bBankReviewed&&!UI->BankConfirm->GetIsEnabled(),TEXT("Editing split invalidates review instead of silently changing confirmation"));UI->BankAmount->SetKeyboardFocus();SlateKey(A->BindingKey(TEXT("Inventory")));Check(UI->IsOpen(),TEXT("Inventory binding in Treasury amount field does not close the panel"));SlateKey(EKeys::Escape);Check(!UI->IsOpen()&&!A->bProductionArmed,TEXT("Native Escape closes edited Treasury without click-through world action"));A->ToggleInventoryPanel();UI->ShowService(1);A->OpenVendor();Enter(16);}
    else if(Phase==16&&Elapsed>.4)
    {if(ShotPhase!=Phase){AuditPanel();}if(!SnapshotReady(TEXT("Books"))){return;}Check(UI->ServiceTab==1&&A->VendorIsOpen(),TEXT("Guild catalog uses the same own-bag shell"));UI->Close();G->CharterWindow=FGuid::NewGuid();G->CharterWarningAt=W->GetTimeSeconds();G->CharterOpenAt=W->GetTimeSeconds()+30;G->CharterCloseAt=W->GetTimeSeconds()+90;G->CharterPhase=1;Enter(17);}
    else if(Phase==17&&Elapsed>.4)
    {if(!SnapshotReady(TEXT("Warning"))){return;}Check(LDHUD::PublicOpening(*G,W->GetTimeSeconds()).Contains(TEXT("WARNING")),TEXT("Public warning copy exposes endpoints without activator identity"));G->CharterOpenAt=W->GetTimeSeconds()-1;G->CharterCloseAt=W->GetTimeSeconds()+59;G->CharterPhase=2;Enter(18);}
    else if(Phase==18&&Elapsed>.4)
    {Check(LDHUD::EconomyStep(A->OwnerState,*G,W->GetTimeSeconds()).Contains(TEXT("no Charter required")),TEXT("Open-window objective clearly allows every eligible contender"));Capture(TEXT("Opening"));Enter(19);}
    else if(Phase==19&&Elapsed>.4)
    {
        FGuid Target;M->Vision->Tick(0);if(const auto* View=M->Vision->AuthorityViewFor(A.Get())){for(const auto& Seen:View->Entities){if(M->Vision->ResolveObserved(A.Get(),Seen.Id)==B->GetPawn()){Target=Seen.Id;break;}}}
        Check(Target.IsValid(),TEXT("Peer invitation uses an actually observed contender handle"));A->Inspect(Target);A->OfferTrade();Enter(20);
    }
    else if(Phase==20&&Elapsed>.5)
    {
        Check(A->TradeView.Active()&&B->TradeView.Active(),TEXT("Real peer invitation opens the shared service shell for both owners"));B->TradeCommand(LDTrade::Accept);M->Trade->Sample(W->GetTimeSeconds());
        for(const auto& Item:OA->Inventory.Items){if(Item.Kind==ELDItemKind::Fiber){UI->Select(Item.Id);break;}}UI->TradeSelected();M->Trade->Sample(W->GetTimeSeconds());
        const auto* Other=M->StateFor(B.Get());if(!Other->Inventory.Items.IsEmpty()){B->TradeCommand(LDTrade::Add,Other->Inventory.Items[0].Id);M->Trade->Sample(W->GetTimeSeconds());}Enter(21);
    }
    else if(Phase==21&&Elapsed>.4)
    {
        if(ShotPhase!=Phase){AuditPanel();}if(!SnapshotReady(TEXT("Trade"))){return;}
        Check(UI->ServiceTab==6&&A->TradeView.OwnItems.Num()==1&&A->TradeView.OtherItems.Num()==1&&!UI->TradeText().Contains(TEXT("357")),TEXT("Peer pane exposes only the two explicitly offered items, not the rival's wallet/bag"));A->CancelTrade();M->Trade->Sample(W->GetTimeSeconds());Enter(22);
    }
    else if(Phase==22&&Elapsed>.3)
    {Check(!M->Trade->HasSession(A.Get())&&!OA->Inventory.Items.ContainsByPredicate([](const auto& Item){return Item.bReserved;}),TEXT("Native peer-pane cancellation releases the shared exact-item reservations"));Check(M->Economy->AuditFailures==0&&M->Economy->Audit(Error),TEXT("Native presentation/input checks leave physical accounting reconciled"));Check(M->Economy->Export(TEXT("B66Verified"),Error),TEXT("Native UI authority ledger exported"));Finish();}
#endif
}
