#include "Tests/LDB92RosterProbe.h"
#include "Population/LDB92Roster.h"
#include "Economy/LDB92Economy.h"
#include "Combat/LDCombatDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "World/LDWorldObjects.h"
#include "World/LDPortableTools.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

ALDB92RosterProbe::ALDB92RosterProbe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDB92RosterProbe::Check(bool Good,const FString& Name)
{auto J=MakeShared<FJsonObject>();J->SetBoolField(TEXT("passed"),Good);J->SetStringField(TEXT("case"),Name);Checks.Add(MakeShared<FJsonValueObject>(J));Failures+=!Good;UE_LOG(LogTemp,Display,TEXT("LD_B92_ROSTER_PROBE passed=%d %s"),Good,*Name);}
void ALDB92RosterProbe::Finish()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* R=M->Population.Get();FString Output;FParse::Value(FCommandLine::Get(),TEXT("B92Output="),Output);
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("stage"),TEXT("B92"));J->SetStringField(TEXT("scope"),TEXT("native_population_admission_and_lifecycle"));J->SetBoolField(TEXT("full_stage_acceptance"),false);J->SetBoolField(TEXT("completed"),true);J->SetNumberField(TEXT("failures"),Failures);J->SetNumberField(TEXT("human_capacity"),R?R->HumanLimit():0);J->SetArrayField(TEXT("checks"),Checks);
    TArray<TSharedPtr<FJsonValue>> Rows;
    if(R){for(const auto& P:R->People()){auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("source"),P.Source.ToString());V->SetStringField(TEXT("id"),P.Id.ToString());V->SetNumberField(TEXT("role"),uint8(P.Role));V->SetStringField(TEXT("body"),P.Body.IsValid()?P.Body->GetName():TEXT("Missing"));V->SetStringField(TEXT("home"),P.Home.ToString());Rows.Add(MakeShared<FJsonValueObject>(V));}}
    J->SetArrayField(TEXT("population"),Rows);Rows.Reset();
    if(R){for(const auto& S:R->Seats()){auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("id"),S.Id.ToString());V->SetBoolField(TEXT("human"),S.Human);auto* PC=S.Controller.Get();const auto* O=M->StateFor(PC);V->SetNumberField(TEXT("life"),O?O->Life:0);V->SetStringField(TEXT("body"),PC&&PC->GetPawn()?PC->GetPawn()->GetName():TEXT("Missing"));Rows.Add(MakeShared<FJsonValueObject>(V));}}
    J->SetArrayField(TEXT("seats"),Rows);FString Json;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Json));const bool Saved=FFileHelper::SaveStringToFile(Json,*(Output/TEXT("Roster.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    bool LedgerSaved=false;if(R&&R->Economy){LedgerSaved=FFileHelper::SaveStringToFile(R->Economy->Accounting().ExportJson(TEXT("B92RosterLifecycle")),*(Output/TEXT("Ledger.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);}
    Phase=99;FPlatformMisc::RequestExitWithStatus(false,Failures||!Saved||!LedgerSaved?3:0);
}
void ALDB92RosterProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(Phase==99){return;}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* R=M?M->Population.Get():nullptr;
    if(!Began){Began=GetWorld()->GetTimeSeconds();}if(!R||!R->Ready){if(GetWorld()->GetTimeSeconds()-Began>120){Check(false,TEXT("Roster startup timed out"));Finish();}return;}
    FString Error;auto* E=R->Economy.Get();auto* C=M->Combat.Get();auto* Objects=ALDWorldObjects::Get(GetWorld());
    auto Bodies=[&](){int32 N=0;for(TActorIterator<ALDCharacter> It(GetWorld());It;++It){if(!It->IsActorBeingDestroyed()){++N;}}return N;};
    if(Phase==0)
    {
        Check(M->IsPopulationProfile()&&!M->Economy&&!M->WorldChecks&&!M->WorldRuntime,TEXT("Explicit population review excludes legacy economy, lazy purses and victory rewards"));
        Check(R->Audit(Error)&&R->Seats().Num()==24&&R->People().Num()==196&&Bodies()==220,TEXT("220 actual native bodies:196 noncontenders and24 contenders"));
        Check(!R->GetIsReplicated()&&E&&!E->GetIsReplicated(),TEXT("Roster identities/role/control labels and full ledger stay server-only"));
        Check(R->Humans()==0&&E->Accounting().TotalGold()==3288&&E->Accounting().Minted()==0,TEXT("Unoccupied human capacity is controlled by24 AI seats without extra endowments"));
        int32 Counts[5]={};TSet<FGuid> Ids;bool Roles=true;
        for(const auto& P:R->People()){++Counts[uint8(P.Role)];Ids.Add(P.Id);Roles&=P.Body.IsValid()&&!P.Body->GetIsReplicated()&&!P.Body->IsA<ALDResident>();if(P.Role==ELDB92Role::Civilian){Roles&=E->Accounting().Account(E->Wallet(P.Id))->Gold==3;}if(P.Role==ELDB92Role::Official){Roles&=C->StateFor(P.Body.Get())->ProtectionUntil==MAX_dbl;}}
        Check(Counts[0]==96&&Counts[1]==32&&Counts[2]==24&&Counts[3]==20&&Counts[4]==24&&Ids.Num()==196&&Roles,TEXT("Exact role capacities and96 three-gold purses; no legacy wandering/lazy purse or duplicate identity"));
        bool Starts=true;
        for(const auto& S:R->Seats()){const auto* O=M->StateFor(S.Controller.Get());Starts&=O&&O->Gold==0&&O->BankGold==0&&O->Inventory.bInitialized&&O->Inventory.Items.IsEmpty()&&O->Inventory.Bindings.Num()==11&&O->Build.Actions.Num()==6&&O->Build.Level==1&&O->WardCharges==0;}
        Check(Starts,TEXT("All24 native contender starts retain the audited empty kit and normal six/eleven preparation"));
        const auto Before=E->Accounting().ExportJson(TEXT("Same"));
        TArray<ALDPlayerController*> Humans;
        for(int32 I=0;I<R->HumanLimit();++I)
        {
            const auto Id=R->Contenders[I].Id;auto* Body=R->Contenders[I].Body.Get();const auto Life=C->StateFor(Body)->Identity;
            auto* PC=GetWorld()->SpawnActor<ALDB92Controller>();Humans.Add(PC);
            Check(PC&&R->Admit(PC,Error)&&M->ContenderId(PC)==Id&&PC->GetPawn()==Body&&C->StateFor(Body)->Identity==Life,FString::Printf(TEXT("Human admission%d takes the existing ID/body/life"),I+1));
            const uint32 Revision=PC->OwnerState.Revision;Check(R->Admit(PC,Error)&&PC->OwnerState.Revision==Revision,TEXT("Repeated admission neither rekeys nor grants again"));
        }
        Check(R->Humans()==R->HumanLimit()&&24-R->Humans()==(R->HumanLimit()==4?20:12)&&Bodies()==220,TEXT("Configured4/12 humans replace20/12 AI; no double contender/body count"));
        Check(Before==E->Accounting().ExportJson(TEXT("Same"))&&R->Audit(Error),TEXT("All admissions leave every account, stock item, budget and receipt untouched"));
        auto* Extra=GetWorld()->SpawnActor<ALDB92Controller>();Check(!R->Admit(Extra,Error)&&!M->StateFor(Extra)&&Bodies()==220,TEXT("Overcapacity admission rejects before state/body/endowment allocation"));Extra->Destroy();
        auto* Human=Humans[0];Seat=M->ContenderId(Human);Original=Cast<ALDCharacter>(Human->GetPawn());LifeBody=C->StateFor(Original.Get())->Identity;
        auto* O=M->StateFor(Human);
        // Explicit conserved funding and progression/source preparation fixtures;
        // these are not work/market/learning acceptance or normal earnings.
        LDEconomy::FTransaction Fund;Fund.Id=FGuid::NewGuid();Fund.Cause=TEXT("DiagnosticFixtureTransfer");Fund.Gold={{TEXT("capital"),E->Ledger.Account(TEXT("capital"))->Revision,-30},{E->Wallet(Seat),1,20},{E->Bank(Seat),1,10}};
        Check(E->Ledger.Commit(Fund,Error),TEXT("Fixture moves30 existing reserve gold into wallet20/bank10 without mint"));O->Gold=20;O->BankGold=10;M->AwardExperience(Human,135);O=M->StateFor(Human);O->CoreOrders=1;O->SupplyOrders=1;O->Witness=FGuid::NewGuid();O->Build.Cooldowns[uint8(ELDSkill::Heave)]=GetWorld()->GetTimeSeconds()+60;
        auto* Peer=M->StateFor(Humans[1]);O->Gold=19;Peer->Gold=1;M->RecordEconomyGold(TEXT("PeerTransfer"));O->Gold=20;Peer->Gold=0;M->RecordEconomyGold(TEXT("PeerTransfer"));
        Check(E->Ledger.Account(E->Wallet(Seat))->Gold==20&&E->Ledger.Minted()==0&&E->Ledger.Burned()==0&&E->Audit(Error)&&R->RecordGold(TEXT("PeerTransfer"),0,0,Error),TEXT("Inherited peer accounting transfers existing gold and accepts item-only exchange without a tax or mint"));
        ALDItem* Torch=nullptr;for(auto Obj:Objects->Objects){if(Obj.IsValid()&&Obj->Kind==ELDObjectKind::Torch&&Obj->Physical.IsValid()){Torch=Obj->Physical.Get();break;}}
        Check(Torch&&M->Inventory->Adopt(Human,Torch,NAME_None,false),TEXT("Diagnostic source preparation takes one existing finite Torch rather than creating an item"));
        if(Torch){Tool=Torch->Identity;Torch->FuelMs=42000;Torch->BindingMask=1<<7;O->Inventory.Bindings[7]=Tool;for(auto& Entry:O->Inventory.Items){if(Entry.Id==Tool){Torch->Project(Entry);}}M->Inventory->Changed(Human);}
        for(auto Obj:Objects->Objects){if(Obj.IsValid()&&Obj->Kind==ELDObjectKind::Crate){Heavy=Obj;break;}}
        if(Heavy.IsValid())
        {ALDWorldObjects::FCargo Binding;Binding.Object=Heavy;Binding.Life=O->Life;Binding.Damage=C->StateFor(Original.Get())->LastDamage;Objects->Cargo.Add(Cast<ALDB91Controller>(Human),Binding);Heavy->Carrier=Cast<ALDB91Controller>(Human);Heavy->bCarriedWorld=true;Heavy->Apply(Heavy->GetActorLocation(),Heavy->Flags(),Heavy->Revision+1);}
        Check(Heavy.IsValid()&&Objects->Carried(Cast<ALDB91Controller>(Human))==Heavy.Get(),TEXT("Diagnostic cargo preparation reserves one existing crate, not a bag clone"));
        const auto Snapshot=*M->StateFor(Human);const double Cooldown=Snapshot.Build.Cooldowns[uint8(ELDSkill::Heave)];
        C->StateFor(Original.Get())->LastCoreToken=1000000;C->StateFor(Original.Get())->CancelledCoreThrough=1000001;Human->RaiseCombatTokenFloor(1000002);
        Human->Destroy();auto* AI=R->Contenders[0].Controller.Get();const auto* After=M->StateFor(AI);
        Check(AI&&AI->IsA<ALDB92SeatController>()&&After&&M->ContenderId(AI)==Seat&&AI->GetPawn()==Original.Get()&&C->StateFor(Original.Get())->Identity==LifeBody,TEXT("Controller destruction takes over the same body before native PawnLeavingGame can delete it"));
        Check(After&&After->Gold==20&&After->BankGold==10&&After->Xp==135&&After->Build.Level==2&&After->Life==Snapshot.Life&&After->Witness==Snapshot.Witness&&After->CoreOrders==1&&After->SupplyOrders==1&&After->Build.Cooldowns[uint8(ELDSkill::Heave)]==Cooldown,TEXT("AI takeover retains funds, bank, XP, build, life, cooldown and work/proof counters"));
        Check(Torch&&Torch->Holder==AI&&Torch->FuelMs==42000&&After&&After->Inventory.Find(Tool)&&After->Inventory.Bindings[7]==Tool&&Objects->Conserved(Error),TEXT("One exact bound Torch/fuel and its physical holder follow the persistent seat"));
        Check(Heavy.IsValid()&&Heavy->Carrier==AI&&Heavy->bCarriedWorld&&Objects->Carried(Cast<ALDB91Controller>(AI))==Heavy.Get()&&!Objects->Carried(Cast<ALDB91Controller>(Human)),TEXT("Existing carried crate follows the same body without requesting a new ground pad"));
        Check(AI&&AI->NextCombatToken()==1000003&&C->StateFor(Original.Get())->LastCoreToken==1000000&&C->StateFor(Original.Get())->CancelledCoreThrough>=1000001,TEXT("New controller advances above existing body input/cancellation watermarks without rewinding them"));
        Check(!M->StateFor(Human)&&!M->ContenderId(Human).IsValid()&&R->Humans()==R->HumanLimit()-1,TEXT("Retired controller cannot authorize old requests or create a second account"));
        const auto Stable=E->Accounting().ExportJson(TEXT("Same"));Check(!R->Release(Human,Error)&&E->Accounting().ExportJson(TEXT("Same"))==Stable,TEXT("Repeated disconnect/logout cannot grant, retire or tax the seat twice"));
        if(!After||!AI){Finish();return;}
        C->Die(Original.Get(),GetWorld()->GetTimeSeconds());After=M->StateFor(AI);
        Check(After&&!After->bActive&&After->Gold==17&&After->BankGold==10&&After->ReturnAt>GetWorld()->GetTimeSeconds(),TEXT("Actual contender death path burns ceiling15% wallet and schedules native return"));
        Check(E->Accounting().Burned()==3&&E->Accounting().TotalGold()==3285&&E->Audit(Error),TEXT("Death tax commits once to the same full-profile wallet without touching protected bank"));
        auto* NewHuman=GetWorld()->SpawnActor<ALDB92Controller>();const double Due=After?After->ReturnAt:0;const uint32 Life=After?After->Life:0;
        Check(R->Admit(NewHuman,Error)&&M->ContenderId(NewHuman)==Seat&&!NewHuman->GetPawn()&&!NewHuman->OwnerState.bActive&&NewHuman->OwnerState.ReturnAt==Due&&NewHuman->OwnerState.Life==Life,TEXT("A waiting-life takeover retains the exact return deadline and grants no early body"));
        ReturnedHuman=NewHuman;Phase=1;
    }
    else if(Phase==1)
    {
        auto* PC=ReturnedHuman.Get();const auto* O=M->StateFor(PC);if(!PC||!O){Check(false,TEXT("Waiting seat retains its controller/state"));Finish();return;}if(!O->bActive||!PC->GetPawn()){if(GetWorld()->GetTimeSeconds()-Began>90){Check(false,TEXT("Native return timed out"));Finish();}return;}
        auto* Body=Cast<ALDCharacter>(PC->GetPawn());
        Check(Body&&Body!=Original.Get()&&C->StateFor(Body)->Identity!=LifeBody&&O->Life==2,TEXT("Ordinary return creates one new body life while retaining contender identity"));
        Check(O->Gold==17&&O->BankGold==10&&O->Xp==135&&O->CoreOrders==1&&O->SupplyOrders==1&&O->Witness.IsValid()&&!O->Inventory.Find(Tool),TEXT("Return retains money/progression/counters; dropped tool is not granted again"));
        ALDItem* Torch=nullptr;for(TActorIterator<ALDItem> It(GetWorld());It;++It){if(It->Identity==Tool){Torch=*It;break;}}
        Check(Torch&&Torch->State==0&&!Torch->Holder.IsValid()&&Torch->FuelMs==42000&&!Torch->BindingMask&&Objects->Conserved(Error),TEXT("Death drops one original Torch with spent fuel and no restored binding"));
        Check(Heavy.IsValid()&&!Heavy->Carrier.IsValid()&&!Heavy->bCarriedWorld&&!Objects->Carried(Cast<ALDB91Controller>(PC)),TEXT("Death safely releases the same carried crate without a second instance"));
        Check(Bodies()==220&&R->Humans()==R->HumanLimit()&&R->Audit(Error),TEXT("After death/return, exactly220 bodies and24 stable economic seats remain"));
        Check(E->Accounting().Minted()==0&&E->Accounting().Burned()==3&&E->Accounting().Remaining(TEXT("CoreOrders"))==72&&E->Accounting().Remaining(TEXT("SupplyOrders"))==24,TEXT("Admission/takeover/return never refresh global quotas or mint rewards"));
        Finish();
    }
#endif
}
