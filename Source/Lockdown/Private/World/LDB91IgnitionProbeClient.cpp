#include "World/LDEnvironmentDirector.h"
#include "World/LDB91TraversalContract.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Core/LDGroundQueries.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "Engine/World.h"

void ALDB91Controller::TickB91IgnitionProbe()
{
#if !UE_BUILD_SHIPPING
    if(!ProbeTask){return;}const double Now=GetWorld()->GetTimeSeconds();const auto* Torch=OwnerState.Inventory.Items.FindByPredicate([](const auto& I){return I.Kind==ELDItemKind::Torch;});const auto* Oil=OwnerState.Inventory.Items.FindByPredicate([](const auto& I){return I.Kind==ELDItemKind::OilFlask;});
    const auto* Tree=ObjectView.FindByPredicate([&](const auto& V){return V.Kind==ELDObjectKind::Sapling&&FVector::Dist2D(V.Position,GetPawn()->GetActorLocation())<125;});
    auto Bind=[&](const FLDInventoryItem* I,uint8 Slot){if(!I){return;}FLDLoadoutRequest R;R.Version=2;R.Match=OwnerState.Match;R.Life=OwnerState.Life;R.Token=5000+ProbeTask*10+ProbeStep;R.InventoryRevision=OwnerState.Inventory.Revision;R.BuildRevision=OwnerState.Build.Revision;R.ItemRevision=I->Revision;R.Item=I->Id;R.Command=LDInventory::Equip;R.Binding=Slot;R.EquipmentDigest=ULDEquipmentCatalog::Load(8)->Checksum;ToolProbeDevToken=R.Token;ToolProbeDevNonce=0;ToolProbeDevAt=Now;ServerLoadoutRequest(R);};
    auto Issue=[&](bool Harvest=false){auto& R=ObjectProbeRequest;R=FLDObjectRequest();R.Match=OwnerState.Match;R.Life=OwnerState.Life;R.Sequence=++WorldSequence;R.Contract=ALDWorldObjects::ActiveContract();R.Verb=Harvest?ELDObjectVerb::Harvest:ELDObjectVerb::Ignite;R.InventoryRevision=OwnerState.Inventory.Revision;R.Accessory=0;if(Torch){R.Item=Torch->Id;R.ItemRevision=Torch->Revision;}if(Tree&&ProbeTask!=7){R.Observed=Tree->Handle;R.ObjectRevision=Tree->Revision;}else{FVector G;const FVector P=GetPawn()->GetActorLocation()+FVector(EnvironmentProbeRole==1?100:-100,0,-92.15);R.Point=LDGround::MapSurface(GetWorld(),P,G)?G:P;}ServerObjectInteraction(R);ProbeSent=true;ProbeHoldAt=ProbeRetryAt=Now;};
    bool Done=false;const bool Act=ProbeTask==2||ProbeTask==5||ProbeTask==6||ProbeTask==7||EnvironmentProbeRole==1;
    if(!Act){Done=Now-ProbeAt>.6;}
    else if(ProbeTask==2)
    {if(!ProbeStep&&Torch){Bind(Torch,7);ProbeStep=1;}Done=Torch&&Torch->BindingMask==(1<<7)&&!OwnerState.DevelopmentKind;}
    else if(ProbeTask==7)
    {
        if(!ProbeStep&&Torch){if(Torch->BindingMask!=(1<<7)){Bind(Torch,7);}ProbeStep=1;}
        if(ProbeStep==1&&Torch&&Torch->BindingMask==(1<<7)&&!OwnerState.DevelopmentKind){ToolProbeDevToken=0;if(EnvironmentProbeRole==1){Bind(Oil,8);ProbeStep=2;}else{ProbeStep=3;}}
        if(EnvironmentProbeRole==1&&ProbeStep==2&&Oil&&Oil->BindingMask==(1<<8)&&!OwnerState.DevelopmentKind){ToolProbeDevToken=0;SendItemAction(Oil->Id,FGuid(),GetPawn()->GetActorLocation()+FVector(100,0,-92.15));ProbeStep=3;}
        if(EnvironmentProbeRole==1){Done=ProbeStep==3&&!Oil;}
        else if(ProbeStep==3&&!ProbeSent&&SurfaceView.ContainsByPredicate([](const auto& V){return V.States&64;})){Issue();}
    }
    else if(!ProbeSent&&Now-ProbeAt>.3){Issue(ProbeTask==6);}
    bool Result=ProbeSent&&LastObjectReceipt.Sequence==ObjectProbeRequest.Sequence&&LastObjectReceipt.Result;
    if(ProbeTask==8&&EnvironmentProbeRole==1&&Result&&!ProbeStep){ToolProbeIdentity=Torch?Torch->Id:FGuid();ProbeStep=1;ProbeRetryAt=Now;}
    if(ProbeTask==8&&EnvironmentProbeRole==1&&ProbeStep==1&&Now-ProbeRetryAt>11){ProbeSent=false;Issue();ProbeStep=2;Result=false;}
    if(ProbeSent&&!Result&&Now-ProbeHoldAt>.08){ProbeHoldAt=Now;if(ProbeTask==4&&Now-ProbeAt>.6){ServerObjectRelease(ObjectProbeRequest.Match,ObjectProbeRequest.Life,ObjectProbeRequest.Sequence);}else{ServerObjectHold(ObjectProbeRequest.Match,ObjectProbeRequest.Life,ObjectProbeRequest.Sequence,++Nonce);}}
    if(Result&&ProbeTask==5){IgnitionProbeWinner=LastObjectReceipt.Result==1;}
    if(Result&&Act&&ProbeTask!=2&&!(ProbeTask==7&&EnvironmentProbeRole==1))
    {Done=ProbeTask==8?ProbeStep==2:ProbeTask==7?Now-ProbeAt>1.6:true;}
    if(Done&&Act&&(ProbeTask==4||ProbeTask==5||ProbeTask==8)&&Result)
    {if(!ProbeReceipt.IsValid()){ProbeReceipt=LastObjectReceipt.Receipt;ProbeDuplicateSerial=ObjectReceiptSerial;ServerObjectInteraction(ObjectProbeRequest);ServerObjectInteraction(ObjectProbeRequest);}Done=ObjectReceiptSerial>=ProbeDuplicateSerial+2&&LastObjectReceipt.Receipt==ProbeReceipt;}
    if(ToolProbeDevToken&&Now-ToolProbeDevAt>.08){ToolProbeDevAt=Now;ServerDevelopmentHold(OwnerState.Match,OwnerState.Life,ToolProbeDevToken,++ToolProbeDevNonce,false);}
    if(!Done&&Now-ProbeAt<40){return;}auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("fixture"),TEXT("ignition"));R->SetNumberField(TEXT("phase"),ProbeTask);R->SetBoolField(TEXT("completed"),Done);R->SetNumberField(TEXT("result"),Result?LastObjectReceipt.Result:0);R->SetStringField(TEXT("reason"),Result?LastObjectReceipt.Reason:IntentFeedback);R->SetNumberField(TEXT("fuel_ms"),Torch?Torch->FuelMs:0);R->SetBoolField(TEXT("reserved"),Torch&&Torch->bReserved);R->SetBoolField(TEXT("duplicate_same"),!ProbeReceipt.IsValid()||LastObjectReceipt.Receipt==ProbeReceipt);R->SetBoolField(TEXT("same_retry_item"),ProbeTask!=8||EnvironmentProbeRole!=1||Torch&&Torch->Id==ToolProbeIdentity);R->SetNumberField(TEXT("surface_count"),SurfaceView.Num());uint8 States=0;for(const auto& V:SurfaceView){States|=V.States;}R->SetNumberField(TEXT("states"),States);
    TArray<TSharedPtr<FJsonValue>> Fields;for(TFieldIterator<FProperty> I(FLDObjectView::StaticStruct());I;++I){Fields.Add(MakeShared<FJsonValueString>(I->GetName()));}R->SetArrayField(TEXT("object_fields"),Fields);Fields.Reset();for(TFieldIterator<FProperty> I(FLDEnvironmentSurface::StaticStruct());I;++I){Fields.Add(MakeShared<FJsonValueString>(I->GetName()));}R->SetArrayField(TEXT("surface_fields"),Fields);
    FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));ServerB91ProbeAck(ProbeTask,Json);FString Output;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Output);FFileHelper::SaveStringToFile(Json,*(Output/((EnvironmentProbeRole==1?TEXT("A"):TEXT("B"))+FString::Printf(TEXT("_%d.json"),ProbeTask))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);ProbeTask=0;ToolProbeDevToken=0;
#endif
}
