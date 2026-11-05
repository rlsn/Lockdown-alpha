#include "World/LDCampDecision.h"
#include "Development/LDDemoContentCatalog.h"

void FLDCampReturnWatch::Observe(uint64 InEpoch,double NowSeconds)
{
    if(Epoch==InEpoch){return;}
    *this={};
    Epoch=InEpoch;
    LastProgressSeconds=LastTickSeconds=NowSeconds;
}

void FLDCampReturnWatch::Tick(double NowSeconds,bool bFreeToMove)
{
    if(!bFreeToMove&&NowSeconds>LastTickSeconds){LastProgressSeconds+=NowSeconds-LastTickSeconds;}
    LastTickSeconds=FMath::Max(LastTickSeconds,NowSeconds);
}

bool FLDCampReturnWatch::PlanRefused(double NowSeconds,int32 MaxRefused,double RetrySeconds)
{
    if(bDeadEnd){return false;}
    ++RefusedPlans;
    NextPlanSeconds=NowSeconds+RetrySeconds;
    bDeadEnd=RefusedPlans>=MaxRefused;
    return bDeadEnd;
}

bool FLDCampReturnWatch::Stalled(double NowSeconds,double LimitSeconds)
{
    if(bDeadEnd||NowSeconds-LastProgressSeconds<LimitSeconds){return false;}
    bDeadEnd=true;
    return true;
}

namespace LDCampDecision
{
namespace
{
bool PositiveDistance(const FLDDemoRational& Value, double& Out)
{
    if(Value.Numerator<=0||Value.Denominator<=0){return false;}
    Out=double(Value.Numerator)/double(Value.Denominator);
    return FMath::IsFinite(Out)&&Out>0&&FMath::IsFinite(Out*Out);
}

bool GuidLess(const FGuid& A,const FGuid& B)
{
    if(A.A!=B.A){return A.A<B.A;}
    if(A.B!=B.B){return A.B<B.B;}
    if(A.C!=B.C){return A.C<B.C;}
    return A.D<B.D;
}

bool BodyLess(const FLDCombatBodyRef& A,const FLDCombatBodyRef& B)
{
    if(A.Victim.Kind!=B.Victim.Kind){return uint8(A.Victim.Kind)<uint8(B.Victim.Kind);}
    if(A.Victim.EntityId!=B.Victim.EntityId){return GuidLess(A.Victim.EntityId,B.Victim.EntityId);}
    if(A.Victim.Life!=B.Victim.Life){return A.Victim.Life<B.Victim.Life;}
    return GuidLess(A.BodyIdentity,B.BodyIdentity);
}
}

bool Evaluate(const FLDDemoContentSnapshot& Catalog,FName CampId,const FLDCampDecisionInput& Input,
    TConstArrayView<FLDCombatBodyRef> HostileSources,TConstArrayView<FLDAuthorityTargetSample> Candidates,
    FLDCampDecision& Out,FString& Error)
{
    Error.Reset();
    auto Fail=[&](const TCHAR* Reason)
    {Error=FString::Printf(TEXT("Camp decision %s: %s"),*CampId.ToString(),Reason);return false;};
    const auto& Content=Catalog.Get();
    if(!Input.Match.IsValid()||!Input.World.IsValid()||!Input.Self.IsValid()
        ||Input.Self.Victim.Kind!=ELDVictimKind::CampMember||CampId.IsNone())
    {return Fail(TEXT("invalid match, world, camp or body identity"));}
    if(Input.World.MapId!=FName(*Content.World.MapId)||Input.MemberHome.World!=Input.World
        ||Input.MemberHome.SurfaceId.IsNone()||(Input.Grounded&&Input.SurfaceId.IsNone()))
    {return Fail(TEXT("home/current surface or world revision does not match"));}
    if(!FMath::IsFinite(Input.NowSeconds)||Input.NowSeconds<0
        ||Input.FootCm.ContainsNaN()||Input.CampAnchorFootCm.ContainsNaN()||Input.MemberHome.PositionCm.ContainsNaN())
    {return Fail(TEXT("nonfinite position or invalid authority time"));}
    int64 NowMs=0;
    if(!LDMatchLifecycle::AuthoritySecondsToNowMs(Input.NowSeconds,NowMs,Error)){return false;}
    if(Input.Brain.Phase!=ELDCampBrainPhase::Idle&&Input.Brain.Phase!=ELDCampBrainPhase::Engaged
        &&Input.Brain.Phase!=ELDCampBrainPhase::Returning)
    {return Fail(TEXT("unknown brain phase"));}
    if(Input.Brain.LostTargetSinceSeconds.IsSet()
        &&(Input.Brain.Phase!=ELDCampBrainPhase::Engaged
            ||!FMath::IsFinite(Input.Brain.LostTargetSinceSeconds.GetValue())
            ||Input.Brain.LostTargetSinceSeconds.GetValue()<0
            ||Input.Brain.LostTargetSinceSeconds.GetValue()>Input.NowSeconds))
    {return Fail(TEXT("loss timestamp is not a current Engaged history"));}
    if(!Input.bObservationComplete){return Fail(TEXT("current authorized observation read did not complete"));}
    if(Candidates.Num()>LDObservation::MaxUnits){return Fail(TEXT("candidate count exceeds the observation contract"));}
    const auto* Camp=Content.Camps.FindByPredicate([&](const auto& Row){return FName(*Row.AnchorId)==CampId;});
    if(!Camp){return Fail(TEXT("camp is absent from the approved content"));}
    const FLDDemoMonsterDefinition* Monster=nullptr;
    if(!Catalog.FindMonster(Camp->MonsterId,Monster,Error)){return false;}
    double AttackRange=0,Leash=0,Perception=0,SightRange=0,Arrival=0;
    if(!PositiveDistance(Monster->AttackRangeCm,AttackRange)||!PositiveDistance(Monster->LeashRadiusCm,Leash)
        ||!PositiveDistance(Monster->PerceptionRadiusCm,Perception)
        ||!PositiveDistance(Content.Parameters.VisionRadiusCm,SightRange)
        ||!PositiveDistance(Content.Parameters.CampResetArrivalRadiusCm,Arrival)||Monster->LoseTargetGraceMs<0)
    {return Fail(TEXT("approved decision parameters are invalid"));}
    if(Perception>SightRange){return Fail(TEXT("camp perception exceeds the TraceSight range that supplies candidates"));}
    const double AnchorDistance=FVector::DistSquared2D(Input.FootCm,Input.CampAnchorFootCm);
    const double HomeDistance=FVector::DistSquared2D(Input.FootCm,Input.MemberHome.PositionCm);
    if(!FMath::IsFinite(AnchorDistance)||!FMath::IsFinite(HomeDistance)){return Fail(TEXT("anchor/home distance overflow"));}
    TMap<FLDVictimKey,FGuid> Hostile;
    TSet<FGuid> HostileBodies;
    for(const auto& Source:HostileSources)
    {
        if(!Source.IsValid()||Source.Victim.Kind!=ELDVictimKind::Contender
            ||Hostile.Contains(Source.Victim)||HostileBodies.Contains(Source.BodyIdentity))
        {return Fail(TEXT("hostile sources must be distinct contender bodies"));}
        Hostile.Add(Source.Victim,Source.BodyIdentity);HostileBodies.Add(Source.BodyIdentity);
    }
    TSet<FLDVictimKey> Victims;
    TSet<FGuid> Bodies;
    TSet<FGuid> EntitiesByKind[2];
    const FLDAuthorityTargetSample* Best=nullptr;
    double BestDistance=0;
    const double PerceptionSquared=Perception*Perception;
    for(const auto& Candidate:Candidates)
    {
        if(!Candidate.Target.IsValid()||Candidate.Target.Victim==Input.Self.Victim
            ||Candidate.Target.BodyIdentity==Input.Self.BodyIdentity
            ||Victims.Contains(Candidate.Target.Victim)||Bodies.Contains(Candidate.Target.BodyIdentity))
        {return Fail(TEXT("invalid, self, duplicate or conflicting target identity"));}
        if(EntitiesByKind[uint8(Candidate.Target.Victim.Kind)].Contains(Candidate.Target.Victim.EntityId))
        {return Fail(TEXT("more than one current life for the same target entity"));}
        if(Candidate.World!=Input.World||!FMath::IsFinite(Candidate.SampledAtSeconds)
            ||Candidate.SampledAtSeconds!=Input.NowSeconds||Candidate.FootCm.ContainsNaN())
        {return Fail(TEXT("target sample has stale world/time or nonfinite coordinates"));}
        Victims.Add(Candidate.Target.Victim);Bodies.Add(Candidate.Target.BodyIdentity);
        EntitiesByKind[uint8(Candidate.Target.Victim.Kind)].Add(Candidate.Target.Victim.EntityId);
        // Camp members and contenders that never attacked this camp are observed but never chosen.
        if(Candidate.Target.Victim.Kind!=ELDVictimKind::Contender){continue;}
        const FGuid* HostileBody=Hostile.Find(Candidate.Target.Victim);
        if(!HostileBody){continue;}
        if(*HostileBody!=Candidate.Target.BodyIdentity)
        {return Fail(TEXT("hostile source and current sample disagree on the body of one life"));}
        const double Distance=FVector::DistSquared2D(Input.FootCm,Candidate.FootCm);
        if(!FMath::IsFinite(Distance)){return Fail(TEXT("target distance overflow"));}
        // Perception is measured from the member's current foot; the leash bounds the member, not its target.
        if(Distance>PerceptionSquared){continue;}
        if(!Best||Distance<BestDistance||(Distance==BestDistance&&BodyLess(Candidate.Target,Best->Target)))
        {Best=&Candidate;BestDistance=Distance;}
    }
    FLDCampDecision Result;
    Result.NextBrain=Input.Brain;
    auto Returning=[&]()
    {
        Result.NextBrain.Phase=ELDCampBrainPhase::Returning;
        Result.NextBrain.LostTargetSinceSeconds.Reset();
        if(Input.Grounded&&Input.SurfaceId==Input.MemberHome.SurfaceId&&HomeDistance<=Arrival*Arrival)
        {Result.Intent=ELDCampIntent::ResetReady;Result.GoalFootCm=Input.MemberHome.PositionCm;}
        else if(Input.MovementAllowed)
        {Result.Intent=ELDCampIntent::ReturnHome;Result.GoalFootCm=Input.MemberHome.PositionCm;}
    };
    const bool bBeyondLeash=AnchorDistance>Leash*Leash;
    // #28: a member reset in place beyond its leash fights where it stands; the leash cannot send it back to a
    // dead end that would reset it again at full health.
    const bool bHoldGround=Input.bStrandedInPlace&&bBeyondLeash;
    if(Input.Brain.Phase==ELDCampBrainPhase::Returning
        ||(Input.Brain.Phase==ELDCampBrainPhase::Engaged&&bBeyondLeash&&!bHoldGround))
    {Returning();}
    else if(Input.Brain.Phase==ELDCampBrainPhase::Idle)
    {Result.NextBrain.LostTargetSinceSeconds.Reset();}
    else if(!Best)
    {
        if(!Result.NextBrain.LostTargetSinceSeconds.IsSet())
        {Result.NextBrain.LostTargetSinceSeconds=Input.NowSeconds;}
        int64 ReturnAtMs=0;
        if(!LDMatchLifecycle::DeadlineFromSeconds(Result.NextBrain.LostTargetSinceSeconds.GetValue(),
            Monster->LoseTargetGraceMs,ReturnAtMs,Error)){return false;}
        if(NowMs>=ReturnAtMs){Returning();}
    }
    else
    {
        Result.NextBrain.LostTargetSinceSeconds.Reset();
        Result.Target=Best->Target;Result.GoalFootCm=Best->FootCm;
        if(BestDistance<=AttackRange*AttackRange)
        {
            if(Input.AttackAllowed&&Input.BasicAttackReady){Result.Intent=ELDCampIntent::RequestBasicAttack;}
        }
        else if(Input.MovementAllowed&&!bHoldGround){Result.Intent=ELDCampIntent::Chase;}
    }
    Out=MoveTemp(Result);
    return true;
}
}
