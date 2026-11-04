#pragma once

#include "Development/LDDemoContentCatalog.h"
#include "Dom/JsonObject.h"

namespace LDDemoContent
{
using FValue = TSharedPtr<FJsonValue>;
constexpr int32 MaxFileBytes = 8 * 1024 * 1024;

struct FCheck
{
    explicit FCheck(FString& InError) : Error(InError) {}
    FString& Error;
    bool Good = true;

    bool Require(bool Condition, const FString& Path, const FString& Detail);
};

bool ReadFile(const FString& Path, TArray<uint8>& Out, FString& Error);
bool Hash(const TArray<uint8>& Bytes, FLDDemoContentDigest& Out, FCheck& Check, const FString& Path);
bool Parse(const TArray<uint8>& Bytes, FValue& Out, FCheck& Check, const FString& Path, bool RequireCanonical = true);
bool CanonicalBytes(const FValue& Value, TArray<uint8>& Out, FCheck& Check);
bool Integer(const FValue& Value, int64& Out, FCheck& Check, const FString& Path,
    int64 Min = 0, int64 Max = MAX_int64);
bool String(const FValue& Value, FString& Out, FCheck& Check, const FString& Path,
    int32 Min = 1, int32 Max = 512);
bool Id(const FString& Value, FCheck& Check, const FString& Path);
bool Array(const FValue& Value, const TArray<FValue>*& Out, FCheck& Check,
    const FString& Path, int32 Min, int32 Max);
bool Rational(const FValue& Value, FLDDemoRational& Out, FCheck& Check,
    const FString& Path, const TCHAR* Unit, bool Positive = false, bool Signed = false);

struct FFields
{
    FFields(FCheck& InCheck, const FValue& Value, FString InPath);

    FValue Take(const FString& Key);
    // Whether an optional field is present (it must still be consumed with Take or a typed reader).
    bool Has(const TCHAR* Key) const;
    bool Done();
    bool Text(const TCHAR* Key, FString& Out, int32 Min = 1, int32 Max = 512);
    bool Identifier(const TCHAR* Key, FString& Out);
    bool Literal(const TCHAR* Key, const TCHAR* Expected);
    bool Flag(const TCHAR* Key, bool& Out);
    bool Int(const TCHAR* Key, int64& Out, int64 Min = 0, int64 Max = MAX_int64);
    bool SmallInt(const TCHAR* Key, int32& Out, int32 Min, int32 Max);
    bool Fixed(const TCHAR* Key, const TCHAR* Unit, const TCHAR* Storage, int64& Out,
        int64 Min = 0, int64 Max = MAX_int64);
    bool Fraction(const TCHAR* Key, const TCHAR* Unit, FLDDemoRational& Out, bool Positive = false);
    bool Digest(const TCHAR* Key, FLDDemoContentDigest& Out);

    FCheck& Check;
    FString Path;
    TSharedPtr<FJsonObject> Object;
    TSet<FString> Used;
};

bool Project(const FLDDemoContentBundle& Bundle, const FLDDemoContentDigest& ExpectedRoot,
    FLDDemoContentDefinitions& Out, FString& Error);
bool ValidateNumbers(const FLDDemoContentDefinitions& Definitions, FCheck& Check);
bool ValidateWorld(const FValue& World, const FLDDemoContentDefinitions& Definitions, const FLDDemoContentBundle& Bundle, FCheck& Check);
bool ExactStoredStruct(const UStruct* Type, const void* A, const void* B);
}
