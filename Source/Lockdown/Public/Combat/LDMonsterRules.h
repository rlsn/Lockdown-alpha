#pragma once
#include "CoreMinimal.h"
#include "Development/LDDemoContentDefinitions.h"

enum class ELDMonsterShape : uint8 { Cone, Circle, Line };
struct FLDMonsterArea
{
    ELDMonsterShape Shape=ELDMonsterShape::Cone;
    FVector Origin=FVector::ZeroVector;
    FVector Direction=FVector::ForwardVector;
    double Range=0,Radius=0,Angle=0;
    int64 DelayMs=0,RatioBp=0;
};
namespace LDMonsterRules
{
    LOCKDOWN_API TArray<FLDMonsterArea> Areas(const FLDDemoMonsterSkill& Skill,uint8 Stage,const FVector& Origin,const FVector& Target);
    LOCKDOWN_API bool Contains(const FLDMonsterArea& Area,const FVector& Point);
}
