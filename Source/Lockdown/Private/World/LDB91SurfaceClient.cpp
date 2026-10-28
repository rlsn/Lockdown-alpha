#include "World/LDEnvironmentDirector.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Combat/LDRiverRules.h"
#include "Engine/World.h"

void ALDB91Controller::ClientSurfaceView_Implementation(uint32 Match,uint32 Life,uint64 Revision,const TArray<FLDEnvironmentSurface>& Entries)
{
    if(!IsLocalController()||Match!=OwnerState.Match||Life!=OwnerState.Life||Entries.Num()>384||(Match==SurfaceMatch&&Life==SurfaceLife&&Revision<=SurfaceViewRevision)){return;}
    SurfaceMatch=Match;SurfaceLife=Life;SurfaceViewRevision=Revision;SurfaceAt=GetWorld()->GetTimeSeconds();SurfaceView=Entries;TSet<FIntVector> Seen;
    for(const auto& V:Entries)
    {const FIntPoint XY=LDRiver::Cell(V.Position);const FIntVector Key(XY.X,XY.Y,FMath::RoundToInt(V.Position.Z*10));Seen.Add(Key);auto& Weak=SurfaceProxies.FindOrAdd(Key);auto* O=Weak.Get();
     if(!O){O=GetWorld()->SpawnActor<ALDWorldObject>();O->Kind=ELDObjectKind::LooseItem;Weak=O;}
     O->Apply(V.Position,0,V.Revision);O->SetActorEnableCollision(false);O->Proxy->SetRelativeScale3D(FVector(2,2,.03));O->SetObserved(true);
     auto* Material=Cast<UMaterialInstanceDynamic>(O->Proxy->GetMaterial(0));if(!Material){Material=O->Proxy->CreateAndSetMaterialInstanceDynamic(0);}
     if(Material){const FLinearColor Tint=V.States&4?FLinearColor(1,.2f,0):V.States&2?FLinearColor(.5f,.9f,1):V.States&64?FLinearColor(.3f,.2f,0):FLinearColor(0,.3f,.8f);Material->SetVectorParameterValue(TEXT("Color"),Tint);}}
    for(auto It=SurfaceProxies.CreateIterator();It;++It){if(!Seen.Contains(It.Key())){if(It.Value().IsValid()){It.Value()->Destroy();}It.RemoveCurrent();}}
}
