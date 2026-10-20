#include "Tests/LDDevelopmentProbe.h"
#include "Development/LDWorldChecks.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Core/LDGroundQueries.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "AIController.h"

#if !UE_BUILD_SHIPPING
void ALDDevelopmentProbe::WorldCheck()
{
    auto* W=GetWorld();auto* M=W->GetAuthGameMode<ALDGameMode>();auto* C=M->Combat.Get();auto* D=M->WorldChecks.Get();auto* PC=CastChecked<ALDPlayerController>(W->GetFirstPlayerController());auto* O=M->StateFor(PC);auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());FString Why;uint32 Token=100,Sequence=0;
    Check(!C->RequestEquipped(Body,O->Build.Revision,0,++Token,FVector::ForwardVector,Why)&&D->DrawCount()==0,TEXT("Lobby rejects utility actions without drawing"));
    M->SetLobbyChoice(PC,1,true,Why);M->StartDevelopmentMatch(PC,Why);PC->SetMenuOpen(false);C->SetActorTickEnabled(false);
    for(TActorIterator<ALDResident> It(W);It;++It){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();if(auto* AI=Cast<AAIController>(It->GetController())){AI->StopMovement();}It->SetActorLocation(FVector(1800,1000,92));}
    auto* Target=W->SpawnActor<ALDResident>(FVector(-1700,-900,92),FRotator::ZeroRotator);Target->SetActorTickEnabled(false);Target->GetCharacterMovement()->DisableMovement();
    auto* Other=W->SpawnActor<ALDCharacter>(FVector(-1800,-880,92),FRotator::ZeroRotator);Other->GetCharacterMovement()->DisableMovement();
    auto Advance=[&](double Seconds){W->TimeSeconds+=Seconds;C->Sample(W->GetTimeSeconds());};
    auto Hold=[&](double Seconds){for(double Left=Seconds;Left>1.e-7;){const double Step=FMath::Min(.1,Left);W->TimeSeconds+=Step;C->RefreshSkillHold(Body,Token,++Sequence,false);C->Sample(W->GetTimeSeconds());Left-=Step;}};
    auto Reset=[&]()
    {
        *C->StateFor(Body)=FLDBodyCombatState();C->StateFor(Body)->Hp=95;*C->StateFor(Target)=FLDBodyCombatState();C->StateFor(Target)->Hp=40;
        Body->GetCharacterMovement()->StopMovementImmediately();Body->SetActorLocation(FVector(-1780,-900,92));Body->SetFacing(0);Target->SetActorLocation(FVector(-1700,-900,92));Target->SetFacing(0);Other->SetActorLocation(FVector(1800,1200,92));
        const uint32 Revision=O->Build.Revision+1;O->Build=LDDevelopment::StartingBuild(1);O->Build.Revision=Revision;O->Build.Ranks.Init(1,LDDevelopment::SkillCount);O->Build.Ranks[0]=0;O->Build.Slots={ELDSkill::Pickpocket,ELDSkill::Survey,ELDSkill::Step,ELDSkill::Patch};O->Hp=95;O->ProtectedUntil=0;M->RefreshBuild(PC);Sequence=0;
    };
    auto Cast=[&](uint8 Slot=0){Sequence=0;return C->RequestEquipped(Body,O->Build.Revision,Slot,++Token,FVector::ForwardVector,Why);};
    Reset();int32 Tasks=0;for(TActorIterator<ALDCharterTask> It(W);It;++It){++Tasks;}Check(Tasks==4,TEXT("Four production Charter services are present"));
    for(uint8 I=0;I<4;++I)
    {
        Reset();O->Witness.Invalidate();O->Build.Attributes.Init(12,6);O->Build.Level=5;O->Build.Slots={LDCharter::Skill(I),ELDSkill::None,ELDSkill::None,ELDSkill::None};Body->SetActorLocation(LDCharter::Position(I)+FVector(0,50,92));M->RefreshBuild(PC);const int64 XP=O->Xp,Gold=O->Gold;
        Check(Cast(),FString::Printf(TEXT("%s accepts a learned equipped threshold-twelve check"),LDDevelopment::SkillName(LDCharter::Skill(I))));Hold(LDDevelopment::ChannelSeconds(LDCharter::Skill(I))+.01);
        const FGuid Receipt=O->Witness;Check(Receipt.IsValid()&&O->Gold==Gold&&O->Xp==XP,FString::Printf(TEXT("%s grants one real Witness without gold or XP"),LDDevelopment::SkillName(LDCharter::Skill(I))));
        O->Build.Cooldowns[uint8(LDCharter::Skill(I))]=0;Check(!Cast()&&O->Witness==Receipt,TEXT("Repeated alternative cannot issue another Witness"));
    }
    Reset();O->Witness.Invalidate();O->Build.Slots[0]=ELDSkill::Heave;Body->SetActorLocation(LDCharter::Position(0)+FVector(0,50,92));
    Check(!Cast()&&!O->Build.Cooldowns[9]&&!O->Witness.IsValid(),TEXT("Insufficient public score rejects before cooldown"));
    O->Build.Slots[0]=ELDSkill::None;Check(!Cast(),TEXT("Context cannot activate learned unequipped Charter skill"));
    Reset();O->Build.Slots[0]=ELDSkill::Survey;O->Build.Slots[1]=ELDSkill::None;Body->SetActorLocation(LDCharter::Position(3)+FVector(0,50,92));Cast();C->ReleaseSkillHold(PC,Token);Hold(1.1);Check(!O->Witness.IsValid(),TEXT("Interrupted Charter channel grants no Witness"));
    Reset();O->Build.Slots[0]=ELDSkill::Survey;O->Build.Slots[1]=ELDSkill::None;Body->SetActorLocation(LDCharter::Position(3)+FVector(0,50,92));Cast();Body->SetActorLocation(FVector(-1800,-900,92));Hold(1.1);Check(!O->Witness.IsValid(),TEXT("Charter completion rechecks range and sight"));
    Reset();const FGuid Wallet=D->WalletId(Target),SourceWallet=D->WalletId(Body);Check(D->Balance(Target)==36&&D->Balance(Body)==O->Gold&&SourceWallet==M->ContenderId(PC),TEXT("Residents start with thirty-six; human wallet uses actual gold and stable contender identity"));
    D->SetTestBalance(Body,0);D->SetTestBalance(Target,36);Check(D->InjectTestRolls({20,20,20,20}),TEXT("Private diagnostic roll injection requires the explicit probe flag"));
    Check(Cast()&&FMath::IsNearlyEqual(D->RetryAt(Body,Target)-W->GetTimeSeconds(),90.)&&FMath::IsNearlyEqual(O->Build.Cooldowns[5]-W->GetTimeSeconds(),20.),TEXT("Accepted Pickpocket reserves both twenty-second skill and ninety-second pair deadlines"));
    const double PairEnd=D->RetryAt(Body,Target);Hold(.9);Check(D->DrawCount()==0&&O->Gold==0,TEXT("No roll or transfer before one-second completion"));Hold(.11);
    Check(D->DrawCount()==1&&O->Gold==12&&D->Balance(Target)==24&&O->TheftReceipt.IsValid()&&O->TheftOutcome==1,TEXT("Valid completion draws once and atomically transfers twelve existing gold"));
    const auto FirstReceipt=O->TheftReceipt;const uint64 Draws=D->DrawCount();Check(!C->RequestEquipped(Body,O->Build.Revision,0,Token,FVector::ForwardVector,Why)&&D->DrawCount()==Draws&&O->TheftReceipt==FirstReceipt,TEXT("Direct duplicate token cannot draw or transfer twice"));
    Advance(20.1);Check(!Cast()&&D->DrawCount()==Draws,TEXT("Different request ID cannot bypass pair retry after skill cooldown"));
    W->TimeSeconds=PairEnd-.01;Check(!Cast(),TEXT("Pair window rejects immediately before its deadline"));W->TimeSeconds=PairEnd;Check(Cast(),TEXT("Pair retry permits exactly the deadline"));Hold(1.01);
    Check(O->Gold==24&&D->Balance(Target)==12&&D->DrawCount()==2,TEXT("Second completion spends the live remaining balance"));
    Advance(90);Cast();Hold(1.01);Check(O->Gold==36&&D->Balance(Target)==0,TEXT("Repeated valid successes deplete the persistent purse"));
    Advance(90);Cast();Hold(1.01);Check(O->Gold==36&&D->DrawCount()==4&&O->TheftAmount==0,TEXT("Empty purse draws once and transfers zero without creating money"));
    Advance(90);D->InjectTestRolls({1});const int64 Gold=O->Gold,XP=O->Xp;Cast();Hold(1.01);
    Check(O->TheftOutcome==2&&O->Gold==Gold&&O->Xp==XP&&D->DrawCount()==5,TEXT("Failed roll transfers nothing and grants no XP"));
    Check(D->AlertUntil(Target)>W->GetTimeSeconds()+29&&C->StateFor(Body)->ExposedUntil>W->GetTimeSeconds()+3,TEXT("Failure marks target Alert thirty seconds and exposes source four seconds"));
    Advance(90);const uint64 BeforeCancel=D->DrawCount();Cast();const double Reserved=D->RetryAt(Body,Target);C->ReleaseSkillHold(PC,Token);Hold(1.1);
    Check(D->DrawCount()==BeforeCancel&&D->RetryAt(Body,Target)==Reserved&&PC->IntentFeedback.Contains(TEXT("interrupted"))&&!Cast(),TEXT("Cancellation keeps retry/cooldown, reports interruption and draws nothing"));
    Advance(90);Cast();C->RefreshSkillHold(Body,Token,++Sequence,true);Hold(1.1);Check(D->DrawCount()==BeforeCancel&&C->StateFor(Body)->Action==0,TEXT("Movement pulse cancels theft without a draw"));
    Advance(90);Cast();Advance(.251);Check(D->DrawCount()==BeforeCancel&&C->StateFor(Body)->Action==0,TEXT("Stale hold cancels theft without a draw"));
    Advance(90);Cast();Target->SetFacing(180);Hold(1.1);Check(D->DrawCount()==BeforeCancel,TEXT("Turning out of rear bearing invalidates completion before RNG"));Target->SetFacing(0);
    Advance(90);Cast();Target->SetActorLocation(FVector(-1400,-900,92));Hold(1.1);Check(D->DrawCount()==BeforeCancel,TEXT("Range escape invalidates completion before RNG"));Target->SetActorLocation(FVector(-1700,-900,92));
    Advance(90);C->StateFor(Target)->ProtectionUntil=W->GetTimeSeconds()+5;Check(!Cast()&&D->DrawCount()==BeforeCancel,TEXT("Protected target fails before acceptance and RNG"));C->StateFor(Target)->ProtectionUntil=0;
    C->StateFor(Target)->ExposedUntil=W->GetTimeSeconds()+5;Check(!Cast(),TEXT("Exposed target cannot be pickpocketed"));C->StateFor(Target)->ExposedUntil=0;
    Body->SetActorLocation(FVector(-1620,-900,92));Check(!Cast(),TEXT("Front bearing fails through the ordinary equipped boundary"));Body->SetActorLocation(FVector(-1780,-900,92));
    Body->SetActorLocation(FVector(-1160,-450,92));Target->SetActorLocation(FVector(-1065,-450,92));Check(!Cast(),TEXT("A wall blocks purse acceptance despite range and rear bearing"));Reset();
    Advance(90);D->InjectTestRolls({20});Cast();const double Due=C->StateFor(Body)->ImpactAt;Other->SetActorLocation(Body->GetActorLocation()+FVector(0,-70,0));C->RequestBody(Other,0,FVector(0,1,0),Why);C->StateFor(Other)->ImpactAt=Due;Hold(1.01);
    Check(D->DrawCount()==BeforeCancel&&C->StateFor(Body)->Hp<95,TEXT("Positive damage on theft deadline wins before draw or transfer"));Other->SetActorLocation(FVector(1800,1200,92));
    // Distinct source wallets can race for one purse, serializing live balances.
    Reset();Advance(90);D->SetTestBalance(Target,15);D->InjectTestRolls({20,20});auto NPC=LDDevelopment::StartingBuild(1);NPC.Slots[0]=ELDSkill::Pickpocket;*C->StateFor(Other)=FLDBodyCombatState();C->RegisterNPCBuild(Other,NPC);Other->SetActorLocation(FVector(-1780,-880,92));Other->SetFacing(0);
    const int64 SourceBefore=D->Balance(Body)+D->Balance(Other);const uint64 DrawBeforeRace=D->DrawCount();Cast();const uint32 OtherToken=9001;Check(C->RequestEquipped(Other,NPC.Revision,0,OtherToken,FVector::ForwardVector,Why),TEXT("Another trained humanoid uses the same wallet capability"));
    for(uint32 I=1;I<=11;++I){W->TimeSeconds+=.1;C->RefreshSkillHold(Body,Token,++Sequence,false);C->RefreshSkillHold(Other,OtherToken,I,false);C->Sample(W->GetTimeSeconds());}
    Check(D->Balance(Target)==0&&D->Balance(Body)+D->Balance(Other)==SourceBefore+15&&D->DrawCount()==DrawBeforeRace+2,TEXT("Concurrent completions split fifteen gold without double spending"));
    Check(D->Balance(Other)>=36,TEXT("NPC source receives transfers through its persistent wallet"));
    Advance(20.1);Other->SetActorLocation(FVector(1800,1200,92));Target->SetActorLocation(FVector(1800,1000,92));
    auto* Resistance=W->SpawnActor<ALDCharacter>(FVector(-1700,-900,92),FRotator::ZeroRotator);Resistance->GetCharacterMovement()->DisableMovement();auto Guard=LDDevelopment::StartingBuild(2);Guard.Attributes[4]=10;C->RegisterNPCBuild(Resistance,Guard);D->InjectTestRolls({10});
    Check(Cast(),TEXT("Theft accepts with a live target resistance and source snapshot"));O->Build.Attributes[1]=16;C->StateFor(Resistance)->NPCBuild.Attributes[4]=14;const auto BeforeSnapshot=D->DrawCount();Hold(1.01);
    Check(O->TheftOutcome==2&&D->DrawCount()==BeforeSnapshot+1,TEXT("Completion uses accepted DEX12 rather than changed DEX16 and rechecks target WIS14"));Resistance->Destroy();O->Build.Attributes[1]=12;Target->SetActorLocation(FVector(-1700,-900,92));
    // Human target follows the same capability; target WIS and Alert are live.
    Other->SetActorLocation(Body->GetActorLocation()-FVector(80,0,0));Other->SetFacing(0);Body->SetFacing(0);C->StateFor(Body)->ExposedUntil=0;C->StateFor(Body)->ProtectionUntil=0;C->StateFor(Other)->Action=0;C->StateFor(Other)->NPCBuild.Cooldowns[5]=0;D->SetTestBalance(Body,20);D->InjectTestRolls({1});
    Check(C->RequestEquipped(Other,NPC.Revision,0,9002,FVector::ForwardVector,Why),TEXT("Human target is eligible through the same role-neutral capability"));
    for(uint32 I=1;I<=11;++I){W->TimeSeconds+=.1;C->RefreshSkillHold(Other,9002,I,false);C->Sample(W->GetTimeSeconds());}
    Check(O->AlertUntil>W->GetTimeSeconds()+29&&O->TamperSerial==1&&O->Gold==20,TEXT("Victim receives private tampering/Alert cue without attacker coordinates"));
    Body->SetActorLocation(LDCharter::Position(3)+FVector(0,50,92));Cast(1);Hold(1.01);Check(O->Witness.IsValid(),TEXT("Acquired Witness exists before the real death/return retention check"));
    const FGuid StableSource=D->WalletId(Body);const auto Witness=O->Witness;const auto Retry=O->TheftRetryAt;const uint32 Life=O->Life;
    M->EndContenderLife(PC,W->GetTimeSeconds());Body->Destroy();M->ReturnDueContenders(O->ReturnAt+.01);Body=CastChecked<ALDCharacter>(PC->GetPawn());
    Check(O->Life==Life+1&&D->WalletId(Body)==StableSource&&O->Witness==Witness&&O->TheftRetryAt==Retry&&O->AlertUntil>W->GetTimeSeconds(),TEXT("Contender body replacement retains stable wallet, Witness, retry and Alert"));
    const uint64 BeforeReturn=D->DrawCount();PC->ServerSkillAction(O->Match,Life,10000,O->Build.Revision,0,FVector::ForwardVector);Advance(.1);Check(D->DrawCount()==BeforeReturn,TEXT("Old-life theft request cannot draw on a returned body"));
    Advance(21);Body->SetActorLocation(FVector(-1780,-900,92));Body->GetCharacterMovement()->StopMovementImmediately();Check(!Cast()&&D->RetryAt(Body,Target)>W->GetTimeSeconds()&&D->DrawCount()==BeforeReturn,TEXT("Returned body cannot bypass active wallet-pair retry after its skill cooldown expires"));
    // Kill the depleted resident through actual combat and await its real return.
    Reset();D->SetTestBalance(Target,0);C->StateFor(Target)->Hp=1;C->RequestBody(Body,0,FVector::ForwardVector,Why);Advance(.16);Check(!IsValid(Target),TEXT("Resident death uses the real combat destruction path"));Advance(20.1);
    ALDResident* Returned=nullptr;for(TActorIterator<ALDResident> It(W);It;++It){if(D->WalletId(*It)==Wallet){Returned=*It;break;}}
    Check(Returned&&D->Balance(Returned)==0,TEXT("Resident return binds the original depleted wallet rather than replenishing thirty-six gold"));
    Check(D->DrawCount()==BeforeReturn,TEXT("Death, return, GUIDs and unrelated gameplay do not advance Theft RNG"));
}

void ALDDevelopmentProbe::WorldNormalTick()
{
    auto* W=GetWorld();auto* M=W->GetAuthGameMode<ALDGameMode>();auto* PC=Cast<ALDPlayerController>(W->GetFirstPlayerController());if(!PC||!PC->GetPawn()){return;}auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());auto* O=M->StateFor(PC);const double Now=W->GetTimeSeconds();FString Why;
    if(!Phase){if(Now<1||UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(this)){return;}Check(!O->Gold&&!O->Xp&&!O->Witness.IsValid(),TEXT("Ordinary Charter practice starts with no resources or Witness"));M->SetLobbyChoice(PC,1,true,Why);M->StartDevelopmentMatch(PC,Why);PC->SetMenuOpen(false);NormalLastPosition=Body->GetActorLocation();Phase=1;return;}
    NormalTravel+=FVector::Dist2D(NormalLastPosition,Body->GetActorLocation());NormalLastPosition=Body->GetActorLocation();const FVector Goal=LDCharter::Position(3)+FVector(0,50,92);
    if(Phase==1)
    {
        if(FVector::DistSquared2D(Body->GetActorLocation(),Goal)>25*25){if(LDGround::ClearSegment(W,Body->GetActorLocation(),Goal)){Body->SetSprint(true);Body->AddMovementInput((Goal-Body->GetActorLocation()).GetSafeNormal2D());}return;}
        Body->SetSprint(false);Body->GetCharacterMovement()->StopMovementImmediately();if(Now<O->ProtectedUntil||!Body->GetLastMovementInputVector().IsNearlyZero()){return;}
        Check(NormalTravel>800&&O->Build.Level==1&&O->Build.Attributes[4]==12,TEXT("Native travel reaches visible field report with the unmodified Rogue start"));Check(PC->ContextPrompt().Contains(TEXT("score 12")),TEXT("Task preview shows only own score and public threshold"));PC->SetSkillHeld(1,true);NormalHoldAt=Now;Phase=2;
    }
    else if(Phase==2&&O->Witness.IsValid())
    {PC->SetSkillHeld(1,false);Check(Now-NormalHoldAt>=1&&!O->Gold&&!O->Xp,TEXT("Actual held equipped Survey grants one Witness with no gold or XP"));Check(O->Build.Cooldowns[8]>Now,TEXT("Ordinary Charter service consumes the normal skill cooldown"));NormalHoldAt=Now;Phase=3;}
    else if(Phase==3&&Now>O->Build.Cooldowns[8]+.1){PC->SetSkillHeld(1,true);NormalHoldAt=Now;Phase=4;}
    else if(Phase==4&&Now-NormalHoldAt>1.5){PC->SetSkillHeld(1,false);Check(O->Witness.IsValid()&&!O->Gold&&!O->Xp&&O->Build.Cooldowns[8]<Now,TEXT("Repeated ordinary service rejects before cooldown without duplicate rewards"));Check(M->WorldChecks->DrawCount()==0,TEXT("Charter service and native travel never draw Theft RNG"));Finish();}
}

void ALDDevelopmentProbe::WorldServerTick()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* G=GetWorld()->GetGameState<ALDGameState>();auto* D=M->WorldChecks.Get();
    if(!Phase){for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){if(auto* PC=Cast<ALDPlayerController>(It->Get());PC&&PC->GetPawn()){if(PC->ProbeRole==1){A=PC;}if(PC->ProbeRole==2){B=PC;}}}if(A.IsValid()&&B.IsValid()){Check(G->LobbyCount==2,TEXT("World checks have two independent remote owners"));Enter(1);}return;}
    if(Phase==9){if(FPlatformTime::Seconds()-PhaseAt>3){Finish();}return;}if(!A.IsValid()||!B.IsValid()){Check(false,TEXT("World-check client disconnected early"));Finish();return;}
    auto* AS=M->StateFor(A.Get());auto* BS=M->StateFor(B.Get());auto* AB=CastChecked<ALDCharacter>(A->GetPawn());auto* BB=CastChecked<ALDCharacter>(B->GetPawn());const double Elapsed=FPlatformTime::Seconds()-PhaseAt;
    if(Phase==1&&AS->bReady&&BS->bReady){Enter(2);}
    else if(Phase==2&&G->bMatchStarted&&Elapsed>1)
    {
        for(TActorIterator<ALDResident> It(GetWorld());It;++It){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();if(auto* AI=Cast<AAIController>(It->GetController())){AI->StopMovement();}It->SetActorLocation(FVector(1800,1000,92));}
        AB->SetActorLocation(LDCharter::Position(3)+FVector(0,50,92));BB->SetActorLocation(LDCharter::Position(1)+FVector(0,-50,92));for(auto* Body:{AB,BB}){Body->GetCharacterMovement()->StopMovementImmediately();M->Combat->StateFor(Body)->ProtectionUntil=0;}AS->Gold=0;BS->Gold=20;M->SyncOwner(A.Get());M->SyncOwner(B.Get());Enter(3);
    }
    else if(Phase==3&&Elapsed>3.5)
    {
        Check(AS->Witness.IsValid()&&BS->Witness.IsValid()&&!AS->Gold&&BS->Gold==20&&!AS->Xp&&!BS->Xp,TEXT("Remote equipped Charter alternatives grant independent once-only Witnesses"));
        AB->SetActorLocation(FVector(-1800,-900,92));BB->SetActorLocation(FVector(-1720,-900,92));AB->SetFacing(0);BB->SetFacing(0);D->InjectTestRolls({20,1});Enter(4);
    }
    else if(Phase==4&&Elapsed>2.8)
    {
        Check(AS->Gold==12&&BS->Gold==8&&D->DrawCount()==1&&AS->TheftOutcome==1,TEXT("Remote duplicate Pickpocket RPC commits one draw and one twelve-gold transfer"));Check(BS->TamperSerial==1,TEXT("Remote victim gets private balance-change cue"));Enter(5);
    }
    else if(Phase==5&&Elapsed>1.5)
    {
        Check(D->DrawCount()==1&&AS->Gold==12&&BS->Gold==8,TEXT("New token and replay cannot reroll or double-spend"));
        // Advance the authority clock, not the retry map or cooldown, for a labelled deadline fixture.
        GetWorld()->TimeSeconds=D->RetryAt(AB,BB)+.01;M->Combat->Sample(GetWorld()->GetTimeSeconds());Enter(6);
    }
    else if(Phase==6&&Elapsed>2.8)
    {Check(D->DrawCount()==2&&AS->Gold==12&&BS->Gold==8&&AS->TheftOutcome==2&&BS->AlertUntil>GetWorld()->GetTimeSeconds(),TEXT("Second legal remote attempt fails once and applies private Alert without transfer"));Check(M->Combat->StateFor(AB)->ExposedUntil>GetWorld()->GetTimeSeconds(),TEXT("Failure extends ordinary exposure without sending an origin coordinate"));Enter(7);}
    else if(Phase==7&&Elapsed>2){Enter(8);}
    else if(Phase==8&&Elapsed>1.5){Enter(9);}
}
void ALDDevelopmentProbe::WorldClientTick()
{
    auto* PC=Cast<ALDPlayerController>(GetOwner());if(!PC||!PC->OwnerState.Match){return;}const auto& O=PC->OwnerState;const double Wall=FPlatformTime::Seconds();const bool IsA=Role==TEXT("A");
    if(Wall-LastPing>.2){LastPing=Wall;PC->ServerProbePing(Wall);}if(PC->ProbePhase!=Phase){Phase=PC->ProbePhase;PhaseAt=Wall;bChoiceSent=false;bChecked=false;}const double Elapsed=Wall-PhaseAt;
    if(Phase==1){if(!bChoiceSent){PC->ChooseLobbyClass(IsA?1:2);bChoiceSent=true;}if(!bReadySent&&O.Build.ClassId==(IsA?1:2)&&Elapsed>.4){PC->ToggleLobbyReady();bReadySent=true;}}
    if(Phase==2&&!bStartSent&&O.bLobbyHost){PC->StartLobby();bStartSent=true;}
    const uint32 Token=Phase==3?7001:Phase==4?7010:7030;
    if((Phase==3||((Phase==4||Phase==6)&&IsA))&&!bChoiceSent&&Elapsed>.4){bChoiceSent=true;PC->ServerSkillAction(O.Match,O.Life,Token,O.Build.Revision,Phase==3?1:0,FVector::ForwardVector);if(Phase==4){PC->ServerSkillAction(O.Match,O.Life,Token,O.Build.Revision,0,FVector::ForwardVector);}}
    if((Phase==3||((Phase==4||Phase==6)&&IsA))&&Elapsed>.4&&Elapsed<2.9&&Wall-SkillRefreshAt>.1){SkillRefreshAt=Wall;PC->ServerSkillHold(O.Match,O.Life,Token,uint32(Elapsed*1000),false);}
    if(Phase==5&&IsA&&!bChoiceSent&&Elapsed>.4){bChoiceSent=true;PC->ServerSkillAction(O.Match,O.Life,7010,O.Build.Revision,0,FVector::ForwardVector);PC->ServerSkillAction(O.Match,O.Life,7020,O.Build.Revision,0,FVector::ForwardVector);}
    if(Phase==7&&!bChecked&&Elapsed>.5)
    {bChecked=true;Check(O.Witness.IsValid()&&!O.Xp,TEXT("Owner retains private Witness without XP"));Check(O.Gold==(IsA?12:8),TEXT("Owner receives exact final private gold"));Check(IsA?O.TheftOutcome==2&&O.TheftReceipt.IsValid():O.TamperSerial==2&&O.AlertUntil>0,TEXT("Outcome and tampering cues remain owner-private"));int32 Bodies=0,Tasks=0;for(TActorIterator<ALDCharacter> It(GetWorld());It;++It){++Bodies;}for(TActorIterator<ALDCharterTask> It(GetWorld());It;++It){++Tasks;}Check(Bodies==1&&!Tasks&&PC->LocalView(),TEXT("Restricted client has only own canonical body and filtered task proxies"));Check(O.Build.ClassId==(IsA?1:2),TEXT("Other owner's build is not substituted by target feedback"));}
    if(Phase==9&&Elapsed>1){Finish();}
}
#endif
