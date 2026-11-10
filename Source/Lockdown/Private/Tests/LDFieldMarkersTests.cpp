#include "World/LDFieldMarkers.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDCaptureTypes.h"
#include "Misc/AutomationTest.h"
#include "Tests/LDTestApprovedFixture.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace LDFieldMarkersTests
{
// Test-only candidates around a centre at the origin: one High camp per quadrant and Elite camps chosen so the opposite
// quadrant rule has something to prefer. The positions are not the approved world's.
FLDFieldMarkerState MakeState(const FLDDemoContentParameters& P)
{
    FLDFieldMarkerState State;
    FLDFieldMarkerSchedule& S = State.Schedule;
    S.StartedAtMs = 0;
    S.SegmentMs = P.FieldSegmentMs;
    S.RevealOffsetMs = P.CampMarkRevealMs;
    S.Segments = FLDTimelineParameters::TimedSegments;
    S.Seed = 0x5EED5EEDull;
    S.CentreCm = FVector::ZeroVector;
    S.High = {{TEXT("h_ne"), FVector(1000, 1000, 0)}, {TEXT("h_nw"), FVector(-1000, 1000, 0)},
        {TEXT("h_sw"), FVector(-1000, -1000, 0)}, {TEXT("h_se"), FVector(1000, -1000, 0)}};
    S.Elite = {{TEXT("e_ne"), FVector(3000, 3000, 0)}, {TEXT("e_sw1"), FVector(-3000, -2000, 0)},
        {TEXT("e_sw2"), FVector(-2000, -3000, 0)}, {TEXT("e_nw"), FVector(-3000, 3000, 0)}};
    return State;
}

FLDPublicMatchView ViewAt(const FLDFieldMarkerState& State, int64 NowMs, ELDMatchStage Stage = ELDMatchStage::FinalField)
{
    FLDPublicMatchView View;
    View.Generation = 1;
    View.PublicRevision = 1;
    View.Phase = Stage == ELDMatchStage::Rest ? ELDMatchPhase::Rest : ELDMatchPhase::Development;
    View.ServerNowMs = NowMs;
    View.OpenAtMs = 10000000;
    // A field running since T=0 at authority 0 makes T equal NowMs; a rest freezes T at NowMs.
    View.Timeline.Generation = View.Generation;
    View.Timeline.Stage = Stage;
    View.Timeline.FieldEndT = -1;
    if (Stage == ELDMatchStage::Rest)
    {
        View.Timeline.Step = ELDRestStep::Build;
        View.Timeline.FieldBaseMs = NowMs;
    }
    State.View(View.Markers);
    return View;
}

FName CampOf(const FLDFieldMarkerState& State, const FLDFieldMarker& Marker)
{
    const LDFieldMarkers::FCandidate* Camp = State.CampOf(Marker);
    return Camp ? Camp->CampId : NAME_None;
}
}

// #198 outer §5 camp markers (approved https://github.com/rlsn/Lockdown-alpha/issues/198#issuecomment-6752601175, comment
// 6752628806: "批准整套方案，按票内参数实施"): one High and one Elite camp marked at T = 15/195/375 s, the Elite camp
// preferred in the quadrant opposite the High camp, never the previous segment's camp, taken once by the first clear
// and gone at the segment's end; none in the final field.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDFieldMarkersTest, "Lockdown.Rules.DemoFieldMarkers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDFieldMarkersTest::RunTest(const FString& Parameters)
{
    using namespace LDFieldMarkersTests;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(*this, Catalog)) { return false; }
    const FLDDemoContentParameters& P = Catalog->Get().Parameters;
    bool bGood = TestTrue(TEXT("Markers are revealed 15 s into each 180 s field segment"),
        P.CampMarkRevealMs == 15000 && P.FieldSegmentMs == 180000);
    FLDFieldMarkerState State = MakeState(P);
    if (!TestTrue(TEXT("The test schedule is valid"), State.IsActive())) { return false; }
    int64 At = 0;
    bGood &= TestTrue(TEXT("Reveals at T=15/195/375 s, segment ends at 180/360/540 s, no fourth segment"),
        State.Schedule.RevealAtMs(1, At) && At == 15000 && State.Schedule.RevealAtMs(2, At) && At == 195000
        && State.Schedule.RevealAtMs(3, At) && At == 375000 && State.Schedule.EndAtMs(3, At) && At == 540000
        && !State.Schedule.RevealAtMs(4, At) && !State.Schedule.RevealAtMs(0, At));

    // Quadrants around the centre and the draws.
    const FVector Centre = FVector::ZeroVector;
    bGood &= TestTrue(TEXT("Opposite quadrants flip both signs"),
        (LDFieldMarkers::Quadrant(FVector(1, 1, 0), Centre) ^ 3) == LDFieldMarkers::Quadrant(FVector(-1, -1, 0), Centre)
        && (LDFieldMarkers::Quadrant(FVector(-1, 1, 0), Centre) ^ 3) == LDFieldMarkers::Quadrant(FVector(1, -1, 0), Centre));
    {
        const TArray<LDFieldMarkers::FCandidate>& Elite = State.Schedule.Elite;
        bool bOpposite = true, bSaw1 = false, bSaw2 = false, bNotPrevious = true, bFallback = true, bFallbackNotPrevious = true;
        TArray<LDFieldMarkers::FCandidate> NoneOpposite = {Elite[0], Elite[3]};
        for (uint64 Seed = 1; Seed <= 200; ++Seed)
        {
            const int32 Pick = LDFieldMarkers::PickElite(Seed, 1, FVector(1000, 1000, 0), Centre, Elite, INDEX_NONE);
            bOpposite &= Pick == 1 || Pick == 2;
            bSaw1 |= Pick == 1;
            bSaw2 |= Pick == 2;
            bNotPrevious &= LDFieldMarkers::PickElite(Seed, 2, FVector(1000, 1000, 0), Centre, Elite, 1) == 2;
            const int32 Any = LDFieldMarkers::PickElite(Seed, 3, FVector(1000, 1000, 0), Centre, NoneOpposite, 0);
            bFallback &= Any == 0 || Any == 1;
            bFallbackNotPrevious &= Any == 1;
        }
        bGood &= TestTrue(TEXT("The Elite camp is drawn uniformly among the opposite quadrant's camps"), bOpposite && bSaw1 && bSaw2);
        bGood &= TestTrue(TEXT("The previous segment's Elite camp is never drawn again"), bNotPrevious);
        bGood &= TestTrue(TEXT("Without an opposite camp any Elite camp but the previous one is drawn"), bFallback && bFallbackNotPrevious);
        TArray<int32> Uses;
        Uses.Init(0, 4);
        bool bHighNotPrevious = true;
        for (uint64 Seed = 1; Seed <= 400; ++Seed)
        {
            const int32 Previous = int32(Seed % 4);
            const int32 Pick = LDFieldMarkers::PickHigh(Seed, 2, Previous, 4);
            bHighNotPrevious &= Pick >= 0 && Pick < 4 && Pick != Previous;
            if (Pick >= 0 && Pick < 4) { ++Uses[Pick]; }
        }
        bGood &= TestTrue(TEXT("The High camp is never the previous one and every camp is drawn"),
            bHighNotPrevious && !Uses.Contains(0));
    }

    // Segment 1: nothing before the reveal, then both markers at once.
    TArray<FLDFieldMarker> Revealed, Expired;
    bGood &= TestTrue(TEXT("Before the first reveal nothing is public and the reveal is the next instant"),
        State.NextDueMs(0) == 15000 && !State.Advance(14999, Revealed, Expired) && State.Segment == 0);
    TArray<FLDFieldMarkerView> Rows;
    State.View(Rows);
    bGood &= TestTrue(TEXT("No marker before the first reveal"), Rows.IsEmpty());
    bGood &= TestTrue(TEXT("At T=15 s one High and one Elite camp are revealed"), State.Advance(15000, Revealed, Expired)
        && State.Segment == 1 && Revealed.Num() == 2 && Expired.IsEmpty() && State.High.Phase == ELDFieldMarkerPhase::Revealed
        && State.Elite.Phase == ELDFieldMarkerPhase::Revealed);
    const FName High1 = CampOf(State, State.High), Elite1 = CampOf(State, State.Elite);
    const FLDFieldMarkerState Again = [&P]()
    {
        FLDFieldMarkerState Copy = MakeState(P);
        TArray<FLDFieldMarker> R, E;
        Copy.Advance(15000, R, E);
        return Copy;
    }();
    bGood &= TestTrue(TEXT("The same server seed draws the same camps"),
        CampOf(Again, Again.High) == High1 && CampOf(Again, Again.Elite) == Elite1);
    State.View(Rows);
    bGood &= TestTrue(TEXT("The public view lists High then Elite with their camp, segment and instants"),
        Rows.Num() == 2 && Rows[0].Kind == ELDFieldMarkerKind::High && Rows[0].CampId == High1 && Rows[1].Kind == ELDFieldMarkerKind::Elite
        && Rows[1].CampId == Elite1 && Rows[0].Segment == 1 && Rows[0].RevealAtMs == 15000 && Rows[0].EndAtMs == 180000);
    FString Error;
    bGood &= TestTrue(TEXT("Revealed markers are a valid public view: ") + Error, LDCapture::ValidatePublicMatchView(ViewAt(State, 20000), Error));
    bGood &= TestFalse(TEXT("A marker cannot be public before its reveal"), LDCapture::ValidatePublicMatchView(ViewAt(State, 14999), Error));

    // The first clear takes it once; a camp that is not marked takes nothing.
    bGood &= TestTrue(TEXT("Only the marked camps are open, inside their segment"),
        State.OpenMarkerFor(High1, 20000) == &State.High && State.OpenMarkerFor(Elite1, 20000) == &State.Elite
        && !State.OpenMarkerFor(TEXT("h_unmarked"), 20000) && !State.OpenMarkerFor(High1, 180000) && !State.OpenMarkerFor(High1, 14999));
    const FGuid First(1, 0, 0, 0), Second(2, 0, 0, 0);
    bGood &= TestTrue(TEXT("The first clear claims the High marker"), State.Claim(ELDFieldMarkerKind::High, First, 30000)
        && State.High.Phase == ELDFieldMarkerPhase::Claimed && State.High.ClaimedBy == First);
    bGood &= TestTrue(TEXT("A claimed marker is not offered again in its segment"),
        !State.Claim(ELDFieldMarkerKind::High, Second, 31000) && !State.OpenMarkerFor(High1, 31000) && State.High.ClaimedBy == First);
    State.View(Rows);
    bGood &= TestTrue(TEXT("The public view only says it is taken"), Rows[0].Phase == ELDFieldMarkerPhase::Claimed
        && LDCapture::ValidatePublicMatchView(ViewAt(State, 31000), Error));
    bGood &= TestTrue(TEXT("While the Elite marker is open the segment end is due"), State.NextDueMs(31000) == 180000);

    // Segment end (the rest entry): the unclaimed marker expires without any reward.
    State.EndSegment(Expired);
    bGood &= TestTrue(TEXT("At the segment end the unclaimed marker expires"), Expired.Num() == 1
        && Expired[0].Kind == ELDFieldMarkerKind::Elite && State.Elite.Phase == ELDFieldMarkerPhase::Expired
        && !State.Claim(ELDFieldMarkerKind::Elite, Second, 180000) && !State.OpenMarkerFor(Elite1, 179999));
    bGood &= TestTrue(TEXT("Expired and claimed markers are a valid rest view: ") + Error,
        LDCapture::ValidatePublicMatchView(ViewAt(State, 180000, ELDMatchStage::Rest), Error));
    {
        FLDFieldMarkerState Open = MakeState(P);
        Open.Advance(15000, Revealed, Expired);
        bGood &= TestFalse(TEXT("A revealed marker cannot outlive its segment into the rest"),
            LDCapture::ValidatePublicMatchView(ViewAt(Open, 180000, ELDMatchStage::Rest), Error));
        bGood &= TestTrue(TEXT("Reaching the segment end in the field expires both open markers"),
            Open.Advance(180000, Revealed, Expired) && Expired.Num() == 2 && Open.Segment == 1);
    }

    // Segments 2 and 3: a new draw each, never the previous segment's camps.
    bGood &= TestTrue(TEXT("The next reveal is T=195 s"), State.NextDueMs(180000) == 195000);
    bGood &= TestTrue(TEXT("At T=195 s segment 2 is revealed"), State.Advance(195000, Revealed, Expired) && State.Segment == 2
        && State.High.Phase == ELDFieldMarkerPhase::Revealed && State.Elite.Phase == ELDFieldMarkerPhase::Revealed);
    const FName High2 = CampOf(State, State.High), Elite2 = CampOf(State, State.Elite);
    bGood &= TestTrue(TEXT("Segment 2 marks other camps than segment 1"), High2 != High1 && Elite2 != Elite1);
    State.EndSegment(Expired);
    bGood &= TestTrue(TEXT("At T=375 s segment 3 is revealed"), State.Advance(375000, Revealed, Expired) && State.Segment == 3);
    const FName High3 = CampOf(State, State.High), Elite3 = CampOf(State, State.Elite);
    bGood &= TestTrue(TEXT("Segment 3 marks other camps than segment 2"), High3 != High2 && Elite3 != Elite2);
    State.EndSegment(Expired);
    bGood &= TestTrue(TEXT("The final field has no marker: nothing more is due or revealed"),
        State.NextDueMs(540000) == MAX_int64 && !State.Advance(900000, Revealed, Expired) && State.Segment == 3
        && State.High.Phase == ELDFieldMarkerPhase::Expired && State.Elite.Phase == ELDFieldMarkerPhase::Expired);

    FLDFieldMarkerState Invalid = MakeState(P);
    Invalid.Schedule.Elite.SetNum(1);
    bGood &= TestFalse(TEXT("A tier with a single camp cannot avoid repeating it"), Invalid.IsActive());
    Invalid = MakeState(P);
    Invalid.Schedule.RevealOffsetMs = Invalid.Schedule.SegmentMs;
    bGood &= TestFalse(TEXT("A reveal outside its segment is not valid"), Invalid.IsActive());
    bGood &= TestTrue(TEXT("The reward events are the growth domain's marker events"),
        LDFieldMarkers::RewardEvent(ELDFieldMarkerKind::High) == FName(TEXT("camp_marker_high"))
        && LDFieldMarkers::RewardEvent(ELDFieldMarkerKind::Elite) == FName(TEXT("camp_marker_elite")));
    return bGood;
}
#endif
