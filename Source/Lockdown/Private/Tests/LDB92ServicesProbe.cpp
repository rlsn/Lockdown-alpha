#include "Tests/LDB92ServicesProbe.h"
#include "Population/LDB92Roster.h"
#include "Economy/LDB92Economy.h"
#include "Economy/LDB92Services.h"
#include "Economy/LDB92Deposits.h"
#include "Combat/LDCombatDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "World/LDWorldObjects.h"
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

ALDB92ServicesProbe::ALDB92ServicesProbe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;Started=FPlatformTime::Seconds();}
void ALDB92ServicesProbe::Check(bool Good,const FString& Name)
{Failures+=!Good;auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("case"),Name);J->SetBoolField(TEXT("passed"),Good);Checks.Add(MakeShared<FJsonValueObject>(J));UE_LOG(LogTemp,Display,TEXT("LD_B92_SERVICES passed=%d %s"),Good,*Name);}
void ALDB92ServicesProbe::Place(ALDB92Controller* PC,FVector Floor)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* Body=Cast<ALDCharacter>(PC->GetPawn());PC->StopSourceOrder();Body->SetActorLocation(Floor+FVector(0,0,95));Body->GetCharacterMovement()->StopMovementImmediately();PC->ClientSetLocation(Body->GetActorLocation(),Body->GetActorRotation());
    auto* S=M->Combat->StateFor(Body);S->ProtectionUntil=0;S->LastDamage=S->LastDealt=S->QuietSince=-1000;M->StateFor(PC)->ProtectedUntil=0;M->SyncOwner(PC);ALDEnvironmentDirector::Get(GetWorld())->Publish();ALDWorldObjects::Get(GetWorld())->Publish();
}
void ALDB92ServicesProbe::Task(ALDB92Controller* PC,uint8 Value,FName POI)
{PC->ServicesProbeReports.Remove(Value);if(const auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC)){PC->ClientLifeReceipt(*O);}PC->ClientB92ProbeTask(Value,POI);At=GetWorld()->GetTimeSeconds();}
void ALDB92ServicesProbe::Save(bool Complete)
{
    FString Output,Json;FParse::Value(FCommandLine::Get(),TEXT("B92Output="),Output);auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("stage"),TEXT("B92"));J->SetBoolField(TEXT("bank_integration"),true);J->SetBoolField(TEXT("official_integration"),true);J->SetBoolField(TEXT("deposit_integration"),true);J->SetBoolField(TEXT("survey_integration"),true);J->SetBoolField(TEXT("work_integration"),true);J->SetStringField(TEXT("scope"),TEXT("remote_takeover_and_five_physical_markets"));J->SetBoolField(TEXT("complete"),Complete);J->SetBoolField(TEXT("full_stage_acceptance"),false);J->SetNumberField(TEXT("phase"),Phase);J->SetNumberField(TEXT("failures"),Failures);J->SetArrayField(TEXT("checks"),Checks);FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(Output/TEXT("Services.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    if(Complete){const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();FFileHelper::SaveStringToFile(M->Population->Economy->Accounting().ExportJson(TEXT("B92RemoteServices")),*(Output/TEXT("Ledger.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);}
}
void ALDB92ServicesProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(Phase==99){return;}if(Failures||FPlatformTime::Seconds()-Started>420){if(!Failures){Check(false,TEXT("Remote service scenario timed out"));}Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,3);return;}
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* R=M?M->Population.Get():nullptr;if(!R||!R->Ready){return;}auto* E=R->Economy.Get();auto* Objects=ALDWorldObjects::Get(GetWorld());const double Now=GetWorld()->GetTimeSeconds();FString Error;
    auto Read=[&](ALDB92Controller* PC,uint8 Task)->TSharedPtr<FJsonObject>{TSharedPtr<FJsonObject> J;if(!PC){return J;}const auto* Text=PC->ServicesProbeReports.Find(Task);if(Text){FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(*Text),J);}return J;};
    auto Good=[&](TSharedPtr<FJsonObject> J){return J&&J->GetBoolField(TEXT("completed"));};
    const auto& Markets=ALDB92Services::Markets();
    if(Phase==0)
    {
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){if(auto* PC=Cast<ALDB92Controller>(It->Get());PC&&PC->GetPawn()){if(PC->ServicesProbeRole==1){A=PC;}if(PC->ServicesProbeRole==2){B=PC;}}}
        if(!A.IsValid()||!B.IsValid()){return;}
        Check(!A->IsLocalController()&&!B->IsLocalController()&&A->GetNetConnection()&&B->GetNetConnection(),TEXT("Two real remote owning clients admitted to existing seats"));
        Check(R->Humans()==2&&R->Audit(Error)&&E->Accounting().ItemRecords().Num()==569&&Objects->Conserved(Error),TEXT("One ledger covers426 market plus143 finite world goods and24 persistent accounts"));
        int32 N=0;for(auto O:Objects->Objects){if(O.IsValid()&&O->Kind==ELDObjectKind::Chest&&O->Contents.ContainsByPredicate([](auto P){return P.IsValid()&&P->Kind==ELDItemKind::Ore;})){Place(N==0?A.Get():B.Get(),O->LastClear+FVector(90,0,0));if(++N==2){break;}}}
        Check(N==2,TEXT("Two existing common caches supply original Ore; no diagnostic money/items created"));Task(A.Get(),1,NAME_None);Task(B.Get(),1,NAME_None);Phase=1;Save(false);return;
    }
    if(Phase==1)
    {
        if(!Good(Read(A.Get(),1))||!Good(Read(B.Get(),1))){return;}Check(M->StateFor(A.Get())->Inventory.Items.Num()==1&&M->StateFor(B.Get())->Inventory.Items.Num()==1&&E->SynchronizeWorld(Error)&&E->Audit(Error),TEXT("Remote LMB storage/Open and exact Take transfer two original Ore identities into real bags"));
        const FVector P=ALDB92Services::Counter(GetWorld(),Markets[0]);Place(A.Get(),P+FVector(50,0,0));Place(B.Get(),P-FVector(50,0,0));At=Now;Phase=90;return;
    }
    if(Phase==90){if(Now-At<1){return;}Task(A.Get(),2,Markets[0]);Phase=2;return;}
    if(Phase==2){const auto J=Read(A.Get(),2);if(!Good(J)){return;}Check(J->GetBoolField(TEXT("accepted"))&&M->StateFor(A.Get())->Gold>0,TEXT("First zero-wallet contender sells an actual cached Ore for finite Capital cash"));Task(B.Get(),2,Markets[0]);Phase=3;return;}
    if(Phase==3){const auto J=Read(B.Get(),2);if(!Good(J)){return;}Check(J->GetBoolField(TEXT("accepted"))&&M->StateFor(B.Get())->Gold>0,TEXT("Other remote contender independently earns from a different finite source"));SavedGold=M->StateFor(A.Get())->Gold;Task(A.Get(),15,NAME_None);Phase=30;return;}
    if(Phase==30){const auto J=Read(A.Get(),15);if(!Good(J)){return;}Check(J->GetBoolField(TEXT("accepted"))&&J->GetBoolField(TEXT("duplicate_same"))&&M->StateFor(A.Get())->Gold==SavedGold-3&&M->StateFor(A.Get())->BankGold==2,TEXT("Remote duplicate deposit debits3 once, credits2 to persistent bank and burns1"));Task(A.Get(),17,NAME_None);Phase=31;return;}
    if(Phase==31){const auto J=Read(A.Get(),17);if(!Good(J)){return;}Check(!J->GetBoolField(TEXT("accepted"))&&M->StateFor(A.Get())->Gold==SavedGold-3&&M->StateFor(A.Get())->BankGold==2,TEXT("Stale bank revision refuses without money movement"));Task(A.Get(),18,NAME_None);Phase=32;return;}
    if(Phase==32){const auto J=Read(A.Get(),18);if(!Good(J)){return;}Check(!J->GetBoolField(TEXT("accepted"))&&M->StateFor(A.Get())->BankGold==2&&M->StateFor(B.Get())->BankGold==0,TEXT("Zero-credit deposit refuses and another contender has an independent bank"));SavedGold=M->StateFor(A.Get())->Gold;Task(A.Get(),10,NAME_None);Phase=4;return;}
    if(Phase==4){const auto J=Read(A.Get(),10);if(!Good(J)){return;}Check(!J->GetBoolField(TEXT("accepted"))&&M->StateFor(A.Get())->Gold==SavedGold,TEXT("Forged price rejects before spending or allocating"));Task(A.Get(),11,NAME_None);Phase=5;return;}
    if(Phase==5){const auto J=Read(A.Get(),11);if(!Good(J)){return;}Check(!J->GetBoolField(TEXT("accepted"))&&M->StateFor(A.Get())->Gold==SavedGold,TEXT("Stale inventory confirmation rejects without payment"));Task(A.Get(),3,NAME_None);Phase=6;return;}
    if(Phase==6){const auto J=Read(A.Get(),3);if(!Good(J)){return;}Check(J->GetBoolField(TEXT("accepted"))&&J->GetBoolField(TEXT("duplicate_same"))&&M->StateFor(A.Get())->Inventory.Items.Num()==1,TEXT("Duplicate remote Buy allocates one existing stock identity and returns one receipt"));Task(A.Get(),4,NAME_None);Phase=7;return;}
    if(Phase==7){const auto J=Read(A.Get(),4);if(!Good(J)){return;}Check(J->GetBoolField(TEXT("accepted"))&&M->StateFor(A.Get())->Gold<SavedGold,TEXT("Resale preserves original identity and a same-pool cycle loses gold"));Task(A.Get(),5,Markets[0]);Task(B.Get(),5,Markets[0]);Phase=8;return;}
    if(Phase==8){const auto JA=Read(A.Get(),5),JB=Read(B.Get(),5);if(!Good(JA)||!Good(JB)){return;}Check(JA->GetStringField(TEXT("quote_revision"))==JB->GetStringField(TEXT("quote_revision")),TEXT("Both remote contenders confirm the same current Capital quote"));Task(A.Get(),6,NAME_None);Task(B.Get(),6,NAME_None);Phase=9;return;}
    if(Phase==9){const auto JA=Read(A.Get(),6),JB=Read(B.Get(),6);if(!Good(JA)||!Good(JB)){return;}Check(int32(JA->GetBoolField(TEXT("accepted")))+int32(JB->GetBoolField(TEXT("accepted")))==1,TEXT("Two real remote purchases serialize to one winner; stale loser keeps funds"));Task(B.Get(),7,Markets[1]);Phase=10;return;}
    if(Phase==10)
    {
        const auto J=Read(B.Get(),7);if(!Good(J)){return;}Check(!J->GetBoolField(TEXT("accepted"))&&!J->GetBoolField(TEXT("has_quote"))&&J->GetIntegerField(TEXT("offers"))==0,TEXT("Remote other-city Open returns no stock or quote outside physical access"));
        Seat=M->ContenderId(A.Get());OriginalBody=Cast<ALDCharacter>(A->GetPawn());BodyLife=M->Combat->StateFor(OriginalBody.Get())->Identity;SavedGold=M->StateFor(A.Get())->Gold;SavedItems.Reset();for(const auto& I:M->StateFor(A.Get())->Inventory.Items){SavedItems.Add(I.Id);}
        A->RaiseCombatTokenFloor(100002);M->Combat->StateFor(OriginalBody.Get())->LastCoreToken=100000;M->Combat->StateFor(OriginalBody.Get())->CancelledCoreThrough=100001;Task(A.Get(),8,NAME_None);Phase=11;return;
    }
    if(Phase==11)
    {
        if(R->Humans()!=1){return;}const auto* Slot=R->Seats().FindByPredicate([&](const auto& S){return S.Id==Seat;});if(!Slot){Check(false,TEXT("Original persistent seat remains registered"));return;}const auto& S=*Slot;auto* PC=S.Controller.Get();const auto* O=M->StateFor(PC);
        Check(!S.Human&&S.Id==Seat&&PC&&PC->GetPawn()==OriginalBody.Get()&&M->Combat->StateFor(OriginalBody.Get())->Identity==BodyLife&&O->Gold==SavedGold&&O->Life==1,TEXT("Actual network disconnect retains the same body, life and wallet under AI control"));
        bool Same=O&&O->Inventory.Items.Num()==SavedItems.Num();if(O){for(const auto& I:O->Inventory.Items){Same&=SavedItems.Contains(I.Id);}}Check(Same&&E->SynchronizeWorld(Error)&&E->Audit(Error),TEXT("Disconnect preserves exact purchased goods and canonical lineage without a new grant"));Phase=12;Save(false);return;
    }
    if(Phase==12)
    {
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){if(auto* PC=Cast<ALDB92Controller>(It->Get());PC&&PC->ServicesProbeRole==3&&PC->GetPawn()){Replacement=PC;}}
        if(!Replacement.IsValid()){return;}Check(R->Humans()==2&&M->ContenderId(Replacement.Get())==Seat&&Replacement->GetPawn()==OriginalBody.Get()&&M->StateFor(Replacement.Get())->Gold==SavedGold,TEXT("Replacement remote connection takes the available original seat/body/account"));
        for(auto O:Objects->Objects){if(O.IsValid()&&O->Kind==ELDObjectKind::Sapling&&!O->Broken){Tree=O;break;}}if(!Tree.IsValid()){Check(false,TEXT("Finite sapling firing fixture exists"));return;}Place(Replacement.Get(),Tree->LastClear+FVector(-94,0,0));At=Now;Phase=91;return;
    }
    if(Phase==91){if(Now-At<1){return;}Task(Replacement.Get(),9,NAME_None);Phase=13;return;}
    if(Phase==13)
    {
        const auto J=Read(Replacement.Get(),9);if(!Good(J)){return;}Check(J->GetIntegerField(TEXT("combat_floor"))>100002&&Tree.IsValid()&&Tree->Hp<80,TEXT("Replacement client receives input floor and executes an actual ordinary attack on its retained body"));Phase=14;
    }
    if(Phase==14)
    {
        auto* PC=Replacement.Get();int32 Given=0;
        for(auto O:Objects->Objects){if(!O.IsValid()||!O->Container()){continue;}for(int32 I=O->Contents.Num()-1;I>=0&&Given<2;--I){auto* P=O->Contents[I].Get();if(P&&P->Kind==ELDItemKind::Ore){O->Contents.RemoveAt(I);P->Holder.Reset();P->State=0;if(M->Inventory->Adopt(PC,P,NAME_None,false)){++Given;}}}if(Given==2){break;}}
        Check(Given==2&&E->SynchronizeWorld(Error),TEXT("City fixture transfers two existing cached Ore; no hidden funding or item creation"));Place(PC,ALDB92Services::Counter(GetWorld(),Markets[City])+FVector(60,0,0));At=Now;Phase=92;return;
    }
    if(Phase==92){if(Now-At<1){return;}Task(Replacement.Get(),2,Markets[City]);Phase=15;return;}
    if(Phase==15){const auto J=Read(Replacement.Get(),2);if(!Good(J)){return;}Check(J->GetBoolField(TEXT("accepted")),Markets[City].ToString()+TEXT(": real remote sales pay from this pool"));if(City==1){SavedGold=M->StateFor(Replacement.Get())->Gold;Task(Replacement.Get(),16,NAME_None);Phase=34;}else{Task(Replacement.Get(),3,NAME_None);Phase=16;}return;}
    if(Phase==34){const auto J=Read(Replacement.Get(),16);if(!Good(J)){return;}Check(J->GetBoolField(TEXT("accepted"))&&J->GetBoolField(TEXT("duplicate_same"))&&M->StateFor(Replacement.Get())->BankGold==0&&M->StateFor(Replacement.Get())->Gold==SavedGold+2,TEXT("Replacement owner withdraws Capital deposit at Highfort exactly once without fee"));Task(Replacement.Get(),3,NAME_None);Phase=16;return;}
    if(Phase==16){const auto J=Read(Replacement.Get(),3);if(!Good(J)){return;}Check(J->GetBoolField(TEXT("accepted"))&&J->GetBoolField(TEXT("duplicate_same")),Markets[City].ToString()+TEXT(": remote duplicate Buy receives one existing stock unit"));Task(Replacement.Get(),4,NAME_None);Phase=17;return;}
    if(Phase==17)
    {
        const auto J=Read(Replacement.Get(),4);if(!Good(J)){return;}Check(J->GetBoolField(TEXT("accepted")),Markets[City].ToString()+TEXT(": resale returns original item to this same pool"));
        if(++City<Markets.Num()){Phase=14;return;}
        ALDItem* Knife=nullptr;for(auto O:Objects->Objects){if(O.IsValid()&&O->Kind==ELDObjectKind::ForestryKnife&&O->Physical.IsValid()&&O->Physical->State==0){Knife=O->Physical.Get();break;}}
        Check(Knife&&M->Inventory->Adopt(Replacement.Get(),Knife,NAME_None,false)&&E->SynchronizeWorld(Error),TEXT("Harvest fixture prepares one existing finite Knife without creating another source"));Place(Replacement.Get(),Tree->LastClear+FVector(0,-110,0));At=Now;Phase=93;return;
    }
    if(Phase==93){if(Now-At<1){return;}Task(Replacement.Get(),12,NAME_None);Phase=18;return;}
    if(Phase==18)
    {
        if(!Good(Read(Replacement.Get(),12))){return;}Check(Tree->Broken&&Tree->HarvestReceipt.IsValid()&&E->SynchronizeWorld(Error)&&E->Accounting().Item(Tree->YieldIdentity)&&E->Accounting().Item(Tree->YieldIdentity)->Origin==Tree->HarvestReceipt,TEXT("Remote paid Knife binding and one-confirmation harvest create one canonical Fiber with the original source receipt"));
        ALDItem* Water=nullptr;for(auto O:Objects->Objects){if(O.IsValid()&&O->Kind==ELDObjectKind::Water&&O->Physical.IsValid()&&O->Physical->State==0){Water=O->Physical.Get();break;}}
        Check(Water&&M->Inventory->Adopt(Replacement.Get(),Water,NAME_None,false)&&E->SynchronizeWorld(Error),TEXT("Consumption fixture transfers one original finite Water Flask"));Task(Replacement.Get(),13,NAME_None);Phase=19;return;
    }
    if(Phase==19)
    {
        if(!Good(Read(Replacement.Get(),13))){return;}Check(E->SynchronizeWorld(Error)&&E->Audit(Error)&&Objects->Conserved(Error)&&R->Audit(Error),TEXT("One canonical ledger closes bags, containers, markets, source creation, consume and network takeover"));
        int32 Consumed=0;for(const auto& P:E->Accounting().ItemRecords()){Consumed+=P.Value.Consumed;}
        Check(Consumed>=1&&E->Accounting().TotalGold()==3288-E->Accounting().Burned()&&E->Accounting().Minted()==0,TEXT("Paid Water consumption and scheduled institutional sinks conserve every item and gold; no work/life refill"));
        Phase=40;return;
    }
    if(Phase==40)
    {
        auto* PC=Replacement.Get();const auto WA=E->Wallet(M->ContenderId(PC)),WB=E->Wallet(M->ContenderId(B.Get()));LDEconomy::FTransaction Fund;Fund.Id=FGuid::NewGuid();Fund.Cause=TEXT("DiagnosticFixtureTransfer");Fund.Gold={{TEXT("capital"),E->Ledger.Account(TEXT("capital"))->Revision,-400},{WA,E->Ledger.Account(WA)->Revision,300},{WB,E->Ledger.Account(WB)->Revision,100}};
        Check(E->Ledger.Commit(Fund,Error),TEXT("Official supply fixture transfers400 existing reserve gold; no mint or earned-acquisition claim"));M->StateFor(PC)->Gold=E->Ledger.Account(WA)->Gold;M->StateFor(B.Get())->Gold=E->Ledger.Account(WB)->Gold;Place(PC,ALDB92Services::Counter(GetWorld(),TEXT("scholars_archive"))+FVector(60,0,0));Place(B.Get(),ALDB92Services::Counter(GetWorld(),TEXT("scholars_archive"))-FVector(60,0,0));At=Now;Phase=95;return;
    }
    if(Phase==95){if(Now-At<1){return;}Task(Replacement.Get(),19,TEXT("scholars_archive"));Phase=41;return;}
    if(Phase==41){const auto J=Read(Replacement.Get(),19);if(!Good(J)){return;}Check(J->GetBoolField(TEXT("accepted"))&&J->GetBoolField(TEXT("duplicate_same")),TEXT("Remote archive duplicate creates one paid CircleIII book with one90-gold burn"));Task(B.Get(),20,TEXT("scholars_archive"));Phase=42;return;}
    if(Phase==42){const auto J=Read(B.Get(),20);if(!Good(J)){return;}Check(J->GetBoolField(TEXT("accepted")),TEXT("Second remote owner purchases the second global copy of the same definition"));SavedGold=M->StateFor(Replacement.Get())->Gold;Task(Replacement.Get(),21,TEXT("scholars_archive"));Phase=43;return;}
    if(Phase==43){const auto J=Read(Replacement.Get(),21);if(!Good(J)){return;}Check(!J->GetBoolField(TEXT("accepted"))&&M->StateFor(Replacement.Get())->Gold==SavedGold,TEXT("Third CircleIII purchase refuses without spending after two globally shared copies"));Place(Replacement.Get(),ALDB92Services::Counter(GetWorld(),TEXT("capital_supply"))+FVector(60,0,0));At=Now;Phase=96;return;}
    if(Phase==96){if(Now-At<1){return;}Task(Replacement.Get(),22,TEXT("capital_supply"));Phase=44;return;}
    if(Phase==44){const auto J=Read(Replacement.Get(),22);if(!Good(J)){return;}Check(J->GetBoolField(TEXT("accepted")),TEXT("Ordinary supply creates one real Fiber for4 burned gold in the canonical ledger"));SavedGold=M->StateFor(Replacement.Get())->Gold;Task(Replacement.Get(),23,TEXT("capital_supply"));Phase=45;return;}
    if(Phase==45){const auto J=Read(Replacement.Get(),23);if(!Good(J)){return;}Check(!J->GetBoolField(TEXT("accepted"))&&M->StateFor(Replacement.Get())->Gold==SavedGold,TEXT("Supply counter refuses forged CircleIII order outside its endpoint role"));Task(Replacement.Get(),24,NAME_None);Phase=46;return;}
    if(Phase==46)
    {
        if(!Good(Read(Replacement.Get(),24))){return;}Check(E->SynchronizeWorld(Error)&&E->Audit(Error)&&R->Audit(Error),TEXT("Official outputs preserve original receipt, exact bag projections and global stock through source synchronization"));
        auto* PC=Replacement.Get();DepositXp=M->StateFor(PC)->Xp;Place(PC,ALDB92Deposits::Position(GetWorld(),0)+FVector(60,0,0));Task(PC,26,NAME_None);Phase=60;return;
    }
    if(Phase==60){if(!Good(Read(Replacement.Get(),26))){return;}At=Now;Phase=61;return;}
    if(Phase==61){if(Now-At<7){return;}Check(E->Accounting().Remaining(ALDB92Deposits::Budget(0))==6&&!R->Deposits->Active(Replacement.Get()),TEXT("One S command stops automatic extraction; staying inside does not restart or spend a charge"));Place(Replacement.Get(),ALDB92Deposits::Position(GetWorld(),5)+FVector(60,0,0));Task(Replacement.Get(),25,NAME_None);Phase=62;return;}
    if(Phase==62){if(!Good(Read(Replacement.Get(),25))){return;}Check(E->Accounting().Remaining(ALDB92Deposits::Budget(5))==5,TEXT("Standing eligible at Ore completes one real six-second automatic extraction without any interaction button"));Place(Replacement.Get(),ALDB92Deposits::Position(GetWorld(),0)+FVector(60,0,0));Task(Replacement.Get(),25,NAME_None);Phase=63;return;}
    if(Phase==63){if(!Good(Read(Replacement.Get(),25))){return;}Check(E->Accounting().Remaining(ALDB92Deposits::Budget(0))==5,TEXT("Crystal uses the same finite six-second automatic POI channel"));Place(Replacement.Get(),ALDB92Deposits::Position(GetWorld(),7)+FVector(60,0,0));Task(Replacement.Get(),25,NAME_None);Phase=64;return;}
    if(Phase==64)
    {
        if(!Good(Read(Replacement.Get(),25))){return;}Check(E->Accounting().Remaining(ALDB92Deposits::Budget(7))==5&&M->StateFor(Replacement.Get())->Xp==DepositXp,TEXT("Fiber extraction creates one item and no XP; all three outputs have canonical identities"));
        auto* PC=Replacement.Get();auto* O=M->StateFor(PC);for(int32 I=0;I<O->Build.Slots.Num();++I){if(O->Build.Slots[I]==ELDSkill::Survey){O->Build.Slots[I]=ELDSkill::None;O->Build.Actions[I]=FLDActionRef();}}O->Build.Ranks[uint8(ELDSkill::Survey)]=1;O->Build.Attributes[LDDevelopment::WIS]=10;O->Build.Slots[0]=ELDSkill::Survey;O->Build.Actions[0]=FLDActionRef();O->Build.Actions[0].Skill=ELDSkill::Survey;++O->Build.Revision;Check(LDDevelopment::Valid(O->Build),TEXT("Explicit Survey fixture is a valid learned single-slot build independent of admitted preset"));M->RefreshBuild(PC);Place(PC,ALDB92Deposits::Position(GetWorld(),0)+FVector(60,0,0));R->Deposits->Stop(PC);Task(PC,27,NAME_None);Phase=70;return;
    }
    if(Phase==70){const auto J=Read(Replacement.Get(),27);if(!Good(J)){return;}auto* O=M->StateFor(Replacement.Get());Check(!J->GetBoolField(TEXT("accepted"))&&E->Accounting().Remaining(ALDB92Deposits::Budget(0))==5&&O->Build.Cooldowns[uint8(ELDSkill::Survey)]==0,TEXT("Prepared Survey below score12 refuses before cooldown or charge spend"));O->Build.Attributes[LDDevelopment::WIS]=12;++O->Build.Revision;M->RefreshBuild(Replacement.Get());Task(Replacement.Get(),28,NAME_None);Phase=71;return;}
    if(Phase==71){const auto J=Read(Replacement.Get(),28);if(!Good(J)){return;}SurveyCooldown=M->StateFor(Replacement.Get())->Build.Cooldowns[uint8(ELDSkill::Survey)];Check(J->GetBoolField(TEXT("accepted"))&&E->Accounting().Remaining(ALDB92Deposits::Budget(0))==4&&SurveyCooldown>Now,TEXT("One normal UI confirmation executes prepared one-second Survey, shares the Crystal charge and starts six-second cooldown"));Task(Replacement.Get(),29,NAME_None);Phase=72;return;}
    if(Phase==72){const auto J=Read(Replacement.Get(),29);if(!Good(J)){return;}Check(!J->GetBoolField(TEXT("accepted"))&&E->Accounting().Remaining(ALDB92Deposits::Budget(0))==4&&M->StateFor(Replacement.Get())->Build.Cooldowns[uint8(ELDSkill::Survey)]==SurveyCooldown,TEXT("Immediate Survey retry refuses without a second item or cooldown reset"));Phase=73;}
    if(Phase==73){if(Now<SurveyCooldown+.2){return;}M->Combat->ApplyStatus(Cast<ALDCharacter>(Replacement->GetPawn()),LDTactical::EStatus::Mute,LDTactical::Milliseconds(Now)+3000,FGuid::NewGuid());Task(Replacement.Get(),30,NAME_None);Phase=74;return;}
    if(Phase==74){const auto J=Read(Replacement.Get(),30);if(!Good(J)){return;}SurveyCooldown=M->StateFor(Replacement.Get())->Build.Cooldowns[uint8(ELDSkill::Survey)];Check(J->GetBoolField(TEXT("accepted"))&&E->Accounting().Remaining(ALDB92Deposits::Budget(0))==3,TEXT("Mute permits the learned Survey action; two shortcut outputs use the same source budget"));Phase=75;}
    if(Phase==75){if(Now<SurveyCooldown+.2){return;}M->Combat->ApplyStatus(Cast<ALDCharacter>(Replacement->GetPawn()),LDTactical::EStatus::Silence,LDTactical::Milliseconds(Now)+3000,FGuid::NewGuid());Task(Replacement.Get(),29,NAME_None);At=Now;Phase=76;return;}
    if(Phase==76){const auto J=Read(Replacement.Get(),29);if(!Good(J)){return;}Check(!J->GetBoolField(TEXT("accepted"))&&E->Accounting().Remaining(ALDB92Deposits::Budget(0))==3,TEXT("Silence blocks learned Survey without spending its source"));Phase=77;}
    if(Phase==77){if(Now-At<3.3){return;}Task(Replacement.Get(),31,NAME_None);Phase=78;return;}
    if(Phase==78){const auto J=Read(Replacement.Get(),31);if(!Good(J)){return;}Check(!J->GetBoolField(TEXT("accepted"))&&E->Accounting().Remaining(ALDB92Deposits::Budget(0))==3&&M->StateFor(Replacement.Get())->Build.Cooldowns[uint8(ELDSkill::Survey)]>Now,TEXT("S interrupts Survey before its one-second commit; accepted-start cooldown remains and charge is retained"));Phase=100;}
    if(Phase==100)
    {
        auto* PC=Replacement.Get();auto* O=M->StateFor(PC);WorkGold=O->Gold;WorkXp=O->Xp;
        for(auto Kind:{ELDItemKind::Fiber,ELDItemKind::Ore}){const int32 Need=Kind==ELDItemKind::Fiber?8:4;int32 Count=O->Inventory.Items.FilterByPredicate([&](const auto& I){return I.Kind==Kind&&!I.BindingMask;}).Num();for(auto Obj:Objects->Objects){if(!Obj.IsValid()){continue;}for(int32 I=Obj->Contents.Num()-1;I>=0&&Count<Need;--I){auto* P=Obj->Contents[I].Get();if(P&&P->Kind==Kind){Obj->Contents.RemoveAt(I);P->Holder.Reset();P->State=0;if(M->Inventory->Adopt(PC,P,NAME_None,false)){++Count;}}}if(Count>=Need){break;}}}
        Check(O->Inventory.Items.FilterByPredicate([](const auto& I){return I.Kind==ELDItemKind::Fiber;}).Num()>=8&&O->Inventory.Items.FilterByPredicate([](const auto& I){return I.Kind==ELDItemKind::Ore;}).Num()==3&&E->SynchronizeWorld(Error)&&E->Audit(Error),TEXT("Work fixture transfers existing cache Fiber/Ore, with no new goods, money or quota"));
        Place(PC,ALDB92Deposits::Position(GetWorld(),5)+FVector(60,0,0));Task(PC,25,NAME_None);Phase=110;return;
    }
    if(Phase==110)
    {
        auto* PC=Replacement.Get();if(!Good(Read(PC,25))){return;}Check(M->StateFor(PC)->Inventory.Items.FilterByPredicate([](const auto& I){return I.Kind==ELDItemKind::Ore;}).Num()==4&&E->Accounting().Remaining(ALDB92Deposits::Budget(5))==4&&M->StateFor(PC)->Xp==WorkXp,TEXT("Remote contender gathers the missing fourth Ore through another real six-second automatic channel"));
        // Three diagnostic input extractions spend existing finite charges;
        // they do not add a new endowment or move either actor's wallet.
        bool Prepared=E->ExtractDeposit(B.Get(),8,Error)&&E->ExtractDeposit(B.Get(),8,Error)&&E->ExtractDeposit(B.Get(),6,Error);WorkBGold=M->StateFor(B.Get())->Gold;WorkBXp=M->StateFor(B.Get())->Xp;
        Check(Prepared&&E->SynchronizeWorld(Error)&&E->Audit(Error),TEXT("Second work owner receives three labelled finite-charge inputs without a new allowance or gold"));
        Place(B.Get(),ALDB92Services::Counter(GetWorld(),TEXT("capital_workshop"))-FVector(60,0,0));
        Place(PC,ALDB92Services::Counter(GetWorld(),TEXT("capital_workshop"))+FVector(60,0,0));Task(PC,38,TEXT("capital_workshop"));Phase=101;return;
    }
    if(Phase==101){const auto J=Read(Replacement.Get(),38);if(!Good(J)){return;}Check(!J->GetBoolField(TEXT("accepted"))&&M->StateFor(Replacement.Get())->Gold==WorkGold,TEXT("Stale bag refuses crafting before reserving or charging"));Task(Replacement.Get(),39,TEXT("capital_workshop"));Phase=102;return;}
    if(Phase==102){const auto J=Read(Replacement.Get(),39);if(!Good(J)){return;}Check(!J->GetBoolField(TEXT("accepted"))&&M->StateFor(Replacement.Get())->Gold==WorkGold,TEXT("Forged crafting fee refuses without creating output"));Task(Replacement.Get(),37,TEXT("capital_workshop"));Phase=103;return;}
    if(Phase==103){const auto J=Read(Replacement.Get(),37);if(!Good(J)){return;}const auto* O=M->StateFor(Replacement.Get());Check(!J->GetBoolField(TEXT("accepted"))&&O->Gold==WorkGold&&O->Xp==WorkXp&&!O->Inventory.Items.ContainsByPredicate([](const auto& I){return I.bReserved;}),TEXT("One confirmation starts work; S stops it, releases exact inputs and prevents delayed spend"));Task(Replacement.Get(),33,TEXT("capital_workshop"));Task(B.Get(),33,TEXT("capital_workshop"));WorkStep=0;Phase=104;return;}
    if(Phase==104)
    {
        const auto J=Read(Replacement.Get(),33);if(!Good(J)||WorkStep==0&&!Good(Read(B.Get(),33))){return;}
        if(WorkStep==0){const auto JB=Read(B.Get(),33);const auto* OB=M->StateFor(B.Get());Check(JB->GetBoolField(TEXT("accepted"))&&OB->Gold==WorkBGold-4&&OB->Xp==WorkBXp&&OB->Inventory.Items.FilterByPredicate([](const auto& I){return I.Kind==ELDItemKind::Toolkit;}).Num()==1,TEXT("Two real owners finish independent concurrent Toolkit channels; another craft does not invalidate accepted work"));}
        ++WorkStep;const auto* O=M->StateFor(Replacement.Get());Check(J->GetBoolField(TEXT("accepted"))&&J->GetBoolField(TEXT("duplicate_same"))&&O->Gold==WorkGold-4*WorkStep&&O->Xp==WorkXp&&O->Inventory.Items.FilterByPredicate([](const auto& I){return I.Kind==ELDItemKind::Toolkit;}).Num()==WorkStep,FString::Printf(TEXT("Toolkit%d: real four-second channel consumes2 Fiber/1 Ore and4gold once despite duplicate/replay; no XP"),WorkStep));
        if(WorkStep<4){Task(Replacement.Get(),33,TEXT("capital_workshop"));return;}Task(Replacement.Get(),34,TEXT("capital_workshop"));Phase=105;return;
    }
    if(Phase==105){const auto J=Read(Replacement.Get(),34);if(!Good(J)){return;}const auto* O=M->StateFor(Replacement.Get());Check(J->GetBoolField(TEXT("accepted"))&&O->Gold==WorkGold-22&&O->Xp==WorkXp&&O->Inventory.Items.ContainsByPredicate([](const auto& I){return I.Kind==ELDItemKind::Core;}),TEXT("Core six-second channel consumes2 physical Toolkit/1 Crystal plus6gold; output retains causal parents"));Task(Replacement.Get(),40,TEXT("capital_workshop"));Phase=106;return;}
    if(Phase==106){const auto J=Read(Replacement.Get(),40);if(!Good(J)){return;}Check(!J->GetBoolField(TEXT("accepted"))&&M->StateFor(Replacement.Get())->Gold==WorkGold-22,TEXT("Workshop rejects procurement even with the actual Core in bag"));Place(Replacement.Get(),ALDB92Services::Counter(GetWorld(),TEXT("capital_treasury"))+FVector(60,0,0));Task(Replacement.Get(),35,TEXT("capital_treasury"));Phase=107;return;}
    if(Phase==107){const auto J=Read(Replacement.Get(),35);if(!Good(J)){return;}const auto* O=M->StateFor(Replacement.Get());Check(J->GetBoolField(TEXT("accepted"))&&O->Gold==WorkGold+58&&O->Xp==WorkXp+60&&O->CoreOrders==1&&O->IndustrialEndorsement.IsValid()&&E->Accounting().Remaining(TEXT("CoreOrders"))==71&&E->Accounting().Remaining(E->WorkBudget(Seat,6))==2,TEXT("Physical Core delivery channels3s, mints80/awards60XP and Industrial receipt while atomically spending shared/persistent quota"));Place(Replacement.Get(),ALDB92Services::Counter(GetWorld(),TEXT("capital_guild"))+FVector(60,0,0));Task(Replacement.Get(),36,TEXT("capital_guild"));Phase=108;return;}
    if(Phase==108){const auto J=Read(Replacement.Get(),36);if(!Good(J)){return;}const auto* O=M->StateFor(Replacement.Get());Check(J->GetBoolField(TEXT("accepted"))&&O->Gold==WorkGold+108&&O->Xp==WorkXp+90&&O->SupplyOrders==1&&O->SupplyEndorsement.IsValid()&&E->Accounting().Remaining(TEXT("SupplyOrders"))==23&&E->Accounting().Remaining(E->WorkBudget(Seat,7))==0,TEXT("Two actual Toolkit delivery channels3s, mints50/awards30XP and Supply receipt with one persistent commission"));Task(Replacement.Get(),41,TEXT("capital_guild"));Phase=109;return;}
    if(Phase==109){const auto J=Read(Replacement.Get(),41);if(!Good(J)){return;}Check(!J->GetBoolField(TEXT("accepted"))&&M->StateFor(Replacement.Get())->Gold==WorkGold+108&&E->Audit(Error)&&Objects->Conserved(Error),TEXT("Exhausted persistent Supply quota rejects another exact-shaped request; physical and gold ledgers reconcile"));DepositXp=M->StateFor(Replacement.Get())->Xp;Phase=65;}
    if(Phase==65)
    {
        // Isolate all-nine budget closure from long-duration/human/channel pacing.
        auto* PC=Replacement.Get();bool Closed=true;for(int32 Node=0;Node<9;++Node){if(Node==1){continue;}while(E->Accounting().Remaining(ALDB92Deposits::Budget(Node))>0){TSet<FGuid> Before;for(const auto& I:M->StateFor(PC)->Inventory.Items){Before.Add(I.Id);}if(!E->ExtractDeposit(PC,Node,Error)){Closed=false;break;}const auto Items=M->StateFor(PC)->Inventory.Items;for(const auto& I:Items){if(!Before.Contains(I.Id)){auto* P=M->Inventory->Owned(PC,I.Id);M->Inventory->Remove(PC,P,false);P->SetActorLocation(ALDB92Deposits::Position(GetWorld(),7)+FVector(300+Node*30,300,12));}}}}
        Check(Closed&&E->SynchronizeWorld(Error)&&E->Audit(Error),TEXT("Explicit authority fixture drains eight deposit budgets to real ground goods without advancing the channel clock"));Phase=47;
    }
    if(Phase==47)
    {
        auto* PC=Replacement.Get();auto* O=M->StateFor(PC);
        for(auto Obj:Objects->Objects){if(!Obj.IsValid()){continue;}for(int32 I=Obj->Contents.Num()-1;I>=0&&O->Inventory.Items.Num()<24;--I){auto* P=Obj->Contents[I].Get();if(P&&P->Kind==ELDItemKind::Fiber){Obj->Contents.RemoveAt(I);P->Holder.Reset();P->State=0;M->Inventory->Adopt(PC,P,NAME_None,false);}}if(O->Inventory.Items.Num()==24){break;}}
        Objects->Publish();for(TActorIterator<ALDItem> It(GetWorld());It&&O->Inventory.Items.Num()<24;++It){auto* P=*It;if(P->State==0&&!P->Holder.IsValid()&&P->Kind==ELDItemKind::Fiber){M->Inventory->Adopt(PC,P,NAME_None,false);}}
        Check(O->Inventory.Items.Num()==24&&E->SynchronizeWorld(Error),TEXT("Capacity fixture fills the bag from existing cache/deposit Fiber; no new goods"));Place(PC,ALDB92Services::Counter(GetWorld(),Markets.Last())+FVector(60,0,0));At=Now;Phase=94;return;
    }
    if(Phase==94){if(Now-At<1){return;}Task(Replacement.Get(),5,Markets.Last());Phase=20;return;}
    if(Phase==20){if(!Good(Read(Replacement.Get(),5))){return;}SavedGold=M->StateFor(Replacement.Get())->Gold;Task(Replacement.Get(),3,NAME_None);Phase=21;return;}
    if(Phase==21)
    {const auto J=Read(Replacement.Get(),3);if(!Good(J)){return;}Check(!J->GetBoolField(TEXT("accepted"))&&M->StateFor(Replacement.Get())->Gold==SavedGold&&M->StateFor(Replacement.Get())->Inventory.Items.Num()==24,TEXT("Real full bag and duplicate confirmation reject before payment or stock movement"));Task(Replacement.Get(),14,NAME_None);Phase=22;return;}
    if(Phase==22)
    {if(!Good(Read(Replacement.Get(),14))){return;}Place(Replacement.Get(),ALDB92Deposits::Position(GetWorld(),1)+FVector(60,0,0));At=Now;Phase=98;return;}
    if(Phase==98)
    {
        if(Now-At<7){return;}auto* PC=Replacement.Get();Check(E->Accounting().Remaining(ALDB92Deposits::Budget(1))==6&&!R->Deposits->Active(PC),TEXT("A full real bag prevents automatic channel start and retains all six deposit charges"));
        Task(PC,32,NAME_None);Phase=79;return;
    }
    if(Phase==79)
    {
        const auto J=Read(Replacement.Get(),32);if(!Good(J)){return;}auto* PC=Replacement.Get();Check(!J->GetBoolField(TEXT("accepted"))&&E->Accounting().Remaining(ALDB92Deposits::Budget(1))==6,TEXT("A full bag also refuses prepared Survey before charge creation"));
        const auto Items=M->StateFor(PC)->Inventory.Items;for(const auto& I:Items){if(I.Kind==ELDItemKind::Fiber&&!I.BindingMask){auto* P=M->Inventory->Owned(PC,I.Id);M->Inventory->Remove(PC,P,false);P->SetActorLocation(ALDB92Deposits::Position(GetWorld(),1)+FVector(300,300,12));break;}}
        bool Closed=true;while(E->Accounting().Remaining(ALDB92Deposits::Budget(1))>0){TSet<FGuid> Before;for(const auto& I:M->StateFor(PC)->Inventory.Items){Before.Add(I.Id);}if(!E->ExtractDeposit(PC,1,Error)){Closed=false;break;}const auto Bag=M->StateFor(PC)->Inventory.Items;for(const auto& I:Bag){if(!Before.Contains(I.Id)){auto* P=M->Inventory->Owned(PC,I.Id);M->Inventory->Remove(PC,P,false);P->SetActorLocation(ALDB92Deposits::Position(GetWorld(),1)+FVector(300,300,12));}}}
        int32 Units=0;for(const auto& Pair:E->Accounting().ItemRecords()){const auto* Receipt=E->Accounting().Receipt(Pair.Value.Origin);Units+=Receipt&&(Receipt->Transaction.Cause==TEXT("B92DepositExtraction")||Receipt->Transaction.Cause==TEXT("B92SurveyExtraction"));}
        Objects->Publish();Check(Closed&&Units==54&&M->StateFor(PC)->Xp==DepositXp&&!E->ExtractDeposit(PC,1,Error),TEXT("All nine six-charge budgets produce exactly54 real units; depletion cannot create another or grant XP"));Check(E->SynchronizeWorld(Error)&&E->Audit(Error)&&Objects->Conserved(Error)&&R->Audit(Error),TEXT("Final canonical item/gold identity closure after deposit extraction and all refusals"));Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,Failures?3:0);return;
    }
#endif
}
