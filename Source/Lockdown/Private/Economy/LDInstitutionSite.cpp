#include "Economy/LDInstitutionSite.h"
#include "Region/LDRegionDefinition.h"
#include "Components/SceneComponent.h"
ALDInstitutionSite::ALDInstitutionSite(){bReplicates=false;SetCanBeDamaged(false);RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));}
FVector ALDInstitutionSite::Position(const UWorld* W,uint8 Site)
{
    const uint8 Address=Site==1||Site==3?2:Site==2||Site==4?6:1;const FVector Base=LDRegion::Site(W,Address);const auto* D=LDRegion::Definition(W);
    // Only saved public map metadata participates; never inspect a contender's
    // hidden location or purse to choose an official service position.
    const TArray<FVector> Offsets=Site>=3?TArray<FVector>{{300,0,92},{0,300,92},{0,-300,92},{-300,0,92},{300,300,92}}:TArray<FVector>{{-180,180,92},{-180,-180,92},{0,-300,92},{0,300,92},{-300,0,92}};
    for(const auto& Offset:Offsets){const FVector Point=Base+Offset;bool Clear=!D||!D->Residents.ContainsByPredicate([&](const auto& R){return FVector::DistSquared2D(R,Point)<160*160;});if(Site>=3){Clear&=FVector::DistSquared2D(Point,Position(W,Site-2))>=180*180;}if(Clear){return Point;}}
    return Base+FVector(0,400,92);
}
FString ALDInstitutionSite::Name(uint8 Site)
{const TCHAR* N[]={TEXT("Official City consumer · Toolkit"),TEXT("Official West consumer · Fiber"),TEXT("Official East consumer · Ore"),TEXT("Official West producer · Fiber / 2 gold"),TEXT("Official East producer · Ore / 3 gold")};return Site<5?N[Site]:TEXT("Official institution");}
FString ALDInstitutionSite::StatusName(uint8 Status)
{const TCHAR* N[]={TEXT("Waiting"),TEXT("Working · receipt committed"),TEXT("Skipped · stock empty"),TEXT("Skipped · reserve insufficient"),TEXT("Skipped · stock full"),TEXT("Skipped · allocation failed")};return Status<6?N[Status]:TEXT("Unavailable");}
