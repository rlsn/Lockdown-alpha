#pragma once
#include "CoreMinimal.h"

// Shared in-match presentation tokens, so the HUD band, overhead bars, world feedback and minimap keep one visual
// language: the own body reads green, other contenders red, camp members amber, mana blue and the shield the bright cyan of
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
    inline const FLinearColor OwnHealth(.05f,.45f,.08f);
    inline const FLinearColor EnemyHealth(.70f,.07f,.05f);
    inline const FLinearColor CampHealth(.85f,.32f,.03f);
    inline const FLinearColor Mana(.05f,.22f,.85f);
    inline const FLinearColor Shield(0.f,.78f,1.f);
    inline const FLinearColor Experience(.70f,.45f,.05f);
    inline const FLinearColor BarTrack(.04f,.05f,.07f,.85f);

    // Floating feedback
    inline const FLinearColor DamageDealt(1.f,.93f,.80f);
    inline const FLinearColor DamageTaken(1.f,.36f,.30f);
    inline const FLinearColor Heal(.45f,.95f,.50f);
    inline const FLinearColor ShieldGain(.45f,.90f,1.f);
    inline const FLinearColor ExperienceGain(.80f,.66f,1.f);
    inline const FLinearColor Celebration(1.f,.86f,.36f);

    // Action slot states: each limit has its own fill, and the slot text names it, so no state relies on colour alone.
    inline const FLinearColor SlotReady(.06f,.22f,.24f,.98f);
    inline const FLinearColor SlotArmed(.36f,.27f,.06f,.98f);
    inline const FLinearColor SlotBlocked(.05f,.06f,.08f,.96f);
    inline const FLinearColor SlotNoMana(.08f,.13f,.34f,.98f);
    inline const FLinearColor SlotControlled(.28f,.10f,.32f,.98f);
    inline const FLinearColor SlotCooldownMask(0.f,0.f,0.f,.62f);
    inline const FLinearColor SlotReadyFlash(1.f,.92f,.55f);

    // Build interface palette (sRGB hex, converted to linear). Surfaces step from the base through panels to raised
    // panels; yellow marks what the player can act on, pink marks events; green, orange and red mark a legal place, a
    // replacement and a refusal; the three component types each own one colour. Combat colours above stay separate.
    inline const FLinearColor UIBase=FLinearColor(FColor(0x07,0x09,0x0F));
    inline const FLinearColor UIPanel=FLinearColor(FColor(0x15,0x1B,0x29));
    inline const FLinearColor UIRaised=FLinearColor(FColor(0x1F,0x27,0x38));
    inline const FLinearColor UIText=FLinearColor(FColor(0xF4,0xEC,0xDE));
    inline const FLinearColor UIAction=FLinearColor(FColor(0xFF,0xE1,0x4A));
    inline const FLinearColor UIEvent=FLinearColor(FColor(0xFF,0x3F,0x7F));
    inline const FLinearColor UILegal=FLinearColor(FColor(0x57,0xE3,0x89));
    inline const FLinearColor UIReplace=FLinearColor(FColor(0xFF,0x9A,0x3D));
    inline const FLinearColor UIReject=FLinearColor(FColor(0xFF,0x6A,0x5D));
    inline const FLinearColor ComponentBase=FLinearColor(FColor(0xDF,0xE4,0xEE));
    inline const FLinearColor ComponentMod=FLinearColor(FColor(0x3F,0xD8,0xF0));
    inline const FLinearColor ComponentLink=FLinearColor(FColor(0xB5,0x8C,0xFF));
    constexpr float RadiusPanel=6.f;
    constexpr float RadiusButton=5.f;

    // Menu and lobby text
    inline const FLinearColor MenuTitle(1.f,.82f,.28f);
    inline const FLinearColor MenuSection(.55f,.86f,.92f);
    inline const FLinearColor MenuMuted(.66f,.74f,.78f);
    inline const FLinearColor MenuWarning(1.f,.78f,.55f);
    inline const FLinearColor ModalScrim(0.f,0.f,0.f,.62f);
    inline const FLinearColor InputText(.04f,.07f,.07f);
    inline const FLinearColor RosterHeader(.035f,.07f,.09f);
    inline const FLinearColor RosterRow(.045f,.09f,.12f);
    inline const FLinearColor RosterOwnRow(.07f,.17f,.21f);

    // Command buttons: flat rounded faces from one teal family; keyboard focus draws an action-yellow outline.
    inline const FLinearColor ButtonNormalFill(.045f,.12f,.15f);
    inline const FLinearColor ButtonNormalEdge(.12f,.30f,.33f);
    inline const FLinearColor ButtonHoveredFill(.09f,.24f,.27f);
    inline const FLinearColor ButtonHoveredEdge(.30f,.62f,.62f);
    inline const FLinearColor ButtonPressedFill(.12f,.32f,.32f);
    inline const FLinearColor ButtonPressedEdge(.40f,.75f,.72f);
    inline const FLinearColor ButtonDisabledFill(.035f,.05f,.065f);
    inline const FLinearColor ButtonDisabledEdge(.08f,.11f,.13f);
    inline const FLinearColor FocusOutline(1.f,.82f,.28f);
    // Hold-to-confirm fill sweeping across the button face.
    inline const FLinearColor HoldFill(1.f,.70f,.45f,.45f);

    // Cards, component tiles and their columns
    inline const FLinearColor CardFill(.035f,.06f,.09f,.97f);
    inline const FLinearColor CardIdle(.045f,.12f,.15f);
    inline const FLinearColor ColumnFill(.02f,.03f,.05f,.84f);
    inline const FLinearColor OverlayFill(.02f,.035f,.055f,.94f);
    inline const FLinearColor IconFill(.16f,.26f,.36f);
    inline const FLinearColor IconRim(1.f,1.f,1.f,.18f);
    inline const FLinearColor IconBlocked(.42f,.42f,.42f);
    inline const FLinearColor ShapeFill(.12f,.13f,.15f);
    inline const FLinearColor ShapeRim(1.f,1.f,1.f,.55f);
    inline const FLinearColor DropLegal=StatusPositive;
    inline const FLinearColor DropReplace(1.f,.72f,.25f);

    // Type scale. Every size the interface draws comes from here; the local text-size preference scales it.
    constexpr int32 TypeMicro=11;
    constexpr int32 TypeTiny=12;
    constexpr int32 TypeCaption=13;
    constexpr int32 TypeSmall=14;
    constexpr int32 TypeBody=15;
    constexpr int32 TypeText=16;
    constexpr int32 TypeLabel=17;
    constexpr int32 TypeLead=18;
    constexpr int32 TypeHeading=20;
    constexpr int32 TypeTitle=21;
    constexpr int32 TypeSection=22;
    constexpr int32 TypeSubBanner=26;
    constexpr int32 TypeVerdict=28;
    constexpr int32 TypePage=30;
    constexpr int32 TypeBanner=32;
    constexpr int32 TypeDisplay=34;
    constexpr int32 TypeMenuTitle=40;
    constexpr int32 TypeHero=44;
    constexpr int32 TypeGiant=96;
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
