#include "Combat/LDCombatDirector.h"
#include "Core/LDLog.h"
#include "Character/LDCharacter.h"
#include "Framework/LDLifeAuthority.h"

// Environmental damage (the final showdown's street closure) and the body health read its settlement uses.

bool ALDCombatDirector::ApplyEnvironmentDamage(const FLDCombatBodyRef& Ref, int64 AmountP2, const FLDGameplayWritePermit& Permit,
    FString& Error)
{
    ILDLifeAuthority* Mode=Authority();
    ALDCharacter* Actor=nullptr;
    FLDBodyCombatState* Body=FindBody(Ref,&Actor);
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::Effects)!=ELDCommitReject::None || AmountP2<0)
    {
        Error=TEXT("Environmental damage needs a non-negative amount and the current Effects permit.");
        return false;
    }
    // A body already gone or locked dead takes nothing more.
    if (!Body || !Actor || Body->bPendingDeath || AmountP2==0) { Error.Reset(); return true; }
    const int64 Before=Body->Resources.HpP2;
    Body->Resources.HpP2=AmountP2>=Before ? 0 : Before-AmountP2;
    ++Body->Resources.Revision;
    const FGuid HitId=FGuid::NewGuid();
    Body->Cues.LastHitAtMs=Mode->GetAuthorityNowMs();
    Body->Cues.LastHitId=HitId;
    Body->Cues.LastHitLook=0;
    Body->Cues.LastHitFlags=0;
    MarkViewChanged(Ref.Victim.EntityId);
    UE_LOG(LogLDCombat,Log,TEXT("LD_ENVIRONMENT_DAMAGE t=%lld victim=%s life=%u dmg=%s hp=%s->%s"),Mode->GetAuthorityNowMs(),
        *LDLog::Id(Ref.Victim.EntityId),Ref.Victim.Life,*LDLog::P2(AmountP2),*LDLog::P2(Before),*LDLog::P2(Body->Resources.HpP2));
    if (Body->Resources.HpP2>0) { Error.Reset(); return true; }
    // An unowned environmental death: the normal life commit, no credit, no assist, no reward.
    FLDCombatHit Hit;
    Hit.HitId=HitId;
    Hit.EffectId=Hit.HitId;
    Hit.Target=Ref;
    return LockDeath(*Actor,*Body,Hit,Error);
}

bool ALDCombatDirector::ReadBodyHealth(const FLDCombatBodyRef& Ref, int64& OutHpP2, int64& OutMaxHpP2) const
{
    const FLDBodyCombatState* Body=FindBody(Ref);
    if (!Body) { return false; }
    OutHpP2=Body->Resources.HpP2;
    OutMaxHpP2=Body->Resources.MaxHpP2;
    return true;
}
