#include "Tests/LDB91TraversalProbe.h"
#include "World/LDEnvironmentDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Navigation/LDNavigationComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ALDB91TraversalProbe::ALDB91TraversalProbe()
{PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;Started=FPlatformTime::Seconds();}
void ALDB91TraversalProbe::Check(bool Good,const FString& Text)
{auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("case"),Text);R->SetBoolField(TEXT("passed"),Good);Checks.Add(MakeShared<FJsonValueObject>(R));Failures+=!Good;UE_LOG(LogTemp,Display,TEXT("LD_B91_TRAVERSAL %s passed=%d"),*Text,Good);}
void ALDB91TraversalProbe::Place(ALDB91Controller* PC,FVector Centre)
{PC->Navigation->ResetLife();auto* Body=Cast<ALDCharacter>(PC->GetPawn());Body->GetCharacterMovement()->SetMovementMode(MOVE_Walking);Body->GetCharacterMovement()->StopMovementImmediately();Body->SetActorLocation(Centre);PC->ClientSetLocation(Centre,FRotator::ZeroRotator);}
void ALDB91TraversalProbe::Task(uint8 Value)
{Phase=Value;At=GetWorld()->GetTimeSeconds();Objects->Publish();ALDEnvironmentDirector::Get(GetWorld())->Publish();A->ClientB91ProbeTask(Value);B->ClientB91ProbeTask(Value);Save(false);}
void ALDB91TraversalProbe::Native(ELDObjectVerb Verb,ALDB91Controller* PC)
{Objects->Publish();const auto& C=Objects->Clients.FindChecked(PC);NativeRequest=FLDObjectRequest();NativeRequest.Match=PC->OwnerState.Match;NativeRequest.Life=PC->OwnerState.Life;NativeRequest.Sequence=50000+Phase*10;NativeRequest.Contract=Objects->Contract;NativeRequest.Observed=C.Handles.FindChecked(Anchor);NativeRequest.ObjectRevision=Anchor->Revision;NativeRequest.Verb=Verb;NativeRequest.InventoryRevision=PC->OwnerState.Inventory.Revision;if(Verb==ELDObjectVerb::Attach){NativeRequest.Item=Rope->Identity;NativeRequest.ItemRevision=Rope->Revision;NativeRequest.Accessory=0;}Beat=0;Objects->Begin(PC,NativeRequest);At=GetWorld()->GetTimeSeconds();}
void ALDB91TraversalProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(Phase==99){return;}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M){return;}const double Now=GetWorld()->GetTimeSeconds();
    if(Failures||FPlatformTime::Seconds()-Started>230){if(!Failures){Check(false,TEXT("Traversal fixture timeout"));}Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,3);return;}
    if(!Phase)
    {
        for(FConstPlayerControllerIterator I=GetWorld()->GetPlayerControllerIterator();I;++I){if(auto* PC=Cast<ALDB91Controller>(I->Get());PC&&PC->GetPawn()){if(PC->EnvironmentProbeRole==1){A=PC;}if(PC->EnvironmentProbeRole==2){B=PC;}}}
        if(!A||!B||Now<7){return;}Objects=ALDWorldObjects::Get(GetWorld());if(!Objects||!Objects->Ready){return;}AddTickPrerequisiteActor(Objects);Ladder=Objects->Find(TEXT("prop_ladder_000"));Anchor=Objects->Find(TEXT("roof_anchor_00"));auto* RopeObject=Objects->Find(TEXT("prop_rope_000"));Rope=RopeObject?RopeObject->Physical.Get():nullptr;
        Check(A!=B&&!A->IsLocalController()&&!B->IsLocalController(),TEXT("Two independent remote traversal owners"));Check(Ladder&&Anchor&&Rope&&Objects->Objects.Num()==264,TEXT("12 finite world ladders and16 approved paired anchors"));if(Failures){return;}
        for(auto* PC:{A,B}){auto* Own=M->StateFor(PC);Own->ProtectedUntil=0;M->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()))->ProtectionUntil=0;M->SyncOwner(PC);}
        Place(A,Ladder->LastClear+FVector(-100,0,92.15));Place(B,Ladder->LastClear+FVector(100,0,92.15));
        auto* Body=Cast<ALDCharacter>(A->GetPawn());const auto* C=Body->GetCapsuleComponent();
        for(auto O:Objects->Objects){if(!O.IsValid()||O->Kind!=ELDObjectKind::Anchor){continue;}const bool Supported=Objects->EndpointClear(A,O->Lower)&&Objects->EndpointClear(A,O->Upper);bool Clear=true;const FVector L=O->Lower+FVector(0,0,92.15),U=O->Upper+FVector(0,0,92.15),V(O->Lower.X,O->Lower.Y,U.Z);FCollisionQueryParams Q(SCENE_QUERY_STAT(B91AnchorProbe),false,Body);FHitResult Hit;
         Clear&=!GetWorld()->SweepSingleByChannel(Hit,L,V,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(C->GetScaledCapsuleRadius(),C->GetScaledCapsuleHalfHeight()),Q);Clear&=!GetWorld()->SweepSingleByChannel(Hit,V,U,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(C->GetScaledCapsuleRadius(),C->GetScaledCapsuleHalfHeight()),Q);
         auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("anchor"),O->SourceId.ToString());Row->SetBoolField(TEXT("supported"),Supported);Row->SetBoolField(TEXT("capsule_route_clear"),Clear);Row->SetStringField(TEXT("lower"),O->Lower.ToString());Row->SetStringField(TEXT("upper"),O->Upper.ToString());Geometry.Add(MakeShared<FJsonValueObject>(Row));Check(Supported&&Clear,TEXT("Supported endpoints and bidirectional capsule route: ")+O->SourceId.ToString());}
        Check(Ladder->Weight()==10&&!Ladder->Container()&&!Ladder->BlocksWalk()&&Ladder->Size().Equals(FVector(70,500,30)),TEXT("Ladder is one weight10 nonbag world prop with no walk/sight obstruction"));
        bool Fits=true;for(auto O:Objects->Objects){if(O.IsValid()&&O->Kind==ELDObjectKind::Ladder){FVector Surface;Fits&=Objects->Placement(O.Get(),O->LastClear,Surface);}}Check(Fits,TEXT("All12 ladder pads fit the full5m ground footprint"));Phase=90;At=Now;return;
    }
    if(Phase==90){if(Now-At>1){Task(1);}return;}
    if(Phase==5&&!Interrupted&&Objects->Traversing(A)&&Now-At>.8)
    {Check(!M->Combat->Allows(Cast<ALDCharacter>(A->GetPawn()),LDTactical::EAction::Attack)&&!M->Combat->Allows(Cast<ALDCharacter>(A->GetPawn()),LDTactical::EAction::Item),TEXT("Traversal suppresses attacks and portable activation"));M->Combat->ApplyStatus(Cast<ALDCharacter>(A->GetPawn()),LDTactical::EStatus::Root,LDTactical::Milliseconds(Now)+1000,FGuid::NewGuid());Interrupted=true;}
    if(Phase==91)
    {
        Objects->Hold(A,NativeRequest.Match,NativeRequest.Life,NativeRequest.Sequence,++Beat,false);
        if(!Interrupted&&Now-At>.7){M->Combat->ApplyStatus(Cast<ALDCharacter>(A->GetPawn()),LDTactical::EStatus::Stun,LDTactical::Milliseconds(Now)+1000,FGuid::NewGuid());Interrupted=true;}
        if(Now-At<2){return;}Check(!Objects->Traversing(A)&&FVector::Dist(A->GetPawn()->GetActorLocation(),Anchor->Upper+FVector(0,0,92.15))<5,TEXT("Stun cancels descending rope use and returns to the upper endpoint"));Check(Anchor->LinkInstalled()&&!Objects->Holds.Contains(A),TEXT("Interrupted rope releases reservation and retains its original installed item"));Phase=92;At=Now;return;
    }
    if(Phase==92)
    {if(Now-At<.5){return;}Place(B,Anchor->Lower+FVector(0,0,92.15));Native(ELDObjectVerb::Use,A);Check(!Objects->Traversing(A)&&Anchor->LinkInstalled(),TEXT("Occupied destination rejects traversal before reserving or moving a body"));Place(B,Anchor->Lower+FVector(0,300,92.15));Phase=93;Native(ELDObjectVerb::Use,A);return;}
    if(Phase==93)
    {if(Now-At<.8){return;}Check(!Objects->Traversing(A)&&!Objects->Holds.Contains(A)&&FVector::Dist(A->GetPawn()->GetActorLocation(),Anchor->Upper+FVector(0,0,92.15))<5,TEXT("Missing heartbeat releases the link and safely restores the last endpoint"));Phase=94;Interrupted=false;Native(ELDObjectVerb::Use,A);return;}
    if(Phase==94)
    {Objects->Hold(A,NativeRequest.Match,NativeRequest.Life,NativeRequest.Sequence,++Beat,false);if(!Interrupted&&Now-At>.5){Place(B,Anchor->Lower+FVector(0,0,92.15));Interrupted=true;}if(Now-At<1){return;}Check(Interrupted&&!Objects->Traversing(A)&&FVector::Dist(A->GetPawn()->GetActorLocation(),Anchor->Upper+FVector(0,0,92.15))<5&&FVector::Dist(B->GetPawn()->GetActorLocation(),Anchor->Lower+FVector(0,0,92.15))<5,TEXT("Mid-use destination occupancy cancels without displacing either body"));Place(B,Anchor->Lower+FVector(0,300,92.15));Phase=95;Native(ELDObjectVerb::Use,A);return;}
    if(Phase==95)
    {Objects->Hold(A,NativeRequest.Match,NativeRequest.Life,NativeRequest.Sequence,++Beat,false);if(Now-At<.5){return;}auto Attempt=NativeRequest;Attempt.Sequence=NativeRequest.Sequence+1;Attempt.Verb=ELDObjectVerb::Retrieve;Objects->Begin(A,Attempt);Check(Objects->Traversing(A)&&Anchor->Contents.Num()==1&&Rope->Holder==Anchor,TEXT("Even the deployer cannot retrieve a link while a user occupies it"));Objects->Cancel(A,TEXT("Diagnostic release after busy-retrieval refusal"));Phase=96;Interrupted=false;Native(ELDObjectVerb::Use,A);return;}
    if(Phase==96)
    {
        if(!Interrupted){Objects->Hold(A,NativeRequest.Match,NativeRequest.Life,NativeRequest.Sequence,++Beat,false);if(Now-At<.5){return;}auto* Body=Cast<ALDCharacter>(A->GetPawn());const bool WasTraversing=Objects->Traversing(A);M->EndContenderLife(A,Now);Check(WasTraversing&&IsValid(Body)&&!Objects->Traversing(A)&&!Objects->Holds.Contains(A)&&FVector::Dist(Body->GetActorLocation(),Anchor->Upper+FVector(0,0,92.15))<5&&Anchor->Contents.Num()==1&&Rope->Holder==Anchor,TEXT("Actual end-life restores the body and releases its link before unpossessing or dropping property"));Interrupted=true;return;}
        auto* Own=M->StateFor(A);if(!Own||!Own->bActive||!A->GetPawn()){return;}if(!ReturnAt){ReturnAt=Now;Own->ProtectedUntil=0;M->Combat->StateFor(Cast<ALDCharacter>(A->GetPawn()))->ProtectionUntil=0;M->SyncOwner(A);Place(A,Anchor->Upper+FVector(0,0,92.15));}if(Now-ReturnAt>6.3){Phase=97;Native(ELDObjectVerb::Retrieve,A);}return;
    }
    if(Phase==97)
    {Objects->Hold(A,NativeRequest.Match,NativeRequest.Life,NativeRequest.Sequence,++Beat,false);if(Now-At<2){return;}Check(!Anchor->LinkInstalled()&&Rope->Holder==A&&!Rope->BindingMask&&M->StateFor(A)->Inventory.Find(Rope->Identity),TEXT("Quiet rope detach recovers the same unbound physical identity"));Objects->Begin(A,NativeRequest);Check(Rope->Holder==A&&Anchor->Contents.IsEmpty(),TEXT("Duplicate detach receipt cannot duplicate the recovered Rope"));Phase=98;Native(ELDObjectVerb::Attach,A);Check(!Objects->Holds.Contains(A)&&!Anchor->LinkInstalled()&&Rope->Holder==A,TEXT("Exact owned but unbound Rope cannot activate an anchor"));FString Why;Check(Objects->Conserved(Why),TEXT("All132 finite physical identities have exactly one location after traversal"));Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,Failures?3:0);return;}
    auto Read=[](ALDB91Controller* PC,uint8 Value)->TSharedPtr<FJsonObject>{TSharedPtr<FJsonObject> R;const auto* Text=PC->EnvironmentProbeReports.Find(Value);if(Text){FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(*Text),R);}return R;};const auto RA=Read(A,Phase),RB=Read(B,Phase);if(!RA||!RB){return;}
    Check(RA->GetBoolField(TEXT("completed"))&&RB->GetBoolField(TEXT("completed")),FString::Printf(TEXT("Both remote traversal phase%u reports complete"),Phase));
    auto OneWinner=[&](){return (RA->GetIntegerField(TEXT("receipt_result"))==1)+(RB->GetIntegerField(TEXT("receipt_result"))==1)==1;};
    if(Phase==1)
    {Deployer=RA->GetIntegerField(TEXT("receipt_result"))==1?A:B;Check(OneWinner()&&Ladder->Carrier==Deployer,TEXT("Ladder pickup race binds one existing world instance once"));Place(A,Anchor->Lower+FVector(0,-100,92.15));Place(B,Anchor->Lower+FVector(0,100,92.15));Task(2);return;}
    if(Phase==2)
    {Check(Anchor->LinkedLadder==Ladder&&Ladder->InstalledAt==Anchor&&!Objects->Carried(A)&&!Objects->Carried(B),TEXT("Deployment installs the carried Ladder only at its approved pair"));Check(Anchor->Deployer==M->ContenderId(Deployer)&&Anchor->LinkInstalled(),TEXT("Retrieval authority remains private to the deployer"));Task(3);return;}
    if(Phase==3)
    {Check(OneWinner()&&!Objects->Traversing(A)&&!Objects->Traversing(B),TEXT("One-user link contention admits exactly one full two-second traversal"));const auto* Winner=RA->GetIntegerField(TEXT("receipt_result"))==1?A:B;Check(FVector::Dist(Winner->GetPawn()->GetActorLocation(),Anchor->Upper+FVector(0,0,92.15))<5,TEXT("Native owner body reaches the supported upper landing"));Place(Winner==A?B:A,Anchor->Lower+FVector(0,300,92.15));Task(4);return;}
    if(Phase==4)
    {Check(OneWinner(),TEXT("Installed ladder supports the reverse upper-to-lower route"));Place(A,Anchor->Lower+FVector(0,0,92.15));Place(B,Anchor->Lower+FVector(0,300,92.15));Interrupted=false;Task(5);return;}
    if(Phase==5)
    {Check(Interrupted&&RA->GetIntegerField(TEXT("receipt_result"))==2&&!Objects->Traversing(A)&&FVector::Dist(A->GetPawn()->GetActorLocation(),Anchor->Lower+FVector(0,0,92.15))<5,TEXT("Root interrupts ascent and safely restores the lower endpoint"));Check(Anchor->LinkInstalled(),TEXT("Root releases the reservation without removing the installed ladder"));Place(A,Anchor->Lower+FVector(0,-100,92.15));Place(B,Anchor->Lower+FVector(0,100,92.15));Place(Deployer,Anchor->Upper+FVector(0,0,92.15));Task(6);return;}
    if(Phase==6)
    {
        Check(OneWinner()&&Objects->Carried(Deployer)==Ladder&&!Anchor->LinkInstalled(),TEXT("Only deployer retrieves an unused ladder into the same world-carry binding"));Check(Objects->ForceDrop(Deployer)&&!Ladder->InstalledAt.IsValid()&&FVector::Dist2D(Ladder->LastClear,Anchor->Lower)<2500,TEXT("Upper-end retrieval safely drops the same Ladder onto nearby ground"));
        // Explicit fixture staging: adopt the existing world Rope and establish
        // one legal accessory reference; tool-binding RPC was proven separately.
        Check(M->Inventory->Adopt(A,Rope),TEXT("Rope setup adopts an existing finite world item without creating one"));auto* Own=M->StateFor(A);Own->Inventory.Bindings[7]=Rope->Identity;Rope->BindingMask=1<<7;++Rope->Revision;if(auto* Entry=const_cast<FLDInventoryItem*>(Own->Inventory.Find(Rope->Identity))){Rope->Project(*Entry);}++Own->Inventory.Revision;M->Inventory->Changed(A);
        Place(A,Anchor->Lower+FVector(0,0,92.15));Place(B,Anchor->Lower+FVector(0,300,92.15));Task(7);return;
    }
    if(Phase==7)
    {Check(RA->GetIntegerField(TEXT("receipt_result"))==1&&RA->GetBoolField(TEXT("duplicate_same"))&&Anchor->Contents.Num()==1&&Anchor->Contents[0]==Rope&&Rope->Holder==Anchor&&!Rope->BindingMask&&!M->StateFor(A)->Inventory.Find(Rope->Identity),TEXT("Bound Rope attach moves one existing item into the anchor; duplicate replay commits once"));Check(!M->StateFor(A)->Inventory.Bindings[7].IsValid(),TEXT("Attachment clears the carried accessory reference without consuming the Rope"));M->Combat->ApplyStatus(Cast<ALDCharacter>(A->GetPawn()),LDTactical::EStatus::Mute,LDTactical::Milliseconds(Now)+6000,FGuid::NewGuid());Task(8);return;}
    if(Phase==8)
    {Check(RA->GetIntegerField(TEXT("receipt_result"))==1&&FVector::Dist(A->GetPawn()->GetActorLocation(),Anchor->Upper+FVector(0,0,92.15))<5,TEXT("Mute permits use of a placed rope as a world link without a portable cast"));Check(!RA->GetBoolField(TEXT("active"))&&!Objects->Traversing(A),TEXT("Owner traversal pose stream ends at an authoritative walking landing"));Interrupted=false;Phase=91;Native(ELDObjectVerb::Use,A);return;}
#endif
}
void ALDB91TraversalProbe::Save(bool Complete)
{
    FString Output;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Output);if(Output.IsEmpty()){return;}auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("complete"),Complete);R->SetNumberField(TEXT("failures"),Failures);R->SetNumberField(TEXT("phase"),Phase);R->SetArrayField(TEXT("checks"),Checks);R->SetArrayField(TEXT("geometry"),Geometry);R->SetStringField(TEXT("contract"),Objects?Objects->Contract:TEXT(""));R->SetStringField(TEXT("rope_identity"),Rope?Rope->Identity.ToString():TEXT(""));FString Why;R->SetBoolField(TEXT("conserved"),Objects&&Objects->Conserved(Why));int Items=0,Ladders=0;for(TActorIterator<ALDItem> I(GetWorld());I;++I){++Items;}if(Objects){for(auto O:Objects->Objects){Ladders+=O.IsValid()&&O->Kind==ELDObjectKind::Ladder;}}R->SetNumberField(TEXT("finite_items"),Items);R->SetNumberField(TEXT("ladders"),Ladders);R->SetStringField(TEXT("scope"),TEXT("Editor authority and two independent headless RPC owners.16 supported paired routes use native capsule sweeps; ladder/rope contention and Root/Stun interruption use actual-clock body movement. Rope binding is explicit existing-item fixture staging; separate portable suite covers binding RPC. No rendered, packaged-server, occupancy stress or whole-stage acceptance."));FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(Output/TEXT("Traversal.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
