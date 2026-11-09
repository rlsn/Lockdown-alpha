#include "Art/LDBasketballMotionSet.h"
#include "Animation/AnimSequence.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchIndex.h"
#include "PoseSearch/PoseSearchDerivedData.h"

bool ULDBasketballMotionSet::SetDatabaseAnimations(UPoseSearchDatabase* Database, const TArray<UAnimSequence*>& Animations)
{
    if (!Database || Animations.IsEmpty()) { return false; }
    for (const auto* Animation:Animations) { if (!Animation) { return false; } }
    Database->Modify();
    while (Database->GetNumAnimationAssets()>0) { Database->RemoveAnimationAssetAt(Database->GetNumAnimationAssets()-1); }
    for (auto* Animation:Animations)
    {
        FPoseSearchDatabaseAnimationAsset Entry;
        Entry.AnimAsset=Animation;
        Database->AddAnimationAsset(Entry);
    }
    Database->PostEditChange();
    Database->MarkPackageDirty();
    return Database->GetNumAnimationAssets()==Animations.Num();
}

int32 ULDBasketballMotionSet::DatabasePoseCount(UPoseSearchDatabase* Database)
{
    if (!Database) { return 0; }
    using namespace UE::PoseSearch;
    const auto Built=FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database,ERequestAsyncBuildFlag::ContinueRequest);
    return Built==EAsyncBuildIndexResult::Success ? Database->GetSearchIndex().GetNumPoses():0;
}

#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
bool ULDBasketballMotionSet::SetPalmSocket(USkeletalMesh* Mesh, FName Name, FVector Location)
{
    if (!Mesh || Location.ContainsNaN()) { return false; }
    auto* Socket=const_cast<USkeletalMeshSocket*>(Mesh->FindSocket(Name));
    if (!Socket) { return false; }
    Socket->Modify(); Socket->RelativeLocation=Location;
    Socket->MarkPackageDirty(); Mesh->MarkPackageDirty();
    return true;
}
