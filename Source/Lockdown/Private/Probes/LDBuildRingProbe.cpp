#include "Probes/LDBuildRingProbe.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Containers/Ticker.h"
#include "Development/LDDevTestProtocol.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDGameState.h"
#include "Framework/LDPlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UI/LDChoicesPanel.h"
#include "UI/LDComponentStorage.h"
#include "UI/LDControlSettings.h"
#include "UI/LDHUD.h"
#include "UI/LDStatBar.h"
#include "UI/LDUIRoot.h"
#include "UI/LDWorkshop.h"
#include "UnrealClient.h"

namespace LDDemoTests
{
class FBuildRingProbe:public TSharedFromThis<FBuildRingProbe>
{
public:
    TWeakObjectPtr<ALDPlayerController> Owner;
    FString Output;
    TArray<TSharedPtr<FJsonValue>> Checks,Shots;
    TSharedRef<FJsonObject> Facts=MakeShared<FJsonObject>();
    FString Error;
    double Began=FPlatformTime::Seconds();
    double At=0.,Due=0.;
    int32 Stage=0,Step=0,Shot=0;
    bool bCapturing=false;
    int32 Width=0,Height=0;
    using EHit=ULDWorkshop::EHit;

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
        Report->SetObjectField(TEXT("facts"),Facts);
        FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));
        FFileHelper::SaveStringToFile(Text,*FPaths::Combine(Output,TEXT("BuildRing.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        FPlatformMisc::RequestExit(false);
        return false;
    }
    bool Dev(ALDPlayerController& PC,ELDDevTestOp Op,int64 Amount,FName Definition=NAME_None)
    {
        ELDGrowthReason Reason=ELDGrowthReason::None;
        const uint64 Request=PC.SubmitDevTest(Op,Amount,Reason,Definition,0,0);
        return Request!=0;
    }
    void Resize(ALDPlayerController& PC,int32 InWidth,int32 InHeight)
    {
        if(InWidth==Width&&InHeight==Height){return;}
        Width=InWidth;Height=InHeight;
        PC.ConsoleCommand(FString::Printf(TEXT("r.SetRes %dx%dw"),Width,Height),true);
        Due=FPlatformTime::Seconds()+.8;
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
    ULDWorkshop* Ring(ALDPlayerController& PC) const {return PC.HUDUI?PC.HUDUI->Workshop.Get():nullptr;}
    ULDChoicesPanel* Tray(ALDPlayerController& PC) const {return PC.HUDUI?PC.HUDUI->ChoicesPanel.Get():nullptr;}
    ULDComponentStorage* Cards(ALDPlayerController& PC) const {return PC.HUDUI?PC.HUDUI->Storage.Get():nullptr;}
    bool Busy(ALDPlayerController& PC) const
    {
        return (Tray(PC)&&Tray(PC)->HasPendingRequest())||(Cards(PC)&&Cards(PC)->HasPendingRequest())
            ||(PC.HUDUI&&PC.HUDUI->StatBar&&PC.HUDUI->StatBar->HasPendingRequest());
    }
    void Open(ALDPlayerController& PC,bool bOpen)
    {
        ULDChoicesPanel* Panel=Tray(PC);
        if(!Panel||!PC.RootUI){return;}
        if(bOpen&&!Panel->IsTrayOpen()){PC.RootUI->ShowMainPanel(ELDUIMainPanel::Choices);}
        if(!bOpen&&Panel->IsTrayOpen()){Panel->ClosePanel();}
    }
    // The installed card with the most links (the hover shot's subject).
    FIntPoint MostLinked(ULDWorkshop& Workshop) const
    {
        FIntPoint Best(INDEX_NONE,INDEX_NONE);int32 Most=-1;
        const LDBuildRing::FView& View=Workshop.View();
        for(int32 Host=0;Host<View.Hosts.Num();++Host)
        {
            for(int32 Index=0;Index<View.Hosts[Host].Cards.Num();++Index)
            {
                const int32 Links=Workshop.LinkCount(EHit::Installed,Host,Index);
                if(Links>Most){Most=Links;Best=FIntPoint(Host,Index);}
            }
        }
        return Best;
    }

    // The shots of one ring state: configure, settle, capture.
    struct FShot
    {
        FString Name;
        int32 Width=1920,Height=1080;
        float Text=1.f;
        TFunction<void(ALDPlayerController&,ULDWorkshop&)> Setup;
    };
    TArray<FShot> Plan;
    bool RunPlan(ALDPlayerController& PC,double Now)
    {
        ULDWorkshop* Workshop=Ring(PC);
        if(!Workshop){Check(false,TEXT("The HUD has the build ring"));return false;}
        if(Shot>=Plan.Num()){return false;}
        FShot& Next=Plan[Shot];
        if(Step==0)
        {
            Resize(PC,Next.Width,Next.Height);
            if(PC.Controls){PC.Controls->TextScale=Next.Text;}
            Workshop->PinHover(EHit::None,INDEX_NONE,INDEX_NONE);Workshop->SelectHand(INDEX_NONE);Workshop->ShowHand(false);
            Workshop->FreezeMotion(-1.f,-1.f);
            if(Next.Setup){Next.Setup(PC,*Workshop);}
            Due=FMath::Max(Due,Now+.45);Step=1;return true;
        }
        if(Now<Due){return true;}
        if(Capture(Next.Name)){return true;}
        Step=0;++Shot;
        return Shot<Plan.Num();
    }

    bool Tick(float)
    {
        const double Now=FPlatformTime::Seconds();
        if(Now-Began>480.){Check(false,TEXT("The build ring capture finished in time"),FString::Printf(TEXT("stage %d step %d shot %d"),Stage,Step,Shot));return Finish();}
        ALDPlayerController* PC=Owner.Get();
        UWorld* World=PC?PC->GetWorld():nullptr;
        ALDGameState* GS=World?World->GetGameState<ALDGameState>():nullptr;
        if(!PC||!GS||!PC->GetLocalPlayer()){return true;}
        const FLDProgressionView* View=PC->ReadOwnProgression();
        const FLDPublicMatchView& Match=GS->PublicMatch;
        switch(Stage)
        {
        case 0:
            if(Match.Phase!=ELDMatchPhase::Lobby){return true;}
            PC->SetDemoReady(true);At=Now;Stage=1;return true;
        case 1:
            if(Now-At<.3){return true;}
            PC->StartDemoMatch();At=Now;Stage=2;return true;
        case 2:
            // The opening rest: skip it (retried until the field begins).
            if(Match.Phase==ELDMatchPhase::Development){At=Now;Stage=3;return true;}
            if(Match.Phase!=ELDMatchPhase::Rest||!View||Now-At<1.){return true;}
            Dev(*PC,ELDDevTestOp::SkipPreparation,0);At=Now;return true;
        case 3:
            if(!View||!PC->GetPawn()||PC->OwnerState.LifePhase!=ELDLifePhase::Alive||Now-At<1.5){return true;}
            // One DEV request at a time: peace first, the XP grant once it is answered.
            if(Step==0){Dev(*PC,ELDDevTestOp::SetPeaceful,1);Step=1;At=Now;return true;}
            if(!Dev(*PC,ELDDevTestOp::GrantExperience,6000)){At=Now;return true;}
            At=Now;Stage=4;Step=0;return true;
        case 4:
            if(!View||View->Level<12){if(Now-At>15.){Check(false,TEXT("The DEV XP grant raises the level"),FString::Printf(TEXT("level %d"),View?View->Level:0));return Finish();}return true;}
            Facts->SetNumberField(TEXT("level"),View->Level);
            At=Now;Stage=40;return true;
        case 40:
            // The HUD's level-up flip of this real level-up (its read model samples on its own clock), as keyframes on a frozen
            // moment clock.
            if(!(PC->HUDUI&&PC->HUDUI->HasMomentLevelUp())&&Now-At<3.){return true;}
            Check(PC->HUDUI&&PC->HUDUI->HasMomentLevelUp(),TEXT("The XP grant's level-up reaches the HUD moment"));
            Plan.Reset();
            for(const float Age:{0.f,.04f,.08f,.12f,.5f,1.3f})
            {
                FShot& Key=Plan.AddDefaulted_GetRef();Key.Name=FString::Printf(TEXT("seq-levelup_%04dms.png"),int32(Age*1000.f+.5f));
                Key.Setup=[Age](ALDPlayerController& Owner,ULDWorkshop&){if(Owner.HUDUI){Owner.HUDUI->FreezeMoments(Age);}};
            }
            At=Now;Stage=41;Step=0;Shot=0;Due=Now+.5;return true;
        case 41:
            if(RunPlan(*PC,Now)){return true;}
            if(PC->HUDUI){PC->HUDUI->FreezeMoments(-1.f);}
            At=Now;Stage=5;Step=0;return true;
        case 5:
        {
            // Take offers through the tray (the ring's own command owner) until two card offers wait.
            if(Busy(*PC)||Now-At<.35){return true;}
            ULDChoicesPanel* Panel=Tray(*PC);
            if(!Panel){return true;}
            int32 Empty=0;for(uint8 Key=0;Key<3;++Key){Empty+=LDHUD::ActiveInSlot(*View,Key).IsNone()?1:0;}
            At=Now;
            if(View->bHasActiveOffer&&Empty>0){Panel->ChooseOffer(0);return true;}
            if(View->bHasActiveOffer&&View->ActiveOffer.SkipPoints>0){Panel->SkipOffer();return true;}
            if(View->bHasCardOffer&&View->QueuedCardOffers>2){Panel->PickCardOffer(0);return true;}
            Stage=6;Step=0;return true;
        }
        case 6:
        {
            // Points: the strength keystone and a few more, leaving some for the shop; medals into the body and Q sockets.
            if(Busy(*PC)||Now-At<.4){return true;}
            ULDStatBar* Bar=PC->HUDUI?PC->HUDUI->StatBar.Get():nullptr;
            ULDComponentStorage* Storage=Cards(*PC);
            if(!Bar||!Storage){return true;}
            At=Now;
            if(Step==0){Bar->Allocate(2,18);Step=1;return true;}
            if(Step==1){Bar->Allocate(4,8);Step=2;return true;}
            if(Step==2){Bar->Allocate(0,5);Step=3;return true;}
            if(Step==3||Step==4)
            {
                const LDHUD::EStorageColumn Column=Step==3?LDHUD::EStorageColumn::Body:LDHUD::EStorageColumn::SlotQ;
                ++Step;
                const LDHUD::FStorageView& Model=Storage->Model();
                for(const LDHUD::FStoredView& Stored:Model.Stored)
                {
                    if(LDHUD::CardDefinition(Stored.Card).ToString().StartsWith(TEXT("medal_")))
                    {
                        // A different element than the one already set, so the two sockets react.
                        const LDHUD::FStorageColumnView& Body=Model.Columns[0];
                        if(Step==5&&Body.bHasMedal&&LDHUD::CardDefinition(Body.Medal.Card)==LDHUD::CardDefinition(Stored.Card)){continue;}
                        Storage->DropOn(Stored.Card,Column,NAME_None);break;
                    }
                }
                return true;
            }
            if(Step==5)
            {
                // One installed card back to the library, so the hand shows a card form beside the medals.
                const LDHUD::FStorageView& Model=Storage->Model();
                for(const LDHUD::FStorageColumnView& Column:Model.Columns)
                {
                    if(Column.Entries.Num()>=2){Storage->DropInStorage(Column.Entries.Last().Keys.Last());break;}
                }
                Step=6;return true;
            }
            Facts->SetNumberField(TEXT("owned_cards"),View->Cards.Num());
            Facts->SetNumberField(TEXT("unspent"),double(View->UnspentPoints));
            Open(*PC,true);
            Stage=7;Step=0;Shot=0;At=Now;Due=Now+1.;
            BuildFieldPlan(*PC);
            return true;
        }
        case 7:
            if(RunPlan(*PC,Now)){return true;}
            if(!Error.IsEmpty()){return Finish();}
            // A fuller late-game build (the DEV breakout sample: three skills, six cards, two copies of one): its links.
            if(!Dev(*PC,ELDDevTestOp::ApplyBreakoutPreset,0,TEXT("breakout_lifesteal"))){return true;}
            Stage=71;At=Now;return true;
        case 71:
            if(Now-At<2.5||Busy(*PC)){return true;}
            Facts->SetNumberField(TEXT("preset_cards"),View?View->Cards.Num():0);
            Open(*PC,true);
            Stage=72;Step=0;Shot=0;Due=Now+1.;
            BuildPresetPlan(*PC);
            return true;
        case 72:
            if(RunPlan(*PC,Now)){return true;}
            Open(*PC,false);
            if(PC->Controls){PC->Controls->TextScale=1.f;}
            Resize(*PC,1920,1080);
            Stage=8;At=Now;return true;
        case 8:
            // The first mid-match rest: its build step stocks the shop.
            if(Match.Phase!=ELDMatchPhase::Rest||Match.Timeline.Step!=ELDRestStep::Build||!View||!View->Shop.bStocked){return true;}
            if(Now-At<1.){return true;}
            Open(*PC,true);
            Stage=9;Step=0;Shot=0;Due=Now+1.;
            BuildRestPlan(*PC);
            return true;
        case 9:
            if(RunPlan(*PC,Now)){return true;}
            return Finish();
        default: return Finish();
        }
    }
    void BuildFieldPlan(ALDPlayerController& PC)
    {
        Plan.Reset();
        const auto Hover=[this](FShot& S){S.Setup=[this](ALDPlayerController&,ULDWorkshop& W){const FIntPoint Best=MostLinked(W);W.PinHover(EHit::Installed,Best.X,Best.Y);};};
        for(const FIntVector& Case:{FIntVector(1920,1080,100),FIntVector(1920,1080,125),FIntVector(1280,720,100),FIntVector(1280,720,125)})
        {
            const FString Suffix=FString::Printf(TEXT("%dx%d_%d"),Case.X,Case.Y,Case.Z);
            FShot& Offer=Plan.AddDefaulted_GetRef();Offer.Name=TEXT("Ring_Field_Offer_")+Suffix+TEXT(".png");Offer.Width=Case.X;Offer.Height=Case.Y;Offer.Text=Case.Z/100.f;
            FShot& Linked=Plan.AddDefaulted_GetRef();Linked.Name=TEXT("Ring_Field_Hover_")+Suffix+TEXT(".png");Linked.Width=Case.X;Linked.Height=Case.Y;Linked.Text=Case.Z/100.f;Hover(Linked);
            FShot& Hand=Plan.AddDefaulted_GetRef();Hand.Name=TEXT("Ring_Field_Hand_")+Suffix+TEXT(".png");Hand.Width=Case.X;Hand.Height=Case.Y;Hand.Text=Case.Z/100.f;
            Hand.Setup=[](ALDPlayerController&,ULDWorkshop& W){W.ShowHand(true);W.SelectHand(0);};
        }
        FShot& Stat=Plan.AddDefaulted_GetRef();Stat.Name=TEXT("Ring_Field_Stat_1920x1080_100.png");
        Stat.Setup=[](ALDPlayerController&,ULDWorkshop& W){W.PinHover(EHit::Stat,INDEX_NONE,2);};
        FShot& Medal=Plan.AddDefaulted_GetRef();Medal.Name=TEXT("Ring_Field_Medal_1920x1080_100.png");
        Medal.Setup=[](ALDPlayerController&,ULDWorkshop& W)
        {
            W.ShowHand(true);
            const int32 Index=W.View().Hand.IndexOfByPredicate([](const LDBuildRing::FCard& Card){return Card.Form==ELDCardForm::Medal;});
            W.SelectHand(Index==INDEX_NONE?0:Index);W.PinHover(EHit::Gem,0,INDEX_NONE);
        };
        // The reveal of the offer row and the ring's opening, as keyframes.
        for(const float Age:{0.f,.04f,.08f,.12f,.2f})
        {
            FShot& Key=Plan.AddDefaulted_GetRef();Key.Name=FString::Printf(TEXT("seq-open_%03dms.png"),int32(Age*1000.f+.5f));
            Key.Setup=[Age](ALDPlayerController&,ULDWorkshop& W){W.FreezeMotion(Age,Age);};
        }
        FShot& Closed=Plan.AddDefaulted_GetRef();Closed.Name=TEXT("Ring_Field_Closed_1920x1080_100.png");
        Closed.Setup=[this](ALDPlayerController& PC,ULDWorkshop&){Open(PC,false);};
        FShot& Closed720=Plan.AddDefaulted_GetRef();Closed720.Name=TEXT("Ring_Field_Closed_1280x720_100.png");Closed720.Width=1280;Closed720.Height=720;
        Closed720.Setup=[this](ALDPlayerController& PC,ULDWorkshop&){Open(PC,false);};
        // The field rank-up card over the closed ring, opened by its hotkey (Ctrl + the first skill key).
        for(const FIntVector& Case:{FIntVector(1920,1080,100),FIntVector(1280,720,125)})
        {
            FShot& Rank=Plan.AddDefaulted_GetRef();Rank.Name=FString::Printf(TEXT("Rank_Field_%dx%d_%d.png"),Case.X,Case.Y,Case.Z);
            Rank.Width=Case.X;Rank.Height=Case.Y;Rank.Text=Case.Z/100.f;
            Rank.Setup=[this](ALDPlayerController& PC,ULDWorkshop&)
            {
                Open(PC,false);
                if(PC.HUDUI&&!PC.HUDUI->IsRankUpOpen()){PC.HUDUI->HandleContextualKeyPress(EKeys::Q,false,true,false);}
            };
        }
        FShot& Reopen=Plan.AddDefaulted_GetRef();Reopen.Name=TEXT("Ring_Field_Reopen_1920x1080_100.png");
        Reopen.Setup=[this](ALDPlayerController& PC,ULDWorkshop&){if(PC.HUDUI){PC.HUDUI->CloseRankUp();}Open(PC,true);};
    }
    void BuildPresetPlan(ALDPlayerController&)
    {
        Plan.Reset();
        for(const FIntVector& Case:{FIntVector(1920,1080,100),FIntVector(1280,720,100),FIntVector(1280,720,125)})
        {
            const FString Suffix=FString::Printf(TEXT("%dx%d_%d"),Case.X,Case.Y,Case.Z);
            FShot& Plain=Plan.AddDefaulted_GetRef();Plain.Name=TEXT("Ring_Build_")+Suffix+TEXT(".png");Plain.Width=Case.X;Plain.Height=Case.Y;Plain.Text=Case.Z/100.f;
            FShot& Linked=Plan.AddDefaulted_GetRef();Linked.Name=TEXT("Ring_Build_Hover_")+Suffix+TEXT(".png");Linked.Width=Case.X;Linked.Height=Case.Y;Linked.Text=Case.Z/100.f;
            Linked.Setup=[this](ALDPlayerController&,ULDWorkshop& W){const FIntPoint Best=MostLinked(W);W.PinHover(EHit::Installed,Best.X,Best.Y);};
        }
    }
    void BuildRestPlan(ALDPlayerController& PC)
    {
        Plan.Reset();
        for(const FIntVector& Case:{FIntVector(1920,1080,100),FIntVector(1920,1080,125),FIntVector(1280,720,100),FIntVector(1280,720,125)})
        {
            const FString Suffix=FString::Printf(TEXT("%dx%d_%d"),Case.X,Case.Y,Case.Z);
            FShot& Rest=Plan.AddDefaulted_GetRef();Rest.Name=TEXT("Ring_Rest_")+Suffix+TEXT(".png");Rest.Width=Case.X;Rest.Height=Case.Y;Rest.Text=Case.Z/100.f;
        }
        FShot& Shop=Plan.AddDefaulted_GetRef();Shop.Name=TEXT("Ring_Rest_Shop_1920x1080_100.png");
        Shop.Setup=[](ALDPlayerController&,ULDWorkshop& W){W.PinHover(EHit::Shop,INDEX_NONE,0);};
        FShot& Stat=Plan.AddDefaulted_GetRef();Stat.Name=TEXT("Ring_Rest_Stat_1920x1080_100.png");
        Stat.Setup=[](ALDPlayerController&,ULDWorkshop& W){W.PinHover(EHit::Stat,INDEX_NONE,0);};
        FShot& Hand=Plan.AddDefaulted_GetRef();Hand.Name=TEXT("Ring_Rest_Hand_1920x1080_100.png");
        Hand.Setup=[](ALDPlayerController&,ULDWorkshop& W){W.ShowHand(true);};
        // The random element medal: hovered, then bought through the tray, then sold out with the medal in the library.
        const auto MedalRow=[](ULDWorkshop& W){return W.View().Shop.IndexOfByPredicate([](const LDBuildRing::FShopItem& Item){return Item.Slot==FLDBuildCommand::ShopMedalSlot;});};
        FShot& Medal=Plan.AddDefaulted_GetRef();Medal.Name=TEXT("Ring_Rest_ShopMedal_1920x1080_100.png");
        Medal.Setup=[this,MedalRow](ALDPlayerController& PC,ULDWorkshop& W)
        {
            const FLDProgressionView* View=PC.ReadOwnProgression();
            Check(View&&View->Shop.MedalsMax>0&&View->Shop.MedalsBought==0&&View->UnspentPoints>=View->Shop.MedalPrice,TEXT("The rest shop offers an affordable random medal"));
            Check(MedalRow(W)!=INDEX_NONE,TEXT("The shop wing shows the random medal"));
            W.PinHover(EHit::Shop,INDEX_NONE,MedalRow(W));
        };
        FShot& Bought=Plan.AddDefaulted_GetRef();Bought.Name=TEXT("Ring_Rest_ShopMedalBought_1920x1080_100.png");
        Bought.Setup=[this,MedalRow](ALDPlayerController& PC,ULDWorkshop& W)
        {
            Check(Tray(PC)&&Tray(PC)->BuyShopMedal(),TEXT("The tray sends the medal purchase"));
            W.PinHover(EHit::Shop,INDEX_NONE,MedalRow(W));
            Due=FPlatformTime::Seconds()+1.5;
        };
        FShot& Library=Plan.AddDefaulted_GetRef();Library.Name=TEXT("Ring_Rest_ShopMedalHand_1920x1080_100.png");
        Library.Setup=[this](ALDPlayerController& PC,ULDWorkshop& W)
        {
            const FLDProgressionView* View=PC.ReadOwnProgression();
            Check(View&&View->Shop.MedalsBought==1,TEXT("The medal purchase committed once"));
            W.ShowHand(true);
        };
    }
};

void StartBuildRingProbe(ALDPlayerController& PC)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("LDBuildRingProbe"))||!PC.GetLocalPlayer()||!PC.HasAuthority()){return;}
    auto Probe=MakeShared<FBuildRingProbe>();Probe->Owner=&PC;
    FParse::Value(FCommandLine::Get(),TEXT("LDBuildRingOutput="),Probe->Output);
    if(Probe->Output.IsEmpty()){Probe->Output=FPaths::ProjectSavedDir()/TEXT("BuildRing");}
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float Delta){return Probe->Tick(Delta);}));
}
}
#endif
