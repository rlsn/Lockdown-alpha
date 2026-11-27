#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Framework/LDCaptureTypes.h"
#include "UI/LDHUDModel.h"
#include "LDResultReport.generated.h"

struct FLDDemoContentDefinitions;
struct FLDDamageSummaryView;
class UTexture2D;

// The match's result page ("my report", approved composition B): a header with the verdict; three columns of falling
// weight: the own seat (portrait, the standing, the three figures with their ranks, where the own damage came from), four
// small rankings (every seat, the own bar orange, the winner's gold), and the own build beside another seat's (the winner by
// default; its seat plates pick another); the own growth over the five beats under them. Painted at one fixed 1920 x 1080
// design layout scaled to the widget, so it never re-lays out and never scrolls. Reads the frozen public result, the own
// damage summary of that result and the display catalog only.
UCLASS()
class LOCKDOWN_API ULDResultReport : public UUserWidget
{
    GENERATED_BODY()
public:
    // Rebuilds the page's model when the result, the own seat, the summary or the catalog changed; otherwise nothing.
    void Present(const FLDResultView& Result,const FGuid& Self,const FLDDamageSummaryView& Summary,const FLDDemoContentDefinitions* Content);
    // The seat whose build stands beside the own one (a result row's contender); unknown ids are ignored.
    void Compare(const FGuid& Contender);
    const FGuid& Compared() const {return CompareWith;}
    // Fades the page in from the result's arrival (seconds since); <0 shows it whole.
    void SetAge(float Age){Reveal=Age;}
    // The design-space seat plates the comparison picks from (probes press them through the real hit test).
    int32 ComparePlateCount() const {return PickRects.Num();}
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
private:
    struct FSeat
    {
        FGuid Id;
        int32 Seat=0;
        FString Name;
        bool bWinner=false,bSelf=false,bLeft=false;
        double GuardSeconds=0.,Damage=0.,Taken=0.;
        int64 Kills=0,Deaths=0,Assists=0;
        uint8 Level=0;
        bool bCombat=false,bBuild=false;
        // The build: per host (Q, W, E, D, 本体) the learned active (W/E/Q only) and the installed cards' icon ids.
        FString HostActive[5];
        TArray<FString> HostCards[5];
        FString FinalIcon,FinalName;
        TArray<int32> Points;
    };
    struct FSource{FString IconId,Glyph,Name,Value;};
    struct FPhase{FString Label;TArray<FString> Icons;};
    TArray<FSeat> Seats;
    int32 Self=INDEX_NONE;
    FGuid CompareWith;
    FString Verdict,Duration;
    int32 WinnerSeat=INDEX_NONE;
    bool bSources=false;
    TArray<FSource> Sources;
    TArray<FPhase> Phases;
    TArray<FString> StatNames;
    int32 Keystone=18;
    float Reveal=-1.f;
    // What the model was built from.
    FGuid ShownResult;uint64 ShownSerial=0;FGuid ShownSelf;uint64 ShownSummary=0;const void* ShownContent=nullptr;
    // The comparison's seat plates as painted (design space), for the click.
    mutable TArray<TPair<FBox2D,FGuid>> PickRects;
    mutable FVector2D Origin=FVector2D::ZeroVector;
    mutable float Scale=1.f;
    // Textures by icon id or seat (held so the brushes stay valid).
    UPROPERTY() TMap<FString,TObjectPtr<UTexture2D>> Textures;
    TMap<FString,TSharedPtr<FSlateBrush>> Brushes;
    const FSlateBrush* Brush(const FString& Key,const FString& Asset);
    const FSeat* SeatOf(const FGuid& Id) const;
    int32 RankOf(TFunctionRef<double(const FSeat&)> Value,int32 Index) const;
};
