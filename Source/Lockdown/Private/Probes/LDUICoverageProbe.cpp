#include "Probes/LDUICoverageProbe.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/WidgetTree.h"
#include "Components/EditableTextBox.h"
#include "Containers/Ticker.h"
#include "Development/LDDevTestProtocol.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Framework/LDGameState.h"
#include "Character/LDCharacter.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Combat/LDCombatElements.h"
#include "Framework/LDPlayerController.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UI/LDControlSettings.h"
#include "UI/LDControlsPanel.h"
#include "UI/LDHUD.h"
#include "UI/LDUIRoot.h"
#include "UI/LDWidgets.h"
#include "UI/LDWorkshop.h"
#include "UnrealClient.h"
#include "Probes/LDSlateProbeInput.h"
#include "World/LDCampMember.h"
#include "Navigation/LDNavigationComponent.h"
#include "Framework/Application/SlateApplication.h"

namespace LDDemoTests
{
class FUICoverageProbe:public TSharedFromThis<FUICoverageProbe>
{
public:
    TWeakObjectPtr<ALDPlayerController> Owner;
    FString Output;
    TArray<TSharedPtr<FJsonValue>> Checks,Shots;
    FString Error;
    double Began=FPlatformTime::Seconds();
    double PageAt=0.,Due=0.;
    int32 Page=0,Combo=0,Step=0;
    int32 Width=0,Height=0;
    bool bCapturing=false,bFrontend=false,bClient=false;
    TArray<FString> Missed;

    // One page: Enter runs every tick until it reports the page is up (or the page times out and is listed as missed).
    struct FPage
    {
        FString Name;
        TFunction<bool(ALDPlayerController&,double)> Enter;
        double Timeout=30.;
        bool bAllSizes=true;
        // >= 0: this page is shot at that one combo only, which Enter applies itself before it opens the page.
        int32 Only=-1;
    };
    TArray<FPage> Pages;
    struct FCombo {int32 W,H;float Text;const TCHAR* Tag;};
    const FCombo Combos[4]={{1920,1080,1.f,TEXT("1080_100")},{1920,1080,1.25f,TEXT("1080_125")},{1280,720,1.f,TEXT("720_100")},{1280,720,1.25f,TEXT("720_125")}};

    void Check(bool bPassed,const FString& Name,const FString& Detail=FString())
    {
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),Name);Row->SetBoolField(TEXT("passed"),bPassed);
        if(!Detail.IsEmpty()){Row->SetStringField(TEXT("detail"),Detail);}
        Checks.Add(MakeShared<FJsonValueObject>(Row));
        if(!bPassed&&Error.IsEmpty()){Error=Name+TEXT(": ")+Detail;}
    }
    bool Finish()
    {
        auto Report=MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"),Error.IsEmpty());Report->SetStringField(TEXT("error"),Error);
        Report->SetBoolField(TEXT("frontend"),bFrontend);
        Report->SetArrayField(TEXT("checks"),Checks);Report->SetArrayField(TEXT("screenshots"),Shots);
        TArray<TSharedPtr<FJsonValue>> MissedRows;for(const FString& Name:Missed){MissedRows.Add(MakeShared<FJsonValueString>(Name));}
        Report->SetArrayField(TEXT("missed"),MissedRows);
        FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));
        FFileHelper::SaveStringToFile(Text,*FPaths::Combine(Output,bFrontend?TEXT("UICoverage-Frontend.json"):bClient?TEXT("UICoverage-Client.json"):TEXT("UICoverage-Match.json")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        FPlatformMisc::RequestExit(false);
        return false;
    }
    static ULDCommandButton* FindButton(UUserWidget* Widget,FName Name)
    {
        return Widget&&Widget->WidgetTree?Cast<ULDCommandButton>(Widget->WidgetTree->FindWidget(Name)):nullptr;
    }
    // A product button pressed through its own click delegate; false while it is missing, hidden or disabled.
    static bool Click(UUserWidget* Widget,FName Name)
    {
        ULDCommandButton* Button=FindButton(Widget,Name);
        if(!Button||!Button->GetIsEnabled()||!Button->IsVisible()){return false;}
        Button->OnClicked.Broadcast();
        return true;
    }
    bool Dev(ALDPlayerController& PC,ELDDevTestOp Op,int64 Amount,FName Definition=NAME_None)
    {
        ELDGrowthReason Reason=ELDGrowthReason::None;
        return PC.SubmitDevTest(Op,Amount,Reason,Definition,0,0)!=0;
    }
    static void Key(ALDPlayerController& PC,const FKey& InKey,bool bDown)
    {
        if(InKey.IsValid()){PC.InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),InKey,bDown?IE_Pressed:IE_Released,FPlatformTime::Cycles64()));}
    }
    void Resize(ALDPlayerController& PC,int32 InWidth,int32 InHeight)
    {
        if(InWidth==Width&&InHeight==Height){return;}
        Width=InWidth;Height=InHeight;
        PC.ConsoleCommand(FString::Printf(TEXT("r.SetRes %dx%dw"),Width,Height),true);
        Due=FMath::Max(Due,FPlatformTime::Seconds()+.8);
    }
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
    static const FLDPublicMatchView* Match(ALDPlayerController& PC)
    {
        const ALDGameState* GS=PC.GetWorld()?PC.GetWorld()->GetGameState<ALDGameState>():nullptr;
        return GS?&GS->PublicMatch:nullptr;
    }
    static bool InPhase(ALDPlayerController& PC,ELDMatchPhase Phase)
    {
        const FLDPublicMatchView* View=Match(PC);
        return View&&View->Phase==Phase;
    }
    // True once per page for each Id, at or after At seconds into the page: every product action fires exactly once.
    TSet<int32> Fired;
    bool Once(double Now,int32 Id,double At=0.)
    {
        if(Now-PageAt<At||Fired.Contains(Id)){return false;}
        Fired.Add(Id);return true;
    }
    // Waits Seconds after the page's first Enter call (a settle time for transitions and fades).
    bool Settled(double Now,double Seconds) const {return Now-PageAt>=Seconds;}

    void PlanFrontend()
    {
        // A fresh UserDir opens 怎么玩 by itself the first time the main menu shows.
        Pages.Add({TEXT("Front_HowTo"),[this](ALDPlayerController& PC,double Now){return PC.RootUI&&Settled(Now,3.);}});
        Pages.Add({TEXT("Front_Menu"),[this](ALDPlayerController& PC,double Now)
            {if(Once(Now,0)){Click(PC.RootUI,TEXT("HowToBack"));}return Settled(Now,1.);}});
        Pages.Add({TEXT("Front_MenuHover"),[this](ALDPlayerController& PC,double Now)
            {
                ULDCommandButton* Start=FindButton(PC.RootUI,TEXT("Start"));
                if(Start&&Once(Now,0)){LDSlateProbeInput::Pointer(Start,EKeys::LeftMouseButton,false,false);}
                return Settled(Now,.6);
            }});
        Pages.Add({TEXT("Front_Codex"),[this](ALDPlayerController& PC,double Now){if(Once(Now,0)){Click(PC.RootUI,TEXT("ChainCodex"));}return Settled(Now,1.);}});
        // The start page holds text boxes, which must not restyle while shown: each size closes it, switches resolution and
        // text scale, then reopens it.
        for(int32 Index=0;Index<4;++Index)
        {
            FPage& Start=Pages.Add_GetRef({TEXT("Front_Start"),[this,Index](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0)){Click(PC.RootUI,TEXT("ChainCodexBack"));Click(PC.RootUI,TEXT("StartBack"));}
                if(Once(Now,4,.3)){Resize(PC,Combos[Index].W,Combos[Index].H);if(PC.Controls){PC.Controls->TextScale=Combos[Index].Text;}}
                if(Once(Now,2,1.2))
                {
                    if(auto* Name=Cast<UEditableTextBox>(PC.RootUI->WidgetTree->FindWidget(TEXT("PlayerName")))){Name->SetText(FText::FromString(TEXT("雾港街溜子")));}
                }
                if(Once(Now,1,1.5)){Click(PC.RootUI,TEXT("Start"));}
                if(Once(Now,3,1.8)){if(ULDCommandButton* Host=FindButton(PC.RootUI,TEXT("Host"))){Host->SetKeyboardFocus();}}
                return Settled(Now,2.8);
            }});
            Start.Only=Index;
        }
        Pages.Add({TEXT("Front_Exit"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0)){Click(PC.RootUI,TEXT("StartBack"));}
                if(Once(Now,1,.6)){Click(PC.RootUI,TEXT("Exit"));}
                return Settled(Now,1.4);
            }});
        AddSettings(TEXT("Front"),[](ALDPlayerController& PC){if(PC.RootUI){PC.RootUI->CloseTopModal();}return Click(PC.RootUI,TEXT("Settings"));});
    }
    // The six settings pages through their own tabs; Open opens the panel once.
    void AddSettings(const FString& Prefix,TFunction<bool(ALDPlayerController&)> Open)
    {
        static const TCHAR* Tabs[]={TEXT("TabDisplay"),TEXT("TabAudio"),TEXT("TabControls"),TEXT("TabCamera"),TEXT("TabInterface"),TEXT("TabEffects")};
        for(int32 Index=0;Index<6;++Index)
        {
            const FString Tab=Tabs[Index];
            Pages.Add({Prefix+TEXT("_Settings_")+Tab.RightChop(3),[this,Tab,Index,Open](ALDPlayerController& PC,double Now)
                {
                    if(Index==0&&Once(Now,0)){Open(PC);}
                    if(Once(Now,1,.5)){Click(PC.ControlsUI,*Tab);}
                    return PC.ControlsUI&&PC.ControlsUI->IsOpen()&&Settled(Now,1.2);
                }});
        }
    }
    void PlanMatch()
    {
        Pages.Add({TEXT("Lobby_Solo"),[this](ALDPlayerController& PC,double Now){return PC.RootUI&&InPhase(PC,ELDMatchPhase::Lobby)&&Settled(Now,4.);}});
        // With the guest client (CaptureUICoverage.ps1 starts one): the host's view while the guest is not ready (cannot
        // start, with the reason), then once it is (can start); the room is filled to eight after the guest leaves.
        Pages.Add({TEXT("Lobby_HostWait"),[this](ALDPlayerController& PC,double Now)
            {if(Once(Now,0)){PC.SetDemoAICount(2);}return Guests(PC)==1&&!GuestReady(PC)&&Settled(Now,3.);},120.});
        Pages.Add({TEXT("Lobby_HostCanStart"),[this](ALDPlayerController& PC,double Now)
            {return Guests(PC)==1&&GuestReady(PC)&&Settled(Now,1.);},90.});
        Pages.Add({TEXT("Lobby_Full"),[this](ALDPlayerController& PC,double Now)
            {
                if(Guests(PC)!=0){return false;}
                if(Once(Now,0)){PC.SetDemoAICount(7);}
                return Settled(Now,2.5);
            },90.});
        Pages.Add({TEXT("Lobby_Blueprint"),[this](ALDPlayerController& PC,double Now)
            {if(Once(Now,0)){Click(PC.RootUI,TEXT("LobbyBuildBlueprint"));}return Settled(Now,1.2);}});
        AddSettings(TEXT("Lobby"),[](ALDPlayerController& PC)
            {if(PC.RootUI){PC.RootUI->CloseTopModal();PC.RootUI->CloseTopPanel();}return Click(PC.RootUI,TEXT("LobbySettings"));});
        Pages.Add({TEXT("Match_RestOpening"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0)&&PC.RootUI){PC.RootUI->CloseTopModal();PC.RootUI->CloseTopPanel();}
                if(Once(Now,1,.5)){PC.SetDemoReady(true);PC.StartDemoMatch();}
                if(InPhase(PC,ELDMatchPhase::Lobby)&&Once(Now,2,4.)){PC.StartDemoMatch();}
                return InPhase(PC,ELDMatchPhase::Rest)&&Settled(Now,8.);
            },60.});
        Pages.Add({TEXT("Match_RestRingClosed"),[this](ALDPlayerController& PC,double Now)
            {
                ULDWorkshop* Ring=PC.HUDUI?PC.HUDUI->Workshop.Get():nullptr;
                if(Ring&&Ring->IsOpen()&&Once(Now,0)){PC.HUDUI->ToggleWorkshop();}
                return Settled(Now,1.);
            }});
        // The Lobby's seat portraits (SourceData/UI/lobby_seat_N.png): eight UI-free shots of the host's own body in the
        // opening rest, each held at a different still of the basketball clips (ULDHumanoidVisualComponent::HoldPose)
        // and seen from a different side, from above so the plain floor is the whole backdrop, through the DEV art-capture
        // camera. Scripts/MakeLobbySeats.py cuts them out and paints each seat's jersey colour.
        struct FSeatPose{const TCHAR* Clip;float Time;float Yaw;};
        static const FSeatPose SeatPoses[8]={
            {TEXT("Hooper_Idle"),0.f,-14.f},
            {TEXT("KB_012_AA_Basketball_PlayerA_Basic_Stance_Type01_LH_Loop"),.2f,16.f},
            {TEXT("KB_043_AA_Basketball_PlayerA_Between_The_Legs_Type01_Loop"),.4f,-24.f},
            {TEXT("KB_098_AA_Basketball_PlayerA_Spin_Move_RH_Loop"),.7f,202.f},
            {TEXT("KB_149_AA_Basketball_PlayerA_Free_Throw_LH"),.5f,-8.f},
            {TEXT("KB_044_AA_Basketball_PlayerA_Between_The_Legs_Type02_Loop"),1.3f,28.f},
            {TEXT("KB_152_AA_Basketball_PlayerA_Jump_Shot_RH"),.35f,-20.f},
            {TEXT("KB_041_AA_Basketball_PlayerA_Between_The_Legs_RH_To_LH"),.3f,10.f}};
        for(int32 Seat=0;Seat<8;++Seat)
        {
            FPage& Shot=Pages.Add_GetRef({FString::Printf(TEXT("Ref_Seat%d"),Seat+1),[this,Seat](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0))
                {
                    Resize(PC,1920,1080);
                    // The rest's clock stands still while the portraits are shot; Match_StageCount lets it run again.
                    if(Seat==0){Dev(PC,ELDDevTestOp::SetCountdownPaused,1);}
                    ALDCharacter* Pick=Cast<ALDCharacter>(PC.GetPawn());
                    if(!Pick){return true;}
                    // The body stands alone: every other body is hidden until
                    // Ref_LineupEnd. The camera stands on the side whose far side is open floor, not a wall.
                    for(TActorIterator<AActor> It(PC.GetWorld());It;++It)
                    {
                        // Rivals, their observed stand-ins and the training dummies alike: whatever wears a skeleton.
                        if(*It!=Pick&&!It->IsHidden()&&It->FindComponentByClass<USkeletalMeshComponent>()){It->SetActorHiddenInGame(true);HiddenBodies.Add(*It);}
                    }
                    FVector Side=FVector::ForwardVector;
                    {
                        const FVector Chest=Pick->GetActorLocation();
                        double Best=-1.;
                        for(int32 Step=0;Step<12;++Step)
                        {
                            const FVector Try=FRotator(0.f,Step*30.f,0.f).Vector();
                            FHitResult Hit;FCollisionQueryParams Params;Params.AddIgnoredActor(Pick);
                            const bool bHit=PC.GetWorld()->LineTraceSingleByChannel(Hit,Chest,Chest-Try*1400.f,ECC_Visibility,Params);
                            const double Open=bHit?Hit.Distance:1400.;
                            if(Open>Best){Best=Open;Side=Try;}
                        }
                    }
                    const FSeatPose& Pose=SeatPoses[Seat];
                    Pick->SetActorRotation(FRotator(0.f,Side.Rotation().Yaw-Pose.Yaw,0.f));
                    const FString Path=FString::Printf(TEXT("/Game/Lockdown/Art/Characters/hooper/%s.%s"),Pose.Clip,Pose.Clip);
                    if(auto* Visual=Pick->FindComponentByClass<ULDHumanoidVisualComponent>())
                    {
                        if(UAnimSequence* Still=LoadObject<UAnimSequence>(nullptr,*Path)){Visual->HoldPose(Still,Pose.Time);PosedBody=Pick;}
                    }
                    const FVector Foot=Pick->GetActorLocation()-FVector(0,0,90.f);
                    const FVector Eye=Foot+Side*560.f+FVector(0,0,380.f);
                    FActorSpawnParameters Spawn;Spawn.ObjectFlags|=RF_Transient;
                    if(!LineupCamera.IsValid()){LineupCamera=PC.GetWorld()->SpawnActor<ACameraActor>(Spawn);}
                    if(ACameraActor* Cam=LineupCamera.Get())
                    {
                        Cam->SetActorLocationAndRotation(Eye,(Foot+FVector(0,0,112.f)-Eye).Rotation());
                        Cam->GetCameraComponent()->SetFieldOfView(24.f);Cam->GetCameraComponent()->bConstrainAspectRatio=false;
                        PC.SetDevArtCaptureCamera(Cam);
                    }
                    if(PC.RootUI&&PC.RootUI->GetVisibility()!=ESlateVisibility::Hidden){RootShown=PC.RootUI->GetVisibility();PC.RootUI->SetVisibility(ESlateVisibility::Hidden);}
                    if(PC.HUDUI&&PC.HUDUI->GetVisibility()!=ESlateVisibility::Hidden){HudShown=PC.HUDUI->GetVisibility();PC.HUDUI->SetVisibility(ESlateVisibility::Hidden);}
                }
                return Settled(Now,1.5);
            },10.});
            Shot.Only=0;
        }
        Pages.Add({TEXT("Ref_LineupEnd"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0))
                {
                    PC.SetDevArtCaptureCamera(nullptr);
                    if(ALDCharacter* Body=PosedBody.Get())
                    {
                        if(auto* Visual=Body->FindComponentByClass<ULDHumanoidVisualComponent>()){Visual->HoldPose(nullptr,0.f);}
                    }
                    for(const TWeakObjectPtr<AActor>& Body:HiddenBodies){if(Body.IsValid()){Body->SetActorHiddenInGame(false);}}
                    HiddenBodies.Reset();
                    if(PC.RootUI){PC.RootUI->SetVisibility(RootShown);}
                    if(PC.HUDUI){PC.HUDUI->SetVisibility(HudShown);}
                }
                return Settled(Now,.5);
            },10.,false});
        // Leaving the opening rest: the return countdown (one size, it lasts seconds) and the stage title card, held at a
        // fixed age for the shots and released by Match_Field.
        {
            FPage& Count=Pages.Add_GetRef({TEXT("Match_StageCount"),[this](ALDPlayerController& PC,double Now)
            {
                // The opening rest runs out by itself (REST.OpeningBuildSec): its own last seconds are the countdown.
                if(Once(Now,0)){Resize(PC,1920,1080);StageBase=PC.HUDUI?PC.HUDUI->MomentStageAt():-1.;Dev(PC,ELDDevTestOp::SetCountdownPaused,0);}
                ULDWorkshop* Ring=PC.HUDUI?PC.HUDUI->Workshop.Get():nullptr;
                if(Ring&&Ring->IsOpen()&&Once(Now,3,.3)){PC.HUDUI->ToggleWorkshop();}
                return PC.HUDUI&&PC.HUDUI->HasStageCount()&&Settled(Now,.6);
            },50.});
            Count.Only=0;
        }
        Pages.Add({TEXT("Match_StageCard"),[this](ALDPlayerController& PC,double Now)
            {
                if(!PC.HUDUI){return false;}
                if(Once(Now,0)){KillSeenAt=-1.;}
                // Past its natural end the rest is skipped by the DEV op, which enters the field at once.
                if(Once(Now,5,12.)&&PC.HUDUI->MomentStageAt()==StageBase){Dev(PC,ELDDevTestOp::SkipPreparation,0);}
                if(PC.HUDUI->MomentStageAt()==StageBase){return false;}
                if(KillSeenAt<0.){KillSeenAt=Now;PC.HUDUI->FreezeMoments(.5f);}
                return Now-KillSeenAt>=.3;
            },30.});
        Pages.Add({TEXT("Match_Field"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0)&&PC.HUDUI){PC.HUDUI->FreezeMoments(-1.f);}
                ULDWorkshop* Ring=PC.HUDUI?PC.HUDUI->Workshop.Get():nullptr;
                if(Ring&&Ring->IsOpen()&&Once(Now,3,5.)){PC.HUDUI->ToggleWorkshop();}
                return InPhase(PC,ELDMatchPhase::Development)&&Settled(Now,7.);
            },45.});
        // A real death: the level-1 host, not invulnerable, stands in DEV ownerless fire and miasma at its feet until it
        // falls; the death banner, the damage recap and the spawn choice follow from the authority.
        Pages.Add({TEXT("Match_Death"),[this](ALDPlayerController& PC,double Now)
            {
                if(PC.OwnerState.LifePhase==ELDLifePhase::Dead){return Settled(Now,1.)&&Now-DeadAt>=1.5;}
                DeadAt=Now;
                for(int32 Try=0;Try<30;++Try)
                {
                    if(Once(Now,Try,.5+Try*2.)){Dev(PC,ELDDevTestOp::SpawnGroundState,Try%2==0?int64(ELDGroundStateKind::Fire):int64(ELDGroundStateKind::Miasma));}
                }
                return false;
            },75.});
        Pages.Add({TEXT("Match_Respawned"),[this](ALDPlayerController& PC,double Now)
            {
                if(PC.OwnerState.LifePhase!=ELDLifePhase::Alive){return false;}
                if(Once(Now,1)){Dev(PC,ELDDevTestOp::SetInvulnerable,1);}
                if(Once(Now,2,1.)){Dev(PC,ELDDevTestOp::ApplyBreakoutPreset,0,TEXT("breakout_lifesteal"));}
                ULDWorkshop* Ring=PC.HUDUI?PC.HUDUI->Workshop.Get():nullptr;
                if(Ring&&Ring->IsOpen()&&Once(Now,3,3.)){PC.HUDUI->ToggleWorkshop();}
                return Settled(Now,6.);
            },90.});
        // A real own kill: the max-level, invulnerable host jumps beside the nearest rival (DEV teleport) and right-clicks it
        // through the ordinary pointer order until the authority's kill reaches the HUD; the K.O. moment is then held at a
        // fixed age for the shots and released by the next page.
        Pages.Add({TEXT("Match_Kill"),[this](ALDPlayerController& PC,double Now)
            {
                if(!PC.HUDUI||!PC.Navigation){return false;}
                if(Once(Now,0))
                {
                    KillBase=PC.HUDUI->MomentKillAt();KillSeenAt=-1.;
                    // One hit decides it: a DEV attack-damage term (P2 points), removed again by Match_KillEnd.
                    ELDGrowthReason Reason=ELDGrowthReason::None;
                    PC.SubmitDevTest(ELDDevTestOp::SetAttribute,150000,Reason,NAME_None,0,uint8(ELDDemoBonusStat::ADFlat));
                }
                if(PC.HUDUI->MomentKillAt()!=KillBase)
                {
                    if(KillSeenAt<0.){KillSeenAt=Now;PC.HUDUI->FreezeMoments(.45f);PC.Navigation->DevSetPointer(TOptional<FVector2D>());}
                    return Now-KillSeenAt>=.4;
                }
                const APawn* Self=PC.GetPawn();
                if(!Self){return false;}
                for(int32 Try=0;Try<50;++Try)
                {
                    if(!Once(Now,1+Try,1.+Try*1.2)){continue;}
                    const ALDCharacter* Rival=nullptr;double Best=TNumericLimits<double>::Max();
                    for(TActorIterator<ALDCharacter> It(PC.GetWorld());It;++It)
                    {
                        if(*It==Self||!It->GetController()||It->IsA<ALDCampMemberCharacter>()||It->IsHidden()){continue;}
                        const double Distance=FVector::DistSquared(It->GetActorLocation(),Self->GetActorLocation());
                        if(Distance<Best){Best=Distance;Rival=*It;}
                    }
                    if(!Rival){continue;}
                    if(Best>FMath::Square(320.))
                    {
                        ELDGrowthReason Reason=ELDGrowthReason::None;
                        PC.SubmitDevTest(ELDDevTestOp::TeleportToCamera,0,Reason,NAME_None,0,0,Rival->GetActorLocation()+FVector(140,0,0));
                        continue;
                    }
                    FVector2D Screen;
                    if(PC.ProjectWorldLocationToScreen(Rival->GetActorLocation(),Screen)){PC.Navigation->DevSetPointer(Screen);PC.Navigation->ClickMove();}
                }
                return false;
            },75.});
        Pages.Add({TEXT("Match_KillEnd"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0))
                {
                    if(PC.HUDUI){PC.HUDUI->FreezeMoments(-1.f);}
                    if(PC.Navigation){PC.Navigation->DevSetPointer(TOptional<FVector2D>());}
                    ELDGrowthReason Reason=ELDGrowthReason::None;
                    PC.SubmitDevTest(ELDDevTestOp::SetAttribute,0,Reason,NAME_None,0,uint8(ELDDemoBonusStat::ADFlat));
                }
                return Settled(Now,1.5);
            },10.,false});
        // The first mid rest as it really runs (the rivals sign up for the PK by themselves; the host watches and may
        // bet): each step is waited for, then the stage clock is held (DEV pause; bodies and the fight go on) while its
        // sizes are shot, and let go by the next page.
        const auto RestStep=[this](const TCHAR* Name,ELDRestStep Step,bool bSignup,double Hold,double Timeout)
        {
            Pages.Add({Name,[this,Step,bSignup,Hold](ALDPlayerController& PC,double Now)
            {
                const FLDPublicMatchView* View=Match(PC);
                if(Once(Now,0)){Dev(PC,ELDDevTestOp::SetCountdownPaused,0);HeldAt=-1.;}
                if(!View||View->Phase!=ELDMatchPhase::Rest||View->Timeline.Step!=Step){return false;}
                if(bSignup&&View->ServerNowMs<View->Timeline.SignupOpensAtMs){return false;}
                if(HeldAt<0.){HeldAt=Now;}
                if(Now-HeldAt>=Hold&&Once(Now,1)){Dev(PC,ELDDevTestOp::SetCountdownPaused,1);}
                return Now-HeldAt>=Hold+.8;
            },Timeout});
        };
        RestStep(TEXT("Match_R1Build"),ELDRestStep::Build,false,1.5,240.);
        RestStep(TEXT("Match_R1Signup"),ELDRestStep::Build,true,1.,90.);
        RestStep(TEXT("Match_R1Betting"),ELDRestStep::Betting,false,.5,40.);
        RestStep(TEXT("Match_R1PKReady"),ELDRestStep::PKReady,false,.3,30.);
        RestStep(TEXT("Match_R1PK"),ELDRestStep::PK,false,4.,30.);
        RestStep(TEXT("Match_R1Settlement"),ELDRestStep::Settlement,false,.3,80.);
        RestStep(TEXT("Match_R1Return"),ELDRestStep::Return,false,.3,30.);
        // Back in the field the stage clock is held again, so the second rest does not arrive under the pages below.
        Pages.Add({TEXT("Match_Scoreboard"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0)){Dev(PC,ELDDevTestOp::SetCountdownPaused,0);HeldAt=-1.;}
                if(!InPhase(PC,ELDMatchPhase::Development)&&!Settled(Now,60.)){return false;}
                if(HeldAt<0.){HeldAt=Now;}
                if(Now-HeldAt>=2.&&Once(Now,1)){Dev(PC,ELDDevTestOp::SetCountdownPaused,1);Key(PC,PC.BindingKey(TEXT("Scoreboard")),true);}
                return Now-HeldAt>=3.;
            },90.});
        // The rank-up page of a slot that can rank up. (The live skill card follows the OS cursor, which the probe leaves
        // alone, so it is not shot here.)
        Pages.Add({TEXT("Match_RankUp"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0))
                {
                    Key(PC,PC.BindingKey(TEXT("Scoreboard")),false);
                    for(int32 Index=0;Index<4;++Index){if(Click(PC.HUDUI,*FString::Printf(TEXT("HUDRankUp%d"),Index))){break;}}
                }
                return Settled(Now,1.);
            }});
        Pages.Add({TEXT("Match_Menu"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0)){Click(PC.HUDUI,TEXT("HUDRankClose"));Key(PC,PC.BindingKey(TEXT("Scoreboard")),false);if(PC.RootUI&&!PC.RootUI->IsMenuOpen()){PC.RootUI->ToggleMenu();}}
                return Settled(Now,.8);
            }});
        Pages.Add({TEXT("Match_MenuLeave"),[this](ALDPlayerController& PC,double Now)
            {if(Once(Now,0)){Click(PC.RootUI,TEXT("LeaveMatch"));}return Settled(Now,.8);}});
        AddSettings(TEXT("Match"),[](ALDPlayerController& PC)
            {
                if(PC.RootUI){PC.RootUI->CloseTopModal();}
                if(PC.RootUI&&!PC.RootUI->IsMenuOpen()){PC.RootUI->ToggleMenu();}
                return Click(PC.RootUI,TEXT("MatchSettings"));
            });
        Pages.Add({TEXT("Match_Final"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0)&&PC.RootUI){PC.RootUI->CloseTopModal();PC.RootUI->CloseTopPanel();if(PC.RootUI->IsMenuOpen()){PC.RootUI->ToggleMenu();}}
                if(Once(Now,9)){Dev(PC,ELDDevTestOp::SetCountdownPaused,0);}
                // One request; a second one while the queued opening is pending is refused (StaleContext) and toasts.
                if(InPhase(PC,ELDMatchPhase::Development)&&Once(Now,10,1.)){Dev(PC,ELDDevTestOp::OpenCapture,0);}
                if(InPhase(PC,ELDMatchPhase::Development)&&Once(Now,11,40.)){Dev(PC,ELDDevTestOp::OpenCapture,0);}
                return InPhase(PC,ELDMatchPhase::CaptureOpen)&&Settled(Now,10.);
            },120.});
        Pages.Add({TEXT("Match_Results"),[this](ALDPlayerController& PC,double Now)
            {

                if(!InPhase(PC,ELDMatchPhase::Results)){ResultsSeen=-1.;return false;}
                if(ResultsSeen<0.){ResultsSeen=Now;}
                return Now-ResultsSeen>=3.;
            },900.});
        Pages.Add({TEXT("Match_ResultsReplay"),[this](ALDPlayerController& PC,double Now)
            {
                const ALDGameState* GS=PC.GetWorld()->GetGameState<ALDGameState>();
                if(GS&&PC.HUDUI&&Once(Now,0))
                {
                    for(const FLDResultRow& Row:GS->MatchResult.Rows)
                    {
                        if(Row.ContenderId!=PC.OwnerState.ContenderId){PC.HUDUI->SelectResultContender(Row.ContenderId);break;}
                    }
                }
                return Settled(Now,1.);
            }});
    }
    double ResultsSeen=-1.;
    static int32 Guests(ALDPlayerController& PC)
    {
        const ALDGameState* GS=PC.GetWorld()->GetGameState<ALDGameState>();int32 Count=0;
        if(GS){for(const FLDLobbyMember& M:GS->DemoLobby.Members){Count+=M.Kind==ELDContenderKind::Human&&!M.Host;}}
        return Count;
    }
    static bool GuestReady(ALDPlayerController& PC)
    {
        const ALDGameState* GS=PC.GetWorld()->GetGameState<ALDGameState>();
        if(GS){for(const FLDLobbyMember& M:GS->DemoLobby.Members){if(M.Kind==ELDContenderKind::Human&&!M.Host){return M.Ready;}}}
        return false;
    }
    // The guest client: its own Lobby view before and after it readies, then it leaves the room.
    void PlanClient()
    {
        Pages.Add({TEXT("Lobby_GuestNotReady"),[this](ALDPlayerController& PC,double Now)
            {return PC.RootUI&&InPhase(PC,ELDMatchPhase::Lobby)&&Guests(PC)==1&&Settled(Now,4.);},120.});
        Pages.Add({TEXT("Lobby_GuestReady"),[this](ALDPlayerController& PC,double Now)
            {if(Once(Now,0)){PC.SetDemoReady(true);}return PC.OwnerState.bReady&&Settled(Now,1.5);},30.});
        // Then the probe ends and the client process quits, which drops it from the host's room.
        Pages.Add({TEXT("Lobby_GuestLast"),[this](ALDPlayerController& PC,double Now){return Settled(Now,6.);},30.,false});
    }
    TWeakObjectPtr<ACameraActor> LineupCamera;
    ESlateVisibility RootShown=ESlateVisibility::SelfHitTestInvisible,HudShown=ESlateVisibility::SelfHitTestInvisible;
    double KillBase=-1.,KillSeenAt=-1.,StageBase=-1.,HeldAt=-1.;
    TWeakObjectPtr<ALDCharacter> PosedBody;
    TArray<TWeakObjectPtr<AActor>> HiddenBodies;
    double DeadAt=0.;

    bool Tick(float)
    {
        const double Now=FPlatformTime::Seconds();
        if(Now-Began>1500.){Check(false,TEXT("The UI coverage capture finished in time"),FString::Printf(TEXT("page %d"),Page));return Finish();}
        ALDPlayerController* PC=Owner.Get();
        if(!PC||!PC->GetLocalPlayer()){return true;}
        if(Page>=Pages.Num())
        {
            Check(Missed.IsEmpty(),TEXT("Every planned page was reached"),FString::Join(Missed,TEXT(", ")));
            return Finish();
        }
        FPage& Current=Pages[Page];
        if(Step==0){PageAt=Now;Step=1;Fired.Reset();}
        if(Step==1)
        {
            if(Now-PageAt>Current.Timeout){Missed.Add(Current.Name);Step=0;Combo=0;++Page;return true;}
            if(!Current.Enter(*PC,Now)){return true;}
            Step=2;Combo=0;
        }
        const int32 Count=Current.bAllSizes&&Current.Only<0?4:1;
        if(Combo>=Count){Step=0;Combo=0;++Page;return true;}
        const FCombo& Next=Combos[Current.Only>=0?Current.Only:Combo];
        if(Step==2)
        {
            if(Current.Only<0)
            {
                Resize(*PC,Next.W,Next.H);
                if(PC->Controls){PC->Controls->TextScale=Next.Text;}
            }
            Due=FMath::Max(Due,Now+.6);Step=3;return true;
        }
        if(Now<Due){return true;}
        if(Capture(FString::Printf(TEXT("%s_%s.png"),*Current.Name,Next.Tag))){return true;}
        ++Combo;Step=2;
        return true;
    }
};

void StartUICoverageProbe(ALDPlayerController& PC)
{
    const bool bClient=FParse::Param(FCommandLine::Get(),TEXT("LDUICoverageClient"));
    if(!FParse::Param(FCommandLine::Get(),TEXT("LDUICoverageProbe"))||!PC.GetLocalPlayer()){return;}
    // The guest probe runs in the room it joined (not on the frontend it starts from); every other probe is the host's.
    if(bClient?PC.IsFrontend()||PC.HasAuthority():!PC.HasAuthority()){return;}
    static bool bClientStarted=false;
    if(bClient){if(bClientStarted){return;}bClientStarted=true;}
    auto Probe=MakeShared<FUICoverageProbe>();Probe->Owner=&PC;
    FParse::Value(FCommandLine::Get(),TEXT("LDUICoverageOutput="),Probe->Output);
    if(Probe->Output.IsEmpty()){Probe->Output=FPaths::ProjectSavedDir()/TEXT("UICoverage");}
    Probe->bFrontend=FParse::Param(FCommandLine::Get(),TEXT("LDUICoverageFrontend"));
    // A Slate tooltip opens its own window, which a -RenderOffScreen process cannot create; hover shots use painted hover
    // states only.
    FSlateApplication::Get().SetAllowTooltips(false);
    Probe->bClient=bClient;
    if(Probe->bFrontend){Probe->PlanFrontend();}else if(bClient){Probe->PlanClient();}else{Probe->PlanMatch();}
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float Delta){return Probe->Tick(Delta);}));
}
}
#endif
