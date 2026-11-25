#include "Probes/LDUICoverageProbe.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/WidgetTree.h"
#include "Components/EditableTextBox.h"
#include "Containers/Ticker.h"
#include "Development/LDDevTestProtocol.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Framework/LDGameState.h"
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
    bool bCapturing=false,bFrontend=false;
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
        FFileHelper::SaveStringToFile(Text,*FPaths::Combine(Output,bFrontend?TEXT("UICoverage-Frontend.json"):TEXT("UICoverage-Match.json")),
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
        Pages.Add({TEXT("Lobby_AI5"),[this](ALDPlayerController& PC,double Now)
            {if(Once(Now,0)){PC.SetDemoAICount(5);}return Settled(Now,2.);}});
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
        Pages.Add({TEXT("Match_Field"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0)){Dev(PC,ELDDevTestOp::SkipPreparation,0);}
                if(Once(Now,1,1.)){Dev(PC,ELDDevTestOp::SetInvulnerable,1);}
                if(InPhase(PC,ELDMatchPhase::Development)&&Once(Now,2,2.)){Dev(PC,ELDDevTestOp::ApplyBreakoutPreset,0,TEXT("breakout_lifesteal"));}
                ULDWorkshop* Ring=PC.HUDUI?PC.HUDUI->Workshop.Get():nullptr;
                if(Ring&&Ring->IsOpen()&&Once(Now,3,5.)){PC.HUDUI->ToggleWorkshop();}
                return InPhase(PC,ELDMatchPhase::Development)&&Settled(Now,7.);
            },45.});
        Pages.Add({TEXT("Match_Scoreboard"),[this](ALDPlayerController& PC,double Now)
            {if(Once(Now,0)){Key(PC,PC.BindingKey(TEXT("Scoreboard")),true);}return Settled(Now,.8);}});
        // The live skill card under the pointer, then its Alt formula, then the rank-up page of a slot that can rank up.
        Pages.Add({TEXT("Match_SkillHover"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0)){Key(PC,PC.BindingKey(TEXT("Scoreboard")),false);}
                if(ULDCommandButton* Slot=FindButton(PC.HUDUI,TEXT("HUDActionQ"))){LDSlateProbeInput::Pointer(Slot,EKeys::LeftMouseButton,false,false);}
                return Settled(Now,1.);
            }});
        Pages.Add({TEXT("Match_SkillFormula"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0)){Key(PC,EKeys::LeftAlt,true);}
                if(ULDCommandButton* Slot=FindButton(PC.HUDUI,TEXT("HUDActionQ"))){LDSlateProbeInput::Pointer(Slot,EKeys::LeftMouseButton,false,false);}
                return Settled(Now,.8);
            }});
        Pages.Add({TEXT("Match_RankUp"),[this](ALDPlayerController& PC,double Now)
            {
                if(Once(Now,0))
                {
                    Key(PC,EKeys::LeftAlt,false);
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
                for(int32 Try=0;Try<10;++Try){if(InPhase(PC,ELDMatchPhase::Development)&&Once(Now,10+Try,1.+Try*6.)){Dev(PC,ELDDevTestOp::OpenCapture,0);}}
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
    if(!FParse::Param(FCommandLine::Get(),TEXT("LDUICoverageProbe"))||!PC.GetLocalPlayer()||!PC.HasAuthority()){return;}
    auto Probe=MakeShared<FUICoverageProbe>();Probe->Owner=&PC;
    FParse::Value(FCommandLine::Get(),TEXT("LDUICoverageOutput="),Probe->Output);
    if(Probe->Output.IsEmpty()){Probe->Output=FPaths::ProjectSavedDir()/TEXT("UICoverage");}
    Probe->bFrontend=FParse::Param(FCommandLine::Get(),TEXT("LDUICoverageFrontend"));
    // A Slate tooltip opens its own window, which a -RenderOffScreen process cannot create; hover shots use painted hover
    // states only.
    FSlateApplication::Get().SetAllowTooltips(false);
    if(Probe->bFrontend){Probe->PlanFrontend();}else{Probe->PlanMatch();}
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float Delta){return Probe->Tick(Delta);}));
}
}
#endif
