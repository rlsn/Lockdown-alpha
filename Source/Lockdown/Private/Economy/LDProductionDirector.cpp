#include "Economy/LDProductionDirector.h"
#include "Framework/LDCommandLiveness.h"
#include "World/LDWorldDirector.h"
#include "Economy/LDEconomyDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDevelopmentDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDAcquisitionDirector.h"
#include "Inventory/LDAcquisitionCatalog.h"
#include "Inventory/LDTradeDirector.h"
#include "Inventory/LDVendorDirector.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Region/LDRegionDefinition.h"
#include "Core/LDGroundQueries.h"
#include "Components/SceneComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
namespace
{
    bool Still(const ALDCharacter* B){return B&&B->GetVelocity().SizeSquared2D()<=1&&B->GetCharacterMovement()->GetCurrentAcceleration().SizeSquared2D()<=1&&B->GetLastMovementInputVector().IsNearlyZero();}
}
FVector ALDProductionDirector::Position(const UWorld* W,uint8 Site)
{if((W->GetMapName().Contains(TEXT("L_V07Region"))||W->GetMapName().Contains(TEXT("L_V08Region")))&&Site>=5&&Site<9){const auto* C=ULDWorldCatalog::Load();return C->Position(Site==5?TEXT("HeartForge"):FString::Printf(TEXT("Ruin%c"),'A'+Site-6));}if(Site>=6&&Site<9){const FVector Offsets[]={FVector(-1400,800,0),FVector(0,1050,0),FVector(1450,800,0)};return Offsets[Site-6];}if(Site==5){return LDRegion::Site(W,6)+FVector(-200,220,0);}
    return Site<2?LDRegion::Site(W,8)+FVector(Site?160:-160,0,0):LDRegion::Site(W,Site==3?2:Site==4?6:1);}
FString ALDProductionDirector::SiteName(uint8 Site)
{const TCHAR* N[]={TEXT("Crystal outcrop 1"),TEXT("Crystal outcrop 2"),TEXT("City workshop"),TEXT("West workshop"),TEXT("East workshop"),TEXT("HeartForge"),TEXT("Ruin A cache"),TEXT("Ruin B cache"),TEXT("Ruin C cache")};return Site<9?N[Site]:TEXT("Unknown production site");}
FName ALDProductionDirector::Budget(uint8 Site){return Site==0?FName(TEXT("Outcrop1")):Site==1?FName(TEXT("Outcrop2")):NAME_None;}
ELDItemKind ALDProductionDirector::OutputKind(uint8 Recipe){return Recipe==1?ELDItemKind::Toolkit:Recipe==2?ELDItemKind::Core:ELDItemKind::Crystal;}
bool ALDProductionDirector::RecipeInputs(uint8 Recipe,const TArray<FLDInventoryItem>& Inputs)
{
    if(Recipe==8){return Inputs.IsEmpty();}
    if(Recipe==6){return Inputs.Num()==1&&Inputs[0].Id.IsValid()&&Inputs[0].Kind==ELDItemKind::Core;}
    if(Recipe==7){return Inputs.Num()==2&&Inputs[0].Id.IsValid()&&Inputs[1].Id.IsValid()&&Inputs[0].Id!=Inputs[1].Id&&Inputs[0].Kind==ELDItemKind::Toolkit&&Inputs[1].Kind==ELDItemKind::Toolkit;}
    if(Recipe==5){return Inputs.Num()==1&&Inputs[0].Id.IsValid()&&Inputs[0].Kind==ELDItemKind::Toolkit;}
    if(!Recipe){return Inputs.IsEmpty();}if(Recipe>2||Inputs.Num()!=3){return false;}TSet<FGuid> Ids;int32 First=0,Second=0;
    for(const auto& I:Inputs){if(!I.Id.IsValid()||Ids.Contains(I.Id)){return false;}Ids.Add(I.Id);First+=I.Kind==(Recipe==1?ELDItemKind::Fiber:ELDItemKind::Toolkit);Second+=I.Kind==(Recipe==1?ELDItemKind::Ore:ELDItemKind::Crystal);}return First==2&&Second==1;
}
ALDProductionSite::ALDProductionSite(){bReplicates=false;RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));}
void ALDProductionDirector::Initialize()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!Targets.IsEmpty()||!M||!M->Combat){return;}
    for(uint8 Site=0;Site<(M->IsTacticalProfile()?9:5);++Site)
    {
        auto* T=GetWorld()->SpawnActor<ALDProductionSite>();if(!T){return;}T->Site=Site;T->SetActorLocation(Position(GetWorld(),Site));Targets.Add(T);
        if(Site>=5){continue;}M->Combat->RegisterSkillTask(T,{Site<2?ELDSkill::Survey:ELDSkill::Analyze},[this,Site](ALDCharacter* B,ELDSkill K,const FLDCharacterBuild& Build,uint32 Token,ELDSkillTaskPhase Phase,FString& Error){return SkillUse(B,Site,K,Build,Token,Phase,Error);});
    }
}
bool ALDProductionDirector::Eligible(ALDPlayerController* PC,uint8 Site,bool Quiet,bool AllowAction) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M?M->StateFor(PC):nullptr;const auto* B=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;const auto* S=M&&M->Combat?M->Combat->StateFor(B):nullptr;const auto* G=GetWorld()->GetGameState<ALDGameState>();const double Now=GetWorld()->GetTimeSeconds();const FVector To=Position(GetWorld(),Site);
    return Site<(M&&M->IsTacticalProfile()?9:5)&&M&&M->Economy&&M->Vision&&M->IsPlaying()&&G&&Now<G->MatchDeadline&&O&&O->bActive&&S&&S->Hp>0&&M->Combat->Allows(const_cast<ALDCharacter*>(B),LDTactical::EAction::Service)&&Now>=O->ProtectedUntil&&Now>=S->ProtectionUntil
        &&(AllowAction||(!S->Action&&!O->DevelopmentKind&&!M->Objectives->HasChannel(PC)))&&(!Quiet||Now-FMath::Max(S->LastDamage,S->LastDealt)>=6&&(!M->IsTacticalProfile()||M->Combat->QuietForPreparation(const_cast<ALDCharacter*>(B))))
        &&Still(B)&&FVector::DistSquared2D(B->GetActorLocation(),To)<=FMath::Square(M->Economy->Rules().Range)&&LDGround::ClearSegment(GetWorld(),B->GetActorLocation(),To)
        &&M->Vision->Ray({B->GetActorLocation(),120,M->Vision->LevelAt(B->GetActorLocation())},To);
}
void ALDProductionDirector::Reply(ALDPlayerController* PC,uint32 Token,const FString& Reason)
{
    auto& S=States.FindOrAdd(PC);const FString Copy=Reason;S.Receipts.Add(Token,Copy);while(S.Receipts.Num()>32){uint32 Old=MAX_uint32;for(const auto& P:S.Receipts){Old=FMath::Min(Old,P.Key);}S.Receipts.Remove(Old);}
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(auto* O=M->StateFor(PC)){M->SyncOwner(PC);PC->ClientProductionResult(*O,Token,Copy);}
}
void ALDProductionDirector::Queue(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Token,const FLDProductionPlan& Plan)
{
    if(auto* Clock=GetWorld()->GetAuthGameMode<ALDGameMode>()->Combat.Get()){Clock->FlushPastCommand();}
    if(!HasAuthority()||!PC){return;}auto& S=States.FindOrAdd(PC);auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M->StateFor(PC);
    if(!Token||!O||O->Match!=Match||O->Life!=Life||Plan.Inputs.Num()>4||Plan.CatalogDigest.Len()>64||Plan.Mode>1){Reply(PC,Token,TEXT("Invalid production request or life"));return;}
    if(const auto* R=S.Receipts.Find(Token)){Reply(PC,Token,*R);return;}if(Token<=S.LastRequest){return;}S.LastRequest=Token;
    const double Now=GetWorld()->GetTimeSeconds();S.Tokens=FMath::Min(3.,S.Tokens+FMath::Max(0.,Now-S.RateAt)*4);S.RateAt=Now;
    if(S.Tokens<1||Requests.FilterByPredicate([&](const auto& R){return R.PC==PC;}).Num()>=2){Reply(PC,Token,TEXT("Production request rate limited"));return;}--S.Tokens;
    M->Objectives->CancelClaimForAction(PC,Now);Requests.Add({PC,Match,Life,Token,Plan});
}
bool ALDProductionDirector::Terms(ALDPlayerController* PC,const FLDProductionPlan& P,double Ends,bool Reserved,FString& Error) const
{
    auto Reject=[&](const TCHAR* Why){Error=Why;return false;};auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M->StateFor(PC);const auto& Rules=M->Economy->Rules();
    if(P.Recipe>=20){return M->Acquisition&&M->Acquisition->Terms(PC,P,Reserved,Error);}
    if(!O||P.Site>4||(P.Recipe>2&&(P.Recipe<5||P.Recipe>8))||((P.Site<2)!=(P.Recipe==0))||P.RecipeRevision!=Rules.RecipeRevision||!RecipeInputs(P.Recipe,P.Inputs)){return Reject(TEXT("Recipe changed or wrong exact inputs"));}
    if(P.Recipe==5&&(P.Site!=4||P.Fee||P.Coupon.IsValid()||!M->Economy->ShortageAvailable(Ends)||O->Gold>MAX_int64-Rules.ShortageGold)){return Reject(TEXT("Shortage order unavailable, expired or wallet full"));}
    if(P.Recipe>=6&&(P.Fee||P.Coupon.IsValid()||P.Site!=(P.Recipe==7?3:2))){return Reject(TEXT("Wrong procurement or civic terminal terms"));}
    if(P.Recipe==8&&!M->Economy->CanActivateCharter(PC,Ends)){return Reject(TEXT("Charter unavailable, recovering, window occupied or too late"));}
    if((P.Recipe==6||P.Recipe==7)&&(!M->Economy->OrderAvailable(PC,P.Recipe==6)||O->Gold>MAX_int64-(P.Recipe==6?80:50))){return Reject(TEXT("Procurement quota exhausted or wallet full"));}
    if(!Reserved&&P.InventoryRevision!=O->Inventory.Revision){return Reject(TEXT("Bag changed; preview again"));}
    const int32 Base=P.Recipe==1?Rules.ToolkitFee:P.Recipe==2?Rules.CoreFee:0;
    if(P.Coupon.IsValid()&&(!P.Recipe||P.Coupon!=O->CraftCoupon||O->CouponSite!=P.Site||O->CouponUntil<=Ends)){return Reject(TEXT("Coupon cannot survive completion; preview without it"));}
    if(P.Fee!=FMath::Max(0,Base-(P.Coupon.IsValid()?Rules.CouponDiscount:0))||O->Gold<P.Fee){return Reject(TEXT("Carried gold or fee changed; nothing spent"));}
    TArray<FGuid> Outgoing;
    for(const auto& I:P.Inputs){auto* A=M->Inventory->Owned(PC,I.Id);const auto* Own=O->Inventory.Find(I.Id);if(!A||!Own||A->Kind!=I.Kind||A->Revision!=I.Revision||Own->Revision!=I.Revision||A->bReserved!=Reserved||Own->bReserved!=Reserved||A->Revision>=MAX_uint32-1){return Reject(TEXT("Input unavailable, changed or reserved"));}Outgoing.Add(I.Id);}
    if(!LDInventory::Fits(O->Inventory,O->Build,Outgoing,P.Recipe>=5?TArray<ELDItemKind>{}:TArray<ELDItemKind>{OutputKind(P.Recipe)},&Error)){return false;}
    if(!P.Recipe&&M->Economy->Accounting().Remaining(Budget(P.Site))<=0){return Reject(TEXT("Outcrop depleted"));}return true;
}
void ALDProductionDirector::Reserve(ALDPlayerController* PC,FState& S,bool Value)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M->StateFor(PC);if(!O){return;}
    for(auto& I:S.Plan.Inputs){auto* A=M->Inventory->Owned(PC,I.Id);auto* Own=O->Inventory.Items.FindByPredicate([&](const auto& E){return E.Id==I.Id;});
        // Release only our still-owned reservation; never unlock a later owner.
        if(!A||!Own||(!Value&&!A->bReserved)){continue;}A->bReserved=Value;++A->Revision;Own->bReserved=Value;Own->Revision=A->Revision;I.Revision=A->Revision;++O->Inventory.Revision;}
    M->Inventory->Changed(PC);
}
void ALDProductionDirector::Request(const FRequest& R,double Now)
{
    auto* PC=R.PC.Get();if(!PC){return;}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M->StateFor(PC);auto& S=States.FindChecked(PC);FString Error;
    if(!O||O->Match!=R.Match||O->Life!=R.Life||R.Token<=S.Released||S.Active||!Eligible(PC,R.Plan.Site,R.Plan.Recipe>=20?(M->Acquisition&&M->Acquisition->Quiet(R.Plan)):(R.Plan.Recipe==1||R.Plan.Recipe==2||R.Plan.Recipe>=6))){Reply(PC,R.Token,TEXT("Production unavailable, conflicting action or released hold"));return;}
    if(R.Plan.Recipe==3||R.Plan.Recipe==4)
    {
        const ELDSkill Expected=R.Plan.Recipe==3?ELDSkill::Survey:ELDSkill::Analyze;
        if((R.Plan.Site<2)!=(Expected==ELDSkill::Survey)||!O->Build.Slots.IsValidIndex(R.Plan.SkillSlot)||O->Build.Slots[R.Plan.SkillSlot]!=Expected){Reply(PC,R.Token,TEXT("Equip the matching production skill"));return;}
        const FVector Aim=(Position(GetWorld(),R.Plan.Site)-PC->GetPawn()->GetActorLocation()).GetSafeNormal2D();
        if(!M->Combat->RequestEquipped(Cast<ALDCharacter>(PC->GetPawn()),R.Plan.BuildRevision,R.Plan.SkillSlot,R.Token,Aim,Error,Target(R.Plan.Site))){Reply(PC,R.Token,Error);return;}
        Reply(PC,R.Token,Error);return;
    }
    const auto& Rules=M->Economy->Rules();double ExtraSeconds=0;if(R.Plan.Recipe>=20){const auto* C=ULDAcquisitionCatalog::Load();if(!C){return;}ExtraSeconds=M->Acquisition?M->Acquisition->HoldSeconds(R.Plan):0;if(ExtraSeconds<=0){Reply(PC,R.Token,TEXT("Invalid acquisition recipe"));return;}}
    const double Ends=Now+(R.Plan.Recipe>=20?ExtraSeconds:R.Plan.Recipe==8?4:R.Plan.Recipe>=6?3:R.Plan.Recipe==5?Rules.ShortageSeconds:R.Plan.Recipe==1?Rules.ToolkitSeconds:R.Plan.Recipe==2?Rules.CoreSeconds:Rules.GatherSeconds);
    if(!Terms(PC,R.Plan,Ends,false,Error)){Reply(PC,R.Token,Error);return;}
    if(M->Trade){M->Trade->CancelFor(PC);}if(M->Vendor){M->Vendor->Invalidate(PC);}if(M->Commerce){M->Commerce->Invalidate(PC);}if(M->Bank){M->Bank->Invalidate(PC);}
    S.Plan=R.Plan;S.Match=R.Match;S.Life=R.Life;S.Token=R.Token;S.Ends=Ends;S.LastHold=Now;S.Sequence=0;S.Anchor=PC->GetPawn()->GetActorLocation();S.Active=true;S.Body=Cast<ALDCharacter>(PC->GetPawn());
    const auto* C=M->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()));S.Damage=C->LastDamage;S.Dealt=C->LastDealt;Reserve(PC,S,true);if(S.Plan.Recipe>=20){S.Plan.InventoryRevision=O->Inventory.Revision;}
    auto* Combat=M->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()));Combat->Action=8;Combat->ImpactAt=Ends;
    O->DevelopmentKind=R.Plan.Recipe>=6?7:R.Plan.Recipe==5?6:R.Plan.Recipe?5:4;O->DevelopmentToken=R.Token;O->DevelopmentEndsAt=Ends;O->ProductionSite=R.Plan.Site;O->ProductionSeconds=Ends-Now;
    Reply(PC,R.Token,TEXT("Production accepted; keep holding context. Release spends nothing"));
}
void ALDProductionDirector::RefreshHold(ALDPlayerController* PC,uint32 Life,uint32 Token,uint32 Sequence,bool Moving)
{
    if(auto* Clock=GetWorld()->GetAuthGameMode<ALDGameMode>()->Combat.Get()){Clock->FlushPastCommand();}auto* S=States.Find(PC);if(!S||!S->Active||S->Life!=Life||S->Token!=Token||Sequence<=S->Sequence){return;}S->Sequence=Sequence;S->LastHold=GetWorld()->GetTimeSeconds();if(Moving){Cancel(PC,TEXT("Movement cancelled production"));}}
void ALDProductionDirector::Release(ALDPlayerController* PC,uint32 Life,uint32 Token)
{
    if(auto* Clock=GetWorld()->GetAuthGameMode<ALDGameMode>()->Combat.Get()){Clock->FlushPastCommand();}auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC);if(!O||O->Life!=Life){return;}auto& S=States.FindOrAdd(PC);S.Released=FMath::Max(S.Released,Token);if(S.Active&&S.Token<=Token){Cancel(PC);}}
void ALDProductionDirector::Cancel(ALDPlayerController* PC,const FString& Reason)
{
    auto* S=States.Find(PC);if(!S||!S->Active){return;}Reserve(PC,*S,false);S->Active=false;S->Released=FMath::Max(S->Released,S->Token);
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(auto* O=M->StateFor(PC)){if(auto* C=M->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()));C&&C->Action==8){C->Action=0;C->ImpactAt=0;}O->DevelopmentKind=0;O->DevelopmentToken=0;O->DevelopmentEndsAt=0;O->ActionCue=0;O->SkillEndsAt=0;}
    Reply(PC,S->Token,Reason);
}
void ALDProductionDirector::EndLife(ALDPlayerController* PC)
{Cancel(PC);auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(auto* O=M->StateFor(PC)){O->CraftCoupon.Invalidate();O->CouponUntil=0;M->SyncOwner(PC);}}
void ALDProductionDirector::Sample(double Now)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();
    for(auto& Pair:States){auto* PC=Pair.Key.Get();auto& S=Pair.Value;if(!PC){continue;}auto* O=M->StateFor(PC);
        if(O&&O->CraftCoupon.IsValid()&&Now>=O->CouponUntil){O->CraftCoupon.Invalidate();O->CouponUntil=0;M->SyncOwner(PC);}
        if(!S.Active){continue;}const auto* B=Cast<ALDCharacter>(PC->GetPawn());const auto* C=M->Combat->StateFor(B);FString Error;
        if(!O||S.Body.Get()!=B||O->Match!=S.Match||O->Life!=S.Life||!Eligible(PC,S.Plan.Site,S.Plan.Recipe>=20?(M->Acquisition&&M->Acquisition->Quiet(S.Plan)):(S.Plan.Recipe==1||S.Plan.Recipe==2||S.Plan.Recipe>=6),true)||!C||C->Action!=8||C->LastDamage!=S.Damage||C->LastDealt!=S.Dealt||!LDCommandLiveness::ForProfile(Now,S.LastHold,M->IsUIControlProfile())||FVector::DistSquared2D(B->GetActorLocation(),S.Anchor)>4||!Terms(PC,S.Plan,FMath::Max(Now,S.Ends),true,Error))
        {Cancel(PC,Error.IsEmpty()?TEXT("Production interrupted; nothing spent"):Error);continue;}
        if(Now+1.e-6<S.Ends){continue;}FGuid Output;const bool Good=S.Plan.Recipe>=20?M->Acquisition->Commit(PC,S.Plan,Error):S.Plan.Recipe==8?M->Economy->ActivateCharter(PC,Error):(S.Plan.Recipe==6||S.Plan.Recipe==7)?M->Economy->CommitOrder(PC,S.Plan.Recipe==6,S.Plan.Inputs,Error):S.Plan.Recipe==5?M->Economy->CommitShortage(PC,S.Plan.Inputs[0],Error):M->Economy->CommitProduction(PC,S.Plan.Site,S.Plan.Recipe,S.Plan.Inputs,S.Plan.Fee,Output,Error);
        Cancel(PC,Good?(S.Plan.Recipe>=20?TEXT("Acquisition committed once; no XP, identity and cooldowns retained"):S.Plan.Recipe>=6?TEXT("Civic transaction confirmed; private receipt retained"):S.Plan.Recipe==5?TEXT("Shortage delivery confirmed; +40 gold / +20 XP, no endorsement"):TEXT("Production completed; output owned, no XP")):Error);
    }
    auto Pending=MoveTemp(Requests);Requests.Reset();for(const auto& R:Pending){Request(R,Now);}
    for(auto It=States.CreateIterator();It;++It){if(!It.Key().IsValid()){It.RemoveCurrent();}}
}
bool ALDProductionDirector::SkillUse(ALDCharacter* Body,uint8 Site,ELDSkill Skill,const FLDCharacterBuild& Build,uint32 Token,ELDSkillTaskPhase Phase,FString& Error)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* PC=Body?Cast<ALDPlayerController>(Body->GetController()):nullptr;auto* O=M->StateFor(PC);auto* C=M->Combat->StateFor(Body);const auto& Rules=M->Economy->Rules();
    if(!PC||!O||!Token||!C||!Eligible(PC,Site,false,true)||!LDDevelopment::Equipped(Build,Skill)||Skill!=(Site<2?ELDSkill::Survey:ELDSkill::Analyze)||LDDevelopment::SkillValue(Build,Skill)<(Site<2?Rules.SurveyScore:Rules.AnalyzeScore)) {Error=TEXT("Production skill needs equipped Survey score 12 / Analyze score 14, body sight and living unprotected owner");return false;}
    if(Site<2&&(!LDInventory::Fits(O->Inventory,O->Build,{}, {ELDItemKind::Crystal},&Error)||M->Economy->Accounting().Remaining(Budget(Site))<=0)){Error=TEXT("Outcrop depleted or Crystal exceeds capacity");return false;}
    if(Phase!=ELDSkillTaskPhase::Commit){return true;}
    if(C->Action!=7||C->SkillToken!=Token||C->Target!=Target(Site)){Error=TEXT("Skill channel no longer current");return false;}
    if(Site<2){FGuid Output;return M->Economy->CommitProduction(PC,Site,0,{},0,Output,Error);}
    O->CraftCoupon=FGuid::NewGuid();O->CouponSite=Site;O->CouponUntil=GetWorld()->GetTimeSeconds()+Rules.CouponSeconds;M->SyncOwner(PC);Error=TEXT("Analyze complete; one private workshop coupon, discount 2 for 30 seconds");return true;
}

double ALDProductionDirector::NextEvent(double After,double Through) const
{double Next=Through;for(const auto& P:States){if(P.Value.Active&&P.Value.Ends>After+1.e-7&&P.Value.Ends<Next){Next=P.Value.Ends;}}return Next;}
