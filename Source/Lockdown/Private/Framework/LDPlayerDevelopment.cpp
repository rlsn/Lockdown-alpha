#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Development/LDDevelopmentDirector.h"
#include "UI/LDDevelopmentPanel.h"
#include "Engine/World.h"
#include "Navigation/LDNavigationComponent.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Objectives/LDObjectiveDirector.h"
#include "UI/LDInventoryPanel.h"

void ALDPlayerController::ToggleDevelopmentPanel()
{
    const auto* Public=GetWorld()->GetGameState<ALDGameState>();
    if(!OwnerState.bDevelopmentProfile||(!OwnerState.bActive&&!Navigation->Enabled())||!Public||!Public->bMatchStarted||Public->EndReason){return;}
    if(bControlsSuppressed&&(!DevelopmentUI||!DevelopmentUI->IsOpen())){return;}
    if(InventoryUI&&InventoryUI->IsOpen()){InventoryUI->Close();}
    if(!DevelopmentUI){DevelopmentUI=CreateWidget<ULDDevelopmentPanel>(this);DevelopmentUI->AddToViewport(20);}
    if(DevelopmentUI->IsOpen()){DevelopmentUI->Close();}else{DevelopmentUI->Open();}
}
void ALDPlayerController::BuyBook(ELDSkill Skill)
{StopDevelopmentHold();if(Navigation->Enabled()){OpenVendor();return;}ServerDevelopmentRequest(OwnerState.Match,OwnerState.Life,++NextRequest,LDDevelopmentTransaction::Buy,Skill,FGuid(),FLDBuildPlan());}
void ALDPlayerController::SetInteractionHeld(bool Held){if(Navigation->Enabled()){if(Held&&CanWorldCommand()){ContextV05();}else if(!Held){V05Released(TEXT("Context"));}return;}if(Held){InteractStarted();}else{InteractStopped();}}
void ALDPlayerController::SetSkillHeld(uint8 Slot,bool Held){if(Navigation->Enabled()){if(Held){ArmSkill(Slot,2);}else{EndSkillGesture(Slot,2);}return;}if(Held){SkillStarted(Slot);}else{SkillStopped(Slot);}}
void ALDPlayerController::StartDevelopmentHold(uint8 Kind,FGuid Item,const FLDBuildPlan& Plan)
{
    ClearControls();DevelopmentHoldToken=++NextRequest;DevelopmentHoldSequence=0;LastDevelopmentHoldSent=-1000;
    if(Navigation->Enabled()&&Kind==LDDevelopmentTransaction::Read)
    {const auto* Entry=OwnerState.Inventory.Find(Item);InventoryPendingRequest=DevelopmentHoldToken;ServerInventoryRequest(OwnerState.Match,OwnerState.Life,DevelopmentHoldToken,OwnerState.Inventory.Revision,LDInventory::Read,Item,Entry?Entry->Revision:0,0,FVector::ZeroVector);return;}
    ServerDevelopmentRequest(OwnerState.Match,OwnerState.Life,DevelopmentHoldToken,Kind,ELDSkill::None,Item,Plan);
}
void ALDPlayerController::StartLoadoutHold(uint8 Command,FGuid Item,uint8 Slot)
{
    if(OwnerState.Build.RulesVersion<7||InventoryPending()){return;}const auto* Entry=OwnerState.Inventory.Find(Item);const auto* C=ULDEquipmentCatalog::Load(OwnerState.Build.RulesVersion);if(!Entry||!C){return;}const uint32 ItemRevision=Entry->Revision;
    ClearControls();DevelopmentHoldToken=++NextRequest;DevelopmentHoldSequence=0;LastDevelopmentHoldSent=-1000;bLoadoutMouseHeld=true;InventoryPendingRequest=DevelopmentHoldToken;
    FLDLoadoutRequest R;R.Version=OwnerState.Build.RulesVersion==8?2:1;R.Match=OwnerState.Match;R.Life=OwnerState.Life;R.Token=DevelopmentHoldToken;R.InventoryRevision=OwnerState.Inventory.Revision;R.BuildRevision=OwnerState.Build.Revision;R.ItemRevision=ItemRevision;R.Item=Item;R.Command=Command;R.Binding=Slot;R.EquipmentDigest=C->Checksum;ServerLoadoutRequest(R);
}
void ALDPlayerController::ServerLoadoutRequest_Implementation(FLDLoadoutRequest R)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M?M->StateFor(this):nullptr;const auto* C=ULDEquipmentCatalog::Load(O?O->Build.RulesVersion:7);
    if(O&&C&&O->Build.RulesVersion==8&&R.Version==2&&R.Match==O->Match&&R.Life==O->Life&&R.EquipmentDigest==C->Checksum&&M->Development->Replay(this,R.Token)){return;}
    if(!O||!C||!M->IsTacticalProfile()||!M->IsPlaying()||R.Version!=(O->Build.RulesVersion==8?2:1)||R.Match!=O->Match||R.Life!=O->Life||R.BuildRevision!=O->Build.Revision||R.EquipmentDigest!=C->Checksum||R.Binding>=(O->Build.RulesVersion==8?11:6)||(R.Command!=LDInventory::Bind&&R.Command!=LDInventory::Unbind&&R.Command!=LDInventory::Equip&&R.Command!=LDInventory::Unequip)){ClientIntentResult(R.Token,TEXT("Invalid loadout schema, profile, revision or digest"));return;}
    M->Objectives->CancelClaimForAction(this,GetWorld()->GetTimeSeconds());M->Development->QueueLoadout(this,R.Match,R.Life,R.Token,R.InventoryRevision,R.ItemRevision,R.Command,R.Item,R.Binding);
}
void ALDPlayerController::StopDevelopmentHold()
{
    bLoadoutMouseHeld=false;const uint32 Token=FMath::Max(DevelopmentHoldToken,OwnerState.DevelopmentToken);
    if(Token){ServerDevelopmentRelease(OwnerState.Match,OwnerState.Life,Token);}DevelopmentHoldToken=0;HeldQuickSlot=-1;
}
void ALDPlayerController::ServerDevelopmentRequest_Implementation(uint32 Match,uint32 Life,uint32 Request,uint8 Kind,ELDSkill Skill,FGuid Item,const FLDBuildPlan& Plan)
{
    if(auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M&&M->Inventory&&Kind!=LDDevelopmentTransaction::Prepare){ClientIntentResult(Request,TEXT("Use the inventory item action"));return;}
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();if(Mode&&Mode->Development){Mode->Development->Queue(this,Match,Life,Request,Kind,Skill,Item,Plan);}
    else{ClientIntentResult(Request,TEXT("Development transactions unavailable in this profile."));}
}
void ALDPlayerController::ServerDevelopmentHold_Implementation(uint32 Match,uint32 Life,uint32 Token,uint32 Sequence,bool Moving)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=Mode?Mode->StateFor(this):nullptr;
    if(O&&O->Match==Match&&O->Life==Life&&Mode->Development){Mode->Development->RefreshHold(this,Life,Token,Sequence,Moving);}
}
void ALDPlayerController::ServerDevelopmentRelease_Implementation(uint32 Match,uint32 Life,uint32 Token)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=Mode?Mode->StateFor(this):nullptr;
    if(O&&O->Match==Match&&O->Life==Life&&Mode->Development){Mode->Development->Release(this,Life,Token);}
}
void ALDPlayerController::ClientDevelopmentResult_Implementation(const FLDOwnerState& State,uint32 Request,const FString& Result)
{ClientLifeReceipt_Implementation(State);ClientIntentResult_Implementation(Request,Result);}
