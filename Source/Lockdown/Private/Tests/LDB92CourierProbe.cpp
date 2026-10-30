#include "Tests/LDB92ServicesProbe.h"
#include "Population/LDB92Roster.h"
#include "Economy/LDB92Economy.h"
#include "Inventory/LDInventoryDirector.h"
#include "Combat/LDCombatDirector.h"
#include "World/LDWorldObjects.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Engine/World.h"

void ALDB92ServicesProbe::SaveCourier(bool Complete)
{
    FString Out,Json;FParse::Value(FCommandLine::Get(),TEXT("B92Output="),Out);auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("scope"),TEXT("physical_courier_contracts"));J->SetBoolField(TEXT("complete"),Complete);J->SetBoolField(TEXT("full_stage_acceptance"),false);J->SetNumberField(TEXT("phase"),Phase);J->SetNumberField(TEXT("failures"),Failures);J->SetArrayField(TEXT("checks"),Checks);FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Json));
    for(const TCHAR* Name:{TEXT("Courier.json"),TEXT("Services.json")}){FFileHelper::SaveStringToFile(Json,*(Out/Name),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);}
    if(Complete){auto* E=GetWorld()->GetAuthGameMode<ALDGameMode>()->Population->Economy.Get();FFileHelper::SaveStringToFile(E->Accounting().ExportJson(TEXT("B92Courier")),*(Out/TEXT("Ledger.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);FFileHelper::SaveStringToFile(E->ExportCouriers(),*(Out/TEXT("Contracts.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);}
}
void ALDB92ServicesProbe::TickCourier()
{
#if !UE_BUILD_SHIPPING
    if(Phase==99){return;}if(Failures||FPlatformTime::Seconds()-Started>420){if(!Failures){Check(false,TEXT("Courier scenario timed out"));}SaveCourier(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,3);return;}
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* R=M?M->Population.Get():nullptr;if(!R||!R->Ready){return;}auto* E=R->Economy.Get();auto* Objects=ALDWorldObjects::Get(GetWorld());const double Now=GetWorld()->GetTimeSeconds();FString Error;
    auto Read=[&](ALDB92Controller* PC,uint8 Id){TSharedPtr<FJsonObject> J;if(PC){if(const auto* S=PC->ServicesProbeReports.Find(Id)){FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(*S),J);}}return J;};
    auto Good=[&](TSharedPtr<FJsonObject> J){return J&&J->GetBoolField(TEXT("completed"));};
    if(Phase==0)
    {
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){if(auto* PC=Cast<ALDB92Controller>(It->Get());PC&&PC->GetPawn()){if(PC->ServicesProbeRole==1){A=PC;}if(PC->ServicesProbeRole==2){B=PC;}}}if(!A.IsValid()||!B.IsValid()){return;}
        Check(!A->IsLocalController()&&!B->IsLocalController()&&M->StateFor(A.Get())->Gold==0&&M->StateFor(B.Get())->Gold==0&&E->Accounting().ItemRecords().Num()==569,TEXT("Two real zero-wallet empty-bag owners; no diagnostic funding or courier endowment"));
        Check(E->CourierBoards(GetWorld()).Num()==12,TEXT("Twelve authored courier boards; towns add access without a new market"));
        Check(E->CourierView(A.Get(),TEXT("capital_guild")).Offer.IsNone()&&E->CourierView(A.Get(),TEXT("scholars_market")).Offer.IsNone()&&E->CourierView(A.Get(),TEXT("bellwick")).Offer.IsNone(),TEXT("Three authored boards have no in-band peer; no offer is invented or distance rule relaxed"));
        CourierOrigin=TEXT("riverport_market");const auto V=E->CourierView(A.Get(),CourierOrigin);CourierTarget=V.Offer;Check(!CourierTarget.IsNone()&&V.Distance>=25000&&V.Distance<=80000&&CourierTarget!=CourierOrigin,TEXT("Private offer has distinct reachable endpoints in25000-80000cm shortest strategic band"));
        Place(A.Get(),ALDB92Services::Counter(GetWorld(),CourierOrigin)+FVector(60,0,0));Task(A.Get(),42,CourierOrigin);Phase=1;return;
    }
    if(Phase==1){const auto J=Read(A.Get(),42);if(!Good(J)){return;}Seat=M->ContenderId(A.Get());const auto V=E->CourierView(A.Get(),CourierOrigin);CourierToken=V.Token;Check(J->GetBoolField(TEXT("accepted"))&&J->GetBoolField(TEXT("duplicate_same"))&&V.Contract.IsValid()&&M->StateFor(A.Get())->Inventory.Find(V.Token)&&V.Issuances==39&&V.Payments==20&&E->Accounting().Minted()==0,TEXT("Duplicate remote acceptance creates one sealed physical parcel and spends issuance once, without paid quota or income"));Task(A.Get(),43,CourierOrigin);Phase=2;return;}
    if(Phase==2){const auto J=Read(A.Get(),43);if(!Good(J)){return;}Check(!J->GetBoolField(TEXT("accepted"))&&E->CourierView(A.Get(),CourierOrigin).Token==CourierToken&&E->Accounting().ItemRecords().Num()==570,TEXT("Another acceptance cannot replace or duplicate the active parcel"));Task(A.Get(),44,CourierOrigin);Phase=3;return;}
    if(Phase==3){const auto J=Read(A.Get(),44);if(!Good(J)){return;}Check(!J->GetBoolField(TEXT("accepted"))&&M->StateFor(A.Get())->Gold==0,TEXT("Origin-board delivery refuses: ordinary possession is insufficient at the wrong endpoint"));Place(A.Get(),ALDB92Services::Counter(GetWorld(),CourierTarget)+FVector(60,0,0));Task(A.Get(),45,CourierTarget);Phase=4;return;}
    if(Phase==4){const auto J=Read(A.Get(),45);if(!Good(J)){return;}const auto* I=M->StateFor(A.Get())->Inventory.Find(CourierToken);Check(!J->GetBoolField(TEXT("accepted"))&&I&&!I->bReserved&&E->CourierView(A.Get(),CourierTarget).Payments==20&&M->StateFor(A.Get())->Gold==0,TEXT("One confirmation channels courier delivery; S releases the original parcel without payout or quota spend"));Task(A.Get(),46,CourierTarget);Phase=5;return;}
    if(Phase==5){const auto J=Read(A.Get(),46);if(!Good(J)){return;}Check(J->GetBoolField(TEXT("accepted"))&&J->GetBoolField(TEXT("duplicate_same"))&&M->StateFor(A.Get())->Gold==25&&M->StateFor(A.Get())->Xp==45&&!M->StateFor(A.Get())->Inventory.Find(CourierToken)&&E->CourierView(A.Get(),CourierTarget).Payments==19,TEXT("Three-second remote delivery consumes the original parcel and pays25gold/45XP once despite duplicate confirmation"));Place(A.Get(),ALDB92Services::Counter(GetWorld(),CourierOrigin)+FVector(60,0,0));Task(A.Get(),42,CourierOrigin);Phase=6;return;}
    if(Phase==6)
    {
        if(!Good(Read(A.Get(),42))){return;}const auto V=E->CourierView(A.Get(),CourierOrigin);Check(V.Token.IsValid()&&V.Token!=CourierToken&&V.Issuances==38&&V.Payments==19,TEXT("A second legal job has a fresh token and the persistent remaining quotas"));CourierToken=V.Token;auto* Body=Cast<ALDCharacter>(A->GetPawn());M->Combat->Die(Body,Now);Objects->Publish();Phase=7;return;
    }
    // The registry resolves the same server item even while dropped or carried by a rival.
    ALDItem* Parcel=nullptr;for(auto P:E->Custody){if(IsValid(P)&&P->Identity==CourierToken){Parcel=P;break;}}
    if(Phase==7)
    {
        Check(Parcel&&Parcel->State==0&&!Parcel->Holder.IsValid()&&!M->StateFor(A.Get())->bActive&&E->CourierView(A.Get(),CourierOrigin).Token==CourierToken&&E->CourierView(A.Get(),CourierOrigin).Issuances==38,TEXT("Actual death drops the sealed parcel; contract and quotas survive with no replacement"));if(!Parcel){return;}SavedGold=M->StateFor(A.Get())->Gold;Place(B.Get(),Parcel->GetActorLocation()-FVector(0,0,18)+FVector(60,0,0));Task(B.Get(),48,NAME_None);Phase=8;return;
    }
    if(Phase==8){if(!Good(Read(B.Get(),48))){return;}Check(Parcel&&Parcel->Holder==B.Get()&&M->StateFor(B.Get())->Inventory.Find(CourierToken)&&E->SynchronizeWorld(Error),TEXT("Rival takes the actual dropped parcel through ordinary observed pickup"));Place(B.Get(),ALDB92Services::Counter(GetWorld(),CourierTarget)+FVector(-60,0,0));Task(B.Get(),47,CourierTarget);Phase=9;return;}
    if(Phase==9){const auto J=Read(B.Get(),47);if(!Good(J)){return;}Check(!J->GetBoolField(TEXT("accepted"))&&M->StateFor(B.Get())->Gold==0&&E->CourierView(A.Get(),CourierOrigin).Payments==19,TEXT("A rival holding the parcel cannot redeem another persistent owner's contract"));Task(B.Get(),49,NAME_None);Phase=10;return;}
    if(Phase==10){if(!Good(Read(B.Get(),49))||!M->StateFor(A.Get())->bActive||!A->GetPawn()){return;}Check(Parcel&&Parcel->State==0&&M->StateFor(A.Get())->Life==2&&M->StateFor(A.Get())->Gold==SavedGold&&E->CourierView(A.Get(),CourierOrigin).Token==CourierToken,TEXT("Native return preserves the job and death-taxed wallet while the rival-dropped parcel remains recoverable"));Place(A.Get(),Parcel->GetActorLocation()-FVector(0,0,18)+FVector(60,0,0));Task(A.Get(),48,NAME_None);Phase=11;return;}
    if(Phase==11){if(!Good(Read(A.Get(),48))){return;}Check(Parcel&&Parcel->Holder==A.Get()&&E->Audit(Error),TEXT("Original owner recovers the same parcel identity after death"));Task(A.Get(),8,NAME_None);Phase=14;return;}
    if(Phase==14){if(R->Humans()!=1){return;}Phase=12;SaveCourier(false);return;}
    if(Phase==12)
    {
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){if(auto* PC=Cast<ALDB92Controller>(It->Get());PC&&PC->ServicesProbeRole==3&&PC->GetPawn()){Replacement=PC;}}if(!Replacement.IsValid()){return;}
        const auto V=E->CourierView(Replacement.Get(),CourierTarget);Check(M->ContenderId(Replacement.Get())==Seat&&V.Token==CourierToken&&V.Issuances==38&&V.Payments==19&&M->StateFor(Replacement.Get())->Inventory.Find(CourierToken),TEXT("Actual disconnect and replacement-owner admission retain contract, recovered parcel and persistent quotas"));Place(Replacement.Get(),ALDB92Services::Counter(GetWorld(),CourierTarget)+FVector(60,0,0));Task(Replacement.Get(),46,CourierTarget);Phase=13;return;
    }
    if(Phase==13)
    {
        const auto J=Read(Replacement.Get(),46);if(!Good(J)){return;}Check(J->GetBoolField(TEXT("accepted"))&&M->StateFor(Replacement.Get())->Gold==SavedGold+25&&M->StateFor(Replacement.Get())->Xp==90&&E->CourierView(Replacement.Get(),CourierTarget).Payments==18,TEXT("Recovered parcel completes normally after takeover; only two total remote payouts and no legacy credits"));Phase=15;
    }
    if(Phase==15)
    {
        auto Request=[&](ALDB92Controller* PC,FName Board,uint8 Action){const auto V=E->CourierView(PC,Board);const auto* O=M->StateFor(PC);FLDB92MarketOrder Q;Q.Match=O->Match;Q.Life=O->Life;Q.InventoryRevision=O->Inventory.Revision;Q.WalletRevision=E->Ledger.Account(E->Wallet(M->ContenderId(PC)))->Revision;Q.Kind=ELDItemKind::SealedParcel;Q.CourierAction=Action;Q.CourierRevision=V.Revision;Q.CourierToken=Action==1?FGuid():V.Token;Q.Destination=Action==1?V.Offer:V.Destination;if(Action==2){if(const auto* I=O->Inventory.Find(V.Token)){Q.Inputs.Add(*I);}}return Q;};
        bool Paid=true;for(int32 N=0;N<20;++N){auto Q=Request(B.Get(),CourierOrigin,1);if(!E->CourierCommand(B.Get(),CourierOrigin,Q,FGuid::NewGuid(),Error)){Paid=false;break;}const auto V=E->CourierView(B.Get(),CourierOrigin);auto* P=M->Inventory->Owned(B.Get(),V.Token);auto* O=M->StateFor(B.Get());P->bReserved=true;++P->Revision;auto* I=O->Inventory.Items.FindByPredicate([&](const auto& X){return X.Id==V.Token;});P->Project(*I);++O->Inventory.Revision;Q=Request(B.Get(),V.Destination,2);if(!E->CourierCommand(B.Get(),V.Destination,Q,FGuid::NewGuid(),Error,true)){Paid=false;break;}}
        Check(Paid&&M->StateFor(B.Get())->Gold==500&&M->StateFor(B.Get())->Xp==900&&E->CourierView(B.Get(),CourierOrigin).Payments==0&&E->CourierView(B.Get(),CourierOrigin).Issuances==20,TEXT("Labelled authority fixture delivers20 real parcels with20 bounded receipts; no claim of20 ordinary travel/channels"));
        auto Q=Request(B.Get(),CourierOrigin,1);Check(!E->CourierCommand(B.Get(),CourierOrigin,Q,FGuid::NewGuid(),Error)&&M->StateFor(B.Get())->Gold==500,TEXT("Exhausted20-payment budget forbids another issuance/payout even with20 issuance units left"));
        auto* PC=Replacement.Get();bool Issued=true,VoidRefused=true;while(E->CourierView(PC,CourierOrigin).Issuances>0)
        {
            Q=Request(PC,CourierOrigin,1);if(!E->CourierCommand(PC,CourierOrigin,Q,FGuid::NewGuid(),Error)){Issued=false;break;}const auto V=E->CourierView(PC,CourierOrigin);auto* P=M->Inventory->Owned(PC,V.Token);Q=Request(PC,CourierOrigin,3);if(!E->CourierCommand(PC,CourierOrigin,Q,FGuid::NewGuid(),Error)){Issued=false;break;}
            Q.CourierAction=2;Q.CourierRevision=E->CourierView(PC,CourierOrigin).Revision;Q.Inputs={*M->StateFor(PC)->Inventory.Find(V.Token)};VoidRefused&=!E->CourierCommand(PC,V.Destination,Q,FGuid::NewGuid(),Error);M->Inventory->Remove(PC,P,false);P->SetActorLocation(ALDB92Services::Counter(GetWorld(),CourierOrigin)+FVector(400,400,12));
        }
        Objects->Publish();Check(Issued&&VoidRefused&&E->CourierView(PC,CourierOrigin).Issuances==0&&E->CourierView(PC,CourierOrigin).Payments==18&&M->StateFor(PC)->Gold==SavedGold+25,TEXT("Labelled38-abandon fixture reaches40 total issuances without payout/refill; void parcels cannot redeem and remain physical cargo"));
        Q=Request(PC,CourierOrigin,1);Check(!E->CourierCommand(PC,CourierOrigin,Q,FGuid::NewGuid(),Error),TEXT("Forty-issuance exhaustion rejects another token before allocation"));
        Check(!LDInventory::Commodity(ELDItemKind::SealedParcel)&&!LDInventory::QuickUsable(ELDItemKind::SealedParcel,8)&&M->StateFor(PC)->Commissions==0&&M->StateFor(B.Get())->Commissions==0,TEXT("Sealed contract proof is neither sellable commodity, prepared item nor legacy credit"));
        Check(E->Accounting().Minted()==550&&E->SynchronizeWorld(Error)&&E->Audit(Error)&&Objects->Conserved(Error)&&R->Audit(Error),TEXT("Canonical closure:60 physical tokens,22 paid consumes,38 void cargo; exactly550 named mint"));SaveCourier(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,Failures?3:0);return;
    }
#endif
}
