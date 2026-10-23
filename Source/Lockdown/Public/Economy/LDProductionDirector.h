#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inventory/LDInventoryTypes.h"
#include "Development/LDCharacterBuild.h"
#include "LDProductionDirector.generated.h"
class ALDPlayerController;
class ALDCharacter;
enum class ELDSkillTaskPhase : uint8;
USTRUCT()
struct FLDProductionPlan
{
    GENERATED_BODY()
    UPROPERTY() uint8 Site=0;
    UPROPERTY() uint8 Recipe=0; // sites: two outcrops, City/West/East workshops
    UPROPERTY() uint32 RecipeRevision=1;
    UPROPERTY() uint32 InventoryRevision=0;
    UPROPERTY() uint32 BuildRevision=0;
    UPROPERTY() uint8 SkillSlot=255;
    UPROPERTY() int32 Fee=0;
    UPROPERTY() FGuid Coupon;
    UPROPERTY() TArray<FLDInventoryItem> Inputs;
};
// Server-only service anchor; observers receive only the existing sanitized view tuple.
UCLASS()
class LOCKDOWN_API ALDProductionSite:public AActor
{
    GENERATED_BODY()
public:
    ALDProductionSite();
    uint8 Site=0;
};
UCLASS()
class LOCKDOWN_API ALDProductionDirector:public AActor
{
    GENERATED_BODY()
public:
    ALDProductionDirector(){bReplicates=false;}
    void Initialize();
    static FVector Position(const UWorld* World,uint8 Site);
    static FString SiteName(uint8 Site);
    static FName Budget(uint8 Site);
    static ELDItemKind OutputKind(uint8 Recipe);
    static bool RecipeInputs(uint8 Recipe,const TArray<FLDInventoryItem>& Inputs);
    bool Eligible(ALDPlayerController* PC,uint8 Site,bool Quiet,bool AllowAction=false) const;
    void Queue(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Token,const FLDProductionPlan& Plan);
    void RefreshHold(ALDPlayerController* PC,uint32 Life,uint32 Token,uint32 Sequence,bool Moving);
    void Release(ALDPlayerController* PC,uint32 Life,uint32 Token);
    void Cancel(ALDPlayerController* PC,const FString& Reason=TEXT("Production cancelled; nothing spent"));
    void EndLife(ALDPlayerController* PC);
    void Sample(double Now);
    AActor* Target(uint8 Site) const {return Targets.IsValidIndex(Site)?Targets[Site].Get():nullptr;}
private:
    friend class ALDProductionProbe;
    struct FState
    {
        FLDProductionPlan Plan;
        uint32 Match=0,Life=0,Token=0,Sequence=0,LastRequest=0,Released=0;
        double Ends=0,LastHold=0,Damage=-1,Dealt=-1,RateAt=0,Tokens=3;
        FVector Anchor;
        TWeakObjectPtr<ALDCharacter> Body;
        bool Active=false;
        TMap<uint32,FString> Receipts;
    };
    struct FRequest {TWeakObjectPtr<ALDPlayerController> PC;uint32 Match,Life,Token;FLDProductionPlan Plan;};
    TMap<TWeakObjectPtr<ALDPlayerController>,FState> States;
    TArray<FRequest> Requests;
    UPROPERTY() TArray<TObjectPtr<AActor>> Targets;
    void Request(const FRequest& Request,double Now);
    bool Terms(ALDPlayerController* PC,const FLDProductionPlan& Plan,double Ends,bool Reserved,FString& Error) const;
    void Reserve(ALDPlayerController* PC,FState& State,bool Value);
    void Reply(ALDPlayerController* PC,uint32 Token,const FString& Reason);
    bool SkillUse(ALDCharacter* Body,uint8 Site,ELDSkill Skill,const FLDCharacterBuild& Build,uint32 Token,ELDSkillTaskPhase Phase,FString& Error);
};
