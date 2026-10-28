#include "World/LDWorldObjects.h"
#include "World/LDB91Checkpoint.h"
#include "Engine/World.h"

bool ALDWorldObjects::ValidateCheckpointGeometry(const LDCheckpoint::FData& Data,FString& Error) const
{
    // Validate the proposed scene, ignoring the pristine runtime props that it
    // will replace. Otherwise a legal swap would collide with obsolete poses.
    FCollisionQueryParams Query(SCENE_QUERY_STAT(B91CheckpointPlacement),false);
    for(auto Object:Objects){if(Object.IsValid()){Query.AddIgnoredActor(Object.Get());}}
    for(const auto& Row:Data.Objects)
    {
        auto* Object=Find(FName(*Row.Id));if(!Object||!Object->Heavy()){continue;}
        const FVector Extent=Object->Size()*.5;
        const FVector Floor=Row.Position-FVector(0,0,Extent.Z+3);
        auto Fail=[&](const TCHAR* Reason){Error=FString(Reason)+TEXT(": ")+Row.Id;return false;};
        if(!Floor.Equals(Row.Clear,.1)){return Fail(TEXT("Saved prop clear pad disagrees with its pose"));}
        FVector Centre;if(!SurfaceAt(Floor,Centre)){return Fail(TEXT("Saved prop has no support"));}
        double Highest=Centre.Z;
        const double Grade=Object->Kind==ELDObjectKind::Ladder?FMath::Max(20.,Extent.Size2D()*.25):20.;
        for(int X:{-1,1}){for(int Y:{-1,1})
        {
            FVector Corner;if(!SurfaceAt(Centre+FVector(X*Extent.X,Y*Extent.Y,0),Corner)||FMath::Abs(Corner.Z-Centre.Z)>Grade+.1){return Fail(TEXT("Saved prop footprint is unsupported"));}
            Highest=FMath::Max(Highest,Corner.Z);
        }}
        if(FMath::Abs(Highest-Floor.Z)>.1){return Fail(TEXT("Saved prop floats or penetrates its support"));}
        if(Row.Broken){continue;}
        if(GetWorld()->OverlapAnyTestByChannel(Row.Position,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeBox(Extent),Query)){return Fail(TEXT("Saved prop overlaps fixed world geometry"));}
        for(const auto& Other:Data.Objects)
        {
            if(Other.Id==Row.Id||Other.Broken){continue;}auto* Peer=Find(FName(*Other.Id));
            if(!Peer||!Peer->BlocksWalk()){continue;}
            const FVector Separation=(Row.Position-Other.Position).GetAbs(),Sum=Extent+Peer->Size()*.5;
            if(Separation.X<Sum.X&&Separation.Y<Sum.Y&&Separation.Z<Sum.Z){return Fail(TEXT("Saved solid props overlap"));}
        }
    }
    return true;
}
