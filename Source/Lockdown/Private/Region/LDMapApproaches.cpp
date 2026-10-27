#include "Region/LDMapApproaches.h"
#include "Region/LDMapDefinition.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Dom/JsonObject.h"
#include "Algo/Reverse.h"

namespace LDMapApproaches
{
double Length(const TArray<FVector>& P)
{ double L=0; for(int32 I=1;I<P.Num();++I) { L+=FVector::Distance(P[I-1],P[I]); } return L; }
FVector AtRemaining(const TArray<FVector>& P,double Remaining)
{
    if(P.IsEmpty()) { return FVector::ZeroVector; }
    double Left=FMath::Max(0.,Remaining);
    for(int32 I=P.Num()-1;I>0;--I)
    { const double L=FVector::Distance(P[I],P[I-1]); if(L>UE_SMALL_NUMBER&&Left<=L) { return FMath::Lerp(P[I],P[I-1],Left/L); } Left-=L; }
    return P[0];
}
TArray<FVector> Tail(const TArray<FVector>& P,double Distance)
{
    if(P.IsEmpty()||!FMath::IsFinite(Distance)||Distance<0) { return {}; }
    TArray<FVector> Result; Result.Add(P.Last()); double Left=Distance;
    for(int32 I=P.Num()-1;I>0&&Left>0;--I)
    {
        const double L=FVector::Distance(P[I],P[I-1]); if(L<=UE_SMALL_NUMBER) { continue; }
        Result.Add(FMath::Lerp(P[I],P[I-1],FMath::Min(1.,Left/L))); Left-=L;
    }
    Algo::Reverse(Result); return Result;
}
bool KeyTarget(FName Id,const FString& Kind)
{ return Kind==TEXT("ruin")||Kind==TEXT("boss")||Kind==TEXT("heart")||Id==TEXT("heart_forge")||Id==TEXT("ironworks_forge"); }
FRay StaticRay(UWorld* W,const FVector& A,const FVector& B,double Radius)
{
    FRay R;
    if(!W||!ULDMapDefinition::InBounds(A)||!ULDMapDefinition::InBounds(B)||!FMath::IsFinite(Radius)||Radius<0) { return R; }
    if(FVector::DistSquared2D(A,B)>Radius*Radius) { R.Result=ERay::Range; return R; }
    const FVector EyeA=A+FVector(0,0,120),EyeB=B+FVector(0,0,120);
    FHitResult Wall,Floor; FCollisionQueryParams Q(SCENE_QUERY_STAT(LDMapStaticSight),false);
    const bool HitWall=W->LineTraceSingleByChannel(Wall,EyeA,EyeB,ECC_GameTraceChannel2,Q);
    const bool HitFloor=W->LineTraceSingleByChannel(Floor,EyeA,EyeB,ECC_GameTraceChannel1,Q);
    const auto* Hit=HitWall&&(!HitFloor||Wall.Time<=Floor.Time)?&Wall:HitFloor?&Floor:nullptr;
    if(!Hit) { R.Result=ERay::Clear; return R; }
    R.Result=ERay::Blocked; R.Hit=Hit->ImpactPoint;
    if(const auto* Actor=Hit->GetActor()) { R.Blocker=Actor->Tags.IsEmpty()?Actor->GetFName():Actor->Tags[0]; }
    return R;
}
bool Collect(UWorld* W,const ULDMapDefinition* D,TArray<FTarget>& Targets,FString& Error)
{
    Targets.Reset(); if(!W||!D||!D->Validate(Error)) { return false; }
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);
    if(!Nav) { Error=TEXT("Native navigation unavailable"); return false; }
    auto Floor=[&](FVector P,FVector& Out)
    {
        FHitResult H;
        // Path corner Z is interpolated over potentially long segments; native
        // walking follows the local nav surface, not that interpolated chord.
        FNavLocation Local;
        if(!Nav->ProjectPointToNavigation(P,Local,FVector(60,60,900))) { return false; }
        P.Z=Local.Location.Z;
        if(!W->LineTraceSingleByChannel(H,P+FVector(0,0,50),P-FVector(0,0,120),ECC_GameTraceChannel1)
           ||H.ImpactNormal.Z<FMath::Cos(FMath::DegreesToRadians(40.))) { return false; }
        Out=H.ImpactPoint; return true;
    };
    TArray<FTarget> Candidate;
    for(const auto& POI:D->POIs) { if(KeyTarget(POI.Id,POI.Kind))
    {
        FTarget Target; Target.Id=POI.Id; Target.Goal=D->NavigationTarget(POI.Id);
        for(const auto& Route:D->Routes)
        {
            if(Route.From!=POI.Id&&Route.To!=POI.Id) { continue; }
            FBranch Branch; Branch.From=Route.From==POI.Id?Route.To:Route.From; Branch.Route=Route.Id;
            const auto* Native=UNavigationSystemV1::FindPathToLocationSynchronously(W,D->NavigationTarget(Branch.From),Target.Goal);
            if(!Native||!Native->IsValid()||Native->IsPartial()) { Error=TEXT("Incomplete approach: ")+Route.Id.ToString(); return false; }
            Branch.FullLength=Length(Native->PathPoints);
            const auto MacroTail=Tail(Native->PathPoints,6000); FVector Start;
            if(!Floor(MacroTail[0],Start)) { Error=TEXT("Missing approach start: ")+Route.Id.ToString(); return false; }
            // Recast may choose a shorter local corridor from a trimmed start.
            // Inspect the same fresh local order used by the actual walk fixture.
            const auto* Local=UNavigationSystemV1::FindPathToLocationSynchronously(W,Start+FVector(0,0,2.15),Target.Goal);
            if(!Local||!Local->IsValid()||Local->IsPartial()) { Error=TEXT("Incomplete local approach: ")+Route.Id.ToString(); return false; }
            const auto& Raw=Local->PathPoints; const double L=Length(Raw); Branch.LocalLength=L;
            const int32 Steps=FMath::Max(1,FMath::CeilToInt(L/100));
            for(int32 I=0;I<=Steps;++I)
            { const FVector P=AtRemaining(Raw,L*(1-double(I)/Steps)); FVector Ground;
              if(!Floor(P,Ground)) { Error=TEXT("Missing approach floor: ")+Route.Id.ToString()+TEXT(" at ")+P.ToString(); return false; } Branch.Points.Add(Ground); }
            Target.Branches.Add(MoveTemp(Branch));
        }
        for(int32 A=0;A<Target.Branches.Num();++A) { for(int32 B=A+1;B<Target.Branches.Num();++B)
        {
            FPair Pair; Pair.A=A; Pair.B=B;
            const double Limit=FMath::Min(Length(Target.Branches[A].Points),Length(Target.Branches[B].Points));
            int32 Separate=0;
            for(double Distance:{900.,1800.,2400.,3000.})
            {
                if(Distance>Limit) { continue; }
                FSample S; S.Remaining=Distance;
                S.A=AtRemaining(Target.Branches[A].Points,Distance); S.B=AtRemaining(Target.Branches[B].Points,Distance);
                S.Separation=FVector::Dist2D(S.A,S.B); S.Body=StaticRay(W,S.A,S.B,1800); S.Watch=StaticRay(W,S.A,S.B,2400);
                if((Distance==1800||Distance==3000)&&S.Separation>=600) { ++Separate; }
                Pair.Samples.Add(S);
            }
            Pair.Distinct=Separate==2; Target.Alternative|=Pair.Distinct; Target.Pairs.Add(MoveTemp(Pair));
        } }
        Candidate.Add(MoveTemp(Target));
    } }
    Targets=MoveTemp(Candidate); Error.Reset(); return true;
}
TSharedRef<FJsonObject> Report(const ULDMapDefinition* D,const TArray<FTarget>& Targets)
{
    auto Root=MakeShared<FJsonObject>(); Root->SetStringField(TEXT("payload_sha256"),D->Checksum);
    Root->SetStringField(TEXT("scope"),TEXT("Public static geography; native final60m paths and floor-relative geometry rays. Not unit observation, ledge/smoke/concealment or restricted-client authorization."));
    Root->SetNumberField(TEXT("body_radius_cm"),1800); Root->SetNumberField(TEXT("watch_radius_cm"),2400);
    Root->SetNumberField(TEXT("eye_height_cm"),120); Root->SetNumberField(TEXT("distinct_corridor_separation_cm"),600);
    auto RayName=[](ERay R) { return R==ERay::Clear?TEXT("clear"):R==ERay::Blocked?TEXT("geometry_blocked"):R==ERay::Range?TEXT("out_of_range"):TEXT("invalid"); };
    auto RayJson=[&](const FRay& R) { auto J=MakeShared<FJsonObject>(); J->SetStringField(TEXT("result"),RayName(R.Result)); J->SetStringField(TEXT("blocker"),R.Blocker.ToString()); J->SetStringField(TEXT("hit_cm"),R.Hit.ToString()); return J; };
    TArray<TSharedPtr<FJsonValue>> Rows;
    for(const auto& T:Targets)
    {
        auto J=MakeShared<FJsonObject>(); J->SetStringField(TEXT("target"),T.Id.ToString()); J->SetStringField(TEXT("goal_cm"),T.Goal.ToString()); J->SetBoolField(TEXT("has_alternative"),T.Alternative);
        TArray<TSharedPtr<FJsonValue>> Branches,Pairs;
        for(const auto& B:T.Branches)
        { auto V=MakeShared<FJsonObject>(); V->SetStringField(TEXT("from"),B.From.ToString()); V->SetStringField(TEXT("route"),B.Route.ToString()); V->SetNumberField(TEXT("full_native_path_cm"),B.FullLength); V->SetNumberField(TEXT("local_native_path_cm"),B.LocalLength);
          TArray<TSharedPtr<FJsonValue>> Points; for(FVector P:B.Points) { Points.Add(MakeShared<FJsonValueString>(P.ToString())); } V->SetArrayField(TEXT("points_cm"),Points); Branches.Add(MakeShared<FJsonValueObject>(V)); }
        for(const auto& P:T.Pairs)
        { auto V=MakeShared<FJsonObject>(); V->SetNumberField(TEXT("a"),P.A); V->SetNumberField(TEXT("b"),P.B); V->SetBoolField(TEXT("distinct"),P.Distinct);
          TArray<TSharedPtr<FJsonValue>> Samples;
          for(const auto& S:P.Samples) { auto Q=MakeShared<FJsonObject>(); Q->SetNumberField(TEXT("remaining_cm"),S.Remaining); Q->SetNumberField(TEXT("separation_cm"),S.Separation);
            Q->SetStringField(TEXT("a_cm"),S.A.ToString()); Q->SetStringField(TEXT("b_cm"),S.B.ToString()); Q->SetObjectField(TEXT("body"),RayJson(S.Body)); Q->SetObjectField(TEXT("watch"),RayJson(S.Watch)); Samples.Add(MakeShared<FJsonValueObject>(Q)); }
          V->SetArrayField(TEXT("samples"),Samples); Pairs.Add(MakeShared<FJsonValueObject>(V)); }
        J->SetArrayField(TEXT("branches"),Branches); J->SetArrayField(TEXT("pairs"),Pairs); Rows.Add(MakeShared<FJsonValueObject>(J));
    }
    Root->SetArrayField(TEXT("targets"),Rows); return Root;
}
}
