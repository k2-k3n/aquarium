// dsp.cpp: Deep Water.
#include "dsp.h"

namespace {

// One bubble: a sine that glides up exponentially (a real bubble's pitch rises as it shrinks
// away from the wall it left), a 3 ms soft attack and a fast squared decay. `order` is its
// place in a "glug-glug" run: each one a little later and a little higher.
void spawn(Dsp &d, float level, float wait, float order)
{
    for (uint32_t i = 0; i < kBubbles; ++i) {
        Bubble &b = d.b[i];
        if (b.on) continue;
        const float u = fx::clampf(d.rng.uni() * 0.8f + order * 0.12f, 0.0f, 1.0f);   // small = high and short
        const float hz = 300.0f * resona_exp2f(1.5f * u) * d.bub_pitch;               // 300 .. 850 Hz
        b.len = fx::ms_to_samples(90.0f - 55.0f * u + 8.0f * d.rng.bi());
        b.inv_len = 1.0f / b.len;
        b.inc = hz * fx::kInvRate;
        b.rise = resona_exp2f((0.55f + 0.5f * d.rng.uni()) * b.inv_len);                  // up 0.55 .. 1.05 octaves
        b.amp = level * (0.45f + 0.55f * d.rng.uni());
        b.wait = wait;
        b.t = 0.0f;
        b.ph = 0.0f;
        b.on = true;
        return;
    }
}

void control(Dsp &d)
{
    const float depth = d.depth.y, cur = d.current.y;
    // the sea's low-pass: 9 kHz at the surface, 600 Hz on the bottom, breathing a little with the current
    const float wob = 1.0f + 0.10f * cur * d.drift_f.y;
    const float hz = 9000.0f * resona_exp2f(-3.907f * depth) * wob;
    const float g = fx::tan_small(fx::clampf(hz, 300.0f, 9500.0f) * fx::kPi * fx::kInvRate);
    const float k = 1.0f / (0.7f + 2.1f * depth);            // the "pressure": Q 0.7 .. 2.8
    d.sea.set(g, k);
    d.sea2.set_hz(hz * 1.8f);
    d.makeup = resona_db_to_lin(1.5f + 1.5f * depth - 4.2f * depth * depth * depth);   // level-matched by ear and meter
    d.send = 0.10f + 0.45f * depth;
    d.bub_pitch = resona_exp2f(-0.5f * depth);     // deeper water: bigger, lower bubbles
    d.cave.set_decay(1.4f + 5.0f * depth);
    d.cave.set_damp_hz(7000.0f * resona_exp2f(-2.2f * depth));
    d.cave.set_bandwidth_hz(8000.0f * resona_exp2f(-1.6f * depth));
    d.lfo.set_hz(water::current_hz(depth, cur));   // everything moves slower down there
}

void reset_all(Dsp &d)
{
    d.chorus.clear(); d.sonar.clear(); d.cave.clear();
    for (uint32_t i = 0; i < 3; ++i) d.smear[i].clear();
    d.lfo.ph = 0.0f;
    d.drift_a.reset(); d.drift_b.reset(); d.drift_f.reset();
    d.sea.reset(); d.sea2.reset(); d.rumble.reset();
    d.sonar_lp.reset(); d.sonar_hp.reset(); d.bub_lp.reset();
    for (uint32_t i = 0; i < kBubbles; ++i) d.b[i].on = false;
    d.rng.reset();
    d.fast.reset(); d.slow.reset();
    d.cooldown = 0.0f;
    d.tick = 0;
    d.limiter.reset();
}

}  // namespace

bool dsp_init(Dsp &d, fx::Pool &pool)
{
    bool ok = d.chorus.attach(pool, kChorus);
    ok = d.sonar.attach(pool, kSonar + 4u) && ok;
    for (uint32_t i = 0; i < 3; ++i) ok = d.smear[i].attach(pool, kSmear[i], 0, 0.62f) && ok;
    ok = d.cave.attach(pool, kCave, 30.0f) && ok;
    if (!ok) return false;
    d.cave.set_predelay_ms(18.0f);
    d.cave.set_lowcut_hz(140.0f);
    d.cave.set_mod(0.6f, 0.35f);
    d.drift_a.init(0x51EA51EAu, 0.9f);
    d.drift_b.init(0x0CEA0001u, 0.6f);
    d.drift_f.init(0x00F15400u, 1.3f);
    d.rumble.set_hz(35.0f);
    d.sonar_lp.set_hz(1500.0f);
    d.sonar_hp.set_hz(170.0f);
    d.bub_lp.set_hz(5000.0f);
    d.fast.set(0.5f, 40.0f);
    d.slow.set(30.0f, 350.0f);
    d.rng.seed(0xB0B1E5u);
    d.limiter.init();
    d.depth.init(0.4f, water::kMotionMs);
    d.current.init(0.35f, water::kMotionMs);
    d.sonar_amt.init(0.0f, 60.0f);
    d.mix.init(0.8f, 30.0f);
    d.level.init(1.0f, 30.0f);
    d.bubbles = 0.5f;
    reset_all(d);
    control(d);
    return true;
}

void dsp_settle(Dsp &d)
{
    d.depth.snap(); d.current.snap(); d.sonar_amt.snap(); d.mix.snap(); d.level.snap();
    control(d);
}

void dsp_reset(Dsp &d) { reset_all(d); dsp_settle(d); }

void dsp_param(Dsp &d, uint32_t index, float v)
{
    switch (index) {
    case P_DEPTH: d.depth.set(fx::pct(v)); break;
    case P_CURRENT: d.current.set(fx::pct(v)); break;
    case P_BUBBLES: d.bubbles = fx::pct(v); break;
    case P_SONAR: d.sonar_amt.set(fx::pct(v)); break;
    case P_MIX: d.mix.set(fx::pct(v)); break;
    case P_LEVEL: d.level.set(resona_db_to_lin(v)); break;
    default: break;
    }
}

void dsp_process(Dsp &d, const float *in, float *out, uint32_t frames)
{
    const float thr = 0.004f;   // about -48 dBFS: below this nobody is playing
    for (uint32_t i = 0; i < frames; ++i) {
        const float x = in[i];
        const float depth = d.depth.next(), cur = d.current.next();
        d.drift_f.next();
        if ((d.tick++ & 15u) == 0u) control(d);

        // CURRENT: two taps wandering around 5 and 9 ms, a sine plus slow random drift each
        const float s = d.lfo.sine();
        const float sway = fx::ms_to_samples(0.3f + 2.6f * cur);
        d.chorus.push(x);
        const float ta = 240.0f + sway * (0.7f * s + 0.6f * d.drift_a.next());
        const float tb = 432.0f - sway * (0.6f * s - 0.7f * d.drift_b.next());
        const float moved = (1.0f - 0.3f * cur) * d.chorus.cub(ta) + 0.55f * cur * d.chorus.cub(tb);

        // DEPTH: the water above you
        const float lp = d.sea.lowpass(d.rumble.process(moved));
        const float wet = d.sea2.process(lp) * d.makeup;

        // BUBBLES: a run of them on every pick, strays while you keep playing
        const float e = d.fast.process(x);
        const float es_before = d.slow.y;
        const float es = d.slow.process(x);
        if (d.cooldown > 0.0f) d.cooldown -= 1.0f;
        const float amount = d.bubbles;
        if (amount > 0.005f && es > thr * 0.5f) {
            const float lvl = fx::clampf(es * 1.5f + 0.010f, 0.0f, 0.25f);
            if (d.cooldown <= 0.0f && e > thr * 2.0f && e > 2.0f * es_before + thr) {
                d.cooldown = fx::ms_to_samples(water::kBubbleCooldownMs);
                const uint32_t n = 1u + (uint32_t)(amount * 3.4f * d.rng.uni() + amount * 1.2f);
                float wait = fx::ms_to_samples(4.0f + 20.0f * d.rng.uni());
                for (uint32_t k = 0; k < n && k < 5u; ++k) {
                    spawn(d, lvl, wait, (float)k);
                    wait += fx::ms_to_samples(38.0f + 45.0f * d.rng.uni());
                }
            }
            const float rate = water::stray_bubbles_hz(amount);   // strays per second
            if (d.rng.uni() < rate * fx::kInvRate) spawn(d, lvl * 0.7f, 0.0f, 3.0f * d.rng.uni());
        }
        float bub = 0.0f;
        for (uint32_t k = 0; k < kBubbles; ++k) {
            Bubble &b = d.b[k];
            if (!b.on) continue;
            if (b.wait > 0.0f) { b.wait -= 1.0f; continue; }
            if (b.t >= b.len) { b.on = false; continue; }
            const float a = b.t < 144.0f ? b.t * (1.0f / 144.0f) : 1.0f;
            const float q = 1.0f - b.t * b.inv_len;
            bub += fx::sin01(b.ph) * a * a * (3.0f - 2.0f * a) * q * q * b.amp;
            b.ph = fx::wrap01(b.ph + b.inc);
            b.inc *= b.rise;
            b.t += 1.0f;
        }
        bub = d.bub_lp.process(bub);

        // SONAR: 560 ms away and back, darker and more smeared on every return
        const float amt = d.sonar_amt.next();
        float echo = d.sonar.at(kSonar);
        float loop = d.sonar_hp.process(d.sonar_lp.process(wet * amt + 0.25f * bub * amt + water::kSonarFeedback * echo));
        for (uint32_t k = 0; k < 3; ++k) loop = d.smear[k].process(loop);
        d.sonar.push(fx::sat(loop));

        const float cave = d.cave.process(wet + 0.6f * bub + 0.5f * echo);
        const float sea = wet * (1.0f - 0.25f * depth) + d.send * cave + (1.0f - 0.3f * depth) * echo + bub;

        const float m = d.mix.next();
        out[i] = d.limiter.process((x * (1.0f - m) + sea * m) * d.level.next());
    }
}
