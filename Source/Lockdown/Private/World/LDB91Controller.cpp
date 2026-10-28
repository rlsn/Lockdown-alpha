#include "World/LDEnvironmentDirector.h"
#include "World/LDPortableTools.h"
#include "Character/LDCharacter.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Navigation/LDNavigationComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "Components/PointLightComponent.h"
#include "UI/LDControlSettings.h"

ALDB91GameMode::ALDB91GameMode()
{PlayerControllerClass=ALDB91Controller::StaticClass();HUDClass=ALDB91HUD::StaticClass();}
void ALDB91Controller::BeginPlay()
{Super::BeginPlay();bShowMouseCursor=true;bEnableClickEvents=true;Controls=ULDControlSettings::Load(TEXT("LockdownLocalControlsV1"),8);}
void ALDB91Controller::SetupInputComponent()
{APlayerController::SetupInputComponent();}
void ALDB91Controller::ServerWorldInteraction_Implementation(FLDWorldRequest Request)
{if(auto* E=ALDEnvironmentDirector::Get(GetWorld())){E->Begin(this,Request);}}
void ALDB91Controller::ServerWorldHold_Implementation(uint32 Match,uint32 Life,uint32 Sequence,uint32 NonceValue,bool Release)
{if(auto* E=ALDEnvironmentDirector::Get(GetWorld())){E->Hold(this,Match,Life,Sequence,NonceValue,Release);}}
void ALDB91Controller::ServerWorldRelease_Implementation(uint32 Match,uint32 Life,uint32 Sequence)
{if(auto* E=ALDEnvironmentDirector::Get(GetWorld())){E->Hold(this,Match,Life,Sequence,0,true);}}
void ALDB91Controller::ClientWorldReceipt_Implementation(FLDWorldReceipt Receipt)
{++WorldReceiptSerial;if(Receipt.Sequence>=LastWorldReceipt.Sequence){LastWorldReceipt=Receipt;IntentFeedback=Receipt.Reason;}}
void ALDB91Controller::ClientEnvironmentView_Implementation(uint32 Match,uint32 Life,uint64 Revision,const TArray<FLDEnvironmentViewEntry>& Entries)
{
    if(Match!=OwnerState.Match||Life!=OwnerState.Life||Entries.Num()>128||(Match==LastViewMatch&&Life==LastViewLife&&Revision<=LastViewRevision)){return;}
    if(!IsLocalController()){return;}LastViewMatch=Match;LastViewLife=Life;LastViewRevision=Revision;LastViewAt=GetWorld()->GetTimeSeconds();View=Entries;
    auto* E=ALDEnvironmentDirector::Get(GetWorld());
    if(!E&&GetNetMode()==NM_Client){E=GetWorld()->SpawnActor<ALDEnvironmentDirector>();FString Why;if(!E->Initialize(Why)){View.Reset();IntentFeedback=Why;return;}}
    for(auto& P:BodyViews){if(P.Value.IsValid()){P.Value->SetVisibility(false,true);}}
    for(const auto& Entry:View)
    {
        if(!Entry.PublicDoor.IsNone())
        {if(GetNetMode()==NM_Client&&E){if(auto* D=E->Door(Entry.PublicDoor)){D->Apply(Entry.State,Entry.Revision);}}continue;}
        auto& Weak=BodyViews.FindOrAdd(Entry.Handle);auto* Visual=Weak.Get();
        if(!Visual)
        {
            auto* A=GetWorld()->SpawnActor<AActor>();A->SetActorEnableCollision(false);
            Visual=NewObject<ULDHumanoidVisualComponent>(A);A->SetRootComponent(Visual);A->AddInstanceComponent(Visual);Visual->RegisterComponent();Weak=Visual;
            auto* Glow=NewObject<UPointLightComponent>(A);A->AddInstanceComponent(Glow);Glow->SetupAttachment(Visual);Glow->SetMobility(EComponentMobility::Movable);Glow->SetCastShadows(false);Glow->SetIntensity(1500);Glow->SetLightColor(FLinearColor(1.f,.65f,.25f));Glow->RegisterComponent();Glow->SetVisibility(false);
        }
        Visual->SetWorldLocationAndRotation(Entry.Position,FRotator(0,Entry.Facing/65535.*360,0));
        Visual->ApplyPalette(Entry.Presentation%4);Visual->SetCue(Entry.State);Visual->SetVisibility(true,true);
        if(auto* Glow=Visual->GetOwner()->FindComponentByClass<UPointLightComponent>()){Glow->SetAttenuationRadius(Entry.Light==2?800:600);Glow->SetVisibility(Entry.Light==1||Entry.Light==2);}
    }
    for(auto I=BodyViews.CreateIterator();I;++I)
    {if(!View.ContainsByPredicate([&](const auto& Entry){return Entry.PublicDoor.IsNone()&&Entry.Handle==I.Key();}))
     {if(I.Value().IsValid()){I.Value()->GetOwner()->Destroy();}I.RemoveCurrent();}}
}
const FLDEnvironmentViewEntry* ALDB91Controller::NearestDoor() const
{
    const APawn* B=GetPawn();if(!B){return nullptr;}const FLDEnvironmentViewEntry* Best=nullptr;double Distance=120*120;
    for(const auto& E:View)
    {const double D=FVector::DistSquared2D(B->GetActorLocation(),E.Position);
     if(!E.PublicDoor.IsNone()&&D<=Distance&&FMath::Abs(B->GetActorLocation().Z-E.Position.Z)<100){Best=&E;Distance=D;}}
    return Best;
}
void ALDB91Controller::StartDoor(ELDWorldVerb Verb)
{
    const auto* D=NearestDoor();const auto* E=ALDEnvironmentDirector::Get(GetWorld());if(!D||!E||!CanWorldCommand()){return;}
    ReleaseDoor();HeldObject=false;
    Navigation->Stop();FLDWorldRequest R;R.Match=OwnerState.Match;R.Life=OwnerState.Life;R.Sequence=++WorldSequence;
    R.Observed=D->Handle;R.ObjectRevision=D->Revision;R.Verb=Verb;R.Contract=E->Contract;
    HeldSequence=R.Sequence;Nonce=0;SentAt=GetWorld()->GetTimeSeconds();ServerWorldInteraction(R);
}
void ALDB91Controller::ReleaseDoor()
{if(HeldSequence){if(HeldObject){ServerObjectRelease(OwnerState.Match,OwnerState.Life,HeldSequence);}else{ServerWorldRelease(OwnerState.Match,OwnerState.Life,HeldSequence);}HeldSequence=0;}}
void ALDB91Controller::PlayerTick(float Delta)
{
    Super::PlayerTick(Delta);if(!IsLocalController()){return;}
    if(bTraversalActive&&(!OwnerState.bActive||OwnerState.Match!=TraversalMatch||OwnerState.Life!=TraversalLife)){bTraversalActive=false;}
    if(auto* Body=Cast<ALDCharacter>(GetPawn())){Body->SetPortableLight(LDPortableTools::LightFor(this));}
    TickB91Probe();
    const double Now=GetWorld()->GetTimeSeconds();
    if(Now-SurfaceAt>.5||SurfaceMatch!=OwnerState.Match||SurfaceLife!=OwnerState.Life)
    {SurfaceView.Reset();for(auto P:SurfaceProxies){if(P.Value.IsValid()){P.Value->Destroy();}}SurfaceProxies.Reset();}
    if(GetNetMode()==NM_Client&&!ALDEnvironmentDirector::Get(GetWorld()))
    {auto* E=GetWorld()->SpawnActor<ALDEnvironmentDirector>();FString Why;if(!E->Initialize(Why)){E->Destroy();IntentFeedback=Why;}}
    if(Now-LastViewAt>.5||OwnerState.Match!=LastViewMatch||OwnerState.Life!=LastViewLife)
    {View.Reset();for(auto& P:BodyViews){if(P.Value.IsValid()){P.Value->GetOwner()->Destroy();}}BodyViews.Reset();}
    if(Now-ObjectViewAt>.5||OwnerState.Match!=ObjectViewMatch||OwnerState.Life!=ObjectViewLife)
    {ObjectView.Reset();ContainerView.Reset();ContainerHandle.Invalidate();for(auto& P:ObjectProxies){if(P.Value.IsValid()){P.Value->SetObserved(false);}}}
    TickSourceControls();
    if(auto* Body=Cast<ALDCharacter>(GetPawn()))
    {const float Zoom=(WasInputKeyJustPressed(EKeys::MouseScrollUp)?1.f:0.f)-(WasInputKeyJustPressed(EKeys::MouseScrollDown)?1.f:0.f);if(Zoom){Body->Zoom(Zoom);}}
}
