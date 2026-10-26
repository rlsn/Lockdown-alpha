#include "UI/LDInventoryPanel.h"
#include "UI/LDWidgets.h"
#include "Inventory/LDAcquisitionDirector.h"
#include "Inventory/LDAcquisitionCatalog.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Combat/LDTacticalCatalog.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
void ULDInventoryPanel::BuildAcquisition(UVerticalBox* Supply,UVerticalBox* Craft,UVerticalBox* Ruins)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto* C=ULDAcquisitionCatalog::Load();if(!C){return;}
    if(PC->OwnerState.Build.RulesVersion==8){for(const auto& I:ULDEquipmentCatalog::Load(8)->Items){if(I.Price<0){continue;}Supply->AddChildToVerticalBox(LDWidgets::Button(WidgetTree,FString::Printf(TEXT("City: %s · %dg"),*I.Name,I.Price),[PC,K=I.Kind](){PC->BuyAcquisition(2,K);}));}}
    else for(const auto& I:C->Items){if(I.Price<0){continue;}const bool Supported=I.Kind==ELDItemKind::NullBell||ULDEquipmentCatalog::Load(PC->OwnerState.Build.RulesVersion)->Find(I.Kind);
        Supply->AddChildToVerticalBox(LDWidgets::Button(WidgetTree,FString::Printf(TEXT("City: %s · %dg%s"),*I.Name,I.Price,Supported?TEXT(""):TEXT(" · unavailable")),[PC,K=I.Kind](){PC->BuyAcquisition(2,K);}));}
    for(const auto& G:ULDEquipmentCatalog::Load(PC->OwnerState.Build.RulesVersion)->Gates){if(G.Circle!=2&&G.Circle!=3){continue;}const bool Supported=ULDTacticalCatalog::Load(PC->OwnerState.Build.RulesVersion)->Find(G.Skill)!=nullptr;Supply->AddChildToVerticalBox(LDWidgets::Button(WidgetTree,FString::Printf(TEXT("%s: %s book · %dg%s"),G.Circle==3?TEXT("East Archive (2 global)"):TEXT("West/East trainer"),LDDevelopment::SkillName(G.Skill),G.Circle==3?90:60,Supported?TEXT(""):TEXT(" · unavailable")),[PC,G](){PC->BuyAcquisition(G.Circle==2&&PC->NearProduction(3)?3:4,ELDItemKind::Book,G.Skill);}));}
    for(uint8 N=0;N<C->Recipes.Num();++N){const auto& R=C->Recipes[N];const FString Name=LDInventory::Name(R.Output,ELDSkill::None);Craft->AddChildToVerticalBox(LDWidgets::Button(WidgetTree,TEXT("Preview assembly: ")+Name,[this,N](){PreviewAcquisition(20+N);}));if(R.Station!=2){Craft->AddChildToVerticalBox(LDWidgets::Button(WidgetTree,TEXT("Preview East work order: ")+Name,[this,N](){PreviewAcquisition(20+N,1);}));}}
    for(uint8 Q=0;Q<2;++Q){Craft->AddChildToVerticalBox(LDWidgets::Button(WidgetTree,Q?TEXT("Preview selected Fine → Masterwork"):TEXT("Preview selected Standard → Fine"),[this,Q](){PreviewAcquisition(32+Q,0,Selected);}));}
    AcquisitionStatus=LDWidgets::Text(WidgetTree,TEXT("Choose a recipe to preview exact inputs, fee and final capacity."),14);Craft->AddChildToVerticalBox(AcquisitionStatus);
    AcquisitionConfirm=LDWidgets::Button(WidgetTree,TEXT("Close & arm fresh context hold"),[this,PC](){if(bAcquisitionPreview){const auto P=AcquisitionPlan;bAcquisitionPreview=false;PC->ArmProduction(P);}});Craft->AddChildToVerticalBox(AcquisitionConfirm);
    Ruins->AddChildToVerticalBox(LDWidgets::Text(WidgetTree,TEXT("Ruin adapters: A (-1400,800), B (0,1050), C (1450,800). HeartForge at East (-200,+220). Full authored world/guardian arrives in B76. Unfinished capstones and objective access remain unavailable."),14));
    for(uint8 Site=6;Site<9;++Site){Ruins->AddChildToVerticalBox(LDWidgets::Button(WidgetTree,TEXT("Open / inspect ")+ALDProductionDirector::SiteName(Site),[this,PC,Site](){FLDProductionPlan P;FString Why;if(ALDAcquisitionDirector::Preview(PC->OwnerState.Inventory,PC->OwnerState.Build,34,0,Site,FGuid(),P,Why)){PC->ArmProduction(P);}else{PC->IntentFeedback=Why;}}));}
    CacheRows=WidgetTree->ConstructWidget<UVerticalBox>();Ruins->AddChildToVerticalBox(CacheRows);
}
void ULDInventoryPanel::PreviewAcquisition(uint8 Recipe,uint8 Mode,FGuid Target)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());uint8 Site=255;for(uint8 S=2;S<6;++S){if(PC->NearProduction(S)){Site=S;break;}}FString Why;bAcquisitionPreview=ALDAcquisitionDirector::Preview(PC->OwnerState.Inventory,PC->OwnerState.Build,Recipe,Mode,Site,Target,AcquisitionPlan,Why);
    FString Text=Why;if(bAcquisitionPreview){const auto& O=PC->OwnerState;const auto* C=ULDAcquisitionCatalog::Load();const bool Assembly=Recipe<32;Text=ALDProductionDirector::SiteName(Site)+FString::Printf(TEXT(" · total %dg burned · no XP\n"),AcquisitionPlan.Fee);
        for(const auto& I:AcquisitionPlan.Inputs){Text+=FString::Printf(TEXT("Bag %d: %s · %s\n"),I.Slot+1,*LDInventory::Name(I.Kind,I.Skill),I.Quality==2?TEXT("Masterwork"):I.Quality==1?TEXT("Fine"):TEXT("Standard"));}
        if(Assembly){const auto& R=C->Recipes[Recipe-20];int32 Weight=LDInventory::Weight(O.Inventory)+LDInventory::UnitWeight(R.Output);auto Missing=R.Inputs;for(const auto& V:AcquisitionPlan.Inputs){Weight-=LDInventory::UnitWeight(V.Kind);Missing.RemoveSingle(V.Kind);}for(auto K:Missing){const auto* D=C->Find(K);Text+=FString::Printf(TEXT("Buy missing: %s · %dg\n"),*LDInventory::Name(K,ELDSkill::None),D?D->Price:0);}Text+=FString::Printf(TEXT("Final weight %d/%d\n"),Weight,LDInventory::WeightCapacity(O.Build));Text+=TEXT("Output: ")+LDInventory::Name(R.Output,ELDSkill::None)+TEXT(" · Standard; component craftsmanship lost\n");Text+=FString::Printf(TEXT("Final bag %d/%d · hold %.0fs\n"),O.Inventory.Items.Num()-AcquisitionPlan.Inputs.Num()+1,O.Inventory.Capacity,R.HoldMs/1000.);if(Mode){Text+=TEXT("Missing immediate officially priced components bought in this receipt; all other inputs must be supplied.\n");}}
        else{Text+=TEXT("Same item ID and cooldown; only weapon damage, flat armor or item barrier improves.\n");}
        Text+=TEXT("Six quiet seconds; stationary held context; sight/range 120cm. Release/damage/movement cancels. ");if(O.Gold<AcquisitionPlan.Fee){bAcquisitionPreview=false;Text+=TEXT("Insufficient wallet.");}}
    AcquisitionStatus->SetText(FText::FromString(Text));RefreshAcquisition();
}
void ULDInventoryPanel::RefreshAcquisition()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());if(!AcquisitionConfirm){return;}
    AcquisitionConfirm->SetIsEnabled(bAcquisitionPreview&&AcquisitionPlan.InventoryRevision==PC->OwnerState.Inventory.Revision&&AcquisitionPlan.BuildRevision==PC->OwnerState.Build.Revision&&!PC->InventoryPending());
    uint32 Hash=GetTypeHash(PC->CacheSite);for(const auto& I:PC->CacheItems){Hash=HashCombine(Hash,GetTypeHash(I.Id));}if(Hash==SeenCache){return;}SeenCache=Hash;CacheRows->ClearChildren();
    for(const auto& I:PC->CacheItems){CacheRows->AddChildToVerticalBox(LDWidgets::Button(WidgetTree,TEXT("Take: ")+LDInventory::Name(I.Kind,I.Skill),[PC,I](){FLDProductionPlan P;FString Why;if(ALDAcquisitionDirector::Preview(PC->OwnerState.Inventory,PC->OwnerState.Build,35,0,PC->CacheSite,I.Id,P,Why)){PC->ArmProduction(P);}}));}
}
