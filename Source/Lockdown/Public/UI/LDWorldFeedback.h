#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "LDWorldFeedback.generated.h"
struct FLDCombatOwnerView;
struct FLDProgressionView;
struct FLDEquipmentView;
struct FLDKillFeedView;
struct FLDWorldView;
class FLDDemoContentSnapshot;

// Feedback where the player is looking (APP_SPEC 8): numbers over the own body and over visible units, the own rewards
// beside the own body, and level-up and own-kill confirmations above it. Local presentation of approved facts only; it
// never predicts, extrapolates or changes a gameplay value.
namespace LDWorldFeedback
{
    enum class EKind : uint8 { DamageTaken, Heal, ShieldGain, ShieldAbsorbed, UnitDamage, UnitHeal, Gold, Experience, LevelUp, Kill, UnitCast };
    struct FEvent
    {
        EKind Kind=EKind::DamageTaken;
        int64 Amount=0;            // whole points (half-up from P2), gold or XP; LevelUp: three-choices newly earned; Kill: 1
        bool bOwn=true;            // anchored to the own body, otherwise to the visible unit Handle/Epoch
        FGuid Handle;
        uint32 Epoch=0;
        int32 Level=0;             // LevelUp: the level reached
        bool bChoiceEarned=false;  // LevelUp: at least one three-choice was earned with it
        FString Name;              // Kill: the victim's public roster name; UnitCast: the cast's display name; empty when unknown
        double SampledAtServerSec=0; // UnitDamage/UnitHeal: SampledAtServerSec of the frame that showed the change
    };
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
        const FLDEquipmentView* Equipment=nullptr;
        const FLDKillFeedView* KillFeed=nullptr;
        uint32 PublicGeneration=0;
        // Authority clock sample, used only for the kill-feed freshness rule.
        bool bClock=false;
        int64 NowMs=0;
        const FLDWorldView* Frame=nullptr;
        // Public roster name of a contender; empty when the roster does not list it.
        TFunction<FString(const FGuid&)> RosterName;
        // Display name of a visible cast's source (an ability or an equipment native id, the other 0); empty when unknown.
        TFunction<FString(uint8,uint8)> CastName;
    };
    // Turns consecutive samples into events. The first sample of a match only primes (and so does the first body view
    // of a life, the first frame of an observation generation and the first valid growth view after a missing one);
    // older or foreign generations never produce events. Gold and XP float only for a reward, which an XP gain
    // identifies: buying and selling change gold alone and stay silent.
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
            // The unit's cue (ELDObservedActionCue), its age and cast source in that frame, so a new cast is told apart.
            uint8 Cue=0;
            uint16 CueAgeMs=0;
            uint8 Ability=0;
            uint8 Equipment=0;
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
        bool bGrowth=false;
        uint8 Level=0;
        uint8 Earned=0;
        int64 XP=0;
        bool bGold=false;
        int64 Gold=0;
        bool bKills=false;
        uint64 KillSerial=0;
        bool bFrame=false;
        uint32 FrameGeneration=0;
        uint32 FrameRevision=0;
        TArray<FUnit> Units;
    };
    // "-37", "+12", then the LDTerms lines: shield gained/absorbed, gold, experience, level-up and kill (an unlisted victim reads
    // LDTerms::UnknownVictim),
    LOCKDOWN_API FString Text(const FEvent& Event);
    // Smaller second line of a level-up banner (LDTerms::ChoicesEarned); empty for every other event.
    LOCKDOWN_API FString Subline(const FEvent& Event);
    // A visible unit's number waits for the relay's presentation clock to reach the frame that produced it, so it shows
    // with the proxy's delayed hit pose. It is released once the clock reaches its stamp, at once without a clock and
    // after UnitHoldMaxSeconds of local time at most; it is dropped as soon as its unit is no longer presented.
    // The own body is not delayed: its numbers and the rewards never wait.
    enum class EUnitHold : uint8 { Hold, Release, Drop };
    inline constexpr double UnitHoldMaxSeconds=.5;
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
    // DEV scenarios: every own reward line ("+45 <LDTerms::Gold>", "+60 <LDTerms::Experience>") this widget presented in the current match, in order.
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
