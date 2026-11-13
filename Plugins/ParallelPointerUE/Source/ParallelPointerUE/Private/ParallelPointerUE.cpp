#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Widgets/SWindow.h"
#include "GenericPlatform/GenericWindow.h"
#include "InputCoreTypes.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "LevelEditorViewport.h"
#include "SEditorViewport.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SViewport.h"
#include "Engine/World.h"
#include "UObject/Package.h"
#include "Windows/WindowsHWrapper.h"
#include <sddl.h>

namespace
{
using FJson = TSharedPtr<FJsonObject>;
FString String(const FJson& J, const TCHAR* Key) { FString V; if (J) J->TryGetStringField(Key, V); return V; }
double Number(const FJson& J, const TCHAR* Key, double Default = 0) { double V; return J && J->TryGetNumberField(Key, V) ? V : Default; }
bool Flag(const FJson& J, const TCHAR* Key) { bool V = false; if (J) J->TryGetBoolField(Key, V); return V; }
FJson Read(const FString& Path)
{
    FString Text;
    if (IFileManager::Get().FileSize(*Path) > 65536 || !FFileHelper::LoadFileToString(Text, *Path)) return nullptr;
    FJson J; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J); return J;
}
bool Write(const FString& Path, const FJson& J)
{
    FString Text; FJsonSerializer::Serialize(J.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
    const FString Temp = Path + TEXT(".tmp");
    return FFileHelper::SaveStringToFile(Text, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        && IFileManager::Get().Move(*Path, *Temp, true, false);
}
FString Desktop()
{
    wchar_t Name[512] = {}; DWORD Needed = 0;
    return GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, Name, sizeof(Name), &Needed) ? FString(Name) : FString();
}
FString Account()
{
    HANDLE Token = nullptr; if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token)) return {};
    DWORD Size = 0; GetTokenInformation(Token, TokenUser, nullptr, 0, &Size);
    TArray<uint8> Data; Data.SetNumUninitialized(Size); FString Result;
    if (GetTokenInformation(Token, TokenUser, Data.GetData(), Size, &Size))
    {
        LPWSTR Sid = nullptr;
        if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(Data.GetData())->User.Sid, &Sid)) { Result = Sid; LocalFree(Sid); }
    }
    CloseHandle(Token); return Result;
}
FString Birth()
{
    FILETIME Created, Exit, Kernel, User;
    if (!GetProcessTimes(GetCurrentProcess(), &Created, &Exit, &Kernel, &User)) return {};
    const uint64 Value = (static_cast<uint64>(Created.dwHighDateTime) << 32) | Created.dwLowDateTime;
    return LexToString(Value + 504911232000000000ULL);
}
bool ValidId(const FString& S)
{
    if (S.Len() != 32) return false;
    for (TCHAR C : S) if (!FChar::IsHexDigit(C)) return false;
    return true;
}
FKey Button(const FString& S)
{
    return S == TEXT("left") ? EKeys::LeftMouseButton : S == TEXT("right") ? EKeys::RightMouseButton : S == TEXT("middle") ? EKeys::MiddleMouseButton : EKeys::Invalid;
}
struct FPointer
{
    TSharedPtr<FSlateVirtualUserHandle> User;
    FVector2D Position = FVector2D::ZeroVector;
    TSet<FKey> Pressed;
};
}

class FParallelPointerUEModule final : public IModuleInterface
{
    FString Root, Session, Token, ProcessBirth;
    FTSTicker::FDelegateHandle TickHandle;
    TMap<FString, FPointer> Pointers;
    TMap<FString, FJson> Receipts;
    double LastHeartbeat = 0;
    bool Paused = false;

    FJson Connection() const
    {
        FJson J = MakeShared<FJsonObject>();
        J->SetStringField(TEXT("protocol"), TEXT("parallelpointer-ue/1"));
        J->SetStringField(TEXT("version"), TEXT("2.2.0-alpha"));
        J->SetNumberField(TEXT("pid"), GetCurrentProcessId());
        J->SetStringField(TEXT("process_birth"), ProcessBirth);
        J->SetStringField(TEXT("session"), Session); J->SetStringField(TEXT("token"), Token);
        J->SetStringField(TEXT("account_sid"), Account()); J->SetStringField(TEXT("desktop"), Desktop());
        J->SetStringField(TEXT("project"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
        J->SetNumberField(TEXT("heartbeat"), FDateTime::UtcNow().ToUnixTimestamp());
        J->SetBoolField(TEXT("paused"), Paused); return J;
    }
    FJson Error(const FString& Message) const
    {
        FJson J = MakeShared<FJsonObject>(); J->SetBoolField(TEXT("ok"), false); J->SetStringField(TEXT("message"), Message); return J;
    }
    FJson Execute(const FJson& R)
    {
        if (String(R, TEXT("token")) != Token || String(R, TEXT("session")) != Session || String(R, TEXT("process_birth")) != ProcessBirth)
            return Error(TEXT("Adapter session mismatch."));
        const double Now = FDateTime::UtcNow().ToUnixTimestamp();
        if (Number(R, TEXT("deadline")) < Now || Number(R, TEXT("deadline")) > Now + 30) return Error(TEXT("Request expired or deadline invalid; no input executed."));
        HWND Hwnd = reinterpret_cast<HWND>(FCString::Strtoui64(*String(R, TEXT("hwnd")), nullptr, 0));
        DWORD Pid = 0; GetWindowThreadProcessId(Hwnd, &Pid);
        if (!IsWindow(Hwnd) || Pid != GetCurrentProcessId() || !IsWindowVisible(Hwnd) || IsIconic(Hwnd) || !IsWindowEnabled(Hwnd))
            return Error(TEXT("Target window is unavailable."));
        auto& App = FSlateApplication::Get();
        TArray<TSharedRef<SWindow>> AllWindows; App.GetAllVisibleWindowsOrdered(AllWindows);
        TArray<TSharedRef<SWindow>> Windows;
        for (const auto& W : AllWindows) if (W->GetNativeWindow() && W->GetNativeWindow()->GetOSWindowHandle() == Hwnd) Windows.Add(W);
        if (Windows.IsEmpty()) return Error(TEXT("No Slate window matches this HWND."));
        if (App.GetActiveModalWindow() && !Windows[0]->IsDescendantOf(App.GetActiveModalWindow()) && Windows[0] != App.GetActiveModalWindow())
            return Error(TEXT("A modal dialog blocks this target. Bind its observed window instead."));
        const FString Action = String(R, TEXT("action"));
        FJson Result = MakeShared<FJsonObject>();
        if (Action == TEXT("ue-status"))
        {
            Result->SetBoolField(TEXT("ok"), true); Result->SetBoolField(TEXT("paused"), Paused);
            Result->SetStringField(TEXT("world"), GEditor && GEditor->GetEditorWorldContext().World() ? GEditor->GetEditorWorldContext().World()->GetPathName() : TEXT(""));
            Result->SetStringField(TEXT("project"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
            Result->SetBoolField(TEXT("world_dirty"), GEditor && GEditor->GetEditorWorldContext().World() && GEditor->GetEditorWorldContext().World()->GetOutermost()->IsDirty());
            Result->SetStringField(TEXT("transport"), TEXT("UE-Slate-virtual-user")); return Result;
        }
        if (Paused) return Error(TEXT("Adapter paused after an application focus change. Restart the editor adapter before more input."));
        if (Action == TEXT("ue-save-level"))
        {
            UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
            if (!World || World->GetPathName() != String(R, TEXT("expected_world"))) return Error(TEXT("World changed; save rejected."));
            if (!FPackageName::DoesPackageExist(World->GetOutermost()->GetName())) return Error(TEXT("Save As requires a user-selected filename; this action only saves an existing level."));
            Result->SetBoolField(TEXT("ok"), FEditorFileUtils::SaveLevel(World->PersistentLevel));
            Result->SetBoolField(TEXT("dirty"), World->GetOutermost()->IsDirty());
            Result->SetStringField(TEXT("world"), World->GetPathName()); return Result;
        }
        const FString PointerId = String(R, TEXT("pointer"));
        if (PointerId.IsEmpty() || PointerId.Len() > 80) return Error(TEXT("Invalid pointer identity."));
        if (!Pointers.Contains(PointerId) && Pointers.Num() >= 8) return Error(TEXT("Maximum eight UE virtual users."));
        FPointer& P = Pointers.FindOrAdd(PointerId);
        if (!P.User) P.User = App.FindOrCreateVirtualUser(1000 + Pointers.Num());
        const int32 User = P.User->GetUserIndex();
        const FModifierKeysState Modifiers(Flag(R,TEXT("shift")), false, Flag(R,TEXT("ctrl")), false, Flag(R,TEXT("alt")), false, false, false, false);
        RECT Bounds; GetClientRect(Hwnd, &Bounds);
        auto Point = [&](const TCHAR* X, const TCHAR* Y, FVector2D& Out) -> bool
        {
            const double PX = Number(R, X, -1), PY = Number(R, Y, -1);
            if (!FMath::IsFinite(PX) || !FMath::IsFinite(PY) || PX < 0 || PY < 0 || PX >= Bounds.right || PY >= Bounds.bottom) return false;
            POINT V{static_cast<LONG>(PX), static_cast<LONG>(PY)}; ClientToScreen(Hwnd, &V); Out = FVector2D(V.x,V.y); return true;
        };
        FVector2D Position;
        if (!Point(TEXT("x"), TEXT("y"), Position)) return Error(TEXT("Coordinates outside the target client area."));
        auto Event = [&](FVector2D At, FKey Key = EKeys::Invalid, float Wheel = 0.0f)
        { return FPointerEvent(User, FSlateApplication::CursorPointerIndex, At, P.Position, P.Pressed, Key, Wheel, Modifiers); };
        auto Path = [&](FVector2D At) { return App.LocateWindowUnderMouse(At, Windows, false, User); };
        auto Move = [&](FVector2D At)
        {
            const auto E = Event(At); auto Widgets = Path(At);
            App.RoutePointerMoveEvent(Widgets, E, false); P.Position = At;
        };
        const FKey MouseButton = Button(String(R,TEXT("button")));
        if ((Action == TEXT("click") || Action == TEXT("drag")) && !MouseButton.IsValid()) return Error(TEXT("Invalid mouse button."));
        FVector2D Destination;
        if (Action == TEXT("drag") && !Point(TEXT("to_x"), TEXT("to_y"), Destination)) return Error(TEXT("Drag destination outside the target client area."));
        const int32 Steps = FMath::Clamp(static_cast<int32>(Number(R,TEXT("steps"),12)), 1, 60);
        const FString Text = String(R,TEXT("text"));
        if (Action == TEXT("text") && Text.Len() > 4096) return Error(TEXT("Text exceeds 4096 characters."));
        if (!TArray<FString>{TEXT("move"),TEXT("click"),TEXT("scroll"),TEXT("drag"),TEXT("key"),TEXT("text")}.Contains(Action)) return Error(TEXT("Unsupported adapter action."));
        const double Delta = Number(R,TEXT("delta"));
        if (Action == TEXT("scroll") && (!FMath::IsFinite(Delta) || Delta == 0 || FMath::Abs(Delta) > 1200)) return Error(TEXT("Wheel delta must be nonzero and at most 1200."));
        FKey KeyboardKey(*String(R,TEXT("key")));
        if (Action == TEXT("key") && (!KeyboardKey.IsValid() || KeyboardKey.IsMouseButton() || KeyboardKey.IsGamepadKey())) return Error(TEXT("Expected an Unreal keyboard key name."));
        const HWND Before = GetForegroundWindow();
        bool Handled = false;
        // Editor camera navigation normally polls the hardware cursor/Raw Input.
        // Resolve only the observed level viewport and update its camera directly.
        // This path never delivers a fake hardware press or drags a scene actor.
        if (GEditor && !GEditor->PlayWorld && (Action == TEXT("scroll") || Action == TEXT("drag")))
        {
            const FWidgetPath Widgets = Path(Position);
            for (FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
            {
                auto EditorWidget = Client->GetEditorViewportWidget();
                auto SceneViewport = EditorWidget ? EditorWidget->GetSceneViewport() : nullptr;
                auto ViewWidget = SceneViewport ? SceneViewport->GetViewportWidget().Pin() : nullptr;
                if (!ViewWidget || !Widgets.ContainsWidget(ViewWidget.Get())) continue;
                if (Action == TEXT("drag") && MouseButton == EKeys::LeftMouseButton && !Flag(R,TEXT("alt")))
                    return Error(TEXT("Level viewport left-drag/gizmo editing is not adapted. Use right-drag look, middle-drag pan, or Alt-left-drag orbit."));
                const FVector OldLocation = Client->GetViewLocation();
                const FRotator OldRotation = Client->GetViewRotation();
                FVector Location = OldLocation; FRotator Rotation = OldRotation;
                FString Navigation;
                if (Action == TEXT("scroll"))
                {
                    Navigation=TEXT("dolly");
                    if (Client->IsPerspective()) Location += Rotation.Vector() * (Delta/120.0) * 100.0;
                    else Client->SetOrthoZoom(FMath::Clamp(Client->GetOrthoZoom()*FMath::Pow(0.9,Delta/120.0),1.0,10000000.0));
                }
                else
                {
                    const FVector2D D = Destination-Position;
                    if (MouseButton == EKeys::MiddleMouseButton)
                    {
                        Navigation=TEXT("pan");
                        Location += (FRotationMatrix(Rotation).GetUnitAxis(EAxis::Y)*-D.X + FRotationMatrix(Rotation).GetUnitAxis(EAxis::Z)*D.Y)*2.0;
                    }
                    else
                    {
                        Rotation.Yaw += D.X*0.2; Rotation.Pitch=FMath::Clamp(Rotation.Pitch-D.Y*0.2,-89.0,89.0);
                        Navigation=MouseButton == EKeys::LeftMouseButton ? TEXT("orbit") : TEXT("look");
                        if (Navigation == TEXT("orbit"))
                        {
                            const FVector Pivot=Client->GetLookAtLocation();
                            Location=Pivot-Rotation.Vector()*FMath::Max(1.0,FVector::Distance(OldLocation,Pivot));
                        }
                    }
                }
                Client->SetViewLocation(Location); Client->SetViewRotation(Rotation); Client->Invalidate();
                P.Position=Action == TEXT("drag") ? Destination : Position;
                Result->SetBoolField(TEXT("ok"),true); Result->SetBoolField(TEXT("handled"),true);
                Result->SetBoolField(TEXT("effect_verified"),false); Result->SetBoolField(TEXT("foreground_changed"),Before != GetForegroundWindow());
                Result->SetStringField(TEXT("transport"),TEXT("UE-editor-viewport")); Result->SetStringField(TEXT("navigation"),Navigation);
                Result->SetStringField(TEXT("delivery"),TEXT("editor-thread-returned"));
                Result->SetStringField(TEXT("location_before"),OldLocation.ToString()); Result->SetStringField(TEXT("location_after"),Location.ToString());
                Result->SetStringField(TEXT("rotation_before"),OldRotation.ToString()); Result->SetStringField(TEXT("rotation_after"),Rotation.ToString());
                return Result;
            }
        }
        // Route events using a non-hardware Slate user. ProcessMouseButtonDownEvent would call
        // OS SetCapture, so the explicit-path Route methods are required here.
        Move(Position);
        if (Action == TEXT("click") || Action == TEXT("drag"))
        {
            P.Pressed.Add(MouseButton);
            Handled = App.RoutePointerDownEvent(Path(Position), Event(Position,MouseButton)).IsEventHandled();
            if (Action == TEXT("drag")) for (int32 I=1; I<=Steps; ++I) Move(FMath::Lerp(Position,Destination,static_cast<double>(I)/Steps));
            P.Pressed.Remove(MouseButton);
            Handled |= App.RoutePointerUpEvent(Path(P.Position), Event(P.Position,MouseButton)).IsEventHandled();
            if (auto SlateUser = App.GetUser(User)) SlateUser->ReleaseAllCapture();
        }
        else if (Action == TEXT("scroll"))
        {
            Handled = App.RouteMouseWheelOrGestureEvent(Path(Position),Event(Position,EKeys::Invalid,Delta/120.0)).IsEventHandled();
        }
        else if (Action == TEXT("key"))
        {
            const uint32* KeyCode = nullptr; const uint32* CharCode = nullptr;
            FInputKeyManager::Get().GetCodesFromKey(KeyboardKey,KeyCode,CharCode);
            FKeyEvent KeyEvent(KeyboardKey,Modifiers,User,false,CharCode ? *CharCode : 0,KeyCode ? *KeyCode : 0);
            Handled = App.ProcessKeyDownEvent(KeyEvent); App.ProcessKeyUpEvent(KeyEvent);
        }
        else if (Action == TEXT("text")) for (TCHAR C : Text) Handled |= App.ProcessKeyCharEvent(FCharacterEvent(C,Modifiers,User,false));
        const bool ForegroundChanged = Before != GetForegroundWindow();
        if (ForegroundChanged) Paused = true;
        Result->SetBoolField(TEXT("ok"),true); Result->SetBoolField(TEXT("handled"),Handled);
        Result->SetBoolField(TEXT("effect_verified"),false); Result->SetBoolField(TEXT("foreground_changed"),ForegroundChanged);
        Result->SetNumberField(TEXT("slate_user"),User); Result->SetStringField(TEXT("transport"),TEXT("UE-Slate-virtual-user"));
        Result->SetStringField(TEXT("delivery"),TEXT("editor-thread-returned")); return Result;
    }
    bool Tick(float)
    {
        if (!FSlateApplication::IsInitialized()) return true;
        if (FPlatformTime::Seconds() - LastHeartbeat > 1) { Write(Root/TEXT("connection.json"),Connection()); LastHeartbeat=FPlatformTime::Seconds(); }
        TArray<FString> Files; IFileManager::Get().FindFiles(Files,*(Root/TEXT("requests/*.json")),true,false);
        for (auto It=Receipts.CreateIterator();It;++It) if (!Files.Contains(It.Key()+TEXT(".json"))) It.RemoveCurrent();
        int32 Count=0;
        for (const FString& File : Files)
        {
            if (++Count > 16) break;
            const FString Id=FPaths::GetBaseFilename(File); if (!ValidId(Id)) continue;
            const FString Response=Root/TEXT("responses")/File;
            if (IFileManager::Get().FileExists(*Response)) continue;
            if (const FJson* Cached=Receipts.Find(Id)) { Write(Response,*Cached); continue; }
            if (Receipts.Num() >= 512) continue;
            const FJson R=Read(Root/TEXT("requests")/File);
            // Remember dispatch before running input. A failed receipt write must never repeat it.
            Receipts.Add(Id,Error(TEXT("Execution interrupted; do not automatically repeat.")));
            FJson Result=R && String(R,TEXT("request_id"))==Id ? Execute(R) : Error(TEXT("Invalid request."));
            Result->SetStringField(TEXT("request_id"),Id); Result->SetStringField(TEXT("session"),Session);
            Receipts[Id]=Result;
            Write(Response,Result);
        }
        return true;
    }
public:
    virtual void StartupModule() override
    {
        ProcessBirth=Birth(); Session=FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Token=FGuid::NewGuid().ToString(EGuidFormats::Digits)+FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString Local=FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
        if (Local.IsEmpty() || ProcessBirth.IsEmpty() || Account().IsEmpty() || Desktop().IsEmpty()) return;
        Root=Local/TEXT("ParallelPointerUE")/LexToString(GetCurrentProcessId())/Session;
        IFileManager::Get().MakeDirectory(*(Root/TEXT("requests")),true);
        IFileManager::Get().MakeDirectory(*(Root/TEXT("responses")),true);
        Write(Root/TEXT("connection.json"),Connection());
        FFileHelper::SaveStringToFile(Root,*(FPaths::GetPath(Root)/TEXT("current.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this,&FParallelPointerUEModule::Tick),0.03f);
    }
    virtual void ShutdownModule() override
    {
        if (TickHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
        Pointers.Empty();
        if (!Root.IsEmpty()) IFileManager::Get().Delete(*(Root/TEXT("connection.json")));
    }
};
IMPLEMENT_MODULE(FParallelPointerUEModule,ParallelPointerUE)
