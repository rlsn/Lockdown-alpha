#include "Development/LDProgressionRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentText.h"
#include "Development/LDDevGrowthFixture.h"
#include "Development/LDDevTestProtocol.h"
#include "Development/LDDevelopmentDirector.h"
#include "Development/LDGrowthDirector.h"
#include "Development/LDGrowthProtocol.h"
#include "Development/LDGrowthRequests.h"
#include "Development/LDSkillGrowth.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "UI/LDHUDModel.h"
#include "UI/LDTerms.h"
#include "UObject/Class.h"
#include "Tests/LDTestApprovedFixture.h"
#include "UObject/UnrealType.h"
#include "Vision/LDViewTypes.h"

namespace
{
bool GrowthTestReason(FAutomationTestBase& Test, const FString& What, ELDGrowthReason Actual, ELDGrowthReason Expected)
{
    return Test.TestEqual(What, LDGrowthWire::ReasonName(Actual), LDGrowthWire::ReasonName(Expected));
}

template <typename TEnum>
bool GrowthTestEnum(FAutomationTestBase& Test, const FString& What, TEnum Actual, TEnum Expected)
{
    return Test.TestEqual(What, UEnum::GetValueAsString(Actual), UEnum::GetValueAsString(Expected));
}

bool GrowthTestGuid(FAutomationTestBase& Test, const FString& What, const FGuid& Actual, const FGuid& Expected)
{
    return Test.TestEqual(What, Actual.ToString(), Expected.ToString());
}

// ---- Wire identities and command terms --------------------------------------------------------

// Raw bytes exactly as a client could send them: ASCII test text, at most 64 bytes, no terminator appended.
FLDDefinitionWire GrowthTestRawWire(const TCHAR* Text)
{
    FLDDefinitionWire RawBytes;
    for (int32 Index = 0; Index < int32(sizeof(RawBytes.Bytes)) && Text[Index] != 0; ++Index)
    {
        RawBytes.Bytes[Index] = uint8(Text[Index]);
    }
    return RawBytes;
}

template <typename TCommand, typename TMutation>
bool GrowthTestTermsDiffer(FAutomationTestBase& Test, const TCHAR* Family, const TCHAR* Field, const TCommand& Base, TMutation Mutate)
{
    TCommand Changed = Base;
    Mutate(Changed);
    return Test.TestFalse(FString::Printf(TEXT("%s SameTerms notices a different %s"), Family, Field),
        Base.SameTerms(Changed) || Changed.SameTerms(Base));
}

bool GrowthTestRunWire(FAutomationTestBase& Test)
{
    bool bGood = true;
    for (const TCHAR* IdText : {TEXT("attr_protein"), TEXT("camp"), TEXT("anchor_2")})
    {
        FLDDefinitionWire Encoded;
        FName Decoded;
        const bool bRoundTrip = FLDDefinitionWire::Encode(FName(IdText), Encoded) && Encoded.Decode(Decoded);
        bGood &= Test.TestTrue(FString::Printf(TEXT("%s encodes and decodes"), IdText), bRoundTrip);
        bGood &= Test.TestTrue(FString::Printf(TEXT("%s travels as its ASCII bytes, a terminator and zero padding"), IdText),
            Encoded == GrowthTestRawWire(IdText));
        bGood &= Test.TestTrue(FString::Printf(TEXT("%s decodes to the identical name"), IdText),
            Decoded == FName(IdText) && Decoded.ToString().Equals(IdText, ESearchCase::CaseSensitive));
    }

    const FString Longest = FString::ChrN(63, TEXT('a'));
    FLDDefinitionWire LongestWire;
    FName LongestName;
    bGood &= Test.TestTrue(TEXT("A 63-byte id is the longest that still fits before its terminator"),
        FLDDefinitionWire::Encode(FName(*Longest), LongestWire) && LongestWire.Bytes[62] == uint8('a') && LongestWire.Bytes[63] == 0
        && LongestWire.Decode(LongestName) && LongestName.ToString().Equals(Longest, ESearchCase::CaseSensitive));

    // FName identity ignores case and a packaged build returns the first registered casing ("Camp" for "camp");
    // the editor keeps each casing, so these variants reproduce that here.
    const TPair<const TCHAR*, const TCHAR*> CasedIds[] = {
        {TEXT("Camp"), TEXT("camp")},
        {TEXT("Growthtest_Upper"), TEXT("growthtest_upper")}};
    for (const TPair<const TCHAR*, const TCHAR*>& CasedId : CasedIds)
    {
        FLDDefinitionWire Canonical;
        FName CanonicalName;
        bGood &= Test.TestTrue(FString::Printf(TEXT("%s travels as the canonical bytes %s of the same name"), CasedId.Key, CasedId.Value),
            FLDDefinitionWire::Encode(FName(CasedId.Key), Canonical) && Canonical == GrowthTestRawWire(CasedId.Value)
            && Canonical.Decode(CanonicalName) && CanonicalName == FName(CasedId.Value)
            && CanonicalName.ToString().Equals(CasedId.Value, ESearchCase::CaseSensitive));
    }

    const TPair<const TCHAR*, FName> BadIds[] = {
        {TEXT("a leading digit"), FName(TEXT("2nd_card"))},
        {TEXT("a hyphen"), FName(TEXT("attr-protein"))},
        {TEXT("an empty name"), FName(TEXT(""))},
        {TEXT("NAME_None"), FName(NAME_None)},
        {TEXT("a 64-byte id"), FName(*FString::ChrN(64, TEXT('a')))}};
    for (const TPair<const TCHAR*, FName>& BadId : BadIds)
    {
        FLDDefinitionWire Cleared = GrowthTestRawWire(TEXT("stale_bytes"));
        bGood &= Test.TestFalse(FString::Printf(TEXT("Encode rejects %s"), BadId.Key), FLDDefinitionWire::Encode(BadId.Value, Cleared));
        bGood &= Test.TestTrue(FString::Printf(TEXT("A rejected encode of %s leaves only zero bytes"), BadId.Key),
            Cleared == FLDDefinitionWire());
    }

    FLDDefinitionWire Unterminated;
    FMemory::Memset(Unterminated.Bytes, uint8('a'), sizeof(Unterminated.Bytes));
    FLDDefinitionWire DirtyPadding = GrowthTestRawWire(TEXT("camp"));
    DirtyPadding.Bytes[5] = uint8('x');
    FLDDefinitionWire DirtyTail = GrowthTestRawWire(TEXT("camp"));
    DirtyTail.Bytes[63] = 1;
    const TPair<const TCHAR*, FLDDefinitionWire> BadWires[] = {
        {TEXT("64 non-zero bytes without a terminator"), Unterminated},
        {TEXT("a non-zero byte right after the terminator"), DirtyPadding},
        {TEXT("a non-zero last padding byte"), DirtyTail},
        {TEXT("an uppercase byte"), GrowthTestRawWire(TEXT("cAmp"))},
        {TEXT("a leading digit"), GrowthTestRawWire(TEXT("9camp"))},
        {TEXT("an empty buffer"), FLDDefinitionWire()}};
    for (const TPair<const TCHAR*, FLDDefinitionWire>& BadWire : BadWires)
    {
        FName Leftover(TEXT("camp"));
        bGood &= Test.TestFalse(FString::Printf(TEXT("Decode rejects %s"), BadWire.Key), BadWire.Value.Decode(Leftover));
        bGood &= Test.TestTrue(FString::Printf(TEXT("A rejected decode of %s yields NAME_None"), BadWire.Key), Leftover.IsNone());
    }

    bGood &= Test.TestFalse(TEXT("Unspawned cannot confirm a choice"), LDGrowthWire::IsChoicePhase(ELDLifePhase::Unspawned));
    bGood &= Test.TestTrue(TEXT("Alive can confirm a choice"), LDGrowthWire::IsChoicePhase(ELDLifePhase::Alive));
    bGood &= Test.TestTrue(TEXT("Dead can confirm a choice"), LDGrowthWire::IsChoicePhase(ELDLifePhase::Dead));
    bGood &= Test.TestFalse(TEXT("Left cannot confirm a choice"), LDGrowthWire::IsChoicePhase(ELDLifePhase::Left));

    FLDChoiceCommand Choice;
    Choice.Generation = 3;
    Choice.Life = 2;
    Choice.ExpectedLifePhase = ELDLifePhase::Dead;
    Choice.RequestId = 7;
    Choice.OfferId = FGuid(0x0FFE4000, 1, 2, 3);
    Choice.OfferRevision = 5;
    Choice.OptionId = FGuid(0x0B710000, 4, 5, 6);
    Choice.ExpectedBuildRevision = 9;
    Choice.CatalogDigest.Bytes[0] = 0xAB;
    Choice.bReplace = true;
    Choice.ReplaceSlot = 1;
    Choice.ExpectedOldSkillId = GrowthTestRawWire(TEXT("active_snark_bolt"));
    Choice.ExpectedOldRank = 2;
    Choice.bReplaceConfirmed = true;
    const TCHAR* ChoiceFamily = TEXT("Choice");
    bGood &= Test.TestTrue(TEXT("A copied choice command keeps its terms"), Choice.SameTerms(FLDChoiceCommand(Choice)));
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("Generation"), Choice, [](FLDChoiceCommand& Mutated) { ++Mutated.Generation; });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("Life"), Choice, [](FLDChoiceCommand& Mutated) { ++Mutated.Life; });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("ExpectedLifePhase"), Choice,
        [](FLDChoiceCommand& Mutated) { Mutated.ExpectedLifePhase = ELDLifePhase::Alive; });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("RequestId"), Choice, [](FLDChoiceCommand& Mutated) { ++Mutated.RequestId; });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("OfferId"), Choice,
        [](FLDChoiceCommand& Mutated) { Mutated.OfferId = FGuid(0x0FFE4000, 1, 2, 4); });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("OfferRevision"), Choice,
        [](FLDChoiceCommand& Mutated) { ++Mutated.OfferRevision; });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("OptionId"), Choice,
        [](FLDChoiceCommand& Mutated) { Mutated.OptionId = FGuid(0x0B710000, 4, 5, 7); });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("ExpectedBuildRevision"), Choice,
        [](FLDChoiceCommand& Mutated) { ++Mutated.ExpectedBuildRevision; });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("CatalogDigest"), Choice,
        [](FLDChoiceCommand& Mutated) { Mutated.CatalogDigest.Bytes[31] = 0x5A; });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("bReplace"), Choice,
        [](FLDChoiceCommand& Mutated) { Mutated.bReplace = false; });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("ReplaceSlot"), Choice,
        [](FLDChoiceCommand& Mutated) { Mutated.ReplaceSlot = 0; });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("ExpectedOldSkillId"), Choice,
        [](FLDChoiceCommand& Mutated) { Mutated.ExpectedOldSkillId = GrowthTestRawWire(TEXT("active_not_mad")); });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("ExpectedOldRank"), Choice,
        [](FLDChoiceCommand& Mutated) { Mutated.ExpectedOldRank = 1; });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("bReplaceConfirmed"), Choice,
        [](FLDChoiceCommand& Mutated) { Mutated.bReplaceConfirmed = false; });
    bGood &= GrowthTestTermsDiffer(Test, ChoiceFamily, TEXT("bReroll"), Choice,
        [](FLDChoiceCommand& Mutated) { Mutated.bReroll = true; });

    // #144: Choice is the only request family; the retired shop family number decodes to nothing.
    bGood &= Test.TestTrue(TEXT("Choice is a known growth family"), LDGrowthWire::IsKnownFamily(ELDGrowthFamily::Choice));
    bGood &= Test.TestFalse(TEXT("None is no growth family"), LDGrowthWire::IsKnownFamily(ELDGrowthFamily::None));
    bGood &= Test.TestFalse(TEXT("The retired shop family 2 is refused, not mapped to a neighbour"),
        LDGrowthWire::IsKnownFamily(static_cast<ELDGrowthFamily>(2)));
    return bGood;
}

// ---- Request streams --------------------------------------------------------------------------

FLDChoiceCommand GrowthTestChoiceCommand(uint64 RequestId)
{
    FLDChoiceCommand Command;
    Command.Generation = 4;
    Command.Life = 2;
    Command.ExpectedLifePhase = ELDLifePhase::Alive;
    Command.RequestId = RequestId;
    Command.OfferId = FGuid(0x0FFE4000, 0, 0, uint32(RequestId));
    Command.OfferRevision = RequestId;
    Command.OptionId = FGuid(0x0B710000, 0, 0, uint32(RequestId));
    Command.ExpectedBuildRevision = 1;
    return Command;
}

FLDChoiceReceipt GrowthTestChoiceReceipt(const FLDChoiceCommand& Command, ELDReceiptOutcome Outcome, ELDGrowthReason Reason)
{
    FLDChoiceReceipt Receipt;
    Receipt.Header.Generation = Command.Generation;
    Receipt.Header.Family = ELDGrowthFamily::Choice;
    Receipt.Header.RequestId = Command.RequestId;
    Receipt.Header.OriginalLife = Command.Life;
    Receipt.Header.OriginalLifePhase = Command.ExpectedLifePhase;
    Receipt.Header.Outcome = Outcome;
    Receipt.Header.Reason = LDGrowthWire::Raw(Reason);
    Receipt.OfferId = Command.OfferId;
    Receipt.OptionId = Command.OptionId;
    return Receipt;
}

struct FGrowthTestAdmission
{
    ELDAdmissionStatus Status = ELDAdmissionStatus::None;
    ELDGrowthReason Reason = ELDGrowthReason::None;
    const FLDChoiceReceipt* Final = nullptr;
    bool bReserved = false;
};

FGrowthTestAdmission GrowthTestAdmit(TLDGrowthRequestStream<FLDChoiceCommand, FLDChoiceReceipt>& Stream, const FLDChoiceCommand& Command,
    uint64 Arrival)
{
    FLDPendingReceiptInfo Info;
    Info.OriginalLife = Command.Life;
    Info.OriginalLifePhase = Command.ExpectedLifePhase;
    FGrowthTestAdmission Result;
    Result.Status = Stream.Admit(Command, Command.RequestId, Info, Arrival, Result.Reason, Result.Final, Result.bReserved);
    return Result;
}

bool GrowthTestExpectAdmission(FAutomationTestBase& Test, const FGrowthTestAdmission& Actual, ELDAdmissionStatus Status,
    ELDGrowthReason Reason, bool bReserved, const TCHAR* What)
{
    bool bGood = GrowthTestEnum(Test, FString::Printf(TEXT("%s: admission"), What), Actual.Status, Status);
    bGood &= GrowthTestReason(Test, FString::Printf(TEXT("%s: reason"), What), Actual.Reason, Reason);
    bGood &= Test.TestTrue(FString::Printf(TEXT("%s: sequence %s"), What, bReserved ? TEXT("reserved") : TEXT("not reserved")),
        Actual.bReserved == bReserved);
    bGood &= Test.TestTrue(FString::Printf(TEXT("%s: a receipt is returned only for a Final admission"), What),
        (Actual.Final != nullptr) == (Status == ELDAdmissionStatus::Final));
    return bGood;
}

bool GrowthTestPendingIs(FAutomationTestBase& Test, const TLDGrowthRequestStream<FLDChoiceCommand, FLDChoiceReceipt>& Stream,
    const FLDChoiceCommand& Command, uint64 Arrival, const TCHAR* What)
{
    const TLDGrowthRequestStream<FLDChoiceCommand, FLDChoiceReceipt>::FPending* Pending = Stream.GetPending();
    if (!Test.TestNotNull(FString::Printf(TEXT("%s: a request is pending"), What), Pending))
    {
        return false;
    }
    bool bGood = Test.TestEqual(FString::Printf(TEXT("%s: pending request id"), What), Stream.PendingRequestId(), Command.RequestId);
    bGood &= Test.TestTrue(FString::Printf(TEXT("%s: the pending terms are the admitted ones"), What), Pending->Command.SameTerms(Command));
    bGood &= Test.TestEqual(FString::Printf(TEXT("%s: pending arrival"), What), Pending->Arrival, Arrival);
    bGood &= Test.TestTrue(FString::Printf(TEXT("%s: pending original life and phase"), What),
        Pending->Info.OriginalLife == Command.Life && Pending->Info.OriginalLifePhase == Command.ExpectedLifePhase);
    return bGood;
}

bool GrowthTestRunRequests(FAutomationTestBase& Test)
{
    TLDGrowthRequestStream<FLDChoiceCommand, FLDChoiceReceipt> Stream;
    const FLDChoiceReceipt* Final = nullptr;
    const FLDPendingReceiptInfo* Info = nullptr;
    bool bGood = true;

    FGrowthTestAdmission Admission = GrowthTestAdmit(Stream, GrowthTestChoiceCommand(0), 100);
    bGood &= GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::InvalidRequest, false,
        TEXT("Request id 0"));
    bGood &= Test.TestNull(TEXT("Request id 0 leaves nothing pending"), Stream.GetPending());

    const FLDChoiceCommand First = GrowthTestChoiceCommand(1);
    Admission = GrowthTestAdmit(Stream, First, 101);
    if (!GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::AcceptedPending, ELDGrowthReason::None, true, TEXT("Request 1"))
        || !GrowthTestPendingIs(Test, Stream, First, 101, TEXT("After admitting request 1")))
    {
        return false;
    }
    Admission = GrowthTestAdmit(Stream, First, 102);
    bGood &= GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::AcceptedPending, ELDGrowthReason::None, true,
        TEXT("Request 1 resent with the same terms"));
    bGood &= GrowthTestPendingIs(Test, Stream, First, 101, TEXT("After the identical resend"));
    FLDChoiceCommand ChangedFirst = First;
    ChangedFirst.OptionId = FGuid(0x0B710000, 9, 9, 9);
    Admission = GrowthTestAdmit(Stream, ChangedFirst, 103);
    bGood &= GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::RequestConflict, false,
        TEXT("Request 1 resent with another option"));
    bGood &= GrowthTestPendingIs(Test, Stream, First, 101, TEXT("After the conflicting resend"));
    Admission = GrowthTestAdmit(Stream, GrowthTestChoiceCommand(2), 104);
    bGood &= GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::RequestInFlight, false,
        TEXT("Request 2 while request 1 is pending"));
    bGood &= GrowthTestPendingIs(Test, Stream, First, 101, TEXT("After the in-flight refusal"));
    bGood &= GrowthTestEnum(Test, TEXT("Lookup of pending request 1"), Stream.Lookup(1, Final, Info), ELDReceiptLookup::Pending);
    bGood &= Test.TestTrue(TEXT("A pending lookup reports only the original life and phase"),
        Info && !Final && Info->OriginalLife == First.Life && Info->OriginalLifePhase == First.ExpectedLifePhase);

    if (!Test.TestEqual(TEXT("Request 1 is pending before it finalizes"), Stream.PendingRequestId(), uint64(1)))
    {
        return false;
    }
    Stream.Finalize(GrowthTestChoiceReceipt(First, ELDReceiptOutcome::Rejected, ELDGrowthReason::OfferGuardStale));
    bGood &= Test.TestNull(TEXT("Finalizing request 1 clears the pending slot"), Stream.GetPending());
    bGood &= Test.TestEqual(TEXT("Request 1 is finalized"), Stream.GetFinalizedThrough(), uint64(1));
    Admission = GrowthTestAdmit(Stream, GrowthTestChoiceCommand(3), 105);
    bGood &= GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::OutOfOrder, false,
        TEXT("Request 3 skipping request 2"));
    Admission = GrowthTestAdmit(Stream, First, 106);
    bGood &= GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::Final, ELDGrowthReason::None, true,
        TEXT("Replaying finalized request 1"));
    bGood &= Test.TestTrue(TEXT("The replay returns the stored rejected receipt instead of executing again"),
        Admission.Final && Admission.Final->Header.RequestId == 1 && Admission.Final->Header.Outcome == ELDReceiptOutcome::Rejected
        && LDGrowthWire::Reason(Admission.Final->Header.Reason) == ELDGrowthReason::OfferGuardStale
        && Admission.Final->OptionId == First.OptionId);
    bGood &= Test.TestNull(TEXT("A replay does not reopen the request"), Stream.GetPending());
    Admission = GrowthTestAdmit(Stream, ChangedFirst, 107);
    bGood &= GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::RequestConflict, false,
        TEXT("Finalized request 1 with another option"));
    bGood &= GrowthTestEnum(Test, TEXT("Lookup of finalized request 1"), Stream.Lookup(1, Final, Info), ELDReceiptLookup::Final);
    bGood &= Test.TestTrue(TEXT("A final lookup returns the stored receipt"),
        Final && !Info && Final->Header.RequestId == 1 && Final->OfferId == First.OfferId);
    bGood &= GrowthTestEnum(Test, TEXT("Lookup of request 2 before it is admitted"), Stream.Lookup(2, Final, Info),
        ELDReceiptLookup::NotFound);
    bGood &= GrowthTestEnum(Test, TEXT("Lookup of request id 0"), Stream.Lookup(0, Final, Info), ELDReceiptLookup::NotFound);
    bGood &= Test.TestTrue(TEXT("A NotFound lookup carries nothing"), !Final && !Info);

    const int32 Capacity = TLDGrowthRequestStream<FLDChoiceCommand, FLDChoiceReceipt>::WindowCapacity;
    for (uint64 RequestId = 2; RequestId <= uint64(Capacity) + 1; ++RequestId)
    {
        const FLDChoiceCommand Ordered = GrowthTestChoiceCommand(RequestId);
        const FGrowthTestAdmission InOrder = GrowthTestAdmit(Stream, Ordered, 200 + RequestId);
        if (!Test.TestTrue(FString::Printf(TEXT("Request %llu is admitted in order"), RequestId),
            InOrder.Status == ELDAdmissionStatus::AcceptedPending && Stream.PendingRequestId() == RequestId))
        {
            return false;
        }
        Stream.Finalize(GrowthTestChoiceReceipt(Ordered, ELDReceiptOutcome::Applied, ELDGrowthReason::None));
    }
    bGood &= Test.TestEqual(TEXT("65 requests are finalized"), Stream.GetFinalizedThrough(), uint64(Capacity) + 1);
    Admission = GrowthTestAdmit(Stream, First, 300);
    bGood &= GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::ReceiptExpired, false,
        TEXT("Request 1 after 65 finalized requests"));
    bGood &= GrowthTestEnum(Test, TEXT("Lookup of request 1 outside the 64-receipt window"), Stream.Lookup(1, Final, Info),
        ELDReceiptLookup::ReceiptExpired);
    bGood &= Test.TestTrue(TEXT("An expired lookup carries no receipt"), !Final && !Info);
    bGood &= GrowthTestEnum(Test, TEXT("Lookup of request 2, the oldest receipt still in the window"), Stream.Lookup(2, Final, Info),
        ELDReceiptLookup::Final);
    Admission = GrowthTestAdmit(Stream, GrowthTestChoiceCommand(2), 301);
    bGood &= GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::Final, ELDGrowthReason::None, true,
        TEXT("Replaying request 2 inside the window"));
    bGood &= GrowthTestEnum(Test, TEXT("Lookup of request 66 before it is admitted"), Stream.Lookup(66, Final, Info),
        ELDReceiptLookup::NotFound);
    return bGood;
}

// ---- Progression rules ------------------------------------------------------------------------

// Reads the producer-exported approved_base only; there is no live-data fallback.
bool GrowthTestReadFixture(FAutomationTestBase& Test, TSharedPtr<const FLDDemoContentSnapshot>& OutCatalog)
{
    if (!LDTestApprovedFixture::Read(Test, OutCatalog))
    {
        return false;
    }
    const FLDDemoContentParameters& Tuning = OutCatalog->Get().Parameters;
    const FLDDemoProgressionDefinition& Curve = OutCatalog->Get().Progression;
    const TArray<int64> ApprovedThresholds = {0, 80, 180, 300, 440, 600, 780, 980, 1200, 1440,
        1700, 1980, 2280, 2600, 2940, 3300};
    bool bApproved = Test.TestTrue(TEXT("Fixture carries the approved 16-level cumulative XP thresholds"),
        Curve.XPStart == 0 && Curve.XPThresholds == ApprovedThresholds);
    bApproved &= Test.TestTrue(TEXT("Fixture carries MaxLevel/MaxChoices 16, MaxSkillRank 2 and three active slots"),
        Curve.MaxLevel == 16 && Curve.MaxChoices == 16 && Curve.MaxSkillRank == 2 && Curve.ActiveSlots == 3);
    bApproved &= Test.TestTrue(TEXT("Fixture carries REWARD.PlayerXP 100 + 20/level"),
        Tuning.PlayerXPBase == 100 && Tuning.PlayerXPPerLevel == 20);
    return bApproved;
}

FLDChoiceOption GrowthTestOption(ELDChoiceOptionKind Kind, FName DefinitionId, uint8 FromRank, uint8 ToRank)
{
    FLDChoiceOption Result;
    Result.Kind = Kind;
    Result.DefinitionId = DefinitionId;
    Result.FromRank = FromRank;
    Result.ToRank = ToRank;
    Result.AttributeQuality = Kind == ELDChoiceOptionKind::Attribute ? ELDAttributeQuality::Blue : ELDAttributeQuality::None;
    return Result;
}

bool GrowthTestPoolHas(const TArray<LDProgression::FLegalOption>& Pool, ELDChoiceOptionKind Kind, FName DefinitionId, uint8 FromRank,
    uint8 ToRank)
{
    return Pool.ContainsByPredicate([&](const LDProgression::FLegalOption& Row)
    {
        return Row.Kind == Kind && Row.DefinitionId == DefinitionId && Row.FromRank == FromRank && Row.ToRank == ToRank;
    });
}

bool GrowthTestPoolMentions(const TArray<LDProgression::FLegalOption>& Pool, FName DefinitionId)
{
    return Pool.ContainsByPredicate([&](const LDProgression::FLegalOption& Row) { return Row.DefinitionId == DefinitionId; });
}

int32 GrowthTestKindCount(const TArray<LDProgression::FLegalOption>& Pool, ELDChoiceOptionKind Kind)
{
    int32 Count = 0;
    for (const LDProgression::FLegalOption& Row : Pool)
    {
        Count += Row.Kind == Kind ? 1 : 0;
    }
    return Count;
}

bool GrowthTestPoolSorted(const TArray<LDProgression::FLegalOption>& Pool)
{
    for (int32 Index = 1; Index < Pool.Num(); ++Index)
    {
        const LDProgression::FLegalOption& Earlier = Pool[Index - 1];
        const LDProgression::FLegalOption& Later = Pool[Index];
        const int32 Comparison = Earlier.DefinitionId.ToString().Compare(Later.DefinitionId.ToString(), ESearchCase::CaseSensitive);
        if (Comparison > 0 || (Comparison == 0 && uint8(Earlier.Kind) >= uint8(Later.Kind)))
        {
            return false;
        }
    }
    return true;
}

bool GrowthTestSameOptions(const TArray<LDProgression::FLegalOption>& A, const TArray<LDProgression::FLegalOption>& B)
{
    if (A.Num() != B.Num())
    {
        return false;
    }
    for (int32 Index = 0; Index < A.Num(); ++Index)
    {
        if (A[Index].Kind != B[Index].Kind || A[Index].DefinitionId != B[Index].DefinitionId || A[Index].FromRank != B[Index].FromRank
            || A[Index].ToRank != B[Index].ToRank || A[Index].HiddenActive != B[Index].HiddenActive)
        {
            return false;
        }
    }
    return true;
}

bool GrowthTestBuildSorted(const FLDGrowthBuild& Build)
{
    for (int32 Index = 1; Index < Build.Skills.Num(); ++Index)
    {
        if (Build.Skills[Index - 1].Id.ToString().Compare(Build.Skills[Index].Id.ToString(), ESearchCase::CaseSensitive) >= 0)
        {
            return false;
        }
    }
    for (int32 Index = 1; Index < Build.Cards.Num(); ++Index)
    {
        if (Build.Cards[Index - 1].Id.ToString().Compare(Build.Cards[Index].Id.ToString(), ESearchCase::CaseSensitive) >= 0)
        {
            return false;
        }
    }
    return true;
}

// Only an expected success replaces Build, so an unexpected outcome cannot leak into later steps.
bool GrowthTestApply(FAutomationTestBase& Test, const FLDDemoContentSnapshot& Catalog, FLDGrowthBuild& Build,
    const FLDChoiceOption& ChoiceOption, const LDProgression::FReplacement* Replacement, ELDGrowthReason Expected,
    LDProgression::FAppliedOption& Applied, const TCHAR* What)
{
    FLDGrowthBuild Candidate;
    const ELDGrowthReason Outcome = LDProgression::ApplyOption(Catalog, Build, ChoiceOption, Replacement, Candidate, Applied);
    if (!GrowthTestReason(Test, FString::Printf(TEXT("%s: outcome"), What), Outcome, Expected))
    {
        return false;
    }
    if (Outcome != ELDGrowthReason::None)
    {
        return true;
    }
    const bool bSorted = Test.TestTrue(FString::Printf(TEXT("%s: build rows stay sorted by id"), What), GrowthTestBuildSorted(Candidate));
    Build = MoveTemp(Candidate);
    return bSorted;
}

bool GrowthTestExpectSkill(FAutomationTestBase& Test, const FLDGrowthBuild& Build, FName SkillId, uint8 ExpectedRank, bool bActive,
    uint8 ExpectedSlot, const TCHAR* What)
{
    const FLDLearnedSkill* Learned = Build.FindSkill(SkillId);
    if (!Test.TestNotNull(FString::Printf(TEXT("%s: %s is learned"), What, *SkillId.ToString()), Learned))
    {
        return false;
    }
    bool bGood = Test.TestEqual(FString::Printf(TEXT("%s: %s rank"), What, *SkillId.ToString()), int32(Learned->Rank), int32(ExpectedRank));
    bGood &= Test.TestTrue(FString::Printf(TEXT("%s: %s is %s"), What, *SkillId.ToString(), bActive ? TEXT("active") : TEXT("passive")),
        Learned->bActive == bActive);
    if (bActive)
    {
        bGood &= Test.TestEqual(FString::Printf(TEXT("%s: %s slot"), What, *SkillId.ToString()), int32(Learned->Slot), int32(ExpectedSlot));
    }
    return bGood;
}

FName GrowthTestActiveIn(const FLDGrowthBuild& Build, uint8 Slot)
{
    const int32 Index = Build.ActiveInSlot(Slot);
    return Build.Skills.IsValidIndex(Index) ? Build.Skills[Index].Id : NAME_None;
}

int32 GrowthTestCardCount(const FLDGrowthBuild& Build, FName CardId)
{
    int32 Total = 0;
    for (const FLDAttributeCardCount& Card : Build.Cards)
    {
        if (Card.Id == CardId) { Total += Card.Count; }
    }
    return Total;
}

bool GrowthTestPlan(FAutomationTestBase& Test, const FLDDemoProgressionDefinition& Curve, int64 CurrentXP, uint8 CurrentLevel, int64 Grant,
    int32 ExpectedLevel, int32 ExpectedEarnedThrough, const TCHAR* What)
{
    LDProgression::FExperienceChange Change;
    const ELDGrowthReason Outcome = LDProgression::PlanExperience(Curve, CurrentXP, CurrentLevel, CurrentLevel, Grant, Change);
    bool bGood = GrowthTestReason(Test, FString::Printf(TEXT("%s: outcome"), What), Outcome, ELDGrowthReason::None);
    bGood &= Test.TestEqual(FString::Printf(TEXT("%s: cumulative XP"), What), Change.NewXP, CurrentXP + Grant);
    bGood &= Test.TestEqual(FString::Printf(TEXT("%s: level before"), What), int32(Change.LevelBefore), int32(CurrentLevel));
    bGood &= Test.TestEqual(FString::Printf(TEXT("%s: level after"), What), int32(Change.LevelAfter), ExpectedLevel);
    bGood &= Test.TestEqual(FString::Printf(TEXT("%s: opportunities earned through"), What), int32(Change.EarnedThroughAfter),
        ExpectedEarnedThrough);
    return bGood;
}

bool GrowthTestRunProgression(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(Test, Catalog))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Approved = Catalog->Get();
    const FLDDemoProgressionDefinition& Curve = Approved.Progression;
    bool bGood = true;

    const TPair<int64, int32> LevelCases[] = {{0, 1}, {79, 1}, {80, 2}, {180, 3}, {1439, 9}, {1440, 10},
        {3299, 15}, {3300, 16}, {MAX_int64, 16}};
    for (const TPair<int64, int32>& LevelCase : LevelCases)
    {
        uint8 Level = 0;
        const ELDGrowthReason Outcome = LDProgression::LevelForXP(Curve, LevelCase.Key, Level);
        bGood &= GrowthTestReason(Test, FString::Printf(TEXT("LevelForXP(%lld) outcome"), LevelCase.Key), Outcome, ELDGrowthReason::None);
        bGood &= Test.TestEqual(FString::Printf(TEXT("Level at %lld cumulative XP"), LevelCase.Key), int32(Level), LevelCase.Value);
    }
    uint8 RefusedLevel = 5;
    bGood &= GrowthTestReason(Test, TEXT("LevelForXP(-1)"), LDProgression::LevelForXP(Curve, -1, RefusedLevel),
        ELDGrowthReason::DataInvalid);
    bGood &= Test.TestEqual(TEXT("A refused LevelForXP reports no level"), int32(RefusedLevel), 0);

    // The opening opportunity is ordinal 1, so a contender holds as many opportunities as its level.
    bGood &= GrowthTestPlan(Test, Curve, 0, 1, 79, 1, 1, TEXT("79 XP at level 1 keeps only the opening opportunity"));
    bGood &= GrowthTestPlan(Test, Curve, 0, 1, 80, 2, 2, TEXT("80 XP at level 1"));
    bGood &= GrowthTestPlan(Test, Curve, 0, 1, 1440, 10, 10, TEXT("1440 XP at level 1"));
    bGood &= GrowthTestPlan(Test, Curve, 1440, 10, 1000, 13, 13, TEXT("1000 XP at level 10 reaches level 13"));
    LDProgression::FExperienceChange Change;
    bGood &= GrowthTestReason(Test, TEXT("1 XP past the int64 bound"), LDProgression::PlanExperience(Curve, MAX_int64, 16, 16, 1, Change),
        ELDGrowthReason::ArithmeticOverflow);
    bGood &= Test.TestTrue(TEXT("An overflowing plan carries no partial result"), Change.NewXP == 0 && Change.LevelAfter == 0);
    bGood &= GrowthTestReason(Test, TEXT("80 XP recorded as level 1"), LDProgression::PlanExperience(Curve, 80, 1, 1, 20, Change),
        ELDGrowthReason::DataInvalid);
    bGood &= GrowthTestReason(Test, TEXT("Level 1 without its opening opportunity"),
        LDProgression::PlanExperience(Curve, 0, 1, 0, 80, Change), ELDGrowthReason::DataInvalid);
    bGood &= GrowthTestReason(Test, TEXT("Level 2 without its earned opportunity"),
        LDProgression::PlanExperience(Curve, 80, 2, 1, 20, Change), ELDGrowthReason::DataInvalid);
    bGood &= GrowthTestReason(Test, TEXT("A negative XP grant"), LDProgression::PlanExperience(Curve, 0, 1, 1, -1, Change),
        ELDGrowthReason::DataInvalid);

    const uint8 MaxLevel = uint8(Curve.MaxLevel);
    LDProgression::FRewardAmounts Reward;
    bGood &= GrowthTestReason(Test, TEXT("A level 1 contender kill"),
        LDProgression::ContenderKillReward(Approved.Parameters, MaxLevel, 1, Reward), ELDGrowthReason::None);
    bGood &= Test.TestEqual(TEXT("Level 1 contender kill XP"), Reward.XP, int64(100));
    bGood &= GrowthTestReason(Test, TEXT("A level 10 contender kill"),
        LDProgression::ContenderKillReward(Approved.Parameters, MaxLevel, 10, Reward), ELDGrowthReason::None);
    bGood &= Test.TestEqual(TEXT("Level 10 contender kill XP"), Reward.XP, int64(280));
    bGood &= GrowthTestReason(Test, TEXT("A level 16 contender kill"),
        LDProgression::ContenderKillReward(Approved.Parameters, MaxLevel, 16, Reward), ELDGrowthReason::None);
    bGood &= Test.TestEqual(TEXT("Level 16 contender kill XP"), Reward.XP, int64(400));
    for (const uint8 VictimLevel : {uint8(0), uint8(17)})
    {
        bGood &= GrowthTestReason(Test, FString::Printf(TEXT("A level %d contender kill"), VictimLevel),
            LDProgression::ContenderKillReward(Approved.Parameters, MaxLevel, VictimLevel, Reward), ELDGrowthReason::DataInvalid);
        bGood &= Test.TestTrue(FString::Printf(TEXT("A refused level %d kill grants nothing"), VictimLevel),
            Reward.XP == 0);
    }
    bGood &= GrowthTestReason(Test, TEXT("A monster_scavenger kill"),
        LDProgression::MonsterKillReward(*Catalog, FName(TEXT("monster_scavenger")), 0, Reward), ELDGrowthReason::None);
    bGood &= Test.TestEqual(TEXT("monster_scavenger XP"), Reward.XP, int64(20));
    bGood &= GrowthTestReason(Test, TEXT("A monster_guardian kill"),
        LDProgression::MonsterKillReward(*Catalog, FName(TEXT("monster_guardian")), 0, Reward), ELDGrowthReason::None);
    bGood &= Test.TestEqual(TEXT("monster_guardian XP"), Reward.XP, int64(80));
    bGood &= GrowthTestReason(Test, TEXT("A monster_advisor kill"),
        LDProgression::MonsterKillReward(*Catalog, FName(TEXT("monster_advisor")), 0, Reward), ELDGrowthReason::None);
    bGood &= Test.TestEqual(TEXT("monster_advisor XP"), Reward.XP, int64(180));
    // #97 camp growth: +10% of the base XP per stage that life spawned with, floored.
    for (const auto& [Monster, Stage, XP] : TArray<TTuple<const TCHAR*, uint8, int64>>{
        {TEXT("monster_scavenger"), 3, 26}, {TEXT("monster_guardian"), 5, 120},
        {TEXT("monster_advisor"), 10, 360}, {TEXT("monster_scavenger"), 10, 40}})
    {
        bGood &= GrowthTestReason(Test, FString::Printf(TEXT("A stage %u %s kill"), Stage, Monster),
            LDProgression::MonsterKillReward(*Catalog, FName(Monster), Stage, Reward), ELDGrowthReason::None);
        bGood &= Test.TestEqual(FString::Printf(TEXT("Stage %u %s pays %lld XP"), Stage, Monster, XP), Reward.XP, XP);
    }
    bGood &= GrowthTestReason(Test, TEXT("A camp kill past the growth cap"),
        LDProgression::MonsterKillReward(*Catalog, FName(TEXT("monster_guardian")), 11, Reward), ELDGrowthReason::DataInvalid);
    bGood &= GrowthTestReason(Test, TEXT("An unknown monster kill"),
        LDProgression::MonsterKillReward(*Catalog, FName(TEXT("monster_unknown")), 0, Reward), ELDGrowthReason::DataInvalid);

    FLDGrowthBuild EmptyBuild;
    TArray<LDProgression::FLegalOption> Pool;
    if (!GrowthTestReason(Test, TEXT("The legal pool of an empty level 1 build"), LDProgression::LegalPool(*Catalog, EmptyBuild, Pool),
        ELDGrowthReason::None))
    {
        return false;
    }
    // The empty build may take every catalog definition once: each active, each passive and each attribute card.
    // The expected pool is read from the approved catalog, so new content needs no edit here.
    TArray<TPair<ELDChoiceOptionKind, FString>> ApprovedPool;
    int32 Actives = 0, Passives = 0;
    for (const FLDDemoAbilityDefinition& Ability : Approved.Abilities)
    {
        const bool bActive = Ability.Kind == ELDDemoAbilityKind::Active;
        (bActive ? Actives : Passives) += 1;
        ApprovedPool.Emplace(bActive ? ELDChoiceOptionKind::LearnActive : ELDChoiceOptionKind::LearnPassive, Ability.Id);
    }
    for (const FLDDemoAttributeDefinition& Attribute : Approved.Attributes)
    {
        ApprovedPool.Emplace(ELDChoiceOptionKind::Attribute, Attribute.Id);
    }
    ApprovedPool.Sort([](const TPair<ELDChoiceOptionKind, FString>& A, const TPair<ELDChoiceOptionKind, FString>& B)
        { return A.Value.Compare(B.Value, ESearchCase::CaseSensitive) < 0; });
    bGood &= Test.TestTrue(TEXT("The approved catalog has actives, passives and attribute cards"),
        Actives > 0 && Passives > 0 && Approved.Attributes.Num() > 0);
    bGood &= Test.TestEqual(TEXT("An empty build may learn every active"),
        GrowthTestKindCount(Pool, ELDChoiceOptionKind::LearnActive), Actives);
    bGood &= Test.TestEqual(TEXT("An empty build may learn every passive"),
        GrowthTestKindCount(Pool, ELDChoiceOptionKind::LearnPassive), Passives);
    bGood &= Test.TestEqual(TEXT("An empty build may take every attribute card"),
        GrowthTestKindCount(Pool, ELDChoiceOptionKind::Attribute), Approved.Attributes.Num());
    if (!Test.TestEqual(TEXT("The empty build's legal pool holds every catalog definition once"), Pool.Num(), ApprovedPool.Num()))
    {
        return false;
    }
    TSet<FName> PoolIds;
    for (int32 Index = 0; Index < Pool.Num(); ++Index)
    {
        const LDProgression::FLegalOption& Row = Pool[Index];
        PoolIds.Add(Row.DefinitionId);
        bGood &= Test.TestTrue(FString::Printf(TEXT("Pool row %d is %s from rank 0 to 1"), Index, *ApprovedPool[Index].Value),
            Row.Kind == ApprovedPool[Index].Key && Row.DefinitionId.ToString().Equals(ApprovedPool[Index].Value, ESearchCase::CaseSensitive)
            && Row.FromRank == 0 && Row.ToRank == 1);
    }
    bGood &= Test.TestTrue(TEXT("The pool is sorted by definition id, then kind"), GrowthTestPoolSorted(Pool));
    bGood &= Test.TestEqual(TEXT("No definition appears twice in the pool"), PoolIds.Num(), Pool.Num());

    const FGuid MatchSecret(0x5EC2E700, 1, 2, 3);
    const FGuid Contender(0x0000A001, 0, 0, 1);
    const uint64 Seed = LDProgression::OfferSeed(MatchSecret, Contender, 1);
    bGood &= Test.TestTrue(TEXT("Retrying opportunity 1 reuses its seed"), Seed == LDProgression::OfferSeed(MatchSecret, Contender, 1));
    bGood &= Test.TestTrue(TEXT("Opportunity 2 draws from another seed"), Seed != LDProgression::OfferSeed(MatchSecret, Contender, 2));
    bGood &= Test.TestTrue(TEXT("Another contender's opportunity 1 draws from another seed"),
        Seed != LDProgression::OfferSeed(MatchSecret, FGuid(0x0000A002, 0, 0, 2), 1));
    bGood &= Test.TestTrue(TEXT("The reroll of an opportunity draws from its own seed"),
        LDProgression::RerollSeed(MatchSecret, Contender, 1) != Seed
        && LDProgression::RerollSeed(MatchSecret, Contender, 1) == LDProgression::RerollSeed(MatchSecret, Contender, 1));
    // A level-up draw over the empty build's pool (112 §6 composition is covered by DemoGrowthDraft).
    LDProgression::FDrawInput Input;
    Input.Content = &Catalog->Get();
    Input.Build = &EmptyBuild;
    Input.Ordinal = 2;
    Input.Seed = Seed;
    TArray<LDProgression::FLegalOption> Offer;
    if (!GrowthTestReason(Test, TEXT("Drawing an offer"), LDProgression::DrawOffer(Input, Pool, Offer), ELDGrowthReason::None)
        || !Test.TestEqual(TEXT("An offer holds three options"), Offer.Num(), 3))
    {
        return false;
    }
    bGood &= Test.TestTrue(TEXT("The three offered options name distinct definitions"), Offer[0].DefinitionId != Offer[1].DefinitionId
        && Offer[0].DefinitionId != Offer[2].DefinitionId && Offer[1].DefinitionId != Offer[2].DefinitionId);
    for (const LDProgression::FLegalOption& Offered : Offer)
    {
        bGood &= Test.TestTrue(FString::Printf(TEXT("Offered %s comes from the legal pool"), *Offered.DefinitionId.ToString()),
            GrowthTestPoolHas(Pool, Offered.Kind, Offered.DefinitionId, Offered.FromRank, Offered.ToRank));
    }
    TArray<LDProgression::FLegalOption> Redrawn;
    bGood &= GrowthTestReason(Test, TEXT("Redrawing with the same seed"), LDProgression::DrawOffer(Input, Pool, Redrawn),
        ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("The same seed draws the same options in the same order"), GrowthTestSameOptions(Offer, Redrawn));
    TSet<FName> Reachable;
    int32 DraftableSkills = 0;
    for (const FLDDemoAbilityDefinition& Ability : Approved.Abilities)
    {
        DraftableSkills += Ability.Draftable ? 1 : 0;
    }
    for (uint64 TrialSeed = 1; TrialSeed <= 256; ++TrialSeed)
    {
        TArray<LDProgression::FLegalOption> Drawn;
        Input.Seed = TrialSeed;
        bGood &= GrowthTestReason(Test, FString::Printf(TEXT("Drawing with seed %llu"), TrialSeed),
            LDProgression::DrawOffer(Input, Pool, Drawn), ELDGrowthReason::None);
        for (const LDProgression::FLegalOption& DrawnOption : Drawn)
        {
            Reachable.Add(DrawnOption.DefinitionId);
        }
    }
    bGood &= Test.TestEqual(TEXT("Every draftable skill and attribute card is drawn by some of 256 seeds"), Reachable.Num(),
        DraftableSkills + Approved.Attributes.Num());
    TArray<LDProgression::FLegalOption> Refused;
    Input.Ordinal = 0;
    bGood &= GrowthTestReason(Test, TEXT("Opportunity 0"), LDProgression::DrawOffer(Input, Pool, Refused), ELDGrowthReason::DataInvalid);
    Input.Ordinal = 2;
    const TArray<LDProgression::FLegalOption> TwoRows = {Pool[0], Pool[1]};
    bGood &= GrowthTestReason(Test, TEXT("Drawing three from two legal options"), LDProgression::DrawOffer(Input, TwoRows, Refused),
        ELDGrowthReason::DataInvalid);
    bGood &= Test.TestEqual(TEXT("Refused draws offer nothing"), Refused.Num(), 0);

    const FName SnarkBolt(TEXT("active_snark_bolt"));
    const FName PantsDown(TEXT("active_pants_down"));
    const FName NotMad(TEXT("active_not_mad"));
    const FName Elbow(TEXT("active_elbow"));
    FLDGrowthBuild Build;
    LDProgression::FAppliedOption Applied;

    const FLDChoiceOption LearnSnark = GrowthTestOption(ELDChoiceOptionKind::LearnActive, SnarkBolt, 0, 1);
    bGood &= Test.TestTrue(TEXT("Learning a first active is legal on an empty build"),
        LDProgression::OptionStillLegal(*Catalog, Build, LearnSnark));
    if (!GrowthTestApply(Test, *Catalog, Build, LearnSnark, nullptr, ELDGrowthReason::None, Applied, TEXT("Learning the first active")))
    {
        return false;
    }
    bGood &= GrowthTestExpectSkill(Test, Build, SnarkBolt, 1, true, 0, TEXT("The first active"));
    bGood &= Test.TestTrue(TEXT("The first active lands in Q (slot 0) at rank 1 and replaces nothing"), Applied.Slot == 0
        && !Applied.bReplaced && Applied.NewRank == 1 && Applied.Kind == ELDChoiceOptionKind::LearnActive
        && Applied.DefinitionId == SnarkBolt);
    bGood &= Test.TestFalse(TEXT("The applied learn option is no longer legal"),
        LDProgression::OptionStillLegal(*Catalog, Build, LearnSnark));
    bGood &= GrowthTestApply(Test, *Catalog, Build, LearnSnark, nullptr, ELDGrowthReason::InvalidOption, Applied,
        TEXT("Applying the first active again"));

    if (!GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::LearnActive, PantsDown, 0, 1), nullptr,
        ELDGrowthReason::None, Applied, TEXT("Learning the second active")))
    {
        return false;
    }
    bGood &= GrowthTestExpectSkill(Test, Build, PantsDown, 1, true, 1, TEXT("The second active"));
    bGood &= Test.TestEqual(TEXT("The second active lands in W (slot 1)"), int32(Applied.Slot), 1);

    const FLDChoiceOption UpgradeSnark = GrowthTestOption(ELDChoiceOptionKind::UpgradeActive, SnarkBolt, 1, 2);
    if (!GrowthTestApply(Test, *Catalog, Build, UpgradeSnark, nullptr, ELDGrowthReason::None, Applied, TEXT("Upgrading Q to rank 2")))
    {
        return false;
    }
    bGood &= GrowthTestExpectSkill(Test, Build, SnarkBolt, 2, true, 0, TEXT("The upgraded Q active"));
    bGood &= Test.TestFalse(TEXT("The applied upgrade option is no longer legal"),
        LDProgression::OptionStillLegal(*Catalog, Build, UpgradeSnark));

    const FLDChoiceOption LearnNotMad = GrowthTestOption(ELDChoiceOptionKind::LearnActive, NotMad, 0, 1);
    if (!GrowthTestApply(Test, *Catalog, Build, LearnNotMad, nullptr, ELDGrowthReason::None, Applied,
        TEXT("Learning the third active into E")))
    {
        return false;
    }
    bGood &= GrowthTestExpectSkill(Test, Build, NotMad, 1, true, 2, TEXT("The third active"));
    const FLDChoiceOption LearnElbow = GrowthTestOption(ELDChoiceOptionKind::LearnActive, Elbow, 0, 1);
    bGood &= GrowthTestApply(Test, *Catalog, Build, LearnElbow, nullptr, ELDGrowthReason::ReplacementRequired, Applied,
        TEXT("A fourth active without a replacement"));
    LDProgression::FReplacement Replacement;
    Replacement.Slot = 0;
    Replacement.OldSkillId = SnarkBolt;
    Replacement.OldRank = 1;
    Replacement.bConfirmed = true;
    bGood &= GrowthTestApply(Test, *Catalog, Build, LearnElbow, &Replacement, ELDGrowthReason::ReplacementMismatch, Applied,
        TEXT("A replacement naming rank 1 of the rank 2 Q active"));
    Replacement.OldRank = 2;
    Replacement.bConfirmed = false;
    bGood &= GrowthTestApply(Test, *Catalog, Build, LearnElbow, &Replacement, ELDGrowthReason::ReplacementRequired, Applied,
        TEXT("An unconfirmed replacement"));
    Replacement.bConfirmed = true;
    Replacement.Slot = 1;
    bGood &= GrowthTestApply(Test, *Catalog, Build, LearnElbow, &Replacement, ELDGrowthReason::ReplacementMismatch, Applied,
        TEXT("A replacement naming the wrong slot"));
    Replacement.Slot = 0;
    if (!GrowthTestApply(Test, *Catalog, Build, LearnElbow, &Replacement, ELDGrowthReason::None, Applied,
        TEXT("The confirmed replacement of Q")))
    {
        return false;
    }
    bGood &= Test.TestNull(TEXT("The replaced active and its rank are forgotten"), Build.FindSkill(SnarkBolt));
    bGood &= GrowthTestExpectSkill(Test, Build, Elbow, 1, true, 0, TEXT("The replacing active"));
    bGood &= GrowthTestExpectSkill(Test, Build, PantsDown, 1, true, 1, TEXT("The untouched W active"));
    bGood &= GrowthTestExpectSkill(Test, Build, NotMad, 1, true, 2, TEXT("The untouched E active"));
    bGood &= Test.TestTrue(TEXT("The replacement reports the slot and the forgotten skill and rank"), Applied.bReplaced && Applied.Slot == 0
        && Applied.ReplacedId == SnarkBolt && Applied.ReplacedRank == 2 && Applied.NewRank == 1 && Applied.DefinitionId == Elbow);
    TArray<LDProgression::FLegalOption> AfterReplacement;
    bGood &= GrowthTestReason(Test, TEXT("The legal pool after the replacement"),
        LDProgression::LegalPool(*Catalog, Build, AfterReplacement), ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("A forgotten active can be learned again from rank 1"),
        GrowthTestPoolHas(AfterReplacement, ELDChoiceOptionKind::LearnActive, SnarkBolt, 0, 1));
    bGood &= Test.TestTrue(TEXT("Held actives offer their next rank"),
        GrowthTestPoolHas(AfterReplacement, ELDChoiceOptionKind::UpgradeActive, PantsDown, 1, 2)
        && GrowthTestPoolHas(AfterReplacement, ELDChoiceOptionKind::UpgradeActive, NotMad, 1, 2)
        && GrowthTestPoolHas(AfterReplacement, ELDChoiceOptionKind::UpgradeActive, Elbow, 1, 2));
    bGood &= Test.TestTrue(TEXT("The pool stays sorted after a replacement"), GrowthTestPoolSorted(AfterReplacement));

    // A replacement that would be valid for Q is still refused by every option that is not a slot-bound new active.
    Replacement.OldSkillId = Elbow;
    Replacement.OldRank = 1;
    bGood &= GrowthTestApply(Test, *Catalog, Build,
        GrowthTestOption(ELDChoiceOptionKind::LearnPassive, FName(TEXT("passive_lucky_star")), 0, 1), &Replacement,
        ELDGrowthReason::ReplacementNotAllowed, Applied, TEXT("A replacement passed to a passive"));
    bGood &= GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::Attribute, FName(TEXT("attr_protein")), 0, 1),
        &Replacement, ELDGrowthReason::ReplacementNotAllowed, Applied, TEXT("A replacement passed to an attribute card"));
    bGood &= GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::UpgradeActive, PantsDown, 1, 2), &Replacement,
        ELDGrowthReason::ReplacementNotAllowed, Applied, TEXT("A replacement passed to an upgrade"));
    FLDGrowthBuild FreeSlots;
    bGood &= GrowthTestApply(Test, *Catalog, FreeSlots, LearnSnark, &Replacement, ELDGrowthReason::ReplacementNotAllowed, Applied,
        TEXT("A replacement while an active slot is free"));

    if (!GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::UpgradeActive, PantsDown, 1, 2), nullptr,
        ELDGrowthReason::None, Applied, TEXT("Upgrading W to rank 2")))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("An upgrade keeps the active in its slot"), int32(Applied.Slot), 1);
    bGood &= GrowthTestExpectSkill(Test, Build, PantsDown, 2, true, 1, TEXT("The rank 2 W active"));
    TArray<LDProgression::FLegalOption> MaxedPool;
    bGood &= GrowthTestReason(Test, TEXT("The legal pool with a rank 2 active"), LDProgression::LegalPool(*Catalog, Build, MaxedPool),
        ELDGrowthReason::None);
    bGood &= Test.TestFalse(TEXT("A rank 2 (advanced) active is absent from the legal pool"), GrowthTestPoolMentions(MaxedPool, PantsDown));
    bGood &= GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::UpgradeActive, PantsDown, 2, 3), nullptr,
        ELDGrowthReason::InvalidOption, Applied, TEXT("Upgrading past rank 2"));

    for (const TCHAR* PassiveText : {TEXT("passive_iced_americano"), TEXT("passive_lucky_star"), TEXT("passive_freeloader")})
    {
        const FName PassiveId(PassiveText);
        if (!GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::LearnPassive, PassiveId, 0, 1), nullptr,
            ELDGrowthReason::None, Applied, PassiveText))
        {
            return false;
        }
        bGood &= GrowthTestExpectSkill(Test, Build, PassiveId, 1, false, 0, TEXT("A passive learned with all active slots held"));
    }
    bGood &= Test.TestEqual(TEXT("Q still holds active_elbow after three passives"), GrowthTestActiveIn(Build, 0), Elbow);
    bGood &= Test.TestEqual(TEXT("W still holds active_pants_down after three passives"), GrowthTestActiveIn(Build, 1), PantsDown);
    bGood &= Test.TestEqual(TEXT("E still holds active_not_mad after three passives"), GrowthTestActiveIn(Build, 2), NotMad);
    bGood &= Test.TestEqual(TEXT("Three actives and three passives are learned"), Build.Skills.Num(), 6);

    const FName Protein(TEXT("attr_protein"));
    const FLDChoiceOption FirstProtein = GrowthTestOption(ELDChoiceOptionKind::Attribute, Protein, 0, 1);
    if (!GrowthTestApply(Test, *Catalog, Build, FirstProtein, nullptr, ELDGrowthReason::None, Applied, TEXT("The first attr_protein card")))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("attr_protein count after one card"), GrowthTestCardCount(Build, Protein), 1);
    bGood &= Test.TestEqual(TEXT("The first card reports its new count"), int32(Applied.NewRank), 1);
    bGood &= Test.TestFalse(TEXT("The applied card option is no longer legal"),
        LDProgression::OptionStillLegal(*Catalog, Build, FirstProtein));
    TArray<LDProgression::FLegalOption> CardPool;
    bGood &= GrowthTestReason(Test, TEXT("The legal pool after one card"), LDProgression::LegalPool(*Catalog, Build, CardPool),
        ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("The pool offers attr_protein from count 1 to 2"),
        GrowthTestPoolHas(CardPool, ELDChoiceOptionKind::Attribute, Protein, 1, 2));
    if (!GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::Attribute, Protein, 1, 2), nullptr,
        ELDGrowthReason::None, Applied, TEXT("The second attr_protein card")))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("attr_protein count after two cards"), GrowthTestCardCount(Build, Protein), 2);
    bGood &= Test.TestEqual(TEXT("The second card reports its new count"), int32(Applied.NewRank), 2);
    bGood &= GrowthTestApply(Test, *Catalog, Build, FirstProtein, nullptr, ELDGrowthReason::InvalidOption, Applied,
        TEXT("Re-applying the stale first card"));
    bGood &= Test.TestEqual(TEXT("Other attribute cards are untouched"), GrowthTestCardCount(Build, FName(TEXT("attr_canteen"))), 0);
    bGood &= Test.TestTrue(TEXT("ApplyOption leaves the level and build revision to its caller"), Build.Level == 1 && Build.Revision == 1);
    return bGood;
}

// ---- DEV growth fixture ------------------------------------------------------------------------

struct FGrowthTestPreset
{
    const TCHAR* What;
    TArray<FName> Cards;
};

// Off without -LDDevGrowthFixture; a preset resolves only to three distinct legal cards of the build; each preset shapes
// exactly one head draw and the next head is the ordinary seeded draw again.
bool GrowthTestRunDevFixture(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(Test, Catalog))
    {
        return false;
    }
    FString Error;
    bool bGood = Test.TestFalse(TEXT("The rules run carries no -LDDevGrowthFixture switch"), LDDevGrowthFixture::IsSwitchPresent());
    bGood &= Test.TestTrue(TEXT("Without the switch the fixture admits no caller and says why"),
        !LDDevGrowthFixture::Admit(nullptr, Error) && Error.Contains(TEXT("-LDDevGrowthFixture")));
    {
        const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
            .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
        if (!Test.TestNotNull(TEXT("An isolated world for the fixture entries"), World))
        {
            return false;
        }
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
        ALDDevelopmentDirector* Development = World->SpawnActor<ALDDevelopmentDirector>();
        ALDGrowthDirector* Growth = World->SpawnActor<ALDGrowthDirector>();
        if (!Test.TestNotNull(TEXT("A development director"), Development) || !Test.TestNotNull(TEXT("A growth director"), Growth))
        {
            return false;
        }
        const FGuid Contender(0xE3, 0, 0, 1);
        FString QueueError;
        FString GrantError;
        FString Detail;
        bGood &= Test.TestTrue(TEXT("DevQueueOffer is refused without the switch and names it"),
            !Development->DevQueueOffer(Contender, {FName(TEXT("active_snark_bolt")), FName(TEXT("attr_protein")),
                FName(TEXT("attr_all_nighter"))}, QueueError) && QueueError.Contains(TEXT("-LDDevGrowthFixture")));
        FString StageError;
        bGood &= Test.TestTrue(TEXT("StageOffer (the Lobby entry) is refused without the switch and names it"),
            !LDDevGrowthFixture::StageOffer(World, Contender, {FName(TEXT("active_snark_bolt")), FName(TEXT("attr_protein")),
                FName(TEXT("attr_all_nighter"))}, StageError) && StageError.Contains(TEXT("-LDDevGrowthFixture")));
        bGood &= Test.TestTrue(TEXT("DevGrantReward is refused without the switch and issues no ticket"),
            Growth->DevGrantReward(Contender, 80, GrantError) == 0 && GrantError.Contains(TEXT("-LDDevGrowthFixture")));
        bGood &= Test.TestTrue(TEXT("No ticket exists to poll after a refused grant"),
            Growth->DevGrantStatus(1, Detail) == ELDDevGrantStatus::Unknown);
    }

    const FName Snark(TEXT("active_snark_bolt"));
    const FName PantsDown(TEXT("active_pants_down"));
    const FName NotMad(TEXT("active_not_mad"));
    const FName LuckyStar(TEXT("passive_lucky_star"));
    const FName Protein(TEXT("attr_protein"));
    const FName AllNighter(TEXT("attr_all_nighter"));
    const FName HandOut(TEXT("attr_hand_out"));
    FLDGrowthBuild Fresh;
    TArray<LDProgression::FLegalOption> Resolved;
    bGood &= Test.TestTrue(FString::Printf(TEXT("Three legal cards resolve in their given order with kinds from the build (%s)"), *Error),
        LDDevGrowthFixture::ResolvePreset(*Catalog, Fresh, 2, {Snark, LuckyStar, Protein}, Resolved, Error) && Resolved.Num() == 3
        && Resolved[0].DefinitionId == Snark && Resolved[0].Kind == ELDChoiceOptionKind::LearnActive && Resolved[0].ToRank == 1
        && Resolved[1].DefinitionId == LuckyStar && Resolved[1].Kind == ELDChoiceOptionKind::LearnPassive
        && Resolved[2].DefinitionId == Protein && Resolved[2].Kind == ELDChoiceOptionKind::Attribute && Resolved[2].ToRank == 1);
    FLDGrowthBuild Learned;
    Learned.Level = 6;
    Learned.Skills = {{Snark, 1, true, 0}, {NotMad, 2, true, 1}};
    Learned.Cards = {{Protein, 2}};
    bGood &= Test.TestTrue(FString::Printf(TEXT("A learned skill resolves to its next rank and a held card to one more (%s)"), *Error),
        LDDevGrowthFixture::ResolvePreset(*Catalog, Learned, 2, {Snark, Protein, PantsDown}, Resolved, Error) && Resolved.Num() == 3
        && Resolved[0].Kind == ELDChoiceOptionKind::UpgradeActive && Resolved[0].FromRank == 1 && Resolved[0].ToRank == 2
        && Resolved[1].Kind == ELDChoiceOptionKind::Attribute && Resolved[1].FromRank == 2 && Resolved[1].ToRank == 3
        && Resolved[2].Kind == ELDChoiceOptionKind::LearnActive);
    const FGrowthTestPreset BadPresets[] = {
        {TEXT("two cards"), {Snark, Protein}},
        {TEXT("four cards"), {Snark, Protein, AllNighter, HandOut}},
        {TEXT("a repeated card"), {Snark, Protein, Snark}},
        {TEXT("an unknown definition"), {Snark, Protein, FName(TEXT("active_unknown"))}},
        {TEXT("NAME_None"), {Snark, Protein, FName()}},
        {TEXT("a skill at its rank cap"), {NotMad, Protein, AllNighter}}};
    for (const FGrowthTestPreset& Bad : BadPresets)
    {
        bGood &= Test.TestTrue(FString::Printf(TEXT("A preset with %s is refused with a reason and no cards"), Bad.What),
            !LDDevGrowthFixture::ResolvePreset(*Catalog, Learned, 2, Bad.Cards, Resolved, Error) && Resolved.IsEmpty() && !Error.IsEmpty());
    }

    TArray<LDProgression::FLegalOption> Pool;
    TArray<LDProgression::FLegalOption> Ordinary;
    LDProgression::FDrawInput Input;
    Input.Content = &Catalog->Get();
    Input.Build = &Fresh;
    Input.Ordinal = 2;
    Input.Seed = LDProgression::OfferSeed(FGuid(1, 2, 3, 4), FGuid(0xE3, 0, 0, 1), 1);
    bGood &= GrowthTestReason(Test, TEXT("The fresh build's legal pool"), LDProgression::LegalPool(*Catalog, Fresh, Pool), ELDGrowthReason::None);
    bGood &= GrowthTestReason(Test, TEXT("The ordinary seeded draw"), LDProgression::DrawOffer(Input, Pool, Ordinary),
        ELDGrowthReason::None);
    TArray<TArray<FName>> Queue = {{PantsDown, AllNighter, HandOut}, {Snark, FName(TEXT("active_unknown")), Protein},
        {NotMad, LuckyStar, Protein}};
    TArray<LDProgression::FLegalOption> Picked;
    bool bPreset = false;
    FString PresetError;
    bGood &= Test.TestTrue(TEXT("The first head draw takes the oldest preset, in its order"),
        LDDevGrowthFixture::PickHead(Input, Pool, Queue, Picked, bPreset, PresetError) == ELDGrowthReason::None && bPreset
        && PresetError.IsEmpty() && Picked.Num() == 3 && Picked[0].DefinitionId == PantsDown && Picked[1].DefinitionId == AllNighter
        && Picked[2].DefinitionId == HandOut && Queue.Num() == 2);
    bGood &= Test.TestTrue(TEXT("A preset that no longer resolves is consumed with an explicit error and the ordinary draw runs"),
        LDDevGrowthFixture::PickHead(Input, Pool, Queue, Picked, bPreset, PresetError) == ELDGrowthReason::None && !bPreset
        && !PresetError.IsEmpty() && GrowthTestSameOptions(Picked, Ordinary) && Queue.Num() == 1);
    bGood &= Test.TestTrue(TEXT("The next head draw takes the next preset"),
        LDDevGrowthFixture::PickHead(Input, Pool, Queue, Picked, bPreset, PresetError) == ELDGrowthReason::None && bPreset
        && Picked[0].DefinitionId == NotMad && Picked[1].DefinitionId == LuckyStar && Picked[2].DefinitionId == Protein && Queue.IsEmpty());
    bGood &= Test.TestTrue(TEXT("With no preset left the head is the ordinary seeded draw: a preset shapes one offer only"),
        LDDevGrowthFixture::PickHead(Input, Pool, Queue, Picked, bPreset, PresetError) == ELDGrowthReason::None && !bPreset
        && PresetError.IsEmpty() && GrowthTestSameOptions(Picked, Ordinary));
    return bGood;
}

// ---- Private owner view schema ----------------------------------------------------------------

void GrowthTestReachableStructs(const FProperty* Property, TSet<const UScriptStruct*>& Out)
{
    if (const FStructProperty* AsStruct = CastField<FStructProperty>(Property))
    {
        bool bSeen = false;
        Out.Add(AsStruct->Struct, &bSeen);
        if (!bSeen)
        {
            for (TFieldIterator<FProperty> It(AsStruct->Struct); It; ++It)
            {
                GrowthTestReachableStructs(*It, Out);
            }
        }
    }
    else if (const FArrayProperty* AsArray = CastField<FArrayProperty>(Property))
    {
        GrowthTestReachableStructs(AsArray->Inner, Out);
    }
    else if (const FSetProperty* AsSet = CastField<FSetProperty>(Property))
    {
        GrowthTestReachableStructs(AsSet->ElementProp, Out);
    }
    else if (const FMapProperty* AsMap = CastField<FMapProperty>(Property))
    {
        GrowthTestReachableStructs(AsMap->KeyProp, Out);
        GrowthTestReachableStructs(AsMap->ValueProp, Out);
    }
}

bool GrowthTestCarriesReference(const UStruct* Struct)
{
    for (TFieldIterator<FProperty> It(Struct); It; ++It)
    {
        const FProperty* Leaf = *It;
        if (const FArrayProperty* AsArray = CastField<FArrayProperty>(Leaf))
        {
            Leaf = AsArray->Inner;
        }
        if (CastField<FObjectPropertyBase>(Leaf) || CastField<FInterfaceProperty>(Leaf) || CastField<FDelegateProperty>(Leaf)
            || CastField<FMulticastDelegateProperty>(Leaf) || CastField<FSetProperty>(Leaf) || CastField<FMapProperty>(Leaf))
        {
            return true;
        }
        if (const FStructProperty* AsStruct = CastField<FStructProperty>(Leaf); AsStruct && GrowthTestCarriesReference(AsStruct->Struct))
        {
            return true;
        }
    }
    return false;
}

bool GrowthTestExactFields(FAutomationTestBase& Test, const UScriptStruct* Struct, std::initializer_list<const TCHAR*> Names)
{
    TSet<FName> Expected;
    for (const TCHAR* Name : Names)
    {
        Expected.Add(FName(Name));
    }
    TSet<FName> Actual;
    for (TFieldIterator<FProperty> It(Struct); It; ++It)
    {
        Actual.Add(It->GetFName());
    }
    FString Extra;
    for (const FName& Name : Actual.Difference(Expected))
    {
        Extra += Name.ToString() + TEXT(" ");
    }
    FString Missing;
    for (const FName& Name : Expected.Difference(Actual))
    {
        Missing += Name.ToString() + TEXT(" ");
    }
    return Test.TestTrue(FString::Printf(TEXT("%s has exactly its approved owner fields (extra: %s; missing: %s)"),
        *Struct->GetName(), *Extra, *Missing), Extra.IsEmpty() && Missing.IsEmpty());
}

// D10: BeginMatch hands every roster contender, human or AI, exactly one opening opportunity with its kept first offer.
// Only a level crossed adds another (a death or respawn has no growth entry), and a new match starts over at one.
bool GrowthTestRunOpening(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(Test, Catalog))
    {
        return false;
    }
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!Test.TestNotNull(TEXT("An isolated world for the growth director"), World))
    {
        return false;
    }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    ALDDevelopmentDirector* Development = World->SpawnActor<ALDDevelopmentDirector>();
    if (!Test.TestNotNull(TEXT("A growth director"), Development))
    {
        return false;
    }
    const FGuid Human(0xD10, 0, 0, 1);
    const FGuid Bot(0xD10, 0, 0, 2);
    const FGuid Late(0xD10, 0, 0, 3);
    FLDMatchContext First;
    First.Generation = 1;
    First.LedgerMatch = FGuid(0xD10, 1, 0, 0);
    FString Error;
    if (!Test.TestTrue(FString::Printf(TEXT("The first match begins (%s)"), *Error), Development->BeginMatch(First, {Human, Bot}, Catalog, Error)))
    {
        return false;
    }
    TArray<LDProgression::FLegalOption> Pool;
    bool bGood = GrowthTestReason(Test, TEXT("The starting build's legal pool"), LDProgression::LegalPool(*Catalog, FLDGrowthBuild(), Pool),
        ELDGrowthReason::None);
    const auto CheckOpening = [&Test, &Catalog](const FLDProgressionView& View, uint32 Generation, const FString& Who)
    {
        bool bOpening = Test.TestTrue(Who + TEXT(": level 1 at the opening XP holds exactly one unconsumed opportunity"),
            View.bValid && View.Generation == Generation && View.Level == 1 && View.CumulativeXP == 0 && View.EarnedThrough == 1
            && View.ConsumedThrough == 0 && View.PendingChoiceCount == 1);
        bOpening &= Test.TestTrue(Who + TEXT(": the opening offer is kept as opportunity 1 with three distinct cards"),
            View.bHasOffer && View.Offer.OpportunityOrdinal == 1 && View.Offer.OfferId.IsValid() && View.Offer.Options.Num() == 3
            && View.Offer.Options[0].DefinitionId != View.Offer.Options[1].DefinitionId
            && View.Offer.Options[0].DefinitionId != View.Offer.Options[2].DefinitionId
            && View.Offer.Options[1].DefinitionId != View.Offer.Options[2].DefinitionId);
        for (const FLDChoiceOption& Option : View.Offer.Options)
        {
            bOpening &= Test.TestTrue(FString::Printf(TEXT("%s: opening card %s is legal for the starting build"), *Who,
                *Option.DefinitionId.ToString()), Option.Kind == ELDChoiceOptionKind::Opening
                && LDProgression::OptionStillLegal(*Catalog, FLDGrowthBuild(), Option));
        }
        return bOpening;
    };
    TMap<FGuid, FGuid> FirstOffers;
    for (const FGuid& Id : {Human, Bot})
    {
        const FString Who = Id == Human ? TEXT("Human") : TEXT("AI");
        const FLDProgressionView View = Development->ReadProgression(Id);
        bGood &= CheckOpening(View, First.Generation, Who);
        FirstOffers.Add(Id, View.Offer.OfferId);
        const FLDProgressionView Again = Development->ReadProgression(Id);
        bGood &= Test.TestTrue(Who + TEXT(": a later read (as after a death or respawn) neither grants nor redraws"),
            Again.EarnedThrough == 1 && Again.PendingChoiceCount == 1 && Again.Offer.OfferId == View.Offer.OfferId
            && Again.Offer.OfferRevision == View.Offer.OfferRevision && Again.ProgressionRevision == View.ProgressionRevision);
    }
    bGood &= Test.TestNotEqual(TEXT("Each contender draws its own opening offer"), FirstOffers[Human], FirstOffers[Bot]);

    // XP short of level 2 adds nothing; each level crossed adds exactly one behind the kept opening offer, ten in all.
    const FLDPrepareGrowthResult Short = Development->PrepareExperience(Human, 79);
    bGood &= Test.TestTrue(TEXT("79 XP keeps only the opening opportunity and its offer"), Short.IsSuccess()
        && Short.Value->Build().Level == 1 && Short.Value->Progression().EarnedThrough == 1
        && Short.Value->Progression().Offer.IsSet() && Short.Value->Progression().Offer->OfferId == FirstOffers[Human]);
    const FLDPrepareGrowthResult Up = Development->PrepareExperience(Human, 80);
    bGood &= Test.TestTrue(TEXT("Level 2 adds opportunity 2 behind the unchanged opening offer"), Up.IsSuccess()
        && Up.Value->Build().Level == 2 && Up.Value->Progression().EarnedThrough == 2 && Up.Value->Progression().PendingCount() == 2
        && Up.Value->Progression().Offer.IsSet() && Up.Value->Progression().Offer->OfferId == FirstOffers[Human]);
    const FLDPrepareGrowthResult Top = Development->PrepareExperience(Human, 3300);
    bGood &= Test.TestTrue(TEXT("Level 16 holds MaxChoices opportunities: the opening one and fifteen level-ups"), Top.IsSuccess()
        && Top.Value->Build().Level == 16 && Top.Value->Progression().EarnedThrough == 16
        && int32(Top.Value->Progression().EarnedThrough) == Catalog->Get().Progression.MaxChoices);

    FLDMatchContext Second;
    Second.Generation = 2;
    Second.LedgerMatch = FGuid(0xD10, 2, 0, 0);
    if (!Test.TestTrue(FString::Printf(TEXT("A new match begins (%s)"), *Error), Development->BeginMatch(Second, {Human, Late}, Catalog, Error)))
    {
        return false;
    }
    const FLDProgressionView Again = Development->ReadProgression(Human);
    bGood &= CheckOpening(Again, Second.Generation, TEXT("Human in the new match"));
    bGood &= Test.TestNotEqual(TEXT("The new match draws a new opening offer"), Again.Offer.OfferId, FirstOffers[Human]);
    bGood &= CheckOpening(Development->ReadProgression(Late), Second.Generation, TEXT("A contender new to the second match"));
    bGood &= Test.TestFalse(TEXT("A contender outside the new roster has no growth record"), Development->ReadProgression(Bot).bValid);
    return bGood;
}

// #153: distinct direction cards hide distinct draftable actives; neither enters normal level-up offers.
bool GrowthTestRunOpeningPool(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(Test, Catalog))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    const FLDDemoProgressionDefinition& Progression = Content.Progression;
    bool bGood = Test.TestTrue(TEXT("Six approved directions use the approved 3:2 hidden-active weights"),
        Content.OpeningCards.Num() == 6 && Progression.OpeningMatchedSkillWeight == 3 && Progression.OpeningOtherSkillWeight == 2);
    // The draftable sets are read, not listed.
    TSet<FName> Actives, Passives;
    for (const FLDDemoAbilityDefinition& Ability : Content.Abilities)
    {
        if (Ability.Draftable)
        {
            (Ability.Kind == ELDDemoAbilityKind::Active ? Actives : Passives).Add(FName(*Ability.Id));
        }
    }
    TArray<LDProgression::FLegalOption> Pool;
    if (!GrowthTestReason(Test, TEXT("The starting build's legal pool"), LDProgression::LegalPool(*Catalog, FLDGrowthBuild(), Pool),
        ELDGrowthReason::None))
    {
        return false;
    }
    const auto CheckComposition = [&](const FString& Who, const TArray<FName>& Ids, const TArray<ELDChoiceOptionKind>& Kinds)
    {
        bool bLegal = Ids.Num() == 3 && Kinds.Num() == 3 && Ids[0] != Ids[1] && Ids[0] != Ids[2] && Ids[1] != Ids[2];
        for (int32 Index = 0; bLegal && Index < 3; ++Index)
        {
            bLegal = Kinds[Index] == ELDChoiceOptionKind::Opening && Content.OpeningCards.ContainsByPredicate(
                [&](const FLDDemoOpeningCardDefinition& Row) { return FName(*Row.Id) == Ids[Index]; });
        }
        return Test.TestTrue(Who + TEXT(": three distinct direction cards"), bLegal);
    };
    const FLDGrowthBuild Empty;
    LDProgression::FDrawInput Input;
    Input.Content = &Content;
    Input.Build = &Empty;
    Input.Ordinal = 1;
    TSet<FName> Reached;
    TSet<FName> HiddenReached;
    TMap<FName, int32> DirectionCounts;
    for (uint64 Seed = 1; Seed <= 512; ++Seed)
    {
        TArray<LDProgression::FLegalOption> Drawn;
        Input.Seed = Seed;
        const ELDGrowthReason Reason = LDProgression::DrawOffer(Input, Pool, Drawn);
        TArray<FName> Ids;
        TArray<ELDChoiceOptionKind> Kinds;
        for (int32 Index = 0; Index < Drawn.Num(); ++Index)
        {
            Ids.Add(Drawn[Index].DefinitionId);
            Kinds.Add(Drawn[Index].Kind);
            Reached.Add(Drawn[Index].DefinitionId);
            HiddenReached.Add(Drawn[Index].HiddenActive);
            ++DirectionCounts.FindOrAdd(Drawn[Index].DefinitionId);
            bGood &= GrowthTestPoolHas(Pool, ELDChoiceOptionKind::LearnActive, Drawn[Index].HiddenActive, 0, 1);
        }
        if (Reason != ELDGrowthReason::None || !CheckComposition(FString::Printf(TEXT("Opening seed %llu"), Seed), Ids, Kinds))
        {
            return false;
        }
        bGood &= Test.TestTrue(TEXT("The three hidden actives are distinct"),
            Drawn[0].HiddenActive != Drawn[1].HiddenActive && Drawn[0].HiddenActive != Drawn[2].HiddenActive
            && Drawn[1].HiddenActive != Drawn[2].HiddenActive);
    }
    bGood &= Test.TestEqual(TEXT("512 opening seeds reach every direction"), Reached.Num(), Content.OpeningCards.Num());
    bGood &= Test.TestEqual(TEXT("512 opening seeds reach every draftable active"), HiddenReached.Num(), Actives.Num());
    for (const auto& Count : DirectionCounts)
    {
        bGood &= Test.TestTrue(TEXT("Direction frequency stays within 20 percent of the equal-weight expectation"),
            FMath::Abs(Count.Value - 256) <= 52);
    }
    TArray<LDProgression::FLegalOption> First, Again;
    Input.Seed = 77;
    bGood &= GrowthTestReason(Test, TEXT("An opening draw"), LDProgression::DrawOffer(Input, Pool, First), ELDGrowthReason::None);
    bGood &= GrowthTestReason(Test, TEXT("The same opening seed"), LDProgression::DrawOffer(Input, Pool, Again), ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("The same seed redraws the same opening offer"), GrowthTestSameOptions(First, Again));

    // Contract 2: a non-draftable skill (new actives until #117) never enters any offer, opening or level-up.
    FLDDemoContentDefinitions Gated = Content;
    const FName Held(TEXT("active_snark_bolt"));
    Gated.Abilities.FindByPredicate([](const FLDDemoAbilityDefinition& Row)
        { return Row.Id == TEXT("active_snark_bolt"); })->Draftable = false;
    LDProgression::FDrawInput GatedInput = Input;
    GatedInput.Content = &Gated;
    FLDGrowthBuild Level3;
    Level3.Level = 3;
    Level3.Skills = {{FName(TEXT("passive_lucky_star")), 1, false, 0}};
    bool bNeverGated = true;
    for (uint64 Seed = 1; Seed <= 512; ++Seed)
    {
        for (const uint8 Ordinal : {uint8(1), uint8(3)})
        {
            TArray<LDProgression::FLegalOption> LevelPool, Drawn;
            const FLDGrowthBuild& Build = Ordinal == 1 ? Empty : Level3;
            LDProgression::LegalPool(*Catalog, Build, LevelPool);
            GatedInput.Build = &Build;
            GatedInput.Ordinal = Ordinal;
            GatedInput.Seed = Seed;
            bNeverGated &= LDProgression::DrawOffer(GatedInput, LevelPool, Drawn) == ELDGrowthReason::None
                && !Drawn.ContainsByPredicate([Held](const LDProgression::FLegalOption& Row)
                    { return Row.DefinitionId == Held || Row.HiddenActive == Held; });
        }
    }
    bGood &= Test.TestTrue(TEXT("A non-draftable active never appears in an opening or level-up offer"), bNeverGated);
    // Still legal to learn outside a draw (DEV learn), so it stays in the legal pool.
    bGood &= Test.TestTrue(TEXT("The non-draftable active stays in the legal pool"), GrowthTestPoolHas(Pool, ELDChoiceOptionKind::LearnActive, Held, 0, 1));

    // Refusals: a short group, malformed counts and opportunity 0 offer nothing.
    TArray<LDProgression::FLegalOption> Refused;
    FLDDemoContentDefinitions Short = Content;
    for (FLDDemoAbilityDefinition& Ability : Short.Abilities)
    {
        Ability.Draftable = Ability.Kind == ELDDemoAbilityKind::Passive || Ability.Id == TEXT("active_snark_bolt");
    }
    LDProgression::FDrawInput ShortInput = Input;
    ShortInput.Content = &Short;
    bGood &= Test.TestFalse(TEXT("One draftable active cannot fill three hidden actives"), LDProgression::DrawFeasible(Short, Empty, Pool, 1));
    bGood &= GrowthTestReason(Test, TEXT("Drawing an opening with one draftable active"), LDProgression::DrawOffer(ShortInput, Pool, Refused),
        ELDGrowthReason::DataInvalid);
    FLDDemoContentDefinitions Four = Content;
    Four.Progression.OpeningMatchedSkillWeight = 0;
    ShortInput.Content = &Four;
    bGood &= GrowthTestReason(Test, TEXT("An opening with an invalid weight"), LDProgression::DrawOffer(ShortInput, Pool, Refused),
        ELDGrowthReason::DataInvalid);
    ShortInput.Content = &Content;
    ShortInput.Ordinal = 0;
    bGood &= GrowthTestReason(Test, TEXT("Opportunity 0"), LDProgression::DrawOffer(ShortInput, Pool, Refused), ELDGrowthReason::DataInvalid);
    bGood &= Test.TestEqual(TEXT("Refused opening draws offer nothing"), Refused.Num(), 0);

    // The DEV fixture still presets any legal cards for the opening; without a preset it draws the opening composition.
    TArray<TArray<FName>> Queue = {{FName(TEXT("active_snark_bolt")), FName(TEXT("attr_protein")),
        FName(TEXT("attr_all_nighter"))}};
    TArray<LDProgression::FLegalOption> Picked;
    bool bPreset = false;
    FString PresetError;
    bGood &= Test.TestTrue(TEXT("An opening preset is still honoured as the DEV override"),
        LDDevGrowthFixture::PickHead(Input, Pool, Queue, Picked, bPreset, PresetError) == ELDGrowthReason::None
        && bPreset && Picked.Num() == 3 && Picked[0].DefinitionId == FName(TEXT("active_snark_bolt")) && Queue.IsEmpty());
    bGood &= Test.TestTrue(TEXT("Without a preset the fixture draws the ordinary opening offer"),
        LDDevGrowthFixture::PickHead(Input, Pool, Queue, Picked, bPreset, PresetError) == ELDGrowthReason::None
        && !bPreset && GrowthTestSameOptions(Picked, First));

    // Every roster contender's kept opening offer follows the composition.
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!Test.TestNotNull(TEXT("An isolated world for the growth director"), World))
    {
        return false;
    }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    ALDDevelopmentDirector* Development = World->SpawnActor<ALDDevelopmentDirector>();
    if (!Test.TestNotNull(TEXT("A growth director"), Development))
    {
        return false;
    }
    TArray<FGuid> Roster;
    for (uint32 Index = 1; Index <= 6; ++Index)
    {
        Roster.Add(FGuid(0xD24, 0, 0, Index));
    }
    FLDMatchContext Match;
    Match.Generation = 1;
    Match.LedgerMatch = FGuid(0xD24, 1, 0, 0);
    FString Error;
    if (!Test.TestTrue(FString::Printf(TEXT("The match begins (%s)"), *Error), Development->BeginMatch(Match, Roster, Catalog, Error)))
    {
        return false;
    }
    for (const FGuid& Id : Roster)
    {
        const FLDProgressionView View = Development->ReadProgression(Id);
        TArray<FName> Ids;
        TArray<ELDChoiceOptionKind> Kinds;
        for (const FLDChoiceOption& Option : View.Offer.Options)
        {
            Ids.Add(Option.DefinitionId);
            Kinds.Add(Option.Kind);
        }
        bGood &= Test.TestTrue(TEXT("The kept offer is opportunity 1"), View.bHasOffer && View.Offer.OpportunityOrdinal == 1)
            && CheckComposition(TEXT("Roster contender ") + Id.ToString(), Ids, Kinds);
    }
    return bGood;
}

bool GrowthTestRunPrivacy(FAutomationTestBase& Test)
{
    // Any field added to the private growth views must be added here deliberately (and told to UI).
    bool bGood = GrowthTestExactFields(Test, FLDGrowthOwnerView::StaticStruct(),
        {TEXT("Generation"), TEXT("PublishRevision"), TEXT("Progression"), TEXT("AirdropClaimedSerial")});
    bGood &= GrowthTestExactFields(Test, FLDProgressionView::StaticStruct(),
        {TEXT("bValid"), TEXT("Generation"), TEXT("Life"), TEXT("LifePhase"), TEXT("bCanChoose"), TEXT("Level"), TEXT("MaxLevel"),
         TEXT("CumulativeXP"), TEXT("LevelThresholdXP"), TEXT("bHasNextThreshold"), TEXT("NextThresholdXP"),
         TEXT("ProgressionRevision"), TEXT("BuildRevision"), TEXT("EarnedThrough"), TEXT("ConsumedThrough"),
         TEXT("PendingChoiceCount"), TEXT("RerollTokens"), TEXT("bHasOffer"), TEXT("Offer"), TEXT("Skills"), TEXT("AttributeCards"),
         TEXT("OpeningCard"), TEXT("CatalogDigest")});
    bGood &= GrowthTestExactFields(Test, FLDChoiceOfferView::StaticStruct(),
        {TEXT("OfferId"), TEXT("OpportunityOrdinal"), TEXT("OfferRevision"), TEXT("GuardBuildRevision"), TEXT("bRerolled"), TEXT("Options")});
    bGood &= GrowthTestExactFields(Test, FLDChoiceOption::StaticStruct(),
        {TEXT("OptionId"), TEXT("Kind"), TEXT("DefinitionId"), TEXT("FromRank"), TEXT("ToRank"), TEXT("AttributeQuality")});
    bGood &= GrowthTestExactFields(Test, FLDLearnedSkillView::StaticStruct(),
        {TEXT("DefinitionId"), TEXT("bActive"), TEXT("Rank"), TEXT("Slot")});
    bGood &= GrowthTestExactFields(Test, FLDAttributeCardView::StaticStruct(), {TEXT("DefinitionId"), TEXT("Count"), TEXT("Quality")});
    bGood &= Test.TestFalse(TEXT("The owner growth view carries no object, delegate or unordered container"),
        GrowthTestCarriesReference(FLDGrowthOwnerView::StaticStruct()));

    const TSet<const UScriptStruct*> GrowthPrivateViews = {FLDGrowthOwnerView::StaticStruct(), FLDProgressionView::StaticStruct(),
        FLDChoiceOfferView::StaticStruct(), FLDChoiceOption::StaticStruct(), FLDLearnedSkillView::StaticStruct(),
        FLDAttributeCardView::StaticStruct()};
    TSet<const UScriptStruct*> Nested;
    for (TFieldIterator<FProperty> It(FLDGrowthOwnerView::StaticStruct()); It; ++It)
    {
        GrowthTestReachableStructs(*It, Nested);
    }
    Nested = Nested.Difference(GrowthPrivateViews);
    Nested.Remove(TBaseStructure<FGuid>::Get());
    Nested.Remove(FLDDemoContentDigest::StaticStruct());
    bGood &= Test.TestEqual(TEXT("The owner growth view nests only its approved structs, FGuid and the catalog digest"), Nested.Num(), 0);

    int32 GrowthMembers = 0;
    bool bOnlyGrowth = false;
    for (TFieldIterator<FProperty> It(FLDOwnerState::StaticStruct()); It; ++It)
    {
        TSet<const UScriptStruct*> Reached;
        GrowthTestReachableStructs(*It, Reached);
        if (Reached.Intersect(GrowthPrivateViews).Num() > 0)
        {
            ++GrowthMembers;
            const FStructProperty* AsStruct = CastField<FStructProperty>(*It);
            bOnlyGrowth = It->GetFName() == FName(TEXT("Growth")) && AsStruct && AsStruct->Struct == FLDGrowthOwnerView::StaticStruct();
        }
    }
    bGood &= Test.TestEqual(TEXT("FLDOwnerState carries growth through exactly one member"), GrowthMembers, 1);
    bGood &= Test.TestTrue(TEXT("That member is FLDOwnerState.Growth"), bOnlyGrowth);
    bGood &= Test.TestTrue(TEXT("The owner state replicates only through the owning controller"),
        GetDefault<ALDPlayerController>()->bOnlyRelevantToOwner);

    for (const UClass* PublicClass : {ALDGameState::StaticClass(), ALDPlayerState::StaticClass(), ALDCharacter::StaticClass()})
    {
        for (TFieldIterator<FProperty> It(PublicClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
        {
            TSet<const UScriptStruct*> Reached;
            GrowthTestReachableStructs(*It, Reached);
            bGood &= Test.TestEqual(FString::Printf(TEXT("%s.%s exposes no private growth view"), *PublicClass->GetName(), *It->GetName()),
                Reached.Intersect(GrowthPrivateViews).Num(), 0);
        }
    }
    TSet<const UScriptStruct*> Observed;
    for (TFieldIterator<FProperty> It(FLDWorldView::StaticStruct()); It; ++It)
    {
        GrowthTestReachableStructs(*It, Observed);
    }
    bGood &= Test.TestEqual(TEXT("Observed world views expose no private growth view"), Observed.Intersect(GrowthPrivateViews).Num(), 0);
    return bGood;
}
// ---- #147 slotted draft: upgrade / new skill / free slot, the full-slot active weight, reroll avoidance, replacement ----
FLDGrowthBuild GrowthTestDraftBuild(uint8 Level, std::initializer_list<FLDLearnedSkill> Skills)
{
    FLDGrowthBuild Build;
    Build.Level = Level;
    Build.Skills = Skills;
    Build.Skills.Sort([](const FLDLearnedSkill& A, const FLDLearnedSkill& B)
        { return A.Id.ToString().Compare(B.Id.ToString(), ESearchCase::CaseSensitive) < 0; });
    return Build;
}

// Draftable skills of the approved catalog in catalog order, so the draft rules are checked on whatever it holds.
void GrowthTestDraftable(const FLDDemoContentDefinitions& Content, TArray<FName>& Actives, TArray<FName>& Passives)
{
    for (const FLDDemoAbilityDefinition& Ability : Content.Abilities)
    {
        if (Ability.Draftable)
        {
            (Ability.Kind == ELDDemoAbilityKind::Active ? Actives : Passives).Add(FName(*Ability.Id));
        }
    }
}

bool GrowthTestRunDraft(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(Test, Catalog))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    const FLDDemoChoiceWeights& W = Content.Parameters.Choice;
    const uint8 TopRank = uint8(Content.Progression.MaxSkillRank);
    bool bGood = Test.TestTrue(TEXT("The approved draft weights and slot chances are loaded"),
        W.NewActiveFreeSlotWeightBp == 7500 && W.NewActiveFullSlotWeightBp == 3500
        && W.FirstSlotUpgradeBp == 7000 && W.FreeSlotAttributeBp == 6000);
    bGood &= Test.TestTrue(TEXT("Attribute quality chances and multipliers are approved"),
        W.AttributeGreenChanceBp == 4000 && W.AttributeGreenMultiplierBp == 9000
        && W.AttributeBlueChanceBp == 3500 && W.AttributeBlueMultiplierBp == 10000
        && W.AttributePurpleChanceBp == 2000 && W.AttributePurpleMultiplierBp == 11000
        && W.AttributeGoldChanceBp == 500 && W.AttributeGoldMultiplierBp == 12000);
    bGood &= Test.TestTrue(TEXT("Rerolls are earned at levels 5 and 11"), Content.Progression.RerollLevels == TArray<int32>{5, 11});
    const uint8 ExpectedTokens[] = {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2};
    for (uint8 Level = 1; Level <= 16; ++Level)
    {
        bGood &= Test.TestEqual(FString::Printf(TEXT("Rerolls earned by level %u"), uint32(Level)),
            LDProgression::RerollTokensEarned(Content.Progression, Level), ExpectedTokens[Level - 1]);
    }
    TArray<FName> Actives, Passives;
    GrowthTestDraftable(Content, Actives, Passives);
    if (!Test.TestTrue(TEXT("The catalog drafts at least four actives and three passives"), Actives.Num() >= 4 && Passives.Num() >= 3))
    {
        return false;
    }
    const auto Weight = [&](const FLDGrowthBuild& Build, ELDChoiceOptionKind Kind, FName Id)
    {
        int64 Out = -1;
        return LDProgression::NewSkillWeight(Content, Build, {Kind, Id, 0, 1}, Out) == ELDGrowthReason::None ? Out : int64(-1);
    };
    const FLDGrowthBuild Empty;
    const FLDGrowthBuild Full = GrowthTestDraftBuild(5,
        {{Actives[0], 1, true, 0}, {Actives[1], 1, true, 1}, {Actives[2], 1, true, 2}, {Passives[0], 1, false, 0}});
    bGood &= Test.TestEqual(TEXT("A new active with a free Q/W/E slot weighs 0.75"),
        Weight(Empty, ELDChoiceOptionKind::LearnActive, Actives[3]), int64(7500));
    bGood &= Test.TestEqual(TEXT("A new passive weighs 1"), Weight(Full, ELDChoiceOptionKind::LearnPassive, Passives[1]), int64(10000));
    bGood &= Test.TestEqual(TEXT("A new active with Q/W/E full weighs 0.35"),
        Weight(Full, ELDChoiceOptionKind::LearnActive, Actives[3]), int64(3500));
    int64 Refused = 0;
    bGood &= GrowthTestReason(Test, TEXT("An upgrade has no new-skill weight"),
        LDProgression::NewSkillWeight(Content, Full, {ELDChoiceOptionKind::UpgradePassive, Passives[0], 1, 2}, Refused), ELDGrowthReason::DataInvalid);
    bGood &= GrowthTestReason(Test, TEXT("An attribute card has no new-skill weight"),
        LDProgression::NewSkillWeight(Content, Full, {ELDChoiceOptionKind::Attribute, FName(*Content.Attributes[0].Id), 0, 1}, Refused),
        ELDGrowthReason::DataInvalid);

    // Slots over many seeds: first slot 70% upgrade/30% attribute, second a new skill, third 60% attribute/40% new skill.
    // Every offer has at most one new active and no repeated family.
    TArray<LDProgression::FLegalOption> FullPool;
    bGood &= GrowthTestReason(Test, TEXT("The full build's pool"), LDProgression::LegalPool(*Catalog, Full, FullPool), ELDGrowthReason::None);
    LDProgression::FDrawInput Input;
    Input.Content = &Content;
    Input.Build = &Full;
    Input.Ordinal = 4;
    int32 FirstUpgrades = 0, FreeAttributes = 0, Violations = 0;
    int32 Qualities[4] = {0, 0, 0, 0};
    TMap<FName, int32> SecondSlot;
    constexpr int32 Seeds = 4096;
    for (int32 Seed = 1; Seed <= Seeds; ++Seed)
    {
        TArray<LDProgression::FLegalOption> Drawn;
        Input.Seed = uint64(Seed) * 7919;
        if (LDProgression::DrawOffer(Input, FullPool, Drawn) != ELDGrowthReason::None || Drawn.Num() != 3)
        {
            ++Violations;
            continue;
        }
        const bool bUpgrade = Drawn[0].Kind == ELDChoiceOptionKind::UpgradeActive || Drawn[0].Kind == ELDChoiceOptionKind::UpgradePassive;
        const bool bFirstOk = bUpgrade || Drawn[0].Kind == ELDChoiceOptionKind::Attribute;
        const bool bNew = Drawn[1].Kind == ELDChoiceOptionKind::LearnActive || Drawn[1].Kind == ELDChoiceOptionKind::LearnPassive;
        const bool bFreeOk = Drawn[2].Kind == ELDChoiceOptionKind::Attribute || Drawn[2].Kind == ELDChoiceOptionKind::LearnActive
            || Drawn[2].Kind == ELDChoiceOptionKind::LearnPassive;
        const int32 NewActives = Drawn.FilterByPredicate([](const LDProgression::FLegalOption& Row)
            { return Row.Kind == ELDChoiceOptionKind::LearnActive; }).Num();
        FirstUpgrades += bUpgrade ? 1 : 0;
        FreeAttributes += Drawn[2].Kind == ELDChoiceOptionKind::Attribute ? 1 : 0;
        for (const LDProgression::FLegalOption& Row : Drawn)
        {
            if (Row.Kind == ELDChoiceOptionKind::Attribute && Row.AttributeQuality != ELDAttributeQuality::None)
            {
                ++Qualities[uint8(Row.AttributeQuality) - 1];
            }
        }
        ++SecondSlot.FindOrAdd(Drawn[1].DefinitionId);
        Violations += !bFirstOk || !bNew || !bFreeOk || NewActives > 1 || Drawn[0].DefinitionId == Drawn[1].DefinitionId
            || Drawn[0].DefinitionId == Drawn[2].DefinitionId || Drawn[1].DefinitionId == Drawn[2].DefinitionId ? 1 : 0;
    }
    bGood &= Test.TestEqual(TEXT("Every Q/W/E-full offer keeps its slots and limits"), Violations, 0);
    bGood &= Test.TestTrue(FString::Printf(TEXT("The first slot upgrades about 70%% of the time (%d of %d)"), FirstUpgrades, Seeds),
        FirstUpgrades > Seeds * 65 / 100 && FirstUpgrades < Seeds * 75 / 100);
    bGood &= Test.TestTrue(FString::Printf(TEXT("The third slot is an attribute about 60%% of the time (%d of %d)"), FreeAttributes, Seeds),
        FreeAttributes > Seeds * 55 / 100 && FreeAttributes < Seeds * 65 / 100);
    const int32 QualityTotal=Qualities[0]+Qualities[1]+Qualities[2]+Qualities[3];
    bGood &= Test.TestTrue(TEXT("Attribute quality distribution reaches 40/35/20/5"),
        QualityTotal>0 && Qualities[0]>QualityTotal*35/100 && Qualities[0]<QualityTotal*45/100
        && Qualities[1]>QualityTotal*30/100 && Qualities[1]<QualityTotal*40/100
        && Qualities[2]>QualityTotal*15/100 && Qualities[2]<QualityTotal*25/100
        && Qualities[3]>QualityTotal*2/100 && Qualities[3]<QualityTotal*8/100);
    // Expected second-slot share: weight 1 per unlearned passive, 0.35 per unlearned active.
    double PassiveCount = 0, ActiveCount = 0;
    int32 PassiveRows = 0, ActiveRows = 0;
    for (const FName Id : Passives) { if (!Full.FindSkill(Id)) { PassiveCount += SecondSlot.FindRef(Id); ++PassiveRows; } }
    for (const FName Id : Actives) { if (!Full.FindSkill(Id)) { ActiveCount += SecondSlot.FindRef(Id); ++ActiveRows; } }
    const double Ratio = ActiveRows && PassiveRows && PassiveCount > 0 ? (ActiveCount / ActiveRows) / (PassiveCount / PassiveRows) : 0.;
    bGood &= Test.TestTrue(FString::Printf(TEXT("A new active follows the 0.35 full-slot weight (%.3f)"), Ratio),
        Ratio > 0.28 && Ratio < 0.42);
    int32 Rarest = MAX_int32, Commonest = 0;
    for (const FName Id : Passives)
    {
        if (Full.FindSkill(Id)) { continue; }
        Rarest = FMath::Min(Rarest, SecondSlot.FindRef(Id));
        Commonest = FMath::Max(Commonest, SecondSlot.FindRef(Id));
    }
    bGood &= Test.TestTrue(FString::Printf(TEXT("Every new passive is equally likely, whatever the build holds (%d..%d)"), Rarest, Commonest),
        Rarest > 0 && Commonest < Rarest * 3 / 2);

    // No learned skill below the top rank: the first slot falls back to an attribute; the second remains a new skill.
    FLDGrowthBuild Topped = GrowthTestDraftBuild(5, {{Actives[0], TopRank, true, 0}, {Passives[0], TopRank, false, 0}});
    TArray<LDProgression::FLegalOption> ToppedPool;
    LDProgression::LegalPool(*Catalog, Topped, ToppedPool);
    Input.Build = &Topped;
    int32 ToppedBad = 0;
    for (int32 Seed = 1; Seed <= 512; ++Seed)
    {
        TArray<LDProgression::FLegalOption> Drawn;
        Input.Seed = uint64(Seed);
        ToppedBad += LDProgression::DrawOffer(Input, ToppedPool, Drawn) != ELDGrowthReason::None
            || Drawn[0].Kind != ELDChoiceOptionKind::Attribute
            || (Drawn[1].Kind != ELDChoiceOptionKind::LearnActive && Drawn[1].Kind != ELDChoiceOptionKind::LearnPassive)
            || Drawn.FilterByPredicate([](const LDProgression::FLegalOption& Row)
                { return Row.Kind == ELDChoiceOptionKind::LearnActive; }).Num() > 1 ? 1 : 0;
    }
    bGood &= Test.TestEqual(TEXT("Without an upgrade the first slot falls back to an attribute"), ToppedBad, 0);
    const TArray<LDProgression::FLegalOption> Cards = {
        {ELDChoiceOptionKind::Attribute, FName(*Content.Attributes[0].Id), 0, 1},
        {ELDChoiceOptionKind::Attribute, FName(*Content.Attributes[1].Id), 0, 1},
        {ELDChoiceOptionKind::Attribute, FName(*Content.Attributes[2].Id), 0, 1}};
    TArray<LDProgression::FLegalOption> CardsOnly;
    bGood &= GrowthTestReason(Test, TEXT("A pool of attribute cards alone"), LDProgression::DrawOffer(Input, Cards, CardsOnly),
        ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("It still fills all three slots"), CardsOnly.Num() == 3);
    bGood &= Test.TestFalse(TEXT("Two attribute cards and nothing else cannot fill an offer"),
        LDProgression::DrawFeasible(Content, Topped, {Cards[0], Cards[1]}, 4));

    // Reroll: the same slots, the previous three cards avoided while the pool allows.
    Input.Build = &Full;
    Input.Seed = 41;
    TArray<LDProgression::FLegalOption> First;
    LDProgression::DrawOffer(Input, FullPool, First);
    int32 Overlaps = 0;
    for (int32 Seed = 1; Seed <= 256; ++Seed)
    {
        LDProgression::FDrawInput Reroll = Input;
        Reroll.Seed = LDProgression::RerollSeed(FGuid(1, 2, 3, 4), FGuid(5, 6, 7, uint32(Seed)), 4);
        Reroll.Avoid = First;
        TArray<LDProgression::FLegalOption> Again;
        if (LDProgression::DrawOffer(Reroll, FullPool, Again) != ELDGrowthReason::None) { ++Overlaps; continue; }
        for (const LDProgression::FLegalOption& Row : Again)
        {
            // The full build has three upgrades, so the upgrade slot can always avoid; so can every other slot.
            Overlaps += First.ContainsByPredicate([&Row](const LDProgression::FLegalOption& Old)
                { return Old.Kind == Row.Kind && Old.DefinitionId == Row.DefinitionId; }) ? 1 : 0;
        }
    }
    bGood &= Test.TestEqual(TEXT("A reroll never repeats a card of the previous offer while others remain"), Overlaps, 0);
    // The slot contract refuses a pool that cannot fill both later cards when the first slot consumes an attribute.
    const TArray<LDProgression::FLegalOption> Tight = {
        {ELDChoiceOptionKind::UpgradeActive, Actives[0], 1, 2}, {ELDChoiceOptionKind::Attribute, FName(*Content.Attributes[0].Id), 0, 1},
        {ELDChoiceOptionKind::Attribute, FName(*Content.Attributes[1].Id), 0, 1}};
    LDProgression::FDrawInput TightInput = Input;
    TightInput.Avoid = Tight;
    TArray<LDProgression::FLegalOption> TightDraw;
    bGood &= Test.TestFalse(TEXT("An upgrade plus only two attributes is not draw-feasible"),
        LDProgression::DrawFeasible(Content, Full, Tight, 4));
    bGood &= GrowthTestReason(Test, TEXT("A draw of that exhausted pool is rejected"),
        LDProgression::DrawOffer(TightInput, Tight, TightDraw), ELDGrowthReason::DataInvalid);

    // Replacement (#147 D13): the new active starts at rank 1 whatever the forgotten rank was.
    FLDGrowthBuild Build = GrowthTestDraftBuild(7,
        {{Actives[0], TopRank, true, 0}, {Actives[1], 1, true, 1}, {Actives[2], 1, true, 2}});
    LDProgression::FReplacement Replacement;
    Replacement.Slot = 0;
    Replacement.OldSkillId = Actives[0];
    Replacement.OldRank = TopRank;
    Replacement.bConfirmed = true;
    LDProgression::FAppliedOption Applied;
    if (!GrowthTestApply(Test, *Catalog, Build, GrowthTestOption(ELDChoiceOptionKind::LearnActive, Actives[3], 0, 1), &Replacement,
        ELDGrowthReason::None, Applied, TEXT("Replacing a top-rank Q")))
    {
        return false;
    }
    bGood &= Test.TestTrue(TEXT("Replacing the top rank learns the new active at rank 1 in the same slot"), Applied.bReplaced
        && Applied.NewRank == 1 && Applied.ReplacedRank == TopRank && !Build.FindSkill(Actives[0]));
    bGood &= GrowthTestExpectSkill(Test, Build, Actives[3], 1, true, 0, TEXT("The replacement"));
    return bGood;
}

// ---- #147 skill growth: G(L) on a learned skill's fixed amounts, never on mana, ratios or durations ----
bool GrowthTestRunSkillGrowth(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(Test, Catalog))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    const int64 Step = Content.Parameters.SkillGrowthPerLevelBp;
    const int64 LaterStep = Content.Parameters.SkillGrowthPerLevelAfter10Bp;
    bool bGood = Test.TestTrue(TEXT("The approved growth steps are 10% through level 10 and 5% after"),
        Step == 1000 && LaterStep == 500);
    bGood &= Test.TestTrue(TEXT("G(1)=1.0, G(5)=1.4, G(10)=1.9, G(13)=2.05, G(16)=2.2; no level 0"),
        LDSkillGrowth::FactorBp(Step, LaterStep, 1) == 10000
        && LDSkillGrowth::FactorBp(Step, LaterStep, 5) == 14000
        && LDSkillGrowth::FactorBp(Step, LaterStep, 10) == 19000
        && LDSkillGrowth::FactorBp(Step, LaterStep, 13) == 20500
        && LDSkillGrowth::FactorBp(Step, LaterStep, 16) == 22000
        && LDSkillGrowth::FactorBp(Step, LaterStep, 0) == 0);
    int64 Grown = 0;
    bGood &= Test.TestTrue(TEXT("A grown amount floors to P2"), LDSkillGrowth::GrowP2(6001, 14000, Grown) && Grown == 8401);
    bGood &= Test.TestFalse(TEXT("Overflow is refused"), LDSkillGrowth::GrowP2(MAX_int64 / 2, 19000, Grown));

    FLDDemoEffectDefinition Damage;
    Damage.Opcode = uint8(ELDDemoEffectOp::Damage);
    Damage.BaseP2 = 6000; Damage.ADRatioBp = 5000; Damage.DurationMs = 1000; Damage.RatioBp = 2500;
    FLDDemoEffectDefinition Mana;
    Mana.Opcode = uint8(ELDDemoEffectOp::ManaOnHit);
    Mana.BaseP2 = 1200;
    FLDDemoEffectDefinition Timed;
    Timed.Opcode = uint8(ELDDemoEffectOp::TimedStatBonus);
    Timed.DurationMs = 3000;
    Timed.Stats = {{ELDDemoBonusStat::ArmorFlat, 1200, {}}, {ELDDemoBonusStat::AttackSpeedBonusRatio, 3500, {}}};
    FLDDemoRankDefinition Rank;
    Rank.Rank = 1; Rank.ManaCostP2 = 6000; Rank.CooldownMs = 12000;
    Rank.Effects = {Damage, Mana, Timed};
    FLDDemoRankDefinition At5;
    if (!Test.TestTrue(TEXT("A rank grows at level 5"), LDSkillGrowth::GrowRank(Rank, Step, LaterStep, 5, At5)))
    {
        return false;
    }
    bGood &= Test.TestTrue(TEXT("The base amount grows 1.4x; coefficients, ratios and durations do not"),
        At5.Effects[0].BaseP2 == 8400 && At5.Effects[0].ADRatioBp == 5000 && At5.Effects[0].DurationMs == 1000 && At5.Effects[0].RatioBp == 2500);
    bGood &= Test.TestEqual(TEXT("A mana grant does not grow"), At5.Effects[1].BaseP2, int64(1200));
    bGood &= Test.TestTrue(TEXT("A timed bonus grows its points, not its ratios or duration"), At5.Effects[2].Stats[0].Amount == 1680
        && At5.Effects[2].Stats[1].Amount == 3500 && At5.Effects[2].DurationMs == 3000);
    bGood &= Test.TestTrue(TEXT("Mana cost and cooldown never grow"), At5.ManaCostP2 == 6000 && At5.CooldownMs == 12000 && At5.Rank == 1);

    // The snapshot serves every authored rank for every level, grown by the approved step, and refuses anything else.
    for (const FLDDemoAbilityDefinition& Ability : Content.Abilities)
    {
        for (const FLDDemoRankDefinition& Authored : Ability.Ranks)
        {
            for (int32 Level = 1; Level <= Content.Progression.MaxLevel; ++Level)
            {
                const FLDDemoRankDefinition* Served = Catalog->FindRank(Ability, uint8(Authored.Rank), uint8(Level));
                FLDDemoRankDefinition Expected;
                const bool bExpected = LDSkillGrowth::GrowRank(Authored, Step, LaterStep, uint8(Level), Expected);
                bool bSame = Served && bExpected && Served->Effects.Num() == Expected.Effects.Num();
                for (int32 Index = 0; bSame && Index < Expected.Effects.Num(); ++Index)
                {
                    bSame = Served->Effects[Index].BaseP2 == Expected.Effects[Index].BaseP2;
                }
                if (!bSame)
                {
                    bGood &= Test.TestTrue(FString::Printf(TEXT("%s rank %d at level %d is served grown"), *Ability.Id, Authored.Rank, Level), false);
                }
            }
        }
        bGood &= Test.TestNull(Ability.Id + TEXT(" has no level 0 row"), Catalog->FindRank(Ability, 1, 0));
        bGood &= Test.TestNull(Ability.Id + TEXT(" has no row past the top level"), Catalog->FindRank(Ability, 1, uint8(Content.Progression.MaxLevel + 1)));
        bGood &= Test.TestNull(Ability.Id + TEXT(" has no rank past the top"), Catalog->FindRank(Ability, uint8(Content.Progression.MaxSkillRank + 1), 1));
    }
    FLDDemoAbilityDefinition Stranger;
    Stranger.Ranks = {Rank};
    bGood &= Test.TestNull(TEXT("An ability outside the snapshot is refused"), Catalog->FindRank(Stranger, 1, 1));
    return bGood;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthWireTest, "Lockdown.Rules.DemoGrowthWire",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthWireTest::RunTest(const FString& Parameters)
{
    return GrowthTestRunWire(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthRequestsTest, "Lockdown.Rules.DemoGrowthRequests",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthRequestsTest::RunTest(const FString& Parameters)
{
    return GrowthTestRunRequests(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthProgressionTest, "Lockdown.Rules.DemoGrowthProgression",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthProgressionTest::RunTest(const FString& Parameters)
{
    const bool bProgression = GrowthTestRunProgression(*this);
    const bool bFixture = GrowthTestRunDevFixture(*this);
    return bProgression && bFixture;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthOpeningTest, "Lockdown.Rules.DemoGrowthOpening",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthOpeningTest::RunTest(const FString& Parameters)
{
    return GrowthTestRunOpening(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthOpeningPoolTest, "Lockdown.Rules.DemoGrowthOpeningPool",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthOpeningPoolTest::RunTest(const FString& Parameters)
{
    return GrowthTestRunOpeningPool(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthPrivacySchemaTest, "Lockdown.Rules.DemoGrowthPrivacySchema",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthPrivacySchemaTest::RunTest(const FString& Parameters)
{
    return GrowthTestRunPrivacy(*this);
}

// DEV test commands (#78): shape, the fixture gate, admission through the growth request stream, the Transactions commit
// of XP grants through the reward composite, stale match/life and Left refusals, replay without a second commit, cooldown
// reset on the persistent tracks and the pure refill/reset parts; then the map-event reward identity (D26) and a reward
// to a contender who left after the locked fact, on the same growth domain.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoDevTestCommandsTest, "Lockdown.Rules.DemoDevTestCommands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoDevTestCommandsTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(*this, Catalog))
    {
        return false;
    }
    const FLDDemoProgressionDefinition& Progression = Catalog->Get().Progression;
    if (!TestTrue(TEXT("The catalog has a level-2 threshold that two positive grants can stay below"),
        Progression.XPThresholds.Num() >= 2 && Progression.XPThresholds[1] > 2))
    {
        return false;
    }
    // Two DEV grants that together stay one XP below level 2: request 1, then request 2 up to SubLevelXP.
    const int64 SubLevelXP = Progression.XPThresholds[1] - 1;
    const int64 FirstGrantXP = SubLevelXP / 2;

    // ---- Shape ----
    const auto Command = [](uint32 Generation, uint32 Life, uint64 RequestId, ELDDevTestOp Op, int64 Amount)
    {
        FLDDevTestCommand Out;
        Out.Generation = Generation;
        Out.Life = Life;
        Out.RequestId = RequestId;
        Out.Op = Op;
        Out.Amount = Amount;
        return Out;
    };
    GrowthTestReason(*this, TEXT("A positive XP grant is a valid shape"),
        LDDevTest::ValidateShape(Command(1, 1, 1, ELDDevTestOp::GrantExperience, 5)), ELDGrowthReason::None);
    GrowthTestReason(*this, TEXT("Request id 0 is refused"),
        LDDevTest::ValidateShape(Command(1, 1, 0, ELDDevTestOp::GrantExperience, 5)), ELDGrowthReason::InvalidRequest);
    GrowthTestReason(*this, TEXT("An unknown op is refused, not mapped to a neighbour"),
        LDDevTest::ValidateShape(Command(1, 1, 1, static_cast<ELDDevTestOp>(99), 0)), ELDGrowthReason::InvalidRequest);
    // #144 retired GrantGold (2), BuyItem (6) and SellItem (7); their numbers are unknown ops, never reused.
    for (const uint8 Retired : {uint8(2), uint8(6), uint8(7)})
    {
        GrowthTestReason(*this, FString::Printf(TEXT("Retired op %u is refused as unknown"), uint32(Retired)),
            LDDevTest::ValidateShape(Command(1, 1, 1, static_cast<ELDDevTestOp>(Retired), 5)), ELDGrowthReason::InvalidRequest);
        TestFalse(FString::Printf(TEXT("Retired op %u is no known op"), uint32(Retired)), LDDevTest::IsKnownOp(static_cast<ELDDevTestOp>(Retired)));
    }
    GrowthTestReason(*this, TEXT("A zero grant is refused"),
        LDDevTest::ValidateShape(Command(1, 1, 1, ELDDevTestOp::GrantExperience, 0)), ELDGrowthReason::NumericDomain);
    GrowthTestReason(*this, TEXT("A negative grant is refused"),
        LDDevTest::ValidateShape(Command(1, 1, 1, ELDDevTestOp::GrantExperience, -3)), ELDGrowthReason::NumericDomain);
    GrowthTestReason(*this, TEXT("A refill with an amount is refused"),
        LDDevTest::ValidateShape(Command(1, 1, 1, ELDDevTestOp::RefillResources, 1)), ELDGrowthReason::NumericDomain);
    GrowthTestReason(*this, TEXT("A cooldown reset without an amount is a valid shape"),
        LDDevTest::ValidateShape(Command(1, 1, 1, ELDDevTestOp::ResetCooldowns, 0)), ELDGrowthReason::None);
    TestFalse(TEXT("Commands that differ only in amount have different terms"),
        Command(1, 1, 1, ELDDevTestOp::GrantExperience, 5).SameTerms(Command(1, 1, 1, ELDDevTestOp::GrantExperience, 6)));
    const auto Attribute = [&Command](uint64 RequestId, ELDDemoBonusStat Stat, int64 Amount, uint32 Generation = 1)
    {
        FLDDevTestCommand Out = Command(Generation, 1, RequestId, ELDDevTestOp::SetAttribute, Amount);
        Out.Stat = uint8(Stat);
        return Out;
    };
    GrowthTestReason(*this, TEXT("A DEV attribute term of 0 (removal) is a valid shape"),
        LDDevTest::ValidateShape(Attribute(1, ELDDemoBonusStat::CritChanceBonusRatio, 0)), ELDGrowthReason::None);
    GrowthTestReason(*this, TEXT("A negative DEV attribute term is refused"),
        LDDevTest::ValidateShape(Attribute(1, ELDDemoBonusStat::ADFlat, -1)), ELDGrowthReason::NumericDomain);
    GrowthTestReason(*this, TEXT("The timed-only ADBonusRatio is no DEV attribute stat"),
        LDDevTest::ValidateShape(Attribute(1, ELDDemoBonusStat::ADBonusRatio, 100)), ELDGrowthReason::InvalidRequest);
    GrowthTestReason(*this, TEXT("A stat outside the enum is refused"),
        LDDevTest::ValidateShape(Attribute(1, static_cast<ELDDemoBonusStat>(40), 100)), ELDGrowthReason::InvalidRequest);
    FLDDevTestCommand ClearNamed = Command(1, 1, 1, ELDDevTestOp::ClearAttributes, 0);
    ClearNamed.Stat = uint8(ELDDemoBonusStat::APFlat);
    GrowthTestReason(*this, TEXT("Only SetAttribute names a stat"), LDDevTest::ValidateShape(ClearNamed), ELDGrowthReason::InvalidRequest);
    GrowthTestReason(*this, TEXT("A clear with an amount is refused"),
        LDDevTest::ValidateShape(Command(1, 1, 1, ELDDevTestOp::ClearAttributes, 5)), ELDGrowthReason::NumericDomain);
    TestFalse(TEXT("Commands that differ only in stat have different terms"),
        Attribute(1, ELDDemoBonusStat::ADFlat, 5).SameTerms(Attribute(1, ELDDemoBonusStat::APFlat, 5)));

    // ---- Pure combat parts ----
    FLDBodyCombatState::FResources Pool;
    Pool.HpP2 = 40;
    Pool.MaxHpP2 = 100;
    Pool.MpP2 = 0;
    Pool.MaxMpP2 = 60;
    Pool.ShieldP2 = 7;
    Pool.Revision = 3;
    TestTrue(TEXT("A refill reports a change"), ALDCombatDirector::DevRefillPool(Pool));
    TestTrue(TEXT("A refill sets HP and MP to their maxima, keeps the shield and advances the resource revision once"),
        Pool.HpP2 == 100 && Pool.MpP2 == 60 && Pool.ShieldP2 == 7 && Pool.Revision == 4);
    TestFalse(TEXT("A full pool refills without a change or a revision"), ALDCombatDirector::DevRefillPool(Pool) || Pool.Revision != 4);
    TArray<FLDCombatTrack> Tracks;
    FLDCombatTrack& Cooling = Tracks.AddDefaulted_GetRef();
    Cooling.IssuedCooldownId = FGuid(0xD7, 0, 0, 1);
    Cooling.Kind = ELDCombatSourceKind::Learned;
    Cooling.ReadyAtMs = 9000;
    Cooling.LastDurationMs = 8000;
    FLDCombatTrack& Ready = Tracks.AddDefaulted_GetRef();
    Ready.IssuedCooldownId = FGuid(0xD7, 0, 0, 2);
    Ready.ReadyAtMs = 500;
    TestEqual(TEXT("Only the cooling track is reset"), ALDCombatDirector::DevResetTracks(Tracks, 1000), 1);
    TestTrue(TEXT("The reset track is ready now and keeps identity, kind and last duration"),
        Tracks[0].ReadyAtMs == 1000 && Tracks[0].IssuedCooldownId == FGuid(0xD7, 0, 0, 1)
        && Tracks[0].Kind == ELDCombatSourceKind::Learned && Tracks[0].LastDurationMs == 8000);
    TestEqual(TEXT("A track already ready keeps its earlier end"), Tracks[1].ReadyAtMs, int64(500));
    TArray<FLDDemoStatBonus> Layer;
    TestTrue(TEXT("A first DEV term is a change"), ALDCombatDirector::DevApplyAttribute(Layer, false, ELDDemoBonusStat::MoveSpeedBonusRatio, 500));
    TestTrue(TEXT("A second DEV term is a change"), ALDCombatDirector::DevApplyAttribute(Layer, false, ELDDemoBonusStat::ADFlat, 2000));
    TestTrue(TEXT("Range travels as whole cm"), ALDCombatDirector::DevApplyAttribute(Layer, false, ELDDemoBonusStat::AttackRangeCmFlat, 50));
    TestTrue(TEXT("DEV terms stay sorted by stat, range as a Cm rational with no Amount"),
        Layer.Num() == 3 && Layer[0].Stat == ELDDemoBonusStat::ADFlat && Layer[0].Amount == 2000
        && Layer[1].Stat == ELDDemoBonusStat::AttackRangeCmFlat && Layer[1].Amount == 0 && Layer[1].Cm.Numerator == 50
        && Layer[1].Cm.Denominator == 1 && Layer[2].Stat == ELDDemoBonusStat::MoveSpeedBonusRatio);
    TestFalse(TEXT("The same term again is no change"), ALDCombatDirector::DevApplyAttribute(Layer, false, ELDDemoBonusStat::ADFlat, 2000));
    TestTrue(TEXT("A new amount replaces the stat's term"), ALDCombatDirector::DevApplyAttribute(Layer, false, ELDDemoBonusStat::ADFlat, 300)
        && Layer.Num() == 3 && Layer[0].Amount == 300);
    TestTrue(TEXT("Amount 0 removes only that stat's term"), ALDCombatDirector::DevApplyAttribute(Layer, false, ELDDemoBonusStat::ADFlat, 0)
        && Layer.Num() == 2 && Layer[0].Stat == ELDDemoBonusStat::AttackRangeCmFlat);
    TestFalse(TEXT("Removing an absent term is no change"), ALDCombatDirector::DevApplyAttribute(Layer, false, ELDDemoBonusStat::APFlat, 0));
    TestTrue(TEXT("A clear empties the layer"), ALDCombatDirector::DevApplyAttribute(Layer, true, ELDDemoBonusStat::ADFlat, 0) && Layer.IsEmpty());
    TestFalse(TEXT("Clearing an empty layer is no change"), ALDCombatDirector::DevApplyAttribute(Layer, true, ELDDemoBonusStat::ADFlat, 0));

    // ---- Pure projection: the DEV layer is its own additive row ----
    const auto DevTerm = [](ELDDemoBonusStat Stat, int64 Amount)
    {
        FLDDemoStatBonus Out;
        Out.Stat = Stat;
        if (Stat == ELDDemoBonusStat::AttackRangeCmFlat) { Out.Cm.Numerator = Amount; Out.Cm.Denominator = 1; }
        else { Out.Amount = Amount; }
        return Out;
    };
    const auto ProjectWith = [&Catalog](TArray<FLDDemoStatBonus> Dev, TArray<FLDAttributeCardValue> Cards = {})
    {
        FLDAttributeBuildValues Values(1, {}, MoveTemp(Cards));
        Values.DevBonuses = MoveTemp(Dev);
        return LDAttributeProjection::Project(Values, *Catalog);
    };
    const FLDAttributeProjectionResult Plain = ProjectWith({});
    const FLDAttributeProjectionResult WithDev = ProjectWith({DevTerm(ELDDemoBonusStat::ADFlat, 2000),
        DevTerm(ELDDemoBonusStat::AttackSpeedBonusRatio, 2500)});
    if (!TestTrue(TEXT("Level-1 projections with and without a DEV layer succeed"), Plain.IsSuccess() && WithDev.IsSuccess()))
    {
        return false;
    }
    const FLDAttributeProjection& PlainValue = Plain.Value.GetValue();
    const FLDAttributeProjection& DevValue = WithDev.Value.GetValue();
    const FLDAttributeContributions& DevAD = DevValue.Contributions[int32(ELDProjectedAttribute::AD)];
    TestTrue(TEXT("DEV AD +20 points adds exactly 2000 P2 and shows as the Dev row, not as a build source"),
        DevValue.Final.ADP2 == PlainValue.Final.ADP2 + 2000 && DevAD.Dev.Flat.Numerator == 20 && DevAD.Dev.Flat.Denominator == 1
        && DevAD.PermanentChoices.Flat.Numerator == 0 && DevAD.Passives.Flat.Numerator == 0);
    const FLDAttributeContributions& DevSpeed = DevValue.Contributions[int32(ELDProjectedAttribute::AttackSpeed)];
    TestTrue(TEXT("A DEV ratio is an additive ratio of the Dev row"),
        DevSpeed.Dev.AdditiveRatio.Numerator * 4 == DevSpeed.Dev.AdditiveRatio.Denominator
        && DevValue.Final.MaxHPP2 == PlainValue.Final.MaxHPP2);
    FLDTemporaryAttributeTerms Timed;
    FLDDemoStatBonus TimedAD;
    TimedAD.Stat = ELDDemoBonusStat::ADFlat;
    TimedAD.Amount = 100;
    Timed.Bonuses.Add(TimedAD);
    const FLDAttributeProjectionResult TimedDev = LDAttributeProjection::ApplyTemporary(DevValue, Timed, *Catalog);
    TestTrue(TEXT("Temporary terms stack on top of the DEV row"),
        TimedDev.IsSuccess() && TimedDev.Value->Final.ADP2 == DevValue.Final.ADP2 + 100);
    const auto ProjectionError = [this](const TCHAR* What, const FLDAttributeProjectionResult& Result, ELDAttributeProjectionError Expected)
    {
        TestEqual(What, int32(Result.Error.Code), int32(Expected));
    };
    ProjectionError(TEXT("A stat named twice in the DEV layer"),
        ProjectWith({DevTerm(ELDDemoBonusStat::ADFlat, 1), DevTerm(ELDDemoBonusStat::ADFlat, 2)}), ELDAttributeProjectionError::DuplicateSource);
    ProjectionError(TEXT("ADBonusRatio in the DEV layer"), ProjectWith({DevTerm(ELDDemoBonusStat::ADBonusRatio, 100)}),
        ELDAttributeProjectionError::InputDomain);
    ProjectionError(TEXT("A negative DEV term"), ProjectWith({DevTerm(ELDDemoBonusStat::APFlat, -100)}),
        ELDAttributeProjectionError::InputDomain);
    const FLDDemoContentParameters& Bounds = Catalog->Get().Parameters;
    ProjectionError(TEXT("A DEV term past the catalog maximum refuses rather than clamps"),
        ProjectWith({DevTerm(ELDDemoBonusStat::MaxHPFlat, Bounds.MaxHP.MaxP2)}), ELDAttributeProjectionError::OutOfBounds);

    // ---- Authority: isolated match with the growth domain ----
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Isolated DEV test world"), World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Mode = World->SpawnActor<ALDGameMode>();
    auto* Public = World->SpawnActor<ALDGameState>();
    auto* Driver = World->SpawnActor<ALDCombatDirector>();
    auto* Growth = World->SpawnActor<ALDGrowthDirector>();
    auto* Development = World->SpawnActor<ALDDevelopmentDirector>();
    if (!Mode || !Public || !Driver || !Growth || !Development) { AddError(TEXT("Fixture actors failed to spawn")); return false; }
    Mode->bDemoWorld = Mode->bEntryAccepted = Mode->bAuthorityInitialized = true;
    Mode->GameState = Public;
    // The directors resolve the core through the world's authority game mode, exactly as in a hosted match.
    World->CopyGameState(Mode, Public);
    Mode->MatchContext = {4, FGuid::NewGuid()};
    Mode->MatchPhase = ELDMatchPhase::Development;
    Mode->Combat = Driver;
    Mode->Growth = Growth;
    Mode->Development = Development;
    Mode->DemoContent = Catalog;
    Mode->AuthorityStep = {Mode->MatchContext, 1, 0, 0};
    const FGuid Tester(0xD7, 0, 0, 0x10);
    const FGuid Leaver(0xD7, 0, 0, 0x20);
    for (const FGuid& Id : {Tester, Leaver})
    {
        auto& Life = Mode->Lives.Add(Id);
        Life.Kind = ELDVictimKind::Contender;
        Life.Phase = Id == Leaver ? ELDLifePhase::Left : ELDLifePhase::Unspawned;
        Life.Life = 1;
        Life.Revision = 2;
    }
    FString Error;
    Growth->Match = Mode->MatchContext;
    Growth->Roster = {Tester, Leaver};
    Growth->Content = Catalog;
    if (!TestTrue(TEXT("Growth opens for the roster"), Development->BeginMatch(Mode->MatchContext, Growth->Roster, Catalog, Error)))
    { AddError(Error); return false; }
    // The tester's persistent combat record: one learned track still cooling down, another one already ready.
    FLDCombatEntityRecord& CombatRecord = Driver->Records.Add(Tester);
    CombatRecord.Kind = ELDVictimKind::Contender;
    CombatRecord.Entity = Tester;
    CombatRecord.Build.Level = 1;
    CombatRecord.Projection = PlainValue;
    FLDCombatTrack& Learned = CombatRecord.Tracks.AddDefaulted_GetRef();
    Learned.IssuedCooldownId = FGuid(0xD7, 0, 1, 1);
    Learned.Kind = ELDCombatSourceKind::Learned;
    Learned.ReadyAtMs = 60000;
    FLDCombatTrack& Spent = CombatRecord.Tracks.AddDefaulted_GetRef();
    Spent.IssuedCooldownId = FGuid(0xD7, 0, 1, 2);
    Spent.Kind = ELDCombatSourceKind::Learned;
    Spent.ReadyAtMs = 0;
    World->Tick(LEVELTICK_TimeOnly, .025f);

    // One 25 ms tick of native time; every boundary below advances 1 ms inside it.
    int64 StepAtMs = 0;
    const auto RunPhase = [&](ELDCommitPhase Phase, TFunctionRef<bool(const FLDGameplayWritePermit&)> Execute)
    {
        StepAtMs += 1;
        Error.Reset();
        const ELDCommitReject Begin = Mode->BeginAuthorityStep(*Driver, Mode->MatchContext, StepAtMs);
        if (Begin != ELDCommitReject::None)
        {
            AddError(FString::Printf(TEXT("The boundary at %lld ms did not begin: reject %d"), StepAtMs, int32(Begin)));
            return false;
        }
        const bool Ran = Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Motion, [](const auto&) { return true; }, Error)
            && Mode->ExecuteAuthorityPhase(*Driver, Phase, Execute, Error)
            && Mode->FinishAuthorityStep(*Driver, Error);
        if (!Ran) { AddError(FString::Printf(TEXT("Boundary phase %u failed: %s"), uint32(Phase), *Error)); }
        return Ran;
    };
    const auto RunTransactions = [&]()
    {
        return RunPhase(ELDCommitPhase::Transactions,
            [&](const FLDGameplayWritePermit& Permit) { return Growth->ProcessTransactions(Permit, Error); });
    };
    const auto XP = [&](const FGuid& Id) { const FLDProgressionState* P = Development->FindProgression(Id); return P ? P->CumulativeXP : -1; };
    const auto Status = [&](const FString& What, const FLDDevTestResult& Result, ELDAdmissionStatus Expected, ELDGrowthReason Reason)
    {
        GrowthTestEnum(*this, What + TEXT(" (status)"), Result.Status, Expected);
        GrowthTestReason(*this, What + TEXT(" (reason)"), LDGrowthWire::Reason(Result.Reason), Reason);
    };
    const auto LastReceipt = [&](const FGuid& Id) -> const FLDDevTestReceipt*
    {
        const auto* Record = Growth->DevTests.Find(Id);
        return Record && !Record->Unpublished.IsEmpty() ? &Record->Unpublished.Last() : nullptr;
    };
    const uint32 Gen = Mode->MatchContext.Generation;

    // The gate: the rules run carries no -LDDevGrowthFixture switch, so the public entry admits nothing and says why.
    TestFalse(TEXT("The rules run carries no -LDDevGrowthFixture switch"), LDDevGrowthFixture::IsSwitchPresent());
    const FLDDevTestResult Gated = Growth->QueueDevTest(Tester, Command(Gen, 1, 1, ELDDevTestOp::GrantExperience, FirstGrantXP), true);
    Status(TEXT("Without the switch a DEV test command is refused"), Gated, ELDAdmissionStatus::AdmissionRejected,
        ELDGrowthReason::NotAuthority);
    TestTrue(TEXT("The refusal names the switch and no stream was opened"),
        Gated.Detail.Contains(TEXT("-LDDevGrowthFixture")) && !Growth->DevTests.Contains(Tester));

    // Admission behind the gate.
    Status(TEXT("A command of another match"),
        Growth->AdmitDevTest(Tester, Command(Gen + 1, 1, 1, ELDDevTestOp::GrantExperience, FirstGrantXP)),
        ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::OldMatch);
    Status(TEXT("A malformed command"), Growth->AdmitDevTest(Tester, Command(Gen, 1, 1, ELDDevTestOp::GrantExperience, 0)),
        ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::NumericDomain);
    Status(TEXT("A caller that is not a contender"),
        Growth->AdmitDevTest(FGuid(0xD7, 0, 0, 0x99), Command(Gen, 1, 1, ELDDevTestOp::GrantExperience, FirstGrantXP)),
        ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::NotContender);
    Status(TEXT("A first request that skips id 1"),
        Growth->AdmitDevTest(Tester, Command(Gen, 1, 2, ELDDevTestOp::GrantExperience, FirstGrantXP)),
        ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::OutOfOrder);
    const FLDDevTestCommand FirstGrant = Command(Gen, 1, 1, ELDDevTestOp::GrantExperience, FirstGrantXP);
    const FLDDevTestResult Accepted = Growth->AdmitDevTest(Tester, FirstGrant);
    Status(TEXT("Request 1 is accepted"), Accepted, ELDAdmissionStatus::AcceptedPending, ELDGrowthReason::None);
    TestTrue(TEXT("The accepted request reserves its sequence"), Accepted.bSequenceReserved);
    Status(TEXT("The same request again"), Growth->AdmitDevTest(Tester, FirstGrant), ELDAdmissionStatus::AcceptedPending,
        ELDGrowthReason::None);
    Status(TEXT("Request 1 with other terms"),
        Growth->AdmitDevTest(Tester, Command(Gen, 1, 1, ELDDevTestOp::GrantExperience, FirstGrantXP + 1)),
        ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::RequestConflict);
    Status(TEXT("Request 2 while 1 is pending"), Growth->AdmitDevTest(Tester, Command(Gen, 1, 2, ELDDevTestOp::GrantExperience, 5)),
        ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::RequestInFlight);
    TestEqual(TEXT("Nothing is granted before the Transactions phase"), XP(Tester), int64(0));

    if (!RunTransactions()) { return false; }
    const FLDDevTestReceipt* Receipt = LastReceipt(Tester);
    TestTrue(TEXT("The XP grant commits once with an applied receipt that names the grant"), Receipt && Receipt->Header.RequestId == 1
        && Receipt->Header.Outcome == ELDReceiptOutcome::Applied && Receipt->Op == ELDDevTestOp::GrantExperience
        && Receipt->Detail.Contains(FString::Printf(TEXT("xp +%lld"), FirstGrantXP)));
    TestEqual(TEXT("Cumulative XP holds the first grant"), XP(Tester), FirstGrantXP);
    const FLDDevTestResult Replay = Growth->AdmitDevTest(Tester, FirstGrant);
    Status(TEXT("A replay of the committed request"), Replay, ELDAdmissionStatus::Final, ELDGrowthReason::None);
    TestTrue(TEXT("The replay answers with the original receipt"), Replay.bHasReceipt && Replay.Receipt.Header.RequestId == 1
        && Replay.Receipt.Header.Outcome == ELDReceiptOutcome::Applied);
    if (!RunTransactions()) { return false; }
    TestEqual(TEXT("The replay commits nothing a second time"), XP(Tester), FirstGrantXP);

    // XP below the next level: cumulative XP only; the level and the build stay.
    Status(TEXT("Request 2: XP below the next level"),
        Growth->AdmitDevTest(Tester, Command(Gen, 1, 2, ELDDevTestOp::GrantExperience, SubLevelXP - FirstGrantXP)),
        ELDAdmissionStatus::AcceptedPending, ELDGrowthReason::None);
    if (!RunTransactions()) { return false; }
    Receipt = LastReceipt(Tester);
    TestTrue(TEXT("The XP grant applies"), Receipt && Receipt->Header.RequestId == 2 && Receipt->Header.Outcome == ELDReceiptOutcome::Applied);
    TestEqual(TEXT("Cumulative XP holds both grants"), XP(Tester), SubLevelXP);
    TestTrue(TEXT("The level is unchanged below the threshold"),
        Development->FindBuild(Tester) && Development->FindBuild(Tester)->Level == 1);

    // A command of an earlier life never applies.
    Status(TEXT("Request 3 names life 0"), Growth->AdmitDevTest(Tester, Command(Gen, 0, 3, ELDDevTestOp::GrantExperience, 9)),
        ELDAdmissionStatus::AcceptedPending, ELDGrowthReason::None);
    if (!RunTransactions()) { return false; }
    Receipt = LastReceipt(Tester);
    TestTrue(TEXT("The old-life command is rejected"), Receipt && Receipt->Header.RequestId == 3
        && Receipt->Header.Outcome == ELDReceiptOutcome::Rejected
        && LDGrowthWire::Reason(Receipt->Header.Reason) == ELDGrowthReason::OldLife);
    TestEqual(TEXT("The rejected command granted nothing"), XP(Tester), SubLevelXP);

    // Refill needs a living body: a contender that is not Alive is refused (the Alive path is the pure DevRefillPool above).
    Status(TEXT("Request 4: refill while unspawned"), Growth->AdmitDevTest(Tester, Command(Gen, 1, 4, ELDDevTestOp::RefillResources, 0)),
        ELDAdmissionStatus::AcceptedPending, ELDGrowthReason::None);
    if (!RunTransactions()) { return false; }
    Receipt = LastReceipt(Tester);
    TestTrue(TEXT("A refill without a living phase is rejected as Dead"), Receipt && Receipt->Header.RequestId == 4
        && LDGrowthWire::Reason(Receipt->Header.Reason) == ELDGrowthReason::Dead);
    // Cooldown reset on the persistent record, at the commit boundary.
    Status(TEXT("Request 5: reset cooldowns"), Growth->AdmitDevTest(Tester, Command(Gen, 1, 5, ELDDevTestOp::ResetCooldowns, 0)),
        ELDAdmissionStatus::AcceptedPending, ELDGrowthReason::None);
    if (!RunTransactions()) { return false; }
    Receipt = LastReceipt(Tester);
    TestTrue(TEXT("The reset applies"), Receipt && Receipt->Header.RequestId == 5 && Receipt->Header.Outcome == ELDReceiptOutcome::Applied);
    const FLDCombatEntityRecord& AfterReset = Driver->Records.FindChecked(Tester);
    TestTrue(TEXT("The cooling learned track is ready at the commit boundary"),
        AfterReset.Tracks.Num() == 2 && AfterReset.Tracks[0].ReadyAtMs == StepAtMs
        && AfterReset.Tracks[0].IssuedCooldownId == FGuid(0xD7, 0, 1, 1));
    TestEqual(TEXT("A ready track keeps its earlier end"), AfterReset.Tracks[1].ReadyAtMs, int64(0));

    // A contender that left gets nothing from a DEV command.
    Status(TEXT("The leaver's request 1"), Growth->AdmitDevTest(Leaver, Command(Gen, 1, 1, ELDDevTestOp::GrantExperience, FirstGrantXP)),
        ELDAdmissionStatus::AcceptedPending, ELDGrowthReason::None);
    const int64 LeaverXP = XP(Leaver);
    if (!RunTransactions()) { return false; }
    Receipt = LastReceipt(Leaver);
    TestTrue(TEXT("A command of a contender that left is rejected as LeftMatch"), Receipt
        && LDGrowthWire::Reason(Receipt->Header.Reason) == ELDGrowthReason::LeftMatch);
    TestEqual(TEXT("The leaver received no XP"), XP(Leaver), LeaverXP);

    // ---- Learn: any legal card as the head opportunity's choice, with every choice rule ----
    const auto Learn = [&](uint64 RequestId, const TCHAR* Id, uint8 Slot)
    {
        FLDDevTestCommand Out = Command(Gen, 1, RequestId, ELDDevTestOp::LearnOption, 0);
        FLDDefinitionWire::Encode(FName(Id), Out.Definition);
        Out.Slot = Slot;
        return Out;
    };
    FLDDevTestCommand BadSlot = Learn(1, TEXT("attr_protein"), 4);
    GrowthTestReason(*this, TEXT("A learn names slot 0..3"), LDDevTest::ValidateShape(BadSlot), ELDGrowthReason::InvalidRequest);
    FLDDevTestCommand NoDefinition = Command(Gen, 1, 1, ELDDevTestOp::LearnOption, 0);
    GrowthTestReason(*this, TEXT("A learn names a definition"), LDDevTest::ValidateShape(NoDefinition), ELDGrowthReason::InvalidRequest);

    const auto Commit = [&](const TCHAR* What, const FLDDevTestCommand& Sent, ELDGrowthReason Expected) -> const FLDDevTestReceipt*
    {
        Status(What, Growth->AdmitDevTest(Tester, Sent), ELDAdmissionStatus::AcceptedPending, ELDGrowthReason::None);
        if (!RunTransactions()) { return nullptr; }
        const FLDDevTestReceipt* Final = LastReceipt(Tester);
        if (TestTrue(FString(What) + TEXT(" finalizes"), Final && Final->Header.RequestId == Sent.RequestId))
        {
            GrowthTestReason(*this, FString(What) + TEXT(" (commit)"), LDGrowthWire::Reason(Final->Header.Reason), Expected);
            return Final;
        }
        return nullptr;
    };
    if (!Commit(TEXT("Request 6: learn while unspawned"), Learn(6, TEXT("attr_protein"), 0), ELDGrowthReason::LifePhaseChanged))
    { return false; }
    Mode->Lives.FindChecked(Tester).Phase = ELDLifePhase::Dead;
    const FLDProgressionState* Before = Development->FindProgression(Tester);
    const uint8 ConsumedBefore = Before ? Before->ConsumedThrough : 0;
    if (!Commit(TEXT("Request 7: an id that is no legal option"), Learn(7, TEXT("attr_unknown"), 0), ELDGrowthReason::InvalidOption))
    { return false; }
    if (!Commit(TEXT("Request 8: replacing an empty Q"), Learn(8, TEXT("attr_protein"), 1), ELDGrowthReason::ReplacementMismatch))
    { return false; }

    // ---- DEV attribute layer: Combat's installed build, without a growth build revision ----
    const FLDCombatEntityRecord& Fighter = Driver->Records.FindChecked(Tester);
    const FLDExpectedBuildVersions InstalledBefore = Fighter.Installed;
    const uint64 GrowthRevisionBefore = Development->FindBuild(Tester)->Revision;
    const FLDDevTestReceipt* AttrReceipt = Commit(TEXT("Request 9: DEV AD +20 while dead"),
        Attribute(9, ELDDemoBonusStat::ADFlat, 2000, Gen), ELDGrowthReason::None);
    if (!AttrReceipt) { return false; }
    TestTrue(TEXT("The receipt names the stat and amount"), AttrReceipt->Stat == uint8(ELDDemoBonusStat::ADFlat) && AttrReceipt->Amount == 2000);
    TestTrue(TEXT("The persistent build holds the DEV term and its projection shows it as the Dev row"),
        Fighter.Build.DevBonuses.Num() == 1 && Fighter.Build.DevBonuses[0].Amount == 2000
        && Fighter.Projection.Final.ADP2 == PlainValue.Final.ADP2 + 2000
        && Fighter.Projection.Contributions[int32(ELDProjectedAttribute::AD)].Dev.Flat.Numerator == 20);
    TestTrue(TEXT("Neither Combat's installed build revision nor the growth build revision moves"),
        Fighter.Installed.BuildRevision == InstalledBefore.BuildRevision
        && Development->FindBuild(Tester)->Revision == GrowthRevisionBefore);
    const FLDDevTestReceipt* TooMuch = Commit(TEXT("Request 10: DEV max HP past the catalog maximum"),
        Attribute(10, ELDDemoBonusStat::MaxHPFlat, Bounds.MaxHP.MaxP2, Gen), ELDGrowthReason::NumericDomain);
    TestTrue(TEXT("The refusal explains the bound and the layer is unchanged"), TooMuch && TooMuch->Detail.Contains(TEXT("MaxHP"))
        && Fighter.Build.DevBonuses.Num() == 1 && Fighter.Projection.Final.MaxHPP2 == PlainValue.Final.MaxHPP2);
    FLDCombatCaller GrowthCaller;
    GrowthCaller.Match = Mode->GetMatchContext();
    GrowthCaller.Kind = ELDVictimKind::Contender;
    GrowthCaller.EntityId = Tester;
    FLDBuildProjectionInput Smuggled = Fighter.Build;
    Smuggled.ResultBuildRevision = Fighter.Installed.BuildRevision + 1;
    TestEqual(TEXT("A Growth build change that names a DEV layer"),
        int32(Driver->PrepareBuildChange(GrowthCaller, Smuggled, Fighter.Installed).Error), int32(ELDCombatPrepareError::InvalidInput));

    const FLDDevTestReceipt* LearnedReceipt = Commit(TEXT("Request 11: an attribute card while dead"), Learn(11, TEXT("attr_protein"), 0),
        ELDGrowthReason::None);
    if (!LearnedReceipt) { return false; }
    TestTrue(TEXT("The receipt names the learned definition"), LearnedReceipt->Definition == FName(TEXT("attr_protein")));
    const FLDGrowthBuild* AfterLearn = Development->FindBuild(Tester);
    const FLDProgressionState* ProgressionAfter = Development->FindProgression(Tester);
    TestTrue(TEXT("The card is in the build and the opening opportunity is consumed"),
        AfterLearn && AfterLearn->Cards.Num() == 1 && AfterLearn->Cards[0].Id == FName(TEXT("attr_protein"))
        && AfterLearn->Cards[0].Count == 1 && ProgressionAfter && ProgressionAfter->ConsumedThrough == ConsumedBefore + 1
        && ProgressionAfter->Records.Num() == 1);
    TestTrue(TEXT("Combat installed the new build"), Driver->Records.FindChecked(Tester).Installed.BuildRevision == AfterLearn->Revision);
    const FLDAttributeProjectionResult CardOnly = ProjectWith({}, {FLDAttributeCardValue(TEXT("attr_protein"), 1)});
    TestTrue(TEXT("The real build change keeps the installed DEV term on top of the new card"),
        CardOnly.IsSuccess() && Fighter.Build.DevBonuses.Num() == 1 && Fighter.Build.AttributeCards.Num() == 1
        && Fighter.Projection.Final.ADP2 == CardOnly.Value->Final.ADP2 + 2000);
    const FLDDevTestReceipt* NoOpportunity = Commit(TEXT("Request 12: learn with no opportunity left"),
        Learn(12, TEXT("attr_all_nighter"), 0), ELDGrowthReason::NoOffer);
    TestTrue(TEXT("The refusal says to raise the level first"), NoOpportunity && NoOpportunity->Detail.Contains(TEXT("raise the level")));
    TestTrue(TEXT("Nothing more was learned"), Development->FindBuild(Tester)->Cards.Num() == 1);
    if (!Commit(TEXT("Request 13: clear the DEV layer"), Command(Gen, 1, 13, ELDDevTestOp::ClearAttributes, 0), ELDGrowthReason::None))
    { return false; }
    TestTrue(TEXT("A clear removes only the DEV term; the learned card stays"),
        Fighter.Build.DevBonuses.IsEmpty() && Fighter.Build.AttributeCards.Num() == 1
        && Fighter.Projection.Final.ADP2 == CardOnly.Value->Final.ADP2);

    // ---- Reset to the match start: the BeginMatch growth state and an empty Combat build, prepared against each other ----
    GrowthTestReason(*this, TEXT("A reset names nothing"),
        LDDevTest::ValidateShape(Command(Gen, 1, 1, ELDDevTestOp::ResetToMatchStart, 5)), ELDGrowthReason::NumericDomain);
    if (!TestTrue(TEXT("The tester holds XP and a card before the reset"),
        XP(Tester) == SubLevelXP && Development->FindBuild(Tester)->Cards.Num() == 1))
    { return false; }
    const uint64 BuildRevisionBeforeReset = Development->FindBuild(Tester)->Revision;
    const uint64 OfferRevisionBeforeReset = Development->FindProgression(Tester)->LastOfferRevision;
    const FLDDevTestReceipt* ResetReceipt = Commit(TEXT("Request 14: reset to the match start while dead"),
        Command(Gen, 1, 14, ELDDevTestOp::ResetToMatchStart, 0), ELDGrowthReason::None);
    if (!ResetReceipt) { return false; }
    TestTrue(TEXT("The reset receipt explains what changed"), ResetReceipt->Detail.Contains(TEXT("->1"))
        && ResetReceipt->Detail.Contains(TEXT("cooldown tracks dropped")));
    const FLDGrowthBuild* ResetBuild = Development->FindBuild(Tester);
    const FLDProgressionState* ResetProgression = Development->FindProgression(Tester);
    TestTrue(TEXT("Growth is back at level 1 with no XP, skills or cards and a higher build revision"),
        ResetBuild && ResetBuild->Level == 1 && ResetBuild->Skills.IsEmpty() && ResetBuild->Cards.IsEmpty()
        && ResetBuild->Revision > BuildRevisionBeforeReset && ResetProgression && ResetProgression->CumulativeXP == 0);
    TestTrue(TEXT("Only the opening opportunity is open, with a fresh opening offer"),
        ResetProgression && ResetProgression->EarnedThrough == 1 && ResetProgression->ConsumedThrough == 0
        && ResetProgression->Records.IsEmpty() && ResetProgression->Offer.IsSet() && ResetProgression->Offer->Ordinal == 1
        && ResetProgression->Offer->Revision > OfferRevisionBeforeReset && ResetProgression->Offer->Options.Num() == 3);
    TestTrue(TEXT("Combat installed the empty level-1 build at the growth build revision, without a DEV layer"),
        Fighter.Build.Level == 1 && Fighter.Build.AttributeCards.IsEmpty() && Fighter.Build.Actives.IsEmpty()
        && Fighter.Build.DevBonuses.IsEmpty() && Fighter.Installed.BuildRevision == ResetBuild->Revision
        && Fighter.Projection.Final.ADP2 == PlainValue.Final.ADP2);
    TestTrue(TEXT("Every learned cooldown track is dropped"), !Fighter.Tracks.ContainsByPredicate(
        [](const FLDCombatTrack& Track) { return Track.Kind != ELDCombatSourceKind::Innate; }));
    TArray<FLDCombatTrack> Mixed;
    Mixed.AddDefaulted_GetRef().Kind = ELDCombatSourceKind::Learned;
    FLDCombatTrack& Innate = Mixed.AddDefaulted_GetRef();
    Innate.Kind = ELDCombatSourceKind::Innate;
    Innate.ReadyAtMs = 9000;
    Mixed.AddDefaulted_GetRef().Kind = ELDCombatSourceKind::Learned;
    TestTrue(TEXT("The pure track drop keeps only the innate dash, ready now"), ALDCombatDirector::DevDropTracks(Mixed, 1000) == 2
        && Mixed.Num() == 1 && Mixed[0].Kind == ELDCombatSourceKind::Innate && Mixed[0].ReadyAtMs == 1000);
    const FLDDevTestReceipt* Relearned = Commit(TEXT("Request 15: the fresh opening opportunity takes a card again"),
        Learn(15, TEXT("attr_protein"), 0), ELDGrowthReason::None);
    TestTrue(TEXT("After the reset the card is learned once more"), Relearned && Development->FindBuild(Tester)->Cards.Num() == 1
        && Development->FindProgression(Tester)->ConsumedThrough == 1);

    // ---- Match-wide ops: Listen Host connection only ----
    TestTrue(TEXT("Camp refresh and early capture are the match-wide ops"), LDDevTest::IsMatchWide(ELDDevTestOp::RefreshCamps)
        && LDDevTest::IsMatchWide(ELDDevTestOp::OpenCapture) && !LDDevTest::IsMatchWide(ELDDevTestOp::GrantExperience));
    GrowthTestReason(*this, TEXT("A camp refresh names nothing"),
        LDDevTest::ValidateShape(Command(Gen, 1, 1, ELDDevTestOp::RefreshCamps, 1)), ELDGrowthReason::NumericDomain);
    Status(TEXT("A camp refresh from an owning client"), Growth->AdmitDevTest(Tester, Command(Gen, 1, 16, ELDDevTestOp::RefreshCamps, 0)),
        ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::NotAuthority);
    Status(TEXT("An early capture from an owning client"), Growth->AdmitDevTest(Tester, Command(Gen, 1, 16, ELDDevTestOp::OpenCapture, 0)),
        ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::NotAuthority);
    const auto HostCommit = [&](const TCHAR* What, const FLDDevTestCommand& Sent, ELDGrowthReason Expected) -> const FLDDevTestReceipt*
    {
        Status(What, Growth->AdmitDevTest(Tester, Sent, true), ELDAdmissionStatus::AcceptedPending, ELDGrowthReason::None);
        if (!RunTransactions()) { return nullptr; }
        const FLDDevTestReceipt* Final = LastReceipt(Tester);
        if (TestTrue(FString(What) + TEXT(" finalizes"), Final && Final->Header.RequestId == Sent.RequestId))
        {
            GrowthTestReason(*this, FString(What) + TEXT(" (commit)"), LDGrowthWire::Reason(Final->Header.Reason), Expected);
            return Final;
        }
        return nullptr;
    };
    const FLDDevTestReceipt* NoCamps = HostCommit(TEXT("Request 16: the Host's camp refresh without a camp director"),
        Command(Gen, 1, 16, ELDDevTestOp::RefreshCamps, 0), ELDGrowthReason::DataNotReady);
    TestTrue(TEXT("The refusal names the missing camps"), NoCamps && NoCamps->Detail.Contains(TEXT("camp director")));
    if (!TestTrue(TEXT("The capture reducer begins far in the future"), Mode->CaptureReducer.Begin(1000000, 500, 500, Error))) { return false; }
    Mode->MatchPhase = ELDMatchPhase::CaptureOpen;
    if (!HostCommit(TEXT("Request 17: an early capture outside development"), Command(Gen, 1, 17, ELDDevTestOp::OpenCapture, 0),
        ELDGrowthReason::StaleContext)) { return false; }
    TestEqual(TEXT("A refused early capture keeps the scheduled opening"), Mode->CaptureReducer.GetOpenAtMs(), int64(1000000));
    Mode->MatchPhase = ELDMatchPhase::Development;
    if (!HostCommit(TEXT("Request 18: the Host's early capture during development"), Command(Gen, 1, 18, ELDDevTestOp::OpenCapture, 0),
        ELDGrowthReason::None)) { return false; }
    TestTrue(TEXT("The circle opens at the commit boundary and the driver stops there"),
        Mode->CaptureReducer.GetOpenAtMs() == StepAtMs && Mode->NextCaptureDueMs() == StepAtMs && !Mode->CaptureReducer.IsOpen());
    FLDCaptureReducer Circle;
    FString CircleError;
    TestFalse(TEXT("An inactive reducer opens nothing early"), Circle.DevOpenAt(10, CircleError));
    TestTrue(TEXT("A pure reducer begins"), Circle.Begin(5000, 500, 500, CircleError));
    TestFalse(TEXT("An early opening must be earlier than the schedule"), Circle.DevOpenAt(5000, CircleError));
    TOptional<FLDCaptureProposal> Proposal;
    TestTrue(TEXT("An early opening is the next CloseBoundary's opening"), Circle.DevOpenAt(1200, CircleError)
        && Circle.NextDueMs() == 1200 && Circle.CloseBoundary(1200, Proposal, CircleError) && Circle.IsOpen());
    TestFalse(TEXT("An open circle opens nothing early"), Circle.DevOpenAt(1300, CircleError));

    // ---- D26 (#79) map event reward: (Event, Serial) is the business identity, so the match pays each event once ----
    const FName Event(TEXT("airdrop"));
    const int64 EventXP = Bounds.AirdropRewardXP;
    if (!TestTrue(TEXT("The approved airdrop pays positive XP"), EventXP > 0)) { return false; }
    ELDRewardOutcome EventOutcome = ELDRewardOutcome::None;
    ELDGrowthReason EventReason = ELDGrowthReason::None;
    FLDRewardReceipt EventReceipt;
    const auto ClaimEvent = [&](const FGuid& Recipient, FName Name, uint64 Serial, int64 Amount)
    {
        EventOutcome = ELDRewardOutcome::None;
        EventReason = ELDGrowthReason::None;
        EventReceipt = FLDRewardReceipt();
        return RunPhase(ELDCommitPhase::Transactions, [&](const FLDGameplayWritePermit& Permit)
        {
            EventOutcome = Growth->CommitEventReward(Recipient, Name, Serial, Amount, Permit, EventReceipt, EventReason);
            return true;
        });
    };
    const int64 EventXPBefore = XP(Tester);
    uint8 EventLevel = 0;
    if (!TestTrue(TEXT("The level after the event XP is defined by the approved curve"),
        LDProgression::LevelForXP(Progression, EventXPBefore + EventXP, EventLevel) == ELDGrowthReason::None))
    { return false; }
    if (!ClaimEvent(Tester, Event, 1, EventXP)) { return false; }
    TestTrue(TEXT("The first claim of airdrop 1 is granted to its recipient in one reward composite"),
        EventOutcome == ELDRewardOutcome::Granted && EventReason == ELDGrowthReason::None && EventReceipt.Recipient == Tester
        && EventReceipt.XPDelta == EventXP && EventReceipt.LevelBefore == 1 && EventReceipt.LevelAfter == EventLevel);
    TestEqual(TEXT("The event XP reaches the recipient's growth"), XP(Tester), EventXPBefore + EventXP);
    TestTrue(TEXT("Its level follows the approved curve and Combat installed the grown build"),
        Development->FindBuild(Tester)->Level == EventLevel
        && Driver->Records.FindChecked(Tester).Installed.BuildRevision == Development->FindBuild(Tester)->Revision);
    const uint64 ProgressionRevisionAfterEvent = Development->FindProgression(Tester)->Revision;
    const uint64 BuildRevisionAfterEvent = Development->FindBuild(Tester)->Revision;
    if (!ClaimEvent(Tester, Event, 1, EventXP)) { return false; }
    TestTrue(TEXT("A second claim of the same (Event, Serial) is permanently rejected as DataInvalid"),
        EventOutcome == ELDRewardOutcome::PermanentlyRejected && EventReason == ELDGrowthReason::DataInvalid);
    TestTrue(TEXT("The refused second claim pays nothing and moves no revision"), XP(Tester) == EventXPBefore + EventXP
        && Development->FindProgression(Tester)->Revision == ProgressionRevisionAfterEvent
        && Development->FindBuild(Tester)->Revision == BuildRevisionAfterEvent);
    const int64 LeaverXPBeforeEvent = XP(Leaver);
    if (!ClaimEvent(Leaver, Event, 1, EventXP)) { return false; }
    TestTrue(TEXT("The same (Event, Serial) claimed by another contender is refused too: the event pays once, whoever claims it"),
        EventOutcome == ELDRewardOutcome::PermanentlyRejected && EventReason == ELDGrowthReason::DataInvalid
        && XP(Leaver) == LeaverXPBeforeEvent);
    const int64 NextXPBefore = XP(Tester);
    if (!ClaimEvent(Tester, Event, 2, EventXP)) { return false; }
    TestTrue(TEXT("A different serial of the same event pays"), EventOutcome == ELDRewardOutcome::Granted
        && EventReceipt.XPDelta == EventXP && XP(Tester) == NextXPBefore + EventXP);
    const int64 AfterSecondEvent = XP(Tester);
    if (!ClaimEvent(Tester, NAME_None, 3, EventXP)) { return false; }
    TestTrue(TEXT("An event without a name is refused"), EventOutcome == ELDRewardOutcome::PermanentlyRejected);
    if (!ClaimEvent(Tester, Event, 0, EventXP)) { return false; }
    TestTrue(TEXT("Event serial 0 is refused"), EventOutcome == ELDRewardOutcome::PermanentlyRejected);
    if (!ClaimEvent(Tester, Event, 3, 0)) { return false; }
    TestTrue(TEXT("An event without XP is refused"), EventOutcome == ELDRewardOutcome::PermanentlyRejected);
    TestEqual(TEXT("Malformed event claims pay nothing"), XP(Tester), AfterSecondEvent);
    if (!ClaimEvent(Tester, Event, 3, EventXP)) { return false; }
    TestTrue(TEXT("A malformed claim does not spend its serial"), EventOutcome == ELDRewardOutcome::Granted
        && XP(Tester) == AfterSecondEvent + EventXP);

    // ---- A camp kill locked while its killer was in the match pays him after he left, without a Combat projection ----
    if (!TestTrue(TEXT("The catalog has a monster"), !Catalog->Get().Monsters.IsEmpty())) { return false; }
    const FName MonsterId(*Catalog->Get().Monsters[0].Id);
    LDProgression::FRewardAmounts MonsterReward;
    if (!TestTrue(TEXT("The first approved monster pays positive XP at stage 0"),
        LDProgression::MonsterKillReward(*Catalog, MonsterId, 0, MonsterReward) == ELDGrowthReason::None && MonsterReward.XP > 0))
    { return false; }
    // Bring the leaver to one kill short of level 2 when the kill alone would not cross it, so the kill raises the level and
    // its build would need a projection if the recipient still had a body.
    const int64 LeaverShortfall = Progression.XPThresholds[1] - MonsterReward.XP - XP(Leaver);
    if (LeaverShortfall > 0)
    {
        if (!ClaimEvent(Leaver, Event, 4, LeaverShortfall)) { return false; }
        if (!TestTrue(TEXT("An event reward to a contender who left settles without a body"),
            EventOutcome == ELDRewardOutcome::Granted && XP(Leaver) == LeaverXPBeforeEvent + LeaverShortfall))
        { return false; }
    }
    FLDCommittedDeath Owed;
    Owed.Key.LedgerMatch = Mode->MatchContext.LedgerMatch;
    Owed.Key.Victim.Kind = ELDVictimKind::CampMember;
    Owed.Key.Victim.EntityId = FGuid(0xD7, 0, 3, 1);
    Owed.Key.Victim.Life = 1;
    Owed.VictimBody.Victim = Owed.Key.Victim;
    Owed.VictimBody.BodyIdentity = FGuid(0xD7, 0, 3, 2);
    Owed.FatalEffectId = FGuid(0xD7, 0, 3, 3);
    Owed.CreditOwner = Leaver;
    Owed.RewardBasis.RewardDefinitionId = MonsterId;
    Owed.RewardBasis.bCreditOwnerEligibleAtDeath = true;
    Owed.RewardBasis.NoRewardReason = ELDNoRewardReason::None;
    Owed.OccurredAtMs = StepAtMs + 1;
    const int64 LeaverXPBeforeKill = XP(Leaver);
    FLDRewardReceipt Owing;
    if (!RunPhase(ELDCommitPhase::DeathsAndRewards,
        [&](const FLDGameplayWritePermit& Permit) { Owing = Growth->CommitDeathReward(Owed, Permit); return true; }))
    { return false; }
    TestTrue(TEXT("The locked camp kill is granted to the contender who left"),
        Owing.Outcome == ELDRewardOutcome::Granted && Owing.Recipient == Leaver && Owing.XPDelta == MonsterReward.XP
        && XP(Leaver) == LeaverXPBeforeKill + MonsterReward.XP);
    TestTrue(TEXT("The kill raised his level, and no Combat record was made for a body he no longer has"),
        Owing.LevelAfter > Owing.LevelBefore && Development->FindBuild(Leaver)->Level == Owing.LevelAfter
        && !Driver->Records.Contains(Leaver));
    FLDRewardReceipt Retried;
    if (!RunPhase(ELDCommitPhase::DeathsAndRewards,
        [&](const FLDGameplayWritePermit& Permit) { Retried = Growth->CommitDeathReward(Owed, Permit); return true; }))
    { return false; }
    TestTrue(TEXT("The retried fact answers with the original receipt and pays nothing again"),
        Retried.Outcome == ELDRewardOutcome::Granted && Retried.XPDelta == MonsterReward.XP
        && XP(Leaver) == LeaverXPBeforeKill + MonsterReward.XP);

    // A closed match opens no new request, while a replay still answers.
    Mode->MatchPhase = ELDMatchPhase::Results;
    Status(TEXT("A new request after the match closed"), Growth->AdmitDevTest(Tester, Command(Gen, 1, 19, ELDDevTestOp::GrantExperience, 5)),
        ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::MatchClosed);
    Status(TEXT("A replay after the match closed"), Growth->AdmitDevTest(Tester, FirstGrant), ELDAdmissionStatus::Final,
        ELDGrowthReason::None);
    return true;
}

// #112 §8: one contender death settles the killer's and the assisters' XP parts in one composite commit, every part split
// evenly, exactly once; malformed assister facts are refused; contributions record on the single entry.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoAssistSettlementTest, "Lockdown.Rules.DemoAssist.Settlement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoAssistSettlementTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(*this, Catalog))
    {
        return false;
    }
    const FLDDemoContentParameters& Tuning = Catalog->Get().Parameters;
    TestEqual(TEXT("The approved assist window is 15 s"), Tuning.AssistWindowMs, int64(15000));

    // ---- Public Q/W facts of a build (#112 §10) ----
    const FLDDemoAbilityDefinition* Active = Catalog->Get().Abilities.FindByPredicate(
        [](const FLDDemoAbilityDefinition& Row) { return Row.Kind == ELDDemoAbilityKind::Active; });
    const FLDDemoAbilityDefinition* Passive = Catalog->Get().Abilities.FindByPredicate(
        [](const FLDDemoAbilityDefinition& Row) { return Row.Kind == ELDDemoAbilityKind::Passive; });
    if (!TestTrue(TEXT("The catalog has an active and a passive ability"), Active && Passive)) { return false; }
    FLDBuildProjectionInput Build;
    Build.Level = 5;
    FLDLearnedActiveValue& Held = Build.Actives.AddDefaulted_GetRef();
    Held.AbilityId = FName(*Active->Id);
    Held.Rank = uint8(Catalog->Get().Progression.MaxSkillRank);
    Held.Entry = ELDCombatEntry::W;
    FLDObservedActivesView Public;
    TestTrue(TEXT("A held W active projects its native id and rank into the W slot only"),
        ALDCombatDirector::PublicActivesOf(Build, *Catalog, Public) && Public.Abilities[0] == 0 && Public.Ranks[0] == 0
        && Public.Abilities[1] == Active->NativeId && Public.Ranks[1] == Held.Rank);
    FLDBuildProjectionInput PassiveBuild = Build;
    PassiveBuild.Actives[0].AbilityId = FName(*Passive->Id);
    TestFalse(TEXT("A passive never fills a public Q/W slot"), ALDCombatDirector::PublicActivesOf(PassiveBuild, *Catalog, Public));
    FLDBuildProjectionInput OverRank = Build;
    OverRank.Actives[0].Rank = uint8(Catalog->Get().Progression.MaxSkillRank + 1);
    TestFalse(TEXT("A rank above the approved maximum is refused"), ALDCombatDirector::PublicActivesOf(OverRank, *Catalog, Public));

    // ---- Authority fixture ----
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Isolated assist world"), World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Mode = World->SpawnActor<ALDGameMode>();
    auto* GameState = World->SpawnActor<ALDGameState>();
    auto* Driver = World->SpawnActor<ALDCombatDirector>();
    auto* Growth = World->SpawnActor<ALDGrowthDirector>();
    auto* Development = World->SpawnActor<ALDDevelopmentDirector>();
    if (!Mode || !GameState || !Driver || !Growth || !Development) { AddError(TEXT("Fixture actors failed to spawn")); return false; }
    Mode->bDemoWorld = Mode->bEntryAccepted = Mode->bAuthorityInitialized = true;
    Mode->GameState = GameState;
    World->CopyGameState(Mode, GameState);
    Mode->MatchContext = {5, FGuid::NewGuid()};
    Mode->MatchPhase = ELDMatchPhase::Development;
    Mode->Combat = Driver;
    Mode->Growth = Growth;
    Mode->Development = Development;
    Mode->DemoContent = Catalog;
    Mode->AuthorityStep = {Mode->MatchContext, 1, 0, 0};
    // Sorted ids keep the expected assister order readable: A1 < A2.
    const FGuid Killer(0xA5, 0, 0, 0x10), A1(0xA5, 0, 0, 0x20), A2(0xA5, 0, 0, 0x30), Victim(0xA5, 0, 0, 0x40);
    const TArray<FGuid> Roster = {Killer, A1, A2, Victim};
    for (const FGuid& Id : Roster)
    {
        auto& Life = Mode->Lives.Add(Id);
        Life.Kind = ELDVictimKind::Contender;
        Life.Phase = ELDLifePhase::Unspawned;
        Life.Life = 1;
        Life.Revision = 2;
    }
    FString Error;
    Growth->Match = Mode->MatchContext;
    Growth->Roster = Roster;
    Growth->Content = Catalog;
    if (!TestTrue(TEXT("Growth opens for the roster"), Development->BeginMatch(Mode->MatchContext, Roster, Catalog, Error)))
    { AddError(Error); return false; }
    for (const FGuid& Id : Roster)
    {
        FLDCombatEntityRecord& Record = Driver->Records.Add(Id);
        Record.Kind = ELDVictimKind::Contender;
        Record.Entity = Id;
        Record.Build.Level = 1;
    }
    World->Tick(LEVELTICK_TimeOnly, .025f);

    // ---- The single contribution entry ----
    FLDBodyCombatState Life;
    Life.Victim = {ELDVictimKind::Contender, Victim, 1};
    TestTrue(TEXT("Another contender's contact records a contribution"), Driver->RecordAssistContribution(Life, A1, 1000));
    TestFalse(TEXT("A contact by the victim itself is no contribution"), Driver->RecordAssistContribution(Life, Victim, 1100));
    TestFalse(TEXT("An unowned contact is no contribution"), Driver->RecordAssistContribution(Life, FGuid(), 1100));
    TestFalse(TEXT("A contact by an entity without a contender record is no contribution"),
        Driver->RecordAssistContribution(Life, FGuid(0xA5, 0, 0, 0x99), 1100));
    TestTrue(TEXT("A later contact refreshes the same contributor"), Driver->RecordAssistContribution(Life, A1, 4000)
        && Life.AssistContributions.Num() == 1 && Life.AssistContributions[0].LastAtMs == 4000);
    FLDBodyCombatState Camp;
    Camp.Victim = {ELDVictimKind::CampMember, FGuid::NewGuid(), 1};
    TestFalse(TEXT("Camp members collect no assist contributions"), Driver->RecordAssistContribution(Camp, A1, 1000)
        || !Camp.AssistContributions.IsEmpty());
    Life.bPendingDeath = true;
    TestFalse(TEXT("A life whose death is locked takes no new contribution"), Driver->RecordAssistContribution(Life, A2, 5000));

    // ---- Settlement ----
    int64 StepAtMs = 0;
    const auto Settle = [&](const FLDCommittedDeath& Fact, FLDRewardReceipt& Out)
    {
        StepAtMs += 1;
        Error.Reset();
        if (Mode->BeginAuthorityStep(*Driver, Mode->MatchContext, StepAtMs) != ELDCommitReject::None)
        {
            AddError(FString::Printf(TEXT("The boundary at %lld ms did not begin."), StepAtMs));
            return false;
        }
        const bool Ran = Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Motion, [](const auto&) { return true; }, Error)
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::DeathsAndRewards,
                [&](const FLDGameplayWritePermit& Permit) { Out = Growth->CommitDeathReward(Fact, Permit); return true; }, Error)
            && Mode->FinishAuthorityStep(*Driver, Error);
        if (!Ran) { AddError(FString::Printf(TEXT("DeathsAndRewards boundary failed: %s"), *Error)); }
        return Ran;
    };
    const auto XP = [&](const FGuid& Id) { const FLDProgressionState* P = Development->FindProgression(Id); return P ? P->CumulativeXP : -1; };
    const auto TotalXP = [&]()
    {
        int64 Sum = 0;
        for (const FGuid& Id : Roster) { Sum += XP(Id); }
        return Sum;
    };
    const auto ContenderDeath = [&](uint32 LifeNumber, const TArray<FGuid>& Assisters)
    {
        FLDCommittedDeath Fact;
        Fact.Key = {Mode->MatchContext.LedgerMatch, {ELDVictimKind::Contender, Victim, LifeNumber}};
        Fact.VictimBody = {Fact.Key.Victim, FGuid::NewGuid()};
        Fact.VictimContender = Victim;
        Fact.FatalEffectId = FGuid(0xA5, 1, 0, LifeNumber);
        Fact.CreditOwner = Killer;
        Fact.RewardBasis.RewardDefinitionId = FName(*Catalog->Get().Character.Id);
        Fact.RewardBasis.VictimLevelAtDeath = 1;
        Fact.RewardBasis.bCreditOwnerEligibleAtDeath = true;
        Fact.RewardBasis.NoRewardReason = ELDNoRewardReason::None;
        Fact.Assisters = Assisters;
        Fact.OccurredAtMs = 1000;
        Fact.OccurredAtSeconds = 1.;
        return Fact;
    };
    LDProgression::FRewardAmounts Base;
    GrowthTestReason(*this, TEXT("The level-1 contender bounty is approved"),
        LDProgression::ContenderKillReward(Tuning, uint8(Catalog->Get().Progression.MaxLevel), 1, Base), ELDGrowthReason::None);
    const int64 SharedXP = Base.XP / 3;
    if (!TestTrue(TEXT("The split leaves every share below the first level threshold"),
        Catalog->Get().Progression.XPThresholds.Num() >= 2 && SharedXP > 0 && SharedXP < Catalog->Get().Progression.XPThresholds[1]))
    { return false; }

    const FLDCommittedDeath Assisted = ContenderDeath(1, {A1, A2});
    FLDRewardReceipt Receipt;
    if (!Settle(Assisted, Receipt)) { return false; }
    TestTrue(TEXT("The assisted kill is granted once"), Receipt.Outcome == ELDRewardOutcome::Granted && Receipt.Recipient == Killer);
    TestTrue(TEXT("Killer and assisters split the XP evenly, floored"),
        XP(Killer) == SharedXP && XP(A1) == SharedXP && XP(A2) == SharedXP && XP(Victim) == 0);
    TestTrue(TEXT("The receipt lists both assisters with their parts in fact order"),
        Receipt.Assists.Num() == 2 && Receipt.Assists[0].Recipient == A1 && Receipt.Assists[1].Recipient == A2
        && Receipt.Assists[0].XPDelta == SharedXP && Receipt.Assists[1].XPDelta == SharedXP && Receipt.XPDelta == SharedXP);
    TestTrue(TEXT("The receipt names the killer's committed progression revision"), Receipt.Revisions.bHasProgression
        && Receipt.Revisions.ProgressionRevision == Development->FindProgression(Killer)->Revision);

    FLDRewardReceipt Replay;
    if (!Settle(Assisted, Replay)) { return false; }
    TestTrue(TEXT("The same death settles only once: a replay returns the original receipt and pays nothing"),
        Replay.Outcome == ELDRewardOutcome::Granted && Replay.Assists.Num() == 2 && XP(Killer) == SharedXP && XP(A1) == SharedXP
        && XP(A2) == SharedXP);
    FLDRewardReceipt Conflict;
    if (!Settle(ContenderDeath(1, {A1}), Conflict)) { return false; }
    GrowthTestReason(*this, TEXT("Another assister set under the same death key conflicts"), Conflict.Reason, ELDGrowthReason::DeathConflict);

    const auto Refused = [&](const TCHAR* What, const FLDCommittedDeath& Fact)
    {
        FLDRewardReceipt Out;
        const int64 Before = TotalXP();
        if (!Settle(Fact, Out)) { return; }
        TestTrue(What, Out.Outcome == ELDRewardOutcome::PermanentlyRejected && Out.Reason == ELDGrowthReason::DataInvalid
            && TotalXP() == Before);
    };
    Refused(TEXT("The killer is never his own assister"), ContenderDeath(2, {A1, Killer}));
    Refused(TEXT("Assisters are sorted and unique"), ContenderDeath(3, {A2, A1}));
    Refused(TEXT("The victim is never an assister"), ContenderDeath(4, {Victim}));
    Refused(TEXT("An assister must be a contender of this match"), ContenderDeath(5, {FGuid(0xA5, 0, 0, 0x99)}));
    FLDCommittedDeath Unowned = ContenderDeath(6, {A1});
    Unowned.CreditOwner.Invalidate();
    Unowned.RewardBasis.bCreditOwnerEligibleAtDeath = false;
    Unowned.RewardBasis.NoRewardReason = ELDNoRewardReason::NoOwner;
    Refused(TEXT("A death without a killer carries no assisters"), Unowned);
    FLDCommittedDeath Camp2;
    Camp2.Key = {Mode->MatchContext.LedgerMatch, {ELDVictimKind::CampMember, FGuid::NewGuid(), 1}};
    Camp2.CreditOwner = Killer;
    Camp2.RewardBasis.RewardDefinitionId = FName(*Catalog->Get().Monsters[0].Id);
    Camp2.RewardBasis.bCreditOwnerEligibleAtDeath = true;
    Camp2.RewardBasis.NoRewardReason = ELDNoRewardReason::None;
    Camp2.Assisters = {A1};
    Refused(TEXT("A camp kill only rewards the last hit"), Camp2);

    FLDRewardReceipt Plain;
    const int64 XPBeforeUnowned = TotalXP();
    Unowned = ContenderDeath(7, {});
    Unowned.CreditOwner.Invalidate();
    Unowned.RewardBasis.bCreditOwnerEligibleAtDeath = false;
    Unowned.RewardBasis.NoRewardReason = ELDNoRewardReason::NoOwner;
    if (!Settle(Unowned, Plain)) { return false; }
    TestTrue(TEXT("A death without a killer pays nobody"), Plain.Outcome == ELDRewardOutcome::NoReward && TotalXP() == XPBeforeUnowned);
    return true;
}

// #147 v7 §5.1: the slotted three-choice draft (upgrade, new skill, attribute or new skill), the full-slot new-active weight,
// per-offer limits, reroll avoidance, and a replacement active starting at its basic rank.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthDraftTest, "Lockdown.Rules.DemoGrowthDraft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthDraftTest::RunTest(const FString& Parameters)
{
    return GrowthTestRunDraft(*this);
}

// #147: a skill's fixed amounts grow with its owner's level, G(L) = 1 + SKILL.GrowthPerLevel x (L - 1).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthSkillGrowthTest, "Lockdown.Rules.DemoGrowthSkillGrowth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthSkillGrowthTest::RunTest(const FString& Parameters)
{
    return GrowthTestRunSkillGrowth(*this);
}

// 112 §6: the reroll command through admission and the Transactions commit: tokens, one per offer, receipt replay.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthRerollTest, "Lockdown.Rules.DemoGrowthReroll",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthRerollTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(*this, Catalog))
    {
        return false;
    }
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Isolated reroll world"), World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Mode = World->SpawnActor<ALDGameMode>();
    auto* Public = World->SpawnActor<ALDGameState>();
    auto* Driver = World->SpawnActor<ALDCombatDirector>();
    auto* Growth = World->SpawnActor<ALDGrowthDirector>();
    auto* Development = World->SpawnActor<ALDDevelopmentDirector>();
    if (!Mode || !Public || !Driver || !Growth || !Development) { AddError(TEXT("Fixture actors failed to spawn")); return false; }
    Mode->bDemoWorld = Mode->bEntryAccepted = Mode->bAuthorityInitialized = true;
    Mode->GameState = Public;
    World->CopyGameState(Mode, Public);
    Mode->MatchContext = {6, FGuid::NewGuid()};
    Mode->MatchPhase = ELDMatchPhase::Development;
    Mode->Combat = Driver;
    Mode->Growth = Growth;
    Mode->Development = Development;
    Mode->DemoContent = Catalog;
    Mode->AuthorityStep = {Mode->MatchContext, 1, 0, 0};
    const FGuid Tester(0x112, 6, 0, 1);
    auto& Life = Mode->Lives.Add(Tester);
    Life.Kind = ELDVictimKind::Contender;
    // Dead may confirm a choice; a reroll needs no body.
    Life.Phase = ELDLifePhase::Dead;
    Life.Life = 1;
    Life.Revision = 2;
    FString Error;
    Growth->Match = Mode->MatchContext;
    Growth->Roster = {Tester};
    Growth->Content = Catalog;
    if (!TestTrue(TEXT("Growth opens"), Development->BeginMatch(Mode->MatchContext, Growth->Roster, Catalog, Error))) { AddError(Error); return false; }
    World->Tick(LEVELTICK_TimeOnly, .025f);
    int64 StepAtMs = 0;
    const auto RunTransactions = [&]()
    {
        StepAtMs += 1;
        Error.Reset();
        const bool bRan = Mode->BeginAuthorityStep(*Driver, Mode->MatchContext, StepAtMs) == ELDCommitReject::None
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Motion, [](const auto&) { return true; }, Error)
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Transactions,
                [&](const FLDGameplayWritePermit& Permit) { return Growth->ProcessTransactions(Permit, Error); }, Error)
            && Mode->FinishAuthorityStep(*Driver, Error);
        if (!bRan) { AddError(FString::Printf(TEXT("Transactions boundary failed: %s"), *Error)); }
        return bRan;
    };
    uint64 NextRequest = 0;
    const auto MakeReroll = [&]()
    {
        const FLDProgressionView View = Development->ReadProgression(Tester);
        FLDChoiceCommand Out;
        Out.Generation = View.Generation;
        Out.Life = View.Life;
        Out.ExpectedLifePhase = View.LifePhase;
        Out.RequestId = ++NextRequest;
        Out.OfferId = View.Offer.OfferId;
        Out.OfferRevision = View.Offer.OfferRevision;
        Out.ExpectedBuildRevision = View.BuildRevision;
        Out.CatalogDigest = View.CatalogDigest;
        Out.bReroll = true;
        return Out;
    };
    const auto Final = [&](uint64 RequestId)
    {
        FLDReceiptQuery Query;
        Query.Generation = Mode->MatchContext.Generation;
        Query.Family = ELDGrowthFamily::Choice;
        Query.RequestId = RequestId;
        return Development->QueryChoiceReceipt(Tester, Query);
    };

    FLDChoiceCommand Bad = MakeReroll();
    Bad.OptionId = FGuid::NewGuid();
    TestEqual(TEXT("A reroll naming an option is refused at admission"),
        LDGrowthWire::Reason(Development->QueueChoice(Tester, Bad).Reason), ELDGrowthReason::InvalidRequest);
    NextRequest = 0;

    // Level 1 has no token.
    FLDChoiceCommand Command = MakeReroll();
    Development->QueueChoice(Tester, Command);
    if (!RunTransactions()) { return false; }
    FLDChoiceReceiptQueryResult Result = Final(Command.RequestId);
    TestTrue(TEXT("The opening reroll is refused even before tokens exist"), Result.bHasReceipt
        && Result.Receipt.Header.Outcome == ELDReceiptOutcome::Rejected
        && LDGrowthWire::Reason(Result.Receipt.Header.Reason) == ELDGrowthReason::OpeningNoReroll
        && Development->ReadProgression(Tester).Offer.OfferId == Command.OfferId);

    for (const uint8 Level : {uint8(5), uint8(11)})
    {
        Development->Records.FindChecked(Tester).Build.Level = Level;
        const FLDProgressionView Opening = Development->ReadProgression(Tester);
        Command = MakeReroll();
        Development->QueueChoice(Tester, Command);
        if (!RunTransactions()) { return false; }
        Result = Final(Command.RequestId);
        TestTrue(TEXT("Level 5/11 tokens cannot reroll an unchosen opening offer"),
            Result.bHasReceipt && LDGrowthWire::Reason(Result.Receipt.Header.Reason) == ELDGrowthReason::OpeningNoReroll
            && Development->ReadProgression(Tester).RerollTokens == Opening.RerollTokens
            && Development->ReadProgression(Tester).Offer.OfferId == Opening.Offer.OfferId);
    }
    // Exercise the ordinary level-up reroll independently of the opening offer.
    Development->Records.FindChecked(Tester).Build.Level = 5;
    auto& Record = Development->Records.FindChecked(Tester);
    Record.Progression.ConsumedThrough = 1;
    Record.Progression.EarnedThrough = 5;
    TArray<LDProgression::FLegalOption> Pool, Picked;
    LDProgression::LegalPool(*Catalog, Record.Build, Pool);
    LDProgression::FDrawInput Draw;
    Draw.Content = &Catalog->Get();
    Draw.Build = &Record.Build;
    Draw.Ordinal = 2;
    Draw.Seed = 123;
    if (LDProgression::DrawOffer(Draw, Pool, Picked) != ELDGrowthReason::None) { return false; }
    Development->InstallHead(Tester, Record, 2, false, Picked);
    const FLDProgressionView Before = Development->ReadProgression(Tester);
    TestEqual(TEXT("Level 5 holds one reroll token"), Before.RerollTokens, uint8(1));
    Command = MakeReroll();
    Development->QueueChoice(Tester, Command);
    if (!RunTransactions()) { return false; }
    Result = Final(Command.RequestId);
    const FLDProgressionView After = Development->ReadProgression(Tester);
    TestTrue(TEXT("The reroll is applied and names the replacing offer"), Result.bHasReceipt
        && Result.Receipt.Header.Outcome == ELDReceiptOutcome::Applied && Result.Receipt.bRerolled
        && Result.Receipt.bHasNextOffer && Result.Receipt.NextOfferId == After.Offer.OfferId && Result.Receipt.RerollTokensLeft == 0);
    TestTrue(TEXT("Same opportunity, a new offer marked rerolled, the token spent, nothing consumed or learned"),
        After.Offer.OfferId != Before.Offer.OfferId && After.Offer.OpportunityOrdinal == Before.Offer.OpportunityOrdinal
        && After.Offer.bRerolled && After.RerollTokens == 0 && After.ConsumedThrough == Before.ConsumedThrough
        && After.BuildRevision == Before.BuildRevision && After.Skills.IsEmpty());
    int32 Repeated = 0;
    for (const FLDChoiceOption& Option : After.Offer.Options)
    {
        Repeated += Before.Offer.Options.ContainsByPredicate([&Option](const FLDChoiceOption& Old)
            { return Old.DefinitionId == Option.DefinitionId && Old.Kind == Option.Kind; }) ? 1 : 0;
    }
    TestEqual(TEXT("The rerolled level-up avoids the previous three cards"), Repeated, 0);
    const FLDSubmitResult Replay = Development->QueueChoice(Tester, Command);
    TestTrue(TEXT("A replay answers the original receipt without a second reroll"), Replay.Status == ELDAdmissionStatus::Final
        && Replay.bHasChoiceReceipt && Replay.ChoiceReceipt.NextOfferId == After.Offer.OfferId
        && Development->ReadProgression(Tester).Offer.OfferId == After.Offer.OfferId);

    // Level 11 earns another token, kept for later: the rerolled offer still cannot be rerolled again.
    Development->Records.FindChecked(Tester).Build.Level = 11;
    TestEqual(TEXT("Level 11 holds the second token"), Development->ReadProgression(Tester).RerollTokens, uint8(1));
    Command = MakeReroll();
    Development->QueueChoice(Tester, Command);
    if (!RunTransactions()) { return false; }
    Result = Final(Command.RequestId);
    TestTrue(TEXT("One reroll per offer"), Result.bHasReceipt && LDGrowthWire::Reason(Result.Receipt.Header.Reason)
        == ELDGrowthReason::OfferAlreadyRerolled && Development->ReadProgression(Tester).RerollTokens == 1);
    // A stale offer revision is refused like any choice.
    FLDChoiceCommand Stale = MakeReroll();
    Stale.OfferRevision += 1;
    Development->QueueChoice(Tester, Stale);
    if (!RunTransactions()) { return false; }
    Result = Final(Stale.RequestId);
    TestTrue(TEXT("A reroll with a stale guard is refused"), Result.bHasReceipt
        && LDGrowthWire::Reason(Result.Receipt.Header.Reason) == ELDGrowthReason::OfferGuardStale);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthOpeningCardTest, "Lockdown.Rules.DemoGrowthOpeningCard",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthOpeningCardTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(*this, Catalog)) { return false; }
    FString DisplayError;
    if (!TestTrue(TEXT("The approved display table is registered for this standalone test"),
        LDDemoContentText::Register(*Catalog, DisplayError)))
    { AddError(DisplayError); return false; }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    FLDGrowthBuild Empty;
    TArray<LDProgression::FLegalOption> Pool, Picked;
    LDProgression::LegalPool(*Catalog, Empty, Pool);
    LDProgression::FDrawInput Draw;
    Draw.Content = &Content;
    Draw.Build = &Empty;
    Draw.Ordinal = 1;
    Draw.Seed = 153;
    if (!TestTrue(TEXT("The opening draw succeeds"), LDProgression::DrawOffer(Draw, Pool, Picked) == ELDGrowthReason::None))
    { return false; }
    for (const auto& Direction : Content.OpeningCards)
    {
        for (const auto& Ability : Content.Abilities)
        {
            if (Ability.Kind != ELDDemoAbilityKind::Active) { continue; }
            bool bIntersection = false;
            for (const auto& Bonus : Direction.Stats)
            {
                for (const auto& Rank : Ability.Ranks)
                {
                    for (const auto& Effect : Rank.Effects)
                    {
                        bIntersection |= (Bonus.Stat == ELDDemoBonusStat::ADFlat && Effect.ADRatioBp > 0)
                            || (Bonus.Stat == ELDDemoBonusStat::APFlat && Effect.APRatioBp > 0)
                            || (Bonus.Stat == ELDDemoBonusStat::MaxHPFlat && Effect.MaxHPRatioBp > 0);
                    }
                }
            }
            TestEqual(TEXT("Weights use actual effect growth coefficients, never skill IDs"),
                LDProgression::OpeningActiveWeight(Content, Direction, Ability), bIntersection ? int64(3) : int64(2));
        }
        FLDChoiceOption Option;
        Option.Kind = ELDChoiceOptionKind::Opening;
        Option.DefinitionId = FName(*Direction.Id);
        Option.ToRank = 1;
        FLDGrowthBuild Applied;
        LDProgression::FAppliedOption Detail;
        const FName Hidden = Picked[0].HiddenActive;
        TestEqual(TEXT("An opening choice without the server-only active is refused"),
            LDProgression::ApplyOption(*Catalog, Empty, Option, nullptr, Applied, Detail), ELDGrowthReason::InvalidOption);
        if (!TestEqual(TEXT("The selected direction and its active apply atomically"),
            LDProgression::ApplyOption(*Catalog, Empty, Option, nullptr, Applied, Detail, Hidden), ELDGrowthReason::None))
        { return false; }
        TestTrue(TEXT("The card is permanent; the active is initial rank in Q; this costs one choice"),
            Applied.OpeningCard == Option.DefinitionId && Applied.Skills.Num() == 1 && Applied.Cards.IsEmpty()
            && Applied.Skills[0].Id == Hidden && Applied.Skills[0].Rank == 1 && Applied.Skills[0].Slot == 0
            && Detail.OpeningActive == Hidden && Applied.ChoicesInvested() == 1);
        TestFalse(TEXT("A second opening cannot be applied"), LDProgression::OptionStillLegal(*Catalog, Applied, Option));
        FLDAttributeBuildValues Values(1, {}, {});
        Values.OpeningCard = Direction.Id;
        const auto Projection = LDAttributeProjection::Project(Values, *Catalog);
        if (!TestTrue(TEXT("All six approved bundles project at level 1"), Projection.IsSuccess()))
        { return false; }
        const auto& P = Content.Parameters;
        FLDProjectedAttributes Expected;
        Expected.MaxHPP2 = P.MaxHP.BaseP2;
        Expected.MaxMPP2 = P.MaxMP.BaseP2;
        Expected.ADP2 = P.AD.BaseP2;
        Expected.APP2 = P.AP.BaseP2;
        Expected.ArmorP2 = P.Armor.BaseP2;
        Expected.MagicResistP2 = P.MagicResist.BaseP2;
        Expected.AttackSpeed = P.AttackSpeed.Base;
        Expected.MoveSpeedCmPerSec = P.MoveSpeedCmPerSec.Base;
        Expected.AttackRangeCm = P.AttackRangeCm.Base;
        Expected.HPRegenPerSec = P.HPRegen.Base;
        Expected.MPRegenPerSec = P.MPRegen.Base;
        Expected.CritChanceBp = P.CritChanceBaseBp;
        Expected.CooldownHasteP2 = P.CooldownHasteBaseP2;
        for (const auto& Bonus : Direction.Stats)
        {
            switch (Bonus.Stat)
            {
            case ELDDemoBonusStat::ADFlat: Expected.ADP2 += Bonus.Amount; break;
            case ELDDemoBonusStat::APFlat: Expected.APP2 += Bonus.Amount; break;
            case ELDDemoBonusStat::MaxHPFlat: Expected.MaxHPP2 += Bonus.Amount; break;
            case ELDDemoBonusStat::MaxMPFlat: Expected.MaxMPP2 += Bonus.Amount; break;
            case ELDDemoBonusStat::ArmorFlat: Expected.ArmorP2 += Bonus.Amount; break;
            case ELDDemoBonusStat::MagicResistFlat: Expected.MagicResistP2 += Bonus.Amount; break;
            case ELDDemoBonusStat::CooldownHasteFlat: Expected.CooldownHasteP2 += Bonus.Amount; break;
            case ELDDemoBonusStat::AttackSpeedBonusRatio:
                Expected.AttackSpeed.Numerator *= 10000 + Bonus.Amount;
                Expected.AttackSpeed.Denominator *= 10000;
                break;
            case ELDDemoBonusStat::MoveSpeedBonusRatio:
                Expected.MoveSpeedCmPerSec.Numerator *= 10000 + Bonus.Amount;
                Expected.MoveSpeedCmPerSec.Denominator *= 10000;
                break;
            case ELDDemoBonusStat::AttackRangeCmFlat:
                Expected.AttackRangeCm.Numerator = Expected.AttackRangeCm.Numerator * Bonus.Cm.Denominator
                    + Bonus.Cm.Numerator * Expected.AttackRangeCm.Denominator;
                Expected.AttackRangeCm.Denominator *= Bonus.Cm.Denominator;
                break;
            default: AddError(TEXT("An opening bundle contains an unsupported expected stat")); return false;
            }
        }
        const auto& Actual = Projection.Value->Final;
        const FString Label = Direction.Id + TEXT(": ");
        TestEqual(Label + TEXT("max HP"), Actual.MaxHPP2, Expected.MaxHPP2);
        TestEqual(Label + TEXT("max MP"), Actual.MaxMPP2, Expected.MaxMPP2);
        TestEqual(Label + TEXT("AD"), Actual.ADP2, Expected.ADP2);
        TestEqual(Label + TEXT("AP"), Actual.APP2, Expected.APP2);
        TestEqual(Label + TEXT("armor"), Actual.ArmorP2, Expected.ArmorP2);
        TestEqual(Label + TEXT("magic resist"), Actual.MagicResistP2, Expected.MagicResistP2);
        TestEqual(Label + TEXT("haste"), Actual.CooldownHasteP2, Expected.CooldownHasteP2);
        TestEqual(Label + TEXT("crit"), Actual.CritChanceBp, Expected.CritChanceBp);
        const auto EqualRate = [](const FLDDemoRational& A, const FLDDemoRational& B)
            { return A.Numerator * B.Denominator == B.Numerator * A.Denominator; };
        TestTrue(Label + TEXT("attack speed"), EqualRate(Actual.AttackSpeed, Expected.AttackSpeed));
        TestTrue(Label + TEXT("move speed"), EqualRate(Actual.MoveSpeedCmPerSec, Expected.MoveSpeedCmPerSec));
        TestTrue(Label + TEXT("attack range"), EqualRate(Actual.AttackRangeCm, Expected.AttackRangeCm));
        TestTrue(Label + TEXT("HP regeneration"), EqualRate(Actual.HPRegenPerSec, Expected.HPRegenPerSec));
        TestTrue(Label + TEXT("MP regeneration"), EqualRate(Actual.MPRegenPerSec, Expected.MPRegenPerSec));
        FLDProgressionView View;
        View.Level = 1;
        View.Offer.Options = {Option};
        const auto Card = LDHUD::ChoiceCard(Option, View, &Content, {});
        TestTrue(TEXT("Direction, three typed stat lines and the mystery active are on the face"),
            Card.Kind == LDTerms::OpeningCardKind && Card.SentencePlain.Contains(LDTerms::OpeningHiddenActive)
            && !Card.bNeedsReplacement && LDHUD::IsOpeningOffer(View.Offer));
        for (const auto& Stat : Direction.Stats)
        {
            TestTrue(TEXT("Every stat is visible without hovering"), Card.SentencePlain.Contains(LDHUD::StatLine(Stat)));
        }
        const FString HiddenName = LDHUD::ContentName(&Content, Hidden.ToString());
        const FString FaceText = Card.Name + Card.Detail + Card.SentencePlain + Card.AdvancedPlain + FString::Join(Card.Lines, TEXT("\n"));
        TestFalse(TEXT("The owner card model carries no hidden skill ID"), FaceText.Contains(Hidden.ToString()));
        TestFalse(TEXT("The owner card model carries no hidden skill display name"), FaceText.Contains(HiddenName));
        Draw.Build = &Applied;
        Draw.Ordinal = 2;
        LDProgression::LegalPool(*Catalog, Applied, Pool);
        TestEqual(TEXT("The next opportunity uses the normal level-up draw"), LDProgression::DrawOffer(Draw, Pool, Picked),
            ELDGrowthReason::None);
        for (const auto& Row : Picked)
        {
            TestTrue(TEXT("Later draws contain neither opening cards nor hidden actives"),
                Row.Kind != ELDChoiceOptionKind::Opening && Row.HiddenActive.IsNone());
        }
        Draw.Build = &Empty;
        Draw.Ordinal = 1;
        LDProgression::LegalPool(*Catalog, Empty, Pool);
        LDProgression::DrawOffer(Draw, Pool, Picked);
    }
    // A synthetic active coefficient shows that weighting follows edited content rather than a memorized list.
    FLDDemoAbilityDefinition Synthetic;
    FLDDemoRankDefinition Rank;
    FLDDemoEffectDefinition Effect;
    Effect.APRatioBp = 1;
    Rank.Effects.Add(Effect);
    Synthetic.Ranks.Add(Rank);
    FLDDemoOpeningCardDefinition Direction;
    FLDDemoStatBonus Stat;
    Stat.Stat = ELDDemoBonusStat::APFlat;
    Direction.Stats.Add(Stat);
    TestEqual(TEXT("A previously unknown active with AP growth matches AP directions"),
        LDProgression::OpeningActiveWeight(Content, Direction, Synthetic), int64(3));
    Synthetic.Ranks[0].Effects[0].APRatioBp = 0;
    TestEqual(TEXT("Removing its coefficient removes the match"),
        LDProgression::OpeningActiveWeight(Content, Direction, Synthetic), int64(2));
    return true;
}

#endif
