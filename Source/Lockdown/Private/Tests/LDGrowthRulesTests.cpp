#include "Development/LDProgressionRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDemoContentCatalog.h"
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
    for (const TCHAR* IdText : {TEXT("active_elbow"), TEXT("camp"), TEXT("anchor_2")})
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
        {TEXT("a hyphen"), FName(TEXT("active-elbow"))},
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

    bGood &= Test.TestFalse(TEXT("Unspawned cannot send a build command"), LDGrowthWire::IsBuildLifePhase(ELDLifePhase::Unspawned));
    bGood &= Test.TestTrue(TEXT("Alive can send a build command"), LDGrowthWire::IsBuildLifePhase(ELDLifePhase::Alive));
    bGood &= Test.TestTrue(TEXT("Dead can send a build command"), LDGrowthWire::IsBuildLifePhase(ELDLifePhase::Dead));
    bGood &= Test.TestFalse(TEXT("Left cannot send a build command"), LDGrowthWire::IsBuildLifePhase(ELDLifePhase::Left));

    FLDBuildCommand Build;
    Build.Generation = 3;
    Build.Life = 2;
    Build.ExpectedLifePhase = ELDLifePhase::Dead;
    Build.RequestId = 7;
    Build.ExpectedBuildRevision = 9;
    Build.CatalogDigest.Bytes[0] = 0xAB;
    Build.Kind = ELDBuildCommandKind::ActiveChoice;
    Build.OfferId = FGuid(0x0FFE4000, 1, 2, 3);
    Build.OfferRevision = 5;
    Build.Option = GrowthTestRawWire(TEXT("active_snark_bolt"));
    Build.bLearnIfPossible = true;
    Build.Slot = 1;
    Build.Blueprint = GrowthTestRawWire(TEXT("active_seethe"));
    Build.PointCounts = {1, 0, 2, 0, 0};
    const TCHAR* BuildFamily = TEXT("Build");
    bGood &= Test.TestTrue(TEXT("A copied build command keeps its terms"), Build.SameTerms(FLDBuildCommand(Build)));
    bGood &= GrowthTestTermsDiffer(Test, BuildFamily, TEXT("Generation"), Build, [](FLDBuildCommand& Mutated) { ++Mutated.Generation; });
    bGood &= GrowthTestTermsDiffer(Test, BuildFamily, TEXT("Life"), Build, [](FLDBuildCommand& Mutated) { ++Mutated.Life; });
    bGood &= GrowthTestTermsDiffer(Test, BuildFamily, TEXT("ExpectedLifePhase"), Build,
        [](FLDBuildCommand& Mutated) { Mutated.ExpectedLifePhase = ELDLifePhase::Alive; });
    bGood &= GrowthTestTermsDiffer(Test, BuildFamily, TEXT("RequestId"), Build, [](FLDBuildCommand& Mutated) { ++Mutated.RequestId; });
    bGood &= GrowthTestTermsDiffer(Test, BuildFamily, TEXT("ExpectedBuildRevision"), Build,
        [](FLDBuildCommand& Mutated) { ++Mutated.ExpectedBuildRevision; });
    bGood &= GrowthTestTermsDiffer(Test, BuildFamily, TEXT("CatalogDigest"), Build,
        [](FLDBuildCommand& Mutated) { Mutated.CatalogDigest.Bytes[31] = 0x5A; });
    bGood &= GrowthTestTermsDiffer(Test, BuildFamily, TEXT("Kind"), Build,
        [](FLDBuildCommand& Mutated) { Mutated.Kind = ELDBuildCommandKind::LearnBlueprint; });
    bGood &= GrowthTestTermsDiffer(Test, BuildFamily, TEXT("OfferId"), Build,
        [](FLDBuildCommand& Mutated) { Mutated.OfferId = FGuid(0x0FFE4000, 1, 2, 4); });
    bGood &= GrowthTestTermsDiffer(Test, BuildFamily, TEXT("OfferRevision"), Build, [](FLDBuildCommand& Mutated) { ++Mutated.OfferRevision; });
    bGood &= GrowthTestTermsDiffer(Test, BuildFamily, TEXT("Option"), Build,
        [](FLDBuildCommand& Mutated) { Mutated.Option = GrowthTestRawWire(TEXT("active_elbow")); });
    bGood &= GrowthTestTermsDiffer(Test, BuildFamily, TEXT("bLearnIfPossible"), Build,
        [](FLDBuildCommand& Mutated) { Mutated.bLearnIfPossible = false; });
    bGood &= GrowthTestTermsDiffer(Test, BuildFamily, TEXT("Slot"), Build,
        [](FLDBuildCommand& Mutated) { Mutated.Slot = FLDBuildCommand::AnySlot; });
    bGood &= GrowthTestTermsDiffer(Test, BuildFamily, TEXT("Blueprint"), Build,
        [](FLDBuildCommand& Mutated) { Mutated.Blueprint = GrowthTestRawWire(TEXT("active_unplug")); });
    bGood &= GrowthTestTermsDiffer(Test, BuildFamily, TEXT("PointCounts"), Build,
        [](FLDBuildCommand& Mutated) { Mutated.PointCounts[4] = 1; });
    bGood &= GrowthTestTermsDiffer(Test, BuildFamily, TEXT("PointCounts length"), Build,
        [](FLDBuildCommand& Mutated) { Mutated.PointCounts.Pop(); });

    // #198: Build is the only request family; the retired choice (1) and shop (2) family numbers decode to nothing.
    bGood &= Test.TestTrue(TEXT("Build is a known growth family"), LDGrowthWire::IsKnownFamily(ELDGrowthFamily::Build));
    bGood &= Test.TestFalse(TEXT("None is no growth family"), LDGrowthWire::IsKnownFamily(ELDGrowthFamily::None));
    for (const uint8 Retired : {uint8(1), uint8(2)})
    {
        bGood &= Test.TestFalse(FString::Printf(TEXT("The retired family %u is refused, not mapped to a neighbour"), uint32(Retired)),
            LDGrowthWire::IsKnownFamily(static_cast<ELDGrowthFamily>(Retired)));
    }
    // Retired reasons keep their numbers reserved: none of them names a reason any more.
    for (const uint16 Retired : {uint16(40), uint16(46), uint16(47), uint16(49), uint16(50), uint16(51)})
    {
        bGood &= Test.TestEqual(FString::Printf(TEXT("Retired growth reason %u has no name"), uint32(Retired)),
            FString(LDGrowthWire::ReasonName(LDGrowthWire::Reason(Retired))), FString(TEXT("Unknown")));
    }
    const TPair<ELDGrowthReason, uint16> NewReasons[] = {{ELDGrowthReason::NotSafe, 52}, {ELDGrowthReason::NoEmptySlot, 53},
        {ELDGrowthReason::SlotOccupied, 54}, {ELDGrowthReason::NoBlueprint, 55}, {ELDGrowthReason::NotEnoughPoints, 56},
        {ELDGrowthReason::NoActiveOffer, 57}, {ELDGrowthReason::PhaseClosed, 58}};
    for (const TPair<ELDGrowthReason, uint16>& Row : NewReasons)
    {
        bGood &= Test.TestTrue(FString::Printf(TEXT("%s travels as %u"), LDGrowthWire::ReasonName(Row.Key), uint32(Row.Value)),
            LDGrowthWire::Raw(Row.Key) == Row.Value && FString(LDGrowthWire::ReasonName(Row.Key)) != TEXT("Unknown"));
    }
    return bGood;
}

// ---- Request streams --------------------------------------------------------------------------

using FGrowthTestStream = TLDGrowthRequestStream<FLDBuildCommand, FLDBuildReceipt>;

FLDBuildCommand GrowthTestBuildCommand(uint64 RequestId)
{
    FLDBuildCommand Command;
    Command.Generation = 4;
    Command.Life = 2;
    Command.ExpectedLifePhase = ELDLifePhase::Alive;
    Command.RequestId = RequestId;
    Command.ExpectedBuildRevision = 1;
    Command.Kind = ELDBuildCommandKind::ActiveChoice;
    Command.OfferId = FGuid(0x0FFE4000, 0, 0, uint32(RequestId));
    Command.OfferRevision = RequestId;
    Command.Option = GrowthTestRawWire(TEXT("active_elbow"));
    return Command;
}

FLDBuildReceipt GrowthTestBuildReceipt(const FLDBuildCommand& Command, ELDReceiptOutcome Outcome, ELDGrowthReason Reason)
{
    FLDBuildReceipt Receipt;
    Receipt.Header.Generation = Command.Generation;
    Receipt.Header.Family = ELDGrowthFamily::Build;
    Receipt.Header.RequestId = Command.RequestId;
    Receipt.Header.OriginalLife = Command.Life;
    Receipt.Header.OriginalLifePhase = Command.ExpectedLifePhase;
    Receipt.Header.Outcome = Outcome;
    Receipt.Header.Reason = LDGrowthWire::Raw(Reason);
    Receipt.Kind = Command.Kind;
    Receipt.OfferId = Command.OfferId;
    Command.Option.Decode(Receipt.DefinitionId);
    return Receipt;
}

struct FGrowthTestAdmission
{
    ELDAdmissionStatus Status = ELDAdmissionStatus::None;
    ELDGrowthReason Reason = ELDGrowthReason::None;
    const FLDBuildReceipt* Final = nullptr;
    bool bReserved = false;
};

FGrowthTestAdmission GrowthTestAdmit(FGrowthTestStream& Stream, const FLDBuildCommand& Command, uint64 Arrival)
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

bool GrowthTestPendingIs(FAutomationTestBase& Test, const FGrowthTestStream& Stream, const FLDBuildCommand& Command, uint64 Arrival,
    const TCHAR* What)
{
    const FGrowthTestStream::FPending* Pending = Stream.GetPending();
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
    FGrowthTestStream Stream;
    const FLDBuildReceipt* Final = nullptr;
    const FLDPendingReceiptInfo* Info = nullptr;
    bool bGood = true;

    FGrowthTestAdmission Admission = GrowthTestAdmit(Stream, GrowthTestBuildCommand(0), 100);
    bGood &= GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::InvalidRequest, false,
        TEXT("Request id 0"));
    bGood &= Test.TestNull(TEXT("Request id 0 leaves nothing pending"), Stream.GetPending());

    const FLDBuildCommand First = GrowthTestBuildCommand(1);
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
    FLDBuildCommand ChangedFirst = First;
    ChangedFirst.Option = GrowthTestRawWire(TEXT("active_seethe"));
    Admission = GrowthTestAdmit(Stream, ChangedFirst, 103);
    bGood &= GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::RequestConflict, false,
        TEXT("Request 1 resent with another option"));
    bGood &= GrowthTestPendingIs(Test, Stream, First, 101, TEXT("After the conflicting resend"));
    Admission = GrowthTestAdmit(Stream, GrowthTestBuildCommand(2), 104);
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
    Stream.Finalize(GrowthTestBuildReceipt(First, ELDReceiptOutcome::Rejected, ELDGrowthReason::OfferGuardStale));
    bGood &= Test.TestNull(TEXT("Finalizing request 1 clears the pending slot"), Stream.GetPending());
    bGood &= Test.TestEqual(TEXT("Request 1 is finalized"), Stream.GetFinalizedThrough(), uint64(1));
    Admission = GrowthTestAdmit(Stream, GrowthTestBuildCommand(3), 105);
    bGood &= GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::OutOfOrder, false,
        TEXT("Request 3 skipping request 2"));
    Admission = GrowthTestAdmit(Stream, First, 106);
    bGood &= GrowthTestExpectAdmission(Test, Admission, ELDAdmissionStatus::Final, ELDGrowthReason::None, true,
        TEXT("Replaying finalized request 1"));
    bGood &= Test.TestTrue(TEXT("The replay returns the stored rejected receipt instead of executing again"),
        Admission.Final && Admission.Final->Header.RequestId == 1 && Admission.Final->Header.Outcome == ELDReceiptOutcome::Rejected
        && LDGrowthWire::Reason(Admission.Final->Header.Reason) == ELDGrowthReason::OfferGuardStale
        && Admission.Final->DefinitionId == FName(TEXT("active_elbow")));
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

    const int32 Capacity = FGrowthTestStream::WindowCapacity;
    for (uint64 RequestId = 2; RequestId <= uint64(Capacity) + 1; ++RequestId)
    {
        const FLDBuildCommand Ordered = GrowthTestBuildCommand(RequestId);
        const FGrowthTestAdmission InOrder = GrowthTestAdmit(Stream, Ordered, 200 + RequestId);
        if (!Test.TestTrue(FString::Printf(TEXT("Request %llu is admitted in order"), RequestId),
            InOrder.Status == ELDAdmissionStatus::AcceptedPending && Stream.PendingRequestId() == RequestId))
        {
            return false;
        }
        Stream.Finalize(GrowthTestBuildReceipt(Ordered, ELDReceiptOutcome::Applied, ELDGrowthReason::None));
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
    Admission = GrowthTestAdmit(Stream, GrowthTestBuildCommand(2), 301);
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
    bApproved &= Test.TestTrue(TEXT("Fixture carries MaxLevel 16, three active slots and active opportunities at levels 3 and 6"),
        Curve.MaxLevel == 16 && Curve.ActiveSlots == 3 && Curve.ActiveOfferLevels == TArray<int32>({3, 6}));
    bApproved &= Test.TestTrue(TEXT("Fixture carries REWARD.PlayerXP 100 + 20/level"),
        Tuning.PlayerXPBase == 100 && Tuning.PlayerXPPerLevel == 20);
    bApproved &= Test.TestTrue(TEXT("Fixture carries 3 opening points, 3 per level, 3 per 400 XP past level 16 and a 4 s safe window"),
        Tuning.OpeningPoints == 3 && Tuning.PointsPerLevel == 3 && Tuning.MaxLevelPoints == 3 && Tuning.MaxLevelPointsXP == 400
        && Tuning.BuildSafeWindowMs == 4000);
    bApproved &= Test.TestTrue(TEXT("Fixture carries the five point-bought stats"), OutCatalog->Get().Stats.Num() == 5);
    return bApproved;
}

bool GrowthTestPlan(FAutomationTestBase& Test, const FLDDemoProgressionDefinition& Curve, int64 CurrentXP, uint8 CurrentLevel, int64 Grant,
    int32 ExpectedLevel, int32 ExpectedActiveEarned, const TCHAR* What)
{
    LDProgression::FExperienceChange Change;
    const ELDGrowthReason Outcome = LDProgression::PlanExperience(Curve, CurrentXP, CurrentLevel,
        LDProgression::ActiveOpportunitiesAt(Curve, CurrentLevel), Grant, Change);
    bool bGood = GrowthTestReason(Test, FString::Printf(TEXT("%s: outcome"), What), Outcome, ELDGrowthReason::None);
    bGood &= Test.TestEqual(FString::Printf(TEXT("%s: cumulative XP"), What), Change.NewXP, CurrentXP + Grant);
    bGood &= Test.TestEqual(FString::Printf(TEXT("%s: level before"), What), int32(Change.LevelBefore), int32(CurrentLevel));
    bGood &= Test.TestEqual(FString::Printf(TEXT("%s: level after"), What), int32(Change.LevelAfter), ExpectedLevel);
    bGood &= Test.TestEqual(FString::Printf(TEXT("%s: active opportunities earned"), What), int32(Change.ActiveEarnedAfter),
        ExpectedActiveEarned);
    return bGood;
}

FLDGrowthBuild GrowthTestBuild(const FLDDemoContentDefinitions& Content, uint8 Level, TArray<FLDLearnedActive> Actives = {},
    TArray<FName> Blueprints = {}, TArray<int32> Points = {})
{
    FLDGrowthBuild Build = LDProgression::StartBuild(Content);
    Build.Level = Level;
    Build.Actives = MoveTemp(Actives);
    Build.PendingBlueprints = MoveTemp(Blueprints);
    if (!Points.IsEmpty())
    {
        Build.StatPoints = MoveTemp(Points);
    }
    LDProgression::NormalizeBuild(Build);
    return Build;
}

bool GrowthTestPoints(FAutomationTestBase& Test, const FLDDemoContentDefinitions& Content, uint8 Level, int64 XP, int64 Expected,
    const TCHAR* What)
{
    int64 Earned = -1;
    const ELDGrowthReason Reason = LDProgression::EarnedPoints(Content, Level, XP, Earned);
    return GrowthTestReason(Test, FString::Printf(TEXT("%s: outcome"), What), Reason, ELDGrowthReason::None)
        && Test.TestEqual(FString::Printf(TEXT("%s: earned points"), What), Earned, Expected);
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
    // The extra active opportunities are reached at strictly increasing levels 2..MaxLevel; any other table is refused.
    for (const TArray<int32>& Broken : {TArray<int32>({6, 3}), TArray<int32>({3, 3}), TArray<int32>({1}), TArray<int32>({17})})
    {
        FLDDemoProgressionDefinition BrokenCurve = Curve;
        BrokenCurve.ActiveOfferLevels = Broken;
        uint8 Level = 0;
        bGood &= GrowthTestReason(Test, FString::Printf(TEXT("Active offer levels [%s] are refused"),
            *FString::JoinBy(Broken, TEXT(","), [](int32 Value) { return FString::FromInt(Value); })),
            LDProgression::LevelForXP(BrokenCurve, 0, Level), ELDGrowthReason::DataInvalid);
    }

    // The opening opportunity is ordinal 1; reaching level 3 and level 6 each add exactly one more.
    const TPair<int32, int32> OpportunityCases[] = {{1, 1}, {2, 1}, {3, 2}, {5, 2}, {6, 3}, {10, 3}, {16, 3}};
    for (const TPair<int32, int32>& Row : OpportunityCases)
    {
        bGood &= Test.TestEqual(FString::Printf(TEXT("Active opportunities held at level %d"), Row.Key),
            int32(LDProgression::ActiveOpportunitiesAt(Curve, uint8(Row.Key))), Row.Value);
    }
    bGood &= GrowthTestPlan(Test, Curve, 0, 1, 79, 1, 1, TEXT("79 XP at level 1 keeps only the opening opportunity"));
    bGood &= GrowthTestPlan(Test, Curve, 0, 1, 80, 2, 1, TEXT("Level 2 adds no active opportunity"));
    bGood &= GrowthTestPlan(Test, Curve, 80, 2, 100, 3, 2, TEXT("Level 3 adds the first extra active opportunity"));
    bGood &= GrowthTestPlan(Test, Curve, 0, 1, 600, 6, 3, TEXT("Crossing levels 3 and 6 at once adds both"));
    bGood &= GrowthTestPlan(Test, Curve, 1440, 10, 1000, 13, 3, TEXT("1000 XP at level 10 reaches level 13 without another"));
    bGood &= GrowthTestPlan(Test, Curve, 3300, 16, 5000, 16, 3, TEXT("XP keeps accumulating at the top level"));
    LDProgression::FExperienceChange Change;
    bGood &= GrowthTestReason(Test, TEXT("1 XP past the int64 bound"), LDProgression::PlanExperience(Curve, MAX_int64, 16, 3, 1, Change),
        ELDGrowthReason::ArithmeticOverflow);
    bGood &= Test.TestTrue(TEXT("An overflowing plan carries no partial result"), Change.NewXP == 0 && Change.LevelAfter == 0);
    bGood &= GrowthTestReason(Test, TEXT("80 XP recorded as level 1"), LDProgression::PlanExperience(Curve, 80, 1, 1, 20, Change),
        ELDGrowthReason::DataInvalid);
    bGood &= GrowthTestReason(Test, TEXT("Level 1 without its opening opportunity"),
        LDProgression::PlanExperience(Curve, 0, 1, 0, 80, Change), ELDGrowthReason::DataInvalid);
    bGood &= GrowthTestReason(Test, TEXT("Level 3 without its earned opportunity"),
        LDProgression::PlanExperience(Curve, 180, 3, 1, 20, Change), ELDGrowthReason::DataInvalid);
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

    // ---- #198 points: derived from the level and XP, never stored ----
    bGood &= GrowthTestPoints(Test, Approved, 1, 0, 3, TEXT("The opening grants 3 points"));
    bGood &= GrowthTestPoints(Test, Approved, 2, 80, 6, TEXT("Level 2 adds 3"));
    bGood &= GrowthTestPoints(Test, Approved, 10, 1440, 30, TEXT("Level 10 holds 3 + 9 x 3"));
    bGood &= GrowthTestPoints(Test, Approved, 15, 3299, 45, TEXT("Level 15 one XP short of the top earns no XP grant"));
    bGood &= GrowthTestPoints(Test, Approved, 16, 3300, 48, TEXT("Level 16 at its threshold holds 3 + 15 x 3"));
    bGood &= GrowthTestPoints(Test, Approved, 16, 3699, 48, TEXT("399 XP past level 16 grants nothing yet"));
    bGood &= GrowthTestPoints(Test, Approved, 16, 3700, 51, TEXT("Every full 400 XP past level 16 grants 3"));
    bGood &= GrowthTestPoints(Test, Approved, 16, 4099, 51, TEXT("A partial second 400 XP grants nothing more"));
    bGood &= GrowthTestPoints(Test, Approved, 16, 4100, 54, TEXT("The second full 400 XP grants 3 more"));
    bGood &= GrowthTestPoints(Test, Approved, 16, 7300, 78, TEXT("Ten full 400 XP past level 16"));
    int64 Earned = -1;
    bGood &= GrowthTestReason(Test, TEXT("A level that does not match its XP"), LDProgression::EarnedPoints(Approved, 2, 0, Earned),
        ELDGrowthReason::DataInvalid);
    bGood &= GrowthTestReason(Test, TEXT("Negative XP"), LDProgression::EarnedPoints(Approved, 1, -1, Earned), ELDGrowthReason::DataInvalid);
    bGood &= Test.TestEqual(TEXT("A refused derivation reports no points"), Earned, int64(0));
    {
        FLDDemoContentDefinitions Broken = Approved;
        Broken.Parameters.MaxLevelPointsXP = 0;
        bGood &= GrowthTestReason(Test, TEXT("A zero max-level XP step"), LDProgression::EarnedPoints(Broken, 16, 3700, Earned),
            ELDGrowthReason::DataInvalid);
        Broken.Parameters.MaxLevelPointsXP = 400;
        Broken.Parameters.PointsPerLevel = -1;
        bGood &= GrowthTestReason(Test, TEXT("Negative points per level"), LDProgression::EarnedPoints(Broken, 2, 80, Earned),
            ELDGrowthReason::DataInvalid);
        Broken.Parameters.PointsPerLevel = 3;
        Broken.Parameters.MaxLevelPoints = MAX_int64 / 2;
        bGood &= GrowthTestReason(Test, TEXT("Max-level grants past the int64 bound"), LDProgression::EarnedPoints(Broken, 16, 4100, Earned),
            ELDGrowthReason::ArithmeticOverflow);
    }

    const FLDGrowthBuild Start = LDProgression::StartBuild(Approved);
    bGood &= Test.TestTrue(TEXT("The match-start build is level 1 at revision 1 with one zero count per stat and nothing learned"),
        Start.Level == 1 && Start.Revision == 1 && Start.StatPoints == TArray<int32>({0, 0, 0, 0, 0}) && Start.Actives.IsEmpty()
        && Start.PendingBlueprints.IsEmpty());
    int64 Unspent = -1;
    bGood &= Test.TestTrue(TEXT("The match-start build holds the 3 opening points unspent"),
        LDProgression::UnspentPoints(Approved, Start, 0, Unspent) == ELDGrowthReason::None && Unspent == 3);
    bGood &= Test.TestTrue(TEXT("Allocated points are subtracted"),
        LDProgression::UnspentPoints(Approved, GrowthTestBuild(Approved, 1, {}, {}, {1, 0, 2, 0, 0}), 0, Unspent)
            == ELDGrowthReason::None && Unspent == 0);
    bGood &= Test.TestTrue(TEXT("Points earned past level 16 are unspent until allocated"),
        LDProgression::UnspentPoints(Approved, GrowthTestBuild(Approved, 16, {}, {}, {40, 0, 0, 0, 8}), 3700, Unspent)
            == ELDGrowthReason::None && Unspent == 3);
    bGood &= GrowthTestReason(Test, TEXT("More allocated than earned is a broken authority record"),
        LDProgression::UnspentPoints(Approved, GrowthTestBuild(Approved, 1, {}, {}, {2, 0, 2, 0, 0}), 0, Unspent),
        ELDGrowthReason::AuthorityFault);
    bGood &= GrowthTestReason(Test, TEXT("A point row per stat is required"),
        LDProgression::UnspentPoints(Approved, GrowthTestBuild(Approved, 1, {}, {}, {1, 0, 0, 0}), 0, Unspent), ELDGrowthReason::DataInvalid);
    bGood &= GrowthTestReason(Test, TEXT("A negative point count is refused"),
        LDProgression::UnspentPoints(Approved, GrowthTestBuild(Approved, 1, {}, {}, {-1, 0, 0, 0, 0}), 0, Unspent),
        ELDGrowthReason::DataInvalid);

    // ---- Build shape ----
    const FName Elbow(TEXT("active_elbow")), Seethe(TEXT("active_seethe")), Unplug(TEXT("active_unplug")), NotMad(TEXT("active_not_mad"));
    bGood &= GrowthTestReason(Test, TEXT("Three distinct actives in Q/W/E and a sorted blueprint"),
        LDProgression::ValidateBuild(Approved, GrowthTestBuild(Approved, 3, {{Seethe, 2}, {Elbow, 0}, {Unplug, 1}}, {NotMad})),
        ELDGrowthReason::None);
    FLDGrowthBuild Unsorted = GrowthTestBuild(Approved, 3, {{Elbow, 1}, {Seethe, 0}});
    bGood &= Test.TestTrue(TEXT("NormalizeBuild orders actives by slot"), Unsorted.Actives[0].Id == Seethe && Unsorted.Actives[1].Id == Elbow);
    Unsorted.Actives.Swap(0, 1);
    const TPair<const TCHAR*, FLDGrowthBuild> BadBuilds[] = {
        {TEXT("actives out of slot order"), Unsorted},
        {TEXT("one active twice"), GrowthTestBuild(Approved, 3, {{Elbow, 0}, {Elbow, 1}})},
        {TEXT("two actives in one slot"), GrowthTestBuild(Approved, 3, {{Elbow, 0}, {Seethe, 0}})},
        {TEXT("a slot past E"), GrowthTestBuild(Approved, 3, {{Elbow, 3}})},
        {TEXT("an unknown active"), GrowthTestBuild(Approved, 3, {{FName(TEXT("active_unknown")), 0}})},
        {TEXT("a blueprint of a learned active"), GrowthTestBuild(Approved, 3, {{Elbow, 0}}, {Elbow})},
        {TEXT("an unknown blueprint"), GrowthTestBuild(Approved, 3, {}, {FName(TEXT("active_unknown"))})},
        {TEXT("a blueprint twice"), GrowthTestBuild(Approved, 3, {}, {Seethe, Seethe})},
        {TEXT("a missing point row"), GrowthTestBuild(Approved, 3, {}, {}, {0, 0, 0, 0})}};
    for (const TPair<const TCHAR*, FLDGrowthBuild>& Bad : BadBuilds)
    {
        bGood &= GrowthTestReason(Test, FString::Printf(TEXT("A build with %s"), Bad.Key), LDProgression::ValidateBuild(Approved, Bad.Value),
            ELDGrowthReason::DataInvalid);
    }

    const FGuid MatchSecret(0x5EC2E700, 1, 2, 3);
    const FGuid Contender(0x0000A001, 0, 0, 1);
    const uint64 Seed = LDProgression::OfferSeed(MatchSecret, Contender, 1);
    bGood &= Test.TestTrue(TEXT("Retrying opportunity 1 reuses its seed"), Seed == LDProgression::OfferSeed(MatchSecret, Contender, 1));
    bGood &= Test.TestTrue(TEXT("Opportunity 2 draws from another seed"), Seed != LDProgression::OfferSeed(MatchSecret, Contender, 2));
    bGood &= Test.TestTrue(TEXT("Another contender's opportunity 1 draws from another seed"),
        Seed != LDProgression::OfferSeed(MatchSecret, FGuid(0x0000A002, 0, 0, 2), 1));
    bGood &= Test.TestTrue(TEXT("Another match secret draws from another seed"),
        Seed != LDProgression::OfferSeed(FGuid(0x5EC2E700, 1, 2, 4), Contender, 1));
    return bGood;
}

// ---- DEV growth fixture ------------------------------------------------------------------------

struct FGrowthTestPreset
{
    const TCHAR* What;
    TArray<FName> Actives;
};

// Off without -LDDevGrowthFixture; a preset resolves only to one to three distinct actives the build neither holds nor keeps
// as a blueprint; each preset shapes exactly one head draw and the next head is the ordinary seeded draw again.
bool GrowthTestRunDevFixture(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(Test, Catalog))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    const FName Snark(TEXT("active_snark_bolt"));
    const FName PantsDown(TEXT("active_pants_down"));
    const FName NotMad(TEXT("active_not_mad"));
    const FName Seethe(TEXT("active_seethe"));
    const FName Elbow(TEXT("active_elbow"));
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
            !Development->DevQueueOffer(Contender, {Snark, PantsDown, NotMad}, QueueError) && QueueError.Contains(TEXT("-LDDevGrowthFixture")));
        FString StageError;
        bGood &= Test.TestTrue(TEXT("StageOffer (the Lobby entry) is refused without the switch and names it"),
            !LDDevGrowthFixture::StageOffer(World, Contender, {Snark, PantsDown, NotMad}, StageError)
            && StageError.Contains(TEXT("-LDDevGrowthFixture")));
        bGood &= Test.TestTrue(TEXT("DevGrantReward is refused without the switch and issues no ticket"),
            Growth->DevGrantReward(Contender, 80, GrantError) == 0 && GrantError.Contains(TEXT("-LDDevGrowthFixture")));
        bGood &= Test.TestTrue(TEXT("No ticket exists to poll after a refused grant"),
            Growth->DevGrantStatus(1, Detail) == ELDDevGrantStatus::Unknown);
    }

    const FLDGrowthBuild Fresh = LDProgression::StartBuild(Content);
    bGood &= Test.TestTrue(FString::Printf(TEXT("One to three distinct actives resolve, also one outside the opening pool (%s)"), *Error),
        LDDevGrowthFixture::ResolvePreset(Content, Fresh, {NotMad}, Error)
        && LDDevGrowthFixture::ResolvePreset(Content, Fresh, {Snark, NotMad}, Error)
        && LDDevGrowthFixture::ResolvePreset(Content, Fresh, {Snark, PantsDown, NotMad}, Error));
    const FLDGrowthBuild Held = GrowthTestBuild(Content, 6, {{Snark, 0}}, {Seethe});
    const FGrowthTestPreset BadPresets[] = {
        {TEXT("no active"), {}},
        {TEXT("four actives"), {Snark, PantsDown, NotMad, Elbow}},
        {TEXT("a repeated active"), {PantsDown, Elbow, PantsDown}},
        {TEXT("an unknown definition"), {PantsDown, FName(TEXT("active_unknown"))}},
        {TEXT("a stat id"), {PantsDown, FName(TEXT("stat_physique"))}},
        {TEXT("NAME_None"), {PantsDown, FName()}},
        {TEXT("a learned active"), {PantsDown, Snark}},
        {TEXT("a pending blueprint"), {PantsDown, Seethe}}};
    for (const FGrowthTestPreset& Bad : BadPresets)
    {
        bGood &= Test.TestTrue(FString::Printf(TEXT("A preset with %s is refused with a reason"), Bad.What),
            !LDDevGrowthFixture::ResolvePreset(Content, Held, Bad.Actives, Error) && !Error.IsEmpty());
    }

    const uint64 Seed = LDProgression::OfferSeed(FGuid(1, 2, 3, 4), FGuid(0xE3, 0, 0, 1), 2);
    TArray<FName> Ordinary;
    bGood &= GrowthTestReason(Test, TEXT("The ordinary seeded draw"), LDProgression::DrawActiveOffer(Content, Held, 2, Seed, Ordinary),
        ELDGrowthReason::None);
    TArray<TArray<FName>> Queue = {{PantsDown, NotMad}, {Snark, Elbow}, {Elbow}};
    TArray<FName> Picked;
    bool bPreset = false;
    FString PresetError;
    bGood &= Test.TestTrue(TEXT("The first head draw takes the oldest preset, in its order"),
        LDDevGrowthFixture::PickHead(Content, Held, 2, Seed, Queue, Picked, bPreset, PresetError) == ELDGrowthReason::None && bPreset
        && PresetError.IsEmpty() && Picked == TArray<FName>({PantsDown, NotMad}) && Queue.Num() == 2);
    bGood &= Test.TestTrue(TEXT("A preset that no longer resolves is consumed with an explicit error and the ordinary draw runs"),
        LDDevGrowthFixture::PickHead(Content, Held, 2, Seed, Queue, Picked, bPreset, PresetError) == ELDGrowthReason::None && !bPreset
        && !PresetError.IsEmpty() && Picked == Ordinary && Queue.Num() == 1);
    bGood &= Test.TestTrue(TEXT("The next head draw takes the next preset, a single active included"),
        LDDevGrowthFixture::PickHead(Content, Held, 2, Seed, Queue, Picked, bPreset, PresetError) == ELDGrowthReason::None && bPreset
        && Picked == TArray<FName>({Elbow}) && Queue.IsEmpty());
    bGood &= Test.TestTrue(TEXT("With no preset left the head is the ordinary seeded draw: a preset shapes one offer only"),
        LDDevGrowthFixture::PickHead(Content, Held, 2, Seed, Queue, Picked, bPreset, PresetError) == ELDGrowthReason::None && !bPreset
        && PresetError.IsEmpty() && Picked == Ordinary);
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

bool GrowthTestRunPrivacy(FAutomationTestBase& Test)
{
    // Any field added to the private growth views must be added here deliberately (and told to UI).
    bool bGood = GrowthTestExactFields(Test, FLDGrowthOwnerView::StaticStruct(),
        {TEXT("Generation"), TEXT("PublishRevision"), TEXT("Progression"), TEXT("AirdropClaimedSerial")});
    bGood &= GrowthTestExactFields(Test, FLDProgressionView::StaticStruct(),
        {TEXT("bValid"), TEXT("Generation"), TEXT("Life"), TEXT("LifePhase"), TEXT("Level"), TEXT("MaxLevel"),
         TEXT("CumulativeXP"), TEXT("LevelThresholdXP"), TEXT("bHasNextThreshold"), TEXT("NextThresholdXP"),
         TEXT("ProgressionRevision"), TEXT("BuildRevision"), TEXT("UnspentPoints"), TEXT("StatPoints"), TEXT("Actives"),
         TEXT("PendingBlueprints"), TEXT("ActiveOpportunitiesEarned"), TEXT("ActiveOpportunitiesConsumed"),
         TEXT("QueuedActiveOpportunities"), TEXT("bHasActiveOffer"), TEXT("ActiveOffer"), TEXT("BuildSafeAtMs"), TEXT("CatalogDigest"),
         TEXT("PermanentSpentPoints")});
    bGood &= GrowthTestExactFields(Test, FLDActiveOfferView::StaticStruct(),
        {TEXT("OfferId"), TEXT("OpportunityOrdinal"), TEXT("OfferRevision"), TEXT("Options")});
    bGood &= GrowthTestExactFields(Test, FLDActiveSlotView::StaticStruct(), {TEXT("DefinitionId"), TEXT("Slot")});
    bGood &= GrowthTestExactFields(Test, FLDStatPointsView::StaticStruct(), {TEXT("StatId"), TEXT("Points")});
    bGood &= Test.TestFalse(TEXT("The owner growth view carries no object, delegate or unordered container"),
        GrowthTestCarriesReference(FLDGrowthOwnerView::StaticStruct()));

    const TSet<const UScriptStruct*> GrowthPrivateViews = {FLDGrowthOwnerView::StaticStruct(), FLDProgressionView::StaticStruct(),
        FLDActiveOfferView::StaticStruct(), FLDActiveSlotView::StaticStruct(), FLDStatPointsView::StaticStruct()};
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

// ---- Opening: BeginMatch, the opening offer and the queue at levels 3 and 6 ---------------------

bool GrowthTestOpeningFlag(const FLDDemoContentDefinitions& Content, FName Id)
{
    const FLDDemoAbilityDefinition* Ability = Content.Abilities.FindByPredicate(
        [Id](const FLDDemoAbilityDefinition& Row) { return FName(*Row.Id) == Id; });
    return Ability && Ability->Opening;
}

bool GrowthTestDistinct(const TArray<FName>& Ids)
{
    for (int32 Index = 0; Index < Ids.Num(); ++Index)
    {
        for (int32 Other = Index + 1; Other < Ids.Num(); ++Other)
        {
            if (Ids[Index] == Ids[Other])
            {
                return false;
            }
        }
    }
    return true;
}

// BeginMatch hands every roster contender, human or AI, the opening opportunity with its kept offer of three distinct
// opening actives and the opening points. Only levels 3 and 6 add active opportunities (a death or respawn has no growth
// entry), and a new match starts over.
bool GrowthTestRunOpening(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(Test, Catalog))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
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
    bool bGood = Test.TestTrue(TEXT("A match without a roster is refused"), !Development->BeginMatch(First, {}, Catalog, Error) && !Error.IsEmpty());
    bGood &= Test.TestTrue(TEXT("A roster naming one contender twice is refused"),
        !Development->BeginMatch(First, {Human, Human}, Catalog, Error) && !Error.IsEmpty());
    bGood &= Test.TestTrue(TEXT("A roster with an invalid contender is refused"),
        !Development->BeginMatch(First, {Human, FGuid()}, Catalog, Error) && !Error.IsEmpty());
    bGood &= Test.TestTrue(TEXT("A match without the catalog is refused"),
        !Development->BeginMatch(First, {Human}, nullptr, Error) && !Error.IsEmpty());
    if (!Test.TestTrue(FString::Printf(TEXT("The first match begins (%s)"), *Error), Development->BeginMatch(First, {Human, Bot}, Catalog, Error)))
    {
        return false;
    }
    const auto CheckOpening = [&Test, &Content](const FLDProgressionView& View, uint32 Generation, const FString& Who)
    {
        bool bOpening = Test.TestTrue(Who + TEXT(": level 1 at the opening XP holds exactly the opening opportunity"),
            View.bValid && View.Generation == Generation && View.Level == 1 && View.MaxLevel == 16 && View.CumulativeXP == 0
            && View.ActiveOpportunitiesEarned == 1 && View.ActiveOpportunitiesConsumed == 0 && View.QueuedActiveOpportunities == 1);
        bOpening &= Test.TestTrue(Who + TEXT(": the opening points are unspent, one row per stat in catalog order, nothing learned"),
            View.UnspentPoints == 3 && View.StatPoints.Num() == Content.Stats.Num() && View.Actives.IsEmpty()
            && View.PendingBlueprints.IsEmpty() && View.BuildRevision == 1);
        for (int32 Index = 0; Index < View.StatPoints.Num() && Index < Content.Stats.Num(); ++Index)
        {
            bOpening &= Test.TestTrue(FString::Printf(TEXT("%s: stat row %d is %s with no point"), *Who, Index, *Content.Stats[Index].Id),
                View.StatPoints[Index].StatId == FName(*Content.Stats[Index].Id) && View.StatPoints[Index].Points == 0);
        }
        bOpening &= Test.TestTrue(Who + TEXT(": the opening offer is kept as opportunity 1 with three distinct actives"),
            View.bHasActiveOffer && View.ActiveOffer.OpportunityOrdinal == 1 && View.ActiveOffer.OfferId.IsValid()
            && View.ActiveOffer.OfferRevision > 0 && View.ActiveOffer.Options.Num() == 3 && GrowthTestDistinct(View.ActiveOffer.Options));
        for (const FName Option : View.ActiveOffer.Options)
        {
            bOpening &= Test.TestTrue(FString::Printf(TEXT("%s: opening option %s is flagged for the opening pool"), *Who, *Option.ToString()),
                GrowthTestOpeningFlag(Content, Option));
        }
        bOpening &= Test.TestTrue(Who + TEXT(": the view names the catalog it was drawn from"), View.CatalogDigest == Content.Identity.Root);
        return bOpening;
    };
    TMap<FGuid, FGuid> FirstOffers;
    for (const FGuid& Id : {Human, Bot})
    {
        const FString Who = Id == Human ? TEXT("Human") : TEXT("AI");
        const FLDProgressionView View = Development->ReadProgression(Id);
        bGood &= CheckOpening(View, First.Generation, Who);
        FirstOffers.Add(Id, View.ActiveOffer.OfferId);
        const FLDProgressionView Again = Development->ReadProgression(Id);
        bGood &= Test.TestTrue(Who + TEXT(": a later read (as after a death or respawn) neither grants nor redraws"),
            Again.ActiveOpportunitiesEarned == 1 && Again.QueuedActiveOpportunities == 1 && Again.ActiveOffer.OfferId == View.ActiveOffer.OfferId
            && Again.ActiveOffer.OfferRevision == View.ActiveOffer.OfferRevision && Again.ProgressionRevision == View.ProgressionRevision);
    }
    bGood &= Test.TestNotEqual(TEXT("Each contender draws its own opening offer"), FirstOffers[Human], FirstOffers[Bot]);

    // XP only moves the level; levels 3 and 6 queue one active opportunity each behind the kept opening offer.
    const FLDPrepareGrowthResult Zero = Development->PrepareExperience(Human, 0);
    bGood &= Test.TestTrue(TEXT("A zero grant changes nothing and advances no revision"), Zero.IsSuccess() && !Zero.Value->ChangesBuild()
        && Zero.Value->Progression().Revision == Development->FindProgression(Human)->Revision);
    const FLDPrepareGrowthResult Short = Development->PrepareExperience(Human, 79);
    bGood &= Test.TestTrue(TEXT("79 XP keeps only the opening opportunity and its offer"), Short.IsSuccess()
        && Short.Value->Build().Level == 1 && Short.Value->Progression().ActiveEarned == 1 && !Short.Value->ChangesBuild()
        && Short.Value->Progression().Offer.IsSet() && Short.Value->Progression().Offer->OfferId == FirstOffers[Human]);
    const FLDPrepareGrowthResult Two = Development->PrepareExperience(Human, 80);
    bGood &= Test.TestTrue(TEXT("Level 2 changes the build but queues no active opportunity"), Two.IsSuccess()
        && Two.Value->Build().Level == 2 && Two.Value->ChangesBuild() && Two.Value->Progression().ActiveEarned == 1
        && Two.Value->Build().StatPoints == Development->FindBuild(Human)->StatPoints);
    const FLDPrepareGrowthResult Three = Development->PrepareExperience(Human, 180);
    bGood &= Test.TestTrue(TEXT("Level 3 queues opportunity 2 behind the unchanged opening offer"), Three.IsSuccess()
        && Three.Value->Build().Level == 3 && Three.Value->Progression().ActiveEarned == 2
        && Three.Value->Progression().QueuedActiveOpportunities() == 2
        && Three.Value->Progression().Offer.IsSet() && Three.Value->Progression().Offer->OfferId == FirstOffers[Human]);
    const FLDPrepareGrowthResult Six = Development->PrepareExperience(Human, 600);
    bGood &= Test.TestTrue(TEXT("Level 6 queues opportunity 3"), Six.IsSuccess() && Six.Value->Build().Level == 6
        && Six.Value->Progression().ActiveEarned == 3 && Six.Value->Progression().QueuedActiveOpportunities() == 3);
    const FLDPrepareGrowthResult Top = Development->PrepareExperience(Human, 7300);
    bGood &= Test.TestTrue(TEXT("Level 16 holds the opening opportunity and one per active offer level"), Top.IsSuccess()
        && Top.Value->Build().Level == 16 && Top.Value->Progression().CumulativeXP == 7300
        && int32(Top.Value->Progression().ActiveEarned) == 1 + Content.Progression.ActiveOfferLevels.Num());
    bGood &= Test.TestTrue(TEXT("A grant that does not fit the progression is refused"),
        !Development->PrepareExperience(Human, -1).IsSuccess() && !Development->PrepareExperience(FGuid(0xD10, 9, 9, 9), 80).IsSuccess());

    FLDMatchContext Second;
    Second.Generation = 2;
    Second.LedgerMatch = FGuid(0xD10, 2, 0, 0);
    if (!Test.TestTrue(FString::Printf(TEXT("A new match begins (%s)"), *Error), Development->BeginMatch(Second, {Human, Late}, Catalog, Error)))
    {
        return false;
    }
    const FLDProgressionView Again = Development->ReadProgression(Human);
    bGood &= CheckOpening(Again, Second.Generation, TEXT("Human in the new match"));
    bGood &= Test.TestNotEqual(TEXT("The new match draws a new opening offer"), Again.ActiveOffer.OfferId, FirstOffers[Human]);
    bGood &= CheckOpening(Development->ReadProgression(Late), Second.Generation, TEXT("A contender new to the second match"));
    bGood &= Test.TestFalse(TEXT("A contender outside the new roster has no growth record"), Development->ReadProgression(Bot).bValid);
    return bGood;
}

// ---- Active pools: the opening flag, exclusions, uniform distinct draws ------------------------

bool GrowthTestRunOpeningPool(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(Test, Catalog))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    TArray<FName> All, Opening;
    for (const FLDDemoAbilityDefinition& Ability : Content.Abilities)
    {
        All.Add(FName(*Ability.Id));
        if (Ability.Opening)
        {
            Opening.Add(FName(*Ability.Id));
        }
    }
    const FName NotMad(TEXT("active_not_mad")), StareDown(TEXT("active_stare_down")), FlyerTrap(TEXT("active_flyer_trap"));
    bool bGood = Test.TestTrue(TEXT("The approved catalog holds 16 actives, 13 of them in the opening pool"),
        All.Num() == 16 && Opening.Num() == 13);
    bGood &= Test.TestTrue(TEXT("S14, S15 and S19 stay out of the opening pool"),
        !Opening.Contains(NotMad) && !Opening.Contains(StareDown) && !Opening.Contains(FlyerTrap)
        && All.Contains(NotMad) && All.Contains(StareDown) && All.Contains(FlyerTrap));

    const FLDGrowthBuild Fresh = LDProgression::StartBuild(Content);
    TArray<FName> Eligible;
    bGood &= Test.TestTrue(TEXT("The opening opportunity draws from exactly the opening-flagged actives, in catalog order"),
        LDProgression::EligibleActives(Content, Fresh, 1, Eligible) == ELDGrowthReason::None && Eligible == Opening);
    bGood &= Test.TestTrue(TEXT("Every later opportunity draws from every active"),
        LDProgression::EligibleActives(Content, Fresh, 2, Eligible) == ELDGrowthReason::None && Eligible == All
        && LDProgression::EligibleActives(Content, Fresh, 3, Eligible) == ELDGrowthReason::None && Eligible == All);
    bGood &= GrowthTestReason(Test, TEXT("Ordinal 0 names no opportunity"), LDProgression::EligibleActives(Content, Fresh, 0, Eligible),
        ELDGrowthReason::DataInvalid);
    const FName Elbow(TEXT("active_elbow")), Seethe(TEXT("active_seethe"));
    const FLDGrowthBuild Held = GrowthTestBuild(Content, 3, {{Elbow, 0}}, {Seethe, NotMad});
    bGood &= Test.TestTrue(TEXT("A learned active and pending blueprints are never eligible again"),
        LDProgression::EligibleActives(Content, Held, 1, Eligible) == ELDGrowthReason::None && Eligible.Num() == Opening.Num() - 2
        && !Eligible.Contains(Elbow) && !Eligible.Contains(Seethe)
        && LDProgression::EligibleActives(Content, Held, 2, Eligible) == ELDGrowthReason::None && Eligible.Num() == All.Num() - 3
        && !Eligible.Contains(Elbow) && !Eligible.Contains(Seethe) && !Eligible.Contains(NotMad) && Eligible.Contains(StareDown));
    bGood &= GrowthTestReason(Test, TEXT("An invalid build has no pool"),
        LDProgression::EligibleActives(Content, GrowthTestBuild(Content, 3, {{Elbow, 0}}, {Elbow}), 2, Eligible), ELDGrowthReason::DataInvalid);

    // Uniform without replacement and deterministic in the seed: every pool member shows up about equally often.
    const auto Draws = [&](const FLDGrowthBuild& Build, uint8 Ordinal, const TArray<FName>& Pool, const TCHAR* What)
    {
        TMap<FName, int32> Counts;
        constexpr int32 Rounds = 1300;
        bool bShape = true;
        for (int32 Round = 0; Round < Rounds; ++Round)
        {
            const uint64 Seed = LDProgression::OfferSeed(FGuid(1, 2, 3, 4), FGuid(0xE3, 0, 0, uint32(Round)), Ordinal);
            TArray<FName> Offer, Replay;
            const bool bDrawn = LDProgression::DrawActiveOffer(Content, Build, Ordinal, Seed, Offer) == ELDGrowthReason::None
                && LDProgression::DrawActiveOffer(Content, Build, Ordinal, Seed, Replay) == ELDGrowthReason::None;
            bShape &= bDrawn && Offer == Replay && Offer.Num() == FMath::Min(3, Pool.Num()) && GrowthTestDistinct(Offer);
            for (const FName Id : Offer)
            {
                bShape &= Pool.Contains(Id);
                ++Counts.FindOrAdd(Id);
            }
        }
        bool bGoodDraws = Test.TestTrue(FString::Printf(TEXT("%s: every draw replays, holds min(3, pool) distinct pool members"), What),
            bShape);
        // Each member's expectation is Rounds x 3 / pool; a member far outside half to one and a half of it is no uniform draw.
        const double Expected = double(Rounds) * FMath::Min(3, Pool.Num()) / Pool.Num();
        for (const FName Id : Pool)
        {
            const int32 Count = Counts.FindRef(Id);
            bGoodDraws &= Test.TestTrue(FString::Printf(TEXT("%s: %s drawn %d times of about %.0f"), What, *Id.ToString(), Count, Expected),
                Count > Expected * 0.5 && Count < Expected * 1.5);
        }
        return bGoodDraws;
    };
    bGood &= Draws(Fresh, 1, Opening, TEXT("The opening offer"));
    bGood &= Draws(Fresh, 2, All, TEXT("A level 3 offer of an empty build"));
    TArray<FName> HeldPool;
    LDProgression::EligibleActives(Content, Held, 2, HeldPool);
    bGood &= Draws(Held, 2, HeldPool, TEXT("A later offer of a build with a learned active and blueprints"));

    // Fewer eligible actives show fewer options; none leaves the opportunity without an offer.
    TArray<FLDLearnedActive> ThreeLearned = {{All[0], 0}, {All[1], 1}, {All[2], 2}};
    TArray<FName> Rest;
    for (int32 Index = 3; Index < All.Num(); ++Index)
    {
        Rest.Add(All[Index]);
    }
    TArray<FName> AllButTwo = Rest;
    const FName LeftA = AllButTwo.Pop();
    const FName LeftB = AllButTwo.Pop();
    TArray<FName> Offer;
    bGood &= Test.TestTrue(TEXT("Two eligible actives make an offer of exactly those two"),
        LDProgression::DrawActiveOffer(Content, GrowthTestBuild(Content, 6, ThreeLearned, AllButTwo), 3, 7, Offer) == ELDGrowthReason::None
        && Offer.Num() == 2 && Offer.Contains(LeftA) && Offer.Contains(LeftB));
    bGood &= Test.TestTrue(TEXT("No eligible active makes no offer, without an error"),
        LDProgression::DrawActiveOffer(Content, GrowthTestBuild(Content, 6, ThreeLearned, Rest), 3, 7, Offer) == ELDGrowthReason::None
        && Offer.IsEmpty());
    TArray<FName> OpeningBlueprints;
    TArray<FLDLearnedActive> OpeningLearned;
    for (const FName Id : Opening)
    {
        if (OpeningLearned.Num() < 3)
        {
            OpeningLearned.Add({Id, uint8(OpeningLearned.Num())});
        }
        else
        {
            OpeningBlueprints.Add(Id);
        }
    }
    const FLDGrowthBuild OpeningSpent = GrowthTestBuild(Content, 6, OpeningLearned, OpeningBlueprints);
    bGood &= Test.TestTrue(TEXT("With the opening pool exhausted ordinal 1 draws nothing while a later ordinal draws the rest"),
        LDProgression::DrawActiveOffer(Content, OpeningSpent, 1, 7, Offer) == ELDGrowthReason::None && Offer.IsEmpty()
        && LDProgression::DrawActiveOffer(Content, OpeningSpent, 2, 7, Offer) == ELDGrowthReason::None && Offer.Num() == 3
        && Offer.Contains(NotMad) && Offer.Contains(StareDown) && Offer.Contains(FlyerTrap));
    return bGood;
}

// ---- Legality matrix and the three build changes (pure) ----------------------------------------

LDProgression::FBuildContext GrowthTestContext(LDProgression::EMatchStage Stage, ELDLifePhase Phase, bool bSafe)
{
    LDProgression::FBuildContext Context;
    Context.Stage = Stage;
    Context.LifePhase = Phase;
    Context.bSafe = bSafe;
    return Context;
}

bool GrowthTestRunBuildRules(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(Test, Catalog))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    using EStage = LDProgression::EMatchStage;
    using EKind = ELDBuildCommandKind;
    const LDProgression::FBuildContext RestAlive = GrowthTestContext(EStage::Rest, ELDLifePhase::Alive, false);
    const LDProgression::FBuildContext RestLocked = GrowthTestContext(EStage::RestLocked, ELDLifePhase::Alive, true);
    const LDProgression::FBuildContext Safe = GrowthTestContext(EStage::Live, ELDLifePhase::Alive, true);
    const LDProgression::FBuildContext Unsafe = GrowthTestContext(EStage::Live, ELDLifePhase::Alive, false);
    const LDProgression::FBuildContext Dead = GrowthTestContext(EStage::Live, ELDLifePhase::Dead, false);
    const LDProgression::FBuildContext Closed = GrowthTestContext(EStage::Closed, ELDLifePhase::Alive, true);
    const LDProgression::FBuildContext Unspawned = GrowthTestContext(EStage::Live, ELDLifePhase::Unspawned, true);
    const LDProgression::FBuildContext Left = GrowthTestContext(EStage::Live, ELDLifePhase::Left, true);
    struct FGateRow
    {
        const TCHAR* What;
        const LDProgression::FBuildContext* Context;
        ELDGrowthReason Learn;
        ELDGrowthReason Choice;
        ELDGrowthReason Allocate;
    };
    const FGateRow Matrix[] = {
        {TEXT("rest, alive inside the window"), &RestAlive, ELDGrowthReason::None, ELDGrowthReason::None, ELDGrowthReason::None},
        {TEXT("rest, builds locked"), &RestLocked, ELDGrowthReason::BuildLocked, ELDGrowthReason::BuildLocked, ELDGrowthReason::BuildLocked},
        {TEXT("live, alive and safe"), &Safe, ELDGrowthReason::None, ELDGrowthReason::None, ELDGrowthReason::None},
        {TEXT("live, alive inside the window"), &Unsafe, ELDGrowthReason::NotSafe, ELDGrowthReason::None, ELDGrowthReason::NotSafe},
        {TEXT("live, dead"), &Dead, ELDGrowthReason::Dead, ELDGrowthReason::None, ELDGrowthReason::None},
        {TEXT("closed"), &Closed, ELDGrowthReason::PhaseClosed, ELDGrowthReason::PhaseClosed, ELDGrowthReason::PhaseClosed},
        {TEXT("unspawned"), &Unspawned, ELDGrowthReason::LifePhaseChanged, ELDGrowthReason::LifePhaseChanged, ELDGrowthReason::LifePhaseChanged},
        {TEXT("left"), &Left, ELDGrowthReason::LifePhaseChanged, ELDGrowthReason::LifePhaseChanged, ELDGrowthReason::LifePhaseChanged}};
    bool bGood = true;
    for (const FGateRow& Row : Matrix)
    {
        bGood &= GrowthTestReason(Test, FString::Printf(TEXT("Learning, %s"), Row.What), LDProgression::LearnGate(*Row.Context), Row.Learn);
        bGood &= GrowthTestReason(Test, FString::Printf(TEXT("LearnBlueprint, %s"), Row.What),
            LDProgression::CommandGate(EKind::LearnBlueprint, *Row.Context), Row.Learn);
        bGood &= GrowthTestReason(Test, FString::Printf(TEXT("ActiveChoice, %s"), Row.What),
            LDProgression::CommandGate(EKind::ActiveChoice, *Row.Context), Row.Choice);
        bGood &= GrowthTestReason(Test, FString::Printf(TEXT("AllocatePoints, %s"), Row.What),
            LDProgression::CommandGate(EKind::AllocatePoints, *Row.Context), Row.Allocate);
    }
    bGood &= GrowthTestReason(Test, TEXT("No command kind"), LDProgression::CommandGate(EKind::None, Safe), ELDGrowthReason::InvalidRequest);
    bGood &= GrowthTestReason(Test, TEXT("An unknown command kind is refused, not mapped to a neighbour"),
        LDProgression::CommandGate(static_cast<EKind>(4), Safe), ELDGrowthReason::InvalidRequest);

    const FName Elbow(TEXT("active_elbow")), Seethe(TEXT("active_seethe")), Unplug(TEXT("active_unplug")), NotMad(TEXT("active_not_mad"));
    uint8 Slot = 9;
    bGood &= Test.TestTrue(TEXT("An empty preferred slot is taken"),
        LDProgression::PickLearnSlot(GrowthTestBuild(Content, 1), 1, Slot) && Slot == 1);
    bGood &= Test.TestTrue(TEXT("Without a preference the lowest empty slot is taken"),
        LDProgression::PickLearnSlot(GrowthTestBuild(Content, 1, {{Elbow, 0}}), FLDBuildCommand::AnySlot, Slot) && Slot == 1);
    bGood &= Test.TestTrue(TEXT("An occupied preferred slot falls back to the lowest empty one"),
        LDProgression::PickLearnSlot(GrowthTestBuild(Content, 1, {{Elbow, 0}, {Seethe, 2}}), 2, Slot) && Slot == 1);
    bGood &= Test.TestFalse(TEXT("Three learned actives leave no slot"),
        LDProgression::PickLearnSlot(GrowthTestBuild(Content, 1, {{Elbow, 0}, {Seethe, 1}, {Unplug, 2}}), FLDBuildCommand::AnySlot, Slot));

    // ---- ActiveChoice: learned in the same commit when allowed, otherwise stored with the reason ----
    const FLDGrowthBuild Empty = GrowthTestBuild(Content, 1);
    const FLDGrowthBuild Full = GrowthTestBuild(Content, 6, {{Elbow, 0}, {Seethe, 1}, {Unplug, 2}});
    FLDGrowthBuild Out;
    LDProgression::FChoiceOutcome Outcome;
    const auto Choose = [&](const FLDGrowthBuild& From, FName Option, bool bLearn, uint8 Preferred, const LDProgression::FBuildContext& Context)
    {
        return LDProgression::ApplyActiveChoice(Content, From, Option, bLearn, Preferred, Context, Out, Outcome);
    };
    bGood &= Test.TestTrue(TEXT("The opening choice in the opening rest is learned into Q"),
        Choose(Empty, Elbow, true, FLDBuildCommand::AnySlot, RestAlive) == ELDGrowthReason::None
        && Outcome.Result == ELDBuildChoiceResult::Learned && Outcome.Slot == 0 && Outcome.StoredReason == ELDGrowthReason::None
        && Out.Actives.Num() == 1 && Out.Actives[0].Id == Elbow && Out.Actives[0].Slot == 0 && Out.PendingBlueprints.IsEmpty()
        && Out.Revision == Empty.Revision);
    bGood &= Test.TestTrue(TEXT("A preferred empty slot is honoured"),
        Choose(Empty, Elbow, true, 2, Safe) == ELDGrowthReason::None && Outcome.Result == ELDBuildChoiceResult::Learned
        && Outcome.Slot == 2 && Out.Actives[0].Slot == 2);
    bGood &= Test.TestTrue(TEXT("Inside the safe window the choice is stored as a blueprint, saying NotSafe"),
        Choose(Empty, Elbow, true, FLDBuildCommand::AnySlot, Unsafe) == ELDGrowthReason::None
        && Outcome.Result == ELDBuildChoiceResult::StoredAsBlueprint && Outcome.StoredReason == ELDGrowthReason::NotSafe
        && Out.Actives.IsEmpty() && Out.PendingBlueprints == TArray<FName>({Elbow}));
    bGood &= Test.TestTrue(TEXT("While dead the choice is stored, saying Dead"),
        Choose(Empty, Elbow, true, FLDBuildCommand::AnySlot, Dead) == ELDGrowthReason::None
        && Outcome.Result == ELDBuildChoiceResult::StoredAsBlueprint && Outcome.StoredReason == ELDGrowthReason::Dead);
    bGood &= Test.TestTrue(TEXT("With Q, W and E learned the choice is stored, saying NoEmptySlot"),
        Choose(Full, NotMad, true, FLDBuildCommand::AnySlot, Safe) == ELDGrowthReason::None
        && Outcome.Result == ELDBuildChoiceResult::StoredAsBlueprint && Outcome.StoredReason == ELDGrowthReason::NoEmptySlot
        && Out.Actives.Num() == 3 && Out.PendingBlueprints == TArray<FName>({NotMad}));
    bGood &= Test.TestTrue(TEXT("A choice that does not ask to learn is stored without a reason"),
        Choose(Empty, Elbow, false, FLDBuildCommand::AnySlot, Safe) == ELDGrowthReason::None
        && Outcome.Result == ELDBuildChoiceResult::StoredAsBlueprint && Outcome.StoredReason == ELDGrowthReason::None);
    const FLDGrowthBuild Holding = GrowthTestBuild(Content, 3, {{Elbow, 0}}, {Seethe});
    const auto Refused = [&](const TCHAR* What, ELDGrowthReason Actual, ELDGrowthReason Expected, const FLDGrowthBuild& From) -> bool
    {
        const bool bReason = GrowthTestReason(Test, What, Actual, Expected);
        const bool bUnchanged = Test.TestTrue(FString::Printf(TEXT("%s changes nothing"), What), Out.Actives.Num() == From.Actives.Num()
            && Out.PendingBlueprints == From.PendingBlueprints && Out.StatPoints == From.StatPoints && Out.Level == From.Level);
        return bReason && bUnchanged;
    };
    bGood &= Refused(TEXT("Choosing a learned active"), Choose(Holding, Elbow, true, FLDBuildCommand::AnySlot, Safe),
        ELDGrowthReason::InvalidOption, Holding);
    bGood &= Refused(TEXT("Choosing a pending blueprint"), Choose(Holding, Seethe, true, FLDBuildCommand::AnySlot, Safe),
        ELDGrowthReason::InvalidOption, Holding);
    bGood &= Refused(TEXT("Choosing an unknown active"), Choose(Holding, FName(TEXT("active_unknown")), true, FLDBuildCommand::AnySlot, Safe),
        ELDGrowthReason::InvalidOption, Holding);
    bGood &= Refused(TEXT("A preferred slot past E"), Choose(Holding, Unplug, true, 3, Safe), ELDGrowthReason::InvalidRequest, Holding);
    bGood &= Refused(TEXT("A preferred slot without asking to learn"), Choose(Holding, Unplug, false, 1, Safe),
        ELDGrowthReason::InvalidRequest, Holding);
    bGood &= Refused(TEXT("A choice after the match closed"), Choose(Holding, Unplug, true, FLDBuildCommand::AnySlot, Closed),
        ELDGrowthReason::PhaseClosed, Holding);
    bGood &= Refused(TEXT("A choice before the first life"), Choose(Holding, Unplug, true, FLDBuildCommand::AnySlot, Unspawned),
        ELDGrowthReason::LifePhaseChanged, Holding);

    // ---- LearnBlueprint: into a named empty slot, never a replacement before PR5 ----
    const auto Learn = [&](const FLDGrowthBuild& From, FName Blueprint, uint8 Target, const LDProgression::FBuildContext& Context)
    {
        return LDProgression::ApplyLearnBlueprint(Content, From, Blueprint, Target, Context, Out);
    };
    bGood &= Test.TestTrue(TEXT("A pending blueprint is learned into the named empty slot and leaves the blueprints"),
        Learn(Holding, Seethe, 2, Safe) == ELDGrowthReason::None && Out.Actives.Num() == 2 && Out.Actives[1].Id == Seethe
        && Out.Actives[1].Slot == 2 && Out.PendingBlueprints.IsEmpty());
    bGood &= Test.TestTrue(TEXT("The rest window allows learning whatever the safe window"), Learn(Holding, Seethe, 1, RestAlive) == ELDGrowthReason::None
        && Out.Actives.Num() == 2);
    bGood &= Refused(TEXT("Learning inside the safe window"), Learn(Holding, Seethe, 1, Unsafe), ELDGrowthReason::NotSafe, Holding);
    bGood &= Refused(TEXT("Learning while dead"), Learn(Holding, Seethe, 1, Dead), ELDGrowthReason::Dead, Holding);
    bGood &= Refused(TEXT("Learning into an occupied slot"), Learn(Holding, Seethe, 0, Safe), ELDGrowthReason::SlotOccupied, Holding);
    bGood &= Refused(TEXT("Learning an active that is no pending blueprint"), Learn(Holding, Unplug, 1, Safe), ELDGrowthReason::NoBlueprint,
        Holding);
    bGood &= Refused(TEXT("Learning into a slot past E"), Learn(Holding, Seethe, 3, Safe), ELDGrowthReason::InvalidRequest, Holding);
    bGood &= Refused(TEXT("Learning after the match closed"), Learn(Holding, Seethe, 1, Closed), ELDGrowthReason::PhaseClosed, Holding);

    // ---- AllocatePoints: one atomic step within the unspent points ----
    const auto Allocate = [&](const FLDGrowthBuild& From, int64 XP, const TArray<uint8>& Counts, const LDProgression::FBuildContext& Context)
    {
        return LDProgression::ApplyAllocation(Content, From, XP, Counts, Context, Out);
    };
    bGood &= Test.TestTrue(TEXT("The three opening points are allocated across two stats in one step"),
        Allocate(Empty, 0, {1, 0, 2, 0, 0}, RestAlive) == ELDGrowthReason::None && Out.StatPoints == TArray<int32>({1, 0, 2, 0, 0}));
    const FLDGrowthBuild Spent = Out;
    bGood &= Refused(TEXT("One point more than the unspent ones"), Allocate(Empty, 0, {2, 0, 2, 0, 0}, Safe),
        ELDGrowthReason::NotEnoughPoints, Empty);
    bGood &= Refused(TEXT("Any point once none is left"), Allocate(Spent, 0, {0, 0, 0, 0, 1}, Safe), ELDGrowthReason::NotEnoughPoints, Spent);
    bGood &= Refused(TEXT("An allocation of no point"), Allocate(Empty, 0, {0, 0, 0, 0, 0}, Safe), ELDGrowthReason::InvalidRequest, Empty);
    bGood &= Refused(TEXT("An allocation without a count per stat"), Allocate(Empty, 0, {1, 0, 0, 0}, Safe), ELDGrowthReason::InvalidRequest,
        Empty);
    bGood &= Refused(TEXT("Allocating inside the safe window"), Allocate(Empty, 0, {1, 0, 0, 0, 0}, Unsafe), ELDGrowthReason::NotSafe, Empty);
    bGood &= Refused(TEXT("Allocating after the match closed"), Allocate(Empty, 0, {1, 0, 0, 0, 0}, Closed), ELDGrowthReason::PhaseClosed,
        Empty);
    bGood &= Test.TestTrue(TEXT("A dead contender allocates during the death wait"),
        Allocate(Empty, 0, {0, 0, 0, 3, 0}, Dead) == ELDGrowthReason::None && Out.StatPoints == TArray<int32>({0, 0, 0, 3, 0}));
    const FLDGrowthBuild Top = GrowthTestBuild(Content, 16, {}, {}, {40, 0, 0, 0, 8});
    bGood &= Test.TestTrue(TEXT("The XP grant past level 16 is allocated like any other point"),
        Allocate(Top, 3700, {0, 3, 0, 0, 0}, Safe) == ELDGrowthReason::None && Out.StatPoints == TArray<int32>({40, 3, 0, 0, 8}));
    bGood &= Refused(TEXT("Points not yet granted past level 16"), Allocate(Top, 3699, {0, 1, 0, 0, 0}, Safe),
        ELDGrowthReason::NotEnoughPoints, Top);
    bGood &= Refused(TEXT("An allocation on a build that spent more than it earned"),
        Allocate(GrowthTestBuild(Content, 1, {}, {}, {4, 0, 0, 0, 0}), 0, {0, 1, 0, 0, 0}, Safe), ELDGrowthReason::AuthorityFault,
        GrowthTestBuild(Content, 1, {}, {}, {4, 0, 0, 0, 0}));
    return bGood;
}

// ---- #198 skill growth: G(L) on an active's fixed amounts, never on mana, ratios or durations ----
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
    bool bGood = Test.TestTrue(TEXT("The approved growth steps are 15% through level 10 and 7.5% after"),
        Step == 1500 && LaterStep == 750);
    bGood &= Test.TestTrue(TEXT("G(1)=1.0, G(5)=1.6, G(10)=2.35, G(13)=2.575, G(16)=2.8; no level 0"),
        LDSkillGrowth::FactorBp(Step, LaterStep, 1) == 10000
        && LDSkillGrowth::FactorBp(Step, LaterStep, 5) == 16000
        && LDSkillGrowth::FactorBp(Step, LaterStep, 10) == 23500
        && LDSkillGrowth::FactorBp(Step, LaterStep, 13) == 25750
        && LDSkillGrowth::FactorBp(Step, LaterStep, 16) == 28000
        && LDSkillGrowth::FactorBp(Step, LaterStep, 0) == 0);
    int64 Grown = 0;
    bGood &= Test.TestTrue(TEXT("A grown amount floors to P2"), LDSkillGrowth::GrowP2(6001, 16000, Grown) && Grown == 9601);
    bGood &= Test.TestFalse(TEXT("Overflow is refused"), LDSkillGrowth::GrowP2(MAX_int64 / 2, 23500, Grown));

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
    FLDDemoAbilityValues Authored;
    Authored.ManaCostP2 = 6000; Authored.CooldownMs = 12000;
    Authored.Effects = {Damage, Mana, Timed};
    FLDDemoAbilityValues At5;
    if (!Test.TestTrue(TEXT("A value set grows at level 5"), LDSkillGrowth::GrowValues(Authored, Step, LaterStep, 5, At5)))
    {
        return false;
    }
    bGood &= Test.TestTrue(TEXT("The base amount grows 1.6x; coefficients, ratios and durations do not"),
        At5.Effects[0].BaseP2 == 9600 && At5.Effects[0].ADRatioBp == 5000 && At5.Effects[0].DurationMs == 1000 && At5.Effects[0].RatioBp == 2500);
    bGood &= Test.TestEqual(TEXT("A mana grant does not grow"), At5.Effects[1].BaseP2, int64(1200));
    bGood &= Test.TestTrue(TEXT("A timed bonus grows its points, not its ratios or duration"), At5.Effects[2].Stats[0].Amount == 1920
        && At5.Effects[2].Stats[1].Amount == 3500 && At5.Effects[2].DurationMs == 3000);
    bGood &= Test.TestTrue(TEXT("Mana cost and cooldown never grow"), At5.ManaCostP2 == 6000 && At5.CooldownMs == 12000);

    // The snapshot serves every active's one value set for every level, grown by the approved step, and refuses anything else.
    for (const FLDDemoAbilityDefinition& Ability : Content.Abilities)
    {
        for (int32 Level = 1; Level <= Content.Progression.MaxLevel; ++Level)
        {
            const FLDDemoAbilityValues* Served = Catalog->FindValues(Ability, uint8(Level));
            FLDDemoAbilityValues Expected;
            const bool bExpected = LDSkillGrowth::GrowValues(Ability.Values, Step, LaterStep, uint8(Level), Expected);
            bool bSame = Served && bExpected && Served->Effects.Num() == Expected.Effects.Num()
                && Served->ManaCostP2 == Ability.Values.ManaCostP2 && Served->CooldownMs == Ability.Values.CooldownMs;
            for (int32 Index = 0; bSame && Index < Expected.Effects.Num(); ++Index)
            {
                bSame = Served->Effects[Index].BaseP2 == Expected.Effects[Index].BaseP2;
            }
            if (!bSame)
            {
                bGood &= Test.TestTrue(FString::Printf(TEXT("%s at level %d is served grown"), *Ability.Id, Level), false);
            }
        }
        bGood &= Test.TestNull(Ability.Id + TEXT(" has no level 0 values"), Catalog->FindValues(Ability, 0));
        bGood &= Test.TestNull(Ability.Id + TEXT(" has no values past the top level"),
            Catalog->FindValues(Ability, uint8(Content.Progression.MaxLevel + 1)));
    }
    FLDDemoAbilityDefinition Stranger;
    Stranger.Values = Authored;
    bGood &= Test.TestNull(TEXT("An ability outside the snapshot is refused"), Catalog->FindValues(Stranger, 1));
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

// #198: the legality matrix (rest window, rest steps with builds locked, field alive inside or past the safe window, dead,
// closed) and the atomic build changes behind ActiveChoice, LearnBlueprint and AllocatePoints.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthBuildRulesTest, "Lockdown.Rules.DemoGrowthBuildRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthBuildRulesTest::RunTest(const FString& Parameters)
{
    return GrowthTestRunBuildRules(*this);
}

// #198: an active's fixed amounts grow with its owner's level, G(L) = 1 + 0.15 x min(L-1, 9) + 0.075 x max(L-10, 0).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthSkillGrowthTest, "Lockdown.Rules.DemoGrowthSkillGrowth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthSkillGrowthTest::RunTest(const FString& Parameters)
{
    return GrowthTestRunSkillGrowth(*this);
}

// DEV test commands (#78): shape, the fixture gate, admission through the growth request stream, the Transactions commit
// of XP grants through the reward composite, stale match/life and Left refusals, replay without a second commit, cooldown
// reset on the persistent tracks, the #198 build ops and the pure refill/reset parts; then the map-event reward identity
// (D26) and a reward to a contender who left after the locked fact, on the same growth domain.
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
    const auto Named = [&Command](uint32 Generation, uint64 RequestId, ELDDevTestOp Op, const TCHAR* Id, int64 Amount = 0, uint8 Slot = 0)
    {
        FLDDevTestCommand Out = Command(Generation, 1, RequestId, Op, Amount);
        FLDDefinitionWire::Encode(FName(Id), Out.Definition);
        Out.Slot = Slot;
        return Out;
    };
    GrowthTestReason(*this, TEXT("A positive XP grant is a valid shape"),
        LDDevTest::ValidateShape(Command(1, 1, 1, ELDDevTestOp::GrantExperience, 5)), ELDGrowthReason::None);
    GrowthTestReason(*this, TEXT("Request id 0 is refused"),
        LDDevTest::ValidateShape(Command(1, 1, 0, ELDDevTestOp::GrantExperience, 5)), ELDGrowthReason::InvalidRequest);
    GrowthTestReason(*this, TEXT("An unknown op is refused, not mapped to a neighbour"),
        LDDevTest::ValidateShape(Command(1, 1, 1, static_cast<ELDDevTestOp>(99), 0)), ELDGrowthReason::InvalidRequest);
    // #144 retired GrantGold (2), BuyItem (6) and SellItem (7), #198 LearnOption (5); their numbers are unknown ops, never reused.
    for (const uint8 Retired : {uint8(2), uint8(5), uint8(6), uint8(7)})
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
    // #198 build ops: a blueprint and a learn name an active, an allocation a stat and a positive amount.
    GrowthTestReason(*this, TEXT("A blueprint grant naming an active is a valid shape"),
        LDDevTest::ValidateShape(Named(1, 1, ELDDevTestOp::GrantBlueprint, TEXT("active_seethe"))), ELDGrowthReason::None);
    GrowthTestReason(*this, TEXT("A blueprint grant names a definition"),
        LDDevTest::ValidateShape(Command(1, 1, 1, ELDDevTestOp::GrantBlueprint, 0)), ELDGrowthReason::InvalidRequest);
    GrowthTestReason(*this, TEXT("A blueprint grant names no slot"),
        LDDevTest::ValidateShape(Named(1, 1, ELDDevTestOp::GrantBlueprint, TEXT("active_seethe"), 0, 1)), ELDGrowthReason::InvalidRequest);
    GrowthTestReason(*this, TEXT("A learn into E is a valid shape"),
        LDDevTest::ValidateShape(Named(1, 1, ELDDevTestOp::LearnActive, TEXT("active_seethe"), 0, 3)), ELDGrowthReason::None);
    GrowthTestReason(*this, TEXT("A learn names slot 0..3"),
        LDDevTest::ValidateShape(Named(1, 1, ELDDevTestOp::LearnActive, TEXT("active_seethe"), 0, 4)), ELDGrowthReason::InvalidRequest);
    GrowthTestReason(*this, TEXT("A learn names a definition"),
        LDDevTest::ValidateShape(Command(1, 1, 1, ELDDevTestOp::LearnActive, 0)), ELDGrowthReason::InvalidRequest);
    GrowthTestReason(*this, TEXT("A learn carries no amount"),
        LDDevTest::ValidateShape(Named(1, 1, ELDDevTestOp::LearnActive, TEXT("active_seethe"), 1)), ELDGrowthReason::NumericDomain);
    GrowthTestReason(*this, TEXT("An allocation of a positive amount to a stat is a valid shape"),
        LDDevTest::ValidateShape(Named(1, 1, ELDDevTestOp::AllocatePoints, TEXT("stat_strength"), 2)), ELDGrowthReason::None);
    GrowthTestReason(*this, TEXT("An allocation of no point is refused"),
        LDDevTest::ValidateShape(Named(1, 1, ELDDevTestOp::AllocatePoints, TEXT("stat_strength"), 0)), ELDGrowthReason::NumericDomain);
    GrowthTestReason(*this, TEXT("An allocation names a stat"),
        LDDevTest::ValidateShape(Command(1, 1, 1, ELDDevTestOp::AllocatePoints, 2)), ELDGrowthReason::InvalidRequest);
    GrowthTestReason(*this, TEXT("Only the build ops name a definition"),
        LDDevTest::ValidateShape(Named(1, 1, ELDDevTestOp::ResetCooldowns, TEXT("active_seethe"))), ELDGrowthReason::InvalidRequest);

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
    const auto ProjectWith = [&Catalog](TArray<FLDDemoStatBonus> Dev, TArray<FLDStatPointsValue> Points = {})
    {
        FLDAttributeBuildValues Values(1, MoveTemp(Points));
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
        && DevAD.PermanentChoices.Flat.Numerator == 0);
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
    World->Tick(LEVELTICK_TimeOnly, .1f);

    // One 100 ms tick of native time; every boundary below advances 1 ms inside it.
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
    const FLDCombatEntityRecord& Fighter = Driver->Records.FindChecked(Tester);
    const auto GrowthBuild = [&]() -> const FLDGrowthBuild& { return *Development->FindBuild(Tester); };

    // ---- #198 build ops: like a build command they need a life, never the build safe window ----
    if (!Commit(TEXT("Request 6: learn while unspawned"), Named(Gen, 6, ELDDevTestOp::LearnActive, TEXT("active_seethe")),
        ELDGrowthReason::LifePhaseChanged))
    { return false; }
    Mode->Lives.FindChecked(Tester).Phase = ELDLifePhase::Dead;
    if (!Commit(TEXT("Request 7: a blueprint of an id that is no active"), Named(Gen, 7, ELDDevTestOp::GrantBlueprint, TEXT("stat_strength")),
        ELDGrowthReason::InvalidOption))
    { return false; }

    // ---- DEV attribute layer: Combat's installed build, without a growth build revision ----
    const FLDExpectedBuildVersions InstalledBefore = Fighter.Installed;
    const uint64 GrowthRevisionBefore = GrowthBuild().Revision;
    const FLDDevTestReceipt* AttrReceipt = Commit(TEXT("Request 8: DEV AD +20 while dead"),
        Attribute(8, ELDDemoBonusStat::ADFlat, 2000, Gen), ELDGrowthReason::None);
    if (!AttrReceipt) { return false; }
    TestTrue(TEXT("The receipt names the stat and amount"), AttrReceipt->Stat == uint8(ELDDemoBonusStat::ADFlat) && AttrReceipt->Amount == 2000);
    TestTrue(TEXT("The persistent build holds the DEV term and its projection shows it as the Dev row"),
        Fighter.Build.DevBonuses.Num() == 1 && Fighter.Build.DevBonuses[0].Amount == 2000
        && Fighter.Projection.Final.ADP2 == PlainValue.Final.ADP2 + 2000
        && Fighter.Projection.Contributions[int32(ELDProjectedAttribute::AD)].Dev.Flat.Numerator == 20);
    TestTrue(TEXT("Neither Combat's installed build revision nor the growth build revision moves"),
        Fighter.Installed.BuildRevision == InstalledBefore.BuildRevision && GrowthBuild().Revision == GrowthRevisionBefore);
    const FLDDevTestReceipt* TooMuch = Commit(TEXT("Request 9: DEV max HP past the catalog maximum"),
        Attribute(9, ELDDemoBonusStat::MaxHPFlat, Bounds.MaxHP.MaxP2, Gen), ELDGrowthReason::NumericDomain);
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

    // Points: a DEV allocation spends unspent points on one stat and reaches Combat with the DEV term kept on top.
    const FLDDevTestReceipt* Allocated = Commit(TEXT("Request 10: two points into stat_strength while dead"),
        Named(Gen, 10, ELDDevTestOp::AllocatePoints, TEXT("stat_strength"), 2), ELDGrowthReason::None);
    if (!Allocated) { return false; }
    const FLDAttributeProjectionResult StrengthOnly = ProjectWith({}, {FLDStatPointsValue(TEXT("stat_strength"), 2)});
    TestTrue(TEXT("The build holds the two points and Combat installed it with the DEV term on top"),
        GrowthBuild().StatPoints == TArray<int32>({0, 0, 2, 0, 0}) && Fighter.Installed.BuildRevision == GrowthBuild().Revision
        && StrengthOnly.IsSuccess() && Fighter.Projection.Final.ADP2 == StrengthOnly.Value->Final.ADP2 + 2000
        && Fighter.Build.DevBonuses.Num() == 1);
    const FLDDevTestReceipt* Overdrawn = Commit(TEXT("Request 11: two more points with one left"),
        Named(Gen, 11, ELDDevTestOp::AllocatePoints, TEXT("stat_physique"), 2), ELDGrowthReason::NotEnoughPoints);
    TestTrue(TEXT("The refusal names the unspent points and nothing changed"), Overdrawn && Overdrawn->Detail.Contains(TEXT("1 unspent"))
        && GrowthBuild().StatPoints == TArray<int32>({0, 0, 2, 0, 0}));
    if (!Commit(TEXT("Request 12: points into an id that is no stat"), Named(Gen, 12, ELDDevTestOp::AllocatePoints, TEXT("active_seethe"), 1),
        ELDGrowthReason::InvalidOption))
    { return false; }

    // Blueprints and learns: a blueprint alone never reaches Combat; a learn installs the active in its slot.
    const uint64 InstalledBeforeBlueprint = Fighter.Installed.BuildRevision;
    if (!Commit(TEXT("Request 13: a blueprint of active_seethe"), Named(Gen, 13, ELDDevTestOp::GrantBlueprint, TEXT("active_seethe")),
        ELDGrowthReason::None))
    { return false; }
    TestTrue(TEXT("The blueprint is pending; the growth build revision rises while Combat's installed one stays"),
        GrowthBuild().PendingBlueprints == TArray<FName>({FName(TEXT("active_seethe"))})
        && GrowthBuild().Revision > InstalledBeforeBlueprint && Fighter.Installed.BuildRevision == InstalledBeforeBlueprint);
    if (!Commit(TEXT("Request 14: the same blueprint again"), Named(Gen, 14, ELDDevTestOp::GrantBlueprint, TEXT("active_seethe")),
        ELDGrowthReason::InvalidOption))
    { return false; }
    const FLDDevTestReceipt* LearnedSeethe = Commit(TEXT("Request 15: learn the blueprint into the lowest empty slot"),
        Named(Gen, 15, ELDDevTestOp::LearnActive, TEXT("active_seethe")), ELDGrowthReason::None);
    TestTrue(TEXT("The blueprint is learned in Q and consumed; Combat installed the active at the growth revision"),
        LearnedSeethe && LearnedSeethe->Definition == FName(TEXT("active_seethe")) && GrowthBuild().PendingBlueprints.IsEmpty()
        && GrowthBuild().Actives.Num() == 1 && GrowthBuild().Actives[0].Id == FName(TEXT("active_seethe")) && GrowthBuild().Actives[0].Slot == 0
        && Fighter.Installed.BuildRevision == GrowthBuild().Revision && Fighter.Build.Actives.Num() == 1
        && Fighter.Build.Actives[0].AbilityId == FName(TEXT("active_seethe")) && Fighter.Build.Actives[0].Entry == ELDCombatEntry::Q);
    const FLDDevTestReceipt* Replaced = Commit(TEXT("Request 16: learn active_elbow over Q"),
        Named(Gen, 16, ELDDevTestOp::LearnActive, TEXT("active_elbow"), 0, 1), ELDGrowthReason::None);
    TestTrue(TEXT("The DEV learn forgets the active in the named slot and says so"), Replaced
        && Replaced->Detail.Contains(TEXT("forgotten")) && GrowthBuild().Actives.Num() == 1
        && GrowthBuild().Actives[0].Id == FName(TEXT("active_elbow")) && Fighter.Build.Actives[0].AbilityId == FName(TEXT("active_elbow")));
    if (!Commit(TEXT("Request 17: learn an active already learned"), Named(Gen, 17, ELDDevTestOp::LearnActive, TEXT("active_elbow")),
        ELDGrowthReason::InvalidOption))
    { return false; }
    // The build safe window binds players, not the DEV ops: an unsafe living contender still allocates.
    Mode->Lives.FindChecked(Tester).Phase = ELDLifePhase::Alive;
    Driver->Records.FindChecked(Tester).LastBuildUnsafeAtMs = StepAtMs;
    if (!Commit(TEXT("Request 18: the last point while alive inside the safe window"),
        Named(Gen, 18, ELDDevTestOp::AllocatePoints, TEXT("stat_agility"), 1), ELDGrowthReason::None))
    { return false; }
    TestTrue(TEXT("Every opening point is allocated"), GrowthBuild().StatPoints == TArray<int32>({0, 0, 2, 0, 1}));
    Mode->Lives.FindChecked(Tester).Phase = ELDLifePhase::Dead;
    if (!Commit(TEXT("Request 19: clear the DEV layer"), Command(Gen, 1, 19, ELDDevTestOp::ClearAttributes, 0), ELDGrowthReason::None))
    { return false; }
    TestTrue(TEXT("A clear removes only the DEV term; the learned active and the points stay"),
        Fighter.Build.DevBonuses.IsEmpty() && Fighter.Build.Actives.Num() == 1 && Fighter.Build.StatPoints.Num() == 2);

    // ---- Reset to the match start: the BeginMatch growth state and an empty Combat build, prepared against each other ----
    GrowthTestReason(*this, TEXT("A reset names nothing"),
        LDDevTest::ValidateShape(Command(Gen, 1, 1, ELDDevTestOp::ResetToMatchStart, 5)), ELDGrowthReason::NumericDomain);
    if (!TestTrue(TEXT("The tester holds XP, an active and points before the reset"),
        XP(Tester) == SubLevelXP && GrowthBuild().Actives.Num() == 1))
    { return false; }
    const uint64 BuildRevisionBeforeReset = GrowthBuild().Revision;
    const uint64 OfferRevisionBeforeReset = Development->FindProgression(Tester)->LastOfferRevision;
    Driver->Records.FindChecked(Tester).LastBuildUnsafeAtMs = StepAtMs;
    const FLDDevTestReceipt* ResetReceipt = Commit(TEXT("Request 20: reset to the match start while dead"),
        Command(Gen, 1, 20, ELDDevTestOp::ResetToMatchStart, 0), ELDGrowthReason::None);
    if (!ResetReceipt) { return false; }
    TestTrue(TEXT("The reset receipt explains what changed"), ResetReceipt->Detail.Contains(TEXT("->1"))
        && ResetReceipt->Detail.Contains(TEXT("cooldown tracks dropped")));
    const FLDGrowthBuild* ResetBuild = Development->FindBuild(Tester);
    const FLDProgressionState* ResetProgression = Development->FindProgression(Tester);
    TestTrue(TEXT("Growth is back at level 1 with no XP, actives, blueprints or points and a higher build revision"),
        ResetBuild && ResetBuild->Level == 1 && ResetBuild->Actives.IsEmpty() && ResetBuild->PendingBlueprints.IsEmpty()
        && ResetBuild->StatPoints == TArray<int32>({0, 0, 0, 0, 0}) && ResetBuild->Revision > BuildRevisionBeforeReset
        && ResetProgression && ResetProgression->CumulativeXP == 0);
    TestTrue(TEXT("Only the opening opportunity is open, with a fresh opening offer"),
        ResetProgression && ResetProgression->ActiveEarned == 1 && ResetProgression->ActiveConsumed == 0
        && ResetProgression->Records.IsEmpty() && ResetProgression->Offer.IsSet() && ResetProgression->Offer->Ordinal == 1
        && ResetProgression->Offer->Revision > OfferRevisionBeforeReset && ResetProgression->Offer->Options.Num() == 3);
    TestTrue(TEXT("Combat installed the empty level-1 build at the growth build revision, without a DEV layer"),
        Fighter.Build.Level == 1 && Fighter.Build.StatPoints.IsEmpty() && Fighter.Build.Actives.IsEmpty()
        && Fighter.Build.DevBonuses.IsEmpty() && Fighter.Installed.BuildRevision == ResetBuild->Revision
        && Fighter.Projection.Final.ADP2 == PlainValue.Final.ADP2);
    TestTrue(TEXT("Every learned cooldown track is dropped and the build safe window has passed"), !Fighter.Tracks.ContainsByPredicate(
        [](const FLDCombatTrack& Track) { return Track.Kind != ELDCombatSourceKind::Innate; })
        && Fighter.LastBuildUnsafeAtMs == 0 && Driver->ReadBuildSafeAtMs(Tester) == 0);
    TArray<FLDCombatTrack> Mixed;
    Mixed.AddDefaulted_GetRef().Kind = ELDCombatSourceKind::Learned;
    FLDCombatTrack& Innate = Mixed.AddDefaulted_GetRef();
    Innate.Kind = ELDCombatSourceKind::Innate;
    Innate.ReadyAtMs = 9000;
    Mixed.AddDefaulted_GetRef().Kind = ELDCombatSourceKind::Learned;
    TestTrue(TEXT("The pure track drop keeps only the innate dash, ready now"), ALDCombatDirector::DevDropTracks(Mixed, 1000) == 2
        && Mixed.Num() == 1 && Mixed[0].Kind == ELDCombatSourceKind::Innate && Mixed[0].ReadyAtMs == 1000);
    if (!Commit(TEXT("Request 21: the opening points are allocatable again"),
        Named(Gen, 21, ELDDevTestOp::AllocatePoints, TEXT("stat_brains"), 3), ELDGrowthReason::None))
    { return false; }

    // ---- Match-wide ops: Listen Host connection only ----
    TestTrue(TEXT("Camp refresh and early capture are the match-wide ops"), LDDevTest::IsMatchWide(ELDDevTestOp::RefreshCamps)
        && LDDevTest::IsMatchWide(ELDDevTestOp::OpenCapture) && !LDDevTest::IsMatchWide(ELDDevTestOp::GrantExperience));
    GrowthTestReason(*this, TEXT("A camp refresh names nothing"),
        LDDevTest::ValidateShape(Command(Gen, 1, 1, ELDDevTestOp::RefreshCamps, 1)), ELDGrowthReason::NumericDomain);
    Status(TEXT("A camp refresh from an owning client"), Growth->AdmitDevTest(Tester, Command(Gen, 1, 22, ELDDevTestOp::RefreshCamps, 0)),
        ELDAdmissionStatus::AdmissionRejected, ELDGrowthReason::NotAuthority);
    Status(TEXT("An early capture from an owning client"), Growth->AdmitDevTest(Tester, Command(Gen, 1, 22, ELDDevTestOp::OpenCapture, 0)),
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
    const FLDDevTestReceipt* NoCamps = HostCommit(TEXT("Request 22: the Host's camp refresh without a camp director"),
        Command(Gen, 1, 22, ELDDevTestOp::RefreshCamps, 0), ELDGrowthReason::DataNotReady);
    TestTrue(TEXT("The refusal names the missing camps"), NoCamps && NoCamps->Detail.Contains(TEXT("camp director")));
    if (!TestTrue(TEXT("The capture reducer begins far in the future"), Mode->CaptureReducer.Begin(1000000, 500, 500, Error))) { return false; }
    Mode->MatchPhase = ELDMatchPhase::CaptureOpen;
    if (!HostCommit(TEXT("Request 23: an early capture outside development"), Command(Gen, 1, 23, ELDDevTestOp::OpenCapture, 0),
        ELDGrowthReason::StaleContext)) { return false; }
    TestEqual(TEXT("A refused early capture keeps the scheduled opening"), Mode->CaptureReducer.GetOpenAtMs(), int64(1000000));
    Mode->MatchPhase = ELDMatchPhase::Development;
    {
        // #198: the DEV shortcut needs a running timed field segment; a fixture timeline with 1 ms rest steps reaches
        // field 1 at 2 ms (fixture timing only, not approved durations).
        FLDTimelineParameters Fast;
        Fast.FieldSegmentMs = 1000000;
        Fast.OpeningBuildMs = Fast.BuildMs = Fast.SignupMs = Fast.DrawMs = Fast.BettingMs = Fast.PKReadyMs = Fast.PKMaxMs
            = Fast.SettlementMs = Fast.PayoutMs = Fast.ReturnMs = 1;
        for (int64& Wait : Fast.RespawnMs) { Wait = 1; }
        TArray<FLDTimelineEvent> Events;
        FString TimelineError;
        if (!TestTrue(TEXT("The fixture timeline reaches field 1"), Mode->Timeline.Begin(Fast, 0, TimelineError)
            && Mode->Timeline.Advance(1, 0, true, Events, TimelineError) && Mode->Timeline.Advance(2, 0, true, Events, TimelineError)
            && Mode->Timeline.GetStage() == ELDMatchStage::Field))
        {
            AddError(TimelineError);
            return false;
        }
    }
    if (!HostCommit(TEXT("Request 24: the Host's early capture during development"), Command(Gen, 1, 24, ELDDevTestOp::OpenCapture, 0),
        ELDGrowthReason::None)) { return false; }
    TestTrue(TEXT("The circle opens at the commit boundary and the driver stops there"),
        Mode->CaptureReducer.GetOpenAtMs() == StepAtMs && Mode->NextCaptureDueMs() == StepAtMs && !Mode->CaptureReducer.IsOpen()
        && Mode->Timeline.GetStage() == ELDMatchStage::FinalField);
    FLDCaptureReducer Circle;
    FString CircleError;
    TestFalse(TEXT("An inactive reducer schedules no opening"), Circle.ScheduleOpen(10, CircleError));
    TestTrue(TEXT("A pure reducer begins"), Circle.Begin(5000, 500, 500, CircleError));
    TestFalse(TEXT("A scheduled opening must be earlier than the current schedule"), Circle.ScheduleOpen(5000, CircleError));
    TOptional<FLDCaptureProposal> Proposal;
    TestTrue(TEXT("A scheduled opening is the next CloseBoundary's opening"), Circle.ScheduleOpen(1200, CircleError)
        && Circle.NextDueMs() == 1200 && Circle.CloseBoundary(1200, Proposal, CircleError) && Circle.IsOpen());
    TestFalse(TEXT("An open circle schedules nothing"), Circle.ScheduleOpen(1300, CircleError));

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
    TestTrue(TEXT("The first claim of airdrop 1 is granted"),
        EventOutcome == ELDRewardOutcome::Granted && EventReason == ELDGrowthReason::None && EventReceipt.Recipient == Tester
        && EventReceipt.XPDelta == EventXP && EventReceipt.LevelBefore == 1 && EventReceipt.LevelAfter == EventLevel);
    TestEqual(TEXT("The event XP reaches the recipient's growth"), XP(Tester), EventXPBefore + EventXP);
    TestTrue(TEXT("Its level follows the approved curve and Combat installed the grown build"),
        GrowthBuild().Level == EventLevel && Driver->Records.FindChecked(Tester).Installed.BuildRevision == GrowthBuild().Revision);
    const uint64 ProgressionRevisionAfterEvent = Development->FindProgression(Tester)->Revision;
    const uint64 BuildRevisionAfterEvent = GrowthBuild().Revision;
    if (!ClaimEvent(Tester, Event, 1, EventXP)) { return false; }
    TestTrue(TEXT("A second claim of the same (Event, Serial) is permanently rejected as DataInvalid"),
        EventOutcome == ELDRewardOutcome::PermanentlyRejected && EventReason == ELDGrowthReason::DataInvalid);
    TestTrue(TEXT("The refused second claim pays nothing and moves no revision"), XP(Tester) == EventXPBefore + EventXP
        && Development->FindProgression(Tester)->Revision == ProgressionRevisionAfterEvent && GrowthBuild().Revision == BuildRevisionAfterEvent);
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
    Status(TEXT("A new request after the match closed"), Growth->AdmitDevTest(Tester, Command(Gen, 1, 25, ELDDevTestOp::GrantExperience, 5)),
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
    if (!TestTrue(TEXT("The catalog has two actives"), Catalog->Get().Abilities.Num() >= 2)) { return false; }
    const FLDDemoAbilityDefinition& Active = Catalog->Get().Abilities[0];
    FLDBuildProjectionInput Build;
    Build.Level = 5;
    FLDLearnedActiveValue& Held = Build.Actives.AddDefaulted_GetRef();
    Held.AbilityId = FName(*Active.Id);
    Held.Entry = ELDCombatEntry::W;
    FLDObservedActivesView Public;
    TestTrue(TEXT("A held W active projects its native id into the W slot only"),
        ALDCombatDirector::PublicActivesOf(Build, *Catalog, Public) && Public.Abilities[0] == 0
        && Public.Abilities[1] == Active.NativeId && Public.Abilities[2] == 0);
    FLDBuildProjectionInput Twice = Build;
    FLDLearnedActiveValue& Second = Twice.Actives.AddDefaulted_GetRef();
    Second.AbilityId = FName(*Catalog->Get().Abilities[1].Id);
    Second.Entry = ELDCombatEntry::W;
    TestFalse(TEXT("Two actives never share a public slot"), ALDCombatDirector::PublicActivesOf(Twice, *Catalog, Public));
    FLDBuildProjectionInput Unknown = Build;
    Unknown.Actives[0].AbilityId = FName(TEXT("active_unknown"));
    TestFalse(TEXT("An active outside the catalog is refused"), ALDCombatDirector::PublicActivesOf(Unknown, *Catalog, Public));
    FLDBuildProjectionInput Unbound = Build;
    Unbound.Actives[0].Entry = ELDCombatEntry::None;
    TestFalse(TEXT("An active without a Q/W/E entry is refused"), ALDCombatDirector::PublicActivesOf(Unbound, *Catalog, Public));

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

// #198 build commands through admission and the Transactions commit: the opening choice learned in Q during the countdown,
// points allocated in the countdown, replay and same-terms dedupe, stale build/offer revisions, offers queued at levels 3 and
// 6, the build safe window (NotSafe until BuildSafeWindowSec after the last combat action), dead choices and allocations,
// and a queued opportunity without an eligible active waiting without an offer until a build change frees one.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoGrowthBuildCommandsTest, "Lockdown.Rules.DemoGrowthBuildCommands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoGrowthBuildCommandsTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!GrowthTestReadFixture(*this, Catalog))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    const int64 WindowMs = Content.Parameters.BuildSafeWindowMs;
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Isolated build command world"), World)) { return false; }
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
    // #198: the match opens in the opening rest R0, build step.
    Mode->MatchPhase = ELDMatchPhase::Rest;
    {
        FLDTimelineParameters TimelineParameters;
        FString TimelineError;
        if (!TestTrue(TEXT("The approved timeline begins in the R0 build step"),
            FLDTimelineParameters::FromContent(Content.Parameters, TimelineParameters, TimelineError)
            && Mode->Timeline.Begin(TimelineParameters, 0, TimelineError)))
        {
            AddError(TimelineError);
            return false;
        }
    }
    Mode->Combat = Driver;
    Mode->Growth = Growth;
    Mode->Development = Development;
    Mode->DemoContent = Catalog;
    Mode->AuthorityStep = {Mode->MatchContext, 1, 0, 0};
    const FGuid Tester(0x198, 6, 0, 1);
    const FGuid Hoarder(0x198, 6, 0, 2);
    const TArray<FGuid> Roster = {Tester, Hoarder};
    for (const FGuid& Id : Roster)
    {
        auto& Life = Mode->Lives.Add(Id);
        Life.Kind = ELDVictimKind::Contender;
        Life.Phase = ELDLifePhase::Alive;
        Life.Life = 1;
        Life.Revision = 2;
    }
    FString Error;
    Growth->Match = Mode->MatchContext;
    Growth->Roster = Roster;
    Growth->Content = Catalog;
    if (!TestTrue(TEXT("Growth opens"), Development->BeginMatch(Mode->MatchContext, Roster, Catalog, Error))) { AddError(Error); return false; }
    const FLDAttributeProjectionResult Plain = LDAttributeProjection::Project(FLDAttributeBuildValues(1, {}), *Catalog);
    if (!TestTrue(TEXT("The level-1 projection"), Plain.IsSuccess())) { return false; }
    for (const FGuid& Id : Roster)
    {
        FLDCombatEntityRecord& Record = Driver->Records.Add(Id);
        Record.Kind = ELDVictimKind::Contender;
        Record.Entity = Id;
        Record.Build.Level = 1;
        Record.Projection = Plain.Value.GetValue();
    }
    // Native time for every boundary below, which must stay at or below the world clock.
    for (int32 Tick = 0; Tick < 200; ++Tick) { World->Tick(LEVELTICK_TimeOnly, .1f); }
    int64 StepAtMs = 0;
    const auto RunTransactions = [&](int64 AtMs)
    {
        StepAtMs = FMath::Max(AtMs, StepAtMs + 1);
        Error.Reset();
        const bool bRan = Mode->BeginAuthorityStep(*Driver, Mode->MatchContext, StepAtMs) == ELDCommitReject::None
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Motion, [](const auto&) { return true; }, Error)
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Transactions,
                [&](const FLDGameplayWritePermit& Permit) { return Growth->ProcessTransactions(Permit, Error); }, Error)
            && Mode->FinishAuthorityStep(*Driver, Error);
        if (!bRan) { AddError(FString::Printf(TEXT("Transactions boundary at %lld ms failed: %s"), StepAtMs, *Error)); }
        return bRan;
    };
    const auto Reward = [&](const FGuid& Id, uint64 Serial, int64 Amount)
    {
        StepAtMs += 1;
        Error.Reset();
        ELDRewardOutcome Outcome = ELDRewardOutcome::None;
        ELDGrowthReason Reason = ELDGrowthReason::None;
        FLDRewardReceipt Receipt;
        const bool bRan = Mode->BeginAuthorityStep(*Driver, Mode->MatchContext, StepAtMs) == ELDCommitReject::None
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Motion, [](const auto&) { return true; }, Error)
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Transactions, [&](const FLDGameplayWritePermit& Permit)
                {
                    Outcome = Growth->CommitEventReward(Id, FName(TEXT("airdrop")), Serial, Amount, Permit, Receipt, Reason);
                    return true;
                }, Error)
            && Mode->FinishAuthorityStep(*Driver, Error);
        return TestTrue(FString::Printf(TEXT("An event reward of %lld XP is granted (%s)"), Amount, *Error),
            bRan && Outcome == ELDRewardOutcome::Granted);
    };
    const uint32 Gen = Mode->MatchContext.Generation;
    TMap<FGuid, uint64> NextRequest;
    // The command a client builds from its own accepted view; the RequestId is the next one of that contender's stream.
    const auto Base = [&](const FGuid& Id, ELDBuildCommandKind Kind)
    {
        const FLDProgressionView View = Development->ReadProgression(Id);
        FLDBuildCommand Out;
        Out.Generation = View.Generation;
        Out.Life = View.Life;
        Out.ExpectedLifePhase = View.LifePhase;
        Out.RequestId = NextRequest.FindRef(Id) + 1;
        Out.ExpectedBuildRevision = View.BuildRevision;
        Out.CatalogDigest = View.CatalogDigest;
        Out.Kind = Kind;
        return Out;
    };
    const auto Choice = [&](const FGuid& Id, FName Option, bool bLearn, uint8 Slot = FLDBuildCommand::AnySlot)
    {
        const FLDProgressionView View = Development->ReadProgression(Id);
        FLDBuildCommand Out = Base(Id, ELDBuildCommandKind::ActiveChoice);
        Out.OfferId = View.ActiveOffer.OfferId;
        Out.OfferRevision = View.ActiveOffer.OfferRevision;
        FLDDefinitionWire::Encode(Option, Out.Option);
        Out.bLearnIfPossible = bLearn;
        Out.Slot = Slot;
        return Out;
    };
    const auto LearnBlueprint = [&](const FGuid& Id, FName Blueprint, uint8 Slot)
    {
        FLDBuildCommand Out = Base(Id, ELDBuildCommandKind::LearnBlueprint);
        FLDDefinitionWire::Encode(Blueprint, Out.Blueprint);
        Out.Slot = Slot;
        return Out;
    };
    const auto Allocate = [&](const FGuid& Id, TArray<uint8> Counts)
    {
        FLDBuildCommand Out = Base(Id, ELDBuildCommandKind::AllocatePoints);
        Out.PointCounts = MoveTemp(Counts);
        return Out;
    };
    const auto Query = [&](const FGuid& Id, uint64 RequestId)
    {
        FLDReceiptQuery Lookup;
        Lookup.Generation = Gen;
        Lookup.Family = ELDGrowthFamily::Build;
        Lookup.RequestId = RequestId;
        return Development->QueryBuildReceipt(Id, Lookup);
    };
    // Admits Command, commits it at AtMs and returns its final receipt (an empty one when it never finalized).
    const auto Commit = [&](const FGuid& Id, const FLDBuildCommand& Command, int64 AtMs, const TCHAR* What) -> FLDBuildReceipt
    {
        const FLDSubmitResult Admitted = Development->QueueBuild(Id, Command);
        if (!TestTrue(FString::Printf(TEXT("%s is admitted (%s)"), What, LDGrowthWire::ReasonName(LDGrowthWire::Reason(Admitted.Reason))),
            Admitted.Status == ELDAdmissionStatus::AcceptedPending && Admitted.bSequenceReserved))
        {
            return FLDBuildReceipt();
        }
        NextRequest.FindOrAdd(Id) = Command.RequestId;
        const FLDBuildReceiptQueryResult Pending = Query(Id, Command.RequestId);
        TestTrue(FString::Printf(TEXT("%s is pending until the Transactions phase"), What), Pending.Lookup == ELDReceiptLookup::Pending
            && Pending.bHasPending && Pending.Pending.OriginalLife == Command.Life);
        if (!RunTransactions(AtMs)) { return FLDBuildReceipt(); }
        const FLDBuildReceiptQueryResult Final = Query(Id, Command.RequestId);
        TestTrue(FString::Printf(TEXT("%s finalizes"), What), Final.Lookup == ELDReceiptLookup::Final && Final.bHasReceipt);
        return Final.Receipt;
    };
    const auto Rejected = [&](const FLDBuildReceipt& Receipt, ELDGrowthReason Reason, const TCHAR* What)
    {
        return TestTrue(FString::Printf(TEXT("%s is rejected"), What), Receipt.Header.Outcome == ELDReceiptOutcome::Rejected)
            && GrowthTestReason(*this, FString::Printf(TEXT("%s (reason)"), What), LDGrowthWire::Reason(Receipt.Header.Reason), Reason);
    };
    const auto BuildOf = [&](const FGuid& Id) -> const FLDGrowthBuild& { return *Development->FindBuild(Id); };
    const auto ProgressionOf = [&](const FGuid& Id) -> const FLDProgressionState& { return *Development->FindProgression(Id); };
    const auto Unspent = [&](const FGuid& Id) { return Development->ReadProgression(Id).UnspentPoints; };
    const auto FightOf = [&](const FGuid& Id) -> FLDCombatEntityRecord& { return Driver->Records.FindChecked(Id); };

    // ---- Admission: the shape of each kind, the match and the membership ----
    const FLDProgressionView Opening = Development->ReadProgression(Tester);
    if (!TestTrue(TEXT("The tester holds the opening offer"), Opening.bHasActiveOffer && Opening.ActiveOffer.Options.Num() == 3)) { return false; }
    const FName First = Opening.ActiveOffer.Options[0];
    const auto Refused = [&](const TCHAR* What, const FLDBuildCommand& Command, ELDGrowthReason Reason)
    {
        const FLDSubmitResult Result = Development->QueueBuild(Tester, Command);
        TestTrue(FString::Printf(TEXT("%s is refused at admission"), What),
            Result.Status == ELDAdmissionStatus::AdmissionRejected && !Result.bSequenceReserved && !Result.bHasBuildReceipt);
        GrowthTestReason(*this, FString::Printf(TEXT("%s (admission reason)"), What), LDGrowthWire::Reason(Result.Reason), Reason);
    };
    const FLDBuildCommand Good = Choice(Tester, First, true);
    FLDBuildCommand Bad = Good;
    Bad.Option = FLDDefinitionWire();
    Refused(TEXT("An active choice without an option"), Bad, ELDGrowthReason::InvalidRequest);
    Bad = Good;
    Bad.OfferId.Invalidate();
    Refused(TEXT("An active choice without an offer"), Bad, ELDGrowthReason::InvalidRequest);
    Bad = Good;
    Bad.PointCounts = {1, 0, 0, 0, 0};
    Refused(TEXT("An active choice carrying points"), Bad, ELDGrowthReason::InvalidRequest);
    Bad = Good;
    FLDDefinitionWire::Encode(FName(TEXT("active_seethe")), Bad.Blueprint);
    Refused(TEXT("An active choice naming a blueprint"), Bad, ELDGrowthReason::InvalidRequest);
    Bad = Good;
    Bad.Slot = 3;
    Refused(TEXT("An active choice preferring a slot past E"), Bad, ELDGrowthReason::InvalidRequest);
    Bad = Good;
    Bad.bLearnIfPossible = false;
    Bad.Slot = 1;
    Refused(TEXT("An active choice preferring a slot without asking to learn"), Bad, ELDGrowthReason::InvalidRequest);
    Bad = LearnBlueprint(Tester, First, FLDBuildCommand::AnySlot);
    Refused(TEXT("A blueprint learn without a slot"), Bad, ELDGrowthReason::InvalidRequest);
    Bad = LearnBlueprint(Tester, First, 0);
    Bad.OfferId = Good.OfferId;
    Refused(TEXT("A blueprint learn naming an offer"), Bad, ELDGrowthReason::InvalidRequest);
    Bad = LearnBlueprint(Tester, First, 0);
    Bad.bLearnIfPossible = true;
    Refused(TEXT("A blueprint learn asking to learn if possible"), Bad, ELDGrowthReason::InvalidRequest);
    Refused(TEXT("An allocation without a count per stat"), Allocate(Tester, {1, 0, 0, 0}), ELDGrowthReason::InvalidRequest);
    Refused(TEXT("An allocation of no point"), Allocate(Tester, {0, 0, 0, 0, 0}), ELDGrowthReason::InvalidRequest);
    Bad = Allocate(Tester, {1, 0, 0, 0, 0});
    Bad.Slot = 0;
    Refused(TEXT("An allocation naming a slot"), Bad, ELDGrowthReason::InvalidRequest);
    Bad = Good;
    Bad.Kind = ELDBuildCommandKind::None;
    Refused(TEXT("A command without a kind"), Bad, ELDGrowthReason::InvalidRequest);
    Bad = Good;
    Bad.Kind = static_cast<ELDBuildCommandKind>(4);
    Refused(TEXT("An unknown command kind"), Bad, ELDGrowthReason::InvalidRequest);
    Bad = Good;
    Bad.ExpectedBuildRevision = 0;
    Refused(TEXT("A command without a build revision"), Bad, ELDGrowthReason::InvalidRequest);
    Bad = Good;
    Bad.ExpectedLifePhase = ELDLifePhase::Unspawned;
    Refused(TEXT("A command of an unspawned contender"), Bad, ELDGrowthReason::InvalidRequest);
    Bad = Good;
    Bad.Generation = Gen + 1;
    Refused(TEXT("A command of another match"), Bad, ELDGrowthReason::OldMatch);
    GrowthTestReason(*this, TEXT("A caller outside the roster is refused"),
        LDGrowthWire::Reason(Development->QueueBuild(FGuid(0x198, 6, 0, 9), Good).Reason), ELDGrowthReason::NotContender);
    Bad = Good;
    Bad.RequestId = 2;
    Refused(TEXT("A first request that skips id 1"), Bad, ELDGrowthReason::OutOfOrder);

    // ---- Countdown: the opening choice is learned at once, points are allocated whatever the window ----
    FightOf(Tester).LastBuildUnsafeAtMs = 1;
    const FLDBuildReceipt Opened = Commit(Tester, Good, 1000, TEXT("The opening choice in the countdown"));
    TestTrue(TEXT("The opening choice is applied and learned into Q"), Opened.Header.Outcome == ELDReceiptOutcome::Applied
        && Opened.Kind == ELDBuildCommandKind::ActiveChoice && Opened.DefinitionId == First && Opened.OpportunityOrdinal == 1
        && Opened.ChoiceResult == ELDBuildChoiceResult::Learned && Opened.LearnedSlot == 0
        && LDGrowthWire::Reason(Opened.StoredReason) == ELDGrowthReason::None && Opened.OfferId == Opening.ActiveOffer.OfferId
        && !Opened.bHasNextOffer && Opened.UnspentPointsAfter == 3);
    TestTrue(TEXT("The build holds it in Q; the opportunity is consumed with nothing left queued"),
        BuildOf(Tester).Actives.Num() == 1 && BuildOf(Tester).Actives[0].Id == First && BuildOf(Tester).Actives[0].Slot == 0
        && ProgressionOf(Tester).ActiveConsumed == 1 && ProgressionOf(Tester).QueuedActiveOpportunities() == 0
        && !ProgressionOf(Tester).Offer.IsSet() && ProgressionOf(Tester).Records.Num() == 1
        && ProgressionOf(Tester).Records[0].Chosen == First && ProgressionOf(Tester).Records[0].RequestId == Good.RequestId);
    TestTrue(TEXT("The receipt names the committed revisions and Combat installed the active in Q"),
        Opened.Header.Revisions.bHasBuild && Opened.Header.Revisions.BuildRevision == BuildOf(Tester).Revision
        && Opened.Header.Revisions.ProgressionRevision == ProgressionOf(Tester).Revision
        && FightOf(Tester).Installed.BuildRevision == BuildOf(Tester).Revision && FightOf(Tester).Build.Actives.Num() == 1
        && FightOf(Tester).Build.Actives[0].AbilityId == First && FightOf(Tester).Build.Actives[0].Entry == ELDCombatEntry::Q);
    const uint64 RevisionAfterOpening = BuildOf(Tester).Revision;
    const FLDSubmitResult Replayed = Development->QueueBuild(Tester, Good);
    TestTrue(TEXT("A replay answers the original receipt and changes nothing"), Replayed.Status == ELDAdmissionStatus::Final
        && Replayed.bHasBuildReceipt && Replayed.BuildReceipt.Header.RequestId == Good.RequestId
        && Replayed.BuildReceipt.ChoiceResult == ELDBuildChoiceResult::Learned && BuildOf(Tester).Revision == RevisionAfterOpening);
    FLDBuildCommand Conflict = Good;
    Conflict.Option = FLDDefinitionWire();
    FLDDefinitionWire::Encode(Opening.ActiveOffer.Options[1], Conflict.Option);
    Refused(TEXT("The same request id with another option"), Conflict, ELDGrowthReason::RequestConflict);
    FLDBuildCommand Missing = Choice(Tester, Opening.ActiveOffer.Options[1], true);
    Missing.OfferId = FGuid(0x198, 6, 1, 2);
    Missing.OfferRevision = 1;
    Rejected(Commit(Tester, Missing, 1001, TEXT("A choice without a current offer")), ELDGrowthReason::NoActiveOffer,
        TEXT("A choice without a current offer"));
    FLDBuildCommand Consumed = Choice(Tester, Opening.ActiveOffer.Options[1], true);
    Consumed.OfferId = Opening.ActiveOffer.OfferId;
    Consumed.OfferRevision = Opening.ActiveOffer.OfferRevision;
    Rejected(Commit(Tester, Consumed, 1002, TEXT("A second choice of the consumed opening offer")), ELDGrowthReason::OfferAlreadyCommitted,
        TEXT("A second choice of the consumed opening offer"));

    const FLDBuildCommand Spend = Allocate(Tester, {1, 0, 2, 0, 0});
    const FLDBuildReceipt Spent = Commit(Tester, Spend, 1003, TEXT("Allocating the opening points in the countdown"));
    const FLDAttributeProjectionResult Expected = LDAttributeProjection::Project(FLDAttributeBuildValues(1,
        {FLDStatPointsValue(TEXT("stat_physique"), 1), FLDStatPointsValue(TEXT("stat_strength"), 2)}), *Catalog);
    TestTrue(TEXT("The allocation is applied in one step and reported per stat"), Spent.Header.Outcome == ELDReceiptOutcome::Applied
        && Spent.Kind == ELDBuildCommandKind::AllocatePoints && Spent.PointsSpent == TArray<uint8>({1, 0, 2, 0, 0})
        && Spent.UnspentPointsAfter == 0 && BuildOf(Tester).StatPoints == TArray<int32>({1, 0, 2, 0, 0}) && Unspent(Tester) == 0);
    TestTrue(TEXT("Combat projects the allocated points"), Expected.IsSuccess() && FightOf(Tester).Installed.BuildRevision == BuildOf(Tester).Revision
        && FightOf(Tester).Projection.Final.ADP2 == Expected.Value->Final.ADP2
        && FightOf(Tester).Projection.Final.MaxHPP2 == Expected.Value->Final.MaxHPP2
        && FightOf(Tester).Projection.Final.ADP2 > Plain.Value->Final.ADP2);
    const uint64 RevisionAfterPoints = BuildOf(Tester).Revision;
    Rejected(Commit(Tester, Allocate(Tester, {0, 0, 0, 0, 1}), 1004, TEXT("A point with none left")), ELDGrowthReason::NotEnoughPoints,
        TEXT("A point with none left"));
    TestTrue(TEXT("A refused allocation changes nothing"), BuildOf(Tester).Revision == RevisionAfterPoints
        && BuildOf(Tester).StatPoints == TArray<int32>({1, 0, 2, 0, 0}));

    // ---- Development: level 3 queues an offer of every active but the learned one ----
    Mode->MatchPhase = ELDMatchPhase::Development;
    FightOf(Tester).LastBuildUnsafeAtMs = 0;
    if (!Reward(Tester, 1, 180)) { return false; }
    const FLDProgressionView Three = Development->ReadProgression(Tester);
    TestTrue(TEXT("Level 3 queues opportunity 2 with an offer and adds 6 points"), Three.Level == 3 && Three.ActiveOpportunitiesEarned == 2
        && Three.QueuedActiveOpportunities == 1 && Three.bHasActiveOffer && Three.ActiveOffer.OpportunityOrdinal == 2
        && Three.ActiveOffer.Options.Num() == 3 && GrowthTestDistinct(Three.ActiveOffer.Options)
        && !Three.ActiveOffer.Options.Contains(First) && Three.UnspentPoints == 6);
    FLDBuildCommand Stale = Allocate(Tester, {1, 0, 0, 0, 0});
    Stale.ExpectedBuildRevision -= 1;
    Rejected(Commit(Tester, Stale, StepAtMs + 1, TEXT("An allocation against an older build revision")), ELDGrowthReason::BuildStale,
        TEXT("An allocation against an older build revision"));
    FLDBuildCommand Foreign = Allocate(Tester, {1, 0, 0, 0, 0});
    Foreign.CatalogDigest.Bytes[0] ^= 0xFF;
    Rejected(Commit(Tester, Foreign, StepAtMs + 1, TEXT("A command for another catalog")), ELDGrowthReason::CatalogMismatch,
        TEXT("A command for another catalog"));
    FLDBuildCommand LaterLife = Allocate(Tester, {1, 0, 0, 0, 0});
    LaterLife.Life = 2;
    Rejected(Commit(Tester, LaterLife, StepAtMs + 1, TEXT("A command of another life")), ELDGrowthReason::OldLife,
        TEXT("A command of another life"));
    FLDBuildCommand WrongPhase = Allocate(Tester, {1, 0, 0, 0, 0});
    WrongPhase.ExpectedLifePhase = ELDLifePhase::Dead;
    Rejected(Commit(Tester, WrongPhase, StepAtMs + 1, TEXT("A command expecting another life phase")), ELDGrowthReason::LifePhaseChanged,
        TEXT("A command expecting another life phase"));
    FLDBuildCommand OldGuard = Choice(Tester, Three.ActiveOffer.Options[0], true);
    OldGuard.OfferRevision += 1;
    Rejected(Commit(Tester, OldGuard, StepAtMs + 1, TEXT("A choice against another offer revision")), ELDGrowthReason::OfferGuardStale,
        TEXT("A choice against another offer revision"));
    FLDBuildCommand Unknown = Choice(Tester, Three.ActiveOffer.Options[0], true);
    Unknown.OfferId = FGuid(0x198, 6, 1, 1);
    Rejected(Commit(Tester, Unknown, StepAtMs + 1, TEXT("A choice of an unknown offer")), ELDGrowthReason::UnknownOffer,
        TEXT("A choice of an unknown offer"));
    Rejected(Commit(Tester, Choice(Tester, First, true), StepAtMs + 1, TEXT("A choice of an active outside the offer")),
        ELDGrowthReason::InvalidOption, TEXT("A choice of an active outside the offer"));
    TestTrue(TEXT("Every refusal left the offer and the build untouched"), Development->ReadProgression(Tester).ActiveOffer.OfferId
        == Three.ActiveOffer.OfferId && BuildOf(Tester).Revision == Three.BuildRevision && Unspent(Tester) == 6);

    // ---- The build safe window: a combat action makes learning and allocating NotSafe for BuildSafeWindowSec ----
    const int64 UnsafeAt = StepAtMs + 10;
    FightOf(Tester).LastBuildUnsafeAtMs = UnsafeAt;
    TestEqual(TEXT("Combat reads the window as the last combat action plus BuildSafeWindowSec"), Driver->ReadBuildSafeAtMs(Tester),
        UnsafeAt + WindowMs);
    TestEqual(TEXT("The owner view carries the same window"), Development->ReadProgression(Tester).BuildSafeAtMs, UnsafeAt + WindowMs);
    const FName Chosen = Three.ActiveOffer.Options[0];
    const FLDBuildReceipt Stored = Commit(Tester, Choice(Tester, Chosen, true), UnsafeAt + 1, TEXT("A choice right after combat"));
    TestTrue(TEXT("The choice still consumes its offer but stores the active as a blueprint, saying NotSafe"),
        Stored.Header.Outcome == ELDReceiptOutcome::Applied && Stored.ChoiceResult == ELDBuildChoiceResult::StoredAsBlueprint
        && LDGrowthWire::Reason(Stored.StoredReason) == ELDGrowthReason::NotSafe && Stored.OpportunityOrdinal == 2
        && BuildOf(Tester).PendingBlueprints == TArray<FName>({Chosen}) && BuildOf(Tester).Actives.Num() == 1
        && ProgressionOf(Tester).ActiveConsumed == 2 && !ProgressionOf(Tester).Offer.IsSet());
    TestEqual(TEXT("A blueprint alone does not reach Combat"), FightOf(Tester).Build.Actives.Num(), 1);
    Rejected(Commit(Tester, Allocate(Tester, {0, 1, 0, 0, 0}), UnsafeAt + 2, TEXT("Allocating inside the window")),
        ELDGrowthReason::NotSafe, TEXT("Allocating inside the window"));
    Rejected(Commit(Tester, LearnBlueprint(Tester, Chosen, 1), UnsafeAt + WindowMs - 1, TEXT("Learning one ms before the window ends")),
        ELDGrowthReason::NotSafe, TEXT("Learning one ms before the window ends"));
    TestTrue(TEXT("The refused learn and allocation changed nothing"), BuildOf(Tester).PendingBlueprints == TArray<FName>({Chosen})
        && Unspent(Tester) == 6);
    Rejected(Commit(Tester, LearnBlueprint(Tester, Chosen, 0), UnsafeAt + WindowMs, TEXT("Learning into the occupied Q")),
        ELDGrowthReason::SlotOccupied, TEXT("Learning into the occupied Q"));
    Rejected(Commit(Tester, LearnBlueprint(Tester, Opening.ActiveOffer.Options[2], 1), StepAtMs + 1, TEXT("Learning without the blueprint")),
        ELDGrowthReason::NoBlueprint, TEXT("Learning without the blueprint"));
    const FLDBuildReceipt Learned = Commit(Tester, LearnBlueprint(Tester, Chosen, 1), StepAtMs + 1, TEXT("Learning once the window passed"));
    TestTrue(TEXT("The blueprint is learned into W and Combat installs it"), Learned.Header.Outcome == ELDReceiptOutcome::Applied
        && Learned.Kind == ELDBuildCommandKind::LearnBlueprint && Learned.ChoiceResult == ELDBuildChoiceResult::Learned
        && Learned.LearnedSlot == 1 && Learned.DefinitionId == Chosen && BuildOf(Tester).PendingBlueprints.IsEmpty()
        && BuildOf(Tester).Actives.Num() == 2 && BuildOf(Tester).Actives[1].Id == Chosen && BuildOf(Tester).Actives[1].Slot == 1
        && FightOf(Tester).Build.Actives.Num() == 2 && FightOf(Tester).Installed.BuildRevision == BuildOf(Tester).Revision);
    const FLDBuildReceipt Safe = Commit(Tester, Allocate(Tester, {0, 1, 0, 0, 0}), StepAtMs + 1, TEXT("Allocating once the window passed"));
    TestTrue(TEXT("The safe allocation is applied"), Safe.Header.Outcome == ELDReceiptOutcome::Applied && Safe.UnspentPointsAfter == 5);

    // ---- Dead: a choice is stored, a learn waits for the next life, points can be allocated ----
    Mode->Lives.FindChecked(Tester).Phase = ELDLifePhase::Dead;
    if (!Reward(Tester, 2, 420)) { return false; }
    const FLDProgressionView Six = Development->ReadProgression(Tester);
    TestTrue(TEXT("Level 6 queues opportunity 3 with an offer of the remaining actives and adds 9 points"), Six.Level == 6
        && Six.ActiveOpportunitiesEarned == 3 && Six.QueuedActiveOpportunities == 1 && Six.bHasActiveOffer
        && Six.ActiveOffer.OpportunityOrdinal == 3 && !Six.ActiveOffer.Options.Contains(First) && !Six.ActiveOffer.Options.Contains(Chosen)
        && Six.UnspentPoints == 14 && Six.ActiveOffer.OfferRevision > Three.ActiveOffer.OfferRevision);
    const FName DeadPick = Six.ActiveOffer.Options[0];
    const FLDBuildReceipt DeadChoice = Commit(Tester, Choice(Tester, DeadPick, true), StepAtMs + 1, TEXT("A choice while dead"));
    TestTrue(TEXT("A dead contender still chooses; the active is stored, saying Dead"),
        DeadChoice.Header.Outcome == ELDReceiptOutcome::Applied && DeadChoice.ChoiceResult == ELDBuildChoiceResult::StoredAsBlueprint
        && LDGrowthWire::Reason(DeadChoice.StoredReason) == ELDGrowthReason::Dead && BuildOf(Tester).HasBlueprint(DeadPick)
        && Development->ReadProgression(Tester).QueuedActiveOpportunities == 0);
    Rejected(Commit(Tester, LearnBlueprint(Tester, DeadPick, 2), StepAtMs + 1, TEXT("Learning while dead")), ELDGrowthReason::Dead,
        TEXT("Learning while dead"));
    FightOf(Tester).LastBuildUnsafeAtMs = StepAtMs;
    const FLDBuildReceipt DeadPoints = Commit(Tester, Allocate(Tester, {0, 0, 0, 0, 2}), StepAtMs + 1, TEXT("Allocating while dead"));
    TestTrue(TEXT("The death wait allows allocating, inside the window too"), DeadPoints.Header.Outcome == ELDReceiptOutcome::Applied
        && DeadPoints.UnspentPointsAfter == 12 && BuildOf(Tester).StatPoints == TArray<int32>({1, 1, 2, 0, 2}));

    // ---- An opportunity without an eligible active waits without an offer and never blocks points ----
    {
        auto& Hoard = Development->Records.FindChecked(Hoarder);
        for (const FLDDemoAbilityDefinition& Ability : Content.Abilities)
        {
            Hoard.Build.PendingBlueprints.Add(FName(*Ability.Id));
        }
        LDProgression::NormalizeBuild(Hoard.Build);
        FLDActiveOpportunityRecord& Record = Hoard.Progression.Records.AddDefaulted_GetRef();
        Record.Ordinal = 1;
        Record.OfferId = Hoard.Progression.Offer->OfferId;
        Record.Chosen = Hoard.Progression.Offer->Options[0];
        Hoard.Progression.ActiveConsumed = 1;
        Hoard.Progression.Offer.Reset();
    }
    if (!Reward(Hoarder, 3, 180)) { return false; }
    const FLDProgressionView Waiting = Development->ReadProgression(Hoarder);
    TestTrue(TEXT("Level 3 queues opportunity 2, which keeps no offer while no active is eligible"), Waiting.Level == 3
        && Waiting.QueuedActiveOpportunities == 1 && !Waiting.bHasActiveOffer && Waiting.UnspentPoints == 9);
    FLDBuildCommand NoOffer = Choice(Hoarder, FName(TEXT("active_elbow")), true);
    NoOffer.OfferId = FGuid(0x198, 6, 2, 1);
    NoOffer.OfferRevision = 1;
    Rejected(Commit(Hoarder, NoOffer, StepAtMs + 1, TEXT("A choice while the opportunity waits")), ELDGrowthReason::NoActiveOffer,
        TEXT("A choice while the opportunity waits"));
    const FLDBuildReceipt HoardPoints = Commit(Hoarder, Allocate(Hoarder, {9, 0, 0, 0, 0}), StepAtMs + 1, TEXT("Allocating while waiting"));
    TestTrue(TEXT("The waiting opportunity never blocks points, and the build change finds nothing eligible yet"),
        HoardPoints.Header.Outcome == ELDReceiptOutcome::Applied && HoardPoints.UnspentPointsAfter == 0 && !HoardPoints.bHasNextOffer
        && !Development->ReadProgression(Hoarder).bHasActiveOffer);
    // A build change that frees an active (as a rest-only forget will in #198 PR5) draws the waiting head at once.
    const FName Freed = Development->Records.FindChecked(Hoarder).Build.PendingBlueprints.Last();
    Development->Records.FindChecked(Hoarder).Build.PendingBlueprints.Remove(Freed);
    const FName Kept = Development->Records.FindChecked(Hoarder).Build.PendingBlueprints[0];
    const FLDBuildReceipt Freeing = Commit(Hoarder, LearnBlueprint(Hoarder, Kept, 0), StepAtMs + 1, TEXT("A learn after an active was freed"));
    const FLDProgressionView Drawn = Development->ReadProgression(Hoarder);
    TestTrue(TEXT("The waiting opportunity draws its offer from the one eligible active"),
        Freeing.Header.Outcome == ELDReceiptOutcome::Applied && Freeing.bHasNextOffer && Drawn.bHasActiveOffer
        && Freeing.NextOfferId == Drawn.ActiveOffer.OfferId && Drawn.ActiveOffer.OpportunityOrdinal == 2
        && Drawn.ActiveOffer.Options == TArray<FName>({Freed}));

    // ---- A closed match opens no new request, while a replay still answers ----
    Mode->MatchPhase = ELDMatchPhase::Results;
    Refused(TEXT("A new request after the match closed"), Allocate(Tester, {1, 0, 0, 0, 0}), ELDGrowthReason::MatchClosed);
    GrowthTestEnum(*this, TEXT("A replay after the match closed answers its receipt"), Development->QueueBuild(Tester, Spend).Status,
        ELDAdmissionStatus::Final);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
