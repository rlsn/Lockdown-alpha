#include "Misc/AutomationTest.h"
#include "UI/LDHUDModel.h"
#include "UI/LDControlSettings.h"
#include "Combat/LDTacticalCatalog.h"
#include "Combat/LDTacticalRules.h"
#include "Inventory/LDEquipmentCatalog.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDB86HUDTest,"Lockdown.Rules.V08OwnerActionReadiness",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDB86HUDTest::RunTest(const FString& Parameters)
{
    FLDOwnerState O;O.Build=LDDevelopment::TacticalStart(5,ULDTacticalCatalog::Load(8)->Checksum,8);O.bActive=true;O.Mp=100;O.Inventory.RulesVersion=8;O.Inventory.Bindings.SetNum(11);
    auto View=[&](){return LDHUD::Action(O,0,false,10);};
    TestTrue(TEXT("Known equipped spell initially ready"),View().CanActivate);
    O.Mp=29;TestEqual(TEXT("Frostline needs30MP, not generic Ready"),View().State,FString(TEXT("Need 30 MP")));
    O.Mp=100;O.StatusMask=1<<uint8(LDTactical::EStatus::Mute);TestTrue(TEXT("Mute leaves learned action access"),View().CanActivate);
    O.StatusMask=1<<uint8(LDTactical::EStatus::Silence);TestEqual(TEXT("Silence explains learned action denial"),View().State,FString(TEXT("Silenced")));
    O.StatusMask=0;O.ChannelKind=3;TestFalse(TEXT("Heart channel cannot advertise concurrent casting"),View().CanActivate);O.ChannelKind=0;
    O.Build.Cooldowns[uint8(ELDSkill::Frostline)]=10.001;TestFalse(TEXT("Before exact cooldown endpoint blocked"),View().CanActivate);
    O.Build.Cooldowns[uint8(ELDSkill::Frostline)]=10;TestTrue(TEXT("Inclusive cooldown endpoint ready"),View().CanActivate);
    TestEqual(TEXT("Death reason distinct from protection"),(O.bActive=false,View()).State,FString(TEXT("Returning")));O.bActive=true;
    TestFalse(TEXT("Open panel cannot advertise world input"),LDHUD::Action(O,0,false,10,false,true).CanActivate);
    FLDInventoryItem I;I.Id=FGuid::NewGuid();I.Kind=ELDItemKind::PurityCharm;I.BindingMask=1<<7;O.Inventory.Items.Add(I);O.Inventory.Bindings[7]=I.Id;
    O.StatusMask=1<<uint8(LDTactical::EStatus::Silence);TestTrue(TEXT("Silence permits physical accessory"),LDHUD::Action(O,0,true,10).CanActivate);
    O.StatusMask=1<<uint8(LDTactical::EStatus::Mute);TestEqual(TEXT("Mute explains physical accessory denial"),LDHUD::Action(O,0,true,10).State,FString(TEXT("Muted")));
    O.StatusMask=0;O.MajorProtectionReadyAt=10001;TestFalse(TEXT("Different major protection still shares owner lock"),LDHUD::Action(O,0,true,10).CanActivate);O.MajorProtectionReadyAt=10000;
    O.Inventory.Items[0].ReadyAt=11000;TestFalse(TEXT("Physical reuse endpoint survives otherwise ready owner"),LDHUD::Action(O,0,true,10).CanActivate);
    O.Inventory.Items[0].Kind=ELDItemKind::ResonanceRing;TestEqual(TEXT("Passive is different from Empty"),LDHUD::Action(O,0,true,10).State,FString(TEXT("Passive")));
    O.Inventory.Bindings[7].Invalidate();TestEqual(TEXT("Consumed slot cannot auto-fill from bag"),LDHUD::Action(O,0,true,10).State,FString(TEXT("Empty")));
    FLDObservedEntity Other;Other.Kind=ELDObservedKind::Body;const FString Hidden=LDHUD::Inspector(O,&Other,10);O.Mp=4321;O.Gold=98765;TestEqual(TEXT("Detailed action model grants no enemy inspection rights"),LDHUD::Inspector(O,&Other,10),Hidden);
    return true;
}
#endif
