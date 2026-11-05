#include "Audio/LDAudioCueTracker.h"
#include "Development/LDDemoContentIds.h"

void LDAudio::AddUnique(TArray<ELDAudioCue>& Out, ELDAudioCue Cue)
{
    if (IsPlayableCue(Cue) && !Out.Contains(Cue)) { Out.Add(Cue); }
}

void FLDAudioCueTracker::Observe(const FLDAudioFrame& Frame, TArray<ELDAudioCue>& Out)
{
    ObservePublic(Frame, Out);
    ObserveOwn(Frame, Out);
}

void FLDAudioCueTracker::ObservePublic(const FLDAudioFrame& Frame, TArray<ELDAudioCue>& Out)
{
    const FLDPublicMatchView* Public = Frame.Public;
    if (!Public || Public->Generation == 0) { return; }
    const bool bOpen = Public->Phase == ELDMatchPhase::CaptureOpen || Public->CircleState != ELDCircleState::NotOpen;
    if (Public->Generation != PublicGeneration)
    {
        // A match seen for the first time only primes: transitions before this sample are never replayed.
        PublicGeneration = Public->Generation;
        bOpenSeen = bOpen;
        CircleEpoch = Public->CaptureEpoch;
        GraffitiSeen = Public->GraffitiOwner;
    }
    else
    {
        // At most one capture cue per sample; the opening outranks a simultaneous first occupier.
        bool bCaptureCue = false;
        if (bOpen && !bOpenSeen)
        {
            bOpenSeen = true;
            LDAudio::AddUnique(Out, ELDAudioCue::CaptureOpened);
            bCaptureCue = true;
        }
        if (Public->CaptureEpoch != CircleEpoch)
        {
            // Every public circle change advances the epoch, so one epoch is one event.
            CircleEpoch = Public->CaptureEpoch;
            const bool bRunning = Public->Phase == ELDMatchPhase::CaptureOpen;
            // A freshly sprayed graffiti is the flagpole's fanfare (the opening cue) and outranks the zone change.
            const bool bSprayed = Public->GraffitiOwner.IsValid() && Public->GraffitiOwner != GraffitiSeen;
            const ELDAudioCue Circle = !bRunning ? ELDAudioCue::None
                : bSprayed ? ELDAudioCue::CaptureOpened
                : Public->CircleState == ELDCircleState::Solo ? ELDAudioCue::CaptureStarted
                : Public->CircleState == ELDCircleState::Contested ? ELDAudioCue::CaptureContested : ELDAudioCue::None;
            if (Circle != ELDAudioCue::None && !bCaptureCue && (bSprayed || Frame.LocalSeconds - LastCaptureCueAt >= CaptureRepeatSeconds))
            {
                LDAudio::AddUnique(Out, Circle);
                bCaptureCue = true;
            }
        }
        GraffitiSeen = Public->GraffitiOwner;
        if (bCaptureCue) { LastCaptureCueAt = Frame.LocalSeconds; }
    }

    const FLDKillFeedView* Feed = Frame.KillFeed;
    if (Feed && Feed->Generation != 0 && Feed->Generation == Public->Generation)
    {
        if (Feed->Generation != KillGeneration) { KillGeneration = Feed->Generation; KillSerial = 0; }
        uint64 Newest = KillSerial;
        bool bFresh = false;
        for (const FLDKillNotice& Notice : Feed->Notices)
        {
            if (Notice.Serial <= KillSerial) { continue; }
            Newest = FMath::Max(Newest, Notice.Serial);
            // Same freshness rule as the kill banner; the player's own death sounds through the owner projection.
            const bool bCurrent = !Frame.bClock || Frame.NowMs - Notice.OccurredAtMs <= KillNoticeFreshMs;
            const bool bOwnDeath = Frame.Self.IsValid() && Notice.Victim == Frame.Self;
            bFresh |= bCurrent && !bOwnDeath;
        }
        KillSerial = Newest;
        if (bFresh) { LDAudio::AddUnique(Out, ELDAudioCue::KillNotice); }
    }

    const FLDResultView* Result = Frame.Result;
    if (Result && Result->Generation == Public->Generation && Result->ResultId.IsValid()
        && Result->ResultId == Public->ResultId && Result->ResultId != ResultId)
    {
        const bool bHasWinner = Result->Winner.IsValid();
        // Victory or defeat needs the own identity; wait for the owner projection rather than guessing.
        if (bHasWinner && !Frame.Self.IsValid()) { return; }
        ResultId = Result->ResultId;
        if (Frame.bClock && Frame.NowMs - Result->EndedAtMs > ResultFreshMs) { return; }
        LDAudio::AddUnique(Out, !bHasWinner ? ELDAudioCue::MatchEndedNoWinner
            : Result->Winner == Frame.Self ? ELDAudioCue::MatchVictory : ELDAudioCue::MatchDefeat);
    }
}

void FLDAudioCueTracker::ObserveOwn(const FLDAudioFrame& Frame, TArray<ELDAudioCue>& Out)
{
    if (Frame.OwnMatch == 0 || Frame.OwnLife == 0) { return; }
    if (Frame.OwnMatch == LifeMatch && Frame.OwnLife < LifeNumber) { return; }
    if (Frame.OwnMatch != LifeMatch)
    {
        LifeMatch = Frame.OwnMatch;
        LifeNumber = Frame.OwnLife;
        LifePhase = Frame.OwnLifePhase;
    }
    else if (Frame.OwnLife >= LifeNumber)
    {
        if (Frame.OwnLife == LifeNumber && LifePhase == ELDLifePhase::Alive && Frame.OwnLifePhase == ELDLifePhase::Dead)
        { LDAudio::AddUnique(Out, ELDAudioCue::SelfDeath); }
        else if (Frame.OwnLife > LifeNumber && Frame.OwnLifePhase == ELDLifePhase::Alive)
        { LDAudio::AddUnique(Out, ELDAudioCue::SelfRespawn); }
        LifeNumber = Frame.OwnLife;
        LifePhase = Frame.OwnLifePhase;
    }

    const FLDCombatOwnerView* Combat = Frame.OwnCombat;
    if (Combat && Combat->bHasBody && Combat->Generation == Frame.OwnMatch && Combat->Life == Frame.OwnLife)
    {
        const FLDCombatStatusView* Shield = Combat->Statuses.FindByPredicate([](const FLDCombatStatusView& Status)
            { return Status.StatusId == uint8(ELDDemoStatusId::Shield); });
        if (Combat->Generation != HitMatch || Combat->Life != HitLife)
        {
            HitMatch = Combat->Generation;
            HitLife = Combat->Life;
            HitRevision = Combat->Revision;
            HitHPP2 = Combat->HPP2;
            HitMaxHPP2 = Combat->MaxHPP2;
            HitShieldP2 = Combat->ShieldP2;
            HitShieldEndsAtMs = Shield ? Shield->ExpiresAtMs : 0;
            AttackReleasedAtMs = Combat->Cues.ReleasedAtMs;
        }
        else if (Combat->Revision > HitRevision)
        {
            // A newer release in the own cue facts; only a basic one sounds (Q/W/1/2/D sound from their receipts).
            // One body releases at most one action per authority ms, so a later release time is a new release.
            const FLDCombatCueFacts& Facts = Combat->Cues;
            if (Facts.ReleasedAtMs > AttackReleasedAtMs)
            {
                if (Facts.ReleasedAtMs <= Combat->AsOfMs && Facts.bReleasedBasic
                    && Frame.OwnLifePhase == ELDLifePhase::Alive && Combat->HPP2 > 0)
                {
                    LDAudio::AddUnique(Out, ELDAudioCue::SelfAttack);
                }
                AttackReleasedAtMs = Facts.ReleasedAtMs;
            }
            // Lost points with an unchanged or larger maximum: a smaller maximum (sold equipment) only clamps.
            // A lethal hit is left to the death cue.
            const bool bLost = Combat->HPP2 < HitHPP2 && Combat->MaxHPP2 >= HitMaxHPP2 && Combat->HPP2 > 0;
            // A hit the own shield took whole: shield points lost while the shield is still listed or before its listed end
            // (the same rule as the shield-absorbed number); a shield that merely ran out stays silent.
            const bool bAbsorbed = !bLost && Combat->ShieldP2 < HitShieldP2 && Combat->HPP2 >= HitHPP2
                && (Shield || HitShieldEndsAtMs > Combat->AsOfMs);
            if ((bLost || bAbsorbed) && Frame.OwnLifePhase == ELDLifePhase::Alive && Frame.LocalSeconds - LastHitAt >= HitRepeatSeconds)
            {
                LDAudio::AddUnique(Out, bLost ? ELDAudioCue::SelfHit : ELDAudioCue::SelfShieldHit);
                LastHitAt = Frame.LocalSeconds;
            }
            HitRevision = Combat->Revision;
            HitHPP2 = Combat->HPP2;
            HitMaxHPP2 = Combat->MaxHPP2;
            HitShieldP2 = Combat->ShieldP2;
            HitShieldEndsAtMs = Shield ? Shield->ExpiresAtMs : 0;
        }
    }

    const FLDProgressionView* Growth = Frame.OwnProgression;
    if (Growth && Growth->bValid && Growth->Generation == Frame.OwnMatch)
    {
        if (Growth->Generation != GrowthMatch)
        {
            GrowthMatch = Growth->Generation;
            GrowthLevel = Growth->Level;
            GrowthEarned = Growth->EarnedThrough;
        }
        else
        {
            // A level or a newly earned three-choice; the next queued offer shown after a pick is not new.
            if (Growth->Level > GrowthLevel || Growth->EarnedThrough > GrowthEarned) { LDAudio::AddUnique(Out, ELDAudioCue::LevelUp); }
            GrowthLevel = FMath::Max(GrowthLevel, Growth->Level);
            GrowthEarned = FMath::Max(GrowthEarned, Growth->EarnedThrough);
        }
    }
}

void FLDAudioCueTracker::ObserveCombatReceipt(const FLDCombatActionReceipt& Receipt, uint32 OwnMatch, uint32 OwnLife, TArray<ELDAudioCue>& Out)
{
    // Another match or life is an old answer: it never sounds for the current body.
    if (OwnMatch == 0 || OwnLife == 0 || Receipt.Generation != OwnMatch || Receipt.Life != OwnLife) { return; }
    if (Receipt.Generation != CastMatch || Receipt.Life != CastLife)
    {
        CastMatch = Receipt.Generation;
        CastLife = Receipt.Life;
        CastKeys.Reset();
    }
    const bool bActive = Receipt.Entry == ELDCombatEntry::Q || Receipt.Entry == ELDCombatEntry::W
        || Receipt.Entry == ELDCombatEntry::Item1 || Receipt.Entry == ELDCombatEntry::Item2 || Receipt.Entry == ELDCombatEntry::Dash;
    const bool bReleased = Receipt.Reject == ELDCombatReject::None
        && (Receipt.Phase == ELDCombatPhase::Released || Receipt.Phase == ELDCombatPhase::Finished);
    if (!bActive || !bReleased) { return; }
    const uint64 Key = Receipt.Origin == ELDCombatRequestOrigin::Direct ? Receipt.DirectSequence : (Receipt.CombatToken | (uint64(1) << 63));
    if ((Key & ~(uint64(1) << 63)) == 0 || CastKeys.Contains(Key)) { return; }
    if (CastKeys.Num() >= 16) { CastKeys.RemoveAt(0); }
    CastKeys.Add(Key);
    LDAudio::AddUnique(Out, ELDAudioCue::SelfCast);
}

void FLDAudioCueTracker::FinishShop(uint32 Generation, uint64 RequestId, ELDReceiptOutcome Outcome, TArray<ELDAudioCue>& Out)
{
    if (Generation != ShopMatch) { ShopMatch = Generation; ShopFinalThrough = 0; }
    // Request ids only grow within a match once final: a repeated or late final answer stays silent.
    if (RequestId == 0 || RequestId <= ShopFinalThrough) { return; }
    if (Outcome != ELDReceiptOutcome::Applied && Outcome != ELDReceiptOutcome::Rejected) { return; }
    ShopFinalThrough = RequestId;
    LDAudio::AddUnique(Out, Outcome == ELDReceiptOutcome::Applied ? ELDAudioCue::PurchaseSucceeded : ELDAudioCue::TradeRejected);
}

void FLDAudioCueTracker::ObserveShopSubmit(const FLDSubmitResult& Result, uint32 OwnMatch, TArray<ELDAudioCue>& Out)
{
    if (Result.Family != ELDGrowthFamily::Shop || OwnMatch == 0 || Result.Generation != OwnMatch) { return; }
    if (Result.Status == ELDAdmissionStatus::Final && Result.bHasShopReceipt)
    { FinishShop(Result.Generation, Result.RequestId, Result.ShopReceipt.Header.Outcome, Out); return; }
    // An unreserved refusal is final for the player but does not consume the id, which the next request reuses.
    if (Result.Status == ELDAdmissionStatus::AdmissionRejected && !Result.bSequenceReserved
        && Result.RequestId != 0 && Result.RequestId > (Result.Generation == ShopMatch ? ShopFinalThrough : 0))
    { LDAudio::AddUnique(Out, ELDAudioCue::TradeRejected); }
}

void FLDAudioCueTracker::ObserveShopReceipt(const FLDShopReceiptQueryResult& Result, uint32 OwnMatch, TArray<ELDAudioCue>& Out)
{
    if (Result.Query.Family != ELDGrowthFamily::Shop || OwnMatch == 0 || Result.Query.Generation != OwnMatch) { return; }
    if (Result.Lookup != ELDReceiptLookup::Final || !Result.bHasReceipt) { return; }
    FinishShop(Result.Query.Generation, Result.Query.RequestId, Result.Receipt.Header.Outcome, Out);
}
