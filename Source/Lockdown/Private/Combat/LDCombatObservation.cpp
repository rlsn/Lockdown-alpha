#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatCues.h"
#include "Character/LDCharacter.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDDemoContentIds.h"

// Read-only projections of Combat state for the observation owner: action cues and projectiles in flight.
FLDCombatCueFacts ALDCombatDirector::CueFactsOf(const FLDBodyCombatState& Body)
{
    FLDCombatCueFacts Facts=Body.Cues;
    if (Body.Action.IsSet())
    {
        Facts.bActing=true;
        Facts.bActingBasic=Body.Action->Kind==ELDCombatActionKind::Basic;
        Facts.ActingStartedAtMs=Body.Action->StartedAtMs;
    }
    Facts.DashUntilMs=Body.DashUntilMs;
    return Facts;
}

void ALDCombatDirector::AppendObservedAction(const ALDCharacter& Body, int64 NowMs, FLDObservedPublicFacts& InOut) const
{
    InOut.ActionCue=ELDObservedActionCue::None;
    InOut.ActionCueAtMs=0;
    const FLDBodyCombatState* State=StateFor(&Body);
    if (!State || State->bPendingDeath) { return; }
    const LDCombatCues::FCue Cue=LDCombatCues::Select(CueFactsOf(*State),NowMs);
    InOut.ActionCue=Cue.Cue;
    InOut.ActionCueAtMs=Cue.AtMs;
}

bool ALDCombatDirector::ReadObservedProjectiles(TArray<FLDObservedProjectileFact>& Out, FString& Error) const
{
    Out.Reset();
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content)
    {
        Error=TEXT("Observed projectiles require the approved Demo content.");
        return false;
    }
    for (const FLDCombatProjectile& Projectile : Projectiles)
    {
        const FVector2D Heading=FVector2D(Projectile.Direction.X,Projectile.Direction.Y).GetSafeNormal();
        const FLDDemoAbilityDefinition* Ability=nullptr;
        FString LookupError;
        // The chalk throw is the only approved projectile; any other source has no look and is not invented one.
        if (!Content->FindAbility(Projectile.Source.DefinitionId.ToString(),Ability,LookupError)
            || Ability->NativeId!=uint8(ELDDemoAbilityId::ActiveArcBolt) || Heading.IsNearlyZero())
        {
            Out.Reset();
            Error=FString::Printf(TEXT("Projectile %s from %s has no approved observed look. %s"),
                *Projectile.EffectId.ToString(),*Projectile.Source.DefinitionId.ToString(),*LookupError);
            return false;
        }
        FLDObservedProjectileFact& Fact=Out.AddDefaulted_GetRef();
        Fact.Key=Projectile.EffectId;
        Fact.PositionCm=Projectile.OriginCm+FVector(Heading.X,Heading.Y,0)*Projectile.TravelledCm;
        Fact.Heading=Heading;
        Fact.Look=ELDObservedProjectileLook::Chalk;
    }
    Error.Reset();
    return true;
}
