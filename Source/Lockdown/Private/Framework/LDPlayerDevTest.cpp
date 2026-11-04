#include "Framework/LDPlayerController.h"

#include "Economy/LDEconomyDirector.h"
#include "Engine/World.h"
#include "Framework/LDGameMode.h"
#include "HAL/IConsoleManager.h"

namespace
{
const TCHAR* DevTestOpLabel(ELDDevTestOp Op)
{
    switch (Op)
    {
    case ELDDevTestOp::GrantExperience: return TEXT("加经验");
    case ELDDevTestOp::GrantGold: return TEXT("加零花钱");
    case ELDDevTestOp::RefillResources: return TEXT("补满生命与法力");
    case ELDDevTestOp::ResetCooldowns: return TEXT("重置冷却");
    default: return TEXT("未知操作");
    }
}
}

uint64 ALDPlayerController::SubmitDevTest(ELDDevTestOp Op, int64 Amount, ELDGrowthReason& OutLocalReason)
{
    FLDDevTestCommand Command;
    Command.Op = Op;
    Command.Amount = Amount;
    // The shape is checked before a request id is reserved, so a malformed command consumes nothing.
    Command.RequestId = 1;
    OutLocalReason = LDDevTest::ValidateShape(Command);
    if (OutLocalReason != ELDGrowthReason::None)
    {
        return 0;
    }
    const uint64 RequestId = BeginGrowthRequest(DevTestCursor, OutLocalReason);
    if (RequestId == 0)
    {
        return 0;
    }
    Command.Generation = OwnerState.Match;
    Command.Life = OwnerState.Life;
    Command.RequestId = RequestId;
    ServerDevTestCommand(Command);
    return RequestId;
}

void ALDPlayerController::ServerDevTestCommand_Implementation(const FLDDevTestCommand& Command)
{
#if WITH_DEV_AUTOMATION_TESTS
    ALDGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ALDGameMode>() : nullptr;
    const FGuid Contender = Mode ? Mode->ContenderId(this) : FGuid();
    if (!Mode || !Mode->Economy || !Contender.IsValid())
    {
        ClientDevTestResult(LDDevTest::Refused(Command, ELDGrowthReason::DataNotReady,
            TEXT("The authority has no growth domain or this connection is not a contender of the current match.")));
        return;
    }
    // The command always acts on this connection's own contender; no field of it names another one.
    ClientDevTestResult(Mode->Economy->QueueDevTest(Contender, Command));
#else
    ClientDevTestResult(LDDevTest::Refused(Command, ELDGrowthReason::NotAuthority,
        TEXT("DEV test commands are compiled out of this build.")));
#endif
}

void ALDPlayerController::ClientDevTestResult_Implementation(const FLDDevTestResult& Result)
{
    SettleGrowthRequest(DevTestCursor, Result.Generation, Result.RequestId, Result.bSequenceReserved,
        Result.Status == ELDAdmissionStatus::Final);
    const ELDDevTestOp Op = Result.bHasReceipt ? Result.Receipt.Op : ELDDevTestOp::None;
    const FString& Detail = Result.bHasReceipt ? Result.Receipt.Detail : Result.Detail;
    UE_LOG(LogTemp, Display, TEXT("LD_DEV_TEST_RESULT match=%u request=%llu status=%u op=%s reason=%s detail=%s"),
        Result.Generation, Result.RequestId, uint32(Result.Status), LDDevTest::OpName(Op),
        LDGrowthWire::ReasonName(LDGrowthWire::Reason(Result.Reason)), *Detail);
    if (IsLocalController())
    {
        if (Result.Status == ELDAdmissionStatus::AdmissionRejected)
        {
            PostIntentNotice(FString::Printf(TEXT("测试模式：请求被拒绝（%s）"),
                LDGrowthWire::ReasonName(LDGrowthWire::Reason(Result.Reason))));
        }
        else if (Result.Status == ELDAdmissionStatus::Final && Result.bHasReceipt)
        {
            const bool bApplied = Result.Receipt.Header.Outcome == ELDReceiptOutcome::Applied;
            PostIntentNotice(bApplied
                ? FString::Printf(TEXT("测试模式：%s 已生效"), DevTestOpLabel(Op))
                : FString::Printf(TEXT("测试模式：%s 被拒绝（%s）"), DevTestOpLabel(Op),
                    LDGrowthWire::ReasonName(LDGrowthWire::Reason(Result.Receipt.Header.Reason))));
        }
    }
    OnDevTestResult.Broadcast(Result);
}

#if WITH_DEV_AUTOMATION_TESTS
// ---- DEV console commands: thin senders over SubmitDevTest; the authority decides every result ----
namespace
{
ALDPlayerController* DevTestLocalController(UWorld* World)
{
    if (!World)
    {
        return nullptr;
    }
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        ALDPlayerController* Controller = Cast<ALDPlayerController>(It->Get());
        if (Controller && Controller->IsLocalController())
        {
            return Controller;
        }
    }
    return nullptr;
}

// A positive decimal integer of at most 18 digits; anything else is refused rather than truncated.
bool DevTestParseAmount(const TArray<FString>& Args, int64& Out, FOutputDevice& Ar, const TCHAR* Usage)
{
    Out = 0;
    const FString Text = Args.Num() == 1 ? Args[0].TrimStartAndEnd() : FString();
    bool bDigits = !Text.IsEmpty() && Text.Len() <= 18;
    for (const TCHAR Char : Text)
    {
        bDigits = bDigits && FChar::IsDigit(Char);
    }
    if (!bDigits)
    {
        Ar.Logf(TEXT("LD_DEV_TEST_REFUSED usage: %s"), Usage);
        return false;
    }
    Out = FCString::Atoi64(*Text);
    return true;
}

void DevTestSend(UWorld* World, ELDDevTestOp Op, int64 Amount, FOutputDevice& Ar)
{
    ALDPlayerController* Controller = DevTestLocalController(World);
    if (!Controller)
    {
        Ar.Log(TEXT("LD_DEV_TEST_REFUSED this world has no local Lockdown player controller."));
        return;
    }
    ELDGrowthReason Reason = ELDGrowthReason::None;
    const uint64 RequestId = Controller->SubmitDevTest(Op, Amount, Reason);
    if (RequestId == 0)
    {
        Ar.Logf(TEXT("LD_DEV_TEST_REFUSED %s was not sent: %s"), LDDevTest::OpName(Op), LDGrowthWire::ReasonName(Reason));
        return;
    }
    Ar.Logf(TEXT("LD_DEV_TEST_SENT %s amount=%lld request=%llu; the result follows as LD_DEV_TEST_RESULT."),
        LDDevTest::OpName(Op), Amount, RequestId);
}

FAutoConsoleCommandWithWorldArgsAndOutputDevice GLDDevTestXP(
    TEXT("ld.Dev.XP"),
    TEXT("DEV test session: grant <amount> XP to your own contender (levels and opportunities as from a reward)."),
    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
        {
            int64 Amount = 0;
            if (DevTestParseAmount(Args, Amount, Ar, TEXT("ld.Dev.XP <positive amount>")))
            {
                DevTestSend(World, ELDDevTestOp::GrantExperience, Amount, Ar);
            }
        }));

FAutoConsoleCommandWithWorldArgsAndOutputDevice GLDDevTestGold(
    TEXT("ld.Dev.Gold"),
    TEXT("DEV test session: mint <amount> gold into your own account (ledger cause DevFixture)."),
    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
        {
            int64 Amount = 0;
            if (DevTestParseAmount(Args, Amount, Ar, TEXT("ld.Dev.Gold <positive amount>")))
            {
                DevTestSend(World, ELDDevTestOp::GrantGold, Amount, Ar);
            }
        }));

FAutoConsoleCommandWithWorldArgsAndOutputDevice GLDDevTestRefill(
    TEXT("ld.Dev.Refill"),
    TEXT("DEV test session: refill your living body's HP and MP to their committed maxima."),
    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
        {
            if (!Args.IsEmpty())
            {
                Ar.Log(TEXT("LD_DEV_TEST_REFUSED usage: ld.Dev.Refill (no arguments)"));
                return;
            }
            DevTestSend(World, ELDDevTestOp::RefillResources, 0, Ar);
        }));

FAutoConsoleCommandWithWorldArgsAndOutputDevice GLDDevTestResetCooldowns(
    TEXT("ld.Dev.ResetCooldowns"),
    TEXT("DEV test session: make every Q/W and gear cooldown of yours ready now."),
    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
        {
            if (!Args.IsEmpty())
            {
                Ar.Log(TEXT("LD_DEV_TEST_REFUSED usage: ld.Dev.ResetCooldowns (no arguments)"));
                return;
            }
            DevTestSend(World, ELDDevTestOp::ResetCooldowns, 0, Ar);
        }));
}
#endif
