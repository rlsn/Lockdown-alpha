#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/LDEnvironmentDirector.h"
#include "LDB91Probe.generated.h"
class FJsonObject;
class FJsonValue;
UCLASS()
class LOCKDOWN_API ALDB91Probe:public AActor
{
    GENERATED_BODY()
public:
    ALDB91Probe();
    virtual void Tick(float Delta) override;
private:
    ALDEnvironmentDirector* Environment=nullptr;
    ALDB91Controller* PC=nullptr;
    ALDEnvironmentDoor* Rear=nullptr;
    uint32 Token=0,Nonce=0;
    int32 Phase=0,Index=0,Failures=0;
    double At=0,WalkAt=0,Length=0,Falling=0,NextTrace=0;
    FVector Last,Goal;
    uint32 PriorRevision=0;
    FGuid PriorReceipt;
    FLDWorldRequest Request;
    TArray<FVector> Starts,Goals;
    TArray<FString> Names;
    TArray<TSharedPtr<FJsonValue>> Checks,Walks;
    FString Trace=TEXT("case,elapsed_s,x_cm,y_cm,z_cm,walked_cm\n");
    void Check(bool Good,const FString& Name);
    void Issue(ELDWorldVerb Verb,uint32 Revision=0);
    void Next(int32 Value);
    void Save(bool Complete);
};
