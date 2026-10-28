#include "World/LDEnvironmentDirector.h"
#include "World/LDB91TraversalContract.h"
#include "Character/LDCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/LDNavigationComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

void ALDB91Controller::ServerObjectInteraction_Implementation(FLDObjectRequest R)
{if(auto* O=ALDWorldObjects::Get(GetWorld())){O->Begin(this,R);}}
void ALDB91Controller::ServerObjectHold_Implementation(uint32 Match,uint32 Life,uint32 Seq,uint32 N)
{if(auto* O=ALDWorldObjects::Get(GetWorld())){O->Hold(this,Match,Life,Seq,N,false);}}
void ALDB91Controller::ServerObjectRelease_Implementation(uint32 Match,uint32 Life,uint32 Seq)
{if(auto* O=ALDWorldObjects::Get(GetWorld())){O->Hold(this,Match,Life,Seq,0,true);}}
void ALDB91Controller::ClientObjectReceipt_Implementation(FLDWorldReceipt R)
{++ObjectReceiptSerial;if(R.Sequence>=LastObjectReceipt.Sequence){LastObjectReceipt=R;IntentFeedback=R.Reason;}}
bool ALDB91Controller::HasObjectCargo() const
{if(GetNetMode()!=NM_Client){if(auto* O=ALDWorldObjects::Get(GetWorld())){return O->Carried(this)!=nullptr;}}return bObjectCargo&&OwnerState.Life==ObjectViewLife&&OwnerState.Match==ObjectViewMatch;}
void ALDB91Controller::ClientCargoState_Implementation(uint32 Match,uint32 Life,bool Carrying,FGuid Observed)
{if(Match==OwnerState.Match&&Life==OwnerState.Life){bObjectCargo=Carrying;CargoHandle=Carrying?Observed:FGuid();}}
void ALDB91Controller::ClientContainerView_Implementation(uint32 Match,uint32 Life,FGuid Handle,uint32 Revision,const TArray<FLDContainerEntry>& Entries)
{if(Match!=OwnerState.Match||Life!=OwnerState.Life||Entries.Num()>24){return;}ContainerHandle=Handle;ContainerRevision=Revision;ContainerView=Entries;ContainerSelection=FMath::Clamp(ContainerSelection,0,FMath::Max(0,Entries.Num()-1));}
void ALDB91Controller::ClientObjectView_Implementation(uint32 Match,uint32 Life,uint64 Revision,const TArray<FLDObjectView>& Entries)
{
    if(Match!=OwnerState.Match||Life!=OwnerState.Life||Entries.Num()>128||(Match==ObjectViewMatch&&Life==ObjectViewLife&&Revision<=ObjectViewRevision)){return;}
    ObjectView=Entries;ObjectViewMatch=Match;ObjectViewLife=Life;ObjectViewRevision=Revision;ObjectViewAt=GetWorld()->GetTimeSeconds();
    if(GetNetMode()==NM_Client)
    {
        for(auto& P:ObjectProxies){if(P.Value.IsValid()){P.Value->SetObserved(false);}}
        for(const auto& V:Entries)
        {auto& Weak=ObjectProxies.FindOrAdd(V.Handle);auto* P=Weak.Get();if(!P){P=GetWorld()->SpawnActor<ALDWorldObject>();Weak=P;}P->Kind=V.Kind;P->Apply(V.Position,V.State,V.Revision);P->SetObserved(!(V.State&5));}
    }
    const auto* Open=Entries.FindByPredicate([&](const auto& E){return E.Handle==ContainerHandle&&E.Revision==ContainerRevision;});
    if(!Open||!GetPawn()||FVector::Dist2D(GetPawn()->GetActorLocation(),Open->Position)>120)
    {ContainerView.Reset();ContainerHandle.Invalidate();ContainerRevision=0;}
}
const FLDObjectView* ALDB91Controller::NearestObject() const
{
    if(!GetPawn()){return nullptr;}const FLDObjectView* Best=nullptr;double Distance=120*120;
    if(HasObjectCargo()){return ObjectView.FindByPredicate([&](const auto& V){return V.Handle==CargoHandle;});}
    for(const auto& V:ObjectView){const double D=FVector::DistSquared2D(GetPawn()->GetActorLocation(),V.Position);
    if(!(V.State&7)&&D<=Distance&&FMath::Abs(GetPawn()->GetActorLocation().Z-V.Position.Z)<150){Best=&V;Distance=D;}}
    return Best;
}
void ALDB91Controller::StartObject(ELDObjectVerb Verb,FVector Point)
{
    const auto* O=NearestObject();if(!CanWorldCommand()||(!O&&Verb!=ELDObjectVerb::Drop)){return;}ReleaseDoor();Navigation->Stop();
    FLDObjectRequest R;R.Match=OwnerState.Match;R.Life=OwnerState.Life;R.Sequence=++WorldSequence;R.Contract=LD_B91_LINK_WORLD_SHA;R.InventoryRevision=OwnerState.Inventory.Revision;R.Verb=Verb;R.Point=Point;
    if(O){R.Observed=O->Handle;R.ObjectRevision=O->Revision;R.ItemRevision=O->PhysicalRevision;}
    if(Verb==ELDObjectVerb::Take)
    {if(!ContainerView.IsValidIndex(ContainerSelection)){return;}const auto& I=ContainerView[ContainerSelection];R.Item=I.Item;R.ItemRevision=I.Revision;R.Observed=ContainerHandle;R.ObjectRevision=ContainerRevision;}
    if(Verb==ELDObjectVerb::Store||Verb==ELDObjectVerb::Drop)
    {if(!OwnerState.Inventory.Items.IsValidIndex(BagSelection)){return;}const auto& I=OwnerState.Inventory.Items[BagSelection];R.Item=I.Id;R.ItemRevision=I.Revision;}
    HeldObject=true;HeldSequence=R.Sequence;Nonce=0;SentAt=GetWorld()->GetTimeSeconds();ServerObjectInteraction(R);
}
void ALDB91Controller::ToggleSelectedTool()
{
    if(!CanWorldCommand()||!OwnerState.Inventory.Items.IsValidIndex(BagSelection)){return;}ReleaseDoor();const auto& Item=OwnerState.Inventory.Items[BagSelection];
    FLDObjectRequest R;R.Match=OwnerState.Match;R.Life=OwnerState.Life;R.Sequence=++WorldSequence;R.Contract=LD_B91_LINK_WORLD_SHA;R.InventoryRevision=OwnerState.Inventory.Revision;R.Item=Item.Id;R.ItemRevision=Item.Revision;R.Verb=ELDObjectVerb::Toggle;R.Accessory=255;
    for(int N=0;N<4;++N){if(OwnerState.Inventory.Bindings[7+N]==R.Item){R.Accessory=N;break;}}ServerObjectInteraction(R);
}
void ALDB91Controller::StartLink(ELDObjectVerb Verb)
{
    if(!CanWorldCommand()||!GetPawn()){return;}const FLDObjectView* Anchor=nullptr;double Best=120*120;
    for(const auto& V:ObjectView){const double D=FVector::DistSquared2D(GetPawn()->GetActorLocation(),V.Position);if(V.Kind==ELDObjectKind::Anchor&&!(V.State&4)&&D<=Best&&FMath::Abs(GetPawn()->GetActorLocation().Z-V.Position.Z)<150){Anchor=&V;Best=D;}}
    if(!Anchor){return;}FLDObjectRequest R;R.Match=OwnerState.Match;R.Life=OwnerState.Life;R.Sequence=++WorldSequence;R.Contract=LD_B91_LINK_WORLD_SHA;R.Verb=Verb;R.Observed=Anchor->Handle;R.ObjectRevision=Anchor->Revision;R.InventoryRevision=OwnerState.Inventory.Revision;
    if(Verb==ELDObjectVerb::Deploy)
    {const auto* Ladder=ObjectView.FindByPredicate([&](const auto& V){return V.Handle==CargoHandle&&V.Kind==ELDObjectKind::Ladder;});if(!Ladder){return;}R.Anchor=Anchor->Handle;R.AnchorRevision=Anchor->Revision;R.Observed=Ladder->Handle;R.ObjectRevision=Ladder->Revision;}
    if(Verb==ELDObjectVerb::Attach)
    {if(!OwnerState.Inventory.Items.IsValidIndex(BagSelection)){return;}const auto& I=OwnerState.Inventory.Items[BagSelection];R.Item=I.Id;R.ItemRevision=I.Revision;R.Accessory=255;for(int N=0;N<4;++N){if(OwnerState.Inventory.Bindings[7+N]==I.Id){R.Accessory=N;break;}}}
    ReleaseDoor();Navigation->Stop();HeldObject=true;HeldSequence=R.Sequence;Nonce=0;SentAt=GetWorld()->GetTimeSeconds();ServerObjectInteraction(R);
}
void ALDB91Controller::ClientTraversalState_Implementation(uint32 Match,uint32 Life,uint32 Sequence,bool Active,FVector_NetQuantize10 Position)
{
    if(!IsLocalController()||Match!=OwnerState.Match||Life!=OwnerState.Life||Sequence<TraversalSequence){return;}auto* Body=Cast<ALDCharacter>(GetPawn());if(!Body){return;}
    if(Sequence==TraversalSequence&&Active&&!bTraversalActive){return;}TraversalSequence=Sequence;TraversalMatch=Match;TraversalLife=Life;bTraversalActive=Active;Navigation->ResetLife();Body->GetCharacterMovement()->StopMovementImmediately();Body->SetActorLocation(Position);
    if(Active){Body->GetCharacterMovement()->DisableMovement();}else{Body->GetCharacterMovement()->SetMovementMode(MOVE_Walking);}
}
void ALDB91Controller::ClientTraversalPose_Implementation(uint32 Match,uint32 Life,uint32 Sequence,FVector_NetQuantize10 Position)
{if(bTraversalActive&&Sequence==TraversalSequence&&Match==OwnerState.Match&&Life==OwnerState.Life){if(auto* Body=GetPawn().Get()){Body->SetActorLocation(Position);}}}

void ALDB91Controller::StartIgnition()
{
    if(!CanWorldCommand()||!GetPawn()||!OwnerState.Inventory.Items.IsValidIndex(BagSelection)){return;}const auto& Item=OwnerState.Inventory.Items[BagSelection];if(Item.Kind!=ELDItemKind::Torch){IntentFeedback=TEXT("Select a physically bound Torch");return;}
    FLDObjectRequest R;R.Match=OwnerState.Match;R.Life=OwnerState.Life;R.Sequence=++WorldSequence;R.Contract=LD_B91_LINK_WORLD_SHA;R.Verb=ELDObjectVerb::Ignite;R.Item=Item.Id;R.ItemRevision=Item.Revision;R.InventoryRevision=OwnerState.Inventory.Revision;R.Accessory=255;
    for(int N=0;N<4;++N){if(OwnerState.Inventory.Bindings[7+N]==Item.Id){R.Accessory=N;break;}}
    const auto* Socket=NearestObject();if(Socket&&Socket->Kind==ELDObjectKind::Sapling){R.Observed=Socket->Handle;R.ObjectRevision=Socket->Revision;}
    else{FHitResult Hit;if(!GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_GameTraceChannel1),false,Hit)){return;}R.Point=Hit.ImpactPoint;}
    ReleaseDoor();Navigation->Stop();HeldObject=true;HeldSequence=R.Sequence;Nonce=0;SentAt=GetWorld()->GetTimeSeconds();ServerObjectInteraction(R);
}
