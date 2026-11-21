#include "LDBasketballMotionImport.h"
#include "Character/LDBasketballMotion.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimCurveTypes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

namespace
{
FString SourcePath(const FString& Source){return FPaths::Combine(FPaths::ProjectDir(),TEXT("SourceData"),Source);}
bool Digest(const FString& Source,FString& Out,FString& Error)
{
    TArray<uint8> Bytes;
    if(!FFileHelper::LoadFileToArray(Bytes,*SourcePath(Source))){Error=TEXT("Missing basketball animation source: ")+Source;return false;}
    uint8 Hash[SHA256_DIGEST_LENGTH];
    if (!SHA256(Bytes.GetData(),Bytes.Num(),Hash)) { Error=TEXT("Basketball source hash failed.");return false; }
    Out=BytesToHex(Hash,SHA256_DIGEST_LENGTH).ToLower();return true;
}
template<class T>T* Asset(const FString& Folder,const FString& Name)
{
    const FString Package=Folder/Name;
    if(auto* Existing=LoadObject<T>(nullptr,*(Package+TEXT(".")+Name))){return Existing;}
    auto* Result=NewObject<T>(CreatePackage(*Package),*Name,RF_Public|RF_Standalone);
    FAssetRegistryModule::AssetCreated(Result);return Result;
}
bool Numbers(const TArray<TSharedPtr<FJsonValue>>& Values,int32 Count,TArray<double>& Out)
{
    if(Values.Num()!=Count){return false;}Out.Reset();
    for(const auto& Value:Values){double N;if(!Value->TryGetNumber(N)||!FMath::IsFinite(N)){return false;}Out.Add(N);}return true;
}
bool Vec(const TSharedPtr<FJsonObject>& Object,const FString& Name,FVector& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Values;TArray<double> V;
    if(!Object->TryGetArrayField(Name,Values)||!Numbers(*Values,3,V)){return false;}
    Out=FVector(V[0],V[1],V[2]);return true;
}
}

bool LDBasketballMotionImport::ValidateSource(const ULDBasketballMotionSet& Motion,const FString& Source,FString& Error)
{
    FString Hash;if(!Digest(Source,Hash,Error)){return false;}
    if(Hash!=Motion.SourceSha256){Error=TEXT("Basketball source changed; run ImportDemoArt.ps1 -Mode ImportBasketball.");return false;}
    return true;
}

bool LDBasketballMotionImport::Import(USkeletalMesh& Body,const FString& Source,const FString& Folder,const FString& Name,
    TArray<UObject*>& Assets,FString& Error)
{
    const auto Fail=[&](const FString& Why){Error=TEXT("Basketball animation import: ")+Why;return false;};
    FString Text,Hash;TSharedPtr<FJsonObject> Json;
    if(!Digest(Source,Hash,Error)||!FFileHelper::LoadFileToString(Text,*SourcePath(Source))
        ||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Json)||!Json.IsValid()){return Fail(TEXT("invalid source JSON."));}
    int32 Schema,Fps;const TArray<TSharedPtr<FJsonValue>>* Rows;const TSharedPtr<FJsonObject>* Groups;
    const TSharedPtr<FJsonObject>* Palms;const TSharedPtr<FJsonObject>* Normals;double Radius,RunSpeed;
    if(!Json->TryGetNumberField(TEXT("schema"),Schema)||Schema!=1||!Json->TryGetNumberField(TEXT("fps"),Fps)||Fps<=0||Fps>120
        ||!Json->TryGetArrayField(TEXT("clips"),Rows)||Rows->IsEmpty()||!Json->TryGetObjectField(TEXT("groups"),Groups)
        ||!Json->TryGetObjectField(TEXT("palms"),Palms)||!Json->TryGetObjectField(TEXT("palm_normals"),Normals)
        ||!Json->TryGetNumberField(TEXT("ball_radius_cm"),Radius)||!FMath::IsFinite(Radius)||Radius<=0
        ||!Json->TryGetNumberField(TEXT("run_speed_cm_per_sec"),RunSpeed)||!FMath::IsFinite(RunSpeed)||RunSpeed<=0)
    {return Fail(TEXT("invalid source header."));}
    FVector Right=FVector::ZeroVector,Left=FVector::ZeroVector,RightNormal=FVector::ZeroVector,LeftNormal=FVector::ZeroVector;
    if(!Vec(*Palms,TEXT("r"),Right)||!Vec(*Palms,TEXT("l"),Left)||!Vec(*Normals,TEXT("r"),RightNormal)||!Vec(*Normals,TEXT("l"),LeftNormal))
    {return Fail(TEXT("invalid palm calibration."));}
    struct FTrack{FName Bone;TArray<FVector3f> P,S;TArray<FQuat4f> Q;};
    struct FClip{int32 Id,Frames;FString Name;FLDBasketballActionClip Window;TArray<FTrack> Tracks;TMap<FName,TArray<FRichCurveKey>> Curves;};
    TArray<FClip> Parsed;TSet<int32> Ids;TSet<FString> Names;
    // Validate every track and group before changing any existing asset.
    for(const auto& Value:*Rows)
    {
        const TSharedPtr<FJsonObject>* Row;const TSharedPtr<FJsonObject>* Tracks;const TSharedPtr<FJsonObject>* Curves;
        if(!Value->TryGetObject(Row)){return Fail(TEXT("clip is not an object."));}
        FClip Clip;double Start,Release,End;
        if(!(*Row)->TryGetNumberField(TEXT("id"),Clip.Id)||Ids.Contains(Clip.Id)||!(*Row)->TryGetStringField(TEXT("asset"),Clip.Name)
            ||Clip.Name.IsEmpty()||Clip.Name.Contains(TEXT("/"))||Clip.Name.Contains(TEXT("."))||Names.Contains(Clip.Name)
            ||!(*Row)->TryGetNumberField(TEXT("frames"),Clip.Frames)||Clip.Frames<2||Clip.Frames>36000
            ||!(*Row)->TryGetNumberField(TEXT("start"),Start)||!(*Row)->TryGetNumberField(TEXT("release"),Release)
            ||!(*Row)->TryGetNumberField(TEXT("end"),End)||!FMath::IsFinite(Start)||!FMath::IsFinite(Release)||!FMath::IsFinite(End)
            ||Start<0||Start>=Release||Release>=End||End>double(Clip.Frames-1)/Fps+.001
            ||!(*Row)->TryGetBoolField(TEXT("left_hand"),Clip.Window.bLeftHand)
            ||!(*Row)->TryGetObjectField(TEXT("tracks"),Tracks)||(*Tracks)->Values.IsEmpty()
            ||!(*Row)->TryGetObjectField(TEXT("curves"),Curves)) {return Fail(TEXT("invalid clip header."));}
        Clip.Window.Start=Start;Clip.Window.Release=Release;Clip.Window.End=End;
        for(const auto& Pair:(*Tracks)->Values)
        {
            if(Body.GetRefSkeleton().FindBoneIndex(FName(*Pair.Key))==INDEX_NONE){return Fail(FString(TEXT("source skeleton does not match body: "))+Pair.Key);}
            const TSharedPtr<FJsonObject>* Channels;if(!Pair.Value->TryGetObject(Channels)){return Fail(TEXT("invalid track."));}
            FTrack Track;Track.Bone=FName(*Pair.Key);
            const TArray<TSharedPtr<FJsonValue>> *P,*Q,*S;
            if(!(*Channels)->TryGetArrayField(TEXT("p"),P)||!(*Channels)->TryGetArrayField(TEXT("q"),Q)||!(*Channels)->TryGetArrayField(TEXT("s"),S))
            {return Fail(TEXT("missing transform channel."));}
            for(const auto* Channel:{P,Q,S}){if(Channel->Num()!=1&&Channel->Num()!=Clip.Frames){return Fail(TEXT("inconsistent track frame count."));}}
            for(int32 I=0;I<Clip.Frames;++I)
            {
                const TArray<TSharedPtr<FJsonValue>> *PV,*QV,*SV;TArray<double> A,B,C;
                if(!(*P)[P->Num()==1?0:I]->TryGetArray(PV)||!(*Q)[Q->Num()==1?0:I]->TryGetArray(QV)||!(*S)[S->Num()==1?0:I]->TryGetArray(SV)
                    ||!Numbers(*PV,3,A)||!Numbers(*QV,4,B)||!Numbers(*SV,3,C)){return Fail(TEXT("invalid transform values."));}
                const FQuat4f Rotation(B[0],B[1],B[2],B[3]);
                if(!FMath::IsNearlyEqual(Rotation.SizeSquared(),1.f,.002f)){return Fail(TEXT("invalid rotation."));}
                Track.P.Add(FVector3f(A[0],A[1],A[2]));Track.Q.Add(Rotation.GetNormalized());Track.S.Add(FVector3f(C[0],C[1],C[2]));
            }
            Clip.Tracks.Add(MoveTemp(Track));
        }
        for(const auto& Curve:(*Curves)->Values)
        {
            const TArray<TSharedPtr<FJsonValue>>* Values;TArray<double> V;
            if(!Curve.Value->TryGetArray(Values)||!Numbers(*Values,Clip.Frames,V)){return Fail(TEXT("invalid curve samples."));}
            auto& Keys=Clip.Curves.Add(FName(*Curve.Key));
            for(int32 I=0;I<V.Num();++I){FRichCurveKey Key(float(I)/Fps,V[I]);Key.InterpMode=RCIM_Linear;Keys.Add(Key);}
        }
        for(const FName Curve:{FName(TEXT("BallX")),FName(TEXT("BallY")),FName(TEXT("BallZ")),FName(TEXT("BallLeft")),FName(TEXT("BallContact"))})
        {if(!Clip.Curves.Contains(Curve)){return Fail(TEXT("missing calibrated prop curve."));}}
        Ids.Add(Clip.Id);Names.Add(Clip.Name);Parsed.Add(MoveTemp(Clip));
    }
    const TPair<const TCHAR*,TArray<FLDBasketballActionClip> ULDBasketballMotionSet::*> Banks[]={
        {TEXT("idle"),&ULDBasketballMotionSet::Idle},{TEXT("run"),&ULDBasketballMotionSet::Run},{TEXT("handling"),&ULDBasketballMotionSet::Handling},
        {TEXT("passes"),&ULDBasketballMotionSet::Passes},{TEXT("standing_shots"),&ULDBasketballMotionSet::StandingShots},
        {TEXT("moving_shots"),&ULDBasketballMotionSet::MovingShots},{TEXT("turns"),&ULDBasketballMotionSet::Turns},{TEXT("crossovers"),&ULDBasketballMotionSet::Crossovers}};
    for(const auto& Bank:Banks)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values;if(!(*Groups)->TryGetArrayField(Bank.Key,Values)||Values->IsEmpty()){return Fail(TEXT("missing motion bank."));}
        for(const auto& V:*Values){int32 Id;if(!V->TryGetNumber(Id)||!Ids.Contains(Id)){return Fail(TEXT("motion bank references absent clip."));}}
    }
    auto* Motion=Asset<ULDBasketballMotionSet>(Folder,Name);Motion->SourceSha256=Hash;
    Motion->RightPalm=Right;Motion->LeftPalm=Left;Motion->RightPalmNormal=RightNormal.GetSafeNormal();Motion->LeftPalmNormal=LeftNormal.GetSafeNormal();
    Motion->BallRadiusCm=Radius;Motion->RunSpeedCmPerSec=RunSpeed;
    for(auto& Clip:Parsed)
    {
        auto* Animation=Asset<UAnimSequence>(Folder,Clip.Name);
        Animation->SetSkeleton(Body.GetSkeleton());Animation->SetPreviewMesh(&Body);
        Animation->bEnableRootMotion=false;Animation->bForceRootLock=true;Animation->RootMotionRootLock=ERootMotionRootLock::AnimFirstFrame;
        auto& Controller=Animation->GetController();Controller.OpenBracket(FText::FromString(TEXT("Import calibrated basketball motion")),false);
        Controller.InitializeModel();Controller.RemoveAllBoneTracks(false);Controller.RemoveAllCurvesOfType(ERawCurveTrackTypes::RCT_Float,false);
        Controller.SetFrameRate(FFrameRate(Fps,1),false);Controller.SetNumberOfFrames(FFrameNumber(Clip.Frames-1),false);
        for(const auto& Track:Clip.Tracks)
        {
            if(!Controller.AddBoneCurve(Track.Bone,false)||!Controller.SetBoneTrackKeys(Track.Bone,Track.P,Track.Q,Track.S,false))
            {Controller.CloseBracket(false);return Fail(TEXT("could not write bone track."));}
        }
        for(const auto& Pair:Clip.Curves)
        {
            const FAnimationCurveIdentifier Id(Pair.Key,ERawCurveTrackTypes::RCT_Float);
            if(!Controller.AddCurve(Id,AACF_DefaultCurve,false)||!Controller.SetCurveKeys(Id,Pair.Value,false))
            {Controller.CloseBracket(false);return Fail(TEXT("could not write prop curve."));}
        }
        Controller.NotifyPopulated();Controller.CloseBracket(false);Animation->MarkPackageDirty();Assets.Add(Animation);Clip.Window.Animation=Animation;
    }
    for(const auto& Bank:Banks)
    {
        auto& Clips=Motion->*(Bank.Value);Clips.Reset();
        for(const auto& Value:(*Groups)->GetArrayField(Bank.Key))
        {const int32 Id=int32(Value->AsNumber());Clips.Add(Parsed.FindByPredicate([&](const FClip& Clip){return Clip.Id==Id;})->Window);}
    }
    auto* Material=Asset<UMaterial>(Folder,TEXT("M_Basketball"));
    auto* Data=Material->GetEditorOnlyData();Data->ExpressionCollection.Empty();
    auto* UV=NewObject<UMaterialExpressionTextureCoordinate>(Material);auto* Pattern=NewObject<UMaterialExpressionCustom>(Material);
    FCustomInput Input;Input.InputName=TEXT("UV");Input.Input.Expression=UV;Pattern->Inputs={Input};Pattern->OutputType=CMOT_Float3;
    Pattern->Code=TEXT("float seam=min(abs(sin(UV.x*12.56637)),abs(cos(UV.y*3.141593))); seam=min(seam,abs(sin((UV.x+UV.y)*6.283185))); return lerp(float3(.018,.009,.004),float3(.64,.15,.018),smoothstep(.035,.07,seam));");
    auto* Roughness=NewObject<UMaterialExpressionConstant>(Material);Roughness->R=.82f;
    Data->ExpressionCollection.AddExpression(UV);Data->ExpressionCollection.AddExpression(Pattern);Data->ExpressionCollection.AddExpression(Roughness);
    Data->BaseColor.Expression=Pattern;Data->Roughness.Expression=Roughness;Material->PostEditChange();Material->MarkPackageDirty();Assets.Add(Material);
    Motion->BallMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));Motion->BallMaterial=Material;
    if(!Motion->Validate(Body,Error)){return false;}
    Motion->MarkPackageDirty();Assets.Add(Motion);return true;
}
