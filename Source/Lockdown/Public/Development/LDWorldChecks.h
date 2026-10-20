#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Development/LDCharacterBuild.h"
#include "LDWorldChecks.generated.h"
class ALDPlayerController;
class ALDCharacter;
enum class ELDSkillTaskPhase : uint8;

namespace LDCharter
{
    constexpr double Threshold=12;
    inline ELDSkill Skill(uint8 Index){const ELDSkill Skills[]={ELDSkill::Heave,ELDSkill::Analyze,ELDSkill::Parley,ELDSkill::Survey};return Index<4?Skills[Index]:ELDSkill::None;}
    inline FVector Position(uint8 Index){const FVector Points[]={{-560,100,0},{-240,100,0},{-400,-60,0},{-400,260,0}};return Index<4?Points[Index]:FVector::ZeroVector;}
    inline const TCHAR* Name(uint8 Index){const TCHAR* Names[]={TEXT("Heavy goods"),TEXT("Civic mechanism"),TEXT("Negotiation"),TEXT("Field report")};return Index<4?Names[Index]:TEXT("Charter");}
}

UCLASS()
class LOCKDOWN_API ALDCharterTask : public AActor
{
    GENERATED_BODY()
public:
    ALDCharterTask();
    uint8 Service=0;
};

// Server-only match domain. No wallet identity, target resistance, seed or roll is replicated.
UCLASS()
class LOCKDOWN_API ALDWorldChecks : public AActor
{
    GENERATED_BODY()
public:
    ALDWorldChecks();
    virtual void BeginPlay() override;
    void EnsureTargets();
    FGuid WalletId(ALDCharacter* Body);
    void BindReturnedBody(ALDCharacter* Body,FGuid Wallet);
    void CancelAttempt(ALDCharacter* Source,uint32 Token);
    int64 Balance(ALDCharacter* Body);
    double RetryAt(ALDCharacter* Source,ALDCharacter* Target);
    double AlertUntil(ALDCharacter* Target);
    uint64 DrawCount() const {return Draws;}
    int32 WalletCount() const {return Wallets.Num();}
    bool UseWallet(ALDCharacter* Source,ALDCharacter* Target,const FLDCharacterBuild& Snapshot,uint32 Token,ELDSkillTaskPhase Phase,FString& Result);
    bool UseCharter(ALDCharacter* Source,ALDCharterTask* Task,const FLDCharacterBuild& Snapshot,ELDSkill Skill,uint32 Token,ELDSkillTaskPhase Phase,FString& Result);
#if !UE_BUILD_SHIPPING
    bool InjectTestRolls(const TArray<int32>& Rolls);
    bool SetTestBalance(ALDCharacter* Body,int64 Value);
#endif
private:
    struct FReceipt
    {
        FGuid Id,SourceBody,TargetWallet;
        TWeakObjectPtr<ALDCharacter> Source,Target;
        bool Committed=false,Success=false,Cancelled=false;
        int32 Roll=0;
        int64 Amount=0;
        FString Feedback;
    };
    struct FWallet
    {
        bool Human=false;
        TWeakObjectPtr<ALDPlayerController> Owner;
        int64 Gold=36;
        double Alert=0;
        TMap<FGuid,double> Retry;
        TMap<uint32,FReceipt> Receipts;
    };
    TMap<FGuid,FWallet> Wallets;
    TMap<TWeakObjectPtr<ALDCharacter>,FGuid> BodyWallets;
    FRandomStream Theft;
    uint64 Draws=0;
#if !UE_BUILD_SHIPPING
    TArray<int32> TestRolls;
#endif
    int64 Gold(const FWallet& Wallet) const;
    void SetGold(FWallet& Wallet,int64 Value);
    int32 Wisdom(ALDCharacter* Body) const;
    bool LiveWallet(ALDCharacter* Body,const FWallet& Wallet) const;
    int32 Draw();
};
