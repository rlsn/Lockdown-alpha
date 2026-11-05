#include "World/LDDormZones.h"
#include "Development/LDDemoContentCatalog.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapRegistry.h"

bool FLDDormZones::IsValid() const
{
    if (Dorms.IsEmpty() || !FMath::IsFinite(RadiusCm) || RadiusCm <= 0) { return false; }
    for (const FDorm& Dorm : Dorms)
    {
        if (Dorm.Id.IsNone() || !FMath::IsFinite(Dorm.CenterCm.X) || !FMath::IsFinite(Dorm.CenterCm.Y)) { return false; }
    }
    return true;
}

const FLDDormZones::FDorm* FLDDormZones::Find(const FVector& FootCm) const
{
    for (const FDorm& Dorm : Dorms)
    {
        const double X = FootCm.X - Dorm.CenterCm.X;
        const double Y = FootCm.Y - Dorm.CenterCm.Y;
        if (X * X + Y * Y <= RadiusCm * RadiusCm) { return &Dorm; }
    }
    return nullptr;
}

bool LDDormZones::Resolve(const ULDMapRegistry& Registry, const FLDDemoContentSnapshot& Content,
    FLDDormZones& Out, FString& Error)
{
    Out = {};
    const auto* Geometry = Registry.GetGeometry();
    if (!Registry.Enabled() || !Geometry)
    {
        Error = TEXT("The dorms have no registered Demo world.");
        return false;
    }
    const auto& Radius = Content.Get().Parameters.DormRadiusCm;
    const double NativeRadius = Radius.Denominator > 0 ? double(Radius.Numerator) / double(Radius.Denominator) : 0.;
    if (!FMath::IsFinite(NativeRadius) || NativeRadius <= 0)
    {
        Error = TEXT("The approved dorm radius is invalid.");
        return false;
    }
    FLDDormZones Candidate;
    Candidate.RadiusCm = NativeRadius;
    for (const FLDDemoWorldAnchor& Anchor : Geometry->Anchors)
    {
        if (Anchor.Kind != ELDDemoWorldAnchorKind::Dorm) { continue; }
        const FName Id(*Anchor.Id);
        const FLDMapBinding* Binding = Registry.Resolve(Id, Registry.Generation);
        if (!Binding || !Binding->Floor.IsValid())
        {
            Error = FString::Printf(TEXT("Dorm '%s' is not bound to registered ground."), *Anchor.Id);
            return false;
        }
        FLDDormZones::FDorm& Dorm = Candidate.Dorms.AddDefaulted_GetRef();
        Dorm.Id = Id;
        Dorm.CenterCm = FVector2D(Binding->Position.X, Binding->Position.Y);
    }
    if (!Candidate.IsValid())
    {
        Error = TEXT("The compiled world has no valid dorm.");
        return false;
    }
    Out = MoveTemp(Candidate);
    Error.Reset();
    return true;
}
