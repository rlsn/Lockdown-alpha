#include "World/LDEnvironmentDirector.h"
#include "Combat/LDTacticalCatalog.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Core/LDGroundQueries.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"
void ALDB91Controller::TickB91CombatProbe()
{
#if !UE_BUILD_SHIPPING
    if(!ProbeTask){return;}const double Now=GetWorld()->GetTimeSeconds();const bool Act=EnvironmentProbeRole==1&&ProbeTask!=3&&(ProbeTask<11||ProbeTask>=14)||EnvironmentProbeRole==2&&(ProbeTask==3||ProbeTask==13);
    if(ProbeTask==8&&EnvironmentProbeRole==1&&!ProbeSent&&Now-ProbeAt>.2)
    {auto R=FieldProbeRequest;R.Token=NextCombatToken();R.BuildRevision=OwnerState.Build.Revision;R.InventoryRevision=OwnerState.Inventory.Revision;ServerTacticalAction(R);ProbeSent=true;FieldProbeFeedback=FeedbackSerial();ProbeHoldAt=Now;}
    if(ProbeTask!=8&&Act&&!ProbeSent&&Now-ProbeAt>.2)
    {
        const FVector Expected=GetPawn()->GetActorLocation()+FVector(300,0,0);const auto* Target=View.FindByPredicate([&](const auto& V){return V.PublicDoor.IsNone()&&FVector::DistSquared2D(V.Position,Expected)<80*80;});FVector Ground;const FVector Hint=EnvironmentProbeRole==2?GetPawn()->GetActorLocation():Expected;if(!LDGround::MapSurface(GetWorld(),Hint,Ground)){return;}
        if(ProbeTask==13){ServerCombatAction(OwnerState.Match,OwnerState.Life,NextCombatToken(),0,FVector(0,500,0));}
        else if(ProbeTask==10){const auto* Item=OwnerState.Inventory.Items.FindByPredicate([](const auto& I){return I.Kind==ELDItemKind::SeersDust;});if(!Item){return;}SendItemAction(Item->Id,FGuid(),GetPawn()->GetActorLocation()-FVector(0,0,92.15));}
        else if(ProbeTask==2||ProbeTask==3){const auto Kind=ProbeTask==2?ELDItemKind::FrostPrism:ELDItemKind::FrostCapsule;const auto* Item=OwnerState.Inventory.Items.FindByPredicate([&](const auto& I){return I.Kind==Kind;});if(!Item||ProbeTask==2&&!Target){return;}SendItemAction(Item->Id,Target?Target->Handle:FGuid(),Ground);}
        else
        {
            const uint8 Slot=ProbeTask==1||ProbeTask==4||ProbeTask==16?0:ProbeTask==6||ProbeTask==14?1:ProbeTask==7||ProbeTask==15?2:3;if((ProbeTask==7||ProbeTask==15)&&!Target){return;}const auto* D=ULDTacticalCatalog::Load(8)->Find(OwnerState.Build.Slots[Slot]);if(!D){return;}
            FLDTacticalRequest R;R.Schema=8;R.WireVersion=5;R.ActionKind=0;R.SourceKind=0;R.SourceDefinition=FName(*D->Id);R.TargetMode=uint8(D->Target);R.Match=OwnerState.Match;R.Life=OwnerState.Life;R.Token=NextCombatToken();R.BuildRevision=OwnerState.Build.Revision;R.InventoryRevision=OwnerState.Inventory.Revision;R.CatalogDigest=OwnerState.Build.CatalogDigest;R.Slot=Slot;R.Point=Ground;R.Direction=(Ground-GetPawn()->GetActorLocation()).GetSafeNormal2D();if(Target){R.Observed=Target->Handle;}FieldProbeRequest=R;ServerTacticalAction(R);
        }
        ProbeSent=true;FieldProbeFeedback=FeedbackSerial();ProbeHoldAt=Now;
    }
    const bool Done=Act?ProbeSent&&Now-ProbeHoldAt>1.2&&FeedbackSerial()>FieldProbeFeedback:Now-ProbeAt>1.2;if(!Done&&Now-ProbeAt<10){return;}
    auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("completed"),Done);R->SetNumberField(TEXT("phase"),ProbeTask);R->SetStringField(TEXT("reason"),IntentFeedback);R->SetNumberField(TEXT("body_count"),View.Num());R->SetBoolField(TEXT("old_handle_resolved"),View.ContainsByPredicate([&](const auto& V){return V.Handle==FieldProbeRequest.Observed;}));R->SetNumberField(TEXT("surface_count"),SurfaceView.Num());
    TArray<TSharedPtr<FJsonValue>> Fields;for(TFieldIterator<FProperty> I(FLDEnvironmentSurface::StaticStruct());I;++I){Fields.Add(MakeShared<FJsonValueString>(I->GetName()));}R->SetArrayField(TEXT("surface_fields"),Fields);
    FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));ServerB91ProbeAck(ProbeTask,Json);FString Out;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Out);FFileHelper::SaveStringToFile(Json,*(Out/((EnvironmentProbeRole==1?TEXT("A"):TEXT("B"))+FString::Printf(TEXT("_%d.json"),ProbeTask))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);ProbeTask=0;
#endif
}
