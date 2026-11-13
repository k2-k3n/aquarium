// Own the guitar route so a missing or faulted pack can fall back to dry guitar. MIT.
#pragma once
#include <resona/platform.h>
#include <resona/effects.hpp>

namespace own_audio {

struct Graph {
    resona::Effects fx;
    resona_effect_handle handle;
    bool owned, dry;
};

inline int route(Graph &g, resona_effect_handle h)
{
    g.fx.clear_routes();
    if (h) g.fx.connect(g.fx.Guitar, h);
    const uint32_t source = h ? h : g.fx.Guitar;
    g.fx.output(source);
    g.fx.capture(source);
    for (uint8_t ch = 0; ch < 2; ++ch) {
        g.fx.connect(g.fx.Usb, g.fx.Output, 1.0f, ch, ch);
        g.fx.connect(g.fx.Bluetooth, g.fx.Output, 1.0f, ch, ch);
    }
    return g.fx.commit();
}

inline int start(const resona_platform *P, Graph &g, const char *id, const char *version,
                 const float *values, uint32_t count)
{
    if (!g.owned) {
        const int rc = g.fx.begin(P);
        if (rc) return rc;
        g.owned = true;
    }
    if (!g.dry) {
        const int rc = route(g, 0);
        if (rc) return rc;
        g.dry = true;
    }

    g.fx.clear_error();
    const resona_effect_handle h = g.fx.pack(id, version);
    if (!h) return g.fx.error() ? g.fx.error() : RESONA_ERR_STATE;
    resona_effect_info info = {};
    int rc = g.fx.info(h, &info);
    if (!rc && info.param_count != count) rc = RESONA_ERR_STATE;
    for (uint32_t i = 0; !rc && i < count; ++i) rc = g.fx.param(h, i, values[i]);
    if (!rc) rc = g.fx.enable(h, true);
    if (!rc) rc = route(g, h);
    if (rc) {
        if (g.fx.destroy(h)) g.handle = h;
        return rc;
    }
    g.handle = h;
    g.dry = false;
    return RESONA_OK;
}

inline int stop(Graph &g)
{
    if (!g.dry) {
        const int rc = route(g, 0);
        if (rc) return rc;
        g.dry = true;
    }
    const int rc = g.fx.destroy(g.handle);
    if (!rc) g.handle = 0;
    return rc;
}

}  // namespace own_audio
