#include "Tests/LDProductionProbe.h"
#include "Economy/LDProductionDirector.h"
#include "Economy/LDEconomyDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Combat/LDCombatDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDTradeDirector.h"
#include "Character/LDCharacter.h"
#include "UI/LDInventoryPanel.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
ALDProductionProbe::ALDProductionProbe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDProductionProbe::Tick(float Delta)
{
#if !UE_BUILD_SHIPPING
    if(Done){return;}auto* W=GetWorld();auto* M=W->GetAuthGameMode<ALDGameMode>();auto* A=Cast<ALDPlayerController>(W->GetFirstPlayerController());if(!A||!A->GetPawn()){return;}Done=true;
    int32 Failures=0;TArray<TSharedPtr<FJsonValue>> Checks;
    auto Check=[&](bool Good,const FString& Message){Failures+=!Good;Checks.Add(MakeShared<FJsonValueString>((Good?TEXT("PASS "):TEXT("FAIL "))+Message));UE_LOG(LogTemp,Display,TEXT("LD_PRODUCTION_PROBE %s %s"),Good?TEXT("PASS"):TEXT("FAIL"),*Message);};
    auto Finish=[&](){auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("failures"),Failures);R->SetArrayField(TEXT("checks"),Checks);R->SetStringField(TEXT("scope"),TEXT("Two local owners, controlled authority clock, labelled diagnostic items/gold/builds; no remote input or human travel evidence"));FString Json,Path;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));FParse::Value(FCommandLine::Get(),TEXT("LDProbeReport="),Path);if(!FFileHelper::SaveStringToFile(Json,*Path)){++Failures;}FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);};
    auto* B=Cast<ALDPlayerController>(UGameplayStatics::CreatePlayer(this,1,true));if(!B||!B->GetPawn()){Check(false,TEXT("Second owner admitted"));Finish();return;}
    FString Error;M->SetLobbyChoice(A,1,true,Error);M->SetLobbyChoice(B,2,true,Error);if(!M->StartDevelopmentMatch(A,Error)){Check(false,Error);Finish();return;}
    auto* D=M->Production.Get();auto* E=M->Economy.Get();auto* C=M->Combat.Get();auto* I=M->Inventory.Get();auto* OA=M->StateFor(A);auto* OB=M->StateFor(B);uint32 Token=30000,Sequence=0;
    OA->Gold=OB->Gold=1000;E->GoldChanged(TEXT("DiagnosticReward"),2000);C->SetActorTickEnabled(false);
    auto Body=[&](ALDPlayerController* PC){return Cast<ALDCharacter>(PC->GetPawn());};
    auto Move=[&](ALDPlayerController* PC,uint8 Site){D->Cancel(PC);auto* O=M->StateFor(PC);auto* S=C->StateFor(Body(PC));S->Action=0;S->ProtectionUntil=0;S->LastDamage=S->LastDealt=-1000;O->ProtectedUntil=0;Body(PC)->GetCharacterMovement()->StopMovementImmediately();Body(PC)->SetActorLocation(D->Position(W,Site)+FVector(-50,30,92));};
    auto Create=[&](ALDPlayerController* PC,ELDItemKind Kind){auto* P=W->SpawnActor<ALDItem>();P->Kind=Kind;const bool Good=I->Adopt(PC,P,TEXT("DiagnosticItemCreation"));if(!Good){P->Destroy();}return Good?P->Identity:FGuid();};
    auto Clear=[&](ALDPlayerController* PC){D->Cancel(PC);const auto Items=M->StateFor(PC)->Inventory.Items;for(const auto& Entry:Items){I->Remove(PC,I->Owned(PC,Entry.Id),true,TEXT("DiagnosticCleanup"));}};
    auto Plan=[&](ALDPlayerController* PC,uint8 Site,uint8 Recipe){FLDProductionPlan P;P.Site=Site;P.Recipe=Recipe;auto* O=M->StateFor(PC);P.InventoryRevision=O->Inventory.Revision;P.BuildRevision=O->Build.Revision;P.Fee=Recipe==1?4:Recipe==2?6:0;if(Recipe==1||Recipe==2){int32 First=0,Second=0;for(const auto& Entry:O->Inventory.Items){if(Entry.Kind==(Recipe==1?ELDItemKind::Fiber:ELDItemKind::Toolkit)&&First<2){P.Inputs.Add(Entry);++First;}else if(Entry.Kind==(Recipe==1?ELDItemKind::Ore:ELDItemKind::Crystal)&&Second<1){P.Inputs.Add(Entry);++Second;}}}return P;};
    auto Start=[&](ALDPlayerController* PC,const FLDProductionPlan& P){Sequence=0;D->States.FindOrAdd(PC).Tokens=3;const auto* O=M->StateFor(PC);D->Queue(PC,O->Match,O->Life,++Token,P);D->Sample(W->GetTimeSeconds());return Token;};
    auto Hold=[&](ALDPlayerController* PC,uint32 Id,double Seconds,bool Skill=false){for(double Left=Seconds;Left>1.e-7;){const double Step=FMath::Min(.1,Left);W->TimeSeconds+=Step;const auto* O=M->StateFor(PC);if(Skill){C->RefreshSkillHold(Body(PC),Id,++Sequence,false);C->Sample(W->GetTimeSeconds());}else{D->RefreshHold(PC,O->Life,Id,++Sequence,false);D->Sample(W->GetTimeSeconds());}Left-=Step;}};
    auto Inputs=[&](ALDPlayerController* PC){Create(PC,ELDItemKind::Fiber);Create(PC,ELDItemKind::Fiber);Create(PC,ELDItemKind::Ore);};
    Check(E->Accounting().Remaining(D->Budget(0))==3&&E->Accounting().Remaining(D->Budget(1))==3,TEXT("Two match-global outcrops start at three charges each"));
    Move(A,0);const int64 Gold=OA->Gold;const int64 Xp=OA->Xp;uint32 Id=Start(A,Plan(A,0,0));Hold(A,Id,5.9);Check(E->Accounting().Remaining(D->Budget(0))==3,TEXT("Basic extraction cannot finish before six seconds"));Hold(A,Id,.11);
    Check(E->Accounting().Remaining(D->Budget(0))==2&&OA->Inventory.Items.Last().Kind==ELDItemKind::Crystal&&OA->Gold==Gold&&OA->Xp==Xp,TEXT("Basic gather creates one Crystal for one charge with no gold or XP"));
    const int32 Count=OA->Inventory.Items.Num();D->Queue(A,OA->Match,OA->Life,Id,Plan(A,0,0));D->Sample(W->GetTimeSeconds());Check(OA->Inventory.Items.Num()==Count,TEXT("Gather request replay cannot duplicate extraction"));
    Id=Start(A,Plan(A,0,0));D->Release(A,OA->Life,Id);Hold(A,Id,6.1);Check(E->Accounting().Remaining(D->Budget(0))==2&&!OA->DevelopmentKind,TEXT("Early release consumes no charge"));
    Id=Start(A,Plan(A,0,0));W->TimeSeconds+=.251;D->Sample(W->GetTimeSeconds());Check(!OA->DevelopmentKind&&E->Accounting().Remaining(D->Budget(0))==2,TEXT("Stale hold expires after 250 milliseconds"));
    Id=Start(A,Plan(A,0,0));D->RefreshHold(A,OA->Life,Id,1,true);D->RefreshHold(A,OA->Life,Id,2,false);Check(!OA->DevelopmentKind,TEXT("Movement pulse cannot be erased by stationary refresh"));
    Id=Start(A,Plan(A,0,0));Hold(A,Id,6.1);Move(B,0);const uint32 IA=Start(A,Plan(A,0,0)),IB=Start(B,Plan(B,0,0));
    for(int32 N=0;N<61;++N){W->TimeSeconds+=.1;D->RefreshHold(A,OA->Life,IA,N+1,false);D->RefreshHold(B,OB->Life,IB,N+1,false);D->Sample(W->GetTimeSeconds());}
    Check(E->Accounting().Remaining(D->Budget(0))==0&&OA->Inventory.Items.FilterByPredicate([](const auto& V){return V.Kind==ELDItemKind::Crystal;}).Num()+OB->Inventory.Items.FilterByPredicate([](const auto& V){return V.Kind==ELDItemKind::Crystal;}).Num()==3,TEXT("Two owners racing final charge create exactly one final output"));
    Check(W->GetGameState<ALDGameState>()->OutcropOne==0&&E->Accounting().Remaining(D->Budget(1))==3,TEXT("Public depletion reflects actual per-node budget without refilling the other"));
    Clear(A);Move(A,2);Inputs(A);auto P=Plan(A,2,1);const auto Parents=P.Inputs;const auto Weight=LDInventory::Weight(OA->Inventory);Id=Start(A,P);
    Check(OA->DevelopmentKind==5&&OA->Gold==Gold&&LDInventory::Weight(OA->Inventory)==Weight&&I->Owned(A,Parents[0].Id)->bReserved,TEXT("Accepted crafting reserves exact owned inputs, retains mass and escrows no gold"));
    const auto Reserved=OA->Inventory.Find(Parents[0].Id)->Revision;I->Queue(A,OA->Match,OA->Life,900001,OA->Inventory.Revision,LDInventory::Drop,Parents[0].Id,Reserved,0,A->GetPawn()->GetActorLocation());I->Sample(W->GetTimeSeconds());
    Check(I->Owned(A,Parents[0].Id)&&I->Owned(A,Parents[0].Id)->bReserved,TEXT("Shared reservation rejects inventory drop during crafting"));
    D->Release(A,OA->Life,Id);Check(!I->Owned(A,Parents[0].Id)->bReserved&&OA->Gold==Gold,TEXT("Cancellation releases original instances with zero fee"));
    P=Plan(A,2,1);Id=Start(A,P);Hold(A,Id,4.1);const FGuid Toolkit=OA->Inventory.Items.Last().Id;auto* Physical=I->Owned(A,Toolkit);
    Check(Physical&&Physical->Kind==ELDItemKind::Toolkit&&Physical->ParentIds.Num()==3&&OA->Gold==Gold-4,TEXT("Toolkit consumes exact parents, burns four gold and publishes causal lineage"));
    Check(!OA->Inventory.Find(P.Inputs[0].Id)&&E->Accounting().Item(P.Inputs[0].Id)->Consumed,TEXT("Craft parent identities remain as consumed audit records"));
    Inputs(A);E->bFailNextProduction=true;const int64 BeforeFee=OA->Gold;P=Plan(A,2,1);Id=Start(A,P);Hold(A,Id,4.1);
    Check(OA->Gold==BeforeFee&&I->Owned(A,P.Inputs[0].Id)&&!I->Owned(A,P.Inputs[0].Id)->bReserved,TEXT("Failed output allocation retains all inputs and gold and releases reservation"));
    Id=Start(A,Plan(A,2,1));D->RefreshHold(A,OA->Life,Id,1,false);Body(A)->SetActorLocation(Body(A)->GetActorLocation()+FVector(3,0,0));D->Sample(W->GetTimeSeconds());Check(!OA->DevelopmentKind&&OA->Gold==BeforeFee,TEXT("Authoritative displacement cancels crafting without fee"));
    Move(A,2);Id=Start(A,Plan(A,2,1));C->StateFor(Body(A))->LastDamage=W->GetTimeSeconds();D->Sample(W->GetTimeSeconds());Check(!OA->DevelopmentKind&&OA->Gold==BeforeFee,TEXT("Positive damage invalidates channel before completion"));
    Start(A,Plan(A,2,1));Check(!OA->DevelopmentKind,TEXT("Six combat-quiet seconds required before crafting"));Move(A,2);
    P=Plan(A,2,1);++P.RecipeRevision;Start(A,P);Check(!OA->DevelopmentKind,TEXT("Stale recipe revision rejects before reserving"));
    P=Plan(A,2,1);P.Inputs[2]=P.Inputs[0];Start(A,P);Check(!OA->DevelopmentKind,TEXT("Duplicate or wrong exact recipe IDs reject"));
    for(uint8 Site:{uint8(3),uint8(4)}){Move(A,Site);Check(D->Eligible(A,Site,true),TEXT("Same native workshop access at ")+D->SiteName(Site));}
    // A conserved wallet transfer models a late loss without protecting craft gold.
    Move(A,2);P=Plan(A,2,1);Id=Start(A,P);const int64 Removed=OA->Gold-3;
    auto Transfer=[&](int64 Amount){LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=TEXT("DiagnosticWalletTransfer");const auto WA=E->Wallet(M->ContenderId(A)),WB=E->Wallet(M->ContenderId(B));T.Gold={{WA,E->Ledger.Account(WA)->Revision,-Amount},{WB,E->Ledger.Account(WB)->Revision,Amount}};const bool Good=E->Ledger.Commit(T,Error);OA->Gold=E->Ledger.Account(WA)->Gold;OB->Gold=E->Ledger.Account(WB)->Gold;return Good;};
    Check(Transfer(Removed),TEXT("Late wallet fixture is a conserved transfer, not escrow or mint"));D->Sample(W->GetTimeSeconds());Check(!OA->DevelopmentKind&&OA->Gold==3&&I->Owned(A,P.Inputs[0].Id)&&!I->Owned(A,P.Inputs[0].Id)->bReserved,TEXT("Late wallet loss below fee cancels without consuming reserved inputs"));Check(Transfer(-Removed),TEXT("Wallet fixture restoration preserves accounting"));
    Move(A,2);Move(B,2);Body(B)->SetActorLocation(Body(A)->GetActorLocation()+FVector(0,-60,0));P=Plan(A,2,1);Id=Start(A,P);Hold(A,Id,3.85);const double HitHp=C->StateFor(Body(A))->Hp;const int64 HitGold=OA->Gold;Check(C->RequestBody(Body(B),0,FVector(0,1,0),Error),TEXT("Real basic attack accepted for craft-deadline fixture"));W->TimeSeconds=D->States.FindChecked(A).Ends;D->RefreshHold(A,OA->Life,Id,++Sequence,false);C->Sample(W->GetTimeSeconds());Check(C->StateFor(Body(A))->Hp<HitHp&&!OA->DevelopmentKind&&OA->Gold==HitGold&&I->Owned(A,P.Inputs[0].Id),TEXT("Real positive hit on craft deadline wins before consumption or fee"));
    Clear(A);Move(A,2);Inputs(A);for(int32 N=0;N<21;++N){Create(A,ELDItemKind::Fiber);}Check(OA->Inventory.Items.Num()==24,TEXT("Full-bag crafting fixture has 24 real physical items"));
    Id=Start(A,Plan(A,2,1));Hold(A,Id,4.1);Check(OA->Inventory.Items.Num()==22&&OA->Inventory.Items.Last().Kind==ELDItemKind::Toolkit,TEXT("Net final capacity allows crafting from a full bag"));
    Clear(A);Move(A,1);for(int32 N=0;N<24;++N){Create(A,ELDItemKind::Fiber);}Start(A,Plan(A,1,0));Check(E->Accounting().Remaining(D->Budget(1))==3&&!OA->DevelopmentKind,TEXT("Full bag cannot spend an outcrop charge"));Clear(A);
    Move(A,1);P=Plan(A,1,3);P.SkillSlot=1;Id=Start(A,P);Check(C->StateFor(Body(A))->Action==7&&OA->Build.Cooldowns[uint8(ELDSkill::Survey)]>W->GetTimeSeconds(),TEXT("Survey uses actual equipped skill acceptance and normal cooldown"));Hold(A,Id,1.1,true);
    Check(E->Accounting().Remaining(D->Budget(1))==2&&OA->Inventory.Items.Last().Kind==ELDItemKind::Crystal,TEXT("Survey extracts through the same charge and capacity transaction in one second"));
    const auto Cooldown=OA->Build.Cooldowns[uint8(ELDSkill::Survey)];P=Plan(A,1,3);P.SkillSlot=1;Start(A,P);Check(E->Accounting().Remaining(D->Budget(1))==2&&OA->Build.Cooldowns[uint8(ELDSkill::Survey)]==Cooldown,TEXT("Production F shortcut cannot bypass normal Survey cooldown"));
    P=Plan(A,1,3);P.SkillSlot=0;Start(A,P);Check(C->StateFor(Body(A))->Action!=7,TEXT("A different equipped slot cannot authorize Survey"));
    Move(B,2);P=Plan(B,2,4);P.SkillSlot=1;const auto BeforeCooldown=OB->Build.Cooldowns[uint8(ELDSkill::Analyze)];Start(B,P);Check(!OB->CraftCoupon.IsValid()&&OB->Build.Cooldowns[uint8(ELDSkill::Analyze)]==BeforeCooldown,TEXT("Analyze score below fourteen rejects without spending cooldown"));
    // Explicit labelled build fixture; normal allocation/preparation is covered separately.
    OB->Build.Attributes[3]=14;++OB->Build.Revision;P=Plan(B,2,4);P.SkillSlot=1;Id=Start(B,P);Hold(B,Id,2.1,true);
    Check(OB->CraftCoupon.IsValid()&&OB->CouponSite==2&&OB->CouponUntil>W->GetTimeSeconds()+29,TEXT("Analyze grants one private workshop-bound thirty-second coupon"));
    const FGuid OldCoupon=OB->CraftCoupon;W->TimeSeconds=OB->Build.Cooldowns[uint8(ELDSkill::Analyze)]+.1;P=Plan(B,2,4);P.SkillSlot=1;Id=Start(B,P);Hold(B,Id,2.1,true);Check(OB->CraftCoupon.IsValid()&&OB->CraftCoupon!=OldCoupon&&OB->CouponUntil>W->GetTimeSeconds()+29,TEXT("New valid Analyze replaces the previous coupon without stacking"));
    Inputs(B);P=Plan(B,2,1);P.Coupon=OB->CraftCoupon;P.Fee=2;const FGuid Coupon=OB->CraftCoupon;Id=Start(B,P);D->Release(B,OB->Life,Id);Check(OB->CraftCoupon==Coupon,TEXT("Cancelled craft preserves Analyze coupon"));
    P=Plan(B,2,1);P.Coupon=Coupon;P.Fee=2;Id=Start(B,P);Hold(B,Id,4.1);Check(!OB->CraftCoupon.IsValid()&&OB->Gold==998,TEXT("Successful discounted craft burns two and consumes coupon once"));
    Create(B,ELDItemKind::Toolkit);Create(B,ELDItemKind::Crystal);Id=Start(B,Plan(B,2,2));Hold(B,Id,6.1);Check(OB->Inventory.Items.Last().Kind==ELDItemKind::Core&&OB->Gold==992&&I->Owned(B,OB->Inventory.Items.Last().Id)->ParentIds.Num()==3,TEXT("Core consumes two Toolkits and one Crystal in six seconds for six gold"));
    Inputs(B);OB->CraftCoupon=FGuid::NewGuid();OB->CouponSite=2;OB->CouponUntil=W->GetTimeSeconds()+3;P=Plan(B,2,1);P.Coupon=OB->CraftCoupon;P.Fee=2;Start(B,P);Check(!OB->DevelopmentKind,TEXT("Coupon unable to survive scheduled completion rejects at start"));
    OB->CouponUntil=W->GetTimeSeconds()+30;Move(B,3);P=Plan(B,3,1);P.Coupon=OB->CraftCoupon;P.Fee=2;Start(B,P);Check(!OB->DevelopmentKind,TEXT("Coupon cannot be used at another workshop"));
    Move(B,2);P=Plan(B,2,1);P.Coupon=OB->CraftCoupon;P.Fee=2;Id=Start(B,P);OB->CouponUntil=W->GetTimeSeconds()+1;Hold(B,Id,1.1);Check(!OB->DevelopmentKind&&OB->Gold==992&&I->Owned(B,P.Inputs[0].Id),TEXT("Coupon expiry is revalidated during channel with no spend"));
    // Exercise actual native preview/arm methods; this does not claim physical input QA.
    B->ToggleInventoryPanel();if(B->InventoryUI){auto* UI=B->InventoryUI.Get();UI->ProductionInputs.Reset();P=Plan(B,2,1);for(const auto& Input:P.Inputs){UI->Select(Input.Id);UI->ToggleProductionInput();}UI->PreviewProduction(1);Check(UI->DisplayedProduction.Inputs.Num()==3&&UI->DisplayedProduction.Fee==4,TEXT("Native panel previews three selected IDs and exact fee"));UI->ArmProductionPreview();Check(B->bProductionArmed&&!B->InventoryUI->IsOpen()&&!OB->DevelopmentKind&&B->bFreshInputRequired,TEXT("Native preview closes and arms fresh hold without starting or spending"));B->CancelArmed();}
    Move(B,2);Id=Start(B,Plan(B,2,1));const auto DeathInputs=Plan(B,2,1).Inputs;OB->CraftCoupon=FGuid::NewGuid();OB->CouponUntil=W->GetTimeSeconds()+30;M->EndContenderLife(B,W->GetTimeSeconds());Check(!OB->CraftCoupon.IsValid()&&!OB->DevelopmentKind&&!OB->bActive,TEXT("Real contender death cancels production and clears private coupon before drops"));
    const auto Charges=E->Accounting().Remaining(D->Budget(0));W->TimeSeconds=OB->ReturnAt+.1;M->ReturnDueContenders(W->GetTimeSeconds());Check(OB->bActive&&E->Accounting().Remaining(D->Budget(0))==Charges,TEXT("Actual respawn never refills match-global outcrops"));
    Check(E->AuditFailures==0&&E->Audit(Error),TEXT("All production paths reconcile actual physical items, accounts and lineage"));Check(E->Export(TEXT("B63Verified"),Error),TEXT("Complete production ledger exported for independent replay"));Finish();
#endif
}
