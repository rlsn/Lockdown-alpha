#include "World/LDEnvironmentDirector.h"
#include "Navigation/LDNavigationComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "UnrealClient.h"
void ALDB91Controller::TickB91EncounterProbe()
{
#if !UE_BUILD_SHIPPING
    if(!ProbeTask){return;}const double Now=GetWorld()->GetTimeSeconds();bool Done=EnvironmentProbeRole==2&&Now-ProbeAt>.8;
    if(EnvironmentProbeRole==1)
    {
        const bool Walk=ProbeTask<=3||ProbeTask==5||ProbeTask==11||ProbeTask==12||ProbeTask==15||ProbeTask==16||ProbeTask==17||ProbeTask==18||ProbeTask==20||ProbeTask==23;
        auto Find=[&](ELDObjectKind Kind){return ObjectView.FindByPredicate([&](const auto& V){return V.Kind==Kind&&!(V.State&5)&&FVector::DistSquared2D(V.Position,ObjectProbePad)<150.*150;});};
        if(!ProbeSent&&Now-ProbeAt>.25)
        {
            if(Walk){Navigation->Navigate(ObjectProbePad);}
            else if(ProbeTask==4){const auto* D=View.FindByPredicate([](const auto& V){return V.PublicDoor==TEXT("building_riverport_00_door_north");});if(!D){return;}PointerOrder(true,FGuid(),D->Handle,D->Position);}
            else if(ProbeTask==6||ProbeTask==8){const auto* V=Find(ELDObjectKind::Crate);if(!V){return;}PointerOrder(false,V->Handle,FGuid(),V->Position);}
            else if(ProbeTask==7){const auto* I=OwnerState.Inventory.Items.FindByPredicate([](const auto& V){return V.Kind==ELDItemKind::ForestryKnife;});if(!I){return;}IssueObject(ELDObjectVerb::Store,ContainerHandle,I->Id);}
            else if(ProbeTask==9){const auto* I=ContainerView.FindByPredicate([](const auto& V){return V.Kind==ELDItemKind::ForestryKnife;});if(!I){return;}IssueObject(ELDObjectVerb::Take,ContainerHandle,I->Item);}
            else if(ProbeTask==10||ProbeTask==21||ProbeTask==25){const auto Kind=ProbeTask==10?ELDItemKind::Rope:ProbeTask==21?ELDItemKind::ForestryKnife:ELDItemKind::Torch;BagSelection=OwnerState.Inventory.Items.IndexOfByPredicate([&](const auto& I){return I.Kind==Kind;});if(BagSelection==INDEX_NONE){return;}ItemCommand(TEXT("Equip"),ProbeTask==10?1:0);}
            else if(ProbeTask==13){const auto* V=Find(ELDObjectKind::Anchor);if(!V){return;}UseAccessory(1);ConfirmSource(V->Handle,V->Position);}
            else if(ProbeTask==14||ProbeTask==19){const auto* V=Find(ELDObjectKind::Anchor);if(!V){return;}IssueObject(ProbeTask==14?ELDObjectVerb::Use:ELDObjectVerb::Retrieve,V->Handle);}
            else if(ProbeTask==22||ProbeTask==26){const auto* V=Find(ProbeTask==22?ELDObjectKind::Sapling:ELDObjectKind::TallGrass);if(!V){return;}UseAccessory(0);ConfirmSource(V->Handle,V->Position);}
            ProbeSent=true;ProbeHoldAt=Now;ProbeDuplicateSerial=ProbeTask==4?WorldReceiptSerial:ObjectReceiptSerial;
        }
        if(Walk&&ProbeSent&&ProbeRetries<3&&Now-ProbeHoldAt>1&&Now-ProbeRetryAt>1&&!Navigation->HasMoveOrder()&&FVector::Dist2D(GetPawn()->GetActorLocation(),ObjectProbePad)>160)
        {++ProbeRetries;ProbeRetryAt=Now;Navigation->Navigate(ObjectProbePad);}
        if(Walk){Done=ProbeSent&&Now-ProbeHoldAt>.5&&(FVector::Dist2D(GetPawn()->GetActorLocation(),ObjectProbePad)<60||Navigation->Status==TEXT("Arrived")&&FVector::Dist2D(GetPawn()->GetActorLocation(),ObjectProbePad)<160)&&FMath::Abs(GetPawn()->GetActorLocation().Z-92.15-ObjectProbePad.Z)<25;}
        else if(ProbeTask==10||ProbeTask==21||ProbeTask==25){const auto Kind=ProbeTask==10?ELDItemKind::Rope:ProbeTask==21?ELDItemKind::ForestryKnife:ELDItemKind::Torch;const int Slot=ProbeTask==10?1:0;const auto* I=OwnerState.Inventory.Find(OwnerState.Inventory.Bindings[7+Slot]);Done=ProbeSent&&I&&I->Kind==Kind&&!OwnerState.DevelopmentKind&&Now-ProbeHoldAt>.5;}
        else if(ProbeTask==24||ProbeTask==27){Done=Now-ProbeAt>(ProbeTask==24?1.5:3.5);}
        else if(ProbeTask==4){Done=ProbeSent&&WorldReceiptSerial>ProbeDuplicateSerial&&LastWorldReceipt.Result&&Now-ProbeHoldAt>.6;}
        else{Done=ProbeSent&&ObjectReceiptSerial>ProbeDuplicateSerial&&LastObjectReceipt.Result&&Now-ProbeHoldAt>.6;}
    }
    if(EnvironmentProbeRole==1&&FParse::Param(FCommandLine::Get(),TEXT("LDB91EncounterCapture"))&&ProbeFrame!=ProbeTask&&((ProbeTask==15||ProbeTask==24)&&Now-ProbeAt>1||ProbeTask==22&&ChannelActive&&Now-ProbeAt>2))
    {FString Out;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Out);FScreenshotRequest::RequestScreenshot(Out/FString::Printf(TEXT("Reference_%u.png"),ProbeTask),true,false);ProbeFrame=ProbeTask;}
    if(!Done&&Now-ProbeAt<40){return;}auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("phase"),ProbeTask);R->SetBoolField(TEXT("completed"),Done);R->SetStringField(TEXT("reason"),IntentFeedback);R->SetNumberField(TEXT("result"),ProbeTask==4?LastWorldReceipt.Result:LastObjectReceipt.Result);R->SetBoolField(TEXT("other_body_seen"),View.ContainsByPredicate([](const auto& V){return V.PublicDoor.IsNone();}));R->SetStringField(TEXT("position"),GetPawn()->GetActorLocation().ToString());R->SetStringField(TEXT("navigation"),Navigation->Status);R->SetNumberField(TEXT("explicit_move_retries"),ProbeRetries);
    FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));ServerB91ProbeAck(ProbeTask,Json);FString Out;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Out);FFileHelper::SaveStringToFile(Json,*(Out/FString::Printf(TEXT("%s_%d.json"),EnvironmentProbeRole==1?TEXT("A"):TEXT("B"),ProbeTask)),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);ProbeTask=0;
#endif
}
