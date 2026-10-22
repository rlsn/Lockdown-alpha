#include "Combat/LDCombatDirector.h"
#include "Core/LDRules.h"
#include "Combat/LDCombatRules.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Framework/LDGameMode.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDVendorDirector.h"
#include "Framework/LDPlayerController.h"
#include "Vision/LDVisionDirector.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Core/LDGroundQueries.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Development/LDDevelopmentDirector.h"
#include "Development/LDWorldChecks.h"
#include "Region/LDRegionDirector.h"

ALDCorpse::ALDCorpse()
{
    bReplicates=false;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    InitialLifeSpan=20;
}
ALDCombatDirector::ALDCombatDirector()
{
    bReplicates=false;
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PostPhysics;
}
void ALDCombatDirector::BeginPlay()
{
    Super::BeginPlay(); check(HasAuthority());
    for(TActorIterator<ALDCharacter> It(GetWorld());It;++It){Register(*It);}
}
void ALDCombatDirector::Register(ALDCharacter* Body)
{
    if(!Body || Bodies.Contains(Body)){return;}
    FLDBodyCombatState State;
    State.Hp=Body->IsA<ALDResident>()?40:100;
    if (auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();Mode&&Mode->IsDevelopmentProfile())
    {
        if (auto* PC=Cast<ALDPlayerController>(Body->GetController())) { if (auto* PrivateBuildState=Mode->StateFor(PC)) { State.Hp=PrivateBuildState->Hp; } }
    }
    State.Home=Body->GetActorLocation(); State.QuietSince=GetWorld()->GetTimeSeconds();
    Bodies.Add(Body,State);
}
uint8 ALDCombatDirector::CueFor(const ALDCharacter* Body) const
{
    const auto* S=StateFor(Body); if(!S){return 0;}
    const double Now=GetWorld()->GetTimeSeconds();
    return S->Action | (Now<S->WarningUntil?16:0) | (Now<S->HitUntil?32:0)
        | (Now<S->ProtectionUntil?64:0) | (Now<S->ExposedUntil?128:0);
}
double ALDCombatDirector::ExposedFor(const ALDCharacter* Body) const
{const auto* S=StateFor(Body);return S?S->ExposedUntil:0;}
void ALDCombatDirector::Queue(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 RequestId,uint8 Action,const FVector& Direction)
{
    // Three bounded edge intents per connection/sample; retries are handled by the controller.
    int32 Count=0;for(const auto& Intent:Intents){if(Intent.Controller==PC){++Count;}}
    if(Count>=3){PC->CompleteCombatRequest(RequestId,TEXT("Too many queued actions."));return;}
    Intents.Add({PC,Match,Life,RequestId,Action,Direction});
}

bool ALDCombatDirector::LegalTarget(ALDCharacter* Source,AActor* Target,double Reach,const FVector* Aim,double Cone) const
{
    if(!IsValid(Source)||!IsValid(Target)||Source==Target){return false;}
    const auto* S=StateFor(Source); if(!S||S->Hp<=0){return false;}
    if(const ALDCharacter* Body=Cast<ALDCharacter>(Target))
    {const auto* T=StateFor(Body);if(!T||T->Hp<=0||GetWorld()->GetTimeSeconds()<T->ProtectionUntil){return false;}}
    else if(const ALDWard* Ward=Cast<ALDWard>(Target))
    {if(Ward->Hp<=0||Ward->ExpiresAt<=GetWorld()->GetTimeSeconds()){return false;}}
    else{return false;}
    const FVector From=Source->GetActorLocation(),To=Target->GetActorLocation();
    if(FVector::DistSquared2D(From,To)>Reach*Reach || (Aim&&!LDCombat::InCone(From,To,*Aim,Reach,Cone))){return false;}
    const ALDGameMode* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if(!Mode||!Mode->Vision){return false;}
    if(ALDPlayerController* PC=Cast<ALDPlayerController>(Source->GetController()))
    {if(!Mode->Vision->CanSee(PC,To)){return false;}}
    else if(!Mode->Vision->Ray({From,720,Mode->Vision->LevelAt(From)},To)){return false;}
    return LDGround::ClearSegment(GetWorld(),From,To);
}
AActor* ALDCombatDirector::Nearest(ALDCharacter* Source,double Reach,const FVector& Aim,double Cone,bool bAssassin,bool bBodiesOnly) const
{
    AActor* Best=nullptr; double Distance=DBL_MAX; FGuid BestId;
    const double Now=GetWorld()->GetTimeSeconds();
    auto Consider=[&](AActor* Target,FGuid Id)
    {
        if(Source->IsA<ALDWildSentinel>()&&(!GetWorld()->GetAuthGameMode<ALDGameMode>()->Region||!GetWorld()->GetAuthGameMode<ALDGameMode>()->Region->SentinelCanTarget(Cast<ALDCharacter>(Target)))){return;}
        if(!LegalTarget(Source,Target,Reach,&Aim,Cone)){return;}
        if(bAssassin)
        {
            const ALDCharacter* Body=Cast<ALDCharacter>(Target);const auto* S=StateFor(Body);
            if(!Body||!S||!LDCombat::IsRear(Source->GetActorLocation(),Body->GetActorLocation(),Body->GetActorForwardVector())
                ||!LDCombat::TargetUnalert(Now,S->LastDamage,S->LastDealt)){return;}
        }
        const double D=FVector::DistSquared2D(Source->GetActorLocation(),Target->GetActorLocation());
        if(D<Distance || (D==Distance && Id.ToString()<BestId.ToString())){Distance=D;Best=Target;BestId=Id;}
    };
    for(const auto& Pair:Bodies){if(Pair.Key.IsValid()){Consider(Pair.Key.Get(),Pair.Value.Identity);}}
    if(!bAssassin&&!bBodiesOnly){for(TActorIterator<ALDWard> It(GetWorld());It;++It){Consider(*It,It->Identity);}}
    return Best;
}
bool ALDCombatDirector::Request(ALDPlayerController* PC,uint8 Action,const FVector& Direction,FString& Result)
{
    ALDGameMode* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    const FLDOwnerState* PrivateState=Mode?Mode->StateFor(PC):nullptr;
    if(!PrivateState||!PrivateState->bActive){Result=TEXT("No active body.");return false;}
    return RequestBody(Cast<ALDCharacter>(PC->GetPawn()),Action,Direction,Result);
}
bool ALDCombatDirector::RequestBody(ALDCharacter* Body,uint8 Action,const FVector& Direction,FString& Result)
{
    auto* S=StateFor(Body); const double Now=GetWorld()->GetTimeSeconds();
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if(!Mode->IsPlaying()){Result=TEXT("Gameplay is unavailable in the lobby or after results.");return false;}
    if(Mode->IsDevelopmentProfile()&&Action!=0){Result=TEXT("Use an equipped skill slot; legacy skill shortcuts are disabled in v0.4.");return false;}
    if(!S||S->Hp<=0||Now<S->ProtectionUntil||Direction.ContainsNaN()||Direction.SizeSquared2D()<UE_SMALL_NUMBER||FMath::Abs(Direction.Z)>.001||Action>2)
    {Result=TEXT("Action unavailable.");return false;}
    auto* Move=CastChecked<ULDCharacterMovementComponent>(Body->GetCharacterMovement());
    if(S->Action==4&&Now>=S->DashUntil){S->Action=0;Move->bCanWalkOffLedges=true;}
    if(S->Action==3&&Now>=S->RecoveryUntil){S->Action=0;}
    if(Action==2)
    {
        if(Now<S->DashReady||S->Action==4||!Move->StartAuthorityDash(Direction))
        {Result=TEXT("Dash unavailable or cooling down.");return false;}
        S->Action=4;S->Target.Reset();S->DashUntil=Now+LDCombat::DashDuration;S->DashReady=Now+LDCombat::DashCooldown;
        Move->bCanWalkOffLedges=false;
        Result=TEXT("Dash accepted.");
    }
    else
    {
        if((S->Action==7||S->Action==8)&&Action==0&&Now>=S->BasicReady)
        {if(S->Action==8){if(auto* PC=Cast<ALDPlayerController>(Body->GetController());PC&&Mode->Development){Mode->Development->Cancel(PC,TEXT("Interrupted by attack."));}}CancelSkill(Body,*S);}
        if(S->Action){Result=TEXT("Finish the current action first.");return false;}
        const FVector Aim=Direction.GetSafeNormal2D();
        if(Action==0)
        {
            if(Now<S->BasicReady){Result=TEXT("Attack cooling down.");return false;}
            S->Action=1;S->ImpactAt=Now+(Body->IsA<ALDWildSentinel>()?.3:LDCombat::BasicWindup);S->BasicReady=Now+(Body->IsA<ALDWildSentinel>()?1.2:LDCombat::BasicCadence);
            const auto* Build=BuildFor(Body);
            S->CommittedDamage=Body->IsA<ALDWildSentinel>()?12:Mode->IsDevelopmentProfile()&&Build?LDDevelopment::BasicDamage(*Build):20;
            S->Skill=ELDSkill::None;
            Result=TEXT("Attack committed.");
        }
        else
        {
            if(Now<S->AssassinReady||!LDCombat::Quiet(Now,S->QuietSince,S->ExposedUntil,Move->bWantsSprint,S->Action==4))
            {Result=TEXT("Assassination needs quiet, unexposed preparation.");return false;}
            AActor* Target=Nearest(Body,112,Aim,30,true);
            if(!Target){Result=TEXT("No legal rear assassination target.");return false;}
            S->Target=Target;S->Action=2;S->ImpactAt=Now+LDCombat::AssassinWindup;S->AssassinReady=Now+LDCombat::AssassinCooldown;
            auto* Victim=CastChecked<ALDCharacter>(Target);auto* T=StateFor(Victim);T->WarningUntil=FMath::Max(T->WarningUntil,S->ImpactAt);
            Sync(Victim,*T); // Private reliable warning has no source identity or coordinates.
            if(auto* PC=Cast<ALDPlayerController>(Victim->GetController())){PC->ClientLifeReceipt(PC->OwnerState);}
            Result=TEXT("Assassination committed: target warned.");
        }
        S->CommittedAim=Aim;S->Order=++NextOrder;
    }
    Move->bCombatWalkOnly=S->Action==2||S->Action==3;
    if(Mode->IsUIControlProfile()){Body->SetFacing(Direction.Rotation().Yaw);}
    S->QuietSince=Now;S->ExposedUntil=FMath::Max(S->ExposedUntil,Now+4);Sync(Body,*S);
    if(auto* PC=Cast<ALDPlayerController>(Body->GetController()))
    {if(auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->Objectives.Get()){O->Interrupt(PC,Now,Mode->IsDevelopmentProfile());}}
    return true;
}
void ALDCombatDirector::Sync(ALDCharacter* Body,FLDBodyCombatState& S)
{
    auto* PC=Cast<ALDPlayerController>(Body->GetController());
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=PC&&Mode?Mode->StateFor(PC):nullptr;
    if(!O||!O->bActive){return;}
    const uint8 Cue=CueFor(Body);
    if(O->Hp==S.Hp&&O->BasicReadyAt==float(S.BasicReady)&&O->AssassinReadyAt==float(S.AssassinReady)
        &&O->DashReadyAt==float(S.DashReady)&&O->WarningUntil==float(S.WarningUntil)&&O->DamageUntil==float(S.HitUntil)
        &&O->ExposedUntil==float(S.ExposedUntil)&&O->ActionCue==Cue&&O->ProtectedUntil==float(S.ProtectionUntil)&&O->IncomingSector==S.IncomingSector
        &&O->ActiveSkill==(S.Action?S.Skill:ELDSkill::None)&&O->SkillEndsAt==(S.Action==4?S.DashUntil:S.Action?S.ImpactAt:0)){return;}
    O->Hp=S.Hp;O->BasicReadyAt=float(S.BasicReady);O->AssassinReadyAt=float(S.AssassinReady);O->DashReadyAt=float(S.DashReady);
    O->WarningUntil=float(S.WarningUntil);O->DamageUntil=float(S.HitUntil);O->ExposedUntil=float(S.ExposedUntil);
    O->ActionCue=Cue;O->ProtectedUntil=float(S.ProtectionUntil);O->IncomingSector=S.IncomingSector;
    O->ActiveSkill=S.Action?S.Skill:ELDSkill::None;O->SkillEndsAt=S.Action==4?S.DashUntil:S.Action?S.ImpactAt:0;Mode->SyncOwner(PC);
}
void ALDCombatDirector::Sample(double Now)
{
    struct FImpact{ALDCharacter* Source;AActor* Target;double Damage;};TArray<FImpact> Impacts;
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if(Mode->IsFinished())
    {
        if(Mode->Inventory){Mode->Inventory->Sample(Now);if(Mode->Vendor){Mode->Vendor->Sample(Now);}}
        if(Mode->Development){Mode->Development->Sample(Now);}
        if(Mode->IsDevelopmentProfile())
        {for(auto& Pair:Bodies){if(auto* Body=Pair.Key.Get()){CancelSkill(Body,Pair.Value);Sync(Body,Pair.Value);}}}
        for(const auto& Intent:Intents){if(auto* PC=Intent.Controller.Get()){PC->CompleteCombatRequest(Intent.Request,TEXT("Demo finished."));}}
        Intents.Reset();return;
    }
    auto Priority=[&](const FIntent& I)
    {
        const auto* O=Mode->StateFor(I.Controller.Get());
        if(I.Action>=3&&O&&O->Build.Slots.IsValidIndex(I.Action-3)) { return O->Build.Slots[I.Action-3]==ELDSkill::Step?2:1; }
        return I.Action==2?2:1;
    };
    Intents.StableSort([&](const FIntent& A,const FIntent& B){return Priority(A)>Priority(B);});
    TSet<ALDPlayerController*> Accepted;
    for(const auto& Intent:Intents)
    {
        auto* PC=Intent.Controller.Get();if(!PC){continue;}
        const auto* O=Mode->StateFor(PC);FString Reason;
        if(!O||O->Match!=Intent.Match||O->Life!=Intent.Life){Reason=TEXT("Stale queued life.");}
        else if(Accepted.Contains(PC)){Reason=TEXT("Superseded by a higher-priority action.");}
        else
        {
            AActor* Target=Intent.Observed.IsValid()&&Mode->Vision?Mode->Vision->ResolveObserved(PC,Intent.Observed):nullptr;
            const auto Skill=Intent.Action>=3&&O->Build.Slots.IsValidIndex(Intent.Action-3)?O->Build.Slots[Intent.Action-3]:ELDSkill::None;
            if(Mode->IsUIControlProfile()&&Intent.Action>=3&&Skill!=ELDSkill::Step&&Skill!=ELDSkill::Patch&&!Target){Reason=TEXT("Target no longer visible");}
            else if(Intent.Action>=3?RequestEquipped(Cast<ALDCharacter>(PC->GetPawn()),Intent.BuildRevision,Intent.Action-3,Intent.Request,Intent.Direction,Reason,Target):Request(PC,Intent.Action,Intent.Direction,Reason)){Accepted.Add(PC);}
        }
        PC->CompleteCombatRequest(Intent.Request,Reason);
    }
    Intents.Reset();
    // Gather against one pre-damage world. Applying one hit cannot erase another due hit.
    for(auto& Pair:Bodies)
    {
        ALDCharacter* Body=Pair.Key.Get();auto& S=Pair.Value;if(!Body||S.Hp<=0){continue;}
        auto* Move=CastChecked<ULDCharacterMovementComponent>(Body->GetCharacterMovement());
        if(Move->bWantsSprint||S.Action==4){S.QuietSince=Now;}
        if(S.Action==4&&Now>=S.DashUntil){CancelSkill(Body,S);Move->bCanWalkOffLedges=true;}
        if(S.Action==3&&Now>=S.RecoveryUntil){CancelSkill(Body,S);}
        if((S.Action==1||S.Action==2||S.Action==6)&&Now+1.e-6>=S.ImpactAt)
        {
            AActor* Target=nullptr;const bool bAssassin=S.Action==2,bSpark=S.Action==6;
            if(bSpark){if(Cast<ALDCharacter>(S.Target.Get())&&LegalTarget(Body,S.Target.Get(),500,nullptr,0)){Target=S.Target.Get();}}
            else if(!bAssassin){Target=Nearest(Body,96,S.CommittedAim,40,false);}
            else if(ALDCharacter* Victim=Cast<ALDCharacter>(S.Target.Get()))
            {
                const auto* T=StateFor(Victim);
                if(T&&!Move->bWantsSprint&&LegalTarget(Body,Victim,112,nullptr,0)
                    &&LDCombat::IsRear(Body->GetActorLocation(),Victim->GetActorLocation(),Victim->GetActorForwardVector())
                    &&LDCombat::TargetUnalert(Now,T->LastDamage,T->LastDealt)){Target=Victim;}
            }
            if(Target){Impacts.Add({Body,Target,bAssassin&&S.Skill==ELDSkill::None?60:S.CommittedDamage});}
            S.Action=bAssassin||bSpark?3:0;S.RecoveryUntil=Now+(bSpark?.35:.25);S.Target.Reset();
        }
        Move->bCombatWalkOnly=S.Action==2||S.Action==3||S.Action==6;
    }
    TSet<ALDCharacter*> Deaths;TMap<ALDPlayerController*,double> SentinelDamage;
    for(const auto& Hit:Impacts)
    {
        auto* S=StateFor(Hit.Source);S->LastDealt=Now;S->ExposedUntil=FMath::Max(S->ExposedUntil,Now+4);
        if(Mode->Vision&&Mode->Vision->IsPublicSensorActive()&&Mode->Vision->Ray({Mode->Vision->PublicLocation(),600,1},Hit.Target->GetActorLocation()))
        {S->ExposedUntil=FMath::Max(S->ExposedUntil,Now+12);}
        if(auto* Ward=Cast<ALDWard>(Hit.Target)){Ward->Hp-=Hit.Damage;continue;}
        auto* Victim=CastChecked<ALDCharacter>(Hit.Target);auto* T=StateFor(Victim);
        if(Victim->IsA<ALDWildSentinel>()){if(auto* PC=Cast<ALDPlayerController>(Hit.Source->GetController())){SentinelDamage.FindOrAdd(PC)+=Hit.Damage;}}
        T->Hp=FMath::Max(0.,T->Hp-Hit.Damage);T->LastDamage=Now;T->QuietSince=Now;T->HitUntil=Now+.15;
        T->ExposedUntil=FMath::Max(T->ExposedUntil,Now+4);
        if(auto* PC=Cast<ALDPlayerController>(Victim->GetController())){if(Mode->Objectives){Mode->Objectives->Interrupt(PC,Now);}if(Mode->Development){Mode->Development->Cancel(PC,TEXT("Damage cancelled the transaction."));}}
        const FVector Bearing=Hit.Source->GetActorLocation()-Victim->GetActorLocation();
        T->IncomingSector=uint8(FMath::RoundToInt(FRotator::ClampAxis(Bearing.Rotation().Yaw)/45))%8;
        if(T->Action==7||((T->Action==1||T->Action==2||T->Action==6)&&T->ImpactAt>Now+1.e-6))
        {CancelSkill(Victim,*T);}
        if(T->Hp<=0){Deaths.Add(Victim);}
    }
    if(Mode->Region&&Mode->Region->SentinelBody()&&Deaths.Contains(Mode->Region->SentinelBody())){Mode->Region->SentinelDefeated(SentinelDamage,Now);}
    // Damage wins against held completion, including a hit on the exact deadline.
    CompleteSkillChannels(Now);
    if(Mode->Development&&!Mode->Inventory){Mode->Development->Sample(Now);}
    for(auto& Pair:Bodies){if(Pair.Key.IsValid()){Sync(Pair.Key.Get(),Pair.Value);}}
    for(ALDCharacter* Body:Deaths){Die(Body,Now);}
    if(Mode->Inventory){Mode->Inventory->Sample(Now);if(Mode->Vendor){Mode->Vendor->Sample(Now);}if(Mode->Development){Mode->Development->Sample(Now);}}
    for(auto It=Bodies.CreateIterator();It;++It){if(!It.Key().IsValid()){It.RemoveCurrent();}}
    for(int32 I=Residents.Num()-1;I>=0;--I)
    {if(Now>=Residents[I].At){auto* Returned=GetWorld()->SpawnActor<ALDResident>(Residents[I].Home,FRotator::ZeroRotator);if(Mode->WorldChecks){Mode->WorldChecks->BindReturnedBody(Returned,Residents[I].Wallet);}if(Residents[I].HasBuild){RegisterNPCBuild(Returned,Residents[I].Build);StateFor(Returned)->Hp=LDDevelopment::MaxHP(Residents[I].Build);}Residents.RemoveAtSwap(I);}}
    Mode->ReturnDueContenders(Now);
    if(Mode->Region){Mode->Region->Sample(Now);}
    if(Mode->Objectives){Mode->Objectives->Sample(Now);}
}
void ALDCombatDirector::Die(ALDCharacter* Body,double Now)
{
    auto* S=StateFor(Body);if(!S){return;}
    if(ALDCorpse* Corpse=GetWorld()->SpawnActor<ALDCorpse>(Body->GetActorLocation()-FVector(0,0,65),Body->GetActorRotation())){Corpse->Presentation=Body->PresentationKey;}
    if(auto* PC=Cast<ALDPlayerController>(Body->GetController())){GetWorld()->GetAuthGameMode<ALDGameMode>()->EndContenderLife(PC,Now);}
    else if(Body->IsA<ALDResident>()){if(auto* R=GetWorld()->GetAuthGameMode<ALDGameMode>()->Region.Get()){++R->PeacefulDeaths;}auto* W=GetWorld()->GetAuthGameMode<ALDGameMode>()->WorldChecks.Get();Residents.Add({Now+20,S->Home,W?W->WalletId(Body):FGuid(),S->bHasNPCBuild,S->NPCBuild});}
    if(AController* Controller=Body->GetController()){Controller->UnPossess();if(!Controller->IsPlayerController()){Controller->Destroy();}}
    Body->Destroy();
}
void ALDCombatDirector::Tick(float Delta)
{
    Super::Tick(Delta);const double Now=GetWorld()->GetTimeSeconds();
    if(Now>=NextSample)
    {
        if(NextSample>0&&Now-NextSample>.1){UE_LOG(LogTemp,Warning,TEXT("LD authority combat sample late by %.3fs; sampling current pose once"),Now-NextSample);}
        NextSample=LDRules::NextSample(Now,NextSample,.05);const double Begin=FPlatformTime::Seconds();Sample(Now);LastSampleMs=(FPlatformTime::Seconds()-Begin)*1000;++SampleSerial;
    }
}
