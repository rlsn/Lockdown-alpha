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
#include "Region/LDMapRegistry.h"
#include "Framework/LDBuildBlueprintComponent.h"
#include "Framework/LDGameMode.h"
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
#include "Core/LDLog.h"
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
    bool bCapturing=false,bFrontend=false,bClient=false,bEntrant=false,bOneSize=false;
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
        // start, with the reason), then once it is (can start); the room is filled to eight around the two humans, and the
        // guest plays the match too (its own views: the network warning, the contested mini flag).
        Pages.Add({TEXT("Lobby_HostWait"),[this](ALDPlayerController& PC,double Now)
            {if(Once(Now,0)){PC.SetDemoAICount(2);}return Guests(PC)==1&&!GuestReady(PC)&&Settled(Now,3.);},120.});
        Pages.Add({TEXT("Lobby_HostCanStart"),[this](ALDPlayerController& PC,double Now)
            {return Guests(PC)==1&&GuestReady(PC)&&Settled(Now,1.);},90.});
        Pages.Add({TEXT("Lobby_Full"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0)){PC.SetDemoAICount(6);}
                return Guests(PC)==1&&GuestReady(PC)&&Settled(Now,2.5);
            },90.});
        Pages.Add({TEXT("Lobby_ButtonPressed"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0)){LDSlateProbeInput::Pointer(FindButton(PC.RootUI,TEXT("LobbyBuildBlueprint")),EKeys::LeftMouseButton,true,false);}
                return Settled(Now,.6);
            },10.,false});
        Pages.Add({TEXT("Lobby_Blueprint"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0)){LDSlateProbeInput::Pointer(FindButton(PC.RootUI,TEXT("LobbyBuildBlueprint")),EKeys::LeftMouseButton,false,true);}
                // The button's own click if the routed release did not open the viewer.
                if(Once(Now,1,.5)&&PC.BuildBlueprints&&!PC.BuildBlueprints->IsViewerOpen()){Click(PC.RootUI,TEXT("LobbyBuildBlueprint"));}
                return Settled(Now,1.6);
            }});
        AddSettings(TEXT("Lobby"),[](ALDPlayerController& PC)
            {if(PC.RootUI){PC.RootUI->CloseTopModal();PC.RootUI->CloseTopPanel();}return Click(PC.RootUI,TEXT("LobbySettings"));});
        Pages.Add({TEXT("Match_RestOpening"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0)&&PC.RootUI){PC.RootUI->CloseTopModal();PC.RootUI->CloseTopPanel();}
                if(Once(Now,3,.2)){Dev(PC,ELDDevTestOp::SetDailyPreset,int64(ELDDailyModifier::Typhoon)+16*int64(ELDDailyModifier::Rain));}
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
        struct FSeatPose{const TCHAR* Clip;float Time;float Yaw;bool bBall;};
        // Basketball stills keep the ball in hand; the personality stills (hooper/Quaternius) carry none, so it is hidden.
        static const FSeatPose SeatPoses[8]={
            {TEXT("Hooper_Idle"),0.f,-14.f,true},
            {TEXT("Quaternius/Hooper/QN_Idle_FoldArms_Loop"),1.f,16.f,false},
            {TEXT("KB_043_AA_Basketball_PlayerA_Between_The_Legs_Type01_Loop"),.4f,-24.f,true},
            {TEXT("Quaternius/Hooper/QN_Dance_Loop"),.4f,10.f,false},
            {TEXT("KB_149_AA_Basketball_PlayerA_Free_Throw_LH"),.5f,-8.f,true},
            {TEXT("Quaternius/Hooper/QN_Idle_TalkingPhone_Loop"),1.2f,20.f,false},
            {TEXT("Quaternius/Hooper/QN_Yes"),1.f,-18.f,false},
            {TEXT("Quaternius/Hooper/QN_Sword_Idle"),.7f,12.f,false}};
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
                    const FString Path=FString::Printf(TEXT("/Game/Lockdown/Art/Characters/hooper/%s.%s"),Pose.Clip,*FPaths::GetCleanFilename(Pose.Clip));
                    if(auto* Visual=Pick->FindComponentByClass<ULDHumanoidVisualComponent>())
                    {
                        if(UAnimSequence* Still=LoadObject<UAnimSequence>(nullptr,*Path)){Visual->HoldPose(Still,Pose.Time,!Pose.bBall);PosedBody=Pick;}
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
                if(PC.OwnerState.LifePhase!=ELDLifePhase::Alive){HeldAt=-1.;return false;}
                // One DEV command is in flight at a time: the preset follows the first by its own delay.
                if(HeldAt<0.){HeldAt=Now;Dev(PC,ELDDevTestOp::SetInvulnerable,1);}
                if(Now-HeldAt>=1.5&&Once(Now,2)){Dev(PC,ELDDevTestOp::ApplyBreakoutPreset,0,TEXT("breakout_element_loop"));}
                ULDWorkshop* Ring=PC.HUDUI?PC.HUDUI->Workshop.Get():nullptr;
                if(Ring&&Ring->IsOpen()&&Now-HeldAt>=3.5&&Once(Now,3)){PC.HUDUI->ToggleWorkshop();}
                return Now-HeldAt>=6.;
            },90.});
        // Field sites as they are: the host waits for one to exist (the airdrop, the mid-field objective, a delivery
        // locker), holds the stage clock, jumps beside it (DEV teleport) and is shot standing there.
        const auto FieldSite=[this](const TCHAR* Name,TFunction<bool(const FLDPublicMatchView&,FVector&)> Find,double Timeout,bool bInside=false)
        {
            Pages.Add({Name,[this,Find,bInside](ALDPlayerController& PC,double Now)
            {
                const FLDPublicMatchView* View=Match(PC);
                if(Once(Now,0)){Dev(PC,ELDDevTestOp::SetCountdownPaused,0);HeldAt=-1.;}
                FVector Site=FVector::ZeroVector;
                if(!View||(View->Phase!=ELDMatchPhase::Development&&View->Phase!=ELDMatchPhase::CaptureOpen)||!Find(*View,Site)){return false;}
                // Inside: the host stands in the site itself with the stage clock running, so the site's own progress (a
                // flag's spray, a locker's opening) is on screen; the shot is then one size, taken while it runs.
                if(HeldAt<0.){HeldAt=Now;if(bInside){Resize(PC,1920,1080);}else{Dev(PC,ELDDevTestOp::SetCountdownPaused,1);}}
                static const FVector Beside[]={FVector(-330,0,0),FVector(0,330,0),FVector(240,230,0)};
                static const FVector Within[]={FVector(-60,0,0),FVector(0,70,0),FVector(60,40,0)};
                const FVector* Sides=bInside?Within:Beside;
                for(int32 Try=0;Try<3;++Try)
                {
                    if(Now-HeldAt>=.6+Try*1.&&Once(Now,1+Try))
                    {
                        const APawn* Self=PC.GetPawn();
                        if(Self&&FVector::Dist2D(Self->GetActorLocation(),Site)<(bInside?110.:420.)){break;}
                        ELDGrowthReason Reason=ELDGrowthReason::None;
                        PC.SubmitDevTest(ELDDevTestOp::TeleportToCamera,0,Reason,NAME_None,0,0,Site+Sides[Try]+FVector(0,0,120));
                    }
                }
                return Now-HeldAt>=(bInside?2.6:5.);
            },Timeout});
            if(bInside){Pages.Last().Only=0;}
        };
        DealerEarliest=Pages.Num();
        // The skill card of the Q slot: the HUD is told the pointer is over the slot (its DEV hover widget; the OS cursor
        // stays where it is), then with Alt held for the formula; the next page lets both go.
        Pages.Add({TEXT("Match_SkillHover"),[this](ALDPlayerController& PC,double Now)
            {
                UWidget* Slot=PC.HUDUI&&PC.HUDUI->WidgetTree?PC.HUDUI->WidgetTree->FindWidget(TEXT("HUDActionQ")):nullptr;
                if(!Slot){return false;}
                if(Once(Now,0)){PC.HUDUI->ProbeHover(Slot);}
                return Settled(Now,1.2);
            },15.});
        Pages.Add({TEXT("Match_SkillHoverAlt"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0)){Key(PC,EKeys::LeftAlt,true);}
                return Settled(Now,1.2);
            },15.});
        Pages.Add({TEXT("Match_SkillHoverEnd"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0)){Key(PC,EKeys::LeftAlt,false);if(PC.HUDUI){PC.HUDUI->ProbeHover(nullptr);}}
                return Settled(Now,.6);
            },10.,false});
        FieldSite(TEXT("Match_Airdrop"),[](const FLDPublicMatchView& View,FVector& Site)
            {Site=View.Airdrop.PositionCm;return View.Airdrop.Phase==ELDAirdropPhase::Announced||View.Airdrop.Phase==ELDAirdropPhase::Landed;},40.);
        FieldSite(TEXT("Match_Objective"),[](const FLDPublicMatchView& View,FVector& Site)
            {Site=View.Objective.CentreCm;return View.Objective.Phase==ELDObjectivePhase::Announced||View.Objective.Phase==ELDObjectivePhase::Open;},70.);
        // The open flag: the host steps inside (DEV teleport) while the guest client does the same, so the flag is
        // contested; the guest leaves again a few seconds later and the host's own spray runs.
        {
            FPage& Contested=Pages.Add_GetRef({TEXT("Match_FlagContested"),[this](ALDPlayerController& PC,double Now)
            {
                const FLDPublicMatchView* View=Match(PC);
                if(Once(Now,0)){Dev(PC,ELDDevTestOp::SetCountdownPaused,0);HeldAt=-1.;Resize(PC,1920,1080);}
                if(!View||View->Objective.Kind!=ELDObjectiveKind::MiniFlag||View->Objective.Phase!=ELDObjectivePhase::Open){return false;}
                if(HeldAt<0.){HeldAt=Now;}
                static const FVector Within[]={FVector(-60,0,0),FVector(0,70,0),FVector(60,40,0)};
                for(int32 Try=0;Try<3;++Try)
                {
                    if(Now-HeldAt>=.2+Try*1.&&Once(Now,1+Try))
                    {
                        const APawn* Self=PC.GetPawn();
                        if(Self&&FVector::Dist2D(Self->GetActorLocation(),View->Objective.CentreCm)<110.){break;}
                        ELDGrowthReason Reason=ELDGrowthReason::None;
                        PC.SubmitDevTest(ELDDevTestOp::TeleportToCamera,0,Reason,NAME_None,0,0,View->Objective.CentreCm+Within[Try]+FVector(0,0,120));
                    }
                }
                return View->Objective.bContested&&Now-HeldAt>=1.2;
            },45.});
            Contested.Only=0;
            FPage& Spray=Pages.Add_GetRef({TEXT("Match_FlagSpray"),[this](ALDPlayerController& PC,double Now)
            {
                const FLDPublicMatchView* View=Match(PC);
                if(Once(Now,0)){Resize(PC,1920,1080);}
                return View&&View->Objective.Phase==ELDObjectivePhase::Open&&!View->Objective.bContested&&View->Objective.SprayMs>=900;
            },30.});
            Spray.Only=0;
        }
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
                    // Never a DEV teleport in a rest (the game's DevTeleport faults a room there): only in the field.
                    if(!Rival||!InPhase(PC,ELDMatchPhase::Development)){continue;}
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
        // The build ring with a real build on it (the DEV preset's skills, cards and medals): equipped hosts, then a hand
        // card held so the hosts that can take it light up. The next page puts the ring away again.
        Pages.Add({TEXT("Match_RingEquipped"),[this](ALDPlayerController& PC,double Now)
            {
                ULDWorkshop* Ring=PC.HUDUI?PC.HUDUI->Workshop.Get():nullptr;
                if(!Ring){return false;}
                if(!Ring->IsOpen()&&Once(Now,0)){PC.HUDUI->ToggleWorkshop();}
                return Ring->IsOpen()&&Settled(Now,2.);
            },20.});
        Pages.Add({TEXT("Match_RingHeld"),[this](ALDPlayerController& PC,double Now)
            {
                ULDWorkshop* Ring=PC.HUDUI?PC.HUDUI->Workshop.Get():nullptr;
                if(!Ring||!Ring->IsOpen()){return false;}
                // The kill before this page left a reward offer: each option is taken in turn until one lands in the hand
                // (a card with a free host installs itself; a medal waits in the library), and is then held.
                for(int32 Take=0;Take<3;++Take)
                {
                    if(Ring->View().Hand.IsEmpty()&&Once(Now,10+Take,.3+Take*1.6)){Ring->HandleKey(Take==0?EKeys::One:Take==1?EKeys::Two:EKeys::Three);}
                }
                if(Ring->View().Hand.IsEmpty()){return Settled(Now,7.);}
                if(Once(Now,1)){Ring->ShowHand(true);HeldAt=Now;}
                if(Now-HeldAt>=.5&&Once(Now,2)){for(int32 Index=0;Index<12&&!Ring->SelectHand(Index);++Index){}}
                return Now-HeldAt>=1.5;
            },25.});
        Pages.Add({TEXT("Match_RingMedal"),[this](ALDPlayerController& PC,double Now)
            {
                ULDWorkshop* Ring=PC.HUDUI?PC.HUDUI->Workshop.Get():nullptr;
                if(!Ring||!Ring->IsOpen()){return false;}
                // The held medal goes into the first host's socket through the ring's own click; a held card onto its host.
                if(Once(Now,0))
                {
                    for(int32 Host=0;Host<Ring->View().Hosts.Num();++Host)
                    {
                        if(Ring->View().Hosts[Host].bSocket&&!Ring->View().Hosts[Host].bHasMedal){Ring->ProbeClick(ULDWorkshop::EHit::Gem,Host,0);break;}
                    }
                }
                if(Once(Now,1,1.)){Ring->SelectHand(INDEX_NONE);Ring->ShowHand(false);}
                return Settled(Now,2.5);
            },20.});
        Pages.Add({TEXT("Match_RingEnd"),[this](ALDPlayerController& PC,double Now)
            {
                ULDWorkshop* Ring=PC.HUDUI?PC.HUDUI->Workshop.Get():nullptr;
                if(Ring&&Once(Now,0)){Ring->SelectHand(INDEX_NONE);Ring->ShowHand(false);if(Ring->IsOpen()){PC.HUDUI->ToggleWorkshop();}}
                return Settled(Now,1.);
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
        if(bEntrant)
        {
            // -LDUICoverageEntrant: the host goes into its own sign-up disk during the window (ordinary ground clicks towards
            // the disk the authority names, then the DEV teleport if the walk has not arrived), so the steps that follow
            // are an entrant's: drawn, fighting (still invulnerable from the DEV op), paid.
            Pages.Add({TEXT("Match_R1Signup"),[this](ALDPlayerController& PC,double Now)
            {
                const FLDPublicMatchView* View=Match(PC);
                if(Once(Now,0)){Dev(PC,ELDDevTestOp::SetCountdownPaused,0);HeldAt=-1.;}
                if(!View||View->Phase!=ELDMatchPhase::Rest||View->Timeline.Step!=ELDRestStep::Build||View->ServerNowMs<View->Timeline.SignupOpensAtMs){return false;}
                const auto* Mode=PC.GetWorld()->GetAuthGameMode<ALDGameMode>();
                FVector Disk=FVector::ZeroVector;double Radius=0.;
                if(!Mode||!Mode->SignupDiskFor(PC.OwnerState.ContenderId,Disk,Radius)){return false;}
                ULDWorkshop* Ring=PC.HUDUI?PC.HUDUI->Workshop.Get():nullptr;
                if(Ring&&Ring->IsOpen()&&Once(Now,9)){PC.HUDUI->ToggleWorkshop();}
                if(!View->Timeline.Signups.Contains(PC.OwnerState.ContenderId))
                {
                    // A ground click towards the disk every 0.7 s; a disk off the screen is clicked at the screen's edge on its
                    // side, and the follow camera brings it in.
                    for(int32 Step=0;Step<60;++Step)
                    {
                        if(!PC.Navigation||!Once(Now,10+Step,.4+Step*.7)){continue;}
                        int32 W=0,H=0;PC.GetViewportSize(W,H);
                        FVector2D Screen;
                        if(!PC.ProjectWorldLocationToScreen(Disk,Screen)||W<=0||H<=0){continue;}
                        Screen.X=FMath::Clamp(Screen.X,W*.12,W*.88);Screen.Y=FMath::Clamp(Screen.Y,H*.18,H*.74);
                        PC.Navigation->DevSetPointer(Screen);PC.Navigation->ClickMove();
                    }
                    for(int32 Try=0;Try<3;++Try)
                    {
                        if(Once(Now,80+Try,4.+Try*1.5))
                        {
                            ELDGrowthReason Reason=ELDGrowthReason::None;
                            PC.SubmitDevTest(ELDDevTestOp::TeleportToCamera,0,Reason,NAME_None,0,0,Disk+FVector(0,0,120));
                        }
                    }
                    return false;
                }
                if(HeldAt<0.){HeldAt=Now;if(PC.Navigation){PC.Navigation->DevSetPointer(TOptional<FVector2D>());}}
                if(Now-HeldAt>=.6&&Once(Now,8)){Dev(PC,ELDDevTestOp::SetCountdownPaused,1);}
                return Now-HeldAt>=1.4;
            },100.});
        }
        else{RestStep(TEXT("Match_R1Signup"),ELDRestStep::Build,true,1.,90.);}
        Pages.Add({TEXT("Match_R1Betting"),[this](ALDPlayerController& PC,double Now)
            {
                const FLDPublicMatchView* View=Match(PC);
                if(Once(Now,0)){Dev(PC,ELDDevTestOp::SetCountdownPaused,0);HeldAt=-1.;}
                if(!View||View->Phase!=ELDMatchPhase::Rest||View->Timeline.Step!=ELDRestStep::Betting||!PC.HUDUI){return false;}
                if(HeldAt<0.)
                {
                    HeldAt=Now;
                    const ULDWorkshop* Ring=PC.HUDUI->Workshop.Get();
                    UE_LOG(LogLDTest,Display,TEXT("LD_UI_COVERAGE_BET_WINDOW ms=%lld ring_open=%d"),View->Timeline.StepEndsAtMs-View->Timeline.StepStartedAtMs,Ring&&Ring->IsOpen()?1:0);
                }
                const auto Press=[&](int32 Want,int32 Id)
                {
                    int32 Seen=0;
                    // The side panel's rows first, then the bar over the open ring (only one of the two is on screen).
                    for(const TCHAR* Kind:{TEXT("HUDPKBet"),TEXT("HUDPKCompactBet")})
                    {
                        for(int32 Index=1;Index<=8;++Index)
                        {
                            ULDCommandButton* Row=FindButton(PC.HUDUI,*(FString(Kind)+FString::FromInt(Index)));
                            if(!Row||Row->GetVisibility()!=ESlateVisibility::Visible){continue;}
                            if(Seen++!=Want){continue;}
                            const bool bReached=LDSlateProbeInput::SendClick(Row);
                            UE_LOG(LogLDTest,Display,TEXT("LD_UI_COVERAGE_BET_PRESS id=%d row=%s reached=%d enabled=%d left_ms=%lld"),Id,*Row->GetName(),bReached?1:0,
                                Row->GetIsEnabled()?1:0,[&PC,View](){int64 NowMs=0;return PC.ReadDemoAuthorityTimeMs(NowMs)?View->Timeline.StepEndsAtMs-NowMs:int64(-1);}());
                            return;
                        }
                    }
                    UE_LOG(LogLDTest,Display,TEXT("LD_UI_COVERAGE_BET_PRESS id=%d row=none left_ms=%lld"),Id,View->Timeline.StepEndsAtMs-View->ServerNowMs);
                };
                const double InWindow=Now-HeldAt;
                // What the rows are, frame by frame, over the first two seconds of the window.
                if(Now-HeldAt<2.&&BetTraceFrames<40)
                {
                    FString Rows;
                    for(int32 Index=1;Index<=4;++Index)
                    {
                        for(const TCHAR* Kind:{TEXT("HUDPKBet"),TEXT("HUDPKCompactBet")})
                        {
                            const ULDCommandButton* Row=FindButton(PC.HUDUI,*(FString(Kind)+FString::FromInt(Index)));
                            if(Row){Rows+=FString::Printf(TEXT("%s%d=%d%d "),Kind[5]==TEXT('C')?TEXT("c"):TEXT("p"),Index,int32(Row->GetVisibility()),Row->GetIsEnabled()?1:0);}
                        }
                    }
                    const ULDWorkshop* Ring=PC.HUDUI->Workshop.Get();
                    UE_LOG(LogLDTest,Display,TEXT("LD_UI_COVERAGE_BET_TRACE t=%.2f bettor=%d ring=%d rows=%s"),Now-HeldAt,
                        View->PK.IsBettor(PC.OwnerState.ContenderId)?1:0,Ring&&Ring->IsOpen()?1:0,*Rows);
                    ++BetTraceFrames;
                }
                if(InWindow>=.3&&Once(Now,1)){Press(0,1);}
                if(InWindow>=1.3&&Once(Now,2)){Press(1,2);}
                if(InWindow>=2.3&&Once(Now,3)){Dev(PC,ELDDevTestOp::SetCountdownPaused,1);}
                return InWindow>=3.1;
            },40.});
        // The betting step again with the build ring open (the clock is still held): the one-line PK bar over the ring.
        Pages.Add({TEXT("Match_R1BettingRing"),[this](ALDPlayerController& PC,double Now)
            {
                ULDWorkshop* Ring=PC.HUDUI?PC.HUDUI->Workshop.Get():nullptr;
                if(!Ring){return false;}
                if(!Ring->IsOpen()&&Once(Now,0)){PC.HUDUI->ToggleWorkshop();}
                return Ring->IsOpen()&&Settled(Now,1.5);
            },15.});
        Pages.Add({TEXT("Match_R1BettingRingEnd"),[this](ALDPlayerController& PC,double Now)
            {
                ULDWorkshop* Ring=PC.HUDUI?PC.HUDUI->Workshop.Get():nullptr;
                if(Ring&&Ring->IsOpen()&&Once(Now,0)){PC.HUDUI->ToggleWorkshop();}
                return Settled(Now,.8);
            },10.,false});
        RestStep(TEXT("Match_R1PKReady"),ELDRestStep::PKReady,false,.3,30.);
        RestStep(TEXT("Match_R1PK"),ELDRestStep::PK,false,4.,30.);
        // A spectator's throw the way a player makes it (the clock stays held): the throw button arms the aim, the pointer
        // over a spot outside the arena (grey crossed reticle; a click there keeps the aim and says why), then over the
        // arena floor (the reticle in the telegraph's colour), a click throws; the telegraph, then the landing.
        auto Aim=[this](ALDPlayerController& PC,const FVector& Ground)
            {
                FVector2D Screen;
                if(!PC.Navigation||!PC.ProjectWorldLocationToScreen(Ground,Screen)){return false;}
                PC.Navigation->DevSetPointer(Screen);
                return true;
            };
        Pages.Add({TEXT("Match_R1PKThrowAimOutside"),[this,Aim](ALDPlayerController& PC,double Now)
            {
                const FLDPublicMatchView* View=Match(PC);
                ULDHUD* HUD=PC.HUDUI;
                if(!View||!HUD||View->Timeline.Step!=ELDRestStep::PK){return Settled(Now,3.);}
                if(Once(Now,0)){HUD->TogglePKThrowAim();}
                // The nearest on-screen spot the HUD's own rule calls outside the arena (eight bearings, outward in steps).
                int32 W=0,H=0;
                PC.GetViewportSize(W,H);
                FVector Out=View->PK.ArenaCentreCm+FVector(4000.,0.,0.);
                bool bFound=false;
                for(double Reach=1500.;Reach<=4500.&&!bFound;Reach+=150.)
                {
                    for(int32 Bearing=0;Bearing<8&&!bFound;++Bearing)
                    {
                        const FVector Try=View->PK.ArenaCentreCm+FRotator(0.,45.*Bearing,0.).Vector()*Reach;
                        FVector2D Screen;
                        if(HUD->PKThrowSpot(Try)==ELDPKThrowResult::OutsideArena&&PC.ProjectWorldLocationToScreen(Try,Screen)
                            &&Screen.X>.1*W&&Screen.X<.6*W&&Screen.Y>.15*H&&Screen.Y<.8*H){Out=Try;bFound=true;}
                    }
                }
                if(!Aim(PC,Out)){return false;}
                if(Once(Now,1)&&HUD->IsPKThrowArmed()){HUD->ThrowPKAtPointer();}
                return HUD->IsPKThrowArmed()&&Settled(Now,1.);
            },10.,false});
        Pages.Add({TEXT("Match_R1PKThrowAim"),[this,Aim](ALDPlayerController& PC,double Now)
            {
                const FLDPublicMatchView* View=Match(PC);
                ULDHUD* HUD=PC.HUDUI;
                if(!View||!HUD||!HUD->IsPKThrowArmed()){return Settled(Now,3.);}
                return Aim(PC,View->PK.ArenaCentreCm+FVector(120,80,0))&&Settled(Now,.8);
            },10.,false});
        Pages.Add({TEXT("Match_R1PKThrow"),[this](ALDPlayerController& PC,double Now)
            {
                ULDHUD* HUD=PC.HUDUI;
                if(HUD&&Once(Now,0)&&HUD->IsPKThrowArmed()){HUD->ThrowPKAtPointer();}
                return true;
            },10.,false});
        Pages.Add({TEXT("Match_R1PKThrowLanded"),[this](ALDPlayerController& PC,double Now)
            {
                if(Settled(Now,.3)&&PC.Navigation){PC.Navigation->DevSetPointer(TOptional<FVector2D>());return true;}
                return false;
            },10.,false});
        RestStep(TEXT("Match_R1Settlement"),ELDRestStep::Settlement,false,.3,80.);
        RestStep(TEXT("Match_R1Return"),ELDRestStep::Return,false,.3,30.);
        // The second field: the arena (its objective) and the first delivery locker that is out, each waited for with the
        // stage clock running, then the host standing at the arena and inside the locker's reach while it opens.
        Pages.Add({TEXT("Match_Field2"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0)){Dev(PC,ELDDevTestOp::SetCountdownPaused,0);}
                return InPhase(PC,ELDMatchPhase::Development)&&Settled(Now,2.);
            },60.,false});
        FieldSite(TEXT("Match_Locker"),[](const FLDPublicMatchView& View,FVector& Site)
            {if(View.Lockers.IsEmpty()){return false;}Site=View.Lockers[0].PositionCm;return true;},85.,true);
        FieldSite(TEXT("Match_Arena"),[](const FLDPublicMatchView& View,FVector& Site)
            {Site=View.Objective.CentreCm;return View.Objective.Kind==ELDObjectiveKind::Arena&&(View.Objective.Phase==ELDObjectivePhase::Announced||View.Objective.Phase==ELDObjectivePhase::Open);},110.);
        // The second-hand dealer: the match plans its time and camp (ALDGameMode::GetPeddler; anywhere from the end of the
        // first field to the third, out for about a minute), so its three pages are not at a fixed place: Tick puts them
        // in front of the next page as soon as it is out in a field (after the host's preset). The host jumps beside its
        // camp, is shot with it, beats it (the DEV attack term again) and is shot with the three-pick it pays; the last
        // page hands the stage clock back as it was.
        DealerPages.Add({TEXT("Match_Peddler"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0)){HeldAt=-1.;}
                const auto* Mode=PC.GetWorld()->GetAuthGameMode<ALDGameMode>();
                const auto* Registry=PC.GetWorld()->GetSubsystem<ULDMapRegistry>();
                if(!Mode||!Registry||!Mode->GetPeddler().bSpawned||Mode->GetPeddler().bGone||!InPhase(PC,ELDMatchPhase::Development)){return false;}
                const FLDMapBinding* Camp=Registry->Resolve(Mode->GetPeddler().CampAnchor,Registry->Generation);
                if(!Camp){return false;}
                DealerSite=Camp->Position;
                if(HeldAt<0.){HeldAt=Now;Dev(PC,ELDDevTestOp::SetCountdownPaused,1);}
                static const FVector Beside[]={FVector(-300,0,0),FVector(0,300,0),FVector(220,-220,0)};
                for(int32 Try=0;Try<3;++Try)
                {
                    if(Now-HeldAt>=.6+Try*1.&&Once(Now,1+Try))
                    {
                        const APawn* Self=PC.GetPawn();
                        if(Self&&FVector::Dist2D(Self->GetActorLocation(),DealerSite)<420.){break;}
                        ELDGrowthReason Reason=ELDGrowthReason::None;
                        PC.SubmitDevTest(ELDDevTestOp::TeleportToCamera,0,Reason,NAME_None,0,0,DealerSite+Beside[Try]+FVector(0,0,120));
                    }
                }
                return Now-HeldAt>=4.5;
            },30.});
        DealerPages.Add({TEXT("Match_PeddlerReward"),[this](ALDPlayerController& PC,double Now)
            {
                const auto* Mode=PC.GetWorld()->GetAuthGameMode<ALDGameMode>();
                if(!Mode||!PC.Navigation){return false;}
                if(Once(Now,0))
                {
                    HeldAt=-1.;
                    ELDGrowthReason Reason=ELDGrowthReason::None;
                    PC.SubmitDevTest(ELDDevTestOp::SetAttribute,150000,Reason,NAME_None,0,uint8(ELDDemoBonusStat::ADFlat));
                }
                // The dealer's body by its camp: once it has been clicked and no body stands there any more, it is beaten
                // (the plan's own flags only tell that it came out and that its time ran out).
                const ALDCharacter* Dealer=nullptr;double Best=FMath::Square(900.);
                for(TActorIterator<ALDCampMemberCharacter> It(PC.GetWorld());It;++It)
                {
                    const double Distance=FVector::DistSquared2D(It->GetActorLocation(),DealerSite);
                    if(!It->IsHidden()&&Distance<Best){Best=Distance;Dealer=*It;}
                }
                // Only a dealer this page clicked counts: without one the page times out and is reported as missed.
                if(!Dealer&&bDealerClicked)
                {
                    if(HeldAt<0.){HeldAt=Now;PC.Navigation->DevSetPointer(TOptional<FVector2D>());}
                    return Now-HeldAt>=2.5;
                }
                for(int32 Try=0;Try<25;++Try)
                {
                    if(!Once(Now,1+Try,1.+Try*1.2)){continue;}
                    if(Dealer){bDealerClicked=true;}
                    FVector2D Screen;
                    if(Dealer&&PC.ProjectWorldLocationToScreen(Dealer->GetActorLocation(),Screen)){PC.Navigation->DevSetPointer(Screen);PC.Navigation->ClickMove();}
                }
                return false;
            },45.});
        DealerPages.Add({TEXT("Match_PeddlerEnd"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0))
                {
                    if(PC.Navigation){PC.Navigation->DevSetPointer(TOptional<FVector2D>());}
                    ELDGrowthReason Reason=ELDGrowthReason::None;
                    PC.SubmitDevTest(ELDDevTestOp::SetAttribute,0,Reason,NAME_None,0,uint8(ELDDemoBonusStat::ADFlat));
                }
                // The stage clock as the interrupted pages had it.
                if(Once(Now,1,.6)){Dev(PC,ELDDevTestOp::SetCountdownPaused,bDealerClockHeld?1:0);}
                return Settled(Now,1.4);
            },10.,false});
        // Where the dealer may still be to come: the clock runs until it is out (Tick then inserts its pages) or its time
        // has passed.
        Pages.Add({TEXT("Match_PeddlerWait"),[this](ALDPlayerController& PC,double Now)
            {
                if(bDealerInserted){return true;}
                if(Once(Now,0)){Dev(PC,ELDDevTestOp::SetCountdownPaused,0);}
                const auto* Mode=PC.GetWorld()->GetAuthGameMode<ALDGameMode>();
                return Mode&&(Mode->GetPeddler().bGone||DealerOut(PC));
            },240.,false});
        // Back in the field the stage clock is held again, so the next rest does not arrive under the pages below.
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
                    // A real pointer press through the window's hit test (the "+" lies under the full-screen ring layer).
                    bool bPressed=false;
                    for(int32 Index=0;Index<3&&!bPressed;++Index)
                    {
                        ULDCommandButton* Plus=FindButton(PC.HUDUI,*FString::Printf(TEXT("HUDRankUp%d"),Index));
                        if(Plus&&Plus->IsVisible()&&Plus->GetIsEnabled()){bPressed=LDSlateProbeInput::SendClick(Plus);}
                    }
                    UE_LOG(LogLDTest,Display,TEXT("LD_UI_COVERAGE_RANKUP_POINTER reached=%d"),bPressed?1:0);
                }
                if(Once(Now,1,.5))
                {
                    const bool bOpen=PC.HUDUI&&PC.HUDUI->IsRankUpOpen();
                    const APawn* Body=PC.GetPawn();
                    UE_LOG(LogLDTest,Display,TEXT("LD_UI_COVERAGE_RANKUP_PAGE open=%d phase=%d alive=%d"),bOpen?1:0,
                        int32(Match(PC)?Match(PC)->Phase:ELDMatchPhase::Lobby),Body?1:0);
                    // The hotkey path when the pointer did not open it, so the page is still shot.
                    if(!bOpen){for(int32 Index=0;Index<4;++Index){if(Click(PC.HUDUI,*FString::Printf(TEXT("HUDRankUp%d"),Index))){break;}}}
                }
                return Settled(Now,1.6);
            }});
        // The same page by its hotkey (Ctrl + the first skill key), then a rank bought by a pointer press on its card: both
        // ways in, in the field, with the fight around the host going on.
        Pages.Add({TEXT("Match_RankUpKey"),[this](ALDPlayerController& PC,double Now)
            {
                if(!PC.HUDUI){return true;}
                const auto Points=[&PC](){const FLDProgressionView* Own=PC.ReadOwnProgression();return Own?int32(Own->SkillPoints):-1;};
                if(Once(Now,0)){PC.HUDUI->CloseRankUp();}
                if(Once(Now,1,.5))
                {
                    bool bTaken=false;
                    for(const TCHAR* Action:{TEXT("Skill1"),TEXT("Skill2"),TEXT("SkillE")})
                    {if(!PC.HUDUI->IsRankUpOpen()){bTaken|=PC.HUDUI->HandleContextualKeyPress(PC.BindingKey(Action),false,true,false);}}
                    UE_LOG(LogLDTest,Display,TEXT("LD_UI_COVERAGE_RANKUP_HOTKEY taken=%d open=%d points=%d"),bTaken?1:0,PC.HUDUI->IsRankUpOpen()?1:0,Points());
                }
                if(Once(Now,2,1.6))
                {
                    RankPointsBefore=Points();
                    const bool bPressed=LDSlateProbeInput::SendClick(FindButton(PC.HUDUI,TEXT("HUDRankCard0")));
                    UE_LOG(LogLDTest,Display,TEXT("LD_UI_COVERAGE_RANKUP_CARD_POINTER reached=%d"),bPressed?1:0);
                }
                if(Once(Now,3,3.2))
                {UE_LOG(LogLDTest,Display,TEXT("LD_UI_COVERAGE_RANKUP_BOUGHT points=%d->%d open=%d"),RankPointsBefore,Points(),PC.HUDUI->IsRankUpOpen()?1:0);}
                return Settled(Now,3.6);
            },12.,false});
        Pages.Add({TEXT("Match_Menu"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0)){Click(PC.HUDUI,TEXT("HUDRankClose"));Key(PC,PC.BindingKey(TEXT("Scoreboard")),false);if(PC.RootUI&&!PC.RootUI->IsMenuOpen()){PC.RootUI->ToggleMenu();}}
                return Settled(Now,.8);
            }});
        Pages.Add({TEXT("Match_MenuPressed"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0)){LDSlateProbeInput::Pointer(FindButton(PC.RootUI,TEXT("LeaveMatch")),EKeys::LeftMouseButton,true,false);}
                return Settled(Now,.6);
            },10.,false});
        Pages.Add({TEXT("Match_MenuLeave"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0)){LDSlateProbeInput::Pointer(FindButton(PC.RootUI,TEXT("LeaveMatch")),EKeys::LeftMouseButton,false,true);}
                // The row's own click as well, in case the routed release did not reach it (opening the confirmation twice is one).
                if(Once(Now,1,.4)){Click(PC.RootUI,TEXT("LeaveMatch"));}
                return Settled(Now,1.2);
            }});
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
        // The final with a graffiti on the flagpole: the score bug's foot lines (whose graffiti it is, then the aside).
        Pages.Add({TEXT("Match_FinalGraffiti"),[this](ALDPlayerController& PC,double Now)
            {
                const FLDPublicMatchView* View=Match(PC);
                if(!View||View->Phase!=ELDMatchPhase::CaptureOpen||!View->GraffitiOwner.IsValid()){HeldAt=-1.;return false;}
                if(HeldAt<0.){HeldAt=Now;}
                return Now-HeldAt>=.6;
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
    int32 RankPointsBefore=-1;
    int32 BetTraceFrames=0;
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
    // The guest client: its own Lobby view before and after it readies, then its own match beside the host.
    void PlanClient()
    {
        Pages.Add({TEXT("Lobby_GuestNotReady"),[this](ALDPlayerController& PC,double Now)
            {return PC.RootUI&&InPhase(PC,ELDMatchPhase::Lobby)&&Guests(PC)==1&&Settled(Now,4.);},120.});
        Pages.Add({TEXT("Lobby_GuestReady"),[this](ALDPlayerController& PC,double Now)
            {if(Once(Now,0)){PC.SetDemoReady(true);}return PC.OwnerState.bReady&&Settled(Now,1.5);},30.});
        Pages.Add({TEXT("Lobby_GuestLast"),[this](ALDPlayerController& PC,double Now){return Settled(Now,6.);},30.,false});
        // The guest stays ready until the host starts, then plays: the network warning under the engine's own packet
        // simulation (this client only, switched off again), and the mini flag, which it enters while the host sprays so
        // the flag is contested, then leaves. It stays in the match until the results.
        Pages.Add({TEXT("Guest_Started"),[this](ALDPlayerController& PC,double Now)
            {
                for(int32 Try=0;Try<200;++Try){if(InPhase(PC,ELDMatchPhase::Lobby)&&!PC.OwnerState.bReady&&Once(Now,Try,2.*Try)){PC.SetDemoReady(true);}}
                return !InPhase(PC,ELDMatchPhase::Lobby)&&!InPhase(PC,ELDMatchPhase::Starting)&&Settled(Now,8.);
            },600.,false});
        Pages.Add({TEXT("Guest_NetSpike"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0)){PC.ConsoleCommand(TEXT("NetEmulation.PktLag 260"));}
                if(Once(Now,1,.2)){PC.ConsoleCommand(TEXT("NetEmulation.PktLoss 8"));}
                return Settled(Now,8.);
            },30.});
        Pages.Add({TEXT("Guest_NetCalm"),[this](ALDPlayerController& PC,double Now)
            {if(Once(Now,0)){PC.ConsoleCommand(TEXT("NetEmulation.Off"));}return Settled(Now,2.);},10.,false});
        {
            FPage& Flag=Pages.Add_GetRef({TEXT("Guest_FlagContested"),[this](ALDPlayerController& PC,double Now)
            {
                const FLDPublicMatchView* View=Match(PC);
                if(Once(Now,0)){HeldAt=-1.;Resize(PC,1920,1080);}
                // It steps in only once somebody is spraying (the host): alone it would simply win the flag.
                if(!View||View->Objective.Kind!=ELDObjectiveKind::MiniFlag||View->Objective.Phase!=ELDObjectivePhase::Open||View->Objective.SprayMs<=0){return false;}
                if(HeldAt<0.){HeldAt=Now;}
                static const FVector Within[]={FVector(50,-60,0),FVector(-40,-70,0),FVector(70,0,0)};
                for(int32 Try=0;Try<3;++Try)
                {
                    if(Now-HeldAt>=.1+Try*1.&&Once(Now,1+Try))
                    {
                        const APawn* Self=PC.GetPawn();
                        if(Self&&FVector::Dist2D(Self->GetActorLocation(),View->Objective.CentreCm)<120.){break;}
                        ELDGrowthReason Reason=ELDGrowthReason::None;
                        PC.SubmitDevTest(ELDDevTestOp::TeleportToCamera,0,Reason,NAME_None,0,0,View->Objective.CentreCm+Within[Try]+FVector(0,0,120));
                    }
                }
                return Now-HeldAt>=5.;
            },600.});
            Flag.Only=0;
        }
        Pages.Add({TEXT("Guest_FlagLeft"),[this](ALDPlayerController& PC,double Now)
            {
                const FLDPublicMatchView* View=Match(PC);
                if(View&&View->Phase==ELDMatchPhase::Development&&Once(Now,0))
                {
                    ELDGrowthReason Reason=ELDGrowthReason::None;
                    PC.SubmitDevTest(ELDDevTestOp::TeleportToCamera,0,Reason,NAME_None,0,0,View->Objective.CentreCm+FVector(700,300,120));
                }
                return Settled(Now,2.);
            },10.,false});
        Pages.Add({TEXT("Guest_Results"),[this](ALDPlayerController& PC,double Now){return InPhase(PC,ELDMatchPhase::Results)&&Settled(Now,4.);},1400.,false});
    }
    TWeakObjectPtr<ACameraActor> LineupCamera;
    ESlateVisibility RootShown=ESlateVisibility::SelfHitTestInvisible,HudShown=ESlateVisibility::SelfHitTestInvisible;
    double KillBase=-1.,KillSeenAt=-1.,StageBase=-1.,HeldAt=-1.;
    TWeakObjectPtr<ALDCharacter> PosedBody;
    FVector DealerSite=FVector::ZeroVector;
    bool bDealerClicked=false,bDealerInserted=false,bDealerClockHeld=false;
    TArray<FPage> DealerPages;
    int32 DealerEarliest=MAX_int32;
    // The dealer is out, in a field, with the host alive to go to it.
    static bool DealerOut(ALDPlayerController& PC)
    {
        const auto* Mode=PC.GetWorld()?PC.GetWorld()->GetAuthGameMode<ALDGameMode>():nullptr;
        return Mode&&Mode->GetPeddler().bSpawned&&!Mode->GetPeddler().bGone&&InPhase(PC,ELDMatchPhase::Development)&&PC.OwnerState.LifePhase==ELDLifePhase::Alive;
    }
    TArray<TWeakObjectPtr<AActor>> HiddenBodies;
    double DeadAt=0.;

    bool Tick(float)
    {
        const double Now=FPlatformTime::Seconds();
        if(Now-Began>1750.){Check(false,TEXT("The UI coverage capture finished in time"),FString::Printf(TEXT("page %d"),Page));return Finish();}
        ALDPlayerController* PC=Owner.Get();
        if(!PC||!PC->GetLocalPlayer()){return true;}
        if(Page>=Pages.Num())
        {
            Check(Missed.IsEmpty(),TEXT("Every planned page was reached"),FString::Join(Missed,TEXT(", ")));
            return Finish();
        }
        if(Step==0&&!bDealerInserted&&!DealerPages.IsEmpty()&&Page>=DealerEarliest&&DealerOut(*PC))
        {
            const FLDPublicMatchView* View=Match(*PC);
            bDealerClockHeld=View&&View->Timeline.bCountdownPaused;
            Pages.Insert(DealerPages,Page);
            bDealerInserted=true;
        }
        FPage& Current=Pages[Page];
        if(Step==0){PageAt=Now;Step=1;Fired.Reset();}
        if(Step==1)
        {
            if(Now-PageAt>Current.Timeout){Missed.Add(Current.Name);Step=0;Combo=0;++Page;return true;}
            if(!Current.Enter(*PC,Now)){return true;}
            Step=2;Combo=0;
        }
        // -LDUICoverageOneSize (a recorded run): every page at 1920x1080 and 100 % only, so the window never changes size.
        const int32 Count=Current.bAllSizes&&Current.Only<0&&!bOneSize?4:1;
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
    Probe->bEntrant=FParse::Param(FCommandLine::Get(),TEXT("LDUICoverageEntrant"));
    Probe->bOneSize=FParse::Param(FCommandLine::Get(),TEXT("LDUICoverageOneSize"));
    if(Probe->bFrontend){Probe->PlanFrontend();}else if(bClient){Probe->PlanClient();}else{Probe->PlanMatch();}
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float Delta){return Probe->Tick(Delta);}));
}
}
#endif
