#include "Population/LDB92Roster.h"
#include "World/LDWorldObjects.h"
#include "Inventory/LDInventoryDirector.h"
#include "Economy/LDB92Deposits.h"
#include "Economy/LDB92Witness.h"
#include "Development/LDDevelopmentDirector.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "UnrealClient.h"

void ALDB92Controller::ServerB92ProbeHello_Implementation(uint8 ClientRole)
{
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("LDB92ServicesProbe"))&&ClientRole>=1&&ClientRole<=3){ServicesProbeRole=ClientRole;}
#endif
}
void ALDB92Controller::ServerB92ProbeReport_Implementation(uint8 Task,const FString& Report)
{
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("LDB92ServicesProbe"))&&Report.Len()<12000){ServicesProbeReports.Add(Task,Report);}
#endif
}
void ALDB92Controller::ClientB92ProbeTask_Implementation(uint8 Task,FName POI)
{
#if !UE_BUILD_SHIPPING
    if(!FParse::Param(FCommandLine::Get(),TEXT("LDB92ServicesProbe"))){return;}
    ServicesProbeTask=Task;ServicesProbeStep=0;ServicesProbePOI=POI;ServicesProbeAt=GetWorld()->GetTimeSeconds();ServicesProbeBefore.Reset();for(const auto& I:OwnerState.Inventory.Items){ServicesProbeBefore.Add(I.Id);}
#endif
}
void ALDB92Controller::TickServicesProbe()
{
#if !UE_BUILD_SHIPPING
    if(GetNetMode()!=NM_Client||!FParse::Param(FCommandLine::Get(),TEXT("LDB92ServicesProbe"))||!OwnerState.Match||!GetPawn()){return;}
    FString ProbeLabel;FParse::Value(FCommandLine::Get(),TEXT("B92Role="),ProbeLabel);
    if(!ServicesProbeRole){ServicesProbeRole=ProbeLabel==TEXT("B")?2:ProbeLabel==TEXT("A2")?3:1;ServerB92ProbeHello(ServicesProbeRole);}
    const double Now=GetWorld()->GetTimeSeconds();const uint8 Task=ServicesProbeTask;if(!Task){return;}
    auto Complete=[&](bool Accepted)
    {
        auto J=MakeShared<FJsonObject>();J->SetBoolField(TEXT("completed"),true);J->SetStringField(TEXT("feedback"),IntentFeedback);J->SetStringField(TEXT("market_feedback"),MarketMessage);J->SetBoolField(TEXT("accepted"),Accepted);J->SetStringField(TEXT("gold"),LexToString(OwnerState.Gold));J->SetStringField(TEXT("bank"),LexToString(OwnerState.BankGold));J->SetNumberField(TEXT("life"),OwnerState.Life);J->SetNumberField(TEXT("combat_floor"),CombatTokenFloor());J->SetBoolField(TEXT("duplicate_same"),ServicesProbeReplaySame);J->SetNumberField(TEXT("bag_count"),OwnerState.Inventory.Items.Num());J->SetBoolField(TEXT("has_quote"),MarketView.Session.IsValid());J->SetNumberField(TEXT("offers"),MarketView.Offers.Num());J->SetStringField(TEXT("quote_revision"),LexToString(MarketView.Revision));
        TArray<TSharedPtr<FJsonValue>> Items;for(const auto& I:OwnerState.Inventory.Items){auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("id"),I.Id.ToString());V->SetNumberField(TEXT("kind"),uint8(I.Kind));Items.Add(MakeShared<FJsonValueObject>(V));}J->SetArrayField(TEXT("items"),Items);
        FString Json,Output;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Json));FParse::Value(FCommandLine::Get(),TEXT("B92Output="),Output);FFileHelper::SaveStringToFile(Json,*(Output/FString::Printf(TEXT("%s_%d.json"),*ProbeLabel,Task)),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);ServerB92ProbeReport(Task,Json);ServicesProbeTask=0;
    };
    auto Send=[&](FLDB92MarketOrder R){ServicesProbeSequence=R.Sequence;ServicesProbeSerial=MarketReceiptSerial;ServerB92Order(R);ServicesProbeAt=Now;};
    if(Task>=51&&Task<=69)
    {
        if(Task==52)
        {
            if(!ServicesProbeStep){const auto* Book=OwnerState.Inventory.Items.FindByPredicate([](const auto& I){return I.Kind==ELDItemKind::Book&&I.Skill==ELDSkill::Parley;});if(!Book){Complete(false);return;}BagSelection=OwnerState.Inventory.Items.IndexOfByPredicate([&](const auto& I){return I.Id==Book->Id;});ItemCommand(TEXT("Read"),0);ServicesProbeStep=1;ServicesProbeAt=Now;}
            if(Now-ServicesProbeAt>4&&!OwnerState.DevelopmentKind){Complete(LDDevelopment::Rank(OwnerState.Build,ELDSkill::Parley)==1);}return;
        }
        if(Task==53||Task==54||Task==56)
        {
            if(!ServicesProbeStep){if(Task!=54){auto P=LDDevelopment::PlanFrom(OwnerState.Build);P.InventoryRevision=OwnerState.Inventory.Revision;P.Bindings=OwnerState.Inventory.Bindings;for(int32 I=0;I<P.Slots.Num();++I){if(P.Slots[I]==ELDSkill::Parley){P.Slots[I]=ELDSkill::None;P.Actions[I]=FLDActionRef();}}P.Slots[0]=ELDSkill::Parley;P.Actions[0]=FLDActionRef();P.Actions[0].Skill=ELDSkill::Parley;if(Task==56){P.Attributes[LDDevelopment::CHA]+=2;P.Ranks[uint8(ELDSkill::Parley)]=2;}ArmPreparation(P);}else{PointerOrder(true,FGuid(),FGuid(),ALDB92Services::Counter(GetWorld(),TEXT("riverport_workshop")));}ServicesProbeStep=1;ServicesProbeAt=Now;}
            if(Now-ServicesProbeAt>(Task==53?3:4)&&!OwnerState.DevelopmentKind){Complete(OwnerState.Build.Slots[0]==ELDSkill::Parley&&(Task!=56||LDDevelopment::SkillValue(OwnerState.Build,ELDSkill::Parley)>=12));}return;
        }
        if(Task==55||Task==57||Task==58)
        {
            if(!ServicesProbeStep){ArmSkill(0);ConfirmArmed(ALDB92Witness::Position(GetWorld(),ServicesProbePOI==TEXT("capital_guild")?0:1,2),FGuid());ServicesProbeStep=1;ServicesProbeAt=Now;return;}
            if(Task==57&&ServicesProbeStep==1&&ChannelActive&&Channel.Domain==7){StopSourceOrder();ServicesProbeStep=2;}
            if(Task==58&&ServicesProbeStep==1&&ChannelActive&&Channel.Domain==7){if(FParse::Param(FCommandLine::Get(),TEXT("B92Capture"))){FString Out;FParse::Value(FCommandLine::Get(),TEXT("B92Output="),Out);FScreenshotRequest::RequestScreenshot(Out/TEXT("Witness.png"),true,false);}ServicesProbeStep=2;}
            if(Now-ServicesProbeAt>3.2){Complete(OwnerState.Witness.IsValid());}return;
        }
        if(Task==69)
        {if(!ServicesProbeStep){CharterPage=true;if(FParse::Param(FCommandLine::Get(),TEXT("B92Capture"))){FString Out;FParse::Value(FCommandLine::Get(),TEXT("B92Output="),Out);FScreenshotRequest::RequestScreenshot(Out/TEXT("Charter.png"),true,false);}ServicesProbeStep=1;ServicesProbeAt=Now;}else if(Now-ServicesProbeAt>1){Complete(OwnerState.CharterPurchase.IsValid());}return;}
        if(!ServicesProbeStep){ServicesProbeSequence=++MarketSequence;ServerB92Open(OwnerState.Match,OwnerState.Life,ServicesProbeSequence,ServicesProbePOI);ServicesProbeStep=1;return;}
        if(ServicesProbeStep==1)
        {
            if(MarketReceiptSequence!=ServicesProbeSequence||!MarketView.Session.IsValid()||Now-ServicesProbeAt<.65){return;}FLDB92MarketOrder R;
            if(Task==51||Task==60||Task==61){const auto* Offer=MarketView.Offers.FindByPredicate([&](const auto& V){return Task==51?V.Kind==ELDItemKind::Book&&V.Skill==ELDSkill::Parley:V.Kind==(Task==60?ELDItemKind::Fiber:ELDItemKind::Ore);});if(!Offer){Complete(false);return;}R=MarketOrder(true,Offer->Kind);R.Skill=Offer->Skill;R.Price=Offer->Ask;R.Supply=true;}
            else if(Task==66){R=MarketOrder(true,ELDItemKind::Fiber);R.BankAction=1;R.Amount=52;R.BankRevision=MarketView.BankRevision;R.Price=0;R.Revision=0;}
            else{R=MarketOrder(true,ELDItemKind::Fiber);R.Charter=true;R.Price=100;R.Amount=Task==67||Task==68?50:Task==64?101:Task==65?50:100;R.BankRevision=MarketView.BankRevision;R.Revision=0;if(Task==63){--R.WalletRevision;}}
            Send(R);if(Task==51||Task==66||Task==67){ServerB92Order(R);}ServicesProbeStep=2;return;
        }
        if(MarketReceiptSequence==ServicesProbeSequence&&((Task!=51&&Task!=66&&Task!=67)||ServicesProbeReceiptCounts.FindRef(ServicesProbeSequence)>=2)){Complete(MarketAccepted);}return;
    }
    if(Task>=42&&Task<=50)
    {
        if(Task==48)
        {
            if(!ServicesProbeStep){for(const auto& V:ObjectView){if(V.Kind==ELDObjectKind::LooseItem&&FVector::Dist2D(GetPawn()->GetActorLocation(),V.Position)<150){IssueObject(ELDObjectVerb::Pickup,V.Handle);ServicesProbeStep=1;ServicesProbeAt=Now;break;}}}
            if(OwnerState.Inventory.Items.ContainsByPredicate([](const auto& I){return I.Kind==ELDItemKind::SealedParcel;})){Complete(true);}return;
        }
        if(Task==49)
        {
            const auto* P=OwnerState.Inventory.Items.FindByPredicate([](const auto& I){return I.Kind==ELDItemKind::SealedParcel;});if(!P){Complete(ServicesProbeStep!=0);return;}if(!ServicesProbeStep){BagSelection=OwnerState.Inventory.Items.IndexOfByPredicate([&](const auto& I){return I.Id==P->Id;});ItemCommand(TEXT("Drop"),0);ConfirmSource(FGuid(),GetPawn()->GetActorLocation()-FVector(0,0,95)+FVector(70,0,0));ServicesProbeStep=1;}return;
        }
        if(!ServicesProbeStep){ServicesProbeSequence=++MarketSequence;ServerB92Open(OwnerState.Match,OwnerState.Life,ServicesProbeSequence,ServicesProbePOI);ServicesProbeStep=1;return;}
        if(ServicesProbeStep==1)
        {
            if(MarketReceiptSequence!=ServicesProbeSequence||!MarketView.Session.IsValid()){return;}auto R=CourierOrder(Task==42||Task==43?1:Task==50?3:2);
            if(Task==47){const auto* P=OwnerState.Inventory.Items.FindByPredicate([](const auto& I){return I.Kind==ELDItemKind::SealedParcel;});if(P){R.CourierToken=P->Id;R.Inputs={*P};R.Destination=ServicesProbePOI;}}
            ServicesProbeSaved=R;Send(R);if(Task==42||Task==46){ServerB92Order(R);}ServicesProbeStep=2;return;
        }
        if(Task==45&&ServicesProbeStep==2&&ChannelActive&&Channel.Domain==6){StopSourceOrder();ServicesProbeStep=3;}
        if(Task==46&&ServicesProbeStep==2&&ChannelActive&&Channel.Domain==6){CourierPage=true;if(FParse::Param(FCommandLine::Get(),TEXT("B92Capture"))){FString Output;FParse::Value(FCommandLine::Get(),TEXT("B92Output="),Output);FScreenshotRequest::RequestScreenshot(Output/TEXT("Courier.png"),true,false);}ServicesProbeStep=3;}
        if(Task==45||Task==46){if(Now-ServicesProbeAt>4.8&&!OwnerState.DevelopmentKind&&MarketReceiptSequence==ServicesProbeSequence){Complete(MarketAccepted);}return;}
        if(MarketReceiptSequence==ServicesProbeSequence&&(Task!=42||ServicesProbeReceiptCounts.FindRef(ServicesProbeSequence)>=2)){Complete(MarketAccepted);}return;
    }
    if(Task>=33&&Task<=41)
    {
        if(!ServicesProbeStep){ServicesProbeSequence=++MarketSequence;ServerB92Open(OwnerState.Match,OwnerState.Life,ServicesProbeSequence,ServicesProbePOI);ServicesProbeStep=1;return;}
        if(ServicesProbeStep==1)
        {
            if(MarketReceiptSequence!=ServicesProbeSequence||!MarketView.Session.IsValid()){return;}
            const uint8 Recipe=Task==34?2:Task==35||Task==40?6:Task==36||Task==41?7:1;auto R=WorkOrder(Recipe);
            if(Task==38){--R.InventoryRevision;}if(Task==39){++R.Price;}
            if(Task==41){R.Inputs=ServicesProbeSaved.Inputs;}
            if(Task==33||Task==36){ServicesProbeSaved=R;}Send(R);if(Task==33){ServerB92Order(R);}ServicesProbeStep=2;return;
        }
        if(Task==37&&ServicesProbeStep==2&&ChannelActive&&Channel.Domain==6){StopSourceOrder();ServicesProbeStep=3;}
        if(Task==34&&ServicesProbeStep==2&&ChannelActive&&Channel.Domain==6){if(FParse::Param(FCommandLine::Get(),TEXT("B92Capture"))){FString Output;FParse::Value(FCommandLine::Get(),TEXT("B92Output="),Output);FScreenshotRequest::RequestScreenshot(Output/TEXT("Work.png"),true,false);}ServicesProbeStep=3;}
        if(Task>=38){if(MarketReceiptSequence==ServicesProbeSequence){Complete(MarketAccepted);}return;}
        const double Wait=Task==34?7:Task==33||Task==37?5.2:4;
        if(Now-ServicesProbeAt>Wait&&!OwnerState.DevelopmentKind&&MarketReceiptSequence==ServicesProbeSequence)
        {if(Task==33&&ServicesProbeStep==2){ServerB92Order(ServicesProbeSaved);ServicesProbeStep=4;ServicesProbeAt=Now-5;return;}Complete(MarketAccepted);}return;
    }
    if(Task>=27&&Task<=32)
    {
        if(!ServicesProbeStep){ArmSkill(0);ConfirmArmed(ALDB92Deposits::Position(GetWorld(),Task==32?1:0),FGuid());ServicesProbeStep=1;ServicesProbeAt=Now;return;}
        if(Task==28&&ServicesProbeStep==1&&ChannelActive&&Channel.Domain==5){if(FParse::Param(FCommandLine::Get(),TEXT("B92Capture"))){FString Output;FParse::Value(FCommandLine::Get(),TEXT("B92Output="),Output);FScreenshotRequest::RequestScreenshot(Output/TEXT("Survey.png"),true,false);}ServicesProbeStep=2;}
        if(Task==31&&ServicesProbeStep==1&&ChannelActive&&Channel.Domain==5){StopSourceOrder();ServicesProbeStep=2;}
        const bool Received=OwnerState.Inventory.Items.ContainsByPredicate([&](const auto& I){return !ServicesProbeBefore.Contains(I.Id);});
        if(Task==28||Task==30){if(Received||Now-ServicesProbeAt>4.5){Complete(Received);}}
        else if(Now-ServicesProbeAt>1.8){Complete(Received);}return;
    }
    if(Task==25)
    {
        if(OwnerState.Inventory.Items.ContainsByPredicate([&](const auto& I){return !ServicesProbeBefore.Contains(I.Id);})){StopSourceOrder();Complete(true);}return;
    }
    if(Task==26)
    {
        if(ChannelActive&&Channel.Domain==4&&!ServicesProbeStep){if(FParse::Param(FCommandLine::Get(),TEXT("B92Capture"))){FString Output;FParse::Value(FCommandLine::Get(),TEXT("B92Output="),Output);FScreenshotRequest::RequestScreenshot(Output/TEXT("Deposit.png"),true,false);}ServicesProbeStep=1;ServicesProbeAt=Now;}
        if(ServicesProbeStep&&Now-ServicesProbeAt>.7){StopSourceOrder();Complete(true);}return;
    }
    if(Task>=19&&Task<=23)
    {
        if(!ServicesProbeStep){ServicesProbeSequence=++MarketSequence;ServerB92Open(OwnerState.Match,OwnerState.Life,ServicesProbeSequence,ServicesProbePOI);ServicesProbeStep=1;return;}
        if(ServicesProbeStep==1){if(MarketReceiptSequence!=ServicesProbeSequence||!MarketView.Session.IsValid()){return;}const auto* V=MarketView.Offers.FindByPredicate([&](const auto& X){return Task==22?X.Kind==ELDItemKind::Fiber:X.Kind==ELDItemKind::Book&&X.Stock>=0;});FLDB92MarketOrder R;
            if(Task==23){R=MarketOrder(true,ELDItemKind::Book);R.Skill=ELDSkill::None;for(const auto& X:OwnerState.Inventory.Items){if(X.Kind==ELDItemKind::Book){R.Skill=X.Skill;break;}}R.Price=90;}
            else{if(!V){Complete(false);return;}R=MarketOrder(true,V->Kind);R.Skill=V->Skill;R.Price=V->Ask;}R.Supply=true;Send(R);if(Task==19){ServerB92Order(R);}ServicesProbeStep=2;return;}
        if(MarketReceiptSequence==ServicesProbeSequence&&(Task!=19||ServicesProbeReceiptCounts.FindRef(ServicesProbeSequence)>=2)){Complete(MarketAccepted);}return;
    }
    if(Task==24)
    {if(!ServicesProbeStep){if(FParse::Param(FCommandLine::Get(),TEXT("B92Capture"))){FString Output;FParse::Value(FCommandLine::Get(),TEXT("B92Output="),Output);FScreenshotRequest::RequestScreenshot(Output/TEXT("Supply.png"),true,false);}ServicesProbeStep=1;ServicesProbeAt=Now;}else if(Now-ServicesProbeAt>1){Complete(true);}return;}
    if(Task>=15&&Task<=18)
    {
        if(!ServicesProbeStep)
        {auto R=MarketOrder(true,ELDItemKind::Fiber);R.BankAction=Task==16?2:1;R.Amount=Task==15?3:Task==18?1:2;R.BankRevision=MarketView.BankRevision;R.Price=0;R.Revision=0;if(Task==17){--R.BankRevision;}Send(R);if(Task==15||Task==16){ServerB92Order(R);}ServicesProbeStep=1;}
        else if(MarketReceiptSequence==ServicesProbeSequence&&((Task!=15&&Task!=16)||ServicesProbeReceiptCounts.FindRef(ServicesProbeSequence)>=2)){Complete(MarketAccepted);}return;
    }
    if(Task==1)
    {
        if(OwnerState.Inventory.Items.ContainsByPredicate([](const auto& I){return I.Kind==ELDItemKind::Ore;})){Complete(true);return;}
        if(!ServicesProbeStep)
        {for(const auto& O:ObjectView){if(O.Kind==ELDObjectKind::Chest&&FVector::Dist2D(GetPawn()->GetActorLocation(),O.Position)<180){IssueObject(ELDObjectVerb::Open,O.Handle);ServicesProbeStep=1;break;}}}
        else if(ServicesProbeStep==1)
        {for(const auto& I:ContainerView){if(I.Kind==ELDItemKind::Ore){IssueObject(ELDObjectVerb::Take,ContainerHandle,I.Item);ServicesProbeStep=2;break;}}}return;
    }
    if(Task==8)
    {if(!ServicesProbeStep){Complete(true);ServicesProbeTask=8;ServicesProbeStep=1;ServicesProbeAt=Now;}else if(Now-ServicesProbeAt>.8){FPlatformMisc::RequestExit(false);}return;}
    if(Task==9)
    {
        if(!ServicesProbeStep){for(const auto& O:ObjectView){if(O.Kind==ELDObjectKind::Sapling&&FVector::Dist2D(GetPawn()->GetActorLocation(),O.Position)<150){ServicesProbeSequence=NextCombatToken();ServerCombatAction(OwnerState.Match,OwnerState.Life,ServicesProbeSequence,0,(FVector(O.Position)-GetPawn()->GetActorLocation()).GetSafeNormal2D());ServicesProbeStep=1;ServicesProbeAt=Now;break;}}}
        else if(Now-ServicesProbeAt>1.5){Complete(true);}return;
    }
    if(Task==12||Task==13)
    {
        const auto Kind=Task==12?ELDItemKind::ForestryKnife:ELDItemKind::WaterFlask;const auto* I=OwnerState.Inventory.Items.FindByPredicate([&](const auto& V){return V.Kind==Kind;});
        if(!I){if(ServicesProbeStep>=2){Complete(true);}return;}
        if(!ServicesProbeStep){StartLoadoutHold(LDInventory::Equip,I->Id,Task==12?7:8);ServicesProbeStep=1;return;}
        if(ServicesProbeStep==1&&OwnerState.Inventory.Bindings[Task==12?7:8]==I->Id)
        {
            if(Task==12){for(const auto& O:ObjectView){if(O.Kind==ELDObjectKind::Sapling&&FVector::Dist2D(GetPawn()->GetActorLocation(),O.Position)<150){UseAccessory(0);ConfirmSource(O.Handle,O.Position);ServicesProbeStep=2;break;}}}
            else{SendItemAction(I->Id,FGuid(),GetPawn()->GetActorLocation()-FVector(0,0,90));ServicesProbeStep=2;}return;
        }
        if(Task==12&&ServicesProbeStep==2&&OwnerState.Inventory.Items.ContainsByPredicate([&](const auto& V){return V.Kind==ELDItemKind::Fiber&&!ServicesProbeBefore.Contains(V.Id);})){Complete(true);}return;
    }
    if(Task==14)
    {if(!ServicesProbeStep){if(FParse::Param(FCommandLine::Get(),TEXT("B92Capture"))){FString Output;FParse::Value(FCommandLine::Get(),TEXT("B92Output="),Output);FScreenshotRequest::RequestScreenshot(Output/TEXT("Market.png"),true,false);}ServicesProbeStep=1;ServicesProbeAt=Now;}else if(Now-ServicesProbeAt>1){Complete(true);}return;}
    if(Task==6)
    {if(!ServicesProbeStep){Send(ServicesProbeSaved);ServicesProbeStep=1;}else if(MarketReceiptSequence==ServicesProbeSequence){Complete(MarketAccepted);}return;}
    if(Task==4)
    {if(!ServicesProbeStep){const auto* I=OwnerState.Inventory.Find(ServicesProbeBought);if(!I){Complete(false);return;}Send(MarketOrder(false,I->Kind,I->Id));ServicesProbeStep=1;}else if(MarketReceiptSequence==ServicesProbeSequence){Complete(MarketAccepted);}return;}
    if(Task==3||Task==10||Task==11)
    {
        if(!ServicesProbeStep)
        {
            if(!MarketView.Session.IsValid()||MarketView.Offers.IsEmpty()){return;}const auto* Cheapest=&MarketView.Offers[0];for(const auto& V:MarketView.Offers){if(V.Ask<Cheapest->Ask){Cheapest=&V;}}
            auto R=MarketOrder(true,Cheapest->Kind);if(Task==10){++R.Price;}if(Task==11){--R.InventoryRevision;}Send(R);if(Task==3){ServerB92Order(R);}ServicesProbeStep=1;
        }
        else if(MarketReceiptSequence==ServicesProbeSequence&&(Task!=3||ServicesProbeReceiptCounts.FindRef(ServicesProbeSequence)>=2))
        {for(const auto& I:OwnerState.Inventory.Items){if(!ServicesProbeBefore.Contains(I.Id)){ServicesProbeBought=I.Id;}}Complete(MarketAccepted);}return;
    }
    if(Task==2||Task==5||Task==7)
    {
        if(!ServicesProbeStep){ServicesProbeSequence=++MarketSequence;ServerB92Open(OwnerState.Match,OwnerState.Life,ServicesProbeSequence,ServicesProbePOI);ServicesProbeStep=1;ServicesProbeAt=Now;return;}
        if(ServicesProbeStep==1)
        {
            if(MarketReceiptSequence!=ServicesProbeSequence){return;}
            if(Task==7){Complete(MarketAccepted);return;}if(!MarketView.Session.IsValid()){return;}
            if(Task==5){const auto* Cheapest=&MarketView.Offers[0];for(const auto& V:MarketView.Offers){if(V.Ask<Cheapest->Ask){Cheapest=&V;}}ServicesProbeSaved=MarketOrder(true,Cheapest->Kind);Complete(true);return;}
            ServicesProbeStep=2;
        }
        if(ServicesProbeStep==2&&Now-ServicesProbeAt>.3)
        {const auto* Ore=OwnerState.Inventory.Items.FindByPredicate([](const auto& I){return I.Kind==ELDItemKind::Ore;});if(!Ore){Complete(true);return;}Send(MarketOrder(false,Ore->Kind,Ore->Id));ServicesProbeStep=3;}
        else if(ServicesProbeStep==3&&MarketReceiptSequence==ServicesProbeSequence){if(!MarketAccepted){Complete(false);return;}ServicesProbeStep=2;ServicesProbeAt=Now;}return;
    }
#endif
}
