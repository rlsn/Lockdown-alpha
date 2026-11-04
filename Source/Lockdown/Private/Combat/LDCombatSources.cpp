#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDTacticalRules.h"
#include "Character/LDCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"

namespace LDCombatSourcesDetail
{
    constexpr ELDCombatEntry LearnedEntries[]={ELDCombatEntry::Q,ELDCombatEntry::W};
    constexpr ELDCombatEntry GearEntries[]={ELDCombatEntry::Item1,ELDCombatEntry::Item2};
    constexpr ELDCombatEntry OwnerEntries[]={ELDCombatEntry::Q,ELDCombatEntry::W,ELDCombatEntry::Item1,ELDCombatEntry::Item2,
        ELDCombatEntry::Dash};

    int32 LearnedSlot(ELDCombatEntry Entry)
    { return Entry==ELDCombatEntry::Q ? 0 : Entry==ELDCombatEntry::W ? 1 : INDEX_NONE; }

    int32 GearSlot(ELDCombatEntry Entry)
    { return Entry==ELDCombatEntry::Item1 ? 0 : Entry==ELDCombatEntry::Item2 ? 1 : INDEX_NONE; }

    const FLDLearnedActiveValue* FindLearned(const FLDBuildProjectionInput& Build, ELDCombatEntry Entry)
    {
        return LearnedSlot(Entry)==INDEX_NONE ? nullptr
            : Build.Actives.FindByPredicate([Entry](const FLDLearnedActiveValue& Candidate) { return Candidate.Entry==Entry; });
    }

    const FLDEquippedItemValue* FindEquipped(const FLDBuildProjectionInput& Build, ELDCombatEntry Entry)
    {
        return GearSlot(Entry)==INDEX_NONE ? nullptr
            : Build.Equipment.FindByPredicate([Entry](const FLDEquippedItemValue& Candidate) { return Candidate.Entry==Entry; });
    }

    const FLDDemoRankDefinition* FindRankRow(const FLDDemoAbilityDefinition& Ability, uint8 Rank)
    { return Ability.Ranks.FindByPredicate([Rank](const FLDDemoRankDefinition& Candidate) { return Candidate.Rank==int32(Rank); }); }

    double RationalValue(const FLDDemoRational& Value)
    { return Value.Denominator>0 ? double(Value.Numerator)/double(Value.Denominator) : 0.; }

    int32 DisplayCm(const FLDDemoRational& Value)
    {
        const double Cm=RationalValue(Value);
        return FMath::IsFinite(Cm) ? FMath::RoundToInt32(FMath::Clamp(Cm,0.,double(MAX_int32))) : 0;
    }

    bool NativeWalkSpeed(const FLDDemoRational& Speed, float& Out)
    {
        const double CmPerSec=Speed.Denominator>0 && Speed.Numerator>0 ? RationalValue(Speed) : 0.;
        if (!FMath::IsFinite(CmPerSec) || CmPerSec<=0 || CmPerSec>MAX_flt) { return false; }
        Out=float(CmPerSec);
        return true;
    }

    int64 CommonDivisor(int64 Left, int64 Right)
    {
        while (Right!=0)
        {
            const int64 Next=Left%Right;
            Left=Right;
            Right=Next;
        }
        return Left<0 ? -Left : Left;
    }

    // Exact reduced sum of two nonnegative ratios; false when it does not fit int64.
    bool AddRatios(const FLDDemoRational& Left, const FLDDemoRational& Right, FLDDemoRational& Out)
    {
        if (Left.Denominator<=0 || Right.Denominator<=0 || Left.Numerator<0 || Right.Numerator<0) { return false; }
        const int64 Shared=CommonDivisor(Left.Denominator,Right.Denominator);
        const int64 LeftScale=Right.Denominator/Shared, RightScale=Left.Denominator/Shared;
        if (Left.Numerator>MAX_int64/LeftScale || Right.Numerator>MAX_int64/RightScale || Left.Denominator>MAX_int64/LeftScale)
        { return false; }
        const int64 LeftPart=Left.Numerator*LeftScale, RightPart=Right.Numerator*RightScale;
        if (LeftPart>MAX_int64-RightPart) { return false; }
        const int64 SumNumerator=LeftPart+RightPart, SumDenominator=Left.Denominator*LeftScale;
        const int64 Divisor=CommonDivisor(SumNumerator,SumDenominator);
        Out.Numerator=SumNumerator/Divisor;
        Out.Denominator=SumDenominator/Divisor;
        return true;
    }

    uint8 RootControlBlocks(const FLDDemoPolicies& Policies)
    {
        uint8 Blocks=0;
        for (const ELDDemoRootBlock Block : Policies.RootBlocks)
        {
            const ELDCombatControlBlock Control=Block==ELDDemoRootBlock::Move ? ELDCombatControlBlock::Move : ELDCombatControlBlock::Displacement;
            Blocks=static_cast<uint8>(Blocks|static_cast<uint8>(Control));
        }
        return Blocks;
    }

    FLDCombatPrepareResult PrepareFailure(ELDCombatPrepareError Code, const FString& Message)
    {
        FLDCombatPrepareResult Refused;
        Refused.Error=Code;
        Refused.Message=Message;
        return Refused;
    }

    FLDCampResetPrepareResult CampResetFailure(ELDCombatPrepareError Code, const FString& Message)
    {
        FLDCampResetPrepareResult Refused;
        Refused.Error=Code;
        Refused.Message=Message;
        return Refused;
    }

    ELDCombatPrepareError FromProjectionError(ELDAttributeProjectionError Code)
    {
        switch (Code)
        {
        case ELDAttributeProjectionError::Catalog: return ELDCombatPrepareError::Catalog;
        case ELDAttributeProjectionError::NumericUnderflow: return ELDCombatPrepareError::NumericUnderflow;
        case ELDAttributeProjectionError::NumericDomain:
        case ELDAttributeProjectionError::OutOfBounds:
        case ELDAttributeProjectionError::Overflow: return ELDCombatPrepareError::NumericDomain;
        default: return ELDCombatPrepareError::InvalidInput;
        }
    }

    // Equipment is projected in slot order (Item1, Item2); entries outside those slots are refused.
    FLDAttributeProjectionResult ProjectInput(const FLDBuildProjectionInput& Build, const FLDDemoContentSnapshot& Content)
    {
        TArray<FString> Equipped;
        for (const ELDCombatEntry Slot : GearEntries)
        {
            if (const FLDEquippedItemValue* Item=FindEquipped(Build,Slot)) { Equipped.Add(Item->DefinitionId.ToString()); }
        }
        if (Equipped.Num()!=Build.Equipment.Num())
        {
            FLDAttributeProjectionResult Refused;
            Refused.Error.Code=ELDAttributeProjectionError::InputDomain;
            Refused.Error.Field=TEXT("Equipment");
            Refused.Error.Message=TEXT("Equipment must occupy distinct Item1/Item2 entries");
            return Refused;
        }
        return LDAttributeProjection::Project(
            FLDAttributeBuildValues(Build.Level,Build.Passives,Build.AttributeCards,MoveTemp(Equipped)),Content);
    }

    bool ProjectedBasicTiming(const FLDAttributeProjection& Projection, const FLDDemoContentSnapshot& Content,
        FLDBasicTiming& Out, ELDCombatPrepareError& Code, FString& Error)
    {
        const FLDDemoContentParameters& Parameters=Content.Get().Parameters;
        FLDBasicTimingInput Basic;
        Basic.BaseAttackSpeed=Parameters.AttackSpeed.Base;
        Basic.CurrentAttackSpeed=Projection.Final.AttackSpeed;
        Basic.BaseWindupMs=Parameters.BasicAttackWindupMs;
        const FLDBasicTimingResult Evaluated=LDTactical::EvaluateBasicTiming(Basic,Content);
        if (!Evaluated.IsSuccess())
        {
            Code=Evaluated.Error.Code==ELDCombatEvaluationError::Definition ? ELDCombatPrepareError::Catalog
                : ELDCombatPrepareError::NumericDomain;
            Error=FString::Printf(TEXT("Basic timing refused: %s: %s"),*Evaluated.Error.Field,*Evaluated.Error.Message);
            return false;
        }
        Out=Evaluated.Value.GetValue();
        return true;
    }

    bool PublicEquipment(const FLDBuildProjectionInput& Build, const FLDDemoContentSnapshot& Content, FLDObservedEquipmentView& Out)
    {
        Out=FLDObservedEquipmentView();
        for (const ELDCombatEntry Slot : GearEntries)
        {
            const FLDEquippedItemValue* Item=FindEquipped(Build,Slot);
            if (!Item) { continue; }
            const FLDDemoEquipmentDefinition* Definition=nullptr;
            FString LookupError;
            if (!Content.FindEquipment(Item->DefinitionId.ToString(),Definition,LookupError) || !Definition) { return false; }
            Out.Slots[GearSlot(Slot)]=Definition->NativeId;
        }
        return true;
    }

    // Shape of the complete post-change build; passives and cards are checked by the attribute projection.
    bool ValidateBuildInput(const FLDBuildProjectionInput& Input, const FLDExpectedBuildVersions& Installed,
        const FLDDemoContentSnapshot& Content, FString& Error)
    {
        const FLDDemoProgressionDefinition& Progression=Content.Get().Progression;
        if (Input.Level<1 || int32(Input.Level)>Progression.MaxLevel)
        {
            Error=FString::Printf(TEXT("InvalidInput: level %d is outside 1..%d."),int32(Input.Level),Progression.MaxLevel);
            return false;
        }
        if (Input.ResultBuildRevision<=Installed.BuildRevision || Input.ResultInventoryRevision<Installed.InventoryRevision)
        {
            Error=TEXT("InvalidInput: the build revision must advance and the inventory revision must not regress.");
            return false;
        }
        if (Input.Actives.Num()>Progression.ActiveSlots || Input.Equipment.Num()>Progression.EquipmentSlots)
        {
            Error=TEXT("InvalidInput: more held sources than the approved active or equipment slots.");
            return false;
        }
        FString LookupError;
        int64 ConsumedChoices=0;
        for (int32 Index=0; Index<Input.Actives.Num(); ++Index)
        {
            const FLDLearnedActiveValue& Learned=Input.Actives[Index];
            const int32 Slot=LearnedSlot(Learned.Entry);
            const FLDDemoAbilityDefinition* Ability=nullptr;
            const bool bKnown=!Learned.AbilityId.IsNone() && Content.FindAbility(Learned.AbilityId.ToString(),Ability,LookupError)
                && Ability && Ability->Kind==ELDDemoAbilityKind::Active;
            bool bDuplicate=false;
            for (int32 Earlier=0; Earlier<Index; ++Earlier)
            {
                if (Input.Actives[Earlier].Entry==Learned.Entry || Input.Actives[Earlier].AbilityId==Learned.AbilityId) { bDuplicate=true; }
            }
            if (Slot==INDEX_NONE || Slot>=Progression.ActiveSlots || !bKnown || bDuplicate || Learned.Rank<1
                || int32(Learned.Rank)>Progression.MaxSkillRank || !FindRankRow(*Ability,Learned.Rank))
            {
                Error=FString::Printf(TEXT("InvalidInput: learned active %d (%s) has an unknown, duplicate or out-of-domain entry, id or rank."),
                    Index,*Learned.AbilityId.ToString());
                return false;
            }
            ConsumedChoices+=Learned.Rank;
        }
        for (int32 Index=0; Index<Input.Equipment.Num(); ++Index)
        {
            const FLDEquippedItemValue& Item=Input.Equipment[Index];
            const int32 Slot=GearSlot(Item.Entry);
            const FLDDemoEquipmentDefinition* Definition=nullptr;
            const bool bKnown=!Item.DefinitionId.IsNone() && Content.FindEquipment(Item.DefinitionId.ToString(),Definition,LookupError)
                && Definition;
            bool bDuplicate=false;
            for (int32 Earlier=0; Earlier<Index; ++Earlier)
            {
                if (Input.Equipment[Earlier].Entry==Item.Entry || Input.Equipment[Earlier].Instance==Item.Instance) { bDuplicate=true; }
            }
            if (Slot==INDEX_NONE || Slot>=Progression.EquipmentSlots || !Item.Instance.IsValid() || !bKnown || bDuplicate)
            {
                Error=FString::Printf(TEXT("InvalidInput: equipment %d (%s) has an unknown, duplicate or out-of-slot entry or instance."),
                    Index,*Item.DefinitionId.ToString());
                return false;
            }
        }
        // Every active rank, passive rank and card consumed one choice: the opening one plus one per level-up.
        for (const FLDLearnedPassiveValue& Passive : Input.Passives) { ConsumedChoices+=Passive.Rank; }
        for (const FLDAttributeCardValue& Card : Input.AttributeCards) { ConsumedChoices+=Card.Count; }
        const int64 ChoiceBound=FMath::Min<int64>(int64(Input.Level),Progression.MaxChoices);
        if (ConsumedChoices>ChoiceBound)
        {
            Error=FString::Printf(TEXT("InvalidInput: the build uses %lld choices; level %d allows at most %lld."),
                ConsumedChoices,int32(Input.Level),ChoiceBound);
            return false;
        }
        return true;
    }

    // Debt first: the latest unexpired unbound track (stable id on ties), else a ready one by stable id.
    const FLDCombatTrack* ChooseGearTrack(const FLDCombatEntityRecord& Record, FName DefinitionId, int64 NowMs,
        const TArray<FGuid>& Claimed, const TArray<FGuid>& HeldInstances)
    {
        const FLDCombatTrack* Debt=nullptr;
        const FLDCombatTrack* Ready=nullptr;
        for (const FLDCombatTrack& Track : Record.Tracks)
        {
            // A track still bound to a copy that stays held is never taken over by another copy.
            if (Track.Kind!=ELDCombatSourceKind::Gear || Track.DefinitionId!=DefinitionId || Claimed.Contains(Track.IssuedCooldownId)
                || (Track.bBound && HeldInstances.Contains(Track.BoundInstance)))
            { continue; }
            if (Track.ReadyAtMs>NowMs)
            {
                if (!Debt || Track.ReadyAtMs>Debt->ReadyAtMs
                    || (Track.ReadyAtMs==Debt->ReadyAtMs && Track.IssuedCooldownId<Debt->IssuedCooldownId))
                { Debt=&Track; }
            }
            else if (!Ready || Track.IssuedCooldownId<Ready->IssuedCooldownId) { Ready=&Track; }
        }
        return Debt ? Debt : Ready;
    }
}

// COMBAT 5.1: learned definitions keep one track; each held active gear copy binds one of at most
// EquipmentSlots tracks per definition, debt before ready before a newly issued one.
bool LDCombatSourceRules::PlanTrackBindings(const FLDCombatEntityRecord& Record, const FLDBuildProjectionInput& Input, int64 NowMs,
    const FLDDemoContentSnapshot& Content, TArray<FLDCombatBindingPlan>& Out, ELDCombatPrepareError& Code, FString& Error)
{
    Out.Reset();
    Code=ELDCombatPrepareError::None;
    TArray<FGuid> Claimed;
    for (const ELDCombatEntry Slot : LDCombatSourcesDetail::LearnedEntries)
    {
        const FLDLearnedActiveValue* Learned=LDCombatSourcesDetail::FindLearned(Input,Slot);
        if (!Learned) { continue; }
        FLDCombatBindingPlan Row;
        Row.Entry=Slot;
        Row.Source.Kind=ELDCombatSourceKind::Learned;
        Row.Source.DefinitionId=Learned->AbilityId;
        // A relearned definition returns to its original track and end time, bound or not.
        if (const FLDCombatTrack* Existing=Record.Tracks.FindByPredicate([Learned](const FLDCombatTrack& Candidate)
            { return Candidate.Kind==ELDCombatSourceKind::Learned && Candidate.DefinitionId==Learned->AbilityId; }))
        {
            Row.IssuedCooldownId=Existing->IssuedCooldownId;
            Row.ReadyAtMs=Existing->ReadyAtMs;
        }
        else
        {
            Row.IssuedCooldownId=FGuid::NewGuid();
            Row.bNewTrack=true;
        }
        if (Claimed.Contains(Row.IssuedCooldownId))
        {
            Code=ELDCombatPrepareError::InvalidInput;
            Error=FString::Printf(TEXT("InvalidInput: %s is learned in two entries."),*Learned->AbilityId.ToString());
            return false;
        }
        Claimed.Add(Row.IssuedCooldownId);
        Out.Add(Row);
    }
    TArray<FGuid> Held;
    for (const FLDEquippedItemValue& Equipped : Input.Equipment)
    {
        if (!Equipped.Instance.IsValid() || Held.Contains(Equipped.Instance))
        {
            Code=ELDCombatPrepareError::InvalidInput;
            Error=TEXT("InvalidInput: equipment instances must be valid and distinct.");
            return false;
        }
        Held.Add(Equipped.Instance);
    }
    struct FGearRow
    {
        FLDCombatBindingPlan Plan;
        bool bResolved=false;
    };
    TArray<FGearRow> GearRows;
    // First every copy that stays held keeps its own bound track.
    for (const ELDCombatEntry Slot : LDCombatSourcesDetail::GearEntries)
    {
        const FLDEquippedItemValue* Item=LDCombatSourcesDetail::FindEquipped(Input,Slot);
        if (!Item) { continue; }
        const FLDDemoEquipmentDefinition* Definition=nullptr;
        FString LookupError;
        if (Item->DefinitionId.IsNone() || !Content.FindEquipment(Item->DefinitionId.ToString(),Definition,LookupError) || !Definition)
        {
            Code=ELDCombatPrepareError::InvalidInput;
            Error=FString::Printf(TEXT("InvalidInput: equipment %s is not in the approved catalog."),*Item->DefinitionId.ToString());
            return false;
        }
        if (!Definition->HasActive) { continue; }
        FGearRow& GearRow=GearRows.AddDefaulted_GetRef();
        GearRow.Plan.Entry=Slot;
        GearRow.Plan.Source.Kind=ELDCombatSourceKind::Gear;
        GearRow.Plan.Source.DefinitionId=Item->DefinitionId;
        GearRow.Plan.Source.ItemInstance=Item->Instance;
        const FLDCombatTrack* Bound=Record.Tracks.FindByPredicate([Item](const FLDCombatTrack& Candidate)
        { return Candidate.Kind==ELDCombatSourceKind::Gear && Candidate.bBound && Candidate.BoundInstance==Item->Instance; });
        if (!Bound) { continue; }
        if (Bound->DefinitionId!=Item->DefinitionId || Claimed.Contains(Bound->IssuedCooldownId))
        {
            Code=ELDCombatPrepareError::InvalidInput;
            Error=TEXT("InvalidInput: an equipment instance is bound to a cooldown track of another definition.");
            return false;
        }
        GearRow.Plan.IssuedCooldownId=Bound->IssuedCooldownId;
        GearRow.Plan.ReadyAtMs=Bound->ReadyAtMs;
        GearRow.bResolved=true;
        Claimed.Add(Bound->IssuedCooldownId);
    }
    const int32 TrackLimit=Content.Get().Progression.EquipmentSlots;
    for (FGearRow& GearRow : GearRows)
    {
        if (GearRow.bResolved) { continue; }
        const FName DefinitionId=GearRow.Plan.Source.DefinitionId;
        if (const FLDCombatTrack* Chosen=LDCombatSourcesDetail::ChooseGearTrack(Record,DefinitionId,NowMs,Claimed,Held))
        {
            GearRow.Plan.IssuedCooldownId=Chosen->IssuedCooldownId;
            GearRow.Plan.ReadyAtMs=Chosen->ReadyAtMs;
        }
        else
        {
            int32 Issued=0;
            for (const FLDCombatTrack& Track : Record.Tracks)
            {
                if (Track.Kind==ELDCombatSourceKind::Gear && Track.DefinitionId==DefinitionId) { ++Issued; }
            }
            for (const FGearRow& Other : GearRows)
            {
                if (Other.Plan.bNewTrack && Other.Plan.Source.DefinitionId==DefinitionId) { ++Issued; }
            }
            if (Issued>=TrackLimit)
            {
                Code=ELDCombatPrepareError::CapacityExceeded;
                Error=FString::Printf(TEXT("CapacityExceeded: %s already has %d persistent cooldown tracks."),
                    *DefinitionId.ToString(),Issued);
                return false;
            }
            GearRow.Plan.IssuedCooldownId=FGuid::NewGuid();
            GearRow.Plan.ReadyAtMs=0;
            GearRow.Plan.bNewTrack=true;
        }
        GearRow.bResolved=true;
        Claimed.Add(GearRow.Plan.IssuedCooldownId);
    }
    for (const FGearRow& GearRow : GearRows) { Out.Add(GearRow.Plan); }
    return true;
}

const FName& LDCombatSourceRules::InnateDashId()
{
    static const FName Id(TEXT("innate_dash"));
    return Id;
}

bool LDCombatSourceRules::InnateDashTerms(const FLDDemoContentSnapshot& Content, const TArray<FLDEquipmentPassiveFamily>& Passives,
    FLDInnateDashTerms& Out, FString& Error)
{
    Out=FLDInnateDashTerms();
    const FLDDemoEquipmentActive& Dash=Content.Get().Character.Dash;
    const FLDDemoEffectDefinition* Move=Dash.Rank.Effects.Num()==1 && Dash.Rank.Effects[0].GetOp()==ELDDemoEffectOp::Dash
        ? &Dash.Rank.Effects[0] : nullptr;
    if (Dash.Target.Mode!=ELDDemoTargetMode::DirectionDash || !Move || Move->DistanceCm.Numerator<=0 || Move->DistanceCm.Denominator<=0
        || Move->DurationMs<=0 || Dash.Rank.CooldownMs<=0 || Dash.Rank.ManaCostP2<0)
    {
        Error=TEXT("Catalog: the approved character has no executable innate dash.");
        return false;
    }
    int64 CooldownMs=Dash.Rank.CooldownMs;
    FLDDemoRational Distance=Move->DistanceCm;
    for (const FLDEquipmentPassiveFamily& Family : Passives)
    {
        const FLDDemoEquipmentDefinition* Item=nullptr;
        FString LookupError;
        if (!Content.FindEquipment(Family.EquipmentId,Item,LookupError) || !Item || !Item->HasPassive)
        {
            Error=FString::Printf(TEXT("Catalog: equipment passive %s has no approved definition. %s"),*Family.Family,*LookupError);
            return false;
        }
        for (const FLDDemoEffectDefinition& Effect : Item->Passive.Effects)
        {
            if (Effect.GetOp()!=ELDDemoEffectOp::DashBoost) { continue; }
            const int64 Scale=10000+Effect.DistanceBonusBp;
            if (Effect.CooldownRatioBp<=0 || Effect.CooldownRatioBp>10000 || Effect.DistanceBonusBp<0
                || CooldownMs>MAX_int64/Effect.CooldownRatioBp || Distance.Numerator>MAX_int64/Scale || Distance.Denominator>MAX_int64/10000)
            {
                Error=FString::Printf(TEXT("NumericDomain: dash boost of %s is outside the checked domain."),*Item->Id);
                return false;
            }
            const int64 Scaled=CooldownMs*Effect.CooldownRatioBp;
            CooldownMs=Scaled/10000+(Scaled%10000!=0 ? 1 : 0);
            const int64 Numerator=Distance.Numerator*Scale, Denominator=Distance.Denominator*10000;
            const int64 Divisor=LDCombatSourcesDetail::CommonDivisor(Numerator,Denominator);
            Distance.Numerator=Numerator/Divisor;
            Distance.Denominator=Denominator/Divisor;
            Out.bBoosted=true;
        }
    }
    if (CooldownMs<=Move->DurationMs)
    {
        Error=TEXT("NumericDomain: a boosted dash would be ready before it ends.");
        return false;
    }
    Out.DistanceCm=Distance;
    Out.DurationMs=Move->DurationMs;
    Out.CooldownMs=CooldownMs;
    Out.ManaCostP2=Dash.Rank.ManaCostP2;
    Error.Reset();
    return true;
}

ELDCombatReject LDCombatSourceRules::ActiveGate(const FLDActiveGateInput& Input, const FLDDemoPolicies& Policies)
{
    if (Input.bCheckBusy && Input.bAbilityInProgress) { return ELDCombatReject::Busy; }
    if (Input.ReadyAtMs>Input.NowMs) { return ELDCombatReject::Cooldown; }
    if (Input.MpP2<Input.ManaCostP2) { return ELDCombatReject::InsufficientMana; }
    if (Input.bDash && Input.Roots && LDTactical::RootBlocks(*Input.Roots,Input.NowMs,Policies,ELDDemoRootBlock::Dash))
    { return ELDCombatReject::ControlBlocked; }
    return ELDCombatReject::None;
}

bool ALDCombatDirector::ProjectBuild(const FLDBuildProjectionInput& Build, FLDAttributeProjection& Out, FString& Error) const
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content)
    {
        Error=TEXT("Build projection requires the approved Demo content.");
        return false;
    }
    const FLDAttributeProjectionResult Projected=LDCombatSourcesDetail::ProjectInput(Build,*Content);
    if (!Projected.IsSuccess())
    {
        Error=FString::Printf(TEXT("Build projection refused: %s: %s"),*Projected.Error.Field,*Projected.Error.Message);
        return false;
    }
    Out=Projected.Value.GetValue();
    Error.Reset();
    return true;
}

const FLDCombatTrack* ALDCombatDirector::TrackFor(const FLDCombatEntityRecord& Record, ELDCombatEntry Entry) const
{
    if (Entry==ELDCombatEntry::Dash)
    {
        return Record.Tracks.FindByPredicate([](const FLDCombatTrack& Track) { return Track.Kind==ELDCombatSourceKind::Innate; });
    }
    if (const FLDLearnedActiveValue* Learned=LDCombatSourcesDetail::FindLearned(Record.Build,Entry))
    {
        return Record.Tracks.FindByPredicate([Learned](const FLDCombatTrack& Track)
        { return Track.Kind==ELDCombatSourceKind::Learned && Track.DefinitionId==Learned->AbilityId; });
    }
    if (const FLDEquippedItemValue* Item=LDCombatSourcesDetail::FindEquipped(Record.Build,Entry))
    {
        return Record.Tracks.FindByPredicate([Item](const FLDCombatTrack& Track)
        {
            return Track.Kind==ELDCombatSourceKind::Gear && Track.bBound && Track.BoundInstance==Item->Instance
                && Track.DefinitionId==Item->DefinitionId;
        });
    }
    return nullptr;
}

FLDCombatTrack* ALDCombatDirector::TrackFor(FLDCombatEntityRecord& Record, ELDCombatEntry Entry)
{
    return const_cast<FLDCombatTrack*>(
        static_cast<const ALDCombatDirector*>(this)->TrackFor(static_cast<const FLDCombatEntityRecord&>(Record),Entry));
}

bool ALDCombatDirector::ResolveEntry(const FLDCombatEntityRecord& Record, ELDCombatEntry Entry, FLDCombatSourceRef& OutSource,
    uint8& OutRank, const FLDDemoAbilityDefinition*& OutAbility, const FLDDemoEquipmentDefinition*& OutEquipment) const
{
    OutSource=FLDCombatSourceRef();
    OutRank=0;
    OutAbility=nullptr;
    OutEquipment=nullptr;
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { return false; }
    FString LookupError;
    if (Entry==ELDCombatEntry::Dash)
    {
        // Every contender holds the innate dash; camp members have no D entry.
        if (Record.Kind!=ELDVictimKind::Contender || Content->Get().Character.Dash.Rank.Effects.IsEmpty()) { return false; }
        OutSource.Kind=ELDCombatSourceKind::Innate;
        OutSource.DefinitionId=LDCombatSourceRules::InnateDashId();
        OutRank=static_cast<uint8>(Content->Get().Character.Dash.Rank.Rank);
        return true;
    }
    if (const FLDLearnedActiveValue* Learned=LDCombatSourcesDetail::FindLearned(Record.Build,Entry))
    {
        const FLDDemoAbilityDefinition* Ability=nullptr;
        if (Learned->AbilityId.IsNone() || !Content->FindAbility(Learned->AbilityId.ToString(),Ability,LookupError) || !Ability
            || Ability->Kind!=ELDDemoAbilityKind::Active || !LDCombatSourcesDetail::FindRankRow(*Ability,Learned->Rank))
        { return false; }
        OutSource.Kind=ELDCombatSourceKind::Learned;
        OutSource.DefinitionId=Learned->AbilityId;
        OutRank=Learned->Rank;
        OutAbility=Ability;
        return true;
    }
    if (const FLDEquippedItemValue* Item=LDCombatSourcesDetail::FindEquipped(Record.Build,Entry))
    {
        const FLDDemoEquipmentDefinition* Equipment=nullptr;
        if (!Item->Instance.IsValid() || Item->DefinitionId.IsNone()
            || !Content->FindEquipment(Item->DefinitionId.ToString(),Equipment,LookupError) || !Equipment)
        { return false; }
        OutSource.Kind=ELDCombatSourceKind::Gear;
        OutSource.DefinitionId=Item->DefinitionId;
        OutSource.ItemInstance=Item->Instance;
        // Equipment actives have one fixed rank; gear without an active resolves and is refused by the caller.
        OutRank=Equipment->HasActive ? static_cast<uint8>(Equipment->Active.Rank.Rank) : 0;
        OutEquipment=Equipment;
        return true;
    }
    return false;
}

bool ALDCombatDirector::ActiveOf(const FLDCombatSourceRef& Source, uint8 Rank, const FLDDemoAbilityDefinition* Ability,
    const FLDDemoEquipmentDefinition* Equipment, const FLDDemoTargetDefinition*& OutTarget, const FLDDemoRankDefinition*& OutRank,
    int64& OutWindupMs) const
{
    OutTarget=nullptr;
    OutRank=nullptr;
    OutWindupMs=0;
    const FLDDemoContentSnapshot* Content=Catalog();
    if (Ability)
    {
        OutTarget=&Ability->Target;
        OutRank=LDCombatSourcesDetail::FindRankRow(*Ability,Rank);
        OutWindupMs=Ability->WindupMs;
    }
    else if (Equipment && Equipment->HasActive)
    {
        OutTarget=&Equipment->Active.Target;
        OutRank=&Equipment->Active.Rank;
        OutWindupMs=Equipment->Active.WindupMs;
    }
    else if (Source.Kind==ELDCombatSourceKind::Innate && Content)
    {
        const FLDDemoEquipmentActive& Dash=Content->Get().Character.Dash;
        OutTarget=&Dash.Target;
        OutRank=&Dash.Rank;
        OutWindupMs=Dash.WindupMs;
    }
    return OutTarget && OutRank;
}

bool ALDCombatDirector::InnateDashFor(const FLDCombatEntityRecord& Record, const FLDBodyCombatState* Body,
    FLDInnateDashTerms& Out, FString& Error) const
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content)
    {
        Error=TEXT("The innate dash requires the approved Demo content.");
        return false;
    }
    // A body uses its installed projection; a dead contender explains the persistent build of the next life.
    const FLDAttributeProjection& Projection=Body ? Body->AttributeProjection : Record.Projection;
    return LDCombatSourceRules::InnateDashTerms(*Content,Projection.EquipmentPassives,Out,Error);
}

bool ALDCombatDirector::PlanBindings(const FLDCombatEntityRecord& Record, const FLDBuildProjectionInput& Input, int64 NowMs,
    TArray<FLDCombatBindingPlan>& Out, FString& Error) const
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content)
    {
        Out.Reset();
        Error=TEXT("Cooldown binding requires the approved Demo content.");
        return false;
    }
    ELDCombatPrepareError Code=ELDCombatPrepareError::None;
    return LDCombatSourceRules::PlanTrackBindings(Record,Input,NowMs,*Content,Out,Code,Error);
}

FLDCombatEntryView ALDCombatDirector::EntryView(const FLDCombatEntityRecord& Record, const FLDBodyCombatState* Body,
    ELDCombatEntry Entry, int64 NowMs) const
{
    FLDCombatEntryView View;
    View.Entry=Entry;
    FLDCombatSourceRef Source;
    uint8 Rank=0;
    const FLDDemoAbilityDefinition* Ability=nullptr;
    const FLDDemoEquipmentDefinition* Equipment=nullptr;
    if (!ResolveEntry(Record,Entry,Source,Rank,Ability,Equipment))
    {
        // An occupied slot whose definition cannot resolve still names what the owner holds.
        if (const FLDLearnedActiveValue* Learned=LDCombatSourcesDetail::FindLearned(Record.Build,Entry))
        {
            View.SourceKind=ELDCombatSourceKind::Learned;
            View.DefinitionId=Learned->AbilityId;
            View.Rank=Learned->Rank;
        }
        else if (const FLDEquippedItemValue* Item=LDCombatSourcesDetail::FindEquipped(Record.Build,Entry))
        {
            View.SourceKind=ELDCombatSourceKind::Gear;
            View.DefinitionId=Item->DefinitionId;
            View.ItemInstance=Item->Instance;
        }
        View.Unavailable=ELDCombatReject::SourceUnavailable;
        return View;
    }
    View.SourceKind=Source.Kind;
    View.DefinitionId=Source.DefinitionId;
    View.ItemInstance=Source.ItemInstance;
    const FLDDemoTargetDefinition* Target=nullptr;
    const FLDDemoRankDefinition* RankRow=nullptr;
    int64 WindupMs=0;
    if (Ability) { View.Rank=Rank; }
    View.bHasActive=ActiveOf(Source,Rank,Ability,Equipment,Target,RankRow,WindupMs);
    if (View.bHasActive)
    {
        View.TargetMode=static_cast<uint8>(Target->Mode);
        View.ManaCostP2=RankRow->ManaCostP2;
        // A dash has no authored target range; its reach is the Dash effect distance of the active rank.
        const FLDDemoEffectDefinition* Dash=Target->Mode==ELDDemoTargetMode::DirectionDash
            ? RankRow->Effects.FindByPredicate([](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp()==ELDDemoEffectOp::Dash; })
            : nullptr;
        View.RangeCm=LDCombatSourcesDetail::DisplayCm(Dash ? Dash->DistanceCm : Target->RangeCm);
        FLDInnateDashTerms Innate;
        FString TermsError;
        if (Source.Kind==ELDCombatSourceKind::Innate)
        {
            // The owner sees the reach this body's held equipment gives the dash right now.
            if (!InnateDashFor(Record,Body,Innate,TermsError))
            {
                View.Unavailable=ELDCombatReject::AuthorityDataUnavailable;
                return View;
            }
            View.RangeCm=LDCombatSourcesDetail::DisplayCm(Innate.DistanceCm);
            View.ManaCostP2=Innate.ManaCostP2;
        }
    }
    if (const FLDCombatTrack* Track=TrackFor(Record,Entry))
    {
        View.IssuedCooldownId=Track->IssuedCooldownId;
        View.ReadyAtMs=Track->ReadyAtMs;
        View.CooldownMs=Track->LastDurationMs;
    }
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!View.bHasActive) { View.Unavailable=ELDCombatReject::NoActiveEffect; return View; }
    if (!Body || Body->bPendingDeath || !Content) { View.Unavailable=ELDCombatReject::Dead; return View; }
    FLDActiveGateInput Gate;
    Gate.NowMs=NowMs;
    Gate.ReadyAtMs=View.ReadyAtMs;
    Gate.MpP2=Body->Resources.MpP2;
    Gate.ManaCostP2=View.ManaCostP2;
    Gate.bDash=Target->Mode==ELDDemoTargetMode::DirectionDash;
    Gate.Roots=&Body->Roots;
    View.Unavailable=LDCombatSourceRules::ActiveGate(Gate,Content->Get().Policies);
    if (View.Unavailable==ELDCombatReject::None && Body->Action.IsSet() && Body->Action->Kind==ELDCombatActionKind::Ability)
    { View.Unavailable=ELDCombatReject::Busy; }
    return View;
}

void ALDCombatDirector::MarkViewChanged(const FGuid& Entity)
{
    if (FLDCombatEntityRecord* Record=FindRecord(Entity)) { ++Record->ViewRevision; }
}

FLDCombatPrepareResult ALDCombatDirector::PrepareBuildChange(const FLDCombatCaller& Caller, const FLDBuildProjectionInput& Input,
    const FLDExpectedBuildVersions& Expected) const
{
    using EPrepare=ELDCombatPrepareError;
    const ALDGameMode* Mode=Core();
    if (!HasAuthority() || !Mode)
    { return LDCombatSourcesDetail::PrepareFailure(EPrepare::NotAuthority,TEXT("NotAuthority: only the authority Combat owner prepares builds.")); }
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content)
    { return LDCombatSourcesDetail::PrepareFailure(EPrepare::Catalog,TEXT("Catalog: the approved Demo content is not loaded.")); }
    if (!Caller.Match.IsValid() || Caller.Match!=Mode->GetMatchContext())
    { return LDCombatSourcesDetail::PrepareFailure(EPrepare::StaleMatch,TEXT("StaleMatch: the caller is not bound to the current match.")); }
    const FLDCombatEntityRecord* Record=Caller.Kind==ELDVictimKind::Contender ? FindRecord(Caller.EntityId) : nullptr;
    if (!Record || Record->Kind!=ELDVictimKind::Contender)
    { return LDCombatSourcesDetail::PrepareFailure(EPrepare::UnknownContender,TEXT("UnknownContender: no persistent Combat record.")); }
    if (Expected.BuildRevision!=Record->Installed.BuildRevision || Expected.InventoryRevision!=Record->Installed.InventoryRevision)
    {
        return LDCombatSourcesDetail::PrepareFailure(EPrepare::StaleBuild,FString::Printf(
            TEXT("StaleBuild: expected %llu/%llu but Combat installed %llu/%llu."),Expected.BuildRevision,
            Expected.InventoryRevision,Record->Installed.BuildRevision,Record->Installed.InventoryRevision));
    }
    FString Reason;
    if (!LDCombatSourcesDetail::ValidateBuildInput(Input,Record->Installed,*Content,Reason))
    { return LDCombatSourcesDetail::PrepareFailure(EPrepare::InvalidInput,Reason); }
    const FLDAttributeProjectionResult Projected=LDCombatSourcesDetail::ProjectInput(Input,*Content);
    if (!Projected.IsSuccess())
    {
        return LDCombatSourcesDetail::PrepareFailure(LDCombatSourcesDetail::FromProjectionError(Projected.Error.Code),
            FString::Printf(TEXT("Projection refused: %s: %s"),*Projected.Error.Field,*Projected.Error.Message));
    }
    const FLDAttributeProjection& Projection=Projected.Value.GetValue();
    EPrepare Code=EPrepare::None;
    FLDBasicTiming Timing;
    float WalkSpeed=0;
    if (!LDCombatSourcesDetail::ProjectedBasicTiming(Projection,*Content,Timing,Code,Reason))
    { return LDCombatSourcesDetail::PrepareFailure(Code,Reason); }
    if (!LDCombatSourcesDetail::NativeWalkSpeed(Projection.Final.MoveSpeedCmPerSec,WalkSpeed))
    { return LDCombatSourcesDetail::PrepareFailure(EPrepare::NumericDomain,TEXT("NumericDomain: the projected move speed has no native value.")); }
    TArray<FLDCombatBindingPlan> Plan;
    if (!LDCombatSourceRules::PlanTrackBindings(*Record,Input,Mode->GetAuthorityNowMs(),*Content,Plan,Code,Reason))
    { return LDCombatSourcesDetail::PrepareFailure(Code,Reason); }
    FLDPreparedCombatProjection Prepared;
    const FLDBodyCombatState* Body=FindEntityBody(Caller.EntityId);
    if (Body && !Body->bPendingDeath)
    {
        // One final-maximum conversion per pool; a dead contender changes only the persistent build.
        FLDResourceMaxChangeInput HpChange;
        HpChange.Pool=ELDResourcePool::HP;
        HpChange.OldCurrentP2=Body->Resources.HpP2;
        HpChange.OldMaxP2=Body->Resources.MaxHpP2;
        HpChange.NewMaxP2=Projection.Final.MaxHPP2;
        FLDResourceMaxChangeInput MpChange;
        MpChange.Pool=ELDResourcePool::MP;
        MpChange.OldCurrentP2=Body->Resources.MpP2;
        MpChange.OldMaxP2=Body->Resources.MaxMpP2;
        MpChange.NewMaxP2=Projection.Final.MaxMPP2;
        const FLDResourceMaxChangeResult Hp=LDAttributeProjection::ResourceMaxChange(HpChange,*Content);
        const FLDResourceMaxChangeResult Mp=LDAttributeProjection::ResourceMaxChange(MpChange,*Content);
        if (!Hp.IsSuccess() || !Mp.IsSuccess())
        {
            const FLDResourceMaxChangeResult& Refused=Hp.IsSuccess() ? Mp : Hp;
            return LDCombatSourcesDetail::PrepareFailure(LDCombatSourcesDetail::FromProjectionError(Refused.Error.Code),
                FString::Printf(TEXT("%s maximum change refused: %s: %s"),Hp.IsSuccess() ? TEXT("MP") : TEXT("HP"),
                    *Refused.Error.Field,*Refused.Error.Message));
        }
        Prepared.bAlive=true;
        Prepared.Body={Body->Victim,Body->Identity};
        Prepared.ExpectedResourceRevision=Body->Resources.Revision;
        Prepared.NewHPP2=Hp.Value->NewCurrentP2;
        Prepared.NewMaxHPP2=Projection.Final.MaxHPP2;
        Prepared.NewMPP2=Mp.Value->NewCurrentP2;
        Prepared.NewMaxMPP2=Projection.Final.MaxMPP2;
    }
    Prepared.Match=Mode->GetMatchContext();
    Prepared.Contender=Caller.EntityId;
    Prepared.Expected=Expected;
    Prepared.Input=Input;
    Prepared.Projection=Projection;
    Prepared.BindingPlan=MoveTemp(Plan);
    Prepared.ExpectedSourceRevision=Record->SourceRevision;
    FLDCombatPrepareResult Result;
    Result.Value.Emplace(MoveTemp(Prepared));
    return Result;
}

void ALDCombatDirector::ApplyBuildChange(FLDPreparedCombatProjection&& Prepared, const FLDGameplayWritePermit& Permit)
{
    ALDGameMode* Mode=Core();
    if (!Mode)
    {
        UE_LOG(LogTemp,Error,TEXT("LD_COMBAT_BUILD_APPLY_NO_CORE contender=%s"),*Prepared.Contender.ToString());
        return;
    }
    const FLDDemoContentSnapshot* Content=Catalog();
    const ELDCommitPhase Phase=Permit.GetPhase();
    const bool bGrowthPhase=Phase==ELDCommitPhase::DeathsAndRewards || Phase==ELDCommitPhase::RespawnsAndWorld
        || Phase==ELDCommitPhase::Transactions;
    FLDCombatEntityRecord* Record=FindRecord(Prepared.Contender);
    ALDCharacter* Actor=nullptr;
    FLDBodyCombatState* Body=Prepared.bAlive ? FindBody(Prepared.Body,&Actor) : nullptr;
    const FLDBodyCombatState* Current=FindEntityBody(Prepared.Contender);
    bool bValid=bGrowthPhase && Mode->ValidateWritePermit(Permit,Phase)==ELDCommitReject::None && Content!=nullptr
        && Prepared.Match==Mode->GetMatchContext() && Record && Record->Kind==ELDVictimKind::Contender
        && Record->Installed.BuildRevision==Prepared.Expected.BuildRevision
        && Record->Installed.InventoryRevision==Prepared.Expected.InventoryRevision
        && Record->SourceRevision==Prepared.ExpectedSourceRevision
        && (Prepared.bAlive
            ? Body && Actor && Actor->GetCharacterMovement() && !Body->bPendingDeath
                && Body->Resources.Revision==Prepared.ExpectedResourceRevision
            : !Current || Current->bPendingDeath);
    TArray<FGuid> PlannedIds;
    for (int32 Index=0; bValid && Index<Prepared.BindingPlan.Num(); ++Index)
    {
        const FLDCombatBindingPlan& Row=Prepared.BindingPlan[Index];
        const FLDCombatTrack* Existing=Record->Tracks.FindByPredicate([&Row](const FLDCombatTrack& Candidate)
        { return Candidate.IssuedCooldownId==Row.IssuedCooldownId; });
        const bool bGear=Row.Source.Kind==ELDCombatSourceKind::Gear;
        bValid=(bGear || Row.Source.Kind==ELDCombatSourceKind::Learned) && Row.IssuedCooldownId.IsValid()
            && bGear==Row.Source.ItemInstance.IsValid() && !PlannedIds.Contains(Row.IssuedCooldownId)
            && (Row.bNewTrack ? Existing==nullptr
                : Existing && Existing->Kind==Row.Source.Kind && Existing->DefinitionId==Row.Source.DefinitionId);
        PlannedIds.Add(Row.IssuedCooldownId);
    }
    FLDBasicTiming Timing;
    FLDObservedEquipmentView PublicSlots;
    float WalkSpeed=0;
    if (bValid && Body)
    {
        ELDCombatPrepareError Code=ELDCombatPrepareError::None;
        FString Reason;
        bValid=LDCombatSourcesDetail::ProjectedBasicTiming(Prepared.Projection,*Content,Timing,Code,Reason)
            && LDCombatSourcesDetail::NativeWalkSpeed(Prepared.Projection.Final.MoveSpeedCmPerSec,WalkSpeed)
            && LDCombatSourcesDetail::PublicEquipment(Prepared.Input,*Content,PublicSlots)
            && Prepared.NewMaxHPP2==Prepared.Projection.Final.MaxHPP2 && Prepared.NewMaxMPP2==Prepared.Projection.Final.MaxMPP2;
    }
    if (!bValid)
    {
        // Prepare and Apply share one permit scope, so any drift is a contract violation: nothing is installed.
        Mode->FaultAuthority(FString::Printf(TEXT("Combat build install for %s lost its permit, versions, body or bindings."),
            *Prepared.Contender.ToString()));
        return;
    }
    Record->Build=Prepared.Input;
    Record->Installed.BuildRevision=Prepared.Input.ResultBuildRevision;
    Record->Installed.InventoryRevision=Prepared.Input.ResultInventoryRevision;
    Record->Projection=Prepared.Projection;
    for (const FLDCombatBindingPlan& Row : Prepared.BindingPlan)
    {
        FLDCombatTrack* Track=Row.bNewTrack ? &Record->Tracks.AddDefaulted_GetRef()
            : Record->Tracks.FindByPredicate([&Row](const FLDCombatTrack& Candidate) { return Candidate.IssuedCooldownId==Row.IssuedCooldownId; });
        if (Row.bNewTrack)
        {
            Track->IssuedCooldownId=Row.IssuedCooldownId;
            Track->Kind=Row.Source.Kind;
            Track->DefinitionId=Row.Source.DefinitionId;
        }
        // Rebinding keeps the absolute ReadyAt: a source change never washes or extends cooldown debt.
        Track->bBound=true;
        Track->BoundInstance=Row.Source.Kind==ELDCombatSourceKind::Gear ? Row.Source.ItemInstance : FGuid();
    }
    for (FLDCombatTrack& Track : Record->Tracks)
    {
        // The innate dash track is never a held source of the build; it stays bound across every build change.
        if (PlannedIds.Contains(Track.IssuedCooldownId) || Track.Kind==ELDCombatSourceKind::Innate) { continue; }
        Track.bBound=false;
        Track.BoundInstance.Invalidate();
    }
    ++Record->SourceRevision;
    if (Body)
    {
        Body->AttributeProjection=Prepared.Projection;
        ++Body->AttributeRevision;
        Body->BasicTiming=Timing;
        Body->Resources.MaxHpP2=Prepared.NewMaxHPP2;
        Body->Resources.HpP2=Prepared.NewHPP2;
        Body->Resources.MaxMpP2=Prepared.NewMaxMPP2;
        Body->Resources.MpP2=Prepared.NewMPP2;
        // The sub-P2 remainder stays an unbooked part of the old points; a full pool banks nothing.
        if (Body->Resources.HpP2>=Body->Resources.MaxHpP2) { Body->HpRegenMicroP2=0; }
        if (Body->Resources.MpP2>=Body->Resources.MaxMpP2) { Body->MpRegenMicroP2=0; }
        ++Body->Resources.Revision;
        if (!Body->PublicFacts.IsSet()) { Body->PublicFacts.Emplace(); }
        Body->PublicFacts->Level=Prepared.Input.Level;
        Body->PublicFacts->Equipment=PublicSlots;
        Actor->GetCharacterMovement()->MaxWalkSpeed=WalkSpeed;
    }
    MarkViewChanged(Prepared.Contender);
}

FLDGearBindingPreview ALDCombatDirector::PreviewGearBinding(const FLDCombatCaller& Caller, FName EquipmentId) const
{
    FLDGearBindingPreview Preview;
    const ALDGameMode* Mode=Core();
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Mode || !Content)
    {
        Preview.Error=TEXT("AuthorityDataUnavailable: the Combat owner has no current core or content.");
        return Preview;
    }
    if (!Caller.Match.IsValid() || Caller.Match!=Mode->GetMatchContext())
    {
        Preview.Error=TEXT("StaleMatch: the caller is not bound to the current match.");
        return Preview;
    }
    const FLDCombatEntityRecord* Record=Caller.Kind==ELDVictimKind::Contender ? FindRecord(Caller.EntityId) : nullptr;
    if (!Record || Record->Kind!=ELDVictimKind::Contender)
    {
        Preview.Error=TEXT("UnknownContender: no persistent Combat record.");
        return Preview;
    }
    const FLDDemoEquipmentDefinition* Definition=nullptr;
    FString LookupError;
    if (EquipmentId.IsNone() || !Content->FindEquipment(EquipmentId.ToString(),Definition,LookupError) || !Definition)
    {
        Preview.Error=FString::Printf(TEXT("SourceUnavailable: %s is not in the approved catalog. %s"),*EquipmentId.ToString(),*LookupError);
        return Preview;
    }
    Preview.bHasActive=Definition->HasActive;
    if (!Definition->HasActive)
    {
        Preview.bValid=true;
        return Preview;
    }
    // Same debt-first choice as Prepare, for one additional copy next to the current holdings.
    TArray<FGuid> Held;
    for (const FLDEquippedItemValue& Item : Record->Build.Equipment) { Held.Add(Item.Instance); }
    const TArray<FGuid> NoneClaimed;
    const int64 NowMs=Mode->GetAuthorityNowMs();
    if (const FLDCombatTrack* Chosen=LDCombatSourcesDetail::ChooseGearTrack(*Record,EquipmentId,NowMs,NoneClaimed,Held))
    {
        Preview.bInherits=Chosen->ReadyAtMs>NowMs;
        Preview.ReadyAtMs=Chosen->ReadyAtMs;
        Preview.bValid=true;
        return Preview;
    }
    int32 Issued=0;
    for (const FLDCombatTrack& Track : Record->Tracks)
    {
        if (Track.Kind==ELDCombatSourceKind::Gear && Track.DefinitionId==EquipmentId) { ++Issued; }
    }
    if (Issued>=Content->Get().Progression.EquipmentSlots)
    {
        Preview.Error=FString::Printf(TEXT("CapacityExceeded: %s already has %d persistent cooldown tracks."),*EquipmentId.ToString(),Issued);
        return Preview;
    }
    Preview.bValid=true;
    return Preview;
}

FLDCombatEntryView ALDCombatDirector::QueryAvailability(const FLDCombatCaller& Caller, ELDCombatEntry Entry) const
{
    FLDCombatEntryView Refused;
    Refused.Entry=Entry;
    const ALDGameMode* Mode=Core();
    if (!Mode || !Catalog()) { Refused.Unavailable=ELDCombatReject::AuthorityDataUnavailable; return Refused; }
    if (!Caller.Match.IsValid() || Caller.Match!=Mode->GetMatchContext()) { Refused.Unavailable=ELDCombatReject::StaleMatch; return Refused; }
    if (!Mode->IsGameplayWriteOpen(Caller.Match)) { Refused.Unavailable=ELDCombatReject::MatchClosed; return Refused; }
    if (LDCombatSourcesDetail::LearnedSlot(Entry)==INDEX_NONE && LDCombatSourcesDetail::GearSlot(Entry)==INDEX_NONE
        && Entry!=ELDCombatEntry::Dash)
    { Refused.Unavailable=ELDCombatReject::InvalidRequest; return Refused; }
    const FLDCombatEntityRecord* Record=Caller.Kind==ELDVictimKind::Contender ? FindRecord(Caller.EntityId) : nullptr;
    if (!Record || Record->Kind!=ELDVictimKind::Contender) { Refused.Unavailable=ELDCombatReject::NotOwner; return Refused; }
    return EntryView(*Record,FindEntityBody(Caller.EntityId),Entry,Mode->GetAuthorityNowMs());
}

bool ALDCombatDirector::BuildOwnerView(const FLDCombatCaller& Caller, FLDCombatOwnerView& Out) const
{
    Out=FLDCombatOwnerView();
    const ALDGameMode* Mode=Core();
    const FLDDemoContentSnapshot* Content=Catalog();
    const FLDCombatEntityRecord* Record=Caller.Kind==ELDVictimKind::Contender ? FindRecord(Caller.EntityId) : nullptr;
    if (!Mode || !Content || !Caller.Match.IsValid() || Caller.Match!=Mode->GetMatchContext()
        || !Record || Record->Kind!=ELDVictimKind::Contender)
    { return false; }
    const FLDBodyCombatState* Body=FindEntityBody(Caller.EntityId);
    const int64 NowMs=Mode->GetAuthorityNowMs();
    const FLDDemoPolicies& Policies=Content->Get().Policies;
    // A body explains its installed projection; a dead contender explains the persistent build of the next life.
    const FLDAttributeProjection& Projection=Body ? Body->AttributeProjection : Record->Projection;
    Out.Generation=Caller.Match.Generation;
    if (Body) { Out.Life=Body->Victim.Life; }
    else if (const auto* LifeRecord=Mode->Lives.Find(Caller.EntityId)) { Out.Life=LifeRecord->Life; }
    Out.Revision=Record->ViewRevision;
    Out.AsOfMs=NowMs;
    Out.bHasBody=Body!=nullptr;
    Out.SourceRevision=Record->SourceRevision;
    Out.NextAttackAtMs=Record->NextAttackAtMs;
    if (Body)
    {
        Out.HPP2=Body->Resources.HpP2;
        Out.MaxHPP2=Body->Resources.MaxHpP2;
        Out.MPP2=Body->Resources.MpP2;
        Out.MaxMPP2=Body->Resources.MaxMpP2;
        Out.ShieldP2=Body->Resources.ShieldP2;
        if (!Body->bPendingDeath)
        {
            const int64 RootedUntilMs=LDTactical::ActiveRootUntilMs(Body->Roots,NowMs);
            if (RootedUntilMs>0)
            {
                FLDCombatStatusView& RootStatus=Out.Statuses.AddDefaulted_GetRef();
                RootStatus.StatusId=static_cast<uint8>(ELDDemoStatusId::Root);
                RootStatus.ExpiresAtMs=RootedUntilMs;
                RootStatus.ControlBlocks=LDCombatSourcesDetail::RootControlBlocks(Policies);
            }
            if (Body->Shield.IsSet() && Body->Resources.ShieldP2>0)
            {
                FLDCombatStatusView& ShieldStatus=Out.Statuses.AddDefaulted_GetRef();
                ShieldStatus.StatusId=static_cast<uint8>(ELDDemoStatusId::Shield);
                ShieldStatus.ExpiresAtMs=Body->Shield->ExpiresAtMs;
                ShieldStatus.AmountP2=Body->Resources.ShieldP2;
            }
            if (Body->Action.IsSet())
            {
                Out.ActionEntry=Body->Action->Entry;
                Out.ActionPhase=ELDCombatPhase::Accepted;
                Out.ActionToken=Body->Action->Token;
                Out.ActionReleaseAtMs=Body->Action->ReleaseAtMs;
            }
            Out.Cues=CueFactsOf(*Body);
            Out.bRooted=LDTactical::RootBlocks(Body->Roots,NowMs,Policies,ELDDemoRootBlock::Move);
            if (Projection.OutOfCombatHPRegen.IsSet())
            {
                const int64 DelayMs=Projection.OutOfCombatHPRegen->DelayMs;
                Out.bOutOfCombatRegenActive=DelayMs>=0 && Body->LastCombatAtMs<=MAX_int64-DelayMs
                    && NowMs>=Body->LastCombatAtMs+DelayMs;
            }
        }
    }
    else
    {
        Out.MaxHPP2=Projection.Final.MaxHPP2;
        Out.MaxMPP2=Projection.Final.MaxMPP2;
    }
    for (const ELDCombatEntry Slot : LDCombatSourcesDetail::OwnerEntries) { Out.Entries.Add(EntryView(*Record,Body,Slot,NowMs)); }
    Out.MoveSpeedCmPerSec=float(LDCombatSourcesDetail::RationalValue(Projection.Final.MoveSpeedCmPerSec));
    Out.AttackRangeCm=float(LDCombatSourcesDetail::RationalValue(Projection.Final.AttackRangeCm));
    for (const FLDAttributeContributions& Row : Projection.Contributions)
    {
        FLDCombatAttributeRowView& RowView=Out.Attributes.AddDefaulted_GetRef();
        RowView.Attribute=static_cast<uint8>(Row.Attribute);
        RowView.Base=Row.Base;
        RowView.Level=Row.LevelGrowth.Flat;
        RowView.Choices=Row.PermanentChoices.Flat;
        RowView.Passives=Row.Passives.Flat;
        RowView.Equipment=Row.Equipment.Flat;
        RowView.Final=Row.Final;
        RowView.bClamped=Row.bClamped;
        if (!LDCombatSourcesDetail::AddRatios(Row.LevelGrowth.AdditiveRatio,Row.PermanentChoices.AdditiveRatio,RowView.PercentBonus)
            || !LDCombatSourcesDetail::AddRatios(RowView.PercentBonus,Row.Passives.AdditiveRatio,RowView.PercentBonus)
            || !LDCombatSourcesDetail::AddRatios(RowView.PercentBonus,Row.Equipment.AdditiveRatio,RowView.PercentBonus))
        {
            UE_LOG(LogTemp,Error,TEXT("LD_COMBAT_OWNER_VIEW_RATIO contender=%s attribute=%u"),
                *Caller.EntityId.ToString(),uint32(Row.Attribute));
            Out=FLDCombatOwnerView();
            return false;
        }
    }
    for (const FLDEquipmentPassiveFamily& Family : Projection.EquipmentPassives) { Out.EquipmentPassives.Add(FName(*Family.Family)); }
    return true;
}

FLDCampResetPrepareResult ALDCombatDirector::PrepareCampReset(const FLDCampResetInput& Input, const FLDGameplayWritePermit& Permit) const
{
    using EPrepare=ELDCombatPrepareError;
    const ALDGameMode* Mode=Core();
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::RespawnsAndWorld)!=ELDCommitReject::None)
    {
        return LDCombatSourcesDetail::CampResetFailure(EPrepare::NotAuthority,
            TEXT("NotAuthority: a camp reset needs the current RespawnsAndWorld permit."));
    }
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { return LDCombatSourcesDetail::CampResetFailure(EPrepare::Catalog,TEXT("Catalog: the approved Demo content is not loaded.")); }
    if (!Input.Body.IsValid() || Input.Body.Victim.Kind!=ELDVictimKind::CampMember || Input.ReturnEpoch==0)
    {
        return LDCombatSourcesDetail::CampResetFailure(EPrepare::InvalidInput,
            TEXT("InvalidInput: a camp reset names one camp member life and a nonzero return epoch."));
    }
    ALDCharacter* Actor=nullptr;
    const FLDBodyCombatState* Body=FindBody(Input.Body,&Actor);
    if (!Body || !Actor)
    {
        return LDCombatSourcesDetail::CampResetFailure(EPrepare::UnknownContender,
            TEXT("UnknownContender: the camp member life is not a registered Combat body."));
    }
    // The same key was already installed: replay without a second apply.
    if (Input.ReturnEpoch<=Body->ResetThroughEpoch)
    {
        FLDCampResetPrepareResult Replayed;
        Replayed.bAlreadyApplied=true;
        return Replayed;
    }
    if (Body->bPendingDeath || Body->Resources.HpP2<=0)
    {
        return LDCombatSourcesDetail::CampResetFailure(EPrepare::InvalidInput,
            TEXT("InvalidInput: a lethal camp member keeps its death instead of a reset."));
    }
    if (Body->Resources.Revision!=Input.ExpectedResourceRevision || Body->StateRevision!=Input.ExpectedStateRevision)
    {
        return LDCombatSourcesDetail::CampResetFailure(EPrepare::StaleBuild,
            TEXT("StaleBuild: the camp member's resources or statuses changed after the reset was read."));
    }
    const double ArrivalCm=LDCombatSourcesDetail::RationalValue(Content->Get().Parameters.CampResetArrivalRadiusCm);
    if (!(ArrivalCm>0) || FVector::DistSquared2D(FootOf(*Actor),Input.HomeFootCm)>ArrivalCm*ArrivalCm)
    {
        return LDCombatSourcesDetail::CampResetFailure(EPrepare::InvalidInput,
            TEXT("InvalidInput: the camp member foot is outside CAMP.ResetArrivalRadiusCm of its home."));
    }
    const FLDDemoMonsterDefinition* Monster=nullptr;
    FString LookupError;
    if (Body->MonsterId.IsNone() || !Content->FindMonster(Body->MonsterId.ToString(),Monster,LookupError) || !Monster
        || Monster->HPP2<=0 || Monster->MaxMPP2<0)
    {
        return LDCombatSourcesDetail::CampResetFailure(EPrepare::Catalog,
            FString::Printf(TEXT("Catalog: monster %s has no executable maxima. %s"),*Body->MonsterId.ToString(),*LookupError));
    }
    // Every temporary state is cleared, so the post-reset maxima are the monster's base maxima.
    FLDPreparedCampReset Prepared;
    Prepared.Input=Input;
    Prepared.Match=Mode->GetMatchContext();
    Prepared.MaxHPP2=Monster->HPP2;
    Prepared.MaxMPP2=Monster->MaxMPP2;
    FLDCampResetPrepareResult Result;
    Result.Value.Emplace(MoveTemp(Prepared));
    return Result;
}

void ALDCombatDirector::ApplyCampReset(FLDPreparedCampReset&& Prepared, const FLDGameplayWritePermit& Permit)
{
    ALDGameMode* Mode=Core();
    if (!Mode)
    {
        UE_LOG(LogTemp,Error,TEXT("LD_COMBAT_CAMP_RESET_NO_CORE member=%s"),*Prepared.Input.Body.Victim.EntityId.ToString());
        return;
    }
    FLDBodyCombatState* Body=FindBody(Prepared.Input.Body);
    if (Mode->ValidateWritePermit(Permit,ELDCommitPhase::RespawnsAndWorld)!=ELDCommitReject::None
        || Prepared.Match!=Mode->GetMatchContext() || !Body || Body->bPendingDeath || Body->Resources.HpP2<=0
        || Body->Victim.Kind!=ELDVictimKind::CampMember
        || Body->Resources.Revision!=Prepared.Input.ExpectedResourceRevision
        || Body->StateRevision!=Prepared.Input.ExpectedStateRevision
        || Prepared.Input.ReturnEpoch<=Body->ResetThroughEpoch || Prepared.MaxHPP2<=0 || Prepared.MaxMPP2<0)
    {
        Mode->FaultAuthority(FString::Printf(TEXT("Combat camp reset of %s lost its permit, life or revisions."),
            *Prepared.Input.Body.Victim.EntityId.ToString()));
        return;
    }
    LDTactical::DispelRoots(Body->Roots);
    Body->Shield.Reset();
    AbandonAction(*Body,ELDCombatReject::Cancelled);
    Body->Resources.ShieldP2=0;
    Body->Resources.MaxHpP2=Prepared.MaxHPP2;
    Body->Resources.HpP2=Prepared.MaxHPP2;
    Body->Resources.MaxMpP2=Prepared.MaxMPP2;
    Body->Resources.MpP2=Prepared.MaxMPP2;
    Body->HpRegenMicroP2=0;
    Body->MpRegenMicroP2=0;
    Body->LastCombatAtMs=0;
    Body->MonsterNextAttackAtMs=0;
    Body->ResetThroughEpoch=Prepared.Input.ReturnEpoch;
    ++Body->Resources.Revision;
    ++Body->StateRevision;
    MarkViewChanged(Prepared.Input.Body.Victim.EntityId);
}
