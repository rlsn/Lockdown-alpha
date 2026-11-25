#pragma once
#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace LDDemoTests
{
    // DEV-only game recorder for real-match review footage. Inert unless -LDRecordGame=<folder> is present. It records the
    // game's own frames (the game viewport's back buffer through FFrameGrabber, UI included) and the game's own master
    // submix, never the screen or the system audio, so a covered window or the user's other apps never reach the footage.
    // Frames go to <folder>/frames/NNNNNN.jpg with their game-clock time in frames.csv; the audio to game_audio.wav; the
    // clock alignment to recorder.json. -LDRecordGameFps (default 30) caps the frame rate; -LDRecordGameWidth (default 1600)
    // the frame width. The audio is the master submix's own buffers on the frame clock (render stalls become silence, so
    // the timeline holds). Scripts/AssembleGameRecording.py muxes them. Started once per process; later calls do nothing.
    void StartGameRecorder();
}
#endif
