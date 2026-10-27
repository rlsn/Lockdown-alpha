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

ALDB91GameMode::ALDB91GameMode()
{PlayerControllerClass=ALDB91Controller::StaticClass();HUDClass=ALDB91HUD::StaticClass();}
void ALDB91Controller::BeginPlay()
{Super::BeginPlay();bShowMouseCursor=true;}
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
    if(GetNetMode()==NM_Client&&!ALDEnvironmentDirector::Get(GetWorld()))
    {auto* E=GetWorld()->SpawnActor<ALDEnvironmentDirector>();FString Why;if(!E->Initialize(Why)){E->Destroy();IntentFeedback=Why;}}
    if(Now-LastViewAt>.5||OwnerState.Match!=LastViewMatch||OwnerState.Life!=LastViewLife)
    {View.Reset();for(auto& P:BodyViews){if(P.Value.IsValid()){P.Value->GetOwner()->Destroy();}}BodyViews.Reset();}
    if(Now-ObjectViewAt>.5||OwnerState.Match!=ObjectViewMatch||OwnerState.Life!=ObjectViewLife)
    {ObjectView.Reset();ContainerView.Reset();ContainerHandle.Invalidate();for(auto& P:ObjectProxies){if(P.Value.IsValid()){P.Value->SetObserved(false);}}}
    if(WasInputKeyJustPressed(EKeys::RightMouseButton))
    {ReleaseDoor();FHitResult Hit;if(CanWorldCommand()&&GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_GameTraceChannel1),false,Hit)){Navigation->Navigate(Hit.ImpactPoint);}}
    if(WasInputKeyJustPressed(EKeys::S)||WasInputKeyJustPressed(EKeys::Escape)){ReleaseDoor();Navigation->Stop();}
    if(WasInputKeyJustPressed(EKeys::F))
    {HeldKey=EKeys::F;if(const auto* O=NearestObject()){if(O->Kind==ELDObjectKind::Anchor||O->Kind==ELDObjectKind::Ladder&&(O->State&16)){StartLink(ELDObjectVerb::Use);}else{StartObject(O->Kind==ELDObjectKind::Ladder?ELDObjectVerb::Carry:uint8(O->Kind)>=4?ELDObjectVerb::Pickup:ELDObjectVerb::Open);}}
     else if(const auto* D=NearestDoor()){StartDoor((D->State&1)?ELDWorldVerb::Close:ELDWorldVerb::Open);}}
    if(WasInputKeyJustPressed(EKeys::V)){HeldKey=EKeys::V;StartDoor(ELDWorldVerb::Lockpick);}
    if(WasInputKeyJustPressed(EKeys::B)){HeldKey=EKeys::B;StartDoor(ELDWorldVerb::Force);}
    if(WasInputKeyJustPressed(EKeys::Tab)){ContainerSelection=(ContainerSelection+1)%FMath::Max(1,ContainerView.Num());}
    if(WasInputKeyJustPressed(EKeys::I)){BagSelection=(BagSelection+1)%FMath::Max(1,OwnerState.Inventory.Items.Num());}
    for(int N=0;N<4;++N){const FKey Key=TArray<FKey>{EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four}[N];if(WasInputKeyJustPressed(Key)&&OwnerState.Inventory.Items.IsValidIndex(BagSelection))
    {ReleaseDoor();ToolBindingHeld=true;ToolBindingKey=Key;StartLoadoutHold(LDInventory::Equip,OwnerState.Inventory.Items[BagSelection].Id,7+N);}}
    if(WasInputKeyJustPressed(EKeys::U)&&OwnerState.Inventory.Items.IsValidIndex(BagSelection))
    {ReleaseDoor();ToolBindingHeld=true;ToolBindingKey=EKeys::U;StartLoadoutHold(LDInventory::Unequip,OwnerState.Inventory.Items[BagSelection].Id,7);}
    if(ToolBindingHeld&&!IsInputKeyDown(ToolBindingKey)){StopDevelopmentHold();ToolBindingHeld=false;}
    if(WasInputKeyJustPressed(EKeys::L)){ToggleSelectedTool();}
    for(const auto& P:TArray<TPair<FKey,ELDObjectVerb>>{{EKeys::H,ELDObjectVerb::Attach},{EKeys::O,ELDObjectVerb::Deploy},{EKeys::K,ELDObjectVerb::Retrieve}})
    {if(WasInputKeyJustPressed(P.Key)){HeldKey=P.Key;StartLink(P.Value);}}
    for(const auto& P:TArray<TPair<FKey,ELDObjectVerb>>{{EKeys::T,ELDObjectVerb::Take},{EKeys::Y,ELDObjectVerb::Store},{EKeys::C,ELDObjectVerb::Carry},{EKeys::P,ELDObjectVerb::Push},{EKeys::X,ELDObjectVerb::Break}})
    {if(WasInputKeyJustPressed(P.Key)){HeldKey=P.Key;StartObject(P.Value);}}
    for(const auto& P:TArray<TPair<FKey,ELDObjectVerb>>{{EKeys::G,ELDObjectVerb::Drop},{EKeys::J,ELDObjectVerb::Place}})
    {if(WasInputKeyJustPressed(P.Key)){FHitResult Hit;HeldKey=P.Key;if(GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_GameTraceChannel1),false,Hit)){StartObject(P.Value,Hit.ImpactPoint);}}}
    if(HeldSequence)
    {if(!IsInputKeyDown(HeldKey)||!CanWorldCommand()){ReleaseDoor();}
     else if(Now-SentAt>=.1){SentAt=Now;if(HeldObject){ServerObjectHold(OwnerState.Match,OwnerState.Life,HeldSequence,++Nonce);}else{ServerWorldHold(OwnerState.Match,OwnerState.Life,HeldSequence,++Nonce,false);}}}
    if(auto* Body=Cast<ALDCharacter>(GetPawn()))
    {const float Zoom=(WasInputKeyJustPressed(EKeys::MouseScrollUp)?1.f:0.f)-(WasInputKeyJustPressed(EKeys::MouseScrollDown)?1.f:0.f);if(Zoom){Body->Zoom(Zoom);}}
}
void ALDB91HUD::DrawHUD()
{
    Super::DrawHUD();const auto* PC=Cast<ALDB91Controller>(PlayerOwner);if(!PC){return;}
    DrawRect(FLinearColor(0,0,0,.8f),12,12,Canvas->SizeX-24,296);
    DrawText(TEXT("B91 FUNCTIONAL ENVIRONMENT / finite storage, flasks and cargo / remaining systems in development"),FLinearColor::White,22,20);
    DrawText(TEXT("RMB move | S stop | hold F open/close (0.3s) | hold V rear lockpick (3s) | hold B force rear (3s) | Esc cancel"),FLinearColor::White,22,43);
    DrawText(PC->IntentFeedback,FLinearColor::Yellow,22,66);
    if(const auto* D=PC->NearestDoor())
    {DrawText(D->PublicDoor.ToString()+FString::Printf(TEXT(" | revision %u | %s%s%s"),D->Revision,D->State&1?TEXT("open"):TEXT("closed"),D->State&2?TEXT(" / resident locked"):TEXT(" / unlocked"),D->State&4?TEXT(" / broken open"):TEXT("")),FLinearColor::White,22,89);}
    DrawText(TEXT("Only observed door revisions and local body handles are transmitted. Public fronts cannot lock. No full-match services active."),FLinearColor::White,22,115);
    DrawText(TEXT("Objects: hold F open/pickup | T take | Y store | C carry | J place at cursor | P push | X break | G drop bag item at cursor"),FLinearColor::White,22,138);
    DrawText(TEXT("Tab next container item | I next bag item | transfers/cargo need 6 quiet seconds | carry moves at 70% speed"),FLinearColor::White,22,160);
    FString Contents=TEXT("Opened storage: ");for(int N=0;N<PC->ContainerView.Num();++N){Contents+=(N==PC->ContainerSelection?TEXT(" >"):TEXT(" "))+LDInventory::Name(PC->ContainerView[N].Kind,ELDSkill::None);}
    DrawText(Contents,FLinearColor::White,22,182);
    const auto& Bag=PC->OwnerState.Inventory;const auto* Item=Bag.Items.IsValidIndex(PC->BagSelection)?&Bag.Items[PC->BagSelection]:nullptr;
    DrawText(FString::Printf(TEXT("Bag %d/24 | selected: %s | cargo: %s"),Bag.Items.Num(),Item?*LDInventory::Name(Item->Kind,Item->Skill):TEXT("empty"),PC->HasObjectCargo()?TEXT("carried"):TEXT("none")),FLinearColor::White,22,204);
    DrawText(TEXT("Hold 1/2/3/4: bind selected item to accessory 1/2/3/4 (quiet + 2s) | hold U unbind | L toggle bound Torch/Lantern"),FLinearColor::White,22,226);
    if(Item&&LDPortableTools::IsTool(Item->Kind)){DrawText(FString::Printf(TEXT("Physical tool | fuel %.1fs | %s | binding mask %u | cosmetic light adds no sight"),Item->FuelMs/1000.,Item->bLit?TEXT("lit"):TEXT("off"),Item->BindingMask),FLinearColor::Yellow,22,248);}
    DrawText(TEXT("Approved anchor: hold H attach bound Rope | O install carried Ladder | F traverse (2s) | K retrieve own unused link"),FLinearColor::White,22,270);
}
