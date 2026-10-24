#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Economy/LDProductionDirector.h"
#include "LDAcquisitionDirector.generated.h"
class ALDPlayerController;
UCLASS()
class LOCKDOWN_API ALDAcquisitionDirector:public AActor
{
    GENERATED_BODY()
public:
    ALDAcquisitionDirector(){bReplicates=false;}
    bool Initialize(FString& Why);
    static bool Preview(const FLDInventory& Bag,const FLDCharacterBuild& Build,uint8 Recipe,uint8 Mode,uint8 Site,FGuid Target,FLDProductionPlan& Out,FString& Why);
    bool Terms(ALDPlayerController* PC,const FLDProductionPlan& Plan,bool Reserved,FString& Why) const;
    bool Commit(ALDPlayerController* PC,const FLDProductionPlan& Plan,FString& Why);
    void Purchase(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Token,uint8 Site,ELDItemKind Kind,ELDSkill Skill,uint32 BagRevision,const FString& Digest);
    double HoldSeconds(const FLDProductionPlan& Plan) const;
    bool Quiet(const FLDProductionPlan& Plan) const;
    void CacheView(ALDPlayerController* PC,uint8 Site);
    // B76 guardian calls this from its one paid defeat receipt; no artificial kill source.
    bool GuardianDrops(FVector Position,FGuid DefeatReceipt,FString& Why);
private:
    struct FClient {uint32 Last=0;double At=0,Tokens=4;TMap<uint32,FString> Receipts;};
    TMap<TWeakObjectPtr<ALDPlayerController>,FClient> Clients;
};
