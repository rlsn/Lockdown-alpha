#include "Misc/AutomationTest.h"
#include "Art/LDBasketballMotionSet.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "PoseSearch/PoseSearchDatabase.h"

#include "UObject/StrongObjectPtr.h"

static void ValidateBasketballAssets(FAutomationTestBase* Test, ULDBasketballMotionSet* Set, USkeletalMesh* Mesh)
{
    Test->TestNotNull(TEXT("Right-hand prop socket survives retargeting"),Mesh->FindSocket(TEXT("BasketballSocket")));
    Test->TestNotNull(TEXT("Left-hand prop socket"),Mesh->FindSocket(TEXT("BasketballSocketL")));
    TSet<UAnimSequence*> Animations;
    int32 PoseCount=0;
    for (auto* Database:{Set->Dribble.Get(),Set->Idle.Get(),Set->Handling.Get(),Set->Carry.Get()})
    {
        if (!Test->TestNotNull(TEXT("Search database"),Database)) { continue; }
        const int32 Count=ULDBasketballMotionSet::DatabasePoseCount(Database);
        Test->TestTrue(TEXT("Database contains indexed poses"),Count>0);
        PoseCount+=Count;
        for (int32 I=0;I<Database->GetNumAnimationAssets();++I)
        {
            auto* Animation=Cast<UAnimSequence>(Database->GetAnimationAsset(I));
            if (Test->TestNotNull(TEXT("Database entry is an animation"),Animation)) { Animations.Add(Animation); }
        }
    }
    Test->TestTrue(TEXT("Search has indexed basketball poses"),PoseCount>0);
    Test->TestEqual(TEXT("One running between-the-legs dash"),Set->Crossovers.Num(),1);
    Test->TestTrue(TEXT("No compressed jump-stop shuffle"),Set->Stops.IsEmpty());
    Test->TestEqual(TEXT("Both original spin directions"),Set->Turns.Num(),2);
    if (Set->Dribble)
    {
        Test->TestEqual(TEXT("Single run across movement upgrades"),Set->Dribble->GetNumAnimationAssets(),1);
        for (int32 I=0;I<Set->Dribble->GetNumAnimationAssets();++I)
        {
            Test->TestTrue(TEXT("Run remains on 007"),Set->Dribble->GetAnimationAsset(I)->GetName().StartsWith(TEXT("KB_007_")));
        }
    }
    if (Set->Handling)
    {
        TSet<FString> Names;
        for (int32 I=0;I<Set->Handling->GetNumAnimationAssets();++I) { Names.Add(Set->Handling->GetAnimationAsset(I)->GetName().Left(7)); }
        Test->TestTrue(TEXT("Both requested idle variations"),Names.Contains(TEXT("KB_043_")) && Names.Contains(TEXT("KB_044_")));
    }
    for (const auto* Group:{&Set->Passes,&Set->StandingShots,&Set->MovingShots,&Set->Crossovers,&Set->Catches,&Set->Turns})
    {
        Test->TestTrue(TEXT("Every basketball action has authored motion"),!Group->IsEmpty());
        for (const auto& Clip:*Group)
        {
            if (!Test->TestNotNull(TEXT("Action clip"),Clip.Animation.Get())) { continue; }
            Animations.Add(Clip.Animation);
            const int32 SourceId=FCString::Atoi(*Clip.Animation->GetName().Mid(3,3));
            Test->TestTrue(TEXT("Actions use imported basketball pack motions"),SourceId>0 && SourceId<178);
            Test->TestTrue(TEXT("Action windows remain inside source motion"),Clip.Start>=0 && Clip.Start<Clip.Release && Clip.Release<=Clip.End && Clip.End<=Clip.Animation->GetPlayLength()+.001f);
        }
    }
    Test->TestTrue(TEXT("Locomotion and action coverage"),Animations.Num()>=20);
    const FVector BindScale=Mesh->GetRefSkeleton().GetRefBonePose()[0].GetScale3D();
    for (auto* Animation:Animations)
    {
        Test->TestTrue(Animation->GetName()+TEXT(" uses the hero skeleton"),Animation->GetSkeleton()==Mesh->GetSkeleton());
        const auto* Model=Animation->GetDataModel();
        if (!Test->TestNotNull(TEXT("Imported bone data"),Model)) { continue; }
        const FTransform Root=Model->GetBoneTrackTransform(TEXT("root"),FFrameNumber(0));
        Test->TestTrue(Animation->GetName()+TEXT(" preserves the Tripo bind scale"),Root.GetScale3D().Equals(BindScale,.02));
        for (const TCHAR* Name:{TEXT("BallX"),TEXT("BallY"),TEXT("BallZ"),TEXT("BallLeft"),TEXT("BallContact")})
        {
            Test->TestNotNull(Animation->GetName()+TEXT(" has synchronized prop curve ")+Name,Animation->GetCurveData().GetCurveData(Name));
        }
        for (float T=0;T<Animation->GetPlayLength();T+=.1f)
        {
            const FAnimExtractContext Context(T,false);
            const float Contact=Animation->EvaluateCurveData(TEXT("BallContact"),Context,true);
            Test->TestTrue(TEXT("Hand contact weight remains normalized"),FMath::IsFinite(Contact) && Contact>=-.001f && Contact<=1.001f);
            const float Height=Animation->EvaluateCurveData(TEXT("BallZ"),Context,true);
            Test->TestTrue(Animation->GetName()+TEXT(" keeps the ball center above its radius"),FMath::IsFinite(Height) && Height>=11.5f);
        }
    }
}

class FCheckBasketballAssets : public IAutomationLatentCommand
{
public:
    FCheckBasketballAssets(FAutomationTestBase* InTest, ULDBasketballMotionSet* InSet, USkeletalMesh* InMesh)
        : Test(InTest), Set(InSet), Mesh(InMesh), Started(FPlatformTime::Seconds()) {}
    virtual bool Update() override
    {
        for (auto* Database:{Set->Dribble.Get(),Set->Idle.Get(),Set->Handling.Get(),Set->Carry.Get()})
        {
            if (!Database) { Test->AddError(TEXT("Missing basketball pose search database")); return true; }
            if (ULDBasketballMotionSet::DatabasePoseCount(Database)==0)
            {
                if (FPlatformTime::Seconds()-Started<30) { return false; }
                Test->AddError(TEXT("Basketball pose search index did not finish within 30 seconds"));
                return true;
            }
        }
        ValidateBasketballAssets(Test,Set.Get(),Mesh.Get());
        return true;
    }
private:
    FAutomationTestBase* Test;
    TStrongObjectPtr<ULDBasketballMotionSet> Set;
    TStrongObjectPtr<USkeletalMesh> Mesh;
    double Started;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBasketballMotionAssetsTest,"Lockdown.Rules.BasketballMotionAssets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDBasketballMotionAssetsTest::RunTest(const FString&)
{
    auto* Set=LoadObject<ULDBasketballMotionSet>(nullptr,TEXT("/Game/Lockdown/Art_Test/Characters/Kobe/Basketball/DA_KobeBasketball"));
    auto* Mesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Lockdown/Art_Test/Characters/Kobe/SK_BasketballHero"));
    if (!TestNotNull(TEXT("Basketball motion set"),Set) || !TestNotNull(TEXT("Preview mesh"),Mesh)) { return false; }
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FCheckBasketballAssets>(this,Set,Mesh));
    return true;
}
