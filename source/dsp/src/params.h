// params.h: Deep Water. Your guitar, heard from under the sea.
//
//   input -> CURRENT (two drifting vibrato taps: the water moves between you and the sound)
//         -> DEPTH (the sea's low-pass, 9 kHz at the surface down to 600 Hz, with a "pressure"
//            resonance that grows as you dive) -> a cave reverb that gets longer and darker
//         +  SONAR (a long, dark echo that smears more with every return)
//         +  BUBBLES (rising sine "bloops" on every pick and scattered while you play)
//         -> MIX with the guitar -> LEVEL -> limiter
#ifndef PARAMS_H
#define PARAMS_H

#include <stdint.h>

#include <resona/module.h>

#define MODULE_ID "org.resona.creative.aquarium.voice"
#define MODULE_NAME "Deep Water"
#define MODULE_TITLE "DEEP WATER"
#define MODULE_VERSION "1.0.1"

enum Param : uint32_t { P_DEPTH, P_CURRENT, P_BUBBLES, P_SONAR, P_MIX, P_LEVEL, P_COUNT };

const resona_param kParams[P_COUNT] = {
    {"DEPTH", 0.0f, 100.0f, 40.0f, "%"},
    {"CURRENT", 0.0f, 100.0f, 35.0f, "%"},
    {"BUBBLES", 0.0f, 100.0f, 50.0f, "%"},
    {"SONAR", 0.0f, 100.0f, 0.0f, "%"},
    {"MIX", 0.0f, 100.0f, 80.0f, "%"},
    {"LEVEL", -12.0f, 6.0f, 0.0f, "dB"},
};

#define PX_NONE {0, 0.0f, 0, nullptr}
const resona_param_ext kParamExt[P_COUNT] = {PX_NONE, PX_NONE, PX_NONE, PX_NONE, PX_NONE, PX_NONE};

#endif
