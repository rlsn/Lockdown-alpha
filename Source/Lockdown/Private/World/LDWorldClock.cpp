#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Navigation/LDNavigationComponent.h"
#include "Vision/LDVisionDirector.h"
#include "Engine/World.h"

bool ALDCombatDirector::AdvanceAuthority(const FLDMatchContext& Match,int64 ThroughMs,FString& Error)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if (!Mode || Mode->Combat!=this || !Mode->Vision || bAdvancing || !Mode->IsGameplayWriteOpen(Match)
        || ThroughMs<Mode->GetAuthorityNowMs())
    {
        Error=TEXT("The current Combat driver cannot advance this closed, stale or reentrant step.");
        return false;
    }
    int64 MaxStepMs=0;
    if (!LDMatchLifecycle::AuthoritySecondsToNowMs(GetDefault<ULDCharacterMovementComponent>()->MaxSimulationTimeStep,MaxStepMs,Error)
        || MaxStepMs<=0)
    {
        Error=TEXT("Native CMC has no representable positive simulation step.");
        return false;
    }
    TGuardValue<bool> Advancing(bAdvancing,true);
    while (Mode->GetAuthorityNowMs()<ThroughMs)
    {
        const int64 End=Mode->GetAuthorityNowMs()+FMath::Min(MaxStepMs,ThroughMs-Mode->GetAuthorityNowMs());
        const auto Admission=Mode->BeginAuthorityStep(*this,Match,End);
        if (Admission!=ELDCommitReject::None)
        {
            Error=FString::Printf(TEXT("Core refused native step: %u."),uint8(Admission));
            return false;
        }
        TArray<TWeakObjectPtr<ALDCharacter>> CurrentBodies;
        Bodies.GetKeys(CurrentBodies);
        CurrentBodies.Sort([&](const auto& Left,const auto& Right)
        { return Bodies.FindChecked(Left).Victim.EntityId<Bodies.FindChecked(Right).Victim.EntityId; });
        if (!Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::Motion,[&](const FLDGameplayWritePermit& Permit)
        {
            for (const auto& Weak : CurrentBodies)
            {
                auto* Body=Weak.Get();
                auto* PC=Body?Cast<ALDPlayerController>(Body->GetController()):nullptr;
                FLDCombatBodyRef Ref;
                if (!Body || !PC || !Mode->ReadBodyRef(*Body,Ref))
                { Error=TEXT("The native step contains an invalid registered contender body."); return false; }
                if (!PC->Navigation->AdvanceAuthorityMotion(Mode->GetAuthorityStep(),Ref,Permit,Error)) { return false; }
            }
            return true;
        },Error)) { return false; }
        if (!Mode->ExecuteAuthorityPhase(*this,ELDCommitPhase::Actions,[&](const FLDGameplayWritePermit& Permit)
        {
            for (const auto& Weak : CurrentBodies)
            {
                auto* Body=Weak.Get();
                auto* PC=Body?Cast<ALDPlayerController>(Body->GetController()):nullptr;
                if (!PC) { Error=TEXT("The owned order consumer disappeared during a native step."); return false; }
                if (!PC->Navigation->ProcessAuthorityOrders(Permit,Error)) { return false; }
            }
            return true;
        },Error) || !Mode->FinishAuthorityStep(*this,Error)) { return false; }
    }
    return Mode->Vision->PublishAuthority(Match,Error);
}
