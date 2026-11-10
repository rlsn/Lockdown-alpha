#include "Development/LDGrowthProtocol.h"

namespace
{
bool GrowthWireIsIdHead(uint8 Byte)
{
    return Byte >= 'a' && Byte <= 'z';
}

bool GrowthWireIsIdTail(uint8 Byte)
{
    return GrowthWireIsIdHead(Byte) || (Byte >= '0' && Byte <= '9') || Byte == '_';
}

bool GrowthWireSameDigest(const FLDDemoContentDigest& A, const FLDDemoContentDigest& B)
{
    return FMemory::Memcmp(A.Bytes, B.Bytes, sizeof(A.Bytes)) == 0;
}
}

bool FLDDefinitionWire::Encode(FName Id, FLDDefinitionWire& Out)
{
    Out = FLDDefinitionWire();
    // FName identity ignores case and non-editor builds keep only the first registered casing (a reflected
    // name spelled with other capitals changes how the same id prints), so the wire carries the canonical lowercase spelling of the same name.
    const FString Text = Id.ToString().ToLower();
    if (Id.IsNone() || Text.IsEmpty() || Text.Len() >= int32(sizeof(Out.Bytes)))
    {
        return false;
    }
    for (int32 Index = 0; Index < Text.Len(); ++Index)
    {
        const TCHAR Character = Text[Index];
        if (Character > 0x7f || !(Index == 0 ? GrowthWireIsIdHead(uint8(Character)) : GrowthWireIsIdTail(uint8(Character))))
        {
            Out = FLDDefinitionWire();
            return false;
        }
        Out.Bytes[Index] = uint8(Character);
    }
    return true;
}

bool FLDDefinitionWire::Decode(FName& Out) const
{
    Out = NAME_None;
    int32 Length = 0;
    while (Length < int32(sizeof(Bytes)) && Bytes[Length] != 0)
    {
        if (!(Length == 0 ? GrowthWireIsIdHead(Bytes[Length]) : GrowthWireIsIdTail(Bytes[Length])))
        {
            return false;
        }
        ++Length;
    }
    // Length 64 means no terminator; every byte after the terminator must be zero padding.
    if (Length == 0 || Length >= int32(sizeof(Bytes)))
    {
        return false;
    }
    for (int32 Index = Length + 1; Index < int32(sizeof(Bytes)); ++Index)
    {
        if (Bytes[Index] != 0)
        {
            return false;
        }
    }
    FString Text;
    Text.Reserve(Length);
    for (int32 Index = 0; Index < Length; ++Index)
    {
        Text.AppendChar(TCHAR(Bytes[Index]));
    }
    Out = FName(*Text);
    return true;
}

bool FLDBuildCommand::SameTerms(const FLDBuildCommand& Other) const
{
    return Generation == Other.Generation && Life == Other.Life && ExpectedLifePhase == Other.ExpectedLifePhase
        && RequestId == Other.RequestId && ExpectedBuildRevision == Other.ExpectedBuildRevision
        && GrowthWireSameDigest(CatalogDigest, Other.CatalogDigest) && Kind == Other.Kind && OfferId == Other.OfferId
        && OfferRevision == Other.OfferRevision && Option == Other.Option && bLearnIfPossible == Other.bLearnIfPossible
        && Slot == Other.Slot && Blueprint == Other.Blueprint && PointCounts == Other.PointCounts
        && Component == Other.Component && bInstallIfPossible == Other.bInstallIfPossible && TargetKind == Other.TargetKind
        && TargetDefinition == Other.TargetDefinition && TargetSegment == Other.TargetSegment && BoundActive == Other.BoundActive
        && Element == Other.Element;
}

bool LDGrowthWire::IsKnownFamily(ELDGrowthFamily Family)
{
    return Family == ELDGrowthFamily::Build;
}

bool LDGrowthWire::IsKnownBuildKind(ELDBuildCommandKind Kind)
{
    switch (Kind)
    {
    case ELDBuildCommandKind::ActiveChoice:
    case ELDBuildCommandKind::LearnBlueprint:
    case ELDBuildCommandKind::AllocatePoints:
    case ELDBuildCommandKind::OpeningBase:
    case ELDBuildCommandKind::InstallComponent:
    case ELDBuildCommandKind::UninstallComponent:
    case ELDBuildCommandKind::BuyNpcOffer:
    case ELDBuildCommandKind::Respec:
    case ELDBuildCommandKind::ReplaceActive:
    case ELDBuildCommandKind::ForgetActive:
        return true;
    case ELDBuildCommandKind::None:
        break;
    }
    return false;
}

bool LDGrowthWire::IsKnownTargetKind(ELDComponentTargetKind Kind)
{
    switch (Kind)
    {
    case ELDComponentTargetKind::None:
    case ELDComponentTargetKind::Active:
    case ELDComponentTargetKind::EffectSegment:
    case ELDComponentTargetKind::Component:
    case ELDComponentTargetKind::Dash:
        return true;
    }
    return false;
}

bool LDGrowthWire::IsKnownElement(ELDDemoElement Element)
{
    switch (Element)
    {
    case ELDDemoElement::None:
    case ELDDemoElement::Fire:
    case ELDDemoElement::Water:
    case ELDDemoElement::Electric:
        return true;
    }
    return false;
}

bool LDGrowthWire::DecodeOptional(const FLDDefinitionWire& Wire, FName& Out)
{
    Out = NAME_None;
    return Wire == FLDDefinitionWire() || Wire.Decode(Out);
}

bool LDGrowthWire::IsBuildLifePhase(ELDLifePhase Phase)
{
    return Phase == ELDLifePhase::Alive || Phase == ELDLifePhase::Dead;
}

const TCHAR* LDGrowthWire::ReasonName(ELDGrowthReason Reason)
{
    switch (Reason)
    {
    case ELDGrowthReason::None: return TEXT("None");
    case ELDGrowthReason::InvalidRequest: return TEXT("InvalidRequest");
    case ELDGrowthReason::OldMatch: return TEXT("OldMatch");
    case ELDGrowthReason::OldLife: return TEXT("OldLife");
    case ELDGrowthReason::LifePhaseChanged: return TEXT("LifePhaseChanged");
    case ELDGrowthReason::LeftMatch: return TEXT("LeftMatch");
    case ELDGrowthReason::MatchClosed: return TEXT("MatchClosed");
    case ELDGrowthReason::NotContender: return TEXT("NotContender");
    case ELDGrowthReason::CatalogMismatch: return TEXT("CatalogMismatch");
    case ELDGrowthReason::NotAuthority: return TEXT("NotAuthority");
    case ELDGrowthReason::Dead: return TEXT("Dead");
    case ELDGrowthReason::RequestConflict: return TEXT("RequestConflict");
    case ELDGrowthReason::OutOfOrder: return TEXT("OutOfOrder");
    case ELDGrowthReason::ReceiptExpired: return TEXT("ReceiptExpired");
    case ELDGrowthReason::RequestInFlight: return TEXT("RequestInFlight");
    case ELDGrowthReason::UnknownOffer: return TEXT("UnknownOffer");
    case ELDGrowthReason::InvalidOption: return TEXT("InvalidOption");
    case ELDGrowthReason::OfferAlreadyCommitted: return TEXT("OfferAlreadyCommitted");
    case ELDGrowthReason::OfferGuardStale: return TEXT("OfferGuardStale");
    case ELDGrowthReason::BuildStale: return TEXT("BuildStale");
    case ELDGrowthReason::NotSafe: return TEXT("NotSafe");
    case ELDGrowthReason::NoEmptySlot: return TEXT("NoEmptySlot");
    case ELDGrowthReason::SlotOccupied: return TEXT("SlotOccupied");
    case ELDGrowthReason::NoBlueprint: return TEXT("NoBlueprint");
    case ELDGrowthReason::NotEnoughPoints: return TEXT("NotEnoughPoints");
    case ELDGrowthReason::NoActiveOffer: return TEXT("NoActiveOffer");
    case ELDGrowthReason::PhaseClosed: return TEXT("PhaseClosed");
    case ELDGrowthReason::NotOwned: return TEXT("NotOwned");
    case ELDGrowthReason::AlreadyInstalled: return TEXT("AlreadyInstalled");
    case ELDGrowthReason::NotInstalled: return TEXT("NotInstalled");
    case ELDGrowthReason::RestOnly: return TEXT("RestOnly");
    case ELDGrowthReason::InvalidTarget: return TEXT("InvalidTarget");
    case ELDGrowthReason::OpeningBaseUnavailable: return TEXT("OpeningBaseUnavailable");
    case ELDGrowthReason::OpeningBaseTaken: return TEXT("OpeningBaseTaken");
    case ELDGrowthReason::OpeningActiveFirst: return TEXT("OpeningActiveFirst");
    case ELDGrowthReason::ReplacementNotAllowed: return TEXT("ReplacementNotAllowed");
    case ELDGrowthReason::BuildLocked: return TEXT("BuildLocked");
    case ELDGrowthReason::NpcPoolTooSmall: return TEXT("NpcPoolTooSmall");
    case ELDGrowthReason::OfferOutstanding: return TEXT("OfferOutstanding");
    case ELDGrowthReason::NothingToRespec: return TEXT("NothingToRespec");
    case ELDGrowthReason::SlotEmpty: return TEXT("SlotEmpty");
    case ELDGrowthReason::NotRestWindow: return TEXT("NotRestWindow");
    case ELDGrowthReason::DataNotReady: return TEXT("DataNotReady");
    case ELDGrowthReason::StaleContext: return TEXT("StaleContext");
    case ELDGrowthReason::NumericUnderflow: return TEXT("NumericUnderflow");
    case ELDGrowthReason::NumericDomain: return TEXT("NumericDomain");
    case ELDGrowthReason::ArithmeticOverflow: return TEXT("ArithmeticOverflow");
    case ELDGrowthReason::DataInvalid: return TEXT("DataInvalid");
    case ELDGrowthReason::PrepareUnavailable: return TEXT("PrepareUnavailable");
    case ELDGrowthReason::AuthorityFault: return TEXT("AuthorityFault");
    case ELDGrowthReason::CapacityExceeded: return TEXT("CapacityExceeded");
    case ELDGrowthReason::NoRewardNoOwner: return TEXT("NoRewardNoOwner");
    case ELDGrowthReason::NoRewardSelf: return TEXT("NoRewardSelf");
    case ELDGrowthReason::NoRewardOwnerLeft: return TEXT("NoRewardOwnerLeft");
    case ELDGrowthReason::DeathConflict: return TEXT("DeathConflict");
    case ELDGrowthReason::StaleDeath: return TEXT("StaleDeath");
    }
    return TEXT("Unknown");
}