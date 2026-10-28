#include "World/LDB91Checkpoint.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/SecureHash.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

namespace LDCheckpoint
{
using JObject=TSharedPtr<FJsonObject>;
using JValue=TSharedPtr<FJsonValue>;
static FString Json(const JObject& O){FString S;FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&S));return S;}
static FString Guid(FGuid G){return G.IsValid()?G.ToString(EGuidFormats::Digits):FString();}
static JValue Vector(FVector P){return MakeShared<FJsonValueArray>(TArray<JValue>{MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)});}
static bool Keys(const JObject& O,std::initializer_list<const TCHAR*> Names)
{if(!O||O->Values.Num()!=int32(Names.size())){return false;}for(auto* N:Names){if(!O->HasField(N)){return false;}}return true;}
static bool Number(const JObject& O,const TCHAR* K,double& V,double Min,double Max)
{const auto* P=O->Values.Find(K);return P&&(*P)->Type==EJson::Number&&(*P)->TryGetNumber(V)&&FMath::IsFinite(V)&&V>=Min&&V<=Max;}
template<class T>static bool Integer(const JObject& O,const TCHAR* K,T& V,double Min,double Max)
{double N;if(!Number(O,K,N,Min,Max)||FMath::FloorToDouble(N)!=N){return false;}V=T(N);return true;}
static bool String(const JObject& O,const TCHAR* K,FString& V,int32 Limit=128)
{const auto* P=O->Values.Find(K);return P&&(*P)->Type==EJson::String&&(*P)->TryGetString(V)&&V.Len()<=Limit;}
static bool Boolean(const JObject& O,const TCHAR* K,bool& V)
{const auto* P=O->Values.Find(K);return P&&(*P)->Type==EJson::Boolean&&(*P)->TryGetBool(V);}
static bool Id(const JObject& O,const TCHAR* K,FGuid& G,bool Optional=false)
{FString S;if(!String(O,K,S,32)){return false;}if(S.IsEmpty()&&Optional){G.Invalidate();return true;}return FGuid::ParseExact(S,EGuidFormats::Digits,G)&&G.IsValid();}
static bool Point(const JObject& O,const TCHAR* K,FVector& P)
{const TArray<JValue>* A;if(!O->TryGetArrayField(K,A)||A->Num()!=3){return false;}double N[3];for(int I=0;I<3;++I){if((*A)[I]->Type!=EJson::Number||!(*A)[I]->TryGetNumber(N[I])||!FMath::IsFinite(N[I])){return false;}}P=FVector(N[0],N[1],N[2]);return FMath::Abs(P.X)<=118000&&FMath::Abs(P.Y)<=118000&&P.Z>=-100&&P.Z<=1600;}
FString Hash(const FString& Text){FTCHARToUTF8 B(*Text);FSHA256Signature H;SHA256(reinterpret_cast<const uint8*>(B.Get()),B.Length(),H.Signature);return H.ToString().ToLower();}
FString Encode(const FData& D)
{
    auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("committed"),D.Committed);Root->SetNumberField(TEXT("door_mutation"),D.DoorMutation);TArray<JValue> Objects,Items,Consumed,Doors;
    for(const auto& R:D.Objects){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("id"),R.Id);O->SetNumberField(TEXT("kind"),R.Kind);O->SetField(TEXT("position"),Vector(R.Position));O->SetField(TEXT("clear"),Vector(R.Clear));O->SetNumberField(TEXT("revision"),R.Revision);O->SetNumberField(TEXT("hp"),R.Hp);O->SetBoolField(TEXT("broken"),R.Broken);O->SetStringField(TEXT("harvest"),Guid(R.Harvest));O->SetStringField(TEXT("yield"),Guid(R.Yield));O->SetStringField(TEXT("burn"),Guid(R.Burn));O->SetStringField(TEXT("physical"),Guid(R.Physical));TArray<JValue> C;for(auto G:R.Contents){C.Add(MakeShared<FJsonValueString>(Guid(G)));}O->SetArrayField(TEXT("contents"),C);Objects.Add(MakeShared<FJsonValueObject>(O));}
    for(const auto& R:D.Items){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("id"),Guid(R.Id));O->SetStringField(TEXT("receipt"),Guid(R.Receipt));O->SetStringField(TEXT("origin"),R.Origin);O->SetStringField(TEXT("location"),R.Location);O->SetNumberField(TEXT("kind"),R.Kind);O->SetNumberField(TEXT("revision"),R.Revision);O->SetField(TEXT("position"),Vector(R.Position));O->SetNumberField(TEXT("fuel"),R.Fuel);O->SetBoolField(TEXT("lit"),R.Lit);O->SetNumberField(TEXT("ready_ms"),R.Ready);O->SetNumberField(TEXT("proc_ms"),R.Proc);Items.Add(MakeShared<FJsonValueObject>(O));}
    for(const auto& R:D.Consumed){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("id"),Guid(R.Id));O->SetStringField(TEXT("receipt"),Guid(R.Receipt));O->SetStringField(TEXT("origin"),R.Origin);O->SetNumberField(TEXT("kind"),R.Kind);Consumed.Add(MakeShared<FJsonValueObject>(O));}
    for(const auto& R:D.Doors){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("id"),R.Id);O->SetNumberField(TEXT("revision"),R.Revision);O->SetNumberField(TEXT("state"),R.State);Doors.Add(MakeShared<FJsonValueObject>(O));}
    Root->SetArrayField(TEXT("objects"),Objects);Root->SetArrayField(TEXT("items"),Items);Root->SetArrayField(TEXT("consumed"),Consumed);Root->SetArrayField(TEXT("doors"),Doors);return Json(Root);
}
bool Decode(const FString& Text,FData& Out,FString& Error)
{
    auto Fail=[&](){Error=TEXT("Invalid checkpoint schema, bounds or duplicate identity");return false;};JObject Root;if(Text.Len()>4*1024*1024||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Keys(Root,{TEXT("committed"),TEXT("door_mutation"),TEXT("objects"),TEXT("items"),TEXT("consumed"),TEXT("doors")})){return Fail();}
    FData D;const TArray<JValue> *Objects,*Items,*Consumed,*Doors;
    if(!Integer(Root,TEXT("committed"),D.Committed,0,MAX_uint32)||!Integer(Root,TEXT("door_mutation"),D.DoorMutation,1,MAX_uint32)||!Root->TryGetArrayField(TEXT("objects"),Objects)||!Root->TryGetArrayField(TEXT("items"),Items)||!Root->TryGetArrayField(TEXT("consumed"),Consumed)||!Root->TryGetArrayField(TEXT("doors"),Doors)||Objects->Num()<435||Objects->Num()>642||Items->Num()+Consumed->Num()<143||Items->Num()+Consumed->Num()>207||Doors->Num()!=72){return Fail();}
    TSet<FString> ObjectIds,DoorIds;TSet<FGuid> ItemIds;
    for(const auto& V:*Objects)
    {if(V->Type!=EJson::Object){return Fail();}const auto O=V->AsObject();FObject R;const TArray<JValue>* Contents;if(!Keys(O,{TEXT("id"),TEXT("kind"),TEXT("position"),TEXT("clear"),TEXT("revision"),TEXT("hp"),TEXT("broken"),TEXT("harvest"),TEXT("yield"),TEXT("burn"),TEXT("physical"),TEXT("contents")})||!String(O,TEXT("id"),R.Id)||R.Id.IsEmpty()||ObjectIds.Contains(R.Id)||!Integer(O,TEXT("kind"),R.Kind,1,15)||!Point(O,TEXT("position"),R.Position)||!Point(O,TEXT("clear"),R.Clear)||!Integer(O,TEXT("revision"),R.Revision,1,MAX_uint32)||!Number(O,TEXT("hp"),R.Hp,0,80)||!Boolean(O,TEXT("broken"),R.Broken)||!Id(O,TEXT("harvest"),R.Harvest,true)||!Id(O,TEXT("yield"),R.Yield,true)||!Id(O,TEXT("burn"),R.Burn,true)||!Id(O,TEXT("physical"),R.Physical,true)||!O->TryGetArrayField(TEXT("contents"),Contents)||Contents->Num()>24){return Fail();}ObjectIds.Add(R.Id);TSet<FGuid> Unique;for(auto C:*Contents){FString S;FGuid G;if(C->Type!=EJson::String||!C->TryGetString(S)||!FGuid::ParseExact(S,EGuidFormats::Digits,G)||!G.IsValid()||Unique.Contains(G)){return Fail();}Unique.Add(G);R.Contents.Add(G);}D.Objects.Add(MoveTemp(R));}
    for(const auto& V:*Items)
    {if(V->Type!=EJson::Object){return Fail();}const auto O=V->AsObject();FItem R;if(!Keys(O,{TEXT("id"),TEXT("receipt"),TEXT("origin"),TEXT("location"),TEXT("kind"),TEXT("revision"),TEXT("position"),TEXT("fuel"),TEXT("lit"),TEXT("ready_ms"),TEXT("proc_ms")})||!Id(O,TEXT("id"),R.Id)||ItemIds.Contains(R.Id)||!Id(O,TEXT("receipt"),R.Receipt)||!String(O,TEXT("origin"),R.Origin)||R.Origin.IsEmpty()||!String(O,TEXT("location"),R.Location)||!Integer(O,TEXT("kind"),R.Kind,0,97)||!Integer(O,TEXT("revision"),R.Revision,1,MAX_uint32)||!Point(O,TEXT("position"),R.Position)||!Integer(O,TEXT("fuel"),R.Fuel,0,300000)||!Boolean(O,TEXT("lit"),R.Lit)||!Integer(O,TEXT("ready_ms"),R.Ready,0,300000)||!Integer(O,TEXT("proc_ms"),R.Proc,0,300000)){return Fail();}ItemIds.Add(R.Id);D.Items.Add(MoveTemp(R));}
    for(const auto& V:*Consumed)
    {if(V->Type!=EJson::Object){return Fail();}const auto O=V->AsObject();FConsumed R;if(!Keys(O,{TEXT("id"),TEXT("receipt"),TEXT("origin"),TEXT("kind")})||!Id(O,TEXT("id"),R.Id)||ItemIds.Contains(R.Id)||!Id(O,TEXT("receipt"),R.Receipt)||!String(O,TEXT("origin"),R.Origin)||R.Origin.IsEmpty()||!Integer(O,TEXT("kind"),R.Kind,0,97)){return Fail();}ItemIds.Add(R.Id);D.Consumed.Add(MoveTemp(R));}
    for(const auto& V:*Doors)
    {if(V->Type!=EJson::Object){return Fail();}const auto O=V->AsObject();FDoor R;if(!Keys(O,{TEXT("id"),TEXT("revision"),TEXT("state")})||!String(O,TEXT("id"),R.Id)||R.Id.IsEmpty()||DoorIds.Contains(R.Id)||!Integer(O,TEXT("revision"),R.Revision,1,MAX_uint32)||!Integer(O,TEXT("state"),R.State,0,7)){return Fail();}DoorIds.Add(R.Id);D.Doors.Add(MoveTemp(R));}
    Out=MoveTemp(D);return true;
}
FString Envelope(const FString& Payload,const FString& Objects,const FString& Doors)
{auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("schema"),1);O->SetStringField(TEXT("profile"),TEXT("v09_environment"));O->SetStringField(TEXT("scope"),TEXT("settled_environment"));O->SetStringField(TEXT("objects_contract"),Objects);O->SetStringField(TEXT("doors_contract"),Doors);O->SetStringField(TEXT("payload"),Payload);O->SetStringField(TEXT("sha256"),Hash(Payload));return Json(O);}
bool Open(const FString& Text,const FString& Objects,const FString& Doors,FData& D,FString& Error)
{JObject O;FString Profile,Scope,A,B,Payload,Sha;int Schema=0;if(Text.Len()>5*1024*1024||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),O)||!Keys(O,{TEXT("schema"),TEXT("profile"),TEXT("scope"),TEXT("objects_contract"),TEXT("doors_contract"),TEXT("payload"),TEXT("sha256")})||!Integer(O,TEXT("schema"),Schema,1,1)||!String(O,TEXT("profile"),Profile)||Profile!=TEXT("v09_environment")||!String(O,TEXT("scope"),Scope)||Scope!=TEXT("settled_environment")||!String(O,TEXT("objects_contract"),A)||A!=Objects||!String(O,TEXT("doors_contract"),B)||B!=Doors||!String(O,TEXT("payload"),Payload,4*1024*1024)||!String(O,TEXT("sha256"),Sha,64)||Sha!=Hash(Payload)){Error=TEXT("Checkpoint profile, contract or checksum rejected");return false;}return Decode(Payload,D,Error);}
}
