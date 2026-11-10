#include "Framework/LDPlayerController.h"
#include "Core/LDLog.h"

#include "Development/LDGrowthDirector.h"
#include "Engine/World.h"
#include "Framework/LDGameMode.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDDemoEntry.h"
#include "HAL/IConsoleManager.h"

uint64 ALDPlayerController::SubmitDevTest(ELDDevTestOp Op, int64 Amount, ELDGrowthReason& OutLocalReason, FName Definition,
    uint8 Slot, uint8 Stat)
{
    FLDDevTestCommand Command;
    Command.Op = Op;
    Command.Amount = Amount;
    Command.Slot = Slot;
    Command.Stat = Stat;
    if (!Definition.IsNone() && !FLDDefinitionWire::Encode(Definition, Command.Definition))
    {
        OutLocalReason = ELDGrowthReason::InvalidRequest;
        return 0;
    }
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
    if (!Mode || !Mode->Growth || !Contender.IsValid())
    {
        ClientDevTestResult(LDDevTest::Refused(Command, ELDGrowthReason::DataNotReady,
            TEXT("The authority has no growth domain or this connection is not a contender of the current match.")));
        return;
    }
    // The command always acts on this connection's own contender; no field of it names another one. On the server only the
    // Listen Host's own controller is local, which admits the match-wide ops.
    ClientDevTestResult(Mode->Growth->QueueDevTest(Contender, Command, IsLocalController()));
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
    UE_LOG(LogLDGrowth, Display, TEXT("LD_DEV_TEST_RESULT match=%u request=%llu status=%u op=%s reason=%s detail=%s"),
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
                ? FString::Printf(TEXT("测试模式：%s 已生效"), LDDevTest::OpLabel(Op))
                : FString::Printf(TEXT("测试模式：%s 被拒绝（%s）"), LDDevTest::OpLabel(Op),
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

bool DevTestParseAmount(const TArray<FString>& Args, int64& Out, FOutputDevice& Ar, const TCHAR* Usage)
{
    if (Args.Num() != 1 || !LDDevTest::ParseAmount(Args[0], Out))
    {
        Ar.Logf(TEXT("LD_DEV_TEST_REFUSED usage: %s"), Usage);
        return false;
    }
    return true;
}

void DevTestSend(UWorld* World, ELDDevTestOp Op, int64 Amount, FOutputDevice& Ar, FName Definition = NAME_None, uint8 Slot = 0,
    uint8 Stat = 0)
{
    ALDPlayerController* Controller = DevTestLocalController(World);
    if (!Controller)
    {
        Ar.Log(TEXT("LD_DEV_TEST_REFUSED this world has no local Lockdown player controller."));
        return;
    }
    ELDGrowthReason Reason = ELDGrowthReason::None;
    const uint64 RequestId = Controller->SubmitDevTest(Op, Amount, Reason, Definition, Slot, Stat);
    if (RequestId == 0)
    {
        Ar.Logf(TEXT("LD_DEV_TEST_REFUSED %s was not sent: %s"), LDDevTest::OpName(Op), LDGrowthWire::ReasonName(Reason));
        return;
    }
    Ar.Logf(TEXT("LD_DEV_TEST_SENT %s amount=%lld definition=%s slot=%u stat=%u request=%llu; the result follows as LD_DEV_TEST_RESULT."),
        LDDevTest::OpName(Op), Amount, *Definition.ToString(), uint32(Slot), uint32(Stat), RequestId);
}


// The approved runtime catalog this process loaded; the authority validates every command against its own copy.
TSharedPtr<const FLDDemoContentSnapshot> DevTestCatalog(FOutputDevice& Ar)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    FString Error;
    if (!LDDemoEntry::LoadRuntimeContent(Catalog, Error) || !Catalog)
    {
        Ar.Logf(TEXT("LD_DEV_TEST_REFUSED the approved Demo catalog is unavailable: %s"), *Error);
        return nullptr;
    }
    return Catalog;
}

FString DevTestDisplayName(const FLDDemoContentDefinitions& Definitions, const FString& Id)
{
    const FLDDemoDisplayDefinition* Display = Definitions.Display.FindByPredicate(
        [&Id](const FLDDemoDisplayDefinition& Row) { return Row.Id == Id; });
    return Display ? Display->NameText : FString(TEXT("(no display name)"));
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

FAutoConsoleCommandWithWorldArgsAndOutputDevice GLDDevTestList(
    TEXT("ld.Dev.List"),
    TEXT("DEV test session: list every active skill and stat id of the approved runtime catalog."),
    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
        {
            const TSharedPtr<const FLDDemoContentSnapshot> Catalog = DevTestCatalog(Ar);
            if (!Catalog)
            {
                return;
            }
            const FLDDemoContentDefinitions& Definitions = Catalog->Get();
            Ar.Log(TEXT("LD_DEV_TEST_LIST learn with ld.Dev.Learn <active> [Q|W|E], blueprint with ld.Dev.Blueprint <active>, "
                "allocate with ld.Dev.Points <stat> <amount>"));
            for (const FLDDemoAbilityDefinition& Ability : Definitions.Abilities)
            {
                Ar.Logf(TEXT("  active %s %s%s"), *Ability.Id, *DevTestDisplayName(Definitions, Ability.Id),
                    Ability.Opening ? TEXT(" (opening)") : TEXT(""));
            }
            for (const FLDDemoStatDefinition* Stat : Definitions.StatsInDesignOrder())
            {
                Ar.Logf(TEXT("  stat   %s %s"), *Stat->Id, *DevTestDisplayName(Definitions, Stat->Id));
            }
        }));

FAutoConsoleCommandWithWorldArgsAndOutputDevice GLDDevTestLearn(
    TEXT("ld.Dev.Learn"),
    TEXT("DEV test session: learn the active <id> now, in the lowest empty slot or replacing the active in [Q|W|E]."),
    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
        {
            const FString SlotText = Args.Num() == 2 ? Args[1].TrimStartAndEnd().ToUpper() : FString();
            const uint8 Slot = SlotText == TEXT("Q") ? 1 : SlotText == TEXT("W") ? 2 : SlotText == TEXT("E") ? 3 : 0;
            if (Args.IsEmpty() || Args.Num() > 2 || (Args.Num() == 2 && Slot == 0))
            {
                Ar.Log(TEXT("LD_DEV_TEST_REFUSED usage: ld.Dev.Learn <active id> [Q|W|E]"));
                return;
            }
            DevTestSend(World, ELDDevTestOp::LearnActive, 0, Ar, FName(*Args[0].TrimStartAndEnd()), Slot);
        }));

FAutoConsoleCommandWithWorldArgsAndOutputDevice GLDDevTestBlueprint(
    TEXT("ld.Dev.Blueprint"),
    TEXT("DEV test session: add the active <id> to your pending blueprints without an opportunity."),
    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
        {
            if (Args.Num() != 1)
            {
                Ar.Log(TEXT("LD_DEV_TEST_REFUSED usage: ld.Dev.Blueprint <active id>"));
                return;
            }
            DevTestSend(World, ELDDevTestOp::GrantBlueprint, 0, Ar, FName(*Args[0].TrimStartAndEnd()));
        }));

FAutoConsoleCommandWithWorldArgsAndOutputDevice GLDDevTestPoints(
    TEXT("ld.Dev.Points"),
    TEXT("DEV test session: allocate <amount> of your unspent points to the stat <id>, without the build safe window."),
    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
        {
            int64 Amount = 0;
            if (Args.Num() != 2 || !LDDevTest::ParseAmount(Args[1], Amount))
            {
                Ar.Log(TEXT("LD_DEV_TEST_REFUSED usage: ld.Dev.Points <stat id> <positive amount>"));
                return;
            }
            DevTestSend(World, ELDDevTestOp::AllocatePoints, Amount, Ar, FName(*Args[0].TrimStartAndEnd()));
        }));

FAutoConsoleCommandWithWorldArgsAndOutputDevice GLDDevTestAttr(
    TEXT("ld.Dev.Attr"),
    TEXT("DEV test session: set your DEV attribute term <MaxHP|MaxMP|AD|AP|Armor|MR|Haste|Range|AttackSpeed|MoveSpeed|Crit> "
        "<value> on top of your build (points, cm or percent; 0 removes it). A final value outside its bound is refused."),
    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
        {
            const LDDevTest::FStatField* Stat = nullptr;
            if (Args.Num() == 2)
            {
                for (const LDDevTest::FStatField& Row : LDDevTest::StatFields())
                {
                    if (Args[0].TrimStartAndEnd().Equals(Row.ConsoleName, ESearchCase::IgnoreCase))
                    {
                        Stat = &Row;
                    }
                }
            }
            int64 Amount = 0;
            if (!Stat || !LDDevTest::ParseScaled(Args[1], Stat->Decimals, Amount))
            {
                Ar.Log(TEXT("LD_DEV_TEST_REFUSED usage: ld.Dev.Attr <stat> <value>, value >= 0 with at most two decimals (Range: whole cm):"));
                for (const LDDevTest::FStatField& Row : LDDevTest::StatFields())
                {
                    Ar.Logf(TEXT("  %-11s %s"), Row.ConsoleName, Row.Unit);
                }
                return;
            }
            DevTestSend(World, ELDDevTestOp::SetAttribute, Amount, Ar, NAME_None, 0, Stat->Stat);
        }));

FAutoConsoleCommandWithWorldArgsAndOutputDevice GLDDevTestClearAttr(
    TEXT("ld.Dev.ClearAttr"),
    TEXT("DEV test session: remove every DEV attribute term of yours; your build itself stays."),
    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
        {
            if (!Args.IsEmpty())
            {
                Ar.Log(TEXT("LD_DEV_TEST_REFUSED usage: ld.Dev.ClearAttr (no arguments)"));
                return;
            }
            DevTestSend(World, ELDDevTestOp::ClearAttributes, 0, Ar);
        }));

FAutoConsoleCommandWithWorldArgsAndOutputDevice GLDDevTestReset(
    TEXT("ld.Dev.Reset"),
    TEXT("DEV test session: put yourself back to the match start: level 1, no XP, actives, blueprints, allocated points or "
        "DEV attributes, a fresh opening offer and every cooldown ready."),
    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
        {
            if (!Args.IsEmpty())
            {
                Ar.Log(TEXT("LD_DEV_TEST_REFUSED usage: ld.Dev.Reset (no arguments)"));
                return;
            }
            DevTestSend(World, ELDDevTestOp::ResetToMatchStart, 0, Ar);
        }));

FAutoConsoleCommandWithWorldArgsAndOutputDevice GLDDevTestRefreshCamps(
    TEXT("ld.Dev.RefreshCamps"),
    TEXT("DEV test session, Listen Host only: every cleared camp waiting for its refresh respawns at the next boundary."),
    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
        {
            if (!Args.IsEmpty())
            {
                Ar.Log(TEXT("LD_DEV_TEST_REFUSED usage: ld.Dev.RefreshCamps (no arguments)"));
                return;
            }
            DevTestSend(World, ELDDevTestOp::RefreshCamps, 0, Ar);
        }));

FAutoConsoleCommandWithWorldArgsAndOutputDevice GLDDevTestOpenCapture(
    TEXT("ld.Dev.OpenCapture"),
    TEXT("DEV test session, Listen Host only, development phase: open the capture circle now instead of after the full development time."),
    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
        {
            if (!Args.IsEmpty())
            {
                Ar.Log(TEXT("LD_DEV_TEST_REFUSED usage: ld.Dev.OpenCapture (no arguments)"));
                return;
            }
            DevTestSend(World, ELDDevTestOp::OpenCapture, 0, Ar);
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
    TEXT("DEV test session: make every Q/W/E and gear cooldown of yours ready now."),
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
