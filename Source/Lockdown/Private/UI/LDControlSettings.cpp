#include "UI/LDControlSettings.h"
#include "Kismet/GameplayStatics.h"

const TArray<FLDControlBinding>& ULDControlSettings::Defaults(bool Tactical)
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
    };
    static const TArray<FLDControlBinding> TacticalValues=[](){auto V=Values;V.RemoveAll([](const auto& B){return B.Action==TEXT("Item4");});V.Insert({TEXT("Skill5"),TEXT("Equipped skill 5"),EKeys::D},7);return V;}();
    return Tactical?TacticalValues:Values;
}
FKey ULDControlSettings::Key(FName Action) const
{if(const FKey* K=Bindings.Find(Action)){return *K;}for(const auto& B:Definitions()){if(B.Action==Action){return B.Key;}}return FKey();}
FString ULDControlSettings::Label(FName Action) const {return Key(Action).GetDisplayName().ToString();}
void ULDControlSettings::ResetBindings(){Bindings.Reset();for(const auto& B:Definitions()){Bindings.Add(B.Action,B.Key);}}
bool ULDControlSettings::Rebind(FName Action,FKey NewKey,FString& Error)
{
    const auto* Definition=Definitions().FindByPredicate([&](const FLDControlBinding& B){return B.Action==Action;});
    if(!Definition||Action==TEXT("Back")||NewKey==EKeys::Escape){Error=TEXT("Escape is reserved for Back.");return false;}
    if(!NewKey.IsValid()||NewKey.IsGamepadKey()||NewKey.IsAxis2D()||NewKey.IsAxis3D()
        ||(Definition->Axis&&NewKey!=EKeys::MouseWheelAxis)||(!Definition->Axis&&NewKey.IsAxis1D()))
    {Error=Definition->Axis?TEXT("Zoom uses the mouse wheel axis."):TEXT("Choose a keyboard key or mouse button.");return false;}
    for(const auto& B:Definitions())
    {
        const FKey Other=Key(B.Action);
        const bool WheelOverlap=(NewKey==EKeys::MouseScrollUp||NewKey==EKeys::MouseScrollDown)&&Other==EKeys::MouseWheelAxis;
        if(B.Action!=Action&&(Other==NewKey||WheelOverlap)){Error=TEXT("Already used by ")+B.Label+TEXT(". Choose another key.");return false;}
    }
    Bindings.Add(Action,NewKey);Error.Empty();return true;
}
bool ULDControlSettings::Valid() const
{
    if(Schema!=(Profile==7?2:1)||(Profile!=6&&Profile!=7)||Bindings.Num()!=Definitions().Num()||Key(TEXT("Back"))!=EKeys::Escape||!FMath::IsFinite(TextScale)||TextScale<1||TextScale>1.25||!FMath::IsFinite(Zoom)||Zoom<1100||Zoom>1700){return false;}
    TSet<FKey> Seen;
    for(const auto& B:Definitions())
    {const FKey K=Key(B.Action);if(!Bindings.Contains(B.Action)||!K.IsValid()||K.IsGamepadKey()||K.IsAxis2D()||K.IsAxis3D()||Seen.Contains(K)||(!B.Axis&&K.IsAxis1D())||(B.Axis&&K!=EKeys::MouseWheelAxis)){return false;}Seen.Add(K);}
    return !Seen.Contains(EKeys::MouseScrollUp)&&!Seen.Contains(EKeys::MouseScrollDown);
}
bool ULDControlSettings::MigrateTactical()
{
    if(Profile==7&&Schema==2){return Valid();}
    const auto Old=Bindings;TMap<FName,FKey> Next;const auto& Def=Defaults(true);TSet<FKey> Used;
    // D belongs to slot five after migration. Existing actions bound to D relocate.
    Used.Add(EKeys::D);Next.Add(TEXT("Skill5"),EKeys::D);
    for(const auto& B:Def){if(B.Action==TEXT("Skill5")){continue;}const auto* Existing=Old.Find(B.Action);const FKey K=Existing?*Existing:B.Key;
        const bool ValidKey=K.IsValid()&&!K.IsGamepadKey()&&!K.IsAxis2D()&&!K.IsAxis3D()&&(B.Axis?K==EKeys::MouseWheelAxis:!K.IsAxis1D())&&(B.Action==TEXT("Back")?K==EKeys::Escape:K!=EKeys::Escape)&&K!=EKeys::MouseScrollUp&&K!=EKeys::MouseScrollDown;
        if(ValidKey&&!Used.Contains(K)){Next.Add(B.Action,K);Used.Add(K);}}
    const TArray<FKey> Spare={EKeys::B,EKeys::C,EKeys::G,EKeys::H,EKeys::J,EKeys::K,EKeys::L,EKeys::M,EKeys::N,EKeys::O,EKeys::U,EKeys::V,EKeys::X,EKeys::Y,EKeys::Z,EKeys::Four,EKeys::Five};
    for(const auto& B:Def){if(Next.Contains(B.Action)){continue;}FKey K=B.Key;if(Used.Contains(K)){const auto* Free=Spare.FindByPredicate([&](FKey Key){return !Used.Contains(Key);});if(!Free){return false;}K=*Free;}Next.Add(B.Action,K);Used.Add(K);}
    Bindings=MoveTemp(Next);Profile=7;Schema=2;return Valid();
}
ULDControlSettings* ULDControlSettings::Load(const FString& Slot,uint8 RequestedProfile)
{
    const bool Tactical=RequestedProfile==7;const FString LegacySlot=Slot==TEXT("LockdownLocalControlsV1")||Slot.StartsWith(TEXT("LockdownControlsProbe_"))?Slot:TEXT("LockdownLocalControlsV1");
    const FString SafeSlot=Tactical?(LegacySlot==TEXT("LockdownLocalControlsV1")?TEXT("LockdownLocalControlsV07"):LegacySlot+TEXT("_V07")):LegacySlot;
    auto* Settings=UGameplayStatics::DoesSaveGameExist(SafeSlot,0)?Cast<ULDControlSettings>(UGameplayStatics::LoadGameFromSlot(SafeSlot,0)):nullptr;
    if(!Settings&&Tactical&&UGameplayStatics::DoesSaveGameExist(LegacySlot,0)){Settings=Cast<ULDControlSettings>(UGameplayStatics::LoadGameFromSlot(LegacySlot,0));}
    if(!Settings){Settings=CastChecked<ULDControlSettings>(UGameplayStatics::CreateSaveGameObject(StaticClass()));Settings->ResetBindings();}
    if(!FMath::IsFinite(Settings->TextScale)||Settings->TextScale<1||Settings->TextScale>1.25){Settings->TextScale=1;}if(!FMath::IsFinite(Settings->Zoom)||Settings->Zoom<1100||Settings->Zoom>1700){Settings->Zoom=1400;}
    if(Tactical){if(!Settings->MigrateTactical()){Settings->Profile=7;Settings->Schema=2;Settings->ResetBindings();}}
    else if(!Settings->Valid()){Settings->Profile=6;Settings->Schema=1;Settings->ResetBindings();}
    Settings->StorageSlot=SafeSlot;return Settings;
}
bool ULDControlSettings::Save() const{return UGameplayStatics::SaveGameToSlot(const_cast<ULDControlSettings*>(this),StorageSlot,0);}
