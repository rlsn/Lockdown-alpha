#include "World/LDEnvironmentDirector.h"
#include "World/LDB91TraversalContract.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "Engine/World.h"
#include "UI/LDControlSettings.h"
#include "UnrealClient.h"
#include "GameFramework/GameStateBase.h"

void ALDB91Controller::TickB91SaplingsProbe()
{
#if !UE_BUILD_SHIPPING
    if(!ProbeTask){return;}const double Now=GetWorld()->GetTimeSeconds();const FLDObjectView* Source=nullptr;double Best=ProbeTask>=12?1000*1000:125*125;
    for(const auto& V:ObjectView){const double D=FVector::DistSquared2D(V.Position,GetPawn()->GetActorLocation());if(V.Kind==(ProbeTask==11?ELDObjectKind::Water:ProbeTask==14?ELDObjectKind::Chest:ProbeTask==15?ELDObjectKind::ForestryKnife:ELDObjectKind::Sapling)&&D<Best){Source=&V;Best=D;}}
    if(ProbeTask==10&&Now-ProbeAt<8.2){return;}
    if(Now-ProbeRetryAt>3){ProbeRetryAt=Now;UE_LOG(LogTemp,Display,TEXT("LD_CONTROL_WAIT phase=%d step=%d sent=%d source=%d bag=%d dev=%d command=%d feedback=%s"),ProbeTask,ProbeStep,ProbeSent,Source!=nullptr,OwnerState.Inventory.Items.Num(),OwnerState.DevelopmentKind,CanWorldCommand(),*IntentFeedback);}
    const bool Act=ProbeTask<=2||ProbeTask==7?ProbeTask!=7||SaplingProbeWinner:EnvironmentProbeRole==1;
    if(Act&&ProbeTask>=11&&ProbeTask<=15)
    {
        if(!ProbeSent)
        {
            if(ProbeTask==12||ProbeTask==13){const auto* Door=View.FindByPredicate([](const auto& V){return V.PublicDoor==TEXT("building_riverport_00_door_south");});if(!Door){return;}PointerOrder(true,FGuid(),Door->Handle,Door->Position);}
            else{if(!Source){return;}if(ProbeTask==11){BasicAttackArmed=true;ConfirmSource(Source->Handle,Source->Position);}else{PointerOrder(ProbeTask==15,Source->Handle,FGuid(),Source->Position);}}
            ProbeSent=true;ProbeHoldAt=Now;
        }
    }
    else if(Act&&ProbeTask==5)
    {if(Source&&!(Source->State&5)&&Now-ProbeRetryAt>.4){ProbeRetryAt=Now;ServerCombatAction(OwnerState.Match,OwnerState.Life,NextCombatToken(),0,(FVector(Source->Position)-GetPawn()->GetActorLocation()).GetSafeNormal2D());}}
    else if(Act&&!ProbeSent&&Source)
    {
        const auto* Knife=OwnerState.Inventory.Items.FindByPredicate([](const auto& I){return I.Kind==ELDItemKind::ForestryKnife;});if(!Knife){return;}
        const int Slot=OwnerState.Inventory.Bindings.Find(Knife->Id)-7;
        if(Slot<0||Slot>=4){if(!ProbeStep&&!Knife->bReserved){BagSelection=OwnerState.Inventory.Items.IndexOfByPredicate([&](const auto& I){return I.Id==Knife->Id;});ItemCommand(TEXT("Equip"),0);ProbeStep=1;}return;}
        ObjectProbeRequest=FLDObjectRequest();auto& R=ObjectProbeRequest;R.Match=OwnerState.Match;R.Life=OwnerState.Life;R.Contract=ALDWorldObjects::ActiveContract();R.Observed=Source->Handle;R.ObjectRevision=Source->Revision;R.InventoryRevision=OwnerState.Inventory.Revision;R.Item=Knife->Id;R.ItemRevision=Knife->Revision;R.Accessory=Slot;R.Verb=ProbeTask==2?ELDObjectVerb::Chop:ELDObjectVerb::Harvest;
        if(ProbeTask==10){R.Sequence=++WorldSequence;R.Item.Invalidate();ServerObjectInteraction(R);}
        else{UseAccessory(Slot,ProbeTask==2);ConfirmSource(Source->Handle,Source->Position);R.Sequence=WorldSequence;}
        ProbeSent=true;ProbeHoldAt=Now;
    }
    if(ProbeSent&&ChannelActive&&Channel.Sequence==ObjectProbeRequest.Sequence&&Now-ProbeHoldAt>.3&&ProbeStep!=2&&(ProbeTask==4||ProbeTask==16))
    {SentAt=Now+(ProbeTask==4?1.:3.);ProbeStep=2;}
    if(ChannelActive&&Channel.Sequence==ObjectProbeRequest.Sequence){ProbeRetries|=1;if((ProbeTask==1||ProbeTask==4)&&FParse::Param(FCommandLine::Get(),TEXT("LDB91NetCapture"))&&!ProbeFrame&&GetWorld()->GetGameState()->GetServerWorldTimeSeconds()-Channel.Started>1){FString Out;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Out);FScreenshotRequest::RequestScreenshot(Out/TEXT("SourceChannel.png"),true,false);++ProbeFrame;}}
    if(ProbeSent&&ProbeTask==3&&Now-ProbeHoldAt>.5&&ProbeStep!=2){StopSourceOrder();ProbeStep=2;}
    const bool Received=ProbeSent&&LastObjectReceipt.Sequence==ObjectProbeRequest.Sequence&&LastObjectReceipt.Result!=0;
    if(Received&&ProbeTask==1){SaplingProbeWinner=LastObjectReceipt.Result==1;}
    bool Done=Act?(ProbeTask==5?Source&&(Source->State&5):Received&&Now-ProbeHoldAt>.7):Now-ProbeAt>.7;
    if(Act&&ProbeTask>=11&&ProbeTask<=15)
    {if(ProbeTask==11){Done=Source&&(Source->State&5)&&Now-ProbeHoldAt>1;}else if(ProbeTask==12||ProbeTask==13){const auto* Door=View.FindByPredicate([](const auto& V){return V.PublicDoor==TEXT("building_riverport_00_door_south");});Done=Door&&bool(Door->State&1)==(ProbeTask==12)&&Now-ProbeHoldAt>1;}else if(ProbeTask==14){Done=ContainerHandle.IsValid()&&Now-ProbeHoldAt>1;}else{Done=OwnerState.Inventory.Items.ContainsByPredicate([](const auto& I){return I.Kind==ELDItemKind::ForestryKnife&&!I.BindingMask;})&&Now-ProbeHoldAt>1;}}
    if(Received&&(ProbeTask==1||ProbeTask==8))
    {if(!ProbeReceipt.IsValid()){ProbeReceipt=LastObjectReceipt.Receipt;ProbeDuplicateSerial=ObjectReceiptSerial;ServerObjectInteraction(ObjectProbeRequest);ServerObjectInteraction(ObjectProbeRequest);}Done=ObjectReceiptSerial>=ProbeDuplicateSerial+2&&LastObjectReceipt.Receipt==ProbeReceipt;}
    if(ProbeTask==16&&Received&&ProbeStep==2){ServerObjectHold(ObjectProbeRequest.Match,ObjectProbeRequest.Life,ObjectProbeRequest.Sequence,++Nonce);ProbeStep=3;ProbeRetryAt=Now;Done=false;}
    if(ProbeTask==16&&ProbeStep==3&&Now-ProbeRetryAt<.4){Done=false;}
    if(!Done){return;}auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("fixture"),TEXT("saplings"));R->SetNumberField(TEXT("phase"),ProbeTask);R->SetBoolField(TEXT("completed"),true);R->SetBoolField(TEXT("acted"),Act);R->SetNumberField(TEXT("result"),ProbeTask==14?LastObjectReceipt.Result:Received?LastObjectReceipt.Result:0);R->SetStringField(TEXT("reason"),Received?LastObjectReceipt.Reason:TEXT("observation"));R->SetBoolField(TEXT("duplicate_same"),!ProbeReceipt.IsValid()||ProbeReceipt==LastObjectReceipt.Receipt);R->SetBoolField(TEXT("cleared"),Source&&(Source->State&5));R->SetBoolField(TEXT("owner_progress"),(ProbeRetries&1)!=0);R->SetBoolField(TEXT("context_removed"),!Controls->Key(TEXT("Context")).IsValid());R->SetStringField(TEXT("input"),TEXT("One source confirmation; no physical key held; player command maintains liveness"));
    TArray<TSharedPtr<FJsonValue>> Fields;for(TFieldIterator<FProperty> I(FLDObjectView::StaticStruct());I;++I){Fields.Add(MakeShared<FJsonValueString>(I->GetName()));}R->SetArrayField(TEXT("wire_fields"),Fields);
    FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));ServerB91ProbeAck(ProbeTask,Json);FString Output;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Output);const FString ProbeLabel=EnvironmentProbeRole==1?TEXT("A"):TEXT("B");FFileHelper::SaveStringToFile(Json,*(Output/(ProbeLabel+FString::Printf(TEXT("_%d.json"),ProbeTask))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);ProbeTask=0;
#endif
}
