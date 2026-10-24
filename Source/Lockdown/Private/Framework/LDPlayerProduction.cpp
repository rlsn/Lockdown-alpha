#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "UI/LDInventoryPanel.h"
#include "Inventory/LDAcquisitionDirector.h"
#include "Inventory/LDAcquisitionCatalog.h"
#include "Core/LDGroundQueries.h"
#include "Engine/World.h"
bool ALDPlayerController::NearProduction(uint8 Site) const
{return Site<9&&OwnerState.bActive&&GetPawn()&&FVector::Dist2D(GetPawn()->GetActorLocation(),ALDProductionDirector::Position(GetWorld(),Site))<=120&&LDGround::ClearSegment(GetWorld(),GetPawn()->GetActorLocation(),ALDProductionDirector::Position(GetWorld(),Site));}
void ALDPlayerController::ArmProduction(const FLDProductionPlan& Plan)
{
    const auto Copy=Plan;if(InventoryUI&&InventoryUI->IsOpen()){InventoryUI->Close();}CancelArmed();ProductionPlan=Copy;bProductionArmed=true;bFreshInputRequired=true;
    IntentFeedback=TEXT("Preview armed. Release all controls, then hold ")+BindingLabel(TEXT("Context"))+TEXT(" at ")+ALDProductionDirector::SiteName(Plan.Site)+TEXT(". Escape cancels.");
}
void ALDPlayerController::StartProductionHold()
{
    const auto Plan=ProductionPlan;bProductionArmed=false;ClearControls();const uint32 Token=++NextRequest;
    if(Plan.Recipe==3||Plan.Recipe==4){HeldSkillSlot=Plan.SkillSlot;HeldSkillGesture=3;SkillHoldToken=Token;SkillHoldSequence=0;LastSkillHoldSent=-1000;}
    else{ProductionHoldToken=Token;ProductionSequence=0;LastProductionSent=-1000;}
    ServerProductionRequest(OwnerState.Match,OwnerState.Life,Token,Plan);
}
void ALDPlayerController::StopProductionHold()
{if(ProductionHoldToken){ServerProductionRelease(OwnerState.Match,OwnerState.Life,ProductionHoldToken);}ProductionHoldToken=0;}
void ALDPlayerController::ServerProductionRequest_Implementation(uint32 Match,uint32 Life,uint32 Token,const FLDProductionPlan& Plan)
{if(auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M&&M->Production){M->Production->Queue(this,Match,Life,Token,Plan);}else{ClientIntentResult(Token,TEXT("Production unavailable"));}}
void ALDPlayerController::ServerProductionHold_Implementation(uint32 Match,uint32 Life,uint32 Token,uint32 Sequence,bool Moving)
{auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M?M->StateFor(this):nullptr;if(O&&O->Match==Match&&O->Life==Life&&M->Production){M->Production->RefreshHold(this,Life,Token,Sequence,Moving);}}
void ALDPlayerController::ServerProductionRelease_Implementation(uint32 Match,uint32 Life,uint32 Token)
{auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M?M->StateFor(this):nullptr;if(O&&O->Match==Match&&O->Life==Life&&M->Production){M->Production->Release(this,Life,Token);}}
void ALDPlayerController::ClientProductionResult_Implementation(const FLDOwnerState& State,uint32 Token,const FString& Reason)
{
    ClientLifeReceipt_Implementation(State);ClientIntentResult_Implementation(Token,Reason);
    if(ProductionHoldToken==Token&&State.DevelopmentToken!=Token){ProductionHoldToken=0;}
    if(SkillHoldToken==Token&&State.ActiveSkill==ELDSkill::None){HeldSkillSlot=255;SkillHoldToken=0;}
}

void ALDPlayerController::BuyAcquisition(uint8 Site,ELDItemKind Kind,ELDSkill Skill)
{const auto* C=ULDAcquisitionCatalog::Load();if(C){FLDSupplyRequest R;R.Match=OwnerState.Match;R.Life=OwnerState.Life;R.Token=++NextRequest;R.Site=Site;R.Kind=Kind;R.Skill=Skill;R.InventoryRevision=OwnerState.Inventory.Revision;R.Digest=C->Checksum;ServerAcquisitionPurchase(R);}}
void ALDPlayerController::ServerAcquisitionPurchase_Implementation(const FLDSupplyRequest& R)
{if(auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M&&M->Acquisition){M->Acquisition->Purchase(this,R.Match,R.Life,R.Token,R.Site,R.Kind,R.Skill,R.InventoryRevision,R.Digest);}}
void ALDPlayerController::ClientAcquisitionCache_Implementation(uint8 Site,const TArray<FLDInventoryItem>& Items)
{CacheSite=Site;CacheItems=Items;}
