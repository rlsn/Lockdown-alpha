#pragma once

#include "CoreMinimal.h"
#include "Combat/LDCombatTypes.h"
#include "Templates/FunctionFwd.h"

// The owner's combat receipts (ClientCombatReceipt) in arrival order. Each recorded receipt takes the next serial; every
// presenter keeps its own cursor and reads all receipts after it, so several receipts that arrive in one frame each
// present once instead of only the newest one.
struct LOCKDOWN_API FLDCombatReceiptLog
{
    // Receipts kept for cursors that have not read them yet; presenters read every frame.
    static constexpr int32 Capacity=32;

    // Records the receipt under the next serial; false (nothing recorded) once the serial is exhausted.
    bool Push(const FLDCombatActionReceipt& Receipt);
    // Appends the kept receipts after Seen, oldest first, and moves Seen to Serial(). Returns how many receipts after
    // Seen had already left the window because the cursor fell more than Capacity behind (0 normally).
    uint32 ReadSince(uint32& Seen,TArray<FLDCombatActionReceipt>& Out) const;
    // The newest kept receipt after Mark that matches; null when there is none.
    const FLDCombatActionReceipt* FindLatestSince(uint32 Mark,TFunctionRef<bool(const FLDCombatActionReceipt&)> Match) const;
    // The newest kept receipt; null when none is kept.
    const FLDCombatActionReceipt* Latest() const { return Window.IsEmpty()?nullptr:&Window.Last(); }
    // Forgets the kept receipts of a retired match. The serial keeps counting so existing cursors stay valid.
    void Clear() { Window.Reset(); }
    // Serial of the newest recorded receipt; 0 before the first.
    uint32 Serial() const { return LastSerial; }
private:
    // Oldest first; the last entry carries LastSerial.
    TArray<FLDCombatActionReceipt> Window;
    uint32 LastSerial=0;
    // Serial of the newest receipt pushed out by Capacity.
    uint32 EvictedThrough=0;
};
