#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Combat/LDEnvironmentRules.h"
#include "Combat/LDTacticalRequest.h"
#include "Net/DataBunch.h"
#include "UObject/UnrealType.h"
#include "World/LDEnvironmentDirector.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDB91FieldWire,"Lockdown.Rules.B91FieldWire",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDB91FieldWire::RunTest(const FString&)
{
    FLDTacticalRequest R;R.Schema=8;R.WireVersion=5;R.ActionKind=1;R.SourceKind=2;R.Match=R.Life=R.Token=R.BuildRevision=1;R.CatalogDigest=FString::ChrN(64,'a');R.SourceDefinition=TEXT("water_flask");R.Point=FVector(-118000,118000,800);
    FNetBitWriter W(nullptr,8192);bool Good=false;R.NetSerialize(W,nullptr,Good);TestTrue(TEXT("Explicit full-map wire accepts supported boundary point"),Good);
    FLDTacticalRequest Read;FNetBitReader Reader(nullptr,W.GetData(),W.GetNumBits());Read.NetSerialize(Reader,nullptr,Good);TestTrue(TEXT("Full-map request preserves position and binding identity"),Good&&FVector(Read.Point).Equals(R.Point,.11)&&Read.SourceDefinition==R.SourceDefinition);
    R.WireVersion=4;FNetBitWriter Old(nullptr,8192);R.NetSerialize(Old,nullptr,Good);TestFalse(TEXT("Legacy schema8 wire keeps its old bounds"),Good);
    R.WireVersion=5;R.Point.Z=1700;FNetBitWriter High(nullptr,8192);R.NetSerialize(High,nullptr,Good);TestFalse(TEXT("Unsupported full-map height is refused"),Good);
    const TSet<FName> Allowed={TEXT("Position"),TEXT("Revision"),TEXT("States")};int Fields=0;
    for(TFieldIterator<FProperty> I(FLDEnvironmentSurface::StaticStruct());I;++I){++Fields;TestTrue(TEXT("Surface view exposes only approved observed facts"),Allowed.Contains(I->GetFName()));TestNull(TEXT("No canonical references in surface view"),CastField<FObjectPropertyBase>(*I));}TestEqual(TEXT("Surface wire has three fields"),Fields,3);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDB91FieldMask,"Lockdown.Rules.B91FieldMask",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDB91FieldMask::RunTest(const FString&)
{
    const FVector Points[]={FVector(-100000,-100000,0),FVector(100000,100000,800),FVector(-118000,118000,400)};
    for(auto P:Points){const auto Cells=LDEnvironment::Mask(P,P,0,180,true,16,false,true);TestTrue(TEXT("Full-map signed mask admits all elevation bands and quadrants"),!Cells.IsEmpty()&&Cells.Num()<=16);for(auto Id:Cells){TestTrue(TEXT("Cell centre stays near selected target"),FVector::DistSquared2D(LDRiver::Centre(Id),P)<=180*180);}
     TestTrue(TEXT("Legacy grid does not silently widen"),LDEnvironment::Mask(P,P,0,180,true,16).IsEmpty());}
    LDEnvironment::FCell Slope;Slope.SurfaceZ=400;Slope.SurfaceNormal=FVector(-.25,0,1).GetSafeNormal();const FIntPoint Id(0,0);const FVector Centre=LDRiver::Centre(Id);
    TestTrue(TEXT("Support follows a sloped floor beyond centre-height tolerance"),Slope.Supports(Id,Centre+FVector(90,0,422.5)));
    TestFalse(TEXT("Support rejects a separate floor above that plane"),Slope.Supports(Id,Centre+FVector(90,0,822.5)));
    const auto Mask=LDEnvironment::Mask(FVector(-100000,100,0),FVector(-99000,100,0),700,80,false,4,false,true);TestTrue(TEXT("Full-map line retains cast cap"),Mask.Num()<=4&&!Mask.IsEmpty());return true;
}
#endif
