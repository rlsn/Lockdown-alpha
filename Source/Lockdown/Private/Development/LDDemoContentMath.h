#pragma once

#include "LDDemoContentJson.h"
#include "Math/BigInt.h"

namespace LDDemoContent
{
// Import-time arithmetic, shared by content and geometry. Gameplay owns its
// own calculations; every operation here guards the fixed-width scratch space.
using FBig = TBigInt<2048>;

inline FBig Big(int64 Value)
{
    if (Value >= 0) { return FBig(Value); }
    FBig Result = FBig(-(Value + 1)) + FBig(1);
    Result.Negate();
    return Result;
}

struct FExact
{
    FBig N;
    FBig D = FBig(1);
};

class FNumbers
{
public:
    explicit FNumbers(FCheck& InCheck) : Check(InCheck) {}

    FExact Value(int64 N, int64 D = 1) { return Reduce({Big(N), Big(D)}); }
    FExact Value(const FLDDemoRational& R) { return Value(R.Numerator, R.Denominator); }

    FExact Add(const FExact& A, const FExact& B)
    {
        const FBig Left = Product(A.N, B.D), Right = Product(B.N, A.D);
        if (!Check.Require(FMath::Max(Bits(Left), Bits(Right)) < 2046, TEXT("numeric"), TEXT("exact addition scratch overflow")))
        {
            return {};
        }
        return Reduce({Left + Right, Product(A.D, B.D)});
    }

    FExact Sub(const FExact& A, FExact B)
    {
        B.N.Negate();
        return Add(A, B);
    }

    FExact Mul(const FExact& A, const FExact& B)
    {
        return Reduce({Product(A.N, B.N), Product(A.D, B.D)});
    }

    bool LE(const FExact& A, const FExact& B) { return CompareSigned(Product(A.N, B.D), Product(B.N, A.D)) <= 0 && Check.Good; }
    bool LT(const FExact& A, const FExact& B) { return CompareSigned(Product(A.N, B.D), Product(B.N, A.D)) < 0 && Check.Good; }
    bool EQ(const FExact& A, const FExact& B) { return Product(A.N, B.D) == Product(B.N, A.D) && Check.Good; }
    FExact Max(const FExact& A, const FExact& B) { return LE(A, B) ? B : A; }

    bool NativePair(const FExact& A, const FString& Path)
    {
        return Check.Require(!A.N.IsNegative() && A.N <= FBig(MAX_int64) && A.D > FBig(int64(0)) && A.D <= FBig(MAX_int64),
            Path, TEXT("exact reduced result exceeds native int64 representation"));
    }

    bool Interval(const FLDDemoRational& Rate, int64& Out, const FString& Path)
    {
        const FExact R = Mul(Value(1000), Value(Rate.Denominator, Rate.Numerator));
        if (!Check.Good) { return false; }
        FBig Quotient = R.N;
        FBig Remainder;
        Quotient.DivideWithRemainder(R.D, Remainder);
        if (!Remainder.IsZero()) { Quotient += FBig(1); }
        if (!Check.Require(Quotient > FBig(int64(0)) && Quotient <= FBig(MAX_int64), Path, TEXT("attack interval overflow")))
        {
            return false;
        }
        Out = Quotient.ToInt();
        return true;
    }

    bool Add64(int64 A, int64 B, int64& Out, const FString& Path)
    {
        if (!Check.Require(A >= 0 && B >= 0 && A <= MAX_int64 - B, Path, TEXT("int64 addition overflow"))) { return false; }
        Out = A + B;
        return true;
    }

    bool Mul64(int64 A, int64 B, int64& Out, const FString& Path)
    {
        if (!Check.Require(A >= 0 && B >= 0 && (B == 0 || A <= MAX_int64 / B), Path, TEXT("int64 multiplication overflow"))) { return false; }
        Out = A * B;
        return true;
    }

private:
    static int32 CompareSigned(FBig Left, FBig Right)
    {
        const bool Negative = Left.IsNegative();
        if (Negative != Right.IsNegative()) { return Negative ? -1 : 1; }
        // UE 5.8's BigInt signed ordering recurses or misorders two negatives.
        // Compare only positive magnitudes without changing the shared engine.
        if (Negative) { Left.Negate(); Right.Negate(); }
        if (Left == Right) { return 0; }
        const int32 Magnitude = Left < Right ? -1 : 1;
        return Negative ? -Magnitude : Magnitude;
    }

    static int32 Bits(FBig Value)
    {
        if (Value.IsNegative()) { Value.Negate(); }
        return Value.IsZero() ? 0 : Value.GetHighestNonZeroBit() + 1;
    }

    FBig Product(const FBig& A, const FBig& B)
    {
        const int32 ProductBits = Bits(A) + Bits(B);
        if (!Check.Require(ProductBits <= 2046, TEXT("numeric"), TEXT("exact multiplication scratch overflow")))
        {
            return FBig(int64(0));
        }
        if (ProductBits <= 62)
        {
            FBig Left = A, Right = B;
            const bool Negative = Left.IsNegative() != Right.IsNegative();
            if (Left.IsNegative()) { Left.Negate(); }
            if (Right.IsNegative()) { Right.Negate(); }
            const int64 NativeProduct = Left.ToInt() * Right.ToInt();
            return Big(Negative ? -NativeProduct : NativeProduct);
        }
        return A * B;
    }

    FExact Reduce(FExact R)
    {
        if (!Check.Good) { return {}; }
        if (!Check.Require(R.D > FBig(int64(0)), TEXT("numeric"), TEXT("nonpositive denominator"))) { return {}; }
        if (R.D == FBig(1)) { return R; }
        FBig A = R.N, B = R.D;
        if (A.IsNegative()) { A.Negate(); }
        while (!B.IsZero())
        {
            FBig Remainder;
            A.DivideWithRemainder(B, Remainder);
            A = B;
            B = Remainder;
        }
        R.N /= A;
        R.D /= A;
        return R;
    }

    FCheck& Check;
};
}
