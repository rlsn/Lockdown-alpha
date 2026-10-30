#include "Economy/LDB92Services.h"
#include "Economy/LDB92Economy.h"
#include "Economy/LDProductionDirector.h"
#include "Population/LDB92Roster.h"
#include "Inventory/LDInventoryDirector.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDevelopmentDirector.h"
#include "World/LDWorldObjects.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

TArray<uint8> ALDB92Services::WorkRecipes(FName POI)
{
    if(POI==TEXT("capital_treasury")){return {6};}
    if(POI==TEXT("capital_guild")||POI==TEXT("riverport_guild")){return {7};}
    static const TArray<FName> Workshops={TEXT("capital_workshop"),TEXT("highfort_workshop"),TEXT("scholars_workshop"),TEXT("riverport_workshop"),TEXT("ironworks_workshop"),TEXT("oakmere"),TEXT("millford"),TEXT("reedbank"),TEXT("ashcross"),TEXT("bellwick"),TEXT("stonewake")};
    return Workshops.Contains(POI)?TArray<uint8>{1,2}:TArray<uint8>{};
}
FString ALDB92Services::WorkName(uint8 Recipe)
{return Recipe==1?TEXT("Craft Toolkit"):Recipe==2?TEXT("Craft Core"):Recipe==6?TEXT("Core procurement"):Recipe==7?TEXT("Supply commission"):TEXT("Unknown work");}
FName ALDB92Economy::WorkBudget(FGuid Id,uint8 Recipe)
{return FName(*((Recipe==6?TEXT("Core:"):TEXT("Supply:"))+Id.ToString(EGuidFormats::Digits)));}
bool ALDB92Economy::WorkTerms(ALDB92Controller* PC,FName POI,const FLDB92MarketOrder& R,bool Reserved,FString& Error) const
{
    if(R.CourierAction){return R.CourierAction==2&&CourierTerms(PC,POI,R,Reserved,Error);}
    auto Reject=[&](const TCHAR* Why){Error=Why;return false;};auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M?M->StateFor(PC):nullptr;
    if(!HasAuthority()||!Ready||!FullWorld||Closed||!O||!O->bActive||!Seats.Contains(M->ContenderId(PC))||!ALDB92Services::WorkRecipes(POI).Contains(R.WorkRecipe)||R.Inputs.Num()>3||!ALDProductionDirector::RecipeInputs(R.WorkRecipe,R.Inputs)||R.Supply||R.BankAction||R.Amount||R.BankRevision||R.Item.IsValid()||R.ItemRevision||!R.Buy||R.Skill!=ELDSkill::None||(!Reserved&&R.Revision!=WorkRevision)||WorkRevision==MAX_uint32||O->Inventory.Revision>=MAX_uint32-4)
    {return Reject(TEXT("Work or exact recipe terms changed"));}
    const uint8 Recipe=R.WorkRecipe;const int32 Fee=Recipe==1?Catalog.ToolkitFee:Recipe==2?Catalog.CoreFee:0,Reward=Recipe==6?80:Recipe==7?50:0;
    const auto W=Wallet(M->ContenderId(PC));const auto* A=Ledger.Account(W);
    if(!A||A->Revision!=R.WalletRevision||A->Gold!=O->Gold||A->Gold<Fee||A->Gold>MAX_int64-Reward||R.Price!=Fee||R.Kind!=(Recipe==1||Recipe==7?ELDItemKind::Toolkit:ELDItemKind::Core)||R.InventoryRevision!=O->Inventory.Revision)
    {return Reject(TEXT("Bag, wallet or quoted fee changed; confirm again"));}
    if(Reward&&(Ledger.Remaining(Recipe==6?TEXT("CoreOrders"):TEXT("SupplyOrders"))<=0||Ledger.Remaining(WorkBudget(M->ContenderId(PC),Recipe))<=0))
    {return Reject(TEXT("Procurement quota exhausted"));}
    TArray<FGuid> Outgoing;
    for(const auto& I:R.Inputs)
    {
        auto* P=M->Inventory->Owned(PC,I.Id);const auto* Own=O->Inventory.Find(I.Id);const auto* L=Ledger.Item(I.Id);
        if(!P||!Own||!L||L->Consumed||L->Location!=W||P->Kind!=I.Kind||Own->Kind!=I.Kind||P->Revision!=I.Revision||Own->Revision!=I.Revision||P->bReserved!=Reserved||Own->bReserved!=Reserved||P->BindingMask||Own->BindingMask||P->Revision>=MAX_uint32-2)
        {return Reject(TEXT("Exact input unavailable, bound, changed or reserved"));}Outgoing.Add(I.Id);
    }
    return LDInventory::Fits(O->Inventory,O->Build,Outgoing,Recipe<6?TArray<ELDItemKind>{R.Kind}:TArray<ELDItemKind>{},&Error);
}
bool ALDB92Economy::CommitWork(ALDB92Controller* PC,FName POI,const FLDB92MarketOrder& R,FGuid Receipt,FString& Error)
{
    if(R.CourierAction){return R.CourierAction==2&&CourierCommand(PC,POI,R,Receipt,Error,true);}
    if(!Receipt.IsValid()||Ledger.Receipt(Receipt)||!SynchronizeWorld(Error)||!Audit(Error)||!WorkTerms(PC,POI,R,true,Error)){return false;}
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M->StateFor(PC);const uint8 Recipe=R.WorkRecipe;const auto Id=M->ContenderId(PC);const auto W=Wallet(Id);const auto* A=Ledger.Account(W);
    const int32 Reward=Recipe==6?80:Recipe==7?50:0;
    ALDItem* Output=nullptr;if(Recipe<6){Output=GetWorld()->SpawnActor<ALDItem>();if(!Output){Error=TEXT("Output allocation failed; nothing spent");return false;}Output->Kind=R.Kind;}
    LDEconomy::FTransaction T;T.Id=Receipt;T.Cause=Recipe==6?TEXT("B92CoreProcurement"):Recipe==7?TEXT("B92SupplyProcurement"):TEXT("B92CraftProduction");T.Mint=Reward;T.Burn=R.Price;
    if(Reward||R.Price){T.Gold.Add({W,A->Revision,int64(Reward)-R.Price});}
    if(Reward){T.Budget=Recipe==6?TEXT("CoreOrders"):TEXT("SupplyOrders");T.Units=1;T.Budgets.Add({WorkBudget(Id,Recipe),1});}
    TArray<ALDItem*> Physical;TArray<FGuid> Parents;
    for(const auto& I:R.Inputs){auto* P=M->Inventory->Owned(PC,I.Id);Physical.Add(P);Parents.Add(I.Id);LDEconomy::FItemDelta D;D.Id=I.Id;D.Kind=I.Kind;D.Expected=Ledger.Item(I.Id)->Revision;D.Op=LDEconomy::EItemOp::Consume;D.From=W;T.Items.Add(D);}
    if(Output){LDEconomy::FItemDelta D;D.Id=Output->Identity;D.Kind=Output->Kind;D.Op=LDEconomy::EItemOp::Create;D.To=W;D.Parents=Parents;T.Items.Add(D);}
    if(!Ledger.Commit(T,Error)){if(Output){Output->Destroy();}return false;}
    for(auto* P:Physical){ALDWorldObjects::Get(GetWorld())->RecordConsumption(P);LDInventory::Remove(O->Inventory,P->Identity);P->bReserved=false;P->State=2;P->Holder.Reset();Custody.Remove(P);P->Destroy();}
    if(Output)
    {
        uint8 Slot=0;while(O->Inventory.Items.ContainsByPredicate([&](const auto& I){return I.Slot==Slot;})){++Slot;}
        Output->Slot=Slot;Output->State=1;Output->Holder=PC;Output->OriginReceipt=T.Id;Output->ParentIds=Parents;++Output->Revision;FLDInventoryItem I;Output->Project(I);O->Inventory.Items.Add(I);Custody.Add(Output);
    }
    ++O->Inventory.Revision;O->Gold=Ledger.Account(W)->Gold;if(Reward){++WorkRevision;}
    if(Recipe==6){++O->CoreOrders;if(!O->IndustrialEndorsement.IsValid()){O->IndustrialEndorsement=T.Id;}}if(Recipe==7){++O->SupplyOrders;O->SupplyEndorsement=T.Id;}
    M->Inventory->Changed(PC);if(Reward){M->AwardExperience(PC,Recipe==6?60:30);}
    Error=Reward?TEXT("Delivery complete; reward and endorsement recorded once"):TEXT("Craft complete; fee burned, real output in bag, no XP");return true;
}
void ALDB92Services::WorkProgress(ALDB92Controller* PC,const FWork& W,bool Active)
{FLDB91Channel P;P.Match=W.Match;P.Life=W.Life;P.Domain=6;P.Sequence=W.Order.Sequence;P.Started=W.Started;P.Ends=W.Ends;P.Label=(W.Order.CourierAction?FString(TEXT("Courier delivery")):WorkName(W.Order.WorkRecipe))+TEXT(" (S to stop)");PC->ClientB91Channel(P,Active);}
bool ALDB92Services::BeginWork(ALDB92Controller* PC,FClient& C,const FLDB92MarketOrder& R,FGuid Receipt,FString& Error)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* E=M->Population->Economy.Get();auto* O=M->StateFor(PC);auto* B=Cast<ALDCharacter>(PC->GetPawn());
    if(C.Work.Active||!B->GetCharacterMovement()->IsMovingOnGround()||B->GetVelocity().SizeSquared2D()>1||B->GetCharacterMovement()->GetCurrentAcceleration().SizeSquared2D()>1||!B->GetLastMovementInputVector().IsNearlyZero()||!M->Combat->QuietForPreparation(B)||!E->SynchronizeWorld(Error)||!E->Audit(Error)||!E->WorkTerms(PC,C.View.POI,R,false,Error))
    {if(Error.IsEmpty()){Error=TEXT("Stop moving and finish other actions before work");}return false;}
    const auto& Rules=E->Rules();FWork W;W.Active=true;W.Match=O->Match;W.Life=O->Life;W.BuildRevision=O->Build.Revision;W.Body=B;W.Anchor=B->GetActorLocation();W.Started=GetWorld()->GetTimeSeconds();W.Ends=W.Started+(!R.CourierAction&&R.WorkRecipe==1?Rules.ToolkitSeconds:!R.CourierAction&&R.WorkRecipe==2?Rules.CoreSeconds:3);W.Order=R;W.Receipt=Receipt;
    if(W.Ends>=M->GetGameState<ALDGameState>()->MatchDeadline){Error=TEXT("Not enough match time to finish");return false;}
    auto* Combat=M->Combat->StateFor(B);W.Damage=Combat->LastDamage;W.Dealt=Combat->LastDealt;
    for(auto& I:W.Order.Inputs){auto* P=M->Inventory->Owned(PC,I.Id);P->bReserved=true;++P->Revision;auto* Own=O->Inventory.Items.FindByPredicate([&](const auto& V){return V.Id==I.Id;});P->Project(*Own);I=*Own;}
    ++O->Inventory.Revision;W.Order.InventoryRevision=O->Inventory.Revision;C.Work=W;Combat->Action=8;Combat->Skill=ELDSkill::None;Combat->ImpactAt=W.Ends;O->DevelopmentKind=!R.CourierAction&&R.WorkRecipe<6?5:7;O->ProductionSeconds=W.Ends-W.Started;O->DevelopmentToken=R.Sequence;O->DevelopmentEndsAt=W.Ends;
    M->Inventory->Changed(PC);WorkProgress(PC,W,true);Error=TEXT("Work started; release the button. S stops without spending");return true;
}
void ALDB92Services::StopWork(ALDB92Controller* PC,const FString& Reason)
{
    auto* C=Clients.Find(PC);if(!C||!C->Work.Active){return;}const FWork W=C->Work;C->Work.Active=false;auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M->StateFor(PC);
    if(O)
    {
        bool Changed=false;for(const auto& I:W.Order.Inputs){auto* P=M->Inventory->Owned(PC,I.Id);auto* Own=O->Inventory.Items.FindByPredicate([&](const auto& V){return V.Id==I.Id;});if(P&&Own&&P->bReserved&&P->Revision==I.Revision){P->bReserved=false;++P->Revision;P->Project(*Own);Changed=true;}}
        if(Changed){++O->Inventory.Revision;}
        if(O->Match==W.Match&&O->Life==W.Life&&(O->DevelopmentKind==5||O->DevelopmentKind==7)&&O->DevelopmentToken==W.Order.Sequence){O->DevelopmentKind=0;O->DevelopmentToken=0;O->DevelopmentEndsAt=0;O->ProductionSeconds=0;if(auto* S=M->Combat->StateFor(W.Body.Get());S&&S->Action==8&&S->ImpactAt==W.Ends){S->Action=0;S->ImpactAt=0;}O->ActionCue=0;O->SkillEndsAt=0;}
        M->Inventory->Changed(PC);
    }
    WorkProgress(PC,W,false);if(auto* Cached=C->Receipts.Find(W.Order.Sequence)){Cached->Message=Reason;Cached->Accepted=M->Population->Economy->Accounting().Receipt(W.Receipt)!=nullptr;}
    Reply(PC,W.Order.Sequence,W.Receipt,M->Population->Economy->Accounting().Receipt(W.Receipt)!=nullptr,Reason);
}
void ALDB92Services::SampleWork(ALDB92Controller* PC,FClient& C)
{
    auto& W=C.Work;if(!W.Active){return;}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M->StateFor(PC);const auto* B=Cast<ALDCharacter>(PC->GetPawn());const auto* S=M->Combat->StateFor(B);FString Error;
    if(!O||!B||!S||O->Match!=W.Match||O->Life!=W.Life||O->Build.Revision!=W.BuildRevision||B!=W.Body.Get()||O->DevelopmentToken!=W.Order.Sequence||!Eligible(PC,C.View.POI,true)||S->LastDamage!=W.Damage||S->LastDealt!=W.Dealt||FVector::DistSquared(B->GetActorLocation(),W.Anchor)>4||B->GetVelocity().SizeSquared2D()>1||!B->GetCharacterMovement()->IsMovingOnGround()||B->GetCharacterMovement()->GetCurrentAcceleration().SizeSquared2D()>1||!B->GetLastMovementInputVector().IsNearlyZero()||!M->Combat->QuietForPreparation(const_cast<ALDCharacter*>(B))||!M->Population->Economy->WorkTerms(PC,C.View.POI,W.Order,true,Error))
    {StopWork(PC,TEXT("Work interrupted; inputs, fee and quotas retained"));return;}
    if(GetWorld()->GetTimeSeconds()<W.Ends){return;}M->Population->Economy->CommitWork(PC,C.View.POI,W.Order,W.Receipt,Error);StopWork(PC,Error);
}
