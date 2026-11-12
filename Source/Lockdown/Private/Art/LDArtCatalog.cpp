#include "Art/LDArtCatalog.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Development/LDDemoContentIds.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"

namespace
{
    const TCHAR* const ArtAnimIds[] = { TEXT("Idle"), TEXT("Run"), TEXT("Attack"), TEXT("Cast"), TEXT("Hit"), TEXT("Death"), TEXT("Spawn") };
    static_assert(UE_ARRAY_COUNT(ArtAnimIds) == LDArt::AnimCount, "Every presentation state needs exactly one stable id.");
    const TCHAR* const ArtZoneIds[] = { TEXT("outside"), TEXT("wall_base"), TEXT("capture"), TEXT("blocker"), TEXT("ground") };
    static_assert(UE_ARRAY_COUNT(ArtZoneIds) == int32(ELDArtZone::Count), "Every placement zone needs exactly one stable id.");
    // A flinch reads as a hit only if it ends before the next blow of a fast exchange.
    constexpr float ArtHitMaxSec = .3f;

    bool ArtFail(FString& Error, const FString& Message) { Error = Message; return false; }

    bool ArtUsableTransform(const FTransform& Transform)
    {
        return !Transform.ContainsNaN() && Transform.IsRotationNormalized() && Transform.GetScale3D().GetMin() > UE_KINDA_SMALL_NUMBER;
    }

    bool ArtResolveClip(const FLDArtCharacter& Body, int32 Index, const USkeleton& Skeleton, FString& Error)
    {
        const FLDArtAnimation& Clip = Body.Animations[Index];
        const ELDArtAnim Expected = ELDArtAnim(Index);
        const FString Label = FString::Printf(TEXT("Character %s %s clip"), *Body.Id, *LDArt::AnimId(Expected));
        if (Clip.State != Expected)
        { return ArtFail(Error, FString::Printf(TEXT("Character %s clip %d is %s, expected %s."), *Body.Id, Index, *LDArt::AnimId(Clip.State), *LDArt::AnimId(Expected))); }
        if (!Clip.Sequence) { return ArtFail(Error, Label + TEXT(" has no animation sequence.")); }
        if (Clip.Sequence->GetSkeleton() != &Skeleton) { return ArtFail(Error, Label + TEXT(" does not target the mesh skeleton: ") + Clip.Sequence->GetPathName()); }
        if (Clip.Sequence->bEnableRootMotion) { return ArtFail(Error, Label + TEXT(" uses root motion; bodies are moved by the capsule only.")); }
        const float Length = Clip.Sequence->GetPlayLength();
        if (!(Length > .05f && Length < 30.f)) { return ArtFail(Error, FString::Printf(TEXT("%s has no usable length (%.3f s)."), *Label, Length)); }
        if (Clip.bLoop != LDArt::IsLoopingAnim(Expected))
        { return ArtFail(Error, Label + (Clip.bLoop ? TEXT(" must play once.") : TEXT(" must loop."))); }
        if (!(Clip.PlayRate >= .1f && Clip.PlayRate <= 10.f)) { return ArtFail(Error, FString::Printf(TEXT("%s has an unusable play rate %.3f."), *Label, Clip.PlayRate)); }
        if (LDArt::HasImpact(Expected) ? !(Clip.ImpactSec > 0.f && Clip.ImpactSec < Length) : Clip.ImpactSec != 0.f)
        { return ArtFail(Error, FString::Printf(TEXT("%s impact time %.3f s is not inside a clip that has one (length %.3f s)."), *Label, Clip.ImpactSec, Length)); }
        if (Expected == ELDArtAnim::Hit && Length / Clip.PlayRate > ArtHitMaxSec + 1e-3f)
        { return ArtFail(Error, FString::Printf(TEXT("%s lasts %.3f s at its rate; a hit must end within %.1f s."), *Label, Length / Clip.PlayRate, ArtHitMaxSec)); }
        if (Clip.SourceClip.IsEmpty()) { return ArtFail(Error, Label + TEXT(" does not name its source clip.")); }
        return true;
    }

    template<typename TSlot>
    bool ArtResolveSlots(const TArray<TSlot>& Slots, const TArray<FName>& TintSlots, const FString& Label, FString& Error)
    {
        if (Slots.IsEmpty()) { return ArtFail(Error, Label + TEXT(" mesh has no material slots.")); }
        for (const TSlot& Slot : Slots)
        {
            if (!Slot.MaterialInterface) { return ArtFail(Error, Label + TEXT(" material slot has no material: ") + Slot.MaterialSlotName.ToString()); }
        }
        for (const FName Tint : TintSlots)
        {
            if (!Slots.ContainsByPredicate([Tint](const TSlot& Slot) { return Slot.MaterialSlotName == Tint; }))
            { return ArtFail(Error, Label + TEXT(" tint slot is not a mesh material slot: ") + Tint.ToString()); }
        }
        return true;
    }

    // A worn part: exactly one mesh; a rigid part hangs from a body socket or bone, a skinned part shares the body
    // skeleton so it can follow the body pose.
    bool ArtResolveAttachment(const FLDArtCharacter& Body, const USkeleton& Skeleton, const FLDArtAttachment& Part, FString& Error)
    {
        const FString Label = FString::Printf(TEXT("Character %s part %s"), *Body.Id, *Part.Id);
        if (!LDArt::IsValidId(Part.Id)) { return ArtFail(Error, Label + TEXT(" needs a lowercase id.")); }
        if (!Part.StaticMesh == !Part.SkinnedMesh) { return ArtFail(Error, Label + TEXT(" needs exactly one static or skinned mesh.")); }
        if (Part.StaticMesh)
        {
            if (Part.Socket.IsNone() || (!Body.Mesh->FindSocket(Part.Socket) && Body.Mesh->GetRefSkeleton().FindBoneIndex(Part.Socket) == INDEX_NONE))
            { return ArtFail(Error, Label + TEXT(" hangs from no body socket or bone: ") + Part.Socket.ToString()); }
            return ArtResolveSlots(Part.StaticMesh->GetStaticMaterials(), Part.TintSlots, Label, Error);
        }
        if (!Part.Socket.IsNone()) { return ArtFail(Error, Label + TEXT(" is skinned and follows the body pose; it names no socket.")); }
        if (Part.SkinnedMesh->GetSkeleton() != &Skeleton) { return ArtFail(Error, Label + TEXT(" does not share the body skeleton.")); }
        return ArtResolveSlots(Part.SkinnedMesh->GetMaterials(), Part.TintSlots, Label, Error);
    }

    bool ArtResolveCharacter(const FLDArtCharacter& Body, FString& Error)
    {
        const FString Label = TEXT("Character ") + Body.Id;
        if (!Body.Mesh) { return ArtFail(Error, Label + TEXT(" has no skeletal mesh.")); }
        for (int32 LOD=0; LOD<Body.Mesh->GetLODNum(); ++LOD)
        {
            if (!Body.Mesh->GetLODInfo(LOD)->bAllowCPUAccess)
            { return ArtFail(Error, Label + TEXT(" needs CPU mesh access for model picking; rerun ImportDemoArt.ps1 -Mode Import.")); }
        }
        const USkeleton* Skeleton = Body.Mesh->GetSkeleton();
        if (!Skeleton) { return ArtFail(Error, Label + TEXT(" mesh has no skeleton: ") + Body.Mesh->GetPathName()); }
        if (!ArtUsableTransform(Body.MeshTransform)) { return ArtFail(Error, Label + TEXT(" has an unusable mesh transform.")); }
        if (!(Body.HeightCm >= 120.f && Body.HeightCm <= 260.f))
        { return ArtFail(Error, FString::Printf(TEXT("%s displayed height %.1f cm is not an adult body."), *Label, Body.HeightCm)); }
        if (!(Body.RunSpeedCmPerSec >= 50.f && Body.RunSpeedCmPerSec <= 2000.f))
        { return ArtFail(Error, FString::Printf(TEXT("%s run reference speed %.1f cm/s is unusable."), *Label, Body.RunSpeedCmPerSec)); }
        if (Body.HandBone.IsNone() || Body.Mesh->GetRefSkeleton().FindBoneIndex(Body.HandBone) == INDEX_NONE)
        { return ArtFail(Error, Label + TEXT(" hand bone is not in the mesh skeleton: ") + Body.HandBone.ToString()); }
        const TArray<FSkeletalMaterial>& Materials = Body.Mesh->GetMaterials();
        if (Materials.IsEmpty()) { return ArtFail(Error, Label + TEXT(" mesh has no material slots.")); }
        for (const FSkeletalMaterial& Slot : Materials)
        {
            if (!Slot.MaterialInterface) { return ArtFail(Error, Label + TEXT(" material slot has no material: ") + Slot.MaterialSlotName.ToString()); }
        }
        for (const FName Slot : Body.TintSlots)
        {
            if (!Materials.ContainsByPredicate([Slot](const FSkeletalMaterial& Material) { return Material.MaterialSlotName == Slot; }))
            { return ArtFail(Error, Label + TEXT(" tint slot is not a mesh material slot: ") + Slot.ToString()); }
        }
        int32 TintSlots = Body.TintSlots.Num();
        TSet<FString> PartIds;
        for (const FLDArtAttachment& Part : Body.Attachments)
        {
            if (!ArtResolveAttachment(Body, *Skeleton, Part, Error)) { return false; }
            bool bSeen = false;
            PartIds.Add(Part.Id, &bSeen);
            if (bSeen) { return ArtFail(Error, Label + TEXT(" wears two parts with id ") + Part.Id); }
            TintSlots += Part.TintSlots.Num();
        }
        if (TintSlots == 0) { return ArtFail(Error, Label + TEXT(" has no identity tint slot on the body or a worn part.")); }
        if (Body.Animations.Num() < LDArt::AnimCount - 1 || Body.Animations.Num() > LDArt::AnimCount)
        { return ArtFail(Error, FString::Printf(TEXT("%s has %d clips; every state but the optional Spawn needs one."), *Label, Body.Animations.Num())); }
        for (int32 Index = 0; Index < Body.Animations.Num(); ++Index)
        {
            if (!ArtResolveClip(Body, Index, *Skeleton, Error)) { return false; }
        }
        return true;
    }
}

FString LDArt::AnimId(ELDArtAnim Anim)
{
    const int32 Index = int32(Anim);
    return Index >= 0 && Index < AnimCount ? FString(ArtAnimIds[Index]) : FString::Printf(TEXT("Anim%d"), Index);
}

bool LDArt::ParseAnimId(const FString& Id, ELDArtAnim& Out)
{
    for (int32 Index = 0; Index < AnimCount; ++Index)
    {
        if (Id.Equals(ArtAnimIds[Index], ESearchCase::CaseSensitive)) { Out = ELDArtAnim(Index); return true; }
    }
    Out = ELDArtAnim::Count;
    return false;
}

FString LDArt::ZoneId(ELDArtZone Zone)
{
    const int32 Index = int32(Zone);
    return Index >= 0 && Index < int32(ELDArtZone::Count) ? FString(ArtZoneIds[Index]) : FString::Printf(TEXT("zone%d"), Index);
}

bool LDArt::ParseZoneId(const FString& Id, ELDArtZone& Out)
{
    for (int32 Index = 0; Index < int32(ELDArtZone::Count); ++Index)
    {
        if (Id.Equals(ArtZoneIds[Index], ESearchCase::CaseSensitive)) { Out = ELDArtZone(Index); return true; }
    }
    Out = ELDArtZone::Count;
    return false;
}

bool LDArt::IsValidId(const FString& Id)
{
    if (Id.IsEmpty() || Id.Len() > 64 || !FChar::IsLower(Id[0])) { return false; }
    for (const TCHAR Char : Id)
    {
        if (!((Char >= 'a' && Char <= 'z') || (Char >= '0' && Char <= '9') || Char == '_')) { return false; }
    }
    return true;
}

const FLDArtAnimation* FLDArtCharacter::Find(ELDArtAnim State) const
{
    return Animations.FindByPredicate([State](const FLDArtAnimation& Clip) { return Clip.State == State; });
}

const FLDArtCharacter* FLDArtResolved::ForSeat(uint8 Seat) const { return Seat < LDArt::SeatCount ? Seats[Seat] : nullptr; }

const FLDArtCharacter* FLDArtResolved::ForMonster(uint8 CreatureId) const { return Monsters.FindRef(CreatureId); }

bool ULDArtCatalog::Resolve(FLDArtResolved& Out, FString& Error) const
{
    Out = FLDArtResolved();
    TMap<FString, const FLDArtCharacter*> Bodies;
    for (int32 Index = 0; Index < Characters.Num(); ++Index)
    {
        const FLDArtCharacter& Body = Characters[Index];
        if (!LDArt::IsValidId(Body.Id) || Bodies.Contains(Body.Id))
        { return ArtFail(Error, FString::Printf(TEXT("Character %d needs a unique lowercase id, found '%s'."), Index, *Body.Id)); }
        if (!ArtResolveCharacter(Body, Error)) { return false; }
        Bodies.Add(Body.Id, &Body);
    }
    if (Seats.Num() != LDArt::SeatCount)
    { return ArtFail(Error, FString::Printf(TEXT("Expected %d seat looks, found %d."), LDArt::SeatCount, Seats.Num())); }
    for (int32 Seat = 0; Seat < LDArt::SeatCount; ++Seat)
    {
        const FLDArtCharacter* Body = Bodies.FindRef(Seats[Seat].Character);
        if (Seats[Seat].Seat != Seat || !Body)
        { return ArtFail(Error, FString::Printf(TEXT("Seat look %d must be seat %d with a catalog character, found seat %d '%s'."), Seat, Seat, Seats[Seat].Seat, *Seats[Seat].Character)); }
        Out.Seats[Seat] = Body;
    }
    // The Low and High camp tiers, the D26 Boss (its own taller body, so it reads apart from the tier it resembles),
    // then the #97 Elite tier (a taller body of its own).
    const uint8 Tiers[] = { uint8(ELDDemoMonsterId::MonsterScavenger), uint8(ELDDemoMonsterId::MonsterGuardian),
        uint8(ELDDemoMonsterId::MonsterDean), uint8(ELDDemoMonsterId::MonsterAdvisor) };
    if (Monsters.Num() != UE_ARRAY_COUNT(Tiers))
    { return ArtFail(Error, FString::Printf(TEXT("Expected %d monster looks, found %d."), int32(UE_ARRAY_COUNT(Tiers)), Monsters.Num())); }
    for (int32 Index = 0; Index < Monsters.Num(); ++Index)
    {
        const FLDArtMonsterLook& Look = Monsters[Index];
        const FLDArtCharacter* Body = Bodies.FindRef(Look.Character);
        if (Look.CreatureId != Tiers[Index] || !Body)
        { return ArtFail(Error, FString::Printf(TEXT("Camp tier look %d must be creature %d with a catalog character, found %d '%s'."), Index, Tiers[Index], Look.CreatureId, *Look.Character)); }
        // Camp NPCs must read apart from every contender and from the other tier at a glance (APP_SPEC 1.1, 8).
        for (const FLDArtCharacter* Seat : Out.Seats)
        {
            if (Seat == Body) { return ArtFail(Error, TEXT("Camp tier body is also a contender body: ") + Look.Character); }
        }
        for (const TPair<uint8, const FLDArtCharacter*>& Other : Out.Monsters)
        {
            if (Other.Value == Body) { return ArtFail(Error, TEXT("Two monster looks use the same body: ") + Look.Character); }
        }
        Out.Monsters.Add(Look.CreatureId, Body);
    }
    TMap<FString, const FLDArtProp*> PropsById;
    TSet<FString> Drawn;
    for (int32 Index = 0; Index < Props.Num(); ++Index)
    {
        const FLDArtProp& Prop = Props[Index];
        if (!LDArt::IsValidId(Prop.Id) || PropsById.Contains(Prop.Id))
        { return ArtFail(Error, FString::Printf(TEXT("Prop %d needs a unique lowercase id, found '%s'."), Index, *Prop.Id)); }
        if (!Prop.Mesh) { return ArtFail(Error, TEXT("Prop has no static mesh: ") + Prop.Id); }
        PropsById.Add(Prop.Id, &Prop);
    }
    if (Decorations.IsEmpty()) { return ArtFail(Error, TEXT("The catalog places no decoration.")); }
    for (int32 Index = 0; Index < Decorations.Num(); ++Index)
    {
        const FLDArtDecoration& Decoration = Decorations[Index];
        const FLDArtProp* Prop = PropsById.FindRef(Decoration.Prop);
        if (!Prop) { return ArtFail(Error, FString::Printf(TEXT("Decoration %d uses unknown prop '%s'."), Index, *Decoration.Prop)); }
        if (!ArtUsableTransform(Decoration.Transform))
        { return ArtFail(Error, FString::Printf(TEXT("Decoration %d (%s) has an unusable transform."), Index, *Decoration.Prop)); }
        if (Decoration.Zone >= ELDArtZone::Count)
        { return ArtFail(Error, FString::Printf(TEXT("Decoration %d (%s) has no placement zone."), Index, *Decoration.Prop)); }
        // A blocker's look names exactly one blocker, drawn once; no other zone names one.
        if ((Decoration.Zone == ELDArtZone::Blocker) == Decoration.Blocker.IsEmpty())
        { return ArtFail(Error, FString::Printf(TEXT("Decoration %d (%s) must name a world blocker exactly when it is in the blocker zone."), Index, *Decoration.Prop)); }
        if (!Decoration.Blocker.IsEmpty())
        {
            bool bAlready = false;
            Drawn.Add(Decoration.Blocker, &bAlready);
            if (bAlready) { return ArtFail(Error, TEXT("Two decorations draw the same world blocker: ") + Decoration.Blocker); }
        }
    }
    for (const FString& Blocker : HiddenBlockers)
    {
        bool bAlready = false;
        Drawn.Add(Blocker, &bAlready);
        if (Blocker.IsEmpty() || bAlready) { return ArtFail(Error, TEXT("A hidden blocker is empty or already drawn by a decoration: ") + Blocker); }
    }
    Out.AirdropCrate = PropsById.FindRef(AirdropProp);
    if (!Out.AirdropCrate || !ArtUsableTransform(AirdropTransform))
    { return ArtFail(Error, TEXT("The airdrop crate needs a catalog prop and a usable transform, found '") + AirdropProp + TEXT("'.")); }
    Out.AirdropTransform = AirdropTransform;
    Error.Reset();
    return true;
}
