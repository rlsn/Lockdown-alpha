#include "Combat/LDCombatDirector.h"
#include "Core/LDLog.h"
#include "Character/LDCharacter.h"
#include "Framework/LDLifeAuthority.h"
#include "Region/LDMapRegistry.h"
#include "Vision/LDVisionDirector.h"
#include "Engine/World.h"

// #198 session D: the Combat side of the PK (approved outer parameters, issue #198 comment 6752601175 §2/§3/§6). PK lives
// share one isolation group (IsOutOfGroup); same-side bodies never affect each other (IsIsolated, every hit, projectile,
// area, unit target and dash contact gate); the ring burns straight through HP; spectator throws slow and expose.

bool ALDCombatDirector::SetPKBody(const FLDCombatBodyRef& Ref, const FGuid& Group, uint8 Side, const FLDGameplayWritePermit& Permit,
    FString& Error)
{
    const ILDLifeAuthority* Mode=Authority();
    FLDBodyCombatState* Body=FindBody(Ref);
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::RespawnsAndWorld)!=ELDCommitReject::None
        || !Body || Body->bPendingDeath || Ref.Victim.Kind!=ELDVictimKind::Contender || !Group.IsValid() || Side==0)
    {
        Error=TEXT("A PK body needs a living contender body, the PK group, a side and the current RespawnsAndWorld permit.");
        return false;
    }
    Body->IsolationGroup=Group;
    Body->PKSide=Side;
    Body->ExposedUntilMs=0;
    ++Body->StateRevision;
    MarkViewChanged(Ref.Victim.EntityId);
    Error.Reset();
    return true;
}

bool ALDCombatDirector::ApplyThrowHit(const FLDCombatBodyRef& Ref, const FGuid& EffectId, int64 SlowBp, int64 SlowMs, int64 ExposeMs,
    const FLDGameplayWritePermit& Permit, FString& Error)
{
    ILDLifeAuthority* Mode=Authority();
    FLDBodyCombatState* Body=FindBody(Ref);
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::Effects)!=ELDCommitReject::None || !EffectId.IsValid()
        || SlowBp<=0 || SlowBp>10000 || SlowMs<=0 || ExposeMs<=0)
    {
        Error=TEXT("A throw hit needs its effect, a valid slow and exposure, and the current Effects permit.");
        return false;
    }
    if (!Body || Body->bPendingDeath) { Error.Reset(); return true; }
    const int64 NowMs=Mode->GetAuthorityNowMs();
    FLDControlApplication App;
    App.Kind=ELDControlKind::Slow;
    App.EffectId=EffectId;
    App.DurationMs=SlowMs;
    App.RatioBp=SlowBp;
    App.bSpectatorThrow=true;
    bool bApplied=false;
    if (!CommitControl(*Body,App,NowMs,bApplied,Error)) { return false; }
    int64 ExposedUntil=0;
    if (!LDMatchLifecycle::AddDurationMs(NowMs,ExposeMs,ExposedUntil,Error)) { return false; }
    Body->ExposedUntilMs=ExposedUntil;
    Body->Cues.LastHitAtMs=NowMs;
    Body->Cues.LastHitId=FGuid::NewGuid();
    Body->Cues.LastHitLook=0;
    Body->Cues.LastHitFlags=0;
    ++Body->StateRevision;
    MarkViewChanged(Ref.Victim.EntityId);
    UE_LOG(LogLDCombat,Display,TEXT("LD_PK_THROW_HIT t=%lld victim=%s slowed=%d exposed_until_ms=%lld"),NowMs,
        *LDLog::Id(Ref.Victim.EntityId),bApplied ? 1 : 0,ExposedUntil);
    Error.Reset();
    return true;
}

bool ALDCombatDirector::IsExposed(const FLDCombatBodyRef& Ref, int64 NowMs) const
{
    const FLDBodyCombatState* Body=FindBody(Ref);
    return Body && !Body->bPendingDeath && Body->ExposedUntilMs>NowMs;
}

bool ALDCombatDirector::HasAreaSight(const FVector& FromCm, const FVector& ToCm, bool& bOutVisible, FString& Error) const
{
    bOutVisible=false;
    const ILDLifeAuthority* Mode=Authority();
    const ULDMapRegistry* Registry=GetWorld() ? GetWorld()->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!Mode || !Mode->GetVisionAuthority() || !Registry)
    {
        Error=TEXT("Area sight requires the observation owner and the registered world.");
        return false;
    }
    const ELDSightResult Sight=Mode->GetVisionAuthority()->TraceSight(FromCm,ToCm,Registry->Revision());
    if (Sight!=ELDSightResult::Visible && Sight!=ELDSightResult::Blocked && Sight!=ELDSightResult::OutOfRange)
    {
        Error=TEXT("Area sight failed on a registered body.");
        return false;
    }
    bOutVisible=Sight==ELDSightResult::Visible;
    Error.Reset();
    return true;
}
