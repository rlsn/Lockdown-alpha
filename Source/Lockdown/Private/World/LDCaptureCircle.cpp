#include "World/LDCaptureCircle.h"
#include "Development/LDDemoContentCatalog.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapRegistry.h"
#include "Components/PrimitiveComponent.h"

namespace
{
    double CaptureDistanceSquaredXY(const FVector& Foot, const FVector2D& Center)
    {
        const double X = Foot.X - Center.X;
        const double Y = Foot.Y - Center.Y;
        return X * X + Y * Y;
    }
}

bool FLDCaptureCircle::IsValid() const
{
    return !CaptureId.IsNone() && FMath::IsFinite(RadiusCm) && RadiusCm > 0
        && FMath::IsFinite(CenterCm.X) && FMath::IsFinite(CenterCm.Y) && Ground.IsValid();
}

bool FLDCaptureCircle::Supports(const FLDNativeSupportSample& Support) const
{
    return Support.Grounded && Support.Floor.IsValid() && Support.Floor == Ground;
}

bool FLDCaptureCircle::Contains(const FVector& FootCm, const FLDNativeSupportSample& Support) const
{
    return Supports(Support) && CaptureDistanceSquaredXY(FootCm, CenterCm) <= RadiusCm * RadiusCm;
}

bool LDCaptureCircle::Resolve(const ULDMapRegistry& Registry, const FLDDemoContentSnapshot& Content,
    FLDCaptureCircle& Out, FString& Error)
{
    Out = {};
    const auto* Geometry = Registry.GetGeometry();
    const FString& CaptureId = Content.Get().World.CaptureId;
    if (!Registry.Enabled() || !Geometry || CaptureId.IsEmpty())
    {
        Error = TEXT("The capture circle has no registered Demo world.");
        return false;
    }
    const auto* Anchor = Geometry->Anchors.FindByPredicate([&](const FLDDemoWorldAnchor& Row)
    { return Row.Id == CaptureId && Row.Kind == ELDDemoWorldAnchorKind::Capture; });
    const FName Id(*CaptureId);
    const FLDMapBinding* Binding = Registry.Resolve(Id, Registry.Generation);
    if (!Anchor || !Binding || !Binding->Floor.IsValid())
    {
        Error = TEXT("The unique compiled capture anchor is not bound to registered ground.");
        return false;
    }
    const auto& Radius = Content.Get().Parameters.CaptureRadiusCm;
    const double NativeRadius = Radius.Denominator > 0 ? double(Radius.Numerator) / double(Radius.Denominator) : 0.;
    if (!FMath::IsFinite(NativeRadius) || NativeRadius <= 0)
    {
        Error = TEXT("The approved capture radius is invalid.");
        return false;
    }
    Out.CaptureId = Id;
    Out.CenterCm = FVector2D(Binding->Position.X, Binding->Position.Y);
    Out.RadiusCm = NativeRadius;
    Out.Ground = Binding->Floor;
    if (!Out.IsValid())
    {
        Out = {};
        Error = TEXT("The capture anchor position is not finite.");
        return false;
    }
    Error.Reset();
    return true;
}

bool LDCaptureCircle::Extract(const FLDCaptureCircle& Circle, const FLDNativeMotionBatch& Batch,
    bool& InOutInside, TArray<FLDCaptureCrossing>& Out, FString& Error)
{
    Out.Reset();
    if (!Circle.IsValid() || Batch.StartFootCm.ContainsNaN() || Batch.EndFootCm.ContainsNaN())
    {
        Error = TEXT("Capture extraction requires a resolved circle and finite native motion.");
        return false;
    }
    FLDNativeSupportSample Support = Batch.InitialSupport;
    FVector Foot = Batch.StartFootCm;
    bool Inside = Circle.Contains(Foot, Support);
    if (Inside != InOutInside)
    {
        Error = TEXT("Native motion does not continue the published capture presence.");
        return false;
    }
    auto Emit = [&](bool Next, double Seconds)
    {
        if (Next != Inside)
        {
            Out.Add({Next, Seconds});
            Inside = Next;
        }
    };
    for (const FLDNativeMotionRecord& Record : Batch.Records)
    {
        if (!Record.FromFootCm.Equals(Foot, .000001) || Record.ToFootCm.ContainsNaN()
            || !FMath::IsFinite(Record.StartServerSeconds) || !FMath::IsFinite(Record.EndServerSeconds)
            || Record.EndServerSeconds < Record.StartServerSeconds)
        {
            Error = TEXT("Native motion records are discontinuous or not finite.");
            return false;
        }
        if (Record.Kind == ELDNativeMotionRecordKind::Support) { Support = Record.Support; }
        else if (Record.Kind == ELDNativeMotionRecordKind::Movement && Circle.Supports(Support))
        {
            const double AX = Record.FromFootCm.X - Circle.CenterCm.X;
            const double AY = Record.FromFootCm.Y - Circle.CenterCm.Y;
            const double DX = Record.ToFootCm.X - Record.FromFootCm.X;
            const double DY = Record.ToFootCm.Y - Record.FromFootCm.Y;
            const double A = DX * DX + DY * DY;
            const double B = 2. * (AX * DX + AY * DY);
            const double C = AX * AX + AY * AY - Circle.RadiusCm * Circle.RadiusCm;
            const double Disc = B * B - 4. * A * C;
            if (A > 0 && Disc >= 0)
            {
                const double Root = FMath::Sqrt(Disc);
                const double Q = -.5 * (B + (B >= 0 ? Root : -Root));
                double First = Q / A;
                double Second = Q != 0 ? C / Q : First;
                if (First > Second) { Swap(First, Second); }
                const double Span = Record.EndServerSeconds - Record.StartServerSeconds;
                if (First > 0 && First < 1) { Emit(true, Record.StartServerSeconds + First * Span); }
                if (Second > 0 && Second < 1) { Emit(false, Record.StartServerSeconds + Second * Span); }
            }
        }
        Foot = Record.ToFootCm;
        Emit(Circle.Contains(Foot, Support), Record.EndServerSeconds);
    }
    if (!Foot.Equals(Batch.EndFootCm, .000001))
    {
        Error = TEXT("Native motion records do not reach the batch end foot.");
        return false;
    }
    InOutInside = Inside;
    Error.Reset();
    return true;
}
