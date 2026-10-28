#include "World/LDEnvironmentDirector.h"
#include "Character/LDCharacter.h"
#include "Navigation/LDNavigationComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"
#include "Engine/World.h"

void ALDB91Controller::TickB91GrassProbe()
{
#if !UE_BUILD_SHIPPING
    if(!ProbeTask){return;}const double Now=GetWorld()->GetTimeSeconds();
    if(ProbeTask==6&&EnvironmentProbeRole==1&&!ProbeSent&&Now-ProbeAt>.15){BasicAttackArmed=true;ConfirmSource(FGuid(),GetPawn()->GetActorLocation()+FVector(0,500,0));ProbeSent=true;}
    if(ProbeTask==5&&EnvironmentProbeRole==1&&!ProbeSent){const auto* G=ObjectView.FindByPredicate([this](const auto& V){return V.Kind==ELDObjectKind::TallGrass&&FVector::DistSquared2D(GetPawn()->GetActorLocation(),V.Position)<120.*120;});if(G){Navigation->Navigate(FVector(G->Position)+FVector(100,0,-78));ProbeSent=true;}}
    if(ProbeTask==13)
    {const auto* T=OwnerState.Inventory.Items.FindByPredicate([](const auto& I){return I.Kind==ELDItemKind::Torch;});if(T&&!ProbeSent){BagSelection=OwnerState.Inventory.Items.IndexOfByPredicate([&](const auto& I){return I.Id==T->Id;});ItemCommand(TEXT("Equip"),0);ProbeSent=true;}if(!T||T->BindingMask!=(1<<7)||OwnerState.DevelopmentKind){return;}}
    if(ProbeTask==14)
    {
        const auto* G=ObjectView.FindByPredicate([this](const auto& O){return O.Kind==ELDObjectKind::TallGrass&&!(O.State&5)&&FVector::DistSquared2D(GetPawn()->GetActorLocation(),O.Position)<=120.*120;});if(!ProbeSent&&G){UseAccessory(0);ConfirmSource(G->Handle,G->Position);ObjectProbeRequest.Sequence=WorldSequence;ProbeSent=true;ProbeHoldAt=Now;}
        if(!ProbeSent||LastObjectReceipt.Sequence!=ObjectProbeRequest.Sequence||!LastObjectReceipt.Result){return;}
        if(!ProbeReceipt.IsValid())
        {ProbeReceipt=LastObjectReceipt.Receipt;ProbeDuplicateSerial=ObjectReceiptSerial;const auto* T=OwnerState.Inventory.Items.FindByPredicate([](const auto& I){return I.Kind==ELDItemKind::Torch;});if(T&&G){FLDObjectRequest R;R.Match=OwnerState.Match;R.Life=OwnerState.Life;R.Sequence=ObjectProbeRequest.Sequence;R.Contract=ALDWorldObjects::ActiveContract();R.Verb=ELDObjectVerb::Ignite;R.Observed=G->Handle;R.ObjectRevision=G->Revision;R.Item=T->Id;ServerObjectInteraction(R);}return;}
        if(ObjectReceiptSerial<=ProbeDuplicateSerial){return;}
    }
    const double Wait=ProbeTask==5?1.3:ProbeTask==7?1.9:ProbeTask==8?1.2:ProbeTask==10?.35:ProbeTask==11?.9:ProbeTask==15?3.4:ProbeTask==16?1.2:.75;
    if(Now-ProbeAt<Wait){return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("LDB91NetCapture"))&&ProbeTask==1&&!ProbeFrame){FString Out;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Out);FScreenshotRequest::RequestScreenshot(Out/TEXT("GrassConcealment.png"),true,false);++ProbeFrame;ProbeRetryAt=Now;return;}if(ProbeTask==1&&ProbeFrame&&Now-ProbeRetryAt<.3){return;}
    auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("fixture"),TEXT("grass"));R->SetNumberField(TEXT("phase"),ProbeTask);R->SetBoolField(TEXT("completed"),true);const auto* Body=View.FindByPredicate([](const auto& V){return V.PublicDoor.IsNone();});R->SetBoolField(TEXT("body_seen"),Body!=nullptr);R->SetStringField(TEXT("body_handle"),Body?Body->Handle.ToString():TEXT(""));R->SetNumberField(TEXT("result"),ProbeTask==14?LastObjectReceipt.Result:0);R->SetBoolField(TEXT("duplicate_same"),!ProbeReceipt.IsValid()||LastObjectReceipt.Receipt==ProbeReceipt);R->SetNumberField(TEXT("object_count"),ObjectView.Num());R->SetNumberField(TEXT("surface_count"),SurfaceView.Num());
    TArray<TSharedPtr<FJsonValue>> Fields;for(TFieldIterator<FProperty> I(FLDEnvironmentViewEntry::StaticStruct());I;++I){Fields.Add(MakeShared<FJsonValueString>(I->GetName()));}R->SetArrayField(TEXT("wire_fields"),Fields);
    FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));ServerB91ProbeAck(ProbeTask,Json);FString Out;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Out);FFileHelper::SaveStringToFile(Json,*(Out/FString::Printf(TEXT("%s_%d.json"),EnvironmentProbeRole==1?TEXT("A"):TEXT("B"),ProbeTask)),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);ProbeTask=0;
#endif
}
