#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "LDWorldFeedback.generated.h"
struct FLDCombatOwnerView;
struct FLDProgressionView;
struct FLDKillFeedView;
struct FLDWorldView;
struct FLDDemoContentDefinitions;
class FLDDemoContentSnapshot;
class ALDPlayerController;
class ALDGameState;

// Feedback where the player is looking (APP_SPEC 8): numbers over the own body and over visible units, the own rewards
// beside the own body, and level-up and own-kill confirmations above it. Local presentation of approved facts only; it
// never predicts, extrapolates or changes a gameplay value.
namespace LDWorldFeedback
{
    // DealtConfirm: the own receipt of a hit the own body landed (LDHitFeel), shown as a short word in front of it.
    // Vouchers: the own voucher gain (spec §12 飞入), a reward beside the own body.
    // PerfectDodge / ChainEnd (#305): a perfect dodge of the own body and the end of one of its counted chains (Amount = its
    // steps), each a word over the own body.
    enum class EKind : uint8 { DamageTaken, Heal, ShieldGain, ShieldAbsorbed, UnitDamage, UnitHeal, Experience, LevelUp, Kill, UnitCast, DealtConfirm,
        UnitReaction, Vouchers, PerfectDodge, ChainEnd };
    struct FEvent
    {
        EKind Kind=EKind::DamageTaken;
        int64 Amount=0;            // whole points (half-up from P2) or XP; LevelUp: stat points newly earned; Kill: 1
        bool bOwn=true;            // anchored to the own body, otherwise to the visible unit Handle/Epoch
        FGuid Handle;
        uint32 Epoch=0;
        int32 Level=0;             // LevelUp: the level reached
        int32 Offers=0;            // LevelUp: active opportunities newly earned with it (#198)
        int32 CardOffers=0;        // LevelUp: card offers it queued (one per level)
        FString Name;              // Kill: the victim's public roster name; UnitCast: the cast's display name; LevelUp: the
                                   // components it granted (#198 B), joined; empty when unknown or none
        double SampledAtServerSec=0; // UnitDamage/UnitHeal: SampledAtServerSec of the frame that showed the change
        uint8 HitFlags=0;          // DealtConfirm: the receipt's LDObservation::EHitFlags
        int32 Tier=0;              // DamageTaken/UnitDamage: DamageTier of the hit against the body's max HP
        uint8 Element=0;           // UnitReaction: the element that set it off (ELDCombatElement), its ink
    };
    // Spec §12: a damage number grows with its share of the struck body's max HP: 0 below 10%, 1 to 30%, 2 to 60%, 3 from 60%
    // (the top tier shakes).
    LOCKDOWN_API int32 DamageTier(int64 DamageP2,int64 MaxHpP2);
    // The font scale of a tier (1 for tier 0).
    LOCKDOWN_API float DamageTierScale(int32 Tier);
    // One sample of the SC-07 inputs: the owner's own projections, room-public DTOs and the current authorized frame of
    // this match and life (nullptr otherwise). The tracker re-checks every match, life and generation itself.
    struct FSample
    {
        uint32 Match=0;
        uint32 Life=0;
        ELDLifePhase LifePhase=ELDLifePhase::Unspawned;
        FGuid Self;
        const FLDCombatOwnerView* OwnCombat=nullptr;
        const FLDProgressionView* Progression=nullptr;
        const FLDKillFeedView* KillFeed=nullptr;
        uint32 PublicGeneration=0;
        // Authority clock sample, used only for the kill-feed freshness rule.
        bool bClock=false;
        int64 NowMs=0;
        const FLDWorldView* Frame=nullptr;
        // Public roster name of a contender; empty when the roster does not list it.
        TFunction<FString(const FGuid&)> RosterName;
        // Display name of a visible cast's ability native id; empty when unknown.
        TFunction<FString(uint8)> CastName;
    TFunction<FString(uint8)> MonsterCastName;
    };
    // The sample of PC's own projections, the room-public views of Public and PC's current authorized frame of this match
    // and life (none while dead or for another generation), as both the feedback layer and the playtest log observe it.
    // Content resolves cast display names (stable ids without it). Pointers stay valid until the next relay update.
    LOCKDOWN_API void MakeSample(const ALDPlayerController& PC, const ALDGameState& Public,
        const FLDDemoContentDefinitions* Content, FSample& Out);
    // Turns consecutive samples into events. The first sample of a match only primes (and so does the first body view
    // of a life, the first frame of an observation generation and the first valid growth view after a missing one);
    // older or foreign generations never produce events. XP floats for every reward that grants it.
    class LOCKDOWN_API FTracker
    {
    public:
        // Same freshness as the HUD kill feed: an own kill already older when first seen is marked seen silently.
        static constexpr int64 KillNoticeFreshMs=6000;
        void Observe(const FSample& Sample,TArray<FEvent>& Out);
        // Observation paused (the layer was hidden): body and unit baselines prime again, so changes made while nobody
        // watched never read as one fresh hit. Rewards, levels and kills keep their baselines.
        void Interrupt();
    private:
        struct FUnit
        {
            FGuid Handle;
            uint32 Epoch=0;
            bool bCamp=false;
            int64 HPP2=0;
            int64 MaxP2=0;
            double SampledAtSec=0;
            // The unit's cue (ELDObservedActionCue), its age and cast ability in that frame, so a new cast is told apart.
            uint8 Cue=0;
            uint16 CueAgeMs=0;
            // The unit's reaction cue in that frame (spec §12: a new one floats its name).
            uint8 Reaction=0;
            uint16 ReactionAgePlusOneMs=0;
            uint8 ReactionElement=0;
            uint8 Ability=0;
        };
        void Reset(uint32 InMatch);
        void ObserveBody(const FSample& Sample,TArray<FEvent>& Out);
        void ObserveGrowth(const FSample& Sample,TArray<FEvent>& Out);
        void ObserveKills(const FSample& Sample,TArray<FEvent>& Out);
        void ObserveUnits(const FSample& Sample,TArray<FEvent>& Out);
        uint32 Match=0;
        bool bBody=false;
        uint32 BodyLife=0;
        uint64 BodyRevision=0;
        int64 BodyAsOfMs=0;
        int64 HPP2=0;
        int64 MaxHPP2=0;
        int64 ShieldP2=0;
        int64 ShieldEndsAtMs=0;    // end of the shield status listed with the baseline; 0 when none was listed
        int64 DealtAtMs=0;         // the own hit receipt of the baseline (LastDealtAtMs)
        int64 ReactionAtMs=0;      // the own body's last reaction of the baseline (Cues.LastReactionAtMs)
        uint32 PerfectDodges=0;    // the own body's perfect dodges of the baseline life
        bool bChains=false;        // the chain ends of this match are primed
        uint64 ChainSerial=0;      // the newest own chain end already told
        bool bGrowth=false;
        uint8 Level=0;
        uint8 OffersEarned=0;
        // Earned stat points: the unspent plus the allocated ones, so an allocation in the same sample never hides a grant.
        int64 PointsEarned=0;
        int64 XP=0;
        int32 Vouchers=0;
        bool bKills=false;
        uint64 KillSerial=0;
        bool bFrame=false;
        uint32 FrameGeneration=0;
        uint32 FrameRevision=0;
        TArray<FUnit> Units;
    };
    // "-37", "+12", then the LDTerms lines: shield gained/absorbed, experience, level-up and kill (an unlisted victim reads
    // LDTerms::UnknownVictim),
    LOCKDOWN_API FString Text(const FEvent& Event);
    // Smaller second line of a level-up banner (LDTerms::LevelUpGains: the points, active opportunities and card offers it
    // brought); empty for every other event.
    LOCKDOWN_API FString Subline(const FEvent& Event);
    // A visible unit's number waits for the relay's presentation clock to reach the frame that produced it, so it shows
    // with the proxy's delayed hit pose. It is released once the clock reaches its stamp, at once without a clock and
    // after UnitHoldMaxSeconds of local time at most; it is dropped as soon as its unit is no longer presented.
    // The own body is not delayed: its numbers and the rewards never wait.
    enum class EUnitHold : uint8 { Hold, Release, Drop };
    // Longer than the relay's longest presentation delay (LDObservedPresentation::MaxDelaySeconds, checked in
    // LDWorldFeedback.cpp), so a number only falls back to local time when the clock really stalls.
    inline constexpr double UnitHoldMaxSeconds=.7;
    LOCKDOWN_API EUnitHold UnitHold(bool bPresented,bool bPresentationClock,double PresentationAtServerSec,double SampledAtServerSec,double HeldSeconds);

    // Shown records of the widget, stamped with local presentation time.
    struct FHeldNumber
    {
        FEvent Event;
        double QueuedAt=0;
    };
    struct FFloater
    {
        FEvent Event;
        FString Text;
        double BornAt=0;
        double PopAt=0;            // a merged number pops again without restarting its rise
        // Own numbers: 0 centred over the own bars, then alternating right and left. Unit numbers: step*2+side (0 right,
        // 1 left) beside the body. Rewards: row beside the own body.
        int32 Lane=0;
    };
    struct FBanner
    {
        EKind Kind=EKind::LevelUp;
        FString Text;
        FString Subline;
        double BornAt=0;
    };
}

// Full-viewport layer above the world labels that is never hit-tested or focused; ULDUIRoot shows it exactly while the
// in-match HUD is. Ticks observe the owner's approved facts of the running match; paints draw only for the living owner.
// A unit's numbers show with its delayed presented pose and last only while that unit is presented (no LastSeen).
UCLASS()
class LOCKDOWN_API ULDWorldFeedback : public UUserWidget
{
    GENERATED_BODY()
public:
#if WITH_DEV_AUTOMATION_TESTS
    // DEV scenarios: every own reward line ("+60 <LDTerms::Experience>") this widget presented in the current match, in order.
    const TArray<FString>& DevPresentedRewards() const { return DevRewards; }
#endif
protected:
    virtual void NativeTick(const FGeometry& Geometry,float Delta) override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool Enabled) const override;
private:
    void Present(const LDWorldFeedback::FEvent& Event,double Now);
    void ClearShown();
    LDWorldFeedback::FTracker Tracker;
    // Visible-unit numbers waiting for the presentation clock (UnitHold), in arrival order.
    TArray<LDWorldFeedback::FHeldNumber> Held;
    TArray<LDWorldFeedback::FFloater> Floaters;
    TArray<LDWorldFeedback::FBanner> Banners;
    double LevelUpAt=-1;
    double LastTickAt=-1;
    // The own screen edge flash (ULDImpactFxSubsystem::ScreenFlashAlpha), drawn under every line.
    mutable FSlateBrush EdgeBrush;
    uint32 ShownMatch=0;
    uint32 ShownLife=0;
    // Approved public definitions, only for the display names of visible casts; loaded once per widget.
    TSharedPtr<const FLDDemoContentSnapshot> DisplayContent;
    bool bDisplayContentTried=false;
#if WITH_DEV_AUTOMATION_TESTS
    TArray<FString> DevRewards;
    uint32 DevRewardsMatch=0;
#endif
};
