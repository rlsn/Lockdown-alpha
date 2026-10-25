#include "Economy/LDCommerceDirector.h"
#include "Economy/LDEconomyDirector.h"
#include "Economy/LDBankDirector.h"
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
FVector ALDCommerceDirector::Position(const UWorld* W,uint8 Service){return LDRegion::Site(W,Service==2?2:Service==3?6:1);}
FString ALDCommerceDirector::ServiceName(uint8 Service){const TCHAR* Names[]={TEXT("Official materials"),TEXT("City market"),TEXT("WestTown market"),TEXT("EastTown market")};return Service<4?Names[Service]:TEXT("Unknown service");}
bool ALDCommerceDirector::Eligible(ALDPlayerController* PC,uint8 Service) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M?M->StateFor(PC):nullptr;const auto* Body=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;const auto* S=M&&M->Combat?M->Combat->StateFor(Body):nullptr;const auto* G=GetWorld()->GetGameState<ALDGameState>();const double Now=GetWorld()->GetTimeSeconds();const FVector Site=Position(GetWorld(),Service);
    return Service<4&&M&&M->Economy&&G&&G->MatchDeadline>Now&&M->IsPlaying()&&O&&O->bActive&&Body&&S&&S->Hp>0&&M->Combat->Allows(const_cast<ALDCharacter*>(Body),LDTactical::EAction::Service)&&Now>=O->ProtectedUntil&&Now>=S->ProtectionUntil&&!S->Action&&!O->DevelopmentKind&&!M->Objectives->HasChannel(PC)&&FVector::Dist2D(Body->GetActorLocation(),Site)<=120&&LDGround::ClearSegment(GetWorld(),Body->GetActorLocation(),Site)&&M->Vision->Ray({Body->GetActorLocation(),120,M->Vision->LevelAt(Body->GetActorLocation())},Site);
}
bool ALDCommerceDirector::TakeToken(FClient& C){const double Now=GetWorld()->GetTimeSeconds();C.Tokens=FMath::Min(4.,C.Tokens+FMath::Max(0.,Now-C.At)*4);C.At=Now;if(C.Tokens<1){return false;}--C.Tokens;return true;}
bool ALDCommerceDirector::Valid(ALDPlayerController* PC,const FClient& C) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M->StateFor(PC);const auto* S=M->Combat->StateFor(PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr);
    return C.View.Session.IsValid()&&O&&O->Match==C.View.Match&&O->Life==C.View.Life&&S&&C.Damage==S->LastDamage&&C.Dealt==S->LastDealt&&Eligible(PC,C.View.Service)&&(!M->Trade||!M->Trade->HasSession(PC));
}
void ALDCommerceDirector::Reply(ALDPlayerController* PC,uint32 Id,const FString& Reason)
{
    const FString Message=Reason;auto& C=Clients.FindOrAdd(PC);if(Id){C.Receipts.Add(Id,Message);while(C.Receipts.Num()>64){uint32 Old=MAX_uint32;for(const auto& P:C.Receipts){Old=FMath::Min(Old,P.Key);}C.Receipts.Remove(Old);}}
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M->StateFor(PC);if(!O){return;}
    if(C.View.Session.IsValid()){const auto Terms=M->Economy->CommerceQuote(C.View.Service);C.View.Revision=Terms.Revision;C.View.Offers=Terms.Offers;C.View.Cash=Terms.Cash;C.View.QuotedAt=Terms.QuotedAt;}else{C.View.Offers.Reset();}
    PC->ClientCommerceView(*O,C.View,Id,Message);
}
void ALDCommerceDirector::Open(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Token,uint8 Service)
{
    auto& C=Clients.FindOrAdd(PC);auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M->StateFor(PC);if(!Token||Token<=C.LastToken||Token<=C.ClosedThrough){return;}C.LastToken=Token;C.View=FLDCommerceView();C.View.Token=Token;C.View.Service=Service;
    if(!TakeToken(C)||!O||O->Match!=Match||O->Life!=Life||!Eligible(PC,Service)){Reply(PC,0,TEXT("Approach this official service within 120 cm, alive/unprotected, body sight and no conflicting action"));return;}
    if(M->Trade){M->Trade->CancelFor(PC);}if(M->Vendor){M->Vendor->Invalidate(PC);}if(M->Bank){M->Bank->Invalidate(PC);}
    const auto* S=M->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()));C.Damage=S->LastDamage;C.Dealt=S->LastDealt;C.View.Session=FGuid::NewGuid();C.View.Match=Match;C.View.Life=Life;Reply(PC,0,TEXT("Live local quote · One unit per acknowledged Buy/Sell"));
}
void ALDCommerceDirector::Close(ALDPlayerController* PC,uint32 Token){auto& C=Clients.FindOrAdd(PC);C.ClosedThrough=FMath::Max(C.ClosedThrough,Token);if(C.View.Token<=Token){C.View.Session.Invalidate();}}
void ALDCommerceDirector::Invalidate(ALDPlayerController* PC){if(auto* C=Clients.Find(PC);C&&C->View.Session.IsValid()){C->View.Session.Invalidate();Reply(PC,0,TEXT("Commerce closed: another service opened"));}}
void ALDCommerceDirector::Queue(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Id,FGuid Session,uint32 Revision,uint8 Command,ELDItemKind Kind,FGuid Item,uint32 ItemRevision,uint32 InventoryRevision,int32 Price)
{
    if(auto* Clock=GetWorld()->GetAuthGameMode<ALDGameMode>()->Combat.Get()){Clock->FlushPastCommand();}
    auto& C=Clients.FindOrAdd(PC);auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC);if(!Id||!O||O->Match!=Match||O->Life!=Life){Reply(PC,Id,TEXT("Stale commerce life"));return;}
    if(const auto* R=C.Receipts.Find(Id)){Reply(PC,Id,*R);return;}if(Requests.ContainsByPredicate([&](const auto& R){return R.PC==PC&&R.Id==Id;})){return;}
    if(Id<=C.LastRequest){Reply(PC,Id,TEXT("Expired commerce request"));return;}C.LastRequest=Id;
    if(!TakeToken(C)||Requests.FilterByPredicate([&](const auto& R){return R.PC==PC;}).Num()>=2){Reply(PC,Id,TEXT("Commerce busy · Try again"));return;}
    GetWorld()->GetAuthGameMode<ALDGameMode>()->Objectives->CancelClaimForAction(PC,GetWorld()->GetTimeSeconds());Requests.Add({PC,Match,Life,Id,Session,Revision,Command,Kind,Item,ItemRevision,InventoryRevision,Price});
}
void ALDCommerceDirector::Sample(double Now)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();
    for(auto It=Clients.CreateIterator();It;++It){auto* PC=It.Key().Get();if(!PC){It.RemoveCurrent();continue;}auto& C=It.Value();if(!C.View.Session.IsValid()){continue;}if(!Valid(PC,C)){C.View.Session.Invalidate();Reply(PC,0,TEXT("Commerce closed: range, sight, life or action changed"));}else if(M->Economy->CommerceQuote(C.View.Service).Revision!=C.View.Revision){Reply(PC,0,TEXT("Quote changed · Review updated terms"));}}
    const auto Pending=MoveTemp(Requests);Requests.Reset();
    for(const auto& R:Pending){auto* PC=R.PC.Get();if(!PC){continue;}const auto& C=Clients.FindChecked(PC);const auto* O=M->StateFor(PC);FGuid Received;FString Error;
        if(!O||O->Match!=R.Match||O->Life!=R.Life||C.View.Session!=R.Session||!Valid(PC,C)){Reply(PC,R.Id,TEXT("Commerce closed · Nothing exchanged"));continue;}
        if(!M->Economy->CommitCommerce(PC,C.View.Service,R.QuoteRevision,R.Command,R.Kind,R.Item,R.ItemRevision,R.InventoryRevision,R.Price,Received,Error)){Reply(PC,R.Id,Error);continue;}
        Reply(PC,R.Id,R.Command==1?TEXT("Buy confirmed · One item received"):TEXT("Sell confirmed · One item transferred"));
    }
}
