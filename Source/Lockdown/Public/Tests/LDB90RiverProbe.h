#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDB90RiverProbe.generated.h"

class ALDCharacter;
class ALDB90PreviewController;
class ULDMapDefinition;
class ULDMapSurfaceSubsystem;
class FJsonValue;

UCLASS()
class LOCKDOWN_API ALDB90RiverProbe : public AActor
{
    GENERATED_BODY()
public:
    ALDB90RiverProbe();
    virtual void Tick(float Delta) override;
private:
    struct FCase { FString Id; FVector Centre,Direction; double HalfLength=1000; bool Freeze=false,Water=true,Residual=false; };
    ALDCharacter* Body=nullptr;
    ALDB90PreviewController* Controller=nullptr;
    const ULDMapDefinition* Definition=nullptr;
    ULDMapSurfaceSubsystem* Surface=nullptr;
    TArray<FCase> Cases;
    TArray<TSharedPtr<FJsonValue>> Results;
    FString Output,Trace;
    int32 Index=0,Phase=0,Failures=0,WetSamples=0,IceSamples=0,SlowSamples=0,StunSamples=0,FallingSamples=0;
    double PhaseAt=0,WalkAt=0,IceAt=-1,Peak=0,IcePeak=0,Length=0;
    FVector Last,Goal;
    bool Setup=true,PathStable=true,ExitWet=false,ExpiryClear=false;
    bool Capture=false,Shot=false;
    bool Initialize();
    void BeginCase();
    void EndCase(bool Arrived,const FString& Why);
    void Finish();
};
