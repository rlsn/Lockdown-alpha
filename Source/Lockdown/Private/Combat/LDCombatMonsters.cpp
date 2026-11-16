#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDLifeAuthority.h"
#include "Region/LDMapRegistry.h"
#include "World/LDCampDirector.h"
#include "Engine/World.h"

bool ALDCombatDirector::ReleaseMonsterSkill(ALDCharacter& Actor,FLDBodyCombatState& Body,const FLDCombatAction& Action,ELDCombatReject& Reject,FString& Error)
{
    Reject=ELDCombatReject::None;
    const FLDDemoMonsterDefinition* Monster=nullptr;
    if(!Catalog() || !Catalog()->FindMonster(Body.MonsterId.ToString(),Monster,Error)){return false;}
    const auto& Skill=Monster->Skill;
    const int64 Now=Authority()->GetAuthorityNowMs();
    if(Skill.Pattern==ELDMonsterPattern::Guard)
    {if(!LDMatchLifecycle::AddDurationMs(Now,Skill.GuardDurationMs,Body.MonsterGuardUntilMs,Error)){return false;}}
    if(Skill.Pattern==ELDMonsterPattern::Charge)
    {
        auto* Movement=Cast<ULDCharacterMovementComponent>(Actor.GetCharacterMovement());
        if(!Movement){Error=TEXT("A charging monster has no native movement component.");return false;}
        if(IsActionBlocked(Body,ELDCombatControlAction::Move)||!Movement->CanStartDash(Action.Direction,20.))
        {Reject=ELDCombatReject::Blocked;FinishAction(Body,Reject,false);return true;}
        double Distance=-1;
        for(const auto& S:MonsterStrikes){if(S.Token==Action.Token && S.Payload.SourceBody.Victim==Body.Victim){Distance=S.Area.Range;break;}}
        if(Distance<=0){Reject=ELDCombatReject::Blocked;FinishAction(Body,Reject,false);return true;}
        if(!LDMatchLifecycle::AddDurationMs(Now,Skill.PulseIntervalMs,Body.DashUntilMs,Error)
            || !Movement->StartDemoDash(Action.Direction,Distance,double(Skill.PulseIntervalMs)/1000.,Error)){return false;}
        Body.DashContact=MakeShared<FLDCombatDashContact>();
        Body.DashContact->Body={Body.Victim,Body.Identity};
        Body.DashContact->StartMs=Now;Body.DashContact->EndMs=Body.DashUntilMs;
        Body.DashContact->Direction=Action.Direction;
    }
    Body.MonsterAttackCount=(Body.MonsterAttackCount+1)%Skill.Every;
    for(auto& Strike:MonsterStrikes)
    {
        if(Strike.Payload.SourceBody!=FLDCombatBodyRef{Body.Victim,Body.Identity} || Strike.Token!=Action.Token){continue;}
        Strike.bReleased=true;
        auto& Hit=Strike.Payload;
        Hit.HitId=Strike.Key;Hit.EffectId=Strike.Key;Hit.Token=Action.Token;
        Hit.Source.Kind=ELDCombatSourceKind::Basic;
        Hit.Source.DefinitionId=Body.MonsterId;
        Hit.HitClass=ELDCombatHitClass::Ability;
        Hit.DamageType=(Skill.Pattern==ELDMonsterPattern::Circle || Skill.Pattern==ELDMonsterPattern::EchoCircle
            || Skill.Pattern==ELDMonsterPattern::DoubleCircle)?ELDDemoDamageType::Magical:ELDDemoDamageType::Physical;
        Hit.ADRatioBp=Strike.Area.RatioBp;Hit.SourceADP2=Body.AttributeProjection.Final.ADP2;
        if(Body.CampStage>=Skill.EvolveStage && Monster->Tier==ELDDemoCampTier::High){Hit.SlowBp=Skill.EvolvedSlowBp;Hit.SlowDurationMs=Skill.EvolvedSlowMs;}
        if(Skill.Pattern==ELDMonsterPattern::Charge)
        {
            // The existing native motion/contact path delivers only the swept, terrain-clipped prefix.
            Hit.bDashContactHit=true;
            Body.DashContact->EffectId=Strike.Key;Body.DashContact->Source=Hit.Source;
            Body.DashContact->NativeHit=Hit;Body.DashContact->NativeHitsRemaining=MAX_int32;
        }
    }
    FinishAction(Body,ELDCombatReject::None,true);
    return true;
}

bool ALDCombatDirector::AdvanceMonsterStrikes(int64 NowMs,FString& Error)
{
    TArray<FLDMonsterStrike> Due;
    for(int32 I=MonsterStrikes.Num()-1;I>=0;--I)
    {
        const auto& S=MonsterStrikes[I];
        const auto* Source=FindBody(S.Payload.SourceBody);
        if(!S.bReleased && (!Source || Source->bSuspended || Source->bPendingDeath || !Source->Action.IsSet()
            || Source->Action->Token!=S.Token))
        {MonsterStrikes.RemoveAt(I);continue;}
        if(S.bReleased && S.AtMs<=NowMs){Due.Add(S);MonsterStrikes.RemoveAt(I);}
    }
    Due.Sort([](const auto& A,const auto& B){return A.AtMs==B.AtMs?A.Key<B.Key:A.AtMs<B.AtMs;});
    for(const auto& S:Due)
    {
        const FLDDemoMonsterDefinition* Monster=nullptr;
        if(!Catalog()->FindMonster(S.Payload.Source.DefinitionId.ToString(),Monster,Error)){return false;}
        if(Monster->Skill.Pattern==ELDMonsterPattern::Charge){continue;}
        if(Monster->Skill.Pattern==ELDMonsterPattern::Projectile)
        {
            FLDCombatProjectile P;
            P.FlightId=FGuid::NewGuid();P.EffectId=S.Key;P.Token=S.Token;P.Payload=S.Payload;
            P.SourceBody=S.Payload.SourceBody;P.SourceEntity=S.Payload.SourceEntity;P.Source=S.Payload.Source;
            P.DamageType=S.Payload.DamageType;P.SourceADP2=S.Payload.SourceADP2;P.ADRatioBp=S.Payload.ADRatioBp;
            P.OriginCm=S.Area.Origin;P.Direction=S.Area.Direction;P.RangeCm=S.Area.Range;P.RadiusCm=S.Area.Radius;
            P.SpeedCmPerSec=double(Monster->Skill.ProjectileSpeedCmPerSec.Numerator)/Monster->Skill.ProjectileSpeedCmPerSec.Denominator;P.AdvancedThroughMs=NowMs;P.SlowBp=S.Payload.SlowBp;P.SlowDurationMs=S.Payload.SlowDurationMs;
            Projectiles.Add(P);continue;
        }
        const auto* Source=FindBody(S.Payload.SourceBody);
        TArray<FLDCombatBodyRef> Targets;
        const double Radius=S.Area.Shape==ELDMonsterShape::Circle?S.Area.Radius:
            S.Area.Shape==ELDMonsterShape::Line?FMath::Sqrt(FMath::Square(S.Area.Range)+FMath::Square(S.Area.Radius)):S.Area.Range;
        if(!CollectAreaTargets(S.Area.Origin,Radius,Source,S.Payload.SourceEntity,Targets,Error)){return false;}
        for(const auto& Target:Targets)
        {
            if(Target.Victim.Kind!=ELDVictimKind::Contender){continue;}
            ALDCharacter* TargetActor=nullptr;
            const auto* TargetBody=FindBody(Target,&TargetActor);
            if(!TargetBody || !TargetActor || !LDMonsterRules::Contains(S.Area,FootOf(*TargetActor))){continue;}
            auto Hit=S.Payload;Hit.HitId=FGuid::NewGuid();Hit.Target=Target;
            PendingHits.Add(Hit);
        }
    }
    return true;
}

bool ALDCombatDirector::CampPeerDied(const FLDCombatBodyRef& Peer,const FLDGameplayWritePermit& Permit,FString& Error)
{
    if(!Authority() || Authority()->ValidateWritePermit(Permit,ELDCommitPhase::DeathsAndRewards)!=ELDCommitReject::None)
    {Error=TEXT("Camp passive requires the current death permit.");return false;}
    auto* Body=FindBody(Peer);
    if(!Body || Body->bPendingDeath){return true;}
    const FLDDemoMonsterDefinition* Monster=nullptr;
    if(!Catalog()->FindMonster(Body->MonsterId.ToString(),Monster,Error)){return false;}
    if(Monster->Skill.Pattern==ELDMonsterPattern::Enrage && !Body->bMonsterEnraged)
    {Body->bMonsterEnraged=true;++Body->StateRevision;MarkViewChanged(Body->Victim.EntityId);}
    return true;
}

bool ALDCombatDirector::CancelCampWindup(const FLDCombatBodyRef& Ref,const FLDGameplayWritePermit& Permit,FString& Error)
{
    if(!Authority() || Authority()->ValidateWritePermit(Permit,ELDCommitPhase::Actions)!=ELDCommitReject::None)
    {Error=TEXT("Camp return cancellation requires the current Actions permit.");return false;}
    ALDCharacter* Actor=nullptr;
    auto* Body=FindBody(Ref,&Actor);
    if(Body && Body->Action.IsSet()){AbandonAction(*Body,ELDCombatReject::Cancelled);}
    if(Body && Body->DashContact)
    {
        Body->DashContact->EndMs=Authority()->GetAuthorityNowMs();
        Body->DashContact.Reset();Body->DashUntilMs=0;
        if(auto* Movement=Actor?Cast<ULDCharacterMovementComponent>(Actor->GetCharacterMovement()):nullptr)
        {Movement->StopDemoDash();}
    }
    return true;
}
