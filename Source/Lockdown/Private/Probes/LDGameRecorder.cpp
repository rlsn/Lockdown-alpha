#include "Probes/LDGameRecorder.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "AudioDevice.h"
#include "ISubmixBufferListener.h"
#include "Sound/SoundSubmix.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameEngine.h"
#include "Engine/World.h"
#include "FrameGrabber.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Slate/SceneViewport.h"

DEFINE_LOG_CATEGORY_STATIC(LogLDGameRecorder, Log, All);

namespace LDDemoTests
{
namespace
{
    struct FStamp final : IFramePayload
    {
        explicit FStamp(double InSeconds) : Seconds(InSeconds) {}
        double Seconds;
    };

    // The master submix as the game rendered it, placed on the recorder's clock: a buffer that arrives later than the samples
    // so far cover is preceded by silence, so render stalls keep the timeline instead of squeezing it.
    struct FAudioTap final : ISubmixBufferListener
    {
        double StartSeconds = 0.0;
        FCriticalSection Lock;
        TArray<int16> Samples;
        int32 Rate = 0;
        int64 Frames = 0;
        double FirstSeconds = -1.0;
        int64 PaddedFrames = 0;
        const FString Name = TEXT("LDGameRecorder");

        virtual const FString& GetListenerName() const override { return Name; }
        virtual void OnNewSubmixBuffer(const USoundSubmix*, float* Data, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double) override
        {
            if (NumChannels <= 0 || SampleRate <= 0) { return; }
            const int32 Count = NumSamples / NumChannels;
            const double Now = FPlatformTime::Seconds() - StartSeconds;
            FScopeLock Guard(&Lock);
            if (Rate == 0) { Rate = SampleRate; FirstSeconds = Now - double(Count) / SampleRate; }
            if (Samples.Num() > SampleRate * 2 * 60 * 15) { return; }
            const int64 Expected = int64((Now - FirstSeconds) * Rate) - Count;
            if (Expected - Frames > Rate / 20)
            {
                const int64 Gap = Expected - Frames;
                Samples.AddZeroed(int32(Gap * 2));
                Frames += Gap;
                PaddedFrames += Gap;
            }
            for (int32 Index = 0; Index < Count; ++Index)
            {
                const float L = Data[Index * NumChannels];
                const float R = NumChannels > 1 ? Data[Index * NumChannels + 1] : L;
                Samples.Add(int16(FMath::Clamp(L, -1.f, 1.f) * 32767.f));
                Samples.Add(int16(FMath::Clamp(R, -1.f, 1.f) * 32767.f));
            }
            Frames += Count;
        }

        bool Save(const FString& File)
        {
            FScopeLock Guard(&Lock);
            if (Rate == 0) { return false; }
            TArray<uint8> Wav;
            const uint32 DataBytes = uint32(Samples.Num() * sizeof(int16));
            auto Put32 = [&Wav](uint32 V) { Wav.Append(reinterpret_cast<const uint8*>(&V), 4); };
            auto Put16 = [&Wav](uint16 V) { Wav.Append(reinterpret_cast<const uint8*>(&V), 2); };
            Wav.Append(reinterpret_cast<const uint8*>("RIFF"), 4); Put32(36 + DataBytes);
            Wav.Append(reinterpret_cast<const uint8*>("WAVEfmt "), 8); Put32(16); Put16(1); Put16(2); Put32(uint32(Rate));
            Put32(uint32(Rate) * 4); Put16(4); Put16(16);
            Wav.Append(reinterpret_cast<const uint8*>("data"), 4); Put32(DataBytes);
            Wav.Append(reinterpret_cast<const uint8*>(Samples.GetData()), DataBytes);
            return FFileHelper::SaveArrayToFile(Wav, *File);
        }
    };

    // One recorder per process: frames from the game viewport's back buffer and the master submix of the game's own audio
    // device. Nothing here reads the desktop or the system audio.
    struct FRecorder
    {
        FString Folder;
        double Fps = 30.0;
        int32 Width = 1600;
        double StartSeconds = 0.0;
        double LastCapture = -1.0;
        FDateTime StartUtc;
        TUniquePtr<FFrameGrabber> Grabber;
        FTSTicker::FDelegateHandle Tick;
        FDelegateHandle PreExit;
        TSharedPtr<FAudioTap, ESPMode::ThreadSafe> Tap;
        FAudioDeviceHandle Device;
        int32 Written = 0;
        TAtomic<int32> Pending{0};
        FString FrameLog;
        bool bFinished = false;

        static UWorld* GameWorld()
        {
            if (!GEngine) { return nullptr; }
            for (const FWorldContext& Context : GEngine->GetWorldContexts())
            {
                if (Context.WorldType == EWorldType::Game && Context.World()) { return Context.World(); }
            }
            return nullptr;
        }

        bool TryBegin()
        {
            UGameEngine* Engine = Cast<UGameEngine>(GEngine);
            if (!Engine || !Engine->SceneViewport.IsValid()) { return false; }
            const FIntPoint Size = Engine->SceneViewport->GetSizeXY();
            if (Size.X <= 0 || Size.Y <= 0) { return false; }
            UWorld* World = GameWorld();
            if (!World) { return false; }
            const int32 OutW = FMath::Min(Width, Size.X) & ~1;
            const FIntPoint Out(OutW, (FMath::RoundToInt(double(Size.Y) * OutW / Size.X)) & ~1);
            Grabber = MakeUnique<FFrameGrabber>(Engine->SceneViewport.ToSharedRef(), Out);
            Grabber->StartCapturingFrames();
            IFileManager::Get().MakeDirectory(*(Folder / TEXT("frames")), true);
            StartSeconds = FPlatformTime::Seconds();
            StartUtc = FDateTime::UtcNow();
            Device = World->GetAudioDevice();
            if (Device.IsValid())
            {
                // The recorded game runs off screen, never focused; the engine default mutes an unfocused game's mix.
                FApp::SetUnfocusedVolumeMultiplier(1.f);
                // The mix still renders and reaches the tap below, but the device output is faded to silence, so a
                // recording never plays through the user's speakers while they work.
                Device->FadeOut();
                Tap = MakeShared<FAudioTap, ESPMode::ThreadSafe>();
                Tap->StartSeconds = StartSeconds;
                Device->RegisterSubmixBufferListener(Tap.ToSharedRef(), Device->GetMainSubmixObject());
            }
            else
            {
                UE_LOG(LogLDGameRecorder, Error, TEXT("LD_GAME_RECORDER_NO_AUDIO the game world has no audio device (-nosound?)"));
            }
            FrameLog = TEXT("index,seconds\n");
            UE_LOG(LogLDGameRecorder, Display, TEXT("LD_GAME_RECORDER_START folder=%s size=%dx%d fps=%.1f"), *Folder, Out.X, Out.Y, Fps);
            return true;
        }

        void Drain()
        {
            if (!Grabber) { return; }
            for (FCapturedFrameData& Frame : Grabber->GetCapturedFrames())
            {
                const double Seconds = Frame.GetPayload<FStamp>()->Seconds;
                const int32 Index = Written++;
                FrameLog += FString::Printf(TEXT("%d,%.4f\n"), Index, Seconds);
                const FString File = Folder / TEXT("frames") / FString::Printf(TEXT("%06d.jpg"), Index);
                ++Pending;
                Async(EAsyncExecution::ThreadPool, [this, File, Size = Frame.BufferSize, Pixels = MoveTemp(Frame.ColorBuffer)]() mutable
                {
                    for (FColor& Pixel : Pixels) { Pixel.A = 255; }
                    const FImageView View(Pixels.GetData(), Size.X, Size.Y, EGammaSpace::sRGB);
                    if (!FImageUtils::SaveImageByExtension(*File, View, 88))
                    {
                        UE_LOG(LogLDGameRecorder, Error, TEXT("LD_GAME_RECORDER_WRITE_FAILED file=%s"), *File);
                    }
                    --Pending;
                });
            }
        }

        bool OnTick(float)
        {
            if (bFinished) { return false; }
            if (!Grabber && !TryBegin()) { return true; }
            const double Now = FPlatformTime::Seconds() - StartSeconds;
            if (LastCapture < 0.0 || Now - LastCapture >= 1.0 / Fps - 0.002)
            {
                Grabber->CaptureThisFrame(MakeShared<FStamp, ESPMode::ThreadSafe>(Now));
                LastCapture = Now;
            }
            Drain();
            if (IsEngineExitRequested()) { Finish(); return false; }
            return true;
        }

        void Finish()
        {
            if (bFinished || !Grabber) { bFinished = true; return; }
            bFinished = true;
            const double EndSeconds = FPlatformTime::Seconds() - StartSeconds;
            bool bAudio = false;
            if (Tap.IsValid())
            {
                if (Device.IsValid()) { Device->UnregisterSubmixBufferListener(Tap.ToSharedRef(), Device->GetMainSubmixObject()); }
                bAudio = Tap->Save(Folder / TEXT("game_audio.wav"));
                if (!bAudio) { UE_LOG(LogLDGameRecorder, Error, TEXT("LD_GAME_RECORDER_AUDIO_EMPTY the master submix delivered no audio")); }
            }
            Grabber->StopCapturingFrames();
            FlushRenderingCommands();
            for (int32 Attempt = 0; Attempt < 50 && Grabber->HasOutstandingFrames(); ++Attempt)
            {
                FlushRenderingCommands();
                Drain();
                FPlatformProcess::Sleep(0.01f);
            }
            Drain();
            Grabber->Shutdown();
            Grabber.Reset();
            for (int32 Attempt = 0; Attempt < 3000 && Pending.Load() > 0; ++Attempt) { FPlatformProcess::Sleep(0.01f); }
            FFileHelper::SaveStringToFile(FrameLog, *(Folder / TEXT("frames.csv")));
            const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
            Json->SetStringField(TEXT("start_utc"), StartUtc.ToIso8601());
            Json->SetNumberField(TEXT("frames"), Written);
            Json->SetNumberField(TEXT("seconds"), EndSeconds);
            Json->SetBoolField(TEXT("audio"), bAudio);
            if (bAudio)
            {
                Json->SetNumberField(TEXT("audio_start_seconds"), Tap->FirstSeconds);
                Json->SetNumberField(TEXT("audio_padded_seconds"), double(Tap->PaddedFrames) / Tap->Rate);
            }
            Json->SetNumberField(TEXT("fps_cap"), Fps);
            Json->SetStringField(TEXT("source"), TEXT("game viewport back buffer (FFrameGrabber) and the game's master submix buffers; no desktop or system audio"));
            FString Text;
            FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
            FFileHelper::SaveStringToFile(Text, *(Folder / TEXT("recorder.json")));
            UE_LOG(LogLDGameRecorder, Display, TEXT("LD_GAME_RECORDER_STOP frames=%d seconds=%.2f pending=%d"), Written, EndSeconds, Pending.Load());
        }
    };

    FRecorder* GRecorder = nullptr;
}

void StartGameRecorder()
{
    FString Folder;
    if (GRecorder || !FParse::Value(FCommandLine::Get(), TEXT("-LDRecordGame="), Folder) || Folder.IsEmpty()) { return; }
    GRecorder = new FRecorder();
    GRecorder->Folder = FPaths::ConvertRelativePathToFull(Folder);
    FParse::Value(FCommandLine::Get(), TEXT("-LDRecordGameFps="), GRecorder->Fps);
    GRecorder->Fps = FMath::Clamp(GRecorder->Fps, 5.0, 60.0);
    FParse::Value(FCommandLine::Get(), TEXT("-LDRecordGameWidth="), GRecorder->Width);
    GRecorder->Width = FMath::Clamp(GRecorder->Width, 640, 3840);
    GRecorder->Tick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(GRecorder, &FRecorder::OnTick));
    // The exit path can end the loop before the next ticker pass; finishing here still has the game world and its device.
    GRecorder->PreExit = FCoreDelegates::OnEnginePreExit.AddLambda([]() { if (GRecorder) { GRecorder->Finish(); } });
}
}
#endif
