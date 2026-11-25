#pragma once
#include "CoreMinimal.h"

// Shared in-match presentation tokens, so the HUD band, overhead bars, world feedback and minimap keep one visual
// language: the own body reads green, other contenders red, camp members amber, momentum hot orange and the shield the bright cyan of
// the body's shield marker.
// Local presentation only; no gameplay value or rule lives here. Sizes are Slate units at the 1080p reference.
namespace LDHUDStyle
{
    // Surfaces and text
    inline const FLinearColor PanelFill(.025f,.045f,.07f,.94f);
    inline const FLinearColor PanelRim(.42f,.55f,.62f,.45f);
    inline const FLinearColor TextPrimary(.94f,.96f,.98f);
    inline const FLinearColor TextSecondary(.70f,.78f,.84f);
    inline const FLinearColor TextAccent(1.f,.82f,.40f);
    inline const FLinearColor TextWarning(1.f,.70f,.45f);
    inline const FLinearColor TextDanger(1.f,.47f,.42f);
    // Own status squares: what helps the bearer is edged green, what hurts it red (LDHUD::FSelfIconView::bPositive).
    inline const FLinearColor StatusPositive(.45f,.95f,.50f);
    inline const FLinearColor StatusNegative(1.f,.47f,.42f);
    // Outline behind world-anchored text and bars, so they read over any ground or body.
    inline const FLinearColor Rim(0.f,0.f,0.f,.85f);

    // Resources. Values are linear (Slate converts to sRGB), so saturated fills need low channels; white bar text with a
    // dark outline then keeps its contrast.
    inline const FLinearColor EnemyHealth(.70f,.07f,.05f);
    // Red-green colorblind friendly Okabe-Ito palette (#331): own crisp sky-blue, hostile vivid orange-vermilion.
    inline const FLinearColor EnemyHealthColorblind(.90f,.32f,.02f);
    // A contender who cannot be attacked right now (anyone in a rest outside the PK, a PK teammate): never the hostile red.
    inline const FLinearColor PeacefulHealth(.16f,.42f,.78f);
    inline const FLinearColor CampHealth(.85f,.32f,.03f);
    // A camp member's tier (ELDDemoCampTier as uint8): Low white, High blue, Elite violet, Ancient gold, the Boss red. The world
    // label's name and bar frame and the minimap's camp diamond share it.
    inline FLinearColor CampTierInk(uint8 Tier)
    {
        switch(Tier)
        {
        case 0: return FLinearColor(.94f,.95f,.96f);
        case 1: return FLinearColor(.26f,.58f,1.f);
        case 2: return FLinearColor(1.f,.30f,.22f);
        case 3: return FLinearColor(.70f,.38f,1.f);
        case 4: return FLinearColor(1.f,.76f,.16f);
        default: return FLinearColor(1.f,.8f,.42f);
        }
    }
    // Momentum: a hot orange bar; a full bar glows overdrive gold (the bar, slot rings, target bars and charge halos).
    inline const FLinearColor Momentum(.95f,.30f,.02f);
    inline const FLinearColor Overdrive(1.f,.80f,.10f);
    inline const FLinearColor Shield(0.f,.78f,1.f);
    inline const FLinearColor BarTrack(.04f,.05f,.07f,.85f);

    // Floating feedback
    inline const FLinearColor DamageDealt(1.f,.93f,.80f);
    inline const FLinearColor DamageTaken(1.f,.36f,.30f);
    inline const FLinearColor Heal(.45f,.95f,.50f);
    inline const FLinearColor ShieldGain(.45f,.90f,1.f);
    inline const FLinearColor ExperienceGain(.80f,.66f,1.f);
    inline const FLinearColor Celebration(1.f,.86f,.36f);

    inline const FLinearColor SlotReadyFlash(1.f,.92f,.55f);

    // Build interface palette (sRGB hex, converted to linear): the raised surface, the text, the action, legal, replace and
    // reject marks, and one colour per component type. Combat colours above stay separate.
    inline const FLinearColor UIRaised=FLinearColor(FColor(0x1F,0x27,0x38));
    inline const FLinearColor UIText=FLinearColor(FColor(0xF4,0xEC,0xDE));
    inline const FLinearColor UIAction=FLinearColor(FColor(0xFF,0xE1,0x4A));
    inline const FLinearColor UILegal=FLinearColor(FColor(0x57,0xE3,0x89));
    inline const FLinearColor UIReplace=FLinearColor(FColor(0xFF,0x9A,0x3D));
    inline const FLinearColor UIReject=FLinearColor(FColor(0xFF,0x6A,0x5D));
    // Card types (ELDCardType): each its own colour beside its own shape (LDUIShapes::KindShape); Element stones use #10B981 emerald (#328 §2).
    inline const FLinearColor CardTrigger=FLinearColor(FColor(0xDF,0xE4,0xEE));
    inline const FLinearColor CardModify=FLinearColor(FColor(0x3F,0xD8,0xF0));
    inline const FLinearColor CardConvert=FLinearColor(FColor(0xB5,0x8C,0xFF));
    inline const FLinearColor CardGrow=FLinearColor(FColor(0x7E,0xE0,0x81));
    inline const FLinearColor CardElement=FLinearColor(FColor(0x10,0xB9,0x81));
    // #328 §2 3-way reward rims: Active Skill #F59E0B, Build Card #3B82F6, Element Stone #10B981.
    inline const FLinearColor RewardActiveRim=FLinearColor(FColor(0xF5,0x9E,0x0B));
    inline const FLinearColor RewardCardRim=FLinearColor(FColor(0x3B,0x82,0xF6));
    inline const FLinearColor RewardStoneRim=FLinearColor(FColor(0x10,0xB9,0x81));

    // Type scale calibrated for FontDPI=96 (1 pt = 4/3 px at 1080p; the UI scales by 2/3 at 720p): the smallest step is 14pt
    // (18.7px at 1080p, 12.4px at 720p), so no text drops under 12 px at 720p; key labels and numbers 14-15pt.
    // Every size the interface draws comes from here; TextScale multiplies it.
    constexpr int32 TypeMicro=14;
    constexpr int32 TypeTiny=14;
    constexpr int32 TypeCaption=14;
    constexpr int32 TypeSmall=14;
    constexpr int32 TypeBody=14;
    constexpr int32 TypeText=15;
    constexpr int32 TypeLabel=14;
    constexpr int32 TypeLead=15;
    constexpr int32 TypeHeading=16;
    constexpr int32 TypeTitle=17;
    constexpr int32 TypeSection=18;
    constexpr int32 TypePage=23;
    constexpr int32 TypeBanner=25;
    constexpr int32 TypeDisplay=26;
    constexpr int32 TypeGiant=72;
    // Title role: the heavy face leans right by 8 degrees.
    constexpr float TitleSkew=.1405f;

    // Local presentation timings (real seconds; never compared with an authority deadline).
    constexpr double FloatSeconds=1.1;
    constexpr double BannerSeconds=2.4;
    constexpr double ReadyFlashSeconds=.45;
    constexpr double NoticeSeconds=2.;
    constexpr double PulseSeconds=6.;
    // An event-feed line fades out over its last half second.
    constexpr double FeedFadeSeconds=.5;

    // Interface motion. Motion is presentation only: it never delays input or a submission, and the reduced combat-effects
    // setting does not change it.
    constexpr double MotionHover=.08;
    constexpr double MotionPickup=.12;
    constexpr double MotionSnap=.10;
    constexpr double MotionInstallLand=.16;
    constexpr double MotionInstallSettle=.30;
    constexpr double MotionReject=.18;
    constexpr double MotionRejectNotice=2.;
    constexpr double MotionWorkshopOpen=.24;
    constexpr double MotionWorkshopClose=.16;
    constexpr double MotionOfferCards=.32;
    constexpr double MotionOfferStagger=.06;
    constexpr double MotionChoose=.40;
    constexpr double MotionGain=.45;
    constexpr double MotionAddPoint=.12;
    constexpr double MotionHoldConfirm=.60;
    constexpr double MotionFinalLock=.40;
    constexpr float HoverScale=1.04f;
    constexpr float PickupScale=1.20f;
    constexpr float PressScale=.97f;
    constexpr float ReadyPopScale=1.08f;
}
