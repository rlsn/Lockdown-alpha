#include "Tests/LDB91ObjectsProbe.h"
#include "World/LDEnvironmentDirector.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Inventory/LDInventoryDirector.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDMapDefinition.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ALDB91ObjectsProbe::ALDB91ObjectsProbe()
{PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;Started=FPlatformTime::Seconds();}
void ALDB91ObjectsProbe::Check(bool Good,const FString& Text)
{auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("case"),Text);R->SetBoolField(TEXT("passed"),Good);Checks.Add(MakeShared<FJsonValueObject>(R));Failures+=!Good;UE_LOG(LogTemp,Display,TEXT("LD_B91_OBJECTS %s passed=%d"),*Text,Good);}
void ALDB91ObjectsProbe::Place(ALDB91Controller* PC,FVector Point)
{PC->Navigation->Stop();auto* Body=Cast<ALDCharacter>(PC->GetPawn());Body->SetActorLocation(Point);Body->GetCharacterMovement()->StopMovementImmediately();PC->ClientSetLocation(Point,FRotator::ZeroRotator);}
void ALDB91ObjectsProbe::Task(uint8 Value,bool Both)
{Phase=Value;At=GetWorld()->GetTimeSeconds();Objects->Publish();if(Both){A->ClientB91ProbeTask(Value);B->ClientB91ProbeTask(Value);}else{Winner->ClientB91ProbeTask(Value);}Save(false);}
void ALDB91ObjectsProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    const double Now=GetWorld()->GetTimeSeconds();if(Phase==99){return;}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M){return;}
    if(Failures||FPlatformTime::Seconds()-Started>200){if(!Failures){Check(false,TEXT("Object network fixture timed out"));}Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,3);return;}
    auto Count=[](UWorld* W){int N=0;for(TActorIterator<ALDItem> I(W);I;++I){++N;}return N;};
    auto Near=[&](ALDWorldObject* O){const FVector Base=O->LastClear+FVector(0,0,92.15);Place(A,Base+FVector(-100,0,0));Place(B,Base+FVector(100,0,0));};
    if(!Phase)
    {
        for(FConstPlayerControllerIterator I=GetWorld()->GetPlayerControllerIterator();I;++I){if(auto* PC=Cast<ALDB91Controller>(I->Get());PC&&PC->GetPawn()){if(PC->EnvironmentProbeRole==1){A=PC;}if(PC->EnvironmentProbeRole==2){B=PC;}}}
        if(!A||!B||Now<7){return;}Objects=ALDWorldObjects::Get(GetWorld());if(!Objects||!Objects->Ready){return;}AddTickPrerequisiteActor(Objects);
        Check(A!=B&&!A->IsLocalController()&&!B->IsLocalController(),TEXT("Two independent remote object owners"));
        Check(Objects->Objects.Num()==204&&Count(GetWorld())==132,TEXT("Exact 204 objects and 132 finite physical endowments"));
        for(TActorIterator<ALDItem> I(GetWorld());I;++I){InitialIds.Add(I->Identity);}
        for(auto O:Objects->Objects)
        {FVector Surface=O->LastClear;const bool Valid=Objects->Placement(O.Get(),O->LastClear,Surface);auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("source_id"),O->SourceId.ToString());R->SetNumberField(TEXT("kind"),uint8(O->Kind));R->SetBoolField(TEXT("placement_valid"),Valid);
         auto Vector=[](FVector P){return TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)};};R->SetArrayField(TEXT("source_cm"),Vector(O->SourcePosition));R->SetArrayField(TEXT("fitted_cm"),Vector(O->LastClear));Manifest.Add(MakeShared<FJsonValueObject>(R));}
        Check(!Manifest.ContainsByPredicate([](const auto& R){return !R->AsObject()->GetBoolField(TEXT("placement_valid"));}),TEXT("All initial pads pass current physical placement and corridor rules"));
        auto* Crate=Objects->Find(TEXT("prop_crate_000"));auto* E=ALDEnvironmentDirector::Get(GetWorld());const auto* Map=ULDMapDefinition::Get(GetWorld());FVector Surface;
        int Denied=0;for(auto D:E->Doors){Denied+=!Objects->Placement(Crate,D->GetActorLocation()-FVector(0,0,130),Surface);}Check(Denied==72,TEXT("All72 authored door pads reject cargo placement"));
        Denied=0;for(auto P:E->StairStarts){Denied+=!Objects->Placement(Crate,P,Surface);}Check(Denied==16,TEXT("All16 permanent stair landings reject cargo placement"));
        Denied=0;for(const auto& P:Map->Spawns){Denied+=!Objects->Placement(Crate,P.Position,Surface);}Check(Denied==24,TEXT("All24 contender spawn pads reject cargo placement"));
        Denied=0;for(const auto& P:Map->Services){Denied+=!Objects->Placement(Crate,P.Position,Surface);}Check(Denied==36,TEXT("All36 service reach pads reject cargo placement"));
        const auto* Heart=Map->Find(TEXT("heart"));Check(Heart&&!Objects->Placement(Crate,Heart->Position,Surface),TEXT("Heart reach pad rejects cargo placement"));
        Denied=0;for(const auto& Route:Map->Routes){FVector P=(Route.Points[0]+Route.Points[1])*.5;P.Z=Map->Height(FVector2D(P));Denied+=!Objects->Placement(Crate,P,Surface);}Check(Denied==117,TEXT("All117 authored route corridors reject cargo placement"));
        Target=Objects->Find(TEXT("prop_water_flask_000"));if(!Target){Check(false,TEXT("Exact Riverport flask exists"));return;}
        for(auto* PC:{A,B}){auto* O=M->StateFor(PC);O->ProtectedUntil=0;M->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()))->ProtectionUntil=0;M->SyncOwner(PC);}
        Near(Target);Phase=90;At=Now;return;
    }
    auto Read=[](ALDB91Controller* PC,uint8 Value)->TSharedPtr<FJsonObject>
    {TSharedPtr<FJsonObject> R;const auto* Text=PC->EnvironmentProbeReports.Find(Value);if(Text){FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(*Text),R);}return R;};
    if(Phase==90){if(Now-At>1){Task(1);}return;}
    static FLDObjectRequest NativeRequest;static uint32 Beat=0;static double PriorHP=0;static FVector PushOrigin,PushBody;static int BagBefore=0;
    auto NativeCarry=[&](uint32 Seq)
    {Near(Target);Objects->Publish();auto* Own=M->StateFor(Winner);NativeRequest.Match=Own->Match;NativeRequest.Life=Own->Life;NativeRequest.Sequence=Seq;NativeRequest.Layout=1;NativeRequest.Contract=Objects->Contract;NativeRequest.ObjectRevision=Target->Revision;NativeRequest.InventoryRevision=Own->Inventory.Revision;NativeRequest.Verb=ELDObjectVerb::Carry;Objects->Begin(Winner,NativeRequest);At=Now;};
    if((Phase>=91&&Phase<=98)||Phase==100||Phase==101)
    {
        if(Phase==91||Phase==94||Phase==96||Phase==98||Phase==100||Phase==101){Objects->Hold(Winner,NativeRequest.Match,NativeRequest.Life,NativeRequest.Sequence,++Beat,false);}
        if(Phase==91&&Now-At>1.2)
        {Check(Objects->Carried(Winner)==Target,TEXT("Cargo can be carried again after placement"));auto* Other=Winner==A?B:A;auto* Body=Cast<ALDCharacter>(Winner->GetPawn());PriorHP=M->Combat->StateFor(Body)->Hp;Place(Other,Body->GetActorLocation()+FVector(0,80,0));FString Why;
         Check(M->Combat->RequestBody(Cast<ALDCharacter>(Other->GetPawn()),0,FVector(0,-1,0),Why),TEXT("Ordinary combat attack starts against cargo carrier"));Phase=92;At=Now;return;}
        if(Phase==92&&Now-At>.9)
        {Check(M->Combat->StateFor(Cast<ALDCharacter>(Winner->GetPawn()))->Hp<PriorHP&&!Objects->Carried(Winner),TEXT("Actual combat damage safely drops cargo before later actions"));Phase=93;At=Now;return;}
        if(Phase==93&&Now-At>6.2){NativeCarry(50001);Phase=94;return;}
        if(Phase==94&&Now-At>1.2)
        {Check(Objects->Carried(Winner)==Target,TEXT("Cargo re-enters the same world binding after quiet"));auto* Body=Cast<ALDCharacter>(Winner->GetPawn());M->Combat->ApplyStatus(Body,LDTactical::EStatus::Stun,LDTactical::Milliseconds(Now)+1000,FGuid::NewGuid());FString Why;
         const bool Action=M->Combat->RequestBody(Body,0,FVector(1,0,0),Why);Check(!Action&&!Objects->Carried(Winner)&&M->Combat->StateFor(Body)->Action==0,TEXT("Explicit attack drops cargo but Stun still rejects the attack"));Phase=95;At=Now;return;}
        if(Phase==95&&Now-At>6.2){NativeCarry(50002);Phase=96;return;}
        if(Phase==96&&Now-At>1.2)
        {Check(Objects->Carried(Winner)==Target,TEXT("Same cargo exists before contender end-life"));M->EndContenderLife(Winner,Now);Check(!Objects->Carried(Winner)&&!Target->Carrier.IsValid(),TEXT("Contender end-life safely releases carried world cargo"));Phase=97;At=Now;return;}
        if(Phase==97&&Now-At>.3)
        {Objects->Publish();bool Aligned=true;for(auto O:Objects->Objects){if(O.IsValid()&&O->Physical.IsValid()&&O->Physical->State==0){Aligned&=O->GetActorLocation().Equals(O->Physical->GetActorLocation(),.1);}}
         Check(Aligned,TEXT("Life drops move existing physical proxies to their actual ground locations"));FString Why;Check(Objects->Conserved(Why)&&Count(GetWorld())==InitialIds.Num(),TEXT("Damage, control and life changes preserve all 132 finite identities"));
         Winner=Winner==A?B:A;Place(Winner,Target->LastClear+FVector(3000,0,92.15));FVector Direction=FVector::ZeroVector,PushPad=Target->LastClear;
         // The ordinary forced-drop pad can be a legal cul-de-sac. Refit this
         // same instance to a genuinely clear native lane for the positive push
         // fixture, without weakening ordinary placement or creating a prop.
         for(int Ring=0;Ring<=20&&Direction.IsNearlyZero();++Ring){for(int Y=-Ring;Y<=Ring&&Direction.IsNearlyZero();++Y){for(int X=-Ring;X<=Ring&&Direction.IsNearlyZero();++X)
         {if(FMath::Max(FMath::Abs(X),FMath::Abs(Y))!=Ring){continue;}FVector Pad=Target->LastClear;if(!Objects->Placement(Target,Target->LastClear+FVector(X*100,Y*100,0),Pad)){continue;}
          for(FVector Candidate:{FVector(1,0,0),FVector(-1,0,0),FVector(0,1,0),FVector(0,-1,0)})
          {bool Clear=true;for(int N=1;N<=15;++N){FVector Surface=Pad;Clear&=Objects->Placement(Target,Pad+Candidate*(N*10),Surface);}if(Clear){Direction=Candidate;PushPad=Pad;break;}}}}}
         Check(!Direction.IsNearlyZero(),TEXT("Native push fixture has a legal 150cm swept lane"));if(Direction.IsNearlyZero()){return;}
         Target->LastClear=PushPad;Target->Apply(PushPad+FVector(0,0,113),Target->Flags(),Target->Revision+1);
         Place(Winner,Target->LastClear-Direction*110+FVector(0,0,92.15));Objects->Publish();PushOrigin=Target->GetActorLocation();PushBody=Winner->GetPawn()->GetActorLocation();
         const auto OldReport=Read(Winner,7);if(OldReport){FGuid::Parse(OldReport->GetStringField(TEXT("target_handle")),NativeRequest.Observed);}auto* Own=M->StateFor(Winner);NativeRequest.Match=Own->Match;NativeRequest.Life=Own->Life;NativeRequest.Sequence=50003;NativeRequest.ObjectRevision=Target->Revision;NativeRequest.InventoryRevision=Own->Inventory.Revision;NativeRequest.Verb=ELDObjectVerb::Push;Objects->Begin(Winner,NativeRequest);Phase=98;At=Now;return;}
        if(Phase==98&&Now-At>1.2)
        {Check(FMath::Abs(FVector::Dist2D(PushOrigin,Target->GetActorLocation())-150)<1&&Winner->GetPawn()->GetActorLocation().Equals(PushBody,.1),TEXT("One-second swept push moves cargo 150cm without displacing the body"));FString Why;Check(Objects->Conserved(Why)&&Count(GetWorld())==InitialIds.Num(),TEXT("Final storage and cargo source closure"));
         Target=Objects->Find(TEXT("prop_barrel_016"));Place(Winner,Target->LastClear+FVector(-110,0,92.15));Objects->Publish();const auto* Client=Objects->Clients.Find(Winner);const auto* Handle=Client?Client->Handles.Find(Target):nullptr;
         NativeRequest.Observed=Handle?*Handle:FGuid();auto* Own=M->StateFor(Winner);BagBefore=Own->Inventory.Items.Num();NativeRequest.Match=Own->Match;NativeRequest.Life=Own->Life;NativeRequest.Sequence=50004;NativeRequest.ObjectRevision=Target->Revision;NativeRequest.InventoryRevision=Own->Inventory.Revision;NativeRequest.Verb=ELDObjectVerb::Carry;Objects->Begin(Winner,NativeRequest);Phase=100;At=Now;return;}
        if(Phase==100&&Now-At>1.2)
        {Check(Target->Weight()==16&&Objects->Carried(Winner)==Target&&FMath::Abs(Cast<ALDCharacter>(Winner->GetPawn())->GetCharacterMovement()->GetMaxSpeed()-294)<.1,TEXT("Empty barrel carries at weight16 and 70 percent speed"));NativeRequest.Sequence=50005;NativeRequest.ObjectRevision=Target->Revision;NativeRequest.Verb=ELDObjectVerb::Place;NativeRequest.Point=Target->LastClear;Objects->Begin(Winner,NativeRequest);Phase=101;At=Now;return;}
        if(Phase==101&&Now-At>1.7)
        {Check(!Target->Carrier.IsValid()&&Target->Proxy->GetCollisionResponseToChannel(ECC_GameTraceChannel2)==ECR_Ignore&&FMath::Abs(Cast<ALDCharacter>(Winner->GetPawn())->GetCharacterMovement()->GetMaxSpeed()-420)<.1,TEXT("Placed barrel restores movement and remains non-occluding"));FString Why;Check(M->StateFor(Winner)->Inventory.Items.Num()==BagBefore&&Objects->Conserved(Why)&&Count(GetWorld())==InitialIds.Num(),TEXT("Barrel transport creates no bag instance or material"));Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,Failures?3:0);return;}
        return;
    }
    const bool Both=Phase!=4&&Phase!=8;const auto RA=Read(Both?A:Winner,Phase),RB=Both?Read(B,Phase):nullptr;if(!RA||(Both&&!RB)){return;}
    Check(RA->GetBoolField(TEXT("completed"))&&(!Both||RB->GetBoolField(TEXT("completed"))),FString::Printf(TEXT("Remote object phase %u completed"),Phase));
    auto Race=[&](){return int(RA->GetIntegerField(TEXT("receipt_result"))==1)+int(RB->GetIntegerField(TEXT("receipt_result"))==1)==1;};
    if(Phase==1)
    {
        Check(Race()&&Target->Physical.IsValid()&&Target->Physical->State==1&&Target->Physical->Holder.IsValid(),TEXT("Flask race has one physical owner and one winner"));
        Check(RA->GetBoolField(TEXT("duplicate_same"))&&RB->GetBoolField(TEXT("duplicate_same")),TEXT("Winning and losing duplicate sequences replay the same receipts"));
        Check(!RA->GetIntegerField(TEXT("container_count_before"))&&!RB->GetIntegerField(TEXT("container_count_before")),TEXT("Unopened container inventories are absent on both clients"));
        Check(RA->GetBoolField(TEXT("private_revision_absent"))&&RB->GetBoolField(TEXT("private_revision_absent")),TEXT("Observed empty ground reveals no held-item revision"));
        Target=Objects->Find(TEXT("prop_chest_002"));Near(Target);Task(2);return;
    }
    if(Phase==2)
    {Check(RA->GetIntegerField(TEXT("container_count"))==3&&RB->GetIntegerField(TEXT("container_count"))==3,TEXT("Legal Open quotes exactly two Fiber and one Ore"));Task(3);return;}
    if(Phase==3)
    {
        Check(Race()&&Target->Contents.Num()==2,TEXT("Exact-item Take race removes one stored instance"));Winner=RA->GetIntegerField(TEXT("receipt_result"))==1?A:B;Task(4,false);return;
    }
    if(Phase==4)
    {
        Check(RA->GetIntegerField(TEXT("receipt_result"))==1&&Target->Contents.Num()==3,TEXT("Store returns the same unbound physical item without minting"));
        auto* Crate=Objects->Find(TEXT("prop_crate_002"));StoredIds.Reset();
        // Diagnostic setup transfers existing cache instances; it creates no item.
        for(int N=0;N<2;++N){auto P=Target->Contents.Pop();P->Holder=Crate;++P->Revision;Crate->Contents.Add(P);StoredIds.Add(P->Identity);}
        Target->Apply(Target->GetActorLocation(),Target->Flags(),Target->Revision+1);Crate->Apply(Crate->GetActorLocation(),Crate->Flags(),Crate->Revision+1);
        Target=Crate;BeforeCount=Count(GetWorld());Near(Target);Task(5);return;
    }
    if(Phase==5)
    {
        int Dropped=0;for(TActorIterator<ALDItem> I(GetWorld());I;++I){Dropped+=StoredIds.Contains(I->Identity)&&I->State==0&&!I->Holder.IsValid();}
        Check(Race()&&Target->Broken&&Target->Contents.IsEmpty()&&Dropped==2&&Count(GetWorld())==BeforeCount,TEXT("Filled break race drops exactly its two existing instances once"));
        Check(Target->Revision==Target->CollisionRevision&&Target->Revision==Target->SightRevision&&Target->Revision==Target->NavigationRevision,TEXT("Broken cover uses one collision/sight/navigation revision"));
        Check(RA->GetBoolField(TEXT("broken_proxy_clear"))&&RB->GetBoolField(TEXT("broken_proxy_clear")),TEXT("Both clients remove observed broken cover collision"));
        Target=Objects->Find(TEXT("prop_barrel_002"));BeforeCount=Count(GetWorld());Near(Target);Task(6);return;
    }
    if(Phase==6)
    {
        Check(Race()&&Target->Broken&&Count(GetWorld())==BeforeCount,TEXT("Empty barrel break race creates no oil, material or item"));
        Target=Objects->Find(TEXT("prop_crate_012"));BeforeRevision=Target->Revision;Near(Target);Task(7);return;
    }
    if(Phase==7)
    {
        Winner=RA->GetIntegerField(TEXT("receipt_result"))==1?A:B;Check(Race()&&Target->Carrier==Winner&&Target->Revision==BeforeRevision+1,TEXT("Cargo race binds one existing world instance to one owner"));
        const auto* Body=Cast<ALDCharacter>(Winner->GetPawn());Check(FMath::Abs(Body->GetCharacterMovement()->GetMaxSpeed()-294)<.1,TEXT("Carried cargo limits native movement to 70 percent"));
        bool Blocked=true;for(auto V:{LDTactical::EAction::Attack,LDTactical::EAction::Skill,LDTactical::EAction::Item,LDTactical::EAction::Service}){Blocked&=!M->Combat->Allows(const_cast<ALDCharacter*>(Body),V);}
        Check(Blocked,TEXT("Cargo suppresses attacks, skills, items and services"));Task(8,false);return;
    }
    if(Phase==8)
    {
        Check(RA->GetIntegerField(TEXT("receipt_result"))==1&&RA->GetBoolField(TEXT("unsafe_place_retained_cargo"))&&!Target->Carrier.IsValid(),TEXT("Occupied placement retains cargo; later native walk and legal placement succeed"));
        Check(!Objects->Carried(A)&&!Objects->Carried(B)&&Count(GetWorld())==InitialIds.Num(),TEXT("Placed cargo restores hands and preserves all finite item identities"));
        FString Why;Check(Objects->Conserved(Why),TEXT("Every finite item occupies exactly one bag, container or ground location"));
        FGuid::Parse(RA->GetStringField(TEXT("target_handle")),NativeRequest.Observed);NativeCarry(50000);Phase=91;
    }
#endif
}
void ALDB91ObjectsProbe::Save(bool Complete)
{
    FString Output;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Output);if(Output.IsEmpty()){return;}auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("complete"),Complete);R->SetNumberField(TEXT("failures"),Failures);R->SetStringField(TEXT("stage"),TEXT("B91"));R->SetNumberField(TEXT("phase"),Phase);R->SetStringField(TEXT("contract"),Objects?Objects->Contract:TEXT(""));R->SetArrayField(TEXT("checks"),Checks);R->SetArrayField(TEXT("fitted_objects"),Manifest);R->SetNumberField(TEXT("finite_initial_items"),InitialIds.Num());FString Why;R->SetBoolField(TEXT("conserved"),Objects&&Objects->Conserved(Why));R->SetStringField(TEXT("conservation_reason"),Why);
    R->SetStringField(TEXT("scope"),TEXT("Native editor authority with two independent headless remote owners; storage, pickup/break/carry races and physical placement. Remaining B91 systems and full-stage acceptance are open."));
    R->SetStringField(TEXT("push_setup"),TEXT("Positive push refits the same existing crate to a native clear pad; original generated placements remain in fitted_objects. No item or prop is created by this setup."));
    TArray<TSharedPtr<FJsonValue>> StartIds,BreakIds,Items;
    for(auto Id:InitialIds){StartIds.Add(MakeShared<FJsonValueString>(Id.ToString()));}for(auto Id:StoredIds){BreakIds.Add(MakeShared<FJsonValueString>(Id.ToString()));}
    for(TActorIterator<ALDItem> I(GetWorld());I;++I)
    {auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("id"),I->Identity.ToString());J->SetNumberField(TEXT("kind"),uint8(I->Kind));J->SetNumberField(TEXT("state"),I->State);J->SetNumberField(TEXT("revision"),I->Revision);J->SetStringField(TEXT("origin"),I->Institution.ToString());J->SetStringField(TEXT("origin_receipt"),I->OriginReceipt.ToString());
     FString Location=TEXT("ground");if(auto* C=Cast<ALDWorldObject>(I->Holder.Get())){Location=TEXT("container:")+C->SourceId.ToString();}else if(auto* PC=Cast<ALDB91Controller>(I->Holder.Get())){Location=PC==A?TEXT("owner:A"):TEXT("owner:B");}J->SetStringField(TEXT("location"),Location);Items.Add(MakeShared<FJsonValueObject>(J));}
    R->SetArrayField(TEXT("initial_item_ids"),StartIds);R->SetArrayField(TEXT("break_item_ids"),BreakIds);R->SetArrayField(TEXT("physical_items"),Items);
    FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(Output/TEXT("Objects.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
