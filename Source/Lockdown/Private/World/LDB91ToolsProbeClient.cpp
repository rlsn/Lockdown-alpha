#include "World/LDEnvironmentDirector.h"
#include "World/LDB91TraversalContract.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Navigation/LDNavigationComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "Engine/World.h"

void ALDB91Controller::TickB91ToolsProbe()
{
#if !UE_BUILD_SHIPPING
    if(!ProbeTask){return;}const double Now=GetWorld()->GetTimeSeconds();
    const auto* Item=OwnerState.Inventory.Items.FindByPredicate([](const auto& I){return I.Kind==ELDItemKind::Torch;});
    const auto* Ground=ObjectView.FindByPredicate([&](const auto& V){return V.Kind==ELDObjectKind::Torch&&!(V.State&4)&&FVector::Dist2D(V.Position,GetPawn()->GetActorLocation())<=120;});
    int Peers=0,PeerLight=0;for(const auto& E:View){if(E.PublicDoor.IsNone()){++Peers;PeerLight=FMath::Max(PeerLight,int(E.Light));}}
    const bool GroundLit=ObjectView.ContainsByPredicate([](const auto& E){return E.Kind==ELDObjectKind::Torch&&(E.State&8)&&!(E.State&4);});
    auto Issue=[&](ELDObjectVerb Verb)
    {
        auto& R=ObjectProbeRequest;R=FLDObjectRequest();R.Match=OwnerState.Match;R.Life=OwnerState.Life;R.Sequence=++WorldSequence;R.Contract=ALDWorldObjects::ActiveContract();R.Verb=Verb;R.InventoryRevision=OwnerState.Inventory.Revision;R.Accessory=255;
        if(Ground){R.Observed=Ground->Handle;R.ObjectRevision=Ground->Revision;R.ItemRevision=Ground->PhysicalRevision;}
        if(Item){R.Item=Item->Id;R.ItemRevision=Item->Revision;}else{R.Item=ToolProbeIdentity;}
        for(int N=0;N<4;++N){if(OwnerState.Inventory.Bindings[7+N]==R.Item){R.Accessory=N;break;}}
        R.Point=GetPawn()->GetActorLocation()+FVector(0,50,-92.15);ServerObjectInteraction(R);ProbeSent=true;ProbeHoldAt=Now;
    };
    auto Bind=[&](bool Remove,uint8 Slot)
    {
        if(!Item){return;}FLDLoadoutRequest R;R.Version=2;R.Match=OwnerState.Match;R.Life=OwnerState.Life;R.Token=3000+ProbeTask;R.InventoryRevision=OwnerState.Inventory.Revision;R.BuildRevision=OwnerState.Build.Revision;R.ItemRevision=Item->Revision;R.Item=Item->Id;R.Command=Remove?LDInventory::Unequip:LDInventory::Equip;R.Binding=Slot;R.EquipmentDigest=ULDEquipmentCatalog::Load(8)->Checksum;
        ToolProbeDevToken=R.Token;ToolProbeDevNonce=0;ToolProbeDevAt=Now;ServerLoadoutRequest(R);
    };
    bool Done=false;
    if(EnvironmentProbeRole==2)
    {Done=Now-ProbeAt>.5&&(ProbeTask==3?PeerLight==1:ProbeTask==4?Peers==0:ProbeTask==5?GroundLit&&PeerLight==0:ProbeTask==7?PeerLight==1:ProbeTask==8?PeerLight==0:true);}
    else if(ProbeTask==1)
    {if(!ProbeSent&&Ground){Issue(ELDObjectVerb::Pickup);}if(Item){ToolProbeIdentity=Item->Id;Done=LastObjectReceipt.Sequence==ObjectProbeRequest.Sequence&&LastObjectReceipt.Result==1;}}
    else if(ProbeTask==2)
    {if(!ProbeStep&&Item){Bind(false,7);ProbeStep=1;}Done=Item&&Item->BindingMask==(1<<7)&&!OwnerState.DevelopmentKind;}
    else if(ProbeTask==3)
    {
        if(!ProbeSent&&Item){Issue(ELDObjectVerb::Toggle);}
        if(ProbeSent&&LastObjectReceipt.Sequence==ObjectProbeRequest.Sequence&&LastObjectReceipt.Result)
        {if(!ProbeReceipt.IsValid()){ProbeReceipt=LastObjectReceipt.Receipt;ProbeDuplicateSerial=ObjectReceiptSerial;ServerObjectInteraction(ObjectProbeRequest);ServerObjectInteraction(ObjectProbeRequest);}
         Done=ObjectReceiptSerial>=ProbeDuplicateSerial+2&&LastObjectReceipt.Receipt==ProbeReceipt&&Item&&Item->bLit;}
    }
    else if(ProbeTask==4)
    {
        if(!ProbeStep&&Item){Issue(ELDObjectVerb::Toggle);ProbeStep=1;}
        if(ProbeStep==1&&LastObjectReceipt.Sequence==ObjectProbeRequest.Sequence&&LastObjectReceipt.Result==2)
        {ProbeStep=2;ProbeSent=false;}
        if(ProbeStep==2&&Now-ProbeAt>2.2&&!(OwnerState.StatusMask&(1<<2))&&Item){Bind(true,7);ProbeStep=3;}
        Done=ProbeStep==3&&Item&&!Item->BindingMask&&!OwnerState.DevelopmentKind&&Item->bLit;
    }
    else if(ProbeTask==5)
    {
        if(!ProbeStep&&Item){Issue(ELDObjectVerb::Drop);ProbeStep=1;}
        if(ProbeStep==1&&!Item&&LastObjectReceipt.Sequence==ObjectProbeRequest.Sequence&&LastObjectReceipt.Result==1)
        {Issue(ELDObjectVerb::Toggle);ProbeStep=2;}
        Done=ProbeStep==2&&LastObjectReceipt.Sequence==ObjectProbeRequest.Sequence&&LastObjectReceipt.Result==2&&GroundLit;
    }
    else if(ProbeTask==6)
    {
        if(!ProbeStep&&Ground){Issue(ELDObjectVerb::Pickup);ProbeStep=1;}
        if(ProbeStep==1&&Item&&LastObjectReceipt.Sequence==ObjectProbeRequest.Sequence&&LastObjectReceipt.Result==1){Bind(false,8);ProbeStep=2;}
        Done=ProbeStep==2&&Item&&Item->Id==ToolProbeIdentity&&Item->BindingMask==(1<<8)&&!OwnerState.DevelopmentKind&&Item->bLit;
    }
    else if(ProbeTask==7)
    {if(!ProbeSent&&Ground){Issue(ELDObjectVerb::Pickup);}Done=Item&&Item->Id==ToolProbeIdentity&&Item->bLit&&LastObjectReceipt.Sequence==ObjectProbeRequest.Sequence&&LastObjectReceipt.Result==1;}
    else if(ProbeTask==8)
    {
        if(!ProbeStep&&Item){Bind(false,7);ProbeStep=1;}
        if(ProbeStep==1&&Item&&Item->BindingMask==(1<<7)&&!Item->bLit&&Item->FuelMs==0&&!OwnerState.DevelopmentKind){Issue(ELDObjectVerb::Toggle);ProbeStep=2;}
        Done=ProbeStep==2&&LastObjectReceipt.Sequence==ObjectProbeRequest.Sequence&&LastObjectReceipt.Result==2&&Item&&Item->FuelMs==0&&!Item->bLit;
    }
    if(ProbeSent&&LastObjectReceipt.Sequence==ObjectProbeRequest.Sequence&&!LastObjectReceipt.Result&&Now-ProbeHoldAt>.08)
    {ProbeHoldAt=Now;ServerObjectHold(ObjectProbeRequest.Match,ObjectProbeRequest.Life,ObjectProbeRequest.Sequence,++Nonce);}
    if(ToolProbeDevToken&&Now-ToolProbeDevAt>.08)
    {ToolProbeDevAt=Now;ServerDevelopmentHold(OwnerState.Match,OwnerState.Life,ToolProbeDevToken,++ToolProbeDevNonce,false);}
    if(!Done&&Now-ProbeAt<25){return;}
    auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("phase"),ProbeTask);R->SetBoolField(TEXT("completed"),Done);R->SetStringField(TEXT("reason"),LastObjectReceipt.Reason);R->SetNumberField(TEXT("receipt_result"),LastObjectReceipt.Result);R->SetBoolField(TEXT("duplicate_same"),ProbeTask!=3||EnvironmentProbeRole!=1||ProbeReceipt==LastObjectReceipt.Receipt);
    R->SetStringField(TEXT("owned_item"),Item?Item->Id.ToString():TEXT(""));R->SetNumberField(TEXT("fuel_ms"),Item?Item->FuelMs:0);R->SetBoolField(TEXT("lit"),Item&&Item->bLit);R->SetNumberField(TEXT("binding"),Item?Item->BindingMask:0);R->SetNumberField(TEXT("peer_body_count"),Peers);R->SetNumberField(TEXT("peer_light"),PeerLight);R->SetBoolField(TEXT("ground_lit"),GroundLit);
    TArray<TSharedPtr<FJsonValue>> ObjectFields,BodyFields;bool FuelField=false;
    for(TFieldIterator<FProperty> I(FLDObjectView::StaticStruct());I;++I){ObjectFields.Add(MakeShared<FJsonValueString>(I->GetName()));FuelField|=I->GetName().Contains(TEXT("Fuel"));}
    for(TFieldIterator<FProperty> I(FLDEnvironmentViewEntry::StaticStruct());I;++I){BodyFields.Add(MakeShared<FJsonValueString>(I->GetName()));FuelField|=I->GetName().Contains(TEXT("Fuel"));}
    R->SetArrayField(TEXT("object_fields"),ObjectFields);R->SetArrayField(TEXT("body_fields"),BodyFields);R->SetBoolField(TEXT("world_payload_contains_fuel"),FuelField);
    FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));ServerB91ProbeAck(ProbeTask,Json);FString Output;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Output);const FString Label=EnvironmentProbeRole==1?TEXT("A"):TEXT("B");FFileHelper::SaveStringToFile(Json,*(Output/(Label+FString::Printf(TEXT("_%d.json"),ProbeTask))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    ProbeTask=0;ToolProbeDevToken=0;
#endif
}
