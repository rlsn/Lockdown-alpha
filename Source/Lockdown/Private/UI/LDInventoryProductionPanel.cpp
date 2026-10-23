#include "UI/LDInventoryPanel.h"
#include "UI/LDWidgets.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
void ULDInventoryPanel::BuildProduction(UVerticalBox* All)
{
    ProductionStatus=LDWidgets::Text(WidgetTree,TEXT("Production: approach WildCrossing outcrops or a settlement workshop"),14);ProductionStatus->SetWrapTextAt(460);All->AddChildToVerticalBox(ProductionStatus);
    ProductionInput=LDWidgets::Button(WidgetTree,TEXT("Add/remove selected input"),[this](){ToggleProductionInput();});All->AddChildToVerticalBox(ProductionInput);
    auto* Recipes=WidgetTree->ConstructWidget<UHorizontalBox>();All->AddChildToVerticalBox(Recipes);
    const TCHAR* Names[]={TEXT("Gather · 6s"),TEXT("Toolkit · 4s"),TEXT("Core · 6s"),TEXT("Survey · equipped · 1s"),TEXT("Analyze · equipped · 2s"),TEXT("Shortage order · Toolkit · 3s")};
    for(uint8 R=0;R<6;++R){auto* B=LDWidgets::Button(WidgetTree,Names[R],[this,R](){PreviewProduction(R);});ProductionActions.Add(B);if(R==1||R==2){Recipes->AddChildToHorizontalBox(B);}else{All->AddChildToVerticalBox(B);}}
    All->AddChildToVerticalBox(LDWidgets::Button(WidgetTree,TEXT("Toggle workshop coupon"),[this](){bUseCoupon=!bUseCoupon;Refresh();}));
    ProductionConfirm=LDWidgets::Button(WidgetTree,TEXT("Close and arm fresh hold"),[this](){ArmProductionPreview();});All->AddChildToVerticalBox(ProductionConfirm);
}
void ULDInventoryPanel::ToggleProductionInput()
{auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto* I=PC->OwnerState.Inventory.Find(Selected);if(!I||I->bReserved||!LDInventory::Commodity(I->Kind)){return;}if(ProductionInputs.Contains(Selected)){ProductionInputs.Remove(Selected);}else if(ProductionInputs.Num()<3){ProductionInputs.Add(Selected);}bProductionPreview=false;Refresh();}
void ULDInventoryPanel::PreviewProduction(uint8 Recipe){ProductionRecipe=Recipe;bProductionPreview=true;Refresh();}
void ULDInventoryPanel::ArmProductionPreview()
{if(!bProductionPreview||!ProductionConfirm->GetIsEnabled()){return;}const auto Plan=DisplayedProduction;CastChecked<ALDPlayerController>(GetOwningPlayer())->ArmProduction(Plan);bProductionPreview=false;}
void ULDInventoryPanel::RefreshProduction()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto& O=PC->OwnerState;const auto* G=GetWorld()->GetGameState<ALDGameState>();const double Now=G?G->GetServerWorldTimeSeconds():0;
    uint8 Site=255;for(uint8 S=0;S<5;++S){if(PC->NearProduction(S)&&(Site==255||FVector::DistSquared2D(PC->GetPawn()->GetActorLocation(),ALDProductionDirector::Position(GetWorld(),S))<FVector::DistSquared2D(PC->GetPawn()->GetActorLocation(),ALDProductionDirector::Position(GetWorld(),Site)))){Site=S;}}
    ProductionInputs.RemoveAll([&](FGuid Id){const auto* I=O.Inventory.Find(Id);return !I||I->bReserved;});const auto* SelectedItem=O.Inventory.Find(Selected);
    ProductionInput->SetIsEnabled(SelectedItem&&!SelectedItem->bReserved&&LDInventory::Commodity(SelectedItem->Kind)&&!PC->InventoryPending()&&!bConfirm);
    for(uint8 R=0;R<6;++R){ProductionActions[R]->SetIsEnabled(Site<5&&((Site<2)==(R==0||R==3))&&(R!=5||(Site==4&&G&&G->ShortagePhase==1&&G->ShortageRemaining))&&!PC->InventoryPending()&&!bConfirm&&!bTradeReview);}
    FLDProductionPlan P;P.Site=Site;P.Recipe=ProductionRecipe;P.InventoryRevision=O.Inventory.Revision;P.BuildRevision=O.Build.Revision;
    if(P.Recipe==1||P.Recipe==2){for(FGuid Id:ProductionInputs){P.Inputs.Add(*O.Inventory.Find(Id));}P.Fee=P.Recipe==1?4:6;if(bUseCoupon&&O.CraftCoupon.IsValid()&&O.CouponSite==Site){P.Coupon=O.CraftCoupon;P.Fee-=2;}}
    if(P.Recipe==5){for(FGuid Id:ProductionInputs){P.Inputs.Add(*O.Inventory.Find(Id));}}
    FString Text=Site<5?ALDProductionDirector::SiteName(Site):TEXT("Approach a settlement workshop or WildCrossing outcrop");
    Text+=TEXT("\nInputs (select bag item, then Add/remove):");for(FGuid Id:ProductionInputs){const auto* I=O.Inventory.Find(Id);Text+=TEXT("\n")+LDInventory::Name(I->Kind,I->Skill)+TEXT(" · ")+Id.ToString().Left(8);}
    Text+=O.CraftCoupon.IsValid()?FString::Printf(TEXT("\nCoupon: %s · %.1fs · %s"),*ALDProductionDirector::SiteName(O.CouponSite),FMath::Max(0.,O.CouponUntil-Now),bUseCoupon?TEXT("use when bound"):TEXT("disabled")):TEXT("\nNo workshop coupon");
    bool Good=bProductionPreview&&Site<5&&((Site<2)==(P.Recipe==0||P.Recipe==3));FString Why;
    if(bProductionPreview)
    {
        if(P.Recipe==5)
        {Good&=Site==4&&G&&G->ShortagePhase==1&&G->ShortageRemaining&&Now+3<G->MatchDeadline-LDDevelopment::DeadlineSeconds+780&&ALDProductionDirector::RecipeInputs(5,P.Inputs);Text+=TEXT("\nOne selected Toolkit → +40 gold / +20 XP · 3s hold\nFirst valid completion wins; strictly before shortage expires. No endorsement.");}
        else if(P.Recipe>=3){const ELDSkill K=P.Recipe==3?ELDSkill::Survey:ELDSkill::Analyze;const int32 SkillIndex=O.Build.Slots.Find(K);P.SkillSlot=SkillIndex>=0?uint8(SkillIndex):255;Good&=SkillIndex>=0&&LDDevelopment::Equipped(O.Build,K)&&LDDevelopment::SkillValue(O.Build,K)>=(P.Recipe==3?12:14)&&Now>=O.Build.Cooldowns[uint8(K)];Text+=TEXT("\nUses equipped ")+FString(LDDevelopment::SkillName(K))+TEXT(" and its normal cooldown; score 12 Survey / 14 Analyze required.");}
        else
        {
            TArray<FGuid> Outgoing;for(const auto& I:P.Inputs){Outgoing.Add(I.Id);}const auto Kind=ALDProductionDirector::OutputKind(P.Recipe);const bool Fits=LDInventory::Fits(O.Inventory,O.Build,Outgoing,{Kind},&Why);Good&=ALDProductionDirector::RecipeInputs(P.Recipe,P.Inputs)&&Fits&&O.Gold>=P.Fee;
            const int32 Duration=P.Recipe==1?4:6;Good&=!P.Coupon.IsValid()||O.CouponUntil>Now+Duration;
            int32 FinalWeight=LDInventory::Weight(O.Inventory)+LDInventory::UnitWeight(Kind);for(const auto& I:P.Inputs){FinalWeight-=LDInventory::UnitWeight(I.Kind);}
            Text+=FString::Printf(TEXT("\nPreview: %s · burn %d gold · hold %ds\nFinal bag %d/%d slots · %d/%d weight. %s"),*LDInventory::Name(Kind,ELDSkill::None),P.Fee,Duration,O.Inventory.Items.Num()-P.Inputs.Num()+1,O.Inventory.Capacity,FinalWeight,LDInventory::WeightCapacity(O.Build),*Why);
        }
        if(Site<2&&G){const int32 Charges=Site?G->OutcropTwo:G->OutcropOne;Text+=FString::Printf(TEXT("\nPublic remaining charges: %d/3 (match-wide)"),Charges);Good&=Charges>0;}
        Text+=TEXT("\nCraft requires 6 combat-quiet seconds. Stand still, alive/unprotected, body sight within 120 cm. No XP.");
        if(!Good){Text+=TEXT("\nPreview unavailable: check exact inputs, capacity, gold, coupon duration or equipped skill.");}
    }
    ProductionStatus->SetText(FText::FromString(Text));DisplayedProduction=P;ProductionConfirm->SetIsEnabled(Good&&!PC->InventoryPending()&&!bConfirm&&!bTradeReview);
}
