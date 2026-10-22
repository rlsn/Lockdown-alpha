#include "Tests/LDInputProbe.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Navigation/LDNavigationComponent.h"
#include "UI/LDHUD.h"
#include "UI/LDMinimap.h"
#include "UI/LDHUDModel.h"
#include "UI/LDControlsPanel.h"
#include "UI/LDDevelopmentPanel.h"
#include "Vision/LDWorldViewRelay.h"
#include "Vision/LDVisionDirector.h"
#include "Region/LDRegionDefinition.h"
#include "Core/LDGroundQueries.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

void ALDInputProbe::HUDServerTick()
{
    const double E=FPlatformTime::Seconds()-PhaseAt;auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* G=GetWorld()->GetGameState<ALDGameState>();
    if(Phase==18){if(E>4){Finish();}return;}
    if(!A.IsValid()||!B.IsValid()){Check(false,TEXT("HUD client disconnected before final phase"));Finish();return;}
    if(Phase==10)
    {
        if(!Step){for(auto PC:{A,B}){HUDAnchors.Add(PC->GetPawn()->GetActorLocation());FString Reason;Check(M->Vision->PlaceWard(PC.Get(),PC->GetPawn()->GetActorLocation()+FVector(-20,-20,0),Reason),TEXT("HUD source case consumes the one real finite starting ward"));}Step=1;}
        if(E>6){int32 I=0;for(auto PC:{A,B}){Check(!PC->Navigation->IsAuthorityActive()&&FVector::Dist2D(PC->GetPawn()->GetActorLocation(),HUDAnchors[I++])>140,TEXT("Minimap ground order moves own native body without a target or cast"));}Enter(11);}
    }
    else if(Phase==11&&E>3)
    {for(auto PC:{A,B}){Check(!PC->Navigation->IsAuthorityActive(),TEXT("Rejected minimap cell and major-panel gestures issue no route"));}int32 I=0;for(auto T:Far){T->SetActorLocation(M->Vision->PublicLocation()+FVector(I++*50,0,92));}Enter(12);}
    else if(Phase==12&&E>3)
    {int32 I=0;for(auto PC:{A,B}){Check(!PC->Navigation->IsAuthorityActive()&&M->StateFor(PC.Get())->Build.Cooldowns[3]<GetWorld()->GetTimeSeconds()&&FMath::IsNearlyEqual(M->Combat->StateFor(Far[I].Get())->Hp,FarHp[I]-LDDevelopment::SkillValue(M->StateFor(PC.Get())->Build,ELDSkill::Spark),.001),TEXT("Public-source inspection cannot cast remotely or auto-approach"));++I;}M->Vision->bPublicationEnabled=false;Enter(13);}
    else if(Phase==13)
    {if(E>1.3){M->Vision->bPublicationEnabled=true;}if(E>2.5){auto* O=M->StateFor(A.Get());O->Gold=100;M->SyncOwner(A.Get());M->EndContenderLife(A.Get(),GetWorld()->GetTimeSeconds());Check(!M->StateFor(A.Get())->bActive&&M->StateFor(B.Get())->bActive&&!G->EndReason,TEXT("One living contender does not become winner when another dies"));Enter(14);}}
    else if(Phase==14&&E>3){Enter(15);}
    else if(Phase==15&&M->StateFor(A.Get())->bActive&&M->StateFor(A.Get())->Life==2)
    {Check(M->StateFor(A.Get())->WardCharges==0&&M->StateFor(A.Get())->LastLostGold==LDDevelopment::DeathLoss(100),TEXT("Real return preserves death receipt and never grants fresh ward supply"));Enter(16);}
    else if(Phase==16&&E>2)
    {G->MatchDeadline=float(GetWorld()->GetTimeSeconds()+1);Enter(17);}
    else if(Phase==17&&E>3)
    {Check(G->EndReason==2&&G->ResultReceipt.IsValid()&&!M->StateFor(A.Get())->bWinner&&!M->StateFor(B.Get())->bWinner,TEXT("Real objective deadline creates one No acquisition receipt without a winner"));Enter(18);}
}
void ALDInputProbe::HUDClientTick()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwner());auto* HUD=PC->HUDUI.Get();const double E=FPlatformTime::Seconds()-PhaseAt;
    if(!HUD||!HUD->Minimap){Check(false,TEXT("Native HUD and minimap exist"));Finish();return;}
    auto* Map=HUD->Minimap.Get();auto* Body=Cast<ALDCharacter>(PC->GetPawn());const auto* R=PC->LocalView();const auto* D=LDRegion::Definition(GetWorld());const FLDVisionGrid Grid=D?D->Grid:FLDVisionGrid();
    if(Phase==10&&Body)
    {
        if(!Step&&E>.5)
        {
            BodyAnchor=Body->GetActorLocation();CameraMask=R->LastView().Current;PC->ArmSkill(0);Map->Gesture(LDHUD::MapPoint(FVector(3500,2000,0),Grid),false);Step=1;
        }
        if(Step==1&&E>1)
        {
            Check(PC->IsTargeting()&&FVector::Dist2D(BodyAnchor,Body->GetActorLocation())<1&&R->LastView().Current==CameraMask,TEXT("Minimap left-pan does not cast, move the body or expand sight"));
            Check(R->LastView().Entities.ContainsByPredicate([](const auto& V){return V.Kind==ELDObservedKind::Ward&&V.Presentation==1;})&&R->LastView().Entities.ContainsByPredicate([](const auto& V){return V.Kind==ELDObservedKind::Ward&&V.Presentation==0;}),TEXT("Owner-filtered ward source distinguishes own from observed foreign ward in existing mask"));
            const auto Before=PC->Navigation->LocalIdentity();Map->Gesture(LDHUD::MapPoint(BodyAnchor+FVector(200,0,0),Grid),true);Check(!PC->IsTargeting()&&PC->Navigation->LocalIdentity()==Before,TEXT("First minimap right-click consumes armed cancellation without movement"));Step=2;
        }
        if(Step==2&&E>1.4){Map->Gesture(LDHUD::MapPoint(BodyAnchor+FVector(200,0,0),Grid),true);Step=3;}
        if(Step==3&&E>4.5){Check(FVector::Dist2D(BodyAnchor,Body->GetActorLocation())>140&&!PC->Inspected.IsValid(),TEXT("Minimap marker gesture remains a ground order with no selection"));Step=4;}
    }
    if(Phase==11&&!Step&&E>.5)
    {
        FVector Surface;Check(LDGround::MapSurface(GetWorld(),FVector(1400,-2100,0),Surface)&&FMath::Abs(Surface.Z-200)<2,TEXT("Map resolves authored 200cm plateau rather than a different floor"));
        const auto Before=PC->Navigation->LocalIdentity();Map->Gesture(LDHUD::MapPoint(FVector(-2100,-1530,0),Grid),true);Check(PC->Navigation->LocalIdentity()==Before&&PC->IntentFeedback==TEXT("No walkable route"),TEXT("Map wall cell rejects without exposing a hidden dynamic target"));
        PC->ToggleControlsPanel();const auto Blocked=PC->Navigation->LocalIdentity();Check(!Map->Gesture(FVector2D(.2,.2),true)&&PC->Navigation->LocalIdentity()==Blocked,TEXT("Major panel blocks minimap commands even when called outside its frame"));PC->ControlsUI->Close();Step=1;
        PC->ClientIntentResult_Implementation(9000001,TEXT("HUD receipt fixture"));const auto Serial=PC->FeedbackSerial();PC->IntentFeedback=TEXT("Later local feedback");PC->ClientIntentResult_Implementation(9000001,TEXT("HUD receipt fixture"));
        Check(PC->FeedbackSerial()==Serial&&PC->IntentFeedback==TEXT("Later local feedback"),TEXT("Duplicate receipt cannot replay a toast or overwrite newer local feedback"));
        PC->ClientIntentResult_Implementation(9000002,TEXT("HUD receipt fixture"));Check(PC->FeedbackSerial()==Serial+1,TEXT("Distinct receipts with the same text still produce distinct outcomes"));
    }
    if(Phase==12&&Body)
    {
        if(!Step&&E>.6)
        {const FVector Expected=FVector(900+(Role==TEXT("A")?0:50),900,92);for(const auto& V:R->LastView().Entities){if(V.Kind==ELDObservedKind::Body&&FVector::Dist2D(V.Position,Expected)<15){Target=V.Id;break;}}PC->Inspect(Target);Step=1;}
        if(Step==1&&E>1)
        {Check(Target.IsValid()&&HUD->InspectionText().Contains(TEXT("HP / level / class: Unknown"))&&HUD->InspectionText().Contains(TEXT("Out of reach"))&&HUD->CommandText().Contains(TEXT("YOURS")),TEXT("Remote-source inspector masks private fields while own commands remain anchored"));
        int32 Bodies=0,Controllers=0;bool Own=true;for(TActorIterator<ALDCharacter> It(GetWorld());It;++It){++Bodies;Own&=*It==Body;}for(TActorIterator<ALDPlayerController> It(GetWorld());It;++It){++Controllers;Own&=*It==PC;}
        Check(Bodies==1&&Controllers==1&&Own,TEXT("Restricted HUD client has only its own canonical body/controller; others remain eight-field observations"));PC->ArmSkill(1);PC->ConfirmArmed(FVector(900,900,0),Target);Step=2;}
        if(Step==2&&E>2){Check(PC->IntentFeedback.Contains(TEXT("No visible living Spark target"))&&!PC->Navigation->IsFollowing(),TEXT("Observed distant body rejects skill reach without automatic travel"));Step=3;}
    }
    if(Phase==13)
    {
        if(!Step&&E>.8){Check(R->IsStale()&&!PC->Inspected.IsValid()&&HUD->InspectionText().Contains(TEXT("YOUR CHARACTER"))&&R->LastView().Entities.IsEmpty(),TEXT("Stale view removes dynamic markers, outline, target and other inspector"));Step=1;}
        if(Step==1&&E>2){Check(!PC->Inspected.IsValid()&&!PC->IsTargeting(),TEXT("Reappearance never silently reacquires the old selection"));Step=2;}
    }
    if(Phase==14&&!Step&&E>.7)
    {
        if(Role==TEXT("A")){PC->ToggleDevelopmentPanel();const auto Before=PC->DevelopmentUI->Preview().Attributes;PC->DevelopmentUI->Choose(1,0);Check(!PC->OwnerState.bActive&&!PC->CanWorldCommand()&&PC->DevelopmentUI->IsOpen()&&PC->DevelopmentUI->Preview().Attributes==Before,TEXT("Dead own build remains read-only without enabling world commands"));PC->DevelopmentUI->Close();}
        else{Check(PC->OwnerState.bActive&&!GetWorld()->GetGameState<ALDGameState>()->EndReason,TEXT("Last living contender has no fabricated winner panel"));}Step=1;
    }
    if(Phase==16&&!Step&&E>.5)
    {Check(!PC->Inspected.IsValid()&&!PC->IsTargeting()&&(Role==TEXT("B")||(PC->OwnerState.Life==2&&PC->OwnerState.ProtectedUntil>GetWorld()->GetGameState<ALDGameState>()->GetServerWorldTimeSeconds()&&PC->Navigation->bFollowCamera)),TEXT("Return presents a new protected life with cleared targets and follow camera"));Step=1;}
    if(Phase==17&&!Step&&E>2)
    {Check(HUD->TerminalText().Contains(TEXT("NO ACQUISITION"))&&!PC->CanWorldCommand()&&!PC->Navigation->IsFollowing(),TEXT("Terminal UI follows real No acquisition receipt and blocks commands"));Step=1;}
    if(Phase==18&&E>1){Check(true,TEXT("All B53 HUD phases completed"));Finish();}
}
