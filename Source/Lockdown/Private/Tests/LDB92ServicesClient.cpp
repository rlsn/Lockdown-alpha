#include "Population/LDB92Roster.h"
#include "World/LDWorldObjects.h"
#include "Inventory/LDInventoryDirector.h"
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
        auto J=MakeShared<FJsonObject>();J->SetBoolField(TEXT("completed"),true);J->SetBoolField(TEXT("accepted"),Accepted);J->SetStringField(TEXT("gold"),LexToString(OwnerState.Gold));J->SetNumberField(TEXT("life"),OwnerState.Life);J->SetNumberField(TEXT("combat_floor"),CombatTokenFloor());J->SetBoolField(TEXT("duplicate_same"),ServicesProbeReplaySame);J->SetNumberField(TEXT("bag_count"),OwnerState.Inventory.Items.Num());J->SetBoolField(TEXT("has_quote"),MarketView.Session.IsValid());J->SetNumberField(TEXT("offers"),MarketView.Offers.Num());J->SetStringField(TEXT("quote_revision"),LexToString(MarketView.Revision));
        TArray<TSharedPtr<FJsonValue>> Items;for(const auto& I:OwnerState.Inventory.Items){auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("id"),I.Id.ToString());V->SetNumberField(TEXT("kind"),uint8(I.Kind));Items.Add(MakeShared<FJsonValueObject>(V));}J->SetArrayField(TEXT("items"),Items);
        FString Json,Output;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Json));FParse::Value(FCommandLine::Get(),TEXT("B92Output="),Output);FFileHelper::SaveStringToFile(Json,*(Output/FString::Printf(TEXT("%s_%d.json"),*ProbeLabel,Task)),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);ServerB92ProbeReport(Task,Json);ServicesProbeTask=0;
    };
    auto Send=[&](FLDB92MarketOrder R){ServicesProbeSequence=R.Sequence;ServicesProbeSerial=MarketReceiptSerial;ServerB92Order(R);ServicesProbeAt=Now;};
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
