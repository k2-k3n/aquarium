// dsp.h: Deep Water's state.
#ifndef DSP_H
#define DSP_H

#include <stdint.h>

#include "fx_core.hpp"
#include "params.h"
#include "water_model.h"

constexpr float kCave = 1.3f;                 // the reverb's size
constexpr uint32_t kChorus = 2048u;           // 42 ms: the CURRENT taps
constexpr uint32_t kSonar = water::kSonarMs * 48u;           // 560 ms: one sonar return
constexpr uint32_t kSmear[3] = {1123u, 1733u, 2399u};
constexpr uint32_t kBubbles = 8u;
constexpr uint32_t kPoolFloats = kChorus + kSonar + 4u + kSmear[0] + kSmear[1] + kSmear[2] + 12u
                               + fx::Plate::floats(kCave, 30.0f) + 64u;

struct Bubble {
    float wait, t, len, inv_len, ph, inc, rise, amp;
    bool on;
};

struct Dsp {
    // CURRENT
    fx::Line chorus;
    fx::Lfo lfo;
    fx::Drift drift_a, drift_b, drift_f;
    // DEPTH
    fx::Svf sea;
    fx::LowPass1 sea2;
    fx::HighPass1 rumble;
    fx::Plate cave;
    float makeup, send, bub_pitch;   // control values, refreshed every 16 samples
    uint32_t tick;
    // SONAR
    fx::Line sonar;
    fx::Diffuser smear[3];
    fx::LowPass1 sonar_lp;
    fx::HighPass1 sonar_hp;
    // BUBBLES
    Bubble b[kBubbles];
    fx::Rng rng;
    fx::Follower fast, slow;
    float cooldown;
    fx::LowPass1 bub_lp;

    fx::Limiter limiter;
    fx::Smooth depth, current, sonar_amt, mix, level;
    float bubbles;
};

bool dsp_init(Dsp &d, fx::Pool &pool);
void dsp_settle(Dsp &d);
void dsp_reset(Dsp &d);
void dsp_param(Dsp &d, uint32_t index, float value);
void dsp_process(Dsp &d, const float *in, float *out, uint32_t frames);

#endif
