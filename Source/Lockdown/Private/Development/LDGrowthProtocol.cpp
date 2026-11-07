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
    // FName identity ignores case and non-editor builds keep only the first registered casing (a reflected "Shop"
    // turns "shop" into "Shop"), so the wire carries the canonical lowercase spelling of the same name.
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

bool FLDChoiceCommand::SameTerms(const FLDChoiceCommand& Other) const
{
    return Generation == Other.Generation && Life == Other.Life && ExpectedLifePhase == Other.ExpectedLifePhase
        && RequestId == Other.RequestId && OfferId == Other.OfferId && OfferRevision == Other.OfferRevision
        && OptionId == Other.OptionId && ExpectedBuildRevision == Other.ExpectedBuildRevision
        && GrowthWireSameDigest(CatalogDigest, Other.CatalogDigest) && bReplace == Other.bReplace && ReplaceSlot == Other.ReplaceSlot
        && ExpectedOldSkillId == Other.ExpectedOldSkillId && ExpectedOldRank == Other.ExpectedOldRank
        && bReplaceConfirmed == Other.bReplaceConfirmed && bReroll == Other.bReroll;
}

bool FLDShopCommand::SameTerms(const FLDShopCommand& Other) const
{
    return Generation == Other.Generation && Life == Other.Life && RequestId == Other.RequestId && Kind == Other.Kind
        && SessionId == Other.SessionId && QuoteRevision == Other.QuoteRevision && GrowthWireSameDigest(CatalogDigest, Other.CatalogDigest)
        && WorldMapId == Other.WorldMapId && WorldLayoutRevision == Other.WorldLayoutRevision
        && WorldGeometryRevision == Other.WorldGeometryRevision && ExpectedAccountRevision == Other.ExpectedAccountRevision
        && ExpectedInventoryRevision == Other.ExpectedInventoryRevision && ExpectedBuildRevision == Other.ExpectedBuildRevision
        && DefinitionId == Other.DefinitionId && BuyPrice == Other.BuyPrice && Instance == Other.Instance
        && ExpectedItemRevision == Other.ExpectedItemRevision && SellPrice == Other.SellPrice && UpgradePrice == Other.UpgradePrice;
}

bool LDGrowthWire::IsKnownFamily(ELDGrowthFamily Family)
{
    return Family == ELDGrowthFamily::Choice || Family == ELDGrowthFamily::Shop;
}

bool LDGrowthWire::IsKnownShopKind(ELDShopCommandKind Kind)
{
    return Kind == ELDShopCommandKind::Buy || Kind == ELDShopCommandKind::Sell || Kind == ELDShopCommandKind::Upgrade;
}

bool LDGrowthWire::IsChoicePhase(ELDLifePhase Phase)
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
    case ELDGrowthReason::MatchNotStarted: return TEXT("MatchNotStarted");
    case ELDGrowthReason::RequestConflict: return TEXT("RequestConflict");
    case ELDGrowthReason::OutOfOrder: return TEXT("OutOfOrder");
    case ELDGrowthReason::ReceiptExpired: return TEXT("ReceiptExpired");
    case ELDGrowthReason::RequestInFlight: return TEXT("RequestInFlight");
    case ELDGrowthReason::NoOffer: return TEXT("NoOffer");
    case ELDGrowthReason::UnknownOffer: return TEXT("UnknownOffer");
    case ELDGrowthReason::InvalidOption: return TEXT("InvalidOption");
    case ELDGrowthReason::OfferAlreadyCommitted: return TEXT("OfferAlreadyCommitted");
    case ELDGrowthReason::OfferGuardStale: return TEXT("OfferGuardStale");
    case ELDGrowthReason::BuildStale: return TEXT("BuildStale");
    case ELDGrowthReason::ReplacementRequired: return TEXT("ReplacementRequired");
    case ELDGrowthReason::ReplacementMismatch: return TEXT("ReplacementMismatch");
    case ELDGrowthReason::ReplacementNotAllowed: return TEXT("ReplacementNotAllowed");
    case ELDGrowthReason::NoReroll: return TEXT("NoReroll");
    case ELDGrowthReason::OfferAlreadyRerolled: return TEXT("OfferAlreadyRerolled");
    case ELDGrowthReason::NoShopSession: return TEXT("NoShopSession");
    case ELDGrowthReason::QuoteStale: return TEXT("QuoteStale");
    case ELDGrowthReason::PriceChanged: return TEXT("PriceChanged");
    case ELDGrowthReason::UnknownItem: return TEXT("UnknownItem");
    case ELDGrowthReason::ItemNotOwned: return TEXT("ItemNotOwned");
    case ELDGrowthReason::ItemStale: return TEXT("ItemStale");
    case ELDGrowthReason::EquipmentFull: return TEXT("EquipmentFull");
    case ELDGrowthReason::InsufficientGold: return TEXT("InsufficientGold");
    case ELDGrowthReason::InventoryStale: return TEXT("InventoryStale");
    case ELDGrowthReason::AccountStale: return TEXT("AccountStale");
    case ELDGrowthReason::UnknownShop: return TEXT("UnknownShop");
    case ELDGrowthReason::StaleWorld: return TEXT("StaleWorld");
    case ELDGrowthReason::OutsideRange: return TEXT("OutsideRange");
    case ELDGrowthReason::NotGrounded: return TEXT("NotGrounded");
    case ELDGrowthReason::InvalidGround: return TEXT("InvalidGround");
    case ELDGrowthReason::WrongSurface: return TEXT("WrongSurface");
    case ELDGrowthReason::InvalidApproachRegion: return TEXT("InvalidApproachRegion");
    case ELDGrowthReason::DataNotReady: return TEXT("DataNotReady");
    case ELDGrowthReason::StaleContext: return TEXT("StaleContext");
    case ELDGrowthReason::UpgradeUsed: return TEXT("UpgradeUsed");
    case ELDGrowthReason::UpgradeUnavailable: return TEXT("UpgradeUnavailable");
    case ELDGrowthReason::AlreadyUpgraded: return TEXT("AlreadyUpgraded");
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
