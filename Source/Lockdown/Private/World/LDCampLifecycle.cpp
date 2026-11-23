#include "World/LDCampLifecycle.h"
#include "Development/LDDemoContentIds.h"
#include "Region/LDMapGeometry.h"
#include "Vision/LDViewTypes.h"

int32 FLDCampRoster::FindCamp(FName CampId) const
{
    return Camps.IndexOfByPredicate([CampId](const FLDCampSpec& Camp) { return Camp.CampId == CampId; });
}

FName FLDCampSpec::MemberSpecies(int32 Slot, TConstArrayView<FName> Draw) const
{
    if (!Members.IsValidIndex(Slot)) { return NAME_None; }
    if (Members[Slot].bTeambuild && !TeambuildMonsterId.IsNone()) { return TeambuildMonsterId; }
    return Draw.IsValidIndex(Members[Slot].Pool) ? Draw[Members[Slot].Pool] : NAME_None;
}

namespace
{
// The pool of Representative's tier in Spec, added once with every approved species of that tier.
int32 FindOrAddPool(const FLDDemoContentDefinitions& Content, const FLDDemoMonsterDefinition& Representative,
    FLDCampSpec& Spec, FString& Error)
{
    const int32 Found = Spec.Pools.IndexOfByPredicate([&](const FLDCampSpeciesPool& P) { return P.Tier == Representative.Tier; });
    if (Found != INDEX_NONE) { return Found; }
    FLDCampSpeciesPool& Pool = Spec.Pools.AddDefaulted_GetRef();
    Pool.Tier = Representative.Tier;
    for (const auto& Species : Content.Monsters)
    {
        if (Species.Tier != Representative.Tier) { continue; }
        if (Species.CountPerCamp != Representative.CountPerCamp
            || Species.RewardXP != Representative.RewardXP)
        {
            Error = TEXT("Species of one camp tier disagree on member count, reward or refresh.");
            return INDEX_NONE;
        }
        Pool.Species.Add(FName(*Species.Id));
        Pool.SpeciesIds.Add(Species.NativeId);
    }
    return Spec.Pools.Num() - 1;
}
}

namespace LDCamp
{
bool BuildRoster(const FLDDemoContentDefinitions& Content, const FLDDemoWorldGeometry& World,
    FLDCampRoster& Out, FString& Error)
{
    FLDCampRoster Candidate;
    TSet<FName> Camps, Members;
    bool bLow = false, bHigh = false;
    TArray<FString> CampIds = Content.World.CampIds;
    if (World.bSceneLayout)
    {
        CampIds.Reset();
        for (const auto& Anchor : World.Anchors)
        { if (Anchor.Kind == ELDDemoWorldAnchorKind::Camp) { CampIds.Add(Anchor.Id); } }
    }
    for (const FString& CampText : CampIds)
    {
        const FName CampId(*CampText);
        const auto* Anchor = World.Anchors.FindByPredicate(
            [&CampText](const FLDDemoWorldAnchor& Row) { return Row.Id == CampText; });
        const FString& Template = World.bSceneLayout && Anchor && !Anchor->PrototypeId.IsEmpty() ? Anchor->PrototypeId : CampText;
        const auto* Placement = Content.Camps.FindByPredicate(
            [&Template](const FLDDemoCampPlacement& Row) { return Row.AnchorId == Template; });
        const auto* Monster = Placement ? Content.Monsters.FindByPredicate(
            [Placement](const FLDDemoMonsterDefinition& Row) { return Row.Id == Placement->MonsterId; }) : nullptr;
        if (CampId.IsNone() || Camps.Contains(CampId) || !Placement || !Monster || !Anchor
            || Anchor->Kind != ELDDemoWorldAnchorKind::Camp || Anchor->SurfaceId.IsEmpty()
            || Monster->NativeId == 0 || Monster->CountPerCamp <= 0
            || Anchor->Members.IsEmpty()
            || (Anchor->Members.Num() != Monster->CountPerCamp
                && !(World.bSceneLayout && Anchor->Members.ContainsByPredicate([](const auto& Home) { return !Home.PrototypeId.IsEmpty(); }))))
        {
            Error = FString::Printf(TEXT("Camp %s has no unique approved placement, monster, world anchor and member homes."), *CampText);
            return false;
        }
        FLDCampSpec Spec;
        Spec.CampId = CampId;
        Spec.DefinitionId = FName(*Template);
        Spec.bAuthoredMembers = World.bSceneLayout && Anchor->Members.ContainsByPredicate([](const auto& Home) { return !Home.PrototypeId.IsEmpty(); });
        Spec.SurfaceId = FName(*Anchor->SurfaceId);
        Spec.MonsterId = FName(*Monster->Id);
        Spec.CreatureId = Monster->NativeId;
        if (FindOrAddPool(Content, *Monster, Spec, Error) != 0) { return false; }
        Spec.Tier = Monster->Tier;
        if (!Anchor->PositionCm.ToVector(Spec.AnchorCm, Error)) { return false; }
        for (const auto& Home : Anchor->Members)
        {
            FLDCampMemberSpec Member;
            Member.MemberId = FName(*Home.Id);
            const FString MemberTemplate=World.bSceneLayout && !Home.PrototypeId.IsEmpty() ? Home.PrototypeId : Template;
            const auto* MemberPlacement=Content.Camps.FindByPredicate([&](const auto& Row) { return Row.AnchorId==MemberTemplate; });
            const auto* MemberMonster=MemberPlacement ? Content.Monsters.FindByPredicate([&](const auto& Row) { return Row.Id==MemberPlacement->MonsterId; }) : nullptr;
            if (!MemberMonster || !MemberMonster->NativeId)
            { Error=TEXT("Camp member has no approved monster prototype: ")+Home.Id; return false; }
            Member.DefinitionId=FName(*MemberTemplate);
            // The member prototype only names a tier representative; its species comes from that tier's draw.
            Member.Pool=FindOrAddPool(Content,*MemberMonster,Spec,Error);
            if (Member.Pool==INDEX_NONE) { return false; }
            if (Member.MemberId.IsNone() || Members.Contains(Member.MemberId))
            {
                Error = FString::Printf(TEXT("Camp %s has a missing or repeated member home."), *CampText);
                return false;
            }
            if (!Home.PositionCm.ToVector(Member.HomeCm, Error)) { return false; }
            Members.Add(Member.MemberId);
            Spec.Members.Add(MoveTemp(Member));
        }
        // Spec §11.2: every High, Elite and Ancient camp reserves one teambuild member at its anchor.
        if (!Spec.bAuthoredMembers && Spec.Tier != ELDDemoCampTier::Low)
        {
            // A High camp's teambuild member is of the species each spawn selects; Elite and Ancient camps bring one fixed
            // High-tier species.
            const FLDDemoMonsterDefinition* High = Spec.Tier == ELDDemoCampTier::High ? nullptr
                : Content.Monsters.FindByPredicate([](const FLDDemoMonsterDefinition& M) { return M.Tier == ELDDemoCampTier::High; });
            if (!High && Spec.Tier != ELDDemoCampTier::High)
            {
                Error = FString::Printf(TEXT("Camp %s has no approved High-tier member for its teambuild slot."), *CampText);
                return false;
            }
            FLDCampMemberSpec Team;
            Team.MemberId = FName(*(CampText + TEXT("_teambuild")));
            Team.HomeCm = Spec.AnchorCm;
            Team.bTeambuild = true;
            Team.DefinitionId = Spec.DefinitionId;
            if (Members.Contains(Team.MemberId))
            {
                Error = FString::Printf(TEXT("Camp %s has a member home named like its teambuild slot."), *CampText);
                return false;
            }
            Members.Add(Team.MemberId);
            Spec.Members.Add(MoveTemp(Team));
            if (High)
            {
                Spec.TeambuildMonsterId = FName(*High->Id);
                Spec.TeambuildCreatureId = High->NativeId;
            }
        }
        bLow |= Spec.Tier == ELDDemoCampTier::Low;
        bHigh |= Spec.Tier == ELDDemoCampTier::High;
        Camps.Add(CampId);
        Candidate.Camps.Add(MoveTemp(Spec));
    }
    if (Candidate.Camps.IsEmpty() || (!World.bSceneLayout && (Content.Camps.Num() != Candidate.Camps.Num() || !bLow || !bHigh)))
    {
        Error = TEXT("The Demo requires every placed camp in the roster, covering both tiers.");
        return false;
    }
    Out = MoveTemp(Candidate);
    Error.Reset();
    return true;
}
}

bool FLDCampMatchState::Begin(const FLDMatchContext& InMatch, const TSharedRef<const FLDCampRoster>& InRoster, FString& Error)
{
    if (IsActive() || !InMatch.IsValid() || InRoster->Camps.IsEmpty())
    {
        Error = TEXT("Camp bookkeeping needs a new valid match, an approved roster and no active previous match.");
        return false;
    }
    TArray<FLDCampGroupState> NewCamps;
    TMap<FGuid, FIntPoint> NewIndex;
    NewCamps.SetNum(InRoster->Camps.Num());
    for (int32 Camp = 0; Camp < InRoster->Camps.Num(); ++Camp)
    {
        const int32 Count = InRoster->Camps[Camp].Members.Num();
        if (Count <= 0)
        {
            Error = TEXT("An approved camp has no member homes.");
            return false;
        }
        NewCamps[Camp].Members.SetNum(Count);
        for (int32 Slot = 0; Slot < Count; ++Slot)
        {
            const FGuid Id = FGuid::NewGuid();
            if (!Id.IsValid() || NewIndex.Contains(Id))
            {
                Error = TEXT("Camp member entity identity allocation failed.");
                return false;
            }
            NewCamps[Camp].Members[Slot].EntityId = Id;
            NewIndex.Add(Id, FIntPoint(Camp, Slot));
        }
    }
    Match = InMatch;
    Roster = InRoster;
    Camps = MoveTemp(NewCamps);
    MemberIndex = MoveTemp(NewIndex);
    Error.Reset();
    return true;
}

void FLDCampMatchState::End()
{
    Match = {};
    Roster.Reset();
    Camps.Reset();
    MemberIndex.Reset();
}

const FLDCampMemberState* FLDCampMatchState::FindMember(const FGuid& EntityId, int32* OutCamp, int32* OutSlot) const
{
    const FIntPoint* At = MemberIndex.Find(EntityId);
    if (!At || !Camps.IsValidIndex(At->X) || !Camps[At->X].Members.IsValidIndex(At->Y)) { return nullptr; }
    if (OutCamp) { *OutCamp = At->X; }
    if (OutSlot) { *OutSlot = At->Y; }
    return &Camps[At->X].Members[At->Y];
}

const FLDCampMemberState* FLDCampMatchState::FindLiving(const FLDCombatBodyRef& Body, int32* OutCamp, int32* OutSlot) const
{
    if (!Body.IsValid() || Body.Victim.Kind != ELDVictimKind::CampMember) { return nullptr; }
    const FLDCampMemberState* Member = FindMember(Body.Victim.EntityId, OutCamp, OutSlot);
    return Member && Member->bAlive && Member->Body == Body ? Member : nullptr;
}

FLDCampMemberState* FLDCampMatchState::MutableLiving(const FLDCombatBodyRef& Body, FString& Error)
{
    int32 Camp = INDEX_NONE, Slot = INDEX_NONE;
    if (!IsActive() || !FindLiving(Body, &Camp, &Slot))
    {
        Error = TEXT("The body is not a current living camp member of this match.");
        return nullptr;
    }
    return &Camps[Camp].Members[Slot];
}

TArray<FLDCampPublicView> FLDCampMatchState::BuildPublicViews() const
{
    TArray<FLDCampPublicView> Out;
    if (!Roster) { return Out; }
    for (const auto& Spec : Roster->Camps)
    {
        Out.Add({Spec.CampId, Spec.MonsterId, Spec.Tier, FVector2D(Spec.AnchorCm.X, Spec.AnchorCm.Y)});
    }
    return Out;
}

bool FLDCampMatchState::SelectSpecies(int32 Camp,TConstArrayView<int32> Choices,TArray<FName>& Out,FString& Error)
{
    auto* Group=IsActive() && Camps.IsValidIndex(Camp)?&Camps[Camp]:nullptr;
    if(!Group || Group->Members.ContainsByPredicate([](const auto& M){return M.bAlive;})
        || (Group->Generation!=0 && !Group->bRespawnPending))
    {Error=TEXT("Species can only be selected for a whole-group new life.");return false;}
    if(Group->PendingSpecies.IsEmpty())
    {
        const auto& Pools=Roster->Camps[Camp].Pools;
        if(Choices.Num()!=Pools.Num()){Error=TEXT("A species draw needs one choice per camp tier pool.");return false;}
        TArray<FName> Draw;
        for(int32 Pool=0;Pool<Pools.Num();++Pool)
        {
            if(!Pools[Pool].Species.IsValidIndex(Choices[Pool])){Error=TEXT("Species draw is outside the camp tier pool.");return false;}
            Draw.Add(Pools[Pool].Species[Choices[Pool]]);
        }
        Group->PendingSpecies=MoveTemp(Draw);
    }
    Out=Group->PendingSpecies;Error.Reset();return true;
}

bool FLDCampMatchState::CanSpawnInitial(int32 Camp, FString& Error) const
{
    const FLDCampGroupState* Group = IsActive() ? FindCamp(Camp) : nullptr;
    if (!Group || Group->Generation != 0 || Group->bRespawnPending
        || Group->Members.ContainsByPredicate([](const FLDCampMemberState& M) { return M.Life != 0 || M.bAlive; }))
    {
        Error = TEXT("Initial camp spawn requires an unspawned camp of the active match.");
        return false;
    }
    Error.Reset();
    return true;
}

bool FLDCampMatchState::IsRespawnDue(int32 Camp, int64 NowMs) const
{
    const FLDCampGroupState* Group = IsActive() ? FindCamp(Camp) : nullptr;
    return Group && Group->bRespawnPending && NowMs >= Group->RespawnDueMs;
}

#if WITH_DEV_AUTOMATION_TESTS
int32 FLDCampMatchState::DevRefreshNow(int64 NowMs)
{
    int32 Waiting = 0;
    for (FLDCampGroupState& Group : Camps)
    {
        if (IsActive() && Group.Generation != 0 && !Group.Members.ContainsByPredicate([](const auto& M) { return M.bAlive; }))
        {
            Group.RespawnDueMs = Group.bRespawnPending ? FMath::Min(Group.RespawnDueMs, NowMs) : NowMs;
            Group.bRespawnPending = true;
            ++Waiting;
        }
    }
    return Waiting;
}
#endif

void FLDCampMatchState::QueueFieldReset(int32 Camp)
{
    FLDCampGroupState& Group = Camps[Camp];
    for (FLDCampMemberState& Member : Group.Members)
    {
        Member.bAlive = false;
        Member.Body = {};
        Member.Brain = {};
        Member.HostileSources.Reset();
    }
    Group.PendingSpecies.Reset();
    Group.bRespawnPending = true;
    Group.RespawnDueMs = 0;
}

int64 FLDCampMatchState::NextRespawnDueMs() const
{
    int64 Due = MAX_int64;
    if (!IsActive()) { return Due; }
    for (const FLDCampGroupState& Group : Camps)
    {
        if (Group.bRespawnPending) { Due = FMath::Min(Due, Group.RespawnDueMs); }
    }
    return Due;
}

bool FLDCampMatchState::CommitSpawn(int32 Camp, TArrayView<const FLDCombatBodyRef> Bodies, double AtSeconds, FString& Error)
{
    FLDCampGroupState* Group = IsActive() && Camps.IsValidIndex(Camp) ? &Camps[Camp] : nullptr;
    // A spawn without the teambuild affix leaves the reserved last slot empty.
    const bool bTeambuildSlot = Group && !Roster->Camps[Camp].Members.IsEmpty() && Roster->Camps[Camp].Members.Last().bTeambuild;
    if (!Group || (Bodies.Num() != Group->Members.Num() && !(bTeambuildSlot && Bodies.Num() == Group->Members.Num() - 1))
        || !FMath::IsFinite(AtSeconds) || AtSeconds < 0)
    {
        Error = TEXT("Camp spawn commit needs one current body per member (the teambuild slot only with its affix) at a valid authority time.");
        return false;
    }
    if(Group->PendingSpecies.IsEmpty() && Roster->Camps[Camp].Pools.ContainsByPredicate([](const auto& P){return P.Species.Num()>1;}))
    {Error=TEXT("A multi-species camp cannot spawn without a committed pending draw.");return false;}
    const bool bInitial = Group->Generation == 0 && !Group->bRespawnPending;
    const bool bAllDead = !Group->Members.ContainsByPredicate([](const FLDCampMemberState& M) { return M.bAlive; });
    if (!bAllDead || (!bInitial && !Group->bRespawnPending) || Group->Generation == MAX_uint32)
    {
        Error = TEXT("Only an unspawned or fully cleared camp with a pending refresh can commit a whole-group spawn.");
        return false;
    }
    TSet<FGuid> BodyIds;
    for (int32 Slot = 0; Slot < Bodies.Num(); ++Slot)
    {
        const FLDCombatBodyRef& Body = Bodies[Slot];
        const FLDCampMemberState& Member = Group->Members[Slot];
        if (!Body.IsValid() || Body.Victim.Kind != ELDVictimKind::CampMember || Body.Victim.EntityId != Member.EntityId
            || Member.Life == MAX_uint32 || Body.Victim.Life != Member.Life + 1 || BodyIds.Contains(Body.BodyIdentity))
        {
            Error = TEXT("Committed camp bodies must be the next core life of each slot with distinct physical bodies.");
            return false;
        }
        BodyIds.Add(Body.BodyIdentity);
    }
    for (int32 Slot = 0; Slot < Bodies.Num(); ++Slot)
    {
        FLDCampMemberState& Member = Group->Members[Slot];
        Member.Life = Bodies[Slot].Victim.Life;
        Member.Body = Bodies[Slot];
        Member.bAlive = true;
        Member.Brain = {};
        Member.AggroFloorSeconds = AtSeconds;
        Member.HostileSources.Reset();
    }
    // A camp whose every pool has one species may commit without a draw: each pool's only species is the draw.
    const auto& Pools=Roster->Camps[Camp].Pools;
    Group->SelectedSpecies.Reset();
    Group->CreatureIds.Reset();
    for(int32 Pool=0;Pool<Pools.Num();++Pool)
    {
        const FName Species=Group->PendingSpecies.IsEmpty()?Pools[Pool].Species[0]:Group->PendingSpecies[Pool];
        Group->SelectedSpecies.Add(Species);
        Group->CreatureIds.Add(Pools[Pool].SpeciesIds[Pools[Pool].Species.IndexOfByKey(Species)]);
    }
    Group->PendingSpecies.Reset();
    ++Group->Generation;
    Group->bRespawnPending = false;
    Group->ClearedAtMs = Group->RespawnDueMs = 0;
    Error.Reset();
    return true;
}

ELDCampDeathResult FLDCampMatchState::RecordDeath(const FLDDeathKey& Death, int64 OccurredAtMs, FString& Error)
{
    int32 Camp = INDEX_NONE, Slot = INDEX_NONE;
    const FLDCampMemberState* Found = IsActive() && Death.IsValid() && Death.LedgerMatch == Match.LedgerMatch
        && Death.Victim.Kind == ELDVictimKind::CampMember ? FindMember(Death.Victim.EntityId, &Camp, &Slot) : nullptr;
    if (!Found || Death.Victim.Life > Found->Life || OccurredAtMs < 0)
    {
        Error = TEXT("Camp death does not name a committed life of this match.");
        return ELDCampDeathResult::Rejected;
    }
    // Every earlier life ended before the whole-group refresh that created the current one.
    if (Death.Victim.Life < Found->Life || !Found->bAlive)
    {
        Error.Reset();
        return ELDCampDeathResult::Duplicate;
    }
    FLDCampGroupState& Group = Camps[Camp];
    bool bLastLiving = true;
    for (int32 Other = 0; Other < Group.Members.Num(); ++Other)
    {
        bLastLiving &= Other == Slot || !Group.Members[Other].bAlive;
    }
    FLDCampMemberState& Member = Group.Members[Slot];
    Member.bAlive = false;
    Member.Body = {};
    Member.Brain = {};
    Member.HostileSources.Reset();
    if (bLastLiving)
    {
        Group.bRespawnPending = false;
        Group.ClearedAtMs = OccurredAtMs;
        Group.RespawnDueMs = 0;
    }
    Error.Reset();
    return bLastLiving ? ELDCampDeathResult::CampCleared : ELDCampDeathResult::MemberDead;
}

ELDCampContactResult FLDCampMatchState::ApplyContact(const FLDCampContact& Contact, FString& Error)
{
    Error.Reset();
    if (!IsActive() || !FMath::IsFinite(Contact.OccurredAtSeconds) || Contact.OccurredAtSeconds < 0)
    {
        Error = TEXT("Hostile contact has no active camp match or a valid authority time.");
        return ELDCampContactResult::Rejected;
    }
    if (Contact.Victim.Victim.Kind != ELDVictimKind::CampMember) { return ELDCampContactResult::NotCampMember; }
    int32 Camp = INDEX_NONE;
    if (!FindLiving(Contact.Victim, &Camp)) { return ELDCampContactResult::StaleVictim; }
    const FLDCombatBodyRef& Source = Contact.Source;
    if (!Source.IsValid() || Source.Victim.Kind != ELDVictimKind::Contender) { return ELDCampContactResult::NoContenderSource; }
    // Plan every member first so a refused contact leaves the camp untouched.
    struct FChange { int32 Slot; int32 Replace; bool bAdd; };
    TArray<FChange, TInlineAllocator<2>> Changes;
    bool bFresh = false;
    FLDCampGroupState& Group = Camps[Camp];
    for (int32 Slot = 0; Slot < Group.Members.Num(); ++Slot)
    {
        const FLDCampMemberState& Member = Group.Members[Slot];
        if (!Member.bAlive || Member.Brain.Phase == ELDCampBrainPhase::Returning) { continue; }
        if (Contact.OccurredAtSeconds <= Member.AggroFloorSeconds) { continue; }
        bFresh = true;
        int32 Replace = INDEX_NONE;
        bool bAdd = true;
        for (int32 Index = 0; Index < Member.HostileSources.Num(); ++Index)
        {
            const FLDCombatBodyRef& Known = Member.HostileSources[Index];
            if (Known.Victim.EntityId != Source.Victim.EntityId) { continue; }
            if (Known.Victim.Life == Source.Victim.Life && Known.BodyIdentity != Source.BodyIdentity)
            {
                Error = TEXT("Two physical bodies were reported for one hostile contender life.");
                return ELDCampContactResult::Rejected;
            }
            // Contender lives are monotonic: an earlier life can never become a current target again.
            if (Known.Victim.Life >= Source.Victim.Life) { bAdd = false; }
            else { Replace = Index; }
        }
        if (bAdd && Replace == INDEX_NONE && Member.HostileSources.Num() >= MaxHostileSources)
        {
            Error = TEXT("Camp hostile sources exceed the contender roster bound.");
            return ELDCampContactResult::Rejected;
        }
        if (bAdd || Member.Brain.Phase == ELDCampBrainPhase::Idle) { Changes.Add({Slot, Replace, bAdd}); }
    }
    for (const FChange& Change : Changes)
    {
        FLDCampMemberState& Member = Group.Members[Change.Slot];
        if (Change.bAdd)
        {
            if (Change.Replace != INDEX_NONE) { Member.HostileSources[Change.Replace] = Source; }
            else { Member.HostileSources.Add(Source); }
        }
        if (Member.Brain.Phase == ELDCampBrainPhase::Idle)
        {
            Member.Brain.Phase = ELDCampBrainPhase::Engaged;
            Member.Brain.LostTargetSinceSeconds.Reset();
        }
    }
    return !Changes.IsEmpty() ? ELDCampContactResult::Retaliating
        : bFresh ? ELDCampContactResult::Unchanged : ELDCampContactResult::StaleEvent;
}

bool FLDCampMatchState::ApplyDecision(const FLDCombatBodyRef& Body, const FLDCampDecision& Decision, bool& bBeganReturn, FString& Error)
{
    bBeganReturn = false;
    FLDCampMemberState* Member = MutableLiving(Body, Error);
    if (!Member) { return false; }
    const ELDCampBrainPhase From = Member->Brain.Phase, To = Decision.NextBrain.Phase;
    // Engagement starts only from a consumed hostile contact and ends only through a reset.
    const bool bAllowed = From == To || (From != ELDCampBrainPhase::Returning && To == ELDCampBrainPhase::Returning);
    if (!bAllowed || (To != ELDCampBrainPhase::Engaged && Decision.NextBrain.LostTargetSinceSeconds.IsSet()))
    {
        Error = TEXT("Camp decision attempted a transition reserved for contacts or resets.");
        return false;
    }
    if (From != ELDCampBrainPhase::Returning && To == ELDCampBrainPhase::Returning)
    {
        if (Member->ReturnEpoch == MAX_uint64)
        {
            Error = TEXT("Camp return epoch exhausted.");
            return false;
        }
        ++Member->ReturnEpoch;
        bBeganReturn = true;
    }
    Member->Brain = Decision.NextBrain;
    Error.Reset();
    return true;
}

bool FLDCampMatchState::ReadResetKey(const FLDCombatBodyRef& Body, FLDCampResetKey& Out, FString& Error) const
{
    const FLDCampMemberState* Member = IsActive() ? FindLiving(Body) : nullptr;
    if (!Member || Member->Brain.Phase != ELDCampBrainPhase::Returning || Member->ReturnEpoch <= Member->ResetThroughEpoch)
    {
        Error = TEXT("Only a living member in an unreset Returning cycle has a camp reset key.");
        return false;
    }
    Out = {Match.LedgerMatch, Body.Victim, Member->ReturnEpoch};
    Error.Reset();
    return true;
}

bool FLDCampMatchState::CompleteReset(const FLDCampResetKey& Key, double AtSeconds, FString& Error)
{
    int32 Camp = INDEX_NONE, Slot = INDEX_NONE;
    const FLDCampMemberState* Found = IsActive() && Key.IsValid() && Key.LedgerMatch == Match.LedgerMatch
        && Key.Victim.Kind == ELDVictimKind::CampMember ? FindMember(Key.Victim.EntityId, &Camp, &Slot) : nullptr;
    if (!Found || !Found->bAlive || Found->Life != Key.Victim.Life || Found->Brain.Phase != ELDCampBrainPhase::Returning
        || Key.ReturnEpoch != Found->ReturnEpoch || Found->ResetThroughEpoch >= Key.ReturnEpoch
        || !FMath::IsFinite(AtSeconds) || AtSeconds < Found->AggroFloorSeconds)
    {
        Error = TEXT("Camp reset does not match the member's current unreset Returning cycle.");
        return false;
    }
    FLDCampMemberState& Member = Camps[Camp].Members[Slot];
    Member.Brain = {};
    Member.HostileSources.Reset();
    Member.ResetThroughEpoch = Key.ReturnEpoch;
    Member.AggroFloorSeconds = AtSeconds;
    Error.Reset();
    return true;
}

void FLDCampMatchState::ForgetTargetsForRest(double NowSeconds)
{
    for (FLDCampGroupState& Group : Camps)
    {
        for (FLDCampMemberState& Member : Group.Members)
        {
            if (!Member.bAlive) { continue; }
            Member.Brain.LostTargetSinceSeconds.Reset();
            Member.HostileSources.Reset();
            Member.AggroFloorSeconds = FMath::Max(Member.AggroFloorSeconds, NowSeconds);
        }
    }
}

bool FLDCampMatchState::NextDirectSequence(const FLDCombatBodyRef& Body, uint64& Out, FString& Error)
{
    FLDCampMemberState* Member = MutableLiving(Body, Error);
    if (!Member) { return false; }
    if (Member->LastDirectSequence == MAX_uint64)
    {
        Error = TEXT("Camp member action sequence exhausted.");
        return false;
    }
    Out = ++Member->LastDirectSequence;
    return true;
}

bool FLDCampMatchState::AppendObservedFacts(const FLDCombatBodyRef& Body, FLDObservedPublicFacts& InOut, FString& Error) const
{
    int32 Camp = INDEX_NONE, Slot = INDEX_NONE;
    const FLDCampMemberState* Member = IsActive() ? FindLiving(Body, &Camp, &Slot) : nullptr;
    if (!Member)
    {
        Error = TEXT("Observed camp facts require a current living camp member.");
        return false;
    }
    // An Elite or Ancient teambuild member wears its fixed High species; every other member its tier pool's draw.
    const FLDCampSpec& Spec = Roster->Camps[Camp];
    const FLDCampMemberSpec& Home = Spec.Members[Slot];
    const uint8 CreatureId = Home.bTeambuild && Spec.TeambuildCreatureId != 0 ? Spec.TeambuildCreatureId
        : Camps[Camp].CreatureIds.IsValidIndex(Home.Pool) ? Camps[Camp].CreatureIds[Home.Pool] : 0;
    if (CreatureId == 0)
    {
        Error = TEXT("A living camp member has no committed species for its tier pool.");
        return false;
    }
    const uint8 Returning = uint8(ELDDemoStatusId::CampReturning);
    const bool bListed = InOut.Statuses.ContainsByPredicate([Returning](const FLDObservedStatus& S) { return S.StatusId == Returning; });
    const bool bReturning = Member->Brain.Phase == ELDCampBrainPhase::Returning;
    if ((InOut.CreatureId != 0 && InOut.CreatureId != CreatureId) || bListed
        || (bReturning && InOut.Statuses.Num() >= LDObservation::MaxStatuses))
    {
        Error = TEXT("Observed camp facts conflict with another producer or exceed the status bound.");
        return false;
    }
    InOut.CreatureId = CreatureId;
    if (bReturning) { InOut.Statuses.Add({Returning, 1, 0}); }
    Error.Reset();
    return true;
}
