#include "World/LDEnvironmentDirector.h"
#include "World/LDB91ObjectsContract.h"
#include "Navigation/LDNavigationComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"

void ALDB91Controller::TickB91ObjectsProbe()
{
#if !UE_BUILD_SHIPPING
    if(!ProbeTask){return;}const double Now=GetWorld()->GetTimeSeconds();const FVector P=GetPawn()->GetActorLocation();
    ProbeMotion+=FString::Printf(TEXT("%d,%.6f,%.3f,%.3f,%.3f\n"),ProbeTask,Now,P.X,P.Y,P.Z);
    auto Find=[&](ELDObjectKind Kind)->const FLDObjectView*
    {const FLDObjectView* Best=nullptr;double Distance=125*125;for(const auto& V:ObjectView){const double D=FVector::DistSquared2D(V.Position,P);if(V.Kind==Kind&&D<Distance){Best=&V;Distance=D;}}return Best;};
    auto Issue=[&](ELDObjectVerb Verb,const FLDObjectView* V,FVector Point=FVector::ZeroVector)
    {
        ObjectProbeRequest=FLDObjectRequest();auto& R=ObjectProbeRequest;R.Match=OwnerState.Match;R.Life=OwnerState.Life;R.Sequence=++WorldSequence;R.Contract=LD_B91_OBJECTS_SHA;
        R.InventoryRevision=OwnerState.Inventory.Revision;R.Verb=Verb;R.Point=Point;if(V){R.Observed=V->Handle;R.ObjectRevision=V->Revision;R.ItemRevision=V->PhysicalRevision;}
        if(Verb==ELDObjectVerb::Take){R.Observed=ContainerHandle;R.ObjectRevision=ContainerRevision;for(const auto& I:ContainerView){if(I.Kind==ELDItemKind::Fiber){R.Item=I.Item;R.ItemRevision=I.Revision;break;}}}
        if(Verb==ELDObjectVerb::Store){R.Observed=ContainerHandle;R.ObjectRevision=ContainerRevision;for(const auto& I:OwnerState.Inventory.Items){if(I.Kind==ELDItemKind::Fiber){R.Item=I.Id;R.ItemRevision=I.Revision;break;}}}
        ServerObjectInteraction(R);ProbeSent=true;ProbeHoldAt=Now;
    };
    if(!ProbeSent&&ProbeTask!=8)
    {
        const auto* V=Find(ProbeTask==1?ELDObjectKind::Water:ProbeTask<=4?ELDObjectKind::Chest:ProbeTask==6?ELDObjectKind::Barrel:ELDObjectKind::Crate);
        if(!V){if(Now-ProbeAt<15){return;}}
        else
        {
            if(ProbeTask==1){ObjectProbeContainerBefore=ContainerView.Num();}
            if(ProbeTask==3&&ContainerView.IsEmpty()){return;}
            if(ProbeTask==4&&!OwnerState.Inventory.Items.ContainsByPredicate([](const auto& I){return I.Kind==ELDItemKind::Fiber;})){return;}
            if(ProbeTask==7){ObjectProbePad=FVector(V->Position)-FVector(0,0,113);}
            Issue(ProbeTask==1?ELDObjectVerb::Pickup:ProbeTask==2?ELDObjectVerb::Open:ProbeTask==3?ELDObjectVerb::Take:ProbeTask==4?ELDObjectVerb::Store:ProbeTask==7?ELDObjectVerb::Carry:ELDObjectVerb::Break,V);
        }
    }
    bool Done=false;
    if(ProbeTask==8)
    {
        const auto* V=ObjectView.FindByPredicate([](const auto& E){return E.Kind==ELDObjectKind::Crate&&(E.State&2);});
        if(ProbeStep==0&&V){ObjectProbeStart=P;ObjectProbeRejectedPlace=false;Issue(ELDObjectVerb::Place,V,P-FVector(0,0,92.15));ProbeStep=1;}
        else if(ProbeStep==1&&LastObjectReceipt.Sequence==ObjectProbeRequest.Sequence&&LastObjectReceipt.Result)
        {ObjectProbeRejectedPlace=LastObjectReceipt.Result==2&&HasObjectCargo();Navigation->Navigate(ObjectProbeStart+FVector(200,0,-92.15));ProbeStep=2;ProbeSent=false;}
        else if(ProbeStep==2&&Navigation->Status==TEXT("Arrived"))
        {Navigation->Navigate(ObjectProbeStart+(ObjectProbeStart-ObjectProbePad).GetSafeNormal2D()*80-FVector(0,0,92.15));ProbeStep=3;}
        else if(ProbeStep==3&&Navigation->Status==TEXT("Arrived")&&V)
        {Navigation->Stop();Issue(ELDObjectVerb::Place,V,ObjectProbePad);ProbeStep=4;}
        else if(ProbeStep==4&&LastObjectReceipt.Sequence==ObjectProbeRequest.Sequence&&LastObjectReceipt.Result)
        {Done=LastObjectReceipt.Result==1&&!HasObjectCargo()&&ObjectProbeRejectedPlace;}
    }
    else if(ProbeSent&&LastObjectReceipt.Sequence==ObjectProbeRequest.Sequence&&LastObjectReceipt.Result)
    {
        if(ProbeTask==1)
        {
            if(!ProbeReceipt.IsValid()){ProbeReceipt=LastObjectReceipt.Receipt;ProbeDuplicateSerial=ObjectReceiptSerial;ServerObjectInteraction(ObjectProbeRequest);ServerObjectInteraction(ObjectProbeRequest);return;}
            const auto* Empty=Find(ELDObjectKind::Water);
            Done=ObjectReceiptSerial>=ProbeDuplicateSerial+2&&ProbeReceipt==LastObjectReceipt.Receipt&&Empty&&(Empty->State&4)&&!Empty->PhysicalRevision;
        }
        else if(ProbeTask==5||ProbeTask==6)
        {const auto* V=Find(ProbeTask==5?ELDObjectKind::Crate:ELDObjectKind::Barrel);const auto* Weak=V?ObjectProxies.Find(V->Handle):nullptr;const auto* Proxy=Weak?Weak->Get():nullptr;Done=V&&(V->State&1)&&Proxy&&Proxy->Proxy->GetCollisionEnabled()==ECollisionEnabled::NoCollision;}
        else{Done=ProbeTask!=2||ContainerHandle.IsValid();}
    }
    if(ProbeSent&&LastObjectReceipt.Sequence==ObjectProbeRequest.Sequence&&!LastObjectReceipt.Result&&Now-ProbeHoldAt>.08)
    {ProbeHoldAt=Now;ServerObjectHold(ObjectProbeRequest.Match,ObjectProbeRequest.Life,ObjectProbeRequest.Sequence,++Nonce);}
    if(!Done&&Now-ProbeAt<20){return;}
    auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("fixture"),TEXT("objects"));R->SetNumberField(TEXT("phase"),ProbeTask);R->SetBoolField(TEXT("completed"),Done);
    R->SetNumberField(TEXT("receipt_result"),LastObjectReceipt.Result);R->SetStringField(TEXT("receipt"),LastObjectReceipt.Receipt.ToString());R->SetStringField(TEXT("reason"),LastObjectReceipt.Reason);
    R->SetStringField(TEXT("target_handle"),ObjectProbeRequest.Observed.ToString());
    R->SetBoolField(TEXT("duplicate_same"),ProbeTask!=1||ProbeReceipt==LastObjectReceipt.Receipt);R->SetNumberField(TEXT("container_count_before"),ObjectProbeContainerBefore);
    R->SetBoolField(TEXT("private_revision_absent"),ProbeTask!=1||Done);
    R->SetNumberField(TEXT("container_count"),ContainerView.Num());R->SetBoolField(TEXT("carrying"),HasObjectCargo());R->SetBoolField(TEXT("unsafe_place_retained_cargo"),ObjectProbeRejectedPlace);
    R->SetBoolField(TEXT("broken_proxy_clear"),(ProbeTask!=5&&ProbeTask!=6)||Done);
    TArray<TSharedPtr<FJsonValue>> Fields,Entries;for(TFieldIterator<FProperty> I(FLDObjectView::StaticStruct());I;++I){Fields.Add(MakeShared<FJsonValueString>(I->GetName()));}
    for(const auto& V:ObjectView){auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("handle"),V.Handle.ToString());J->SetNumberField(TEXT("kind"),uint8(V.Kind));J->SetStringField(TEXT("position"),FVector(V.Position).ToString());J->SetNumberField(TEXT("revision"),V.Revision);J->SetNumberField(TEXT("state"),V.State);J->SetNumberField(TEXT("physical_revision"),V.PhysicalRevision);Entries.Add(MakeShared<FJsonValueObject>(J));}
    R->SetArrayField(TEXT("wire_fields"),Fields);R->SetArrayField(TEXT("received_entries"),Entries);
    FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));ServerB91ProbeAck(ProbeTask,Json);
    FString Output;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Output);const FString Label=EnvironmentProbeRole==1?TEXT("A"):TEXT("B");
    FFileHelper::SaveStringToFile(Json,*(Output/(Label+FString::Printf(TEXT("_%d.json"),ProbeTask))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    FFileHelper::SaveStringToFile(ProbeMotion,*(Output/(Label+TEXT("_Motion.csv"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);ProbeTask=0;
#endif
}
