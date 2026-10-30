#include "Economy/LDB92Services.h"
#include "Economy/LDB92Economy.h"
#include "Population/LDB92Roster.h"
#include "Framework/LDGameMode.h"
#include "Combat/LDCombatDirector.h"
#include "World/LDWorldObjects.h"
#include "Region/LDMapDefinition.h"
#include "Engine/World.h"

bool FLDB92MarketOrder::operator==(const FLDB92MarketOrder& B) const
{return Match==B.Match&&Life==B.Life&&Sequence==B.Sequence&&InventoryRevision==B.InventoryRevision&&ItemRevision==B.ItemRevision&&WalletRevision==B.WalletRevision&&Session==B.Session&&Item==B.Item&&Revision==B.Revision&&Kind==B.Kind&&Buy==B.Buy&&Price==B.Price;}
ALDB92Services::ALDB92Services(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
const TArray<FName>& ALDB92Services::Markets()
{static const TArray<FName> Values={TEXT("capital_treasury"),TEXT("highfort_market"),TEXT("scholars_market"),TEXT("riverport_market"),TEXT("ironworks_market")};return Values;}
FVector ALDB92Services::Counter(const UWorld* W,FName POI)
{const auto* Map=ULDMapDefinition::Get(W);const auto* P=Map?Map->ServiceFor(POI):nullptr;return P?P->Position:FVector::ZeroVector;}
bool ALDB92Services::Eligible(ALDB92Controller* PC,FName POI) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M?M->StateFor(PC):nullptr;const auto* B=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;
    const auto* S=M&&M->Combat?M->Combat->StateFor(B):nullptr;auto* Env=ALDEnvironmentDirector::Get(GetWorld());const double Now=GetWorld()->GetTimeSeconds();const FVector P=Counter(GetWorld(),POI);
    return Markets().Contains(POI)&&M&&M->Population&&M->Population->Ready&&M->IsPlaying()&&Now<M->GetGameState<ALDGameState>()->MatchDeadline&&O&&O->bActive&&B&&S&&S->Hp>0
        &&Now>=O->ProtectedUntil&&Now>=S->ProtectionUntil&&Now-S->LastDamage>=6&&Now-S->LastDealt>=6&&!S->Action&&!O->DevelopmentKind
        &&M->Combat->Allows(const_cast<ALDCharacter*>(B),LDTactical::EAction::Service)&&!ALDWorldObjects::Get(GetWorld())->Carried(PC)
        &&FVector::DistSquared2D(B->GetActorLocation(),P)<=120*120&&FMath::Abs(B->GetActorLocation().Z-P.Z-90)<100&&Env&&Env->Ray(B->GetActorLocation(),P+FVector(0,0,90),nullptr,Env->SightFor(PC));
}
bool ALDB92Services::Token(FClient& C)
{const double Now=GetWorld()->GetTimeSeconds();C.Tokens=FMath::Min(4.,C.Tokens+FMath::Max(0.,Now-C.At)*4);C.At=Now;if(C.Tokens<1){return false;}--C.Tokens;return true;}
void ALDB92Services::Reply(ALDB92Controller* PC,uint32 Sequence,FGuid Receipt,bool Accepted,const FString& Message)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M?M->StateFor(PC):nullptr;if(!O){return;}auto& C=Clients.FindOrAdd(PC);
    if(!C.View.Session.IsValid()||C.View.Match!=O->Match||C.View.Life!=O->Life||!Eligible(PC,C.View.POI)){C.View.Session.Invalidate();C.View.Offers.Reset();}
    else
    {
        const int32 Index=Markets().Find(C.View.POI);const auto* E=M->Population->Economy.Get();const auto Q=E->Quote(E->Rules().Markets[Index].Id);
        C.View.Revision=Q.Revision;C.View.WalletRevision=E->Accounting().Account(E->Wallet(M->ContenderId(PC)))->Revision;C.View.Offers.Reset();
        for(const auto& G:Q.Goods){FLDB92Offer V;V.Kind=G.Kind;V.Stock=G.Stock;V.Ask=G.Ask;V.Bid=G.Bid;C.View.Offers.Add(V);}
    }
    PC->ClientB92Market(*O,C.View,Sequence,Receipt,Accepted,Message);
}
void ALDB92Services::Open(ALDB92Controller* PC,uint32 Match,uint32 Life,uint32 Sequence,FName POI)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!PC||!M){return;}M->Combat->FlushPastCommand();auto& C=Clients.FindOrAdd(PC);const auto* O=M->StateFor(PC);
    if(!O||!Sequence||Sequence<=C.Last){return;}C.Last=Sequence;C.View=FLDB92MarketView();
    if(!Token(C)||O->Match!=Match||O->Life!=Life||!Eligible(PC,POI)){Reply(PC,Sequence,FGuid(),false,TEXT("Approach this counter within120cm, with sight, quiet and free hands"));return;}
    C.View.Session=FGuid::NewGuid();C.View.Match=Match;C.View.Life=Life;C.View.POI=POI;Reply(PC,Sequence,FGuid(),true,TEXT("Current local prices. Each confirmation exchanges one unit."));
}
void ALDB92Services::Close(ALDB92Controller* PC)
{if(!PC){return;}Clients.FindOrAdd(PC).View=FLDB92MarketView();Reply(PC,0,FGuid(),false,TEXT("Market closed"));}
void ALDB92Services::Order(ALDB92Controller* PC,const FLDB92MarketOrder& R)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!PC||!M){return;}M->Combat->FlushPastCommand();const auto* O=M->StateFor(PC);auto& C=Clients.FindOrAdd(PC);
    if(!O||O->Match!=R.Match||O->Life!=R.Life||!R.Sequence){return;}
    if(const auto* Old=C.Receipts.Find(R.Sequence)){Reply(PC,R.Sequence,Old->Receipt,Old->Accepted&&Old->Order==R,Old->Order==R?Old->Message:TEXT("Confirmation reused with changed terms"));return;}
    if(R.Sequence<=C.Last){Reply(PC,R.Sequence,FGuid(),false,TEXT("Expired confirmation"));return;}C.Last=R.Sequence;
    FCached Result;Result.Order=R;Result.Receipt=FGuid::NewGuid();
    if(!Token(C)||!C.View.Session.IsValid()||R.Session!=C.View.Session||!Eligible(PC,C.View.POI)){Result.Message=TEXT("Counter access changed; nothing exchanged");}
    else
    {
        auto* E=M->Population->Economy.Get();FLDB92Trade T;T.Request=Result.Receipt;T.Contender=M->ContenderId(PC);T.Market=E->Rules().Markets[Markets().Find(C.View.POI)].Id;T.Item=R.Item;T.Kind=R.Kind;T.Buy=R.Buy;T.QuoteRevision=R.Revision;T.WalletRevision=R.WalletRevision;T.ItemRevision=R.ItemRevision;T.InventoryRevision=R.InventoryRevision;T.Price=R.Price;FGuid Received;
        Result.Accepted=E->Trade(T,Received,Result.Message);if(Result.Accepted){Result.Message=R.Buy?TEXT("Bought one unit"):TEXT("Sold one unit");}
    }
    C.Receipts.Add(R.Sequence,Result);while(C.Receipts.Num()>96){uint32 Oldest=MAX_uint32;for(const auto& P:C.Receipts){Oldest=FMath::Min(Oldest,P.Key);}C.Receipts.Remove(Oldest);}
    Reply(PC,R.Sequence,Result.Receipt,Result.Accepted,Result.Message);
}
void ALDB92Services::Tick(float Delta)
{
    Super::Tick(Delta);const double Now=GetWorld()->GetTimeSeconds();if(Now<Next){return;}Next=Now+.1;
    for(auto It=Clients.CreateIterator();It;++It)
    {
        auto* PC=It.Key().Get();if(!PC){It.RemoveCurrent();continue;}auto& C=It.Value();if(!C.View.Session.IsValid()){continue;}
        if(!Eligible(PC,C.View.POI)){C.View.Session.Invalidate();Reply(PC,0,FGuid(),false,TEXT("Counter closed: range, sight, life or action changed"));continue;}
        auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* E=M->Population->Economy.Get();const auto Q=E->Quote(E->Rules().Markets[Markets().Find(C.View.POI)].Id);
        if(Q.Revision!=C.View.Revision){Reply(PC,0,FGuid(),true,TEXT("Prices changed; review the current terms"));}
    }
}
