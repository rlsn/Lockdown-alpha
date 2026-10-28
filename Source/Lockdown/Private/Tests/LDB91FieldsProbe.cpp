#include "Tests/LDB91FieldsProbe.h"
#include "World/LDEnvironmentDirector.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Region/LDMapDefinition.h"
#include "Core/LDGroundQueries.h"
#include "Navigation/LDNavigationComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ALDB91FieldsProbe::ALDB91FieldsProbe(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;Started=FPlatformTime::Seconds();}
void ALDB91FieldsProbe::Check(bool Good,const FString& Text)
{auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("case"),Text);R->SetBoolField(TEXT("passed"),Good);Checks.Add(MakeShared<FJsonValueObject>(R));Failures+=!Good;UE_LOG(LogTemp,Display,TEXT("LD_B91_FIELDS %s passed=%d"),*Text,Good);}
void ALDB91FieldsProbe::Place(ALDB91Controller* PC,FVector Point)
{PC->Navigation->Stop();auto* Body=Cast<ALDCharacter>(PC->GetPawn());Body->SetActorLocation(Point);Body->GetCharacterMovement()->StopMovementImmediately();PC->ClientSetLocation(Point,FRotator::ZeroRotator);}
void ALDB91FieldsProbe::Task(uint8 Value)
{Phase=Value;At=GetWorld()->GetTimeSeconds();A->ClientB91ProbeTask(Value);B->ClientB91ProbeTask(Value);Save(false);}
void ALDB91FieldsProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(Phase==99){return;}const double Now=GetWorld()->GetTimeSeconds();const int64 Time=LDTactical::Milliseconds(Now);auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M){return;}auto* C=M->Combat.Get();
    if(Failures||FPlatformTime::Seconds()-Started>240){if(!Failures){Check(false,TEXT("Field fixture timed out"));}Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,3);return;}
    if(!Phase)
    {
        for(FConstPlayerControllerIterator I=GetWorld()->GetPlayerControllerIterator();I;++I){if(auto* PC=Cast<ALDB91Controller>(I->Get());PC&&PC->GetPawn()){if(PC->EnvironmentProbeRole==1){A=PC;}if(PC->EnvironmentProbeRole==2){B=PC;}}}
        if(!A||!B||Now<7){return;}Objects=ALDWorldObjects::Get(GetWorld());if(!Objects||!Objects->Ready){return;}
        Check(A!=B&&!A->IsLocalController()&&!B->IsLocalController(),TEXT("Two independent remote field owners"));
        Water=Objects->Find(TEXT("prop_water_flask_000"))->Physical;Oil=Objects->Find(TEXT("prop_oil_flask_000"))->Physical;Second=Objects->Find(TEXT("prop_water_flask_001"))->Physical;
        Check(Water.IsValid()&&Oil.IsValid()&&Second.IsValid()&&M->Inventory->Adopt(A,Water.Get())&&M->Inventory->Adopt(A,Oil.Get()),TEXT("Fixture stages two existing finite flasks without minting items"));
        bool Found=false;for(int N=0;N<200&&!Found;++N)
        {const FVector XY(80000+(N%20)*400,-80000+(N/20)*400,0);FHitResult Hit;if(!GetWorld()->LineTraceSingleByChannel(Hit,XY+FVector(0,0,1600),XY-FVector(0,0,100),ECC_GameTraceChannel1)){continue;}Aim=Hit.ImpactPoint;if(ULDMapDefinition::Get(GetWorld())->RiverAt(Aim)){continue;}
         const FVector Start=Aim+FVector(-300,0,92.15);FVector Floor;if(!LDGround::MapSurface(GetWorld(),Start-FVector(0,0,92.15),Floor)||FMath::Abs(Floor.Z-Aim.Z)>1||!LDGround::ClearSegment(GetWorld(),Start,Aim+FVector(0,0,30))){continue;}
         Place(A,Start);Place(B,Aim+FVector(100,100,92.15));Found=C->EnvironmentMask(Cast<ALDCharacter>(A->GetPawn()),Aim,true,0,180,16).Num()==4;}
        Check(Found&&FMath::Abs(Aim.X)>20000,TEXT("Four supported dry cells beyond legacy wire bounds"));
        for(auto* PC:{A,B}){M->StateFor(PC)->ProtectedUntil=0;C->StateFor(Cast<ALDCharacter>(PC->GetPawn()))->ProtectionUntil=0;M->SyncOwner(PC);}Phase=90;At=Now;return;
    }
    if(Phase==90){if(Now-At>1){Task(1);}return;}
    if(Phase==91){if(Now-At>1.3){Task(4);}return;}
    if(Phase==92){if(C->RiverCells.IsEmpty()){Task(6);}return;}
    if(Phase==93)
    {
        const auto* D=ULDEquipmentCatalog::Load(8)->Find(ELDItemKind::WaterFlask);if(Time<M->StateFor(A)->ItemFamilies.FindRef(D->CooldownKey())+200){return;}
        Check(M->Inventory->Adopt(A,Second.Get()),TEXT("Capacity retry uses a third existing finite flask"));Task(7);return;
    }
    if(Phase==8&&Now-At>2&&!ReplayRevision)
    {Check(Second.IsValid()&&Second->Holder==A&&!Second->bReserved&&C->RiverCells.Num()==1024&&C->EnvironmentPool.Num()==1024,TEXT("Normal activation at match cap retains item and fixed pool"));const auto* D=ULDEquipmentCatalog::Load(8)->Find(ELDItemKind::WaterFlask);Check(M->StateFor(A)->ItemFamilies.FindRef(D->CooldownKey())<Time,TEXT("Capacity rejection starts no family cooldown"));ReplayRevision=C->EnvironmentRevision;}
    auto Read=[](ALDB91Controller* PC,uint8 Key)->TSharedPtr<FJsonObject>{TSharedPtr<FJsonObject> R;const auto* Text=PC->EnvironmentProbeReports.Find(Key);if(Text){FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(*Text),R);}return R;};
    const auto RA=Read(A,Phase),RB=Read(B,Phase);if(!RA||!RB){return;}
    Check(RA->GetBoolField(TEXT("completed"))&&RB->GetBoolField(TEXT("completed")),FString::Printf(TEXT("Both remote field phase %u reports complete"),Phase));
    if(Phase==1){Check(Water.IsValid()&&C->RiverCells.IsEmpty()&&!Water->BindingMask,TEXT("Unbound request creates no field or consumption"));Task(2);return;}
    if(Phase==2){Check(Water->BindingMask==(1<<7)&&Oil->BindingMask==(1<<8),TEXT("Real quiet preparation binds both original flasks"));C->ApplyStatus(Cast<ALDCharacter>(A->GetPawn()),LDTactical::EStatus::Mute,Time+1200,FGuid::NewGuid());Task(3);return;}
    if(Phase==3){Check(Water.IsValid()&&C->RiverCells.IsEmpty(),TEXT("Mute rejects bound activation without spending"));Phase=91;At=Now;return;}
    if(Phase==4)
    {Check(!Water.IsValid()&&C->RiverCells.Num()==4,TEXT("Paid Water and duplicate replay consume once into four cells"));Check((RA->GetIntegerField(TEXT("states"))&1)&&(RB->GetIntegerField(TEXT("states"))&1),TEXT("Both authorized owners receive wet surface cues"));Check(C->StateFor(Cast<ALDCharacter>(B->GetPawn()))->Tactical.Has(LDTactical::EStatus::Wet,Time),TEXT("Supported full-map contact applies Wet"));Task(5);return;}
    if(Phase==5){Check(Oil.IsValid()&&C->RiverCells.Num()==4&&!(RA->GetIntegerField(TEXT("states"))&64),TEXT("Oil on Wet rejects with original item retained"));Phase=92;At=Now;return;}
    if(Phase==6)
    {Check(!Oil.IsValid()&&C->RiverCells.Num()==4,TEXT("Paid Oil succeeds after Wet expiry"));bool OnlyOil=true;for(const auto& P:C->RiverCells){OnlyOil&=P.Value.Mask(Time)==64;}Check(OnlyOil,TEXT("Oil has no automatic Burning transition"));Phase=93;At=Now;return;}
    if(Phase==7)
    {
        C->RetireEnvironment(Time);Check(C->RiverCells.IsEmpty(),TEXT("Prior paid fields retire before capacity fixture"));
        // Diagnostic allocator setup only: terrain-supported cells with detached
        // sponsors. This creates no items or player casts, and grants no cost proof.
        int Added=0;bool Accepted=true;ALDCombatDirector::FRiverCommand Q;auto Begin=[&](){Q=ALDCombatDirector::FRiverCommand();Q.Input=LDEnvironment::EInput::Water;Q.State.Owner=Q.State.BudgetOwner=FGuid::NewGuid();Q.State.Life=FGuid::NewGuid();Q.State.Cast=FGuid::NewGuid();Q.State.Created=Time;Q.State.Until=Time+10000;Q.State.Revision=++C->EnvironmentRevision;};Begin();
        for(int N=0;N<5000&&Added<1024;++N){const FIntPoint Id(220+N%100,220+N/100);const FVector P=LDRiver::Centre(Id);FHitResult H;if(!GetWorld()->LineTraceSingleByChannel(H,P+FVector(0,0,1600),P-FVector(0,0,100),ECC_GameTraceChannel1)){continue;}Q.Cells.Add(Id);Q.Surfaces.Add(Id,H.ImpactPoint.Z);Q.Normals.Add(Id,H.ImpactNormal);++Added;if(Q.Cells.Num()==64){Accepted&=C->QueueEnvironment(Q,Time);C->CommitRiver(Time);Begin();}}
        Check(Accepted&&Added==1024&&C->RiverCells.Num()==1024,TEXT("Terrain-supported diagnostic fixture fills 1024 match cells with 64 per sponsor"));
        auto Overflow=Q;Overflow.Cells.Add(LDRiver::Cell(Aim));Overflow.Surfaces.Add(LDRiver::Cell(Aim),Aim.Z);Check(!C->QueueEnvironment(Overflow,Time),TEXT("Allocator rejects one additional cell at match cap"));
        Place(B,Aim+FVector(4000,0,92.15));ReplayRevision=0;Task(8);return;
    }
    if(Phase==8)
    {
        Check(ReplayRevision&&!Second.IsValid()&&RA->GetBoolField(TEXT("capacity_retained"))&&C->RiverCells.Num()==4,TEXT("Retained physical Water succeeds once after actual-clock capacity expiry"));Check(RB->GetIntegerField(TEXT("surface_count"))==0,TEXT("Distant owner receives no canonical field registry"));
        auto* Body=Cast<ALDCharacter>(A->GetPawn());Check(C->StageEnvironment(Body,Aim,LDEnvironment::EInput::Frost,true,0,180,Time,1000),TEXT("Diagnostic Frost transitions real wet supported cells"));C->CommitRiver(Time);
        auto Conflict=C->EnvironmentCommand(Body,LDEnvironment::EInput::Water,Time,1000);Conflict.Cells.Add(LDRiver::Cell(Aim));Conflict.Surfaces.Add(LDRiver::Cell(Aim),Aim.Z+400);Check(!C->QueueEnvironment(Conflict,Time),TEXT("Conflicting stacked support plane rejects atomically"));
        Place(A,Aim+FVector(100,100,92.15));C->RefreshRiverContact(Body,Time,true);auto* S=C->StateFor(Body);Check(S->Tactical.SurfaceSlow==30&&S->Tactical.Has(LDTactical::EStatus::Wet,Time)&&!S->Tactical.Has(LDTactical::EStatus::Stun,Time),TEXT("Diagnostic wet ice contact slows30 percent and retains Wet without Stun"));
        FString Why;int Count=0;for(TActorIterator<ALDItem> I(GetWorld());I;++I){++Count;}Check(Objects->Conserved(Why)&&Count==129,TEXT("Exactly three paid consumes leave129 conserved physical items"));
        // Find an actual terrain ramp where using centre height alone would
        // incorrectly miss grounded contact within the same 2m cell.
        bool SlopeContact=false;const auto* Definition=ULDMapDefinition::Get(GetWorld());
        for(const auto& Plateau:Definition->Plateaus){for(int Angle=0;Angle<32&&!SlopeContact;++Angle){for(int Step=0;Step<10&&!SlopeContact;++Step)
        {const double Theta=Angle*2*PI/32;const FVector P=Plateau.Position+FVector(FMath::Cos(Theta),FMath::Sin(Theta),0)*(Plateau.Radius-1000+Step*1000);const auto Id=LDRiver::Cell(P);const FVector XY=LDRiver::Centre(Id);FHitResult H;
         if(!GetWorld()->LineTraceSingleByChannel(H,XY+FVector(0,0,1600),XY-FVector(0,0,100),ECC_GameTraceChannel1)||H.ImpactNormal.Z>.995||H.ImpactNormal.Z<.9){continue;}
         const FVector Offset(FMath::Sign(H.ImpactNormal.X)*90,FMath::Sign(H.ImpactNormal.Y)*90,0);FVector Floor;if(!LDGround::MapSurface(GetWorld(),H.ImpactPoint+Offset,Floor)||FMath::Abs(Floor.Z-H.ImpactPoint.Z)<=12){continue;}
         LDEnvironment::FCell Plane;Plane.SurfaceZ=H.ImpactPoint.Z;Plane.SurfaceNormal=H.ImpactNormal;if(!Plane.Supports(Id,Floor)){continue;}
         Place(A,Floor+FVector(0,0,92.15));S->Tactical.Statuses.RemoveAll([](const auto& T){return T.Kind==LDTactical::EStatus::Wet;});C->RiverContacts.Remove(Body);
         if(!C->StageEnvironment(Body,H.ImpactPoint,LDEnvironment::EInput::Water,true,0,180,Time,1000)){continue;}C->CommitRiver(Time);SlopeContact=S->Tactical.Has(LDTactical::EStatus::Wet,Time);}}if(SlopeContact){break;}}
        Check(SlopeContact,TEXT("Actual ramp contact follows support normal beyond12cm centre-height difference"));
        Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,Failures?3:0);return;
    }
#endif
}
void ALDB91FieldsProbe::Save(bool Complete)
{FString Output;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Output);if(Output.IsEmpty()){return;}auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("complete"),Complete);R->SetNumberField(TEXT("failures"),Failures);R->SetNumberField(TEXT("phase"),Phase);R->SetArrayField(TEXT("checks"),Checks);R->SetStringField(TEXT("scope"),TEXT("Two remote headless owners; paid Water/Oil, real quiet binding and clocks. Finite flasks explicitly staged; capacity and Frost are diagnostic setup. No paid Frost acquisition, visual or full-stage acceptance."));R->SetStringField(TEXT("aim"),Aim.ToString());FString Why;R->SetBoolField(TEXT("conserved"),Objects&&Objects->Conserved(Why));FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(Output/TEXT("Fields.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);}
