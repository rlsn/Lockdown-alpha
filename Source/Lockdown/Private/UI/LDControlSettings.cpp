#include "UI/LDControlSettings.h"
#include "Kismet/GameplayStatics.h"

const TArray<FLDControlBinding>& ULDControlSettings::Defaults()
{
    static const TArray<FLDControlBinding> Values={
        {TEXT("Select"),TEXT("Select / confirm target"),EKeys::LeftMouseButton},
        {TEXT("Navigate"),TEXT("Move / approach / cancel targeting"),EKeys::RightMouseButton},
        {TEXT("Attack"),TEXT("Arm basic attack"),EKeys::A},
        {TEXT("Skill1"),TEXT("Equipped skill 1"),EKeys::Q},{TEXT("Skill2"),TEXT("Equipped skill 2"),EKeys::W},
        {TEXT("Skill3"),TEXT("Equipped skill 3"),EKeys::E},{TEXT("Skill4"),TEXT("Equipped skill 4"),EKeys::R},
        {TEXT("Item1"),TEXT("Quick item 1"),EKeys::One},{TEXT("Item2"),TEXT("Quick item 2"),EKeys::Two},
        {TEXT("Item3"),TEXT("Quick item 3"),EKeys::Three},{TEXT("Item4"),TEXT("Quick item 4"),EKeys::Four},
        {TEXT("Context"),TEXT("Hold contextual action"),EKeys::F},{TEXT("Stop"),TEXT("Stop movement / held action"),EKeys::S},
        {TEXT("Sprint"),TEXT("Hold sprint"),EKeys::LeftShift},{TEXT("Inventory"),TEXT("Inventory"),EKeys::I},
        {TEXT("Development"),TEXT("Character / preparation"),EKeys::P},{TEXT("Trade"),TEXT("Offer trade"),EKeys::T},
        {TEXT("PanUp"),TEXT("Pan camera up"),EKeys::Up},{TEXT("PanDown"),TEXT("Pan camera down"),EKeys::Down},
        {TEXT("PanLeft"),TEXT("Pan camera left"),EKeys::Left},{TEXT("PanRight"),TEXT("Pan camera right"),EKeys::Right},
        {TEXT("Recenter"),TEXT("Hold camera recenter"),EKeys::SpaceBar},{TEXT("Follow"),TEXT("Toggle camera follow"),EKeys::Home},
        {TEXT("Zoom"),TEXT("Camera zoom"),EKeys::MouseWheelAxis,true},
        {TEXT("Help"),TEXT("Help / controls / settings"),EKeys::F1},{TEXT("Back"),TEXT("Back / cancel (reserved)"),EKeys::Escape}
    };return Values;
}
FKey ULDControlSettings::Key(FName Action) const
{if(const FKey* K=Bindings.Find(Action)){return *K;}for(const auto& B:Defaults()){if(B.Action==Action){return B.Key;}}return FKey();}
FString ULDControlSettings::Label(FName Action) const {return Key(Action).GetDisplayName().ToString();}
void ULDControlSettings::ResetBindings(){Bindings.Reset();for(const auto& B:Defaults()){Bindings.Add(B.Action,B.Key);}}
bool ULDControlSettings::Rebind(FName Action,FKey NewKey,FString& Error)
{
    const auto* Definition=Defaults().FindByPredicate([&](const FLDControlBinding& B){return B.Action==Action;});
    if(!Definition||Action==TEXT("Back")||NewKey==EKeys::Escape){Error=TEXT("Escape is reserved for Back.");return false;}
    if(!NewKey.IsValid()||NewKey.IsGamepadKey()||NewKey.IsAxis2D()||NewKey.IsAxis3D()
        ||(Definition->Axis&&NewKey!=EKeys::MouseWheelAxis)||(!Definition->Axis&&NewKey.IsAxis1D()))
    {Error=Definition->Axis?TEXT("Zoom uses the mouse wheel axis."):TEXT("Choose a keyboard key or mouse button.");return false;}
    for(const auto& B:Defaults())
    {
        const FKey Other=Key(B.Action);
        const bool WheelOverlap=(NewKey==EKeys::MouseScrollUp||NewKey==EKeys::MouseScrollDown)&&Other==EKeys::MouseWheelAxis;
        if(B.Action!=Action&&(Other==NewKey||WheelOverlap)){Error=TEXT("Already used by ")+B.Label+TEXT(". Choose another key.");return false;}
    }
    Bindings.Add(Action,NewKey);Error.Empty();return true;
}
bool ULDControlSettings::Valid() const
{
    if(Schema!=1||Bindings.Num()!=Defaults().Num()||Key(TEXT("Back"))!=EKeys::Escape||!FMath::IsFinite(TextScale)||TextScale<1||TextScale>1.25||!FMath::IsFinite(Zoom)||Zoom<1100||Zoom>1700){return false;}
    TSet<FKey> Seen;
    for(const auto& B:Defaults())
    {const FKey K=Key(B.Action);if(!Bindings.Contains(B.Action)||!K.IsValid()||K.IsGamepadKey()||K.IsAxis2D()||K.IsAxis3D()||Seen.Contains(K)||(!B.Axis&&K.IsAxis1D())||(B.Axis&&K!=EKeys::MouseWheelAxis)){return false;}Seen.Add(K);}
    return !Seen.Contains(EKeys::MouseScrollUp)&&!Seen.Contains(EKeys::MouseScrollDown);
}
ULDControlSettings* ULDControlSettings::Load(const FString& Slot)
{
    const FString SafeSlot=Slot==TEXT("LockdownLocalControlsV1")||Slot.StartsWith(TEXT("LockdownControlsProbe_"))?Slot:TEXT("LockdownLocalControlsV1");
    auto* Settings=UGameplayStatics::DoesSaveGameExist(SafeSlot,0)?Cast<ULDControlSettings>(UGameplayStatics::LoadGameFromSlot(SafeSlot,0)):nullptr;
    if(!Settings||!Settings->Valid()){Settings=CastChecked<ULDControlSettings>(UGameplayStatics::CreateSaveGameObject(StaticClass()));Settings->ResetBindings();}
    Settings->StorageSlot=SafeSlot;
    return Settings;
}
bool ULDControlSettings::Save() const{return UGameplayStatics::SaveGameToSlot(const_cast<ULDControlSettings*>(this),StorageSlot,0);}
