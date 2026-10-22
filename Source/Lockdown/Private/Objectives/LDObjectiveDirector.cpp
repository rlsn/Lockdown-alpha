#include "Objectives/LDObjectiveDirector.h"
#include "Inventory/LDTradeDirector.h"
#include "Objectives/LDObjectiveRules.h"
#include "Core/LDGroundQueries.h"
#include "Core/LDRules.h"
#include "Region/LDRegionDefinition.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Vision/LDVisionDirector.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavigationSystem.h"
#include "AIController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Inventory/LDInventoryDirector.h"

ALDSeal::ALDSeal(){Kind=ELDItemKind::Seal;}
ALDObjectiveSite::ALDObjectiveSite()
{
    bReplicates=false;RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Marker=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PublicSite"));Marker->SetupAttachment(RootComponent);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder"));
    Marker->SetStaticMesh(Cylinder.Object);Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);Marker->SetCanEverAffectNavigation(false);
    Label=CreateDefaultSubobject<UTextRenderComponent>(TEXT("PublicLabel"));Label->SetupAttachment(RootComponent);
    Label->SetHorizontalAlignment(EHTA_Center);Label->SetVerticalAlignment(EVRTA_TextCenter);
    Label->SetWorldSize(26);Label->SetTextRenderColor(FColor(235,240,216));Label->SetRelativeLocation(FVector(0,0,100));
    Label->SetRelativeRotation(FRotator(60,135,0));
}
void ALDObjectiveSite::Configure(uint8 Site)
{
    SetActorLocation(LDRegion::Site(GetWorld(),Site));
    Marker->SetRelativeLocation(FVector(0,0,8));Marker->SetRelativeScale3D(Site==LDObjective::Heart?FVector(1.5,1.5,.16):FVector(.9,.9,.16));
    Label->SetText(FText::FromString(Site==LDObjective::Guild?TEXT("GUILD"):Site==LDObjective::Heart?TEXT("HEART"):*LDRegion::SiteName(GetWorld(),Site)));
    auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Lockdown/Generated/Materials/M_Character.M_Character"));
    if(Base){auto* Material=UMaterialInstanceDynamic::Create(Base,this);Material->SetVectorParameterValue(TEXT("Tint"),Site==LDObjective::Heart?FLinearColor(.65f,.14f,.12f):FLinearColor(.18f,.36f,.5f));Marker->SetMaterial(0,Material);}
}
ALDObjectiveDirector::ALDObjectiveDirector(){bReplicates=false;Transactions.Initialize(int32(FPlatformTime::Cycles()));}
void ALDObjectiveDirector::BeginPlay()
{
    Super::BeginPlay();check(HasAuthority());
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    auto* GS=GetWorld()->GetGameState<ALDGameState>();GS->MatchDeadline=Mode->IsDevelopmentProfile()?0:float(Mode->StartedAt()+LDRules::MatchDeadlineSeconds);GS->ForceNetUpdate();
}
double ALDObjectiveDirector::Deadline() const{return GetWorld()->GetGameState<ALDGameState>()->MatchDeadline;}
bool ALDObjectiveDirector::Eligible(ALDPlayerController* PC,double Now) const
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    const auto* O=Mode->StateFor(PC);const auto* Body=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;
    const auto* Combat=Body?Mode->Combat->StateFor(Body):nullptr;
    return Mode->IsPlaying()&&O&&O->bActive&&Body&&Combat&&Combat->Hp>0&&Now>=Combat->ProtectionUntil&&Now>=O->ProtectedUntil;
}
bool ALDObjectiveDirector::Near(ALDPlayerController* PC,uint8 Site,double Range) const
{
    if(Site==LDObjective::Fabricator){Site=LDObjective::Guild;}
    const APawn* Body=PC?PC->GetPawn():nullptr;const FVector P=LDRegion::Site(GetWorld(),Site);
    return Body&&FVector::DistSquared2D(Body->GetActorLocation(),P)<=Range*Range&&LDGround::ClearSegment(GetWorld(),Body->GetActorLocation(),P);
}
bool ALDObjectiveDirector::CanFabricate(ALDPlayerController* PC) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M->StateFor(PC);
    return M->Region&&O&&(!M->Inventory||M->Inventory->HasSpace(PC,ELDItemKind::Seal))&&LDObjective::CanFabricate(O->Witness.IsValid(),O->Commissions,O->Gold,O->FabricationReceipt.IsValid(),O->SealId.IsValid()||CarriedBy(PC));
}
void ALDObjectiveDirector::FailNextFabricationForProbe()
{
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("LDSealProbe"))){bFailFabrication=true;}
#endif
}
bool ALDObjectiveDirector::Stationary(ALDPlayerController* PC,const FLDInteractionState& S) const
{
    const auto* Body=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;
    return Body&&!S.bMoving&&Body->GetVelocity().SizeSquared2D()<9&&Body->GetCharacterMovement()->GetCurrentAcceleration().SizeSquared2D()<1;
}
void ALDObjectiveDirector::Queue(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 RequestId,uint8 Target,FGuid Observed)
{
    if(!GetWorld()->GetAuthGameMode<ALDGameMode>()->IsPlaying()){PC->CompleteContextRequest(RequestId,TEXT("Interaction unavailable in lobby or after results."));return;}
    int32 Count=0;for(const auto& R:Requests){if(R.PC==PC){++Count;}}
    if(Count>=2){PC->CompleteContextRequest(RequestId,TEXT("Too many interaction requests."));return;}
    auto& S=Interactions.FindOrAdd(PC);
    if(RequestId>S.Token){S.Token=RequestId;S.Life=Life;S.HoldSequence=0;S.bHeld=true;S.bMoving=false;S.LastHold=GetWorld()->GetTimeSeconds();}
    Requests.Add({PC,Match,Life,RequestId,Target,Observed});
}
void ALDObjectiveDirector::RefreshHold(ALDPlayerController* PC,uint32 Life,uint32 Token,uint32 Sequence,bool Moving,double Now)
{
    auto* S=Interactions.Find(PC);
    if(!S||!S->bHeld||S->Life!=Life||S->Token!=Token||Sequence<=S->HoldSequence||Token<=S->CancelledThrough){return;}
    S->HoldSequence=Sequence;S->LastHold=Now;S->bMoving=Moving;
}
void ALDObjectiveDirector::Release(ALDPlayerController* PC,uint32 Life,uint32 Token)
{
    auto& S=Interactions.FindOrAdd(PC);
    if(S.Life!=Life||Token<S.Token){return;}
    S.CancelledThrough=FMath::Max(S.CancelledThrough,Token);S.bHeld=false;ClearChannel(PC,S);
}
bool ALDObjectiveDirector::HasChannel(ALDPlayerController* PC) const
{const auto* S=Interactions.Find(PC);return S&&S->Channel!=LDObjective::NoChannel;}
void ALDObjectiveDirector::ClearChannel(ALDPlayerController* PC,FLDInteractionState& S)
{
    if(S.Channel==LDObjective::Fabricate){S.bHeld=false;}
    const bool Had=S.Channel!=0;S.Channel=0;S.Route=0;S.Started=0;S.bContested=false;
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();if(auto* O=Mode->StateFor(PC))
    {if(O->ChannelKind||O->ChannelProgress){O->ChannelKind=0;O->ChannelRoute=0;O->ChannelProgress=0;O->ChannelStartedAt=0;Mode->SyncOwner(PC);}}
    if(Had){PC->ClientIntentResult(S.Token,TEXT("Interaction interrupted; progress reset."));}
}
void ALDObjectiveDirector::Interrupt(ALDPlayerController* PC,double Now,bool bEndHold)
{
    auto* S=Interactions.Find(PC);if(!S){return;}S->LastInterrupt=Now;if(bEndHold){S->bHeld=false;}ClearChannel(PC,*S);
}
ALDSeal* ALDObjectiveDirector::CarriedBy(ALDPlayerController* PC) const
{for(TActorIterator<ALDSeal> It(GetWorld());It;++It){if(It->State==1&&It->Carrier==PC){return *It;}}return nullptr;}
void ALDObjectiveDirector::Announce(ALDSeal* Item,uint8 Kind)
{
    auto* GS=GetWorld()->GetGameState<ALDGameState>();++GS->SealNoticeSerial;GS->SealNoticeKind=Kind;
    GS->SealNoticeDistrict=Item->District;GS->ForceNetUpdate();
}
void ALDObjectiveDirector::Drop(ALDPlayerController* PC,const FVector& Point)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=Mode->StateFor(PC);
    if(ALDSeal* Item=CarriedBy(PC))
    {
        FNavLocation NavPoint;auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
        FVector DropPoint=LDRegion::Site(GetWorld(),LDObjective::Guild)+FVector(80,0,16);bool Placed=false;
        if(Nav&&Nav->ProjectPointToNavigation(Point,NavPoint,FVector(240,240,400)))
        {
            FHitResult Ground;
            if(GetWorld()->LineTraceSingleByChannel(Ground,NavPoint.Location+FVector(0,0,400),NavPoint.Location-FVector(0,0,400),ECC_GameTraceChannel1)
                &&Ground.ImpactNormal.Z>=FMath::Cos(FMath::DegreesToRadians(40.))
                &&!GetWorld()->OverlapAnyTestByObjectType(Ground.ImpactPoint+FVector(0,0,16),FQuat::Identity,FCollisionObjectQueryParams(ECC_WorldStatic),FCollisionShape::MakeSphere(12)))
            {DropPoint=Ground.ImpactPoint+FVector(0,0,16);Placed=true;}
        }
        if(!Placed){UE_LOG(LogTemp,Warning,TEXT("LD Seal drop used Guild workbench fallback"));}
        if(Mode->Inventory){Mode->Inventory->Remove(PC,Item,false);}Item->Carrier.Reset();Item->State=0;Item->SetActorLocation(DropPoint);Item->District=LDRegion::District(GetWorld(),DropPoint);Announce(Item,2);
    }
    if(O){O->SealId.Invalidate();}
}
void ALDObjectiveDirector::AnnounceVoluntaryDrop(ALDSeal* Item,const FVector& Point)
{Item->Carrier.Reset();Item->SetActorLocation(Point);Item->District=LDRegion::District(GetWorld(),Point);Announce(Item,2);}
void ALDObjectiveDirector::EndLife(ALDPlayerController* PC,const FVector& Point,double Now)
{
    Interrupt(PC,Now,true);Drop(PC,Point);
    if(auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC)){O->ContractStage=0;O->ContractId.Invalidate();O->ContractDestination=0;}
}
void ALDObjectiveDirector::Withdraw(ALDPlayerController* PC,const FVector& Point,double Now)
{if(!GetWorld()->GetAuthGameMode<ALDGameMode>()->IsFinished()){EndLife(PC,Point,Now);}Interactions.Remove(PC);Requests.RemoveAll([&](const FRequest& R){return R.PC==PC;});}
ALDSeal* ALDObjectiveDirector::SeedFixtureSeal()
{
#if !UE_BUILD_SHIPPING
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if(Mode->bScaffoldProfile&&!Mode->IsFinished()&&!bSeeded)
    {
        auto* Item=GetWorld()->SpawnActor<ALDSeal>(LDObjective::SeedPosition(),FRotator::ZeroRotator);
        if(Item){bSeeded=true;Item->Identity=FGuid::NewGuid();Item->District=LDRegion::District(GetWorld(),Item->GetActorLocation());Announce(Item,1);UE_LOG(LogTemp,Display,TEXT("LD_FIXTURE one Seal seeded at Guild (acquisition only; ownership transactions are real)"));}
        return Item;
    }
#endif
    return nullptr;
}
bool ALDObjectiveDirector::SetDefencesFixture(uint8 Flags)
{
#if !UE_BUILD_SHIPPING
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if(Mode->bScaffoldProfile&&!Mode->IsFinished()&&Flags<=15)
    {auto* GS=GetWorld()->GetGameState<ALDGameState>();GS->DefensiveFlags=Flags;GS->ForceNetUpdate();UE_LOG(LogTemp,Display,TEXT("LD_FIXTURE defensive flags=%u (access setup, not tower combat)"),Flags);return true;}
#endif
    return false;
}
void ALDObjectiveDirector::Request(const FRequest& R,double Now)
{
    auto* PC=R.PC.Get();if(!PC){return;}auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=Mode->StateFor(PC);
    auto& S=Interactions.FindOrAdd(PC);FString Reason;
    if(!O||O->Match!=R.Match||O->Life!=R.Life||R.Id!=S.Token||R.Id<=S.CancelledThrough){Reason=TEXT("Stale interaction or released hold.");}
    else if(Now>Deadline()){Reason=TEXT("Match deadline passed.");}
    else if(!Eligible(PC,Now)){Reason=TEXT("Life unavailable, protected or demo finished.");}
    else if(S.Channel){Reason=TEXT("Interaction already occupied.");}
    else if(Mode->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()))->Action){Reason=TEXT("Combat action occupied.");}
    else if(S.LastInterrupt>=Now-1.e-6){Reason=TEXT("Interrupted on this authority sample.");}
    else if(R.Target==LDObjective::Seal)
    {
        if(Mode->Inventory){PC->CompleteContextRequest(R.Id,TEXT("Use the current item pickup action"));S.bHeld=false;return;}
        auto* Item=Cast<ALDSeal>(Mode->Vision->ResolveObserved(PC,R.Observed));
        if(O->SealId.IsValid()){Reason=TEXT("Seal carry slot full.");}
        else if(!Item||Item->State!=0){Reason=TEXT("Seal already taken or no longer visible.");}
        else if(FVector::DistSquared2D(PC->GetPawn()->GetActorLocation(),Item->GetActorLocation())>80*80||!LDGround::ClearSegment(GetWorld(),PC->GetPawn()->GetActorLocation(),Item->GetActorLocation())){Reason=TEXT("Seal needs visible clear ground within 80 cm.");}
        else{Item->State=1;Item->Carrier=PC;Item->District=LDRegion::District(GetWorld(),PC->GetPawn()->GetActorLocation());O->SealId=Item->Identity;Reason=TEXT("Seal pickup confirmed.");Mode->SyncOwner(PC);}
        S.bHeld=false;
    }
    else if(R.Target<LDObjective::Guild||(R.Target>LDObjective::Heart&&!((R.Target==6||R.Target==LDObjective::Fabricator)&&LDRegion::Definition(GetWorld())))||!Near(PC,R.Target)){Reason=TEXT("Approach the public site within 120 cm on clear ground.");}
    else if(R.Target==LDObjective::Fabricator&&!CanFabricate(PC)){S.bHeld=false;Reason=TEXT("Fabrication needs Witness, 4 unused credits, 100 gold, an empty Seal slot and no prior craft this match.");}
    else
    {
        S.Target=R.Target;
        if(R.Target==LDObjective::Guild&&O->ContractStage==0)
        {
            if(LDRegion::Definition(GetWorld())&&O->PaidJobs>=8){Reason=TEXT("All eight paid courier contracts completed for this match.");}
            else if(Now<O->WorkReadyAt){Reason=TEXT("Courier acceptance cooling down.");}
            else{if(Mode->Trade){Mode->Trade->CancelFor(PC);}O->ContractId=FGuid::NewGuid();O->ContractStage=1;
                if(const auto* D=LDRegion::Definition(GetWorld())){const auto Towns=D->Towns();if(!O->FirstCourierTown){O->FirstCourierTown=Towns[Transactions.RandRange(0,1)];}O->ContractDestination=Towns[(Towns.IndexOfByKey(O->FirstCourierTown)+O->PaidJobs)%Towns.Num()];}
                Mode->SyncOwner(PC);Reason=TEXT("Courier accepted: hold E at your private assigned town to collect.");}
        }
        else
        {
            BeginChannel(PC,S,Now);
            Reason=S.Channel?TEXT("Hold accepted; server progress started."):(R.Target==LDObjective::Heart?TEXT("Heart locked: carry a Seal or clear all four defences."):TEXT("Follow your private courier stage."));
        }
    }
    PC->CompleteContextRequest(R.Id,Reason);
}
void ALDObjectiveDirector::BeginChannel(ALDPlayerController* PC,FLDInteractionState& S,double Now)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=Mode->StateFor(PC);
    if(!S.bHeld||!LDObjective::FreshHold(Now,S.LastHold)||S.LastInterrupt>=Now-1.e-6||!Stationary(PC,S)||!Near(PC,S.Target)||!Eligible(PC,Now)){return;}
    const auto* Combat=Mode->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()));if(Combat->Action||Combat->LastDamage>=Now-1.e-6){return;}
    uint8 Kind=0,Route=0;
    if(S.Target==LDObjective::Fabricator&&CanFabricate(PC)){Kind=LDObjective::Fabricate;}
    if(O->ContractStage==1){if(const auto* D=LDRegion::Definition(GetWorld())){const auto* N=D->Node(O->ContractDestination);if(N&&N->Site==S.Target){Kind=LDObjective::Collect;}}else if(S.Target==LDObjective::Station){Kind=LDObjective::Collect;}}
    if(S.Target==LDObjective::Guild&&O->ContractStage==2){Kind=LDObjective::Deliver;}
    if(S.Target==LDObjective::Heart)
    {const auto* GS=GetWorld()->GetGameState<ALDGameState>();Route=GS->DefensiveFlags==0?LDObjective::Assault:(CarriedBy(PC)?LDObjective::SealRoute:0);if(Route){Kind=LDObjective::Claim;}}
    if(!Kind){return;}if(Mode->Trade){Mode->Trade->CancelFor(PC);}S.Channel=Kind;S.Route=Route;S.Started=Now;S.Anchor=PC->GetPawn()->GetActorLocation();S.Contract=O->ContractId;S.bContested=false;
    UpdatePrivate(PC,S,Now);
}
void ALDObjectiveDirector::UpdatePrivate(ALDPlayerController* PC,FLDInteractionState& S,double Now)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=Mode->StateFor(PC);if(!O){return;}
    const float Progress=S.Channel?float(FMath::Clamp(Now-S.Started,0.,LDObjective::Duration(S.Channel))):0;
    if(O->ChannelKind!=S.Channel||O->ChannelRoute!=S.Route||O->ChannelStartedAt!=float(S.Started)||O->ChannelProgress!=Progress)
    {O->ChannelKind=S.Channel;O->ChannelRoute=S.Route;O->ChannelStartedAt=float(S.Started);O->ChannelProgress=Progress;Mode->SyncOwner(PC);}
}
void ALDObjectiveDirector::Sample(double Now)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* GS=GetWorld()->GetGameState<ALDGameState>();if(!Mode->IsPlaying()){return;}
    // Independent private RNG, so simultaneous recovery has no transport-order or role preference.
    for(int32 I=Requests.Num()-1;I>0;--I){Requests.Swap(I,Transactions.RandRange(0,I));}
    for(const auto& R:Requests){Request(R,Now);}Requests.Reset();
    bool Active=false,Contested=false;uint8 Routes=0;double PublicProgress=0;
    for(auto It=Interactions.CreateIterator();It;++It)
    {
        auto* PC=It.Key().Get();if(!PC){It.RemoveCurrent();continue;}auto& S=It.Value();auto* O=Mode->StateFor(PC);if(!O){continue;}
        const auto* Body=Cast<ALDCharacter>(PC->GetPawn());const auto* Combat=Body?Mode->Combat->StateFor(Body):nullptr;
        const bool Valid=Eligible(PC,Now)&&S.Life==O->Life&&S.bHeld&&LDObjective::FreshHold(Now,S.LastHold)
            &&Stationary(PC,S)&&Near(PC,S.Target)&&Combat&&!Combat->Action&&Combat->LastDamage<Now-1.e-6&&S.LastInterrupt<Now-1.e-6;
        if(S.Channel&&(!Valid||FVector::DistSquared2D(Body->GetActorLocation(),S.Anchor)>4)){ClearChannel(PC,S);}
        if(!LDObjective::FreshHold(Now,S.LastHold)){S.bHeld=false;}
        if(!S.Channel&&Valid){BeginChannel(PC,S,Now);}
        if(!S.Channel){continue;}
        if(S.Channel==LDObjective::Fabricate)
        {
            if(!CanFabricate(PC)){ClearChannel(PC,S);S.bHeld=false;continue;}
            if(LDObjective::DueBeforeDeadline(Now,S.Started,LDObjective::FabricateSeconds,Deadline()))
            {
                // Materialize first. No resource or receipt is changed if creation fails.
                ALDSeal* Item=nullptr;
                if(!bFailFabrication){Item=GetWorld()->SpawnActor<ALDSeal>(Body->GetActorLocation(),FRotator::ZeroRotator);}
                bFailFabrication=false;
                if(Item)
                {
                    Item->Identity=FGuid::NewGuid();
                    if(Mode->Inventory&&!Mode->Inventory->Adopt(PC,Item)){Item->Destroy();Item=nullptr;}
                }
                if(Item)
                {
                    Item->State=1;Item->Carrier=PC;Item->District=LDRegion::District(GetWorld(),Body->GetActorLocation());
                    O->Gold-=100;O->Commissions-=4;O->SealId=Item->Identity;O->FabricationReceipt=FGuid::NewGuid();Announce(Item,1);
                    UE_LOG(LogTemp,Display,TEXT("LD_FABRICATION_RECEIPT gold=%lld credits=%d paid=%u"),O->Gold,O->Commissions,O->PaidJobs);
                }
                S.Channel=0;S.Route=0;S.Started=0;S.bHeld=false;UpdatePrivate(PC,S,Now);Mode->SyncOwner(PC);
                PC->CompleteContextRequest(S.Token,Item?TEXT("Concord Seal fabricated: 4 credits and 100 gold spent. Witness retained."):TEXT("Seal creation failed. No resources spent; retry fabrication."));
            }
            else{UpdatePrivate(PC,S,Now);}
            continue;
        }
        if(S.Channel==LDObjective::Collect||S.Channel==LDObjective::Deliver)
        {
            const bool Correct=S.Contract.IsValid()&&O->ContractId==S.Contract&&O->ContractStage==(S.Channel==LDObjective::Collect?1:2);
            if(!Correct||(LDRegion::Definition(GetWorld())&&O->PaidJobs>=8)){ClearChannel(PC,S);continue;}
            if(LDObjective::DueBeforeDeadline(Now,S.Started,3,Deadline()))
            {
                const bool Delivered=S.Channel==LDObjective::Deliver;
                if(Delivered){O->Gold+=25;Mode->AwardExperience(PC,Mode->IsDevelopmentProfile()?45:15);++O->Commissions;++O->DeliverySerial;if(LDRegion::Definition(GetWorld())){++O->PaidJobs;}O->WorkReadyAt=float(Now+20);O->ContractStage=0;O->ContractId.Invalidate();O->ContractDestination=0;}
                else{O->ContractStage=2;}
                S.Channel=0;S.Route=0;S.Started=0;S.bHeld=false;UpdatePrivate(PC,S,Now);Mode->SyncOwner(PC);
                PC->CompleteContextRequest(S.Token,Delivered?(Mode->IsDevelopmentProfile()?TEXT("Delivery confirmed: +25 gold, +45 XP."):TEXT("Delivery confirmed: +25 gold, +15 XP.")):TEXT("Package collected: return to Guild and hold E to deliver."));
                UE_LOG(LogTemp,Display,TEXT("LD_COURIER_RECEIPT delivered=%d serial=%u gold=%lld xp=%lld"),Delivered,O->DeliverySerial,O->Gold,O->Xp);
            }
            else{UpdatePrivate(PC,S,Now);}
            continue;
        }
        if((S.Route==LDObjective::SealRoute&&!CarriedBy(PC))||(S.Route==LDObjective::Assault&&GS->DefensiveFlags)){ClearChannel(PC,S);continue;}
        bool IsContested=false;
        for(FConstPlayerControllerIterator Other=GetWorld()->GetPlayerControllerIterator();Other;++Other)
        {auto* Opponent=Cast<ALDPlayerController>(Other->Get());if(Opponent!=PC&&Eligible(Opponent,Now)&&FVector::DistSquared2D(Opponent->GetPawn()->GetActorLocation(),LDRegion::Site(GetWorld(),LDObjective::Heart))<=240*240){IsContested=true;break;}}
        if(IsContested||S.bContested){S.Started=Now;S.bContested=IsContested;}
        Active=true;Contested|=IsContested;Routes|=S.Route;HeartSensorUntil=Now+2;
        PublicProgress=FMath::Max(PublicProgress,Now-S.Started);UpdatePrivate(PC,S,Now);
        if(!IsContested&&LDObjective::DueBeforeDeadline(Now,S.Started,10,Deadline())){Finish(PC,S.Route,Now);return;}
    }
    GS->bClaimActive=Active;GS->bContested=Contested;GS->ClaimRoute=Routes;GS->ClaimProgress=Contested?0:float(FMath::Clamp(PublicProgress,0.,10.));GS->ForceNetUpdate();
    for(TActorIterator<ALDSeal> It(GetWorld());It;++It)
    {
        if(It->State!=1||!It->Carrier.IsValid()||!It->Carrier->GetPawn()){continue;}
        const uint8 District=LDRegion::District(GetWorld(),It->Carrier->GetPawn()->GetActorLocation());if(District!=It->District){It->District=District;Announce(*It,3);}
    }
    // Completion is evaluated before the deadline, but only scheduled completions <= deadline may win.
    if(Now>=Deadline()){Finish(nullptr,0,Now);}
}
void ALDObjectiveDirector::Finish(ALDPlayerController* Winner,uint8 Route,double Now)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* GS=GetWorld()->GetGameState<ALDGameState>();if(Mode->IsFinished()){return;}
    if(Winner&&Route==LDObjective::SealRoute)
    {if(auto* Item=CarriedBy(Winner)){if(Mode->Inventory){Mode->Inventory->Remove(Winner,Item,true);}else{Item->State=2;Item->Carrier.Reset();}}Mode->StateFor(Winner)->SealId.Invalidate();}
    GS->ResultReceipt=FGuid::NewGuid();++GS->ResultSerial;GS->WinningRoute=Route;GS->MatchEndedAt=float(Now);GS->EndReason=Winner?1:2;
    GS->ClaimProgress=Winner?10:0;GS->bClaimActive=false;GS->bContested=false;GS->ForceNetUpdate();
    for(auto& Pair:Interactions)
    {if(auto* PC=Pair.Key.Get()){Pair.Value.bHeld=false;Pair.Value.Channel=0;auto* O=Mode->StateFor(PC);if(O){O->bWinner=PC==Winner;O->ChannelKind=0;O->ChannelProgress=0;Mode->SyncOwner(PC);PC->ClientLifeReceipt(*O);}}}
    for(TActorIterator<ALDCharacter> It(GetWorld());It;++It)
    {It->GetCharacterMovement()->DisableMovement();if(auto* AI=Cast<AAIController>(It->GetController())){AI->StopMovement();}}
    UE_LOG(LogTemp,Display,TEXT("LD_OBJECTIVE_RESULT reason=%u route=%u serial=%u"),GS->EndReason,Route,GS->ResultSerial);
}
