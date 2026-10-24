#include "Inventory/LDVendorDirector.h"
#include "Combat/LDTacticalCatalog.h"
#include "Economy/LDCommerceDirector.h"
#include "Economy/LDBankDirector.h"
#include "Inventory/LDTradeDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDevelopmentDirector.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Region/LDRegionDefinition.h"
#include "Vision/LDVisionDirector.h"
#include "Core/LDGroundQueries.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
bool ALDVendorDirector::Eligible(ALDPlayerController* PC) const
{
    const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M?const_cast<ALDGameMode*>(M)->StateFor(PC):nullptr;const auto* Body=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;const auto* S=M&&M->Combat?M->Combat->StateFor(Body):nullptr;
    const FVector Site=LDRegion::Site(GetWorld(),1);const double Now=GetWorld()->GetTimeSeconds();
    return BookPrice>0&&BookPrice<=1000000&&CatalogRevision&&M&&M->IsPlaying()&&O&&O->bActive&&Body&&S&&S->Hp>0&&M->Combat->Allows(const_cast<ALDCharacter*>(Body),LDTactical::EAction::Service)&&Now>=O->ProtectedUntil&&Now>=S->ProtectionUntil&&!S->Action&&!O->DevelopmentKind&&!M->Objectives->HasChannel(PC)&&FVector::Dist2D(Body->GetActorLocation(),Site)<=120&&LDGround::ClearSegment(GetWorld(),Body->GetActorLocation(),Site)&&M->Vision->Ray({Body->GetActorLocation(),120,M->Vision->LevelAt(Body->GetActorLocation())},Site);
}
bool ALDVendorDirector::TakeToken(FClient& C)
{const double Now=GetWorld()->GetTimeSeconds();C.Tokens=FMath::Min(4.,C.Tokens+FMath::Max(0.,Now-C.At)*4);C.At=Now;if(C.Tokens<1){return false;}--C.Tokens;return true;}
FLDVendorQuote ALDVendorDirector::Quote(ALDPlayerController* PC,const FClient& C) const
{
    FLDVendorQuote Q;Q.Match=C.Match;Q.Life=C.Life;Q.Token=C.Token;Q.Session=C.Session;Q.CatalogRevision=CatalogRevision;
    if(C.Session.IsValid()){TArray<ELDSkill> Skills={ELDSkill::Step,ELDSkill::Spark,ELDSkill::Patch,ELDSkill::Parley};if(GetWorld()->GetAuthGameMode<ALDGameMode>()->IsTacticalProfile()){Skills.Append({ELDSkill::AnchorBash,ELDSkill::ArcBolt,ELDSkill::PurgeTouch,ELDSkill::SuretyShell,ELDSkill::VeilStep,ELDSkill::Counterpulse,ELDSkill::FarwatchWard,ELDSkill::WireSnare,ELDSkill::SmokeVeil,ELDSkill::SeersFlare});}for(auto Skill:Skills){FLDVendorOffer Offer;Offer.Skill=Skill;Offer.Price=BookPrice;Q.Offers.Add(Offer);}}return Q;
}
bool ALDVendorDirector::ValidSession(ALDPlayerController* PC,const FClient& C) const
{const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=const_cast<ALDGameMode*>(M)->StateFor(PC);const auto* S=M->Combat->StateFor(PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr);return C.Session.IsValid()&&O&&O->Match==C.Match&&O->Life==C.Life&&S&&S->LastDamage==C.Damage&&Eligible(PC);}
void ALDVendorDirector::Open(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Token)
{
    auto& C=Clients.FindOrAdd(PC);auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M->StateFor(PC);
    if(!Token||Token<=C.ClosedThrough||Token<=C.Token){return;}
    C.Token=Token;C.Session.Invalidate();
    if(!TakeToken(C)||!O||O->Match!=Match||O->Life!=Life||!Eligible(PC)){PC->ClientVendorClosed(Token,TEXT("Approach the Guild within 120 cm, alive, unprotected and unoccupied"));return;}
    if(M->Bank){M->Bank->Invalidate(PC);}
    if(M->Commerce){M->Commerce->Invalidate(PC);}
    if(M->Trade){M->Trade->CancelFor(PC);}
    C.Match=Match;C.Life=Life;C.Damage=M->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()))->LastDamage;C.Session=FGuid::NewGuid();C.SentRevision=CatalogRevision;C.SentPrice=BookPrice;
    PC->ClientVendorQuote(*O,Quote(PC,C));
}
void ALDVendorDirector::Close(ALDPlayerController* PC,uint32 Token)
{auto& C=Clients.FindOrAdd(PC);C.ClosedThrough=FMath::Max(C.ClosedThrough,Token);if(C.Token<=Token){C.Session.Invalidate();}}
void ALDVendorDirector::Reply(ALDPlayerController* PC,uint32 Id,const FReceipt& Receipt)
{
    auto& C=Clients.FindOrAdd(PC);C.Receipts.Add(Id,Receipt);while(C.Receipts.Num()>64){uint32 Old=MAX_uint32;for(const auto& Pair:C.Receipts){Old=FMath::Min(Old,Pair.Key);}C.Receipts.Remove(Old);}
    if(const auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC)){PC->ClientVendorResult(*O,Id,Quote(PC,C),Receipt.Item,Receipt.Reason);}
}
void ALDVendorDirector::Queue(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Id,FGuid Session,uint32 Revision,int32 Price,uint32 InventoryRevision,ELDSkill Skill)
{
    auto& C=Clients.FindOrAdd(PC);const auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC);
    if(!Id||!O||O->Match!=Match||O->Life!=Life){PC->ClientIntentResult(Id,TEXT("Purchase unavailable or stale life"));return;}
    if(const auto* R=C.Receipts.Find(Id)){if(!R->bFinal){return;}PC->ClientVendorResult(*O,Id,Quote(PC,C),R->Item,R->Reason);return;}
    if(Id<=C.LastRequest){PC->ClientIntentResult(Id,TEXT("Expired purchase request"));return;}
    int32 Pending=0;for(const auto& R:Requests){Pending+=R.PC==PC;}if(!TakeToken(C)||Pending>=2){PC->ClientIntentResult(Id,TEXT("Vendor busy; try again"));return;}
    C.LastRequest=Id;C.Receipts.Add(Id,{FGuid(),TEXT("Purchase pending"),false});Requests.Add({PC,Match,Life,Id,Session,Revision,Price,InventoryRevision,Skill});
}
void ALDVendorDirector::Request(const FRequest& R)
{
    auto* PC=R.PC.Get();if(!PC){return;}auto& C=Clients.FindChecked(PC);auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M->StateFor(PC);auto Reject=[&](const TCHAR* Text){Reply(PC,R.Id,{FGuid(),Text});};
    if(!O||O->Match!=R.Match||O->Life!=R.Life||R.Session!=C.Session||!ValidSession(PC,C)){Reject(TEXT("Vendor closed; nothing spent"));return;}
    if(R.QuoteRevision!=CatalogRevision||R.Price!=BookPrice){Reject(TEXT("Catalog updated. Review the price and click Buy again"));return;}
    if(!LDDevelopmentTransaction::Sold(R.Skill)&&!(M->IsTacticalProfile()&&uint8(R.Skill)>=10&&M->TacticalCatalog->Find(R.Skill)&&M->TacticalCatalog->Find(R.Skill)->Circle==1)){Reject(TEXT("This book is not sold here"));return;}
    if(R.InventoryRevision!=O->Inventory.Revision){Reject(TEXT("Bag changed; review and click Buy again"));return;}
    FString CapacityReason;if(!M->Inventory->HasSpace(PC,ELDItemKind::Book,&CapacityReason)){Reply(PC,R.Id,{FGuid(),CapacityReason+TEXT("; nothing spent")});return;}
    if(O->Gold<BookPrice){Reject(TEXT("Not enough gold; nothing spent"));return;}
    ALDBook* Book=bFailCreation?nullptr:GetWorld()->SpawnActor<ALDBook>();bFailCreation=false;if(!Book){Reject(TEXT("Book creation failed; nothing spent"));return;}Book->Skill=R.Skill;Book->State=0;
    if(!M->Inventory->Adopt(PC,Book,TEXT("OfficialBookPurchase"),false)){Book->Destroy();Reject(TEXT("Inventory full; nothing spent"));return;}O->Gold-=BookPrice;M->RecordEconomyGold(TEXT("OfficialBookPurchase"),0,BookPrice);M->SyncOwner(PC);Reply(PC,R.Id,{Book->Identity,FString::Printf(TEXT("Purchased one %s book for %d gold"),LDDevelopment::SkillName(R.Skill),BookPrice)});
}
void ALDVendorDirector::Sample(double Now)
{
    for(auto It=Clients.CreateIterator();It;++It)
    {
        auto* PC=It.Key().Get();if(!PC){It.RemoveCurrent();continue;}auto& C=It.Value();if(!C.Session.IsValid()){continue;}
        if(!ValidSession(PC,C)){C.Session.Invalidate();PC->ClientVendorClosed(C.Token,TEXT("Vendor closed: range, sight, damage or eligibility changed"));}
        else if(C.SentRevision!=CatalogRevision||C.SentPrice!=BookPrice){C.SentRevision=CatalogRevision;C.SentPrice=BookPrice;PC->ClientVendorQuote(*GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC),Quote(PC,C));}
    }
    const auto Pending=MoveTemp(Requests);Requests.Reset();for(const auto& R:Pending){Request(R);}
}
void ALDVendorDirector::FailNextCreationForProbe()
{
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("LDVendorProbe"))){bFailCreation=true;}
#endif
}

void ALDVendorDirector::Invalidate(ALDPlayerController* PC){auto* C=Clients.Find(PC);if(C&&C->Session.IsValid()){C->Session.Invalidate();PC->ClientVendorClosed(C->Token,TEXT("Vendor closed: another service opened"));}}
