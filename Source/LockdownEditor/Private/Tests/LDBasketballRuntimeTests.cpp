#include "Character/LDBasketballAnimInstance.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Art/LDArtCatalog.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "FileHelpers.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBasketballRuntimeTest,"Lockdown.Rules.Art.BasketballRuntime",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FLDBasketballRuntimeTest::RunTest(const FString& Parameters)
{
    auto* Catalog=LoadObject<ULDArtCatalog>(nullptr,ULDArtCatalog::AssetPath);FString Error;FLDArtResolved Resolved;
    if(!TestNotNull(TEXT("Production art catalog"),Catalog)||!TestTrue(TEXT("Production catalog resolves: ")+Error,Catalog->Resolve(Resolved,Error))){return false;}
    const auto* Body=Resolved.ForSeat(0);
    if(!TestNotNull(TEXT("Production player body"),Body)||!TestNotNull(TEXT("Production basketball bank"),Body->BasketballMotion.Get())){return false;}
    for(int32 Seat=0;Seat<LDArt::SeatCount;++Seat)
    {TestTrue(TEXT("Every production player seat uses the same basketball presentation bank"),Resolved.ForSeat(Seat)->BasketballMotion==Body->BasketballMotion);}
    auto* Motion=Body->BasketballMotion.Get();
    TestTrue(TEXT("Default idle is a basketball stance, not a generic idle"),Body->Find(ELDArtAnim::Idle)->Sequence==Motion->Idle[0].Animation);
    TestTrue(TEXT("Default run is the retained right-hand dribble"),Body->Find(ELDArtAnim::Run)->Sequence==Motion->Run[0].Animation);
    TestTrue(TEXT("Default attack is the retained single-hand pass"),Body->Find(ELDArtAnim::Attack)->Sequence==Motion->Passes[0].Animation);
    TestTrue(TEXT("Default cast is the retained gathered shot"),Body->Find(ELDArtAnim::Cast)->Sequence==Motion->StandingShots[0].Animation);
    for(const auto* Bank:{&Motion->Passes,&Motion->StandingShots,&Motion->MovingShots}){for(const auto& Clip:*Bank)
    {
        for(float Delay:{.05f,.12f,.4f,1.2f})
        {
            TestTrue(TEXT("Each authored clip releases exactly at the existing action delay at all tested speeds"),
                FMath::IsNearlyEqual(LDBasketballMotion::SampleTime(Clip,Delay,Delay),Clip.Release,1.e-5f));
            TestTrue(TEXT("The recovery also completes on its original scaled window"),
                FMath::IsNearlyEqual(LDBasketballMotion::SampleTime(Clip,LDBasketballMotion::Duration(Clip,Delay),Delay),Clip.End,1.e-5f));
        }
    }}
    auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);
    ON_SCOPE_EXIT{UEditorLoadingAndSavingUtils::NewBlankMap(false);};
    auto* Owner=World->SpawnActor<AActor>();auto* Visual=NewObject<ULDHumanoidVisualComponent>(Owner);
    Owner->SetRootComponent(Visual);Visual->RegisterComponent();Visual->ApplyLook(Body,FLinearColor::White,TEXT("Basketball runtime fixture"));
    auto* Mesh=Visual->Mesh.Get();auto* Ball=Visual->Basketball.Get();
    Mesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    auto* Animation=Cast<ULDBasketballAnimInstance>(Mesh->GetAnimInstance());
    if(!TestNotNull(TEXT("The production visual, including proxy owners that are not Pawns, installs the runtime graph"),Animation)){return false;}
    TestTrue(TEXT("The graph belongs to the runtime module"),Animation->GetClass()->GetPathName().StartsWith(TEXT("/Script/Lockdown.")));
    TestEqual(TEXT("Basketball never adds pawn or target collision"),Ball->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
    TestFalse(TEXT("Basketball never changes navigation export"),Ball->CanEverAffectNavigation());
    const FTransform Before=Owner->GetActorTransform();
    const auto Evaluate=[&](){Mesh->TickAnimation(1.f/60,false);Mesh->RefreshBoneTransforms();};
    Animation->Prepare(1,500,0,LDBodyMotion::EAction::None,0,.2f,0,nullptr,0,false,true);Evaluate();
    TestFalse(TEXT("A visible player's dribble prop is drawn"),Ball->bHiddenInGame);
    const FVector InitialBall=Ball->GetComponentLocation();
    Animation->Prepare(1.1,500,0,LDBodyMotion::EAction::None,0,.2f,0,nullptr,0,false,true);Evaluate();
    TestTrue(TEXT("Dribbling follows the retained pose/ball curves"),FVector::Distance(InitialBall,Ball->GetComponentLocation())>1);
    const float PosedHeight=Mesh->GetSocketLocation(TEXT("head")).Z-FMath::Min(Mesh->GetSocketLocation(TEXT("foot_l")).Z,Mesh->GetSocketLocation(TEXT("foot_r")).Z);
    TestTrue(TEXT("Retained root scale produces a human-size pose on the current production skeleton"),PosedHeight>80&&PosedHeight<220);
    Animation->Prepare(1.2,500,180,LDBodyMotion::EAction::None,0,.2f,0,nullptr,0,false,true);Evaluate();
    Animation->Prepare(1.3,500,180,LDBodyMotion::EAction::None,0,.2f,0,nullptr,0,false,true);Evaluate();
    TestTrue(TEXT("An abrupt running reversal uses the retained spin clip"),Animation->TurnCount()>0
        &&Animation->CurrentAction()&&Animation->CurrentAction()->GetName().Contains(TEXT("Spin_Move")));
    Animation->Prepare(2,500,180,LDBodyMotion::EAction::Attack,.12f,.12f,2,nullptr,0,false,true);Evaluate();
    TestTrue(TEXT("Moving basic attacks retain locomotion legs and a one-handed pass"),Animation->UsesUpperBody()
        &&Animation->CurrentAction()&&(Animation->CurrentAction()->GetName()==TEXT("Hooper_Attack")||Animation->CurrentAction()->GetName().Contains(TEXT("Wraparound_Pass"))));
    Animation->Prepare(2.04,500,180,LDBodyMotion::EAction::None,0,.12f,0,nullptr,0,false,true);Evaluate();
    TestTrue(TEXT("A released ball finishes its presentation after the authority cue ends"),Animation->CurrentAction()!=nullptr);
    Animation->Prepare(2.5,0,180,LDBodyMotion::EAction::Attack,.01f,.12f,2.5,nullptr,0,false,true);Evaluate();
    Animation->Prepare(2.52,0,180,LDBodyMotion::EAction::None,0,.12f,0,nullptr,0,false,true);Evaluate();
    TestTrue(TEXT("Cancelled windup cannot create a local throw"),Animation->CurrentAction()==nullptr);
    Animation->Prepare(3,500,180,LDBodyMotion::EAction::Cast,.2f,.3f,3,nullptr,0,false,true);Evaluate();
    TestTrue(TEXT("Moving skills use the moving two-hand gather shot over running legs"),Animation->UsesUpperBody()
        &&Animation->CurrentAction()&&Animation->CurrentAction()->GetName().Contains(TEXT("Pull_Up_Jump_Shot")));
    Animation->Prepare(4,0,180,LDBodyMotion::EAction::Cast,.2f,.3f,4,nullptr,0,false,true);Evaluate();
    TestTrue(TEXT("Standing skills use a standing shot, not a generic reach"),!Animation->UsesUpperBody()
        &&Animation->CurrentAction()&&(Animation->CurrentAction()->GetName()==TEXT("Hooper_Cast")||Animation->CurrentAction()->GetName().Contains(TEXT("Free_Throw"))||Animation->CurrentAction()->GetName().Contains(TEXT("Jump_Shot"))));
    Visual->SetHiddenInGame(true,false);Evaluate();
    TestTrue(TEXT("Hiding only the parent immediately hides the ball even after a pending pose evaluates"),Ball->bHiddenInGame);
    Visual->SetHiddenInGame(false,false);
    Animation->Prepare(5,0,180,LDBodyMotion::EAction::None,0,.3f,0,Body->Find(ELDArtAnim::Death)->Sequence,0,false,false);Evaluate();
    TestTrue(TEXT("Falling bodies do not retain a floating ball"),Ball->bHiddenInGame);
    Visual->SetCue(1,.06f,.12f,false,true);Visual->Animate(FVector(500,0,0),.016f);Evaluate();
    TestTrue(TEXT("The production cue/Animate bridge preserves the runtime graph and activates the one-hand attack"),
        Mesh->GetAnimInstance()==Animation&&Animation->UsesUpperBody()&&Animation->CurrentAction()
        &&(Animation->CurrentAction()->GetName()==TEXT("Hooper_Attack")||Animation->CurrentAction()->GetName().Contains(TEXT("Wraparound_Pass"))));
    Visual->SetCue(2,.2f,.3f,false,true);Visual->SetCastAbility(1);Visual->Animate(FVector(500,0,0),.016f);Evaluate();
    TestTrue(TEXT("The production skill cue reaches the moving basketball shot"),Animation->CurrentAction()
        &&Animation->CurrentAction()->GetName().Contains(TEXT("Pull_Up_Jump_Shot")));
    Visual->PlayFall();Visual->Animate(FVector::ZeroVector,.016f);Evaluate();
    TestTrue(TEXT("The production death bridge hides the basketball"),Ball->bHiddenInGame);
    TestTrue(TEXT("Animation never changes the gameplay owner transform"),Before.Equals(Owner->GetActorTransform()));
    return true;
}
