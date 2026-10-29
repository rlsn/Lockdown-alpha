#include "World/LDEnvironmentDirector.h"
#include "Framework/LDCommandLiveness.h"
#include "World/LDB91Forest.h"
#include "World/LDB91Contract.h"
#include "World/LDPortableTools.h"
#include "Development/LDV08Loadout.h"
#include "Region/LDMapDefinition.h"
#include "Combat/LDCombatDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Misc/SecureHash.h"
#include "Character/LDCharacter.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Camera/CameraComponent.h"
#include "Navigation/LDNavigationComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

namespace
{
FString Hash(const FString& Text)
{FTCHARToUTF8 Bytes(*Text);FSHA256Signature H;SHA256(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length(),H.Signature);return H.ToString().ToLower();}
FVector Point(const TSharedPtr<FJsonObject>& O,const TCHAR* Key)
{const auto& A=O->GetArrayField(Key);return FVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber());}
UStaticMeshComponent* Box(UWorld* W,FName Id,FVector P,FVector Size,bool Floor)
{
    auto* A=W->SpawnActor<AStaticMeshActor>(P,FRotator::ZeroRotator);A->Tags.Add(Id);
    auto* C=A->GetStaticMeshComponent();C->SetMobility(EComponentMobility::Movable);
    C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    C->SetWorldScale3D(Size/100.);C->SetCollisionProfileName(TEXT("BlockAll"));
    C->SetCollisionResponseToChannel(ECC_GameTraceChannel1,Floor?ECR_Block:ECR_Ignore);
    C->SetCollisionResponseToChannel(ECC_GameTraceChannel2,ECR_Block);C->SetCanEverAffectNavigation(true);if(Floor){C->ComponentTags.Add(TEXT("B91PermanentFloor"));}return C;
}
uint64 Key(uint32 Life,uint32 Sequence){return (uint64(Life)<<32)|Sequence;}
}
ALDEnvironmentDoor::ALDEnvironmentDoor()
{
    bReplicates=false;Proxy=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorProxy"));SetRootComponent(Proxy);
    Proxy->SetMobility(EComponentMobility::Movable);Proxy->SetCanEverAffectNavigation(true);
    Proxy->SetCollisionProfileName(TEXT("BlockAll"));Proxy->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Ignore);
    Proxy->SetCollisionResponseToChannel(ECC_GameTraceChannel2,ECR_Block);
}
void ALDEnvironmentDoor::Apply(uint8 Flags,uint32 NewRevision)
{
    if(NewRevision<Revision||(NewRevision==Revision&&Proxy->GetStaticMesh())){return;}Open=Flags&1;Locked=Flags&2;Broken=Flags&4;
    Proxy->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    // The parked open panel lies outside the 3m opening, within the existing wall wing.
    // Open panels are recessed into the solid jamb, outside walkable space.
    Proxy->SetWorldTransform(FTransform(FQuat::Identity,GetActorLocation(),FVector(3,.4,2.6)));
    Proxy->SetVisibility(!Open); // Open panel is fully recessed into the structural jamb.
    Proxy->SetCollisionEnabled(Open?ECollisionEnabled::NoCollision:ECollisionEnabled::QueryAndPhysics);
    Revision=CollisionRevision=SightRevision=NavigationRevision=NewRevision;
    ++ProxyUpdates;
    FNavigationSystem::UpdateComponentData(*Proxy);
}
ALDEnvironmentDirector::ALDEnvironmentDirector()
{bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
ALDEnvironmentDirector* ALDEnvironmentDirector::Get(UWorld* W)
{if(W){for(TActorIterator<ALDEnvironmentDirector> It(W);It;++It){return *It;}}return nullptr;}
bool ALDEnvironmentDirector::InFullMap(const UWorld* W)
{const auto* G=W?W->GetGameState<ALDGameState>():nullptr;return G&&G->bEnvironmentProfile;}
bool ALDEnvironmentDirector::LoadContract(FString& Json,FString& Error)
{
    if(!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/V09/environment_b91.json"))))
    {Error=TEXT("Missing compiled B91 contract");return false;}
    Json=Json.Replace(TEXT("\r\n"),TEXT("\n"));
    if(Hash(Json)!=LD_B91_ENV_SHA){Error=TEXT("Stale/unknown B91 contract; run SyncB91Data.py");return false;}return true;
}
bool ALDEnvironmentDirector::Initialize(FString& Error)
{
    if(Ready){return true;}FString Json;if(!LoadContract(Json,Error)){return false;}
    Contract=LD_B91_ENV_SHA;const auto* D=ULDMapDefinition::Get(GetWorld());if(!D||!D->Validate(Error)){return false;}
    TSharedPtr<FJsonObject> Root;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)){return false;}
    // Replace only selected open-shell walls in this explicit runtime profile.
    // B90 saved assets and its standalone proof remain unchanged.
    for(TActorIterator<AStaticMeshActor> It(GetWorld());It;++It)
    {if(It->Tags.ContainsByPredicate([](FName T){return T.ToString().Contains(TEXT("_wall_"));}))
     {auto* C=It->GetStaticMeshComponent();C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetVisibility(false);FNavigationSystem::UpdateComponentData(*C);}
     else if(It->Tags.ContainsByPredicate([](FName T){return T.ToString().StartsWith(TEXT("building_"));})){CameraWalls.Add(It->GetStaticMeshComponent());}}
    TSet<FName> Roofs;for(const auto& V:Root->GetArrayField(TEXT("roof_buildings"))){Roofs.Add(FName(*V->AsString()));}
    for(const auto& V:Root->GetArrayField(TEXT("buildings")))
    {
        const auto B=V->AsObject();const FName Id(*B->GetStringField(TEXT("id")));const FVector P=Point(B,TEXT("position"));
        int32 Part=0;auto Add=[&](FVector Offset,FVector Size,bool Floor=false)
        {auto* C=Box(GetWorld(),FName(*(Id.ToString()+FString::Printf(TEXT("_b91_%02d"),Part++))),P+Offset,Size,Floor);if(!Floor){CameraWalls.Add(C);}return C;};
        for(int32 Side:{-1,1})
        {
            // Two explicit small windows: 6m wide, 1.3m tall, sill at 1m.
            Add(FVector(Side*680,-600,300),FVector(40,600,600));
            if(Side==1&&Roofs.Contains(Id))
            {
                // Approved optional-link portal over the north landing. Ground
                // wall stays intact; the 3m upper opening clears a full capsule.
                Add(FVector(680,600,200),FVector(40,600,400));
                Add(FVector(680,430,500),FVector(40,260,200));
                Add(FVector(680,880,500),FVector(40,40,200));
            }
            else{Add(FVector(Side*680,600,300),FVector(40,600,600));}
            Add(FVector(Side*680,0,50),FVector(40,600,100));Add(FVector(Side*680,0,415),FVector(40,600,370));
            for(int32 Wing:{-1,1}){Add(FVector(Wing*425,Side*880,300),FVector(550,40,600));}
            Add(FVector(0,Side*880,430),FVector(300,40,340));
        }
        if(Roofs.Contains(Id))
        {
            // Twenty 20cm risers / 62cm treads; 3m width. Center hall remains clear.
            for(int32 I=0;I<20;++I)
            {auto* Step=Add(FVector(430,-620+(I+.5)*62,(I+1)*10),FVector(300,62,(I+1)*20),true);Step->SetCanEverAffectNavigation(false);}
            // A continuous authored nav/collision underlay avoids 40cm Recast
            // voxels aliasing the 20cm physical risers. Native feet still step
            // on the real stair boxes, at most one riser above this underlay.
            const double Angle=FMath::Atan2(400.,1240.);const double Rise=10/FMath::Cos(Angle);
            auto* Ramp=Add(FVector(430,0,200-Rise),FVector(300,FMath::Sqrt(1240.*1240+400.*400),20),true);
            Ramp->GetOwner()->SetActorRotation(FQuat(FVector::ForwardVector,Angle));Ramp->SetVisibility(false);
            for(const auto& Pair:TArray<TPair<FVector,FVector>>{
                {FVector(-190,0,390),FVector(940,1720,20)}, {FVector(620,0,390),FVector(80,1720,20)},
                {FVector(430,-740,390),FVector(300,240,20)}, {FVector(430,740,390),FVector(300,240,20)}})
            {Cutaways.Add({Add(Pair.Key,Pair.Value,true),P});}
            StairStarts.Add(P+FVector(430,-750,0));RoofGoals.Add(P+FVector(0,740,400));
        }
    }
    // These floors never move. World-space character corrections must not refer
    // to a nonreplicated dynamic base on an independently reconstructed client.
    for(TActorIterator<AStaticMeshActor> I(GetWorld());I;++I){auto* C=I->GetStaticMeshComponent();if(C->ComponentTags.Contains(TEXT("B91PermanentFloor"))){C->SetMobility(EComponentMobility::Static);}}
    for(const auto& V:Root->GetArrayField(TEXT("doors")))
    {
        const auto O=V->AsObject();auto* DoorActor=GetWorld()->SpawnActor<ALDEnvironmentDoor>(Point(O,TEXT("position"))+FVector(0,0,130),FRotator::ZeroRotator);
        DoorActor->Id=FName(*O->GetStringField(TEXT("id")));DoorActor->Building=FName(*O->GetStringField(TEXT("building")));
        DoorActor->Resident=FName(*O->GetStringField(TEXT("resident")));DoorActor->Rear=O->GetBoolField(TEXT("rear"));
        DoorActor->Tags.Add(DoorActor->Id);DoorActor->Apply(0,1);Doors.Add(DoorActor);
    }
    Ready=Doors.Num()==72&&RoofGoals.Num()==16;if(Ready&&GetNetMode()==NM_Client){Ready=ALDB91Forest::Ensure(GetWorld(),Error);}return Ready;
}
ALDEnvironmentDoor* ALDEnvironmentDirector::Door(FName Id) const
{for(const auto& D:Doors){if(D.IsValid()&&D->Id==Id){return D.Get();}}return nullptr;}
bool ALDEnvironmentDirector::Ray(FVector From,FVector To,const AActor* Endpoint,double Radius,bool SurfaceCue) const
{
    if(!ULDMapDefinition::InBounds(From)||!ULDMapDefinition::InBounds(To)||FVector::DistSquared2D(From,To)>Radius*Radius){return false;}
    if(const auto* Map=ULDMapDefinition::Get(GetWorld());Map&&Map->UpwardLedge(From,To)){return false;}
    if(const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M&&M->Combat&&M->Combat->EnvironmentOccludes(From,To,SurfaceCue)){return false;}
    FCollisionQueryParams Q(SCENE_QUERY_STAT(B91Sight),false);if(Endpoint){Q.AddIgnoredActor(Endpoint);}
    return !GetWorld()->LineTraceTestByChannel(From+FVector(0,0,60),To+FVector(0,0,60),ECC_GameTraceChannel2,Q);
}
bool ALDEnvironmentDirector::Visible(ALDPlayerController* PC,AActor* A) const
{
    const auto* B=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;if(!B||!IsValid(A)||!PC->OwnerState.bActive){return false;}
    const FVector Target=Cast<ALDEnvironmentDoor>(A)?A->GetActorLocation()-FVector(0,0,40):A->GetActorLocation();
    const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();
    const double Radius=SightFor(PC);
    if(!Ray(B->GetActorLocation(),Target,A->IsA<ALDEnvironmentDoor>()?A:nullptr,Radius)){return false;}
    if(const auto* Other=Cast<ALDCharacter>(A);Other&&M&&M->Combat&&M->Combat->Concealed(Other)&&Other!=B)
    {return (M->Vision&&M->Vision->Detects(PC,Other->GetActorLocation()))||(!M->Combat->Concealed(Other,false)&&FVector::DistSquared(B->GetActorLocation(),Other->GetActorLocation())<=200.*200);}return true;
}
float ALDEnvironmentDirector::SightFor(ALDPlayerController* PC) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* Body=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;const auto* S=Body&&M&&M->Combat?M->Combat->StateFor(Body):nullptr;
    if(S&&S->Tactical.Has(LDTactical::EStatus::Blind,LDTactical::Milliseconds(GetWorld()->GetTimeSeconds()))){return 200;}
    const auto* O=PC&&M?M->StateFor(PC):nullptr;return 1800+FMath::Clamp(O?LDV08::TraitValue(O->Build,TEXT("sight"),O->Build.Gear.SightBonus):0,0,50);
}
AActor* ALDEnvironmentDirector::Resolve(ALDPlayerController* PC,FGuid Handle) const
{
    const auto* O=Observers.Find(PC);if(!O||!O->Visible.Contains(Handle)){return nullptr;}
    for(const auto& P:O->Handles){if(P.Value==Handle&&P.Key.IsValid()&&Visible(PC,P.Key.Get())){return P.Key.Get();}}return nullptr;
}
bool ALDEnvironmentDirector::Occupied(const ALDEnvironmentDoor* D) const
{
    const FVector C=D->GetActorLocation();
    for(TActorIterator<ALDCharacter> It(GetWorld());It;++It)
    {const FVector P=It->GetActorLocation();const double R=It->GetCapsuleComponent()->GetScaledCapsuleRadius();
     if(FMath::Abs(P.X-C.X)<=150+R&&FMath::Abs(P.Y-C.Y)<=20+R&&FMath::Abs(P.Z-C.Z)<=130+It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()){return true;}}
    return false;
}
bool ALDEnvironmentDirector::Eligible(ALDB91Controller* PC,ALDEnvironmentDoor* D,ELDWorldVerb Verb,FString& Why) const
{
    auto Fail=[&](const TCHAR* Text){Why=Text;return false;};auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();
    const auto* O=M?M->StateFor(PC):nullptr;const auto* B=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;
    const auto* S=B&&M&&M->Combat?M->Combat->StateFor(B):nullptr;const double Now=GetWorld()->GetTimeSeconds();
    if(!Ready||!D||!O||!B||!S||!O->bActive||!M->IsPlaying()||S->Hp<=0||Now<O->ProtectedUntil||Now<S->ProtectionUntil
       ||S->Action||O->DevelopmentKind||!M->Combat->Allows(const_cast<ALDCharacter*>(B),LDTactical::EAction::Service))
    {return Fail(TEXT("World interaction unavailable"));}
    if(auto* Objects=ALDWorldObjects::Get(GetWorld());Objects&&(Objects->Carried(PC)||Objects->Traversing(PC))){return Fail(TEXT("Finish cargo or traversal before using a door"));}
    const FVector Target=D->GetActorLocation()-FVector(0,0,40);
    if(FVector::DistSquared2D(B->GetActorLocation(),Target)>120*120||FMath::Abs(B->GetActorLocation().Z-Target.Z)>100||!Visible(PC,D))
    {return Fail(TEXT("Visible doorway must be within 120cm on this floor"));}
    if(Verb==ELDWorldVerb::Lock){return Fail(TEXT("Rear locks belong to residents; public fronts cannot lock"));}
    if(Verb==ELDWorldVerb::Open&&(D->Open||D->Locked)){return Fail(TEXT("Door is already open or locked"));}
    if(Verb==ELDWorldVerb::Close&&(!D->Open||D->Broken||Occupied(D))){return Fail(TEXT("Door cannot close or doorway is occupied"));}
    if(Verb==ELDWorldVerb::Lockpick||Verb==ELDWorldVerb::Force)
    {return Fail(TEXT("Special lock work needs an authored item or prepared ability source; unavailable in this increment"));}
    if(uint8(Verb)<1||uint8(Verb)>5){return Fail(TEXT("Unknown world verb"));}return true;
}
void ALDEnvironmentDirector::Reply(ALDB91Controller* PC,const FLDWorldRequest& R,uint8 Result,const FString& Why,uint32 Revision)
{
    if(!PC){return;}auto& C=Clients.FindOrAdd(PC);auto& Receipt=C.Receipts.FindOrAdd(Key(R.Life,R.Sequence));
    if(!Receipt.Receipt.IsValid()){Receipt.Receipt=FGuid::NewGuid();Receipt.Sequence=R.Sequence;}
    Receipt.Result=Result;Receipt.Reason=Why;Receipt.Revision=Revision;PC->ClientWorldReceipt(Receipt);
    FLDB91Channel Progress;Progress.Match=R.Match;Progress.Life=R.Life;Progress.Sequence=R.Sequence;Progress.Domain=1;const auto* H=Holds.Find(PC);if(!Result&&H){Progress.Started=H->Started;Progress.Ends=H->Ends;Progress.Label=TEXT("Door action");}PC->ClientB91Channel(Progress,!Result&&H!=nullptr);
    if(Result){Journal.Add(Receipt);}while(C.Receipts.Num()>64)
    {uint64 Old=MAX_uint64;for(const auto& P:C.Receipts){Old=FMath::Min(Old,P.Key);}C.Receipts.Remove(Old);}
}
void ALDEnvironmentDirector::Begin(ALDB91Controller* PC,const FLDWorldRequest& R)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M||!M->IsEnvironmentProfile()||!PC){return;}
    M->Combat->FlushPastCommand();auto& C=Clients.FindOrAdd(PC);const auto* O=M->StateFor(PC);
    if(const auto* Old=C.Receipts.Find(Key(R.Life,R.Sequence));Old&&O&&R.Match==O->Match){PC->ClientWorldReceipt(*Old);return;}
    if(!O||O->Match!=R.Match||O->Life!=R.Life||R.Layout!=1||R.Contract!=Contract||!R.Sequence||R.Sequence<=C.Last||R.Sequence<=C.CancelledThrough)
    {Reply(PC,R,2,TEXT("Stale world request/profile/layout/life"));return;}
    const double Now=GetWorld()->GetTimeSeconds();C.Tokens=FMath::Min(8.,C.Tokens+FMath::Max(0.,Now-C.RateAt)*8);C.RateAt=Now;C.Last=R.Sequence;
    if(C.Tokens<1){Reply(PC,R,2,TEXT("World interaction busy"));return;}--C.Tokens;
    auto* D=Cast<ALDEnvironmentDoor>(Resolve(PC,R.Observed));FString Why;
    if(!D||D->Revision!=R.ObjectRevision||!Eligible(PC,D,R.Verb,Why)){Reply(PC,R,2,Why.IsEmpty()?TEXT("Door changed or not observed"):Why);return;}
    for(const auto& H:Holds){if(H.Value.Door==D&&H.Key!=PC){Reply(PC,R,2,TEXT("Door reserved; try again"));return;}}
    Cancel(PC,TEXT("Superseded held interaction"));FHold H;H.Request=R;H.Door=D;H.Start=PC->GetPawn()->GetActorLocation();
    if(auto* Objects=ALDWorldObjects::Get(GetWorld())){Objects->Cancel(PC,TEXT("Door interaction started"));}
    H.Started=H.Heartbeat=Now;H.Ends=Now+((R.Verb==ELDWorldVerb::Force||R.Verb==ELDWorldVerb::Lockpick)?3.:.3);
    H.DamageAt=M->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()))->LastDamage;Holds.Add(PC,H);
    Reply(PC,R,0,TEXT("Door action started; S stops. Movement, damage or lost sight cancels"));
}
void ALDEnvironmentDirector::Hold(ALDB91Controller* PC,uint32 Match,uint32 Life,uint32 Sequence,uint32 Nonce,bool Release)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M?M->StateFor(PC):nullptr;if(!O||O->Match!=Match||O->Life!=Life){return;}
    if(Release){auto& C=Clients.FindOrAdd(PC);C.CancelledThrough=FMath::Max(C.CancelledThrough,Sequence);}
    auto* H=Holds.Find(PC);if(!H||H->Request.Sequence!=Sequence){return;}
    if(Release){Cancel(PC,TEXT("Action stopped"));return;}
    if(Nonce>H->Nonce){H->Nonce=Nonce;H->Heartbeat=GetWorld()->GetTimeSeconds();}
}
void ALDEnvironmentDirector::Cancel(ALDB91Controller* PC,const FString& Why)
{if(const auto* H=Holds.Find(PC)){const auto R=H->Request;Holds.Remove(PC);Reply(PC,R,2,Why);}}
bool ALDEnvironmentDirector::ResidentLock(FName Id,FName Resident,bool Locked)
{
    auto* D=Door(Id);if(!HasAuthority()||GetNetMode()==NM_Client||!D||!D->Rear||D->Resident!=Resident||Resident.IsNone()||D->Open||D->Broken){return false;}
    D->Apply((D->State()&~2)|(Locked?2:0),++MutationRevision);return true;
}
void ALDEnvironmentDirector::Publish()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M){return;}++ViewRevision;
    // Listen/standalone renderers also use authorized proxies for other bodies.
    // Canonical authority meshes never become an alternate visibility channel.
    if(GetNetMode()!=NM_DedicatedServer)
    {const auto* Local=GetWorld()->GetFirstPlayerController();for(TActorIterator<ALDCharacter> B(GetWorld());B;++B)
     {if(!Local||B->GetController()!=Local||!Local->IsLocalController()){B->Body->SetVisibility(false,true);}}}
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
    {
        auto* PC=Cast<ALDB91Controller>(It->Get());const auto* O=PC?M->StateFor(PC):nullptr;if(!PC||!O){continue;}
        auto& Observer=Observers.FindOrAdd(PC);Observer.Visible.Reset();TArray<FLDEnvironmentViewEntry> Entries;
        auto Add=[&](AActor* A)
        {
            if(!Visible(PC,A)||A==PC->GetPawn()||Entries.Num()>=128){return;}
            auto& Id=Observer.Handles.FindOrAdd(A);if(!Id.IsValid()){Id=FGuid::NewGuid();}Observer.Visible.Add(Id);
            FLDEnvironmentViewEntry E;E.Handle=Id;E.Position=A->GetActorLocation();
            if(const auto* D=Cast<ALDEnvironmentDoor>(A)){E.PublicDoor=D->Id;E.Revision=D->Revision;E.State=D->State();}
            else if(const auto* B=Cast<ALDCharacter>(A)){E.Presentation=B->PresentationKey%4;E.State=M->Combat->CueFor(B);E.Facing=uint16(FRotator::ClampAxis(B->GetActorRotation().Yaw)/360*65535);E.Light=LDPortableTools::LightFor(Cast<ALDPlayerController>(B->GetController()));}
            Entries.Add(E);
        };
        for(auto D:Doors){if(D.IsValid()){Add(D.Get());}}
        for(TActorIterator<ALDCharacter> B(GetWorld());B;++B){Add(*B);}
        for(auto I=Observer.Handles.CreateIterator();I;++I){if(!I.Key().IsValid()){I.RemoveCurrent();}}
        PC->ClientEnvironmentView(O->Match,O->Life,ViewRevision,Entries);
    }
}
void ALDEnvironmentDirector::Tick(float Delta)
{
    Super::Tick(Delta);if(!Ready){return;}
    const APawn* LocalBody=GetWorld()->GetFirstPlayerController()?GetWorld()->GetFirstPlayerController()->GetPawn().Get():nullptr;
    if(GetNetMode()!=NM_DedicatedServer&&LocalBody)
    {const FVector P=LocalBody->GetActorLocation();for(const auto& C:Cutaways){if(C.Floor.IsValid())
     {const bool Inside=FMath::Abs(P.X-C.Centre.X)<1000&&FMath::Abs(P.Y-C.Centre.Y)<1200&&P.Z<C.Centre.Z+450;
      C.Floor->SetVisibility(!Inside);}}
     if(const auto* Body=Cast<ALDCharacter>(LocalBody))
     {const FVector From=P+FVector(0,0,70),To=Body->Camera->GetComponentLocation();
      auto Cut=[&](const UStaticMeshComponent* C){return FMath::LineBoxIntersection(C->Bounds.GetBox(),From,To,To-From);};
      for(auto C:CameraWalls){if(C.IsValid()){C->SetVisibility(!Cut(C.Get()));}}
      for(auto D:Doors){if(D.IsValid()){D->Proxy->SetVisibility(!D->Open&&!Cut(D->Proxy));}}
     }}
    if(GetNetMode()==NM_Client){return;}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M){return;}
    const double Now=GetWorld()->GetTimeSeconds();TArray<TWeakObjectPtr<ALDB91Controller>> Active;Holds.GetKeys(Active);
    for(const auto& Weak:Active)
    {
        auto* PC=Weak.Get();const auto H=Holds.FindRef(Weak);auto* D=H.Door.Get();const auto* O=PC?M->StateFor(PC):nullptr;
        const auto* Body=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;const auto* S=Body?M->Combat->StateFor(Body):nullptr;FString Why;
        if(!PC){Holds.Remove(Weak);continue;}
        if(!O||O->Life!=H.Request.Life||O->Match!=H.Request.Match||!D||D->Revision!=H.Request.ObjectRevision||!S||S->LastDamage!=H.DamageAt
           ||FVector::DistSquared(H.Start,Body->GetActorLocation())>25||!LDCommandLiveness::Fresh(Now+1.e-6>=H.Ends?FMath::Max(Now,H.Ends):Now,H.Heartbeat)||!Eligible(PC,D,H.Request.Verb,Why))
        {Cancel(PC,TEXT("Held interaction cancelled; state, movement, damage or input changed"));continue;}
        if(Now+1e-6<H.Ends){continue;}
        uint8 Flags=D->State();switch(H.Request.Verb)
        {case ELDWorldVerb::Open:Flags|=1;break;case ELDWorldVerb::Close:Flags&=~1;break;
         case ELDWorldVerb::Lockpick:Flags&=~2;break;case ELDWorldVerb::Force:Flags=(Flags&~2)|5;break;default:break;}
        D->Apply(Flags,++MutationRevision);Holds.Remove(Weak);Reply(PC,H.Request,1,TEXT("Door transaction committed"),D->Revision);
    }
    if(Now>=NextPublish){NextPublish=Now+.1;Publish();}
    for(auto I=Observers.CreateIterator();I;++I){if(!I.Key().IsValid()){I.RemoveCurrent();}}
    for(auto I=Clients.CreateIterator();I;++I){if(!I.Key().IsValid()){I.RemoveCurrent();}}
}
