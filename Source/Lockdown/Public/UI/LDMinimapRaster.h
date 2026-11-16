#pragma once
#include "CoreMinimal.h"
#include "Vision/LDViewTypes.h"
#include "Vision/LDWorldViewRelay.h"
#include "World/LDWorldQueryTypes.h"

namespace LDMinimapRaster
{
    struct FStaticKey
    {
        FBox2D Ground=FBox2D(ForceInit);
        FVector2D FrameSize=FVector2D::ZeroVector;
        double Scale=0.;
        float PixelScale=0.f;
        FLDWorldRevision World;
        bool Matches(const FStaticKey& Other) const
        {
            return Ground.bIsValid&&Other.Ground.bIsValid&&Ground.Min==Other.Ground.Min&&Ground.Max==Other.Ground.Max
                &&FrameSize==Other.FrameSize&&Scale==Other.Scale&&PixelScale==Other.PixelScale&&World==Other.World;
        }
    };
    inline FLinearColor GroundColor(){return FLinearColor(.035f,.05f,.045f);}
    inline FLinearColor CoverageColor(){return FLinearColor(.17f,.33f,.28f);}
    struct FCoverageKey
    {
        uint64 Serial=MAX_uint64;
        uint32 Match=0,Life=0;
        bool Matches(uint64 OtherSerial,uint32 OtherMatch,uint32 OtherLife) const
        {return Serial!=MAX_uint64&&Serial==OtherSerial&&Match==OtherMatch&&Life==OtherLife;}
    };
    inline const FLDWorldView* AuthorizedFrame(bool Alive,uint32 Match,uint32 Life,const FLDObservedFrameRead& Read)
    {
        return Alive&&Read.Frame&&Read.Status==ELDObservedViewStatus::Current&&Read.Frame->MatchGeneration==Match
            &&Read.Frame->ObserverLife==Life?Read.Frame:nullptr;
    }
    inline bool CoveragePixels(const TArray<uint8>& Bits,TArray<FColor>& Pixels)
    {
        if(Bits.Num()!=LDObservation::CoverageBytes){Pixels.Reset();return false;}
        Pixels.SetNumUninitialized(LDObservation::CoverageCells);
        const FColor Hidden=GroundColor().ToFColorSRGB(),Visible=CoverageColor().ToFColorSRGB();
        for(int32 I=0;I<Pixels.Num();++I){Pixels[I]=LDObservation::CoverageBit(Bits,I)?Visible:Hidden;}
        return true;
    }
}
