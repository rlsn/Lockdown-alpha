#include "Probes/LDVectorCastProbe.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Development/LDDevTestProtocol.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Framework/LDGameState.h"
#include "Framework/LDPlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Navigation/LDNavigationComponent.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

namespace LDDemoTests
{
class FVectorCastProbe:public TSharedFromThis<FVectorCastProbe>
{
public:
    struct FCase
    {
        FString Name;
        FKey Key;
        // Degrees of the drag on the ground (0 = east, 90 = south on the north-up screen); unset for a tap.
        TOptional<double> Angle;
    };
    TWeakObjectPtr<ALDPlayerController> Owner;
    FString Output;
    TArray<TSharedPtr<FJsonValue>> Checks,Shots;
    FString Error;
    double Began=FPlatformTime::Seconds();
    double At=0.;
    int32 Stage=0,Step=0,Case=0;
    bool bCapturing=false;
    FVector StartCm=FVector::ZeroVector;
    TArray<FCase> Cases;

    bool Check(bool bPassed,const FString& Name,const FString& Detail=FString())
    {
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),Name);Row->SetBoolField(TEXT("passed"),bPassed);
        if(!Detail.IsEmpty()){Row->SetStringField(TEXT("detail"),Detail);}
        Checks.Add(MakeShared<FJsonValueObject>(Row));
        if(!bPassed&&Error.IsEmpty()){Error=Name+TEXT(": ")+Detail;}
        return bPassed;
    }
    bool Finish()
    {
        auto Report=MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"),Error.IsEmpty());Report->SetStringField(TEXT("error"),Error);
        Report->SetNumberField(TEXT("stage"),Stage);Report->SetArrayField(TEXT("checks"),Checks);Report->SetArrayField(TEXT("screenshots"),Shots);
        FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));
        FFileHelper::SaveStringToFile(Text,*FPaths::Combine(Output,TEXT("VectorCast.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        FPlatformMisc::RequestExit(false);
        return false;
    }
    bool Dev(ALDPlayerController& PC,ELDDevTestOp Op,int64 Amount)
    {
        ELDGrowthReason Reason=ELDGrowthReason::None;
        return PC.SubmitDevTest(Op,Amount,Reason,NAME_None,0,0)!=0;
    }
    static void Key(ALDPlayerController& PC,const FKey& InKey,bool bDown)
    {
        PC.InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),InKey,bDown?IE_Pressed:IE_Released,FPlatformTime::Cycles64()));
    }
    // One screenshot at a time; true while it is still on its way.
    bool Capture(const FString& Name)
    {
        if(FScreenshotRequest::IsScreenshotRequested()){return true;}
        if(!bCapturing)
        {
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(Output,Name),true,false);
            Shots.Add(MakeShared<FJsonValueString>(Name));
            bCapturing=true;
            return true;
        }
        bCapturing=false;
        return false;
    }
    bool Point(ALDPlayerController& PC,const FVector& World)
    {
        FVector2D Screen;
        if(!PC.Navigation||!PC.ProjectWorldLocationToScreen(World,Screen,false)){return false;}
        PC.Navigation->DevSetPointer(Screen);
        return true;
    }
    static FVector Foot(const ALDCharacter& Body)
    {
        return Body.GetActorLocation()-FVector(0,0,Body.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    }

    bool Tick(float)
    {
        const double Now=FPlatformTime::Seconds();
        if(Now-Began>420.){Check(false,TEXT("The vector cast capture finished in time"),FString::Printf(TEXT("stage %d case %d step %d"),Stage,Case,Step));return Finish();}
        ALDPlayerController* PC=Owner.Get();
        UWorld* World=PC?PC->GetWorld():nullptr;
        ALDGameState* GS=World?World->GetGameState<ALDGameState>():nullptr;
        if(!PC||!GS||!PC->GetLocalPlayer()){return true;}
        const FLDPublicMatchView& Match=GS->PublicMatch;
        auto* Body=Cast<ALDCharacter>(PC->GetPawn());
        switch(Stage)
        {
        case 0:
            if(Match.Phase!=ELDMatchPhase::Lobby){return true;}
            PC->SetDemoReady(true);At=Now;Stage=1;return true;
        case 1:
            if(Now-At<.3){return true;}
            PC->StartDemoMatch();At=Now;Stage=2;return true;
        case 2:
            // The opening rest is skipped; the field holds still for the captures.
            if(Match.Phase==ELDMatchPhase::Development&&Body){At=Now;Stage=3;return true;}
            if(Match.Phase==ELDMatchPhase::Rest&&Now-At>2.){Dev(*PC,ELDDevTestOp::SkipPreparation,0);At=Now;}
            return true;
        case 3:
            // One DEV request at a time (the session refuses one sent while another is in flight).
            if(Now-At<.6){return true;}
            At=Now;
            switch(Step++)
            {
            case 0: Dev(*PC,ELDDevTestOp::SetCountdownPaused,1); return true;
            case 1: Dev(*PC,ELDDevTestOp::SetInvulnerable,1); return true;
            case 2: PC->ConsoleCommand(TEXT("ld.Dev.Learn active_cordon_tape Q"),true); return true;
            case 3: PC->ConsoleCommand(TEXT("ld.Dev.Learn active_desk_fan W"),true); return true;
            default: Stage=4;Case=0;Step=0;return true;
            }
        case 4:
            return RunCase(*PC,Body,Now);
        default:
            return Finish();
        }
    }

    bool RunCase(ALDPlayerController& PC,ALDCharacter* Body,double Now)
    {
        if(Case>=Cases.Num()){PC.Navigation->DevSetPointer(TOptional<FVector2D>());Stage=5;return Finish();}
        if(!Body){return true;}
        const FCase& This=Cases[Case];
        switch(Step)
        {
        case 0:
            if(Now-At<1.){return true;}
            Dev(PC,ELDDevTestOp::ResetCooldowns,0);
            At=Now;Step=7;return true;
        case 7:
            if(Now-At<.6){return true;}
            Dev(PC,ELDDevTestOp::RefillResources,0);
            // The start: 3 m north of the body, inside both skills' reach.
            StartCm=Foot(*Body)+FVector(0,-300,0);
            Check(Point(PC,StartCm),This.Name+TEXT(": the start projects onto the screen"));
            At=Now;Step=1;return true;
        case 1:
            if(Now-At<.5){return true;}
            Key(PC,This.Key,true);At=Now;Step=2;return true;
        case 2:
            // Held past the QuickCast preview threshold, the pointer drags along the case's angle.
            if(Now-At<.3){return true;}
            if(This.Angle){Point(PC,StartCm+FRotator(0,*This.Angle,0).Vector()*260.);}
            At=Now;Step=3;return true;
        case 3:
            if(Now-At<.4){return true;}
            {
                ELDCombatEntry Entry=ELDCombatEntry::None;FVector Start;
                const bool bAim=PC.ReadVectorAim(Entry,Start);
                Check(bAim&&FVector::Dist2D(Start,StartCm)<40.,This.Name+TEXT(": the press holds its start as the vector aim"),
                    FString::Printf(TEXT("aim=%d start=%s expected=%s"),bAim?1:0,*Start.ToCompactString(),*StartCm.ToCompactString()));
            }
            Step=4;return true;
        case 4:
            if(Capture(FString::Printf(TEXT("Vector_%s_aim.png"),*This.Name))){return true;}
            Key(PC,This.Key,false);At=Now;Step=5;return true;
        case 5:
            if(Now-At<1.){return true;}
            Step=6;return true;
        case 6:
            if(Capture(FString::Printf(TEXT("Vector_%s_placed.png"),*This.Name))){return true;}
            At=Now;Step=0;++Case;return true;
        default:
            return true;
        }
    }
};

void StartVectorCastProbe(ALDPlayerController& PC)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("LDVectorCastProbe"))||!PC.GetLocalPlayer()||!PC.HasAuthority()){return;}
    auto Probe=MakeShared<FVectorCastProbe>();Probe->Owner=&PC;
    FParse::Value(FCommandLine::Get(),TEXT("LDVectorCastOutput="),Probe->Output);
    if(Probe->Output.IsEmpty()){Probe->Output=FPaths::ProjectSavedDir()/TEXT("VectorCast");}
    Probe->Cases={{TEXT("CordonTap"),EKeys::Q,{}},{TEXT("Cordon0"),EKeys::Q,0.},{TEXT("Cordon60"),EKeys::Q,60.},
        {TEXT("Cordon135"),EKeys::Q,135.},{TEXT("FanTap"),EKeys::W,{}},{TEXT("Fan45"),EKeys::W,45.},{TEXT("Fan200"),EKeys::W,200.}};
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float Delta){return Probe->Tick(Delta);}));
}
}
#endif
