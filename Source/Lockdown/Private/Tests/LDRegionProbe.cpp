#include "Tests/LDDevelopmentProbe.h"
#include "Region/LDRegionDefinition.h"
#include "Region/LDRegionDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Objectives/LDObjectiveRules.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Development/LDDevelopmentDirector.h"
#include "Development/LDWorldChecks.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "Core/LDGroundQueries.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#if !UE_BUILD_SHIPPING
void ALDDevelopmentProbe::RegionCheck()
{
    auto* W=GetWorld();auto* M=W->GetAuthGameMode<ALDGameMode>();auto* D=LDRegion::Definition(W);auto* R=M->Region.Get();auto* C=M->Combat.Get();auto* G=W->GetGameState<ALDGameState>();auto* PC=CastChecked<ALDPlayerController>(W->GetFirstPlayerController());auto* O=M->StateFor(PC);auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());FString Why;uint32 Token=10000,Seq=0;FVector Aim=FVector::ForwardVector;
    Check(D&&R&&D->Validate(Why)&&M->Vision->Grid().CellSize==D->Grid.CellSize,TEXT("Saved region graph and vision grid load together"));
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);bool Routes=true;
    for(const auto& Route:D->Routes){for(int32 I=1;I<Route.Points.Num();++I){const auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(W,Route.Points[I-1],Route.Points[I]);Routes&=Path&&Path->IsValid()&&!Path->IsPartial();}}
    Check(Nav&&Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)&&Routes,TEXT("Every public, covered and outer graph segment has a complete navigation path"));
    const auto* Cover=D->Geometry.FindByPredicate([](const FLDRegionGeometry& E){return E.Name==TEXT("CoveredWest");});const auto* Plateau=D->Geometry.FindByPredicate([](const FLDRegionGeometry& E){return E.High;});
    Check(Cover&&!M->Vision->Ray({Cover->Position-FVector(0,100,0),720,0},Cover->Position+FVector(0,100,0)),TEXT("Authored covered route wall blocks actual geometric sight"));
    const FVector Low=Plateau->Position-FVector(Plateau->Size.X/2+180,0,0);Check(!M->Vision->Ray({Low,720,0},Plateau->Position),TEXT("Low regional sight cannot inspect the raised plateau"));Check(M->Vision->Ray({Plateau->Position,720,1},Low),TEXT("Raised regional sight retains the authored elevation rule"));
    Check(FVector::DistSquared2D(Body->GetActorLocation(),D->Heart)>=600*600,TEXT("Actual initial contender spawn is separated from Heart"));
    int32 Residents=0,Trained=0;for(TActorIterator<ALDResident> It(W);It;++It){++Residents;const auto* S=C->StateFor(*It);Trained+=S&&S->bHasNPCBuild&&LDDevelopment::Equipped(S->NPCBuild,ELDSkill::Patch)?1:0;It->GetCharacterMovement()->StopMovementImmediately();It->SetActorTickEnabled(false);if(auto* AI=Cast<AAIController>(It->GetController())){AI->StopMovement();}}
    Check(Residents==8&&Trained==1,TEXT("Eight peaceful residents include one authored equipped Patch build"));
    M->SetLobbyChoice(PC,1,true,Why);M->StartDevelopmentMatch(PC,Why);PC->SetMenuOpen(false);C->SetActorTickEnabled(false);Body->GetCharacterMovement()->StopMovementImmediately();O->ProtectedUntil=0;C->StateFor(Body)->ProtectionUntil=0;
    auto Advance=[&](double Seconds){W->TimeSeconds+=Seconds;C->Sample(W->GetTimeSeconds());};
    auto Place=[&](uint8 Site){Body->GetCharacterMovement()->StopMovementImmediately();Body->SetActorLocation(D->SitePosition(Site)+FVector(0,50,92));};
    auto Request=[&](uint8 Site){Seq=0;Advance(.4);PC->ServerContextRequest(O->Match,O->Life,++Token,Site,FGuid());C->Sample(W->GetTimeSeconds());};
    auto Hold=[&](double Seconds,bool Skill=false){for(double Left=Seconds;Left>1.e-7;){const double Dt=FMath::Min(.1,Left);W->TimeSeconds+=Dt;if(Skill){C->RefreshSkillHold(Body,Token,++Seq,false);}else{PC->ServerContextHold(O->Match,O->Life,Token,++Seq,false);}C->Sample(W->GetTimeSeconds());Left-=Dt;}};
    for(uint8 Class=0;Class<3;++Class)
    {
        O->Build=LDDevelopment::StartingBuild(Class);M->RefreshBuild(PC);O->Witness.Invalidate();const ELDSkill Skill=Class==0?ELDSkill::Heave:Class==1?ELDSkill::Survey:ELDSkill::Analyze;ALDCharterTask* Task=nullptr;
        for(TActorIterator<ALDCharterTask> It(W);It;++It){if(LDCharter::Skill(It->Service)==Skill){Task=*It;break;}}
        Body->SetActorLocation(Task->GetActorLocation()+FVector(0,50,92));Seq=0;const int64 Gold=O->Gold,XP=O->Xp;Check(C->RequestEquipped(Body,O->Build.Revision,uint8(O->Build.Slots.Find(Skill)),++Token,Aim,Why),FString::Printf(TEXT("Starting class %d has its actual equipped Charter path"),Class));Hold(LDDevelopment::ChannelSeconds(Skill)+.01,true);Check(O->Witness.IsValid()&&O->Gold==Gold&&O->Xp==XP,TEXT("Starting Charter earns a private receipt with no resources"));
    }
    O->Build=LDDevelopment::StartingBuild(1);M->RefreshBuild(PC);O->Gold=0;O->Xp=0;const auto Towns=D->Towns();int64 Lost=0;uint8 Previous=0;
    Place(1);Request(1);const auto FirstContract=O->ContractId;const uint8 First=O->ContractDestination;Check(O->ContractStage==1&&D->Node(First),TEXT("Accepted real courier has a private stable assigned town"));
    Place(D->Node(First==Towns[0]?Towns[1]:Towns[0])->Site);Request(D->Node(First==Towns[0]?Towns[1]:Towns[0])->Site);Hold(3.1);Check(O->ContractStage==1&&O->ContractId==FirstContract,TEXT("Wrong town cannot collect or mutate the destination"));
    M->Vision->SetPublicSensor(false,true);M->Vision->SetPublicSensor(true,true);Check(O->ContractDestination==First,TEXT("Unrelated public-source changes preserve the active destination"));
    for(int32 Job=0;Job<8;++Job)
    {
        if(Job){Advance(FMath::Max(0.,double(O->WorkReadyAt)-W->GetTimeSeconds())+.01);Place(1);Request(1);}
        const uint8 Destination=O->ContractDestination;Check(Destination&&(Job==0||Destination!=Previous),TEXT("Paid courier destinations alternate by region identity"));Previous=Destination;
        Place(D->Node(Destination)->Site);Request(D->Node(Destination)->Site);Hold(2.9);Check(O->ContractStage==1,TEXT("Collection cannot finish before three held seconds"));Hold(.11);Check(O->ContractStage==2,TEXT("Assigned collection commits after three seconds"));
        Place(1);Request(1);const int64 Gold=O->Gold,XP=O->Xp;Hold(3.01);Check(O->PaidJobs==Job+1&&O->Gold==Gold+25&&O->Xp==FMath::Min<int64>(420,XP+45)&&O->Commissions==Job+1&&O->DeliverySerial==Job+1,TEXT("Once-only delivery pays exact gold, XP and commission credit"));
        const auto Paid=O->PaidJobs;PC->ServerContextRequest(O->Match,O->Life,Token,1,FGuid());Advance(.1);Check(O->PaidJobs==Paid&&O->Gold==Gold+25,TEXT("Delivery request replay cannot pay again"));
        if(Job==0)
        {
            Request(1);Check(O->ContractStage==0,TEXT("Twenty-second post-delivery acceptance delay is enforced"));Advance(20);Request(1);const int64 Before=O->Gold;const uint8 Budget=O->PaidJobs;const int32 Credits=O->Commissions;M->EndContenderLife(PC,W->GetTimeSeconds());Lost=Before-O->Gold;Body->Destroy();Check(O->ContractStage==0&&!O->ContractId.IsValid()&&O->PaidJobs==Budget&&O->Commissions==Credits,TEXT("Death cancels active job but retains paid budget and unused credits"));
            W->TimeSeconds=O->ReturnAt+.01;M->ReturnDueContenders(W->GetTimeSeconds());Body=CastChecked<ALDCharacter>(PC->GetPawn());C->StateFor(Body)->ProtectionUntil=0;O->ProtectedUntil=0;Check(FVector::DistSquared2D(Body->GetActorLocation(),D->Heart)>=600*600,TEXT("Actual returned contender remains in the separated city pool"));
        }
    }
    Check(O->Gold==200-Lost&&O->Xp==360&&O->PaidJobs==8&&O->Commissions==8,TEXT("Eight production deliveries have a finite match-wide payout budget"));
    O->Commissions=0;Advance(21);Place(1);Request(1);Check(O->PaidJobs==8&&O->ContractStage==0&&PC->IntentFeedback.Contains(TEXT("eight")),TEXT("Spending credits cannot reopen the eight-paid-job budget"));
    const int64 BeforeVisits=O->Xp;for(uint8 Id:Towns){Body->SetActorLocation(D->Node(Id)->Position+FVector(0,0,92));Advance(.05);}Check(O->VisitedRegions.IsEmpty()&&O->Xp==BeforeVisits,TEXT("Teleport placement and camera coverage do not award traversal XP"));
    for(uint8 Site:{uint8(1),uint8(2),uint8(6)})
    {
        Place(Site);auto Plan=LDDevelopment::PlanFrom(O->Build);++Plan.Attributes[1];const auto Revision=O->Build.Revision;Advance(.4);Seq=0;PC->ServerDevelopmentRequest(O->Match,O->Life,++Token,3,ELDSkill::None,FGuid(),Plan);C->Sample(W->GetTimeSeconds());
        for(int32 I=0;I<21;++I){W->TimeSeconds+=.1;PC->ServerDevelopmentHold(O->Match,O->Life,Token,++Seq,false);C->Sample(W->GetTimeSeconds());}
        Check(O->Build.Revision==Revision+1&&O->Build.Attributes[1]==Plan.Attributes[1],TEXT("Each configured settlement station commits real held preparation"));
    }
    Place(1);const int64 MarketGold=O->Gold;O->Gold=40;M->SyncOwner(PC);Advance(.4);PC->ServerDevelopmentRequest(O->Match,O->Life,++Token,1,ELDSkill::Spark,FGuid(),FLDBuildPlan());C->Sample(W->GetTimeSeconds());Check(O->Books.Num()==1&&O->Gold==0&&O->Xp==BeforeVisits,TEXT("Relocated Guild market sells an actual forty-gold book without XP"));O->Gold=MarketGold;M->SyncOwner(PC);
    // An authored NPC is driven by its production behavior, with explicit damage/clock fixtures.
    ALDResident* Healer=nullptr;for(TActorIterator<ALDResident> It(W);It;++It){if(C->StateFor(*It)->bHasNPCBuild){Healer=*It;break;}}
    const double MaxHP=LDDevelopment::MaxHP(C->StateFor(Healer)->NPCBuild);C->StateFor(Healer)->Hp=MaxHP-30;const double BeforeHeal=C->StateFor(Healer)->Hp;R->UpdateResident(Healer);
    Check(C->StateFor(Healer)->Action==7&&C->StateFor(Healer)->Skill==ELDSkill::Patch,TEXT("Authored injured NPC starts Patch through shared equipped activation"));
    for(int32 I=0;I<21;++I){W->TimeSeconds+=.1;R->UpdateResident(Healer);C->Sample(W->GetTimeSeconds());}
    Check(C->StateFor(Healer)->Hp>BeforeHeal&&C->StateFor(Healer)->Hp<=MaxHP&&C->StateFor(Healer)->NPCBuild.Cooldowns[4]>W->GetTimeSeconds(),TEXT("NPC holds, heals and retains the actual Patch cooldown"));
    auto NPCBuild=C->StateFor(Healer)->NPCBuild;NPCBuild.Slots[0]=ELDSkill::None;C->RegisterNPCBuild(Healer,NPCBuild);Check(!R->UpdateResident(Healer),TEXT("NPC behavior cannot bypass unequipped Patch"));
    NPCBuild.Slots[0]=ELDSkill::Patch;C->RegisterNPCBuild(Healer,NPCBuild);const auto SavedNPC=C->StateFor(Healer)->NPCBuild;const FGuid NPCWallet=M->WorldChecks->WalletId(Healer);const int64 BeforeResidentGold=O->Gold,BeforeResidentXP=O->Xp;C->StateFor(Healer)->Hp=1;Body->SetActorLocation(Healer->GetActorLocation()-FVector(80,0,0));C->RequestBody(Body,0,Aim,Why);Advance(.16);Advance(20.1);ALDResident* ReturnedHealer=nullptr;for(TActorIterator<ALDResident> It(W);It;++It){if(M->WorldChecks->WalletId(*It)==NPCWallet){ReturnedHealer=*It;break;}}
    Check(ReturnedHealer&&C->StateFor(ReturnedHealer)->bHasNPCBuild&&C->StateFor(ReturnedHealer)->NPCBuild.Cooldowns==SavedNPC.Cooldowns&&C->StateFor(ReturnedHealer)->NPCBuild.Slots==SavedNPC.Slots&&O->Gold==BeforeResidentGold&&O->Xp==BeforeResidentXP,TEXT("Real trained-resident death/return retains build, absolute cooldowns and wallet without kill rewards"));
    // Actual sentinel attacks, simultaneous death and real destruction/return.
    auto* Sentinel=R->SentinelBody();const FGuid SentinelWallet=M->WorldChecks->WalletId(Sentinel);auto* SS=C->StateFor(Sentinel);Place(1);SS->Hp=40;Advance(6.1);Check(SS->Hp==100,TEXT("Living sentinel resets HP after six seconds without a legal target"));
    Body->SetActorLocation(D->Sentinel+FVector(650,0,0));Check(!R->SentinelCanTarget(Body),TEXT("Sentinel cannot target a contender beyond its origin leash"));
    auto* Civilian=W->SpawnActor<ALDResident>(D->Sentinel+FVector(80,0,0),FRotator::ZeroRotator);Civilian->SetActorTickEnabled(false);const double CivilHP=C->StateFor(Civilian)->Hp;C->RequestBody(Sentinel,0,Aim,Why);Advance(.31);Check(C->StateFor(Civilian)->Hp==CivilHP,TEXT("Sentinel melee never selects peaceful residents"));Civilian->Destroy();
    Body->SetActorLocation(D->Sentinel-FVector(80,0,0));C->StateFor(Body)->ProtectionUntil=W->GetTimeSeconds()+1;Check(!R->SentinelCanTarget(Body),TEXT("Protected contenders are not legal sentinel targets"));C->StateFor(Body)->ProtectionUntil=0;SS->BasicReady=0;SS->Action=0;const double HP=C->StateFor(Body)->Hp;
    Check(C->RequestBody(Sentinel,0,-Aim,Why)&&FMath::IsNearlyEqual(SS->ImpactAt-W->GetTimeSeconds(),.3),TEXT("Sentinel commits its real 300 ms melee windup"));Advance(.299);Check(C->StateFor(Body)->Hp==HP,TEXT("Sentinel deals no damage before windup"));Advance(.002);Check(C->StateFor(Body)->Hp==HP-12,TEXT("Sentinel impact deals exactly twelve damage"));Check(!C->RequestBody(Sentinel,0,-Aim,Why),TEXT("Sentinel start cadence is 1200 ms"));
    Advance(2);C->StateFor(Body)->Hp=1;SS->Hp=1;C->StateFor(Body)->BasicReady=0;SS->BasicReady=0;C->RequestBody(Body,0,Aim,Why);C->RequestBody(Sentinel,0,-Aim,Why);C->StateFor(Body)->ImpactAt=SS->ImpactAt;const int64 XPBeforeDeath=O->Xp;Advance(.31);
    Check(!O->bActive&&O->Xp==XPBeforeDeath&&!O->PaidSentinels,TEXT("Simultaneously dead credited finisher receives no sentinel reward"));
    W->TimeSeconds=O->ReturnAt+.01;M->ReturnDueContenders(W->GetTimeSeconds());Body=CastChecked<ALDCharacter>(PC->GetPawn());O->ProtectedUntil=0;C->StateFor(Body)->ProtectionUntil=0;
    const double Return=R->SentinelReturnAt();W->TimeSeconds=Return-.01;C->Sample(W->GetTimeSeconds());Check(!R->SentinelBody(),TEXT("Sentinel does not return before sixty seconds"));W->TimeSeconds=Return;C->Sample(W->GetTimeSeconds());Check(R->SentinelBody()&&C->StateFor(R->SentinelBody())->Hp==100&&M->WorldChecks->WalletId(R->SentinelBody())==SentinelWallet,TEXT("Sentinel returns exactly at sixty seconds with full HP"));
    for(int32 Kill=0;Kill<3;++Kill)
    {
        if(Kill){W->TimeSeconds=R->SentinelReturnAt();C->Sample(W->GetTimeSeconds());}
        Sentinel=R->SentinelBody();Body->SetActorLocation(D->Sentinel-FVector(80,0,0));C->StateFor(Body)->Hp=LDDevelopment::MaxHP(O->Build);const int64 Gold=O->Gold,XP=O->Xp;C->StateFor(Body)->BasicReady=0;
        for(int32 Strike=0;Strike<12&&R->SentinelBody();++Strike){C->RequestBody(Body,0,Aim,Why);Advance(.16);if(R->SentinelBody()){Advance(.76);}}
        Check(!R->SentinelBody()&&O->Gold==Gold+(Kill<2?25:0)&&O->Xp==FMath::Min<int64>(420,XP+(Kill<2?45:0))&&O->PaidSentinels==FMath::Min(Kill+1,2),TEXT("Actual sentinel defeat pays only the living finisher within the two-reward budget"));
    }
    // Sabotage uses the actual shared Analyze task; controlled clock boundaries are fixtures.
    Place(7);O->Build=LDDevelopment::StartingBuild(2);M->RefreshBuild(PC);C->StateFor(Body)->Action=0;Seq=0;
    Check(!C->RequestEquipped(Body,O->Build.Revision,1,++Token,Aim,Why)&&O->Build.Cooldowns[7]==0,TEXT("Insufficient Analyze score rejects before cooldown at public fourteen"));
    O->Build.Level=2;O->Build.Ranks[7]=2;const auto Flags=G->DefensiveFlags;Check(C->RequestEquipped(Body,O->Build.Revision,1,++Token,Aim,Why),TEXT("Equipped Analyze fourteen accepts actual watch control"));C->ReleaseSkillHold(PC,Token);Advance(2.1);Check(G->bPublicSensorActive&&!G->PublicSensorOfflineUntil&&O->Build.Cooldowns[7]>W->GetTimeSeconds(),TEXT("Sabotage cancellation has no effect and retains cooldown"));
    Advance(12);Seq=0;C->RequestEquipped(Body,O->Build.Revision,1,++Token,Aim,Why);Hold(2.01,true);const double Offline=G->PublicSensorOfflineUntil,Lockout=G->PublicSensorLockoutUntil;M->Vision->Tick(0);
    Check(!G->bPublicSensorActive&&Offline>W->GetTimeSeconds()+29&&Lockout>W->GetTimeSeconds()+59&&C->StateFor(Body)->ExposedUntil>W->GetTimeSeconds()+3&&G->DefensiveFlags==Flags,TEXT("Real sabotage sets 30/60/4 second endpoints without changing Heart defences"));
    const auto* View=M->Vision->AuthorityViewFor(PC);Check(View&&View->PublicSources==0&&View->Grid.CellSize==D->Grid.CellSize,TEXT("First resulting view removes the public source with matching regional encoding"));
    W->TimeSeconds=Offline-.01;C->Sample(W->GetTimeSeconds());Check(!G->bPublicSensorActive,TEXT("Watchtower stays offline until its fixed endpoint"));W->TimeSeconds=Offline;C->Sample(W->GetTimeSeconds());Check(G->bPublicSensorActive&&G->PublicSensorOfflineUntil==0,TEXT("Watchtower restores at the fixed thirty-second endpoint"));Seq=0;Check(!C->RequestEquipped(Body,O->Build.Revision,1,++Token,Aim,Why)&&G->PublicSensorLockoutUntil==Lockout,TEXT("Restored tower still enforces its sixty-second global lockout"));W->TimeSeconds=Lockout;C->Sample(W->GetTimeSeconds());Check(C->RequestEquipped(Body,O->Build.Revision,1,++Token,Aim,Why),TEXT("Control becomes available exactly at global lockout deadline"));C->ReleaseSkillHold(PC,Token);
}
void ALDDevelopmentProbe::RegionNormalTick()
{
    const bool SealRun=FParse::Param(FCommandLine::Get(),TEXT("LDSealProbe"));
    const bool Session=FParse::Param(FCommandLine::Get(),TEXT("LDReleaseSession"));
    auto* W=GetWorld();auto* M=W->GetAuthGameMode<ALDGameMode>();const auto* D=LDRegion::Definition(W);auto* PC=Cast<ALDPlayerController>(W->GetFirstPlayerController());if(!PC||!PC->GetPawn()||!M->Region){return;}auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());auto* O=M->StateFor(PC);const double Now=W->GetTimeSeconds();FString Why;
    if(!Phase)
    {
        if(Now<1||UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(this)){return;}Check(!O->Gold&&!O->Xp&&!O->Witness.IsValid()&&O->VisitedRegions.IsEmpty()&&!O->PaidJobs,TEXT("Ordinary region starts with zero resources and no reward receipts"));M->SetLobbyChoice(PC,1,true,Why);M->StartDevelopmentMatch(PC,Why);PC->SetMenuOpen(false);NormalLastPosition=Body->GetActorLocation();
        for(TActorIterator<ALDResident> It(W);It;++It){if(M->Combat->StateFor(*It)->bHasNPCBuild){RegionNPC=*It;RegionNPCStart=It->GetActorLocation();}}Check(RegionNPC.IsValid(),TEXT("Ordinary authored trained resident exists"));if(Session){SessionStarted=FPlatformTime::Seconds();OriginalDeadline=M->Objectives->Deadline();}Phase=1;return;
    }
    NormalTravel+=FVector::Dist2D(NormalLastPosition,Body->GetActorLocation());NormalLastPosition=Body->GetActorLocation();if(RegionNPC.IsValid()&&FVector::DistSquared2D(RegionNPC->GetActorLocation(),RegionNPCStart)>100*100){RegionSawNPCTravel=true;}
    if(Session){SessionAudit();if(M->IsFinished()){return;}}
    auto Stop=[&](){Body->SetSprint(false);Body->GetCharacterMovement()->StopMovementImmediately();return Body->GetLastMovementInputVector().IsNearlyZero();};
    auto Move=[&](FVector Goal,double Range)
    {
        if(FVector::DistSquared2D(Body->GetActorLocation(),Goal)<=Range*Range){NormalPath.Reset();return Stop();}
        if(NormalPath.IsEmpty()||NormalPathIndex>=NormalPath.Num()||FVector::DistSquared2D(NormalPath.Last(),Goal)>100*100)
        {
            auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(W,Body->GetActorLocation(),Goal);if(!Path||!Path->IsValid()||Path->IsPartial()){Check(false,TEXT("Ordinary regional travel requires a complete actual navigation path"));Finish();return false;}NormalPath=Path->PathPoints;NormalPathIndex=1;
        }
        while(NormalPathIndex<NormalPath.Num()&&FVector::DistSquared2D(Body->GetActorLocation(),NormalPath[NormalPathIndex])<(NormalPathIndex==NormalPath.Num()-1?Range*Range:15*15)){++NormalPathIndex;}
        if(NormalPathIndex<NormalPath.Num()){Body->SetSprint(true);Body->AddMovementInput((NormalPath[NormalPathIndex]-Body->GetActorLocation()).GetSafeNormal2D());}return false;
    };
    if(Phase==1&&Move(D->Guild+FVector(0,210,92),20)&&Now>=O->ProtectedUntil){Check(LDDevelopment::SkillValue(O->Build,ELDSkill::Survey)==12&&O->Build.Level==1,TEXT("Unmodified Rogue reaches the regional Charter through native movement"));PC->SetSkillHeld(1,true);NormalHoldAt=Now;Phase=2;}
    else if(Phase==2&&O->Witness.IsValid()){PC->SetSkillHeld(1,false);Check(Now-NormalHoldAt>=1&&!O->Gold&&!O->Xp,TEXT("Real starting Survey hold earns Witness without XP or gold"));NormalDeliveries=0;OriginalDeadline=M->Objectives->Deadline();Phase=SealRun?6:3;}
    else if(Phase==3)
    {
        TArray<const FLDRegionNode*> Visits;for(const auto& N:D->Nodes){if(N.Visit){Visits.Add(&N);}}
        if(NormalDeliveries<Visits.Num()){const auto* N=Visits[NormalDeliveries];if(Move(N->Position+FVector(0,0,92),20)){Check(O->VisitedRegions.Contains(N->Id)&&O->Xp==(NormalDeliveries+1)*25,TEXT("Actual living traversal awards each configured first visit exactly once"));++NormalDeliveries;}}
        else{Check(O->VisitedRegions.Num()==3&&O->Xp==75&&!O->Gold,TEXT("Three peaceful visits award exactly seventy-five XP"));Phase=4;}
    }
    else if(Phase==4&&RegionNPC.IsValid()&&Move(RegionNPC->GetActorLocation(),35))
    {
        const double TargetDistance=FVector::DistSquared2D(Body->GetActorLocation(),RegionNPC->GetActorLocation());for(TActorIterator<ALDCharacter> It(W);It;++It){if(*It!=Body&&*It!=RegionNPC.Get()&&FVector::DistSquared2D(Body->GetActorLocation(),It->GetActorLocation())<=TargetDistance){return;}}
        Check(RegionSawNPCTravel,TEXT("Authored resident actually navigated over one metre in the loaded map"));RegionNPCHp=M->Combat->StateFor(RegionNPC.Get())->Hp;Body->SetFacing((RegionNPC->GetActorLocation()-Body->GetActorLocation()).Rotation().Yaw);PC->PressBasicAttack();NormalHoldAt=Now;bSawSkillWarning=false;Phase=5;
    }
    else if(Phase==5&&RegionNPC.IsValid())
    {
        const auto* S=M->Combat->StateFor(RegionNPC.Get());if(S->Hp<RegionNPCHp){bSawSkillWarning=true;}
        if(bSawSkillWarning&&S->Hp>=RegionNPCHp&&S->NPCBuild.Cooldowns[4]>Now){Check(S->Hp>0&&O->Xp==75&&!O->Gold,TEXT("Actual nonlethal basic damage triggers autonomous shared-path NPC Patch with no kill reward"));Check(S->NPCBuild.Ranks[4]==1&&S->NPCBuild.Slots[0]==ELDSkill::Patch,TEXT("NPC healing uses its actual learned equipped rank and cooldown"));NormalDeliveries=0;Phase=6;bChecked=false;}
        else if(!bSawSkillWarning&&Now-NormalHoldAt>2){Phase=4;}
    }
    else if(Phase==6)
    {
        if(SealRun&&NormalDeliveries==4){Check(O->PaidJobs==4&&O->Commissions==4&&O->Gold==100&&!O->FabricationReceipt.IsValid()&&!O->SealId.IsValid(),TEXT("Four ordinary paid jobs earn fabrication resources without automatically crafting"));PC->SetFabricationHeld(true);NormalHoldAt=Now;Phase=11;return;}
        if(NormalDeliveries==8){Check(O->PaidJobs==8&&O->Commissions==8&&O->Gold==200,TEXT("Eight ordinary paid jobs retain separate paid count and unused credits"));Check(O->Xp==420&&O->Build.Level==5&&!O->DeathSerial&&M->Region->PeacefulDeaths==0&&O->VisitedRegions.Num()==3,TEXT("Eight deliveries plus three native visits reach level five without a kill"));Check(NormalTravel>40000&&M->WorldChecks->DrawCount()==0,TEXT("Ordinary regional circuit uses native travel and never draws Theft RNG"));if(Session){PC->SetFabricationHeld(true);Phase=20;NormalHoldAt=Now;}else{Finish();}return;}
        if(!Move(D->Guild+FVector(0,50,92),20)||Now<O->WorkReadyAt){return;}
        if(!bChecked){PC->SetInteractionHeld(true);bChecked=true;}
        if(O->ContractStage==1){PC->SetInteractionHeld(false);NormalPath.Reset();bChecked=false;Phase=7;}
    }
    else if(Phase==7){const auto* N=D->Node(O->ContractDestination);if(N&&Move(N->Position+FVector(0,50,92),20)){PC->SetInteractionHeld(true);NormalHoldAt=Now;Phase=8;}}
    else if(Phase==8&&O->ContractStage==2){PC->SetInteractionHeld(false);Check(Now-NormalHoldAt>=3,TEXT("Ordinary assigned-town collection uses the original three-second hold"));NormalPath.Reset();Phase=9;}
    else if(Phase==9&&Move(D->Guild+FVector(0,50,92),20)){PC->SetInteractionHeld(true);NormalHoldAt=Now;Phase=10;}
    else if(Phase==10&&O->ContractStage==0){PC->SetInteractionHeld(false);++NormalDeliveries;Check(Now-NormalHoldAt>=3&&O->PaidJobs==NormalDeliveries&&O->Gold==25*NormalDeliveries&&O->Xp==FMath::Min(420,(SealRun?25*O->VisitedRegions.Num():75)+45*NormalDeliveries),TEXT("Ordinary delivery pays exact bounded rewards after the original hold"));Phase=6;bChecked=false;}
    else if(SealRun&&Phase==11&&O->FabricationReceipt.IsValid())
    {PC->SetFabricationHeld(false);ProbeSeal=O->SealId;ProbeCraft=O->FabricationReceipt;Check(Now-NormalHoldAt>=4&&ProbeSeal.IsValid()&&!O->Gold&&!O->Commissions&&O->PaidJobs==4&&O->Witness.IsValid(),TEXT("Ordinary four-second fabrication consumes exact earned costs and retains Witness and paid budget"));Phase=12;}
    else if(SealRun&&Phase==12&&Move(D->Heart+FVector(0,50,92),20))
    {PC->SetInteractionHeld(true);NormalHoldAt=Now;Check(W->GetGameState<ALDGameState>()->DefensiveFlags==15,TEXT("Earned carrier approaches the still-protected normal Heart"));Phase=13;}
    else if(SealRun&&Phase==13&&M->IsFinished())
    {
        PC->SetInteractionHeld(false);const auto* G=W->GetGameState<ALDGameState>();Check(Now-NormalHoldAt>=10&&G->EndReason==1&&G->WinningRoute==LDObjective::SealRoute&&O->bWinner&&G->ResultSerial==1&&G->ResultReceipt.IsValid(),TEXT("Actual ten-second earned Seal claim wins once through protected central defences"));
        Check(!O->SealId.IsValid()&&O->FabricationReceipt==ProbeCraft&&O->Witness.IsValid(),TEXT("Victory consumes the earned item but retains private accomplishments"));
        Check(M->Objectives->Deadline()==OriginalDeadline&&Now<=OriginalDeadline&&!O->DeathSerial&&M->Region->PeacefulDeaths==0&&M->WorldChecks->DrawCount()==0&&NormalTravel>20000,TEXT("Zero-kill ordinary circuit preserves the original deadline and uses native travel without Theft draws"));Finish();
    }
    else if(Session&&Phase==20&&O->FabricationReceipt.IsValid())
    {PC->SetFabricationHeld(false);ProbeSeal=O->SealId;ProbeCraft=O->FabricationReceipt;Check(Now-NormalHoldAt>=4&&ProbeSeal.IsValid()&&O->Gold==100&&O->Commissions==4&&O->PaidJobs==8,TEXT("Full-session contender fabricates from actual eight-job earnings once"));NormalDeliveries=0;Phase=21;}
    else if(Session&&Phase==21)
    {const auto Towns=D->Towns();const FVector Goal=NormalDeliveries%3==2?D->Guild:D->Node(Towns[NormalDeliveries%3])->Position;if(Move(Goal+FVector(0,50,92),20)){++NormalDeliveries;}}
}
void ALDDevelopmentProbe::RegionServerTick()
{
    auto* W=GetWorld();auto* M=W->GetAuthGameMode<ALDGameMode>();auto* G=W->GetGameState<ALDGameState>();const auto* D=LDRegion::Definition(W);auto* C=M->Combat.Get();
    if(!Phase){for(FConstPlayerControllerIterator It=W->GetPlayerControllerIterator();It;++It){if(auto* PC=Cast<ALDPlayerController>(It->Get());PC&&PC->GetPawn()){if(PC->ProbeRole==1){A=PC;}if(PC->ProbeRole==2){B=PC;}}}if(A.IsValid()&&B.IsValid()){Check(G->LobbyCount==2,TEXT("Region network uses two independent remote owners"));Enter(1);}return;}
    if(Phase==10){if(FPlatformTime::Seconds()-PhaseAt>3){Finish();}return;}if(!A.IsValid()||!B.IsValid()){Check(false,TEXT("Regional client disconnected before completion"));Finish();return;}
    auto* AS=M->StateFor(A.Get());auto* BS=M->StateFor(B.Get());auto* AB=CastChecked<ALDCharacter>(A->GetPawn());auto* BB=CastChecked<ALDCharacter>(B->GetPawn());const double Elapsed=FPlatformTime::Seconds()-PhaseAt;
    if(Phase==1&&AS->bReady&&BS->bReady){Enter(2);}
    else if(Phase==2&&G->bMatchStarted&&Elapsed>1)
    {
        int32 Side=-1;for(auto* PC:{A.Get(),B.Get()}){auto* O=M->StateFor(PC);M->AwardExperience(PC,60);O->Build.Ranks[7]=2;O->Build.SkillPoints=0;++O->Build.Revision;auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());Body->SetActorLocation(D->Control+FVector(Side*40,-50,92));Body->GetCharacterMovement()->StopMovementImmediately();C->StateFor(Body)->ProtectionUntil=0;O->ProtectedUntil=0;Side=1;M->SyncOwner(PC);}Enter(3);
    }
    else if(Phase==3&&Elapsed>3.5)
    {
        const int32 Exposed=(C->StateFor(AB)->ExposedUntil>W->GetTimeSeconds()?1:0)+(C->StateFor(BB)->ExposedUntil>W->GetTimeSeconds()?1:0);
        Check(!G->bPublicSensorActive&&Exposed==1&&AS->Build.Cooldowns[7]>W->GetTimeSeconds()&&BS->Build.Cooldowns[7]>W->GetTimeSeconds(),TEXT("Concurrent remote Analyze race commits once and retains both accepted cooldowns"));
        const auto* AV=M->Vision->AuthorityViewFor(A.Get());const auto* BV=M->Vision->AuthorityViewFor(B.Get());Check(AV&&BV&&!AV->PublicSources&&!BV->PublicSources&&G->DefensiveFlags==15,TEXT("First published remote views remove only the public source"));
        W->TimeSeconds=G->PublicSensorOfflineUntil;C->Sample(W->GetTimeSeconds());Check(G->bPublicSensorActive&&W->GetTimeSeconds()<G->PublicSensorLockoutUntil,TEXT("Real offline endpoint restores sight while global control lockout persists"));AB->SetActorLocation(D->Guild+FVector(-40,50,92));BB->SetActorLocation(D->Guild+FVector(40,50,92));Enter(4);
    }
    else if(Phase==4&&Elapsed>1.5)
    {Check(AS->ContractStage==1&&BS->ContractStage==1&&D->Node(AS->ContractDestination)&&D->Node(BS->ContractDestination),TEXT("Each remote owner receives its own valid region-addressed courier contract"));AB->SetActorLocation(D->Node(AS->ContractDestination)->Position+FVector(-40,50,92));BB->SetActorLocation(D->Node(BS->ContractDestination)->Position+FVector(40,50,92));Enter(6);}
    else if(Phase==6&&Elapsed>4.5){Check(AS->ContractStage==2&&BS->ContractStage==2,TEXT("Remote held collection resolves at each private assigned town"));AB->SetActorLocation(D->Guild+FVector(-40,50,92));BB->SetActorLocation(D->Guild+FVector(40,50,92));Enter(7);}
    else if(Phase==7&&Elapsed>4.5){Check(AS->Gold==25&&BS->Gold==25&&AS->Xp==105&&BS->Xp==105&&AS->PaidJobs==1&&BS->PaidJobs==1&&AS->Commissions==1&&BS->Commissions==1,TEXT("Remote delivery pays each independent owner exactly once"));Enter(8);}
    else if(Phase==8&&Elapsed>2)
    {
        Check(AS->Gold==25&&BS->Gold==25&&AS->PaidJobs==1&&BS->PaidJobs==1&&!AS->ContractStage&&!BS->ContractStage,TEXT("Delayed replay and new request cannot duplicate payout or bypass acceptance delay"));
        auto KillBatch=[&](bool Equal)
        {
            auto* S=M->Region->SentinelBody();C->StateFor(S)->Hp=1;AB->SetActorLocation(D->Sentinel-FVector(80,0,0));BB->SetActorLocation(D->Sentinel+FVector(80,0,0));C->StateFor(AB)->BasicReady=C->StateFor(BB)->BasicReady=0;
            const uint8 PA=AS->PaidSentinels,PB=BS->PaidSentinels;const bool AExpected=!Equal||C->StateFor(AB)->Identity.ToString()<C->StateFor(BB)->Identity.ToString();FString Why;C->RequestBody(AB,0,FVector::ForwardVector,Why);C->RequestBody(BB,0,-FVector::ForwardVector,Why);W->TimeSeconds+=.16;C->Sample(W->GetTimeSeconds());
            Check(!M->Region->SentinelBody()&&AS->PaidSentinels==PA+(AExpected?1:0)&&BS->PaidSentinels==PB+(AExpected?0:1),Equal?TEXT("Equal lethal-batch damage credits the opaque server-ID tie winner"):TEXT("Unequal lethal-batch damage credits the greatest direct damage"));
        };
        KillBatch(true);W->TimeSeconds=M->Region->SentinelReturnAt();C->Sample(W->GetTimeSeconds());AS->Build.Attributes[0]=16;++AS->Build.Revision;M->SyncOwner(A.Get());KillBatch(false);Enter(9);
    }
    else if(Phase==9&&Elapsed>2){Enter(10);}
}
void ALDDevelopmentProbe::RegionClientTick()
{
    auto* PC=Cast<ALDPlayerController>(GetOwner());if(!PC||!PC->OwnerState.Match){return;}const auto& O=PC->OwnerState;const double Wall=FPlatformTime::Seconds();const bool IsA=Role==TEXT("A");
    if(Wall-LastPing>.2){LastPing=Wall;PC->ServerProbePing(Wall);}if(PC->ProbePhase!=Phase){Phase=PC->ProbePhase;PhaseAt=Wall;bChoiceSent=false;bChecked=false;}const double Elapsed=Wall-PhaseAt;
    if(Phase==1){if(!bChoiceSent){PC->ChooseLobbyClass(2);bChoiceSent=true;}if(!bReadySent&&O.Build.ClassId==2&&Elapsed>.4){PC->ToggleLobbyReady();bReadySent=true;}}
    if(Phase==2&&!bStartSent&&O.bLobbyHost){PC->StartLobby();bStartSent=true;}
    const uint32 Token=Phase==3?8100:Phase==4?8200:Phase==6?8300:8400;
    if((Phase==3||Phase==4||Phase==6||Phase==7)&&!bChoiceSent&&Elapsed>.4){bChoiceSent=true;if(Phase==3){PC->ServerSkillAction(O.Match,O.Life,Token,O.Build.Revision,1,FVector::ForwardVector);}else{uint8 Site=1;if(Phase==6){const auto* D=LDRegion::Definition(GetWorld());const auto* N=D?D->Node(O.ContractDestination):nullptr;Site=N?N->Site:0;}PC->ServerContextRequest(O.Match,O.Life,Token,Site,FGuid());}}
    if((Phase==3||Phase==6||Phase==7)&&Elapsed>.4&&Elapsed<4.2&&Wall-SkillRefreshAt>.1){SkillRefreshAt=Wall;if(Phase==3){PC->ServerSkillHold(O.Match,O.Life,Token,uint32(Elapsed*1000),false);}else{PC->ServerContextHold(O.Match,O.Life,Token,uint32(Elapsed*1000),false);}}
    if(Phase==8&&!bChoiceSent&&Elapsed>.4){bChoiceSent=true;PC->ServerContextRequest(O.Match,O.Life,8400,1,FGuid());PC->ServerContextRequest(O.Match,O.Life,8500,1,FGuid());}
    if(Phase==9&&!bChecked&&Elapsed>.5)
    {
        bChecked=true;Check(O.PaidJobs==1&&O.Commissions==1&&!O.ContractStage,TEXT("Owner retains exact private job count and unused credit"));Check(O.Gold==25+25*O.PaidSentinels&&O.Xp==105+45*O.PaidSentinels,TEXT("Owner receives only its own exact work and sentinel awards"));Check(O.VisitedRegions.IsEmpty()&&O.Build.ClassId==2&&O.Build.Attributes[0]==(IsA?16:8),TEXT("Teleports give no visit XP and private builds remain independent"));const auto* D=LDRegion::Definition(GetWorld());const auto* Relay=PC->LocalView();Check(D&&Relay&&Relay->LastView().Grid.CellSize==D->Grid.CellSize,TEXT("Remote fog/minimap encoding matches the loaded topology"));int32 Bodies=0,Controls=0;for(TActorIterator<ALDCharacter> It(GetWorld());It;++It){++Bodies;}for(TActorIterator<ALDWatchControl> It(GetWorld());It;++It){++Controls;}Check(Bodies==1&&!Controls,TEXT("Restricted client has only its canonical body and filtered world proxies"));
    }
    if(Phase==10&&Elapsed>1){Finish();}
}
#endif
