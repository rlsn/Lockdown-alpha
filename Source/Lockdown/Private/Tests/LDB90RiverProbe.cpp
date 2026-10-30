#include "Tests/LDB90RiverProbe.h"
#include "Region/LDMapDefinition.h"
#include "Region/LDMapSurfaceSubsystem.h"
#include "Region/LDB90Preview.h"
#include "Character/LDCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "UnrealClient.h"
#include "DrawDebugHelpers.h"

ALDB90RiverProbe::ALDB90RiverProbe()
{ PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PostPhysics; }

bool ALDB90RiverProbe::Initialize()
{
    Controller=Cast<ALDB90PreviewController>(GetWorld()->GetFirstPlayerController());
    Body=Controller?Cast<ALDCharacter>(Controller->GetPawn()):nullptr;
    Definition=ULDMapDefinition::Get(GetWorld()); Surface=GetWorld()->GetSubsystem<ULDMapSurfaceSubsystem>();
    FParse::Value(FCommandLine::Get(),TEXT("B90Output="),Output);
    if(!Body||!Definition||!Surface||!Surface->Enabled()||Output.IsEmpty()) { return false; }
    Controller->PadsVisible=false;
    auto Nearest=[&](FVector Point,FVector& Centre,FVector& Tangent,double& Width)
    {
        double Best=DBL_MAX;
        for(const auto& R:Definition->Rivers) { for(int32 I=1;I<R.Points.Num();++I)
        {
            const FVector A=R.Points[I-1],D=R.Points[I]-A;
            const FVector P=A+D*FMath::Clamp(FVector::DotProduct(Point-A,D)/D.SizeSquared(),0.,1.);
            const double Dist=FVector::DistSquared2D(Point,P);
            if(Dist<Best) { Best=Dist; Centre=P; Tangent=D.GetSafeNormal2D(); Width=R.Width; }
        } }
    };
    // Every named crossing: four deck/underpass pairs and three ford pairs.
    for(const auto& P:Definition->POIs) { if(P.Kind==TEXT("crossing"))
    {
        FVector Centre,Tangent; double Width=0; Nearest(P.Position,Centre,Tangent,Width);
        const FVector Across(-Tangent.Y,Tangent.X,0);
        if(Definition->Bridges.Contains(P.Id))
        {
            FVector Deck=Centre; Deck.Z=Definition->NavigationTarget(P.Id).Z;
            Cases.Add({P.Id.ToString()+TEXT("_deck"),Deck,Across,1000,true,false,false});
            Cases.Add({P.Id.ToString()+TEXT("_under_wet"),Centre,Tangent,1000,false,true,false});
            Cases.Add({P.Id.ToString()+TEXT("_under_frozen"),Centre,Tangent,1000,true,true,false});
        }
        else { for(bool Ice:{false,true})
        { Cases.Add({P.Id.ToString()+(Ice?TEXT("_frozen"):TEXT("_wet")),Centre,Across,Width*.5+500,Ice,true,true}); } }
    } }
    // Every authored route/river intersection, including unmarked fords. Read
    // only the checksum-closed native payload, never the authoring docs at runtime.
    auto Cross=[](FVector A,FVector B) { return A.X*B.Y-A.Y*B.X; };
    for(const auto& Route:Definition->Routes) { for(const auto& River:Definition->Rivers)
    { for(int32 I=1;I<Route.Points.Num();++I) { for(int32 J=1;J<River.Points.Num();++J)
    {
        const FVector A=Route.Points[I-1],B=River.Points[J-1],V=Route.Points[I]-A,W=River.Points[J]-B;
        const double Den=Cross(V,W); if(FMath::Abs(Den)<.01) { continue; }
        const double T=Cross(B-A,W)/Den,U=Cross(B-A,V)/Den;
        if(T<0||T>1||U<0||U>1) { continue; }
        const FVector Centre=FMath::Lerp(B,River.Points[J],U),Tangent=W.GetSafeNormal2D(),Across(-Tangent.Y,Tangent.X,0);
        const FString Id=FString::Printf(TEXT("%s__%s_%d_%d"),*Route.Id.ToString(),*River.Id.ToString(),I,J);
        for(bool Ice:{false,true}) { Cases.Add({Id+(Ice?TEXT("_frozen"):TEXT("_wet")),Centre,Across,River.Width*.5+500,Ice,true,true}); }
    } } } }
    for(bool Ice:{false,true}) { Cases.Add({Ice?TEXT("heart_inset_frozen"):TEXT("heart_inset_wet"),Definition->WetInset,FVector(1,0,0),1100,Ice,true,true}); }
    Cases.Add({TEXT("heart_dry_circle"),Definition->Find(TEXT("heart"))->Position,FVector(1,0,0),200,false,false,false});
    Capture=FParse::Param(FCommandLine::Get(),TEXT("LDB90RiverCapture"));
    if(Capture) { Cases.RemoveAll([](const FCase& C){return C.Id!=TEXT("heart_inset_frozen");}); }
    FString Why; Setup=!Surface->SeedFrozenForProbe(Definition->Find(TEXT("heart"))->Position,FGuid::NewGuid(),0,Why);
    Trace=TEXT("case,elapsed_s,x_cm,y_cm,z_cm,speed_cm_s,max_speed_cm_s,water,wet,frozen,slow,stun\n");
    BeginCase(); return true;
}

void ALDB90RiverProbe::BeginCase()
{
    if(Index>=Cases.Num()) { Finish(); return; }
    const auto& C=Cases[Index]; Controller->StopNavigation(); Surface->ResetProbe();
    WetSamples=IceSamples=SlowSamples=StunSamples=FallingSamples=0; Peak=IcePeak=Length=0; IceAt=-1;
    PathStable=true; ExitWet=ExpiryClear=false;
    FVector Start=C.Centre-C.Direction*C.HalfLength; Goal=C.Centre+C.Direction*C.HalfLength;
    if(C.Water) { Start.Z=Definition->Height(FVector2D(Start)); Goal.Z=Definition->Height(FVector2D(Goal)); }
    FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(B90RiverFixture),false,Body);
    if(!GetWorld()->LineTraceSingleByChannel(Hit,Start+FVector(0,0,20),Start-FVector(0,0,100),ECC_GameTraceChannel1,Q))
    { EndCase(false,TEXT("No floor at fixture start")); return; }
    Body->SetActorLocation(Hit.ImpactPoint+FVector(0,0,92),false,nullptr,ETeleportType::TeleportPhysics);
    Body->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    Phase=1; PhaseAt=GetWorld()->GetTimeSeconds();
}

void ALDB90RiverProbe::Tick(float Delta)
{
    Super::Tick(Delta); const double Now=GetWorld()->GetTimeSeconds();
    if(Phase==0) { if(Now>2&&!Initialize()) { FPlatformMisc::RequestExitWithStatus(false,2); } return; }
    if(Phase==4) { return; }
    const auto& C=Cases[Index]; const int64 At=LDTactical::Milliseconds(Now);
    if(Phase==1&&Now-PhaseAt>.3)
    {
        const auto* Before=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Body->GetNavAgentLocation(),Goal);
        const TArray<FVector> Points=Before?Before->PathPoints:TArray<FVector>(); FString Why;
        FVector Water=C.Centre; if(!C.Water) { Water.Z=0; }
        if(C.Freeze&&!Surface->SeedFrozenForProbe(Water,FGuid::NewGuid(),At,Why)) { EndCase(false,Why); return; }
        const auto* After=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Body->GetNavAgentLocation(),Goal);
        PathStable=Before&&After&&Before->IsValid()&&After->IsValid()&&!Before->IsPartial()&&!After->IsPartial()&&Points==After->PathPoints;
        if(!Controller->NavigateTo(Goal)) { EndCase(false,TEXT("No native path")); return; }
        Phase=2; WalkAt=Now; Last=Body->GetActorLocation();
    }
    if(Phase==2)
    {
        Surface->Refresh(Body,At); const auto* Contact=Surface->Contact(Body); if(!Contact) { EndCase(false,TEXT("Missing contact")); return; }
        const double Speed=Body->GetVelocity().Size2D(); Peak=FMath::Max(Peak,Speed);
        Length+=FVector::Dist2D(Last,Body->GetActorLocation()); Last=Body->GetActorLocation();
        if(Contact->Wet) { ++WetSamples; }
        if(Contact->Frozen) { ++IceSamples; if(IceAt<0) { IceAt=Now; } if(Now-IceAt>.2) { IcePeak=FMath::Max(IcePeak,Speed); } }
        else { IceAt=-1; }
        if(Contact->State.SurfaceSlow) { ++SlowSamples; }
        if(Contact->State.Has(LDTactical::EStatus::Stun,At)) { ++StunSamples; }
        if(Body->GetCharacterMovement()->IsFalling()) { ++FallingSamples; }
        const FVector P=Body->GetActorLocation();
        if(Capture)
        {
            DrawDebugBox(GetWorld(),C.Centre+FVector(0,0,12),FVector(300,300,8),FColor::Cyan,false,0,0,4);
            if(!Shot&&IceSamples>=60)
            { FScreenshotRequest::RequestScreenshot(Output/TEXT("RiverContact.png"),true,false); Shot=true; }
        }
        Trace+=FString::Printf(TEXT("%s,%.6f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d\n"),*C.Id,Now-WalkAt,P.X,P.Y,P.Z,Speed,Body->GetCharacterMovement()->GetMaxSpeed(),Contact->Wet,Contact->State.Has(LDTactical::EStatus::Wet,At),Contact->Frozen,Contact->State.SurfaceSlow,Contact->State.Has(LDTactical::EStatus::Stun,At));
        if(!Controller->IsNavigating())
        {
            const bool ReachedRiverGoal=FVector::Dist2D(P,Goal)<60;
            if(!ReachedRiverGoal||!C.Residual) { EndCase(ReachedRiverGoal,ReachedRiverGoal?TEXT("Native arrival"):TEXT("Navigation stopped early")); return; }
            ExitWet=!Contact->Wet&&Contact->State.Has(LDTactical::EStatus::Wet,At);
            Phase=3; PhaseAt=Now;
        }
        else if(Now-WalkAt>25) { EndCase(false,TEXT("Walking timed out")); }
    }
    if(Phase==3&&Now-PhaseAt>3.2)
    { Surface->Refresh(Body,At); const auto* Contact=Surface->Contact(Body); ExpiryClear=Contact&&!Contact->State.Has(LDTactical::EStatus::Wet,At); EndCase(true,TEXT("Native exit and residual expiry")); }
}

void ALDB90RiverProbe::EndCase(bool ReachedRiverGoal,const FString& Why)
{
    const auto& C=Cases[Index]; Controller->StopNavigation();
    const bool IceOkay=C.Freeze&&C.Water?IceSamples>10&&IcePeak>=293&&IcePeak<=295:IceSamples==0&&SlowSamples==0;
    const bool Passed=Setup&&ReachedRiverGoal&&PathStable&&(C.Water?WetSamples>10:WetSamples==0)&&IceOkay&&StunSamples==0&&FallingSamples==0&&(!C.Residual||(ExitWet&&ExpiryClear));
    if(!Passed) { ++Failures; }
    auto R=MakeShared<FJsonObject>(); R->SetStringField(TEXT("id"),C.Id); R->SetBoolField(TEXT("passed"),Passed); R->SetStringField(TEXT("reason"),Why);
    R->SetBoolField(TEXT("arrived"),ReachedRiverGoal); R->SetBoolField(TEXT("frozen_fixture"),C.Freeze); R->SetBoolField(TEXT("water_expected"),C.Water);
    R->SetStringField(TEXT("centre_cm"),C.Centre.ToString());
    R->SetBoolField(TEXT("path_unchanged"),PathStable); R->SetBoolField(TEXT("residual_expected"),C.Residual); R->SetBoolField(TEXT("exit_wet"),ExitWet); R->SetBoolField(TEXT("expiry_clear"),ExpiryClear);
    R->SetNumberField(TEXT("wet_samples"),WetSamples); R->SetNumberField(TEXT("ice_samples"),IceSamples); R->SetNumberField(TEXT("slow_samples"),SlowSamples);
    R->SetNumberField(TEXT("stun_samples"),StunSamples); R->SetNumberField(TEXT("falling_samples"),FallingSamples);
    R->SetNumberField(TEXT("ice_steady_peak_cm_s"),IcePeak); R->SetNumberField(TEXT("peak_cm_s"),Peak); R->SetNumberField(TEXT("walked_cm"),Length);
    Results.Add(MakeShared<FJsonValueObject>(R)); UE_LOG(LogTemp,Display,TEXT("LD_B90_RIVER %s passed=%d %s wet=%d ice=%d speed=%.3f"),*C.Id,Passed,*Why,WetSamples,IceSamples,IcePeak);
    ++Index; BeginCase();
}
void ALDB90RiverProbe::Finish()
{
    Phase=4; auto R=MakeShared<FJsonObject>(); R->SetStringField(TEXT("stage"),TEXT("B90")); R->SetStringField(TEXT("map_sha256"),Definition->Checksum);
    R->SetStringField(TEXT("scope"),TEXT("Standalone actual native walking; diagnostic placements and Frost seeding; no paid casting or competitive match claimed"));
    R->SetBoolField(TEXT("complete"),true); R->SetBoolField(TEXT("dry_seed_rejected"),Setup); R->SetNumberField(TEXT("failures"),Failures); R->SetArrayField(TEXT("cases"),Results);
    R->SetBoolField(TEXT("rendered_heart_fixture"),Capture);
    FString Text; FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Text));
    FFileHelper::SaveStringToFile(Text,*(Output/TEXT("River.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    FFileHelper::SaveStringToFile(Trace,*(Output/TEXT("RiverTrace.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    FPlatformMisc::RequestExitWithStatus(false,Failures?3:0);
}
