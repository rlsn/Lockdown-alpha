#include "Economy/LDB92Economy.h"
#include "Economy/LDB92Services.h"
#include "Population/LDB92Roster.h"
#include "Inventory/LDInventoryDirector.h"
#include "Region/LDMapDefinition.h"
#include "World/LDWorldObjects.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

TArray<FName> ALDB92Economy::CourierBoards(const UWorld* W)
{
    TArray<FName> Out;if(const auto* Map=ULDMapDefinition::Get(W)){for(const auto& S:Map->Services){if(S.Functions.Contains(TEXT("courier_board"))){Out.Add(S.POI);}}}
    Out.Sort(FNameLexicalLess());return Out;
}
int32 ALDB92Economy::CourierDistance(const UWorld* W,FName From,FName To)
{
    const auto* Map=ULDMapDefinition::Get(W);if(!Map||!Map->Find(From)||!Map->Find(To)){return -1;}
    TMap<FName,int32> Distance;TSet<FName> ClosedNodes;Distance.Add(From,0);
    while(true)
    {
        FName NextNode;int32 Best=MAX_int32;for(const auto& P:Distance){if(!ClosedNodes.Contains(P.Key)&&P.Value<Best){NextNode=P.Key;Best=P.Value;}}
        if(NextNode.IsNone()){return -1;}if(NextNode==To){return Best;}ClosedNodes.Add(NextNode);
        for(const auto& Edge:Map->Routes)
        {
            const FName Other=Edge.From==NextNode?Edge.To:Edge.To==NextNode?Edge.From:NAME_None;if(Other.IsNone()||ClosedNodes.Contains(Other)){continue;}
            double Length=0;for(int32 N=1;N<Edge.Points.Num();++N){Length+=FVector::Dist2D(Edge.Points[N-1],Edge.Points[N]);}
            const int32 Candidate=Best+FMath::RoundToInt(Length);const auto* Old=Distance.Find(Other);if(!Old||Candidate<*Old){Distance.Add(Other,Candidate);}
        }
    }
}
FName ALDB92Economy::CourierBudget(FGuid Id,bool Issuance)
{return FName(*((Issuance?TEXT("CourierIssuance:"):TEXT("Courier:"))+Id.ToString(EGuidFormats::Digits)));}
FLDB92CourierView ALDB92Economy::CourierView(ALDB92Controller* PC,FName Board) const
{
    FLDB92CourierView V;auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto Id=M?M->ContenderId(PC):FGuid();if(!Ready||!FullWorld||!Seats.Contains(Id)){return V;}
    V.Issuances=Ledger.Remaining(CourierBudget(Id,true));V.Payments=Ledger.Remaining(CourierBudget(Id,false));const auto* Job=Couriers.Find(Id);V.Revision=Job?Job->Revision:1;
    if(Job&&Job->Active){V.Contract=Job->Contract;V.Token=Job->Token;V.Origin=Job->Origin;V.Destination=Job->Destination;V.Distance=Job->Distance;}
    const auto Boards=CourierBoards(GetWorld());V.Board=Boards.Contains(Board);
    if(V.Board&&!V.Contract.IsValid()&&V.Issuances>0&&V.Payments>0)
    {
        TArray<FName> Options;for(FName Target:Boards){const int32 D=CourierDistance(GetWorld(),Board,Target);if(Target!=Board&&D>=25000&&D<=80000){Options.Add(Target);}}
        if(!Options.IsEmpty()){V.Offer=Options[(Catalog.CourierIssuanceLimit-V.Issuances)%Options.Num()];V.Distance=CourierDistance(GetWorld(),Board,V.Offer);}
    }
    return V;
}
bool ALDB92Economy::CourierTerms(ALDB92Controller* PC,FName POI,const FLDB92MarketOrder& R,bool Reserved,FString& Error) const
{
    auto Reject=[&](const TCHAR* Why){Error=Why;return false;};auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M?M->StateFor(PC):nullptr;const auto Id=M?M->ContenderId(PC):FGuid();const auto* A=Ledger.Account(Wallet(Id));
    if(!HasAuthority()||!Ready||!FullWorld||Closed||!O||!O->bActive||!Seats.Contains(Id)||!CourierBoards(GetWorld()).Contains(POI)||R.CourierAction<1||R.CourierAction>3||R.WorkRecipe||R.Supply||R.BankAction||R.Amount||R.BankRevision||R.Price||R.Revision||R.Item.IsValid()||R.ItemRevision||!R.Buy||R.Kind!=ELDItemKind::SealedParcel||R.Skill!=ELDSkill::None||O->Inventory.Revision>=MAX_uint32-4||R.InventoryRevision!=O->Inventory.Revision||!A||A->Gold!=O->Gold||A->Revision!=R.WalletRevision)
    {return Reject(TEXT("Courier access, bag, wallet or request terms changed"));}
    const auto V=CourierView(PC,POI);if(R.CourierRevision!=V.Revision||V.Revision==MAX_uint32){return Reject(TEXT("Courier contract changed; refresh the board"));}
    if(R.CourierAction==1)
    {
        if(V.Contract.IsValid()||V.Offer.IsNone()||R.Destination!=V.Offer||R.CourierToken.IsValid()||!R.Inputs.IsEmpty()||Reserved){return Reject(TEXT("One active contract, paid/issuance limits and the displayed route are required"));}
        return LDInventory::Fits(O->Inventory,O->Build,{}, {ELDItemKind::SealedParcel},&Error);
    }
    if(!V.Contract.IsValid()||R.CourierToken!=V.Token||R.Destination!=V.Destination){return Reject(TEXT("This is not your current sealed contract"));}
    if(R.CourierAction==3){if(!R.Inputs.IsEmpty()||Reserved){return Reject(TEXT("Abandonment has no material or reward terms"));}return true;}
    if(POI!=V.Destination||V.Payments<=0||A->Gold>MAX_int64-25||R.Inputs.Num()!=1||R.Inputs[0].Id!=V.Token||R.Inputs[0].Kind!=ELDItemKind::SealedParcel){return Reject(TEXT("Deliver your exact parcel at its assigned board; payment quota must remain"));}
    auto* P=M->Inventory->Owned(PC,V.Token);const auto* Own=O->Inventory.Find(V.Token);const auto* L=Ledger.Item(V.Token);
    if(!P||!Own||!L||L->Consumed||L->Location!=Wallet(Id)||P->Kind!=ELDItemKind::SealedParcel||P->OriginReceipt!=V.Contract||P->Revision!=R.Inputs[0].Revision||Own->Revision!=P->Revision||P->BindingMask||Own->BindingMask||P->bReserved!=Reserved||Own->bReserved!=Reserved||P->Revision>=MAX_uint32-2)
    {return Reject(TEXT("The original parcel must be recovered and available in your own bag"));}return true;
}
bool ALDB92Economy::CourierCommand(ALDB92Controller* PC,FName POI,const FLDB92MarketOrder& R,FGuid Receipt,FString& Error,bool Reserved)
{
    if(!Receipt.IsValid()||Ledger.Receipt(Receipt)||!SynchronizeWorld(Error)||!Audit(Error)||!CourierTerms(PC,POI,R,Reserved,Error)){return false;}
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M->StateFor(PC);const auto Id=M->ContenderId(PC);const auto W=Wallet(Id);ALDItem* Parcel=nullptr;
    LDEconomy::FTransaction T;T.Id=Receipt;
    if(R.CourierAction==1)
    {
        Parcel=GetWorld()->SpawnActor<ALDItem>();if(!Parcel){Error=TEXT("Parcel allocation failed; issuance retained");return false;}Parcel->Kind=ELDItemKind::SealedParcel;
        T.Cause=TEXT("B92CourierIssuance");T.Budget=CourierBudget(Id,true);T.Units=1;LDEconomy::FItemDelta D;D.Id=Parcel->Identity;D.Kind=Parcel->Kind;D.Op=LDEconomy::EItemOp::Create;D.To=W;T.Items.Add(D);
    }
    else if(R.CourierAction==2)
    {
        if(!Reserved){Error=TEXT("Delivery needs its accepted channel");return false;}Parcel=M->Inventory->Owned(PC,R.CourierToken);T.Cause=TEXT("B92CourierDelivery");T.Mint=25;T.Gold.Add({W,Ledger.Account(W)->Revision,25});T.Budget=CourierBudget(Id,false);T.Units=1;
        LDEconomy::FItemDelta D;D.Id=Parcel->Identity;D.Kind=Parcel->Kind;D.Op=LDEconomy::EItemOp::Consume;D.From=W;D.Expected=Ledger.Item(D.Id)->Revision;T.Items.Add(D);
    }
    else{T.Cause=TEXT("B92CourierAbandon");}
    if(!Ledger.Commit(T,Error)){if(Parcel&&R.CourierAction==1){Parcel->Destroy();}return false;}
    auto& Job=Couriers.FindOrAdd(Id);++Job.Revision;
    if(R.CourierAction==1)
    {
        uint8 Slot=0;while(O->Inventory.Items.ContainsByPredicate([&](const auto& I){return I.Slot==Slot;})){++Slot;}
        Parcel->Holder=PC;Parcel->State=1;Parcel->Slot=Slot;Parcel->OriginReceipt=Receipt;++Parcel->Revision;FLDInventoryItem I;Parcel->Project(I);O->Inventory.Items.Add(I);++O->Inventory.Revision;Custody.Add(Parcel);
        Job.Contract=Receipt;Job.Token=Parcel->Identity;Job.Origin=POI;Job.Destination=R.Destination;Job.Distance=CourierDistance(GetWorld(),POI,R.Destination);Job.Active=true;Job.Owner=Id;Job.Terminal.Invalidate();CourierArchive.Add(Job.Contract,Job);Error=TEXT("Sealed parcel issued; deliver it at the assigned board");
    }
    else
    {
        Job.Active=false;Job.Terminal=Receipt;CourierArchive.Add(Job.Contract,Job);
        if(R.CourierAction==2)
        {
            ALDWorldObjects::Get(GetWorld())->RecordConsumption(Parcel);LDInventory::Remove(O->Inventory,Parcel->Identity);++O->Inventory.Revision;Parcel->bReserved=false;Parcel->State=2;Parcel->Holder.Reset();Custody.Remove(Parcel);Parcel->Destroy();O->Gold=Ledger.Account(W)->Gold;++O->PaidJobs;++O->DeliverySerial;M->AwardExperience(PC,45);Error=TEXT("Courier delivered once: +25 gold / +45 XP");
        }
        else{Error=TEXT("Contract abandoned without payout; its parcel is now void cargo");}
    }
    M->Inventory->Changed(PC);return true;
}

FString ALDB92Economy::ExportCouriers() const
{
    auto Root=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Rows;
    for(const auto& Pair:CourierArchive){const auto& J=Pair.Value;auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("contract"),J.Contract.ToString());V->SetStringField(TEXT("token"),J.Token.ToString());V->SetStringField(TEXT("owner"),Wallet(J.Owner).ToString());V->SetStringField(TEXT("origin"),J.Origin.ToString());V->SetStringField(TEXT("destination"),J.Destination.ToString());V->SetNumberField(TEXT("distance_cm"),J.Distance);V->SetStringField(TEXT("terminal"),J.Terminal.ToString());V->SetBoolField(TEXT("active"),J.Active);Rows.Add(MakeShared<FJsonValueObject>(V));}
    Root->SetArrayField(TEXT("contracts"),Rows);FString Json;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));return Json;
}
