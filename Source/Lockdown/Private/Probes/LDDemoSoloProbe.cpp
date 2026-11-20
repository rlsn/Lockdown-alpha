#include "Probes/LDDemoEntryProbe.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Character/LDCharacter.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputKeyEventArgs.h"
#include "Navigation/LDNavigationComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Region/LDMapRegistry.h"
#include "Serialization/JsonSerializer.h"
#include "UI/LDWidgets.h"
#include "UObject/UObjectIterator.h"
#include "Vision/LDWorldViewRelay.h"
#include "World/LDSpawnPlacement.h"

namespace LDDemoTests
{
class FSoloProbe:public TSharedFromThis<FSoloProbe>
{
public:
    TWeakObjectPtr<ALDPlayerController> Owner;
    FString Output,Error;
    TArray<TSharedPtr<FJsonValue>> Checks;
    double Began=FPlatformTime::Seconds(),At=0;
    int32 Stage=0,PanStage=0;
    uint32 Generation=0;
    FVector CameraBefore,BodyBefore;
    FKey PanKey;
    bool bReturned=false;
    bool Check(bool b,const TCHAR* Name)
    {
        auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("name"),Name);R->SetBoolField(TEXT("passed"),b);
        Checks.Add(MakeShared<FJsonValueObject>(R));if(!b){Error=Name;}return b;
    }
    bool Finish()
    {
        if(auto* PC=Owner.Get();PC&&PanKey.IsValid())
        {PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),PanKey,IE_Released,FPlatformTime::Cycles64()));}
        auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("passed"),Error.IsEmpty());R->SetStringField(TEXT("error"),Error);
        R->SetNumberField(TEXT("stage"),Stage);R->SetArrayField(TEXT("checks"),Checks);
        FString Text;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Text));
        FFileHelper::SaveStringToFile(Text,*Output,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        FPlatformMisc::RequestExit(false);return false;
    }
    bool Camera(ALDPlayerController& PC,const TCHAR* Label)
    {
        auto* Body=Cast<ALDCharacter>(PC.GetPawn());
        if(!Check(Body&&PC.GetViewTarget()==Body&&PC.CanCameraCommand()&&PC.CanWorldCommand(),Label)){return false;}
        if(PanStage==0)
        {
            CameraBefore=PC.PlayerCameraManager->GetCameraLocation();BodyBefore=Body->GetActorLocation();
            PanKey=PC.BindingKey(TEXT("PanRight"));
            PC.InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),PanKey,IE_Pressed,FPlatformTime::Cycles64()));
            At=FPlatformTime::Seconds();PanStage=1;return true;
        }
        if(FPlatformTime::Seconds()-At<.5){return true;}
        PC.InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),PanKey,IE_Released,FPlatformTime::Cycles64()));
        PanKey=FKey();
        if(!Check(FVector::Dist2D(CameraBefore,PC.PlayerCameraManager->GetCameraLocation())>25
            && BodyBefore.Equals(Body->GetActorLocation(),1.),TEXT("Camera pan key moves the actual camera without moving the body"))){return false;}
        PC.Navigation->Recenter(true);PC.Navigation->UpdateLocalCamera(.016f);PC.Navigation->Recenter(false);
        if(!Check(Body->CameraArm->GetComponentLocation().Equals(Body->PresentedLocation(),.01),TEXT("Recenter returns the camera arm to the owned body"))){return false;}
        PanStage=0;return true;
    }
    bool Tick(float Delta)
    {
        if(FPlatformTime::Seconds()-Began>100){Error=TEXT("Solo flow timed out");return Finish();}
        auto* PC=Owner.Get();auto* W=PC?PC->GetWorld():nullptr;
        auto* GS=W?W->GetGameState<ALDGameState>():nullptr;
        if(!PC||!GS||!PC->GetLocalPlayer()){return true;}
        if(GS->PublicMatch.Phase==ELDMatchPhase::Faulted){Error=TEXT("Solo match faulted");return Finish();}
        if(Stage==0)
        {
            if(GS->PublicMatch.Phase!=ELDMatchPhase::Lobby){return true;}
            if(!Check(GS->DemoLobby.Members.Num()==1&&GS->DemoLobby.AICount==0,TEXT("One local human and zero AI in the normal lobby"))){return Finish();}
            Generation=GS->PublicMatch.Generation;PC->SetDemoReady(true);Stage=1;At=FPlatformTime::Seconds();return true;
        }
        if(Stage==1)
        {
            if(FPlatformTime::Seconds()-At<.25){return true;}
            PC->StartDemoMatch();Stage=2;At=FPlatformTime::Seconds();return true;
        }
        if(Stage==2||Stage==5)
        {
            if(GS->PublicMatch.Phase!=ELDMatchPhase::Rest||!PC->IsWorldLifeValid()||FPlatformTime::Seconds()-At<.5){return true;}
            if(!Camera(*PC,TEXT("Solo opening rest owns an interactive body camera"))){return Finish();}
            if(PanStage!=0){return true;}
            if(Stage==5){return Finish();}
            PC->SubmitRestReady(true);Stage=3;At=FPlatformTime::Seconds();return true;
        }
        if(Stage==3)
        {
            if(GS->PublicMatch.Phase!=ELDMatchPhase::Development||FPlatformTime::Seconds()-At<1){return true;}
            if(!Camera(*PC,TEXT("Solo first field owns an interactive body camera"))){return Finish();}
            if(PanStage!=0){return true;}
            auto* Relay=PC->LocalView();const auto Frame=Relay?Relay->ReadObservedFrame():FLDObservedFrameRead();
            if(!Check(Frame.Frame&&Frame.Status==ELDObservedViewStatus::Current
                &&Frame.Frame->Footprint.CurrentBits.ContainsByPredicate([](uint8 B){return B!=0;}),TEXT("Solo field publishes a current nonempty visibility footprint"))){return Finish();}
            PC->EndDemoMatch();Stage=4;At=FPlatformTime::Seconds();return true;
        }
        if(Stage==4)
        {
            if(GS->PublicMatch.Phase==ELDMatchPhase::Results&&!bReturned)
            {bReturned=true;PC->ReturnDemoToLobby();return true;}
            if(GS->PublicMatch.Phase!=ELDMatchPhase::Lobby){return true;}
            if(!Check(GS->PublicMatch.Generation>Generation,TEXT("End match returns the solo room to a fresh lobby generation"))){return Finish();}
            PC->SetDemoReady(true);PC->StartDemoMatch();Stage=5;At=FPlatformTime::Seconds();return true;
        }
        return true;
    }
};
void StartSoloProbe(ALDPlayerController& PC)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("LDDemoSoloProbe"))||!PC.GetLocalPlayer()||!PC.HasAuthority()){return;}
    auto P=MakeShared<FSoloProbe>();P->Owner=&PC;
    FParse::Value(FCommandLine::Get(),TEXT("LDDemoSoloOutput="),P->Output);
    if(P->Output.IsEmpty()){P->Output=FPaths::ProjectSavedDir()/TEXT("SoloFlow.json");}
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([P](float D){return P->Tick(D);}));
}
}
#endif
