#include "UI/LDControlSettings.h"
#include "Core/LDLog.h"
#include "UI/LDTerms.h"
#include "Containers/StringConv.h"
#include "Internationalization/Regex.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
    constexpr uint32 LocalSettingsMagic = 0x4C444353;
    constexpr uint32 LegacySaveGameMagic = 0x53415647;
    // Schema 4 stored a selector for the retired multi-registry layout. It is parsed only to reject it explicitly.
    constexpr uint32 RetiredRegistrySchema = 4;
    // Schema 5 is the whitelist without PlayerName, MasterVolume, CameraPanCmPerSec and MinimapOrientation; schema 6 adds
    // PlayerName, schema 7 MasterVolume, schema 8 CameraPanCmPerSec, schema 9 MinimapOrientation and schema 10 QuickCast.
    // Missing preferences take their defaults; loading never rewrites an old file.
    constexpr uint32 NamelessSchema = 5;
    constexpr uint32 VolumelessSchema = 6;
    constexpr uint32 PanlessSchema = 7;
    constexpr uint32 OrientationlessSchema = 8;
    constexpr uint32 QuickCastlessSchema = 9;
    // Schema 10 has every current preference but predates the innate D dash binding.
    constexpr uint32 DashlessSchema = 10;
    constexpr uint32 PreviousZoomSchema = 11;
    // Schema 12 has the schema 11 layout; only its zoom range (1100-1900 cm) predates the current camera range.
    constexpr uint32 NearZoomSchema = 12;
    // Schema 13 has the current layout and zoom range; only its key list predates the hold-Tab scoreboard.
    constexpr uint32 ScoreboardlessSchema = 13;
    constexpr uint32 DiagnosticslessSchema = 14;
    // Schema 15 has the current layout; only its key list predates the playtest mark key.
    constexpr uint32 MarklessSchema = 15;
    // Schema 16 has the current layout; its key list still holds the retired equipment-active keys Item1 and Item2.
    constexpr uint32 ItemKeysSchema = 16;
    // Schema 17 predates auto attack after cast; schema 18 stores that preference but predates the E binding.
    constexpr uint32 AutoAttacklessSchema = 17;
    constexpr uint32 SkillElessSchema = 18;
    // Schema 19 has the current key list but predates the music volume (#94).
    constexpr uint32 MusiclessSchema = 19;
    constexpr uint32 DetailslessSchema = 20;
    constexpr uint32 CombatFeelLessSchema = 21;
    constexpr uint32 CombatFeelSchema = 22;
    constexpr float LegacyMinZoom = 1100.f;
    constexpr float LegacyMaxZoom = 1700.f;
    constexpr float NearZoomMax = 1900.f;
    const FName DashAction(TEXT("Dash"));
    const FName ScoreboardAction(TEXT("Scoreboard"));
    const FName MarkAction(TEXT("Mark"));
    const FName SkillEAction(TEXT("SkillE"));
    const FName DetailsAction(TEXT("Details"));
    const FName RetiredItemActions[] = {FName(TEXT("Item1")), FName(TEXT("Item2"))};
    constexpr int64 MaxSettingsBytes = 65536;
    constexpr int32 MaxBindingCount = 64;

    bool IsSupportedOrientation(ELDMinimapOrientation Value)
    { return Value == ELDMinimapOrientation::Camera || Value == ELDMinimapOrientation::Facing || Value == ELDMinimapOrientation::North; }

    bool IsRegistered(FName Action)
    { return ULDControlSettings::Definitions().ContainsByPredicate([&](const FLDControlBinding& B) { return B.Action == Action; }); }

    TArray<FString> UnregisteredActions(const TMap<FName, FKey>& Bindings)
    {
        TArray<FString> Names;
        for (const auto& Pair : Bindings) { if (!IsRegistered(Pair.Key)) { Names.Add(Pair.Key.ToString()); } }
        Names.Sort();
        return Names;
    }

    TArray<FString> MissingActions(const TMap<FName, FKey>& Bindings)
    {
        TArray<FString> Names;
        for (const auto& B : ULDControlSettings::Definitions()) { if (!Bindings.Contains(B.Action)) { Names.Add(B.Action.ToString()); } }
        return Names;
    }

    FString DescribeActions(const TArray<FString>& Names)
    {
        constexpr int32 Shown = 12;
        FString Text;
        for (int32 Index = 0; Index < Names.Num() && Index < Shown; ++Index)
        {
            if (Index) { Text += TEXT(", "); }
            Text += Names[Index];
        }
        if (Names.Num() > Shown) { Text += FString::Printf(TEXT(" 等另外 %d 个"), Names.Num() - Shown); }
        return Text;
    }

    enum class ELocalFileLookup : uint8 { Present, Missing, Failed };

#if PLATFORM_WINDOWS
    FString WindowsSettingsPath(const FString& Filename, bool ForWrite)
    {
        FString Path = ForWrite
            ? IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*Filename)
            : IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*Filename);
        Path.ReplaceInline(TEXT("/"), TEXT("\\"));
        return Path;
    }
#endif

    ELocalFileLookup LookupSettingsFile(const FLDLocalSettingsStorage& Storage, FString& Error)
    {
#if PLATFORM_WINDOWS
        const DWORD Attributes = ::GetFileAttributesW(*WindowsSettingsPath(Storage.Filename(), false));
        const DWORD NativeError = Attributes == INVALID_FILE_ATTRIBUTES ? ::GetLastError() : ERROR_SUCCESS;
        if (Attributes == INVALID_FILE_ATTRIBUTES)
        {
            if (NativeError == ERROR_FILE_NOT_FOUND || NativeError == ERROR_PATH_NOT_FOUND)
            { return ELocalFileLookup::Missing; }
            Error = FString::Printf(TEXT("无法读取本地设置文件信息（Windows 错误 %u）。文件未被替换。"), uint32(NativeError));
            return ELocalFileLookup::Failed;
        }
        if (Attributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            Error = TEXT("无法读取本地设置文件：该路径是一个文件夹。");
            return ELocalFileLookup::Failed;
        }
        return ELocalFileLookup::Present;
#else
        Error = TEXT("本地设置存储需要受支持的 Windows 文件实现。");
        return ELocalFileLookup::Failed;
#endif
    }

    bool AtomicReplaceSettingsFile(const FString& Filename, const FString& Temporary, FString& Error)
    {
#if PLATFORM_WINDOWS
        const FString Target = WindowsSettingsPath(Filename, true), Source = WindowsSettingsPath(Temporary, true);
        HANDLE File = ::CreateFileW(*Source, GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (File == INVALID_HANDLE_VALUE)
        {
            Error = FString::Printf(TEXT("偏好已应用，但无法打开替换文件进行持久写入（Windows 错误 %u）。原文件已保留。"), uint32(::GetLastError()));
            return false;
        }
        const bool Flushed = ::FlushFileBuffers(File) != 0;
        const DWORD FlushError = Flushed ? ERROR_SUCCESS : ::GetLastError();
        const bool Closed = ::CloseHandle(File) != 0;
        const DWORD CloseError = Closed ? ERROR_SUCCESS : ::GetLastError();
        if (!Flushed || !Closed)
        {
            Error = FString::Printf(TEXT("偏好已应用，但无法将替换文件写入磁盘（Windows 错误 %u）。原文件已保留。"), uint32(Flushed ? CloseError : FlushError));
            return false;
        }
        // A sibling temporary file keeps this a same-volume rename. Never predelete or allow copy fallback.
        if (!::MoveFileExW(*Source, *Target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            Error = FString::Printf(TEXT("偏好已应用，但原子替换失败（Windows 错误 %u）。原文件已保留，请重试保存。"), uint32(::GetLastError()));
            return false;
        }
        return true;
#else
        Error = TEXT("此平台不支持本地设置原子替换。原文件已保留。");
        return false;
#endif
    }

    bool ReadConfigFromDisk(const FString& Filename, FConfigFile& Out)
    {
        FString Contents;
        if (Filename.IsEmpty() || !FFileHelper::LoadFileToString(Contents, *Filename)) { return false; }
        Out.ProcessInputFileContents(Contents, Filename);
        return true;
    }

    bool IntMatches(const FConfigFile& File, const TCHAR* Section, const TCHAR* Key, int32 Expected)
    {
        FString Value;
        return File.GetString(Section, Key, Value) && Value.TrimStartAndEnd() == FString::FromInt(Expected);
    }
}

FLDLocalSettingsStorage FLDLocalSettingsStorage::User()
{
    FLDLocalSettingsStorage Result;
    // The former primary file name is kept so an old saved layout is reported and recovered, not silently ignored.
    Result.Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), TEXT("LockdownLocalControlsV1.sav"));
    return Result;
}

FLDLocalSettingsStorage FLDLocalSettingsStorage::Automation(const FGuid& Fixture)
{
    FLDLocalSettingsStorage Result;
    Result.bAutomation = true;
    if (Fixture.IsValid())
    {
        Result.Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Artifacts"), TEXT("Automation"), TEXT("LocalSettings"),
            Fixture.ToString(EGuidFormats::Digits) + TEXT(".sav"));
    }
    return Result;
}

const TArray<FLDControlBinding>& ULDControlSettings::Definitions()
{
    // Actions and default keys: APP_SPEC section 3.1, CONTROLS_AND_TARGETING sections 2 and 3.2. Labels are player-facing copy only.
    static const TArray<FLDControlBinding> Values={
        {TEXT("Select"),TEXT("选择 / 确认目标"),EKeys::LeftMouseButton},
        {TEXT("Navigate"),TEXT("移动 / 攻击"),EKeys::RightMouseButton},
        {TEXT("AttackMove"),TEXT("攻击移动（再左键确认地面）"),EKeys::A},
        {TEXT("Skill1"),TEXT("角色主动 Q"),EKeys::Q},{TEXT("Skill2"),TEXT("角色主动 W"),EKeys::W},
        {TEXT("SkillE"),TEXT("角色主动 E"),EKeys::E},
        {TEXT("Dash"),FString(LDTerms::Dash)+TEXT("（立即朝指针）"),EKeys::D},
        {TEXT("Stop"),TEXT("停止"),EKeys::S},
        {TEXT("Attributes"),TEXT("属性/加点切换"),EKeys::P},
        {TEXT("Details"),TEXT("按住显示构筑与加点"),EKeys::C},
        {TEXT("Scoreboard"),FString(TEXT("按住显示"))+LDTerms::Scoreboard,EKeys::Tab},
        {TEXT("Mark"),TEXT("标记此刻（写入试玩日志）"),EKeys::F9},
        {TEXT("PanUp"),TEXT("镜头上移"),EKeys::Up},{TEXT("PanDown"),TEXT("镜头下移"),EKeys::Down},
        {TEXT("PanLeft"),TEXT("镜头左移"),EKeys::Left},{TEXT("PanRight"),TEXT("镜头右移"),EKeys::Right},
        {TEXT("Recenter"),TEXT("按住镜头回到自己"),EKeys::SpaceBar},{TEXT("Follow"),TEXT("切换镜头跟随"),EKeys::Home},
        {TEXT("Zoom"),TEXT("镜头缩放"),EKeys::MouseWheelAxis,true},
        {TEXT("Back"),TEXT("返回 / 取消（保留键）"),EKeys::Escape}
    };
    return Values;
}
namespace
{
    FString LocalizedKeyName(const FKey& Key)
    {
        const FName Name=Key.GetFName();
        if(Name==TEXT("LeftMouseButton")){return TEXT("鼠标左键");}
        if(Name==TEXT("RightMouseButton")){return TEXT("鼠标右键");}
        if(Name==TEXT("MiddleMouseButton")){return TEXT("鼠标中键");}
        if(Name==TEXT("ThumbMouseButton")){return TEXT("鼠标侧键 1");}
        if(Name==TEXT("ThumbMouseButton2")){return TEXT("鼠标侧键 2");}
        if(Name==TEXT("MouseWheelAxis")){return TEXT("鼠标滚轮");}
        if(Name==TEXT("SpaceBar")){return TEXT("空格");}
        if(Name==TEXT("Escape")){return TEXT("Esc");}
        if(Name==TEXT("Return")){return TEXT("Enter");}
        if(Name==TEXT("BackSpace")){return TEXT("退格");}
        if(Name==TEXT("LeftShift")){return TEXT("左 Shift");}
        if(Name==TEXT("RightShift")){return TEXT("右 Shift");}
        if(Name==TEXT("LeftControl")){return TEXT("左 Ctrl");}
        if(Name==TEXT("RightControl")){return TEXT("右 Ctrl");}
        if(Name==TEXT("LeftAlt")){return TEXT("左 Alt");}
        if(Name==TEXT("RightAlt")){return TEXT("右 Alt");}
        if(Name==TEXT("Up")){return TEXT("上方向键");}
        if(Name==TEXT("Down")){return TEXT("下方向键");}
        if(Name==TEXT("Left")){return TEXT("左方向键");}
        if(Name==TEXT("Right")){return TEXT("右方向键");}
        return Key.GetDisplayName().ToString();
    }
}
FKey ULDControlSettings::Key(FName Action) const
{
    if (!IsRegistered(Action)) { return FKey(); }
    const FKey* Key = Bindings.Find(Action);
    return Key ? *Key : FKey();
}
FString ULDControlSettings::Label(FName Action) const {return LocalizedKeyName(Key(Action));}
void ULDControlSettings::ResetBindings()
{
    Bindings.Reset();
    for (const auto& B : Definitions()) { Bindings.Add(B.Action, B.Key); }
    if (Phase != ELDLocalSettingsPhase::EditingDraft) { Phase = ELDLocalSettingsPhase::ConfirmedApplied; }
}
bool ULDControlSettings::Rebind(FName Action,FKey NewKey,FString& Error)
{
    const auto* Definition=Definitions().FindByPredicate([&](const FLDControlBinding& B){return B.Action==Action;});
    if(!Definition){Error=TEXT("此动作不在当前按键注册表中。");return false;}
    if(Action==TEXT("Back")||NewKey==EKeys::Escape){Error=TEXT("Esc 保留给返回 / 取消。");return false;}
    // Shift is the fixed shift-queue modifier (ALDPlayerController::IsQueueModifierDown), never an action key.
    if(NewKey==EKeys::LeftShift||NewKey==EKeys::RightShift){Error=TEXT("Shift 保留给指令队列。");return false;}
    if(!NewKey.IsValid()||NewKey.IsGamepadKey()||NewKey.IsAxis2D()||NewKey.IsAxis3D()
        ||(Definition->Axis&&NewKey!=EKeys::MouseWheelAxis)||(!Definition->Axis&&NewKey.IsAxis1D()))
    {Error=Definition->Axis?TEXT("镜头缩放只能使用鼠标滚轮。"):TEXT("请选择键盘按键或鼠标按键。");return false;}
    for(const auto& B:Definitions())
    {
        const FKey Other=Key(B.Action);
        const bool WheelOverlap=(NewKey==EKeys::MouseScrollUp||NewKey==EKeys::MouseScrollDown)&&Other==EKeys::MouseWheelAxis;
        if(B.Action!=Action&&(Other==NewKey||WheelOverlap)){Error=TEXT("该键已被“")+B.Label+TEXT("”使用，请选择其他键。");return false;}
    }
    Bindings.Add(Action,NewKey);
    if (Phase != ELDLocalSettingsPhase::EditingDraft) { Phase = ELDLocalSettingsPhase::ConfirmedApplied; }
    Error.Empty();return true;
}
bool ULDControlSettings::Valid() const
{
    FString Error;
    return ValidateLocalSettings(Error);
}
bool ULDControlSettings::ValidatePreferences(FString& Error) const
{
    if (!FMath::IsFinite(TextScale) || TextScale < 1 || TextScale > 1.25
        || !LDCamera::IsValidArmLength(Zoom))
    { Error = TEXT("镜头缩放或文字大小超出当前支持范围。"); return false; }
    if (!FMath::IsFinite(MasterVolume) || MasterVolume < 0 || MasterVolume > 1)
    { Error = TEXT("主音量超出 0–100% 的支持范围。"); return false; }
    if (!FMath::IsFinite(MusicVolume) || MusicVolume < 0 || MusicVolume > 1)
    { Error = TEXT("音乐音量超出 0–100% 的支持范围。"); return false; }
    if (!FMath::IsFinite(CameraPanCmPerSec) || CameraPanCmPerSec < MinCameraPanCmPerSec || CameraPanCmPerSec > MaxCameraPanCmPerSec)
    {
        Error = FString::Printf(TEXT("镜头平移速度超出 %.0f–%.0f cm/s 的支持范围。"), MinCameraPanCmPerSec, MaxCameraPanCmPerSec);
        return false;
    }
    if (!IsSupportedOrientation(MinimapOrientation))
    { Error = TEXT("小地图朝向不是支持的选项（跟随镜头、跟随角色朝向或固定正北）。"); return false; }
    // An empty name is a valid unset name; only a chosen name must pass the local format check.
    FString NameError;
    if (!PlayerName.IsEmpty() && !ValidatePlayerName(PlayerName, NameError))
    { Error = TEXT("名称无效：") + NameError; return false; }
    Error.Empty();
    return true;
}
bool ULDControlSettings::ValidateLocalSettings(FString& Error) const
{
    if (Schema != CurrentSchema)
    { Error = TEXT("本地设置的格式版本不受支持。"); return false; }
    if (!ValidatePreferences(Error)) { return false; }
    const auto Unknown = UnregisteredActions(Bindings);
    const auto Missing = MissingActions(Bindings);
    if (Unknown.Num() || Missing.Num())
    {
        Error = TEXT("按键绑定与 Demo 操作不一致");
        if (Unknown.Num()) { Error += TEXT("；已退役或未知的动作：") + DescribeActions(Unknown); }
        if (Missing.Num()) { Error += TEXT("；缺少的动作：") + DescribeActions(Missing); }
        Error += TEXT("。请恢复默认按键。");
        return false;
    }
    if (Key(TEXT("Back")) != EKeys::Escape)
    { Error = TEXT("返回 / 取消必须保持为 Esc。请恢复默认按键。"); return false; }
    TSet<FKey> Seen;
    for(const auto& B:Definitions())
    {
        const FKey K=Key(B.Action);
        if(!K.IsValid()||K.IsGamepadKey()||K.IsAxis2D()||K.IsAxis3D()||Seen.Contains(K)
            ||(!B.Axis&&K.IsAxis1D())||(B.Axis&&K!=EKeys::MouseWheelAxis)
            ||K==EKeys::MouseScrollUp||K==EKeys::MouseScrollDown||K==EKeys::LeftShift||K==EKeys::RightShift)
        { Error = TEXT("已保存的按键包含无效键、轴或冲突。请恢复默认按键。"); return false; }
        Seen.Add(K);
    }
    Error.Empty();
    return true;
}

void ULDControlSettings::CopyPreferences(const ULDControlSettings& Other)
{
    Schema = Other.Schema;
    Bindings = Other.Bindings;
    bEdgePan = Other.bEdgePan;
    bStartFollowing = Other.bStartFollowing;
    TextScale = Other.TextScale;
    Zoom = Other.Zoom;
    PlayerName = Other.PlayerName;
    MasterVolume = Other.MasterVolume;
    MusicVolume = Other.MusicVolume;
    CameraPanCmPerSec = Other.CameraPanCmPerSec;
    MinimapOrientation = Other.MinimapOrientation;
    bQuickCast = Other.bQuickCast;
    bAutoAttackAfterCast = Other.bAutoAttackAfterCast;
    bShowNetworkDiagnostics = Other.bShowNetworkDiagnostics;
    bShowFPS = Other.bShowFPS;
    bReduceCombatEffects = Other.bReduceCombatEffects;
    bCameraShake = Other.bCameraShake;
}

ULDControlSettings* ULDControlSettings::CreateSessionSettings(const FLDLocalSettingsStorage& InStorage)
{
    auto* Settings = NewObject<ULDControlSettings>();
    Settings->Storage = InStorage;
    Settings->ResetBindings();
    Settings->Phase = ELDLocalSettingsPhase::ConfirmedApplied;
    return Settings;
}

ULDControlSettings* ULDControlSettings::CreateDraft() const
{
    auto* Draft = NewObject<ULDControlSettings>();
    Draft->CopyPreferences(*this);
    return Draft;
}

bool ULDControlSettings::ApplyLocalSettings(const ULDControlSettings& Draft, FString& Error)
{
    if (Phase == ELDLocalSettingsPhase::EditingDraft)
    { Error = TEXT("只能应用到当前生效的本地设置，不能应用到草稿。"); return false; }
    if (!Draft.ValidateLocalSettings(Error)) { return false; }
    CopyPreferences(Draft);
    Phase = ELDLocalSettingsPhase::ConfirmedApplied;
    return true;
}

bool ULDControlSettings::SetAppliedZoom(float Value, FString& Error)
{
    if (Phase == ELDLocalSettingsPhase::EditingDraft || !LDCamera::IsValidArmLength(Value))
    { Error = TEXT("镜头缩放未保存：当前应用的镜头值无效。"); return false; }
    Zoom = Value;
    Phase = ELDLocalSettingsPhase::ConfirmedApplied;
    Error.Empty();
    return true;
}

bool ULDControlSettings::SetAppliedMasterVolume(float Value, FString& Error)
{
    if (Phase == ELDLocalSettingsPhase::EditingDraft)
    { Error = TEXT("主音量未应用：只能修改当前生效的本地设置，不能修改草稿。"); return false; }
    if (!FMath::IsFinite(Value) || Value < 0 || Value > 1)
    { Error = TEXT("主音量未应用：音量必须在 0–100% 之间。"); return false; }
    MasterVolume = Value;
    Phase = ELDLocalSettingsPhase::ConfirmedApplied;
    Error.Empty();
    return true;
}

bool ULDControlSettings::SetAppliedMusicVolume(float Value, FString& Error)
{
    if (Phase == ELDLocalSettingsPhase::EditingDraft)
    { Error = TEXT("音乐音量未应用：只能修改当前生效的本地设置，不能修改草稿。"); return false; }
    if (!FMath::IsFinite(Value) || Value < 0 || Value > 1)
    { Error = TEXT("音乐音量未应用：音量必须在 0–100% 之间。"); return false; }
    MusicVolume = Value;
    Phase = ELDLocalSettingsPhase::ConfirmedApplied;
    Error.Empty();
    return true;
}

bool ULDControlSettings::SetAppliedCameraPanSpeed(float Value, FString& Error)
{
    if (Phase == ELDLocalSettingsPhase::EditingDraft)
    { Error = TEXT("镜头平移速度未应用：只能修改当前生效的本地设置，不能修改草稿。"); return false; }
    if (!FMath::IsFinite(Value) || Value < MinCameraPanCmPerSec || Value > MaxCameraPanCmPerSec)
    {
        Error = FString::Printf(TEXT("镜头平移速度未应用：速度必须在 %.0f–%.0f cm/s 之间。"), MinCameraPanCmPerSec, MaxCameraPanCmPerSec);
        return false;
    }
    CameraPanCmPerSec = Value;
    Phase = ELDLocalSettingsPhase::ConfirmedApplied;
    Error.Empty();
    return true;
}

bool ULDControlSettings::SetAppliedMinimapOrientation(ELDMinimapOrientation Value, FString& Error)
{
    if (Phase == ELDLocalSettingsPhase::EditingDraft)
    { Error = TEXT("小地图朝向未应用：只能修改当前生效的本地设置，不能修改草稿。"); return false; }
    if (!IsSupportedOrientation(Value))
    { Error = TEXT("小地图朝向未应用：只能选择跟随镜头、跟随角色朝向或固定正北。"); return false; }
    MinimapOrientation = Value;
    Phase = ELDLocalSettingsPhase::ConfirmedApplied;
    Error.Empty();
    return true;
}

FString ULDControlSettings::MinimapOrientationLabel(ELDMinimapOrientation Value)
{
    switch (Value)
    {
    case ELDMinimapOrientation::Camera: return TEXT("跟随镜头");
    case ELDMinimapOrientation::Facing: return TEXT("跟随角色朝向");
    case ELDMinimapOrientation::North: return TEXT("固定正北");
    }
    return FString();
}

void ULDControlSettings::SetAppliedFollowing(bool Value)
{
    bStartFollowing = Value;
    if (Phase != ELDLocalSettingsPhase::EditingDraft) { Phase = ELDLocalSettingsPhase::ConfirmedApplied; }
}

bool ULDControlSettings::ValidatePlayerName(const FString& Name, FString& Error)
{
    if (Name.IsEmpty()) { Error = TEXT("名称不能为空。"); return false; }
    if (Name.TrimStartAndEnd().Len() != Name.Len()) { Error = TEXT("名称开头和结尾不能有空格。"); return false; }
    // The name is a ?LDName= URL option, so URL/option delimiters and quoting characters are never accepted.
    static const FString Reserved = TEXT("?=#/\\:%&\"'<>|");
    int32 Characters = 0;
    for (int32 Index = 0; Index < Name.Len(); ++Index)
    {
        const uint32 Char = uint32(Name[Index]);
        ++Characters;
        // A complete UTF-16 surrogate pair is one character; a lone surrogate is invalid text.
        if (StringConv::IsHighSurrogate(Char) && Index + 1 < Name.Len() && StringConv::IsLowSurrogate(uint32(Name[Index + 1])))
        { ++Index; continue; }
        if (Char < 0x20 || (Char >= 0x7F && Char <= 0x9F) || FChar::IsControl(TCHAR(Char))
            || StringConv::IsHighSurrogate(Char) || StringConv::IsLowSurrogate(Char) || !StringConv::IsValidCodepoint(Char))
        { Error = TEXT("名称不能包含控制字符或无效字符。"); return false; }
        int32 Found = INDEX_NONE;
        if (Reserved.FindChar(TCHAR(Char), Found))
        { Error = TEXT("名称不能包含 ? = # / \\ : % & \" ' < > | 这些字符。"); return false; }
    }
    if (Characters > MaxPlayerNameChars)
    { Error = FString::Printf(TEXT("名称最多 %d 个字符。"), MaxPlayerNameChars); return false; }
    Error.Empty();
    return true;
}

bool ULDControlSettings::SetAppliedPlayerName(const FString& Name, FString& Error)
{
    if (Phase == ELDLocalSettingsPhase::EditingDraft)
    { Error = TEXT("名称未保存：只能修改当前生效的本地设置，不能修改草稿。"); return false; }
    if (!ValidatePlayerName(Name, Error)) { return false; }
    PlayerName = Name;
    Phase = ELDLocalSettingsPhase::ConfirmedApplied;
    return true;
}

void ULDControlSettings::ResetLocalDefaults()
{
    Schema = CurrentSchema;
    for (const ELDPreferenceGroup Group : { ELDPreferenceGroup::Audio, ELDPreferenceGroup::Controls,
        ELDPreferenceGroup::Camera, ELDPreferenceGroup::Interface })
    { ResetPreferenceGroup(Group); }
}

void ULDControlSettings::ResetPreferenceGroup(ELDPreferenceGroup Group)
{
    const auto* CurrentDefaults = GetDefault<ULDControlSettings>();
    switch (Group)
    {
    case ELDPreferenceGroup::Audio:
        MasterVolume = CurrentDefaults->MasterVolume;
        MusicVolume = CurrentDefaults->MusicVolume;
        break;
    case ELDPreferenceGroup::Controls:
        bQuickCast = CurrentDefaults->bQuickCast;
        bAutoAttackAfterCast = CurrentDefaults->bAutoAttackAfterCast;
        ResetBindings();
        break;
    case ELDPreferenceGroup::Camera:
        Zoom = CurrentDefaults->Zoom;
        bEdgePan = CurrentDefaults->bEdgePan;
        bStartFollowing = CurrentDefaults->bStartFollowing;
        CameraPanCmPerSec = CurrentDefaults->CameraPanCmPerSec;
        break;
    case ELDPreferenceGroup::Interface:
        TextScale = CurrentDefaults->TextScale;
        MinimapOrientation = CurrentDefaults->MinimapOrientation;
        bShowNetworkDiagnostics = CurrentDefaults->bShowNetworkDiagnostics;
        bShowFPS = CurrentDefaults->bShowFPS;
        bReduceCombatEffects = CurrentDefaults->bReduceCombatEffects;
        bCameraShake = CurrentDefaults->bCameraShake;
        break;
    }
}

FLDLocalSettingsLoadResult ULDControlSettings::LoadLocalSettings(const FLDLocalSettingsStorage& InStorage)
{
    if (!InStorage.IsValid())
    { return { ELDLocalSettingsLoadStatus::InvalidStorage, nullptr, nullptr, TEXT("本地设置存储无效，未选择任何用户文件。") }; }

    auto& Files = IFileManager::Get();
    const FString& Filename = InStorage.Filename();
    FString LookupError;
    const ELocalFileLookup Lookup = LookupSettingsFile(InStorage, LookupError);
    if (Lookup == ELocalFileLookup::Failed)
    { return { ELDLocalSettingsLoadStatus::ReadFailed, nullptr, nullptr, LookupError }; }
    if (Lookup == ELocalFileLookup::Missing)
    { return { ELDLocalSettingsLoadStatus::Missing, CreateSessionSettings(InStorage), nullptr, TEXT("没有已保存的本地偏好。当前使用默认值，尚未保存。") }; }
    TUniquePtr<FArchive> FileReader(Files.CreateFileReader(*Filename, FILEREAD_Silent));
    if (!FileReader)
    { return { ELDLocalSettingsLoadStatus::ReadFailed, nullptr, nullptr, TEXT("无法读取本地设置文件，请检查文件访问权限后重试。") }; }
    const int64 Size = FileReader->TotalSize();
    if (Size < 0)
    { return { ELDLocalSettingsLoadStatus::ReadFailed, nullptr, nullptr, TEXT("无法读取本地设置文件大小。文件未被替换。") }; }
    if (Size < 12 || Size > MaxSettingsBytes)
    { return { ELDLocalSettingsLoadStatus::Corrupt, nullptr, nullptr, TEXT("本地设置文件大小无效。文件未被替换。") }; }
    TArray<uint8> Bytes;
    Bytes.SetNumUninitialized(int32(Size));
    FileReader->Serialize(Bytes.GetData(), Size);
    const bool ReadError = FileReader->IsError();
    if (!FileReader->Close() || ReadError)
    { return { ELDLocalSettingsLoadStatus::ReadFailed, nullptr, nullptr, TEXT("无法读取本地设置文件。文件未被替换。") }; }

    FMemoryReader Reader(Bytes, true);
    Reader.ArMaxSerializeSize = MaxSettingsBytes;
    uint32 Magic = 0, SavedSchema = 0, Checksum = 0;
    Reader << Magic << SavedSchema << Checksum;
    const bool RetiredRegistry = Magic == LocalSettingsMagic && SavedSchema == RetiredRegistrySchema;
    const bool Migrating = Magic == LocalSettingsMagic
        && (SavedSchema == NamelessSchema || SavedSchema == VolumelessSchema || SavedSchema == PanlessSchema
            || SavedSchema == OrientationlessSchema || SavedSchema == QuickCastlessSchema || SavedSchema == DashlessSchema
            || SavedSchema == PreviousZoomSchema || SavedSchema == NearZoomSchema || SavedSchema == ScoreboardlessSchema
            || SavedSchema == DiagnosticslessSchema || SavedSchema == MarklessSchema || SavedSchema == ItemKeysSchema
            || SavedSchema == AutoAttacklessSchema || SavedSchema == SkillElessSchema || SavedSchema == MusiclessSchema
            || SavedSchema == DetailslessSchema || SavedSchema == CombatFeelLessSchema);
    if (Magic == LegacySaveGameMagic || (Magic == LocalSettingsMagic && SavedSchema != CurrentSchema && !RetiredRegistry && !Migrating))
    { return { ELDLocalSettingsLoadStatus::UnsupportedSchema, nullptr, nullptr, TEXT("不支持此本地设置格式。请在设置中使用恢复操作明确替换。") }; }
    if (Magic != LocalSettingsMagic || Checksum != FCrc::MemCrc32(Bytes.GetData() + 12, Bytes.Num() - 12))
    { return { ELDLocalSettingsLoadStatus::Corrupt, nullptr, nullptr, TEXT("本地设置文件已损坏。文件未被替换。") }; }

    auto* Settings = NewObject<ULDControlSettings>();
    Settings->Storage = InStorage;
    uint8 RetiredSelector = 0, Edge = 0, Follow = 0, Orientation = uint8(Settings->MinimapOrientation), Quick = Settings->bQuickCast ? 1 : 0;
    uint8 Network = 0, FPS = 0, AutoAttack = Settings->bAutoAttackAfterCast ? 1 : 0;
    uint8 ReduceEffects = Settings->bReduceCombatEffects ? 1 : 0, CameraShake = Settings->bCameraShake ? 1 : 0;
    int32 Count = 0;
    FString SavedName;
    if (RetiredRegistry) { Reader << RetiredSelector; }
    Reader << Edge << Follow << Settings->TextScale << Settings->Zoom;
    // Schema 6 added the player name, schema 7 the master volume, schema 8 the camera pan speed and schema 9 the minimap
    // orientation, each between the preferences and the binding list; an older layout leaves every later field at its default.
    // Supported layouts 5..current are contiguous; the retired schema 4 has none of these fields.
    if (SavedSchema >= VolumelessSchema) { Reader << SavedName; }
    if (SavedSchema >= PanlessSchema) { Reader << Settings->MasterVolume; }
    if (SavedSchema >= OrientationlessSchema) { Reader << Settings->CameraPanCmPerSec; }
    if (SavedSchema >= QuickCastlessSchema) { Reader << Orientation; }
    if (SavedSchema >= DashlessSchema) { Reader << Quick; }
    if (SavedSchema > DiagnosticslessSchema) { Reader << Network << FPS; }
    if (SavedSchema > AutoAttacklessSchema) { Reader << AutoAttack; }
    if (SavedSchema > MusiclessSchema) { Reader << Settings->MusicVolume; }
    if (SavedSchema >= CombatFeelSchema) { Reader << ReduceEffects << CameraShake; }
    Reader << Count;
    if (Reader.IsError() || Edge > 1 || Follow > 1 || Quick > 1 || Network > 1 || FPS > 1 || AutoAttack > 1
        || ReduceEffects > 1 || CameraShake > 1 || Count < 0 || Count > MaxBindingCount)
    { return { ELDLocalSettingsLoadStatus::Corrupt, nullptr, nullptr, TEXT("本地设置内容格式错误。文件未被替换。") }; }
    if (SavedName.Len() != FCString::Strlen(*SavedName))
    { return { ELDLocalSettingsLoadStatus::Corrupt, nullptr, nullptr, TEXT("已保存的名称格式错误。文件未被替换。") }; }
    Settings->bEdgePan = Edge != 0;
    Settings->bStartFollowing = Follow != 0;
    Settings->bQuickCast = Quick != 0;
    Settings->bAutoAttackAfterCast = AutoAttack != 0;
    Settings->bShowNetworkDiagnostics = Network != 0;
    Settings->bShowFPS = FPS != 0;
    Settings->bReduceCombatEffects = ReduceEffects != 0;
    Settings->bCameraShake = CameraShake != 0;
    Settings->PlayerName = SavedName;
    // Kept as stored: an unsupported value is rejected by the preference validation below, like an out-of-range zoom.
    Settings->MinimapOrientation = static_cast<ELDMinimapOrientation>(Orientation);
    for (int32 Index = 0; Index < Count && !Reader.IsError(); ++Index)
    {
        FString Action, Key;
        Reader << Action << Key;
        if (Reader.IsError() || Action.IsEmpty() || Action.Len() > 64 || Key.IsEmpty() || Key.Len() > 64
            || Action.Len() != FCString::Strlen(*Action) || Key.Len() != FCString::Strlen(*Key)
            || Settings->Bindings.Contains(FName(*Action)))
        { return { ELDLocalSettingsLoadStatus::Corrupt, nullptr, nullptr, TEXT("已保存的按键列表格式错误。文件未被替换。") }; }
        Settings->Bindings.Add(FName(*Action), FKey(FName(*Key)));
    }
    if (Reader.IsError() || !Reader.AtEnd())
    { return { ELDLocalSettingsLoadStatus::Corrupt, nullptr, nullptr, TEXT("本地设置内容不完整或包含多余数据。文件未被替换。") }; }
    FString Error;
    const float LegacyZoomMax = SavedSchema == NearZoomSchema ? NearZoomMax : LegacyMaxZoom;
    // Schema 13 already has the current camera range.
    const bool LegacyZoomValid = SavedSchema >= ScoreboardlessSchema ? LDCamera::IsValidArmLength(Settings->Zoom)
        : FMath::IsFinite(Settings->Zoom) && Settings->Zoom >= LegacyMinZoom && Settings->Zoom <= LegacyZoomMax;
    // An old zoom valid in its own range is kept when the current range holds it; a value below the current minimum
    // is raised to it and reported, never rejected for a range the old build did not have.
    const float StoredZoom = Settings->Zoom;
    if ((RetiredRegistry || Migrating) && LegacyZoomValid)
    { Settings->Zoom = FMath::Clamp(Settings->Zoom, LDCamera::MinArmLength, LDCamera::MaxArmLength); }
    const bool ZoomRaised = Settings->Zoom != StoredZoom;
    if (RetiredRegistry)
    {
        // Explicit incompatibility: no saved binding is loaded or migrated, including names the Demo still uses.
        const FString Retired = DescribeActions(UnregisteredActions(Settings->Bindings));
        Settings->ResetBindings();
        const bool PreferencesValid = LegacyZoomValid && Settings->ValidatePreferences(Error);
        FString Message = TEXT("已保存的按键来自旧版操作布局");
        if (!Retired.IsEmpty()) { Message += TEXT("（已退役动作：") + Retired + TEXT("）"); }
        Message += TEXT("。没有加载或转换任何旧按键；当前使用 Demo 默认按键，旧文件保持不变。");
        Message += PreferencesValid ? TEXT("有效的镜头与文字偏好已保留。") : TEXT("其镜头或文字偏好也无效，未保留。");
        return { ELDLocalSettingsLoadStatus::UnsupportedSchema, nullptr, PreferencesValid ? Settings : nullptr, Message };
    }
    if (Migrating && !LegacyZoomValid)
    { return { ELDLocalSettingsLoadStatus::Corrupt, nullptr, nullptr, FString::Printf(TEXT("旧版本镜头缩放超出原有 %.0f–%.0f cm 范围，无法迁移。文件未被替换。"), LegacyMinZoom, LegacyZoomMax) }; }
    FString MigrationNotes;
    // Retired bindings no longer reserve keys when defaults for new actions are considered.
    if (Migrating && SavedSchema <= ItemKeysSchema)
    {
        int32 Dropped = 0;
        for (const FName& Retired : RetiredItemActions) { Dropped += Settings->Bindings.Remove(Retired); }
        if (Dropped > 0) { MigrationNotes += TEXT("装备不再有主动，已移除装备主动 1/2 的按键。"); }
    }
    // Missing actions use an unclaimed default; hold-details can also use an unclaimed fallback.
    struct FAddedAction { FName Action; uint32 LastWithout; const TCHAR* Name; };
    for (const FAddedAction& Added : { FAddedAction{DashAction, DashlessSchema, LDTerms::Dash},
        FAddedAction{ScoreboardAction, ScoreboardlessSchema, LDTerms::Scoreboard},
        FAddedAction{MarkAction, MarklessSchema, TEXT("标记")},
        FAddedAction{SkillEAction, SkillElessSchema, TEXT("角色主动 E")},
        FAddedAction{DetailsAction, DetailslessSchema, TEXT("构筑详情")} })
    {
        if (!Migrating || SavedSchema > Added.LastWithout || Settings->Bindings.Contains(Added.Action)) { continue; }
        const FLDControlBinding* Default = Definitions().FindByPredicate([&Added](const FLDControlBinding& B) { return B.Action == Added.Action; });
        FKey NewKey = Default ? Default->Key : FKey();
        if (Added.Action == DetailsAction && Settings->Bindings.FindKey(NewKey))
        {
            for (const FKey& Candidate : {EKeys::F10, EKeys::F11, EKeys::F12, EKeys::F8, EKeys::F7, EKeys::F6,
                EKeys::F5, EKeys::F4, EKeys::F3, EKeys::F2, EKeys::F1, EKeys::B, EKeys::F, EKeys::G, EKeys::H,
                EKeys::I, EKeys::J, EKeys::K, EKeys::L, EKeys::M, EKeys::N, EKeys::O, EKeys::R, EKeys::T,
                EKeys::U, EKeys::V, EKeys::X, EKeys::Y, EKeys::Z})
            {
                if (!Settings->Bindings.FindKey(Candidate)) { NewKey = Candidate; break; }
            }
        }
        if (Default && !Settings->Bindings.FindKey(NewKey))
        {
            Settings->Bindings.Add(Added.Action, NewKey);
            MigrationNotes += FString(TEXT("新增的“")) + Added.Name
                + (NewKey == Default->Key ? TEXT("”键使用默认值 ") : TEXT("”键使用 ")) + LocalizedKeyName(NewKey) + TEXT("。");
        }
    }
    const FString Origin = Migrating ? TEXT("旧版本设置无法迁移：") : TEXT("");
    if (!Settings->ValidatePreferences(Error))
    { return { ELDLocalSettingsLoadStatus::Corrupt, nullptr, nullptr, Origin + Error + TEXT("文件未被替换。") }; }
    if (!Settings->ValidateLocalSettings(Error))
    {
        // Recovery data carries only validated preferences; rejected binding names never reach a runtime object.
        Settings->ResetBindings();
        return { ELDLocalSettingsLoadStatus::Corrupt, nullptr, Settings, Origin + Error + TEXT("其他有效的本地偏好可以保留。") };
    }
    if (Migrating)
    {
        // Explicit schema 5–21 migration: the validated values and the defaults of the missing preferences apply now and
        // stay dirty until a verified current-schema save; loading never rewrites the old file.
        Settings->Phase = ELDLocalSettingsPhase::ConfirmedApplied;
        if (SavedSchema <= AutoAttacklessSchema) { MigrationNotes += TEXT("施法后自动攻击默认开启。"); }
        if (SavedSchema <= MusiclessSchema)
        { MigrationNotes += FString::Printf(TEXT("音乐音量使用默认值 %d%%。"), FMath::RoundToInt(100 * Settings->MusicVolume)); }
        MigrationNotes += TEXT("降低战斗特效关闭、镜头震动开启。");
        if (SavedSchema == AutoAttacklessSchema || SavedSchema == SkillElessSchema || SavedSchema == MusiclessSchema
            || SavedSchema == DetailslessSchema || SavedSchema == CombatFeelLessSchema)
        {
            const FString Preserved = SavedSchema != AutoAttacklessSchema
                ? TEXT("已从旧版本设置迁移：名称、按键、镜头、文字、主音量、快速施法、诊断与施法后自动攻击偏好已保留，下次保存时写入新格式。")
                : TEXT("已从旧版本设置迁移：名称、按键、镜头、文字、主音量、快速施法与诊断偏好已保留，下次保存时写入新格式。");
            return { ELDLocalSettingsLoadStatus::Migrated, Settings, nullptr,
                Preserved + MigrationNotes };
        }
        const FString OrientationDefault = TEXT("小地图朝向使用默认值“") + MinimapOrientationLabel(Settings->MinimapOrientation) + TEXT("”，快速施法默认关闭");
        const FString PanDefault = FString::Printf(TEXT("镜头平移速度使用默认值 %.0f cm/s，"), Settings->CameraPanCmPerSec) + OrientationDefault;
        const FString Message = SavedSchema == NamelessSchema
            ? TEXT("已从旧版本设置迁移：按键、镜头与文字偏好已保留，下次保存时写入新格式。主音量使用默认值 100%，") + PanDefault
                + TEXT("。请在开始页设置名称后再建房或加入。")
            : SavedSchema == VolumelessSchema
            ? TEXT("已从旧版本设置迁移：名称、按键、镜头与文字偏好已保留，主音量使用默认值 100%，") + PanDefault + TEXT("，下次保存时写入新格式。")
            : SavedSchema == PanlessSchema
            ? TEXT("已从旧版本设置迁移：名称、按键、镜头、文字与主音量偏好已保留，") + PanDefault + TEXT("，下次保存时写入新格式。")
            : SavedSchema == OrientationlessSchema
            ? TEXT("已从旧版本设置迁移：名称、按键、镜头（含平移速度）、文字与主音量偏好已保留，") + OrientationDefault
                + TEXT("，下次保存时写入新格式。")
            : SavedSchema == QuickCastlessSchema
            ? TEXT("已从旧版本设置迁移：名称、按键、镜头（含平移速度与小地图朝向）、文字与主音量偏好已保留，快速施法默认关闭，下次保存时写入新格式。")
            : TEXT("已从旧版本设置迁移：名称、按键、镜头、文字、主音量与快速施法偏好已保留，下次保存时写入新格式。");
        if (SavedSchema == MarklessSchema || SavedSchema == ItemKeysSchema)
        { return { ELDLocalSettingsLoadStatus::Migrated, Settings, nullptr, Message + MigrationNotes }; }
        if (SavedSchema == DiagnosticslessSchema)
        { return { ELDLocalSettingsLoadStatus::Migrated, Settings, nullptr, Message + TEXT("网络诊断和 FPS 显示默认关闭。") + MigrationNotes }; }
        if (SavedSchema == ScoreboardlessSchema)
        { return { ELDLocalSettingsLoadStatus::Migrated, Settings, nullptr, Message + MigrationNotes }; }
        const FString ZoomNote = ZoomRaised
            ? FString::Printf(TEXT("旧镜头距离 %.0f cm 低于当前最小值，已调整为 %.0f cm"), StoredZoom, Settings->Zoom)
            : FString::Printf(TEXT("旧镜头距离 %.0f cm 原样保留"), Settings->Zoom);
        return { ELDLocalSettingsLoadStatus::Migrated, Settings, nullptr, Message + MigrationNotes + ZoomNote
            + FString::Printf(TEXT("；如需 %.0f cm，请在设置中选择“采用推荐镜头”，只修改距离。"), LDCamera::DefaultArmLength) };
    }
    Settings->Phase = ELDLocalSettingsPhase::PersistenceVerified;
    return { ELDLocalSettingsLoadStatus::Loaded, Settings, nullptr, TEXT("本地偏好已加载。") };
}

FLDLocalSettingsSaveResult ULDControlSettings::SaveLocalSettings()
{
    FString Error;
    if (Phase == ELDLocalSettingsPhase::EditingDraft || !Storage.IsValid() || !ValidateLocalSettings(Error))
    { return { ELDLocalSettingsSaveStatus::Invalid, Error.IsEmpty() ? TEXT("只能保存已确认应用、且指定了存储位置的本地偏好。") : Error }; }
    if (bRecoveryRequired)
    { return { ELDLocalSettingsSaveStatus::RecoveryRequired, TEXT("已在本次运行中应用；原设置文件保持不变。请先在设置中确认替换，再保存。") }; }

    // Deliberate field whitelist: UObject/SaveGame serialization would also write unrelated properties.
    // Current schema order: edge, follow, text scale, zoom, player name, master volume, camera pan speed, minimap orientation,
    // quick cast, diagnostics, auto attack after cast, music volume, combat effects reduction, camera shake, bindings.
    FBufferArchive Payload;
    uint8 Edge = bEdgePan ? 1 : 0, Follow = bStartFollowing ? 1 : 0, SavedOrientation = uint8(MinimapOrientation), SavedQuick = bQuickCast ? 1 : 0;
    uint8 Network = bShowNetworkDiagnostics ? 1 : 0, FPS = bShowFPS ? 1 : 0, AutoAttack = bAutoAttackAfterCast ? 1 : 0;
    uint8 ReduceEffects = bReduceCombatEffects ? 1 : 0, CameraShake = bCameraShake ? 1 : 0;
    float SavedTextScale = TextScale, SavedZoom = Zoom, SavedVolume = MasterVolume, SavedPanSpeed = CameraPanCmPerSec, SavedMusic = MusicVolume;
    FString SavedName = PlayerName;
    int32 Count = Definitions().Num();
    Payload << Edge << Follow << SavedTextScale << SavedZoom << SavedName << SavedVolume << SavedPanSpeed << SavedOrientation
        << SavedQuick << Network << FPS << AutoAttack << SavedMusic << ReduceEffects << CameraShake << Count;
    for (const auto& Binding : Definitions())
    {
        FString Action = Binding.Action.ToString(), KeyName = Key(Binding.Action).GetFName().ToString();
        Payload << Action << KeyName;
    }
    FBufferArchive Bytes;
    uint32 Magic = LocalSettingsMagic, SavedSchema = CurrentSchema, Checksum = FCrc::MemCrc32(Payload.GetData(), Payload.Num());
    Bytes << Magic << SavedSchema << Checksum;
    Bytes.Append(Payload);
    if (Bytes.IsError() || Bytes.Num() > MaxSettingsBytes)
    { return { ELDLocalSettingsSaveStatus::Invalid, TEXT("无法序列化本地设置内容。") }; }
    Phase = ELDLocalSettingsPhase::ConfirmedApplied;
    auto& Files = IFileManager::Get();
    const FString& Filename = Storage.Filename();
    if (!Files.MakeDirectory(*FPaths::GetPath(Filename), true))
    { return { ELDLocalSettingsSaveStatus::WriteFailed, TEXT("偏好已应用，但无法创建本地设置文件夹。请重试保存。") }; }
    const FString Temporary = Filename + TEXT(".tmp-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto RemoveTemporary = [&]()
    {
        if (Files.FileExists(*Temporary) && !Files.Delete(*Temporary, false, false, true))
        { UE_LOG(LogLDUI, Warning, TEXT("Could not remove local settings temporary file: %s"), *Temporary); }
    };
    if (!FFileHelper::SaveArrayToFile(Bytes, *Temporary))
    {
        RemoveTemporary();
        return { ELDLocalSettingsSaveStatus::WriteFailed, TEXT("偏好已应用，但写入本地设置文件失败。请重试保存。") };
    }
    TArray<uint8> Readback;
    if (!FFileHelper::LoadFileToArray(Readback, *Temporary) || Readback != Bytes)
    {
        RemoveTemporary();
        return { ELDLocalSettingsSaveStatus::ReadbackFailed, TEXT("偏好已应用，但无法核验新文件。原文件已保留。") };
    }
    if (!AtomicReplaceSettingsFile(Filename, Temporary, Error))
    {
        RemoveTemporary();
        return { ELDLocalSettingsSaveStatus::WriteFailed, Error };
    }
    Readback.Reset();
    if (!FFileHelper::LoadFileToArray(Readback, *Filename) || Readback != Bytes)
    { return { ELDLocalSettingsSaveStatus::ReadbackFailed, TEXT("偏好已应用，但回读已保存的文件失败。持久化未核验，请重试保存。") }; }
    Phase = ELDLocalSettingsPhase::PersistenceVerified;
    return { ELDLocalSettingsSaveStatus::Saved, TEXT("本地偏好已保存，并已从磁盘核验。") };
}

FLDDisplaySettings FLDDisplaySettings::Read(const UGameUserSettings& Settings)
{
    FLDDisplaySettings Result;
    Result.bVSync = Settings.IsVSyncEnabled();
    Result.Quality = Settings.ScalabilityQuality;
    return Result;
}
bool FLDDisplaySettings::ProductDefaults(const UGameUserSettings& Live, FLDDisplaySettings& Out, FString& Error)
{
    auto* VSync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));
    int32 DefaultVSync = 0;
    if (!VSync || !LexTryParseString(DefaultVSync, *VSync->GetDefaultValue()))
    { Error = TEXT("无法读取引擎的垂直同步默认值，显示与画质未恢复默认。"); return false; }
    auto* EngineDefaults = NewObject<UGameUserSettings>(GetTransientPackage(), Live.GetClass(), NAME_None, RF_Transient);
    EngineDefaults->SetToDefaults();
    FLDDisplaySettings Result = Read(*EngineDefaults);
    Result.bVSync = DefaultVSync != 0;
    if (!Result.IsValid())
    { Error = TEXT("无法确定默认画质，显示与画质未恢复默认。"); return false; }
    Out = Result;
    Error.Empty();
    return true;
}
bool FLDDisplaySettings::operator==(const FLDDisplaySettings& Other) const
{ return bVSync == Other.bVSync && Quality == Other.Quality; }
bool FLDDisplaySettings::IsValid() const
{
    if (!FMath::IsFinite(Quality.ResolutionQuality) || Quality.ResolutionQuality < 0 || Quality.ResolutionQuality > 100) { return false; }
    for (int32 Value : { Quality.ViewDistanceQuality, Quality.AntiAliasingQuality, Quality.ShadowQuality,
        Quality.GlobalIlluminationQuality, Quality.ReflectionQuality, Quality.PostProcessQuality, Quality.TextureQuality,
        Quality.EffectsQuality, Quality.FoliageQuality, Quality.ShadingQuality, Quality.LandscapeQuality })
    { if (Value < 0 || Value > 4) { return false; } }
    return true;
}

bool LDLocalSettings::ResolveConfigDestination(FConfigCacheIni& Cache, const FString& ConfigId, FString& Filename, FString& Error)
{
    Filename.Reset();
    Error.Reset();
    if (ConfigId.IsEmpty())
    {
        Error = TEXT("配置标识为空，无法核验实际文件。");
        return false;
    }
    const auto* Branch = Cache.FindBranch(FName(*ConfigId), ConfigId);
    if (!Branch || Branch->IniPath.IsEmpty())
    {
        Error = TEXT("配置目标不可用：") + ConfigId + TEXT("。无法核验实际文件。");
        return false;
    }
    Filename = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*Branch->IniPath);
    if (Filename.IsEmpty())
    {
        Error = TEXT("无法解析配置目标：") + ConfigId + TEXT("。");
        return false;
    }
    return true;
}

FLDLocalSettingsSaveResult LDLocalSettings::FlushAndVerifyDisplaySettingsFiles(
    FConfigCacheIni& Cache, const FString& SettingsFile, const FString& QualityFile,
    const FString& SettingsSection, const FLDDisplaySettings& Expected)
{
    if (SettingsFile.IsEmpty() || QualityFile.IsEmpty() || SettingsSection.IsEmpty() || !Expected.IsValid())
    { return { ELDLocalSettingsSaveStatus::Invalid, TEXT("显示持久化目标或预期值无效，未写入画质文件。") }; }
    FString PhysicalSettings, PhysicalQuality, ResolutionError;
    if (!ResolveConfigDestination(Cache, SettingsFile, PhysicalSettings, ResolutionError)
        || !ResolveConfigDestination(Cache, QualityFile, PhysicalQuality, ResolutionError))
    { return { ELDLocalSettingsSaveStatus::WriteFailed, ResolutionError }; }
    if (SettingsFile != QualityFile)
    {
        const auto* Quality = Cache.FindConfigFile(QualityFile);
        if (!Quality)
        { return { ELDLocalSettingsSaveStatus::WriteFailed, TEXT("显示改动已应用，但独立的画质配置不可用。请重试保存。") }; }
        if (Quality->NoSave)
        { return { ELDLocalSettingsSaveStatus::WriteFailed, TEXT("显示改动已应用，但独立的画质配置禁止写入。持久化未核验。") }; }
        if (!Cache.Flush(false, QualityFile))
        { return { ELDLocalSettingsSaveStatus::WriteFailed, TEXT("显示改动已应用，但写入独立的画质配置失败。请重试保存。") }; }
    }
    return VerifyDisplaySettingsFile(PhysicalSettings, PhysicalQuality, SettingsSection, Expected);
}

void LDLocalSettings::HoldBorderlessDesktop(UGameUserSettings& Settings)
{
    const auto Quality = Settings.ScalabilityQuality;
    Settings.SetFullscreenMode(EWindowMode::WindowedFullscreen);
    Settings.SetScreenResolution(Settings.GetDesktopResolution());
    Settings.ConfirmVideoMode();
    Settings.ScalabilityQuality = Quality;
}

bool LDLocalSettings::ApplyRenderScale(IConsoleVariable& ScreenPercentage, float ResolutionQuality)
{
    if (ResolutionQuality != 0.f) { return true; }
    ScreenPercentage.Unset(ECVF_SetByScalability);
    return (ScreenPercentage.GetFlags() & ECVF_SetByMask) != ECVF_SetByScalability;
}

FLDLocalSettingsSaveResult LDLocalSettings::VerifyDisplaySettingsFile(
    const FString& SettingsFile, const FString& QualityFile, const FString& SettingsSection, const FLDDisplaySettings& Expected)
{
    if (!Expected.IsValid() || SettingsSection.IsEmpty())
    { return { ELDLocalSettingsSaveStatus::Invalid, TEXT("预期的显示状态无效，无法核验显示值。") }; }
    FConfigFile Display, Quality;
    if (!ReadConfigFromDisk(SettingsFile, Display) || !ReadConfigFromDisk(QualityFile, Quality))
    { return { ELDLocalSettingsSaveStatus::ReadbackFailed, TEXT("显示改动已应用，但无法读取实际配置文件。请重试保存。") }; }
    FString VSync;
    const TCHAR* Section = *SettingsSection;
    const bool DisplayMatches = Display.GetString(Section, TEXT("bUseVSync"), VSync)
        && VSync.Equals(Expected.bVSync ? TEXT("True") : TEXT("False"), ESearchCase::IgnoreCase);
    const TCHAR* QualitySection = TEXT("ScalabilityGroups");
    const bool QualityMatches = IntMatches(Quality, QualitySection, TEXT("sg.ViewDistanceQuality"), Expected.Quality.ViewDistanceQuality)
        && IntMatches(Quality, QualitySection, TEXT("sg.AntiAliasingQuality"), Expected.Quality.AntiAliasingQuality)
        && IntMatches(Quality, QualitySection, TEXT("sg.ShadowQuality"), Expected.Quality.ShadowQuality)
        && IntMatches(Quality, QualitySection, TEXT("sg.GlobalIlluminationQuality"), Expected.Quality.GlobalIlluminationQuality)
        && IntMatches(Quality, QualitySection, TEXT("sg.ReflectionQuality"), Expected.Quality.ReflectionQuality)
        && IntMatches(Quality, QualitySection, TEXT("sg.PostProcessQuality"), Expected.Quality.PostProcessQuality)
        && IntMatches(Quality, QualitySection, TEXT("sg.TextureQuality"), Expected.Quality.TextureQuality)
        && IntMatches(Quality, QualitySection, TEXT("sg.EffectsQuality"), Expected.Quality.EffectsQuality)
        && IntMatches(Quality, QualitySection, TEXT("sg.FoliageQuality"), Expected.Quality.FoliageQuality)
        && IntMatches(Quality, QualitySection, TEXT("sg.ShadingQuality"), Expected.Quality.ShadingQuality)
        && IntMatches(Quality, QualitySection, TEXT("sg.LandscapeQuality"), Expected.Quality.LandscapeQuality);
    FString ScaleText;
    bool ScaleMatches = false;
    if (Quality.GetString(QualitySection, TEXT("sg.ResolutionQuality"), ScaleText))
    {
        double Scale = 0;
        const FRegexPattern Number(TEXT("^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?$"));
        FRegexMatcher Matcher(Number, ScaleText);
        ScaleMatches = Matcher.FindNext() && Matcher.GetMatchEnding() == ScaleText.Len()
            && ScaleText.Len() == FCString::Strlen(*ScaleText)
            && LexTryParseString(Scale, *ScaleText) && FMath::IsFinite(Scale)
            && FMath::IsNearlyEqual(Scale, double(Expected.Quality.ResolutionQuality), .0001);
    }
    if (!DisplayMatches || !QualityMatches || !ScaleMatches)
    { return { ELDLocalSettingsSaveStatus::ReadbackFailed, TEXT("显示改动已应用，但磁盘上的值不一致。持久化未核验，请重试保存。") }; }
    return { ELDLocalSettingsSaveStatus::Saved, TEXT("显示设置已保存，并已从磁盘核验。") };
}
