#include "World/LDCampDecision.h"
#include "Development/LDDemoContentCatalog.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include <limits>

namespace LDCampDecisionTests
{
bool ReadApprovedFixture(FAutomationTestBase& Test,TSharedPtr<const FLDDemoContentSnapshot>& Out)
{
    FString Root,Error,Text;
    if(!FParse::Value(FCommandLine::Get(),TEXT("LDDemoContentFixtures="),Root)||Root.IsEmpty()
        ||!FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Root),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(),TEXT("Artifacts"))))
    {Test.AddError(TEXT("Camp decision tests require isolated LDDemoContentFixtures with approved_base; no live-data fallback."));return false;}
    TSharedPtr<FJsonObject> Index;
    if(!FFileHelper::LoadFileToString(Text,*FPaths::Combine(Root,TEXT("index.json")))
        ||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Index)||!Index)
    {Test.AddError(TEXT("Unreadable camp content fixture index."));return false;}
    FString Format;bool TestOnly=false;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if(!Index->TryGetStringField(TEXT("format"),Format)||Format!=TEXT("demo-native-fixtures-1")
        ||!Index->TryGetBoolField(TEXT("test_only"),TestOnly)||!TestOnly||!Index->TryGetArrayField(TEXT("valid"),Rows))
    {Test.AddError(TEXT("Invalid camp content fixture index."));return false;}
    bool Found=false;FLDDemoContentDigest Digest;
    for(const auto& Value:*Rows)
    {
        const TSharedPtr<FJsonObject>* Row=nullptr;FString Name,Hex;
        if(!Value->TryGetObject(Row)||!(*Row)->TryGetStringField(TEXT("name"),Name))
        {Test.AddError(TEXT("Invalid fixture case."));return false;}
        if(Name!=TEXT("approved_base")){continue;}
        if(Found||!(*Row)->TryGetStringField(TEXT("catalog_sha256"),Hex)||!FLDDemoContentDigest::ParseHex(Hex,Digest,Error))
        {Test.AddError(TEXT("Duplicate or invalid approved_base fixture."));return false;}
        Found=true;
    }
    if(!Found){Test.AddError(TEXT("Missing approved_base fixture."));return false;}
    FLDDemoContentBundle Bundle;TUniquePtr<FLDDemoPreparedContent> Prepared;
    auto* Asset=NewObject<ULDDemoContentCatalog>(GetTransientPackage());
    if(!FLDDemoContentBundle::ReadImportFiles(FPaths::Combine(Root,TEXT("approved_base")),Bundle,Error)
        ||!ULDDemoContentCatalog::PrepareImport(*Asset,Bundle,Digest,Prepared,Error)
        ||!Asset->CommitImport(Prepared,Digest,Error)||!Asset->Read(Digest,Out,Error))
    {Test.AddError(Error);return false;}
    return true;
}

double Ratio(const FLDDemoRational& Value){return double(Value.Numerator)/Value.Denominator;}

FLDCombatBodyRef Contender(uint32 Id,uint32 Life=1)
{return {{ELDVictimKind::Contender,FGuid(Id,0,0,0),Life},FGuid(Id,Life,0,0)};}

FLDCombatBodyRef CampMember(uint32 Slot)
{return {{ELDVictimKind::CampMember,FGuid(1,0,0,Slot),1},FGuid(2,0,0,Slot)};}

FLDCampDecisionInput InputFor(const FLDDemoContentSnapshot& Catalog)
{
    FLDCampDecisionInput Input;
    Input.Match={1,FGuid(1,2,3,4)};
    Input.World={FName(*Catalog.Get().World.MapId),1,1};
    Input.Self=CampMember(1);
    Input.NowSeconds=10;
    Input.FootCm=FVector(0,0,2);
    Input.Grounded=true;Input.SurfaceId=TEXT("fixture_ground");
    Input.CampAnchorFootCm=Input.FootCm;
    Input.MemberHome={Input.World,FVector(2.*Ratio(Catalog.Get().Parameters.CampResetArrivalRadiusCm),0,2),Input.SurfaceId};
    Input.bObservationComplete=true;
    Input.MovementAllowed=Input.AttackAllowed=Input.BasicAttackReady=true;
    return Input;
}

FLDAuthorityTargetSample Sample(const FLDCampDecisionInput& Input,const FLDCombatBodyRef& Body,const FVector& Point)
{return {Body,Point,Input.World,Input.NowSeconds};}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCampDecisionTest,"Lockdown.Rules.CampDecision",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDCampDecisionTest::RunTest(const FString& Parameters)
{
    using namespace LDCampDecisionTests;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if(!ReadApprovedFixture(*this,Catalog)){return false;}
    FString Error;
    for(const auto& Camp:Catalog->Get().Camps)
    {
        const FName CampId(*Camp.AnchorId);const FLDDemoMonsterDefinition* Monster=nullptr;
        if(!Catalog->FindMonster(Camp.MonsterId,Monster,Error)){AddError(Error);return false;}
        const double Attack=Ratio(Monster->AttackRangeCm),Leash=Ratio(Monster->LeashRadiusCm);
        const double Perception=Ratio(Monster->PerceptionRadiusCm);
        const double Arrival=Ratio(Catalog->Get().Parameters.CampResetArrivalRadiusCm);
        const double Grace=double(Monster->LoseTargetGraceMs)/1000.;
        if(!TestTrue(TEXT("Approved camp keeps attack reach inside a perception radius equal to its leash, home arrival well inside it and a positive loss grace"),
            Attack<Perception&&Perception==Leash&&3*Arrival<Leash&&Grace>0)){return false;}
        FLDCampDecisionInput Input=InputFor(*Catalog);FLDCampDecision Out;
        const FVector Anchor=Input.CampAnchorFootCm;
        TArray<FLDCombatBodyRef> Hostile={Contender(3)};
        TArray<FLDAuthorityTargetSample> Candidates={Sample(Input,Contender(3),Anchor+FVector(Attack,0,0))};
        auto Evaluate=[&](){return LDCampDecision::Evaluate(*Catalog,CampId,Input,Hostile,Candidates,Out,Error);};
        auto Picked=[&](const FLDCombatBodyRef& Body){return Out.Target.IsSet()&&Out.Target.GetValue()==Body;};
        auto Lost=[&]()
        {
            return Out.Intent==ELDCampIntent::Hold&&Out.NextBrain.Phase==ELDCampBrainPhase::Engaged
                &&!Out.Target.IsSet()&&!Out.GoalFootCm.IsSet()&&Out.NextBrain.LostTargetSinceSeconds.IsSet()
                &&Out.NextBrain.LostTargetSinceSeconds.GetValue()==Input.NowSeconds;
        };
        TestTrue(TEXT("Idle never engages, even when a recorded attacker is visible nearby"),
            Evaluate()&&Out.Intent==ELDCampIntent::Hold&&Out.NextBrain.Phase==ELDCampBrainPhase::Idle
            &&!Out.Target.IsSet()&&!Out.GoalFootCm.IsSet());
        Input.Brain.Phase=ELDCampBrainPhase::Engaged;
        TestTrue(TEXT("Approved attack boundary is inclusive"),
            Evaluate()&&Out.Intent==ELDCampIntent::RequestBasicAttack&&Picked(Contender(3)));
        Input.BasicAttackReady=false;
        TestTrue(TEXT("Cooldown readiness is not invented"),Evaluate()&&Out.Intent==ELDCampIntent::Hold&&Picked(Contender(3)));
        Input.BasicAttackReady=true;Input.AttackAllowed=false;
        TestTrue(TEXT("Control/action denial is respected"),Evaluate()&&Out.Intent==ELDCampIntent::Hold);
        Input.AttackAllowed=true;Candidates[0].FootCm.X=Attack+.01;
        TestTrue(TEXT("A current visible point beyond attack reach requests pursuit"),
            Evaluate()&&Out.Intent==ELDCampIntent::Chase&&Out.GoalFootCm.IsSet()&&Out.GoalFootCm.GetValue()==Candidates[0].FootCm);
        Input.MovementAllowed=false;
        TestTrue(TEXT("Movement control pauses pursuit"),Evaluate()&&Out.Intent==ELDCampIntent::Hold);
        Input.MovementAllowed=true;

        Hostile={Contender(3),Contender(4)};
        Candidates={Sample(Input,Contender(4),Anchor+FVector(Attack,0,0)),Sample(Input,Contender(3),Anchor-FVector(Attack,0,0))};
        TestTrue(TEXT("Equal-distance choice is stable by full identity"),Evaluate()&&Picked(Contender(3)));
        Swap(Candidates[0],Candidates[1]);Swap(Hostile[0],Hostile[1]);
        TestTrue(TEXT("Candidate and source order cannot choose a different target"),Evaluate()&&Picked(Contender(3)));

        Hostile={Contender(3)};
        Candidates={Sample(Input,CampMember(2),Anchor+FVector(Attack*.5,0,0)),Sample(Input,Contender(3),Anchor-FVector(Attack,0,0))};
        TestTrue(TEXT("A closer same-camp member is never chosen over the attacker"),
            Evaluate()&&Out.Intent==ELDCampIntent::RequestBasicAttack&&Picked(Contender(3)));
        Candidates={Sample(Input,Contender(5),Anchor+FVector(Attack*.5,0,0)),Sample(Input,Contender(3),Anchor-FVector(Attack,0,0))};
        TestTrue(TEXT("A closer contender that never attacked this camp is never chosen"),
            Evaluate()&&Out.Intent==ELDCampIntent::RequestBasicAttack&&Picked(Contender(3)));
        Candidates={Sample(Input,CampMember(2),Anchor+FVector(Attack*.5,0,0)),Sample(Input,Contender(5),Anchor-FVector(Attack*.5,0,0))};
        TestTrue(TEXT("Visible non-attackers leave the member without a target"),Evaluate()&&Lost());
        Candidates={Sample(Input,Contender(3,2),Anchor+FVector(Attack,0,0))};
        TestTrue(TEXT("The attacker's later life is not inherited as a retaliation target"),Evaluate()&&Lost());
        Hostile={Contender(3),Contender(3,2)};
        TestTrue(TEXT("A later life becomes a target only through its own recorded contact"),Evaluate()&&Picked(Contender(3,2)));

        Hostile={Contender(3)};
        Candidates={Sample(Input,Contender(3),Anchor+FVector(Perception,0,0))};
        TestTrue(TEXT("Approved perception boundary is inclusive"),
            Evaluate()&&Out.Intent==ELDCampIntent::Chase&&Picked(Contender(3)));
        Candidates[0].FootCm.X+=.01;
        TestTrue(TEXT("An attacker beyond perception is not pursued"),Evaluate()&&Lost());
        Input.FootCm=Anchor-FVector(Attack,0,0);
        Candidates={Sample(Input,Contender(3),Anchor+FVector(Perception-1,0,0))};
        TestTrue(TEXT("Perception is measured from the member's current foot"),Evaluate()&&Lost());
        Input.FootCm=Anchor+FVector(Leash,0,0);
        Candidates={Sample(Input,Contender(3),Input.FootCm+FVector(Attack*.5,0,0))};
        TestTrue(TEXT("An attacker outside the leash circle but within perception remains selectable"),
            Evaluate()&&Out.Intent==ELDCampIntent::RequestBasicAttack&&Out.NextBrain.Phase==ELDCampBrainPhase::Engaged
            &&Picked(Contender(3)));
        Candidates[0].FootCm=Input.FootCm+FVector((Attack+Perception)*.5,0,0);
        TestTrue(TEXT("Pursuit may head outside the leash circle while the member itself is within it"),
            Evaluate()&&Out.Intent==ELDCampIntent::Chase&&Picked(Contender(3))
            &&Out.GoalFootCm.IsSet()&&Out.GoalFootCm.GetValue()==Candidates[0].FootCm);
        Candidates[0].FootCm=Input.FootCm+FVector(Attack*.5,0,0);
        Input.FootCm.X+=.01;
        TestTrue(TEXT("Once the member itself is strictly beyond the camp-anchor leash it returns and stops selecting"),
            Evaluate()&&Out.Intent==ELDCampIntent::ReturnHome&&Out.NextBrain.Phase==ELDCampBrainPhase::Returning
            &&!Out.Target.IsSet()&&Out.GoalFootCm.IsSet()&&Out.GoalFootCm.GetValue()==Input.MemberHome.PositionCm);
        Input.Brain=Out.NextBrain;Input.FootCm=Anchor;
        Candidates={Sample(Input,Contender(3),Anchor+FVector(Attack*.5,0,0))};
        TestTrue(TEXT("Returning inside the leash keeps returning even with a recorded attacker in reach"),
            Evaluate()&&Out.Intent==ELDCampIntent::ReturnHome&&Out.NextBrain.Phase==ELDCampBrainPhase::Returning
            &&!Out.Target.IsSet()&&Out.GoalFootCm.IsSet()&&Out.GoalFootCm.GetValue()==Input.MemberHome.PositionCm);
        Input.MovementAllowed=false;
        TestTrue(TEXT("Controlled Returning pauses without leaving the phase or reacquiring an eligible attacker"),
            Evaluate()&&Out.Intent==ELDCampIntent::Hold&&Out.NextBrain.Phase==ELDCampBrainPhase::Returning&&!Out.Target.IsSet());
        Input.FootCm=Input.MemberHome.PositionCm+FVector(Arrival,0,0);
        TestTrue(TEXT("True same-surface arrival reports reset readiness without resetting the brain or resources"),
            Evaluate()&&Out.Intent==ELDCampIntent::ResetReady&&Out.NextBrain.Phase==ELDCampBrainPhase::Returning&&!Out.Target.IsSet());
        Input.Grounded=false;
        TestTrue(TEXT("A nearby airborne member cannot reset"),Evaluate()&&Out.Intent!=ELDCampIntent::ResetReady);
        Input.Grounded=true;Input.SurfaceId=TEXT("other_fixture_surface");
        TestTrue(TEXT("A different physical surface cannot reset"),Evaluate()&&Out.Intent!=ELDCampIntent::ResetReady);

        Input=InputFor(*Catalog);Input.Brain.Phase=ELDCampBrainPhase::Engaged;Candidates.Reset();
        TestTrue(TEXT("A completed empty observation records the current loss time without retaining a hidden goal"),
            Evaluate()&&Lost());
        if(!Out.NextBrain.LostTargetSinceSeconds.IsSet()){return false;}
        Input.Brain=Out.NextBrain;Input.NowSeconds+=Grace*.5;
        TestTrue(TEXT("Loss grace never authorizes hidden pursuit"),
            Evaluate()&&Out.Intent==ELDCampIntent::Hold&&!Out.GoalFootCm.IsSet()&&Out.NextBrain.LostTargetSinceSeconds.IsSet()
            &&Out.NextBrain.LostTargetSinceSeconds.GetValue()==Input.Brain.LostTargetSinceSeconds.GetValue());
        Input.NowSeconds=Input.Brain.LostTargetSinceSeconds.GetValue()+Grace;
        TestTrue(TEXT("Grace expiry requests the registered member home"),
            Evaluate()&&Out.NextBrain.Phase==ELDCampBrainPhase::Returning&&Out.Intent==ELDCampIntent::ReturnHome
            &&Out.GoalFootCm.IsSet()&&Out.GoalFootCm.GetValue()==Input.MemberHome.PositionCm);
        Input=InputFor(*Catalog);Input.Brain.Phase=ELDCampBrainPhase::Engaged;Hostile.Reset();
        Candidates={Sample(Input,Contender(3),Anchor+FVector(Attack,0,0))};
        TestTrue(TEXT("Without a consumed hostile contact no visible contender becomes a target"),Evaluate()&&Lost());
    }

    FLDCampDecisionInput Good=InputFor(*Catalog);Good.Brain.Phase=ELDCampBrainPhase::Engaged;
    const FName CampId(*Catalog->Get().Camps[0].AnchorId);
    const FLDCombatBodyRef Attacker=Contender(3);
    const TArray<FLDCombatBodyRef> Sources={Attacker};
    const auto Seen=Sample(Good,Attacker,FVector(10,0,2));
    auto Reject=[&](const TCHAR* Name,const FLDCampDecisionInput& Input,const TArray<FLDCombatBodyRef>& Hostile,
        const TArray<FLDAuthorityTargetSample>& Samples,FName Id)
    {
        FLDCampDecision Out;Out.Intent=ELDCampIntent::ResetReady;Out.GoalFootCm=FVector(7,8,9);Error.Reset();
        TestFalse(Name,LDCampDecision::Evaluate(*Catalog,Id,Input,Hostile,Samples,Out,Error));
        TestTrue(TEXT("Rejection is explicit and cannot overwrite a previous decision"),!Error.IsEmpty()
            &&Out.Intent==ELDCampIntent::ResetReady&&Out.GoalFootCm.IsSet()&&Out.GoalFootCm.GetValue()==FVector(7,8,9));
    };
    auto Bad=Good;Bad.bObservationComplete=false;
    Reject(TEXT("Incomplete observation is not a successful empty candidate set"),Bad,Sources,{},CampId);
    Reject(TEXT("Incomplete observation cannot carry a usable candidate"),Bad,Sources,{Seen},CampId);
    Bad=Good;Bad.Match.Generation=0;Reject(TEXT("Invalid match refused"),Bad,Sources,{Seen},CampId);
    Bad=Good;Bad.Self.BodyIdentity.Invalidate();Reject(TEXT("Invalid self body refused"),Bad,Sources,{Seen},CampId);
    Bad=Good;++Bad.MemberHome.World.GeometryRevision;Reject(TEXT("Stale registered home refused"),Bad,Sources,{Seen},CampId);
    Bad=Good;Bad.NowSeconds=std::numeric_limits<double>::quiet_NaN();Reject(TEXT("Invalid clock refused"),Bad,Sources,{},CampId);
    Bad=Good;Bad.FootCm.X=std::numeric_limits<double>::infinity();Reject(TEXT("Invalid self position refused"),Bad,Sources,{},CampId);
    Bad=Good;Bad.Brain.Phase=static_cast<ELDCampBrainPhase>(255);Reject(TEXT("Unknown brain phase refused"),Bad,Sources,{},CampId);
    Bad=Good;Bad.Brain.LostTargetSinceSeconds=Good.NowSeconds+1;Reject(TEXT("Future loss time refused"),Bad,Sources,{},CampId);
    Reject(TEXT("Unknown camp cannot use arbitrary parameters"),Good,Sources,{Seen},TEXT("unknown_camp"));
    auto Invalid=Seen;++Invalid.World.LayoutRevision;Reject(TEXT("Stale sample world refused"),Good,Sources,{Invalid},CampId);
    Invalid=Seen;Invalid.SampledAtSeconds-=.001;Reject(TEXT("Old sample time refused"),Good,Sources,{Invalid},CampId);
    Invalid=Seen;Invalid.FootCm.Y=std::numeric_limits<double>::quiet_NaN();Reject(TEXT("Nonfinite candidate refused"),Good,Sources,{Invalid},CampId);
    Invalid=Seen;Invalid.Target=Good.Self;Reject(TEXT("Self target refused"),Good,Sources,{Invalid},CampId);
    Reject(TEXT("Duplicate current identity refused"),Good,Sources,{Seen,Seen},CampId);
    TArray<FLDAuthorityTargetSample> Oversized;
    Oversized.Init(Seen,LDObservation::MaxUnits+1);
    Reject(TEXT("Observation capacity is checked before candidate allocation"),Good,Sources,Oversized,CampId);
    Invalid=Seen;Invalid.Target.BodyIdentity=FGuid(99,0,0,0);Reject(TEXT("Two bodies for one current victim refused"),Good,Sources,{Seen,Invalid},CampId);
    Invalid=Seen;++Invalid.Target.Victim.Life;Invalid.Target.BodyIdentity=FGuid(99,0,0,0);
    Reject(TEXT("Two current lives for one typed entity refused"),Good,Sources,{Seen,Invalid},CampId);
    Invalid=Seen;Invalid.Target.BodyIdentity=FGuid(98,0,0,0);
    Reject(TEXT("A recorded attacker life seen with a different body is refused"),Good,Sources,{Invalid},CampId);
    FLDCombatBodyRef Source=Attacker;Source.BodyIdentity.Invalidate();
    Reject(TEXT("Invalid hostile source refused"),Good,{Source},{Seen},CampId);
    Reject(TEXT("A camp member cannot be recorded as a hostile source"),Good,{CampMember(2)},{Seen},CampId);
    Reject(TEXT("Duplicate hostile source refused"),Good,{Attacker,Attacker},{Seen},CampId);
    Source=Attacker;Source.BodyIdentity=FGuid(97,0,0,0);
    Reject(TEXT("Two bodies for one hostile life refused"),Good,{Attacker,Source},{Seen},CampId);
    Source=Contender(4);Source.BodyIdentity=Attacker.BodyIdentity;
    Reject(TEXT("One body cannot belong to two hostile lives"),Good,{Attacker,Source},{},CampId);
    Bad=Good;Bad.Brain=FLDCampBrain();
    Reject(TEXT("Hostile sources are validated while Idle"),Bad,{CampMember(2)},{},CampId);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCampReturnDeadEndTest,"Lockdown.Rules.DemoCamp.ReturnDeadEnd",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDCampReturnDeadEndTest::RunTest(const FString& Parameters)
{
    using namespace LDCampDecisionTests;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if(!ReadApprovedFixture(*this,Catalog)){return false;}
    const FLDDemoContentParameters& P=Catalog->Get().Parameters;
    if(!TestTrue(TEXT("Approved #28 dead-end limits are positive"),P.CampReturnDeadEndPlans>0&&P.CampReturnDeadEndMs>0)){return false;}
    const int32 Plans=int32(P.CampReturnDeadEndPlans);
    const double Limit=double(P.CampReturnDeadEndMs)/1000.,Retry=2.;

    FLDCampReturnWatch W;W.Observe(1,10);
    TestTrue(TEXT("A new Returning cycle starts clean"),
        W.Epoch==1&&W.RefusedPlans==0&&!W.bDeadEnd&&!W.bWarned&&W.MayPlan(10)&&W.LastProgressSeconds==10);
    double Now=10;bool bEnded=false;
    for(int32 Refusal=1;Refusal<Plans;++Refusal)
    {
        bEnded|=W.PlanRefused(Now,Plans,Retry);
        TestFalse(TEXT("A refused plan waits the retry spacing before planning again"),W.MayPlan(Now+Retry*.5));
        Now+=Retry;
        TestTrue(TEXT("The retry spacing elapses"),W.MayPlan(Now));
    }
    TestFalse(TEXT("Fewer than the approved consecutive refusals keep returning"),bEnded||W.bDeadEnd);
    TestTrue(TEXT("The approved consecutive refusal ends the return"),W.PlanRefused(Now,Plans,Retry)&&W.bDeadEnd);
    TestFalse(TEXT("A dead end never plans again"),W.MayPlan(Now+100));
    TestFalse(TEXT("A dead end is reported once"),W.PlanRefused(Now,Plans,Retry)||W.Stalled(Now+100,Limit));
    W.Observe(1,Now+1);
    TestTrue(TEXT("The same epoch keeps its dead end"),W.bDeadEnd&&W.Epoch==1);
    W.Observe(2,Now+1);
    TestTrue(TEXT("A later Returning cycle starts over"),W.Epoch==2&&!W.bDeadEnd&&W.RefusedPlans==0&&W.LastProgressSeconds==Now+1);
    if(Plans>1)
    {
        for(int32 Refusal=1;Refusal<Plans;++Refusal){W.PlanRefused(Now,Plans,Retry);}
        W.PlanAccepted();
        TestFalse(TEXT("An accepted plan restarts the consecutive refusal count"),W.PlanRefused(Now,Plans,Retry)||W.bDeadEnd);
    }

    FLDCampReturnWatch S;S.Observe(5,0);
    TestFalse(TEXT("Short of the approved no-progress span the return continues"),S.Stalled(Limit-.5,Limit));
    S.Progressed(4);
    TestFalse(TEXT("Path progress (or a control pause) restarts the no-progress clock"),S.Stalled(4+Limit-.5,Limit));
    TestTrue(TEXT("The approved no-progress span is inclusive and ends the return"),S.Stalled(4+Limit,Limit)&&S.bDeadEnd);

    FString Error;
    const auto& Camp=Catalog->Get().Camps[0];
    const FName CampId(*Camp.AnchorId);const FLDDemoMonsterDefinition* Monster=nullptr;
    if(!Catalog->FindMonster(Camp.MonsterId,Monster,Error)){AddError(Error);return false;}
    const double Leash=Ratio(Monster->LeashRadiusCm),Attack=Ratio(Monster->AttackRangeCm);
    FLDCampDecisionInput Input=InputFor(*Catalog);FLDCampDecision Out;
    Input.FootCm=Input.CampAnchorFootCm+FVector(Leash+1,0,0);
    const TArray<FLDCombatBodyRef> Hostile={Contender(3)};
    const TArray<FLDAuthorityTargetSample> Candidates={Sample(Input,Contender(3),Input.FootCm+FVector(Attack*.5,0,0))};
    TestTrue(TEXT("A member reset where it stood holds beyond the leash until attacked"),
        LDCampDecision::Evaluate(*Catalog,CampId,Input,{},{},Out,Error)
        &&Out.Intent==ELDCampIntent::Hold&&Out.NextBrain.Phase==ELDCampBrainPhase::Idle);
    Input.Brain.Phase=ELDCampBrainPhase::Engaged;
    TestTrue(TEXT("Once attacked beyond the leash it heads home again instead of fighting"),
        LDCampDecision::Evaluate(*Catalog,CampId,Input,Hostile,Candidates,Out,Error)
        &&Out.Intent==ELDCampIntent::ReturnHome&&Out.NextBrain.Phase==ELDCampBrainPhase::Returning&&!Out.Target.IsSet());
    return true;
}
#endif
