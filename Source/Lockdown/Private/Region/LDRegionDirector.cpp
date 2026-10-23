#include "Region/LDRegionDirector.h"
#include "Region/LDRegionDefinition.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Combat/LDCombatDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Core/LDGroundQueries.h"
#include "Development/LDWorldChecks.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "Engine/World.h"

ALDWatchControl::ALDWatchControl(){bReplicates=false;RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));}
ALDWildSentinel::ALDWildSentinel(const FObjectInitializer& Initializer):Super(Initializer){bReplicates=false;AutoPossessAI=EAutoPossessAI::PlacedInWorldOrSpawned;AIControllerClass=AAIController::StaticClass();}
ALDRegionDirector::ALDRegionDirector(){bReplicates=false;}
void ALDRegionDirector::BeginPlay()
{
    Super::BeginPlay();check(HasAuthority());Definition=const_cast<ULDRegionDefinition*>(LDRegion::Definition(GetWorld()));check(Definition);
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();
    Control=GetWorld()->SpawnActor<ALDWatchControl>(Definition->Control,FRotator::ZeroRotator);
    M->Combat->RegisterSkillTask(Control.Get(),{ELDSkill::Analyze},[this](ALDCharacter* Source,ELDSkill,const FLDCharacterBuild& Build,uint32 Token,ELDSkillTaskPhase Phase,FString& Result){return Sabotage(Source,Build,Token,Phase,Result);});
    SpawnSentinel();
}
void ALDRegionDirector::SpawnSentinel()
{
    Sentinel=GetWorld()->SpawnActor<ALDWildSentinel>(Definition->Sentinel,FRotator::ZeroRotator);check(Sentinel.IsValid());
    Sentinel->GetCharacterMovement()->MaxWalkSpeed=180;auto* S=GetWorld()->GetAuthGameMode<ALDGameMode>()->Combat->StateFor(Sentinel.Get());check(S);S->Hp=100;
    if(SentinelWallet.IsValid()){GetWorld()->GetAuthGameMode<ALDGameMode>()->WorldChecks->BindReturnedBody(Sentinel.Get(),SentinelWallet);}
    Encounter=FGuid::NewGuid();Defeated=false;Victim.Reset();ReturnAt=0;LastLegalTarget=GetWorld()->GetTimeSeconds();
}
void ALDRegionDirector::SetupResident(ALDResident* Body,bool Trained)
{
    if(!Body||!Trained){return;}auto* C=GetWorld()->GetAuthGameMode<ALDGameMode>()->Combat.Get();
    auto Build=LDDevelopment::StartingBuild(2);Build.Ranks[uint8(ELDSkill::Patch)]=1;Build.Slots={ELDSkill::Patch,ELDSkill::Analyze,ELDSkill::None,ELDSkill::None};
    if(C->RegisterNPCBuild(Body,Build)){C->StateFor(Body)->Hp=LDDevelopment::MaxHP(Build);}
}
bool ALDRegionDirector::UpdateResident(ALDResident* Body)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* C=M->Combat.Get();auto* S=C->StateFor(Body);
    if(!S||!S->bHasNPCBuild||!LDDevelopment::Equipped(S->NPCBuild,ELDSkill::Patch)||S->Hp<=0||S->Hp>=LDDevelopment::MaxHP(S->NPCBuild)||!M->IsPlaying()){return false;}
    if(auto* AI=Cast<AAIController>(Body->GetController())){AI->StopMovement();}Body->GetCharacterMovement()->StopMovementImmediately();
    if(S->Action==7&&S->Skill==ELDSkill::Patch){C->RefreshSkillHold(Body,S->SkillToken,S->HoldSequence+1,false);return true;}
    if(!S->Action&&GetWorld()->GetTimeSeconds()>=S->NPCBuild.Cooldowns[uint8(ELDSkill::Patch)]){FString Why;C->RequestEquipped(Body,S->NPCBuild.Revision,0,++NPCToken,FVector::ForwardVector,Why);}return true;
}
bool ALDRegionDirector::SentinelCanTarget(const ALDCharacter* Body) const
{
    if(!Body||!Sentinel.IsValid()){return false;}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* PC=Cast<ALDPlayerController>(Body->GetController());const auto* O=M->StateFor(const_cast<ALDPlayerController*>(PC));const auto* S=M->Combat->StateFor(Body);const double Now=GetWorld()->GetTimeSeconds();
    return FVector::DistSquared2D(Sentinel->GetActorLocation(),Definition->Sentinel)<=600*600&&M->IsPlaying()&&O&&O->bActive&&S&&S->Hp>0&&Now>=S->ProtectionUntil&&Now>=O->ProtectedUntil&&FVector::DistSquared2D(Body->GetActorLocation(),Definition->Sentinel)<=600*600
        &&M->Vision->Ray({Sentinel->GetActorLocation(),600,M->Vision->LevelAt(Sentinel->GetActorLocation())},Body->GetActorLocation());
}
void ALDRegionDirector::Visits(double Now)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
    {
        auto* PC=Cast<ALDPlayerController>(It->Get());auto* O=M->StateFor(PC);auto* Body=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;const auto* S=M->Combat->StateFor(Body);if(!PC||!O||!Body||!S){continue;}
        auto& T=Traversal.FindOrAdd(PC);const FVector Position=Body->GetActorLocation();const bool Eligible=M->IsPlaying()&&O->bActive&&S->Hp>0&&Now>=S->ProtectionUntil&&Now>=O->ProtectedUntil&&Body->GetCharacterMovement()->IsMovingOnGround();
        const double Dt=Now-T.At,Distance=FVector::Dist2D(Position,T.Previous);
        // Consecutive eligible ground movement, bounded by actual movement speed. Teleports,
        // camera/ward coverage and new/protected bodies establish a baseline, never a visit.
        if(Eligible&&T.Eligible&&T.Body==Body&&Dt>0&&Dt<=.25&&Distance>.01&&Distance<=Body->GetCharacterMovement()->GetMaxSpeed()*Dt+30&&Body->GetVelocity().SizeSquared2D()>1)
        {
            for(const auto& N:Definition->Nodes)
            {
                if(!N.Visit||O->VisitedRegions.Contains(N.Id)||FVector::DistSquared2D(T.Previous,N.Position)<=FMath::Square(N.VisitRadius)||FVector::DistSquared2D(Position,N.Position)>FMath::Square(N.VisitRadius)){continue;}
                O->VisitedRegions.Add(N.Id);M->AwardExperience(PC,25);M->SyncOwner(PC);PC->ClientIntentResult(0,TEXT("First traversal visit: +25 XP."));
                UE_LOG(LogTemp,Display,TEXT("LD_VISIT_RECEIPT region=%u count=%d"),N.Id,O->VisitedRegions.Num());
            }
        }
        T.Body=Body;T.Previous=Position;T.At=Now;T.Eligible=Eligible;
    }
    for(auto It=Traversal.CreateIterator();It;++It){if(!It.Key().IsValid()){It.RemoveCurrent();}}
}
void ALDRegionDirector::Sample(double Now)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* G=GetWorld()->GetGameState<ALDGameState>();if(!M->IsPlaying()){return;}
    if(G->PublicSensorOfflineUntil>0&&Now>=G->PublicSensorOfflineUntil){G->PublicSensorOfflineUntil=0;M->Vision->SetPublicSensor(true,false);}
    Visits(Now);
    if(!Sentinel.IsValid()){if(ReturnAt>0&&Now>=ReturnAt){SpawnSentinel();}return;}
    if(Defeated){return;}auto* Body=Sentinel.Get();auto* S=M->Combat->StateFor(Body);auto* AI=Cast<AAIController>(Body->GetController());if(!S||S->Hp<=0||!AI){return;}AI->SetReplicates(false);
    if(!SentinelCanTarget(Victim.Get())||FVector::DistSquared2D(Body->GetActorLocation(),Definition->Sentinel)>600*600){Victim.Reset();}
    if(!Victim.IsValid())
    {
        double Best=DBL_MAX;FGuid Id;
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        {auto* Other=Cast<ALDCharacter>(It->Get()->GetPawn());if(!SentinelCanTarget(Other)||FVector::DistSquared2D(Other->GetActorLocation(),Definition->Sentinel)>500*500){continue;}const double D=FVector::DistSquared2D(Other->GetActorLocation(),Body->GetActorLocation());const auto Next=M->Combat->StateFor(Other)->Identity;if(D<Best||(D==Best&&Next.ToString()<Id.ToString())){Best=D;Id=Next;Victim=Other;}}
    }
    if(auto* Target=Victim.Get())
    {
        LastLegalTarget=Now;const FVector Direction=Target->GetActorLocation()-Body->GetActorLocation();Body->SetFacing(Direction.Rotation().Yaw);
        if(Direction.SizeSquared2D()<=90*90&&LDGround::ClearSegment(GetWorld(),Body->GetActorLocation(),Target->GetActorLocation())){AI->StopMovement();FString Why;M->Combat->RequestBody(Body,0,Direction.GetSafeNormal2D(),Why);}
        else{AI->MoveToLocation(Target->GetActorLocation(),55);}
    }
    else
    {if(Now>=LastLegalTarget+6){S->Hp=100;}if(FVector::DistSquared2D(Body->GetActorLocation(),Definition->Sentinel)>25*25){AI->MoveToLocation(Definition->Sentinel,20);}else{AI->StopMovement();}}
}
void ALDRegionDirector::SentinelDefeated(const TMap<ALDPlayerController*,double>& Damage,double Now)
{
    if(Defeated||!Sentinel.IsValid()){return;}Defeated=true;ReturnAt=Now+60;Victim.Reset();auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();
    SentinelWallet=M->WorldChecks->WalletId(Sentinel.Get());
    ALDPlayerController* Winner=nullptr;double Most=0;FGuid Best;
    for(const auto& P:Damage)
    {const auto* S=M->Combat->StateFor(P.Key?Cast<ALDCharacter>(P.Key->GetPawn()):nullptr);if(!S){continue;}if(P.Value>Most||(P.Value==Most&&S->Identity.ToString()<Best.ToString())){Most=P.Value;Best=S->Identity;Winner=P.Key;}}
    auto* O=M->StateFor(Winner);const auto* S=M->Combat->StateFor(Winner?Cast<ALDCharacter>(Winner->GetPawn()):nullptr);
    if(O&&O->bActive&&S&&S->Hp>0&&O->PaidSentinels<2&&O->SentinelReceipt!=Encounter)
    {O->SentinelReceipt=Encounter;++O->PaidSentinels;O->Gold+=25;M->RecordEconomyGold(TEXT("SentinelReward"),25);M->AwardExperience(Winner,45);M->SyncOwner(Winner);Winner->ClientIntentResult(0,TEXT("Sentinel reward: +25 gold, +45 XP (two paid defeats per match)."));}
    UE_LOG(LogTemp,Display,TEXT("LD_SENTINEL_RECEIPT encounter=%s paid=%d return=%.3f"),*Encounter.ToString(),O?O->PaidSentinels:0,ReturnAt);
}
bool ALDRegionDirector::Sabotage(ALDCharacter* Source,const FLDCharacterBuild& Build,uint32 Token,ELDSkillTaskPhase Phase,FString& Result)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* G=GetWorld()->GetGameState<ALDGameState>();auto* PC=Source?Cast<ALDPlayerController>(Source->GetController()):nullptr;auto* O=M->StateFor(PC);auto* S=M->Combat->StateFor(Source);const double Now=GetWorld()->GetTimeSeconds();
    if(!HasAuthority()||!M->IsPlaying()||!O||!O->bActive||!S||S->Hp<=0||Now<S->ProtectionUntil||!Token||!Control.IsValid()||!LDDevelopment::Equipped(Build,ELDSkill::Analyze)||FVector::DistSquared2D(Source->GetActorLocation(),Definition->Control)>120*120||!LDGround::ClearSegment(GetWorld(),Source->GetActorLocation(),Definition->Control)||!M->Vision->CanSee(PC,Definition->Control)){Result=TEXT("Approach the visible watch control with Analyze equipped.");return false;}
    if(Now<G->PublicSensorLockoutUntil||!M->Vision->IsPublicSensorActive()){Result=TEXT("Watch control is globally locked; accepted skill cooldown is retained.");return false;}
    if(LDDevelopment::SkillValue(Build,ELDSkill::Analyze)<14){Result=TEXT("Own Analyze score is below public 14; no cooldown spent.");return false;}
    if(Phase==ELDSkillTaskPhase::Commit)
    {
        if(S->Action!=7||S->SkillToken!=Token){return false;}G->PublicSensorOfflineUntil=float(Now+30);G->PublicSensorLockoutUntil=float(Now+60);S->ExposedUntil=FMath::Max(S->ExposedUntil,Now+4);M->Vision->SetPublicSensor(false,false);
        Result=TEXT("Watchtower offline for 30 seconds; control locked for 60 seconds. You are exposed for four seconds.");UE_LOG(LogTemp,Display,TEXT("LD_SABOTAGE_RECEIPT request=%u offline=%.3f lockout=%.3f"),Token,G->PublicSensorOfflineUntil,G->PublicSensorLockoutUntil);
    }
    return true;
}
