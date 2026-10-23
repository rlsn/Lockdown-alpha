#include "Economy/LDEconomyDirector.h"
#include "Economy/LDProductionDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Inventory/LDInventoryDirector.h"
#include "Engine/World.h"
bool ALDEconomyDirector::CommitProduction(ALDPlayerController* PC,uint8 Site,uint8 Recipe,const TArray<FLDInventoryItem>& Inputs,int32 Fee,FGuid& Output,FString& Error)
{
    Output.Invalidate();Error.Reset();auto Reject=[&](const TCHAR* Why){Error=Why;return false;};
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M?M->StateFor(PC):nullptr;
    if(!Initialized||!M->IsPlaying()||!O||!O->bActive||Site>4||Recipe>2||((Site<2)!=(Recipe==0))||!ALDProductionDirector::RecipeInputs(Recipe,Inputs)) {return Reject(TEXT("Invalid production transaction"));}
    if(!Audit(Error)){return false;}
    const auto Kind=ALDProductionDirector::OutputKind(Recipe);TArray<FGuid> Parents;TArray<ALDItem*> Physical;
    const FName W=Wallet(M->ContenderId(PC));const auto* Account=Ledger.Account(W);
    const int32 Base=Recipe==1?Catalog.ToolkitFee:Recipe==2?Catalog.CoreFee:0;
    if(!Account||Account->Gold!=O->Gold||Fee<0||(Recipe==0?Fee!=0:(Fee!=Base&&Fee!=FMath::Max(0,Base-Catalog.CouponDiscount)))||O->Gold<Fee){return Reject(TEXT("Carried gold or fee changed"));}
    if(Recipe&&Fee<Base&&(!O->CraftCoupon.IsValid()||O->CouponSite!=Site||O->CouponUntil<=GetWorld()->GetTimeSeconds())){return Reject(TEXT("Workshop coupon expired"));}
    for(const auto& I:Inputs)
    {
        auto* P=M->Inventory->Owned(PC,I.Id);const auto* Own=O->Inventory.Find(I.Id);const auto* L=Ledger.Item(I.Id);
        if(!P||!Own||!L||L->Consumed||L->Location!=W||P->Kind!=I.Kind||P->Revision!=I.Revision||Own->Revision!=I.Revision||!P->bReserved||!Own->bReserved||P->Revision==MAX_uint32){return Reject(TEXT("Reserved input changed"));}
        Parents.Add(I.Id);Physical.Add(P);
    }
    if(!LDInventory::Fits(O->Inventory,O->Build,Parents,{Kind},&Error)){return false;}
    if(!Recipe&&Ledger.Remaining(ALDProductionDirector::Budget(Site))<=0){return Reject(TEXT("Outcrop depleted; nothing extracted"));}
#if !UE_BUILD_SHIPPING
    if(bFailNextProduction&&AllowDiagnosticItems()){bFailNextProduction=false;return Reject(TEXT("Output allocation failed; nothing spent"));}
#endif
    auto* Created=GetWorld()->SpawnActor<ALDItem>();if(!Created){return Reject(TEXT("Output allocation failed; nothing spent"));}Created->Kind=Kind;
    LDEconomy::FTransaction T;T.Id=FGuid::NewGuid();T.Cause=Recipe?TEXT("CraftProduction"):TEXT("CrystalExtraction");T.Burn=Fee;
    if(Fee){T.Gold.Add({W,Account->Revision,-int64(Fee)});}if(!Recipe){T.Budget=ALDProductionDirector::Budget(Site);T.Units=1;}
    for(auto* P:Physical){const auto* L=Ledger.Item(P->Identity);LDEconomy::FItemDelta D;D.Id=P->Identity;D.Kind=P->Kind;D.Op=LDEconomy::EItemOp::Consume;D.From=W;D.Expected=L->Revision;T.Items.Add(D);}
    LDEconomy::FItemDelta D;D.Id=Created->Identity;D.Kind=Kind;D.Op=LDEconomy::EItemOp::Create;D.To=W;D.Parents=Parents;T.Items.Add(D);
    if(!Ledger.Commit(T,Error)){Created->Destroy();return false;}
    // All allocation/validation precedes the single ledger boundary. Publish only
    // after the physical bag, wallet, parents and optional coupon are projected.
    for(auto* P:Physical){LDInventory::Remove(O->Inventory,P->Identity);P->bReserved=false;P->State=2;P->Holder.Reset();P->Destroy();}
    uint8 Slot=0;while(O->Inventory.Items.ContainsByPredicate([&](const auto& I){return I.Slot==Slot;})){++Slot;}
    Created->Holder=PC;Created->State=1;Created->Slot=Slot;Created->OriginReceipt=T.Id;Created->ParentIds=Parents;++Created->Revision;
    FLDInventoryItem Entry;Entry.Id=Created->Identity;Entry.Kind=Kind;Entry.Slot=Slot;Entry.Revision=Created->Revision;O->Inventory.Items.Add(Entry);++O->Inventory.Revision;
    O->Gold=Ledger.Account(W)->Gold;if(Recipe&&Fee<Base){O->CraftCoupon.Invalidate();O->CouponUntil=0;}
    if(!Recipe){auto* G=GetWorld()->GetGameState<ALDGameState>();if(Site){G->OutcropTwo=Ledger.Remaining(ALDProductionDirector::Budget(Site));}else{G->OutcropOne=Ledger.Remaining(ALDProductionDirector::Budget(Site));}G->ForceNetUpdate();}
    Output=Created->Identity;M->Inventory->Changed(PC);
    if(!Audit(Error)){++AuditFailures;UE_LOG(LogTemp,Error,TEXT("LD_PRODUCTION_AUDIT %s"),*Error);}
    UE_LOG(LogTemp,Display,TEXT("LD_PRODUCTION_RECEIPT id=%s site=%u recipe=%u fee=%d output=%s"),*T.Id.ToString(),Site,Recipe,Fee,*Output.ToString());return true;
}
