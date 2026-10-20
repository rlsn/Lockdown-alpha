#include "Tests/LDDevelopmentProbe.h"
#include "Development/LDDevelopmentDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Objectives/LDObjectiveRules.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "UI/LDDevelopmentPanel.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "AIController.h"
#include "Core/LDGroundQueries.h"

void ALDDevelopmentProbe::PreparationCheck()
{
    auto* W=GetWorld();auto* M=W->GetAuthGameMode<ALDGameMode>();auto* C=M->Combat.Get();auto* D=M->Development.Get();
    auto* PC=CastChecked<ALDPlayerController>(W->GetFirstPlayerController());auto* O=M->StateFor(PC);auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());FString Why;
    uint32 Token=1000,Sequence=0;
    auto Advance=[&](double Seconds){W->TimeSeconds+=Seconds;C->Sample(W->GetTimeSeconds());}; // Explicit controlled-clock engineering fixture; never used by gameplay.
    auto Send=[&](uint8 Kind,ELDSkill Skill=ELDSkill::None,FGuid Item=FGuid(),FLDBuildPlan Plan=FLDBuildPlan())
    {Advance(.3);++Token;Sequence=0;PC->ServerDevelopmentRequest(O->Match,O->Life,Token,Kind,Skill,Item,Plan);C->Sample(W->GetTimeSeconds());};
    auto Hold=[&](double Seconds)
    {for(double Left=Seconds;Left>1.e-7;){const double Step=FMath::Min(.1,Left);W->TimeSeconds+=Step;D->RefreshHold(PC,O->Life,Token,++Sequence,false);C->Sample(W->GetTimeSeconds());Left-=Step;}};
    auto Reset=[&]()
    {
        D->Cancel(PC,TEXT("Fixture reset"));*C->StateFor(Body)=FLDBodyCombatState();C->StateFor(Body)->Hp=60;
        Body->GetCharacterMovement()->StopMovementImmediately();Body->SetActorLocation(LDObjective::Position(1)+FVector(0,50,92));
        const uint32 Revision=O->Build.Revision+1;O->Build=LDDevelopment::StartingBuild(1);O->Build.Revision=Revision;O->Xp=0;M->AwardExperience(PC,270);O->Hp=60;O->Gold=400;O->ProtectedUntil=0;M->RefreshBuild(PC);
    };
    Send(LDDevelopmentTransaction::Buy,ELDSkill::Spark);Check(O->Books.IsEmpty(),TEXT("Lobby cannot purchase books"));
    M->SetLobbyChoice(PC,1,true,Why);M->StartDevelopmentMatch(PC,Why);C->SetActorTickEnabled(false);
    for(TActorIterator<ALDResident> It(W);It;++It){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();if(auto* AI=Cast<AAIController>(It->GetController())){AI->StopMovement();}It->SetActorLocation(FVector(1800,1000,92));}
    Reset();Check(D->NearStation(PC),TEXT("Guild preparation station accepts a visible body within range"));
    PC->SetMenuOpen(false);PC->ToggleDevelopmentPanel();
    if(PC->DevelopmentUI&&PC->DevelopmentUI->IsOpen())
    {const auto Before=O->Build;PC->DevelopmentUI->Choose(1,2);Check(PC->DevelopmentUI->Preview().Attributes[2]==Before.Attributes[2]+1&&O->Build.Attributes==Before.Attributes&&O->Hp==60,TEXT("Panel ghost preview changes no authority attribute, point or HP"));PC->DevelopmentUI->Choose(8,0);Check(!O->DevelopmentKind,TEXT("Apply arms a local draft without starting or spending"));PC->DevelopmentUI->Close();}
    else{Check(false,TEXT("Native preparation panel opens"));}
    Body->SetActorLocation(LDObjective::Position(2)+FVector(0,50,92));Send(3,ELDSkill::None,FGuid(),LDDevelopment::PlanFrom(O->Build));Check(O->DevelopmentKind==3,TEXT("West station supports the same held preparation transaction"));D->Release(PC,O->Life,Token);
    Body->SetActorLocation(LDObjective::Position(1)+FVector(0,50,92));
    auto Plan=LDDevelopment::PlanFrom(O->Build);Plan.Attributes[2]++;const auto Original=O->Build;
    Send(3,ELDSkill::None,FGuid(),Plan);Check(O->DevelopmentKind==3&&O->Build.Attributes==Original.Attributes&&C->StateFor(Body)->Action==8,TEXT("Apply starts one held action without changing the build"));
    Hold(1.9);Check(O->Build.Revision==Original.Revision,TEXT("Preparation cannot commit before two seconds"));Hold(.11);
    Check(O->Build.Attributes[2]==10&&O->Build.AttributePoints==5&&O->Build.Revision==Original.Revision+1&&!O->DevelopmentKind,TEXT("Preparation commits attributes and cost once at two seconds"));
    Check(O->Hp==60&&O->MaxHp==100&&C->StateFor(Body)->Hp==60,TEXT("Preparation recalculates maximum HP without healing"));
    PC->ServerDevelopmentRequest(O->Match,O->Life,Token,3,ELDSkill::None,FGuid(),Plan);Advance(.1);
    Check(O->Build.AttributePoints==5,TEXT("Replayed completed request cannot spend points twice"));
    Send(3,ELDSkill::None,FGuid(),Plan);Check(!O->DevelopmentKind&&O->Build.AttributePoints==5,TEXT("Stale revision rejects atomically"));
    Plan=LDDevelopment::PlanFrom(O->Build);Plan.Attributes[0]=16;Plan.Attributes[1]=16;Send(3,ELDSkill::None,FGuid(),Plan);Check(!O->DevelopmentKind,TEXT("Overspending attribute points rejects"));
    Plan=LDDevelopment::PlanFrom(O->Build);Plan.Ranks[3]=1;Send(3,ELDSkill::None,FGuid(),Plan);Check(!O->DevelopmentKind&&!O->Build.Ranks[3],TEXT("Rogue cannot class-train Spark"));
    Plan=LDDevelopment::PlanFrom(O->Build);Plan.Slots[1]=Plan.Slots[0];Send(3,ELDSkill::None,FGuid(),Plan);Check(!O->DevelopmentKind,TEXT("Duplicate equipped skills reject"));
    Plan=LDDevelopment::PlanFrom(O->Build);Plan.Subclass=5;Send(3,ELDSkill::None,FGuid(),Plan);Check(!O->DevelopmentKind&&!O->Build.Subclass,TEXT("Subclass must belong to the starting class"));
    Plan=LDDevelopment::PlanFrom(O->Build);Plan.Slots[3]=ELDSkill::Spark;Send(3,ELDSkill::None,FGuid(),Plan);Check(!O->DevelopmentKind,TEXT("Unlearned equipped skill rejects"));
    auto CancelPlan=[&](){Send(3,ELDSkill::None,FGuid(),LDDevelopment::PlanFrom(O->Build));};
    CancelPlan();D->Release(PC,O->Life,Token);Hold(2.1);Check(!O->DevelopmentKind,TEXT("Release cancels preparation without a commit"));
    CancelPlan();Advance(.251);Check(!O->DevelopmentKind,TEXT("Stale hold cancels after 250 milliseconds"));
    CancelPlan();D->RefreshHold(PC,O->Life,Token,++Sequence,true);D->RefreshHold(PC,O->Life,Token,++Sequence,false);Check(!O->DevelopmentKind,TEXT("Movement pulse cannot be erased by a later stationary hold"));
    CancelPlan();Body->SetActorLocation(Body->GetActorLocation()+FVector(3,0,0));Hold(.1);Check(!O->DevelopmentKind,TEXT("Authoritative displacement cancels preparation"));
    CancelPlan();++O->Build.Revision;Hold(.1);Check(!O->DevelopmentKind,TEXT("Build change during preparation cancels the stale plan"));
    CancelPlan();Body->SetActorLocation(FVector(-1800,-900,92));Hold(.1);Check(!O->DevelopmentKind,TEXT("Leaving preparation range cancels"));Reset();
    C->StateFor(Body)->LastDamage=W->GetTimeSeconds();CancelPlan();Check(!O->DevelopmentKind,TEXT("Recent damage taken blocks preparation"));Advance(6.01);
    C->StateFor(Body)->LastDealt=W->GetTimeSeconds();CancelPlan();Check(!O->DevelopmentKind,TEXT("Recent positive damage dealt blocks preparation"));Advance(6.01);
    C->StateFor(Body)->ProtectionUntil=W->GetTimeSeconds()+10;CancelPlan();Check(!O->DevelopmentKind,TEXT("Protection blocks preparation"));C->StateFor(Body)->ProtectionUntil=0;
    CancelPlan();Check(C->RequestBody(Body,0,FVector(0,-1,0),Why)&&!O->DevelopmentKind&&C->StateFor(Body)->Action==1,TEXT("Basic attack interrupts preparation through common arbitration"));Advance(1);
    Plan=LDDevelopment::PlanFrom(O->Build);PC->ServerDevelopmentRelease(O->Match,O->Life,Token+1);Send(3,ELDSkill::None,FGuid(),Plan);Check(!O->DevelopmentKind,TEXT("Release before queued acceptance prevents a held action"));
    Reset();O->Build.Level=3;Plan=LDDevelopment::PlanFrom(O->Build);Plan.Subclass=4;Send(3,ELDSkill::None,FGuid(),Plan);Check(!O->DevelopmentKind,TEXT("Subclass is unavailable before level four"));Reset();
    Plan=LDDevelopment::PlanFrom(O->Build);Plan.Subclass=4;Send(3,ELDSkill::None,FGuid(),Plan);Hold(2.01);
    Check(O->Build.Subclass==4&&O->Build.SkillPoints==3&&O->Build.Ranks==LDDevelopment::StartingBuild(1).Ranks,TEXT("Watcher choice is free, permanent, and grants no skill"));
    Plan=LDDevelopment::PlanFrom(O->Build);Plan.Subclass=3;Send(3,ELDSkill::None,FGuid(),Plan);Check(!O->DevelopmentKind&&O->Build.Subclass==4,TEXT("Permanent subclass cannot be changed"));
    Plan=LDDevelopment::PlanFrom(O->Build);Plan.Ranks[1]=2;Plan.Slots[2]=ELDSkill::Step;Send(3,ELDSkill::None,FGuid(),Plan);Hold(2.01);
    Check(O->Build.Ranks[1]==2&&O->Build.SkillPoints==1,TEXT("Class learning and rank one-to-two charge cumulative skill points"));
    Plan=LDDevelopment::PlanFrom(O->Build);Plan.Ranks[1]=3;Send(3,ELDSkill::None,FGuid(),Plan);Check(!O->DevelopmentKind&&O->Build.Ranks[1]==2,TEXT("Rank two-to-three requires two additional skill points"));
    Check(C->RequestEquipped(Body,O->Build.Revision,2,9000,FVector::ForwardVector,Why),TEXT("Station-trained Step activates through the equipped path"));
    const double Cooldown=O->Build.Cooldowns[1];Advance(.3);Body->GetCharacterMovement()->StopMovementImmediately();Plan=LDDevelopment::PlanFrom(O->Build);Plan.Slots[2]=ELDSkill::None;Send(3,ELDSkill::None,FGuid(),Plan);Hold(2.01);
    Check(O->Build.Cooldowns[1]==Cooldown&&!LDDevelopment::Equipped(O->Build,ELDSkill::Step),TEXT("Unequipping retains the original absolute skill cooldown"));
    Send(1,ELDSkill::Spark);const FGuid Spark=O->Books.IsEmpty()?FGuid():O->Books.Last().Id;const int64 XP=O->Xp;const auto BeforeRead=O->Build;
    Check(O->Books.Num()==1&&O->Gold==360&&Spark.IsValid(),TEXT("Guild purchase charges exactly forty gold and creates one private unique book"));
    PC->ServerDevelopmentRequest(O->Match,O->Life,Token,1,ELDSkill::Spark,FGuid(),FLDBuildPlan());Advance(.1);Check(O->Gold==360&&O->Books.Num()==1,TEXT("Duplicate purchase request cannot charge or create twice"));
    Send(1,ELDSkill::Heave);Check(O->Gold==360&&O->Books.Num()==1,TEXT("Bookshop rejects skills outside the four-book stock"));
    O->Gold=39;Send(1,ELDSkill::Patch);Check(O->Gold==39&&O->Books.Num()==1,TEXT("Insufficient gold cannot purchase"));O->Gold=360;
    Body->SetActorLocation(LDObjective::Position(2)+FVector(0,50,92));Send(1,ELDSkill::Patch);Check(O->Books.Num()==1,TEXT("West preparation station does not sell Guild books"));
    Body->SetActorLocation(FVector(-1800,-900,92));Send(2,ELDSkill::None,Spark);Hold(2.9);Check(O->Books.Num()==1&&!O->Build.Ranks[3],TEXT("Reading away from stations does not consume before three seconds"));Hold(.11);
    Check(O->Books.IsEmpty()&&O->Build.Ranks[3]==1&&!D->OwnedBook(PC,Spark),TEXT("Reading consumes exactly one owned book and teaches cross-class Spark rank one"));
    Check(O->Xp==XP&&O->Build.SkillPoints==BeforeRead.SkillPoints&&O->Build.Slots==BeforeRead.Slots&&O->Build.Cooldowns==BeforeRead.Cooldowns,TEXT("Book reading grants no XP, points, auto-equipment or cooldown reset"));
    Send(2,ELDSkill::None,Spark);Hold(3.01);Check(O->Build.Ranks[3]==1,TEXT("Consumed book cannot be replayed to rank up"));
    Body->SetActorLocation(LDObjective::Position(1)+FVector(0,50,92));Send(1,ELDSkill::Spark);const auto Known=O->Books.Last().Id;Send(2,ELDSkill::None,Known);Check(!O->DevelopmentKind&&D->OwnedBook(PC,Known),TEXT("Reading a known skill rejects and retains the physical book"));
    Send(1,ELDSkill::Patch);const auto Patch=O->Books.Last().Id;Send(2,ELDSkill::None,Patch);D->Release(PC,O->Life,Token);Hold(3.01);Check(D->OwnedBook(PC,Patch)&&!O->Build.Ranks[4],TEXT("Interrupted reading retains the book without knowledge"));
    Send(2,ELDSkill::None,FGuid::NewGuid());Check(!O->DevelopmentKind,TEXT("Forged or foreign ownership ID cannot read"));
    Send(2,ELDSkill::None,Patch);D->OwnedBook(PC,Patch)->State=0;Hold(.1);Check(!O->DevelopmentKind&&!O->Build.Ranks[4],TEXT("Ownership loss cancels reading before consumption"));
    for(TActorIterator<ALDBook> It(W);It;++It){if(It->Identity==Patch){It->State=1;}}
    Send(2,ELDSkill::None,Patch);const double Due=O->DevelopmentEndsAt;
    auto* Attacker=W->SpawnActor<ALDCharacter>(Body->GetActorLocation()+FVector(70,0,0),FRotator::ZeroRotator);Attacker->GetCharacterMovement()->DisableMovement();
    C->RequestBody(Attacker,0,-FVector::ForwardVector,Why);C->StateFor(Attacker)->ImpactAt=Due;Hold(3.01);
    Check(!O->DevelopmentKind&&!O->Build.Ranks[4]&&D->OwnedBook(PC,Patch)&&C->StateFor(Body)->Hp<60,TEXT("Positive damage on the reading deadline wins over consumption"));Attacker->Destroy();Advance(6.01);
    Send(2,ELDSkill::None,Patch);Hold(3.01);Send(1,ELDSkill::Parley);Send(2,ELDSkill::None,O->Books.Last().Id);Hold(3.01);
    Check(O->Build.Ranks[3]&&O->Build.Ranks[4]&&O->Build.Ranks[6]&&O->Build.Ranks[1]&&O->Build.Ranks[5]&&O->Build.Ranks[8]&&O->Build.Slots.Num()==4,TEXT("Learned library can exceed four while equipment remains four shared slots"));
    Plan=LDDevelopment::PlanFrom(O->Build);Plan.Ranks[3]=2;Plan.Slots={ELDSkill::Spark,ELDSkill::Patch,ELDSkill::Step,ELDSkill::Survey};Send(3,ELDSkill::None,FGuid(),Plan);Hold(2.01);
    Check(O->Build.Ranks[3]==2&&O->Build.SkillPoints==0&&O->Build.Cooldowns[1]==Cooldown,TEXT("Book-learned cross-class skill can rank up for SP without resetting another cooldown"));
    Send(1,ELDSkill::Patch);Send(1,ELDSkill::Step);Send(1,ELDSkill::Parley);O->Gold=100;const int32 Capacity=O->Books.Num();Send(1,ELDSkill::Spark);
    Check(Capacity==4&&O->Books.Num()==4&&O->Gold==100,TEXT("Four unread books is a strict per-owner capacity"));
    const auto Retained=O->Build;const auto Dropped=O->Books;const uint32 Life=O->Life;M->EndContenderLife(PC,W->GetTimeSeconds());Body->Destroy();
    int32 DropCount=0;TSet<FGuid> Ids;for(TActorIterator<ALDBook> It(W);It;++It){if(It->State==0){++DropCount;Ids.Add(It->Identity);}}
    Check(O->Books.IsEmpty()&&DropCount==4&&Ids.Num()==4&&O->Build.Ranks==Retained.Ranks,TEXT("Death drops each unread unique book once and retains learned knowledge"));
    PC->ServerDevelopmentRequest(O->Match,Life,++Token,1,ELDSkill::Spark,FGuid(),FLDBuildPlan());Advance(.1);Check(O->Books.IsEmpty(),TEXT("Dead contender cannot buy books"));
    M->ReturnDueContenders(O->ReturnAt+.01);Body=CastChecked<ALDCharacter>(PC->GetPawn());C->StateFor(Body)->ProtectionUntil=0;Body->SetActorLocation(LDObjective::Position(1)+FVector(0,50,92));
    PC->ServerDevelopmentRequest(O->Match,Life,++Token,1,ELDSkill::Spark,FGuid(),FLDBuildPlan());Advance(.1);Check(O->Life==Life+1&&O->Books.IsEmpty()&&O->Build.Ranks==Retained.Ranks,TEXT("Old-life request cannot restore or buy inventory on the returned body"));
    M->Vision->Tick(.1f);FGuid Observed;for(const auto& E:PC->LocalView()->LastView().Entities){if(E.Kind==ELDObservedKind::Book){Observed=E.Id;break;}}
    Send(4,ELDSkill::None,Dropped[0].Id);Check(O->Books.IsEmpty(),TEXT("Canonical book identity cannot substitute for a filtered observation ID"));
    Body->SetActorLocation(FVector(-1800,-900,92));Send(4,ELDSkill::None,Observed);Check(O->Books.IsEmpty(),TEXT("Previously visible book cannot be picked up out of range"));Body->SetActorLocation(LDObjective::Position(1)+FVector(0,50,92));
    Send(4,ELDSkill::None,Observed);Check(O->Books.Num()==1,TEXT("Visible dropped book can be picked up through an opaque observation ID"));
    Send(4,ELDSkill::None,Observed);Check(O->Books.Num()==1,TEXT("Repeated pickup cannot duplicate a physical book"));
    // Two affordable outcomes from the same Rogue start, through the real held transaction.
    Reset();Plan=LDDevelopment::PlanFrom(O->Build);Plan.Attributes[3]+=2;Plan.Attributes[4]+=4;Plan.Ranks[1]=1;Plan.Ranks[8]=2;Plan.Subclass=4;Plan.Slots[2]=ELDSkill::Step;Send(3,ELDSkill::None,FGuid(),Plan);Hold(2.01);const auto Watcher=O->Build;
    Reset();Plan=LDDevelopment::PlanFrom(O->Build);Plan.Attributes[0]+=4;Plan.Attributes[1]+=2;Plan.Ranks[2]=2;Plan.Ranks[1]=1;Plan.Subclass=3;Plan.Slots={ELDSkill::Pickpocket,ELDSkill::Ambush,ELDSkill::Step,ELDSkill::Survey};Send(3,ELDSkill::None,FGuid(),Plan);Hold(2.01);
    Check(Watcher.Subclass==4&&O->Build.Subclass==3&&Watcher.Attributes!=O->Build.Attributes&&Watcher.Slots!=O->Build.Slots&&LDDevelopment::Valid(Watcher)&&LDDevelopment::Valid(O->Build),TEXT("Same starting Rogue produces two distinct affordable builds via held preparation"));
    Send(2,ELDSkill::None,O->Books[0].Id);GetWorld()->GetGameState<ALDGameState>()->EndReason=2;Advance(.1);Check(!O->DevelopmentKind,TEXT("Results cancel any pending development action"));
    const auto Withdrawal=O->Books[0].Id;D->DropBooks(PC,Body->GetActorLocation());D->DropBooks(PC,Body->GetActorLocation());int32 Withdrawn=0;for(TActorIterator<ALDBook> It(W);It;++It){if(It->Identity==Withdrawal&&It->State==0&&!It->Holder.IsValid()){++Withdrawn;}}Check(O->Books.IsEmpty()&&Withdrawn==1,TEXT("Withdrawal drop is idempotent and clears unread inventory"));
}

void ALDDevelopmentProbe::PreparationServerTick()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* G=GetWorld()->GetGameState<ALDGameState>();
    if(!Phase){for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){if(auto* PC=Cast<ALDPlayerController>(It->Get());PC&&PC->GetPawn()){if(PC->ProbeRole==1){A=PC;}if(PC->ProbeRole==2){B=PC;}}}if(A.IsValid()&&B.IsValid()){Check(G->LobbyCount==2,TEXT("Preparation network has two remote owners"));Enter(1);}return;}
    if(Phase==8){if(FPlatformTime::Seconds()-PhaseAt>3){Finish();}return;}
    if(!A.IsValid()||!B.IsValid()){Check(false,TEXT("Preparation client disconnected early"));Finish();return;}
    const double Elapsed=FPlatformTime::Seconds()-PhaseAt;auto* AS=M->StateFor(A.Get());auto* BS=M->StateFor(B.Get());
    if(Phase==1&&AS->bReady&&BS->bReady){Enter(2);}
    else if(Phase==2&&G->bMatchStarted&&Elapsed>1)
    {
        for(auto* PC:{A.Get(),B.Get()}){auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());Body->SetActorLocation(LDObjective::Position(1)+FVector(PC==A.Get()?-40:40,40,92));Body->GetCharacterMovement()->StopMovementImmediately();M->Combat->StateFor(Body)->ProtectionUntil=0;M->AwardExperience(PC,270);M->StateFor(PC)->Gold=160;M->SyncOwner(PC);}
        Check(AS->Build.ClassId==1&&BS->Build.ClassId==2,TEXT("Independent starting classes remain private"));Enter(3);
    }
    else if(Phase==3&&Elapsed>2)
    {Check(AS->Gold==120&&BS->Gold==120&&AS->Books.Num()==1&&BS->Books.Num()==1,TEXT("Delayed duplicate purchase charges each owner once"));Check(AS->Books.Num()&&BS->Books.Num()&&AS->Books[0].Id!=BS->Books[0].Id,TEXT("Books have distinct private ownership identities"));if(AS->Books.Num()){B->ServerDevelopmentRequest(BS->Match,BS->Life,7002,2,ELDSkill::None,AS->Books[0].Id,FLDBuildPlan());M->Combat->Sample(GetWorld()->GetTimeSeconds());}Check(!BS->DevelopmentKind,TEXT("Adversarial foreign-book ID fixture fails ownership validation"));Enter(4);}
    else if(Phase==4&&Elapsed>4.7)
    {Check(AS->Build.Ranks[3]==1&&AS->Books.IsEmpty()&&AS->Build.SkillPoints==3,TEXT("Delayed held read consumes once without spending SP"));Check(BS->Books.Num()==1&&!BS->DevelopmentKind,TEXT("Released remote read retains its book"));Enter(5);}
    else if(Phase==5&&Elapsed>3.7)
    {Check(AS->Build.Subclass==4&&AS->Build.Ranks[1]==1&&AS->Build.AttributePoints==5&&AS->Build.SkillPoints==2&&AS->Build.Slots[2]==ELDSkill::Spark,TEXT("Delayed preparation commits complete plan with exact costs"));Enter(6);}
    else if(Phase==6&&Elapsed>2)
    {Check(AS->Build.SkillPoints==2&&AS->Build.AttributePoints==5&&!AS->DevelopmentKind,TEXT("Stale and replayed remote plans cannot charge twice"));Enter(7);}
    else if(Phase==7&&Elapsed>1.5){Enter(8);}
}
void ALDDevelopmentProbe::PreparationClientTick()
{
    auto* PC=Cast<ALDPlayerController>(GetOwner());if(!PC||!PC->OwnerState.Match){return;}const auto& O=PC->OwnerState;const double Wall=FPlatformTime::Seconds();
    if(Wall-LastPing>.2){LastPing=Wall;PC->ServerProbePing(Wall);}if(PC->ProbePhase!=Phase){Phase=PC->ProbePhase;PhaseAt=Wall;bChecked=false;bChoiceSent=false;}
    const double Elapsed=Wall-PhaseAt;const bool IsA=Role==TEXT("A");
    if(Phase==1){if(!bChoiceSent){PC->ChooseLobbyClass(IsA?1:2);bChoiceSent=true;}if(!bReadySent&&O.Build.ClassId==(IsA?1:2)&&Elapsed>.4){PC->ToggleLobbyReady();bReadySent=true;}}
    if(Phase==2&&!bStartSent&&O.bLobbyHost){PC->StartLobby();bStartSent=true;}
    if(Phase==3&&!bChoiceSent&&Elapsed>.4){bChoiceSent=true;for(int I=0;I<2;++I){PC->ServerDevelopmentRequest(O.Match,O.Life,7001,1,IsA?ELDSkill::Spark:ELDSkill::Patch,FGuid(),FLDBuildPlan());}}
    if(Phase==4&&!bChoiceSent&&Elapsed>.4&&O.Books.Num()){bChoiceSent=true;PC->ServerDevelopmentRequest(O.Match,O.Life,7010,2,ELDSkill::None,O.Books[0].Id,FLDBuildPlan());}
    if(Phase==4&&Elapsed>.4&&Elapsed<4&&Wall-SkillRefreshAt>.1){SkillRefreshAt=Wall;PC->ServerDevelopmentHold(O.Match,O.Life,7010,uint32(Elapsed*1000),false);if(!IsA&&Elapsed>.9){PC->ServerDevelopmentRelease(O.Match,O.Life,7010);}}
    if(Phase==5&&IsA&&!bChoiceSent&&Elapsed>.4){bChoiceSent=true;auto Plan=LDDevelopment::PlanFrom(O.Build);Plan.Attributes[2]++;Plan.Ranks[1]=1;Plan.Slots[2]=ELDSkill::Spark;Plan.Slots[3]=ELDSkill::Step;Plan.Subclass=4;PC->ServerDevelopmentRequest(O.Match,O.Life,7020,3,ELDSkill::None,FGuid(),Plan);}
    if(Phase==5&&IsA&&Elapsed>.4&&Elapsed<3&&Wall-SkillRefreshAt>.1){SkillRefreshAt=Wall;PC->ServerDevelopmentHold(O.Match,O.Life,7020,uint32(Elapsed*1000),false);}
    if(Phase==6&&IsA&&!bChoiceSent&&Elapsed>.4){bChoiceSent=true;auto Plan=LDDevelopment::PlanFrom(O.Build);--Plan.ExpectedRevision;PC->ServerDevelopmentRequest(O.Match,O.Life,7030,3,ELDSkill::None,FGuid(),Plan);PC->ServerDevelopmentRequest(O.Match,O.Life,7020,3,ELDSkill::None,FGuid(),Plan);}
    if(Phase==7&&!bChecked&&Elapsed>.5)
    {
        bChecked=true;Check(O.bActive&&O.Build.ClassId==(IsA?1:2),TEXT("Owner receives its own class after delayed transactions"));Check(O.Gold==120&&O.Xp==270,TEXT("Purchasing and reading award no XP"));
        Check(IsA?O.Books.IsEmpty()&&O.Build.Ranks[3]==1:O.Books.Num()==1&&!O.Build.Ranks[4],TEXT("Owner receives independent book and learned-library state"));
        Check(IsA?O.Build.Subclass==4&&O.Build.SkillPoints==2:!O.Build.Subclass&&O.Build.SkillPoints==3,TEXT("Preparation changes only the confirming owner's build"));
        int32 Books=0,Bodies=0;for(TActorIterator<ALDBook> It(GetWorld());It;++It){++Books;}for(TActorIterator<ALDCharacter> It(GetWorld());It;++It){++Bodies;}
        Check(!Books&&Bodies==1&&PC->LocalView(),TEXT("Client has no canonical book or opponent private body carrier"));
    }
    if(Phase==8&&Elapsed>1){Finish();}
}

void ALDDevelopmentProbe::PreparationNormalTick()
{
    auto* W=GetWorld();auto* M=W->GetAuthGameMode<ALDGameMode>();auto* PC=Cast<ALDPlayerController>(W->GetFirstPlayerController());if(!PC||!PC->GetPawn()){return;}
    auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());auto* O=M->StateFor(PC);const double Now=W->GetTimeSeconds();FString Why;
    if(!Phase){if(Now<1||UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(this)){return;}Check(!O->Gold&&!O->Xp&&O->Books.IsEmpty(),TEXT("Normal practice starts without currency, XP or books"));M->SetLobbyChoice(PC,1,true,Why);M->StartDevelopmentMatch(PC,Why);PC->SetMenuOpen(false);Phase=1;NormalLastPosition=Body->GetActorLocation();return;}
    if(Now-LastPing>10){LastPing=Now;UE_LOG(LogTemp,Display,TEXT("LD_NORMAL phase=%d position=%s speed=%.1f path=%d/%d ignore=%d stage=%d feedback=%s"),Phase,*Body->GetActorLocation().ToCompactString(),Body->GetVelocity().Size2D(),NormalPathIndex,NormalPath.Num(),PC->IsMoveInputIgnored(),O->ContractStage,*PC->IntentFeedback);}
    NormalTravel+=FVector::Dist2D(NormalLastPosition,Body->GetActorLocation());NormalLastPosition=Body->GetActorLocation();
    auto Travel=[&](uint8 Site)
    {
        const FVector Goal=LDObjective::Position(Site)+FVector(0,50,92);
        if(FVector::DistSquared2D(Body->GetActorLocation(),Goal)<35*35){Body->GetCharacterMovement()->StopMovementImmediately();Body->SetSprint(false);NormalPath.Reset();return true;}
        if(NormalPath.IsEmpty())
        {
            auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(this,Body->GetActorLocation(),Goal,Body);
            if(Path&&Path->IsValid()&&!Path->IsPartial()){NormalPath=Path->PathPoints;}
            else
            {
                // A player does not require AI navigation data. The cooked migration
                // map can lack a nav route; steer only along a swept, walkable segment.
                const FVector From=Body->GetActorLocation();FHitResult Block;
                if(!LDGround::ClearSegment(W,From,Goal)||W->SweepSingleByObjectType(Block,From+FVector(0,0,30),Goal+FVector(0,0,30),FQuat::Identity,FCollisionObjectQueryParams(ECC_WorldStatic),FCollisionShape::MakeSphere(42))){return false;}
                NormalPath={From,Goal};UE_LOG(LogTemp,Display,TEXT("LD_NORMAL clear-segment steering; no AI navigation path available"));
            }
            NormalPathIndex=1;
        }
        while(NormalPath.IsValidIndex(NormalPathIndex)&&FVector::DistSquared2D(Body->GetActorLocation(),NormalPath[NormalPathIndex])<45*45){++NormalPathIndex;}
        const FVector Target=NormalPath.IsValidIndex(NormalPathIndex)?NormalPath[NormalPathIndex]:Goal;Body->SetSprint(true);Body->AddMovementInput((Target-Body->GetActorLocation()).GetSafeNormal2D());return false;
    };
    if(Phase==1&&Travel(1)&&Now>=O->WorkReadyAt&&Now>=O->ProtectedUntil){PC->SetInteractionHeld(true);Phase=2;}
    else if(Phase==2&&O->ContractStage==1){PC->SetInteractionHeld(false);Check(true,TEXT("Courier contract accepted through ordinary context input"));Phase=3;}
    else if(Phase==3&&Travel(2)){PC->SetInteractionHeld(true);NormalHoldAt=Now;Phase=4;}
    else if(Phase==4&&O->ContractStage==2){PC->SetInteractionHeld(false);Check(Now-NormalHoldAt>=3,TEXT("Native travel reaches West and original three-second collection completes"));Phase=5;}
    else if(Phase==5&&Travel(1)){PC->SetInteractionHeld(true);NormalHoldAt=Now;Phase=6;}
    else if(Phase==6&&O->ContractStage==0){PC->SetInteractionHeld(false);++NormalDeliveries;Check(O->Gold==25*NormalDeliveries&&O->Xp==45*NormalDeliveries&&Now-NormalHoldAt>=3,TEXT("Original delivery timer awards exactly twenty-five gold and forty-five XP"));Phase=NormalDeliveries==2?7:1;}
    else if(Phase==7){Check(O->Gold==50&&O->Xp==90&&O->Build.Level==2,TEXT("Two completed courier jobs fund a forty-gold book from an ordinary level-two build"));PC->BuyBook(ELDSkill::Spark);Phase=8;}
    else if(Phase==8&&O->Books.Num()){Check(O->Gold==10&&O->Xp==90,TEXT("Real purchase spends earned currency without XP"));PC->StartDevelopmentHold(2,O->Books[0].Id,FLDBuildPlan());NormalHoldAt=Now;Phase=9;}
    else if(Phase==9&&O->Build.Ranks[3]){PC->StopDevelopmentHold();Check(Now-NormalHoldAt>=3&&O->Books.IsEmpty()&&O->Build.SkillPoints==1&&!LDDevelopment::Equipped(O->Build,ELDSkill::Spark),TEXT("Ordinary three-second reading learns cross-class Spark without equipping or spending SP"));PC->ToggleDevelopmentPanel();if(!PC->DevelopmentUI||!PC->DevelopmentUI->IsOpen()){Check(false,TEXT("Normal preparation panel opens"));Finish();return;}auto* UI=PC->DevelopmentUI.Get();UI->Choose(1,3);UI->Choose(1,2);UI->Choose(2,1);UI->Choose(3,2);UI->Choose(6,3);UI->Choose(3,3);UI->Choose(6,1);UI->Choose(8,0);Check(O->MaxHp==95&&O->Build.SkillPoints==1&&!O->DevelopmentKind,TEXT("Normal panel previews and arms without changing authority state"));UI->StartConfirm();NormalHoldAt=Now;Phase=10;}
    else if(Phase==10&&O->Build.Ranks[1])
    {PC->DevelopmentUI->Close();Check(Now-NormalHoldAt>=2&&!O->Build.AttributePoints&&!O->Build.SkillPoints&&O->Build.Slots[2]==ELDSkill::Spark,TEXT("Ordinary two-second preparation commits an affordable mixed four-slot build"));Check(O->MaxHp==100&&O->Hp==95,TEXT("Normal preparation increases maximum HP without healing"));Check(NormalTravel>4000&&O->Gold==10&&O->Xp==90,TEXT("Native movement completes both courier journeys with no resource grants or teleports"));Finish();}
}
