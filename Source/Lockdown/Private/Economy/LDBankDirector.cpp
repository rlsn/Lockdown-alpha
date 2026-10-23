#include "Economy/LDBankDirector.h"
#include "Economy/LDCommerceDirector.h"
#include "Economy/LDEconomyDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Inventory/LDVendorDirector.h"
#include "Inventory/LDTradeDirector.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Region/LDRegionDefinition.h"
#include "Core/LDGroundQueries.h"
#include "Engine/World.h"

bool ALDBankDirector::Eligible(ALDPlayerController* PC) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M?M->StateFor(PC):nullptr;const auto* Body=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;const auto* S=M&&M->Combat?M->Combat->StateFor(Body):nullptr;
    const double Now=GetWorld()->GetTimeSeconds();const FVector Site=LDRegion::Site(GetWorld(),1);const auto* G=GetWorld()->GetGameState<ALDGameState>();
    return G&&G->MatchDeadline>Now&&M&&M->Economy&&M->IsPlaying()&&O&&O->bActive&&Body&&S&&S->Hp>0&&Now>=O->ProtectedUntil&&Now>=S->ProtectionUntil&&Now>=S->LastDamage+6&&Now>=S->LastDealt+6&&!S->Action&&!O->DevelopmentKind&&!M->Objectives->HasChannel(PC)&&FVector::Dist2D(Body->GetActorLocation(),Site)<=120&&LDGround::ClearSegment(GetWorld(),Body->GetActorLocation(),Site)&&M->Vision->Ray({Body->GetActorLocation(),120,M->Vision->LevelAt(Body->GetActorLocation())},Site);
}
bool ALDBankDirector::TakeToken(FClient& C)
{const double Now=GetWorld()->GetTimeSeconds();C.Tokens=FMath::Min(4.,C.Tokens+FMath::Max(0.,Now-C.At)*4);C.At=Now;if(C.Tokens<1){return false;}--C.Tokens;return true;}
FLDBankView ALDBankDirector::View(ALDPlayerController* PC,const FClient& C) const
{
    auto Q=C.View;auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const FGuid Id=M->ContenderId(PC);const auto& L=M->Economy->Accounting();
    if(const auto* W=L.Account(ALDEconomyDirector::Wallet(Id))){Q.Wallet=W->Gold;Q.WalletRevision=W->Revision;}
    if(const auto* B=L.Account(ALDEconomyDirector::BankAccount(Id))){Q.Bank=B->Gold;Q.BankRevision=B->Revision;}return Q;
}
bool ALDBankDirector::Valid(ALDPlayerController* PC,const FClient& C) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M->StateFor(PC);const auto* S=M->Combat->StateFor(PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr);
    return C.View.Session.IsValid()&&O&&O->Match==C.View.Match&&O->Life==C.View.Life&&S&&S->LastDamage==C.Damage&&S->LastDealt==C.Dealt&&Eligible(PC)&&(!M->Trade||!M->Trade->HasSession(PC));
}
void ALDBankDirector::Open(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Token)
{
    auto& C=Clients.FindOrAdd(PC);auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M->StateFor(PC);
    if(!Token||Token<=C.LastToken||Token<=C.ClosedThrough){return;}C.LastToken=Token;C.View=FLDBankView();C.View.Token=Token;
    if(!TakeToken(C)||!O||O->Match!=Match||O->Life!=Life||!Eligible(PC)){Reply(PC,0,TEXT("Treasury requires 120 cm, body sight, alive/unprotected and six combat-quiet seconds"));return;}
    if(M->Commerce){M->Commerce->Invalidate(PC);}
    if(M->Trade){M->Trade->CancelFor(PC);}if(M->Vendor){M->Vendor->Invalidate(PC);}
    const auto* S=M->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()));C.Damage=S->LastDamage;C.Dealt=S->LastDealt;C.View.Match=Match;C.View.Life=Life;C.View.Session=FGuid::NewGuid();C.View=View(PC,C);Reply(PC,0,TEXT("City Treasury · Official protected service · Match continues"));
}
void ALDBankDirector::Close(ALDPlayerController* PC,uint32 Token)
{auto& C=Clients.FindOrAdd(PC);C.ClosedThrough=FMath::Max(C.ClosedThrough,Token);if(C.View.Token<=Token){C.View.Session.Invalidate();}}
void ALDBankDirector::Reply(ALDPlayerController* PC,uint32 Id,const FString& Reason)
{
    const FString Message=Reason;auto& C=Clients.FindOrAdd(PC);if(Id){C.Receipts.Add(Id,Message);while(C.Receipts.Num()>64){uint32 Old=MAX_uint32;for(const auto& P:C.Receipts){Old=FMath::Min(Old,P.Key);}C.Receipts.Remove(Old);}}
    if(const auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC)){C.View=View(PC,C);PC->ClientBankView(*O,C.View,Id,Message);}
}
void ALDBankDirector::Queue(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Id,FGuid Session,int64 Amount,bool Deposit,uint32 WR,uint32 BR)
{
    auto& C=Clients.FindOrAdd(PC);auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M->StateFor(PC);
    if(!Id||!O||O->Match!=Match||O->Life!=Life){Reply(PC,Id,TEXT("Bank request has stale life"));return;}
    if(const auto* Receipt=C.Receipts.Find(Id)){Reply(PC,Id,*Receipt);return;}
    if(Requests.ContainsByPredicate([&](const auto& R){return R.PC==PC&&R.Id==Id;})){return;}
    if(Id<=C.LastRequest){Reply(PC,Id,TEXT("Expired bank request"));return;}
    C.LastRequest=Id;
    if(!TakeToken(C)||Requests.ContainsByPredicate([&](const auto& R){return R.PC==PC;})){Reply(PC,Id,TEXT("Bank busy; try again"));return;}
    C.LastRequest=Id;Requests.Add({PC,Match,Life,Id,Session,Amount,Deposit,WR,BR});
}
void ALDBankDirector::Sample(double Now)
{
    for(auto It=Clients.CreateIterator();It;++It)
    {
        auto* PC=It.Key().Get();if(!PC){It.RemoveCurrent();continue;}auto& C=It.Value();if(!C.View.Session.IsValid()){continue;}
        if(!Valid(PC,C)){C.View.Session.Invalidate();Reply(PC,0,TEXT("Treasury closed: life, range, sight, combat or action changed"));}
        else{const auto Q=View(PC,C);if(Q.WalletRevision!=C.View.WalletRevision||Q.BankRevision!=C.View.BankRevision){Reply(PC,0,TEXT("Balances changed · Review again"));}}
    }
    const auto Pending=MoveTemp(Requests);Requests.Reset();
    for(const auto& R:Pending)
    {
        auto* PC=R.PC.Get();if(!PC){continue;}const auto& C=Clients.FindChecked(PC);auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M->StateFor(PC);FString Error;
        if(!O||O->Match!=R.Match||O->Life!=R.Life||C.View.Session!=R.Session||!Valid(PC,C)){Reply(PC,R.Id,TEXT("Treasury closed; nothing moved"));continue;}
        if(!M->Economy->CommitBank(PC,R.Amount,R.Deposit,R.WR,R.BR,Error)){Reply(PC,R.Id,Error+TEXT(" · Review again"));continue;}
        Reply(PC,R.Id,R.Deposit?TEXT("Deposit confirmed · Fee burned, bank credit protected"):TEXT("Withdrawal confirmed · Gold is now carried"));
    }
}

void ALDBankDirector::Invalidate(ALDPlayerController* PC)
{if(auto* C=Clients.Find(PC);C&&C->View.Session.IsValid()){C->View.Session.Invalidate();Reply(PC,0,TEXT("Treasury closed: another service opened"));}}
