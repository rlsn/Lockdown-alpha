#include "Combat/LDCombatDirector.h"
#include "World/LDEnvironmentDirector.h"
#include "Engine/World.h"

void ALDCombatDirector::PublishFullMapSurfaces(int64 At)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* E=ALDEnvironmentDirector::Get(GetWorld());if(!M||!E){return;}++SurfaceViewRevision;
    for(FConstPlayerControllerIterator I=GetWorld()->GetPlayerControllerIterator();I;++I)
    {
        auto* PC=Cast<ALDB91Controller>(I->Get());const auto* Own=PC?M->StateFor(PC):nullptr;if(!Own){continue;}TArray<FLDEnvironmentSurface> View;
        if(Own->bActive&&PC->GetPawn())
        {for(const auto& Pair:RiverCells){const auto& C=Pair.Value;if(!C.Mask(At)){continue;}FVector P=LDRiver::Centre(Pair.Key);P.Z=C.SurfaceZ+5;
         if(View.Num()>=384||!E->Ray(PC->GetPawn()->GetActorLocation(),P,nullptr,E->SightFor(PC),true)){continue;}
         FLDEnvironmentSurface V;V.Position=P;V.States=C.Mask(At);for(uint8 N=0;N<7;++N){V.Revision=FMath::Max(V.Revision,C.Get(LDEnvironment::EState(N)).Revision);}View.Add(V);}}
        PC->ClientSurfaceView(Own->Match,Own->Life,SurfaceViewRevision,View);
    }
}
