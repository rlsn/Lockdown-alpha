#include "Economy/LDEconomyCatalog.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"

namespace
{
    bool Integer(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,int32 Min,int32 Max,int32& Out)
    {double N=0;if(!O||!O->TryGetNumberField(Key,N)||!FMath::IsFinite(N)||N<Min||N>Max||N!=FMath::FloorToDouble(N)){return false;}Out=int32(N);return true;}
    bool Object(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,TSharedPtr<FJsonObject>& Out)
    {const TSharedPtr<FJsonObject>* P=nullptr;if(!O||!O->TryGetObjectField(Key,P)){return false;}Out=*P;return Out.IsValid();}
    bool ValidPrice(const LDEconomy::FMarketGood& G,int32 S,int32 N,int32 D)
    {return G.Target>0&&G.Target<=1000&&G.Cap==FMath::Max(12,2*G.Target)&&G.Base>0&&G.Base<=1000000&&S>=0&&S<=G.Cap+1&&N>0&&N<=100&&D>0&&D<=100;}
}
int32 LDEconomy::Ask(const FMarketGood& G,int32 S,int32 N,int32 D)
{
    if(!ValidPrice(G,S,N,D)){return -1;}const int64 A=int64(G.Base)*(3*G.Target-S)*N,B=int64(2)*G.Target*D;
    // Signed ceiling is intentional when stock exceeds 3*T (small markets).
    const int64 Ceil=A>=0?(A+B-1)/B:A/B;
    return int32(FMath::Clamp<int64>(Ceil,(G.Base+1)/2,(3*G.Base+1)/2));
}
int32 LDEconomy::Bid(const FMarketGood& G,int32 S,int32 N,int32 D)
{
    if(!ValidPrice(G,S,N,D)||S>G.Cap){return -1;}
    return FMath::Min(7*Ask(G,S,N,D)/10,FMath::Max(0,Ask(G,S+1,N,D)-1));
}
bool LDEconomy::FCatalog::Load(FString& Error)
{
    FString Json;if(!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/economy_rules.json")))){Error=TEXT("Missing staged economy catalog");return false;}return Parse(Json,Error);
}
bool LDEconomy::FCatalog::Parse(const FString& Json,FString& Error)
{
    Error.Reset();auto Reject=[&](const TCHAR* Why){Error=Why;return false;};TSharedPtr<FJsonObject> Root;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root.IsValid()){return Reject(TEXT("Invalid economy JSON"));}
    FCatalog C;if(!Integer(Root,TEXT("schemaVersion"),1,1,C.Version)){return Reject(TEXT("Unsupported economy schema"));}
    TSharedPtr<FJsonObject> Goods,Weights,MarketRows,Gather,Profile;
    if(!Object(Root,TEXT("goods"),Goods)||!Object(Root,TEXT("otherWeights"),Weights)||!Object(Root,TEXT("markets"),MarketRows)||!Object(Root,TEXT("gather"),Gather)||!Object(Root,TEXT("profile"),Profile)){return Reject(TEXT("Missing economy section"));}
    const TCHAR* Names[]={TEXT("Fiber"),TEXT("Ore"),TEXT("Crystal"),TEXT("Toolkit"),TEXT("Core")};
    if(Goods->Values.Num()!=5||MarketRows->Values.Num()!=3){return Reject(TEXT("Unexpected good or institution count"));}
    for(int32 I=0;I<5;++I){TSharedPtr<FJsonObject> Good;int32 Weight=0;if(!Object(Goods,Names[I],Good)||!Integer(Good,TEXT("weight"),0,100,Weight)||Weight!=LDInventory::UnitWeight(ELDItemKind(uint8(ELDItemKind::Fiber)+I))){return Reject(TEXT("Economy weight disagrees with inventory definitions"));}}
    const TCHAR* Other[]={TEXT("book"),TEXT("ward-charge"),TEXT("legacySeal")};
    for(int32 I=0;I<3;++I){int32 W=0;if(!Integer(Weights,Other[I],0,100,W)||W!=LDInventory::UnitWeight(ELDItemKind(I))){return Reject(TEXT("Legacy item weight mismatch"));}}
    int32 Slots=0,Seconds=0;if(!Integer(Profile,TEXT("inventorySlots"),24,24,Slots)||!Integer(Profile,TEXT("seconds"),1500,1500,Seconds)||!Integer(Gather,TEXT("nodes"),2,2,C.Nodes)||!Integer(Gather,TEXT("chargesPerNode"),3,3,C.Charges)){return Reject(TEXT("Unsupported economy profile or node budget"));}
    for(const TCHAR* Name:{TEXT("WestTown"),TEXT("EastTown"),TEXT("City")})
    {
        TSharedPtr<FJsonObject> M,Stock;int32 Gold=0;
        if(!Object(MarketRows,Name,M)||!Integer(M,TEXT("gold"),0,1000000,Gold)||!Object(M,TEXT("goods"),Stock)||Stock->Values.Num()!=4){return Reject(TEXT("Invalid institution endowment"));}
        FMarketDefinition Def;Def.Id=Name;Def.Gold=Gold;
        for(int32 I=0;I<4;++I){TSharedPtr<FJsonObject> G;FMarketGood Row;Row.Kind=ELDItemKind(uint8(ELDItemKind::Fiber)+I);if(!Object(Stock,Names[I],G)||!Integer(G,TEXT("stock"),1,1000,Row.Target)||!Integer(G,TEXT("base"),1,1000000,Row.Base)){return Reject(TEXT("Invalid market stock/price"));}Row.Cap=FMath::Max(12,2*Row.Target);Def.Goods.Add(Row);}C.Markets.Add(MoveTemp(Def));
    }
    TSharedPtr<FJsonObject> Supply;if(!Object(Root,TEXT("officialSupply"),Supply)||!Integer(Supply,TEXT("Fiber"),1,1000000,C.FiberPrice)||!Integer(Supply,TEXT("Ore"),1,1000000,C.OrePrice)){return Reject(TEXT("Invalid official supply prices"));}
    C.Checksum=FMD5::HashAnsiString(*Json);*this=MoveTemp(C);return true;
}
