// Aquarium: your guitar is playing underwater. MIT.
//
//   SELECTOR   turn: DEPTH (the submarine dives and rises)   click: sonar on/off
//   CUSTOMIZE  turn: CURRENT (the drifting specks) click: BUBBLES few / some / lots
//              long press: reset
//
// Deep Water supplies the tone. The scene follows the encoders and the guitar input meter.
#include <resona/platform.h>
#include <resona/libc.h>
#include <resona/startup_helpers.h>
#include "kit.h"
#include "scene.h"
#include "aquarium_splash.h"
#include "controller_link.h"
#include "../dsp/src/water_model.h"

namespace {

const resona_platform *P;
const char kPack[] = "org.resona.creative.aquarium.voice";
const char kPackVersion[] = "1.0.1";
enum { DEPTH, CURRENT, BUBBLES, SONAR, MIX, LEVEL, PARAMS };
const float kBubbleAmount[3] = {18.0f, 55.0f, 100.0f};
enum { kMaxBubbles = 36, kSpecks = 18 };

struct SavedV1 { uint32_t magic; int depth, current, bubbles, sonar; };
struct Saved { uint32_t magic; int depth, current, bubbles, sonar, sonar_amt, mix, level; };
const uint32_t kMagicV1 = 0x41515531u;   // "AQU1"
const uint32_t kMagic = 0x41515532u;     // "AQU2"

// The controllers' MIDI map: CC on channel 1 (controller/shared/profile.js says the same).
enum { CC_DEPTH = 20, CC_CURRENT, CC_BUBBLES, CC_SONAR, CC_SONAR_AMT, CC_MIX, CC_LEVEL,
       CC_DIVE = 30, CC_RISE, CC_PING, CC_STORM };
enum { kBinds = 11 };

struct Fish { float x, y; int dir; float speed; };
struct Bubble { float x, y, v; uint8_t size, on, phase; };

struct State {
    kit::Audio audio;
    kit::Levels lv;
    kit::Saver saver;
    Saved s;
    uint32_t ms, seed, pick_at, sonar_at;
    float depth, current, scatter, drift, bob, stray, light_phase;
    float sonar_strength;
    uint8_t pending_bubbles;
    uint32_t next_bubble;
    Fish fish[3];
    Bubble bub[kMaxBubbles];
    int dive, rise, ping, storm;   // pads held on a controller
    ctl::Link link;
};

ctl::Bind kBind[kBinds];
void bind(State *st)
{
    Saved &s = st->s;
    const ctl::Bind b[kBinds] = {
        {CC_DEPTH, ctl::RANGE, 0, 100, &s.depth}, {CC_CURRENT, ctl::RANGE, 0, 100, &s.current},
        {CC_BUBBLES, ctl::STEPS, 0, 2, &s.bubbles}, {CC_SONAR, ctl::STEPS, 0, 1, &s.sonar},
        {CC_SONAR_AMT, ctl::RANGE, 0, 100, &s.sonar_amt}, {CC_MIX, ctl::RANGE, 0, 100, &s.mix},
        {CC_LEVEL, ctl::RANGE, -12, 6, &s.level},
        {CC_DIVE, ctl::TRIGGER, 0, 1, nullptr}, {CC_RISE, ctl::TRIGGER, 0, 1, nullptr},
        {CC_PING, ctl::TRIGGER, 0, 1, nullptr}, {CC_STORM, ctl::TRIGGER, 0, 1, nullptr},
    };
    for (int i = 0; i < kBinds; ++i) kBind[i] = b[i];
}

// The pads override the settings while held: DIVE wins over RISE.
int depth_now(const State *st) { return st->dive ? 100 : (st->rise ? 0 : st->s.depth); }
int current_now(const State *st) { return st->storm ? 100 : st->s.current; }
int bubbles_now(const State *st) { return st->storm ? 2 : st->s.bubbles; }
int sonar_amount(const State *st)
{
    return st->ping ? 100 : (st->s.sonar ? st->s.sonar_amt : 0);
}

uint32_t rnd(State *st) { st->seed = st->seed * 1664525u + 1013904223u; return st->seed >> 8; }
float rndf(State *st) { return (float)(rnd(st) & 0xFFFFu) * (1.0f / 65536.0f); }

void push(State *st)
{
    kit::audio_set(P, st->audio, DEPTH, (float)depth_now(st));
    kit::audio_set(P, st->audio, CURRENT, (float)current_now(st));
    kit::audio_set(P, st->audio, BUBBLES, kBubbleAmount[bubbles_now(st)]);
    kit::audio_set(P, st->audio, SONAR, (float)sonar_amount(st));
    kit::audio_set(P, st->audio, MIX, (float)st->s.mix);
    kit::audio_set(P, st->audio, LEVEL, (float)st->s.level);
}

void defaults(State *st) { st->s = {kMagic, 40, 35, 1, 0, 75, 80, 0}; }

void bubble(State *st, float x, float y, int size)
{
    for (int i = 0; i < kMaxBubbles; ++i) {
        Bubble &b = st->bub[i];
        if (b.on) continue;
        b.x = x; b.y = y;
        b.v = 22.0f + 9.0f * (float)size + 14.0f * rndf(st);
        b.size = (uint8_t)size; b.phase = (uint8_t)rnd(st); b.on = 1;
        return;
    }
}

int sub_x(const State *st)
{
    return 85 + (int)((2.0f + 8.0f * st->current) * resona_sinf(st->bob));
}

int sub_y(const State *st)
{
    return 18 + (int)(37.0f * st->depth + 2.0f * resona_sinf(st->bob * 1.7f) + 0.5f);
}

void emit_bubble(State *st)
{
    const int dx = (int)(rnd(st) % 12u);
    const int dy = (int)(rnd(st) % 8u);
    const int size = (int)(rnd(st) % 2u) + (st->depth > 0.55f ? 1 : 0);
    bubble(st, (float)(sub_x(st) + dx), (float)(sub_y(st) + 49 + dy), size);
}

// The SDK exposes a 0–9 input meter. Its transients approximate the audio
// processor's sample-level pick detector without adding work to the DSP.
void react_to_input(State *st, uint8_t prev, uint32_t dt)
{
    if (!st->audio.ready || st->s.mix == 0) {
        st->pending_bubbles = 0;
        st->sonar_strength = 0.0f;
        st->stray = 0.0f;
        for (Bubble &b : st->bub) b.on = 0;
        return;
    }

    const uint8_t in = st->lv.raw_in;
    const float amount = kBubbleAmount[bubbles_now(st)] * 0.01f;
    if (in >= 2 && in >= prev + 2 && st->ms - st->pick_at >= water::kBubbleCooldownMs) {
        st->pick_at = st->ms;
        st->scatter = in * (1.0f / 9.0f);
        st->pending_bubbles = (uint8_t)(1 + bubbles_now(st));
        st->next_bubble = st->ms + 12u;
        const float strength = st->scatter * sonar_amount(st) * 0.01f * st->s.mix * 0.01f;
        // Give an existing pulse time to reach its first return.
        if (strength > 0.0f && (st->sonar_strength == 0.0f || st->ms - st->sonar_at >= water::kSonarMs)) {
            st->sonar_at = st->ms;
            st->sonar_strength = strength;
        }
    }
    if (st->pending_bubbles && (int32_t)(st->ms - st->next_bubble) >= 0) {
        emit_bubble(st);
        --st->pending_bubbles;
        st->next_bubble = st->ms + 38u + rnd(st) % 46u;
    }
    if (in >= 2) {
        st->stray += water::stray_bubbles_hz(amount) * dt * 0.001f;
        if (st->stray >= 1.0f) {
            emit_bubble(st);
            st->stray -= 1.0f;
        }
    } else {
        st->stray = 0.0f;
    }
}

void draw_water(kit::Canvas &c, const State *st)
{
    for (int y = 0; y < c.H; ++y)
        memcpy(c.px + y * c.stride, kAquariumSea + y * c.W, c.W * sizeof(uint16_t));

    // Animate the light in the open water, behind the submarine and HUD.
    constexpr int left = 128, height = 100;
    for (int y = 0; y < height; ++y) {
        const float shimmer = resona_sinf(st->light_phase - y * 0.09f);
        const float light = (1.0f - (float)y / height)
            * (0.10f - 0.45f * st->depth + (0.14f + 0.10f * st->current) * shimmer);
        const int slope = (int)(light * 65536.0f / (c.W - left));
        uint16_t *row = c.px + y * c.stride;
        int gain = 65536;
        for (int x = left; x < c.W; ++x) {
            gain += slope;
            const uint16_t color = row[x];
            const int r = ((color >> 11) * gain + 32768) >> 16;
            const int g = (((color >> 5) & 63) * gain + 32768) >> 16;
            const int b = ((color & 31) * gain + 32768) >> 16;
            row[x] = (uint16_t)((kit::clampi(r, 0, 31) << 11)
                | (kit::clampi(g, 0, 63) << 5) | kit::clampi(b, 0, 31));
        }
    }
}

void draw(State *st)
{
    kit::Canvas c;
    if (!c.begin(P)) return;
    const uint16_t ink = kit::rgb(2, 13, 26), cream = kit::rgb(245, 249, 234);
    const uint16_t aqua = kit::rgb(143, 230, 227), muted = kit::rgb(75, 149, 163);
    const uint16_t amber = kit::rgb(255, 199, 94);
    const int sx = sub_x(st), sy = sub_y(st);

    draw_water(c, st);

    for (int k = 0; k < kSpecks; ++k) {
        const uint32_t h = (uint32_t)k * 2654435761u;
        const int x = ((int)(h % 320u) + (int)st->drift) % 320;
        const int y = 27 + (int)((h >> 10) % 111u);
        c.put(x, y, k % 4 ? muted : aqua);
    }
    for (int k = 0; k < 3; ++k) {
        const Fish &f = st->fish[k];
        const int x = (int)f.x;
        const int y = (int)(f.y + 3.0f * resona_sinf(st->bob + (float)k * 1.8f));
        const uint16_t shadow = kit::rgb(10, 62, 82);
        c.ellipse(x, y, 6 - k, 2, shadow);
        c.tri(x - f.dir * 3, y, x - f.dir * 10, y - 3, x - f.dir * 10, y + 3, shadow);
    }

    const int lamp_x = sx + kSubW - 9, lamp_y = sy + 47;
    const uint32_t elapsed = st->ms - st->sonar_at;
    if (st->sonar_strength > 0.0f && elapsed < 3 * water::kSonarMs) {
        const uint32_t pass = elapsed / water::kSonarMs;
        const float phase = (float)(elapsed % water::kSonarMs) / water::kSonarMs;
        float intensity = st->sonar_strength * (1.0f - 0.65f * phase);
        for (uint32_t i = 0; i < pass; ++i) intensity *= water::kSonarFeedback;
        const int r = 5 + (int)(phase * 42.0f);
        c.ring(lamp_x, lamp_y, r, (r + 1) / 2, kit::mix(ink, aqua, 0.15f + intensity * 0.85f));
    }
    for (int j = 0; j < kSubH; ++j) {
        const uint16_t *row = kAquariumSub + j * kSubW;
        uint16_t *dst = c.px + (sy + j) * c.stride + sx;
        for (int i = 0; i < kSubW; ++i) if (row[i]) dst[i] = row[i];
    }
    for (const Bubble &b : st->bub) {
        if (!b.on) continue;
        const int x = (int)(b.x + 2.0f * resona_sinf(st->bob * 3.0f + (float)b.phase));
        const int y = (int)b.y;
        const int r = 1 + b.size;
        c.ring(x, y, r, r, muted);
        c.put(x - r / 2, y - r, aqua);
    }

    c.text_s(13, 10, 2, cream, ink, "AQUARIUM");
    c.text_s(13, 31, 1, aqua, ink, "DEPTH");
    char depth[12];
    P->format(depth, sizeof depth, "%d M", depth_now(st) * 2);
    c.text_s(13, 42, 2, cream, ink, depth);
    const int gauge_y = 67 + (int)(55.0f * st->depth);
    c.rect(16, 67, 1, 56, muted);
    for (int y = 67; y <= 122; y += 11) c.rect(16, y, 5, 1, muted);
    c.rect(13, gauge_y - 1, 11, 3, amber);

    const bool sonar = sonar_amount(st) > 0;
    c.rect(250, 10, 58, 18, ink);
    c.circle(258, 19, 2, sonar ? aqua : muted);
    c.text(265, 15, 1, sonar ? cream : muted, "SONAR");

    c.rect(0, 138, c.W, 34, ink);
    c.text(13, 142, 1, aqua, "CURRENT");
    char current[12];
    P->format(current, sizeof current, "%d%%", current_now(st));
    c.text(61, 142, 1, cream, current);
    c.rect(13, 153, 89, 2, muted);
    c.rect(13, 153, current_now(st) * 89 / 100, 2, aqua);
    c.text(234, 142, 1, aqua, "BUBBLES");
    for (int i = 0; i < 3; ++i)
        c.rect(236 + 23 * i, 153, 17, 2, i <= bubbles_now(st) ? aqua : muted);

    c.banner(kit::audio_message(st->audio), ink, amber);
    c.present(P);
}

void animate(State *st, uint32_t ms)
{
    const float dt = (float)ms * 0.001f;
    const float glide = 1.0f - resona_expf(-(float)ms / water::kMotionMs);
    st->depth += ((float)depth_now(st) * 0.01f - st->depth) * glide;
    st->current += ((float)current_now(st) * 0.01f - st->current) * glide;
    const float cur = st->current;
    st->light_phase += dt * (0.6f + 2.1f * cur);
    if (st->light_phase >= 6.28318531f) st->light_phase -= 6.28318531f;
    st->scatter *= resona_expf(-dt * 1.5f);
    st->bob += dt * 6.28318531f * water::current_hz(st->depth, cur);
    if (st->bob >= 62.831853f) st->bob -= 62.831853f;
    st->drift += dt * (5.0f + 55.0f * cur);
    if (st->drift >= 320.0f) st->drift -= 320.0f;

    const float pace = 0.65f + cur + 1.6f * st->lv.in + 7.0f * st->scatter;
    for (Fish &f : st->fish) {
        f.x += (float)f.dir * f.speed * pace * dt;
        if ((f.dir > 0 && f.x > 334.0f) || (f.dir < 0 && f.x < -14.0f)) {
            f.dir = -f.dir;
            f.y = 51.0f + rndf(st) * 77.0f;
        }
    }

    for (Bubble &b : st->bub) {
        if (!b.on) continue;
        b.y -= b.v * dt;
        b.x += cur * 11.0f * dt;
        if (b.y < 12.0f || b.x > 324.0f) b.on = 0;
    }
}

int init(void *state, const resona_platform *platform)
{
    State *st = static_cast<State *>(state);
    P = platform;
    const int splash = resona_startup_splash_apply(P, kAquariumSplash, sizeof kAquariumSplash);
    if (splash < 0 && splash != RESONA_ERR_UNSUPPORTED)
        P->log("Aquarium: startup animation could not be saved");
    defaults(st);
    Saved v;
    SavedV1 v1;
    if (kit::load(P, v, kMagic)) st->s = v;
    else if (kit::load(P, v1, kMagicV1)) { st->s.depth = v1.depth; st->s.current = v1.current; st->s.bubbles = v1.bubbles; st->s.sonar = v1.sonar; }
    Saved &s = st->s;
    s.depth = kit::clampi(s.depth, 0, 100); s.current = kit::clampi(s.current, 0, 100); s.bubbles = kit::clampi(s.bubbles, 0, 2);
    s.sonar = s.sonar ? 1 : 0; s.sonar_amt = kit::clampi(s.sonar_amt, 0, 100); s.mix = kit::clampi(s.mix, 0, 100);
    s.level = kit::clampi(s.level, -12, 6);
    bind(st);
    st->seed = 0xA0CEA11u;
    st->depth = (float)st->s.depth * 0.01f;
    st->current = (float)st->s.current * 0.01f;
    st->pick_at = 0u - (uint32_t)water::kBubbleCooldownMs;
    st->fish[0] = {214.0f, 59.0f, -1, 17.0f};
    st->fish[1] = {8.0f, 96.0f, 1, 11.0f};
    st->fish[2] = {262.0f, 62.0f, -1, 7.0f};
    push(st);
    return 0;
}

void event(void *state, const resona_app_event *ev)
{
    State *st = static_cast<State *>(state);
    if (kit::audio_event(P, st->audio, ev, st->ms)) return;
    ctl::Msg m;
    if (ctl::receive(P, st->link, kBind, kBinds, ev, m)) {
        if (!m.changed) return;
        if (m.cc == CC_DIVE) st->dive = m.down;
        else if (m.cc == CC_RISE) st->rise = m.down;
        else if (m.cc == CC_PING) st->ping = m.down;
        else if (m.cc == CC_STORM) st->storm = m.down;
        else kit::save_later(st->saver, st->ms);
        push(st);
        return;
    }
    if (ev->kind != RESONA_APP_EV_INPUT) return;
    const int top = ev->u.input.input == RESONA_INPUT_SELECTOR;
    const int kind = (int)ev->u.input.kind, v = ev->u.input.value;
    if (kind == RESONA_EVENT_TURN) {
        if (top) st->s.depth = kit::clampi(st->s.depth + v * 2, 0, 100);
        else st->s.current = kit::clampi(st->s.current + v * 2, 0, 100);
    } else if (kind == RESONA_EVENT_CLICK) {
        if (top) st->s.sonar ^= 1;
        else st->s.bubbles = (st->s.bubbles + 1) % 3;
    } else if (kind == RESONA_EVENT_LONG && !top) {
        defaults(st);
    } else {
        return;
    }
    push(st);
    kit::save_later(st->saver, st->ms);
}

void frame(void *state, uint32_t ms)
{
    State *st = static_cast<State *>(state);
    st->ms += ms;
    kit::audio_tick(P, st->audio, kPack, kPackVersion, PARAMS, st->ms);
    const uint8_t previous_input = st->lv.last_in;
    kit::levels_tick(P, st->lv, ms);
    kit::save_tick(P, st->saver, st->s, st->ms);
    ctl::sync(P, st->link, kBind, kBinds, st->ms);
    const uint32_t dt = ms < 100 ? ms : 100;
    react_to_input(st, previous_input, dt);
    animate(st, dt);
    draw(st);
}

const resona_app kApp = {
    RESONA_PLATFORM_VERSION, sizeof(resona_app),
    RESONA_APP_ID, RESONA_APP_NAME, RESONA_APP_VERSION,
    sizeof(State), 1024,
    init, event, frame,
};

}  // namespace

extern "C" const resona_app *resona_app_get(void) { return &kApp; }
