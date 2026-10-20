#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Development/LDDevelopmentDirector.h"
#include "UI/LDDevelopmentPanel.h"
#include "Engine/World.h"

void ALDPlayerController::ToggleDevelopmentPanel()
{
    const auto* Public=GetWorld()->GetGameState<ALDGameState>();
    if(!OwnerState.bDevelopmentProfile||!OwnerState.bActive||!Public||!Public->bMatchStarted||Public->EndReason){return;}
    if(bControlsSuppressed&&(!DevelopmentUI||!DevelopmentUI->IsOpen())){return;}
    if(!DevelopmentUI){DevelopmentUI=CreateWidget<ULDDevelopmentPanel>(this);DevelopmentUI->AddToViewport(20);}
    if(DevelopmentUI->IsOpen()){DevelopmentUI->Close();}else{DevelopmentUI->Open();}
}
void ALDPlayerController::BuyBook(ELDSkill Skill)
{StopDevelopmentHold();ServerDevelopmentRequest(OwnerState.Match,OwnerState.Life,++NextRequest,LDDevelopmentTransaction::Buy,Skill,FGuid(),FLDBuildPlan());}
void ALDPlayerController::SetInteractionHeld(bool Held){if(Held){InteractStarted();}else{InteractStopped();}}
void ALDPlayerController::StartDevelopmentHold(uint8 Kind,FGuid Item,const FLDBuildPlan& Plan)
{
    ClearControls();DevelopmentHoldToken=++NextRequest;DevelopmentHoldSequence=0;LastDevelopmentHoldSent=-1000;
    ServerDevelopmentRequest(OwnerState.Match,OwnerState.Life,DevelopmentHoldToken,Kind,ELDSkill::None,Item,Plan);
}
void ALDPlayerController::StopDevelopmentHold()
{
    const uint32 Token=FMath::Max(DevelopmentHoldToken,OwnerState.DevelopmentToken);
    if(Token){ServerDevelopmentRelease(OwnerState.Match,OwnerState.Life,Token);}DevelopmentHoldToken=0;
}
void ALDPlayerController::ServerDevelopmentRequest_Implementation(uint32 Match,uint32 Life,uint32 Request,uint8 Kind,ELDSkill Skill,FGuid Item,const FLDBuildPlan& Plan)
{
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
