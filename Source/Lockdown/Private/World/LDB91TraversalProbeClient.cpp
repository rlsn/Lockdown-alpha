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

void ALDB91Controller::TickB91TraversalProbe()
{
#if !UE_BUILD_SHIPPING
    if(!ProbeTask){return;}const double Now=GetWorld()->GetTimeSeconds();
    const FVector Motion=GetPawn()->GetActorLocation();ProbeMotion+=FString::Printf(TEXT("%d,%.6f,%.3f,%.3f,%.3f\n"),ProbeTask,Now,Motion.X,Motion.Y,Motion.Z);
    const auto* Anchor=ObjectView.FindByPredicate([&](const auto& V){return V.Kind==ELDObjectKind::Anchor&&!(V.State&4)&&FVector::Dist2D(V.Position,GetPawn()->GetActorLocation())<=120;});
    const auto* Ladder=ObjectView.FindByPredicate([&](const auto& V){return V.Kind==ELDObjectKind::Ladder&&!(V.State&5)&&FVector::Dist2D(V.Position,GetPawn()->GetActorLocation())<=150;});
    const auto* Rope=OwnerState.Inventory.Items.FindByPredicate([](const auto& I){return I.Kind==ELDItemKind::Rope;});
    const bool Upper=Anchor&&FVector(Anchor->Position).Z>FVector(Anchor->Endpoint).Z;
    auto Issue=[&](ELDObjectVerb Verb)
    {
        auto& R=ObjectProbeRequest;R=FLDObjectRequest();R.Match=OwnerState.Match;R.Life=OwnerState.Life;R.Sequence=++WorldSequence;R.Contract=LD_B91_LINK_WORLD_SHA;R.Verb=Verb;R.InventoryRevision=OwnerState.Inventory.Revision;
        const auto* O=Verb==ELDObjectVerb::Carry?Ladder:Anchor;
        if(O){R.Observed=O->Handle;R.ObjectRevision=O->Revision;}
        if(Verb==ELDObjectVerb::Deploy&&Anchor)
        {const auto* CargoView=ObjectView.FindByPredicate([&](const auto& V){return V.Handle==CargoHandle;});if(!CargoView){return;}R.Observed=CargoView->Handle;R.ObjectRevision=CargoView->Revision;R.Anchor=Anchor->Handle;R.AnchorRevision=Anchor->Revision;}
        if(Verb==ELDObjectVerb::Attach&&Rope){R.Item=Rope->Id;R.ItemRevision=Rope->Revision;R.Accessory=0;}
        ServerObjectInteraction(R);ProbeSent=true;ProbeHoldAt=Now;
    };
    bool Act=false;ELDObjectVerb Verb=ELDObjectVerb::Use;
    switch(ProbeTask)
    {case 1:Act=Ladder!=nullptr;Verb=ELDObjectVerb::Carry;break;
     case 2:Act=TraversalProbeDeployer&&Anchor;Verb=ELDObjectVerb::Deploy;break;
     case 3:Act=Anchor!=nullptr;break;
     case 4:Act=Upper;break;
     case 5:Act=EnvironmentProbeRole==1&&Anchor;break;
     case 6:Act=Anchor!=nullptr;Verb=ELDObjectVerb::Retrieve;break;
     case 7:Act=EnvironmentProbeRole==1&&Anchor&&Rope;Verb=ELDObjectVerb::Attach;break;
     case 8:Act=EnvironmentProbeRole==1&&Anchor;break;default:return;}
    if(!ProbeSent&&Act){Issue(Verb);}
    if(ProbeSent&&Now-ProbeRetryAt>=.1){ProbeRetryAt=Now;ServerObjectHold(ObjectProbeRequest.Match,ObjectProbeRequest.Life,ObjectProbeRequest.Sequence,++Nonce);}
    const bool Received=ProbeSent&&LastObjectReceipt.Sequence==ObjectProbeRequest.Sequence&&LastObjectReceipt.Result!=0;
    bool Done=Received&&Now-ProbeHoldAt>.35;
    if(Received&&ProbeTask==1){TraversalProbeDeployer=LastObjectReceipt.Result==1;}
    if(ProbeTask==7&&EnvironmentProbeRole==1&&Received)
    {if(!ProbeReceipt.IsValid()){ProbeReceipt=LastObjectReceipt.Receipt;ProbeDuplicateSerial=ObjectReceiptSerial;ServerObjectInteraction(ObjectProbeRequest);ServerObjectInteraction(ObjectProbeRequest);}
     Done=ObjectReceiptSerial>=ProbeDuplicateSerial+2&&ProbeReceipt==LastObjectReceipt.Receipt;}
    if(!Act&&!ProbeSent){Done=Now-ProbeAt>3;}
    if(!Done){return;}
    auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("fixture"),TEXT("traversal"));R->SetNumberField(TEXT("phase"),ProbeTask);R->SetBoolField(TEXT("completed"),true);R->SetBoolField(TEXT("acted"),ProbeSent);R->SetNumberField(TEXT("receipt_result"),Received?LastObjectReceipt.Result:0);R->SetStringField(TEXT("reason"),Received?LastObjectReceipt.Reason:TEXT("observer"));R->SetBoolField(TEXT("active"),bTraversalActive);R->SetBoolField(TEXT("carrying"),bObjectCargo);R->SetBoolField(TEXT("duplicate_same"),ProbeTask!=7||EnvironmentProbeRole!=1||ProbeReceipt==LastObjectReceipt.Receipt);R->SetStringField(TEXT("position"),GetPawn()->GetActorLocation().ToString());
    TArray<TSharedPtr<FJsonValue>> Fields;for(TFieldIterator<FProperty> I(FLDObjectView::StaticStruct());I;++I){Fields.Add(MakeShared<FJsonValueString>(I->GetName()));}R->SetArrayField(TEXT("wire_fields"),Fields);
    FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));ServerB91ProbeAck(ProbeTask,Json);FString Output;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Output);const FString Label=EnvironmentProbeRole==1?TEXT("A"):TEXT("B");FFileHelper::SaveStringToFile(Json,*(Output/(Label+FString::Printf(TEXT("_%d.json"),ProbeTask))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);FFileHelper::SaveStringToFile(ProbeMotion,*(Output/(Label+TEXT("_Motion.csv"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);ProbeTask=0;
#endif
}
