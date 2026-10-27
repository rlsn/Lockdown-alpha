#pragma once
#include "CoreMinimal.h"
class UWorld;
class ULDMapDefinition;
class FJsonObject;

// Public static-geography inspection only. This is not unit observation or
// targeting authority: smoke, ledge tags, concealment and audiences belong to B91.
namespace LDMapApproaches
{
    enum class ERay : uint8 { Invalid, Range, Blocked, Clear };
    struct FRay { ERay Result=ERay::Invalid; FName Blocker; FVector Hit=FVector::ZeroVector; };
    struct FBranch { FName From,Route; TArray<FVector> Points; double FullLength=0,LocalLength=0; };
    struct FSample { double Remaining=0,Separation=0; FVector A,B; FRay Body,Watch; };
    struct FPair { int32 A=0,B=0; bool Distinct=false; TArray<FSample> Samples; };
    struct FTarget { FName Id; FVector Goal; TArray<FBranch> Branches; TArray<FPair> Pairs; bool Alternative=false; };
    LOCKDOWN_API double Length(const TArray<FVector>& Points);
    LOCKDOWN_API FVector AtRemaining(const TArray<FVector>& Points,double Remaining);
    LOCKDOWN_API TArray<FVector> Tail(const TArray<FVector>& Points,double Distance);
    LOCKDOWN_API bool KeyTarget(FName Id,const FString& Kind);
    LOCKDOWN_API FRay StaticRay(UWorld* World,const FVector& A,const FVector& B,double Radius);
    LOCKDOWN_API bool Collect(UWorld* World,const ULDMapDefinition* Definition,TArray<FTarget>& Targets,FString& Error);
    LOCKDOWN_API TSharedRef<FJsonObject> Report(const ULDMapDefinition* Definition,const TArray<FTarget>& Targets);
}
