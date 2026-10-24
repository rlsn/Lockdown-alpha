#include "Tests/LDAcquisitionProbe.h"
#include "Inventory/LDAcquisitionDirector.h"
#include "Inventory/LDAcquisitionCatalog.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDTradeDirector.h"
#include "Development/LDDevelopmentDirector.h"
#include "Economy/LDEconomyDirector.h"
#include "Economy/LDProductionDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Combat/LDCombatDirector.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Region/LDRegionDefinition.h"
#include "Vision/LDVisionDirector.h"
#include "Character/LDCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
ALDAcquisitionProbe::ALDAcquisitionProbe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDAcquisitionProbe::Tick(float Delta)
{
#if !UE_BUILD_SHIPPING
    if(Done){return;}auto* W=GetWorld();auto* M=W->GetAuthGameMode<ALDGameMode>();auto* A=Cast<ALDPlayerController>(W->GetFirstPlayerController());if(!A||!A->GetPawn()){return;}Done=true;
    int32 Failures=0;TArray<TSharedPtr<FJsonValue>> Checks;auto Check=[&](bool Good,const FString& Text){Failures+=!Good;Checks.Add(MakeShared<FJsonValueString>((Good?TEXT("PASS "):TEXT("FAIL "))+Text));UE_LOG(LogTemp,Display,TEXT("LD_B74 %s %s"),Good?TEXT("PASS"):TEXT("FAIL"),*Text);};
    auto* B=Cast<ALDPlayerController>(UGameplayStatics::CreatePlayer(this,1,true));FString Why;M->SetLobbyChoice(A,0,true,Why);if(B){M->SetLobbyChoice(B,1,true,Why);}if(!B||!M->StartDevelopmentMatch(A,Why)){Check(false,Why);return;}
    auto* E=M->Economy.Get();auto* D=M->Production.Get();auto* C=M->Combat.Get();auto* I=M->Inventory.Get();auto* Obj=M->Objectives.Get();auto* Acq=M->Acquisition.Get();auto* Dev=M->Development.Get();const auto* Catalog=ULDAcquisitionCatalog::Load();C->SetActorTickEnabled(false);uint32 Token=40000,Seq=0;
    auto Body=[&](ALDPlayerController* P){return Cast<ALDCharacter>(P->GetPawn());};
    auto Place=[&](ALDPlayerController* P,FVector At){D->Cancel(P);Obj->Interrupt(P,W->GetTimeSeconds(),true);auto* S=C->StateFor(Body(P));S->Action=0;S->ProtectionUntil=0;S->LastDamage=S->LastDealt=-1000;M->StateFor(P)->ProtectedUntil=0;Body(P)->GetCharacterMovement()->StopMovementImmediately();Body(P)->SetActorLocation(At+FVector(0,50,92));};
    auto Site=[&](ALDPlayerController* P,uint8 S){Place(P,LDRegion::Site(W,S));};
    auto Hold=[&](ALDPlayerController* P,uint32 Id,double Seconds,uint8 Kind=0){for(double Left=Seconds;Left>1.e-7;){const double Dt=FMath::Min(.1,Left);W->TimeSeconds+=Dt;auto* Own=M->StateFor(P);if(Kind==1){Obj->RefreshHold(P,Own->Life,Id,++Seq,false,W->GetTimeSeconds());Obj->Sample(W->GetTimeSeconds());}else if(Kind==2){Dev->RefreshHold(P,Own->Life,Id,++Seq,false);Dev->Sample(W->GetTimeSeconds());}else{D->RefreshHold(P,Own->Life,Id,++Seq,false);D->Sample(W->GetTimeSeconds());}Left-=Dt;}};
    auto Start=[&](ALDPlayerController* P,const FLDProductionPlan& Plan){Seq=0;auto* O=M->StateFor(P);D->Queue(P,O->Match,O->Life,++Token,Plan);D->Sample(W->GetTimeSeconds());return Token;};
    auto Context=[&](ALDPlayerController* P,uint8 S){Seq=0;W->TimeSeconds+=.4;auto* O=M->StateFor(P);Obj->Queue(P,O->Match,O->Life,++Token,S,FGuid());Obj->Sample(W->GetTimeSeconds());return Token;};
    auto Find=[&](ALDPlayerController* P,ELDItemKind K){const auto* V=M->StateFor(P)->Inventory.Items.FindByPredicate([&](const auto& V){return V.Kind==K;});return V?V->Id:FGuid();};
    auto Buy=[&](ALDPlayerController* P,ELDItemKind K,uint8 Service=0){W->TimeSeconds+=.5;Place(P,ALDCommerceDirector::Position(W,Service));auto* O=M->StateFor(P);auto* V=M->Commerce.Get();V->Open(P,O->Match,O->Life,++Token,Service);const auto Q=V->Clients.FindChecked(P).View;const auto* Offer=Q.Offers.FindByPredicate([&](const auto& Row){return Row.Kind==K;});if(!Offer){Check(false,TEXT("Official commodity quote"));return;}const int32 Count=O->Inventory.Items.Num();V->Queue(P,O->Match,O->Life,++Token,Q.Session,Q.Revision,1,K,FGuid(),0,O->Inventory.Revision,Offer->Ask);V->Sample(W->GetTimeSeconds());Check(O->Inventory.Items.Num()==Count+1,TEXT("Actual paid commodity creation; no grant"));V->Invalidate(P);};
    auto CommodityPlan=[&](ALDPlayerController* P,uint8 Recipe){auto* O=M->StateFor(P);FLDProductionPlan Plan;Plan.Site=2;Plan.Recipe=Recipe;Plan.InventoryRevision=O->Inventory.Revision;Plan.BuildRevision=O->Build.Revision;Plan.Fee=Recipe==1?4:6;int32 N=0;for(const auto& V:O->Inventory.Items){if(V.Kind==(Recipe==1?ELDItemKind::Fiber:ELDItemKind::Toolkit)&&N++<2){Plan.Inputs.Add(V);}}const FGuid Second=Find(P,Recipe==1?ELDItemKind::Ore:ELDItemKind::Crystal);if(const auto* V=O->Inventory.Find(Second)){Plan.Inputs.Add(*V);}return Plan;};
    auto Toolkit=[&](ALDPlayerController* P){Buy(P,ELDItemKind::Fiber);Buy(P,ELDItemKind::Fiber);Buy(P,ELDItemKind::Ore);Site(P,1);const uint32 Id=Start(P,CommodityPlan(P,1));Hold(P,Id,4.01);Check(Find(P,ELDItemKind::Toolkit).IsValid(),TEXT("Toolkit assembled through retained normal production"));};
    auto Supply=[&](ALDPlayerController* P,uint8 S,ELDItemKind K,ELDSkill Skill=ELDSkill::None){W->TimeSeconds+=.5;Place(P,ALDProductionDirector::Position(W,S));auto* O=M->StateFor(P);Acq->Purchase(P,O->Match,O->Life,++Token,S,K,Skill,O->Inventory.Revision,Catalog->Checksum);};
    auto Craft=[&](ALDPlayerController* P,uint8 R,uint8 Mode=0,FGuid Target=FGuid()){Place(P,ALDProductionDirector::Position(W,R==33||Mode||R==28?4:2));auto* O=M->StateFor(P);FLDProductionPlan Plan;const uint8 S=R==33||Mode||R==28?4:2;Check(Acq->Preview(O->Inventory,O->Build,R,Mode,S,Target,Plan,Why),TEXT("Native exact recipe preview: ")+Why);const uint32 Id=Start(P,Plan);Check(O->DevelopmentKind!=0,TEXT("Held transaction accepted and inputs reserved"));Hold(P,Id,Acq->HoldSeconds(Plan)+.01);Check(!O->DevelopmentKind,TEXT("Held transaction completes or safely releases"));};
    auto* OA=M->StateFor(A);auto* OB=M->StateFor(B);Check(OA->Gold==0&&OB->Gold==0&&!E->AllowDiagnosticItems(),TEXT("Both owners start without money; diagnostic item grants disabled"));
    for(auto* P:{A,B}){for(int32 Job=0;Job<8;++Job){auto* O=M->StateFor(P);W->TimeSeconds=FMath::Max(W->GetTimeSeconds()+.4,double(O->WorkReadyAt)+.01);Site(P,1);Context(P,1);const auto* N=LDRegion::Definition(W)->Node(O->ContractDestination);if(!N){Check(false,TEXT("Courier destination assigned"));break;}Site(P,N->Site);uint32 Id=Context(P,N->Site);Hold(P,Id,3.01,1);Site(P,1);Id=Context(P,1);Hold(P,Id,3.01,1);}Check(M->StateFor(P)->Gold==200&&M->StateFor(P)->PaidJobs==8,TEXT("Eight real courier jobs earn exactly200gold; no reward setter"));}
    Supply(A,2,ELDItemKind::SimpleStaff);const FGuid Staff=Find(A,ELDItemKind::SimpleStaff);Toolkit(A);Craft(A,32,0,Staff);Check(I->Owned(A,Staff)&&I->Owned(A,Staff)->Quality==1,TEXT("Fine upgrade preserves physical ID"));Toolkit(A);Buy(A,ELDItemKind::Crystal,1);Craft(A,33,0,Staff);Check(I->Owned(A,Staff)&&I->Owned(A,Staff)->Quality==2,TEXT("Masterwork upgrade preserves physical ID"));
    auto Exchange=[&](ALDPlayerController* From,ALDPlayerController* To)
    {
        Place(From,ALDProductionDirector::Position(W,2));Place(To,ALDProductionDirector::Position(W,2)+FVector(60,0,0));M->Vision->Tick(0);FGuid Observed;const auto* V=M->Vision->AuthorityViewFor(From);if(V){for(const auto& Entity:V->Entities){if(M->Vision->ResolveObserved(From,Entity.Id)==To->GetPawn()){Observed=Entity.Id;}}}
        auto Command=[&](ALDPlayerController* P,uint8 K,FGuid Item=FGuid()){const auto T=P->TradeView;const auto* O=M->StateFor(P);M->Trade->Queue(P,O->Match,O->Life,++Token,K,T.Session,T.Revision,T.Digest,Item,0);W->TimeSeconds+=.4;M->Trade->Sample(W->GetTimeSeconds());};
        Command(From,LDTrade::Invite,Observed);Command(To,LDTrade::Accept);Command(From,LDTrade::Add,Staff);Command(From,LDTrade::Ready);Command(To,LDTrade::Ready);Command(From,LDTrade::Confirm);Command(To,LDTrade::Confirm);
        Check(I->Owned(To,Staff)&&I->Owned(To,Staff)->Quality==2&&!I->Owned(From,Staff),TEXT("Actual consent exchange preserves same Masterwork ID/quality"));
        Check(From->TradeView.Stage==ELDTradeStage::Committed&&From->TradeView.OwnItems.Num()==1&&From->TradeView.OwnItems[0].Quality==2,TEXT("Completed trade receipt retains voluntarily offered quality after ownership changes"));
    };
    Exchange(A,B);Exchange(B,A);
    Toolkit(A);Buy(A,ELDItemKind::Crystal,1);Craft(A,21);const FGuid Conduit=Find(A,ELDItemKind::ConduitStaff);Check(Conduit.IsValid()&&!I->Owned(A,Staff)&&I->Owned(A,Conduit)->Quality==0&&I->Owned(A,Conduit)->ParentIds.Contains(Staff),TEXT("Branch consumes Masterwork staff, outputs Standard Conduit with parent lineage"));
    Supply(A,3,ELDItemKind::Book,ELDSkill::ForceWave);const auto* Book=OA->Inventory.Items.FindByPredicate([](const auto& V){return V.Kind==ELDItemKind::Book&&V.Skill==ELDSkill::ForceWave;});if(Book){Site(A,2);Seq=0;const uint32 Id=++Token;I->Queue(A,OA->Match,OA->Life,Id,OA->Inventory.Revision,LDInventory::Read,Book->Id,Book->Revision,0,FVector::ZeroVector);I->Sample(W->GetTimeSeconds());Dev->Sample(W->GetTimeSeconds());Hold(A,Id,3.01,2);Check(LDDevelopment::Rank(OA->Build,ELDSkill::ForceWave)==1,TEXT("Paid CircleII book reads through actual held inventory request"));auto P=LDDevelopment::PlanFrom(OA->Build);P.Ranks[28]=2;Seq=0;const uint32 Train=++Token;Dev->Queue(A,OA->Match,OA->Life,Train,LDDevelopmentTransaction::Prepare,ELDSkill::None,FGuid(),P);Dev->Sample(W->GetTimeSeconds());Hold(A,Train,2.01,2);Check(OA->Build.Ranks[28]==2,TEXT("Earned mastery point trains paid cross-class knowledge to rank2"));}else{Check(false,TEXT("Trainer creates book"));}
    // B earns a Core procurement margin before paying for the rare work order.
    Toolkit(B);Toolkit(B);Buy(B,ELDItemKind::Crystal,1);Site(B,1);auto Id=Start(B,CommodityPlan(B,2));Hold(B,Id,6.01);FLDProductionPlan Order;Order.Site=2;Order.Recipe=6;Order.InventoryRevision=OB->Inventory.Revision;Order.BuildRevision=OB->Build.Revision;if(const auto* Core=OB->Inventory.Find(Find(B,ELDItemKind::Core))){Order.Inputs.Add(*Core);}Id=Start(B,Order);Hold(B,Id,3.01);
    Toolkit(B);Toolkit(B);Buy(B,ELDItemKind::Crystal,1);Site(B,1);Id=Start(B,CommodityPlan(B,2));Hold(B,Id,6.01);
    Place(B,ALDProductionDirector::Position(W,8));FLDProductionPlan Open;Acq->Preview(OB->Inventory,OB->Build,34,0,8,FGuid(),Open,Why);Id=Start(B,Open);Hold(B,Id,4.01);FGuid Catalyst;for(const auto& V:B->CacheItems){if(V.Kind==ELDItemKind::ArcaneCatalyst){Catalyst=V.Id;}}Check(Catalyst.IsValid(),TEXT("Quiet cache opening exposes three unique shared contents without transferring"));FLDProductionPlan Take;Acq->Preview(OB->Inventory,OB->Build,35,0,8,Catalyst,Take,Why);Id=Start(B,Take);Hold(B,Id,.11);Check(Find(B,ELDItemKind::ArcaneCatalyst)==Catalyst,TEXT("Explicit capacity-checked catalyst pickup transfers the endowed ID"));
    Craft(B,28,1);const FGuid Sigil=Find(B,ELDItemKind::ScholarsSigil);Check(Sigil.IsValid()&&!I->Owned(B,Catalyst)&&I->Owned(B,Sigil)->ParentIds.Num()==3,TEXT("Completed Scholar work order buys missing RunicStaff, requires real Core/catalyst and records three parents"));
    const int32 Before=E->Accounting().ReceiptCount();const int64 Gold=OB->Gold;Place(B,ALDProductionDirector::Position(W,4));FLDProductionPlan Failed;Check(!Acq->Preview(OB->Inventory,OB->Build,28,1,4,FGuid(),Failed,Why)&&OB->Gold==Gold&&E->Accounting().ReceiptCount()==Before,TEXT("Missing scarce catalyst refuses completed order without consuming or charging"));
    Check(Acq->Initialize(Why)&&E->Accounting().Remaining(TEXT("B74Endowment"))==0,TEXT("Reinitializing acquisition never refills ruins"));Check(E->Audit(Why)&&E->AuditFailures==0,TEXT("Earned purchases, Standard branch, same-ID quality and transient lineage reconcile"));Check(E->Accounting().Minted()==480,TEXT("Only sixteen courier rewards and one Core order minted gold"));
    auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("failures"),Failures);R->SetArrayField(TEXT("checks"),Checks);R->SetStringField(TEXT("scope"),TEXT("Native local two-owner earned B74 journey. Actual couriers, paid components, production, quality, branch, trainer read/mastery and scarce work order. No diagnostic money/items/XP. Controlled placement, protection/quiet setup and authoritative clock advances; not human travel or remote networking."));FString Json,Path;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));FParse::Value(FCommandLine::Get(),TEXT("LDProbeReport="),Path);FFileHelper::SaveStringToFile(Json,*Path);FFileHelper::SaveStringToFile(E->Accounting().ExportJson(TEXT("B74Journey")),*(FPaths::GetPath(Path)/TEXT("Ledger.json")));FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
#endif
}
