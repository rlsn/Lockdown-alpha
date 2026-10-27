#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/LDWorldObjects.h"
#include "LDB91TraversalProbe.generated.h"
class FJsonValue;
UCLASS()
class LOCKDOWN_API ALDB91TraversalProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDB91TraversalProbe();
    virtual void Tick(float Delta) override;
private:
    ALDB91Controller* A=nullptr;
    ALDB91Controller* B=nullptr;
    ALDB91Controller* Deployer=nullptr;
    ALDWorldObjects* Objects=nullptr;
    ALDWorldObject* Ladder=nullptr;
    ALDWorldObject* Anchor=nullptr;
    ALDItem* Rope=nullptr;
    FLDObjectRequest NativeRequest;
    uint8 Phase=0;
    int Failures=0;
    uint32 Beat=0;
    double Started=0,At=0,ReturnAt=0;
    bool Interrupted=false;
    TArray<TSharedPtr<FJsonValue>> Checks,Geometry;
    void Check(bool Good,const FString& Text);
    void Place(ALDB91Controller* PC,FVector Centre);
    void Task(uint8 Value);
    void Save(bool Complete);
    void Native(ELDObjectVerb Verb,ALDB91Controller* PC);
};
