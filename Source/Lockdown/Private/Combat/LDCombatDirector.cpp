#include "Combat/LDCombatDirector.h"
#include "Combat/LDTacticalRules.h"
#include "Character/LDCharacter.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"
#include "Engine/World.h"

ALDCombatDirector::ALDCombatDirector()
{
    bReplicates=false;
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PrePhysics;
}

const FLDBodyCombatState* ALDCombatDirector::StateFor(const ALDCharacter* Body) const
{ return Bodies.Find(const_cast<ALDCharacter*>(Body)); }

bool ALDCombatDirector::PrepareInitialBody(ALDCharacter& Body,const FLDVictimKey& Victim,
    const FLDDemoContentSnapshot& Content,const FLDGameplayWritePermit& Permit,
    FLDBodyCombatState& Out,FString& Error)
{
    Out={};
    const auto* Mode=Cast<ALDGameMode>(GetOwner());
    if (!Mode || Mode->Combat!=this
        || Mode->ValidateWritePermit(Permit,ELDCommitPhase::RespawnsAndWorld)!=ELDCommitReject::None
        || !Victim.IsValid() || Victim.Kind!=ELDVictimKind::Contender || Victim.Life!=1
        || !Body.HasAuthority() || Body.GetWorld()!=GetWorld() || Bodies.Contains(&Body))
    {
        Error=TEXT("Body registration requires the unique initial life and a current core permit.");
        return false;
    }
    for (const auto& Pair : Bodies)
    {
        if (Pair.Value.Victim==Victim)
        {
            Error=TEXT("This victim life already has a Combat body binding.");
            return false;
        }
    }
    const auto& Parameters=Content.Get().Parameters;
    const FLDAttributeBuildValues InitialBuild(1,{},{},{});
    const auto Projection=LDAttributeProjection::Project(InitialBuild,Content);
    if (!Projection.IsSuccess())
    {
        Error=TEXT("Initial attributes are invalid: ")+Projection.Error.Field+TEXT(": ")+Projection.Error.Message;
        return false;
    }
    const auto& Attributes=Projection.Value->Final;
    if (Attributes.MaxHPP2<=0 || Attributes.MaxMPP2<0)
    {
        Error=TEXT("Initial resources are not defined by the validated Demo content.");
        return false;
    }
    FLDBasicTimingInput TimingInput;
    TimingInput.BaseAttackSpeed=Parameters.AttackSpeed.Base;
    TimingInput.CurrentAttackSpeed=Attributes.AttackSpeed;
    TimingInput.BaseWindupMs=Parameters.BasicAttackWindupMs;
    const auto Timing=LDTactical::EvaluateBasicTiming(TimingInput,Content);
    if (!Timing.IsSuccess())
    {
        Error=TEXT("Initial basic timing is invalid: ")+Timing.Error.Field+TEXT(": ")+Timing.Error.Message;
        return false;
    }
    FLDBodyCombatState State;
    State.Identity=FGuid::NewGuid();
    State.Victim=Victim;
    State.Resources.HpP2=State.Resources.MaxHpP2=Attributes.MaxHPP2;
    State.Resources.MpP2=State.Resources.MaxMpP2=Attributes.MaxMPP2;
    State.Resources.Revision=1;
    State.BasicTiming=Timing.Value.GetValue();
    State.AttributeProjection=Projection.Value.GetValue();
    State.AttributeRevision=1;
    FLDObservedPublicFacts Facts;
    Facts.Level=InitialBuild.Level;
    Facts.Equipment.Slots[0]=0;
    Facts.Equipment.Slots[1]=0;
    Facts.Statuses.Reset();
    State.PublicFacts=MoveTemp(Facts);
    Out=MoveTemp(State);
    Error.Reset();
    return true;
}

bool ALDCombatDirector::InstallInitialBody(ALDCharacter& Body,FLDBodyCombatState&& Prepared,
    const FLDGameplayWritePermit& Permit,FLDCombatBodyRef& Out,FString& Error)
{
    Out={};
    const auto* Mode=Cast<ALDGameMode>(GetOwner());
    if (!Mode || Mode->Combat!=this
        || Mode->ValidateWritePermit(Permit,ELDCommitPhase::RespawnsAndWorld)!=ELDCommitReject::None
        || !Prepared.Identity.IsValid() || !Prepared.Victim.IsValid()
        || Prepared.Victim.Kind!=ELDVictimKind::Contender || Prepared.Victim.Life!=1
        || Prepared.Resources.Revision!=1 || Prepared.AttributeRevision!=1
        || !Prepared.PublicFacts.IsSet() || Prepared.PublicFacts->Level!=1 || Bodies.Contains(&Body))
    {
        Error=TEXT("Prepared body lost its unique initial registration preconditions.");
        return false;
    }
    for (const auto& Pair : Bodies)
    {
        if (Pair.Value.Victim==Prepared.Victim)
        { Error=TEXT("Prepared victim life is already registered."); return false; }
    }
    Out={Prepared.Victim,Prepared.Identity};
    Bodies.Add(&Body,MoveTemp(Prepared));
    Error.Reset();
    return true;
}

void ALDCombatDirector::RemoveRegisteredBody(const FLDCombatBodyRef& Ref)
{
    for (auto It=Bodies.CreateIterator();It;++It)
    {
        if (It.Value().Victim==Ref.Victim && It.Value().Identity==Ref.BodyIdentity)
        {
            It.RemoveCurrent();
            return;
        }
    }
    UE_LOG(LogTemp,Error,TEXT("LD_DEMO_BODY_RETIRE_MISMATCH"));
}

void ALDCombatDirector::Tick(float Delta)
{
    Super::Tick(Delta);
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if (!Mode || Mode->Combat!=this || !Mode->IsGameplayWriteOpen(Mode->GetMatchContext())) { return; }
    int64 Through=0;
    FString Error;
    if (!Mode->AvailableAuthorityTime(Through,Error) || !AdvanceAuthority(Mode->GetMatchContext(),Through,Error))
    { Mode->FaultAuthority(Error); }
}
