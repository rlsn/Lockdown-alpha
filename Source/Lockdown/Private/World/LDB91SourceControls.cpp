#include "World/LDEnvironmentDirector.h"
#include "World/LDPortableTools.h"
#include "Character/LDCharacter.h"
#include "Navigation/LDNavigationComponent.h"
#include "UI/LDControlSettings.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

namespace
{
FVector ObjectFloor(const FLDObjectView& V)
{const double H=V.Kind==ELDObjectKind::TallGrass?78:V.Kind==ELDObjectKind::Sapling?203:V.Kind==ELDObjectKind::Crate?113:V.Kind==ELDObjectKind::Barrel?73:V.Kind==ELDObjectKind::Chest?43:V.Kind==ELDObjectKind::Furniture?53:V.Kind==ELDObjectKind::Torch?48:V.Kind==ELDObjectKind::Lantern?33:18;return FVector(V.Position)-FVector(0,0,H);}
}
void ALDB91Controller::ClientB91Channel_Implementation(FLDB91Channel P,bool Active)
{if(P.Match!=OwnerState.Match||P.Life!=OwnerState.Life||(P.Domain==Channel.Domain&&P.Sequence<Channel.Sequence)){return;}if(Active){Channel=P;ChannelActive=true;}else if(Channel.Sequence==P.Sequence&&Channel.Domain==P.Domain){ChannelActive=false;}if(!Active&&HeldSequence==P.Sequence&&HeldObject==(P.Domain==2)){HeldSequence=0;}}
void ALDB91Controller::CancelInterruptibleHolds()
{ReleaseDoor();PendingObject.Invalidate();PendingDoor.Invalidate();PendingSource.Invalidate();ArmedSource.Invalidate();BasicAttackArmed=false;ChannelActive=false;ALDPlayerController::CancelInterruptibleHolds();}
void ALDB91Controller::StopSourceOrder(){CancelInterruptibleHolds();Navigation->Stop();CancelArmed();}
const FLDObjectView* ALDB91Controller::ObjectUnderPointer() const
{float X,Y;if(!GetMousePosition(X,Y)){return nullptr;}const FLDObjectView* Best=nullptr;double D=44*44;for(const auto& V:ObjectView){if(V.State&5){continue;}FVector2D P;if(ProjectWorldLocationToScreen(V.Position,P)){const double N=FVector2D::DistSquared(P,FVector2D(X,Y));if(N<D){D=N;Best=&V;}}}return Best;}
const FLDEnvironmentViewEntry* ALDB91Controller::DoorUnderPointer() const
{float X,Y;if(!GetMousePosition(X,Y)){return nullptr;}const FLDEnvironmentViewEntry* Best=nullptr;double D=40*40;for(const auto& V:View){if(V.PublicDoor.IsNone()){continue;}FVector2D P;if(ProjectWorldLocationToScreen(V.Position,P)){const double N=FVector2D::DistSquared(P,FVector2D(X,Y));if(N<D){D=N;Best=&V;}}}return Best;}
void ALDB91Controller::PointerOrder(bool Right,FGuid Object,FGuid Door,FVector Point)
{
    if(!CanWorldCommand()){return;}const auto* V=ObjectView.FindByPredicate([&](const auto& E){return E.Handle==Object;});
    if(!Right&&(ArmedSource.IsValid()||BasicAttackArmed)){ConfirmSource(Object,Point);return;}
    if(!Right&&!IsTargeting()&&PointerService(Point)){return;}
    if(!Right){SelectedObject=Object;if(!V||!(V->Kind==ELDObjectKind::Chest||V->Kind==ELDObjectKind::Crate||V->Kind==ELDObjectKind::Barrel||V->Kind==ELDObjectKind::Furniture)){return;}}
    StopSourceOrder();
    if(V)
    {const bool Pickup=V->Kind==ELDObjectKind::Water||V->Kind==ELDObjectKind::Oil||V->Kind==ELDObjectKind::LooseItem||V->Kind==ELDObjectKind::Torch||V->Kind==ELDObjectKind::Lantern||V->Kind==ELDObjectKind::Rope||V->Kind==ELDObjectKind::ForestryKnife||V->Kind==ELDObjectKind::Ladder||V->Kind==ELDObjectKind::Anchor;
     if(Right&&!Pickup){PendingObject.Invalidate();Navigation->Navigate(Point);return;}const FVector Floor=ObjectFloor(*V);const FVector Away=(GetPawn()->GetActorLocation()-Floor).GetSafeNormal2D();Navigation->Navigate(Floor,V->Handle);}
    else if(Door.IsValid())
    {const auto* D=View.FindByPredicate([&](const auto& E){return E.Handle==Door;});if(!D){PendingDoor.Invalidate();return;}Navigation->Navigate(FVector(D->Position)-FVector(0,0,130),D->Handle);}
    else{Navigation->Navigate(Point);return;}
    // Navigate cancels the previous order; install its continuation afterwards.
    PendingRight=Right;PendingIssued=GetWorld()->GetTimeSeconds();PendingObject=Object;PendingDoor=Right?Door:FGuid();PendingPoint=Point;
}
void ALDB91Controller::IssueDoor(ELDWorldVerb Verb,FGuid Handle)
{const auto* D=View.FindByPredicate([&](const auto& E){return E.Handle==Handle&&!E.PublicDoor.IsNone();});const auto* E=ALDEnvironmentDirector::Get(GetWorld());if(!D||!E||!CanWorldCommand()){return;}ReleaseDoor();Navigation->Stop();FLDWorldRequest R;R.Match=OwnerState.Match;R.Life=OwnerState.Life;R.Sequence=++WorldSequence;R.Contract=E->Contract;R.Verb=Verb;R.Observed=D->Handle;R.ObjectRevision=D->Revision;HeldObject=false;HeldSequence=R.Sequence;Nonce=0;SentAt=-1000;ServerWorldInteraction(R);}
void ALDB91Controller::IssueObject(ELDObjectVerb Verb,FGuid Handle,FGuid Item,FVector Point)
{
    if(!CanWorldCommand()){return;}const auto* V=ObjectView.FindByPredicate([&](const auto& E){return E.Handle==Handle;});FLDObjectRequest R;R.Match=OwnerState.Match;R.Life=OwnerState.Life;R.Sequence=++WorldSequence;R.Contract=ALDWorldObjects::ActiveContract();R.Verb=Verb;R.InventoryRevision=OwnerState.Inventory.Revision;R.Point=Point;R.Item=Item;
    if(V){R.Observed=V->Handle;R.ObjectRevision=V->Revision;R.ItemRevision=V->PhysicalRevision;}if(const auto* I=OwnerState.Inventory.Find(Item)){R.ItemRevision=I->Revision;R.Accessory=255;for(int N=0;N<4;++N){if(OwnerState.Inventory.Bindings[7+N]==Item){R.Accessory=N;break;}}}
    if(Verb==ELDObjectVerb::Take){const auto* C=ContainerView.FindByPredicate([&](const auto& I){return I.Item==Item;});if(!C){return;}R.Observed=ContainerHandle;R.ObjectRevision=ContainerRevision;R.ItemRevision=C->Revision;}
    if(Verb==ELDObjectVerb::Deploy){const auto* Cargo=ObjectView.FindByPredicate([&](const auto& E){return E.Handle==CargoHandle;});if(!Cargo||!V){return;}R.Anchor=V->Handle;R.AnchorRevision=V->Revision;R.Observed=Cargo->Handle;R.ObjectRevision=Cargo->Revision;}
    ReleaseDoor();Navigation->Stop();HeldObject=true;HeldSequence=R.Sequence;Nonce=0;SentAt=-1000;ServerObjectInteraction(R);
}
void ALDB91Controller::UseAccessory(uint8 Slot,bool Alternate)
{
    if(Slot>=4||!CanWorldCommand()){return;}const auto* I=OwnerState.Inventory.Find(OwnerState.Inventory.Bindings[7+Slot]);if(!I){IntentFeedback=TEXT("Empty accessory slot");return;}StopSourceOrder();ArmedAccessory=Slot;
    if(Alternate&&(I->Kind==ELDItemKind::Torch||I->Kind==ELDItemKind::Lantern)){IssueObject(ELDObjectVerb::Toggle,FGuid(),I->Id);return;}
    if(I->Kind==ELDItemKind::Lantern){IssueObject(ELDObjectVerb::Toggle,FGuid(),I->Id);return;}
    ArmedSource=I->Id;ArmedVerb=I->Kind==ELDItemKind::ForestryKnife?(Alternate?ELDObjectVerb::Chop:ELDObjectVerb::Harvest):I->Kind==ELDItemKind::Rope?ELDObjectVerb::Attach:ELDObjectVerb::Ignite;
    IntentFeedback=I->Kind==ELDItemKind::ForestryKnife?TEXT("Target a sapling. Confirm once; S stops the4s item channel."):I->Kind==ELDItemKind::Torch?TEXT("Target a sapling, grass patch or Oil ground. Confirm once; S stops ignition."):I->Kind==ELDItemKind::Rope?TEXT("Target an approved anchor to attach this Rope."):TEXT("Confirm the item target once; S stops a channel.");
}
void ALDB91Controller::ConfirmSource(FGuid Object,FVector Point)
{
    const auto* V=ObjectView.FindByPredicate([&](const auto& E){return E.Handle==Object;});
    if(BasicAttackArmed){BasicAttackArmed=false;ReleaseDoor();Navigation->Stop();const FVector Aim=((V?FVector(V->Position):Point)-GetPawn()->GetActorLocation()).GetSafeNormal2D();ServerCombatAction(OwnerState.Match,OwnerState.Life,NextCombatToken(),0,Aim);return;}
    const FGuid Id=ArmedSource;ArmedSource.Invalidate();
    if(ArmedVerb==ELDObjectVerb::Place&&HasObjectCargo()){IssueObject(ELDObjectVerb::Place,CargoHandle,FGuid(),Point);return;}
    if(ArmedVerb==ELDObjectVerb::Deploy&&HasObjectCargo()){IssueObject(ELDObjectVerb::Deploy,Object);return;}
    if(ArmedVerb==ELDObjectVerb::Drop){IssueObject(ELDObjectVerb::Drop,FGuid(),Id,Point);return;}
    const auto* I=OwnerState.Inventory.Find(Id);if(!I){return;}
    if(V&&(I->Kind==ELDItemKind::ForestryKnife||I->Kind==ELDItemKind::Torch)&&FVector::Dist2D(GetPawn()->GetActorLocation(),V->Position)>120)
    {const auto Handle=V->Handle;const FVector Floor=ObjectFloor(*V);const auto Verb=ArmedVerb;Navigation->Navigate(Floor,Handle);PendingObject=Handle;PendingSource=Id;PendingSourceVerb=Verb;PendingIssued=GetWorld()->GetTimeSeconds();return;}
    if(I->Kind==ELDItemKind::ForestryKnife){IssueObject(ArmedVerb,Object,Id,Point);}
    else if(I->Kind==ELDItemKind::Torch){IssueObject(ELDObjectVerb::Ignite,V&&(V->Kind==ELDObjectKind::Sapling||V->Kind==ELDObjectKind::TallGrass)?Object:FGuid(),Id,Point);}
    else if(I->Kind==ELDItemKind::Rope){IssueObject(ELDObjectVerb::Attach,Object,Id);}
    else{ReleaseDoor();Navigation->Stop();SendItemAction(Id,FGuid(),Point);}
}
void ALDB91Controller::ItemCommand(FName Command,int32 Index)
{
    if(Command==TEXT("Carry")){IssueObject(ELDObjectVerb::Carry,SelectedObject);return;}if(Command==TEXT("Select")){BagSelection=Index;return;}if(Command==TEXT("Use")){UseAccessory(Index);return;}if(Command==TEXT("Alternate")){UseAccessory(Index,true);return;}
    const auto* I=OwnerState.Inventory.Items.IsValidIndex(BagSelection)?&OwnerState.Inventory.Items[BagSelection]:nullptr;
    if(Command==TEXT("Equip")&&I){StopSourceOrder();StartLoadoutHold(LDInventory::Equip,I->Id,7+Index);}
    else if(Command==TEXT("Unequip")&&I){StopSourceOrder();StartLoadoutHold(LDInventory::Unequip,I->Id,7);}
    else if(Command==TEXT("Drop")&&I){StopSourceOrder();ArmedSource=I->Id;ArmedVerb=ELDObjectVerb::Drop;IntentFeedback=TEXT("Confirm a nearby drop point");}
    else if(Command==TEXT("Store")&&I&&ContainerHandle.IsValid()){IssueObject(ELDObjectVerb::Store,ContainerHandle,I->Id);}
    else if(Command==TEXT("Take")&&ContainerView.IsValidIndex(Index)){IssueObject(ELDObjectVerb::Take,ContainerHandle,ContainerView[Index].Item);}
    else if(Command==TEXT("Place")&&HasObjectCargo()){StopSourceOrder();ArmedSource=CargoHandle;ArmedVerb=ELDObjectVerb::Place;IntentFeedback=TEXT("Confirm cargo placement");}
    else if(Command==TEXT("Deploy")&&HasObjectCargo()){StopSourceOrder();ArmedSource=CargoHandle;ArmedVerb=ELDObjectVerb::Deploy;IntentFeedback=TEXT("Target an approved anchor for the carried Ladder");}
    else if(Command==TEXT("Retrieve")){IssueObject(ELDObjectVerb::Retrieve,SelectedObject);}
}
void ALDB91Controller::TickSourceControls()
{
    if(!Controls){return;}const double Now=GetWorld()->GetTimeSeconds();
    if(Channel.Match!=OwnerState.Match||Channel.Life!=OwnerState.Life){StopSourceOrder();Channel=FLDB91Channel();Channel.Match=OwnerState.Match;Channel.Life=OwnerState.Life;}
    if(HeldSequence&&Now-SentAt>=.1){SentAt=Now;if(!CanWorldCommand()){ReleaseDoor();}else if(HeldObject){ServerObjectHold(OwnerState.Match,OwnerState.Life,HeldSequence,++Nonce);}else{ServerWorldHold(OwnerState.Match,OwnerState.Life,HeldSequence,++Nonce,false);}}
    if(WasInputKeyJustPressed(Controls->Key(TEXT("Stop")))||WasInputKeyJustPressed(EKeys::Escape)){StopSourceOrder();return;}
    if(!CanWorldCommand()){return;}auto* HUD=Cast<ALDB91HUD>(GetHUD());const bool OverUI=HUD&&HUD->PointerOverControls();
    if(WasInputKeyJustPressed(Controls->Key(TEXT("Attack")))){StopSourceOrder();BasicAttackArmed=true;IntentFeedback=TEXT("Confirm an ordinary attack target");}
    for(int N=0;N<4;++N){if(WasInputKeyJustPressed(Controls->Key(FName(*FString::Printf(TEXT("Item%d"),N+1))))){UseAccessory(N);}}
    for(int N=0;N<6;++N){if(WasInputKeyJustPressed(Controls->Key(FName(*FString::Printf(TEXT("Skill%d"),N+1))))){StopSourceOrder();ArmSkill(N);}}
    if(!OverUI&&(WasInputKeyJustPressed(Controls->Key(TEXT("Select")))||WasInputKeyJustPressed(Controls->Key(TEXT("Navigate")))))
    {FHitResult Hit;if(GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_GameTraceChannel1),false,Hit)){const auto* V=ObjectUnderPointer();const auto* D=DoorUnderPointer();const bool Right=WasInputKeyJustPressed(Controls->Key(TEXT("Navigate")));if(!Right&&IsTargeting()&&!ArmedSource.IsValid()&&!BasicAttackArmed){ConfirmArmed(Hit.ImpactPoint,FGuid());}else{PointerOrder(Right,V?V->Handle:FGuid(),D?D->Handle:FGuid(),Hit.ImpactPoint);}}}
    if(!PendingObject.IsValid()&&!PendingDoor.IsValid()){return;}if(Now-PendingIssued>20){StopSourceOrder();IntentFeedback=TEXT("Approach timed out");return;}
    if(PendingObject.IsValid())
    {const auto* V=ObjectView.FindByPredicate([&](const auto& E){return E.Handle==PendingObject&&!(E.State&5);});if(!V){StopSourceOrder();return;}if(Navigation->Status==TEXT("Arrived")&&FVector::Dist2D(GetPawn()->GetActorLocation(),V->Position)<=120&&FMath::Abs(GetPawn()->GetActorLocation().Z-ObjectFloor(*V).Z-90)<100)
     {const auto Copy=*V;const auto Source=PendingSource;const auto Verb=PendingSourceVerb;PendingSource.Invalidate();PendingObject.Invalidate();Navigation->Stop();if(Source.IsValid()){IssueObject(Verb,Copy.Handle,Source);}else if(!PendingRight){SelectedObject=Copy.Handle;IssueObject(ELDObjectVerb::Open,Copy.Handle);}else{IssueObject(Copy.Kind==ELDObjectKind::Anchor?ELDObjectVerb::Use:Copy.Kind==ELDObjectKind::Ladder?ELDObjectVerb::Carry:ELDObjectVerb::Pickup,Copy.Handle);}}}
    else if(PendingDoor.IsValid())
    {const auto* D=View.FindByPredicate([&](const auto& E){return E.Handle==PendingDoor;});if(!D){StopSourceOrder();return;}if(Navigation->Status==TEXT("Arrived")&&FVector::Dist2D(GetPawn()->GetActorLocation(),D->Position)<=120){const auto Copy=*D;PendingDoor.Invalidate();IssueDoor(Copy.State&1?ELDWorldVerb::Close:ELDWorldVerb::Open,Copy.Handle);}}
}
