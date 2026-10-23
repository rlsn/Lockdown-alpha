#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDEconomyUIProbe.generated.h"
class ALDPlayerController;
UCLASS()
class LOCKDOWN_API ALDEconomyUIProbe:public AActor
{
    GENERATED_BODY()
public:
    ALDEconomyUIProbe();
    virtual void Tick(float Delta) override;
private:
    int32 Phase=0,Failures=0;
    double At=0,Started=0,ShotAt=0,CaptureUntil=0;
    int32 ShotPhase=-1;
    FString PendingImage;
    uint32 Token=0;
    int64 Gold=0;
    FString Folder;
    TArray<FString> Checks,Images;
    TWeakObjectPtr<ALDPlayerController> A,B;
    void Check(bool Good,const FString& Text);
    void Enter(int32 Next);
    void Capture(const FString& Name);
    void AuditPanel();
    bool SnapshotReady(const FString& Name);
    void Input(FKey Key,bool Down);
    void Finish();
};
