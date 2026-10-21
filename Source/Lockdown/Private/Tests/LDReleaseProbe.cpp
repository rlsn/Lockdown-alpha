#include "Tests/LDDevelopmentProbe.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Development/LDWorldChecks.h"
#include "Region/LDRegionDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformMemory.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#if !UE_BUILD_SHIPPING
namespace
{
    double P95(TArray<double> Values){if(Values.IsEmpty()){return 0;}Values.Sort();return Values[FMath::Clamp(FMath::CeilToInt(Values.Num()*.95)-1,0,Values.Num()-1)];}
}
void ALDDevelopmentProbe::SessionAudit()
{
    auto* W=GetWorld();auto* M=W->GetAuthGameMode<ALDGameMode>();auto* G=W->GetGameState<ALDGameState>();auto* PC=CastChecked<ALDPlayerController>(W->GetFirstPlayerController());const auto* O=M->StateFor(PC);const double Wall=FPlatformTime::Seconds();
    if(SessionRuleSerial!=M->Combat->SampleSerial){SessionRuleSerial=M->Combat->SampleSerial;SessionRules.Add(M->Combat->LastSampleMs);}
    if(SessionViewSerial!=M->Vision->SampleSerial){SessionViewSerial=M->Vision->SampleSerial;SessionViews.Add(M->Vision->LastSampleMs);}
    if(Wall-SessionLastAudit>=60||M->IsFinished())
    {
        SessionLastAudit=Wall;int32 Actors=0,Seals=0;for(TActorIterator<AActor> It(W);It;++It){++Actors;}for(TActorIterator<ALDSeal> It(W);It;++It){++Seals;}
        SessionActorMin=FMath::Min(SessionActorMin,Actors);SessionActorMax=FMath::Max(SessionActorMax,Actors);SessionMemory.Add(FPlatformMemory::GetStats().UsedPhysical/1048576.);
        auto Sample=MakeShared<FJsonObject>();Sample->SetNumberField(TEXT("wallSeconds"),Wall-SessionStarted);Sample->SetNumberField(TEXT("worldSeconds"),W->GetTimeSeconds());Sample->SetNumberField(TEXT("deadline"),G->MatchDeadline);Sample->SetNumberField(TEXT("actorCount"),Actors);Sample->SetNumberField(TEXT("memoryMiB"),SessionMemory.Last());Sample->SetNumberField(TEXT("gold"),O->Gold);Sample->SetNumberField(TEXT("xp"),O->Xp);Sample->SetNumberField(TEXT("paidJobs"),O->PaidJobs);Sample->SetNumberField(TEXT("credits"),O->Commissions);Sample->SetNumberField(TEXT("deliverySerial"),O->DeliverySerial);Sample->SetNumberField(TEXT("theftDraws"),M->WorldChecks->DrawCount());Sample->SetNumberField(TEXT("deaths"),O->DeathSerial);Sample->SetNumberField(TEXT("sealActors"),Seals);Sample->SetBoolField(TEXT("fabricated"),O->FabricationReceipt.IsValid());Sample->SetNumberField(TEXT("nativeTravelCm"),NormalTravel);Sample->SetNumberField(TEXT("ruleP95Ms"),P95(SessionRules));Sample->SetNumberField(TEXT("viewP95Ms"),P95(SessionViews));SessionSamples.Add(MakeShared<FJsonValueObject>(Sample));
        UE_LOG(LogTemp,Display,TEXT("LD_SESSION elapsed=%.1f actors=%d memory=%.1f jobs=%u gold=%lld credits=%d draws=%llu result=%u"),Wall-SessionStarted,Actors,SessionMemory.Last(),O->PaidJobs,O->Gold,O->Commissions,M->WorldChecks->DrawCount(),G->ResultSerial);
    }
    if(!M->IsFinished()){return;}
    Check(Wall-SessionStarted>=1499&&G->MatchDeadline==OriginalDeadline&&G->EndReason==2&&G->ResultSerial==1&&G->ResultReceipt.IsValid(),TEXT("Original 1500-second practice reaches its deadline and commits one no-acquisition result after a full wall-clock session"));
    Check(O->PaidJobs==8&&O->Xp==420&&O->Gold==100&&O->Commissions==4&&O->FabricationReceipt==ProbeCraft&&ProbeCraft.IsValid()&&O->SealId==ProbeSeal&&ProbeSeal.IsValid()&&!O->bWinner,TEXT("Deadline retains exact earned balances, craft history and unconsumed item without duplicate payout"));
    Check(!O->DeathSerial&&M->Region->PeacefulDeaths==0&&M->WorldChecks->DrawCount()==0&&NormalTravel>150000,TEXT("Full session uses sustained native travel without kills, resource grants or unexpected Theft draws"));
    Check(SessionSamples.Num()>=25&&SessionActorMax-SessionActorMin<=80&&SessionMemory.Num()>=25,TEXT("Full-session minute audits retain bounded actors and memory samples"));
    TArray<double> Late;for(int32 I=SessionMemory.Num()/2;I<SessionMemory.Num();++I){Late.Add(SessionMemory[I]);}Late.Sort();Check(!Late.IsEmpty()&&Late.Last()-Late[0]<128,TEXT("Ordinary late-session process memory spread below128MiB"));
    Check(P95(SessionRules)+P95(SessionViews)<10,TEXT("Ordinary authority and view p95 sum below10ms"));Finish();
}
void ALDDevelopmentProbe::ReleaseAudit()
{
    auto* W=GetWorld();auto* M=W->GetAuthGameMode<ALDGameMode>();auto* G=W->GetGameState<ALDGameState>();auto* PC=CastChecked<ALDPlayerController>(W->GetFirstPlayerController());auto* O=M->StateFor(PC);FString Why;
    Check(!O->Gold&&!O->Xp&&!O->DeathSerial,TEXT("Release life fixture starts from an ordinary fresh contender"));M->SetLobbyChoice(PC,1,true,Why);M->StartDevelopmentMatch(PC,Why);M->Combat->SetActorTickEnabled(false);M->AwardExperience(PC,420);O->Gold=101;M->SyncOwner(PC);
    W->TimeSeconds=M->StartedAt()+1200;const double Now=W->GetTimeSeconds();APawn* Body=PC->GetPawn();M->EndContenderLife(PC,Now);Body->Destroy();
    Check(!O->bActive&&O->Gold==85&&O->LastLostGold==16&&O->DeathSerial==1&&FMath::Abs(O->ReturnAt-Now-46)<.01,TEXT("Actual level-five life at1200 seconds loses16 of101 gold and waits46 seconds"));
    const auto Snapshot=*O;M->EndContenderLife(PC,Now+1);Check(O->Gold==Snapshot.Gold&&O->DeathSerial==Snapshot.DeathSerial&&O->ReturnAt==Snapshot.ReturnAt,TEXT("Duplicate death while waiting cannot charge a second loss or restart the return timer"));
    FString Error;M->PreLogin(TEXT(""),TEXT("127.0.0.1"),FUniqueNetIdRepl(),Error);Check(Error.Contains(TEXT("locked")),TEXT("A guest cannot buy a fresh reward budget after the roster starts"));
    M->Logout(PC);Check(!M->StateFor(PC)&&!M->Objectives->StateFor(PC)&&!M->Objectives->QueuedRequestCount(),TEXT("Waiting withdrawal removes contender, transaction and queued intent state"));
    Check(PC->OwnerState.Gold==85&&PC->OwnerState.LastLostGold==16&&PC->OwnerState.DeathSerial==1,TEXT("Waiting withdrawal leaves the last private death receipt at one charge"));
    M->ReturnDueContenders(Now+100);Check(!PC->GetPawn()&&!M->StateFor(PC),TEXT("A withdrawn waiting contender cannot return or receive a new budget"));Error.Reset();M->PreLogin(TEXT(""),TEXT("127.0.0.1"),FUniqueNetIdRepl(),Error);Check(Error.Contains(TEXT("locked"))&&G->bMatchStarted&&!M->IsFinished(),TEXT("Empty started lobby remains locked without a last-living victory"));
}
#endif
