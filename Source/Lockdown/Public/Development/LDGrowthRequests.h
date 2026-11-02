#pragma once

#include "CoreMinimal.h"
#include "Development/LDGrowthProtocol.h"

// One contender's request stream of one family (PEE §4.4): monotonic RequestId, at most one accepted
// intent waiting for its Transactions phase, a 64-receipt replay window and a finalized-through watermark.
// Final receipts outside the window are never re-executed; they answer ReceiptExpired.
template <typename TCommand, typename TReceipt>
class TLDGrowthRequestStream
{
public:
    static constexpr int32 WindowCapacity = 64;

    struct FPending
    {
        TCommand Command;
        FLDPendingReceiptInfo Info;
        uint64 Arrival = 0;
    };

    // Admission only orders and freezes terms; context checks belong to the caller and the commit.
    ELDAdmissionStatus Admit(const TCommand& Command, uint64 RequestId, const FLDPendingReceiptInfo& Info,
        uint64 Arrival, ELDGrowthReason& Reason, const TReceipt*& Final, bool& bReserved)
    {
        Final = nullptr;
        bReserved = false;
        Reason = ELDGrowthReason::None;
        if (RequestId == 0)
        {
            Reason = ELDGrowthReason::InvalidRequest;
            return ELDAdmissionStatus::AdmissionRejected;
        }
        if (RequestId <= FinalizedThrough)
        {
            if (const TReceipt* Existing = Find(RequestId))
            {
                if (!Terms(RequestId) || !Terms(RequestId)->SameTerms(Command))
                {
                    Reason = ELDGrowthReason::RequestConflict;
                    return ELDAdmissionStatus::AdmissionRejected;
                }
                Final = Existing;
                bReserved = true;
                return ELDAdmissionStatus::Final;
            }
            Reason = ELDGrowthReason::ReceiptExpired;
            return ELDAdmissionStatus::AdmissionRejected;
        }
        if (Pending.IsSet())
        {
            if (PendingRequestId() == RequestId)
            {
                if (!Pending->Command.SameTerms(Command))
                {
                    Reason = ELDGrowthReason::RequestConflict;
                    return ELDAdmissionStatus::AdmissionRejected;
                }
                bReserved = true;
                return ELDAdmissionStatus::AcceptedPending;
            }
            Reason = ELDGrowthReason::RequestInFlight;
            return ELDAdmissionStatus::AdmissionRejected;
        }
        if (RequestId != FinalizedThrough + 1)
        {
            Reason = ELDGrowthReason::OutOfOrder;
            return ELDAdmissionStatus::AdmissionRejected;
        }
        FPending Accepted;
        Accepted.Command = Command;
        Accepted.Info = Info;
        Accepted.Arrival = Arrival;
        Pending.Emplace(MoveTemp(Accepted));
        bReserved = true;
        return ELDAdmissionStatus::AcceptedPending;
    }

    const FPending* GetPending() const { return Pending.IsSet() ? &Pending.GetValue() : nullptr; }
    uint64 PendingRequestId() const { return Pending.IsSet() ? Pending->Command.RequestId : 0; }

    // Consumes the pending sequence with its final receipt (applied or rejected).
    void Finalize(const TReceipt& Receipt)
    {
        check(Pending.IsSet() && Pending->Command.RequestId == Receipt.Header.RequestId);
        if (Window.Num() == WindowCapacity)
        {
            Window.RemoveAt(0, 1, EAllowShrinking::No);
            WindowTerms.RemoveAt(0, 1, EAllowShrinking::No);
        }
        Window.Add(Receipt);
        WindowTerms.Add(Pending->Command);
        FinalizedThrough = Receipt.Header.RequestId;
        Pending.Reset();
    }

    ELDReceiptLookup Lookup(uint64 RequestId, const TReceipt*& Final, const FLDPendingReceiptInfo*& Info) const
    {
        Final = nullptr;
        Info = nullptr;
        if (Pending.IsSet() && PendingRequestId() == RequestId)
        {
            Info = &Pending->Info;
            return ELDReceiptLookup::Pending;
        }
        if (RequestId != 0 && RequestId <= FinalizedThrough)
        {
            Final = Find(RequestId);
            return Final ? ELDReceiptLookup::Final : ELDReceiptLookup::ReceiptExpired;
        }
        return ELDReceiptLookup::NotFound;
    }

    uint64 GetFinalizedThrough() const { return FinalizedThrough; }

    void Reset()
    {
        FinalizedThrough = 0;
        Pending.Reset();
        Window.Reset();
        WindowTerms.Reset();
    }

private:
    const TReceipt* Find(uint64 RequestId) const
    {
        for (int32 Index = Window.Num() - 1; Index >= 0; --Index)
        {
            if (Window[Index].Header.RequestId == RequestId)
            {
                return &Window[Index];
            }
        }
        return nullptr;
    }

    const TCommand* Terms(uint64 RequestId) const
    {
        for (int32 Index = Window.Num() - 1; Index >= 0; --Index)
        {
            if (Window[Index].Header.RequestId == RequestId)
            {
                return &WindowTerms[Index];
            }
        }
        return nullptr;
    }

    uint64 FinalizedThrough = 0;
    TOptional<FPending> Pending;
    TArray<TReceipt> Window;
    TArray<TCommand> WindowTerms;
};
