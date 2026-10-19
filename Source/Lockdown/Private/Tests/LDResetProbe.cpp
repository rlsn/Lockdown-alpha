#include "Tests/LDResetProbe.h"
#include "Framework/LDGameInstance.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Objectives/LDObjectiveRules.h"
#include "Combat/LDCombatDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "Character/LDCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMemory.h"

ALDResetProbe::ALDResetProbe(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDResetProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(GetWorld()->GetTimeSeconds()<.75||UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(this)){return;}
    SetActorTickEnabled(false);Run();
#endif
}
void ALDResetProbe::Run()
{
    auto* GI=GetGameInstance<ULDGameInstance>();if(!GI->ResetEvidence){GI->ResetEvidence=MakeShared<FLDResetEvidence>();}auto& E=*GI->ResetEvidence;
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* GS=GetWorld()->GetGameState<ALDGameState>();
    auto* PC=CastChecked<ALDPlayerController>(GetWorld()->GetFirstPlayerController());auto* Relay=PC->LocalView();
    auto Check=[&](bool Pass,const FString& Text){E.Checks.Add(FString::Printf(TEXT("%s round%d %s"),Pass?TEXT("PASS"):TEXT("FAIL"),E.Round,*Text));if(!Pass){++E.Failures;}UE_LOG(LogTemp,Display,TEXT("LD_RESET %s"),*E.Checks.Last());};
    int32 Residents=0,Wards=0,Items=0,Corpses=0,Vision=0,Combat=0,Objectives=0,Relays=0,Sites=0;
    for(TActorIterator<ALDResident> It(GetWorld());It;++It){++Residents;}
    for(TActorIterator<ALDWard> It(GetWorld());It;++It){++Wards;}
    for(TActorIterator<ALDSeal> It(GetWorld());It;++It){++Items;}
    for(TActorIterator<ALDCorpse> It(GetWorld());It;++It){++Corpses;}
    for(TActorIterator<ALDVisionDirector> It(GetWorld());It;++It){++Vision;}
    for(TActorIterator<ALDCombatDirector> It(GetWorld());It;++It){++Combat;}
    for(TActorIterator<ALDObjectiveDirector> It(GetWorld());It;++It){++Objectives;}
    for(TActorIterator<ALDWorldViewRelay> It(GetWorld());It;++It){++Relays;}
    for(TActorIterator<ALDObjectiveSite> It(GetWorld());It;++It){++Sites;}
    const auto O=PC->OwnerState;
    Check(Residents==8&&!Wards&&!Items&&!Corpses&&Vision==1&&Combat==1&&Objectives==1&&Relays==1&&Sites==3,TEXT("fresh actor populations and no prior ward/item/corpse/director growth"));
    Check(O.Match>E.PreviousGeneration&&O.Life==1&&O.Hp==100&&O.Gold==0&&O.Xp==0&&O.WardCharges==1&&O.bActive&&!O.DeathSerial&&!O.ReturnAt&&!O.ProtectedUntil&&!O.ContractStage&&!O.ContractId.IsValid()&&!O.SealId.IsValid()&&!O.Commissions&&!O.DeliverySerial&&!O.ChannelKind&&!O.BasicReadyAt&&!O.AssassinReadyAt&&!O.DashReadyAt&&!O.bWinner,TEXT("new generation clears life schedules, resources, inventory and receipts"));
    Check(GS->DefensiveFlags==15&&GS->bPublicSensorActive&&!GS->bClaimActive&&!GS->bContested&&!GS->ResultSerial&&!GS->ResultReceipt.IsValid()&&!GS->EndReason&&!GS->SealNoticeSerial,TEXT("fixture, public claim and result state reset"));
    Check(Relay&&!Relay->IsStale()&&Relay->LastView().Generation==O.Match&&Relay->LastView().Explored==Relay->LastView().Current,TEXT("fresh relay has no exploration inherited from prior world"));
    if(E.Round)
    {
        Check(!E.OldActors.ContainsByPredicate([](const auto& Weak){return Weak.IsValid();}),TEXT("all prior world actors, proxies and sources were destroyed"));
        const auto Revision=Relay->AcceptedRevision();const auto Fresh=Relay->LastView();Relay->ApplyFullView(E.PreviousView);PC->ClientLifeReceipt(E.PreviousOwner);
        PC->ServerContextRequest(E.PreviousGeneration,1,900,LDObjective::Guild,FGuid());PC->ServerCombatAction(E.PreviousGeneration,1,901,0,FVector::ForwardVector);
        Mode->Combat->Sample(GetWorld()->GetTimeSeconds());
        Check(PC->OwnerState.Match==O.Match&&!PC->OwnerState.ContractStage&&!PC->OwnerState.Gold&&Relay->AcceptedRevision()==Revision&&Relay->LastView().Explored==Fresh.Explored,TEXT("old-generation commands, private receipt and full view cannot mutate the new run"));
    }
    E.MemoryMiB.Add(FPlatformMemory::GetStats().UsedPhysical/1048576.);
    if(E.Round==10)
    {
        TSharedRef<FJsonObject> Json=MakeShared<FJsonObject>();Json->SetNumberField(TEXT("failures"),E.Failures);Json->SetNumberField(TEXT("restarts"),E.Round);
        TArray<TSharedPtr<FJsonValue>> Lines,Memory;for(const auto& Text:E.Checks){Lines.Add(MakeShared<FJsonValueString>(Text));}for(double Value:E.MemoryMiB){Memory.Add(MakeShared<FJsonValueNumber>(Value));}
        Json->SetArrayField(TEXT("checks"),Lines);Json->SetArrayField(TEXT("processMemoryMiB"),Memory);
        FString Output,Path;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Output));FParse::Value(FCommandLine::Get(),TEXT("LDProbeReport="),Path);
        if(Path.IsEmpty()||!FFileHelper::SaveStringToFile(Output,*Path)){++E.Failures;}FPlatformMisc::RequestExitWithStatus(false,E.Failures?1:0);return;
    }
    // Deliberately dirty every lifetime boundary before actual non-seamless travel.
    // Alternate a pending death/return with a real terminal claim, not a synthetic reset of fields.
    auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());Body->SetActorLocation(FVector(-1700,-1000,92));Body->GetCharacterMovement()->DisableMovement();FString Reason;
    const bool Placed=Mode->Vision->PlaceWard(PC,FVector(-1650,-1000,0),Reason);
    Check(Placed,TEXT("setup creates a real expiring ward: ")+Reason);
    Body->SetActorLocation(LDObjective::Position(LDObjective::Guild)+FVector(0,60,92));
    PC->ServerContextRequest(O.Match,O.Life,100,LDObjective::Guild,FGuid());Mode->Combat->Sample(GetWorld()->GetTimeSeconds());
    auto* Item=Mode->Objectives->SeedFixtureSeal();Item->State=1;Item->Carrier=PC;auto* State=Mode->StateFor(PC);State->SealId=Item->Identity;State->Gold=101;State->Xp=80;State->Commissions=6;State->DeliverySerial=7;Mode->SyncOwner(PC);
    Mode->Objectives->SetDefencesFixture(0);Mode->Vision->SetPublicSensor(false);
    const double Now=GetWorld()->GetTimeSeconds();
    if(E.Round%2==0){Mode->EndContenderLife(PC,Now);GetWorld()->SpawnActor<ALDCorpse>(Body->GetActorLocation(),FRotator::ZeroRotator);Check(!PC->OwnerState.bActive&&PC->OwnerState.ReturnAt>Now&&Item->State==0,TEXT("setup leaves a pending return, dropped item and corpse"));}
    else
    {
        Body->SetActorLocation(FVector(60,0,92));Mode->Objectives->Release(PC,O.Life,100);Mode->Objectives->Queue(PC,O.Match,O.Life,101,LDObjective::Heart,FGuid());Mode->Objectives->RefreshHold(PC,O.Life,101,1,false,Now+.1);Mode->Objectives->Sample(Now+.1);
        Mode->Objectives->RefreshHold(PC,O.Life,101,2,false,Now+10.1);Mode->Objectives->Sample(Now+10.1);
        Check(Mode->IsFinished()&&GS->ResultSerial==1&&PC->OwnerState.bWinner,TEXT("setup leaves a real terminal result and carried inventory"));
    }
    Mode->Vision->Tick(0);E.PreviousGeneration=O.Match;E.PreviousOwner=PC->OwnerState;E.PreviousView=Relay->LastView();E.OldActors.Reset();
    for(TActorIterator<AActor> It(GetWorld());It;++It){E.OldActors.Add(*It);}++E.Round;
    UGameplayStatics::OpenLevel(this,TEXT("/Game/Lockdown/Generated/Maps/L_MigrationSandbox"));
}
