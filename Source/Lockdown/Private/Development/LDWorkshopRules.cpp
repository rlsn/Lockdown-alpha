#include "Development/LDWorkshopRules.h"

bool LDWorkshop::Lists(const TArray<FString>& Ids, FName Id)
{
    return !Id.IsNone() && Ids.ContainsByPredicate([Id](const FString& Row) { return FName(*Row) == Id; });
}

const FLDDemoEffectDefinition* LDWorkshop::FindOp(const FLDDemoComponentDefinition& Definition, ELDDemoEffectOp Op)
{
    return Definition.Effects.FindByPredicate([Op](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp() == Op; });
}

bool LDWorkshop::TargetShape(const FLDDemoComponentDefinition& Definition, ELDComponentTargetKind Kind, FName TargetId,
    FName BoundActive, ELDDemoElement Element, FName BoundComponent)
{
    if (Definition.Kind == ELDDemoComponentKind::Base || !BoundActive.IsNone() || Element != ELDDemoElement::None)
    {
        return false;
    }
    if (Definition.ExtraBases.IsEmpty() ? !BoundComponent.IsNone() : !Lists(Definition.ExtraBases, BoundComponent))
    {
        return false;
    }
    switch (Kind)
    {
    case ELDComponentTargetKind::Active: return Lists(Definition.TargetActives, TargetId);
    case ELDComponentTargetKind::Component: return Lists(Definition.TargetComponents, TargetId);
    case ELDComponentTargetKind::Dash: return Definition.bTargetDash && TargetId.IsNone();
    case ELDComponentTargetKind::None: return Definition.bTargetSelf && TargetId.IsNone();
    default: return false;
    }
}

bool LDWorkshop::Conflicts(const FLDDemoComponentDefinition& A, const FLDDemoComponentDefinition& B)
{
    return A.Id != B.Id && !A.Form.IsEmpty() && A.Form == B.Form;
}
