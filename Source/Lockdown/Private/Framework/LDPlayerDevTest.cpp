#include "Framework/LDPlayerController.h"

#include "Economy/LDEconomyDirector.h"
#include "Engine/World.h"
#include "Framework/LDGameMode.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDDemoEntry.h"
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
    case ELDDevTestOp::LearnOption: return TEXT("选择卡牌");
    case ELDDevTestOp::BuyItem: return TEXT("购买装备");
    case ELDDevTestOp::SellItem: return TEXT("出售装备");
    default: return TEXT("未知操作");
    }
}
}

uint64 ALDPlayerController::SubmitDevTest(ELDDevTestOp Op, int64 Amount, ELDGrowthReason& OutLocalReason, FName Definition,
    uint8 Slot)
{
    FLDDevTestCommand Command;
    Command.Op = Op;
    Command.Amount = Amount;
    Command.Slot = Slot;
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

void DevTestSend(UWorld* World, ELDDevTestOp Op, int64 Amount, FOutputDevice& Ar, FName Definition = NAME_None, uint8 Slot = 0)
{
    ALDPlayerController* Controller = DevTestLocalController(World);
    if (!Controller)
    {
        Ar.Log(TEXT("LD_DEV_TEST_REFUSED this world has no local Lockdown player controller."));
        return;
    }
    ELDGrowthReason Reason = ELDGrowthReason::None;
    const uint64 RequestId = Controller->SubmitDevTest(Op, Amount, Reason, Definition, Slot);
    if (RequestId == 0)
    {
        Ar.Logf(TEXT("LD_DEV_TEST_REFUSED %s was not sent: %s"), LDDevTest::OpName(Op), LDGrowthWire::ReasonName(Reason));
        return;
    }
    Ar.Logf(TEXT("LD_DEV_TEST_SENT %s amount=%lld definition=%s slot=%u request=%llu; the result follows as LD_DEV_TEST_RESULT."),
        LDDevTest::OpName(Op), Amount, *Definition.ToString(), uint32(Slot), RequestId);
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

FAutoConsoleCommandWithWorldArgsAndOutputDevice GLDDevTestList(
    TEXT("ld.Dev.List"),
    TEXT("DEV test session: list every skill, attribute card and item id of the approved runtime catalog."),
    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
        {
            const TSharedPtr<const FLDDemoContentSnapshot> Catalog = DevTestCatalog(Ar);
            if (!Catalog)
            {
                return;
            }
            const FLDDemoContentDefinitions& Definitions = Catalog->Get();
            Ar.Logf(TEXT("LD_DEV_TEST_LIST max skill rank %d; learn with ld.Dev.Learn <id> [Q|W], buy with ld.Dev.Buy <id>"),
                Definitions.Progression.MaxSkillRank);
            for (const FLDDemoAbilityDefinition& Ability : Definitions.Abilities)
            {
                Ar.Logf(TEXT("  %s %s %s"), Ability.Kind == ELDDemoAbilityKind::Active ? TEXT("active ") : TEXT("passive"),
                    *Ability.Id, *DevTestDisplayName(Definitions, Ability.Id));
            }
            for (const FLDDemoAttributeDefinition& Attribute : Definitions.Attributes)
            {
                Ar.Logf(TEXT("  card    %s %s"), *Attribute.Id, *DevTestDisplayName(Definitions, Attribute.Id));
            }
            for (const FLDDemoEquipmentDefinition& Item : Definitions.Equipment)
            {
                Ar.Logf(TEXT("  item    %s %s buy %lld sell %lld%s"), *Item.Id, *DevTestDisplayName(Definitions, Item.Id),
                    Item.BuyPrice, Item.SellGold, Item.HasActive ? TEXT(" (active)") : TEXT(""));
            }
        }));

FAutoConsoleCommandWithWorldArgsAndOutputDevice GLDDevTestLearn(
    TEXT("ld.Dev.Learn"),
    TEXT("DEV test session: take <id> (skill or attribute card) as your current choice; [Q|W] confirms replacing that active."),
    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
        {
            const FString SlotText = Args.Num() == 2 ? Args[1].TrimStartAndEnd().ToUpper() : FString();
            const uint8 Slot = SlotText == TEXT("Q") ? 1 : SlotText == TEXT("W") ? 2 : 0;
            if (Args.IsEmpty() || Args.Num() > 2 || (Args.Num() == 2 && Slot == 0))
            {
                Ar.Log(TEXT("LD_DEV_TEST_REFUSED usage: ld.Dev.Learn <skill or card id> [Q|W]"));
                return;
            }
            DevTestSend(World, ELDDevTestOp::LearnOption, 0, Ar, FName(*Args[0].TrimStartAndEnd()), Slot);
        }));

FAutoConsoleCommandWithWorldArgsAndOutputDevice GLDDevTestBuy(
    TEXT("ld.Dev.Buy"),
    TEXT("DEV test session: buy <item id> at its catalog price into your free equipment slot, without visiting the shop."),
    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
        {
            if (Args.Num() != 1)
            {
                Ar.Log(TEXT("LD_DEV_TEST_REFUSED usage: ld.Dev.Buy <item id>"));
                return;
            }
            DevTestSend(World, ELDDevTestOp::BuyItem, 0, Ar, FName(*Args[0].TrimStartAndEnd()));
        }));

FAutoConsoleCommandWithWorldArgsAndOutputDevice GLDDevTestSell(
    TEXT("ld.Dev.Sell"),
    TEXT("DEV test session: sell the item in equipment slot <1|2> at its catalog price, without visiting the shop."),
    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
        {
            const FString SlotText = Args.Num() == 1 ? Args[0].TrimStartAndEnd() : FString();
            if (SlotText != TEXT("1") && SlotText != TEXT("2"))
            {
                Ar.Log(TEXT("LD_DEV_TEST_REFUSED usage: ld.Dev.Sell <1|2>"));
                return;
            }
            DevTestSend(World, ELDDevTestOp::SellItem, 0, Ar, NAME_None, SlotText == TEXT("1") ? 1 : 2);
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
