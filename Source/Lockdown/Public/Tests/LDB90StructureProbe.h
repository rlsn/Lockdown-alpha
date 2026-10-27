#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDB90StructureProbe.generated.h"
class ALDCharacter;
class ALDB90PreviewController;
class ULDMapDefinition;
class ULDMapRegistry;
class FJsonObject;

UCLASS()
class LOCKDOWN_API ALDB90StructureProbe : public AActor
{
    GENERATED_BODY()
public:
    ALDB90StructureProbe();
    virtual void Tick(float Delta) override;
private:
    struct FCase { FName Service,Building; FString Kind; FVector Start,Goal; };
    ALDCharacter* Body=nullptr;
    ALDB90PreviewController* Controller=nullptr;
    const ULDMapDefinition* Definition=nullptr;
    ULDMapRegistry* Registry=nullptr;
    TSharedPtr<FJsonObject> Root;
    TArray<FCase> Cases;
    TArray<TSharedPtr<class FJsonValue>> Walks;
    FString Output,Trace,Settlement;
    int32 Phase=0,Index=0,Failures=0,Step=0;
    double At=0,WalkAt=0,Length=0,Peak=0,Falling=0,NextSample=0;
    uint32 Replans=0;
    bool Capture=false,Front=false,Rear=false,Inside=false;
    FVector Last;
    bool Initialize();
    void BeginCase();
    void EndCase(bool Passed,const FString& Why);
    void Save(bool Complete);
};
