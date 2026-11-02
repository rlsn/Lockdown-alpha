#include "LDDemoContentJson.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

namespace LDDemoContent
{
bool FCheck::Require(bool Condition, const FString& Path, const FString& Detail)
{
    if (!Good)
    {
        return false;
    }
    if (!Condition)
    {
        Error = FString::Printf(TEXT("DemoContent: %s: %s"), *Path, *Detail);
        Good = false;
    }
    return Good;
}

namespace
{
int32 ScalarLength(const FString& Text)
{
    int32 Length = 0;
    for (int32 I = 0; I < Text.Len(); ++I)
    {
        const uint32 C = static_cast<uint32>(Text[I]);
        if (C >= 0xd800 && C <= 0xdbff)
        {
            if (++I >= Text.Len() || Text[I] < 0xdc00 || Text[I] > 0xdfff)
            {
                return -1;
            }
        }
        else if ((C >= 0xdc00 && C <= 0xdfff) || C > 0x10ffff)
        {
            return -1;
        }
        ++Length;
    }
    return Length;
}

bool IntegerLexeme(const FString& Text, int64& Out)
{
    int32 I = 0;
    const bool Negative = Text.StartsWith(TEXT("-"));
    if (Negative)
    {
        ++I;
    }
    if (I == Text.Len() || (Text[I] == TEXT('0') && (Negative || I + 1 != Text.Len())))
    {
        return false;
    }
    const uint64 Limit = Negative ? uint64(MAX_int64) + 1 : uint64(MAX_int64);
    uint64 Magnitude = 0;
    for (; I < Text.Len(); ++I)
    {
        const TCHAR C = Text[I];
        if (C < TEXT('0') || C > TEXT('9'))
        {
            return false;
        }
        const uint64 Digit = C - TEXT('0');
        if (Magnitude > (Limit - Digit) / 10)
        {
            return false;
        }
        Magnitude = Magnitude * 10 + Digit;
    }
    Out = Negative ? (Magnitude == uint64(MAX_int64) + 1 ? MIN_int64 : -int64(Magnitude)) : int64(Magnitude);
    return true;
}

bool CheckTokens(const FString& Text, FCheck& Check, const FString& Path)
{
    struct FScope
    {
        EJsonNotation Kind;
        TSet<FString> Keys;
    };
    TArray<FScope> Scopes;
    const auto Reader = TJsonReaderFactory<>::Create(Text);
    EJsonNotation Token;
    bool SawRoot = false;
    while (Reader->ReadNext(Token))
    {
        if (!Check.Require(Token != EJsonNotation::Error, Path, Reader->GetErrorMessage()))
        {
            return false;
        }
        if (Token == EJsonNotation::ObjectEnd || Token == EJsonNotation::ArrayEnd)
        {
            if (!Check.Require(!Scopes.IsEmpty(), Path, TEXT("unmatched closing token")))
            {
                return false;
            }
            Scopes.Pop();
            continue;
        }
        if (Scopes.IsEmpty())
        {
            if (!Check.Require(!SawRoot, Path, TEXT("multiple roots")))
            {
                return false;
            }
            SawRoot = true;
        }
        else if (Scopes.Last().Kind == EJsonNotation::ObjectStart)
        {
            const FString& Key = Reader->GetIdentifier();
            if (!Check.Require(ScalarLength(Key) >= 0 && !Scopes.Last().Keys.Contains(Key), Path,
                TEXT("duplicate object key or invalid Unicode key")))
            {
                return false;
            }
            Scopes.Last().Keys.Add(Key);
        }
        if (Token == EJsonNotation::Number)
        {
            int64 Exact;
            // UE 5.8 retains this lexeme even though its reader also computes a double.
            if (!Check.Require(IntegerLexeme(Reader->GetValueAsNumberString(), Exact), Path,
                TEXT("expected a canonical int64 token; decimals, exponents and overflow are forbidden")))
            {
                return false;
            }
        }
        else if (Token == EJsonNotation::String
            && !Check.Require(ScalarLength(Reader->GetValueAsString()) >= 0, Path, TEXT("invalid Unicode scalar")))
        {
            return false;
        }
        if (Token == EJsonNotation::ObjectStart || Token == EJsonNotation::ArrayStart)
        {
            if (!Check.Require(Scopes.Num() < 64, Path, TEXT("JSON nesting limit exceeded")))
            {
                return false;
            }
            Scopes.Add({Token, {}});
        }
    }
    return Check.Require(SawRoot && Scopes.IsEmpty() && Reader->GetErrorMessage().IsEmpty(),
        Path, TEXT("incomplete or invalid JSON"));
}

void Quote(const FString& Text, FString& Out)
{
    Out += TEXT('"');
    for (TCHAR C : Text)
    {
        switch (C)
        {
        case TEXT('"'): Out += TEXT("\\\""); break;
        case TEXT('\\'): Out += TEXT("\\\\"); break;
        case TEXT('\b'): Out += TEXT("\\b"); break;
        case TEXT('\f'): Out += TEXT("\\f"); break;
        case TEXT('\n'): Out += TEXT("\\n"); break;
        case TEXT('\r'): Out += TEXT("\\r"); break;
        case TEXT('\t'): Out += TEXT("\\t"); break;
        default:
            if (static_cast<uint32>(C) < 32)
            {
                Out += FString::Printf(TEXT("\\u%04x"), static_cast<uint32>(C));
            }
            else
            {
                Out += C;
            }
            break;
        }
    }
    Out += TEXT('"');
}

bool WriteCanonical(const FValue& Value, FString& Out, int32 Depth, FCheck& Check)
{
    if (!Check.Require(Value.IsValid() && Depth <= 64, TEXT("JSON"), TEXT("invalid value or depth")))
    {
        return false;
    }
    const auto Indent = [&Out](int32 N) { Out += FString::ChrN(N * 2, TEXT(' ')); };
    switch (Value->Type)
    {
    case EJson::Object:
    {
        const auto& Object = Value->AsObject();
        TArray<FString> Keys;
        for (const auto& Pair : Object->Values)
        {
            Keys.Emplace(Pair.Key.ToView());
        }
        Keys.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
        Out += TEXT('{');
        for (int32 I = 0; I < Keys.Num(); ++I)
        {
            Out += I ? TEXT(",\n") : TEXT("\n");
            Indent(Depth + 1);
            Quote(Keys[I], Out);
            Out += TEXT(": ");
            if (!WriteCanonical(Object->TryGetField(FStringView(Keys[I])), Out, Depth + 1, Check))
            {
                return false;
            }
        }
        if (!Keys.IsEmpty())
        {
            Out += TEXT('\n');
            Indent(Depth);
        }
        Out += TEXT('}');
        return true;
    }
    case EJson::Array:
    {
        const auto& Values = Value->AsArray();
        Out += TEXT('[');
        for (int32 I = 0; I < Values.Num(); ++I)
        {
            Out += I ? TEXT(",\n") : TEXT("\n");
            Indent(Depth + 1);
            if (!WriteCanonical(Values[I], Out, Depth + 1, Check))
            {
                return false;
            }
        }
        if (!Values.IsEmpty())
        {
            Out += TEXT('\n');
            Indent(Depth);
        }
        Out += TEXT(']');
        return true;
    }
    case EJson::String:
        Quote(Value->AsString(), Out);
        return true;
    case EJson::Number:
        if (!Check.Require(Value->PreferStringRepresentation(), TEXT("JSON"), TEXT("number lexeme was lost")))
        {
            return false;
        }
        Out += Value->AsString();
        return true;
    case EJson::Boolean:
        Out += Value->AsBool() ? TEXT("true") : TEXT("false");
        return true;
    case EJson::Null:
        Out += TEXT("null");
        return true;
    default:
        return Check.Require(false, TEXT("JSON"), TEXT("unsupported value"));
    }
}
}

bool ReadFile(const FString& Path, TArray<uint8>& Out, FString& Error)
{
    FCheck Check(Error);
    const int64 Size = IFileManager::Get().FileSize(*Path);
    if (!Check.Require(Size > 0 && Size <= MaxFileBytes, Path, TEXT("missing, empty or oversized input")))
    {
        return false;
    }
    TArray<uint8> Candidate;
    if (!Check.Require(FFileHelper::LoadFileToArray(Candidate, *Path) && Candidate.Num() == Size,
        Path, TEXT("could not read stable file bytes")))
    {
        return false;
    }
    Out = MoveTemp(Candidate);
    Error.Reset();
    return true;
}

bool Hash(const TArray<uint8>& Bytes, FLDDemoContentDigest& Out, FCheck& Check, const FString& Path)
{
    return Check.Require(!Bytes.IsEmpty() && Bytes.Num() <= MaxFileBytes, Path, TEXT("missing or oversized bytes"))
        && Check.Require(SHA256(Bytes.GetData(), Bytes.Num(), Out.Bytes) != nullptr, Path, TEXT("SHA-256 failed"));
}

bool CanonicalBytes(const FValue& Value, TArray<uint8>& Out, FCheck& Check)
{
    FString Text;
    if (!WriteCanonical(Value, Text, 0, Check))
    {
        return false;
    }
    Text += TEXT('\n');
    const FTCHARToUTF8 UTF8(*Text, Text.Len());
    Out.Reset(UTF8.Length());
    Out.Append(reinterpret_cast<const uint8*>(UTF8.Get()), UTF8.Length());
    return true;
}

bool Parse(const TArray<uint8>& Bytes, FValue& Out, FCheck& Check, const FString& Path, bool RequireCanonical)
{
    if (!Check.Require(!Bytes.IsEmpty() && Bytes.Num() <= MaxFileBytes, Path, TEXT("missing or oversized JSON")))
    {
        return false;
    }
    const FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
    const FString Text(Decoded.Length(), Decoded.Get());
    const FTCHARToUTF8 Encoded(*Text, Text.Len());
    if (!Check.Require(Encoded.Length() == Bytes.Num()
        && FMemory::Memcmp(Encoded.Get(), Bytes.GetData(), Bytes.Num()) == 0,
        Path, TEXT("input is not strict UTF-8")) || !CheckTokens(Text, Check, Path))
    {
        return false;
    }
    FValue Candidate;
    if (!Check.Require(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Candidate,
        FJsonSerializer::EFlags::StoreNumbersAsStrings), Path, TEXT("JSON deserialization failed")))
    {
        return false;
    }
    TArray<uint8> Canonical;
    if (RequireCanonical && (!CanonicalBytes(Candidate, Canonical, Check)
        || !Check.Require(Canonical == Bytes, Path, TEXT("bytes differ from the compiler canonical encoding"))))
    {
        return false;
    }
    Out = MoveTemp(Candidate);
    return true;
}

bool Integer(const FValue& Value, int64& Out, FCheck& Check, const FString& Path, int64 Min, int64 Max)
{
    int64 Number = 0;
    if (!Check.Require(Value && Value->Type == EJson::Number && Value->PreferStringRepresentation(),
        Path, TEXT("expected integer token, not string/bool"))
        || !Check.Require(IntegerLexeme(Value->AsString(), Number) && Number >= Min && Number <= Max,
            Path, TEXT("integer outside exact declared range")))
    {
        return false;
    }
    Out = Number;
    return true;
}

bool String(const FValue& Value, FString& Out, FCheck& Check, const FString& Path, int32 Min, int32 Max)
{
    if (!Check.Require(Value && Value->Type == EJson::String, Path, TEXT("expected string")))
    {
        return false;
    }
    const FString Text = Value->AsString();
    const int32 Length = ScalarLength(Text);
    if (!Check.Require(Length >= Min && Length <= Max,
        Path, TEXT("invalid string length or encoding")))
    {
        return false;
    }
    Out = Text;
    return true;
}

bool Id(const FString& Value, FCheck& Check, const FString& Path)
{
    bool Valid = Value.Len() >= 1 && Value.Len() <= 63 && Value[0] >= TEXT('a') && Value[0] <= TEXT('z');
    for (TCHAR C : Value)
    {
        Valid &= (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('0') && C <= TEXT('9')) || C == TEXT('_');
    }
    return Check.Require(Valid, Path, TEXT("expected canonical lowercase ASCII ID of 1..63 bytes"));
}

bool Array(const FValue& Value, const TArray<FValue>*& Out, FCheck& Check,
    const FString& Path, int32 Min, int32 Max)
{
    if (!Check.Require(Value && Value->Type == EJson::Array, Path, TEXT("expected array")))
    {
        return false;
    }
    const auto& Values = Value->AsArray();
    if (!Check.Require(Values.Num() >= Min && Values.Num() <= Max, Path, TEXT("wrong array length")))
    {
        return false;
    }
    Out = &Values;
    return true;
}

bool Rational(const FValue& Value, FLDDemoRational& Out, FCheck& Check,
    const FString& Path, const TCHAR* Unit, bool Positive, bool Signed)
{
    FFields Fields(Check, Value, Path);
    FLDDemoRational Result;
    if ((Unit && !Fields.Literal(TEXT("unit"), Unit))
        || !Fields.Int(TEXT("numerator"), Result.Numerator, Signed ? MIN_int64 : Positive ? 1 : 0)
        || !Fields.Int(TEXT("denominator"), Result.Denominator, 1) || !Fields.Done())
    {
        return false;
    }
    uint64 A = Result.Numerator < 0 ? uint64(-(Result.Numerator + 1)) + 1 : uint64(Result.Numerator);
    uint64 B = Result.Denominator;
    while (B)
    {
        const uint64 Remainder = A % B;
        A = B;
        B = Remainder;
    }
    if (!Check.Require(A == 1, Path, TEXT("rational pair is not reduced")))
    {
        return false;
    }
    Out = Result;
    return true;
}

FFields::FFields(FCheck& InCheck, const FValue& Value, FString InPath)
    : Check(InCheck), Path(MoveTemp(InPath))
{
    if (Check.Require(Value && Value->Type == EJson::Object, Path, TEXT("expected object")))
    {
        Object = Value->AsObject();
    }
}

FValue FFields::Take(const FString& Key)
{
    if (!Check.Good || !Object)
    {
        return nullptr;
    }
    FValue Value;
    for (const auto& Pair : Object->Values)
    {
        if (Pair.Key.ToView().Equals(FStringView(Key), ESearchCase::CaseSensitive))
        {
            Value = Pair.Value;
            break;
        }
    }
    if (!Check.Require(Value.IsValid() && !Used.Contains(Key), Path + TEXT(".") + Key, TEXT("missing or multiply consumed field")))
    {
        return nullptr;
    }
    Used.Add(Key);
    return Value;
}

bool FFields::Done()
{
    if (!Check.Good || !Object)
    {
        return false;
    }
    for (const auto& Pair : Object->Values)
    {
        const FString Key(Pair.Key.ToView());
        if (!Check.Require(Used.Contains(Key), Path + TEXT(".") + Key, TEXT("unknown field")))
        {
            return false;
        }
    }
    return true;
}

bool FFields::Text(const TCHAR* Key, FString& Out, int32 Min, int32 Max)
{
    return String(Take(Key), Out, Check, Path + TEXT(".") + Key, Min, Max);
}

bool FFields::Identifier(const TCHAR* Key, FString& Out)
{
    return Text(Key, Out, 1, 63) && Id(Out, Check, Path + TEXT(".") + Key);
}

bool FFields::Literal(const TCHAR* Key, const TCHAR* Expected)
{
    FString TextValue;
    return Text(Key, TextValue) && Check.Require(TextValue.Equals(Expected, ESearchCase::CaseSensitive),
        Path + TEXT(".") + Key, TEXT("unsupported constant"));
}

bool FFields::Flag(const TCHAR* Key, bool& Out)
{
    const auto Value = Take(Key);
    if (!Check.Require(Value && Value->Type == EJson::Boolean, Path + TEXT(".") + Key, TEXT("expected boolean")))
    {
        return false;
    }
    Out = Value->AsBool();
    return true;
}

bool FFields::Int(const TCHAR* Key, int64& Out, int64 Min, int64 Max)
{
    return Integer(Take(Key), Out, Check, Path + TEXT(".") + Key, Min, Max);
}

bool FFields::SmallInt(const TCHAR* Key, int32& Out, int32 Min, int32 Max)
{
    int64 Number;
    if (!Int(Key, Number, Min, Max))
    {
        return false;
    }
    Out = static_cast<int32>(Number);
    return true;
}

bool FFields::Fixed(const TCHAR* Key, const TCHAR* Unit, const TCHAR* Storage, int64& Out, int64 Min, int64 Max)
{
    FFields Quantity(Check, Take(Key), Path + TEXT(".") + Key);
    return Quantity.Literal(TEXT("unit"), Unit) && Quantity.Int(Storage, Out, Min, Max) && Quantity.Done();
}

bool FFields::Fraction(const TCHAR* Key, const TCHAR* Unit, FLDDemoRational& Out, bool Positive)
{
    return Rational(Take(Key), Out, Check, Path + TEXT(".") + Key, Unit, Positive);
}

bool FFields::Digest(const TCHAR* Key, FLDDemoContentDigest& Out)
{
    FString Hex;
    if (!Text(Key, Hex, 64, 64))
    {
        return false;
    }
    FString Detail;
    return Check.Require(FLDDemoContentDigest::ParseHex(Hex, Out, Detail), Path + TEXT(".") + Key, Detail);
}
}

bool FLDDemoContentDigest::ParseHex(const FString& Hex, FLDDemoContentDigest& Out, FString& Error)
{
    LDDemoContent::FCheck Check(Error);
    if (!Check.Require(Hex.Len() == 64, TEXT("digest"), TEXT("expected 64 lowercase hexadecimal characters")))
    {
        return false;
    }
    FLDDemoContentDigest Candidate;
    for (int32 I = 0; I < 64; ++I)
    {
        const TCHAR C = Hex[I];
        if (!Check.Require((C >= TEXT('0') && C <= TEXT('9')) || (C >= TEXT('a') && C <= TEXT('f')),
            TEXT("digest"), TEXT("invalid hexadecimal character")))
        {
            return false;
        }
        const uint8 Value = C <= TEXT('9') ? C - TEXT('0') : C - TEXT('a') + 10;
        Candidate.Bytes[I / 2] |= I % 2 ? Value : Value << 4;
    }
    Out = Candidate;
    Error.Reset();
    return true;
}

FString FLDDemoContentDigest::ToHex() const
{
    return BytesToHex(Bytes, UE_ARRAY_COUNT(Bytes)).ToLower();
}

bool FLDDemoContentDigest::operator==(const FLDDemoContentDigest& Other) const
{
    return FMemory::Memcmp(Bytes, Other.Bytes, sizeof(Bytes)) == 0;
}
