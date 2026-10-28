#include "World/LDEnvironmentDirector.h"
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

void ALDB91Controller::TickB91FieldsProbe()
{
#if !UE_BUILD_SHIPPING
    if(!ProbeTask){return;}const double Now=GetWorld()->GetTimeSeconds();
    const auto Kind=ProbeTask==5||ProbeTask==6?ELDItemKind::OilFlask:ELDItemKind::WaterFlask;
    const auto* Item=OwnerState.Inventory.Items.FindByPredicate([&](const auto& I){return I.Kind==Kind;});
    auto Bind=[&](ELDItemKind K,uint8 Slot)
    {const auto* I=OwnerState.Inventory.Items.FindByPredicate([&](const auto& V){return V.Kind==K;});if(!I){return;}FLDLoadoutRequest R;R.Version=2;R.Match=OwnerState.Match;R.Life=OwnerState.Life;R.Token=4000+ProbeTask*10+ProbeStep;R.InventoryRevision=OwnerState.Inventory.Revision;R.BuildRevision=OwnerState.Build.Revision;R.ItemRevision=I->Revision;R.Item=I->Id;R.Command=LDInventory::Equip;R.Binding=Slot;R.EquipmentDigest=ULDEquipmentCatalog::Load(8)->Checksum;ToolProbeDevToken=R.Token;ToolProbeDevNonce=0;ToolProbeDevAt=Now;ServerLoadoutRequest(R);};
    auto Issue=[&]()
    {if(!Item){return;}auto& R=FieldProbeRequest;R=FLDTacticalRequest();R.Schema=8;R.WireVersion=5;R.ActionKind=1;R.SourceKind=2;R.SourceDefinition=FName(*ULDEquipmentCatalog::Load(8)->Find(Item->Kind)->Id);R.CatalogDigest=OwnerState.Build.CatalogDigest;R.Match=OwnerState.Match;R.Life=OwnerState.Life;R.Token=NextCombatToken();R.BuildRevision=OwnerState.Build.Revision;R.InventoryRevision=OwnerState.Inventory.Revision;R.ItemRevision=Item->Revision;R.Item=Item->Id;R.Slot=Kind==ELDItemKind::OilFlask?1:0;FVector Ground;const FVector P=GetPawn()->GetActorLocation()+FVector(300,0,-92.15);R.Point=LDGround::MapSurface(GetWorld(),P,Ground)?Ground:P;FieldProbeFeedback=FeedbackSerial();ServerTacticalAction(R);ProbeSent=true;ProbeHoldAt=Now;};
    bool Done=false;
    if(EnvironmentProbeRole==2){const uint8 Wanted=ProbeTask==4?1:ProbeTask==6?64:0;Done=Now-ProbeAt>.7&&(!Wanted||SurfaceView.ContainsByPredicate([&](const auto& V){return (V.States&Wanted)!=0;}));}
    else if(ProbeTask==2)
    {if(!ProbeStep&&Item){Bind(ELDItemKind::WaterFlask,7);ProbeStep=1;}if(ProbeStep==1&&Item&&Item->BindingMask==(1<<7)&&!OwnerState.DevelopmentKind){ToolProbeDevToken=0;Bind(ELDItemKind::OilFlask,8);ProbeStep=2;}const auto* O=OwnerState.Inventory.Items.FindByPredicate([](const auto& I){return I.Kind==ELDItemKind::OilFlask;});Done=ProbeStep==2&&O&&O->BindingMask==(1<<8)&&!OwnerState.DevelopmentKind;}
    else if(ProbeTask==7)
    {if(!ProbeStep&&Item){Bind(ELDItemKind::WaterFlask,7);ProbeStep=1;}Done=ProbeStep&&Item&&Item->BindingMask==(1<<7)&&!OwnerState.DevelopmentKind;}
    else
    {if(!ProbeSent&&Item&&Now-ProbeAt>.3){Issue();}const bool Feedback=ProbeSent&&FeedbackSerial()>FieldProbeFeedback;
     if(ProbeTask==4&&Feedback&&!Item&&!ProbeStep){ServerTacticalAction(FieldProbeRequest);ServerTacticalAction(FieldProbeRequest);ProbeStep=1;ProbeRetryAt=Now;}
     if(ProbeTask==8&&Feedback&&!ProbeStep&&Item&&!Item->bReserved&&IntentFeedback.Contains(TEXT("eligible surface"))){ToolProbeIdentity=Item?Item->Id:FGuid();ProbeStep=1;ProbeRetryAt=Now;}
     if(ProbeTask==8&&ProbeStep==1&&Now-ProbeRetryAt>11&&Item){Issue();ProbeStep=2;}
     Done=ProbeTask==4?ProbeStep&&Now-ProbeRetryAt>.5:ProbeTask==8?ProbeStep==2&&Feedback&&!Item:ProbeTask==6?Feedback&&!Item&&Now-ProbeHoldAt>.6:Feedback;}
    if(ToolProbeDevToken&&Now-ToolProbeDevAt>.08){ToolProbeDevAt=Now;ServerDevelopmentHold(OwnerState.Match,OwnerState.Life,ToolProbeDevToken,++ToolProbeDevNonce,false);}
    if(!Done&&Now-ProbeAt<28){return;}
    auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("fixture"),TEXT("fields"));R->SetNumberField(TEXT("phase"),ProbeTask);R->SetBoolField(TEXT("completed"),Done);R->SetStringField(TEXT("reason"),IntentFeedback);R->SetBoolField(TEXT("owned_item"),Item!=nullptr);R->SetNumberField(TEXT("surface_count"),SurfaceView.Num());uint8 Mask=0;for(const auto& V:SurfaceView){Mask|=V.States;}R->SetNumberField(TEXT("states"),Mask);
    R->SetBoolField(TEXT("capacity_retained"),ProbeTask!=8||EnvironmentProbeRole!=1||ToolProbeIdentity==FieldProbeRequest.Item);
    TArray<TSharedPtr<FJsonValue>> Fields;for(TFieldIterator<FProperty> I(FLDEnvironmentSurface::StaticStruct());I;++I){Fields.Add(MakeShared<FJsonValueString>(I->GetName()));}R->SetArrayField(TEXT("wire_fields"),Fields);
    FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));ServerB91ProbeAck(ProbeTask,Json);FString Output;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Output);const FString Label=EnvironmentProbeRole==1?TEXT("A"):TEXT("B");FFileHelper::SaveStringToFile(Json,*(Output/(Label+FString::Printf(TEXT("_%d.json"),ProbeTask))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);ProbeTask=0;ToolProbeDevToken=0;
#endif
}
