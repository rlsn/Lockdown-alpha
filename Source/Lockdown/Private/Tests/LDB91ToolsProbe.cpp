#include "Tests/LDB91ToolsProbe.h"
#include "World/LDEnvironmentDirector.h"
#include "World/LDPortableTools.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Inventory/LDInventoryDirector.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Navigation/LDNavigationComponent.h"
#include "Vision/LDVisionDirector.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ALDB91ToolsProbe::ALDB91ToolsProbe()
{PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;Started=FPlatformTime::Seconds();}
void ALDB91ToolsProbe::Check(bool Good,const FString& Text)
{auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("case"),Text);R->SetBoolField(TEXT("passed"),Good);Checks.Add(MakeShared<FJsonValueObject>(R));Failures+=!Good;UE_LOG(LogTemp,Display,TEXT("LD_B91_TOOLS %s passed=%d"),*Text,Good);}
void ALDB91ToolsProbe::Record(const FString& Tag)
{if(!Item){return;}auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("tag"),Tag);R->SetNumberField(TEXT("world_seconds"),GetWorld()->GetTimeSeconds());R->SetStringField(TEXT("item"),Item->Identity.ToString());R->SetNumberField(TEXT("remaining_ms"),LDPortableTools::Remaining(Item,GetWorld()->GetTimeSeconds()));R->SetBoolField(TEXT("lit"),Item->bLit);R->SetNumberField(TEXT("revision"),Item->Revision);R->SetNumberField(TEXT("binding_mask"),Item->BindingMask);R->SetStringField(TEXT("location"),Item->Holder==A?TEXT("owner:A"):Item->State==0?TEXT("ground"):TEXT("other"));Fuel.Add(MakeShared<FJsonValueObject>(R));}
void ALDB91ToolsProbe::Place(ALDB91Controller* PC,FVector Point)
{PC->Navigation->Stop();auto* Body=Cast<ALDCharacter>(PC->GetPawn());Body->SetActorLocation(Point);Body->GetCharacterMovement()->StopMovementImmediately();PC->ClientSetLocation(Point,FRotator::ZeroRotator);}
void ALDB91ToolsProbe::Task(uint8 Value)
{Phase=Value;At=GetWorld()->GetTimeSeconds();Objects->Publish();ALDEnvironmentDirector::Get(GetWorld())->Publish();A->ClientB91ProbeTask(Value);B->ClientB91ProbeTask(Value);Save(false);}
void ALDB91ToolsProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(Phase==99){return;}const double Now=GetWorld()->GetTimeSeconds();auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M){return;}
    if(Failures||FPlatformTime::Seconds()-Started>240){if(!Failures){Check(false,TEXT("Portable fixture timed out"));}Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,3);return;}
    auto Count=[](UWorld* W){int N=0;for(TActorIterator<ALDItem> I(W);I;++I){++N;}return N;};
    if(!Phase)
    {
        for(FConstPlayerControllerIterator I=GetWorld()->GetPlayerControllerIterator();I;++I){if(auto* PC=Cast<ALDB91Controller>(I->Get());PC&&PC->GetPawn()){if(PC->EnvironmentProbeRole==1){A=PC;}if(PC->EnvironmentProbeRole==2){B=PC;}}}
        if(!A||!B||Now<7){return;}Objects=ALDWorldObjects::Get(GetWorld());if(!Objects||!Objects->Ready){return;}AddTickPrerequisiteActor(Objects);Torch=Objects->Find(TEXT("prop_torch_000"));Item=Torch?Torch->Physical.Get():nullptr;if(!Item){return;}
        Check(A!=B&&!A->IsLocalController()&&!B->IsLocalController(),TEXT("Two independent remote portable-tool owners"));
        const auto* C=ULDEquipmentCatalog::Load(8);const auto* T=C->Find(ELDItemKind::Torch);const auto* L=C->Find(ELDItemKind::Lantern);const auto* R=C->Find(ELDItemKind::Rope);
        Check(C->Items.Num()==80&&T&&L&&R&&T->Binding==7&&T->Weight==2&&L->Weight==3&&R->Weight==3&&T->Price==-1&&L->Price==-1&&R->Price==-1,TEXT("Three Standard physical tools overlay the unchanged 80-item catalog"));
        Check(Objects->Objects.Num()==232&&Count(GetWorld())==132&&Item->FuelMs==180000&&!Item->bLit,TEXT("Exact 48 finite tools and initial retained fuel"));
        bool FuelValid=true;for(auto O:Objects->Objects){if(O.IsValid()&&O->Physical.IsValid()&&LDPortableTools::IsTool(O->Physical->Kind)){FuelValid&=O->Physical->FuelMs==LDPortableTools::InitialFuel(O->Physical->Kind)&&!O->Physical->bLit&&O->Physical->Quality==0;}}
        Check(FuelValid,TEXT("All24 torches and12 lanterns start full once; ropes have no fuel or stats"));
        for(auto* PC:{A,B}){auto* O=M->StateFor(PC);O->ProtectedUntil=0;M->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()))->ProtectionUntil=0;M->SyncOwner(PC);}
        Place(A,Torch->LastClear+FVector(-80,0,92.15));Place(B,Torch->LastClear+FVector(80,0,92.15));Record(TEXT("initial"));Phase=90;At=Now;return;
    }
    if(Phase==90){if(Now-At>1){Task(1);}return;}
    if(Phase==87)
    {
        auto* O=M->StateFor(A);if(!O||!O->bActive||!A->GetPawn()){return;}
        if(!ReturnAt){ReturnAt=Now;const FVector Floor=Torch->GetActorLocation()-FVector(0,0,Torch->Size().Z*.5+3);Place(A,Floor+FVector(-80,0,92.15));Place(B,Floor+FVector(150,0,92.15));O->ProtectedUntil=0;M->Combat->StateFor(Cast<ALDCharacter>(A->GetPawn()))->ProtectionUntil=0;M->SyncOwner(A);}
        if(Now-ReturnAt>6.3){Record(TEXT("life_return"));Task(7);}return;
    }
    if(Phase==8&&!Seeded&&Item->BindingMask==(1<<7)&&!M->StateFor(A)->DevelopmentKind)
    {
        // Consume a known remaining portion for an actual-clock expiry boundary;
        // no refill, new item or accelerated gameplay clock is introduced.
        Check(LDPortableTools::Remaining(Item,Now)>1400,TEXT("Expiry fixture only reduces existing retained fuel"));Item->FuelMs=1400;Item->FuelAt=Now;++Item->Revision;auto* O=M->StateFor(A);if(auto* E=const_cast<FLDInventoryItem*>(O->Inventory.Find(Item->Identity))){Item->Project(*E);}++O->Inventory.Revision;M->Inventory->Changed(A);Seeded=true;Record(TEXT("diagnostic_fuel_burn"));
    }
    auto Read=[](ALDB91Controller* PC,uint8 Key)->TSharedPtr<FJsonObject>{TSharedPtr<FJsonObject> R;const auto* Text=PC->EnvironmentProbeReports.Find(Key);if(Text){FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(*Text),R);}return R;};
    const auto RA=Read(A,Phase),RB=Read(B,Phase);if(!RA||!RB){return;}
    Check(RA->GetBoolField(TEXT("completed"))&&RB->GetBoolField(TEXT("completed")),FString::Printf(TEXT("Both remote portable phase %u reports complete"),Phase));
    if(Phase==1)
    {Check(Item->Holder==A&&Item->State==1&&RA->GetStringField(TEXT("owned_item"))==Item->Identity.ToString(),TEXT("Pickup adopts the original finite Torch instance"));Check(RB->GetStringField(TEXT("owned_item")).IsEmpty(),TEXT("Observer receives no foreign physical item identity"));Record(TEXT("pickup"));Task(2);return;}
    if(Phase==2)
    {Check(Item->BindingMask==(1<<7)&&LDPortableTools::Remaining(Item,Now)==180000,TEXT("Quiet binding consumes no fuel and creates no extra quick reference"));Check(M->StateFor(A)->Build.Gear.SightBonus==0&&M->StateFor(A)->Build.Gear.Detector==0&&M->StateFor(A)->Build.Gear.Armor==0,TEXT("Bound tools grant no passive sight, detection or armor"));Record(TEXT("bound"));Task(3);return;}
    if(Phase==3)
    {
        Check(Item->bLit&&RA->GetBoolField(TEXT("duplicate_same"))&&RB->GetIntegerField(TEXT("peer_light"))==1,TEXT("Bound toggle and duplicate replay light the same instance once"));
        auto* E=ALDEnvironmentDirector::Get(GetWorld());Check(E->SightFor(A)==1800&&M->Vision->SourcesFor(A).Num()==1&&M->Vision->SourcesFor(A)[0].Radius==1800,TEXT("Cosmetic light preserves the authoritative 18m sight radius"));Record(TEXT("lit"));
        M->Combat->ApplyStatus(Cast<ALDCharacter>(A->GetPawn()),LDTactical::EStatus::Blind,LDTactical::Milliseconds(Now)+1000,FGuid::NewGuid());Check(E->SightFor(A)==200,TEXT("Lit tools do not expand Blind sight"));
        M->Combat->ApplyStatus(Cast<ALDCharacter>(A->GetPawn()),LDTactical::EStatus::Mute,LDTactical::Milliseconds(Now)+2000,FGuid::NewGuid());Place(B,A->GetPawn()->GetActorLocation()+FVector(1900,0,0));Task(4);return;
    }
    if(Phase==4)
    {Check(RA->GetIntegerField(TEXT("receipt_result"))==2&&Item->bLit,TEXT("Mute rejects activation without extinguishing or refilling the item"));Check(!M->Vision->CanObserve(B,A->GetPawn())&&RB->GetIntegerField(TEXT("peer_body_count"))==0,TEXT("Light grants no out-of-range body observation"));Check(!Item->BindingMask,TEXT("Lit unbinding retains one physical fuel clock"));Record(TEXT("unbound"));Place(B,A->GetPawn()->GetActorLocation()+FVector(150,0,0));Task(5);return;}
    if(Phase==5)
    {
        Check(Item->State==0&&!Item->Holder.IsValid()&&Item->bLit&&RB->GetBoolField(TEXT("ground_lit")),TEXT("Dropped Torch remains lit at its actual observed ground proxy"));Check(RA->GetIntegerField(TEXT("receipt_result"))==2,TEXT("Known dropped identity cannot remotely activate the ground emitter"));
        int Fields=0;for(TActorIterator<ALDTacticalField> I(GetWorld());I;++I){++Fields;}Check(Fields==0,TEXT("Cosmetic ground light creates no automatic fire or combat field"));Record(TEXT("ground"));Task(6);return;
    }
    if(Phase==6)
    {Check(Item->Holder==A&&Item->BindingMask==(1<<8)&&Item->bLit,TEXT("Repickup and accessory swap retain the same burning fuel instance"));Record(TEXT("swapped"));M->EndContenderLife(A,Now);Check(Item->State==0&&!Item->Holder.IsValid()&&!Item->BindingMask&&Item->bLit,TEXT("Bound tool drops on end-life without resetting fuel"));Record(TEXT("life_drop"));Phase=87;At=Now;Save(false);return;}
    if(Phase==7)
    {Check(Item->Holder==A&&Item->bLit&&RA->GetStringField(TEXT("owned_item"))==Item->Identity.ToString(),TEXT("New life recovers the original still-burning instance"));Record(TEXT("recovered"));Task(8);return;}
    if(Phase==8)
    {
        Check(Seeded&&LDPortableTools::Remaining(Item,Now)==0&&!Item->bLit&&RA->GetIntegerField(TEXT("receipt_result"))==2,TEXT("Actual expiry stays empty; later activation cannot refill fuel"));Check(LDPortableTools::LightFor(A)==0&&RB->GetIntegerField(TEXT("peer_light"))==0,TEXT("Expired light disappears from owner and observed body presentation"));FString Why;Check(Objects->Conserved(Why)&&Count(GetWorld())==132,TEXT("All132 finite physical identities survive binding, fuel and life changes"));Record(TEXT("empty"));Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,Failures?3:0);return;
    }
#endif
}
void ALDB91ToolsProbe::Save(bool Complete)
{
    FString Output;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Output);if(Output.IsEmpty()){return;}auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("complete"),Complete);R->SetNumberField(TEXT("failures"),Failures);R->SetNumberField(TEXT("phase"),Phase);R->SetArrayField(TEXT("checks"),Checks);R->SetArrayField(TEXT("fuel_trace"),Fuel);R->SetNumberField(TEXT("initial_items"),132);int Count=0;for(TActorIterator<ALDItem> I(GetWorld());I;++I){++Count;}R->SetNumberField(TEXT("final_items"),Count);FString Why;R->SetBoolField(TEXT("conserved"),Objects&&Objects->Conserved(Why));R->SetNumberField(TEXT("base_equipment_count"),ULDEquipmentCatalog::Load(8)->Items.Num());R->SetNumberField(TEXT("bindings"),A?A->OwnerState.Inventory.Bindings.Num():0);R->SetNumberField(TEXT("actions"),A?A->OwnerState.Build.Actions.Num():0);
    R->SetStringField(TEXT("scope"),TEXT("Two independent headless owners, real RPC/quiet preparation and actual-clock fuel. Expiry setup consumes remaining fuel down to 1400ms; it never refills or creates an item. Socket fire, rope/ladder links and full-stage acceptance remain open."));FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(Output/TEXT("Tools.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
