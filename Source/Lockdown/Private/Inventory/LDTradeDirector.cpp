#include "Inventory/LDTradeDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDVendorDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDWorldChecks.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Core/LDGroundQueries.h"
#include "Engine/World.h"
#include "Misc/SecureHash.h"
ALDTradeDirector::FSession* ALDTradeDirector::Find(ALDPlayerController* PC)
{for(auto& P:Sessions){if(P.Value.Sides[0].PC==PC||P.Value.Sides[1].PC==PC){return &P.Value;}}return nullptr;}
bool ALDTradeDirector::HasSession(ALDPlayerController* PC) const
{for(const auto& P:Sessions){if(P.Value.Sides[0].PC==PC||P.Value.Sides[1].PC==PC){return true;}}return false;}
bool ALDTradeDirector::PairEligible(ALDPlayerController* A,ALDPlayerController* B) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const double Now=GetWorld()->GetTimeSeconds();
    if(!A||!B||A==B||!M->IsPlaying()){return false;}
    for(auto* PC:{A,B}){auto* O=M->StateFor(PC);auto* Body=Cast<ALDCharacter>(PC->GetPawn());const auto* C=M->Combat->StateFor(Body);if(!O||!O->bActive||!Body||!C||C->Hp<=0||Now<O->ProtectedUntil||Now<C->ProtectionUntil||C->Action||O->DevelopmentKind||M->Objectives->HasChannel(PC)||Now-FMath::Max(C->LastDamage,C->LastDealt)<6){return false;}}
    if(FVector::Dist2D(A->GetPawn()->GetActorLocation(),B->GetPawn()->GetActorLocation())>120||!LDGround::ClearSegment(GetWorld(),A->GetPawn()->GetActorLocation(),B->GetPawn()->GetActorLocation())){return false;}
    for(auto* PC:{A,B}){APawn* Target=PC==A?B->GetPawn():A->GetPawn();const auto Pos=PC->GetPawn()->GetActorLocation();if(!M->Vision->Ray({Pos,120,M->Vision->LevelAt(Pos)},Target->GetActorLocation())){return false;}const auto* V=M->Vision->AuthorityViewFor(PC);bool Seen=false;if(V){for(const auto& E:V->Entities){if(M->Vision->ResolveObserved(PC,E.Id)==Target){Seen=true;break;}}}if(!Seen){return false;}}
    return true;
}
bool ALDTradeDirector::Eligible(const FSession& S) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();for(const auto& Side:S.Sides){const auto* O=M->StateFor(Side.PC.Get());if(!O||Side.Body!=Side.PC->GetPawn()||Side.Match!=O->Match||Side.Life!=O->Life){return false;}}
    return PairEligible(S.Sides[0].PC.Get(),S.Sides[1].PC.Get());
}
bool ALDTradeDirector::Project(const FSession& S,int32 N) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto& A=S.Sides[N];const auto& B=S.Sides[1-N];const auto* O=M->StateFor(A.PC.Get());if(!O||A.Gold<0||B.Gold<0||A.Gold>O->Gold||O->Gold-A.Gold>MAX_int64-B.Gold||A.Items.Num()>6||B.Items.Num()>6){return false;}
    int32 Seals=O->Inventory.Items.FilterByPredicate([](const auto& I){return I.Kind==ELDItemKind::Seal;}).Num();
    for(const auto& I:A.Items){auto* P=M->Inventory->Owned(A.PC.Get(),I.Id);const auto* Own=O->Inventory.Find(I.Id);if(!P||!Own||!P->bReserved||!Own->bReserved||P->Revision!=I.Revision||Own->Revision!=I.Revision||P->Kind!=I.Kind||P->Skill!=I.Skill){return false;}Seals-=I.Kind==ELDItemKind::Seal;}
    for(const auto& I:B.Items){Seals+=I.Kind==ELDItemKind::Seal;}
    TArray<FGuid> Outgoing;TArray<ELDItemKind> Incoming;
    for(const auto& I:A.Items){Outgoing.Add(I.Id);}for(const auto& I:B.Items){Incoming.Add(I.Kind);}
    return Seals<=1&&LDInventory::Fits(O->Inventory,O->Build,Outgoing,Incoming);
}
void ALDTradeDirector::Reserve(FSide& Side,const FOfferItem& Offered,bool Value)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* PC=Side.PC.Get();auto* O=M->StateFor(PC);auto* Item=M->Inventory->Owned(PC,Offered.Id);if(!O||!Item){return;}Item->bReserved=Value;++Item->Revision;for(auto& I:O->Inventory.Items){if(I.Id==Item->Identity){I.bReserved=Value;I.Revision=Item->Revision;}}++O->Inventory.Revision;M->Inventory->Changed(PC);
}
void ALDTradeDirector::Revision(FSession& S,bool Activity)
{
    ++S.Revision;if(S.Stage!=ELDTradeStage::Invited){S.Stage=ELDTradeStage::Draft;}if(Activity){S.Changed=GetWorld()->GetTimeSeconds();}
    FString Terms=S.Id.ToString()+FString::Printf(TEXT("/%u"),S.Revision);auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();
    for(auto& Side:S.Sides){Side.Ready=Side.Confirmed=false;const auto* O=M->StateFor(Side.PC.Get());if(O){Side.Balance=O->Gold;Side.Inventory=O->Inventory.Revision;}Terms+=FString::Printf(TEXT("/%lld"),Side.Gold);for(const auto& I:Side.Items){Terms+=I.Handle.ToString()+FString::Printf(TEXT("/%u/%u"),uint8(I.Kind),uint8(I.Skill));}}
    S.Digest=FMD5::HashAnsiString(*Terms);UE_LOG(LogTemp,Display,TEXT("LD_TRADE_REV session=%s revision=%u"),*S.Id.ToString(),S.Revision);
}
void ALDTradeDirector::Refresh(FSession& S)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();bool Changed=false;for(auto& Side:S.Sides){const auto* O=M->StateFor(Side.PC.Get());Changed|=O&&(O->Gold!=Side.Balance||O->Inventory.Revision!=Side.Inventory);}
    if(Changed){Revision(S,false);Push(S);}
}
FLDTradeView ALDTradeDirector::View(const FSession& S,int32 N) const
{
    const auto& A=S.Sides[N];const auto& B=S.Sides[1-N];FLDTradeView V;V.Session=S.Id;V.Match=A.Match;V.Life=A.Life;V.Revision=S.Revision;V.Digest=S.Digest;V.Stage=S.Stage;V.bReceiver=N==1;V.bOwnReady=A.Ready;V.bOtherReady=B.Ready;V.bOwnConfirmed=A.Confirmed;V.bOtherConfirmed=B.Confirmed;V.OwnGold=A.Gold;V.OtherGold=B.Gold;V.bOwnValid=Project(S,N);V.bOtherValid=Project(S,1-N);V.ExpiresAt=float(S.Stage==ELDTradeStage::Invited?S.Started+30:FMath::Min(S.Changed+60,S.Started+180));
    for(const auto& I:A.Items){FLDTradeItem T;T.Handle=I.Id;T.Kind=I.Kind;T.Skill=I.Skill;V.OwnItems.Add(T);}for(const auto& I:B.Items){FLDTradeItem T;T.Handle=I.Handle;T.Kind=I.Kind;T.Skill=I.Skill;V.OtherItems.Add(T);}
    V.Message=S.Stage==ELDTradeStage::Invited?(N?TEXT("Invitation from nearby observed humanoid"):TEXT("Invitation sent · waiting for response")):!V.bOwnValid?TEXT("Your offer needs review: check your gold, reserved items and final capacity"):!V.bOtherValid?TEXT("Other offer needs review"):S.Stage==ELDTradeStage::Review?TEXT("Review both directions. Separate confirmation required from each side."):TEXT("Editing offer · every change clears both approvals");if(S.Stage==ELDTradeStage::Invited&&N==1){const auto* Observed=GetWorld()->GetAuthGameMode<ALDGameMode>()->Vision->AuthorityViewFor(A.PC.Get());if(Observed){for(const auto& E:Observed->Entities){if(GetWorld()->GetAuthGameMode<ALDGameMode>()->Vision->ResolveObserved(A.PC.Get(),E.Id)==B.Body.Get()){V.Message=TEXT("Trade invitation from nearby stranger");break;}}}}return V;
}
void ALDTradeDirector::Send(ALDPlayerController* PC,FLDTradeView V,uint32 Request)
{if(!PC){return;}auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC);if(O){V.Serial=++Clients.FindOrAdd(PC).Serial;PC->ClientTradeUpdate(*O,V,Request);}}
void ALDTradeDirector::Push(FSession& S){for(int32 N=0;N<2;++N){Send(S.Sides[N].PC.Get(),View(S,N));}}
void ALDTradeDirector::Finish(FGuid Id,ELDTradeStage Stage,const FString& Reason)
{
    auto* S=Sessions.Find(Id);if(!S){return;}for(auto& Side:S->Sides){for(const auto& I:Side.Items){Reserve(Side,I,false);}}S->Stage=Stage;
    for(int32 N=0;N<2;++N){auto* PC=S->Sides[N].PC.Get();if(!PC){continue;}auto V=View(*S,N);V.Message=Reason;auto& C=Clients.FindOrAdd(PC);C.Terminal.Add(Id,V);C.TerminalOrder.Add(Id);while(C.TerminalOrder.Num()>8){C.Terminal.Remove(C.TerminalOrder[0]);C.TerminalOrder.RemoveAt(0);}Send(PC,V);}Sessions.Remove(Id);
}
void ALDTradeDirector::CancelFor(ALDPlayerController* PC,const FString& Reason){if(auto* S=Find(PC)){Finish(S->Id,ELDTradeStage::Cancelled,Reason);}}
void ALDTradeDirector::Reply(ALDPlayerController* PC,uint32 Id,FGuid Session,const FString& Message)
{
    auto& C=Clients.FindOrAdd(PC);C.Receipts.Add(Id,{Session,Message});while(C.Receipts.Num()>64){uint32 Old=MAX_uint32;for(const auto& P:C.Receipts){Old=FMath::Min(Old,P.Key);}C.Receipts.Remove(Old);}FLDTradeView V;
    if(auto* S=Sessions.Find(Session);S&&(S->Sides[0].PC==PC||S->Sides[1].PC==PC)){V=View(*S,S->Sides[0].PC==PC?0:1);}else if(const auto* T=C.Terminal.Find(Session)){V=*T;}
    if(!Message.IsEmpty()&&V.Stage!=ELDTradeStage::Committed){V.Message=Message;}Send(PC,V,Id);
}
void ALDTradeDirector::Queue(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Id,uint8 Command,FGuid Session,uint32 Rev,const FString& Digest,FGuid Item,int64 Gold)
{
    auto& C=Clients.FindOrAdd(PC);const double Now=GetWorld()->GetTimeSeconds();if(!Id){return;}if(const auto* Receipt=C.Receipts.Find(Id)){const auto Saved=*Receipt;Reply(PC,Id,Saved.Session,Saved.Message);return;}if(Id<=C.Last){return;}
    C.Last=Id;C.Tokens=FMath::Min(4.,C.Tokens+FMath::Max(0.,Now-C.At)*4);C.At=Now;
    if(Command==LDTrade::Cancel){C.CancelledThrough=Id;if(auto* Old=Requests.FindByPredicate([&](const auto& Q){return Q.PC==PC&&Q.Command==LDTrade::Cancel;})){*Old={PC,Match,Life,Id,Command,Session,Rev,Digest,Item,Gold,Now};}else{Requests.Add({PC,Match,Life,Id,Command,Session,Rev,Digest,Item,Gold,Now});}return;}
    if(C.Tokens<1||Requests.FilterByPredicate([&](const auto& R){return R.PC==PC;}).Num()>=4||Digest.Len()>64){FRequest Rejected{PC,Match,Life,Id,0,Session,Rev,TEXT(""),FGuid(),0,FMath::CeilToDouble((Now+.001)*4)/4};if(auto* Old=Requests.FindByPredicate([&](const auto& Q){return Q.PC==PC&&Q.Command==0;})){*Old=Rejected;}else{Requests.Add(Rejected);}return;}
    --C.Tokens;Requests.Add({PC,Match,Life,Id,Command,Session,Rev,Digest,Item,Gold,Command==LDTrade::Invite?FMath::CeilToDouble((Now+.001)*4)/4:Now});
}
void ALDTradeDirector::Request(const FRequest& R)
{
    auto* PC=R.PC.Get();if(!PC){return;}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M->StateFor(PC);auto& C=Clients.FindOrAdd(PC);auto* S=Find(PC);const double Now=GetWorld()->GetTimeSeconds();
    auto Reject=[&](const FString& Text=TEXT("Unavailable")){Reply(PC,R.Id,S?S->Id:R.Session,Text);};
    if(!O||O->Match!=R.Match||O->Life!=R.Life){Reject();return;}
    if(R.Command==LDTrade::Cancel){const FGuid Id=S?S->Id:R.Session;if(S&&(!R.Session.IsValid()||R.Session==S->Id)){Finish(S->Id,ELDTradeStage::Cancelled,TEXT("Cancelled · Nothing transferred"));}Reply(PC,R.Id,Id,TEXT(""));return;}
    if(R.Id<=C.CancelledThrough){Reject(TEXT("Cancelled · Nothing transferred"));return;}
    if(R.Command==LDTrade::Invite)
    {
        auto* Body=Cast<ALDCharacter>(M->Vision->ResolveObserved(PC,R.Item));auto* Other=Body?Cast<ALDPlayerController>(Body->GetController()):nullptr;const FGuid Identity=Body?M->WorldChecks->WalletId(Body):FGuid();const auto* Last=C.Attempts.Find(Identity);const bool Retry=Last&&Now-*Last<10;if(Identity.IsValid()&&!Retry){C.Attempts.Add(Identity,Now);}if(S||!Body||Retry||!Other||HasSession(Other)||!PairEligible(PC,Other)){Reject();return;}
        FSession New;New.Id=FGuid::NewGuid();New.Started=New.Changed=Now;for(int32 N=0;N<2;++N){auto* P=N?Other:PC;auto* State=M->StateFor(P);auto& Side=New.Sides[N];Side.PC=P;Side.Body=P->GetPawn();Side.Match=State->Match;Side.Life=State->Life;Side.Inventory=State->Inventory.Revision;Side.Balance=State->Gold;}
        Revision(New,false);const FGuid Id=New.Id;Sessions.Add(Id,MoveTemp(New));Push(Sessions.FindChecked(Id));Reply(PC,R.Id,Id,TEXT(""));return;
    }
    if(!S||S->Id!=R.Session){Reply(PC,R.Id,R.Session,TEXT("Unavailable"));return;}const FGuid Id=S->Id;const int32 N=S->Sides[0].PC==PC?0:1;auto& Side=S->Sides[N];
    if(!Eligible(*S)){Finish(Id,ELDTradeStage::Cancelled,TEXT("Trade cancelled"));Reply(PC,R.Id,Id,TEXT(""));return;}Refresh(*S);
    if(R.Command==LDTrade::Decline&&S->Stage==ELDTradeStage::Invited){Finish(Id,ELDTradeStage::Cancelled,TEXT("Unavailable"));Reply(PC,R.Id,Id,TEXT(""));return;}
    if(R.Revision!=S->Revision||R.Digest!=S->Digest){Reject(TEXT("Offer changed · Review the current terms"));return;}
    if(R.Command==LDTrade::Accept&&N==1&&S->Stage==ELDTradeStage::Invited){for(auto& P:S->Sides){M->Vendor->Invalidate(P.PC.Get());}S->Started=S->Changed=Now;S->Stage=ELDTradeStage::Draft;Revision(*S,true);Push(*S);Reply(PC,R.Id,Id,TEXT(""));return;}
    if(S->Stage==ELDTradeStage::Invited){Reject();return;}
    bool Edited=false;
    if(R.Command==LDTrade::Add)
    {auto* Item=M->Inventory->Owned(PC,R.Item);if(!Item||Item->bReserved||Side.Items.Num()>=6){Reject(TEXT("Choose an unreserved owned item; six per side maximum"));return;}FOfferItem I{Item->Identity,FGuid::NewGuid(),Item->Revision,Item->Kind,Item->Skill};Reserve(Side,I,true);I.Revision=Item->Revision;Side.Items.Add(I);Edited=true;}
    else if(R.Command==LDTrade::Remove)
    {const int32 Index=Side.Items.IndexOfByPredicate([&](const auto& I){return I.Id==R.Item;});if(Index<0){Reject();return;}Reserve(Side,Side.Items[Index],false);Side.Items.RemoveAt(Index);Edited=true;}
    else if(R.Command==LDTrade::Gold)
    {if(R.Gold<0||R.Gold>1000000000){Reject(TEXT("Gold offer must be an integer from 0 to 1000000000"));return;}if(Side.Gold==R.Gold){Reject(TEXT("Offer unchanged"));return;}Side.Gold=R.Gold;Edited=true;}
    else if(R.Command==LDTrade::Ready)
    {if(!Project(*S,0)||!Project(*S,1)||(S->Sides[0].Items.IsEmpty()&&S->Sides[1].Items.IsEmpty()&&!S->Sides[0].Gold&&!S->Sides[1].Gold)||Side.Ready){Reject(TEXT("Offer needs review · Empty exchanges cannot be readied"));return;}Side.Ready=true;S->Changed=Now;if(S->Sides[0].Ready&&S->Sides[1].Ready){S->Stage=ELDTradeStage::Review;}Push(*S);}
    else if(R.Command==LDTrade::Confirm)
    {if(S->Stage!=ELDTradeStage::Review||!S->Sides[0].Ready||!S->Sides[1].Ready||Side.Confirmed||!Project(*S,0)||!Project(*S,1)){Reject(TEXT("Offer needs review"));return;}Side.Confirmed=true;S->Changed=Now;if(S->Sides[0].Confirmed&&S->Sides[1].Confirmed){Commit(Id);Reply(PC,R.Id,Id,TEXT(""));return;}Push(*S);}
    else{Reject();return;}
    if(Edited){Revision(*S,true);Push(*S);}Reply(PC,R.Id,Id,TEXT(""));
}
void ALDTradeDirector::Commit(FGuid Id)
{
    auto* S=Sessions.Find(Id);if(!S){return;}if(!Eligible(*S)){Finish(Id,ELDTradeStage::Cancelled,TEXT("Trade cancelled"));return;}Refresh(*S);if(S->Stage!=ELDTradeStage::Review||!S->Sides[0].Confirmed||!S->Sides[1].Confirmed||!Project(*S,0)||!Project(*S,1)){Push(*S);return;}
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* A=M->StateFor(S->Sides[0].PC.Get());auto* B=M->StateFor(S->Sides[1].PC.Get());
    // All validation precedes mutation. No callbacks, tick or publication between sides.
    for(int32 N=0;N<2;++N){auto& Side=S->Sides[N];auto* O=M->StateFor(Side.PC.Get());for(const auto& I:Side.Items){LDInventory::Remove(O->Inventory,I.Id);}}
    for(int32 N=0;N<2;++N){auto& Target=S->Sides[1-N];auto* O=M->StateFor(Target.PC.Get());for(const auto& I:S->Sides[N].Items){auto* P=M->Inventory->Owned(S->Sides[N].PC.Get(),I.Id);check(P);uint8 Slot=0;while(O->Inventory.Items.ContainsByPredicate([&](const auto& E){return E.Slot==Slot;})){++Slot;}P->Holder=Target.PC;P->Slot=Slot;P->bReserved=false;++P->Revision;if(auto* Seal=Cast<ALDSeal>(P)){Seal->Carrier=Target.PC;}FLDInventoryItem E;E.Id=P->Identity;E.Kind=P->Kind;E.Skill=P->Skill;E.Slot=Slot;E.Revision=P->Revision;O->Inventory.Items.Add(E);}}
    const int64 GoldA=A->Gold-S->Sides[0].Gold+S->Sides[1].Gold,GoldB=B->Gold-S->Sides[1].Gold+S->Sides[0].Gold;A->Gold=GoldA;B->Gold=GoldB;++A->Inventory.Revision;++B->Inventory.Revision;
    M->Inventory->Changed(S->Sides[0].PC.Get());M->Inventory->Changed(S->Sides[1].PC.Get());UE_LOG(LogTemp,Display,TEXT("LD_TRADE_COMMIT session=%s revision=%u"),*Id.ToString(),S->Revision);Finish(Id,ELDTradeStage::Committed,TEXT("Completed · Exchange received"));
}
void ALDTradeDirector::Sample(double Now)
{
    TArray<FGuid> Active;Sessions.GetKeys(Active);for(auto Id:Active){auto* S=Sessions.Find(Id);if(!S){continue;}if(!Eligible(*S)){Finish(Id,ELDTradeStage::Cancelled,TEXT("Trade cancelled"));continue;}if(Now>=(S->Stage==ELDTradeStage::Invited?S->Started+30:FMath::Min(S->Changed+60,S->Started+180))){Finish(Id,ELDTradeStage::Expired,TEXT("Expired · Nothing transferred"));continue;}Refresh(*S);}
    auto Pending=MoveTemp(Requests);Requests.Reset();Pending.StableSort([](const auto& A,const auto& B){return A.Command==LDTrade::Cancel&&B.Command!=LDTrade::Cancel;});for(const auto& R:Pending){if(R.Due>Now){Requests.Add(R);}else{Request(R);}}
    for(auto It=Clients.CreateIterator();It;++It){if(!It.Key().IsValid()){It.RemoveCurrent();continue;}for(auto A=It.Value().Attempts.CreateIterator();A;++A){if(Now-A.Value()>10){A.RemoveCurrent();}}}
}
