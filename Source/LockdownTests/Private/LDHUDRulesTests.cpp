#include "UI/LDHUDModel.h"
#include "Combat/LDChainRules.h"
#include "Combat/LDDamageRecap.h"
#include "Combat/LDCombatElements.h"
#include "UI/LDBlueprintPanel.h"
#include "Development/LDDemoCardText.h"
#include "UI/LDTerms.h"
#include "UI/LDUIIcons.h"
#include "UI/LDDevTestPanel.h"
#include "Development/LDDevTestProtocol.h"
#include "Development/LDGrowthProtocol.h"
#include "Combat/LDCombatTypes.h"
#include "UI/LDMinimap.h"
#include "UI/LDWorldFeedback.h"
#include "UI/LDWidgets.h"
#include "UI/LDResultBuild.h"
#include "UI/LDWorkshopModel.h"
#include "UI/LDWorkshop.h"
#include "Components/CanvasPanelSlot.h"
#include "UI/LDStatBar.h"
#include "UI/LDWorkshopPreview.h"
#include "Framework/LDDemoEntry.h"
#include "PK/LDPKRules.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Combat/LDAttributeProjection.h"
#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDTacticalRules.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDDemoContentIds.h"
#include "Internationalization/StringTableCore.h"
#include "Internationalization/StringTableRegistry.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "LDTestApprovedDisplay.h"
#include "LDTestApprovedFixture.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDPacingTypes.h"
#include "Engine/Texture2D.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/RichTextBlock.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/World.h"
#include "Framework/LDCaptureTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/HittestGrid.h"
#include "Widgets/SWindow.h"
#include <limits>
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
// A private table id keeps the fixture away from the real DemoContent display table. Its names are the approved
// catalog's (#111), so expectations read them through HudName instead of repeating them.
const FName HudFixtureTable(TEXT("LDHUDRulesDisplayFixture"));
const TCHAR* const HudFixtureIds[]={TEXT("root"),TEXT("shield"),TEXT("respawn_protection"),TEXT("camp_returning"),TEXT("monster_guardian")};
TMap<FString,FString> HudFixtureNames;
FString HudName(const TCHAR* Id)
{
    const FString* Name=HudFixtureNames.Find(Id);
    return Name?*Name:FString(Id);
}
FLDDemoContentDefinitions HudFixtureContent()
{
    FLDDemoContentDefinitions Content;
    for(const TCHAR* Id:HudFixtureIds)
    {FLDDemoDisplayDefinition Display;Display.Id=Id;Display.Namespace=HudFixtureTable.ToString();Display.NameKey=FString(Id)+TEXT(".name");Display.DescriptionKey=FString(Id)+TEXT(".description");Display.IconId=FString(TEXT("icon_"))+Id;Content.Display.Add(Display);}
    FLDDemoMonsterDefinition Monster;Monster.Id=TEXT("monster_guardian");Monster.NativeId=uint8(ELDDemoMonsterId::MonsterGuardian);Content.Monsters.Add(Monster);
    return Content;
}
bool HudRegisterFixtureTable(FAutomationTestBase& Test)
{
    if(!LDTestApprovedDisplay::Names(Test,HudFixtureNames)){return false;}
    FStringTableRef Table=FStringTable::NewStringTable();Table->SetNamespace(HudFixtureTable.ToString());
    for(const TCHAR* Id:HudFixtureIds)
    {
        const FString* Name=HudFixtureNames.Find(Id);
        if(!Name){Test.AddError(FString::Printf(TEXT("The approved catalog has no display name for %s."),Id));return false;}
        const FString Key=FString(Id)+TEXT(".name");
        // Development game targets also compile these tests; only editor builds have the dev-notes overload.
#if WITH_EDITORONLY_DATA
        Table->SetSourceString(FTextKey(*Key),*Name,FString());
#else
        Table->SetSourceString(FTextKey(*Key),*Name);
#endif
    }
    FStringTableRegistry::Get().RegisterStringTable(HudFixtureTable,Table);
    return true;
}
// The game registers the approved display table at startup; when this editor session has not, the approved names are
// registered for the rendered faces (OutTable stays None when the existing table is used and nothing must be unregistered).
bool HudRegisterApprovedNames(FAutomationTestBase& Test,const FLDDemoContentDefinitions& Content,FName& OutTable)
{
    OutTable=NAME_None;
    TMap<FString,FString> Names;
    if(!LDTestApprovedDisplay::Names(Test,Names)||Content.Display.IsEmpty()){return false;}
    const FName Namespace(*Content.Display[0].Namespace);
    if(FStringTableRegistry::Get().FindStringTable(Namespace).IsValid()){return true;}
    OutTable=Namespace;
    FStringTableRef Table=FStringTable::NewStringTable();Table->SetNamespace(OutTable.ToString());
    for(const FLDDemoDisplayDefinition& Row:Content.Display)
    {
        const FString* Name=Names.Find(Row.Id);
        if(!Name||Row.Namespace!=OutTable.ToString()){continue;}
        const auto Put=[&](const FString& Key,const FString& Value)
        {
            if(Key.IsEmpty()){return;}
#if WITH_EDITORONLY_DATA
            Table->SetSourceString(FTextKey(*Key),Value,FString());
#else
            Table->SetSourceString(FTextKey(*Key),Value);
#endif
        };
        Put(Row.NameKey,*Name);Put(Row.DescriptionKey,Row.DescriptionText);
    }
    FStringTableRegistry::Get().RegisterStringTable(OutTable,Table);
    return true;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDPaintOpacityTest,"Lockdown.Rules.HUDPaintOpacity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDPaintOpacityTest::RunTest(const FString& Parameters)
{
    if(!FSlateApplication::IsInitialized())
    {
        AddError(TEXT("The Slate application is required to exercise the paint layer."));
        return false;
    }
    ULDPaintLayer* Layer=NewObject<ULDPaintLayer>();
    Layer->Painter=[](const LDUIPaint::FContext& Paint)
    {
        const FBox2D Box(FVector2D(8,8),FVector2D(72,72));
        Paint.Shape(Paint.Layer,LDUIShapes::EShape::Rounded,Box,5.f,LDUIPaint::FFill::Solid(FLinearColor(1,1,1,.8f)));
        Paint.Edge(Paint.Layer+1,LDUIShapes::EShape::Rounded,Box,5.f,FLinearColor(1,1,1,.6f),2.f);
        Paint.WithOpacity(.5f).Tile(Paint.Layer+2,Box,1,LDUIPaint::ETileState::Normal,nullptr);
    };
    const TSharedRef<SWidget> Slate=Layer->TakeWidget();
    Layer->SetRenderOpacity(.5f);
    FHittestGrid HitTest;
    FSlateWindowElementList Draw{TSharedPtr<SWindow>()};
    const FGeometry Geometry=FGeometry::MakeRoot(FVector2D(80,80),FSlateLayoutTransform());
    FWidgetStyle ParentStyle;ParentStyle.BlendOpacity(.5f);
    Slate->Paint(FPaintArgs(nullptr,HitTest,FVector2D::ZeroVector,0.,0.f),Geometry,FSlateRect(0,0,80,80),Draw,0,ParentStyle,true);

    const auto& Gradients=Draw.GetUncachedDrawElements().Get<(uint8)EElementType::ET_Gradient>();
    TestTrue(TEXT("The layer paints the panel gradient"),!Gradients.IsEmpty()&&!Gradients[0].GradientStops.IsEmpty());
    if(!Gradients.IsEmpty()&&!Gradients[0].GradientStops.IsEmpty())
    {
        TestTrue(TEXT("A panel surface inherits parent and layer opacity"),FMath::IsNearlyEqual(Gradients[0].GradientStops[0].Color.A,.2f,.001f));
    }
    const auto& Meshes=Draw.GetUncachedDrawElements().Get<(uint8)EElementType::ET_CustomVerts>();
    TestTrue(TEXT("The edge and tile paint custom vertices"),Meshes.Num()>1);
    if(Meshes.Num()>1)
    {
        const auto MaxAlpha=[](const FSlateCustomVertsElement& Mesh)
        {
            uint8 Max=0;
            for(const FSlateVertex& Vertex:Mesh.Vertices){Max=FMath::Max(Max,Vertex.Color.A);}
            return Max;
        };
        TestTrue(TEXT("The edge inherits parent and layer opacity"),FMath::Abs(int32(MaxAlpha(Meshes[0]))-38)<=1);
        bool bTileFaded=true;
        for(int32 Index=1;Index<Meshes.Num();++Index){bTileFaded&=MaxAlpha(Meshes[Index])<=33;}
        TestTrue(TEXT("The tile's local opacity also multiplies the inherited opacity"),bTileFaded);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDMaskTest,"Lockdown.Rules.HUDObservationBoundary",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDMaskTest::RunTest(const FString& Parameters)
{
    if(!HudRegisterFixtureTable(*this)){return false;}
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(HudFixtureTable);};
    const FLDDemoContentDefinitions Content=HudFixtureContent();
    FLDObservedUnitView Rival;Rival.Kind=ELDObservedUnitKind::Contender;Rival.AuthorizedFieldMask=LDObservation::ContenderFields;Rival.ContenderId=FGuid::NewGuid();Rival.Level=3;
    Rival.Health.CurrentP2=45001;Rival.Health.MaxP2=60000;Rival.MomentumBar=153;
    FLDObservedStatus Root;Root.StatusId=uint8(ELDDemoStatusId::Root);Root.ExpiresAtServerMs=12500;Rival.Statuses.Add(Root);
    FLDObservedStatus Protection;Protection.StatusId=uint8(ELDDemoStatusId::RespawnProtection);Protection.ExpiresAtServerMs=11500;Rival.Statuses.Add(Protection);
    const FString Shown=LDHUD::ObservedUnit(Rival,TEXT("玩家甲"),&Content,10.);
    auto Has=[](const FString& Text,const TCHAR* Label){return Text.Contains(Label,ESearchCase::CaseSensitive);};
    TestEqualSensitive(TEXT("Observed card renders exactly the SC-07 whitelist with Q/W/E and approved display names"),Shown,
        FString::Printf(TEXT("玩家甲 · 等级 3\n%s 451/600 · %s 60%%\n状态：%s 2.5s · %s 1.5s\n技能：Q 空槽 · W 空槽 · E 空槽"),LDTerms::Health,LDTerms::Momentum,
            *HudName(TEXT("root")),*HudName(TEXT("respawn_protection"))));
    for(const TCHAR* Private:{LDTerms::Experience,TEXT("冷却"),TEXT("被动"),LDTerms::AttackDamage,LDTerms::Armor,TEXT("属性"),TEXT("98765")})
    {TestFalse(FString::Printf(TEXT("Observed card has no private category: %s"),Private),Has(Shown,Private));}
    TestTrue(TEXT("A contender missing from the public roster is not given an invented name"),LDHUD::ObservedUnit(Rival,FString(),&Content,10.).StartsWith(TEXT("未列名参赛者"),ESearchCase::CaseSensitive));
    FLDObservedUnitView Camp;Camp.Kind=ELDObservedUnitKind::CampMember;Camp.AuthorizedFieldMask=LDObservation::CampFields;Camp.CreatureId=uint8(ELDDemoMonsterId::MonsterGuardian);
    Camp.Health.CurrentP2=100;Camp.Health.MaxP2=200;FLDObservedStatus Returning;Returning.StatusId=uint8(ELDDemoStatusId::CampReturning);Camp.Statuses.Add(Returning);
    const FString CampShown=LDHUD::ObservedUnit(Camp,TEXT("Ignored"),&Content,0.);
    TestEqualSensitive(TEXT("Camp card renders exactly name, HP and public status"),CampShown,HudName(TEXT("monster_guardian"))+TEXT("\n")+LDTerms::Health+TEXT(" 1/2\n状态：")+HudName(TEXT("camp_returning")));
    TestFalse(TEXT("Camp card has no level, momentum or a contender name"),Has(CampShown,TEXT("等级"))||Has(CampShown,LDTerms::Momentum)||Has(CampShown,TEXT("Ignored")));
    const LDHUD::FObservedCard CampCard=LDHUD::ObservedCard(Camp,FString(),&Content,0.);
    TestTrue(TEXT("Card structure follows the authorized field mask"),!CampCard.bMomentum&&!CampCard.bActives&&CampCard.Health.bKnown);
    FString Text;
    // The missing-text path is a content error by design: it logs once per id and shows the stable id.
    AddExpectedMessage(TEXT("LD_HUD_DISPLAY_TEXT_MISSING hud_rules_fixture_without_text"),ELogVerbosity::Error,EAutomationExpectedMessageFlags::Contains,1,false);
    TestFalse(TEXT("A definition without display text is a content error"),LDHUD::ContentText(&Content,TEXT("hud_rules_fixture_without_text"),false,Text));
    TestEqualSensitive(TEXT("A missing display text shows the stable id, never a guessed name"),Text,FString(TEXT("hud_rules_fixture_without_text")));
    TestTrue(TEXT("A registered display text resolves through the DemoContent table"),LDHUD::ContentText(&Content,TEXT("root"),false,Text)&&Text==HudName(TEXT("root")));
    TestEqualSensitive(TEXT("Without approved content an observed creature id is reported, not named"),LDHUD::CreatureName(nullptr,2),FString(TEXT("未知野怪 #2")));
    TestEqualSensitive(TEXT("An unknown status id is reported, not named"),LDHUD::StatusName(&Content,200),FString(TEXT("未知状态 #200")));
    FLDLobbyView Lobby;FLDLobbyMember Member;Member.ContenderId=Rival.ContenderId;Member.Name=TEXT("玩家甲");Lobby.Members.Add(Member);
    TestEqualSensitive(TEXT("Roster names come from the public roster"),LDHUD::RosterName(Lobby,Rival.ContenderId),FString(TEXT("玩家甲")));
    TestTrue(TEXT("An unlisted contender has no invented roster name"),LDHUD::RosterName(Lobby,FGuid::NewGuid()).IsEmpty()&&LDHUD::RosterName(Lobby,FGuid()).IsEmpty());
    const FBox2D Ground(FVector2D(-5000,-5000),FVector2D(5000,5000));
    const LDMinimap::FView MapView=LDMinimap::MakeView(ELDMinimapOrientation::North,Ground,FVector2D(240,240),0.,false,0.);
    const FVector World(-1700,-1000,200);const FVector Back=LDMinimap::ToWorld(MapView,LDMinimap::ToFrame(MapView,World));
    TestTrue(TEXT("Map conversion round trips XY and deliberately grants no height or dynamic target"),MapView.IsValid()&&FVector::Dist2D(World,Back)<.001&&Back.Z==0);
    const FVector Edge=LDMinimap::ClampToGround(LDMinimap::ToWorld(MapView,FVector2D(5,-3)*MapView.FrameSize),Ground);
    TestTrue(TEXT("Map edge clamp stays inside the registered ground"),Edge.X>=Ground.Min.X&&Edge.X<Ground.Max.X&&Edge.Y>=Ground.Min.Y&&Edge.Y<Ground.Max.Y&&Edge.Z==0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDLifeStatusTest,"Lockdown.Rules.HUDLifeStatus",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDLifeStatusTest::RunTest(const FString& Parameters)
{
    FLDOwnerState Own;Own.LifePhase=ELDLifePhase::Alive;Own.ProtectedUntilMs=5000;
    TestEqualSensitive(TEXT("Protection counts down from the matching authority clock"),LDHUD::LifeStatus(Own,true,3000),FString(LDTerms::RespawnProtection)+TEXT(" 2.0s · ")+LDTerms::ProtectionRule);
    TestEqualSensitive(TEXT("A live protection never reads zero before the clock reaches its deadline"),LDHUD::LifeStatus(Own,true,4999),FString(LDTerms::RespawnProtection)+TEXT(" 0.1s · ")+LDTerms::ProtectionRule);
    TestEqualSensitive(TEXT("Protection ends once the authority clock reaches the deadline, before the field is cleared"),LDHUD::LifeStatus(Own,true,5000),FString(TEXT("存活")));
    TestEqualSensitive(TEXT("Without the matching clock protection time is unavailable, never local time or zero"),LDHUD::LifeStatus(Own,false,0),FString(LDTerms::RespawnProtection)+TEXT(" · 时间同步中"));
    TestEqualSensitive(TEXT("A time value is ignored while the clock is unavailable"),LDHUD::LifeStatus(Own,false,999999),FString(LDTerms::RespawnProtection)+TEXT(" · 时间同步中"));
    Own.ProtectedUntilMs=0;
    TestEqualSensitive(TEXT("No protection deadline needs no clock"),LDHUD::LifeStatus(Own,false,0),FString(TEXT("存活")));
    TestFalse(TEXT("A living owner has no death banner"),LDHUD::Death(Own,true,0).bVisible);
    Own.LifePhase=ELDLifePhase::Dead;Own.Match=2;Own.Life=3;Own.RespawnAtMs=12000;
    const FString Waiting=LDHUD::LifeStatus(Own,true,2350);
    TestEqualSensitive(TEXT("Respawn waits on the Lives deadline"),Waiting,FString(LDTerms::Dead)+TEXT(" · ")+LDTerms::RespawnWait+TEXT(" 9.7s"));
    TestFalse(TEXT("Death shows no growth loss"),Waiting.Contains(TEXT("损失")));
    TestEqualSensitive(TEXT("The respawn countdown reads zero only once the authority clock reaches it"),LDHUD::LifeStatus(Own,true,12000),FString(LDTerms::Dead)+TEXT(" · ")+LDTerms::RespawnWait+TEXT(" 0.0s"));
    TestEqualSensitive(TEXT("Respawn time without the matching clock is unavailable"),LDHUD::LifeStatus(Own,false,0),FString(LDTerms::Dead)+TEXT(" · ")+LDTerms::RespawnWait+TEXT("（时间同步中）"));
    FLDCombatOwnerView Combat;Combat.Generation=2;Combat.Life=3;Combat.RespawnDurationMs=10000;
    const LDHUD::FDeathView Death=LDHUD::Death(Own,true,2350,false,&Combat);
    TestTrue(TEXT("The death banner names the state and the countdown and keeps the build"),Death.bVisible&&Death.Title==LDTerms::DeathTitle&&Death.Countdown==FString(LDTerms::RespawnWait)+TEXT(" 9.7s")&&Death.Note==LDTerms::DeathNote());
    TestTrue(TEXT("The death ring uses the server's full wait and a compact clock"),
        Death.bWaiting&&Death.bProgressKnown&&FMath::IsNearlyEqual(Death.Progress,.965f)&&Death.Clock==TEXT("0:10"));
    TestEqual(TEXT("An early clock cannot make the ring exceed a full circle"),LDHUD::Death(Own,true,1000,false,&Combat).Progress,1.f);
    TestEqual(TEXT("A due or blocked respawn leaves an empty ring"),LDHUD::Death(Own,true,13000,false,&Combat).Progress,0.f);
    Combat.RespawnDurationMs=5000;
    TestEqual(TEXT("A different server-fixed wait uses its own duration"),LDHUD::Death(Own,true,9500,false,&Combat).Progress,.5f);
    Combat.RespawnDurationMs=10000;
    TestEqualSensitive(TEXT("The banner never invents a respawn time"),LDHUD::Death(Own,false,0).Countdown,FString(LDTerms::RespawnWait)+TEXT(" · 时间同步中"));
    TestFalse(TEXT("Without the authority clock there is no ring or compact clock"),
        LDHUD::Death(Own,false,0,false,&Combat).bProgressKnown||!LDHUD::Death(Own,false,0,false,&Combat).Clock.IsEmpty());
    Combat.Generation=1;
    TestFalse(TEXT("An old match cannot supply the death ring"),LDHUD::Death(Own,true,2350,false,&Combat).bProgressKnown);
    Combat.Generation=2;Combat.Life=2;
    TestFalse(TEXT("An old life cannot supply the death ring"),LDHUD::Death(Own,true,2350,false,&Combat).bProgressKnown);
    Combat.Life=3;Combat.bHasBody=true;
    TestFalse(TEXT("A living combat projection cannot supply the death ring"),LDHUD::Death(Own,true,2350,false,&Combat).bProgressKnown);
    Combat.bHasBody=false;Combat.RespawnDurationMs=0;
    TestFalse(TEXT("An unpublished duration cannot supply the death ring"),LDHUD::Death(Own,true,2350,false,&Combat).bProgressKnown);
    Combat.RespawnDurationMs=12001;
    TestFalse(TEXT("An impossible duration cannot supply the death ring"),LDHUD::Death(Own,true,2350,false,&Combat).bProgressKnown);
    Own.RespawnAtMs=0;
    TestEqualSensitive(TEXT("An eliminated finalist is never promised another respawn"),LDHUD::Death(Own,true,0,true).Countdown,FString(TEXT("终局已出局")));
    TestFalse(TEXT("An eliminated finalist does not automatically enter the death-wait workshop"),LDHUD::Death(Own,true,0,true).bWaiting);
    Own.bActive=true;
    TestTrue(TEXT("The Lives phase, not the legacy active flag, decides the line"),LDHUD::LifeStatus(Own,true,0).StartsWith(LDTerms::Dead,ESearchCase::CaseSensitive));
    Own.LifePhase=ELDLifePhase::Unspawned;TestEqualSensitive(TEXT("An unspawned contender is not shown alive"),LDHUD::LifeStatus(Own,true,0),FString(TEXT("等待出生")));
    Own.LifePhase=ELDLifePhase::Left;TestEqualSensitive(TEXT("A departed contender is not shown alive"),LDHUD::LifeStatus(Own,true,0),FString(TEXT("已离开本局")));
    Own.LifePhase=static_cast<ELDLifePhase>(200);TestEqualSensitive(TEXT("An unknown phase is reported, not shown alive"),LDHUD::LifeStatus(Own,true,0),FString(TEXT("生命状态未知")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDRegenDisplayTest,"Lockdown.Rules.HUDRegenDisplay",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDRegenDisplayTest::RunTest(const FString& Parameters)
{
    FLDOwnerState Own;Own.Match=2;Own.Life=3;Own.LifePhase=ELDLifePhase::Alive;
    FLDCombatOwnerView View;View.Generation=2;View.Life=3;View.bHasBody=true;
    View.HPP2=View.MaxHPP2=62000;View.MomentumP2=View.MaxMomentumP2=30000;
    View.HPRegenPerSec=2.5;
    TestEqualSensitive(TEXT("Full HP still displays regen capacity"),LDHUD::RegenText(Own,&View,true),FString(TEXT("+2.5/s")));
    TestTrue(TEXT("Momentum has no steady rate to display"),LDHUD::RegenText(Own,&View,false).IsEmpty());
    View.HPP2=100;View.HPRegenPerSec=12.345;
    TestEqualSensitive(TEXT("Rate changes render to one decimal independent of HP deltas"),LDHUD::RegenText(Own,&View,true),FString(TEXT("+12.3/s")));
    View.HPRegenPerSec=0;
    TestEqualSensitive(TEXT("A known zero is explicit"),LDHUD::RegenText(Own,&View,true),FString(TEXT("+0.0/s")));View.HPRegenPerSec=12.345;
    TestTrue(TEXT("Missing projection does not invent zero"),LDHUD::RegenText(Own,nullptr,true).IsEmpty());
    View.Generation=1;TestTrue(TEXT("Old match is hidden"),LDHUD::RegenText(Own,&View,true).IsEmpty());View.Generation=2;
    View.Life=2;TestTrue(TEXT("Old life is hidden"),LDHUD::RegenText(Own,&View,true).IsEmpty());View.Life=3;
    Own.LifePhase=ELDLifePhase::Dead;TestTrue(TEXT("Death hides regen even with a lingering live view"),LDHUD::RegenText(Own,&View,true).IsEmpty());Own.LifePhase=ELDLifePhase::Alive;
    View.bHasBody=false;TestTrue(TEXT("Absent body hides regen"),LDHUD::RegenText(Own,&View,true).IsEmpty());View.bHasBody=true;
    View.HPP2=0;TestTrue(TEXT("Pending death hides regen"),LDHUD::RegenText(Own,&View,true).IsEmpty());View.HPP2=100;
    View.MaxHPP2=0;TestTrue(TEXT("Unknown HP maximum hides regen"),LDHUD::RegenText(Own,&View,true).IsEmpty());View.MaxHPP2=62000;
    View.HPRegenPerSec=std::numeric_limits<double>::quiet_NaN();TestTrue(TEXT("Nonfinite rate is hidden"),LDHUD::RegenText(Own,&View,true).IsEmpty());
    View.HPRegenPerSec=-1;TestTrue(TEXT("Malformed negative rate is hidden"),LDHUD::RegenText(Own,&View,true).IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDOwnResourcesTest,"Lockdown.Rules.HUDOwnResources",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDOwnResourcesTest::RunTest(const FString& Parameters)
{
    const FString Health(LDTerms::Health),Momentum(LDTerms::Momentum),Experience(LDTerms::Experience);
    const LDHUD::FResourceView Shown=LDHUD::Resource(Health,12.34,100,true);
    TestTrue(TEXT("Live health rounds up so a living body never reads zero"),Shown.bKnown&&Shown.Text==Health+TEXT(" 13/100")&&FMath::IsNearlyEqual(Shown.Fraction,.1234f,.0001f));
    TestEqualSensitive(TEXT("Momentum rounds down so a cost never looks affordable early"),LDHUD::Resource(Momentum,7.9,30,false).Text,Momentum+TEXT(" 7/30"));
    const LDHUD::FResourceView Unset=LDHUD::Resource(Health,0,0,true);
    TestTrue(TEXT("An unprojected maximum is syncing, never 0/0"),!Unset.bKnown&&Unset.Text==Health+TEXT(" 同步中")&&Unset.Fraction==0.f);
    TestFalse(TEXT("A non-finite value is not shown as a resource"),LDHUD::Resource(Health,std::numeric_limits<double>::quiet_NaN(),100,true).bKnown);
    TestFalse(TEXT("A negative P2 value is not shown as a resource"),LDHUD::ResourceP2(Momentum,-1,100,false).bKnown);
    TestEqualSensitive(TEXT("P2 encodings are hundredths of a point"),LDHUD::ResourceP2(Health,59950,60000,true).Text,Health+TEXT(" 600/600"));
    const LDHUD::FLevelView Mid=LDHUD::Level(3,10,200,180,true,300);
    TestTrue(TEXT("Experience shows progress inside the current level"),Mid.Level==TEXT("等级 3")&&Mid.Experience==Experience+TEXT(" 20/120")&&FMath::IsNearlyEqual(Mid.Fraction,1.f/6.f,.0001f)&&!Mid.bMax);
    const LDHUD::FLevelView Cap=LDHUD::Level(10,10,1500,1440,false,0);
    TestTrue(TEXT("The level cap shows MAX instead of an eleventh threshold"),Cap.bMax&&Cap.Level==TEXT("等级 10")&&Cap.Experience==Experience+TEXT(" MAX"));
    TestEqualSensitive(TEXT("Experience below the level threshold is a data error, not progress"),LDHUD::Level(3,10,100,180,true,300).Experience,Experience+TEXT("数据异常"));
    TestEqualSensitive(TEXT("An invalid level is not shown as level one"),LDHUD::Level(0,10,0,0,true,80).Level,FString(TEXT("等级 —")));
    TestEqualSensitive(TEXT("Zero remainder reads zero"),LDHUD::TenthsUp(0),FString(TEXT("0.0s")));
    TestEqualSensitive(TEXT("Any live remainder reads at least a tenth"),LDHUD::TenthsUp(1),FString(TEXT("0.1s")));
    TestEqualSensitive(TEXT("Remainders round up to the next tenth"),LDHUD::TenthsUp(101),FString(TEXT("0.2s")));
    TestTrue(TEXT("Development names the closed capture point"),LDHUD::PhaseLine(ELDMatchPhase::Development).Contains(LDTerms::CapturePoint));
    TestEqualSensitive(TEXT("#198: a rest has its own phase line"),LDHUD::PhaseLine(ELDMatchPhase::Rest),FString(LDTerms::RestPhase));
    TestTrue(TEXT("An authority fault is stated explicitly"),LDHUD::PhaseLine(ELDMatchPhase::Faulted).Contains(TEXT("故障")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDActionViewTest,"Lockdown.Rules.HUDActionView",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDActionViewTest::RunTest(const FString& Parameters)
{
    LDHUD::FActionContext Context;Context.KeyLabel=TEXT("Q");Context.bOwnContext=true;Context.bPlaying=true;Context.bAlive=true;Context.bClock=true;Context.NowMs=4800;Context.bMomentumKnown=true;Context.MomentumP2=10000;
    LDHUD::FActionInput Bolt;Bolt.Slot=LDHUD::EActionSlot::Q;Bolt.bProjected=true;Bolt.bHasSource=true;Bolt.Name=TEXT("技能甲");Bolt.MomentumCostP2=5000;
    LDHUD::FActionView View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("A ready learned active can be sent and shows its name without a skill level"),View.bCanSubmitIntent&&View.ExecutionBlocks==0&&View.State==TEXT("就绪")&&View.Name==TEXT("技能甲")&&View.Kind==TEXT("角色主动"));
    Bolt.ReadyAtMs=8000;Bolt.CooldownMs=8000;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("Cooldown is an execution limit with its own track remainder"),(View.ExecutionBlocks&LDHUD::ActionCooldown)&&View.State==TEXT("冷却 3.2s")&&View.CooldownRemainingMs==3200&&FMath::IsNearlyEqual(View.CooldownFraction,.4f,.0001f));
    TestTrue(TEXT("Cooldown does not suppress the explicit intent"),View.bCanSubmitIntent);
    Context.MomentumP2=4000;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("Several limits are all reported while the state shows the first"),(View.ExecutionBlocks&LDHUD::ActionCooldown)&&(View.ExecutionBlocks&LDHUD::ActionMomentum)&&View.State.StartsWith(TEXT("冷却")));
    Bolt.ReadyAtMs=0;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("Short momentum greys and explains without blocking the intent"),View.State==LDTerms::MomentumShort()&&View.bCanSubmitIntent);
    Context.MomentumP2=10000;
    LDHUD::FActionInput Empty;Empty.Slot=LDHUD::EActionSlot::W;Empty.bProjected=true;View=LDHUD::Action(Empty,Context);
    TestTrue(TEXT("An empty learned slot is an explicit EmptySource attempt, still sent"),View.Name==TEXT("空槽")&&(View.ExecutionBlocks&LDHUD::ActionEmpty)&&View.bCanSubmitIntent);
    Empty.Slot=LDHUD::EActionSlot::Dash;View=LDHUD::Action(Empty,Context);
    TestTrue(TEXT("An unbound dash says it is unavailable and is still sent"),View.Name==FString(LDTerms::Dash)+TEXT("不可用")&&View.Kind==TEXT("通用冲刺")
        &&View.State==View.Name&&(View.ExecutionBlocks&LDHUD::ActionEmpty)&&View.bCanSubmitIntent);
    Bolt.bControlled=true;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("A control limit is explained"),(View.ExecutionBlocks&LDHUD::ActionControlled)&&View.State==TEXT("受控制无法使用"));
    Bolt.bControlled=false;Bolt.ServerBlock=TEXT("目标不可用");View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("A published server reason is shown as display only"),(View.ExecutionBlocks&LDHUD::ActionServer)&&View.State==TEXT("目标不可用")&&View.bCanSubmitIntent);
    Bolt.ServerBlock.Reset();Bolt.ReadyAtMs=8000;Context.bClock=false;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("Without the authority clock a cooldown is syncing, never ready"),(View.ExecutionBlocks&LDHUD::ActionUnsynced)&&View.State==TEXT("冷却时间同步中"));
    Context.bClock=true;Bolt.ReadyAtMs=0;
    LDHUD::FActionInput Unprojected=Bolt;Unprojected.bProjected=false;View=LDHUD::Action(Unprojected,Context);
    TestTrue(TEXT("Without the owner combat projection nothing is guessed or sent"),!View.bCanSubmitIntent&&View.State==TEXT("未同步"));
    Context.bAlive=false;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("A dead owner cannot send combat intents"),!View.bCanSubmitIntent&&View.State==LDTerms::Dead);
    Context.bAlive=true;Context.bInputCaptured=true;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("A higher UI layer owning input suppresses sending, not the readiness display"),!View.bCanSubmitIntent&&View.State==TEXT("就绪"));
    Context.bInputCaptured=false;Context.bOwnContext=false;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("A stale match or life context never sends"),!View.bCanSubmitIntent&&(View.ExecutionBlocks&LDHUD::ActionUnsynced));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDMatchTimerTest,"Lockdown.Rules.HUDMatchTimer",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDMatchTimerTest::RunTest(const FString& Parameters)
{
    FLDPublicMatchView Public;Public.Generation=4;Public.Phase=ELDMatchPhase::Rest;Public.StartedAtMs=50000;
    TestEqualSensitive(TEXT("A clock behind the match start reads 0:00"),LDHUD::MatchTimer(Public,true,46000),FString(TEXT("0:00")));
    TestEqualSensitive(TEXT("The match start (the opening rest) reads 0:00"),LDHUD::MatchTimer(Public,true,50000),FString(TEXT("0:00")));
    Public.Phase=ELDMatchPhase::Development;
    TestEqualSensitive(TEXT("Elapsed seconds round down"),LDHUD::MatchTimer(Public,true,50999),FString(TEXT("0:00")));
    TestEqualSensitive(TEXT("Minutes carry no leading zero"),LDHUD::MatchTimer(Public,true,62000),FString(TEXT("0:12")));
    TestEqualSensitive(TEXT("Seconds keep two digits"),LDHUD::MatchTimer(Public,true,115000),FString(TEXT("1:05")));
    TestEqualSensitive(TEXT("Ten minutes and more show their own digits"),LDHUD::MatchTimer(Public,true,50000+725000),FString(TEXT("12:05")));
    TestTrue(TEXT("Without the authority clock no time is guessed"),LDHUD::MatchTimer(Public,false,62000).IsEmpty());
    Public.StartedAtMs=0;
    TestTrue(TEXT("Without a published match start no time is shown"),LDHUD::MatchTimer(Public,true,62000).IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDRestTimelineTest,"Lockdown.Rules.HUDRestTimeline",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDRestTimelineTest::RunTest(const FString& Parameters)
{
    // #198: the timeline banner and the rest return countdown come from the public timeline only.
    const FGuid Self=FGuid::NewGuid();
    FLDPublicMatchView Public;Public.Generation=4;Public.Phase=ELDMatchPhase::Rest;
    FLDTimelineView& Timeline=Public.Timeline;
    Timeline.Generation=4;Timeline.Stage=ELDMatchStage::Rest;Timeline.RestIndex=1;Timeline.FieldSegment=1;Timeline.Step=ELDRestStep::Build;
    Timeline.StepStartedAtMs=100000;Timeline.StepEndsAtMs=125000;
    LDHUD::FTimelineView View=LDHUD::TimelineBanner(Public,Self,true,105000);
    TestEqualSensitive(TEXT("A rest names its index, step and whole seconds left"),View.Headline,
        LDTerms::RestLabel(1)+TEXT(" · ")+LDTerms::RestBuild+TEXT(" · 剩 20 秒"));
    TestFalse(TEXT("A PK rest's build step carries the sign-up line"),View.Detail.IsEmpty());
    View=LDHUD::TimelineBanner(Public,Self,false,0);
    TestTrue(TEXT("Without the authority clock nothing is guessed"),View.Headline.EndsWith(TEXT("时间同步中")));
    TestTrue(TEXT("No return countdown outside the return step"),LDHUD::RestReturnCountdown(Public,true,110000).IsEmpty());
    Timeline.Step=ELDRestStep::Return;Timeline.StepStartedAtMs=130000;Timeline.StepEndsAtMs=135000;
    View=LDHUD::TimelineBanner(Public,Self,true,131200);
    TestTrue(TEXT("The return step counts down"),View.Headline==LDTerms::RestLabel(1)+TEXT(" · ")+LDTerms::RestReturn+TEXT(" · 剩 4 秒"));
    TestEqualSensitive(TEXT("The return countdown shows whole seconds rounded up"),LDHUD::RestReturnCountdown(Public,true,131200),FString(TEXT("4")));
    TestEqualSensitive(TEXT("The last millisecond still reads 1"),LDHUD::RestReturnCountdown(Public,true,134999),FString(TEXT("1")));
    TestEqualSensitive(TEXT("Reaching the end holds 1 until the authority enters the field"),LDHUD::RestReturnCountdown(Public,true,136000),FString(TEXT("1")));
    TestTrue(TEXT("Without the authority clock no countdown is guessed"),LDHUD::RestReturnCountdown(Public,false,131200).IsEmpty());
    // The field: T runs from the segment start; "开淫！" greets each field segment.
    Public.Phase=ELDMatchPhase::Development;
    Timeline.Stage=ELDMatchStage::Field;Timeline.Step=ELDRestStep::None;Timeline.FieldBaseMs=180000;Timeline.FieldStartedAtMs=200000;Timeline.FieldEndT=360000;
    Timeline.StepStartedAtMs=200000;Timeline.StepEndsAtMs=380000;
    View=LDHUD::TimelineBanner(Public,Self,true,200500);
    TestEqualSensitive(TEXT("A field segment counts down to the next rest in field time"),View.Headline,LDTerms::FieldSegmentLine(2,3,TEXT("3:00")));
    TestTrue(TEXT("The field has no rest detail"),View.Detail.IsEmpty());
    TestEqualSensitive(TEXT("A field segment starts with the go line"),LDHUD::RestReturnCountdown(Public,true,200500),FString(LDTerms::CountdownGo));
    TestTrue(TEXT("The go line leaves after its moment"),LDHUD::RestReturnCountdown(Public,true,200000+LDHUD::CountdownGoShowMs).IsEmpty());
    Public.Phase=ELDMatchPhase::CaptureOpen;Timeline.Stage=ELDMatchStage::FinalField;Timeline.FieldSegment=3;Timeline.FieldBaseMs=540000;Timeline.FieldEndT=-1;
    TestEqualSensitive(TEXT("The final field names the open flagpole"),LDHUD::TimelineBanner(Public,Self,true,250000).Headline,FString(LDTerms::FinalField));
    Public.Showdown.bStarted=true;Public.Showdown.EndsAtMs=440000;Public.Showdown.ClosureAtMs=320000;Public.Showdown.RespawnsCloseAtMs=350000;
    View=LDHUD::TimelineBanner(Public,Self,true,310000);
    TestTrue(TEXT("The final field visibly warns before closure"),View.Detail==TEXT("封街倒计时 10 秒")
        &&LDHUD::TimelineDetailVisible(Public,View,false));
    View=LDHUD::TimelineBanner(Public,Self,true,320000);
    TestTrue(TEXT("The final field visibly reports active closure"),View.Detail.Contains(TEXT("封街收缩中"))
        &&LDHUD::TimelineDetailVisible(Public,View,false));
    View=LDHUD::TimelineBanner(Public,Self,true,350000);
    TestTrue(TEXT("The final field visibly reports the respawn cutoff"),View.Detail.Contains(TEXT("不再接受新复活"))
        &&LDHUD::TimelineDetailVisible(Public,View,false));
    View=LDHUD::TimelineBanner(Public,Self,true,430000);
    TestTrue(TEXT("The final field visibly warns of the last ten seconds"),View.Detail.Contains(TEXT("最后十秒"))
        &&LDHUD::TimelineDetailVisible(Public,View,false));
    View=LDHUD::TimelineBanner(Public,Self,false,0);
    TestFalse(TEXT("Without an authority clock no countdown detail is shown"),LDHUD::TimelineDetailVisible(Public,View,false));
    View=LDHUD::TimelineBanner(Public,Self,true,310000);
    TestFalse(TEXT("A covering rest service suppresses the detail"),LDHUD::TimelineDetailVisible(Public,View,true));
    Timeline.Stage=ELDMatchStage::Field;
    TestFalse(TEXT("Ordinary field phases keep timeline detail hidden"),LDHUD::TimelineDetailVisible(Public,View,false));
    Timeline.Stage=ELDMatchStage::FinalField;
    Timeline.Generation=3;
    TestTrue(TEXT("Another generation's timeline shows nothing"),LDHUD::TimelineBanner(Public,Self,true,250000).Headline.IsEmpty()
        &&LDHUD::RestReturnCountdown(Public,true,200500).IsEmpty());
    TestEqualSensitive(TEXT("Ready confirmation is short"),
        LDHUD::RestReadyText(ELDRestReadyResult::Accepted),FString(TEXT("已准备。")));
    for(const ELDRestReadyResult Refused:{ELDRestReadyResult::NotContender,ELDRestReadyResult::StaleMatch,
        ELDRestReadyResult::StaleWindow,ELDRestReadyResult::NotOpen,ELDRestReadyResult::None})
    {
        TestTrue(FString::Printf(TEXT("Ready refusal %d is explicit"),int32(Refused)),
            LDHUD::RestReadyText(Refused).StartsWith(TEXT("准备失败")));
    }
    // The personal training reset: a queued reset says what it does; every refusal is explicit, never a success.
    TestEqualSensitive(TEXT("A queued reset refills both and readies the cooldowns"),LDHUD::TrainingResetText(ELDTrainingResetResult::Queued),
        FString(TEXT("训练已重置：你和假人回满，冷却就绪")));
    TestEqualSensitive(TEXT("No dummy says the rest spot is taken"),LDHUD::TrainingResetText(ELDTrainingResetResult::NoDummy),
        FString(TEXT("没有"))+LDTerms::TrainingDummy+TEXT("（休息位被占）"));
    for(const ELDTrainingResetResult Refused:{ELDTrainingResetResult::NotInRest,ELDTrainingResetResult::NotContender,ELDTrainingResetResult::StaleMatch,ELDTrainingResetResult::None})
    {
        const FString Text=LDHUD::TrainingResetText(Refused);
        TestTrue(FString::Printf(TEXT("Training reset refusal %d is explicit"),int32(Refused)),!Text.IsEmpty()&&!Text.Contains(TEXT("已重置")));
    }
    // An unlisted contender-kind unit is the own training dummy only in a rest; a listed one keeps its roster name.
    FLDLobbyView Lobby;FLDLobbyMember& Member=Lobby.Members.AddDefaulted_GetRef();Member.ContenderId=Self;Member.Name=TEXT("玩家甲");
    TestEqualSensitive(TEXT("A listed contender keeps its roster name in a rest"),LDHUD::ObservedContenderName(Lobby,Self,true),FString(TEXT("玩家甲")));
    TestEqualSensitive(TEXT("An unlisted contender in a rest is the training dummy"),LDHUD::ObservedContenderName(Lobby,FGuid::NewGuid(),true),FString(LDTerms::TrainingDummy));
    TestTrue(TEXT("Outside a rest an unlisted contender is not given a name"),LDHUD::ObservedContenderName(Lobby,FGuid::NewGuid(),false).IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDPacingViewTest,"Lockdown.Rules.HUDPacingView",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDPacingViewTest::RunTest(const FString& Parameters)
{
    // P6: the death screen's spawn choice comes from the own owner projection only; every hotspot and
    // affix has a glyph, a name and a hover line.
    FLDOwnerState Own;Own.Life=5;Own.LifePhase=ELDLifePhase::Dead;
    Own.Pacing.SpawnLife=5;Own.Pacing.SpawnCandidates={TEXT("spawn_0"),TEXT("spawn_3")};
    Own.Pacing.SpawnCandidateCm={FVector(0,0,0),FVector(100,0,0)};Own.Pacing.SpawnChoiceEndsAtMs=14000;
    LDHUD::FSpawnChoiceView Choice=LDHUD::SpawnChoice(Own,true,10200);
    TestTrue(TEXT("A dead life with candidates sees its choice and whole seconds left"),Choice.bVisible&&Choice.Count==2
        &&Choice.Chosen==INDEX_NONE&&Choice.Countdown==TEXT("4"));
    Own.Pacing.SpawnChosen=1;
    TestEqual(TEXT("The chosen candidate is lit"),LDHUD::SpawnChoice(Own,true,10200).Chosen,1);
    TestFalse(TEXT("After its window the choice is gone"),LDHUD::SpawnChoice(Own,true,14001).bVisible);
    TestTrue(TEXT("Without the authority clock the choice shows without a guessed countdown"),
        LDHUD::SpawnChoice(Own,false,0).bVisible&&LDHUD::SpawnChoice(Own,false,0).Countdown.IsEmpty());
    Own.Pacing.SpawnLife=4;
    TestFalse(TEXT("Another life's candidates never show"),LDHUD::SpawnChoice(Own,true,10200).bVisible);
    Own.Pacing.SpawnLife=5;Own.LifePhase=ELDLifePhase::Alive;
    TestFalse(TEXT("A living body has no spawn choice"),LDHUD::SpawnChoice(Own,true,10200).bVisible);
    TestTrue(TEXT("Every refused choice says why in one line"),!LDHUD::SpawnChoiceText(ELDSpawnChoiceResult::NoChoice).IsEmpty()
        &&LDHUD::SpawnChoiceText(ELDSpawnChoiceResult::OutOfRange)!=LDHUD::SpawnChoiceText(ELDSpawnChoiceResult::NoChoice));

    const FLDDemoContentParameters Params;
    TSet<FString> Glyphs;
    for(int32 Id=1;Id<=LDPacing::DailyModifiers;++Id)
    {
        const ELDDailyModifier Modifier=ELDDailyModifier(Id);
        TestTrue(FString::Printf(TEXT("Hotspot %d has a name, a glyph and a hover line"),Id),
            FString(LDTerms::DailyName(Modifier)).Len()>0&&FString(LDTerms::DailyGlyph(Modifier)).Len()>0
            &&!LDHUD::DailyDescription(Modifier,Params).IsEmpty());
        Glyphs.Add(LDTerms::DailyGlyph(Modifier));
    }
    TestEqual(TEXT("Every hotspot glyph is distinct"),Glyphs.Num(),LDPacing::DailyModifiers);
    for(int32 Id=1;Id<=LDPacing::CampAffixes;++Id)
    {
        const ELDCampAffix Affix=ELDCampAffix(Id);
        TestTrue(FString::Printf(TEXT("Affix %d has a name, a glyph and a hover line"),Id),
            FString(LDTerms::AffixName(Affix)).Len()>0&&FString(LDTerms::AffixGlyph(Affix)).Len()>0
            &&!LDHUD::AffixDescription(Affix,Params).IsEmpty());
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDCircleColumnsTest,"Lockdown.Rules.HUDCircleColumns",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDCircleColumnsTest::RunTest(const FString& Parameters)
{
    // Mock 1's top-centre panel: Head over the bar, the graffiti owner under it on the left and the guard state on the right.
    FLDPublicMatchView Public;Public.Generation=4;Public.Phase=ELDMatchPhase::Development;Public.OpenAtMs=MAX_int64;
    const LDHUD::FCircleView Closed=LDHUD::Circle(Public,FString(),FString(),true,0);
    TestTrue(TEXT("Outside the open flagpole the whole line is the head"),Closed.Head==Closed.Line&&Closed.Owner.IsEmpty()&&Closed.Aside.IsEmpty());
    Public.Phase=ELDMatchPhase::CaptureOpen;Public.CircleState=ELDCircleState::Solo;Public.SoloSinceMs=100000;Public.DueAtMs=103500;
    Public.GraffitiOwner=FGuid::NewGuid();Public.GuardKeptMs=5000;
    const LDHUD::FCircleView Repaint=LDHUD::Circle(Public,TEXT("玩家甲"),TEXT("玩家乙"),true,101400,3500,20000);
    TestTrue(TEXT("A spray over a graffiti heads with the spray, the owner left and the paused guard right"),
        Repaint.Head==FString(TEXT("玩家甲 "))+LDTerms::Holding+TEXT(" · ")+LDTerms::HoldLeft+TEXT(" 2.1s")
        &&Repaint.Owner==LDTerms::GraffitiOf(TEXT("玩家乙"))
        &&Repaint.Aside==FString(LDTerms::GuardPaused)+TEXT("（")+LDTerms::GuardLeft+TEXT(" 15.0s）"));
    Public.CircleState=ELDCircleState::Guarded;Public.Occupier.Invalidate();Public.SoloSinceMs=Public.DueAtMs=Public.GuardKeptMs=0;Public.GuardDueAtMs=120000;
    const LDHUD::FCircleView Guard=LDHUD::Circle(Public,FString(),TEXT("玩家乙"),true,105000,3500,20000);
    TestTrue(TEXT("A running guard heads with its countdown and names the owner below, nothing on the right"),
        Guard.Head==FString(LDTerms::GuardLeft)+TEXT(" 15.0s")&&Guard.Owner==LDTerms::GraffitiOf(TEXT("玩家乙"))&&Guard.Aside.IsEmpty());
    Public.GraffitiOwner.Invalidate();Public.CircleState=ELDCircleState::Empty;
    const LDHUD::FCircleView Empty=LDHUD::Circle(Public,FString(),FString(),true,0);
    TestTrue(TEXT("An empty zone without a graffiti is the head alone"),Empty.Head==FString(LDTerms::CircleEmpty)&&Empty.Owner.IsEmpty()&&Empty.Aside.IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDPublicMatchTest,"Lockdown.Rules.HUDPublicMatch",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDPublicMatchTest::RunTest(const FString& Parameters)
{
    // #198: the flagpole opens when the final field begins; until then its opening instant is unscheduled (MAX_int64).
    FLDPublicMatchView Public;Public.Generation=4;Public.Phase=ELDMatchPhase::Development;Public.OpenAtMs=MAX_int64;
    const FString Closed=FString(LDTerms::Development)+TEXT(" · ")+LDTerms::CaptureAfterLastRest;
    TestEqualSensitive(TEXT("Development says the flagpole opens after the last rest and prints no countdown"),LDHUD::Circle(Public,FString(),FString(),true,500).Line,Closed);
    TestEqualSensitive(TEXT("A running countdown never reads 0:00 before the opening"),LDHUD::Clock(1),FString(TEXT("0:01")));
    TestEqualSensitive(TEXT("Without the authority clock the line is the same"),LDHUD::Circle(Public,FString(),FString(),false,0).Line,Closed);
    TestTrue(TEXT("The closed line names the last rest R3"),Closed.Contains(TEXT("R3"))&&Closed.Contains(LDTerms::CapturePoint));
    Public.Phase=ELDMatchPhase::CaptureOpen;Public.CircleState=ELDCircleState::Empty;
    TestEqualSensitive(TEXT("An open empty zone names no one"),LDHUD::Circle(Public,FString(),FString(),true,0).Line,FString(LDTerms::CircleEmpty));
    Public.CircleState=ELDCircleState::Contested;
    const LDHUD::FCircleView Contested=LDHUD::Circle(Public,TEXT("玩家甲"),FString(),true,0);
    TestTrue(TEXT("A contest without a holder names no one and shows no progress"),Contested.Line==FString(LDTerms::CircleContested)+TEXT(" · ")+LDTerms::NoProgressKept&&!Contested.bProgress);
    {
        FLDPublicMatchView Paused=Public;Paused.Occupier=FGuid::NewGuid();Paused.PausedProgressMs=1201;
        const FString Contest=FString(LDTerms::CircleContested)+TEXT(" · ")+LDTerms::ProgressKept(TEXT("玩家甲"));
        const LDHUD::FCircleView Held=LDHUD::Circle(Paused,TEXT("玩家甲"),FString(),true,0,3500,20000);
        TestTrue(TEXT("A paused sprayer's progress shows against the approved spray in tenths"),
            Held.Line==Contest+TEXT(" 1.2/3.5 s")&&Held.bProgress&&FMath::IsNearlyEqual(Held.Progress,1201.f/3500.f));
        TestTrue(TEXT("Without the approved spray the paused seconds still show, without a bar"),
            LDHUD::Circle(Paused,TEXT("玩家甲"),FString(),true,0).Line==Contest+TEXT("（1.2 s）")&&!LDHUD::Circle(Paused,TEXT("玩家甲"),FString(),true,0).bProgress);
    }
    Public.CircleState=ELDCircleState::Solo;Public.SoloSinceMs=100000;Public.DueAtMs=103500;
    const LDHUD::FCircleView Solo=LDHUD::Circle(Public,TEXT("玩家甲"),FString(),true,101400);
    TestTrue(TEXT("Spray progress runs from the public spray start to its due time"),Solo.bProgress&&FMath::IsNearlyEqual(Solo.Progress,.4f,.0001f)
        &&Solo.Line==FString(TEXT("玩家甲 "))+LDTerms::Holding+TEXT(" · ")+LDTerms::HoldLeft+TEXT(" 2.1s"));
    const LDHUD::FCircleView Due=LDHUD::Circle(Public,TEXT("玩家甲"),FString(),true,104000);
    TestTrue(TEXT("Reaching the due time waits for the authority and never claims a graffiti or a winner"),Due.Progress==1.f&&Due.Line.EndsWith(TEXT("0.0s"))&&!Due.Line.Contains(LDTerms::Winner));
    TestEqualSensitive(TEXT("An unlisted sprayer is not given an invented name"),LDHUD::Circle(Public,FString(),FString(),false,0).Line,FString(TEXT("未列名参赛者 "))+LDTerms::Holding+TEXT(" · 进度同步中"));
    {
        // The graffiti owner and his guard are public in every open state.
        FLDPublicMatchView Owned=Public;Owned.GraffitiOwner=FGuid::NewGuid();Owned.GuardKeptMs=5000;
        const LDHUD::FCircleView Repaint=LDHUD::Circle(Owned,TEXT("玩家甲"),TEXT("玩家乙"),true,101400,3500,20000);
        TestTrue(TEXT("A sole sprayer's bar shows his spray; the line names the paused guard of the graffiti owner"),
            Repaint.bProgress&&FMath::IsNearlyEqual(Repaint.Progress,.4f,.0001f)
            &&Repaint.Line==FString(TEXT("玩家甲 "))+LDTerms::Holding+TEXT(" · ")+LDTerms::HoldLeft+TEXT(" 2.1s · ")+LDTerms::GraffitiOf(TEXT("玩家乙"))
                +TEXT(" · ")+LDTerms::GuardPaused+TEXT("（")+LDTerms::GuardLeft+TEXT(" 15.0s）"));
        FLDPublicMatchView Guarded=Owned;Guarded.CircleState=ELDCircleState::Guarded;Guarded.Occupier.Invalidate();
        Guarded.SoloSinceMs=Guarded.DueAtMs=Guarded.GuardKeptMs=0;Guarded.GuardDueAtMs=120000;
        const LDHUD::FCircleView Guard=LDHUD::Circle(Guarded,FString(),TEXT("玩家乙"),true,105000,3500,20000);
        TestTrue(TEXT("A running guard counts down to its public win instant"),Guard.bProgress&&FMath::IsNearlyEqual(Guard.Progress,.25f,.0001f)
            &&Guard.Line==LDTerms::GraffitiOf(TEXT("玩家乙"))+TEXT(" · ")+LDTerms::GuardLeft+TEXT(" 15.0s"));
        Guarded.CircleState=ELDCircleState::Empty;Guarded.GuardDueAtMs=0;Guarded.GuardKeptMs=5000;
        const LDHUD::FCircleView Away=LDHUD::Circle(Guarded,FString(),TEXT("玩家乙"),true,105000,3500,20000);
        TestTrue(TEXT("2026-11-07: an owner away from the empty zone pauses his guard at its kept value"),
            Away.Line==LDTerms::GraffitiOf(TEXT("玩家乙"))+TEXT(" · ")+LDTerms::GuardAway+TEXT("（")+LDTerms::GuardLeft+TEXT(" 15.0s）")
            &&Away.bProgress&&FMath::IsNearlyEqual(Away.Progress,.25f,.0001f));
        Guarded.CircleState=ELDCircleState::Contested;
        const LDHUD::FCircleView Frozen=LDHUD::Circle(Guarded,FString(),TEXT("玩家乙"),true,105000,3500,20000);
        TestTrue(TEXT("A contest beside the owner names the frozen guard"),Frozen.Line==FString(LDTerms::CircleContested)+TEXT(" · ")+LDTerms::NoProgressKept
            +TEXT(" · ")+LDTerms::GraffitiOf(TEXT("玩家乙"))+TEXT(" · ")+LDTerms::GuardPaused+TEXT("（")+LDTerms::GuardLeft+TEXT(" 15.0s）")
            &&Frozen.bProgress&&FMath::IsNearlyEqual(Frozen.Progress,.25f,.0001f));
        Guarded.CircleState=ELDCircleState::Guarded;Guarded.GuardDueAtMs=120000;Guarded.GuardKeptMs=0;
        TestFalse(TEXT("A running guard never claims a winner"),LDHUD::Circle(Guarded,FString(),TEXT("玩家乙"),true,125000,3500,20000).Line.Contains(LDTerms::Winner));
    }
    FLDKillNotice Notice;Notice.Cause=ELDKillCause::Contender;
    TestEqualSensitive(TEXT("A contender kill names both public roster names"),LDHUD::KillLine(Notice,TEXT("乙"),TEXT("甲"),LDTerms::CampLow,LDTerms::CampHigh,LDTerms::CampElite,LDTerms::Boss),LDTerms::KillLine(TEXT("甲"),TEXT("乙")));
    Notice.Cause=ELDKillCause::CampHigh;
    TestEqualSensitive(TEXT("A camp death names the tier creature, not a hidden attacker"),LDHUD::KillLine(Notice,TEXT("乙"),TEXT("甲"),LDTerms::CampLow,LDTerms::CampHigh,LDTerms::CampElite,LDTerms::Boss),LDTerms::KilledByLine(TEXT("乙"),LDTerms::CampHigh));
    Notice.Cause=ELDKillCause::Unowned;
    TestEqualSensitive(TEXT("A death without a contender owner says so"),LDHUD::KillLine(Notice,TEXT("乙"),FString(),LDTerms::CampLow,LDTerms::CampHigh,LDTerms::CampElite,LDTerms::Boss),FString(TEXT("乙 "))+LDTerms::Dead+TEXT("（无参赛者击杀）"));
    Notice.Cause=ELDKillCause::Boss;
    TestEqualSensitive(TEXT("A Boss death names the 画饼老板"),LDHUD::KillLine(Notice,TEXT("乙"),TEXT("甲"),LDTerms::CampLow,LDTerms::CampHigh,LDTerms::CampElite,LDTerms::Boss),
        LDTerms::KilledByLine(TEXT("乙"),LDTerms::Boss));
    FLDResultView Result;Result.Reason=ELDMatchEndReason::GuardSucceeded;Result.Winner=FGuid::NewGuid();
    TestEqualSensitive(TEXT("A guard win names its end reason and crowns the one winner"),LDHUD::ResultHeadline(Result,TEXT("甲")),FString(TEXT("守护成功 · "))+LDTerms::Crowned(TEXT("甲")));
    TestTrue(TEXT("The crowning names the winner and the winner title"),LDTerms::Crowned(TEXT("甲")).StartsWith(TEXT("甲 "))&&LDTerms::Crowned(TEXT("甲")).Contains(LDTerms::Winner));
    Result.Winner.Invalidate();
    TestEqualSensitive(TEXT("A capture result without a winner says so"),LDHUD::ResultHeadline(Result,FString()),FString(LDTerms::NoWinner));
    Result.Reason=ELDMatchEndReason::HostEnded;
    TestEqualSensitive(TEXT("A Host-ended match has no winner"),LDHUD::ResultHeadline(Result,FString()),FString(TEXT("房主结束了本局 · "))+LDTerms::NoCapture);
    FLDResultRow Row;Row.ContenderId=FGuid::NewGuid();Row.Name=TEXT("机器人");Row.Kind=ELDContenderKind::OpponentAI;Row.bLeft=true;Row.Kills=3;Row.Deaths=1;Row.FinalLevel=7;
    const LDHUD::FResultRowView Shown=LDHUD::ResultRow(Row,Row.ContenderId,FGuid());
    TestTrue(TEXT("A frozen row keeps the departed AI and its statistics"),Shown.Name==TEXT("机器人（AI）（已离开）")&&Shown.Kills==TEXT("3")&&Shown.Deaths==TEXT("1")&&Shown.Level==TEXT("7")&&Shown.bWinner);
    TestFalse(TEXT("Statistics never mark a winner on their own"),LDHUD::ResultRow(Row,FGuid(),FGuid()).bWinner);
    const FGuid Me=FGuid::NewGuid();
    // D26: the event feed names the Boss's last hit.
    FLDBossView Dean;
    Dean.Phase=ELDBossPhase::Down;Dean.KilledBy=Me;Dean.DownAtMs=90000;
    TestEqualSensitive(TEXT("The last hit is told of its build reward"),LDHUD::BossDownLine(Dean,Me,FString(),LDTerms::Boss),
        LDTerms::BossDownSelf(LDTerms::Boss));
    TestTrue(TEXT("No 吃到大饼 buff is promised any more"),LDHUD::BossDownLine(Dean,Me,FString(),LDTerms::Boss).Contains(LDTerms::BossReward)
        &&!LDHUD::BossDownLine(Dean,Me,FString(),LDTerms::Boss).Contains(TEXT("吃到大饼")));
    TestEqualSensitive(TEXT("Others see the public roster name"),LDHUD::BossDownLine(Dean,FGuid::NewGuid(),TEXT("甲"),LDTerms::Boss),
        LDTerms::BossDownBy(TEXT("甲"),LDTerms::Boss));
    // D27: the 上热搜 line names the leader (#144: no bounty); the leader is told directly.
    FLDLeaderView Named;
    TestTrue(TEXT("Nobody named shows no leader line"),LDHUD::LeaderLine(Named.Leader,Me,FString()).IsEmpty());
    Named.Leader=FGuid::NewGuid();Named.RefreshedAtMs=20000;
    TestEqualSensitive(TEXT("Another contender's notice names the public roster name"),LDHUD::LeaderLine(Named.Leader,Me,TEXT("甲")),
        LDTerms::LeaderNamed(TEXT("甲")));
    TestTrue(TEXT("The notice is called 上热搜 and carries no bounty"),LDHUD::LeaderLine(Named.Leader,Me,TEXT("甲")).StartsWith(LDTerms::Leader)
        &&!LDHUD::LeaderLine(Named.Leader,Me,TEXT("甲")).Contains(TEXT("赏金")));
    TestEqualSensitive(TEXT("An unlisted leader is not given an invented name"),LDHUD::LeaderLine(Named.Leader,Me,FString()),LDTerms::LeaderNamed(TEXT("未列名参赛者")));
    Named.Leader=Me;
    TestEqualSensitive(TEXT("The leader is told its place is public"),LDHUD::LeaderLine(Named.Leader,Me,TEXT("我")),LDTerms::LeaderSelf());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDCardTextTest,"Lockdown.Rules.HUDCardText",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDCardTextTest::RunTest(const FString& Parameters)
{
    // #147/#198 card templates: every number reads a typed field of the active's single value set grown for the owner's level.
    FLDDemoEffectDefinition Root;Root.Opcode=uint8(ELDDemoEffectOp::Root);Root.DurationMs=1000;
    FLDDemoEffectDefinition Damage;Damage.Opcode=uint8(ELDDemoEffectOp::Damage);Damage.BaseP2=6000;Damage.ADRatioBp=5000;
    FLDDemoEffectDefinition Slow;Slow.Opcode=uint8(ELDDemoEffectOp::Slow);Slow.RatioBp=2500;Slow.DurationMs=1500;
    FLDDemoEffectDefinition Timed;Timed.Opcode=uint8(ELDDemoEffectOp::TimedStatBonus);Timed.DurationMs=3000;
    Timed.Stats={{ELDDemoBonusStat::ArmorFlat,1200,{}},{ELDDemoBonusStat::AttackSpeedBonusRatio,5000,{}}};
    FLDDemoAbilityValues Values;Values.MomentumCostP2=5000;Values.CooldownMs=10000;Values.Effects={Root,Damage,Slow,Timed};
    LDHUD::FCardStats L1;L1.ADP2=7000;
    FLDDemoSkillRankParameters Ranks;Ranks.RankBaseScaleBp={10000,15000,20000};Ranks.RankRatioScaleBp={10000,12500,15000};
    const LDHUD::FCardText Face=LDHUD::CardText(TEXT("定身目标 [root.duration] 秒，造成 [damage.amount] 物理伤害。"),Values,L1,Ranks,1000);
    TestEqualSensitive(TEXT("Level 1 numbers: base plus the coefficient times the owner's AD"),Face.Plain,FString(TEXT("定身目标 1 秒，造成 95 物理伤害。")));
    TestEqualSensitive(TEXT("Keywords are bold"),Face.Rich,FString(TEXT("<b>定身</>目标 1 秒，造成 95 物理伤害。")));
    TestTrue(TEXT("The keywords used are listed in glossary order"),Face.bComplete&&Face.Keywords==TArray<FString>{TEXT("定身")});
    TestTrue(TEXT("and the card lists each placeholder's value"),Face.Values==TArray<FString>({TEXT("1"),TEXT("95")}));
    LDHUD::FCardStats L5=L1;L5.Level=5;
    TestEqualSensitive(TEXT("Level 5 grows the base (60 x 1.4 + 35) but not the duration"),
        LDHUD::CardText(TEXT("定身目标 [root.duration] 秒，造成 [damage.amount] 物理伤害。"),Values,L5,Ranks,1000).Plain,FString(TEXT("定身目标 1 秒，造成 119 物理伤害。")));
    LDHUD::FCardStats BlueprintStats=L5;BlueprintStats.bShowScalingFormula=true;
    const LDHUD::FCardText BlueprintFace=LDHUD::CardText(
        TEXT("定身目标 [root.duration] 秒，造成 [damage.amount] 物理伤害。"),Values,BlueprintStats,Ranks,1000);
    TestTrue(TEXT("A blueprint renders fixed values and attribute scaling without assuming the recorded player's attributes"),
        BlueprintFace.bComplete&&BlueprintFace.Plain==TEXT("定身目标 1 秒，造成 84 + 50%攻击力 物理伤害。"));
    FLDDemoEffectDefinition Shield;Shield.Opcode=uint8(ELDDemoEffectOp::Shield);Shield.BaseP2=2500;
    Shield.ArmorRatioBp=3500;Shield.SecondaryRatioBp=2000;
    FLDDemoAbilityValues ShieldValues;ShieldValues.Effects.Add(Shield);
    BlueprintStats.Level=1;
    TestEqualSensitive(TEXT("A blueprint burst keeps the armor coefficient and multiplier"),
        LDHUD::CardText(TEXT("造成 [shield.burst] 伤害。"),ShieldValues,BlueprintStats,Ranks,1000).Plain,
        FString(TEXT("造成 (25 + 35%护甲) × 20% 伤害。")));
    LDHUD::FCardStats L12=L1;L12.Level=12;
    TestEqualSensitive(TEXT("One growth step through every level (60 x 2.1 + 35)"),
        LDHUD::CardText(TEXT("造成 [damage.amount] 物理伤害。"),Values,L12,Ranks,1000).Plain,FString(TEXT("造成 161 物理伤害。")));
    LDHUD::FCardStats R3=L12;R3.ADP2=8000;
    TestEqualSensitive(TEXT("Rank 3 doubles the grown base and raises the coefficient 1.5x (60 x 2.1 x 2 + 75% x 80)"),
        LDHUD::CardText(TEXT("造成 [damage.amount] 物理伤害。"),Values,R3,Ranks,1000,3).Plain,FString(TEXT("造成 312 物理伤害。")));
    TestEqualSensitive(TEXT("Timed stats: points grow, ratios do not"),
        LDHUD::CardText(TEXT("护甲 +[timed_stat_bonus.ArmorFlat]，攻速 +[timed_stat_bonus.AttackSpeedBonusRatio]%。"),Values,L5,Ranks,1000).Plain,
        FString(TEXT("护甲 +17，攻速 +50%。")));
    TestFalse(TEXT("An active's template never reads a per-point bonus"),
        LDHUD::CardText(TEXT("每点 +[per_point.ADFlat]。"),Values,L1,Ranks,1000).bComplete);
    // #198 stat rows: [per_point.<Stat>] reads the per-point bonus exactly as authored, decimals kept and never grown.
    FLDDemoStatDefinition Brains;Brains.Id=TEXT("hud_rules_fixture_brains");
    FLDDemoStatBonus AP;AP.Stat=ELDDemoBonusStat::APFlat;AP.Amount=500;
    FLDDemoStatBonus Haste;Haste.Stat=ELDDemoBonusStat::CooldownHasteFlat;Haste.Amount=100;
    FLDDemoStatBonus Regen;Regen.Stat=ELDDemoBonusStat::MomentumRegenFlat;Regen.Cm.Numerator=6;Regen.Cm.Denominator=100;
    FLDDemoStatBonus Crit;Crit.Stat=ELDDemoBonusStat::CritChanceBonusRatio;Crit.Amount=40;
    Brains.PerPoint={AP,Haste,Regen,Crit};
    const FString StatTemplate=TEXT("每点法术强度 +[per_point.APFlat]、技能急速 +[per_point.CooldownHasteFlat]、交战时劲回复 +[per_point.MomentumRegenFlat]/秒、暴击率 +[per_point.CritChanceBonusRatio]%。");
    const LDHUD::FCardText StatFace=LDHUD::StatCardText(StatTemplate,Brains);
    TestEqualSensitive(TEXT("A stat row shows its per-point bonuses; momentum regeneration reads its Cm fraction"),StatFace.Plain,
        FString(TEXT("每点法术强度 +5、技能急速 +1、交战时劲回复 +0.06/秒、暴击率 +0.4%。")));
    TestTrue(TEXT("Every per-point placeholder reads a number"),StatFace.bComplete&&StatFace.Values.Num()==4);
    TestFalse(TEXT("A stat's template never reads an active's effect"),LDHUD::StatCardText(TEXT("造成 [damage.amount] 伤害。"),Brains).bComplete);
    TestFalse(TEXT("A per-point bonus the stat lacks is never shown as a number"),LDHUD::StatCardText(TEXT("+[per_point.ArmorFlat]"),Brains).bComplete);
    const LDHUD::FCardText Both=LDHUD::CardText(TEXT("被减速或被控制的敌人。"),Values,L1,Ranks,1000);
    TestTrue(TEXT("Slow and control are separate keywords"),Both.Keywords==TArray<FString>({TEXT("减速"),TEXT("控制")})
        &&Both.Rich==TEXT("被<b>减速</>或被<b>控制</>的敌人。"));
    const LDHUD::FCardText Absent=LDHUD::CardText(TEXT("造成 [stun.duration] 秒。"),Values,L1,Ranks,1000);
    TestTrue(TEXT("A placeholder of an absent effect is never shown as a number"),!Absent.bComplete&&Absent.Plain.Contains(TEXT("?")));
    TestFalse(TEXT("A broken template is refused"),LDHUD::CardText(TEXT("造成 [damage] 伤害。"),Values,L1,Ranks,1000).bComplete);
    TArray<LDDemoCardText::FToken> Tokens;FString Error;
    TestFalse(TEXT("An unknown op is refused"),LDDemoCardText::Parse(TEXT("[teleport.amount]"),Tokens,Error));
    TestFalse(TEXT("A stray bracket is refused"),LDDemoCardText::Parse(TEXT("造成 ] 伤害"),Tokens,Error));
    FLDBlueprintRecord BlueprintRecord;
    BlueprintRecord.FinalBuild.Cards.Add({TEXT("card_dash_punch"),TEXT("dash"),FString(),FString()});
    BlueprintRecord.FinalBuild.Cards.Add({TEXT("card_live_wire"),TEXT("body"),FString(),TEXT("fire")});
    const FString BlueprintHistory=ULDBlueprintPanel::Describe(BlueprintRecord,nullptr);
    TestTrue(TEXT("Blueprint card places read as columns and elements, never raw target keys"),
        BlueprintHistory.Contains(TEXT("card_dash_punch → 冲刺（D）"))
        &&BlueprintHistory.Contains(FString(TEXT("card_live_wire → "))+LDTerms::ColumnBody+TEXT(" · ")+LDHUD::ElementName(ELDDemoElement::Fire))
        &&!BlueprintHistory.Contains(TEXT("body ·")));
    TestTrue(TEXT("Keyword lines explain each keyword in bold"),LDHUD::KeywordLines({TEXT("定身")}).Num()==1
        &&LDHUD::KeywordLines({TEXT("定身")})[0].StartsWith(TEXT("<b>定身</>：")));
    for(const TCHAR* Reaction:{LDTerms::ReactionFog,LDTerms::ReactionConduct,LDTerms::ReactionOverload})
    {
        const LDHUD::FCardText Text=LDHUD::CardText(Reaction,{},L1,Ranks,1000);
        const TArray<FString> Lines=LDHUD::KeywordLines(Text.Keywords);
        TestTrue(FString(Reaction)+TEXT(" in a card brings its explanation"),Lines.Num()==1&&Lines[0].Contains(LDTerms::ReactionExplanation(Reaction)));
    }
    for(const LDTerms::FCardKeyword& Keyword:LDTerms::CardKeywords)
    {
        const FString Explain=Keyword.Explain;
        TestFalse(FString(Keyword.Name)+TEXT(" never mentions spray or guard"),Explain.Contains(TEXT("喷漆"))||Explain.Contains(TEXT("守护")));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDContentParametersTest,"Lockdown.Rules.HUDContentParameters",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDContentParametersTest::RunTest(const FString& Parameters)
{
    FLDDemoStatBonus Stat;Stat.Stat=ELDDemoBonusStat::ADFlat;Stat.Amount=2000;
    TestEqualSensitive(TEXT("Flat stats are P2 points"),LDHUD::StatLine(Stat),FString(LDTerms::AttackDamage)+TEXT(" +20"));
    Stat.Stat=ELDDemoBonusStat::AttackSpeedBonusRatio;Stat.Amount=1000;
    TestEqualSensitive(TEXT("Ratio stats are basis points"),LDHUD::StatLine(Stat),FString(LDTerms::AttackSpeed)+TEXT(" +10%"));
    Stat.Stat=ELDDemoBonusStat::AttackRangeCmFlat;Stat.Amount=0;Stat.Cm.Numerator=40;Stat.Cm.Denominator=1;
    TestEqualSensitive(TEXT("Range bonuses are centimetres"),LDHUD::StatLine(Stat),FString(TEXT("攻击距离 +40 厘米")));
    FLDDemoEffectDefinition Damage;Damage.Opcode=uint8(ELDDemoEffectOp::Damage);Damage.BaseP2=10000;Damage.ADRatioBp=8000;Damage.APRatioBp=6500;Damage.DamageType=ELDDemoDamageType::Magical;
    TestEqualSensitive(TEXT("Magic damage uses the approved term and its AD and AP scaling"),LDHUD::EffectLine(Damage),FString::Printf(TEXT("%s 100 + 80%%%s + 65%%%s"),LDTerms::MagicalDamage,LDTerms::AttackDamage,LDTerms::AbilityPower));
    FLDDemoEffectDefinition Root;Root.Opcode=uint8(ELDDemoEffectOp::Root);Root.DurationMs=1300;
    TestEqualSensitive(TEXT("Control duration is shown in seconds"),LDHUD::EffectLine(Root),FString(LDTerms::Root)+TEXT(" 1.3s"));
    FLDDemoEffectDefinition Unknown;Unknown.Opcode=200;
    TestEqualSensitive(TEXT("An unknown effect is reported, never described"),LDHUD::EffectLine(Unknown),FString(TEXT("未知效果 #200")));
    FLDDemoAbilityValues Cost;Cost.MomentumCostP2=4500;Cost.CooldownMs=6400;Cost.Effects.Add(Damage);
    TestEqualSensitive(TEXT("An active's cost line carries momentum and cooldown"),LDHUD::CostLine(Cost),FString(LDTerms::Momentum)+TEXT(" 45 · 冷却 6.4s"));
    FLDDemoContentDefinitions Content;FLDDemoAbilityDefinition Bolt;Bolt.Id=TEXT("hud_rules_fixture_bolt");
    Bolt.Target.Mode=ELDDemoTargetMode::DirectionProjectile;Bolt.Target.RangeCm.Numerator=650;Bolt.Target.RangeCm.Denominator=1;
    Bolt.Values=Cost;Content.Abilities.Add(Bolt);
    Content.Parameters.SkillGrowthPerLevelBp=1000;
    Content.Parameters.SkillRanks.RankBaseScaleBp={10000,15000,20000};Content.Parameters.SkillRanks.RankRatioScaleBp={10000,12500,15000};
    LDHUD::FCardStats Stats;
    AddExpectedMessage(TEXT("LD_HUD_DISPLAY_TEXT_MISSING hud_rules_fixture_bolt"),ELogVerbosity::Error,EAutomationExpectedMessageFlags::Contains,0,false);
    const LDHUD::FActiveCardView Missing=LDHUD::ActiveCard(TEXT("hud_rules_fixture_bolt"),&Content,Stats);
    TestTrue(TEXT("An active without its display row says so instead of inventing a face"),
        !Missing.bKnown&&Missing.SentencePlain==TEXT("内容目录中找不到该技能"));
    AddExpectedMessage(TEXT("LD_HUD_DISPLAY_TEXT_MISSING hud_rules_fixture_unknown"),ELogVerbosity::Error,EAutomationExpectedMessageFlags::Contains,0,false);
    TestTrue(TEXT("An active the catalog lacks says so too"),!LDHUD::ActiveCard(TEXT("hud_rules_fixture_unknown"),&Content,Stats).bKnown);
    FLDDemoDisplayDefinition BoltDisplay;BoltDisplay.Id=TEXT("hud_rules_fixture_bolt");BoltDisplay.IconId=TEXT("icon_hud_rules_fixture_bolt");
    BoltDisplay.NameText=TEXT("测试飞弹");BoltDisplay.DescriptionText=TEXT("命中造成 [damage.amount] 魔法伤害并减速。");
    Content.Display.Add(BoltDisplay);
    const LDHUD::FActiveCardView Card=LDHUD::ActiveCard(TEXT("hud_rules_fixture_bolt"),&Content,Stats);
    TestTrue(TEXT("The card face is the sentence with this level's numbers and its keywords"),Card.bKnown
        &&Card.SentencePlain==TEXT("命中造成 100 魔法伤害并减速。")&&Card.Sentence==TEXT("命中造成 100 魔法伤害并<b>减速</>。")
        &&Card.Keywords.Num()==1&&Card.Keywords[0].StartsWith(TEXT("<b>减速</>：")));
    TestTrue(TEXT("The card shows the target form and the cost"),Card.Target==TEXT("方向弹道 · 射程 650 厘米")&&Card.Cost==LDHUD::CostLine(Cost));
    TestTrue(TEXT("An active card draws its display icon, with the name's first character only as the missing-icon stand-in"),
        Card.Glyph==Card.Name.Left(1)&&Card.IconId==TEXT("icon_hud_rules_fixture_bolt"));
    LDHUD::FCardStats L4;L4.Level=4;
    TestTrue(TEXT("A higher owner level grows the number on the same card (one skill level, #198)"),
        LDHUD::ActiveCard(TEXT("hud_rules_fixture_bolt"),&Content,L4).SentencePlain==TEXT("命中造成 130 魔法伤害并减速。"));
    // A learned branch reads its own values at its rank; a placeholder whose effect the branch replaced reads level 1, and
    // the chosen step follows as its own line.
    FLDDemoAbilityDefinition Trip;Trip.Id=TEXT("hud_rules_fixture_trip");Trip.Target=Bolt.Target;
    FLDDemoEffectDefinition Hold;Hold.Opcode=uint8(ELDDemoEffectOp::Root);Hold.DurationMs=1000;
    FLDDemoEffectDefinition Stun;Stun.Opcode=uint8(ELDDemoEffectOp::Stun);Stun.DurationMs=800;Stun.ExtraDurationMs=500;
    Trip.Values=Cost;Trip.Values.Effects.Add(Hold);
    FLDDemoAbilityBranch TripA;TripA.Path=ELDSkillBranch::A;TripA.Target=Bolt.Target;TripA.Values=Cost;TripA.Values.Effects.Add(Stun);
    Trip.Branches.Add(TripA);Content.Abilities.Add(Trip);
    FLDDemoDisplayDefinition TripDisplay;TripDisplay.Id=Trip.Id;TripDisplay.NameText=TEXT("测试绊");
    TripDisplay.DescriptionText=TEXT("造成 [damage.amount] 伤害，定身 [root.duration] 秒。");
    FLDDemoDisplayDefinition TripStep;TripStep.Id=TEXT("hud_rules_fixture_trip_2a");TripStep.NameText=TEXT("改眩晕");
    TripStep.DescriptionText=TEXT("改为眩晕 [stun.duration] 秒，撞墙再晕 [stun.extra_duration] 秒。");
    Content.Display.Add(TripDisplay);Content.Display.Add(TripStep);
    AddExpectedMessage(TEXT("LD_HUD_DISPLAY_TEXT_MISSING hud_rules_fixture_trip"),ELogVerbosity::Error,EAutomationExpectedMessageFlags::Contains,0,false);
    const LDHUD::FActiveCardView Ranked=LDHUD::ActiveCard(TEXT("hud_rules_fixture_trip"),&Content,Stats,ELDSkillBranch::A);
    TestTrue(TEXT("A branch that replaces an effect still reads whole, at its rank, with its step line"),Ranked.bKnown
        &&Ranked.SentencePlain==TEXT("造成 150 伤害，定身 1 秒。\n改眩晕：改为眩晕 0.8 秒，撞墙再晕 0.5 秒。")&&Ranked.Cost==LDHUD::CostLine(TripA.Values));
    TestEqualSensitive(TEXT("The level-1 face of the same skill is unchanged"),
        LDHUD::ActiveCard(TEXT("hud_rules_fixture_trip"),&Content,Stats).SentencePlain,FString(TEXT("造成 100 伤害，定身 1 秒。")));
    // #198 stat rows: name, per-point text and the allocated points; a stat without its display row says so.
    FLDDemoStatDefinition Skin;Skin.Id=TEXT("hud_rules_fixture_skin");
    FLDDemoStatBonus Armor;Armor.Stat=ELDDemoBonusStat::ArmorFlat;Armor.Amount=250;
    FLDDemoStatBonus Resist;Resist.Stat=ELDDemoBonusStat::MagicResistFlat;Resist.Amount=250;
    Skin.PerPoint={Armor,Resist};
    AddExpectedMessage(TEXT("LD_HUD_DISPLAY_TEXT_MISSING hud_rules_fixture_skin"),ELogVerbosity::Error,EAutomationExpectedMessageFlags::Contains,0,false);
    const LDHUD::FStatRowView NoRow=LDHUD::StatRow(Skin,2,&Content);
    TestTrue(TEXT("A stat without its display row says so"),!NoRow.bKnown&&NoRow.Points==2&&NoRow.PerPoint==TEXT("内容目录中找不到该属性"));
    FLDDemoDisplayDefinition SkinDisplay;SkinDisplay.Id=Skin.Id;SkinDisplay.IconId=TEXT("icon_hud_rules_fixture_skin");
    SkinDisplay.DescriptionText=TEXT("每点护甲和魔抗各 +[per_point.ArmorFlat]。");
    Content.Display.Add(SkinDisplay);
    const LDHUD::FStatRowView SkinRow=LDHUD::StatRow(Skin,3,&Content);
    TestTrue(TEXT("A stat row shows its per-point text, its icon and the allocated points"),SkinRow.bKnown&&SkinRow.Points==3
        &&SkinRow.PerPoint==TEXT("每点护甲和魔抗各 +2.5。")&&SkinRow.IconId==TEXT("icon_hud_rules_fixture_skin")&&SkinRow.StatId==FName(*Skin.Id));
    {
        // The stat icon's popup: each raised attribute's current value and the gain of one point; a missing row reads "?".
        FLDCombatAttributeRowView ArmorRow;ArmorRow.Attribute=uint8(ELDProjectedAttribute::Armor);ArmorRow.Final.Numerator=105;ArmorRow.Final.Denominator=2;
        FLDDemoStatDefinition Strong;Strong.Id=TEXT("hud_rules_fixture_strong");
        FLDDemoStatBonus Crit;Crit.Stat=ELDDemoBonusStat::CritChanceBonusRatio;Crit.Amount=40;
        FLDDemoStatBonus Regen;Regen.Stat=ELDDemoBonusStat::HPRegenFlat;Regen.Cm.Numerator=2;Regen.Cm.Denominator=25;
        Strong.PerPoint={Armor,Crit,Regen};
        const TArray<LDHUD::FStatEffectView> Effects=LDHUD::StatEffects(Strong,{ArmorRow});
        TestTrue(TEXT("A stat's popup lists each raised attribute with its current value and per-point gain"),Effects.Num()==3
            &&Effects[0].Attribute==LDTerms::Armor&&Effects[0].Current==TEXT("52.5")&&Effects[0].PerPoint==TEXT("+2.5")
            &&Effects[1].Attribute==LDTerms::CritChance()&&Effects[1].Current==TEXT("?")&&Effects[1].PerPoint==TEXT("+0.4%")
            &&Effects[2].Attribute==LDTerms::HealthRegen()&&Effects[2].PerPoint==TEXT("+0.08/s"));
    }
    FLDCombatAttributeRowView Crit;Crit.Attribute=uint8(ELDProjectedAttribute::CritChance);Crit.Final.Numerator=1;Crit.Final.Denominator=5;Crit.Base.Denominator=1;
    Crit.Level.Denominator=1;Crit.Choices.Numerator=1;Crit.Choices.Denominator=50;
    const LDHUD::FAttributeRowView Row=LDHUD::AttributeRow(Crit);
    TestTrue(TEXT("Attribute rows use the approved name and show ratios as percent"),Row.Name==LDTerms::CritChance()&&Row.Final==TEXT("20%"));
    TestTrue(TEXT("The breakdown names the allocated points' term, never a card or an opening"),Row.Breakdown.Contains(TEXT("属性点 2%"))
        &&!Row.Breakdown.Contains(TEXT("卡"))&&!Row.Breakdown.Contains(TEXT("开局")));
    TestEqualSensitive(TEXT("A dead owner's growth refusal names the state, not a trade"),LDHUD::GrowthReasonText(ELDGrowthReason::Dead),FString(LDTerms::Dead)+TEXT("，暂时不能操作"));
    TestEqualSensitive(TEXT("Combat rejections are explained"),LDHUD::CombatRejectText(ELDCombatReject::OutOfRange),FString(TEXT("超出射程")));
    FLDCombatEntryView Entry;Entry.DefinitionId=NAME_None;Entry.Unavailable=ELDCombatReject::SourceUnavailable;
    const LDHUD::FActionInput Empty=LDHUD::ActionInput(Entry,LDHUD::EActionSlot::Q,&Content);
    TestTrue(TEXT("An empty combat entry is an explicit empty source with the server reason kept for display"),Empty.bProjected&&!Empty.bHasSource&&Empty.ServerBlock==TEXT("没有可用来源"));
    TestTrue(TEXT("An empty slot has no icon"),Empty.IconId.IsEmpty()&&LDHUD::Action(Empty,LDHUD::FActionContext()).IconId.IsEmpty());
    FLDCombatEntryView BoltEntry;BoltEntry.DefinitionId=TEXT("hud_rules_fixture_bolt");
    const LDHUD::FActionInput BoltInput=LDHUD::ActionInput(BoltEntry,LDHUD::EActionSlot::Q,&Content);
    TestTrue(TEXT("A bound slot draws its definition's display icon"),BoltInput.IconId==TEXT("icon_hud_rules_fixture_bolt")
        &&LDHUD::Action(BoltInput,LDHUD::FActionContext()).IconId==TEXT("icon_hud_rules_fixture_bolt"));
    FLDCombatEntryView DashEntry;DashEntry.DefinitionId=TEXT("dash");
    TestEqualSensitive(TEXT("The dash has its own icon without a display row"),LDHUD::ActionInput(DashEntry,LDHUD::EActionSlot::Dash,&Content).IconId,
        FString(LDUIIcons::Dash));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDStatWheelTest,"Lockdown.Rules.HUDStatWheel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDStatWheelTest::RunTest(const FString& Parameters)
{
    // The wheel over a stat: up adds a point, down takes one back (rest only). It targets the row or cell under the wheel event
    // itself, on the workshop's character rows and on the band's cells, and a collapsed workshop panel takes nothing.
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if(!LDTestApprovedFixture::Read(*this,Catalog)){return false;}
    const FLDDemoContentDefinitions& Content=Catalog->Get();
    const FLDAttributeProjectionResult Base=LDAttributeProjection::Project(FLDAttributeBuildValues(1,{}),*Catalog);
    if(!TestTrue(TEXT("The bare level-1 build projects"),Base.IsSuccess())){return false;}
    LDHUD::FCardStats L1;L1.ADP2=Base.Value->Final.ADP2;L1.APP2=Base.Value->Final.APP2;L1.MaxHPP2=Base.Value->Final.MaxHPP2;L1.ArmorP2=Base.Value->Final.ArmorP2;
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("The wheel fixture has a world"),World)){return false;}
    ON_SCOPE_EXIT{World->DestroyWorld(false);};
    ULDWorkshop* Workshop=NewObject<ULDWorkshop>(World);Workshop->Initialize();
    ULDStatBar* Bar=NewObject<ULDStatBar>(World);Bar->Initialize();
    UWidgetTree* Tree=NewObject<UWidgetTree>();
    auto* Root=Tree->ConstructWidget<UOverlay>();Tree->RootWidget=Root;
    for(UWidget* Child:{static_cast<UWidget*>(Bar),static_cast<UWidget*>(Workshop)})
    {auto* Slot=Root->AddChildToOverlay(Child);Slot->SetHorizontalAlignment(HAlign_Fill);Slot->SetVerticalAlignment(VAlign_Fill);}
    const TSharedRef<SWidget> Slate=Root->TakeWidget();
    FLDProgressionView View;View.bValid=true;View.Generation=1;View.Life=1;View.Level=4;View.BuildRevision=3;View.UnspentPoints=3;
    for(const FLDDemoStatDefinition* Stat:Content.StatsInDesignOrder()){View.StatPoints.Add({FName(*Stat->Id),2});}
    TArray<FLDBuildCommand> Sent;
    Bar->SubmitBuild=[&Sent](const FLDBuildCommand& Command,ELDGrowthReason& Reason){Sent.Add(Command);Reason=ELDGrowthReason::InvalidRequest;return uint64(0);};
    Bar->PeekRequestId=[](){return uint64(1);};
    Workshop->StatBar=Bar;
    ULDWorkshop::FInput Input;Input.Mode=ULDWorkshop::EMode::Full;Input.Progression=&View;Input.Match=1;Input.Content=&Content;
    Input.bRest=true;Input.Step=ELDRestStep::Build;Input.Stats=L1;
    // Laid out far off any desktop, so the real cursor is never over a cell and only the wheel events and the pin aim.
    const FVector2D Origin(-1000000.,-1000000.);
    const FGeometry Geometry=FGeometry::MakeRoot(FVector2D(1920,1080),FSlateLayoutTransform(1.f,Origin));
    const auto Frame=[&]()
    {
        Bar->Present(&View,1,1,LDProgression::EMatchStage::Rest,ELDLifePhase::Alive,&Content,{},true);
        Workshop->Present(Input);
        Slate->SlatePrepass(1.f);
        FHittestGrid HitTest;FSlateWindowElementList Draw{TSharedPtr<SWindow>()};
        Slate->Paint(FPaintArgs(nullptr,HitTest,FVector2D::ZeroVector,1.,1.f/60.f),Geometry,FSlateRect(Origin.X,Origin.Y,Origin.X+1920,Origin.Y+1080),Draw,0,FWidgetStyle(),true);
        for(UUserWidget* Widget:{static_cast<UUserWidget*>(Workshop),static_cast<UUserWidget*>(Bar)})
        {Widget->TakeWidget()->Tick(Widget->GetCachedGeometry(),0.,1.f/60.f);}
    };
    const auto CentreOf=[](const UWidget* Widget){const FGeometry& G=Widget->GetCachedGeometry();return G.GetAbsolutePosition()+G.GetAbsoluteSize()*.5f;};
    const auto Wheel=[&](UUserWidget* Target,FVector2D At,float Delta)
    {
        const FPointerEvent Event(0,0,At,At,TSet<FKey>(),EKeys::MouseWheelAxis,Delta,FModifierKeysState());
        Target->TakeWidget()->OnMouseWheel(Geometry,Event);
    };
    const auto Only=[](const FLDBuildCommand& Command,ELDBuildCommandKind Kind,int32 Index)
    {
        if(Command.Kind!=Kind){return false;}
        for(int32 Stat=0;Stat<Command.PointCounts.Num();++Stat){if(Command.PointCounts[Stat]!=(Stat==Index?1:0)){return false;}}
        return Command.PointCounts.IsValidIndex(Index);
    };
    for(int32 Tick=0;Tick<10;++Tick){Frame();}
    if(!TestTrue(TEXT("The workshop shows its stat rows and the band its cells"),Workshop->StatPlus(1)&&Bar->CellBox(0)&&View.StatPoints.Num()>=3)){return false;}
    // Workshop rows: up over row 1 allocates stat 1, down over row 2 refunds stat 2; no hover tick in between.
    const FVector2D Row1=CentreOf(Workshop->StatPlus(1)),Row2=CentreOf(Workshop->StatPlus(2));
    Wheel(Workshop,Row1,1.f);
    TestTrue(TEXT("Wheel up over a workshop row allocates that stat"),Sent.Num()==1&&Only(Sent.Last(),ELDBuildCommandKind::AllocatePoints,1));
    Wheel(Workshop,Row2,-1.f);
    TestTrue(TEXT("Wheel down over a workshop row refunds that stat"),Sent.Num()==2&&Only(Sent.Last(),ELDBuildCommandKind::Respec,2));
    // A collapsed character panel (the field workshop) keeps its rows' old geometry but takes no wheel.
    Input.Mode=ULDWorkshop::EMode::Field;
    for(int32 Tick=0;Tick<5;++Tick){Frame();}
    Wheel(Workshop,Row1,1.f);Wheel(Workshop,Row2,-1.f);
    TestEqual(TEXT("A collapsed workshop panel sends nothing"),Sent.Num(),2);
    // The band: the cell under the wheel event, not the last tick's hover.
    Input.Mode=ULDWorkshop::EMode::Closed;
    for(int32 Tick=0;Tick<5;++Tick){Frame();}
    Bar->PinHover(2);Frame();
    TestEqual(TEXT("The pinned cell is the band's current hover"),Bar->HoverIndex(),2);
    Wheel(Bar,CentreOf(Bar->CellBox(0)),1.f);
    TestTrue(TEXT("Wheel up over a band cell allocates that cell's stat, not the pinned hover"),
        Sent.Num()==3&&Only(Sent.Last(),ELDBuildCommandKind::AllocatePoints,0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDCardFacesTest,"Lockdown.Rules.HUDCardFaces",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDCardFacesTest::RunTest(const FString& Parameters)
{
    // #198 rendered faces over the approved catalog: every active and component card at levels 1 and 10 with the bare build's
    // stats, and every stat row (as many as the catalog has), names its entry, reads every placeholder as a number, has an
    // icon and never shows spray/guard wording,
    // an unknown effect or a missing-content line.
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if(!LDTestApprovedFixture::Read(*this,Catalog)){return false;}
    const FLDDemoContentDefinitions& Content=Catalog->Get();
    FName Table;
    if(!HudRegisterApprovedNames(*this,Content,Table)){return false;}
    ON_SCOPE_EXIT{if(!Table.IsNone()){FStringTableRegistry::Get().UnregisterStringTable(Table);}};
    const FLDAttributeProjectionResult Base=LDAttributeProjection::Project(FLDAttributeBuildValues(1,{}),*Catalog);
    if(!TestTrue(TEXT("The bare level-1 build projects"),Base.IsSuccess())){return false;}
    LDHUD::FCardStats L1;L1.ADP2=Base.Value->Final.ADP2;L1.APP2=Base.Value->Final.APP2;L1.MaxHPP2=Base.Value->Final.MaxHPP2;L1.ArmorP2=Base.Value->Final.ArmorP2;
    LDHUD::FCardStats L10=L1;L10.Level=10;
    LDHUD::FCardStats Blueprint=L10;Blueprint.bShowScalingFormula=true;
    const auto Bad=[](const FString& Text){return Text.IsEmpty()||Text.Contains(TEXT("喷漆"))||Text.Contains(TEXT("守护"))||Text.Contains(TEXT("未知"))
        ||Text.Contains(TEXT("?"))||Text.Contains(TEXT("内容目录中找不到"))||Text.Contains(TEXT("同步中"));};
    TestTrue(TEXT("The approved catalog has actives, stats and cards"),
        !Content.Abilities.IsEmpty()&&!Content.Stats.IsEmpty()&&!Content.Cards.IsEmpty());
    int32 ActiveFaces=0,StatFaces=0,CardFaces=0;
    for(const FLDDemoAbilityDefinition& Ability:Content.Abilities)
    {
        ++ActiveFaces;
        for(const LDHUD::FCardStats& Stats:{L1,L10})
        {
            const FString At=FString::Printf(TEXT("%s L%d"),*Ability.Id,int32(Stats.Level));
            const LDHUD::FActiveCardView Card=LDHUD::ActiveCard(FName(*Ability.Id),&Content,Stats);
            TestTrue(At+TEXT(": the card reads every placeholder as a number"),Card.bKnown);
            TestFalse(At+TEXT(": name ")+Card.Name,Bad(Card.Name)||Card.Name==Ability.Id);
            TestFalse(At+TEXT(": sentence ")+Card.SentencePlain,Bad(Card.SentencePlain));
            TestFalse(At+TEXT(": target ")+Card.Target,Bad(Card.Target));
            TestFalse(At+TEXT(": cost ")+Card.Cost,Bad(Card.Cost));
            for(const FString& Line:Card.Keywords){TestFalse(At+TEXT(": keyword ")+Line,Bad(Line));}
            TestFalse(At+TEXT(": the card has an icon"),Card.IconId.IsEmpty());
        }
        const LDHUD::FActiveCardView BlueprintCard=LDHUD::ActiveCard(FName(*Ability.Id),&Content,Blueprint);
        TestTrue(Ability.Id+TEXT(": the blueprint resolves every active placeholder"),
            BlueprintCard.bKnown&&!BlueprintCard.SentencePlain.Contains(TEXT("["))&&!BlueprintCard.SentencePlain.Contains(TEXT("]")));
    }
    for(const FLDDemoStatDefinition& Stat:Content.Stats)
    {
        ++StatFaces;
        const LDHUD::FStatRowView Row=LDHUD::StatRow(Stat,2,&Content);
        TestTrue(Stat.Id+TEXT(": the row reads every per-point placeholder as a number"),Row.bKnown&&Row.Points==2);
        TestFalse(Stat.Id+TEXT(": name ")+Row.Name,Bad(Row.Name)||Row.Name==Stat.Id);
        TestFalse(Stat.Id+TEXT(": per point ")+Row.PerPoint,Bad(Row.PerPoint));
        TestFalse(Stat.Id+TEXT(": the row has an icon"),Row.IconId.IsEmpty());
    }
    for(const FLDDemoCardDefinition& Row:Content.Cards)
    {
        ++CardFaces;
        for(const LDHUD::FCardStats& Stats:{L1,L10})
        {
            const FString At=FString::Printf(TEXT("%s L%d"),*Row.Id,int32(Stats.Level));
            const LDHUD::FCardFaceView Card=LDHUD::CardFace(FName(*Row.Id),&Content,Stats);
            TestTrue(At+TEXT(": the card reads every placeholder as a number"),Card.bKnown);
            TestFalse(At+TEXT(": name ")+Card.Name,Bad(Card.Name)||Card.Name==Row.Id);
            TestFalse(At+TEXT(": sentence ")+Card.SentencePlain,Bad(Card.SentencePlain));
            for(const FString& Line:Card.Keywords){TestFalse(At+TEXT(": keyword ")+Line,Bad(Line));}
            TestFalse(At+TEXT(": the card has an icon"),Card.IconId.IsEmpty());
        }
        const LDHUD::FCardFaceView BlueprintCard=LDHUD::CardFace(FName(*Row.Id),&Content,Blueprint);
        TestTrue(Row.Id+TEXT(": the blueprint resolves every card placeholder"),
            BlueprintCard.bKnown&&!BlueprintCard.SentencePlain.Contains(TEXT("["))&&!BlueprintCard.SentencePlain.Contains(TEXT("]")));
    }
    TestTrue(TEXT("Every active, stat and card of the approved catalog has its face checked"),
        ActiveFaces==Content.Abilities.Num()&&StatFaces==Content.Stats.Num()&&CardFaces==Content.Cards.Num());
    if(!FSlateApplication::IsInitialized()||Content.Abilities.IsEmpty())
    {AddError(TEXT("Summary faces require Slate and an approved active."));return false;}
    FLDResultBuild Summary;Summary.bAvailable=true;Summary.CatalogRoot=Content.Identity.Root.ToHex();
    Summary.Actives.Add({FName(*Content.Abilities[0].Id),0});
    for(const FLDDemoCardDefinition& Row:Content.Cards)
    {
        if(Row.Form==ELDCardForm::Body&&Summary.Cards.Num()<7)
        {Summary.Cards.Add({FName(*Row.Id),ELDResultBuildTarget::Body,NAME_None,ELDDemoElement::None});}
    }
    if(!TestEqual(TEXT("The summary fixture has seven approved body cards"),Summary.Cards.Num(),7)){return false;}
    if(!Content.FinalMods.IsEmpty()){Summary.FinalMod=FName(*Content.FinalMods[0].Id);}
    for(const FVector2D Size:{FVector2D(520,90),FVector2D(218,66),FVector2D(760,184)})
    {
        UWidgetTree* Tree=NewObject<UWidgetTree>();
        UWidget* Board=LDResultBuild::Make(Tree,Summary,&Content,Size.X,Size.Y,1.25f);
        Tree->RootWidget=Board;
        TArray<UWidget*> Widgets;Tree->GetAllWidgets(Widgets);
        TArray<FVector2D> PaintedSizes;
        int32 Textures=0;
        for(UWidget* Widget:Widgets)
        {
            if(auto* Face=Cast<ULDPaintLayer>(Widget))
            {
                auto Original=MoveTemp(Face->Painter);
                Face->Painter=[&PaintedSizes,Original=MoveTemp(Original)](const LDUIPaint::FContext& Paint)
                {PaintedSizes.Add(FVector2D(Paint.Geometry.GetLocalSize()));Original(Paint);};
            }
            if(const auto* Image=Cast<UImage>(Widget)){Textures+=Cast<UTexture2D>(Image->GetBrush().GetResourceObject())!=nullptr;}
        }
        const TSharedRef<SWidget> Slate=Board->TakeWidget();Slate->SlatePrepass(1.f);
        FHittestGrid HitTest;FSlateWindowElementList Draw{TSharedPtr<SWindow>()};
        Slate->Paint(FPaintArgs(nullptr,HitTest,FVector2D::ZeroVector,0.,0.f),FGeometry::MakeRoot(Size,FSlateLayoutTransform()),
            FSlateRect(0,0,Size.X,Size.Y),Draw,0,FWidgetStyle(),true);
        for(UWidget* Widget:Widgets){if(auto* Face=Cast<ULDPaintLayer>(Widget)){Face->Painter=nullptr;}}
        const FString At=FString::Printf(TEXT("Frozen summary %.0fx%.0f"),Size.X,Size.Y);
        TestTrue(At+TEXT(": approved active, dash and card textures are retained"),Textures>=3);
        TestTrue(At+TEXT(": skill and card faces are painted"),PaintedSizes.Num()>=5);
        for(const FVector2D Painted:PaintedSizes)
        {TestTrue(At+TEXT(": every painted icon receives usable geometry ")+Painted.ToString(),Painted.X>=4.&&Painted.Y>=4.);}
        if(Size.Y>=180)
        {
            for(const FVector2D Painted:PaintedSizes)
            {TestTrue(At+TEXT(": result icons remain at least 36 physical pixels at 720p"),FMath::Min(Painted.X,Painted.Y)*2./3.>=36.);}
            int32 Overflow=0;
            for(UWidget* Widget:Widgets)
            {
                if(const auto* Text=Cast<UTextBlock>(Widget))
                {Overflow+=Text->GetText().ToString()==FString::Printf(TEXT("+%d"),Summary.Cards.Num()-3);}
            }
            TestEqual(At+TEXT(": a dense column exposes its omitted card count"),Overflow,1);
        }
    }
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("The dock layout fixture has a world"),World)){return false;}
    ON_SCOPE_EXIT{World->DestroyWorld(false);};
    ULDWorkshop* Workshop=NewObject<ULDWorkshop>(World);Workshop->Initialize();
    ULDChoicesPanel* Choices=NewObject<ULDChoicesPanel>(World);Choices->Initialize();
    UWidgetTree* LayoutTree=NewObject<UWidgetTree>();
    auto* Root=LayoutTree->ConstructWidget<UOverlay>();LayoutTree->RootWidget=Root;
    for(UWidget* Child:{static_cast<UWidget*>(Workshop),static_cast<UWidget*>(Choices)})
    {
        auto* Slot=Root->AddChildToOverlay(Child);Slot->SetHorizontalAlignment(HAlign_Fill);Slot->SetVerticalAlignment(VAlign_Fill);
    }
    const TSharedRef<SWidget> Slate=Root->TakeWidget();
    Workshop->Choices=Choices;
    FLDProgressionView View;View.bValid=true;View.Generation=1;View.Life=1;View.Level=1;View.BuildRevision=1;
    View.bHasActiveOffer=true;View.ActiveOffer.OfferId=FGuid(1,2,3,4);View.ActiveOffer.OfferRevision=1;
    for(int32 Index=0;Index<FMath::Min(3,Content.Abilities.Num());++Index){View.ActiveOffer.Options.Add(FName(*Content.Abilities[Index].Id));}
    View.ActiveOffer.SkipPoints=1;
    ULDWorkshop::FInput Input;Input.Mode=ULDWorkshop::EMode::Full;Input.Progression=&View;Input.Match=1;Input.Content=&Content;
    Input.bStage=true;Input.bRest=true;Input.Step=ELDRestStep::Build;Input.Stats=L1;
    const FVector2D LayoutSize(1920,1080);
    const FGeometry Geometry=FGeometry::MakeRoot(LayoutSize,FSlateLayoutTransform(2.f/3.f));
    const auto Frame=[&]()
    {
        Workshop->Present(Input);
        Choices->SetWorkshopPlaces(Workshop->Places());
        Choices->Present(View,1,1,ELDLifePhase::Alive,LDProgression::EMatchStage::Rest,&Content,L1,{});
        Slate->SlatePrepass(2.f/3.f);
        FHittestGrid HitTest;FSlateWindowElementList Draw{TSharedPtr<SWindow>()};
        Slate->Paint(FPaintArgs(nullptr,HitTest,FVector2D::ZeroVector,1.,1.f/60.f),Geometry,FSlateRect(0,0,1280,720),Draw,0,FWidgetStyle(),true);
        Workshop->NativeTick(Geometry,1.f/60.f);Choices->NativeTick(Geometry,1.f/60.f);
    };
    Frame();Choices->OpenPanel();
    {
        // UI-05 same-pass convergence: starting from Closed Workshop (stale Places/TrayDesired, bStage=false), one
        // PresentConverged call opens Full Workshop, updates Stage/Dock rectangles AND reruns Choices->Present + SlatePrepass
        // so StageBox lands in the final Stage rectangle on pass 0 without waiting a second frame.
        ULDWorkshop::FInput ClosedIn=Input;ClosedIn.Mode=ULDWorkshop::EMode::Closed;ClosedIn.bStage=false;
        Workshop->Present(ClosedIn);
        Choices->SetWorkshopPlaces(Workshop->Places());
        Choices->Present(View,1,1,ELDLifePhase::Alive,LDProgression::EMatchStage::Rest,&Content,L1,{});
        Slate->SlatePrepass(2.f/3.f);
        FHittestGrid ColdHit;FSlateWindowElementList ColdDraw{TSharedPtr<SWindow>()};
        Slate->Paint(FPaintArgs(nullptr,ColdHit,FVector2D::ZeroVector,1.,1.f/60.f),Geometry,FSlateRect(0,0,1280,720),ColdDraw,0,FWidgetStyle(),true);

        ULDWorkshop::FInput OpenIn=Input;OpenIn.Mode=ULDWorkshop::EMode::Full;OpenIn.bStage=false;OpenIn.TrayDesired=Choices->TrayDesiredSize();
        ULDWorkshop::PresentConverged(*Workshop,Choices,nullptr,OpenIn,2.f/3.f,[&]()
        {
            Choices->Present(View,1,1,ELDLifePhase::Alive,LDProgression::EMatchStage::Rest,&Content,L1,{});
        });
        const ULDChoicesPanel::FWorkshopPlaces Converged=Workshop->Places();
        const UCanvasPanelSlot* StageSlot=Choices->StageBox?Cast<UCanvasPanelSlot>(Choices->StageBox->Slot):nullptr;
        const FVector2D ExpectedPos=Workshop->Rects.Stage.Min;
        const FVector2D ExpectedSize=Workshop->Rects.Stage.GetSize();
        TestTrue(TEXT("PresentConverged converges bStage, WorkshopPlaces and StageBox geometry in a single pass"),
            OpenIn.bStage&&Choices->IsStageShown()&&Converged.bActive&&Choices->WorkshopPlaces().Equals(Converged)
            &&StageSlot&&StageSlot->GetPosition().Equals(ExpectedPos,1.f)&&StageSlot->GetSize().Equals(ExpectedSize,1.f));

        // Also verify SlatePrepass updates TrayDesiredSize inside the same pass when wrapping pending blueprint + card offers.
        FLDProgressionView WrapView=View;WrapView.bHasActiveOffer=false;
        for(int32 Bp=0;Bp<FMath::Min(4,Content.Abilities.Num());++Bp){WrapView.PendingBlueprints.Add(FName(*Content.Abilities[Bp].Id));}
        WrapView.bHasCardOffer=true;WrapView.CardOffer.OfferId=FGuid(9,9,9,1);
        for(int32 Ci=0;Ci<FMath::Min(3,Content.Cards.Num());++Ci){WrapView.CardOffer.Options.Add(FName(*Content.Cards[Ci].Id));}
        Workshop->Present(ClosedIn);
        Choices->SetWorkshopPlaces(ULDChoicesPanel::FWorkshopPlaces());Choices->SetTrayInset(16.f,80.f,0.f,0.f);
        Choices->Present(WrapView,1,1,ELDLifePhase::Alive,LDProgression::EMatchStage::Rest,&Content,L1,{});
        Slate->SlatePrepass(2.f/3.f);
        ULDWorkshop::FInput WrapOpen=Input;WrapOpen.Mode=ULDWorkshop::EMode::Full;WrapOpen.Progression=&WrapView;
        WrapOpen.bStage=false;WrapOpen.Pending=2;WrapOpen.TrayDesired=Choices->TrayDesiredSize();
        ULDWorkshop::PresentConverged(*Workshop,Choices,nullptr,WrapOpen,2.f/3.f,[&]()
        {
            Choices->Present(WrapView,1,1,ELDLifePhase::Alive,LDProgression::EMatchStage::Rest,&Content,L1,{});
        });
        TestTrue(TEXT("PresentConverged prepasses ChoicesPanel so TrayDesiredSize matches Workshop Pending height on pass 0"),
            WrapOpen.TrayDesired.Equals(Choices->TrayDesiredSize(),.5)&&Workshop->Rects.Pending.bIsValid);
    }
    for(const float TextScale:{1.f,1.25f})
    {
        Workshop->ApplyTextScale(TextScale);Choices->ApplyTextScale(TextScale);
        for(int32 Pick=0;Pick<View.ActiveOffer.Options.Num();++Pick)
        {
            Choices->PreviewOffer(Pick);
            float Smallest=MAX_flt,Largest=0.f;
            for(int32 Tick=0;Tick<90;++Tick)
            {
                Frame();
                if(Tick<30){continue;}
                const float Scale=Workshop->DockBody->GetCachedGeometry().Scale;
                Smallest=FMath::Min(Smallest,Scale);Largest=FMath::Max(Largest,Scale);
            }
            TestTrue(FString::Printf(TEXT("Active dock text %.2f option %d is stable over 60 frames (%.5f..%.5f)"),TextScale,Pick,Smallest,Largest),
                Largest-Smallest<.001f);
        }
    }
    View.bHasActiveOffer=false;View.bFinalModPending=true;View.FinalModOfferId=FGuid(2,3,4,5);
    Input.bFinalStage=true;
    View.FinalModOptions.Reset();
    for(int32 Index=0;Index<4;++Index){View.FinalModOptions.Add(FName(*Content.FinalMods[Index].Id));}
    for(int32 Tick=0;Tick<30;++Tick){Frame();}
    TestTrue(TEXT("Four final options open the same choice stage"),Choices->IsFinalStage());
    TestTrue(TEXT("The fourth final option can be selected"),Choices->SelectFinalMod(3));
    Frame();
    if(TestEqual(TEXT("All four final cards exist"),Choices->FinalCards.Num(),4))
    {
        const UWidget* Last=Choices->FinalCards[3].Frame;
        const FGeometry& G=Last->GetCachedGeometry();
        TestTrue(TEXT("The fourth card remains visible inside the 720p viewport"),Last->IsVisible()
            && G.GetAbsoluteSize().X>30.f && G.GetAbsolutePosition().X+G.GetAbsoluteSize().X<=1280.f);
        View.FinalModOptions.RemoveAt(3);View.FinalModOfferId=FGuid(2,3,4,6);
        for(int32 Tick=0;Tick<30;++Tick){Frame();}
        TestTrue(TEXT("A subsequent three-card offer removes the fourth card from the stage"),
            Last->GetParent()!=Choices->StageCards);
        View.FinalModOptions.Add(FName(*Content.FinalMods[3].Id));View.FinalModOfferId=FGuid(2,3,4,7);
        for(int32 Tick=0;Tick<30;++Tick){Frame();}
        TestTrue(TEXT("A subsequent four-card offer puts the fourth card back on the stage"),
            Last->GetParent()==Choices->StageCards && Last->IsVisible());
    }
    for(int32 First=0;First<Content.FinalMods.Num();First+=3)
    {
        View.FinalModOptions.Reset();
        View.FinalModOfferId=FGuid(2,3,4,First+5);
        for(int32 Index=0;Index<3;++Index){View.FinalModOptions.Add(FName(*Content.FinalMods[(First+Index)%Content.FinalMods.Num()].Id));}
        for(int32 Tick=0;Tick<30;++Tick){Frame();}
        TestTrue(TEXT("Final mod opens its independent stage"),Choices->IsFinalStage());
        for(int32 Pick=0;Pick<3;++Pick)
        {
            Choices->SelectFinalMod(Pick);
            Choices->LeaveStage();Frame();
            Choices->ReopenFinalStage(Pick);
            float Smallest=MAX_flt,Largest=0.f;
            for(int32 Tick=0;Tick<90;++Tick)
            {
                Frame();
                if(Tick<30){continue;}
                const float Scale=Choices->FinalDescText->GetCachedGeometry().Scale;
                Smallest=FMath::Min(Smallest,Scale);Largest=FMath::Max(Largest,Scale);
            }
            const UTextBlock* Rule=Choices->FinalDescText;
            const FGeometry& G=Rule->GetCachedGeometry();
            const FVector2D Size=G.GetAbsoluteSize();
            const bool bReadable=!Rule->GetText().IsEmpty()&&Rule->IsVisible()&&Size.X>30.f&&Rule->GetFont().Size*G.Scale>=10.f
                &&G.GetAbsolutePosition().Y+Size.Y<=720.f;
            const FString At=View.FinalModOptions[Pick].ToString();
            TestTrue(At+TEXT(": reopening retains its readable one-sentence rule inside the screen"),bReadable);
            TestTrue(At+TEXT(": final explanation is stable over 60 frames"),Largest-Smallest<.001f);
        }
    }
    for(const FVector2D Size:{FVector2D(1920,1080),FVector2D(1280,720)})
    {
        Workshop->Layout(Size);
        const auto& R=Workshop->Rects;
        const FString At=FString::Printf(TEXT("Choice stage %.0fx%.0f"),Size.X,Size.Y);
        TestTrue(At+TEXT(": stage and dock fit the screen without overlapping"),
            R.Stage.bIsValid&&R.Dock.bIsValid&&R.Stage.Min.X>=0.&&R.Stage.Max.X<=Size.X
            &&R.Dock.Min.X>=0.&&R.Dock.Max.X<=Size.X&&R.Stage.Min.Y>=0.&&R.Stage.Max.Y<=R.Dock.Min.Y
            &&R.Dock.Max.Y<=Size.Y);
        TestTrue(At+TEXT(": persistent CharacterPanel stays visible beside Stage without overlap"),
            R.Character.bIsValid&&R.Character.GetSize().X>=200.&&R.Character.GetSize().Y>=200.
            &&R.Character.Max.X<=R.Stage.Min.X&&R.Character.Max.X<=R.Dock.Min.X);
    }
    Input.bStage=false;Input.bFinalStage=false;Choices->LeaveStage();
    for(int32 Tick=0;Tick<30;++Tick){Frame();}
    for(const ULDWorkshop::FReaction& Reaction:Workshop->Reactions)
    {
        Workshop->RefreshDock(Reaction.Row->GetCachedGeometry().GetAbsolutePosition()+FVector2D(2,2));
        TestTrue(TEXT("Each element reaction exposes its rule in the actual workshop dock"),
            Workshop->DockBody->GetText().ToString()==LDTerms::ReactionExplanation(Reaction.Name->GetText().ToString())
                &&!Workshop->DockBody->GetText().IsEmpty());
    }
    Input.bDead=true;
    Frame();
    Workshop->RefreshDock(FVector2D(-100,-100));
    TestEqual(TEXT("The death dock reports a recap that has not arrived"),Workshop->DockName->GetText().ToString(),FString(TEXT("承伤记录同步中")));
    Input.DeathRecap.bAvailable=true;
    Input.DeathRecap.Heading=TEXT("近 15 秒承受伤害 100");
    Input.DeathRecap.Rows={TEXT("对手 100 · 普攻 100")};
    LDHUD::FRecapNode Basic;
    Basic.Kind=LDHUD::ERecapNode::Source;Basic.Glyph=TEXT("普");Basic.Name=TEXT("普攻");
    Input.DeathRecap.Chains.AddDefaulted_GetRef().Nodes.Add(Basic);
    Frame();
    Workshop->RefreshDock(FVector2D(-100,-100));
    TestTrue(TEXT("The death dock shows the current life's recap"),
        Workshop->DockName->GetText().ToString()==Input.DeathRecap.Heading
        &&Workshop->DockChainsShown==Input.DeathRecap.Chains
        &&Workshop->DockChains->GetVisibility()==ESlateVisibility::SelfHitTestInvisible);
    const UHorizontalBox* ChainRow=Cast<UHorizontalBox>(Workshop->DockChains->GetChildAt(0));
    const USizeBox* BasicFace=ChainRow?Cast<USizeBox>(ChainRow->GetChildAt(0)):nullptr;
    const UOverlay* BasicOverlay=BasicFace?Cast<UOverlay>(BasicFace->GetContent()):nullptr;
    const UImage* MissingIcon=BasicOverlay&&BasicOverlay->GetChildrenCount()==2?Cast<UImage>(BasicOverlay->GetChildAt(1)):nullptr;
    TestTrue(TEXT("An iconless source exposes its glyph and hover name instead of covering it with an empty image"),
        BasicFace&&BasicFace->GetToolTipText().ToString()==Basic.Name&&MissingIcon
        &&MissingIcon->GetVisibility()==ESlateVisibility::Collapsed);
    if(BasicFace)
    {
        const TSharedRef<SWindow> Window=SNew(SWindow).CreateTitleBar(false).UseOSWindowBorder(false).ClientSize(LayoutSize);
        Window->SetContent(Slate);
        const auto HitSource=[&]()
        {
            Window->SlatePrepass(2.f/3.f);
            FHittestGrid HitTest;
            HitTest.SetHittestArea(FVector2D::ZeroVector,FVector2D(1280,720));
            FSlateWindowElementList Draw(Window);
            Window->Paint(FPaintArgs(nullptr,HitTest,FVector2D::ZeroVector,1.,1.f/60.f),Geometry,
                FSlateRect(0,0,1280,720),Draw,0,FWidgetStyle(),true);
            const FSlateRect Face=BasicFace->GetCachedGeometry().GetRenderBoundingRect();
            const FVector2D Centre((Face.Left+Face.Right)*.5f,(Face.Top+Face.Bottom)*.5f);
            const TSharedPtr<SWidget> FaceWidget=BasicFace->GetCachedWidget();
            const TArray<FWidgetAndPointer> Path=HitTest.GetBubblePath(Centre,0.f,false);
            return FaceWidget.IsValid()&&Path.ContainsByPredicate([&FaceWidget](const FWidgetAndPointer& Hit){return Hit.Widget==FaceWidget;});
        };
        TestTrue(TEXT("The death-dock source is actually reachable by the pointer"),HitSource());
        Workshop->DockChains->SetVisibility(ESlateVisibility::HitTestInvisible);
        TestFalse(TEXT("Hit testing detects when an ancestor blocks the death-dock source"),HitSource());
        Workshop->DockChains->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    }
    Workshop->RefreshDock(Workshop->Reactions[0].Row->GetCachedGeometry().GetAbsolutePosition()+FVector2D(2,2));
    TestTrue(TEXT("A hovered reaction replaces the death recap"),!Workshop->DockKey.StartsWith(TEXT("D|"))
        &&Workshop->DockChains->GetVisibility()==ESlateVisibility::Collapsed);
    Workshop->RefreshDock(FVector2D(-100,-100));
    TestTrue(TEXT("Leaving the reaction restores the death recap"),Workshop->DockKey.StartsWith(TEXT("D|"))
        &&Workshop->DockChainsShown==Input.DeathRecap.Chains);
    Input.bDead=false;
    Frame();
    Workshop->RefreshDock(FVector2D(-100,-100));
    TestTrue(TEXT("The death recap clears after leaving death wait"),
        Workshop->DockBody->GetText().IsEmpty()&&!Workshop->DockKey.StartsWith(TEXT("D|"))
        &&Workshop->DockChains->GetVisibility()==ESlateVisibility::Collapsed);
    Input.bDead=true;
    Frame();
    Workshop->RefreshDock(FVector2D(-100,-100));
    Input.Mode=ULDWorkshop::EMode::Closed;Input.bDead=false;
    Workshop->Present(Input);
    Input.Mode=ULDWorkshop::EMode::Full;
    Frame();
    Workshop->RefreshDock(FVector2D(-100,-100));
    TestTrue(TEXT("Closing during respawn cannot leave a previous life's recap in the reopened dock"),
        Workshop->DockBody->GetText().IsEmpty()&&Workshop->DockName->GetText().IsEmpty());

    ULDComponentStorage* Storage=NewObject<ULDComponentStorage>(World);Storage->Initialize();
    auto* StorageSlot=Root->AddChildToOverlay(Storage);
    StorageSlot->SetHorizontalAlignment(HAlign_Fill);StorageSlot->SetVerticalAlignment(VAlign_Fill);
    Workshop->Storage=Storage;
    FLDProgressionView Dense;Dense.bValid=true;Dense.Generation=1;Dense.Life=1;Dense.Level=16;Dense.BuildRevision=2;
    // A full body column (the nine slots of level 16) and every other approved card stored once.
    int32 Count=0;
    for(const FLDDemoCardDefinition& Row:Content.Cards)
    {
        FLDCardInstanceView& Entry=Dense.Cards.AddDefaulted_GetRef();
        Entry.Serial=uint16(Dense.Cards.Num());Entry.DefinitionId=FName(*Row.Id);
        if(Row.Form==ELDCardForm::Body&&Count<9){Entry.Target.Kind=ELDCardTargetKind::Body;++Count;}
    }
    const auto KeyOf=[](const FLDCardInstanceView& Entry){return LDHUD::CardKey(Entry.DefinitionId,Entry.Serial);};
    Input.Progression=&Dense;Input.TrayDesired=FVector2D(200,100);Input.Pending=2;
    ULDComponentStorage::FAnchors NoAnchors;
    for(const ULDWorkshop::EMode Mode:{ULDWorkshop::EMode::Full,ULDWorkshop::EMode::Field})
    for(const float ScreenScale:{1.f,2.f/3.f})
    {
        Input.Mode=Mode;Input.bServices=Mode==ULDWorkshop::EMode::Full;
        Workshop->LibraryPage=0;
        const FGeometry Screen=FGeometry::MakeRoot(LayoutSize,FSlateLayoutTransform(ScreenScale));
        const auto DenseFrame=[&]()
        {
            Workshop->Present(Input);
            Storage->SetWorkshop(Workshop->StorageLayout());
            Storage->Present(&Dense,1,LDProgression::EMatchStage::Rest,ELDLifePhase::Alive,&Content,L1,NoAnchors,true);
            Slate->SlatePrepass(ScreenScale);
            FHittestGrid HitTest;FSlateWindowElementList Draw{TSharedPtr<SWindow>()};
            Slate->Paint(FPaintArgs(nullptr,HitTest,FVector2D::ZeroVector,1.,1.f/60.f),Screen,
                FSlateRect(0,0,LayoutSize.X*ScreenScale,LayoutSize.Y*ScreenScale),Draw,0,FWidgetStyle(),true);
            Workshop->NativeTick(Screen,1.f/60.f);Storage->NativeTick(Screen,1.f/60.f);
        };
        for(int32 Tick=0;Tick<30;++Tick){DenseFrame();}
        const auto& R=Workshop->Rects;
        const FString At=FString::Printf(TEXT("Dense workshop mode %d scale %.2f"),int32(Mode),ScreenScale);
        if(Mode==ULDWorkshop::EMode::Full)
        {
            TestTrue(At+TEXT(": full-width dock is below all three panels"),
                R.Dock.Min.X<=R.Character.Min.X&&R.Dock.Max.X>=R.Library.Max.X&&R.Dock.Min.Y>=R.Character.Max.Y
                &&R.Dock.Min.Y>=R.Board.Max.Y&&R.Dock.Min.Y>=R.Pending.Max.Y);
        }
        else
        {
            TestTrue(At+TEXT(": field mode keeps compact CharacterPanel visible above Dock with positive LibraryBody height"),
                R.Character.bIsValid&&R.Character.GetSize().X>=200.&&R.Character.GetSize().Y>=100.
                &&R.Character.Max.Y<=R.Dock.Min.Y&&R.LibraryBody.bIsValid&&R.LibraryBody.GetSize().Y>30.);
        }
        TestTrue(At+TEXT(": library has a usable page"),Workshop->LibraryFit.bFits&&!Storage->Cards.IsEmpty());
        const int32 Pages=Workshop->LibraryFit.Pages.Num();
        TSet<FName> SeenCards;
        for(int32 Page=0;Page<Pages;++Page)
        {
            Workshop->LibraryPage=Page;
            for(int32 Tick=0;Tick<5;++Tick){DenseFrame();}
            const FSlateRect Library=Storage->Workshop.Library;
            for(const ULDComponentStorage::FCard& Card:Storage->Cards)
            {
                SeenCards.Add(Card.Component);
                const FSlateRect Face=Card.Face->GetCachedGeometry().GetRenderBoundingRect();
                TestTrue(At+TEXT(": library face stays visible and at least 36 pixels ")+Card.Component.ToString(),
                    Face.Left>=Library.Left-1.f&&Face.Right<=Library.Right+1.f&&Face.Top>=Library.Top-1.f
                    &&Face.Bottom<=Library.Bottom+1.f&&Face.Right-Face.Left>=35.5f&&Face.Bottom-Face.Top>=35.5f);
            }
        }
        TestEqual(At+TEXT(": all stored cards appear across the pages"),SeenCards.Num(),Dense.Cards.Num()-Count);
        const FSlateRect Column=Storage->Workshop.Columns[int32(LDHUD::EStorageColumn::Body)];
        for(const FLDCardInstanceView& Entry:Dense.Cards)
        {
            if(!Entry.IsInstalled()){continue;}
            const FName Key=KeyOf(Entry);
            FSlateRect Tile;
            if(!TestTrue(At+TEXT(": installed face exists ")+Entry.DefinitionId.ToString(),Storage->TileBox(Key,Tile))){continue;}
            const ULDComponentStorage::FTile* Face=Storage->Tiles.FindByPredicate(
                [&Key](const ULDComponentStorage::FTile& Candidate){return Candidate.Component==Key;});
            if(!TestTrue(At+TEXT(": face widget exists"),Face&&Face->Face)){continue;}
            Tile=Face->Face->GetCachedGeometry().GetRenderBoundingRect();
            TestTrue(At+TEXT(": installed face fits its column ")+Entry.DefinitionId.ToString(),
                Tile.Left>=Column.Left&&Tile.Right<=Column.Right&&Tile.Top>=Column.Top&&Tile.Bottom<=Column.Bottom);
            const float Minimum=Mode==ULDWorkshop::EMode::Full?35.5f:20.f;
            TestTrue(At+FString::Printf(TEXT(": installed face remains usable %s (%.2f x %.2f at %.2f,%.2f)"),
                *Entry.DefinitionId.ToString(),Tile.Right-Tile.Left,Tile.Bottom-Tile.Top,Tile.Left,Tile.Top),
                Tile.Right-Tile.Left>=Minimum&&Tile.Bottom-Tile.Top>=Minimum);
            LDHUD::EStorageColumn HitColumn=LDHUD::EStorageColumn::Dash;FName Host;
            const FVector2D Center((Tile.Left+Tile.Right)*.5f,(Tile.Top+Tile.Bottom)*.5f);
            TestTrue(At+TEXT(": scaled face is its own drop target ")+Entry.DefinitionId.ToString(),
                Storage->ColumnAt(Center,HitColumn,Host)&&HitColumn==LDHUD::EStorageColumn::Body&&Host==Key);
        }
        if(const FLDCardInstanceView* Last=Dense.Cards.FindByPredicate([](const FLDCardInstanceView& Entry){return Entry.IsInstalled();}))
        {
            const FName Id=KeyOf(*Last);FSlateRect Tile;
            if(Storage->TileBox(Id,Tile))
            {
                const ULDComponentStorage::FTile* Face=Storage->Tiles.FindByPredicate(
                    [Id](const ULDComponentStorage::FTile& Candidate){return Candidate.Component==Id;});
                if(!Face||!Face->Face){continue;}
                Tile=Face->Face->GetCachedGeometry().GetRenderBoundingRect();
                const FVector2D Center((Tile.Left+Tile.Right)*.5f,(Tile.Top+Tile.Bottom)*.5f);
                TestTrue(At+TEXT(": scaled face can be picked up"),
                    Storage->BeginPress(Center)&&Storage->Press.bArmed&&Storage->Press.Component==Id);
                Storage->UpdateDrag(Center,false);
                TestTrue(At+TEXT(": pickup retains the card"),Storage->IsHolding()&&Storage->Drag.Component==Id);
                Storage->CancelInteraction();
            }
        }
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDSelfIconsTest,"Lockdown.Rules.HUDSelfIcons",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDSelfIconsTest::RunTest(const FString& Parameters)
{
    TestEqualSensitive(TEXT("A Latin name's icon is its upper-cased first letter"),LDHUD::IconGlyph(TEXT(" bolt")),FString(TEXT("B")));
    TestEqualSensitive(TEXT("A Chinese name's icon is its first character"),LDHUD::IconGlyph(TEXT("眩晕")),FString(TEXT("眩")));
    TestEqualSensitive(TEXT("An empty name is never a blank square"),LDHUD::IconGlyph(TEXT("  ")),FString(TEXT("?")));
    // More temporary statuses and recoveries than one strip row: every one shows, control statuses first.
    if(!HudRegisterFixtureTable(*this)){return false;}
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(HudFixtureTable);};
    const FLDDemoContentDefinitions Content=HudFixtureContent();
    FLDCombatOwnerView Combat;
    FLDCombatStatusView Root;Root.StatusId=uint8(ELDDemoStatusId::Root);Root.ExpiresAtMs=5000;Root.ControlBlocks=uint8(ELDCombatControlBlock::Move);
    FLDCombatStatusView Shield;Shield.StatusId=uint8(ELDDemoStatusId::Shield);Shield.ExpiresAtMs=4000;Shield.AmountP2=5000;
    Combat.Statuses={Shield,Root};
    while(Combat.Statuses.Num()<14){Combat.Statuses.Add(Shield);}
    Combat.bOutOfCombatRegenActive=true;
    const LDHUD::FSelfIconsView Full=LDHUD::SelfIcons(&Combat,true,0,&Content);
    TestEqual(TEXT("The strip has no cap: 14 statuses and a recovery all show"),Full.Items.Num(),15);
    TestTrue(TEXT("Control statuses come first, the recovery last"),Full.Items[0].Kind==LDHUD::ESelfIconKind::Control
        &&Full.Items.Last().Source==LDHUD::ESelfIconSource::OutOfCombatRegen);
    TestTrue(TEXT("Without the own projection the grid is syncing"),LDHUD::SelfIcons(nullptr,true,0,&Content).bSyncing);
    // The hover popup names the square and its kind; the description comes from the content catalog.
    const LDHUD::FActionDetailView RootDetail=LDHUD::SelfIconDetail(Full.Items[0],nullptr);
    TestTrue(TEXT("A control status popup heads with its line, kind and tone"),RootDetail.Name==Full.Items[0].Tip&&RootDetail.Summary==TEXT("控制状态 · 负面"));
    // Status tone: what helps the bearer is positive, every control and affliction negative, an unknown id negative.
    for(const ELDDemoStatusId Id:{ELDDemoStatusId::Shield,ELDDemoStatusId::StatBoost,ELDDemoStatusId::Empowered,ELDDemoStatusId::RespawnProtection,ELDDemoStatusId::CampReturning})
    {TestTrue(FString::Printf(TEXT("Status %d is positive"),int32(Id)),LDHUD::StatusIsPositive(uint8(Id)));}
    for(const ELDDemoStatusId Id:{ELDDemoStatusId::Root,ELDDemoStatusId::Slow,ELDDemoStatusId::Exposed,ELDDemoStatusId::Taunt,ELDDemoStatusId::Disarm,
        ELDDemoStatusId::Stun,ELDDemoStatusId::Knockback,ELDDemoStatusId::Silence,ELDDemoStatusId::Burn})
    {TestFalse(FString::Printf(TEXT("Status %d is negative"),int32(Id)),LDHUD::StatusIsPositive(uint8(Id)));}
    TestFalse(TEXT("An unknown status reads as negative"),LDHUD::StatusIsPositive(200));
    TestTrue(TEXT("A shield square is positive"),Full.Items[1].bPositive&&!Full.Items[0].bPositive);
    TestEqualSensitive(TEXT("A shield popup says 临时状态 · 正面"),LDHUD::SelfIconDetail(Full.Items[1],nullptr).Summary,FString(TEXT("临时状态 · 正面")));
    // Remaining-time rings: the first sight of an end instant is the full ring.
    LDHUD::FStatusRings Rings;const FName Key(TEXT("Status.1"));
    TestEqual(TEXT("A newly seen status starts with a full ring"),Rings.Fraction(Key,4000,1000),1.f);
    TestEqual(TEXT("Half the span left is half a ring"),Rings.Fraction(Key,4000,2500),.5f);
    TestEqual(TEXT("A refresh (a later end) restarts the ring full"),Rings.Fraction(Key,8000,2500),1.f);
    TestEqual(TEXT("An earlier end keeps the span and shortens the ring"),Rings.Fraction(Key,5250,2500),.5f);
    TestEqual(TEXT("A passed end is an empty ring"),Rings.Fraction(Key,5250,9000),0.f);
    Rings.Retain({});
    TestEqual(TEXT("A status gone and re-applied starts a fresh full ring"),Rings.Fraction(Key,3000,2000),1.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDCombatOwnerViewTest,"Lockdown.Rules.HUDCombatOwnerView",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDCombatOwnerViewTest::RunTest(const FString& Parameters)
{
    if(!HudRegisterFixtureTable(*this)){return false;}
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(HudFixtureTable);};
    FLDDemoContentDefinitions Content=HudFixtureContent();
    FLDCombatOwnerView Combat;
    FLDCombatStatusView Root;Root.StatusId=uint8(ELDDemoStatusId::Root);Root.ExpiresAtMs=11201;Root.ControlBlocks=uint8(ELDCombatControlBlock::Move);
    FLDCombatStatusView Aura;Aura.StatusId=uint8(ELDDemoStatusId::Shield);Aura.bIsPassive=true;
    FLDCombatStatusView Protection;Protection.StatusId=uint8(ELDDemoStatusId::RespawnProtection);Protection.ExpiresAtMs=20000;
    FLDCombatStatusView Shield;Shield.StatusId=uint8(ELDDemoStatusId::Shield);Shield.AmountP2=8050;Shield.ExpiresAtMs=12000;
    // The shield is listed before the root: the grid still puts the control status first.
    Combat.Statuses={Shield,Aura,Protection,Root};
    auto Tips=[](const LDHUD::FSelfIconsView& View){TArray<FString> Out;for(const LDHUD::FSelfIconView& Icon:View.Items){Out.Add(Icon.Tip);}return FString::Join(Out,TEXT(" | "));};
    LDHUD::FSelfIconsView Icons=LDHUD::SelfIcons(&Combat,true,10000,&Content);
    TestEqualSensitive(TEXT("Control first; time and shield round up; passive auras and protection stay off the grid"),
        Tips(Icons),HudName(TEXT("root"))+TEXT(" 1.3s | ")+HudName(TEXT("shield"))+TEXT(" 81 2.0s"));
    TestTrue(TEXT("Timed statuses carry their end instant and kind"),Icons.Items.Num()==2&&Icons.Items[0].Kind==LDHUD::ESelfIconKind::Control
        &&Icons.Items[0].ExpiresAtMs==11201&&Icons.Items[1].Kind==LDHUD::ESelfIconKind::Temporary&&Icons.Items[1].ExpiresAtMs==12000&&!Icons.bSyncing);
    TestTrue(TEXT("The root square is negative and the shield square positive"),!Icons.Items[0].bPositive&&Icons.Items[1].bPositive
        &&LDHUD::SelfIconDetail(Icons.Items[1],nullptr).Summary==TEXT("临时状态 · 正面"));
    TestEqualSensitive(TEXT("The missing-icon stand-in is the name's first character"),Icons.Items[0].Glyph,LDHUD::IconGlyph(HudName(TEXT("root"))));
    TestTrue(TEXT("Status squares draw their status's display icon"),Icons.Items[0].IconId==TEXT("icon_root")&&Icons.Items[1].IconId==TEXT("icon_shield"));
    TestEqualSensitive(TEXT("Without the matching authority clock no remainder is shown"),Tips(LDHUD::SelfIcons(&Combat,false,0,&Content)),
        HudName(TEXT("root"))+TEXT(" | ")+HudName(TEXT("shield"))+TEXT(" 81"));
    const LDHUD::FSelfIconsView Missing=LDHUD::SelfIcons(nullptr,true,0,&Content);
    TestTrue(TEXT("A missing projection reads as syncing, never as none"),Missing.bSyncing&&Missing.Items.IsEmpty());
    Combat.Statuses.Reset();
    // D21 recoveries are temporary squares while they heal, with no remaining-time ring.
    Combat.bOutOfCombatRegenActive=true;Combat.bDormRegenActive=true;
    Icons=LDHUD::SelfIcons(&Combat,true,10000,&Content);
    TestEqualSensitive(TEXT("Active out-of-combat and dorm recoveries show"),Tips(Icons),
        FString(LDTerms::OutOfCombatRegen)+TEXT(" | ")+LDTerms::DormRegenActive());
    TestTrue(TEXT("Recoveries are untimed temporary squares"),Icons.Items.Num()==2&&Icons.Items[0].Kind==LDHUD::ESelfIconKind::Temporary
        &&Icons.Items[0].ExpiresAtMs==0&&Icons.Items[1].Kind==LDHUD::ESelfIconKind::Temporary&&Icons.Items[1].ExpiresAtMs==0);
    TestTrue(TEXT("Recoveries draw their own icons without a display row"),
        Icons.Items[0].IconId==LDUIIcons::OutOfCombatRegen&&Icons.Items[1].IconId==LDUIIcons::DormRegen);
    TestTrue(TEXT("Recoveries are positive"),Icons.Items.FilterByPredicate([](const LDHUD::FSelfIconView& Icon){return Icon.bPositive;}).Num()==2);
    Content.Parameters.OutOfCombatRegenDelayMs=5000;Content.Parameters.OutOfCombatMaxHPRegenBp=150;
    Content.Parameters.DormRegenDelayMs=3000;Content.Parameters.DormMaxHPRegenBp=1000;
    const LDHUD::FActionDetailView Slow=LDHUD::SelfIconDetail(Icons.Items[0],&Content);
    TestTrue(TEXT("The out-of-combat popup states the REGEN delay and share"),Slow.Name==LDTerms::OutOfCombatRegen&&Slow.Summary==TEXT("临时状态 · 正面")
        &&Slow.Body==TEXT("脱战 5s 后，每秒恢复 1.5% 生命上限\n出手、释放伤害技能或受到伤害立即停止"));
    const LDHUD::FActionDetailView Dorm=LDHUD::SelfIconDetail(Icons.Items[1],&Content);
    TestTrue(TEXT("The dorm popup states the DORM delay and share"),Dorm.Body.StartsWith(FString(TEXT("站在"))+LDTerms::Dorm+TEXT("内且脱战 3s 后，每秒恢复 10% 生命上限")));
    Combat.bOutOfCombatRegenActive=false;Combat.bDormRegenActive=false;
    Icons=LDHUD::SelfIcons(&Combat,true,10000,&Content);
    TestTrue(TEXT("A known projection without statuses or recoveries shows an empty strip"),Icons.Items.IsEmpty()&&!Icons.bSyncing);
    FLDCombatActionReceipt Receipt;Receipt.Origin=ELDCombatRequestOrigin::Direct;Receipt.Entry=ELDCombatEntry::W;
    Receipt.Phase=ELDCombatPhase::Rejected;Receipt.Reject=ELDCombatReject::Cooldown;Receipt.bProtectionRemoved=true;
    TestEqualSensitive(TEXT("A failed own request and the lost protection are separate facts"),LDHUD::CombatReceiptNotice(Receipt),TEXT("角色技能 2 未执行：冷却中 · ")+LDTerms::ProtectionRemoved());
    Receipt.bProtectionRemoved=false;Receipt.Reject=ELDCombatReject::Cancelled;
    TestTrue(TEXT("A cancel tells nothing"),LDHUD::CombatReceiptNotice(Receipt).IsEmpty());
    Receipt.Origin=ELDCombatRequestOrigin::Nav;Receipt.Reject=ELDCombatReject::OutOfRange;
    TestTrue(TEXT("Navigation-issued attacks report through their order, not as an action failure"),LDHUD::CombatReceiptNotice(Receipt).IsEmpty());
    Receipt.Phase=ELDCombatPhase::Accepted;Receipt.Reject=ELDCombatReject::None;Receipt.bProtectionRemoved=true;
    TestEqualSensitive(TEXT("An accepted attack still reports the lost protection"),LDHUD::CombatReceiptNotice(Receipt),LDTerms::ProtectionRemoved());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDSelfIconsNoticeTest,"Lockdown.Rules.HUDSelfIconsNotice",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDSelfIconsNoticeTest::RunTest(const FString& Parameters)
{
    FLDDemoContentDefinitions Content;
    const LDHUD::FSelfIconsView Missing=LDHUD::SelfIcons(nullptr,true,0,&Content);
    TestEqualSensitive(TEXT("A missing own projection shows the syncing line over the own bars"),LDHUD::SelfIconsNotice(Missing,true),FString(TEXT("状态同步中")));
    TestTrue(TEXT("The syncing line hides with the band"),LDHUD::SelfIconsNotice(Missing,false).IsEmpty());
    FLDCombatOwnerView Combat;
    TestTrue(TEXT("A known projection without statuses shows nothing, not syncing"),LDHUD::SelfIconsNotice(LDHUD::SelfIcons(&Combat,true,0,&Content),true).IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDBasicAttackTermTest,"Lockdown.Rules.HUDBasicAttackTerm",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDBasicAttackTermTest::RunTest(const FString& Parameters)
{
    // APP_SPEC 1.2 (#148): the player reads the basic attack by its generic glossary term; retired memes never return in system text.
    const FString NotReady=LDHUD::CombatRejectText(ELDCombatReject::AttackNotReady);
    TestTrue(TEXT("The attack-not-ready reason names the basic attack by its player term"),
        NotReady.Contains(LDTerms::BasicAttack,ESearchCase::CaseSensitive));
    FLDDemoEffectDefinition Lifesteal;Lifesteal.Opcode=uint8(ELDDemoEffectOp::Lifesteal);Lifesteal.RatioBp=1500;
    TestEqualSensitive(TEXT("The lifesteal line names the basic attack by its player term"),LDHUD::EffectLine(Lifesteal),FString(LDTerms::BasicAttack)+TEXT("打掉的")+LDTerms::Health+TEXT("的 15% 回复自身"));
    TestEqualSensitive(TEXT("The basic entry label uses the player term"),LDHUD::EntryLabel(ELDCombatEntry::Basic),FString(LDTerms::BasicAttack));
    const TCHAR* const Retired[]={TEXT("大比兜"),TEXT("精神状态"),TEXT("电量"),TEXT("力大砖飞"),TEXT("阴阳怪气"),TEXT("嘴硬"),TEXT("心大"),TEXT("手速"),
        TEXT("润"),TEXT("整活频率"),TEXT("物理超度"),TEXT("精神攻击"),TEXT("心理防线"),TEXT("提裤子"),TEXT("腿软"),TEXT("捂裆"),TEXT("免死金牌")};
    FString Technical;
    for(int32 Value=0;Value<=MAX_uint8;++Value)
    {
        FLDDemoEffectDefinition Effect;Effect.Opcode=uint8(Value);
        for(const FString& Line:{LDHUD::CombatRejectText(static_cast<ELDCombatReject>(Value)),LDHUD::EffectLine(Effect),LDHUD::EntryLabel(static_cast<ELDCombatEntry>(Value))})
        {for(const TCHAR* Term:Retired){if(Line.Contains(Term,ESearchCase::CaseSensitive)){Technical+=Line+TEXT(" | ");break;}}}
    }
    TestTrue(*FString::Printf(TEXT("No combat reason, effect line or entry label shows a retired meme term (found: %s)"),*Technical),Technical.IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDIdentityPaletteTest,"Lockdown.Rules.HUDIdentityPalette",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDIdentityPaletteTest::RunTest(const FString& Parameters)
{
    // Identity colours follow the locked roster slot 0..5: a portrait shows the same tint as that contender's body.
    auto Spread=[](const FLinearColor& A,const FLinearColor& B){return FMath::Max3(FMath::Abs(A.R-B.R),FMath::Abs(A.G-B.G),FMath::Abs(A.B-B.B));};
    for(int32 Slot=0;Slot<6;++Slot)
    {
        const FLinearColor Tint=LDHUD::PaletteColor(uint8(Slot));
        TestTrue(*FString::Printf(TEXT("Roster slot %d portrait uses its body tint"),Slot),Tint==ULDHumanoidVisualComponent::ContenderTint(uint8(Slot)));
        for(int32 Other=0;Other<Slot;++Other)
        {TestTrue(*FString::Printf(TEXT("Roster slots %d and %d have distinct identity tints"),Other,Slot),Spread(Tint,LDHUD::PaletteColor(uint8(Other)))>=.1f);}
    }
    // A camp member's portrait uses its NPC clothing, never a contender's identity tint.
    for(const ELDDemoMonsterId Monster:{ELDDemoMonsterId::MonsterScavenger,ELDDemoMonsterId::MonsterGuardian})
    {
        const FLinearColor Camp=ULDHumanoidVisualComponent::MonsterTint(uint8(Monster));
        for(int32 Slot=0;Slot<6;++Slot)
        {TestTrue(*FString::Printf(TEXT("Creature %d is not drawn in roster slot %d's tint"),int32(Monster),Slot),Spread(Camp,LDHUD::PaletteColor(uint8(Slot)))>=.1f);}
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDCooldownTextTest,"Lockdown.Rules.HUDCooldownText",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDCooldownTextTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("A ready track draws no remainder"),LDHUD::CooldownText(0).IsEmpty()&&LDHUD::CooldownText(-250).IsEmpty());
    TestEqualSensitive(TEXT("From ten seconds the remainder reads in whole seconds"),LDHUD::CooldownText(12000),FString(TEXT("12")));
    TestEqualSensitive(TEXT("Whole seconds round up"),LDHUD::CooldownText(12001),FString(TEXT("13")));
    TestEqualSensitive(TEXT("Exactly ten seconds reads whole"),LDHUD::CooldownText(10000),FString(TEXT("10")));
    TestEqualSensitive(TEXT("A remainder that rounds up to ten seconds reads whole, never 10.0"),LDHUD::CooldownText(9950),FString(TEXT("10")));
    TestEqualSensitive(TEXT("Below ten seconds the remainder reads in tenths"),LDHUD::CooldownText(3200),FString(TEXT("3.2")));
    TestEqualSensitive(TEXT("Tenths round up"),LDHUD::CooldownText(3201),FString(TEXT("3.3")));
    TestEqualSensitive(TEXT("The last tenth below ten seconds"),LDHUD::CooldownText(9900),FString(TEXT("9.9")));
    TestEqualSensitive(TEXT("Any live remainder reads at least a tenth, never 0.0"),LDHUD::CooldownText(1),FString(TEXT("0.1")));
    LDHUD::FActionContext Context;Context.KeyLabel=TEXT("Q");Context.bOwnContext=true;Context.bPlaying=true;Context.bAlive=true;Context.bClock=true;Context.NowMs=4800;
    LDHUD::FActionInput Bolt;Bolt.Slot=LDHUD::EActionSlot::Q;Bolt.bProjected=true;Bolt.bHasSource=true;Bolt.Name=TEXT("技能甲");Bolt.ReadyAtMs=8000;Bolt.CooldownMs=8000;
    TestEqualSensitive(TEXT("The slot view carries the remainder drawn over the mask"),LDHUD::Action(Bolt,Context).Countdown,FString(TEXT("3.2")));
    Context.NowMs=8000;
    TestTrue(TEXT("The remainder disappears once the authority clock reaches the track end"),LDHUD::Action(Bolt,Context).Countdown.IsEmpty());
    Context.NowMs=4800;Context.bClock=false;
    TestTrue(TEXT("Without the authority clock no remainder is drawn"),LDHUD::Action(Bolt,Context).Countdown.IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDActionStatesTest,"Lockdown.Rules.HUDActionStates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDActionStatesTest::RunTest(const FString& Parameters)
{
    LDHUD::FActionContext Context;Context.KeyLabel=TEXT("Q");Context.bOwnContext=true;Context.bPlaying=true;Context.bAlive=true;Context.bClock=true;Context.NowMs=4800;Context.bMomentumKnown=true;Context.MomentumP2=10000;
    LDHUD::FActionInput Bolt;Bolt.Slot=LDHUD::EActionSlot::Q;Bolt.bProjected=true;Bolt.bHasSource=true;Bolt.Name=TEXT("技能甲");Bolt.MomentumCostP2=3050;
    LDHUD::FActionView View=LDHUD::Action(Bolt,Context);
    LDHUD::EActionLook Shown=LDHUD::ActionLook(View,false);
    TestEqualSensitive(TEXT("The cost chip shows only the momentum cost, rounded down like the shown momentum"),View.Cost,FString(TEXT("30")));
    TestTrue(TEXT("A learned active shows its name's first character as the icon"),View.Glyph==TEXT("技"));
    TestTrue(TEXT("A ready, affordable active looks ready"),Shown==LDHUD::EActionLook::Ready);
    Bolt.ReadyAtMs=8000;Bolt.CooldownMs=8000;View=LDHUD::Action(Bolt,Context);Shown=LDHUD::ActionLook(View,false);
    TestTrue(TEXT("A cooling active looks cooling and carries its remainder"),Shown==LDHUD::EActionLook::Cooling&&View.Countdown==TEXT("3.2"));
    Context.MomentumP2=1000;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("A cooldown outranks short momentum"),LDHUD::ActionLook(View,false)==LDHUD::EActionLook::Cooling);
    Bolt.ReadyAtMs=0;View=LDHUD::Action(Bolt,Context);Shown=LDHUD::ActionLook(View,false);
    TestTrue(TEXT("Short momentum without a cooldown has its own look and still sends"),Shown==LDHUD::EActionLook::MomentumShort&&View.State==LDTerms::MomentumShort()&&View.bCanSubmitIntent);
    Bolt.bControlled=true;View=LDHUD::Action(Bolt,Context);Shown=LDHUD::ActionLook(View,false);
    TestTrue(TEXT("A control status outranks short momentum"),Shown==LDHUD::EActionLook::Controlled);
    Bolt.ReadyAtMs=8000;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("A control status outranks the cooldown, whose remainder still shows"),LDHUD::ActionLook(View,false)==LDHUD::EActionLook::Controlled&&View.Countdown==TEXT("3.2"));
    Shown=LDHUD::ActionLook(View,true);
    TestTrue(TEXT("The armed entry outranks its remaining limits"),Shown==LDHUD::EActionLook::Armed);
    Bolt.bControlled=false;Bolt.ReadyAtMs=0;Context.MomentumP2=10000;
    LDHUD::FActionInput Empty;Empty.Slot=LDHUD::EActionSlot::W;Empty.bProjected=true;View=LDHUD::Action(Empty,Context);Shown=LDHUD::ActionLook(View,true);
    TestTrue(TEXT("An empty slot looks empty even when armed and has no cost or icon glyph"),Shown==LDHUD::EActionLook::Empty&&View.Cost.IsEmpty()&&View.Glyph.IsEmpty()&&View.Name==TEXT("空槽"));
    Context.bAlive=false;View=LDHUD::Action(Bolt,Context);Shown=LDHUD::ActionLook(View,true);
    TestTrue(TEXT("A dead owner's slot is blocked, never armed"),Shown==LDHUD::EActionLook::Blocked&&View.State==LDTerms::Dead);
    Context.bAlive=true;Bolt.ServerBlock=TEXT("目标不可用");View=LDHUD::Action(Bolt,Context);Shown=LDHUD::ActionLook(View,false);
    TestTrue(TEXT("A published server reason blocks the look and its tooltip names the reason"),Shown==LDHUD::EActionLook::Blocked&&View.State==TEXT("目标不可用"));
    Bolt.ServerBlock.Reset();Bolt.MomentumCostP2=0;
    TestTrue(TEXT("An active without a cost shows no chip"),LDHUD::Action(Bolt,Context).Cost.IsEmpty());
    LDHUD::FActionInput Unprojected=Bolt;Unprojected.bProjected=false;Unprojected.MomentumCostP2=5000;View=LDHUD::Action(Unprojected,Context);
    TestTrue(TEXT("Without a projection there is no cost and the slot is unsynced"),View.Cost.IsEmpty()&&LDHUD::ActionLook(View,false)==LDHUD::EActionLook::Blocked);
    FLDCombatActionReceipt Receipt;Receipt.Origin=ELDCombatRequestOrigin::Direct;Receipt.Entry=ELDCombatEntry::W;Receipt.Phase=ELDCombatPhase::Rejected;Receipt.Reject=ELDCombatReject::OutOfRange;
    TestEqualSensitive(TEXT("The pressed slot shows the reason of the player's failed request"),LDHUD::CombatReceiptSlotNotice(Receipt),FString(TEXT("超出射程")));
    Receipt.Entry=ELDCombatEntry::Basic;
    TestTrue(TEXT("The basic attack has no slot to tell"),LDHUD::CombatReceiptSlotNotice(Receipt).IsEmpty());
    Receipt.Entry=ELDCombatEntry::Q;Receipt.Reject=ELDCombatReject::Cancelled;
    TestTrue(TEXT("A cancel tells the slot nothing"),LDHUD::CombatReceiptSlotNotice(Receipt).IsEmpty());
    Receipt.Reject=ELDCombatReject::Cooldown;Receipt.Origin=ELDCombatRequestOrigin::Nav;
    TestTrue(TEXT("A navigation-issued request tells the slot nothing"),LDHUD::CombatReceiptSlotNotice(Receipt).IsEmpty());
    Receipt.Origin=ELDCombatRequestOrigin::Direct;Receipt.Phase=ELDCombatPhase::Accepted;
    TestTrue(TEXT("An accepted request tells the slot nothing"),LDHUD::CombatReceiptSlotNotice(Receipt).IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDDeathCauseTest,"Lockdown.Rules.HUDDeathCause",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDDeathCauseTest::RunTest(const FString& Parameters)
{
    const FGuid Self=FGuid::NewGuid(),Rival=FGuid::NewGuid(),Other=FGuid::NewGuid();
    FLDLobbyView Lobby;
    FLDLobbyMember Me;Me.ContenderId=Self;Me.Name=TEXT("我");Lobby.Members.Add(Me);
    FLDLobbyMember Killer;Killer.ContenderId=Rival;Killer.Name=TEXT("玩家甲");Lobby.Members.Add(Killer);
    FLDKillFeedView Feed;Feed.Generation=7;
    auto AddNotice=[&Feed](uint64 Serial,const FGuid& Victim,const FGuid& By,ELDKillCause Kind,int64 AtMs)
    {FLDKillNotice Entry;Entry.Serial=Serial;Entry.Victim=Victim;Entry.Killer=By;Entry.Cause=Kind;Entry.OccurredAtMs=AtMs;Feed.Notices.Add(Entry);};
    auto CauseAt=[&Feed,&Lobby,&Self](uint32 Generation,int64 NotBeforeMs){return LDHUD::DeathCause(Feed,Generation,Self,NotBeforeMs,Lobby,LDTerms::CampLow,LDTerms::CampHigh,LDTerms::CampElite,LDTerms::Boss);};
    AddNotice(1,Self,FGuid(),ELDKillCause::CampHigh,10000);
    AddNotice(2,Other,Rival,ELDKillCause::Contender,30000);
    AddNotice(3,Self,Rival,ELDKillCause::Contender,50000);
    TestEqualSensitive(TEXT("The newest own death names its killer from the public roster"),CauseAt(7,40000),LDTerms::KilledBy(TEXT("玩家甲")));
    TestTrue(TEXT("A feed of another generation is not this match"),CauseAt(8,0).IsEmpty()&&CauseAt(0,0).IsEmpty());
    TestTrue(TEXT("Before this life's notice arrives an older death never stands in"),CauseAt(7,60000).IsEmpty());
    TestTrue(TEXT("Without an own identity there is no cause"),LDHUD::DeathCause(Feed,7,FGuid(),0,Lobby,LDTerms::CampLow,LDTerms::CampHigh,LDTerms::CampElite,LDTerms::Boss).IsEmpty());
    Feed.Notices.RemoveAt(2);
    TestEqualSensitive(TEXT("A camp death names the approved creature of its tier"),CauseAt(7,0),LDTerms::KilledBy(LDTerms::CampHigh));
    TestTrue(TEXT("Another contender's death is never the player's cause"),CauseAt(7,20000).IsEmpty());
    AddNotice(4,Self,FGuid(),ELDKillCause::CampLow,70000);
    TestEqualSensitive(TEXT("The low camp tier has its own creature"),CauseAt(7,60000),LDTerms::KilledBy(LDTerms::CampLow));
    AddNotice(5,Self,FGuid::NewGuid(),ELDKillCause::Contender,80000);
    TestEqualSensitive(TEXT("A killer missing from the roster is not given an invented name"),CauseAt(7,60000),LDTerms::KilledBy(TEXT("未列名参赛者")));
    AddNotice(6,Self,FGuid(),ELDKillCause::Unowned,90000);
    TestEqualSensitive(TEXT("A death without a contender owner says so"),CauseAt(7,60000),FString(TEXT("无参赛者击杀")));
    FLDOwnerState Own;Own.LifePhase=ELDLifePhase::Dead;Own.RespawnAtMs=12000;
    const FString Note=LDHUD::Death(Own,true,0).Note;
    TestTrue(TEXT("The death note is the one short glossary line that keeps the build"),Note.Len()<=24&&!Note.Contains(TEXT("；"))&&!Note.Contains(TEXT("\n"))
        &&Note==LDTerms::DeathNote());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDKillFeedSelfTest,"Lockdown.Rules.HUDKillFeedSelf",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDKillFeedSelfTest::RunTest(const FString& Parameters)
{
    const FGuid Self=FGuid::NewGuid(),Rival=FGuid::NewGuid(),Other=FGuid::NewGuid();
    auto Line=[](const FLDKillNotice& Source,const FGuid& Viewer,const TCHAR* VictimName,const TCHAR* KillerName)
    {return LDHUD::KillFeedLine(Source,Viewer,VictimName,KillerName,LDTerms::CampLow,LDTerms::CampHigh,LDTerms::CampElite,LDTerms::Boss);};
    FLDKillNotice Notice;Notice.Cause=ELDKillCause::Contender;Notice.Victim=Rival;Notice.Killer=Self;
    LDHUD::FKillFeedLineView Shown=Line(Notice,Self,TEXT("乙"),TEXT("我"));
    TestTrue(TEXT("The player's own kill says 你 and is highlighted"),Shown.bSelf&&Shown.Text==LDTerms::KillLine(TEXT("你"),TEXT("乙")));
    Notice.Victim=Self;Notice.Killer=Rival;Shown=Line(Notice,Self,TEXT("我"),TEXT("甲"));
    TestTrue(TEXT("The player's own death says 你 and is highlighted"),Shown.bSelf&&Shown.Text==LDTerms::KillLine(TEXT("甲"),TEXT("你")));
    Notice.Cause=ELDKillCause::CampLow;Notice.Killer.Invalidate();Shown=Line(Notice,Self,TEXT("我"),TEXT(""));
    TestTrue(TEXT("A camp death of the player says 你"),Shown.bSelf&&Shown.Text==LDTerms::KilledByLine(TEXT("你"),LDTerms::CampLow));
    Notice.Cause=ELDKillCause::Contender;Notice.Victim=Other;Notice.Killer=Rival;Shown=Line(Notice,Self,TEXT("丙"),TEXT("甲"));
    TestTrue(TEXT("Other contenders keep their roster names and are not highlighted"),!Shown.bSelf&&Shown.Text==LDTerms::KillLine(TEXT("甲"),TEXT("丙")));
    Notice.Victim=Self;
    TestFalse(TEXT("Without an own identity nothing is marked or renamed"),Line(Notice,FGuid(),TEXT("我"),TEXT("甲")).bSelf||Line(Notice,FGuid(),TEXT("我"),TEXT("甲")).Text.Contains(TEXT("你")));
    Notice.Cause=ELDKillCause::CampHigh;Notice.Victim=Other;Notice.Killer=Self;Shown=Line(Notice,Self,TEXT("丙"),TEXT("我"));
    TestTrue(TEXT("Only a contender kill can credit the player"),!Shown.bSelf&&Shown.Text==LDTerms::KilledByLine(TEXT("丙"),LDTerms::CampHigh));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDScoreboardTest,"Lockdown.Rules.HUDScoreboard",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDScoreboardTest::RunTest(const FString& Parameters)
{
    const FGuid Ana=FGuid::NewGuid(),Bo=FGuid::NewGuid(),Cy=FGuid::NewGuid(),Di=FGuid::NewGuid(),Gone=FGuid::NewGuid();
    FLDLobbyView Lobby;Lobby.Generation=3;
    for(const TPair<FGuid,const TCHAR*>& Member:TArray<TPair<FGuid,const TCHAR*>>{{Ana,TEXT("阿娜")},{Bo,TEXT("阿博")},{Cy,TEXT("阿西")},{Di,TEXT("阿迪")}})
    {FLDLobbyMember& Row=Lobby.Members.AddDefaulted_GetRef();Row.ContenderId=Member.Key;Row.Name=Member.Value;}
    Lobby.Members[3].Kind=ELDContenderKind::OpponentAI;
    FLDKillFeedView Feed;Feed.Generation=3;
    Feed.Scores={{Ana,1,2,5},{Bo,3,4,0},{Cy,3,1,1},{Di,1,2,0},{Gone,0,0,0}};
    const TArray<LDHUD::FScoreLine> Lines=LDHUD::Scoreboard(Feed,3,Lobby,Ana);
    if(!TestEqual(TEXT("Every contender of the match has a row"),Lines.Num(),5)){return false;}
    TestEqual(TEXT("Most kills first; the tie goes to fewer deaths"),Lines[0].Name,FString(TEXT("阿西")));
    TestEqual(TEXT("Then the other three-kill contender"),Lines[1].Name,FString(TEXT("阿博")));
    TestEqual(TEXT("Equal kills and deaths keep roster order; the own row says so"),Lines[2].Name,FString(TEXT("阿娜（你）")));
    TestTrue(TEXT("Only the own row is marked as the player"),Lines[2].bSelf&&!Lines[0].bSelf&&!Lines[3].bSelf);
    TestEqual(TEXT("An AI opponent is named as one"),Lines[3].Name,FString(TEXT("阿迪（AI）")));
    TestEqual(TEXT("A contender no longer in the room is shown as having left"),Lines[4].Name,FString(TEXT("（已离开）")));
    TestTrue(TEXT("The row keeps its own kills, deaths and assists"),Lines[2].Kills==1&&Lines[2].Deaths==2&&Lines[2].Assists==5);
    TestTrue(TEXT("Another match's feed shows no scoreboard"),LDHUD::Scoreboard(Feed,4,Lobby,Ana).IsEmpty());
    TestTrue(TEXT("No match shows no scoreboard"),LDHUD::Scoreboard(Feed,0,Lobby,Ana).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDEventFeedTest,"Lockdown.Rules.HUDEventFeed",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDEventFeedTest::RunTest(const FString& Parameters)
{
    const FGuid Self=FGuid::NewGuid(),Rival=FGuid::NewGuid();
    FLDLobbyView Lobby;
    {FLDLobbyMember Member;Member.ContenderId=Self;Member.Name=TEXT("我");Lobby.Members.Add(Member);}
    {FLDLobbyMember Member;Member.ContenderId=Rival;Member.Name=TEXT("甲");Lobby.Members.Add(Member);}
    FLDPublicMatchView Public;Public.Generation=3;Public.Phase=ELDMatchPhase::Development;
    Public.Airdrop.Serial=1;Public.Airdrop.Phase=ELDAirdropPhase::Claimed;Public.Boss.Phase=ELDBossPhase::Pending;
    Public.Leader.Leader=Rival;
    FLDKillFeedView Kills;Kills.Generation=3;
    FLDKillNotice Old;Old.Serial=1;Old.Cause=ELDKillCause::Contender;Old.Victim=Rival;Old.Killer=Self;Old.OccurredAtMs=1000;Kills.Notices.Add(Old);
    LDHUD::FEventFeedInput In;In.Public=&Public;In.Kills=&Kills;In.Lobby=&Lobby;In.Self=Self;In.bClock=true;In.NowMs=20000;In.LocalSeconds=100;
    In.LowCampName=LDTerms::CampLow;In.HighCampName=LDTerms::CampHigh;In.EliteCampName=LDTerms::CampElite;In.BossName=LDTerms::Boss;
    LDHUD::FEventFeed Feed;
    auto Texts=[&Feed](){TArray<FString> Out;for(const LDHUD::FEventLine& Line:Feed.Lines()){Out.Add(Line.Text);}return Out;};
    Feed.Observe(In);
    TestTrue(TEXT("Joining mid-match replays neither old kills nor the state already in place"),Feed.Lines().IsEmpty());
    // A fresh kill, the flagpole opening, a new drop landing, the Boss appearing, a new leader and a new graffiti owner.
    FLDKillNotice Fresh;Fresh.Serial=2;Fresh.Cause=ELDKillCause::Contender;Fresh.Victim=Self;Fresh.Killer=Rival;Fresh.OccurredAtMs=19500;Kills.Notices.Add(Fresh);
    Public.Phase=ELDMatchPhase::CaptureOpen;Public.Airdrop.Serial=2;Public.Airdrop.Phase=ELDAirdropPhase::Landed;Public.Boss.Phase=ELDBossPhase::Alive;
    Public.Leader.Leader=Self;Public.GraffitiOwner=Rival;
    In.LocalSeconds=101;Feed.Observe(In);
    const TArray<FString> Told={LDTerms::KillLine(TEXT("甲"),TEXT("你")),FString(LDTerms::CaptureOpened),FString(LDTerms::AirdropLanded),LDTerms::BossAppeared(LDTerms::Boss),
        LDTerms::LeaderSelf(),LDTerms::GraffitiSprayed(TEXT("甲"))};
    TestTrue(TEXT("Each change is told once, oldest first so the newest sits at the bottom"),Texts()==Told);
    TestTrue(TEXT("Lines about the player are highlighted"),Feed.Lines()[0].bSelf&&!Feed.Lines()[1].bSelf&&Feed.Lines()[4].bSelf&&!Feed.Lines()[5].bSelf);
    In.LocalSeconds=102;Feed.Observe(In);
    TestEqual(TEXT("An unchanged state tells nothing again"),Feed.Lines().Num(),Told.Num());
    // The drop is claimed: everyone hears it is gone until the own claim serial says this screen took it.
    Public.Airdrop.Phase=ELDAirdropPhase::Claimed;In.LocalSeconds=103;Feed.Observe(In);
    TestEqual(TEXT("Beyond the row limit the oldest line leaves early"),Feed.Lines().Num(),LDHUD::FEventFeed::MaxLines);
    TestEqualSensitive(TEXT("A claim without the own serial only says the drop is gone"),Feed.Lines().Last().Text,LDTerms::AirdropTaken());
    In.OwnAirdropClaimSerial=2;In.LocalSeconds=103.5;Feed.Observe(In);
    TestTrue(TEXT("The own claim, arriving later, relabels the shown line"),Feed.Lines().Last().Text==LDTerms::AirdropOwnClaim()&&Feed.Lines().Last().bSelf);
    Public.Boss.Phase=ELDBossPhase::Down;Public.Boss.KilledBy=Rival;In.LocalSeconds=104;Feed.Observe(In);
    TestEqualSensitive(TEXT("A downed Boss names its last hit"),Feed.Lines().Last().Text,LDTerms::BossDownBy(TEXT("甲"),LDTerms::Boss));
    In.LocalSeconds=106.2;Feed.Observe(In);
    TestTrue(TEXT("Each line leaves after its five seconds"),Feed.Lines().Num()==2&&Feed.Lines()[0].Text==LDTerms::AirdropOwnClaim());
    In.LocalSeconds=110;Feed.Observe(In);
    TestTrue(TEXT("All lines are gone once their time is up"),Feed.Lines().IsEmpty());
    // Contest and leaving pause the current owner's guard; only a new owner resets it.
    Public.CircleState=ELDCircleState::Contested;Public.GuardKeptMs=4000;In.LocalSeconds=111;Feed.Observe(In);
    TestTrue(TEXT("A guard freezing is no event line"),Feed.Lines().IsEmpty());
    Public.CircleState=ELDCircleState::Solo;In.LocalSeconds=112;Feed.Observe(In);
    TestTrue(TEXT("Leaving a frozen guard does not announce a reset"),Feed.Lines().IsEmpty());
    In.LocalSeconds=113;Feed.Observe(In);
    TestEqual(TEXT("An unchanged paused guard tells nothing"),Feed.Lines().Num(),0);
    Public.CircleState=ELDCircleState::Guarded;Public.GuardDueAtMs=50000;In.LocalSeconds=114;Feed.Observe(In);
    TestEqual(TEXT("A guard that runs again tells nothing"),Feed.Lines().Num(),0);
    Public.CircleState=ELDCircleState::Empty;Public.GuardDueAtMs=0;Public.GuardKeptMs=4500;In.LocalSeconds=114.5;Feed.Observe(In);
    TestEqual(TEXT("A guard paused by its owner leaving the empty zone is no reset line"),Feed.Lines().Num(),0);
    Public.GraffitiOwner=Self;Public.GuardKeptMs=0;In.LocalSeconds=115;Feed.Observe(In);
    TestTrue(TEXT("A successful repaint announces the new owner once"),
        Feed.Lines().Num()==1&&Feed.Lines()[0].Text==LDTerms::GraffitiSprayed(TEXT("你"))&&Feed.Lines()[0].bSelf);
    // A new match starts empty and does not tell the state it opens with.
    Public.Generation=4;Kills.Generation=4;Kills.Notices.Reset();Public.GraffitiOwner.Invalidate();Public.GuardDueAtMs=0;Public.GuardKeptMs=0;In.LocalSeconds=120;Feed.Observe(In);
    TestTrue(TEXT("A new match starts with an empty feed"),Feed.Lines().IsEmpty());
    // #198 outer §5: a drop expiring unclaimed, the Boss leaving, and camp markers revealed, taken and expiring.
    Public.Airdrop.Serial=3;Public.Airdrop.Phase=ELDAirdropPhase::Expired;Public.Boss.Phase=ELDBossPhase::Left;Public.Boss.KilledBy.Invalidate();
    In.LocalSeconds=121;Feed.Observe(In);
    TestTrue(TEXT("An expired drop and a Boss that left are told"),
        Texts()==TArray<FString>({LDTerms::AirdropExpired(),LDTerms::BossLeft(LDTerms::Boss)}));
    In.LocalSeconds=130;Feed.Observe(In);
    FLDFieldMarkerView HighMark;HighMark.Segment=1;HighMark.Kind=ELDFieldMarkerKind::High;HighMark.CampId=TEXT("h03");
    HighMark.Phase=ELDFieldMarkerPhase::Revealed;HighMark.RevealAtMs=15000;HighMark.EndAtMs=180000;
    FLDFieldMarkerView EliteMark=HighMark;EliteMark.Kind=ELDFieldMarkerKind::Elite;EliteMark.CampId=TEXT("e03");
    Public.Markers={HighMark,EliteMark};In.LocalSeconds=131;Feed.Observe(In);
    TestTrue(TEXT("A segment's reveal tells both markers"),Texts()==TArray<FString>({FString(LDTerms::CampMarkHigh),FString(LDTerms::CampMarkElite)}));
    Public.Markers[0].Phase=ELDFieldMarkerPhase::Claimed;In.LocalSeconds=132;Feed.Observe(In);
    TestTrue(TEXT("A taken marker never names who took it"),Feed.Lines().Last().Text==LDTerms::CampMarkTaken(true)&&!Feed.Lines().Last().bSelf);
    TArray<FLDFieldRewardView> OwnRewards;
    {FLDFieldRewardView Mine;Mine.Event=TEXT("camp_marker_high");Mine.Serial=1;OwnRewards.Add(Mine);}
    In.OwnFieldRewards=&OwnRewards;In.LocalSeconds=132.5;Feed.Observe(In);
    TestTrue(TEXT("The own marker reward, arriving later, relabels the shown line"),
        Feed.Lines().Last().Text==LDTerms::CampMarkOwnClaim(true)&&Feed.Lines().Last().bSelf);
    Public.Markers[1].Phase=ELDFieldMarkerPhase::Expired;In.LocalSeconds=133;Feed.Observe(In);
    TestEqualSensitive(TEXT("An unclaimed marker expiring is told"),Feed.Lines().Last().Text,LDTerms::CampMarkExpired());
    const int32 Shown=Feed.Lines().Num();
    In.LocalSeconds=133.5;Feed.Observe(In);
    TestEqual(TEXT("Marker changes are told once"),Feed.Lines().Num(),Shown);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDChoicesFoldTest,"Lockdown.Rules.HUDChoicesFold",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDChoicesFoldTest::RunTest(const FString& Parameters)
{
    const double Wait=LDHUD::FChoicesFold::AutoOpenSeconds;
    {
        LDHUD::FChoicesFold Fold;
        TestFalse(TEXT("A closed panel never folds"),Fold.ShouldFold(100.,false,true));
        Fold.Opened(true,10.);
        TestTrue(TEXT("The opening's automatic open is marked automatic"),Fold.IsAutomatic());
        TestFalse(TEXT("The automatic open stays while its few seconds run"),Fold.ShouldFold(10.+Wait-.01,false,false));
        TestTrue(TEXT("The automatic open folds back to the pending pill after its few seconds"),Fold.ShouldFold(10.+Wait,false,false));
        Fold.Closed();
        TestFalse(TEXT("A folded panel is closed"),Fold.IsAutomatic()||Fold.ShouldFold(10.+Wait+1.,false,true));
    }
    {
        LDHUD::FChoicesFold Fold;Fold.Opened(true,0.);
        TestTrue(TEXT("An own hit folds the automatic opening at once"),Fold.ShouldFold(1.,false,true));
    }
    {
        LDHUD::FChoicesFold Fold;Fold.Opened(true,0.);
        TestFalse(TEXT("A previewed card or a request in flight keeps the panel open"),Fold.ShouldFold(Wait+1.,true,true));
        TestFalse(TEXT("Engaging makes the automatic open the player's own: neither time nor hits fold it"),
            Fold.IsAutomatic()||Fold.ShouldFold(Wait+60.,false,false)||Fold.ShouldFold(Wait+61.,false,true));
    }
    {
        // #222: the player builds in combat and in a rest through the real UI, so a panel the player opened stays open.
        LDHUD::FChoicesFold Fold;Fold.Opened(false,0.);
        TestFalse(TEXT("A panel the player opened never times out and never folds on a hit"),Fold.IsAutomatic()||Fold.ShouldFold(600.,false,false)
            ||Fold.ShouldFold(601.,false,true));
    }
    {
        // The HUD reads hits from the same own-body changes the world feedback floats: lost health or an absorbed shield.
        LDWorldFeedback::FTracker Hits;FLDCombatOwnerView Combat;Combat.Generation=4;Combat.Life=2;Combat.bHasBody=true;Combat.Revision=1;Combat.HPP2=50000;Combat.MaxHPP2=60000;
        FLDCombatStatusView Shield;Shield.StatusId=uint8(ELDDemoStatusId::Shield);Shield.ExpiresAtMs=90000;Shield.AmountP2=3000;Combat.Statuses.Add(Shield);Combat.ShieldP2=3000;Combat.AsOfMs=1000;
        LDWorldFeedback::FSample Body;Body.Match=4;Body.Life=2;Body.LifePhase=ELDLifePhase::Alive;Body.OwnCombat=&Combat;
        auto Observe=[&Hits,&Body](){TArray<LDWorldFeedback::FEvent> Events;Hits.Observe(Body,Events);return Events;};
        auto IsHit=[](const TArray<LDWorldFeedback::FEvent>& Events){return Events.ContainsByPredicate([](const LDWorldFeedback::FEvent& Event)
            {return Event.bOwn&&(Event.Kind==LDWorldFeedback::EKind::DamageTaken||Event.Kind==LDWorldFeedback::EKind::ShieldAbsorbed);});};
        TestFalse(TEXT("The first own-body sample only primes"),IsHit(Observe()));
        Combat.Revision=2;Combat.ShieldP2=1000;Combat.Statuses[0].AmountP2=1000;Combat.AsOfMs=1500;
        TestTrue(TEXT("A hit fully absorbed by the shield is a hit"),IsHit(Observe()));
        Combat.Revision=3;Combat.ShieldP2=0;Combat.Statuses.Reset();Combat.HPP2=48000;Combat.AsOfMs=2000;
        TestTrue(TEXT("Lost health is a hit"),IsHit(Observe()));
        Combat.Revision=4;Combat.HPP2=48100;Combat.AsOfMs=3000;
        TestFalse(TEXT("Regeneration is not a hit"),IsHit(Observe()));
    }
    return true;
}
// Spec §12 结算“本局最离谱”: one row per contender of the result in its order (at most six), its longest chain as step names and its
// strongest hit as source, points and the share of that target's max HP; nothing for another result.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDUIResultHighlightsTest,"Lockdown.Rules.UI.ResultHighlights",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDUIResultHighlightsTest::RunTest(const FString& Parameters)
{
    FLDResultView Result;Result.Generation=3;Result.ResultId=FGuid(1,2,3,4);
    TArray<FGuid> Ids;
    for(int32 Index=0;Index<LDHUD::ResultHighlightsMax+1;++Index)
    {
        FLDResultRow& Row=Result.Rows.AddDefaulted_GetRef();Row.ContenderId=FGuid(9,Index+1,0,0);Row.Name=FString::Printf(TEXT("P%d"),Index+1);
        Ids.Add(Row.ContenderId);
    }
    FLDDamageSummaryView Summary;Summary.Match=3;Summary.ResultId=Result.ResultId;
    FLDResultHighlight& First=Summary.Highlights.AddDefaulted_GetRef();
    First.Contender=Ids[0];First.LongestKey={FName(TEXT("hud_rules_card")),LDChain::ReactionStep(TEXT("Overload")),LDChain::KillStep()};First.LongestSteps=5;
    First.BestSource=FName(TEXT("basic"));First.BestDamageP2=62000;First.BestTargetMaxHpP2=100000;
    FLDResultHighlight& Second=Summary.Highlights.AddDefaulted_GetRef();Second.Contender=Ids[1];
    const LDHUD::FResultHighlightsView View=LDHUD::ResultHighlights(Summary,Result,Ids[1],nullptr);
    bool bGood=TestTrue(TEXT("Available for the result shown, at most one row per seat in its order"),View.bAvailable&&View.Rows.Num()==LDHUD::ResultHighlightsMax
        &&View.Rows[0].Name==TEXT("P1")&&View.Rows[7].Name==TEXT("P8"));
    bGood&=TestEqualSensitive(TEXT("The longest chain reads its step names and length"),View.Rows[0].Chain,
        FString::Printf(TEXT("hud_rules_card → %s → 击杀（5 步）"),*LDTerms::ReactionTerm(uint8(ELDCombatReaction::Overload))));
    bGood&=TestEqualSensitive(TEXT("The strongest hit reads its source, points and share of max HP"),View.Rows[0].Best,
        FString::Printf(TEXT("%s 620（62%%）"),LDTerms::BasicAttack));
    bGood&=TestTrue(TEXT("A contender without a chain or a hit reads a dash; the own row is marked"),View.Rows[1].Chain==TEXT("—")
        &&View.Rows[1].Best==TEXT("—")&&View.Rows[1].bOwn&&!View.Rows[0].bOwn&&View.Rows[2].Chain==TEXT("—"));
    bGood&=TestEqualSensitive(TEXT("A new chain's prompt"),LDHUD::NewChainNotice(nullptr,{FName(TEXT("a_card")),LDChain::KillStep()}),
        FString(TEXT("新连锁：a_card → 击杀")));
    Summary.ResultId=FGuid(5,6,7,8);
    bGood&=TestFalse(TEXT("Another result's summary shows nothing"),LDHUD::ResultHighlights(Summary,Result,Ids[1],nullptr).bAvailable);
    return bGood;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDBuildGateTest,"Lockdown.Rules.HUDBuildGate",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDBuildGateTest::RunTest(const FString& Parameters)
{
    // #198 growth panel: the disabled reasons follow the server's command gate (a hint only; the server decides each command).
    using EStage=LDProgression::EMatchStage;
    LDHUD::FBuildGateView Gate=LDHUD::BuildGate(EStage::Rest,ELDLifePhase::Alive);
    TestTrue(TEXT("A rest build window allows every build step and rest service"),Gate.bCanChoose&&Gate.bCanLearn&&Gate.bCanAllocate&&Gate.bCanInstall
        &&Gate.bCanShop&&Gate.bCanRespec&&Gate.bCanReplace&&Gate.bCanForget&&Gate.bInRest
        &&Gate.ChooseBlocked.IsEmpty()&&Gate.LearnBlocked.IsEmpty()&&Gate.AllocateBlocked.IsEmpty()&&Gate.ShopBlocked.IsEmpty());
    Gate=LDHUD::BuildGate(EStage::RestLocked,ELDLifePhase::Alive);
    TestTrue(TEXT("#222: the return step keeps the own build open and locks only the rest services"),Gate.bCanChoose&&Gate.bCanLearn&&Gate.bCanAllocate
        &&Gate.bCanInstall&&!Gate.bCanShop&&!Gate.bCanRespec&&!Gate.bCanReplace&&!Gate.bCanForget&&Gate.bInRest);
    TestEqualSensitive(TEXT("The locked reason names the lock"),Gate.ShopBlocked,LDHUD::GrowthReasonText(ELDGrowthReason::BuildLocked));
    Gate=LDHUD::BuildGate(EStage::Rest,ELDLifePhase::Dead);
    TestTrue(TEXT("#222: a dead contender in a rest chooses, learns, allocates and installs; the services wait for a living body"),
        Gate.bCanChoose&&Gate.bCanAllocate&&Gate.bCanLearn&&Gate.bCanInstall&&!Gate.bCanShop&&!Gate.bCanForget);
    Gate=LDHUD::BuildGate(EStage::Live,ELDLifePhase::Alive);
    TestTrue(TEXT("#222: on the field every own build step is open, in combat too"),Gate.bCanChoose&&Gate.bCanLearn&&Gate.bCanAllocate&&Gate.bCanInstall
        &&Gate.LearnBlocked.IsEmpty()&&Gate.AllocateBlocked.IsEmpty());
    TestTrue(TEXT("The field hides the rest services and names why they are refused"),!Gate.bInRest&&!Gate.bCanShop&&!Gate.bCanRespec&&!Gate.bCanReplace&&!Gate.bCanForget
        &&Gate.ShopBlocked==LDHUD::GrowthReasonText(ELDGrowthReason::NotRestWindow)&&Gate.ReplaceBlocked==LDHUD::GrowthReasonText(ELDGrowthReason::ReplacementNotAllowed));
    Gate=LDHUD::BuildGate(EStage::Live,ELDLifePhase::Dead);
    TestTrue(TEXT("#222: a dead contender on the field builds as well"),Gate.bCanChoose&&Gate.bCanAllocate&&Gate.bCanLearn&&Gate.bCanInstall);
    Gate=LDHUD::BuildGate(EStage::Closed,ELDLifePhase::Alive);
    TestTrue(TEXT("Results close every build step with a reason"),!Gate.bCanChoose&&!Gate.bCanLearn&&!Gate.bCanAllocate&&!Gate.bCanInstall
        &&Gate.ChooseBlocked==TEXT("现在不是成长时段"));
    TestFalse(TEXT("An unspawned contender sends nothing"),LDHUD::BuildGate(EStage::Rest,ELDLifePhase::Unspawned).bCanChoose);
    {
        // The stage comes from the public match the same way the server reads it.
        FLDTimelineView Timeline;Timeline.Stage=ELDMatchStage::Rest;Timeline.Step=ELDRestStep::Build;
        TestTrue(TEXT("A rest's build step is the rest build window"),LDProgression::StageForPublic(ELDMatchPhase::Rest,Timeline)==EStage::Rest);
        Timeline.Step=ELDRestStep::Return;
        TestTrue(TEXT("The return countdown locks the build"),LDProgression::StageForPublic(ELDMatchPhase::Rest,Timeline)==EStage::RestLocked);
        TestTrue(TEXT("A field segment is live"),LDProgression::StageForPublic(ELDMatchPhase::Development,FLDTimelineView())==EStage::Live);
        FLDTimelineView FinalField;
        FinalField.Stage=ELDMatchStage::FinalField;
        TestTrue(TEXT("The final field keeps the build adjustable"),
            LDProgression::StageForPublic(ELDMatchPhase::Development,FinalField)==EStage::Live
            && LDProgression::StageForPublic(ELDMatchPhase::CaptureOpen,FinalField)==EStage::Live);
        TestTrue(TEXT("Results are closed"),LDProgression::StageForPublic(ELDMatchPhase::Results,FLDTimelineView())==EStage::Closed);
    }
    TestEqualSensitive(TEXT("No empty slot names the rest-only replacement"),LDHUD::BuildBlockedText(ELDGrowthReason::SlotOccupied),FString(LDTerms::ReplaceAtRest));
    TestEqualSensitive(TEXT("A full Q/W/E says so"),LDHUD::BuildBlockedText(ELDGrowthReason::NoEmptySlot),FString(LDTerms::SlotsFull));

    // Receipt lines: learned into a slot, stored as a blueprint with the reason, or the points added and left.
    FLDBuildReceipt Receipt;Receipt.Header.Outcome=ELDReceiptOutcome::Applied;Receipt.Kind=ELDBuildCommandKind::ActiveChoice;
    Receipt.DefinitionId=TEXT("hud_rules_fixture_bolt");Receipt.ChoiceResult=ELDBuildChoiceResult::Learned;Receipt.LearnedSlot=2;
    TestEqualSensitive(TEXT("A learned choice names its slot"),LDHUD::BuildReceiptText(Receipt,nullptr),FString(TEXT("已学到 E：hud_rules_fixture_bolt")));
    {
        // The slot reads as the key the HUD shows for it (rebound keys, dragged slot order), never the server's letter.
        LDHUD::FSlotKeys Shown;Shown.Keys[0]=TEXT("E");Shown.Keys[1]=TEXT("Z");Shown.Keys[2]=TEXT("W");
        TestEqualSensitive(TEXT("A learned choice names the slot's shown key"),LDHUD::BuildReceiptText(Receipt,nullptr,Shown),FString(TEXT("已学到 W：hud_rules_fixture_bolt")));
        TestEqualSensitive(TEXT("Each server slot reads its shown key"),Shown.Of(0)+Shown.Of(1)+Shown.Of(2),FString(TEXT("EZW")));
        TestEqualSensitive(TEXT("An unset key reads as the server slot"),LDHUD::FSlotKeys().Of(1),FString(TEXT("W")));
    }
    Receipt.ChoiceResult=ELDBuildChoiceResult::StoredAsBlueprint;Receipt.StoredReason=uint16(ELDGrowthReason::NoEmptySlot);
    TestEqualSensitive(TEXT("A full Q/W/E stores the choice"),LDHUD::BuildReceiptText(Receipt,nullptr),FString(TEXT("已存为图纸：hud_rules_fixture_bolt（Q/W/E 已满）")));
    Receipt.ChoiceResult=ELDBuildChoiceResult::None;
    TestEqualSensitive(TEXT("A receipt without a result never claims one"),LDHUD::BuildReceiptText(Receipt,nullptr),FString(TEXT("回执缺少结果，请以当前状态为准。")));
    Receipt.Kind=ELDBuildCommandKind::AllocatePoints;Receipt.PointsSpent={2,0,1,0,0};Receipt.UnspentPointsAfter=3;
    TestEqualSensitive(TEXT("An allocation names the points added and left"),LDHUD::BuildReceiptText(Receipt,nullptr),FString(TEXT("已加 3 点，还剩 3 点")));
    Receipt.Header.Family=ELDGrowthFamily::Build;Receipt.Header.Generation=3;Receipt.Header.OriginalLife=2;Receipt.Header.RequestId=7;
    Receipt.Header.Revisions.bHasBuild=true;Receipt.Header.Revisions.BuildRevision=9;
    TestTrue(TEXT("A confirmed allocation in this life may show success feedback"),LDHUD::AppliedStatFeedback(Receipt,3,2,7,5));
    TestFalse(TEXT("Another life cannot replay allocation feedback"),LDHUD::AppliedStatFeedback(Receipt,3,3,7,5));
    TestFalse(TEXT("Another match cannot replay allocation feedback"),LDHUD::AppliedStatFeedback(Receipt,4,2,7,5));
    TestFalse(TEXT("Another request cannot replay allocation feedback"),LDHUD::AppliedStatFeedback(Receipt,3,2,8,5));
    TestFalse(TEXT("An unassigned request cannot show allocation feedback"),LDHUD::AppliedStatFeedback(Receipt,3,2,0,5));
    Receipt.Header.Outcome=ELDReceiptOutcome::Rejected;
    TestFalse(TEXT("A refused allocation cannot show success feedback"),LDHUD::AppliedStatFeedback(Receipt,3,2,7,5));
    Receipt.Header.Outcome=ELDReceiptOutcome::Applied;
    TestFalse(TEXT("An incomplete allocation cannot show success feedback"),LDHUD::AppliedStatFeedback(Receipt,3,2,7,4));
    Receipt.Header.Revisions.bHasBuild=false;
    TestFalse(TEXT("Feedback waits for a committed build revision"),LDHUD::AppliedStatFeedback(Receipt,3,2,7,5));
    Receipt.Kind=ELDBuildCommandKind::FinalMod;Receipt.DefinitionId=TEXT("final_test");
    Receipt.Header.Revisions.bHasBuild=true;
    TestTrue(TEXT("A matching applied final-mod receipt may start feedback"),LDHUD::AppliedFinalFeedback(Receipt,3,2,7,TEXT("final_test")));
    TestFalse(TEXT("Another life cannot replay final-mod feedback"),LDHUD::AppliedFinalFeedback(Receipt,3,3,7,TEXT("final_test")));
    TestFalse(TEXT("Another request cannot start final-mod feedback"),LDHUD::AppliedFinalFeedback(Receipt,3,2,8,TEXT("final_test")));
    TestFalse(TEXT("Another final mod cannot start feedback"),LDHUD::AppliedFinalFeedback(Receipt,3,2,7,TEXT("final_other")));
    Receipt.Header.Outcome=ELDReceiptOutcome::Rejected;
    TestFalse(TEXT("A refused final mod cannot start feedback"),LDHUD::AppliedFinalFeedback(Receipt,3,2,7,TEXT("final_test")));
    Receipt.Header.Outcome=ELDReceiptOutcome::Applied;Receipt.Header.Revisions.bHasBuild=false;
    TestFalse(TEXT("A final-mod receipt without a build revision cannot start feedback"),LDHUD::AppliedFinalFeedback(Receipt,3,2,7,TEXT("final_test")));
    FLDProgressionView FinalView;FinalView.bValid=true;FinalView.BuildRevision=8;
    FLDFinalModSelection Final;Final.DefinitionId=TEXT("final_test");
    FinalView.FinalMod=Final;
    TestFalse(TEXT("A final stamp waits for the confirmed revision"),LDHUD::FinalStampReady(FinalView,9,Final,true));
    FinalView.BuildRevision=9;
    TestFalse(TEXT("A final stamp waits for the board"),LDHUD::FinalStampReady(FinalView,9,Final,false));
    TestTrue(TEXT("The confirmed selection stamps when its board and revision are ready"),LDHUD::FinalStampReady(FinalView,9,Final,true));
    FinalView.FinalMod.DefinitionId=TEXT("final_other");
    TestFalse(TEXT("Another card cannot replay the previous stamp"),LDHUD::FinalStampReady(FinalView,9,Final,true));
    TestFalse(TEXT("A view alone cannot start a stamp"),LDHUD::FinalStampReady(FinalView,0,Final,true));
    Receipt.Kind=ELDBuildCommandKind::SkipActiveOffer;Receipt.PointsDelta=1;
    TestEqualSensitive(TEXT("A skipped offer names the stat points paid"),LDHUD::BuildReceiptText(Receipt,nullptr),LDTerms::PointsGained(1));
    Receipt.PointsDelta=0;
    TestEqualSensitive(TEXT("A skip receipt without its award claims nothing"),LDHUD::BuildReceiptText(Receipt,nullptr),FString(TEXT("回执缺少结果，请以当前状态为准。")));
    Receipt.Header.Outcome=ELDReceiptOutcome::Rejected;Receipt.Header.Reason=uint16(ELDGrowthReason::NotEnoughPoints);
    TestEqualSensitive(TEXT("A refused command names its reason"),LDHUD::BuildReceiptText(Receipt,nullptr),FString(TEXT("没有生效：未分配点数不够")));
    {
        // #198 rest services: each applied receipt names what it did; a missing field never reads as a success.
        FLDBuildReceipt Rest;Rest.Header.Outcome=ELDReceiptOutcome::Applied;
        Rest.Kind=ELDBuildCommandKind::Respec;Rest.PointsRefunded=5;Rest.UnspentPointsAfter=7;
        TestEqualSensitive(TEXT("A respec names the refund and the unspent points"),LDHUD::BuildReceiptText(Rest,nullptr),FString(TEXT("已洗点：返还 5 点，现有 7 点未分配")));
        Rest.Kind=ELDBuildCommandKind::ReplaceActive;Rest.ChoiceResult=ELDBuildChoiceResult::Learned;Rest.LearnedSlot=1;
        Rest.DefinitionId=TEXT("hud_rules_fixture_bolt");Rest.ForgottenId=TEXT("hud_rules_fixture_old");
        TestEqualSensitive(TEXT("A replacement names the slot, the new and the forgotten active"),LDHUD::BuildReceiptText(Rest,nullptr),
            FString(TEXT("已在 W 用 hud_rules_fixture_bolt 替换 hud_rules_fixture_old")));
        Rest.ForgottenId=NAME_None;
        TestEqualSensitive(TEXT("A replacement without the forgotten active claims nothing"),LDHUD::BuildReceiptText(Rest,nullptr),FString(TEXT("回执缺少结果，请以当前状态为准。")));
        Rest.Kind=ELDBuildCommandKind::ForgetActive;Rest.ForgottenId=TEXT("hud_rules_fixture_old");
        TestEqualSensitive(TEXT("A forget names the forgotten active"),LDHUD::BuildReceiptText(Rest,nullptr),LDTerms::Forgotten(TEXT("hud_rules_fixture_old")));
        Rest.Header.Outcome=ELDReceiptOutcome::Rejected;Rest.Header.Reason=uint16(ELDGrowthReason::ShopSoldOut);
        TestEqualSensitive(TEXT("A refused rest service names its reason"),LDHUD::BuildReceiptText(Rest,nullptr),
            FString(TEXT("没有生效："))+LDHUD::GrowthReasonText(ELDGrowthReason::ShopSoldOut));
        const ELDGrowthReason RestReasons[]={ELDGrowthReason::ReplacementNotAllowed,ELDGrowthReason::BuildLocked,
            ELDGrowthReason::OfferOutstanding,ELDGrowthReason::NothingToRespec,ELDGrowthReason::SlotEmpty,ELDGrowthReason::NotRestWindow,
            ELDGrowthReason::ShopSoldOut,ELDGrowthReason::ShopStale,ELDGrowthReason::SkillPointLimit,ELDGrowthReason::NoShop};
        TSet<FString> Seen;
        for(const ELDGrowthReason Reason:RestReasons)
        {
            const FString Text=LDHUD::GrowthReasonText(Reason);bool bDuplicate=false;Seen.Add(Text,&bDuplicate);
            TestTrue(FString::Printf(TEXT("Rest reason %d has its own player text"),int32(Reason)),!Text.IsEmpty()&&!Text.StartsWith(TEXT("未知原因"))&&!bDuplicate);
        }
    }

    // The 构筑 button is lit by what the tray has cards for: the shown offer and learnable blueprints of the current match's
    // own view; unspent points wait on the HUD's stat icons instead.
    FLDProgressionView View;View.bValid=true;View.Generation=5;View.QueuedActiveOpportunities=2;View.UnspentPoints=3;
    LDHUD::FGrowthPending Pending=LDHUD::GrowthPending(&View,5,nullptr);
    TestTrue(TEXT("Queued opportunities without a shown offer have no card to light the button"),!Pending.Any()&&Pending.Offers==2);
    View.bHasActiveOffer=true;View.ActiveOffer.Options={FName(TEXT("active_elbow")),FName(TEXT("active_bolt"))};
    Pending=LDHUD::GrowthPending(&View,5,nullptr);
    TestTrue(TEXT("A shown offer lights the button and keeps the queue count for the pulse"),Pending.Any()&&Pending.bOffer&&Pending.Offers==2);
    View.bHasActiveOffer=false;View.ActiveOffer.Options.Reset();View.QueuedActiveOpportunities=0;
    TestFalse(TEXT("Unspent points alone do not light the button"),LDHUD::GrowthPending(&View,5,nullptr).Any());
    View.bHasActiveOffer=true;View.ActiveOffer.Options={FName(TEXT("active_elbow"))};
    TestFalse(TEXT("Another match's view lights nothing"),LDHUD::GrowthPending(&View,4,nullptr).Any()||LDHUD::GrowthPending(nullptr,5,nullptr).Any());
    View.bHasActiveOffer=false;View.ActiveOffer.Options.Reset();View.UnspentPoints=0;View.PendingBlueprints={FName(TEXT("active_elbow"))};
    Pending=LDHUD::GrowthPending(&View,5,nullptr);
    TestTrue(TEXT("A blueprint with an empty slot lights it alone"),Pending.Any()&&Pending.Blueprints==1);
    for(uint8 Key=0;Key<3;++Key){FLDActiveSlotView& Held=View.Actives.AddDefaulted_GetRef();Held.Slot=Key;Held.DefinitionId=FName(*FString::Printf(TEXT("held_%d"),Key));}
    TestFalse(TEXT("A blueprint waiting for the rest-only replacement does not light it"),LDHUD::GrowthPending(&View,5,nullptr).Any());
    TestTrue(TEXT("The slot reads come from the view"),LDHUD::ActiveInSlot(View,1)==FName(TEXT("held_1"))&&LDHUD::SlotKey(1)==TEXT("W"));
    FLDStatPointsView& Physique=View.StatPoints.AddDefaulted_GetRef();Physique.StatId=TEXT("stat_physique");Physique.Points=4;
    TestTrue(TEXT("Stat points come from the view, 0 for an unlisted stat"),LDHUD::StatPointsOf(View,TEXT("stat_physique"))==4&&LDHUD::StatPointsOf(View,TEXT("stat_brains"))==0);

    // D10 auto-open: once per match, for the first view of the opening offer (opportunity 1); later offers and points never.
    LDHUD::FGrowthAutoOpen AutoOpen;FLDProgressionView Growth;Growth.bValid=true;Growth.Generation=7;Growth.UnspentPoints=3;
    TestFalse(TEXT("Points without the opening offer open nothing"),AutoOpen.Observe(Growth,7));
    Growth.bHasActiveOffer=true;Growth.ActiveOffer.OfferId=FGuid::NewGuid();Growth.ActiveOffer.OpportunityOrdinal=1;
    TestTrue(TEXT("The first view with the opening offer opens the panel"),AutoOpen.Observe(Growth,7));
    TestFalse(TEXT("The same view does not open it again"),AutoOpen.Observe(Growth,7));
    Growth.bHasActiveOffer=false;Growth.UnspentPoints=6;
    TestFalse(TEXT("Newly earned points open nothing"),AutoOpen.Observe(Growth,7));
    Growth.bHasActiveOffer=true;Growth.ActiveOffer.OfferId=FGuid::NewGuid();Growth.ActiveOffer.OpportunityOrdinal=2;
    TestFalse(TEXT("A later offer opens nothing; it only lights the button"),AutoOpen.Observe(Growth,7));
    TestFalse(TEXT("A view of another match opens nothing"),AutoOpen.Observe(Growth,8));
    Growth.Generation=8;Growth.ActiveOffer.OpportunityOrdinal=1;Growth.ActiveOffer.OfferId=FGuid::NewGuid();
    TestTrue(TEXT("A new match's opening offer opens it again"),AutoOpen.Observe(Growth,8));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDPendingPagesTest,"Lockdown.Rules.HUDPendingPages",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDPendingPagesTest::RunTest(const FString& Parameters)
{
    const TArray<float> Decisions={90.f,90.f,90.f};
    const LDWorkshop::FPendingFit Single=LDWorkshop::FitPending(Decisions,270.f,20.f);
    TestTrue(TEXT("A fitting tray stays on one page"),Single.bFits&&Single.Pages.Num()==1
        &&Single.Pages[0].First==0&&Single.Pages[0].Count==3);
    const LDWorkshop::FPendingFit Paged=LDWorkshop::FitPending(Decisions,200.f,20.f);
    TestTrue(TEXT("Paging reserves the controls and keeps each decision whole"),Paged.bFits&&Paged.Pages.Num()==2
        &&Paged.Pages[0].First==0&&Paged.Pages[0].Count==2&&Paged.Pages[1].First==2&&Paged.Pages[1].Count==1);
    const LDWorkshop::FPendingFit Empty=LDWorkshop::FitPending({},0.f,20.f);
    TestTrue(TEXT("No decisions produce one empty page"),Empty.bFits&&Empty.Pages.Num()==1&&Empty.Pages[0].Count==0);
    const LDWorkshop::FPendingFit TooSmall=LDWorkshop::FitPending({181.f,90.f},200.f,20.f);
    TestTrue(TEXT("A decision that cannot fit is reported, not split"),!TooSmall.bFits&&TooSmall.Pages.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDWorkshopPreviewTest,"Lockdown.Rules.HUDWorkshopPreview",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDWorkshopPreviewTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if(!LDTestApprovedFixture::Read(*this,Catalog)){return false;}
    const FLDDemoContentDefinitions& Content=Catalog->Get();
    FName Table;
    if(!HudRegisterApprovedNames(*this,Content,Table)){return false;}
    ON_SCOPE_EXIT{if(!Table.IsNone()){FStringTableRegistry::Get().UnregisterStringTable(Table);}};

    FLDProgressionView View;View.bValid=true;View.Generation=7;View.Life=3;View.LifePhase=ELDLifePhase::Dead;
    View.Level=1;View.BuildRevision=12;View.CombatInstalledRevision=11;View.CatalogDigest=Content.Identity.Root;
    FLDActiveSlotView& Active=View.Actives.AddDefaulted_GetRef();Active.DefinitionId=TEXT("active_pants_down");
    // A stored K08 (cooldown x0.65, cost x1.5) and a stored K16 (a conditional idle bonus), both skill cards.
    FLDCardInstanceView& Quick=View.Cards.AddDefaulted_GetRef();Quick.Serial=1;Quick.DefinitionId=TEXT("card_again_now");
    FLDCardInstanceView& Held=View.Cards.AddDefaulted_GetRef();Held.Serial=2;Held.DefinitionId=TEXT("card_held_shot");
    const FLDGrowthBuild Build=LDProgression::BuildFromView(Content,View);
    const FLDAttributeProjectionResult Projected=LDCombatSourceRules::ProjectBuildInput(
        LDProgression::CombatProjectionInput(&Content,Build),*Catalog);
    if(!TestTrue(TEXT("The current build projects"),Projected.IsSuccess())){return false;}
    FLDCombatOwnerView Combat;Combat.Generation=7;Combat.Life=3;Combat.InstalledBuildRevision=11;Combat.AsOfMs=1000;
    for(const FLDAttributeContributions& Contribution:Projected.Value->Contributions)
    {
        FLDCombatAttributeRowView& Row=Combat.Attributes.AddDefaulted_GetRef();
        Row.Attribute=uint8(Contribution.Attribute);Row.Final=Contribution.Final;
    }
    LDWorkshopPreview::FInput Input;Input.Content=Catalog.Get();Input.Progression=&View;Input.Combat=&Combat;
    Input.Match=7;Input.Stage=LDProgression::EMatchStage::Live;Input.bClock=true;Input.NowMs=1000;
    Input.Component=LDHUD::CardKey(Quick.DefinitionId,Quick.Serial);
    Input.Drop.Column=LDHUD::EStorageColumn::SlotQ;Input.Drop.Kind=ELDCardTargetKind::Skill;Input.Drop.TargetId=Active.DefinitionId;
    using EStatus=LDWorkshopPreview::EStatus;
    const LDWorkshopPreview::FResult Preview=LDWorkshopPreview::Compare(Input);
    TestTrue(TEXT("K08 compares the compiled cooldown without claiming a conditional effect"),
        Preview.Status==EStatus::Ready&&!Preview.bPartial
        &&Preview.Lines.ContainsByPredicate([](const LDWorkshopPreview::FLine& Line)
            {return Line.Label.Contains(LDTerms::WorkshopCooldown)&&Line.Before==TEXT("6.3s")&&Line.After==TEXT("4.095s");}));
    ++View.CombatInstalledRevision;
    TestTrue(TEXT("Mismatched installed revisions wait for synchronization"),LDWorkshopPreview::Compare(Input).Status==EStatus::Synchronizing);
    --View.CombatInstalledRevision;
    View.CatalogDigest.Bytes[0]^=0xFF;
    TestTrue(TEXT("A different catalog waits for synchronization"),LDWorkshopPreview::Compare(Input).Status==EStatus::Synchronizing);
    View.CatalogDigest=Content.Identity.Root;
    Input.Drop.Elements={ELDDemoElement::Fire,ELDDemoElement::Water};
    TestTrue(TEXT("An unresolved element binding asks for a choice"),LDWorkshopPreview::Compare(Input).Status==EStatus::ChoiceRequired);
    Input.Drop.Elements.Reset();
    Input.Component=LDHUD::CardKey(Held.DefinitionId,Held.Serial);
    const LDWorkshopPreview::FResult Conditional=LDWorkshopPreview::Compare(Input);
    TestTrue(TEXT("Target-dependent effects are explicitly marked partial"),
        Conditional.Status==EStatus::Ready&&Conditional.bPartial&&Conditional.Lines.IsEmpty());

    View.LifePhase=ELDLifePhase::Alive;
    Input.Component=LDHUD::CardKey(Quick.DefinitionId,Quick.Serial);
    Combat.bPreviewLiving=true;
    Combat.MaxHPP2=Projected.Value->Final.MaxHPP2;Combat.HPP2=Combat.MaxHPP2/2;
    Combat.MaxMomentumP2=10000;Combat.MomentumP2=5000;
    FLDDemoStatBonus Haste;Haste.Stat=ELDDemoBonusStat::CooldownHasteFlat;Haste.Amount=1500;
    FLDCombatPreviewBonus& Bonus=Combat.PreviewBonuses.AddDefaulted_GetRef();
    Bonus.Stats.Add(Haste);Bonus.ExpiresAtMs=2000;
    FLDTemporaryAttributeTerms Temporary;Temporary.Bonuses.Add(Haste);
    const FLDAttributeProjectionResult Effective=LDAttributeProjection::ApplyTemporary(Projected.Value.GetValue(),Temporary,*Catalog);
    if(!TestTrue(TEXT("The living owner projects its temporary haste"),Effective.IsSuccess())){return false;}
    Combat.Attributes.Reset();
    for(const FLDAttributeContributions& Contribution:Effective.Value->Contributions)
    {
        FLDCombatAttributeRowView& Row=Combat.Attributes.AddDefaulted_GetRef();
        Row.Attribute=uint8(Contribution.Attribute);Row.Final=Contribution.Final;
    }
    int64 OldCooldown=0,NewCooldown=0;FString Error;
    if(!TestTrue(TEXT("Both expected cooldowns use the authority formula"),
        LDTactical::CooldownDurationMs(6300,Effective.Value->Final.CooldownHasteP2,Content.Formulas.HasteScaleP2,OldCooldown,Error)
        &&LDTactical::CooldownDurationMs(4095,Effective.Value->Final.CooldownHasteP2,Content.Formulas.HasteScaleP2,NewCooldown,Error)))
    {return false;}
    const LDWorkshopPreview::FResult Live=LDWorkshopPreview::Compare(Input);
    const LDWorkshopPreview::FLine* Cooldown=Live.Lines.FindByPredicate([](const LDWorkshopPreview::FLine& Line)
        {return Line.Label.Contains(LDTerms::WorkshopCooldown);});
    TestTrue(TEXT("A living owner's temporary haste is reflected in the preview"),
        Live.Status==EStatus::Ready&&!Live.bPartial&&Cooldown
        &&FMath::IsNearlyEqual(FCString::Atod(*Cooldown->Before),double(OldCooldown)/1000.,.0001)
        &&FMath::IsNearlyEqual(FCString::Atod(*Cooldown->After),double(NewCooldown)/1000.,.0001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDFieldRewardNoticesTest,"Lockdown.Rules.HUDFieldRewardNotices",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDFieldRewardNoticesTest::RunTest(const FString& Parameters)
{
    // #198 G: each field reward names its event, XP and what its build part gave; the watch tells each new entry once.
    const auto Reward=[](const TCHAR* Event,uint64 Serial,int64 XP,ELDFieldRewardResult Result,const TCHAR* Definition)
    {FLDFieldRewardView View;View.Event=Event;View.Serial=Serial;View.XP=XP;View.Result=Result;View.Definition=Definition;return View;};
    const FString XP=FString::Printf(TEXT("+200 %s"),LDTerms::Experience);
    TestEqualSensitive(TEXT("An airdrop blueprint names the event, the XP and the blueprint"),
        LDHUD::FieldRewardNotice(Reward(TEXT("airdrop"),1,200,ELDFieldRewardResult::Blueprint,TEXT("hud_fixture_bolt")),nullptr),
        FString(LDTerms::Airdrop)+TEXT("：")+XP+TEXT("，")+LDTerms::FieldRewardBlueprint(TEXT("hud_fixture_bolt")));
    TestEqualSensitive(TEXT("A Boss card offer without XP readies its three-choice"),
        LDHUD::FieldRewardNotice(Reward(TEXT("boss"),1,0,ELDFieldRewardResult::CardOffer,TEXT("")),nullptr),
        FString(LDTerms::Boss)+TEXT("：")+LDTerms::FieldRewardCards());
    TestEqualSensitive(TEXT("A high camp marker blueprint"),
        LDHUD::FieldRewardNotice(Reward(TEXT("camp_marker_high"),3,0,ELDFieldRewardResult::Blueprint,TEXT("hud_fixture_bolt")),nullptr),
        FString(LDTerms::FieldRewardHighCamp)+TEXT("：")+LDTerms::FieldRewardBlueprint(TEXT("hud_fixture_bolt")));
    TestEqualSensitive(TEXT("An elite camp marker readies its card three-choice"),
        LDHUD::FieldRewardNotice(Reward(TEXT("camp_marker_elite"),4,0,ELDFieldRewardResult::CardOffer,TEXT("")),nullptr),
        FString(LDTerms::FieldRewardEliteCamp)+TEXT("：")+LDTerms::FieldRewardCards());
    TestEqualSensitive(TEXT("An empty active pool keeps the blueprint for later"),
        LDHUD::FieldRewardNotice(Reward(TEXT("camp_marker_high"),5,0,ELDFieldRewardResult::BlueprintHeld,TEXT("")),nullptr),
        FString(LDTerms::FieldRewardHighCamp)+TEXT("：")+LDTerms::FieldRewardBlueprintHeld());
    TestEqualSensitive(TEXT("XP alone is still told"),LDHUD::FieldRewardNotice(Reward(TEXT("airdrop"),7,200,ELDFieldRewardResult::None,TEXT("")),nullptr),
        FString(LDTerms::Airdrop)+TEXT("：")+XP);
    TestTrue(TEXT("A blueprint result without its definition never claims a name"),
        !LDHUD::FieldRewardNotice(Reward(TEXT("airdrop"),8,0,ELDFieldRewardResult::Blueprint,TEXT("")),nullptr).Contains(TEXT("「")));
    TestTrue(TEXT("An unknown event is named as unknown, never as another event"),LDHUD::FieldRewardEventName(TEXT("hud_fixture_event")).StartsWith(TEXT("未知事件")));
    TestTrue(TEXT("Nothing to tell is empty"),LDHUD::FieldRewardNotice(Reward(TEXT("airdrop"),9,0,ELDFieldRewardResult::None,TEXT("")),nullptr).IsEmpty());

    // New entries since a known count, oldest first.
    FLDProgressionView View;View.bValid=true;View.Generation=3;
    View.FieldRewards={Reward(TEXT("airdrop"),1,200,ELDFieldRewardResult::Blueprint,TEXT("a")),Reward(TEXT("boss"),1,0,ELDFieldRewardResult::CardOffer,TEXT(""))};
    TArray<FLDFieldRewardView> Fresh=LDHUD::NewFieldRewards(View,1);
    TestTrue(TEXT("Entries past the known count"),Fresh.Num()==1&&Fresh[0].Event==FName(TEXT("boss")));
    TestTrue(TEXT("Nothing past the end"),LDHUD::NewFieldRewards(View,2).IsEmpty()&&LDHUD::NewFieldRewards(View,5).IsEmpty());
    TestEqual(TEXT("A negative count reads from the start"),LDHUD::NewFieldRewards(View,-1).Num(),2);

    // The watch: the first view of a match primes, later entries are told once, another match's view tells nothing.
    LDHUD::FFieldRewardWatch Watch;
    TestTrue(TEXT("The first view primes and replays nothing"),Watch.Observe(&View,3).IsEmpty());
    View.FieldRewards.Add(Reward(TEXT("camp_marker_elite"),2,0,ELDFieldRewardResult::CardOffer,TEXT("")));
    Fresh=Watch.Observe(&View,3);
    TestTrue(TEXT("A new entry is told"),Fresh.Num()==1&&Fresh[0].Event==FName(TEXT("camp_marker_elite"))&&Fresh[0].Serial==2);
    TestTrue(TEXT("The same view tells it only once"),Watch.Observe(&View,3).IsEmpty());
    View.FieldRewards.Add(Reward(TEXT("airdrop"),3,200,ELDFieldRewardResult::Blueprint,TEXT("c")));
    TestTrue(TEXT("A view of another match tells nothing"),Watch.Observe(&View,4).IsEmpty()&&Watch.Observe(nullptr,3).IsEmpty());
    TestEqual(TEXT("The entry is still told for its own match"),Watch.Observe(&View,3).Num(),1);
    View.Generation=4;
    TestTrue(TEXT("A new match primes again"),Watch.Observe(&View,4).IsEmpty());

    // The 构筑 button is lit by a pending card offer.
    FLDProgressionView Pending;Pending.bValid=true;Pending.Generation=5;
    Pending.bHasCardOffer=true;Pending.CardOffer.OfferId=FGuid(1,2,3,4);Pending.QueuedCardOffers=2;
    Pending.CardOffer.Options={FName(TEXT("hud_fixture_a")),FName(TEXT("hud_fixture_b")),FName(TEXT("hud_fixture_c"))};
    const LDHUD::FGrowthPending Counts=LDHUD::GrowthPending(&Pending,5,nullptr);
    TestTrue(TEXT("A pending card offer lights the button and counts the queue"),Counts.Any()&&Counts.bCardOffer&&Counts.CardOffers==2);
    TestFalse(TEXT("Another match's view lights nothing"),LDHUD::GrowthPending(&Pending,6,nullptr).Any());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDDevTestPanelTest,"Lockdown.Rules.HUDDevTestPanel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDDevTestPanelTest::RunTest(const FString& Parameters)
{
    if(!HudRegisterFixtureTable(*this)){return false;}
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(HudFixtureTable);};
    // Entries are exact: a malformed amount or value is refused locally and sends nothing.
    int64 Value=0;
    TestTrue(TEXT("A positive amount parses exactly"),LDDevTest::ParseAmount(TEXT(" 120 "),Value)&&Value==120);
    int32 AmountRefusals=0;
    for(const TCHAR* Bad:{TEXT(""),TEXT("0"),TEXT("-5"),TEXT("1.5"),TEXT("12a"),TEXT("1234567890123456789")}){AmountRefusals+=LDDevTest::ParseAmount(Bad,Value)?0:1;}
    TestEqual(TEXT("Empty, zero, signed, fractional, non-digit and over-long amounts are refused"),AmountRefusals,6);
    TestTrue(TEXT("Two decimals scale to P2 / basis points"),LDDevTest::ParseScaled(TEXT("12.5"),2,Value)&&Value==1250&&LDDevTest::ParseScaled(TEXT("7"),2,Value)&&Value==700);
    TestTrue(TEXT("Whole centimetres scale by one"),LDDevTest::ParseScaled(TEXT("40"),0,Value)&&Value==40&&LDDevTest::ParseScaled(TEXT("0"),2,Value)&&Value==0);
    int32 ValueRefusals=0;
    for(const TPair<const TCHAR*,int32>& Bad:{TPair<const TCHAR*,int32>(TEXT("1.234"),2),{TEXT("1.5"),0},{TEXT("-1"),2},{TEXT(".5"),2},{TEXT("5."),2},{TEXT("1e3"),2}})
    {ValueRefusals+=LDDevTest::ParseScaled(Bad.Key,Bad.Value,Value)?0:1;}
    TestEqual(TEXT("Extra decimals, signs, missing digits and exponents are refused, never rounded"),ValueRefusals,6);
    // One settable field per permanent build stat, each shown in its projected attribute row.
    const TConstArrayView<LDDevTest::FStatField> Fields=LDDevTest::StatFields();
    TSet<uint8> Stats;TSet<FString> Names;bool bRowsValid=true;
    for(const LDDevTest::FStatField& Field:Fields)
    {
        Stats.Add(Field.Stat);Names.Add(Field.ConsoleName);
        bRowsValid&=Field.Attribute<uint8(ELDProjectedAttribute::Count)&&Field.Stat!=uint8(ELDDemoBonusStat::ADBonusRatio);
    }
    TestTrue(TEXT("Ten distinct permanent stats, never the conditional AD ratio"),Fields.Num()==10&&Stats.Num()==10&&Names.Num()==10&&bRowsValid);
    const LDDevTest::FStatField* Crit=Fields.FindByPredicate([](const LDDevTest::FStatField& F){return F.Stat==uint8(ELDDemoBonusStat::CritChanceBonusRatio);});
    const LDDevTest::FStatField* Range=Fields.FindByPredicate([](const LDDevTest::FStatField& F){return F.Stat==uint8(ELDDemoBonusStat::AttackRangeCmFlat);});
    TestTrue(TEXT("Fields name the stat in player terms with the entry unit"),Crit&&Range&&LDDevPanel::StatLabel(*Crit)==LDTerms::CritChance()+TEXT("（百分点）")
        &&LDDevPanel::StatLabel(*Range).EndsWith(TEXT("（厘米）")));

    // Catalog rows: every active, with what the own progression holds of it (#198).
    FLDDemoContentDefinitions Content=HudFixtureContent();
    for(const TCHAR* Id:{TEXT("root"),TEXT("shield"),TEXT("camp_returning")}){FLDDemoAbilityDefinition Active;Active.Id=Id;Content.Abilities.Add(Active);}
    FLDProgressionView Progression;Progression.bValid=true;Progression.Level=3;Progression.CumulativeXP=450;Progression.UnspentPoints=4;
    Progression.QueuedActiveOpportunities=1;
    FLDActiveSlotView Learned;Learned.DefinitionId=TEXT("root");Learned.Slot=1;Progression.Actives.Add(Learned);
    Progression.PendingBlueprints={FName(TEXT("shield"))};
    const TArray<LDDevPanel::FActiveRow> Rows=LDDevPanel::ActiveRows(Content,&Progression);
    TestTrue(TEXT("Every active of the catalog is listed in order"),Rows.Num()==3&&Rows[0].Name==HudName(TEXT("root"))&&Rows[1].Id==FName(TEXT("shield")));
    TestTrue(TEXT("What is held comes from the own progression: a learned slot, a blueprint or nothing"),LDDevPanel::HeldText(Rows[0])==TEXT("已学 W")
        &&LDDevPanel::HeldText(Rows[1])==LDTerms::Blueprint&&LDDevPanel::HeldText(Rows[2])==TEXT("未拥有"));
    TestTrue(TEXT("Without a projection nothing is held"),LDDevPanel::ActiveRows(Content,nullptr)[0].Held==LDDevPanel::EActiveHeld::None);
    TestFalse(TEXT("One held active does not need a replacement"),LDDevPanel::AllActiveSlotsHeld(&Progression));
    Learned.Slot=0;Learned.DefinitionId=TEXT("other_active");Progression.Actives.Add(Learned);
    TestFalse(TEXT("Q and W leave E free"),LDDevPanel::AllActiveSlotsHeld(&Progression));
    Learned.Slot=2;Learned.DefinitionId=TEXT("third_active");Progression.Actives.Add(Learned);
    TestTrue(TEXT("Q, W and E held offer the DEV replacement"),LDDevPanel::AllActiveSlotsHeld(&Progression));
    TestFalse(TEXT("An unprojected progression offers nothing"),LDDevPanel::AllActiveSlotsHeld(nullptr));

    // Own state lines: an unprojected view reads as syncing, never as 0.
    TestEqualSensitive(TEXT("Nothing projected is syncing"),LDDevPanel::StatusLine(nullptr),FString(TEXT("等级 同步中")));
    TestEqualSensitive(TEXT("Level, XP, unspent points and queued active opportunities"),LDDevPanel::StatusLine(&Progression),
        FString::Printf(TEXT("等级 3 · %s 450 · 未分配点数 4 · 待选技能 1 次"),LDTerms::Experience));
    TestEqualSensitive(TEXT("An attribute without a projected row is syncing"),LDDevPanel::AttributeLine(nullptr,uint8(ELDProjectedAttribute::AD)),FString(TEXT("同步中")));
    FLDCombatOwnerView Combat;FLDCombatAttributeRowView AD;AD.Attribute=uint8(ELDProjectedAttribute::AD);
    for(FLDDemoRational* Term:{&AD.Base,&AD.Level,&AD.Choices,&AD.PercentBonus,&AD.Dev,&AD.DevPercent,&AD.Temporary,&AD.TemporaryPercent,&AD.Final})
    {Term->Numerator=0;Term->Denominator=1;}
    AD.Base.Numerator=50;AD.Dev.Numerator=12;AD.Final.Numerator=62;Combat.Attributes.Add(AD);
    const FString ADLine=LDDevPanel::AttributeLine(&Combat,uint8(ELDProjectedAttribute::AD));
    TestTrue(TEXT("The committed final value shows with the test layer among its sources"),ADLine.StartsWith(TEXT("当前 62"))&&ADLine.Contains(TEXT("测试")));

    // The authority's answer, never a guessed success.
    FLDDevTestResult Result;Result.Status=ELDAdmissionStatus::AdmissionRejected;Result.Reason=uint16(ELDGrowthReason::NotAuthority);
    TestTrue(TEXT("An admission refusal names its reason"),LDDevPanel::ResultLine(Result).StartsWith(TEXT("请求被拒绝（")));
    Result.Status=ELDAdmissionStatus::Final;Result.bHasReceipt=true;Result.Receipt.Op=ELDDevTestOp::GrantExperience;Result.Receipt.Header.Outcome=ELDReceiptOutcome::Applied;
    TestEqualSensitive(TEXT("An applied receipt says so"),LDDevPanel::ResultLine(Result),FString(TEXT("加"))+LDTerms::Experience+TEXT(" 已生效"));
    Result.Receipt.Header.Outcome=ELDReceiptOutcome::Rejected;Result.Receipt.Header.Reason=uint16(ELDGrowthReason::DataNotReady);
    TestTrue(TEXT("A refused receipt names the op and its reason"),LDDevPanel::ResultLine(Result).StartsWith(FString(TEXT("加"))+LDTerms::Experience+TEXT(" 被拒绝（")));
    Result.Status=ELDAdmissionStatus::AcceptedPending;Result.bHasReceipt=false;
    TestTrue(TEXT("An admitted command waits for its commit"),LDDevPanel::ResultLine(Result).StartsWith(TEXT("已受理")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDIconsTest,"Lockdown.Rules.HUDIcons",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDIconsTest::RunTest(const FString& Parameters)
{
    // #85: every icon the HUD and the growth panel draw is an imported texture: each active, stat and status of the approved
    // catalog (only the character and the creatures have no icon square), plus the dash and the two recoveries, which have no
    // display row.
    TestTrue(TEXT("Icon packages live in the cooked UI icon folder"),LDUIIcons::PackageFor(TEXT("icon_root"))==TEXT("/Game/Lockdown/Art/UI/Icons/T_icon_root"));
    TestTrue(TEXT("A malformed icon id maps to no package"),LDUIIcons::PackageFor(TEXT("root")).IsEmpty()&&LDUIIcons::PackageFor(TEXT("icon_../x")).IsEmpty()
        &&LDUIIcons::PackageFor(TEXT("icon_Root")).IsEmpty());
    TSharedPtr<const FLDDemoContentSnapshot> Snapshot;
    if(!LDTestApprovedFixture::Read(*this,Snapshot)){return false;}
    const FLDDemoContentDefinitions& Content=Snapshot->Get();
    TSet<FString> NoSquare{Content.Character.Id};
    for(const FLDDemoMonsterDefinition& Monster:Content.Monsters){NoSquare.Add(Monster.Id);}
    TArray<FString> Needed{LDUIIcons::Dash,LDUIIcons::OutOfCombatRegen,LDUIIcons::DormRegen};
    for(const FLDDemoDisplayDefinition& Display:Content.Display){if(!NoSquare.Contains(Display.Id)){Needed.Add(Display.IconId);}}
    TestTrue(TEXT("The catalog lists actives, stats and statuses"),Needed.Num()>3+Content.Abilities.Num()+Content.Stats.Num());
    for(const FString& IconId:Needed)
    {
        const UTexture2D* Texture=LDUIIcons::Find(IconId);
        TestTrue(FString::Printf(TEXT("%s is an imported icon texture"),*IconId),Texture!=nullptr);
    }
    // A missing one is a visible, logged content error rather than a blank square.
    // A fresh id per run: each id is reported once per process.
    const FString Unknown=TEXT("icon_hud_rules_fixture_missing_")+FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
    AddExpectedMessage(TEXT("LD_UI_ICON_MISSING '")+Unknown+TEXT("'"),ELogVerbosity::Error,EAutomationExpectedMessageFlags::Contains,1,false);
    TestNull(TEXT("An id without an imported texture finds nothing"),LDUIIcons::Find(Unknown));
    TestNull(TEXT("A second lookup still finds nothing and logs no more"),LDUIIcons::Find(Unknown));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDTermsGlossaryTest,"Lockdown.Rules.HUDTermsGlossary",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDTermsGlossaryTest::RunTest(const FString& Parameters)
{
    // #111: system text names a few statuses directly; those glossary copies must be the approved catalog's display names.
    TMap<FString,FString> Names;
    if(!LDTestApprovedDisplay::Names(*this,Names)){return false;}
    const TPair<const TCHAR*,const TCHAR*> Shared[]={{TEXT("shield"),LDTerms::Shield},{TEXT("root"),LDTerms::Root},{TEXT("slow"),LDTerms::Slow},
        {TEXT("taunt"),LDTerms::Taunt},{TEXT("disarm"),LDTerms::Disarm},{TEXT("stun"),LDTerms::Stun},
        {TEXT("respawn_protection"),LDTerms::RespawnProtection},
        // #147 keywords: silence and burn are statuses of their own; breach is the shared resistance shred (exposed).
        {TEXT("silence"),LDTerms::Silence},{TEXT("burn"),LDTerms::Burn},{TEXT("exposed"),LDTerms::Breach}};
    for(const TPair<const TCHAR*,const TCHAR*>& Pair:Shared)
    {
        const FString* Name=Names.Find(Pair.Key);
        TestTrue(FString::Printf(TEXT("The glossary's %s is the approved display name"),Pair.Key),Name&&Name->Equals(Pair.Value,ESearchCase::CaseSensitive));
    }
    // Every system term is filled, distinct from the others and never an entry name, so it cannot read as a skill or creature.
    const TCHAR* const Terms[]={LDTerms::Health,LDTerms::Momentum,LDTerms::AttackDamage,LDTerms::AbilityPower,LDTerms::Armor,LDTerms::MagicResist,
        LDTerms::AttackSpeed,LDTerms::MoveSpeed,LDTerms::Crit,LDTerms::Haste,LDTerms::PhysicalDamage,LDTerms::MagicalDamage,
        LDTerms::Experience,LDTerms::BasicAttack,LDTerms::Dash,LDTerms::CapturePoint,LDTerms::CampLow,LDTerms::CampHigh,
        LDTerms::Dead,LDTerms::RespawnWait,LDTerms::Choice,LDTerms::Winner,LDTerms::UnknownVictim};
    TSet<FString> Seen;TArray<FString> EntryNames;Names.GenerateValueArray(EntryNames);
    for(const TCHAR* Term:Terms)
    {
        const FString Value(Term);
        bool bDuplicate=false;Seen.Add(Value,&bDuplicate);
        TestTrue(FString::Printf(TEXT("System term %s is filled, unique and no entry name"),Term),!Value.IsEmpty()&&!bDuplicate&&!EntryNames.Contains(Value));
    }
    // The front page and the crowning tell one story: the capture point decides the winner title.
    TestTrue(TEXT("The title names the winner title"),FString(LDTerms::Title).Contains(LDTerms::Winner));
    TestTrue(TEXT("The tagline names the capture point and the winner title"),FString(LDTerms::Tagline).Contains(LDTerms::CapturePoint)&&FString(LDTerms::Tagline).Contains(LDTerms::Winner));
    TestTrue(TEXT("The slang note is shown text, not a placeholder"),FString(LDTerms::SlangNote).Len()>4);
    TestTrue(TEXT("A kill line keeps both names in order"),LDTerms::KillLine(TEXT("甲"),TEXT("乙")).StartsWith(TEXT("甲 "))&&LDTerms::KillLine(TEXT("甲"),TEXT("乙")).Contains(TEXT(" 乙 ")));
    return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDAttributeViewTest,"Lockdown.Rules.HUDAttributeView",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDAttributeViewTest::RunTest(const FString& Parameters)
{
    // The HUD attribute view lists each projected attribute once, except what the health and momentum bars already show.
    using A=ELDProjectedAttribute;
    const TArray<uint8> Order=LDHUD::AttributeViewOrder();
    for(const A Shown:{A::MaxHP,A::HPRegen,A::MomentumRegen})
    {TestFalse(TEXT("The bars' maxima and regenerations are not repeated"),Order.Contains(uint8(Shown)));}
    TestEqual(TEXT("Every other projected attribute is listed"),Order.Num(),int32(A::Count)-3);
    TSet<FString> Glyphs;
    for(const uint8 Attribute:Order)
    {
        FLDCombatAttributeRowView Probe;Probe.Attribute=Attribute;
        Glyphs.Add(LDHUD::AttributeRow(Probe).Glyph);
    }
    TestEqual(TEXT("Each listed attribute has its own icon glyph"),Glyphs.Num(),Order.Num());
    // The entry shows the final value alone; the popup keeps the unit, the clamp note and the sources.
    FLDCombatAttributeRowView Speed;Speed.Attribute=uint8(A::AttackSpeed);Speed.Final.Numerator=5;Speed.Final.Denominator=4;Speed.bClamped=true;
    const LDHUD::FAttributeRowView Row=LDHUD::AttributeRow(Speed);
    TestEqualSensitive(TEXT("Attack speed reads as whole attacks per minute, without a unit in the entry"),Row.Value,FString(TEXT("75")));
    TestTrue(TEXT("The popup line keeps the unit and the clamp"),Row.Final.StartsWith(TEXT("75 次/分钟"))&&Row.Final.Contains(TEXT("上下限")));
    FLDCombatAttributeRowView SlowSpeed;SlowSpeed.Attribute=uint8(A::AttackSpeed);SlowSpeed.Final.Numerator=9;SlowSpeed.Final.Denominator=10;
    TestEqualSensitive(TEXT("0.9 attacks per second reads as 54"),LDHUD::AttributeRow(SlowSpeed).Value,FString(TEXT("54")));
    FLDCombatAttributeRowView Move;Move.Attribute=uint8(A::MoveSpeed);Move.Final.Numerator=300;Move.Final.Denominator=1;
    TestEqualSensitive(TEXT("Move speed has no unit anywhere"),LDHUD::AttributeRow(Move).Final,FString(TEXT("300")));
    FLDCombatAttributeRowView Haste;Haste.Attribute=uint8(A::CooldownHaste);Haste.Final.Numerator=30;Haste.Final.Denominator=1;
    const LDHUD::FAttributeRowView HasteRow=LDHUD::AttributeRow(Haste);
    TestEqualSensitive(TEXT("Haste 30 reads as a 23% cooldown reduction"),HasteRow.Value,FString(TEXT("23%")));
    TestTrue(TEXT("The haste popup keeps the haste points"),HasteRow.Final.Contains(TEXT("23%"))&&HasteRow.Final.Contains(TEXT("30")));
    FLDCombatAttributeRowView NoHaste;NoHaste.Attribute=uint8(A::CooldownHaste);NoHaste.Final.Denominator=1;
    TestEqualSensitive(TEXT("No haste reads as 0%"),LDHUD::AttributeRow(NoHaste).Value,FString(TEXT("0%")));
    FLDCombatAttributeRowView Crit;Crit.Attribute=uint8(A::CritChance);Crit.Final.Numerator=1;Crit.Final.Denominator=4;
    TestEqualSensitive(TEXT("A ratio reads as a percentage"),LDHUD::AttributeRow(Crit).Value,FString(TEXT("25%")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDReadModelGateTest,"Lockdown.Rules.HUDReadModelGate",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDReadModelGateTest::RunTest(const FString&)
{
    // The clock bucket steps every 1/ReadModelClockHz real seconds while timed text shows, and is absent otherwise.
    TestEqual(TEXT("Nothing timed has no clock bucket"),LDHUD::ReadModelClock(12.34,false),int64(-1));
    TestEqual(TEXT("The bucket is the tenth of a second"),LDHUD::ReadModelClock(12.34,true),int64(123));
    TestEqual(TEXT("The bucket holds within its tenth"),LDHUD::ReadModelClock(12.399,true),int64(123));
    TestEqual(TEXT("The bucket turns at the next tenth"),LDHUD::ReadModelClock(12.4,true),int64(124));
    // A step ring moves visibly between clock buckets only over a short step; an unknown step has no ring.
    TestTrue(TEXT("A short step's ring needs every sample"),LDHUD::ReadModelRingMoves(1000,1000+LDHUD::ReadModelSmoothRingMs-1));
    TestFalse(TEXT("A long step's ring follows the clock bucket"),LDHUD::ReadModelRingMoves(1000,1000+LDHUD::ReadModelSmoothRingMs));
    TestFalse(TEXT("An unknown step has no ring to move"),LDHUD::ReadModelRingMoves(1000,1000));

    LDHUD::FReadModelGate Gate;LDHUD::FReadModelKey Key;
    TestTrue(TEXT("The first sample builds"),Gate.ShouldRebuild(Key));
    Gate.Rebuilt(Key,false);
    TestTrue(TEXT("The first build settles once"),Gate.ShouldRebuild(Key));
    Gate.Rebuilt(Key,false);
    TestFalse(TEXT("An unchanged key with nothing moving skips the rebuild"),Gate.ShouldRebuild(Key));

    // Every input field rebuilds on its own, and each change is followed by one settling rebuild.
    const auto Changes=[](LDHUD::FReadModelKey& K,int32 Field)
    {
        switch(Field)
        {
        case 0:++K.Views;break;case 1:++K.Local;break;case 2:++K.CombatReceipts;break;case 3:++K.Feedback;break;
        case 4:++K.Frame;break;case 5:K.Interaction+=.5;break;case 6:K.Pointer.X+=1;break;case 7:K.Viewport.Y+=1;break;
        case 8:K.TextScale+=.1f;break;default:K.Flags^=uint64(1)<<Field;break;
        }
    };
    for(int32 Field=0;Field<12;++Field)
    {
        Changes(Key,Field);
        TestTrue(FString::Printf(TEXT("Input %d changed: rebuild"),Field),Gate.ShouldRebuild(Key));
        Gate.Rebuilt(Key,false);
        TestTrue(FString::Printf(TEXT("Input %d changed: one settling rebuild"),Field),Gate.ShouldRebuild(Key));
        Gate.Rebuilt(Key,false);
        TestFalse(FString::Printf(TEXT("Input %d settled: skip"),Field),Gate.ShouldRebuild(Key));
    }

    // A clock tick alone rebuilds once, without a settling rebuild.
    Key.Clock=LDHUD::ReadModelClock(5.,true);
    TestTrue(TEXT("A new clock bucket rebuilds"),Gate.ShouldRebuild(Key));
    Gate.Rebuilt(Key,false);
    TestFalse(TEXT("A clock tick needs no settling rebuild"),Gate.ShouldRebuild(Key));
    Key.Clock=LDHUD::ReadModelClock(5.05,true);
    TestFalse(TEXT("The same tenth does not rebuild"),Gate.ShouldRebuild(Key));

    // A live build (something moves with time) rebuilds every sample until a build has nothing moving.
    ++Key.Views;Gate.Rebuilt(Key,true);
    TestTrue(TEXT("After a live build the next sample rebuilds"),Gate.ShouldRebuild(Key));
    Gate.Rebuilt(Key,true);
    TestTrue(TEXT("A live build keeps rebuilding with an unchanged key"),Gate.ShouldRebuild(Key));
    Gate.Rebuilt(Key,false);
    TestFalse(TEXT("A still build stops the per-sample rebuild"),Gate.ShouldRebuild(Key));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDFlowSmoothnessTest,"Lockdown.Rules.HUDFlowSmoothness",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDFlowSmoothnessTest::RunTest(const FString&)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if(!LDTestApprovedFixture::Read(*this,Catalog)){return false;}
    const FLDDemoContentDefinitions& Content=Catalog->Get();

    // 1) Minimap spawn badge hit-testing (#331 §1.3), OwnFootZ camera frustum plane (#329 §2.4), and semantic blocker overlay (#329 §2.2).
    {
        const FBox2D Ground(FVector2D(-5000,-5000),FVector2D(5000,5000));
        const LDMinimap::FView View=LDMinimap::MakeView(ELDMinimapOrientation::North,Ground,FVector2D(260,260),0.,false,0.);
        const TArray<FVector> Candidates={FVector(-3000,-3000,0),FVector(0,0,0),FVector(3200,2800,0)};
        const FVector2D Spot1=LDMinimap::ToFrame(View,Candidates[1]);
        TestEqual(TEXT("Click within 22px hits spawn candidate 1"),
            LDMinimap::HitSpawnCandidate(View,Spot1+FVector2D(15.,12.),Candidates),1);
        TestEqual(TEXT("Click beyond 22px misses spawn badges"),
            LDMinimap::HitSpawnCandidate(View,Spot1+FVector2D(21.,12.),Candidates),INDEX_NONE);

        const FVector HighGroundFoot(1200.,-800.,450.);
        const FVector CamDir=FRotator(-60.,-90.,0.).Vector();
        const FVector CamPos=HighGroundFoot-CamDir*2250.;
        FVector HitElevated=FVector::ZeroVector,HitZero=FVector::ZeroVector;
        TestTrue(TEXT("Minimap camera ray intersects Z = OwnFootZ (450 cm) with zero parallax shift"),
            LDMinimap::IntersectCameraPlane(CamPos,CamDir,HighGroundFoot.Z,HitElevated)
            &&FVector::Dist2D(HitElevated,HighGroundFoot)<1e-3);
        TestTrue(TEXT("Intersecting Z = 0 on 450 cm high ground would have introduced >200 cm parallax offset"),
            LDMinimap::IntersectCameraPlane(CamPos,CamDir,0.,HitZero)&&FVector::Dist2D(HitZero,HighGroundFoot)>200.);
        TestTrue(TEXT("Minimap blocker classifier distinguishes FullBlocker, LowBlocker and non-movement blockers"),
            LDMinimap::ClassifyBlocker(true,true)==LDMinimap::EBlockerOverlay::FullBlocker
            &&LDMinimap::ClassifyBlocker(true,false)==LDMinimap::EBlockerOverlay::LowBlocker
            &&LDMinimap::ClassifyBlocker(false,true)==LDMinimap::EBlockerOverlay::None
            &&LDMinimap::ClassifyBlocker(false,false)==LDMinimap::EBlockerOverlay::None);
    }

    // 2) Host compatibility strip on skill cards & 1-click full-column replacement (#331 §2.2 & §2.3).
    {
        const FLDDemoCardDefinition* SkillCardDef=Content.Cards.FindByPredicate([](const FLDDemoCardDefinition& C){return C.Form==ELDCardForm::Skill;});
        if(TestNotNull(TEXT("Catalog contains a skill-form card"),SkillCardDef)&&!Content.Abilities.IsEmpty())
        {
            FLDProgressionView EmptyProg;EmptyProg.bValid=true;EmptyProg.Level=5;
            bool bHasHost=true,bAllFull=false;
            const FString NoHostText=LDHUD::SkillCardCompatStrip(FName(*SkillCardDef->Id),EmptyProg,&Content,LDHUD::FSlotKeys(),bHasHost,bAllFull);
            TestTrue(TEXT("Skill card without compatible active warns in amber"),!bHasHost&&NoHostText.Contains(TEXT("暂无可装槽")));

            // Find an active compatible with SkillCardDef and fill its column with 4 cards to test 1-click replacement drops.
            for(const FLDDemoAbilityDefinition& Abil:Content.Abilities)
            {
                FLDProgressionView FullColProg;FullColProg.bValid=true;FullColProg.Level=8;
                FLDActiveSlotView Slot0;Slot0.DefinitionId=FName(*Abil.Id);Slot0.Slot=0;Slot0.Branch=ELDSkillBranch::None;
                FullColProg.Actives.Add(Slot0);
                bool bFitsAbil=false,bColFull=false;
                const FString FitText=LDHUD::SkillCardCompatStrip(FName(*SkillCardDef->Id),FullColProg,&Content,LDHUD::FSlotKeys(),bFitsAbil,bColFull);
                if(!bFitsAbil){continue;}
                TestTrue(TEXT("Compatible active appears on skill card strip"),FitText.Contains(TEXT("可装："))&&FitText.Contains(TEXT("0/4")));

                // Install 4 cards on Q and add 1 uninstalled copy to storage.
                for(int32 i=0;i<4;++i)
                {
                    FLDCardInstanceView& Installed=FullColProg.Cards.AddDefaulted_GetRef();
                    Installed.Serial=uint16(i+1);Installed.DefinitionId=FName(*SkillCardDef->Id);
                    Installed.Target.Kind=ELDCardTargetKind::Skill;Installed.Target.Active=FName(*Abil.Id);
                    if(!SkillCardDef->ChooseElements.IsEmpty()){Installed.Target.Element=SkillCardDef->ChooseElements[0];}
                }
                FLDCardInstanceView& Candidate=FullColProg.Cards.AddDefaulted_GetRef();
                Candidate.Serial=99;Candidate.DefinitionId=FName(*SkillCardDef->Id);
                const FName CandidateKey=LDHUD::CardKey(Candidate.DefinitionId,Candidate.Serial);
                const TArray<LDHUD::FCardDrop> RepDrops=LDHUD::ColumnReplaceDrops(CandidateKey,LDHUD::EStorageColumn::SlotQ,FullColProg,&Content);
                TestEqual(TEXT("Full 4-card column exposes 4 direct replacement targets even when workshop < 22"),RepDrops.Num(),4);
                break;
            }
        }
    }

    // 3) Live stat hover delta & 18-point keystone preview / loss warning (#331 §3.1, authoritative LDAttributeProjection).
    if(!Content.StatsInDesignOrder().IsEmpty())
    {
        const FLDDemoStatDefinition* FirstStat=Content.StatsInDesignOrder()[0];
        const LDHUD::FStatHoverPreview UpToKeystone=LDHUD::StatHoverPreview(*FirstStat,17,1,1,true,{},Content,Catalog.Get());
        TestTrue(TEXT("17 -> 18 stat hover previews keystone progress and attribute delta"),
            !UpToKeystone.AttributeLines.IsEmpty()&&UpToKeystone.KeystoneProgress.Contains(TEXT("17/18")));

        const LDHUD::FStatHoverPreview LoseKeystone=LDHUD::StatHoverPreview(*FirstStat,18,1,1,true,{},Content,Catalog.Get());
        TestTrue(TEXT("18 -> 17 refund hover warns about losing keystone"),
            LoseKeystone.bKeystoneActive&&LoseKeystone.bKeystoneLossWarning&&!LoseKeystone.RefundWarning.IsEmpty());

        // CritChance from 0% adds probability points (0% -> >0%, never multiplying 0% * ratio = 0%).
        if(const FLDDemoStatDefinition* CritStat=Content.Stats.FindByPredicate([](const FLDDemoStatDefinition& S)
            {return S.PerPoint.ContainsByPredicate([](const FLDDemoStatBonus& B){return B.Stat==ELDDemoBonusStat::CritChanceBonusRatio;});}))
        {
            const LDHUD::FStatHoverPreview CritPreview=LDHUD::StatHoverPreview(*CritStat,0,1,1,false,{},Content,Catalog.Get());
            const bool bNonZeroCrit=CritPreview.AttributeLines.ContainsByPredicate([](const FString& Line)
                {return Line.Contains(TEXT("0% → "))&&!Line.Contains(TEXT("0% → 0%"));});
            TestTrue(TEXT("Authoritative stat hover projects 0% CritChance to positive probability gain"),bNonZeroCrit);
        }

        // MoveSpeed already at catalog cap clamps After to cap (+0 effective gain).
        if(const FLDDemoStatDefinition* MoveStat=Content.Stats.FindByPredicate([](const FLDDemoStatDefinition& S)
            {return S.PerPoint.ContainsByPredicate([](const FLDDemoStatBonus& B){return B.Stat==ELDDemoBonusStat::MoveSpeedBonusRatio;});}))
        {
            FLDProgressionView CappedProg;CappedProg.bValid=true;CappedProg.Generation=1;CappedProg.Life=1;CappedProg.Level=1;
            CappedProg.StatPoints={{FName(*MoveStat->Id),260}};
            const LDHUD::FStatHoverPreview CappedMove=LDHUD::StatHoverPreview(*MoveStat,260,1,1,false,{},Content,Catalog.Get(),&CappedProg);
            TestTrue(TEXT("MoveSpeed at catalog cap reports +0 effective gain"),
                CappedMove.DeltaSummary.Contains(TEXT("+0 "))||CappedMove.AttributeLines.ContainsByPredicate([](const FString& L){return L.Contains(TEXT("（+0）"));}));
        }
    }

    // 4) Pasted host:port auto-split (ignoring 1-char typing), Rest Shop + Services library fit, reversible RestReady, PK & colorblind (#331 §4).
    {
        FString Host,Port;
        TestTrue(TEXT("Valid pasted IPv4:Port splits cleanly"),
            LDDemoEntry::SplitPastedEndpoint(TEXT(" 192.168.1.20:7777 "),Host,Port)&&Host==TEXT("192.168.1.20")&&Port==TEXT("7777"));
        TestFalse(TEXT("Invalid port > 65535 is not split"),
            LDDemoEntry::SplitPastedEndpoint(TEXT("192.168.1.20:70000"),Host,Port));
        TestTrue(TEXT("Typing first port digit 7 after 192.168.1.20: is detected as a 1-char typing edit"),
            LDDemoEntry::IsSingleCharTypingEdit(TEXT("192.168.1.20:"),TEXT("192.168.1.20:7")));
        TestFalse(TEXT("Pasting full 192.168.1.20:7777 into empty or existing host is not a 1-char typing edit"),
            LDDemoEntry::IsSingleCharTypingEdit(TEXT(""),TEXT("192.168.1.20:7777"))
            ||LDDemoEntry::IsSingleCharTypingEdit(TEXT("127.0.0.1"),TEXT("10.0.0.2:80")));

        // Verify StackedTrayLimit + side-by-side Rest Shop at 1920x1080 with Services + 200px Pending tray keeps positive Library fit.
        const FIntVector4 StoredCounts(3,1,1,1);
        const float ShopRightWidth=FMath::Clamp(1920.f*.34f,520.f,680.f);
        const float TotalRightH=808.f-108.f;
        const float LibraryChrome=70.f+56.f+14.f,PendingChrome=52.f+58.f;
        const float LibraryReserve=FMath::Max(36.f,10000.f-LDWorkshop::StackedTrayLimit(StoredCounts,ShopRightWidth-44.f,10000.f,44.f,1.f));
        const float ShopH=FMath::Clamp(FMath::Min(TotalRightH*.38f,TotalRightH-14.f-(LibraryChrome+LibraryReserve+PendingChrome+116.f)),176.f,296.f);
        const float AvailLibH=TotalRightH-ShopH-14.f;
        const float TrayLim=LDWorkshop::StackedTrayLimit(StoredCounts,ShopRightWidth-44.f,FMath::Max(0.f,AvailLibH-LibraryChrome-PendingChrome),44.f,1.f);
        const float LibBodyH=AvailLibH-(FMath::Min(200.f,TrayLim)+PendingChrome)-LibraryChrome;
        TestTrue(TEXT("1920x1080 Rest Shop + Services + 200px tray preserves positive fitting LibraryBody height"),
            LibBodyH>=LibraryReserve-0.5f&&LDWorkshop::FitLibrary(StoredCounts,ShopRightWidth-44.f,LibBodyH,44.f,1.f).bFits);
        TestTrue(TEXT("Withdrawn rest ready returns explicit confirmation"),
            LDHUD::RestReadyText(ELDRestReadyResult::Withdrawn).Contains(TEXT("已取消准备")));
        TestTrue(TEXT("Colorblind mode switches health bar hues"),
            LDHUDStyle::OwnHealthOf(true)!=LDHUDStyle::OwnHealthOf(false)
            &&LDHUDStyle::EnemyHealthOf(true)!=LDHUDStyle::EnemyHealthOf(false));

        FLDPublicMatchView Public;Public.Generation=1;Public.Phase=ELDMatchPhase::Rest;
        Public.Timeline.Generation=1;Public.Timeline.Stage=ELDMatchStage::Rest;Public.Timeline.RestIndex=1;Public.Timeline.Step=ELDRestStep::Betting;
        Public.Timeline.StepEndsAtMs=10000;
        Public.PK.Generation=1;Public.PK.Serial=1;Public.PK.Format=ELDPKFormat::Duel;Public.PK.SideCount=2;
        const FGuid A(1,0,0,1),B(2,0,0,2),Spectator(3,0,0,3);
        FLDPKEntrantView EntA;EntA.Contender=A;EntA.Side=1;EntA.Level=6;
        FLDPKEntrantView EntB;EntB.Contender=B;EntB.Side=2;EntB.Level=4;
        Public.PK.Entrants={EntA,EntB};
        Public.PK.Bettors={Spectator};
        FLDLobbyView Lobby;Lobby.Members={{A,TEXT("甲"),ELDContenderKind::Human,true,true},{B,TEXT("乙"),ELDContenderKind::Human,false,true}};
        FLDPKParameters PKParams;FString PKError;
        if(TestTrue(TEXT("PK parameters load"),FLDPKParameters::FromContent(Content.Parameters,PKParams,PKError)))
        {
            LDHUD::FPKPanelInput PKIn;PKIn.Self=Spectator;PKIn.bClock=true;PKIn.NowMs=4000;PKIn.Params=&PKParams;
            const LDHUD::FPKPanelView PKView=LDHUD::PKPanel(Public,Lobby,PKIn);
            TestTrue(TEXT("PK betting side line shows entrant level and odds"),
                PKView.bCanBet&&PKView.Sides.Num()==2&&PKView.Sides[0].Line.Contains(TEXT("Lv.6"))&&PKView.Sides[0].BetAction.Contains(TEXT("1:")));
        }

        FLDResultView Res;Res.Generation=1;Res.ResultId=FGuid(9,9,9,9);
        FLDDamageSummaryView Sum;Sum.Match=1;Sum.ResultId=Res.ResultId;
        FLDDamageLine L0;L0.Source.Kind=ELDCombatSourceKind::Basic;L0.DamageP2=7000;
        FLDDamageLine L1;L1.Source.Kind=ELDCombatSourceKind::Innate;L1.DamageP2=3000;
        Sum.Lines={L0,L1};
        const LDHUD::FDamageListView DmgView=LDHUD::DamageSummary(Sum,Res,&Content);
        TestTrue(TEXT("Result damage summary exposes structured share bars"),
            DmgView.bAvailable&&DmgView.Shares.Num()==2&&FMath::IsNearlyEqual(DmgView.Shares[0].Share,.7f,.01f));

        // 5) ULDStatBar batch ordering & >255 refund chunking:
        //    (a) Refunds dispatch BEFORE allocations funded by those refunds when UnspentPoints == 0;
        //    (b) Refund of 260 points splits into 255 + 5 across acknowledged revisions instead of narrowing uint8(260) == 4.
        UWorld* StatWorld=UWorld::CreateWorld(EWorldType::Game,false);
        if(TestNotNull(TEXT("StatBar test world created"),StatWorld))
        {
            ON_SCOPE_EXIT{StatWorld->DestroyWorld(false);};
            ULDStatBar* Bar=NewObject<ULDStatBar>(StatWorld);
            Bar->Initialize();
            const TSharedRef<SWidget> BarSlate=Bar->TakeWidget();
            (void)BarSlate;
            const TArray<const FLDDemoStatDefinition*> StatOrder=Content.StatsInDesignOrder();
            if(StatOrder.Num()>=2)
            {
                TArray<FLDBuildCommand> Dispatched;
                uint64 NextReq=1;
                Bar->PeekRequestId=[&]()->uint64{return Bar->HasPendingRequest()?0:NextReq;};
                Bar->SubmitBuild=[&](const FLDBuildCommand& Cmd,ELDGrowthReason& OutReason)->uint64
                {
                    OutReason=ELDGrowthReason::None;
                    Dispatched.Add(Cmd);
                    return NextReq++;
                };
                auto MakeProg=[&](uint64 Rev,int64 Unspent,int32 S0,int32 S1)
                {
                    FLDProgressionView V;V.bValid=true;V.Generation=1;V.Life=1;V.Level=5;V.BuildRevision=Rev;V.UnspentPoints=Unspent;
                    for(int32 I=0;I<StatOrder.Num();++I)
                    {
                        V.StatPoints.Add({FName(*StatOrder[I]->Id),I==0?S0:I==1?S1:0});
                    }
                    return V;
                };
                auto AckLast=[&](uint64 NewRev,const FLDProgressionView& Updated)
                {
                    FLDSubmitResult Ack;
                    Ack.Generation=1;Ack.Family=ELDGrowthFamily::Build;Ack.RequestId=NextReq-1;
                    Ack.Status=ELDAdmissionStatus::Final;Ack.bHasBuildReceipt=true;
                    Ack.BuildReceipt.Header.Generation=1;Ack.BuildReceipt.Header.OriginalLife=1;Ack.BuildReceipt.Header.RequestId=Ack.RequestId;
                    Ack.BuildReceipt.Header.Outcome=ELDReceiptOutcome::Applied;
                    Ack.BuildReceipt.Header.Revisions.bHasBuild=true;Ack.BuildReceipt.Header.Revisions.BuildRevision=NewRev;
                    Bar->HandleSubmit(Ack);
                    Bar->Present(&Updated,1,1,LDProgression::EMatchStage::Rest,ELDLifePhase::Alive,&Content,{},true,Catalog.Get());
                };

                // Case (a): Unspent == 0, S0 == 2, S1 == 0. First click allocates nothing yet; hold request id = 0 while staging
                // Refund(0, 2) + Allocate(1, 2) together, then FlushNow -> Respec goes FIRST, AllocatePoints goes SECOND after ack!
                FLDProgressionView V0=MakeProg(10,0,2,0);
                Bar->Present(&V0,1,1,LDProgression::EMatchStage::Rest,ELDLifePhase::Alive,&Content,{},true,Catalog.Get());
                Bar->PeekRequestId=[]()->uint64{return 0;}; // hold coalescing window so both queue together
                TestTrue(TEXT("Refund 2 points on stat 0 queued"),Bar->Refund(0,2));
                TestTrue(TEXT("Allocate 2 points on stat 1 funded by pending refund queued"),Bar->Allocate(1,2));
                Bar->PeekRequestId=[&]()->uint64{return Bar->HasPendingRequest()?0:NextReq;};
                Bar->FlushNow();
                if(TestEqual(TEXT("First dispatched command is Respec alone"),Dispatched.Num(),1))
                {
                    TestTrue(TEXT("Refunds dispatch before allocations"),
                        Dispatched[0].Kind==ELDBuildCommandKind::Respec&&Dispatched[0].PointCounts[0]==2&&Dispatched[0].PointCounts[1]==0);
                    AckLast(11,MakeProg(11,2,0,0));
                    if(TestEqual(TEXT("Allocation funded by refund dispatches automatically on revision ack"),Dispatched.Num(),2))
                    {
                        TestTrue(TEXT("Second command allocates the 2 funded points"),
                            Dispatched[1].Kind==ELDBuildCommandKind::AllocatePoints&&Dispatched[1].PointCounts[1]==2);
                    }
                    AckLast(12,MakeProg(12,0,0,2));
                }

                // Case (b): S0 == 260 (> 255). Refund(0, 260) sends 255 in batch 1 and retains 5 for batch 2 after ack.
                Dispatched.Reset();
                FLDProgressionView V260=MakeProg(20,0,260,0);
                Bar->Present(&V260,1,1,LDProgression::EMatchStage::Rest,ELDLifePhase::Alive,&Content,{},true,Catalog.Get());
                TestTrue(TEXT("Refund 260 points accepted"),Bar->Refund(0,260));
                if(TestEqual(TEXT("First chunk of >255 refund sent"),Dispatched.Num(),1))
                {
                    TestTrue(TEXT("First chunk caps at 255 instead of narrowing 260 -> 4"),
                        Dispatched[0].Kind==ELDBuildCommandKind::Respec&&Dispatched[0].PointCounts[0]==255);
                    TestEqual(TEXT("Optimistic effective points stay at 0 while remainder waits"),Bar->EffectivePoints(0),0);
                    AckLast(21,MakeProg(21,255,5,0));
                    if(TestEqual(TEXT("Remaining 5-point refund chunk dispatches after ack"),Dispatched.Num(),2))
                    {
                        TestTrue(TEXT("Second chunk sends exact remainder 5"),
                            Dispatched[1].Kind==ELDBuildCommandKind::Respec&&Dispatched[1].PointCounts[0]==5);
                    }
                }
            }
        }

        // INPUT-05 UI: Controlled and phase-locked action slots display specific short badges ("定身", "沉默", "眩晕", "禁").
        {
            FLDCombatStatusView RootSt;RootSt.StatusId=uint8(ELDDemoStatusId::Root);RootSt.ControlBlocks=uint8(ELDCombatControlBlock::Displacement);
            FLDCombatStatusView SilSt;SilSt.StatusId=uint8(ELDDemoStatusId::Silence);SilSt.ControlBlocks=uint8(ELDCombatControlBlock::Cast);
            FLDCombatStatusView StunSt;StunSt.StatusId=uint8(ELDDemoStatusId::Stun);StunSt.ControlBlocks=uint8(ELDCombatControlBlock::Cast)|uint8(ELDCombatControlBlock::Displacement);
            const TArray<FLDCombatStatusView> RootAndSil{RootSt,SilSt};
            TestEqual(TEXT("Root status produces 定身 badge for displacement slots"),
                LDHUD::ControlStatusBadge(RootAndSil,uint8(ELDCombatControlBlock::Displacement)),FString(TEXT("定身")));
            TestEqual(TEXT("Silence status produces 沉默 badge for cast slots"),
                LDHUD::ControlStatusBadge(RootAndSil,uint8(ELDCombatControlBlock::Cast)),FString(TEXT("沉默")));
            TestEqual(TEXT("Stun outranks Silence/Root with 眩晕 badge"),
                LDHUD::ControlStatusBadge({RootSt,SilSt,StunSt},uint8(ELDCombatControlBlock::Cast)),FString(TEXT("眩晕")));

            LDHUD::FActionInput DashIn;DashIn.Slot=LDHUD::EActionSlot::Dash;DashIn.bProjected=true;DashIn.bHasSource=true;
            DashIn.Name=TEXT("冲刺");DashIn.bControlled=true;
            DashIn.ControlLabel=LDHUD::ControlStatusBadge(RootAndSil,uint8(ELDCombatControlBlock::Displacement));
            LDHUD::FActionContext ActCtx;ActCtx.KeyLabel=TEXT("D");ActCtx.bOwnContext=true;ActCtx.bPlaying=true;ActCtx.bAlive=true;
            ActCtx.bClock=true;ActCtx.NowMs=1000;
            const LDHUD::FActionView DashView=LDHUD::Action(DashIn,ActCtx);
            TestTrue(TEXT("Rooted Dash slot shows Controlled look and 定身 badge"),
                LDHUD::ActionLook(DashView,false)==LDHUD::EActionLook::Controlled&&DashView.LockBadge==TEXT("定身"));

            LDHUD::FActionInput PhaseIn;PhaseIn.Slot=LDHUD::EActionSlot::Q;PhaseIn.bProjected=true;PhaseIn.bHasSource=true;PhaseIn.Name=TEXT("肘击");
            LDHUD::FActionContext LockedCtx=ActCtx;LockedCtx.bPlaying=false;
            const LDHUD::FActionView LockedView=LDHUD::Action(PhaseIn,LockedCtx);
            TestEqual(TEXT("Phase-locked (!bPlaying) slot shows 禁 badge"),LockedView.LockBadge,FString(TEXT("禁")));

            LDHUD::FActionInput ReturnIn=PhaseIn;ReturnIn.ServerReject=ELDCombatReject::NotStarted;ReturnIn.ServerBlock=LDHUD::CombatRejectText(ELDCombatReject::NotStarted);
            TestEqual(TEXT("Rest return countdown (NotStarted) slot shows 禁 badge"),LDHUD::Action(ReturnIn,ActCtx).LockBadge,FString(TEXT("禁")));

            for(const ELDCombatReject Ordinary:{ELDCombatReject::Cooldown,ELDCombatReject::MomentumShort,ELDCombatReject::Busy})
            {
                LDHUD::FActionInput OrdIn=PhaseIn;
                OrdIn.ServerReject=Ordinary;
                OrdIn.ServerBlock=LDHUD::CombatRejectText(Ordinary);
                if(Ordinary==ELDCombatReject::Cooldown){OrdIn.ReadyAtMs=3000;OrdIn.CooldownMs=4000;}
                if(Ordinary==ELDCombatReject::MomentumShort){OrdIn.MomentumCostP2=3000;ActCtx.bMomentumKnown=true;ActCtx.MomentumP2=1000;}
                TestTrue(TEXT("Cooling / momentum-short / busy slots do not show 禁 badge"),
                    LDHUD::Action(OrdIn,ActCtx).LockBadge.IsEmpty());
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDLiveSkillTooltipTest,"Lockdown.Rules.HUDLiveSkillTooltip",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDLiveSkillTooltipTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Snapshot;FString Error;
    if(!TestTrue(TEXT("Approved runtime content loads"),LDDemoEntry::LoadRuntimeContent(Snapshot,Error)&&Snapshot.IsValid())){return false;}
    const FLDDemoContentDefinitions& Content=Snapshot->Get();

    // Pick active_snark_bolt (a learned magical projectile damage active) and equip card_big_one + card_lend_fist + 25 CooldownHaste.
    const FName SkillId(TEXT("active_snark_bolt"));
    const FLDDemoAbilityDefinition* Ab=Content.Abilities.FindByPredicate([](const FLDDemoAbilityDefinition& A){return A.Id==TEXT("active_snark_bolt");});
    const FLDDemoCardDefinition* BigOne=Content.Cards.FindByPredicate([](const FLDDemoCardDefinition& C){return C.Id==TEXT("card_big_one");});
    const FLDDemoCardDefinition* LendFist=Content.Cards.FindByPredicate([](const FLDDemoCardDefinition& C){return C.Id==TEXT("card_lend_fist");});
    if(!TestTrue(TEXT("Catalog contains active_snark_bolt, card_big_one and card_lend_fist"),Ab&&BigOne&&LendFist)){return false;}

    FLDProgressionView Build;Build.bValid=true;Build.Generation=1;Build.Life=1;Build.Level=5;
    FLDActiveSlotView SlotQ;SlotQ.Slot=0;SlotQ.DefinitionId=SkillId;SlotQ.Branch=ELDSkillBranch::None;
    Build.Actives={SlotQ};
    FLDCardInstanceView C1;C1.Serial=1;C1.DefinitionId=TEXT("card_big_one");C1.Target.Kind=ELDCardTargetKind::Skill;C1.Target.Active=SkillId;
    FLDCardInstanceView C2;C2.Serial=2;C2.DefinitionId=TEXT("card_lend_fist");C2.Target.Kind=ELDCardTargetKind::Skill;C2.Target.Active=SkillId;
    FLDCardInstanceView StoredStone;StoredStone.Serial=3;StoredStone.DefinitionId=TEXT("medal_fire");
    FLDCardInstanceView StoredCard;StoredCard.Serial=4;StoredCard.DefinitionId=TEXT("card_practice");
    Build.Cards={C1,C2,StoredStone,StoredCard};
    Build.PendingBlueprints={TEXT("active_elbow")};

    // #328 §2: 3-way pending category counts separate skills, build cards and element stones.
    const LDHUD::FPendingCategoryCounts Cats=LDHUD::PendingCategoryCounts(&Build,1,&Content);
    TestTrue(TEXT("PendingCategoryCounts separates skill blueprints, build cards and element stones (#328)"),
        Cats.SkillCount==1&&Cats.CardCount==1&&Cats.StoneCount==1);

    // Combat owner attributes: AD = 100, AP = 120, MaxHP = 1500, Armor = 40, CooldownHaste = 25.
    FLDCombatOwnerView OwnCombat;OwnCombat.Generation=1;OwnCombat.Life=1;OwnCombat.bHasBody=true;
    auto MakeAttr=[](ELDProjectedAttribute Attr,int64 WholeValue)
    {
        FLDCombatAttributeRowView Row;Row.Attribute=uint8(Attr);
        Row.Final.Numerator=WholeValue;Row.Final.Denominator=1;
        return Row;
    };
    OwnCombat.Attributes={
        MakeAttr(ELDProjectedAttribute::AD,100),
        MakeAttr(ELDProjectedAttribute::AP,120),
        MakeAttr(ELDProjectedAttribute::MaxHP,1500),
        MakeAttr(ELDProjectedAttribute::Armor,40),
        MakeAttr(ELDProjectedAttribute::CooldownHaste,25)
    };

    LDHUD::FActionInput In;In.Slot=LDHUD::EActionSlot::Q;In.bProjected=true;In.bHasSource=true;
    In.DefinitionId=SkillId;In.Name=LDHUD::ContentName(&Content,SkillId.ToString());In.Branch=ELDSkillBranch::None;
    In.MomentumCostP2=Ab->Values.MomentumCostP2;In.CooldownMs=Ab->Values.CooldownMs;
    LDHUD::FActionContext Ctx;Ctx.KeyLabel=TEXT("Q");Ctx.bOwnContext=true;Ctx.bPlaying=true;Ctx.bAlive=true;
    Ctx.bClock=true;Ctx.NowMs=5000;Ctx.bMomentumKnown=true;Ctx.MomentumP2=10000;
    Ctx.Card=LDHUD::CardStatsOf(Build.Level,OwnCombat.Attributes);
    const LDHUD::FActionView ActView=LDHUD::Action(In,Ctx);

    // Compare bare tooltip (no installed cards) vs card+haste tooltip.
    FLDProgressionView BareBuild=Build;BareBuild.Cards.Reset();
    const LDHUD::FLiveSkillTooltip BareTip=LDHUD::EvaluateLiveSkillTooltip(ActView,In,Ctx,&BareBuild,&OwnCombat,&Content,false);
    const LDHUD::FLiveSkillTooltip FullTip=LDHUD::EvaluateLiveSkillTooltip(ActView,In,Ctx,&Build,&OwnCombat,&Content,true);

    // Verify exact combat arithmetic: card_big_one multiplies primary damage & AD rider, and scales CD before 25 haste.
    int64 BigOneDmgFactorBp=10000,BigOneCdBp=10000;
    for(const FLDDemoEffectDefinition& Ef:BigOne->Effects)
    {
        if(Ef.GetOp()==ELDDemoEffectOp::SkillDamageFactor){BigOneDmgFactorBp=10000+Ef.RatioBp;}
        if(Ef.GetOp()==ELDDemoEffectOp::CooldownCost&&Ef.SecondaryRatioBp>0){BigOneCdBp=Ef.SecondaryRatioBp;}
    }
    int64 LendFistAdBp=0;
    for(const FLDDemoEffectDefinition& Ef:LendFist->Effects)
    {
        if(Ef.GetOp()==ELDDemoEffectOp::SkillAdRider){LendFistAdBp+=Ef.ADRatioBp;}
    }
    auto Share=[](int64 A,int64 B){return (A/10000)*B+((A%10000)*B)/10000;};
    const int64 ExpectedFinalDmgP2=Share(BareTip.FinalPrimaryDamageP2,BigOneDmgFactorBp);
    const int64 ExpectedRiderP2=Share(Share(100*100,LendFistAdBp),BigOneDmgFactorBp);
    const int64 CardCdMs=Share(Ab->Values.CooldownMs,BigOneCdBp);
    const int64 HasteScale=Content.Formulas.HasteScaleP2>0?Content.Formulas.HasteScaleP2:10000;
    const int64 ExpectedCdMs=FMath::Max<int64>(Content.Parameters.SkillRanks.CooldownFloorMs,
        FMath::DivideAndRoundUp<int64>(CardCdMs*HasteScale,HasteScale+2500));

    TestEqual(TEXT("Live tooltip primary damage matches exact card_big_one combat multiplier"),FullTip.FinalPrimaryDamageP2,ExpectedFinalDmgP2);
    TestEqual(TEXT("Live tooltip rider matches exact card_lend_fist AD * card_big_one combat value"),FullTip.RiderDamageP2,ExpectedRiderP2);
    TestEqual(TEXT("Live tooltip cooldown matches exact card_big_one * 25 haste formula"),FullTip.FinalCooldownMs,ExpectedCdMs);
    TestTrue(TEXT("Main tooltip body embeds multiplied damage and +rider text"),
        FullTip.Detail.Body.Contains(FString::Printf(TEXT("%lld"),(ExpectedFinalDmgP2+50)/100))
        &&FullTip.Detail.Body.Contains(FString::Printf(TEXT("+ %lld 额外物理伤害"),(ExpectedRiderP2+50)/100)));
    TestTrue(TEXT("Summary displays haste+card adjusted cooldown to 0.1s"),
        FullTip.Detail.Summary.Contains(FString::Printf(TEXT("冷却 %.1f秒"),double(ExpectedCdMs)/1000.)));
    TestTrue(TEXT("Alt expansion lists base, level growth, stat bonus, card multipliers, final damage and cooldown"),
        FullTip.FormulaLines.Num()>=6
        &&FullTip.FormulaLines[0].Contains(TEXT("基础与分支"))
        &&FullTip.FormulaLines[3].Contains(TEXT("构筑乘区"))
        &&FullTip.FormulaLines[4].Contains(FString::Printf(TEXT("= %lld"),(ExpectedFinalDmgP2+50)/100))
        &&FullTip.FormulaLines.Last().Contains(TEXT("技能急速")));

    // #328 §4.4: 6-player tactical scoreboard combines roster, leader crown, graffiti guard bar and sighted skills.
    FLDPublicMatchView Public;Public.Generation=1;Public.Phase=ELDMatchPhase::CaptureOpen;
    const FGuid SelfId(1,0,0,1),RivalId(2,0,0,2);
    Public.Leader.Leader=RivalId;Public.Leader.LeaderLevel=8;
    Public.GraffitiOwner=RivalId;Public.CircleState=ELDCircleState::Guarded;Public.GuardDueAtMs=14000;
    FLDLobbyView Lobby;Lobby.Members={{SelfId,TEXT("我方"),ELDContenderKind::Human,true,true},{RivalId,TEXT("敌王"),ELDContenderKind::Human,false,true}};
    FLDKillFeedView Feed;
    TMap<FGuid,LDHUD::FTacticalSighting> Sightings;
    LDHUD::FTacticalSighting SeenRival;SeenRival.Presentation=1;SeenRival.LastSeenLevel=9;SeenRival.RevealedActives[0]=Ab->NativeId;
    Sightings.Add(RivalId,SeenRival);
    const TArray<LDHUD::FTacticalScoreRow> TacRows=LDHUD::BuildTacticalScoreboard(Public,Feed,Lobby,SelfId,Sightings,&Build,&Content,true,8000);
    TestTrue(TEXT("Tactical scoreboard lists contenders with crown, sighted level, live guard bar and sighted skill"),
        TacRows.Num()==2&&TacRows[1].bLeaderCrown&&TacRows[1].LevelText==TEXT("Lv.9")
        &&TacRows[1].GraffitiFraction>0.5f&&!TacRows[1].ActiveNames[0].IsEmpty());
    return true;
}



