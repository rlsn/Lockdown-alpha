#include "UI/LDResultReport.h"
#include "Blueprint/WidgetTree.h"
#include "Combat/LDDamageRecap.h"
#include "Components/CanvasPanel.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Engine/Texture2D.h"
#include "Framework/LDMatchTimeline.h"
#include "UI/LDBroadcastPaint.h"
#include "UI/LDBroadcastStyle.h"
#include "UI/LDUIBackgrounds.h"
#include "UI/LDUIIcons.h"
#include "UI/LDUIPaint.h"

namespace
{
constexpr float DesignW=1920.f,DesignH=1080.f;
// The hosts in the order the comparison lists them, and their short keys.
const TCHAR* const HostKeys[5]={TEXT("Q"),TEXT("W"),TEXT("E"),TEXT("D"),TEXT("体")};
FString Whole(double Value){return Value>=10000.?FString::Printf(TEXT("%.1fk"),Value/1000.):FString::Printf(TEXT("%.0f"),Value);}
}

TSharedRef<SWidget> ULDResultReport::RebuildWidget()
{
    if(!WidgetTree->RootWidget){WidgetTree->RootWidget=WidgetTree->ConstructWidget<UCanvasPanel>();}
    SetVisibility(ESlateVisibility::Visible);
    return Super::RebuildWidget();
}

const FSlateBrush* ULDResultReport::Brush(const FString& Key,const FString& Asset)
{
    if(Key.IsEmpty()){return nullptr;}
    if(const TSharedPtr<FSlateBrush>* Known=Brushes.Find(Key)){return Known->Get();}
    UTexture2D* Texture=Asset.IsEmpty()?LDUIIcons::Find(Key):LoadObject<UTexture2D>(nullptr,*Asset);
    if(!Texture){Brushes.Add(Key,nullptr);return nullptr;}
    Textures.Add(Key,Texture);
    TSharedPtr<FSlateBrush> Made=MakeShared<FSlateBrush>();
    Made->SetResourceObject(Texture);Made->ImageSize=FVector2D(Texture->GetSizeX(),Texture->GetSizeY());Made->DrawAs=ESlateBrushDrawType::Image;
    Brushes.Add(Key,Made);
    return Made.Get();
}

const ULDResultReport::FSeat* ULDResultReport::SeatOf(const FGuid& Id) const
{
    return Seats.FindByPredicate([&Id](const FSeat& Seat){return Seat.Id==Id;});
}

int32 ULDResultReport::RankOf(TFunctionRef<double(const FSeat&)> Value,int32 Index) const
{
    int32 Rank=1;
    for(const FSeat& Other:Seats){if(Value(Other)>Value(Seats[Index])){++Rank;}}
    return Rank;
}

void ULDResultReport::Present(const FLDResultView& Result,const FGuid& SelfId,const FLDDamageSummaryView& Summary,const FLDDemoContentDefinitions* Content)
{
    const uint64 SummaryKey=HashCombine(GetTypeHash(Summary.ResultId),GetTypeHash(Summary.Lines.Num()));
    if(Result.ResultId==ShownResult&&Result.ResultSerial==ShownSerial&&SelfId==ShownSelf&&SummaryKey==ShownSummary&&Content==ShownContent){return;}
    const bool bNewResult=Result.ResultId!=ShownResult||Result.ResultSerial!=ShownSerial;
    ShownResult=Result.ResultId;ShownSerial=Result.ResultSerial;ShownSelf=SelfId;ShownSummary=SummaryKey;ShownContent=Content;
    Seats.Reset();Self=INDEX_NONE;WinnerSeat=INDEX_NONE;Sources.Reset();Phases.Reset();StatNames.Reset();
    const TArray<const FLDDemoStatDefinition*> Stats=Content?Content->StatsInDesignOrder():TArray<const FLDDemoStatDefinition*>();
    for(const FLDDemoStatDefinition* Stat:Stats){StatNames.Add(LDHUD::ContentName(Content,Stat->Id));}
    Keystone=Content&&Content->Parameters.KeystoneThresholdPoints>0?int32(Content->Parameters.KeystoneThresholdPoints):18;
    const auto Icon=[this,Content](FName Id){const FString IconId=Id.IsNone()?FString():LDHUD::ContentIconId(Content,Id.ToString());Brush(IconId,FString());return IconId;};
    // Rows follow the locked start roster: a row's index is its seat.
    for(int32 Index=0;Index<Result.Rows.Num();++Index)
    {
        const FLDResultRow& Row=Result.Rows[Index];
        FSeat& Seat=Seats.AddDefaulted_GetRef();
        Seat.Id=Row.ContenderId;Seat.Seat=Index;Seat.Name=Row.Name;Seat.bLeft=Row.bLeft;
        Seat.bWinner=Result.Winner.IsValid()&&Row.ContenderId==Result.Winner;Seat.bSelf=SelfId.IsValid()&&Row.ContenderId==SelfId;
        Seat.GuardSeconds=double(Row.BankedGuardMs)/1000.;
        Seat.Kills=int64(Row.Kills);Seat.Deaths=int64(Row.Deaths);Seat.Assists=int64(Row.Assists);Seat.Level=Row.FinalLevel;
        Seat.bCombat=Row.Combat.bAvailable;
        Seat.Damage=double(Row.Combat.PhysicalToPlayersP2+Row.Combat.MagicalToPlayersP2+Row.Combat.ReactionToPlayersP2)/100.;
        Seat.Taken=double(Row.Combat.DamageTakenP2)/100.;
        Seat.bBuild=Row.Build.bAvailable;
        for(const FLDResultBuildActive& Active:Row.Build.Actives){if(Active.Slot<3){Seat.HostActive[Active.Slot]=Icon(Active.DefinitionId);}}
        for(const FLDResultBuildCard& Card:Row.Build.Cards)
        {
            int32 Host=4;
            if(Card.Target==ELDResultBuildTarget::Dash){Host=3;}
            else if(Card.Target==ELDResultBuildTarget::Active)
            {
                const FLDResultBuildActive* On=Row.Build.Actives.FindByPredicate([&Card](const FLDResultBuildActive& A){return A.DefinitionId==Card.TargetId;});
                Host=On&&On->Slot<3?On->Slot:4;
            }
            Seat.HostCards[Host].Add(Icon(Card.DefinitionId));
        }
        if(!Row.Build.FinalMod.IsNone()){Seat.FinalIcon=Icon(Row.Build.FinalMod);Seat.FinalName=LDHUD::ContentName(Content,Row.Build.FinalMod.ToString());}
        Seat.Points=Row.Build.StatPoints;
        if(Seat.bWinner){WinnerSeat=Index;}
        if(Seat.bSelf)
        {
            Self=Index;
            for(int32 Beat=0;Beat<Row.Growth.Num();++Beat)
            {
                const FLDResultGrowthPhase& Growth=Row.Growth[Beat];
                FPhase& Phase=Phases.AddDefaulted_GetRef();
                // Opening and R1–R3 include the following field; the last entry is the final field.
                Phase.Label=Beat==0?FString(TEXT("开局")):Beat>FLDTimelineParameters::TimedSegments?FString(TEXT("终局")):FString::Printf(TEXT("R%d"),Beat);
                for(const FLDResultBuildActive& Skill:Growth.Skills){Phase.Icons.Add(Icon(Skill.DefinitionId));}
                for(FName Card:Growth.AcquiredCards){Phase.Icons.Add(Icon(Card));}
                if(!Growth.FinalMod.IsNone()){Phase.Icons.Add(Icon(Growth.FinalMod));}
            }
        }
        Brush(FString::Printf(TEXT("seat_%d"),Index),LDUIBackgrounds::SeatAsset(Index));
    }
    const FSeat* Winner=WinnerSeat!=INDEX_NONE?&Seats[WinnerSeat]:nullptr;
    Verdict=LDHUD::ResultHeadline(Result,Winner?Winner->Name:FString());
    const int64 Seconds=FMath::Max<int64>(0,Result.DurationMs)/1000;
    Duration=FString::Printf(TEXT("本局用时 %lld:%02lld · 击杀不决定胜负"),Seconds/60,Seconds%60);
    // Where the own damage came from: the own summary of exactly this result (its top three sources).
    const LDHUD::FDamageListView Damage=LDHUD::DamageSummary(Summary,Result,Content);
    bSources=Damage.bAvailable;
    for(int32 Index=0;Index<Damage.Shares.Num()&&Sources.Num()<3;++Index)
    {
        const LDHUD::FDamageShareRow& Share=Damage.Shares[Index];
        Sources.Add({Share.IconId,Share.Glyph,Share.Name,Share.ValueText});
        Brush(Share.IconId,FString());
    }
    // The comparison starts on the winner (on the own win: the next best holder of the flag).
    if(bNewResult||!SeatOf(CompareWith)||(Self!=INDEX_NONE&&CompareWith==Seats[Self].Id))
    {
        int32 Pick=WinnerSeat!=INDEX_NONE&&WinnerSeat!=Self?WinnerSeat:INDEX_NONE;
        for(int32 Index=0;Pick==INDEX_NONE&&Index<Seats.Num();++Index)
        {
            if(Index==Self){continue;}
            int32 Best=INDEX_NONE;
            for(int32 Other=0;Other<Seats.Num();++Other){if(Other!=Self&&(Best==INDEX_NONE||Seats[Other].GuardSeconds>Seats[Best].GuardSeconds)){Best=Other;}}
            Pick=Best;
        }
        CompareWith=Pick!=INDEX_NONE?Seats[Pick].Id:FGuid();
    }
    Invalidate(EInvalidateWidgetReason::Paint);
}

void ULDResultReport::Compare(const FGuid& Contender)
{
    if(!SeatOf(Contender)||(Self!=INDEX_NONE&&Seats[Self].Id==Contender)){return;}
    CompareWith=Contender;
    Invalidate(EInvalidateWidgetReason::Paint);
}

FReply ULDResultReport::NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    const FVector2D At=(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())-Origin)/FMath::Max(Scale,.01f);
    for(const TPair<FBox2D,FGuid>& Pick:PickRects){if(Pick.Key.IsInside(At)){Compare(Pick.Value);return FReply::Handled();}}
    return Super::NativeOnMouseButtonDown(Geometry,Event);
}

int32 ULDResultReport::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const
{
    Layer=Super::NativePaint(Args,Geometry,Culling,Elements,Layer,Style,bEnabled);
    if(Seats.IsEmpty()){return Layer;}
    const FVector2D Size(Geometry.GetLocalSize());
    Scale=FMath::Min(float(Size.X)/DesignW,float(Size.Y)/DesignH);
    Origin=(Size-FVector2D(DesignW,DesignH)*Scale)*.5;
    const float S=Scale;
    const float Alpha=Reveal<0.f?1.f:FMath::Clamp(Reveal/.25f,0.f,1.f);
    const LDUIPaint::FContext P(Elements,Layer,Geometry,Alpha);
    // Design space to local.
    const auto At=[this](float X,float Y){return Origin+FVector2D(X,Y)*Scale;};
    const auto Box=[&At](float X0,float Y0,float X1,float Y1){return FBox2D(At(X0,Y0),At(X1,Y1));};
    const auto Label=[S](float Px,bool bHeavy=true){return LDBroadcastPaint::LabelFont(Px,bHeavy,S);};
    const auto Figure=[S](float Px){return LDBroadcastPaint::FigureFont(Px,S);};
    const auto Text=[&P](int32 L,const FString& Value,const FSlateFontInfo& Font,FVector2D Where,const FLinearColor& Ink,FVector2D Align=FVector2D(0.,.5))
    {return LDBroadcastPaint::Text(P,L,Value,Font,Where,Ink,Align);};
    const auto Picture=[&P,this](int32 L,const FString& Key,const FBox2D& Where,float Opacity=1.f)
    {
        const TSharedPtr<FSlateBrush>* Found=Brushes.Find(Key);
        if(!Found||!Found->IsValid()){LDBroadcastPaint::Block(P,L,Where,LDBroadcast::Raised);return;}
        FSlateDrawElement::MakeBox(P.Elements,L,P.Geometry.ToPaintGeometry(FVector2f(Where.GetSize()),FSlateLayoutTransform(FVector2f(Where.Min))),Found->Get(),
            ESlateDrawEffect::None,FLinearColor::White.CopyWithNewOpacity(P.Opacity*Opacity));
    };
    const auto Tile=[&](int32 L,const FString& IconId,float X,float Y,float Side)
    {
        const FBox2D Where=Box(X,Y,X+Side,Y+Side);
        LDBroadcastPaint::Block(P,L,Where,LDBroadcast::Panel);
        Picture(L+1,IconId,Where);
        LDBroadcastPaint::Frame(P,L+2,Where,LDBroadcast::White.CopyWithNewOpacity(.28f),2.f*S);
    };
    const auto Plate=[&](int32 L,float X,float Y,float H,int32 Seat){LDBroadcastPaint::SeatPlate(P,L,Box(X,Y,X+H*1.15f,Y+H),Seat,S,H*.9f);};
    const FSeat* Own=Self!=INDEX_NONE?&Seats[Self]:nullptr;

    // ---- header: LIVE FINAL, the verdict with the winner's plate, the match's length ----------------------------------
    LDBroadcastPaint::FillSlab(P,Layer+1,Box(72.f,40.f,170.f,84.f),LDBroadcast::LiveRed);
    Text(Layer+2,TEXT("FINAL"),Figure(30.f),At(121.f,62.f),LDBroadcast::White,FVector2D(.5,.5));
    float HeadX=192.f;
    if(WinnerSeat!=INDEX_NONE){Plate(Layer+1,HeadX,42.f,40.f,WinnerSeat);HeadX+=60.f;}
    Text(Layer+2,Verdict,Label(30.f),At(HeadX,62.f),LDBroadcast::White);
    Text(Layer+2,Duration,Label(20.f,false),At(1848.f,62.f),LDBroadcast::Grey,FVector2D(1.,.5));

    // ---- left: the own seat (the page's first read) ---------------------------------------------------------------------
    if(Own)
    {
        const FBox2D Card=Box(72.f,120.f,542.f,940.f);
        LDBroadcastPaint::Block(P,Layer+1,Card,LDBroadcast::Raised);
        Picture(Layer+2,FString::Printf(TEXT("seat_%d"),Own->Seat),Box(72.f,120.f,542.f,640.f));
        // The portrait fades into the card under it.
        for(int32 Step=0;Step<12;++Step)
        {
            const float Y0=460.f+Step*15.f;
            LDBroadcastPaint::Block(P,Layer+3,Box(72.f,Y0,542.f,Y0+15.f),LDBroadcast::Raised.CopyWithNewOpacity(float(Step+1)/12.f));
        }
        Text(Layer+4,FString::FromInt(Own->Seat+1),Figure(150.f),At(96.f,200.f),LDBroadcast::SeatInk(uint8(Own->Seat)));
        const int32 GuardRank=RankOf([](const FSeat& Seat){return Seat.GuardSeconds;},Self);
        const bool bChampion=Own->bWinner;
        const FString Standing=bChampion?FString(TEXT("冠军")):Own->GuardSeconds>0.?FString::Printf(TEXT("守旗第 %d"),GuardRank):FString(TEXT("没守到旗"));
        Text(Layer+4,Standing,Label(bChampion?88.f:72.f),At(96.f,560.f),bChampion?LDBroadcast::Gold:LDBroadcast::White);
        Text(Layer+4,Own->Name,Label(28.f),At(98.f,624.f),LDBroadcast::White);
        // The three figures and their ranks among every seat.
        const struct{const TCHAR* Name;FString Value;int32 Rank;} Figures[3]={
            {TEXT("守旗秒数"),FString::Printf(TEXT("%.1f"),Own->GuardSeconds),Own->GuardSeconds>0.?GuardRank:0},
            {TEXT("对选手伤害"),Own->bCombat?Whole(Own->Damage):FString(TEXT("—")),Own->Damage>0.?RankOf([](const FSeat& Seat){return Seat.Damage;},Self):0},
            {TEXT("击杀/阵亡/助攻"),FString::Printf(TEXT("%lld/%lld/%lld"),Own->Kills,Own->Deaths,Own->Assists),
                Own->Kills+Own->Assists>0?RankOf([](const FSeat& Seat){return double(Seat.Kills+Seat.Assists);},Self):0}};
        for(int32 Index=0;Index<3;++Index)
        {
            const float X0=96.f+Index*146.f;
            LDBroadcastPaint::Block(P,Layer+4,Box(X0,664.f,X0+136.f,780.f),LDBroadcast::Ink);
            LDBroadcastPaint::Block(P,Layer+5,Box(X0,664.f,X0+136.f,667.f),LDBroadcast::White);
            if(Figures[Index].Rank>0){Text(Layer+5,FString::Printf(TEXT("#%d"),Figures[Index].Rank),Figure(24.f),At(X0+126.f,690.f),LDBroadcast::Orange,FVector2D(1.,.5));}
            Text(Layer+5,Figures[Index].Value,Figure(Index==2?38.f:46.f),At(X0+12.f,724.f),LDBroadcast::White);
            Text(Layer+5,Figures[Index].Name,Label(17.f,false),At(X0+12.f,762.f),LDBroadcast::Grey);
        }
        Text(Layer+4,TEXT("你的伤害来自"),Label(20.f),At(96.f,812.f),LDBroadcast::Grey);
        if(!bSources){Text(Layer+4,TEXT("伤害统计不可用"),Label(20.f,false),At(96.f,850.f),LDBroadcast::Mute);}
        else if(Sources.IsEmpty()){Text(Layer+4,TEXT("本局没有造成伤害"),Label(20.f,false),At(96.f,850.f),LDBroadcast::Mute);}
        for(int32 Index=0;Index<Sources.Num();++Index)
        {
            const float Y=834.f+Index*34.f;
            if(Brushes.Contains(Sources[Index].IconId)){Tile(Layer+4,Sources[Index].IconId,96.f,Y,30.f);}
            else{LDBroadcastPaint::Block(P,Layer+4,Box(96.f,Y,126.f,Y+30.f),LDBroadcast::Panel);Text(Layer+5,Sources[Index].Glyph,Label(18.f),At(111.f,Y+15.f),LDBroadcast::White,FVector2D(.5,.5));}
            Text(Layer+5,Sources[Index].Name,Label(20.f,false),At(138.f,Y+15.f),LDBroadcast::White);
            Text(Layer+5,Sources[Index].Value,Figure(24.f),At(518.f,Y+15.f),LDBroadcast::White,FVector2D(1.,.5));
        }
    }

    // ---- middle: four rankings, every seat (lighter than the own card) ---------------------------------------------------
    {
        const struct{const TCHAR* Name;TFunction<double(const FSeat&)> Value;bool bTenths;} Charts[4]={
            {TEXT("守旗秒数"),[](const FSeat& Seat){return Seat.GuardSeconds;},true},
            {TEXT("对选手伤害"),[](const FSeat& Seat){return Seat.Damage;},false},
            {TEXT("承伤"),[](const FSeat& Seat){return Seat.Taken;},false},
            {TEXT("击杀 + 助攻"),[](const FSeat& Seat){return double(Seat.Kills+Seat.Assists);},false}};
        for(int32 Chart=0;Chart<4;++Chart)
        {
            const float X0=570.f+(Chart%2)*326.f,Y0=120.f+(Chart/2)*410.f;
            LDBroadcastPaint::Block(P,Layer+1,Box(X0,Y0,X0+314.f,Y0+398.f),LDBroadcast::Panel.CopyWithNewOpacity(.9f));
            Text(Layer+2,Charts[Chart].Name,Label(22.f),At(X0+16.f,Y0+30.f),LDBroadcast::Grey);
            if(Self!=INDEX_NONE&&Charts[Chart].Value(Seats[Self])>0.){Text(Layer+2,FString::Printf(TEXT("#%d"),RankOf(Charts[Chart].Value,Self)),Figure(28.f),At(X0+298.f,Y0+30.f),LDBroadcast::Orange,FVector2D(1.,.5));}
            TArray<int32> Order;for(int32 Index=0;Index<Seats.Num();++Index){Order.Add(Index);}
            Order.StableSort([&](int32 A,int32 B){return Charts[Chart].Value(Seats[A])>Charts[Chart].Value(Seats[B]);});
            double Top=0.;for(const FSeat& Seat:Seats){Top=FMath::Max(Top,Charts[Chart].Value(Seat));}
            for(int32 Row=0;Row<Order.Num()&&Row<8;++Row)
            {
                const FSeat& Seat=Seats[Order[Row]];
                const float Y=Y0+58.f+Row*41.f;
                Plate(Layer+2,X0+16.f,Y,28.f,Seat.Seat);
                const double Value=Charts[Chart].Value(Seat);
                const FBox2D Track=Box(X0+56.f,Y+7.f,X0+226.f,Y+21.f);
                LDBroadcastPaint::Block(P,Layer+2,Track,LDBroadcast::Ink);
                const FLinearColor Ink=Seat.bSelf?LDBroadcast::Orange:Seat.bWinner?LDBroadcast::Gold:LDBroadcast::Mute;
                if(Top>0.&&Value>0.){LDBroadcastPaint::Block(P,Layer+3,FBox2D(Track.Min,FVector2D(LDBroadcastPaint::BarX(Track,float(Value/Top)),Track.Max.Y)),Ink);}
                Text(Layer+3,Charts[Chart].bTenths?FString::Printf(TEXT("%.1f"),Value):Whole(Value),Figure(24.f),At(X0+298.f,Y+14.f),
                    Seat.bSelf?LDBroadcast::Orange:Value>0.?LDBroadcast::White:LDBroadcast::Mute,FVector2D(1.,.5));
            }
        }
    }

    // ---- right: the own build beside another seat's ----------------------------------------------------------------------
    PickRects.Reset();
    const FSeat* Other=SeatOf(CompareWith);
    {
        LDBroadcastPaint::Block(P,Layer+1,Box(1238.f,120.f,1848.f,940.f),LDBroadcast::Panel.CopyWithNewOpacity(.9f));
        Text(Layer+2,TEXT("构筑对比"),Label(24.f),At(1256.f,152.f),LDBroadcast::White);
        // The other seats' plates: the compared one lit, a click picks another.
        float PickX=1840.f;
        for(int32 Index=Seats.Num()-1;Index>=0;--Index)
        {
            if(Index==Self){continue;}
            PickX-=40.f;
            const bool bOn=Other&&Seats[Index].Id==Other->Id;
            const FBox2D Where(FVector2D(PickX,136.f),FVector2D(PickX+34.f,166.f));
            LDBroadcastPaint::SeatPlate(bOn?P:P.WithOpacity(.38f),Layer+2,Box(PickX,136.f,PickX+34.f,166.f),Seats[Index].Seat,S,26.f);
            if(bOn){LDBroadcastPaint::Frame(P,Layer+3,Box(PickX-3.f,133.f,PickX+37.f,169.f),LDBroadcast::White,2.f*S);}
            PickRects.Add({Where,Seats[Index].Id});
        }
        const float ColA=1316.f,ColB=1590.f;
        const auto Head=[&](float X,const FSeat* Seat,bool bSelfColumn)
        {
            if(!Seat){return;}
            Plate(Layer+2,X,190.f,26.f,Seat->Seat);
            Text(Layer+2,bSelfColumn?FString(TEXT("你")):Seat->Name,Label(20.f),At(X+38.f,203.f),LDBroadcast::White);
            if(Seat->bWinner&&!bSelfColumn)
            {
                LDBroadcastPaint::FillSlab(P,Layer+2,Box(X+188.f,192.f,X+250.f,216.f),LDBroadcast::Gold);
                Text(Layer+3,TEXT("冠军"),Label(16.f),At(X+219.f,204.f),LDBroadcast::Ink,FVector2D(.5,.5));
            }
            LDBroadcastPaint::Block(P,Layer+2,Box(X,224.f,X+250.f,226.f),LDBroadcast::White.CopyWithNewOpacity(.2f));
        };
        Head(ColA,Own,true);Head(ColB,Other,false);
        const auto HostRow=[&](float X,const FSeat* Seat,int32 Host,float Y)
        {
            if(!Seat){return;}
            if(!Seat->bBuild){if(Host==0){Text(Layer+2,TEXT("构筑不可用"),Label(18.f,false),At(X,Y+24.f),LDBroadcast::Mute);}return;}
            float TX=X;
            if(Host<3&&!Seat->HostActive[Host].IsEmpty()){Tile(Layer+2,Seat->HostActive[Host],TX,Y,50.f);TX+=56.f;}
            for(int32 Card=0;Card<Seat->HostCards[Host].Num()&&TX<X+250.f;++Card){Tile(Layer+2,Seat->HostCards[Host][Card],TX,Y+(Host<3?6.f:0.f),Host<3?38.f:50.f);TX+=Host<3?42.f:56.f;}
        };
        for(int32 Host=0;Host<5;++Host)
        {
            const float Y=242.f+Host*62.f;
            Text(Layer+2,HostKeys[Host],Host<4?Figure(28.f):Label(24.f),At(1262.f,Y+25.f),LDBroadcast::Grey);
            HostRow(ColA,Own,Host,Y);HostRow(ColB,Other,Host,Y);
        }
        const float ModY=242.f+5*62.f;
        Text(Layer+2,TEXT("改"),Label(24.f),At(1262.f,ModY+25.f),LDBroadcast::Grey);
        for(const TPair<float,const FSeat*>& Column:{TPair<float,const FSeat*>(ColA,Own),TPair<float,const FSeat*>(ColB,Other)})
        {
            if(!Column.Value){continue;}
            if(Column.Value->FinalIcon.IsEmpty()){Text(Layer+2,TEXT("未改装"),Label(18.f,false),At(Column.Key,ModY+25.f),LDBroadcast::Mute);continue;}
            Tile(Layer+2,Column.Value->FinalIcon,Column.Key,ModY,50.f);
            Text(Layer+3,Column.Value->FinalName,Label(18.f,false),At(Column.Key+60.f,ModY+25.f),LDBroadcast::White);
        }
        // The stat points: the own (orange) over the other's (gold or white), against the keystone line.
        for(int32 Stat=0;Stat<StatNames.Num();++Stat)
        {
            const float Y=640.f+Stat*40.f;
            Text(Layer+2,StatNames[Stat],Label(20.f,false),At(1262.f,Y+12.f),LDBroadcast::White);
            const FBox2D Track=Box(1350.f,Y+2.f,1730.f,Y+22.f);
            LDBroadcastPaint::Block(P,Layer+2,Track,LDBroadcast::Ink);
            const float Mark=LDBroadcastPaint::BarX(Track,float(Keystone)/22.f);
            LDBroadcastPaint::Block(P,Layer+3,FBox2D(FVector2D(Mark-1.f*S,Track.Min.Y),FVector2D(Mark+1.f*S,Track.Max.Y)),LDBroadcast::White.CopyWithNewOpacity(.35f));
            const int32 Mine=Own&&Own->Points.IsValidIndex(Stat)?Own->Points[Stat]:0;
            const int32 Theirs=Other&&Other->Points.IsValidIndex(Stat)?Other->Points[Stat]:0;
            const float Mid=float(Track.GetCenter().Y);
            LDBroadcastPaint::Block(P,Layer+3,FBox2D(Track.Min,FVector2D(LDBroadcastPaint::BarX(Track,Mine/22.f),Mid-1.f*S)),LDBroadcast::Orange);
            LDBroadcastPaint::Block(P,Layer+3,FBox2D(FVector2D(Track.Min.X,Mid+1.f*S),FVector2D(LDBroadcastPaint::BarX(Track,Theirs/22.f),Track.Max.Y)),
                Other&&Other->bWinner?LDBroadcast::Gold:LDBroadcast::White);
            Text(Layer+3,FString::FromInt(Mine),Figure(26.f),At(1790.f,Y+12.f),LDBroadcast::Orange,FVector2D(1.,.5));
            Text(Layer+3,FString::FromInt(Theirs),Figure(26.f),At(1836.f,Y+12.f),Other&&Other->bWinner?LDBroadcast::Gold:LDBroadcast::White,FVector2D(1.,.5));
        }
    }

    // ---- bottom: the own growth over the five beats ----------------------------------------------------------------------
    for(int32 Beat=0;Beat<Phases.Num()&&Beat<5;++Beat)
    {
        const float X0=72.f+Beat*218.f;
        LDBroadcastPaint::Block(P,Layer+1,Box(X0,960.f,X0+210.f,1056.f),LDBroadcast::Panel.CopyWithNewOpacity(.9f));
        Text(Layer+2,Phases[Beat].Label,Label(18.f),At(X0+10.f,976.f),Beat==Phases.Num()-1?LDBroadcast::White:LDBroadcast::Grey);
        float TX=X0+10.f;
        for(int32 Index=0;Index<Phases[Beat].Icons.Num()&&TX+44.f<=X0+206.f;++Index){Tile(Layer+2,Phases[Beat].Icons[Index],TX,994.f,44.f);TX+=48.f;}
        if(Phases[Beat].Icons.IsEmpty()){Text(Layer+2,TEXT("—"),Label(20.f,false),At(X0+12.f,1016.f),LDBroadcast::Mute);}
    }
    return Layer+8;
}
