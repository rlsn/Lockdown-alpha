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

void ALDB91Controller::TickB91SaplingsProbe()
{
#if !UE_BUILD_SHIPPING
    if(!ProbeTask){return;}const double Now=GetWorld()->GetTimeSeconds();const FLDObjectView* Source=nullptr;double Best=125*125;
    for(const auto& V:ObjectView){const double D=FVector::DistSquared2D(V.Position,GetPawn()->GetActorLocation());if(V.Kind==ELDObjectKind::Sapling&&D<Best){Source=&V;Best=D;}}
    const bool Act=ProbeTask<=2||ProbeTask==7?ProbeTask!=7||SaplingProbeWinner:EnvironmentProbeRole==1;
    if(Act&&ProbeTask==5)
    {if(Source&&!(Source->State&5)&&Now-ProbeRetryAt>.4){ProbeRetryAt=Now;ServerCombatAction(OwnerState.Match,OwnerState.Life,++WorldSequence,0,(FVector(Source->Position)-GetPawn()->GetActorLocation()).GetSafeNormal2D());}}
    else if(Act&&!ProbeSent&&Source)
    {auto& R=ObjectProbeRequest;R=FLDObjectRequest();R.Match=OwnerState.Match;R.Life=OwnerState.Life;R.Sequence=++WorldSequence;R.Contract=LD_B91_LINK_WORLD_SHA;R.Observed=Source->Handle;R.ObjectRevision=Source->Revision;R.InventoryRevision=OwnerState.Inventory.Revision;R.Verb=ProbeTask==2?ELDObjectVerb::Chop:ELDObjectVerb::Harvest;ServerObjectInteraction(R);ProbeSent=true;ProbeHoldAt=Now;}
    if(ProbeSent&&Now-ProbeRetryAt>=.1)
    {ProbeRetryAt=Now;if(ProbeTask==3&&Now-ProbeHoldAt>.5){ServerObjectRelease(ObjectProbeRequest.Match,ObjectProbeRequest.Life,ObjectProbeRequest.Sequence);}else{ServerObjectHold(ObjectProbeRequest.Match,ObjectProbeRequest.Life,ObjectProbeRequest.Sequence,++Nonce);}}
    const bool Received=ProbeSent&&LastObjectReceipt.Sequence==ObjectProbeRequest.Sequence&&LastObjectReceipt.Result!=0;
    if(Received&&ProbeTask==1){SaplingProbeWinner=LastObjectReceipt.Result==1;}
    bool Done=Act?(ProbeTask==5?Source&&(Source->State&5):Received&&Now-ProbeHoldAt>.7):Now-ProbeAt>.7;
    if(Received&&(ProbeTask==1||ProbeTask==8))
    {if(!ProbeReceipt.IsValid()){ProbeReceipt=LastObjectReceipt.Receipt;ProbeDuplicateSerial=ObjectReceiptSerial;ServerObjectInteraction(ObjectProbeRequest);ServerObjectInteraction(ObjectProbeRequest);}Done=ObjectReceiptSerial>=ProbeDuplicateSerial+2&&LastObjectReceipt.Receipt==ProbeReceipt;}
    if(!Done){return;}auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("fixture"),TEXT("saplings"));R->SetNumberField(TEXT("phase"),ProbeTask);R->SetBoolField(TEXT("completed"),true);R->SetBoolField(TEXT("acted"),Act);R->SetNumberField(TEXT("result"),Received?LastObjectReceipt.Result:0);R->SetStringField(TEXT("reason"),Received?LastObjectReceipt.Reason:TEXT("observation"));R->SetBoolField(TEXT("duplicate_same"),!ProbeReceipt.IsValid()||ProbeReceipt==LastObjectReceipt.Receipt);R->SetBoolField(TEXT("cleared"),Source&&(Source->State&5));
    TArray<TSharedPtr<FJsonValue>> Fields;for(TFieldIterator<FProperty> I(FLDObjectView::StaticStruct());I;++I){Fields.Add(MakeShared<FJsonValueString>(I->GetName()));}R->SetArrayField(TEXT("wire_fields"),Fields);
    FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));ServerB91ProbeAck(ProbeTask,Json);FString Output;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Output);const FString ProbeLabel=EnvironmentProbeRole==1?TEXT("A"):TEXT("B");FFileHelper::SaveStringToFile(Json,*(Output/(ProbeLabel+FString::Printf(TEXT("_%d.json"),ProbeTask))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);ProbeTask=0;
#endif
}
