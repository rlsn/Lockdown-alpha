#include "Tests/LDTacticalProbe.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDTacticalCatalog.h"
#include "Character/LDCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/LDNavigationComponent.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDTradeDirector.h"
#include "Economy/LDEconomyDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "Region/LDRegionDefinition.h"
#include "AIController.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

ALDTacticalProbe::ALDTacticalProbe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDTacticalProbe::BeginPlay()
{
    Super::BeginPlay();Started=At=FPlatformTime::Seconds();FParse::Value(FCommandLine::Get(),TEXT("LDProbeRole="),Role);if(Role.IsEmpty()){Role=TEXT("Server");}
    if(GetNetMode()==NM_Client){CastChecked<ALDPlayerController>(GetOwner())->ServerProbeHello(Role==TEXT("A")?1:2);}
}
void ALDTacticalProbe::Check(bool Value,const FString& Message){Checks.Add((Value?TEXT("PASS "):TEXT("FAIL "))+Message);if(!Value){++Failures;}UE_LOG(LogTemp,Display,TEXT("LD_B71_PROBE %s"),*Checks.Last());}
void ALDTacticalProbe::Enter(int32 Next)
{
    Phase=Next;At=FPlatformTime::Seconds();Checked=false;
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* C=M->Combat.Get();const double Now=GetWorld()->GetTimeSeconds();
    for(auto* PC:{A.Get(),B.Get()})
    {
        auto* O=M->StateFor(PC);auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());
        PC->Navigation->ResetLife();Body->SetActorLocation(PC==A.Get()?FVector(-1800,-900,92):FVector(-1600,-900,92),false,nullptr,ETeleportType::TeleportPhysics);
        Body->GetCharacterMovement()->StopMovementImmediately();Body->GetCharacterMovement()->SetMovementMode(MOVE_None);
        auto* S=C->StateFor(Body);C->CancelTactical(Body,TEXT("Fixture phase reset"));S->Action=0;S->Tactical=LDTactical::FState();S->Tactical.LastRegen=LDTactical::Milliseconds(Now);
        O->Build=LDDevelopment::TacticalStart(0,M->TacticalCatalog->Checksum);O->Build.Revision=100+Next;
        for(auto Skill:{ELDSkill::ArcBolt,ELDSkill::PurgeTouch,ELDSkill::AnchorBash,ELDSkill::SuretyShell}){O->Build.Ranks[uint8(Skill)]=1;}
        O->Build.Slots={Next==1?ELDSkill::ArcBolt:Next==5||Next==6?ELDSkill::AnchorBash:ELDSkill::PurgeTouch,Next==5||Next==6?ELDSkill::ArcBolt:ELDSkill::AnchorBash,ELDSkill::Heave,ELDSkill::Patch,ELDSkill::SuretyShell};
        Check(LDDevelopment::Valid(O->Build),TEXT("Fixture loadout obeys real shared-slot validation"));
        O->ProtectedUntil=S->ProtectionUntil=0;O->NullBellReadyAt=S->NullBellReadyAt=0;S->CoreRecoveryUntil=0;S->LastDamage=S->LastDealt=-1000;
        O->Hp=O->MaxHp=S->Hp=LDDevelopment::MaxHP(O->Build);O->Mp=O->MaxMp=S->Tactical.MP=LDTactical::MaxMP(O->Build);
        M->SyncOwner(PC);
    }
    auto* SA=C->StateFor(Cast<ALDCharacter>(A->GetPawn()));auto* SB=C->StateFor(Cast<ALDCharacter>(B->GetPawn()));
    if(Next==2||Next==3||Next==4)
    {
        const auto Status=Next==2?LDTactical::EStatus::Silence:Next==3?LDTactical::EStatus::Mute:LDTactical::EStatus::Stun;
        C->ApplyStatus(Cast<ALDCharacter>(A->GetPawn()),Status,LDTactical::Milliseconds(Now)+8000,FGuid::NewGuid());
        auto* O=M->StateFor(A.Get());bool Has=false;for(const auto& I:O->Inventory.Items){if(I.Kind==ELDItemKind::NullBell){O->Inventory.Quick[0]=I.Id;Has=true;break;}}
        if(!Has){auto* Item=GetWorld()->SpawnActor<ALDItem>();Item->Kind=ELDItemKind::NullBell;Item->State=0;Check(M->Inventory->Adopt(A.Get(),Item,TEXT("DiagnosticItemCreation")),TEXT("Labelled fixture creates one journalled Null Bell"));FString Why;LDInventory::BindQuick(O->Inventory,Item->Identity,0,Why);M->Inventory->Changed(A.Get());}
    }
    if(Next==5){SB->Tactical.BlockUntil=LDTactical::Milliseconds(Now)+8000;LDTactical::Barrier(SB->Tactical,140,650,LDTactical::Milliseconds(Now)+8000,LDTactical::Milliseconds(Now));}
    if(Next==6){LDTactical::Barrier(SB->Tactical,140,650,LDTactical::Milliseconds(Now)+8000,LDTactical::Milliseconds(Now));}
    if(Next==7)
    {
        B->GetPawn()->SetActorLocation(FVector(-1720,-900,92),false,nullptr,ETeleportType::TeleportPhysics);
        Check(M->Trade->PairEligible(A.Get(),B.Get()),TEXT("Actual native trade admission remains legal in v07"));
        Check(C->Allows(Cast<ALDCharacter>(A->GetPawn()),LDTactical::EAction::Service),TEXT("Uncontrolled institutional action remains legal"));
        // An earned-mint/paid purchase fixture uses real ledger commits; not a normal journey.
        auto* O=M->StateFor(A.Get());O->Gold+=25;M->RecordEconomyGold(TEXT("CourierReward"),25);
        FGuid Received;FString Why;const auto Quote=M->Economy->CommerceQuote(0);
        Check(M->Economy->CommitCommerce(A.Get(),0,Quote.Revision,1,ELDItemKind::NullBell,FGuid(),0,O->Inventory.Revision,8,Received,Why),TEXT("Official fixed-price Null Bell purchase burns 8 and creates one instance"));
        FString Audit;Check(M->Economy->Audit(Audit),TEXT("Actual economy reconciles after fixture purchase/use"));
        Check(M->Economy->AuditFailures==0,TEXT("No accounting audit failures"));
    }
    A->ClientProbePhase(uint8(Next));B->ClientProbePhase(uint8(Next));
}
void ALDTacticalProbe::ServerTick()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M||!M->Combat){return;}
    if(!Phase)
    {
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){auto* PC=Cast<ALDPlayerController>(It->Get());if(PC&&PC->GetPawn()){if(PC->ProbeRole==1){A=PC;}if(PC->ProbeRole==2){B=PC;}}}
        if(!A.IsValid()||!B.IsValid()){return;}
        FString Why;M->SetLobbyChoice(A.Get(),0,true,Why);M->SetLobbyChoice(B.Get(),0,true,Why);
        if(!M->StartDevelopmentMatch(A.Get(),Why)&&!M->StartDevelopmentMatch(B.Get(),Why)){Check(false,Why);Finish();return;}
        Check(M->IsTacticalProfile()&&M->StateFor(A.Get())->Build.RulesVersion==7,TEXT("Separate native v07 match starts with schema7"));
        Check(FMath::IsNearlyEqual(M->GetGameState<ALDGameState>()->MatchDeadline-M->StartedAt(),2100.,.01),TEXT("v07 deadline is 2100s"));
        for(TActorIterator<ALDResident> It(GetWorld());It;++It){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();if(auto* AI=Cast<AAIController>(It->GetController())){AI->StopMovement();}}
        M->Vision->SetPublicSensor(false);Enter(1);return;
    }
    const double Elapsed=FPlatformTime::Seconds()-At;
    if(Phase==7){if(Elapsed>2.5){Finish();}return;}
    if(!A.IsValid()||!B.IsValid()||!A->GetPawn()||!B->GetPawn()){Check(false,TEXT("Client disconnected before final phase"));Finish();return;}
    auto* C=M->Combat.Get();auto* SA=C->StateFor(Cast<ALDCharacter>(A->GetPawn()));auto* SB=C->StateFor(Cast<ALDCharacter>(B->GetPawn()));
    if(Phase==6&&!Checked&&Elapsed>1.05)
    {Checked=true;Check(SB->Tactical.Has(LDTactical::EStatus::Stun,LDTactical::Milliseconds(GetWorld()->GetTimeSeconds())),TEXT("Anchor Bash applies real stun through barrier"));Check(SB->Hp>649&&SB->Tactical.Barrier<139,TEXT("Physical mitigation/barrier before HP"));}
    if(Elapsed<2.5){return;}
    if(Phase==1){Check(SA->Tactical.MP>150&&SA->Tactical.MP<170,TEXT("Duplicate remote Arc Bolt spends MP once"));Check(SB->Hp<600,TEXT("Snapshot projectile deals actual magical damage"));}
    if(Phase==2){Check(!SA->Tactical.Has(LDTactical::EStatus::Silence,LDTactical::Milliseconds(GetWorld()->GetTimeSeconds())),TEXT("Silence blocks skill but permits Null Bell self cleanse"));Check(SA->Tactical.MP==180&&M->StateFor(A.Get())->Inventory.Items.FilterByPredicate([](const auto& I){return I.Kind==ELDItemKind::NullBell;}).IsEmpty(),TEXT("Silenced skill spends none; Bell consumes once"));}
    if(Phase==3){Check(!SA->Tactical.Has(LDTactical::EStatus::Mute,LDTactical::Milliseconds(GetWorld()->GetTimeSeconds())),TEXT("Mute blocks Bell but permits Purge Touch"));Check(SA->Tactical.MP<160&&M->StateFor(A.Get())->Inventory.Items.ContainsByPredicate([](const auto& I){return I.Kind==ELDItemKind::NullBell;}),TEXT("Muted item kept; accepted Purge costs real MP"));}
    if(Phase==4){Check(SA->Tactical.MP==180&&SA->Tactical.Has(LDTactical::EStatus::Stun,LDTactical::Milliseconds(GetWorld()->GetTimeSeconds())),TEXT("Stun blocks both skill and item without spending"));}
    if(Phase==5){Check(SB->Hp==650&&SB->Tactical.Barrier==140&&!SB->Tactical.BlockUntil,TEXT("Spell block consumes one charge and rejects entire Bash packet"));Check(SA->Tactical.MP<165,TEXT("Blocked packet does not refund release MP"));}
    Enter(Phase+1);
}
void ALDTacticalProbe::ClientTick()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwner());const double Now=FPlatformTime::Seconds();if(Now-LastPing>.15){LastPing=Now;PC->ServerProbePing(Now);}
    if(PC->ProbePhase!=Phase){Phase=PC->ProbePhase;At=Now;Sent=Checked=false;}if(!Phase){return;}
    const double Elapsed=Now-At;const bool IsA=Role==TEXT("A");const auto& O=PC->OwnerState;
    if(!Sent&&IsA&&Elapsed>.3)
    {
        FGuid Target;auto* R=PC->LocalView();if(R){for(const auto& E:R->LastView().Entities){if(E.Kind==ELDObservedKind::Body&&FVector::DistSquared2D(E.Position,FVector(-1600,-900,92))<100){Target=E.Id;break;}}}
        if((Phase==1||Phase==5||Phase==6)&&!Target.IsValid()){return;}
        const uint32 Token=uint32(1000+Phase*100);
        if(Phase==1||Phase==5||Phase==6)
        {PC->SendTacticalAction(Token,0,FVector::ForwardVector,Target);if(Phase==1){PC->SendTacticalAction(Token,0,FVector::ForwardVector,Target);FLDTacticalRequest Bad;Bad.Match=O.Match;Bad.Life=O.Life+1;Bad.Token=Token+1;Bad.BuildRevision=O.Build.Revision;Bad.CatalogDigest=O.Build.CatalogDigest;PC->ServerTacticalAction(Bad);}}
        if(Phase==2||Phase==3||Phase==4)
        {
            if(Phase==2){PC->SendTacticalAction(Token,0,FVector::ForwardVector,FGuid());}
            if(const auto* Item=O.Inventory.Find(O.Inventory.Quick[0])){PC->ServerInventoryRequest(O.Match,O.Life,Token+1,O.Inventory.Revision,LDInventory::Use,Item->Id,Item->Revision,0,FVector::ZeroVector);}
            if(Phase==3||Phase==4){PC->SendTacticalAction(Token+2,0,FVector::ForwardVector,FGuid());}
        }
        Sent=true;
    }
    if(!Checked&&Elapsed>1.9)
    {
        Checked=true;Check(O.Build.RulesVersion==7&&O.Build.CatalogDigest.Len()==64&&O.Build.Slots.Num()==5,TEXT("Owner receives versioned build and catalog digest"));Check(O.MaxMp==180&&O.Mp>=0&&O.Mp<=O.MaxMp,TEXT("Independent owning client receives bounded MP"));
        if(IsA&&Phase==1){Check(O.Mp<170&&O.Mp>150,TEXT("Remote MP confirms one release despite replay/stale life"));}
        if(IsA&&Phase==2){Check(!(O.StatusMask&(1<<1))&&O.Mp==180,TEXT("Own silence cleanse and no skill cost reconcile"));}
        if(IsA&&Phase==3){Check(!(O.StatusMask&(1<<2))&&O.Mp<160,TEXT("Own mute cleanse and skill MP reconcile"));}
        if(IsA&&Phase==4){Check((O.StatusMask&1)&&O.Mp==180,TEXT("Own stun feedback and blocked actions reconcile"));}
        if(!IsA&&Phase==1){Check(O.Hp<600,TEXT("Independent victim receives projectile damage"));}
    }
    if(Phase==7&&Elapsed>1.2){Finish();}
}
void ALDTacticalProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(FPlatformTime::Seconds()-Started>55){Check(false,TEXT("B71 network fixture timed out"));Finish();return;}
    if(GetNetMode()==NM_Client){ClientTick();}else{ServerTick();}
#endif
}
void ALDTacticalProbe::Finish()
{
    if(GetNetMode()==NM_Client)
    {auto* PC=CastChecked<ALDPlayerController>(GetOwner());auto Samples=PC->ProbeRtts;Samples.Sort();Check(Samples.Num()>=15,TEXT("Independent client transport echo measured"));if(!Samples.IsEmpty()){Check(Samples[Samples.Num()/2]>=20&&Samples[Samples.Num()/2]<350,FString::Printf(TEXT("Echo median %.1fms"),Samples[Samples.Num()/2]));}}
    TSharedRef<FJsonObject> Json=MakeShared<FJsonObject>();Json->SetNumberField(TEXT("failures"),Failures);Json->SetStringField(TEXT("scope"),TEXT("Controlled native B71 fixture; legal RPCs, diagnostic build/items/placement; not a normal earned route"));TArray<TSharedPtr<FJsonValue>> Lines;for(const auto& S:Checks){Lines.Add(MakeShared<FJsonValueString>(S));}Json->SetArrayField(TEXT("checks"),Lines);
    FString Directory,Output;FParse::Value(FCommandLine::Get(),TEXT("LDProbeReportDir="),Directory);FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Output));
    if(GetNetMode()!=NM_Client){auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(M&&M->Economy){if(!FFileHelper::SaveStringToFile(M->Economy->Accounting().ExportJson(TEXT("B71Fixture")),*(Directory/TEXT("Ledger.json")))){++Failures;}}}
    if(!FFileHelper::SaveStringToFile(Output,*(Directory/(Role+TEXT(".json"))))){++Failures;}SetActorTickEnabled(false);FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
