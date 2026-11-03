#include "Framework/LDCombatReceiptLog.h"

#include "Templates/Function.h"

bool FLDCombatReceiptLog::Push(const FLDCombatActionReceipt& Receipt)
{
    if (LastSerial == MAX_uint32) { return false; }
    if (Window.Num() >= Capacity)
    {
        EvictedThrough = LastSerial - uint32(Window.Num()) + 1;
        Window.RemoveAt(0);
    }
    Window.Add(Receipt);
    ++LastSerial;
    return true;
}

uint32 FLDCombatReceiptLog::ReadSince(uint32& Seen, TArray<FLDCombatActionReceipt>& Out) const
{
    const uint32 Lost = Seen < EvictedThrough ? EvictedThrough - Seen : 0;
    const uint32 First = LastSerial - uint32(Window.Num()) + 1;
    for (int32 Index = 0; Index < Window.Num(); ++Index)
    {
        if (First + uint32(Index) > Seen) { Out.Add(Window[Index]); }
    }
    Seen = LastSerial;
    return Lost;
}

const FLDCombatActionReceipt* FLDCombatReceiptLog::FindLatestSince(uint32 Mark, TFunctionRef<bool(const FLDCombatActionReceipt&)> Match) const
{
    const uint32 First = LastSerial - uint32(Window.Num()) + 1;
    for (int32 Index = Window.Num() - 1; Index >= 0 && First + uint32(Index) > Mark; --Index)
    {
        if (Match(Window[Index])) { return &Window[Index]; }
    }
    return nullptr;
}
