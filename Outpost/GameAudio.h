#pragma once

#include "AudioSystem.h"

/// The game's audio provider (was Aud.h/Aud.cpp): object positions and
/// liveness, the listener pose, WAV-name-to-ID resolution and the game
/// clock, wired into the AudioWorld the engine's audio system takes at Init.
[[nodiscard]] Neuron::AudioWorld GameAudioWorld();
