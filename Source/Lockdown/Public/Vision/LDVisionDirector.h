#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Vision/LDViewTypes.h"
#include "LDVisionDirector.generated.h"
class ALDPlayerController;
class ALDWorldViewRelay;
class ALDCharacter;

struct FLDSightSource { FVector Position; float Radius; uint8 Level; };

UCLASS()
class LOCKDOWN_API ALDWard : public AActor
{
    GENERATED_BODY()
public:
    ALDWard();
    TWeakObjectPtr<ALDPlayerController> Controller;
    float ExpiresAt = 0;
    int32 Hp = 20;
    FGuid Identity;
};

UCLASS()
class LOCKDOWN_API ALDVisionDirector : public AActor
{
    GENERATED_BODY()
public:
    ALDVisionDirector();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    void Register(ALDPlayerController* Controller);
    void Unregister(ALDPlayerController* Controller);
    bool PlaceWard(ALDPlayerController* Controller, const FVector& Desired, FString& Reason);
    bool Ray(const FLDSightSource& Source, const FVector& Target) const;
    uint8 LevelAt(const FVector& Position) const;
    void SetPublicSensor(bool bActive);
    bool IsPublicSensorActive() const { return bPublicSensor; }
    bool CanSee(ALDPlayerController* Controller, const FVector& Point) const;
    ALDWorldViewRelay* RelayFor(ALDPlayerController* Controller) const;
    const FLDWorldView* AuthorityViewFor(ALDPlayerController* Controller) const;
    AActor* ResolveObserved(ALDPlayerController* Controller,const FGuid& ObservedId) const;
    bool bPublicationEnabled = true;
    uint32 Generation = 1;
    TArray<FLDSightSource> SourcesFor(ALDPlayerController* Controller) const;
    double LastSampleMs=0;
    uint64 SampleSerial=0;
    static FVector PublicPosition() { return FVector(1200,1000,0); }
private:
    struct FObserver
    {
        TWeakObjectPtr<ALDWorldViewRelay> Relay;
        TArray<uint8> Explored;
        TMap<TWeakObjectPtr<AActor>,FGuid> Ids;
        TSet<FGuid> Visible;
        FLDWorldView Pending;
    };
    TMap<TWeakObjectPtr<ALDPlayerController>,FObserver> Observers;
    TArray<TWeakObjectPtr<ALDWard>> Wards;
    TArray<FBox> HighGround;
    double NextSample = 0, NextPublish = 0;
    uint64 Revision = 0;
    bool bPublicSensor = true;
    void Sample(bool bPublish);
};
