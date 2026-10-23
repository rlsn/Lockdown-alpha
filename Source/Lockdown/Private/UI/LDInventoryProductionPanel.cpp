#include "UI/LDInventoryPanel.h"
#include "UI/LDWidgets.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
void ULDInventoryPanel::BuildProduction(UVerticalBox* All)
{
    auto* Choices=WidgetTree->ConstructWidget<UUniformGridPanel>();Choices->SetSlotPadding(FMargin(3));All->AddChildToVerticalBox(Choices);
    const TCHAR* Names[]={TEXT("Gather Crystal · 6s"),TEXT("Craft Toolkit · 4s"),TEXT("Craft Core · 6s"),TEXT("Survey · 1s"),TEXT("Analyze · 2s"),TEXT("East shortage · 3s"),TEXT("City Core order · 3s"),TEXT("West Supply · 3s"),TEXT("City activation · 4s")};
    for(uint8 R=0;R<9;++R){auto* B=LDWidgets::Button(WidgetTree,Names[R],[this,R](){PreviewProduction(R);});ProductionActions.Add(B);Choices->AddChildToUniformGrid(B,(R>=5?R-5:R)/2,(R>=5?R-5:R)%2);}
    ProductionInput=LDWidgets::Button(WidgetTree,TEXT("Add/remove selected input"),[this](){ToggleProductionInput();});All->AddChildToVerticalBox(ProductionInput);
    All->AddChildToVerticalBox(LDWidgets::Button(WidgetTree,TEXT("Clear inputs"),[this](){ProductionInputs.Reset();bProductionPreview=false;Refresh();}));
    CouponToggle=LDWidgets::Button(WidgetTree,TEXT(""),[this](){bUseCoupon=!bUseCoupon;Refresh();});All->AddChildToVerticalBox(CouponToggle);
    ProductionStatus=LDWidgets::Text(WidgetTree,TEXT("Choose a recipe or order to preview its requirements."),14);All->AddChildToVerticalBox(ProductionStatus);
    ProductionConfirm=LDWidgets::Button(WidgetTree,TEXT("Close & arm fresh hold"),[this](){ArmProductionPreview();});All->AddChildToVerticalBox(ProductionConfirm);
}
void ULDInventoryPanel::ToggleProductionInput()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto* I=PC->OwnerState.Inventory.Find(Selected);
    if(!I||I->bReserved||!LDInventory::Commodity(I->Kind)){return;}
    if(ProductionInputs.Contains(Selected)){ProductionInputs.Remove(Selected);}else if(ProductionInputs.Num()<3){ProductionInputs.Add(Selected);}bProductionPreview=false;Refresh();
}
void ULDInventoryPanel::PreviewProduction(uint8 Recipe){ShowService(Recipe>=5?4:3);ProductionRecipe=Recipe;bProductionPreview=true;Refresh();}
void ULDInventoryPanel::ArmProductionPreview()
{if(!bProductionPreview||!ProductionConfirm->GetIsEnabled()){return;}const auto Plan=DisplayedProduction;CastChecked<ALDPlayerController>(GetOwningPlayer())->ArmProduction(Plan);bProductionPreview=false;}
void ULDInventoryPanel::RefreshProduction()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto& O=PC->OwnerState;const auto* G=GetWorld()->GetGameState<ALDGameState>();const double Now=G?G->GetServerWorldTimeSeconds():0;
    uint8 Site=255;for(uint8 S=0;S<5;++S){if(PC->NearProduction(S)&&(Site==255||FVector::DistSquared2D(PC->GetPawn()->GetActorLocation(),ALDProductionDirector::Position(GetWorld(),S))<FVector::DistSquared2D(PC->GetPawn()->GetActorLocation(),ALDProductionDirector::Position(GetWorld(),Site)))){Site=S;}}
    const int32 Count=ProductionInputs.Num();ProductionInputs.RemoveAll([&](FGuid Id){const auto* I=O.Inventory.Find(Id);return !I||I->bReserved;});if(Count!=ProductionInputs.Num()){bProductionPreview=false;}
    const bool Free=!PC->InventoryPending()&&!bConfirm&&!bTradeReview;const auto* SelectedItem=O.Inventory.Find(Selected);
    ProductionInput->SetIsEnabled(SelectedItem&&!SelectedItem->bReserved&&LDInventory::Commodity(SelectedItem->Kind)&&Free);
    for(uint8 R=0;R<9;++R){ProductionActions[R]->SetVisibility((ServiceTab==4)==(R>=5)?ESlateVisibility::Visible:ESlateVisibility::Collapsed);ProductionActions[R]->SetIsEnabled(Free);}
    CouponToggle->SetVisibility(ServiceTab==3&&O.CraftCoupon.IsValid()?ESlateVisibility::Visible:ESlateVisibility::Collapsed);CouponToggle->SetIsEnabled(Free);
    CastChecked<UTextBlock>(CouponToggle->GetContent())->SetText(FText::FromString(bUseCoupon?TEXT("Coupon: use if valid"):TEXT("Coupon: leave unused")));
    FLDProductionPlan P;P.Site=Site;P.Recipe=ProductionRecipe;P.InventoryRevision=O.Inventory.Revision;P.BuildRevision=O.Build.Revision;
    if(P.Recipe==1||P.Recipe==2||P.Recipe==5||P.Recipe==6||P.Recipe==7){for(FGuid Id:ProductionInputs){P.Inputs.Add(*O.Inventory.Find(Id));}}
    const int32 Seconds=P.Recipe==8?4:P.Recipe>=5?3:P.Recipe==3?1:P.Recipe==4?2:P.Recipe==1?4:6;
    P.Fee=P.Recipe==1?4:P.Recipe==2?6:0;
    const bool Coupon=bUseCoupon&&(P.Recipe==1||P.Recipe==2)&&O.CraftCoupon.IsValid()&&O.CouponSite==Site;
    if(Coupon){P.Coupon=O.CraftCoupon;P.Fee-=2;}
    FString Text=Site<5?ALDProductionDirector::SiteName(Site):TEXT("No production service within 120 cm");
    Text+=TEXT("\nSelected inputs:");for(const auto& I:P.Inputs){Text+=FString::Printf(TEXT("\nBag %d · %s"),I.Slot+1,*LDInventory::Name(I.Kind,I.Skill));}if(P.Inputs.IsEmpty()){Text+=TEXT(" none");}
    FString Why;auto Block=[&](const FString& Reason){if(Why.IsEmpty()){Why=Reason;}};
    if(!bProductionPreview){Block(TEXT("Choose an option to preview. Select each bag input, then Add/remove."));}
    if(!O.bActive){Block(TEXT("Waiting to return; service actions unavailable"));}else if(Now<O.ProtectedUntil){Block(TEXT("Return protection must end first"));}
    if(O.DevelopmentKind||O.ChannelKind||(O.ActionCue&15)){Block(TEXT("Finish or cancel your current action"));}
    if(Site>=5){Block(TEXT("Approach the required service within 120 cm with body sight"));}
    if(bProductionPreview)
    {
        if(P.Recipe==0||P.Recipe==3)
        {if(Site>=2){Block(TEXT("Approach a WildCrossing Crystal outcrop"));}const int32 Charges=G?(Site==1?G->OutcropTwo:G->OutcropOne):0;Text+=FString::Printf(TEXT("\nOne Crystal · public charges %d/3 · no fee or XP"),Charges);if(Charges<=0){Block(TEXT("This outcrop is depleted for the match"));}}
        else if(Site<2){Block(TEXT("Approach a settlement workshop"));}
        if(P.Recipe==3||P.Recipe==4)
        {
            const ELDSkill Skill=P.Recipe==3?ELDSkill::Survey:ELDSkill::Analyze;const int32 Index=O.Build.Slots.Find(Skill);P.SkillSlot=Index>=0?uint8(Index):255;const int32 Needed=P.Recipe==3?12:14;
            Text+=FString::Printf(TEXT("\nEquipped %s · score %.0f/%d · normal cooldown"),LDDevelopment::SkillName(Skill),LDDevelopment::SkillValue(O.Build,Skill),Needed);
            if(Index<0){Block(TEXT("Learn and equip this skill at a preparation station"));}else if(LDDevelopment::SkillValue(O.Build,Skill)<Needed){Block(TEXT("Your check score is below the required threshold"));}else if(Now<O.Build.Cooldowns[uint8(Skill)]){Block(TEXT("Equipped skill is cooling down"));}
            if(P.Recipe==4){Text+=TEXT("\nOne workshop-bound coupon: 2 gold off; expires after 30s, clears on death");}
        }
        if(P.Recipe<=2||P.Recipe==5||P.Recipe==6||P.Recipe==7)
        {
            const TCHAR* Needs=P.Recipe==1?TEXT("2 Fiber + 1 Ore"):P.Recipe==2?TEXT("2 Toolkits + 1 Crystal"):P.Recipe==5?TEXT("1 Toolkit"):P.Recipe==6?TEXT("1 Core"):P.Recipe==7?TEXT("2 Toolkits"):TEXT("no inputs");
            Text+=FString(TEXT("\nRequires "))+Needs;
            if(!ALDProductionDirector::RecipeInputs(P.Recipe,P.Inputs)){Block(FString(TEXT("Select exactly "))+Needs+TEXT(" from your bag"));}
        }
        if(P.Recipe<=2)
        {
            const auto Kind=ALDProductionDirector::OutputKind(P.Recipe);TArray<FGuid> Outgoing;int32 Weight=LDInventory::Weight(O.Inventory)+LDInventory::UnitWeight(Kind);for(const auto& I:P.Inputs){Outgoing.Add(I.Id);Weight-=LDInventory::UnitWeight(I.Kind);}
            FString Capacity;if(!LDInventory::Fits(O.Inventory,O.Build,Outgoing,{Kind},&Capacity)){Block(Capacity);}if(O.Gold<P.Fee){Block(TEXT("Insufficient carried gold for the fee"));}
            if(Coupon&&O.CouponUntil<=Now+Seconds){Block(TEXT("Coupon expires before completion; disable it or earn another"));}
            Text+=FString::Printf(TEXT("\nOutput: %s · fee %dg · no XP\nFinal bag %d/%d slots · %d/%d weight"),*LDInventory::Name(Kind,ELDSkill::None),P.Fee,O.Inventory.Items.Num()-P.Inputs.Num()+1,O.Inventory.Capacity,Weight,LDInventory::WeightCapacity(O.Build));
        }
        if(P.Recipe>=5&&P.Recipe<=7)
        {
            const uint8 Required=P.Recipe==5?4:P.Recipe==6?2:3;if(Site!=Required){Block(TEXT("Approach ")+ALDProductionDirector::SiteName(Required));}
            const bool Core=P.Recipe==6;const int32 Remaining=P.Recipe==5?(G?G->ShortageRemaining:0):Core?(G?G->CoreOrdersRemaining:0):(G?G->SupplyOrdersRemaining:0);
            Text+=FString::Printf(TEXT("\nReward %dg / %d XP · public quota %d\nYour completions %d/%d · %s"),P.Recipe==5?40:Core?80:50,P.Recipe==5?20:Core?60:30,Remaining,P.Recipe==5?0:Core?O.CoreOrders:O.SupplyOrders,P.Recipe==5?1:Core?3:1,P.Recipe==5?TEXT("No endorsement"):Core?TEXT("First delivery records Industrial"):TEXT("Records distinct Supply endorsement"));
            if(Remaining<=0||(P.Recipe==6&&O.CoreOrders>=3)||(P.Recipe==7&&O.SupplyOrders>=1)){Block(TEXT("Order quota exhausted; your inputs stay owned"));}
            if(P.Recipe==5&&(!G||G->ShortagePhase!=1||Now+3>=G->MatchDeadline-LDDevelopment::DeadlineSeconds+780)){Block(TEXT("Shortage order must finish strictly before 13:00"));}
        }
        if(P.Recipe==8)
        {
            Text+=TEXT("\nRegistered Charter → 30s public warning + 60s opening for everyone");
            if(Site!=2){Block(TEXT("Approach the City civic terminal"));}if(!O.CharterPurchase.IsValid()){Block(TEXT("Purchase a registered Charter at City Treasury first"));}else if(O.CharterActivation.IsValid()){Block(TEXT("Your Charter activation is already spent"));}else if(O.bCharterAwaitingRespawn){Block(TEXT("Return first; Charter readiness begins at actual respawn"));}else if(Now<O.CharterReadyAt){Block(FString::Printf(TEXT("Charter recovery: %.1fs remaining"),O.CharterReadyAt-Now));}
            if(G&&G->CharterWindow.IsValid()&&Now<G->CharterCloseAt){Block(TEXT("A public warning or opening is already active; no extension"));}
            if(!G||Now+94>G->MatchDeadline){Block(TEXT("Too late for the full activation, warning and opening"));}
        }
        if(G&&Now+Seconds>G->MatchDeadline){Block(TEXT("The hold cannot finish before match deadline"));}
        if(O.CraftCoupon.IsValid()&&ServiceTab==3){Text+=FString::Printf(TEXT("\nCoupon: %s · %.1fs left"),*ALDProductionDirector::SiteName(O.CouponSite),FMath::Max(0.,O.CouponUntil-Now));}
        Text+=FString::Printf(TEXT("\nHold %ds · Release, movement or damage cancels\nAlive/unprotected, stationary, body sight within 120 cm. Craft/orders/activation require 6 quiet seconds."),Seconds);
    }
    Text+=Why.IsEmpty()?TEXT("\nREADY · Close and arm; release all controls, then hold ")+PC->BindingLabel(TEXT("Context")):TEXT("\nUNAVAILABLE · ")+Why;
    ProductionStatus->SetText(FText::FromString(Text));DisplayedProduction=P;ProductionConfirm->SetIsEnabled(bProductionPreview&&Why.IsEmpty()&&Free);
}
