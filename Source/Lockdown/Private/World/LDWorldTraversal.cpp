#include "World/LDWorldObjects.h"
#include "World/LDEnvironmentDirector.h"
#include "World/LDB91TraversalContract.h"
#include "Combat/LDCombatDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDTradeDirector.h"
#include "Character/LDCharacter.h"
#include "Navigation/LDNavigationComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

bool ALDWorldObjects::InitializeTraversal(FString& Error)
{
    FString Json;if(!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/V09/traversal_b91.json")))){Error=TEXT("Missing traversal contract");return false;}
    Json=Json.Replace(TEXT("\r\n"),TEXT("\n"));FTCHARToUTF8 Bytes(*Json);FSHA256Signature Hash;SHA256(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length(),Hash.Signature);
    if(Hash.ToString().ToLower()!=LD_B91_TRAVERSAL_SHA){Error=TEXT("Stale traversal contract");return false;}
    TSharedPtr<FJsonObject> Root;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)){return false;}
    auto Point=[](const TSharedPtr<FJsonObject>& R,const TCHAR* Key){const auto& P=R->GetArrayField(Key);return FVector(P[0]->AsNumber(),P[1]->AsNumber(),P[2]->AsNumber());};
    for(const auto& Value:Root->GetArrayField(TEXT("ladders")))
    {
        const auto R=Value->AsObject();auto* O=GetWorld()->SpawnActor<ALDWorldObject>();O->Kind=ELDObjectKind::Ladder;O->SourceId=FName(*R->GetStringField(TEXT("id")));O->SourcePosition=Point(R,TEXT("position"));FVector Surface;
        if(!FitSource(O,O->SourcePosition,Surface)){Error=TEXT("No legal ladder pad within 10m: ")+O->SourceId.ToString();O->Destroy();return false;}
        O->LastClear=Surface;O->Apply(Surface+FVector(0,0,18),0,1);Objects.Add(O);
    }
    for(const auto& Value:Root->GetArrayField(TEXT("anchors")))
    {
        const auto R=Value->AsObject();auto* O=GetWorld()->SpawnActor<ALDWorldObject>();O->Kind=ELDObjectKind::Anchor;O->SourceId=FName(*R->GetStringField(TEXT("id")));O->SourcePosition=Point(R,TEXT("source_lower"));O->Lower=Point(R,TEXT("lower"));O->Upper=Point(R,TEXT("upper"));
        FVector Lower,Upper;if(!SurfaceAt(O->Lower,Lower)||!SurfaceAt(O->Upper,Upper)||FMath::Abs(Lower.Z-O->Lower.Z)>20||FMath::Abs(Upper.Z-O->Upper.Z)>20||Upper.Z-Lower.Z>500||Upper.Z<=Lower.Z)
        {Error=TEXT("Approved link has no supported fitted endpoints: ")+O->SourceId.ToString();O->Destroy();return false;}
        O->Lower=Lower;O->Upper=Upper;O->LastClear=Lower;O->Apply(Lower+FVector(0,0,18),0,1);Objects.Add(O);
    }
    return true;
}
bool ALDWorldObjects::Traversing(const ALDB91Controller* PC) const
{
    for(const auto& Pair:Holds){if(Pair.Key.Get()==PC&&Pair.Value.Request.Verb==ELDObjectVerb::Use){return true;}}
    for(const auto& Pair:Returns){if(Pair.Key.Get()==PC){return true;}}return false;
}
bool ALDWorldObjects::EndpointClear(ALDB91Controller* PC,FVector Feet) const
{
    const auto* Body=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;if(!Body){return false;}const auto* Capsule=Body->GetCapsuleComponent();
    const double Half=Capsule->GetScaledCapsuleHalfHeight(),Radius=Capsule->GetScaledCapsuleRadius();FVector Floor;
    if(!SurfaceAt(Feet,Floor)||FMath::Abs(Floor.Z-Feet.Z)>10){return false;}const FVector Centre=Floor+FVector(0,0,Half+2.15);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(B91LinkEndpoint),false,Body);
    if(GetWorld()->OverlapBlockingTestByChannel(Centre,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Radius,Half),Query)){return false;}
    for(TActorIterator<ALDCharacter> Other(GetWorld());Other;++Other)
    {if(*Other==Body||!Other->GetActorEnableCollision()){continue;}const auto* C=Other->GetCapsuleComponent();if(FVector::Dist2D(Centre,Other->GetActorLocation())<Radius+C->GetScaledCapsuleRadius()+2&&FMath::Abs(Centre.Z-Other->GetActorLocation().Z)<Half+C->GetScaledCapsuleHalfHeight()){return false;}}
    return true;
}
bool ALDWorldObjects::LinkEligible(ALDB91Controller* PC,ALDWorldObject* Object,const FLDObjectRequest& R,FString& Why) const
{
    auto Fail=[&](const TCHAR* Text){Why=Text;return false;};if(uint8(R.Verb)>14){return Fail(TEXT("Unknown link action"));}
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* Own=M->StateFor(PC);auto* Body=Cast<ALDCharacter>(PC->GetPawn());const auto* State=M->Combat->StateFor(Body);const double Now=GetWorld()->GetTimeSeconds();
    const auto* Active=Holds.Find(PC);const bool Using=Active&&Active->Request.Verb==ELDObjectVerb::Use&&Active->Request.Sequence==R.Sequence;
    if(Returns.Contains(PC)||Traversing(PC)&&!Using){return Fail(TEXT("Finish traversal before another action"));}
    auto* Anchor=R.Verb==ELDObjectVerb::Deploy?Resolve(PC,R.Anchor):Object;
    if(!Object||Object->Revision!=R.ObjectRevision||!Anchor||Anchor->Kind!=ELDObjectKind::Anchor||(R.Verb==ELDObjectVerb::Deploy&&Anchor->Revision!=R.AnchorRevision)){return Fail(TEXT("Current observed approved anchor required"));}
    if(!Using)
    {const FVector End=Anchor->EndFor(Body->GetActorLocation());if(FVector::Dist2D(Body->GetActorLocation(),End)>120||FMath::Abs(Body->GetActorLocation().Z-End.Z-90)>100||!Visible(PC,Anchor)){return Fail(TEXT("Approved endpoint must be visible and within 120cm on this floor"));}}
    if(R.Verb==ELDObjectVerb::Use)
    {
        if(!Anchor->LinkInstalled()||Carried(PC)||!LDTactical::Allows(State->Tactical,LDTactical::EAction::Move,LDTactical::Milliseconds(Now))){return Fail(TEXT("A free body and installed link are required; control interrupts traversal"));}
        return true;
    }
    if(Now-FMath::Max3(State->LastDamage,State->LastDealt,State->QuietSince)<6){return Fail(TEXT("Six combat-quiet seconds required"));}
    if(R.Verb==ELDObjectVerb::Deploy)
    {if(Anchor->LinkInstalled()||Object->Kind!=ELDObjectKind::Ladder||Carried(PC)!=Object||Object->InstalledAt.IsValid()){return Fail(TEXT("Carry one ladder to a vacant approved anchor"));}return true;}
    if(Carried(PC)){return Fail(TEXT("Put down cargo before using a rope or retrieving a link"));}
    if(R.Verb==ELDObjectVerb::Attach)
    {
        auto* Item=M->Inventory->Owned(PC,R.Item);
        if(Anchor->LinkInstalled()||Own->Inventory.Revision!=R.InventoryRevision||!Item||Item->Kind!=ELDItemKind::Rope||Item->Quality!=0||Item->Revision!=R.ItemRevision||Item->bReserved||R.Accessory>=4||Own->Inventory.Bindings[7+R.Accessory]!=Item->Identity||!(Item->BindingMask&(1<<(7+R.Accessory)))||!LDTactical::Allows(State->Tactical,LDTactical::EAction::Item,LDTactical::Milliseconds(Now))){return Fail(TEXT("Vacant anchor and exact bound Standard Rope are required"));}
        return true;
    }
    if(R.Verb==ELDObjectVerb::Retrieve)
    {
        if(!Anchor->LinkInstalled()||Anchor->Deployer!=M->ContenderId(PC)){return Fail(TEXT("Only the deployer can retrieve an unused installed link"));}
        if(!Anchor->Contents.IsEmpty()){auto* Item=Anchor->Contents[0].Get();if(!Item||!M->Inventory->HasSpace(PC,Item->Kind,&Why)){return false;}}
        else{const int Capacity=Own->Build.Attributes.Num()==6?60+2*(Own->Build.Attributes[0]-10):0;if(!Anchor->LinkedLadder.IsValid()||Anchor->LinkedLadder->Weight()>Capacity){return Fail(TEXT("Ladder exceeds natural carrying capacity"));}}
        return true;
    }
    return Fail(TEXT("Unknown link action"));
}
bool ALDWorldObjects::BeginLink(ALDB91Controller* PC,ALDWorldObject* Object,FHold& H,FString& Why)
{
    auto* Anchor=H.Request.Verb==ELDObjectVerb::Deploy?Resolve(PC,H.Request.Anchor):Object;H.Link=Anchor;
    if(H.Request.Verb!=ELDObjectVerb::Use){return true;}
    auto* Body=H.Body.Get();const double Half=Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.15;const FVector Start=Anchor->EndFor(H.Start);const FVector End=Start.Equals(Anchor->Lower)?Anchor->Upper:Anchor->Lower;
    if(!EndpointClear(PC,Start)||!EndpointClear(PC,End)){Why=TEXT("Traversal endpoint occupied or unsupported");return false;}
    H.Destination=End+FVector(0,0,Half);H.Via=FVector(Anchor->Lower.X,Anchor->Lower.Y,Anchor->Upper.Z+Half);
    PC->Navigation->ResetLife();Body->GetCharacterMovement()->StopMovementImmediately();Body->GetCharacterMovement()->DisableMovement();
    PC->ClientTraversalState(H.Request.Match,H.Request.Life,H.Request.Sequence,true,H.Start);return true;
}
bool ALDWorldObjects::MoveTraversal(ALDB91Controller* PC,const FHold& H,double Now,FString& Why)
{
    auto* Body=H.Body.Get();const double Half=Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.15;
    if(!EndpointClear(PC,H.Destination-FVector(0,0,Half))){Why=TEXT("Traversal stopped; destination occupied");return false;}
    const double First=FVector::Dist(H.Start,H.Via),Second=FVector::Dist(H.Via,H.Destination),Distance=(First+Second)*FMath::Clamp((Now-H.Started)/(H.Ends-H.Started),0.,1.);
    const FVector Want=Distance<=First?FMath::Lerp(H.Start,H.Via,First>0?Distance/First:1.):FMath::Lerp(H.Via,H.Destination,Second>0?(Distance-First)/Second:1.);
    FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(B91LinkSweep),false,Body);const auto* C=Body->GetCapsuleComponent();
    if(GetWorld()->SweepSingleByChannel(Hit,Body->GetActorLocation(),Want,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(C->GetScaledCapsuleRadius(),C->GetScaledCapsuleHalfHeight()),Query)){Why=TEXT("Traversal stopped by collision; returning to a supported endpoint");return false;}
    // Pawns deliberately ignore Pawn-channel collision; test their swept
    // vertical segments separately. The link never shoves another body.
    for(TActorIterator<ALDCharacter> Other(GetWorld());Other;++Other)
    {if(*Other==Body||!Other->GetActorEnableCollision()){continue;}const auto* OC=Other->GetCapsuleComponent();const FVector P=Other->GetActorLocation();FVector A=Body->GetActorLocation(),B=Want;A.Z=B.Z=P.Z;
     if(FMath::PointDistToSegment(P,A,B)<C->GetScaledCapsuleRadius()+OC->GetScaledCapsuleRadius()+2&&P.Z+OC->GetScaledCapsuleHalfHeight()>FMath::Min(Body->GetActorLocation().Z,Want.Z)-C->GetScaledCapsuleHalfHeight()&&P.Z-OC->GetScaledCapsuleHalfHeight()<FMath::Max(Body->GetActorLocation().Z,Want.Z)+C->GetScaledCapsuleHalfHeight()){Why=TEXT("Traversal stopped by body occupancy");return false;}}
    Body->SetActorLocation(Want);PC->ClientTraversalPose(H.Request.Match,H.Request.Life,H.Request.Sequence,Want);return true;
}
bool ALDWorldObjects::RestoreTraversal(ALDB91Controller* PC,const FHold& H,bool Success)
{
    auto* Body=H.Body.Get();if(!PC||!Body||PC->GetPawn()!=Body||PC->OwnerState.Match!=H.Request.Match||PC->OwnerState.Life!=H.Request.Life){return true;}
    const double Half=Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.15;const FVector Start=H.Start-FVector(0,0,Half),End=H.Destination-FVector(0,0,Half);FVector Target=Success?End:Start;
    bool Clear=EndpointClear(PC,Target);
    if(!Clear){for(const FVector Base:{Start,End}){for(int Ring=0;Ring<=6&&!Clear;++Ring){for(int Y=-Ring;Y<=Ring&&!Clear;++Y){for(int X=-Ring;X<=Ring&&!Clear;++X){if(FMath::Max(FMath::Abs(X),FMath::Abs(Y))!=Ring){continue;}FVector Floor;if(SurfaceAt(Base+FVector(X*40,Y*40,0),Floor)&&FMath::Abs(Floor.Z-Base.Z)<20&&EndpointClear(PC,Floor)){Target=Floor;Clear=true;}}}}if(Clear){break;}}}
    if(!Clear){return false;} // No telefrag or falling-body fallback. Retry a safe
    // return next tick, without retaining the anchor's one-user reservation.
    Body->SetActorLocation(Target+FVector(0,0,Half));Body->GetCharacterMovement()->StopMovementImmediately();Body->GetCharacterMovement()->SetMovementMode(MOVE_Walking);PC->Navigation->ResetLife();
    PC->ClientTraversalState(H.Request.Match,H.Request.Life,H.Request.Sequence,false,Body->GetActorLocation());return true;
}
void ALDWorldObjects::EndTraversal(ALDB91Controller* PC,const FHold& H,bool Success)
{if(!RestoreTraversal(PC,H,Success)){Returns.Add(PC,H);}}
bool ALDWorldObjects::CommitLink(ALDB91Controller* PC,ALDWorldObject* Object,const FHold& H,FString& Why)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* Own=M->StateFor(PC);auto* Anchor=H.Link.Get();const auto& R=H.Request;if(!Anchor){return false;}
    if(R.Verb==ELDObjectVerb::Use){return EndpointClear(PC,H.Destination-FVector(0,0,H.Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.15));}
    if(R.Verb==ELDObjectVerb::Attach)
    {
        auto* Item=M->Inventory->Owned(PC,R.Item);if(M->Trade){M->Trade->CancelFor(PC);}for(auto& Id:Own->Inventory.Bindings){if(Id==Item->Identity){Id.Invalidate();}}
        Item->BindingMask=0;if(auto* Entry=const_cast<FLDInventoryItem*>(Own->Inventory.Find(Item->Identity))){Entry->BindingMask=0;}
        M->Inventory->Remove(PC,Item,false);Item->Holder=Anchor;Item->State=1;++Item->Revision;Anchor->Contents.Add(Item);Anchor->Deployer=M->ContenderId(PC);Anchor->Apply(Anchor->GetActorLocation(),Anchor->Flags(),Anchor->Revision+1);return true;
    }
    if(R.Verb==ELDObjectVerb::Deploy)
    {
        Object->Carrier.Reset();Object->bCarriedWorld=false;Cargo.Remove(PC);PC->ClientCargoState(R.Match,R.Life,false,FGuid());Object->InstalledAt=Anchor;Anchor->LinkedLadder=Object;Anchor->Deployer=M->ContenderId(PC);
        Object->Apply(Anchor->Lower+FVector(0,0,200),Object->Flags(),Object->Revision+1);Anchor->Apply(Anchor->GetActorLocation(),Anchor->Flags(),Anchor->Revision+1);return true;
    }
    if(R.Verb==ELDObjectVerb::Retrieve)
    {
        if(!Anchor->Contents.IsEmpty())
        {auto* Item=Anchor->Contents[0].Get();Item->Holder.Reset();Item->State=0;if(!M->Inventory->Adopt(PC,Item)){Item->Holder=Anchor;Item->State=1;Why=TEXT("Rope recovery unavailable; installed identity retained");return false;}Anchor->Contents.Reset();}
        else
        {auto* Ladder=Anchor->LinkedLadder.Get();Ladder->InstalledAt.Reset();Ladder->Carrier=PC;Ladder->bCarriedWorld=true;FCargo C;C.Object=Ladder;C.Life=R.Life;C.Damage=H.Damage;Cargo.Add(PC,C);
         auto& Id=Clients.FindOrAdd(PC).Handles.FindOrAdd(Ladder);if(!Id.IsValid()){Id=FGuid::NewGuid();}PC->ClientCargoState(R.Match,R.Life,true,Id);Ladder->Apply(PC->GetPawn()->GetActorLocation()+FVector(0,0,40),Ladder->Flags(),Ladder->Revision+1);Anchor->LinkedLadder.Reset();}
        Anchor->Deployer.Invalidate();Anchor->Apply(Anchor->GetActorLocation(),Anchor->Flags(),Anchor->Revision+1);return true;
    }
    return false;
}
