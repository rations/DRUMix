// panelrender — render the editor offline to a PNG.
//
// This is the panel's test rig: it paints through exactly the code the plug-in
// editor paints with (drawPanel and the Canvas/font/PNG stack under it), but
// with no X11, no host and no VST3 objects, so the layout and the art can be
// checked without loading anything into a DAW. A GUI change is reviewed from
// this plus a real host load, never from one alone.
//
// Usage: panelrender <output.png> [resource-dir]
// The resource directory defaults to the usual runtime resolution
// (DRUMIX_RESOURCE_DIR, the bundle layout, or an executable-relative dir).

#include "drumpanel.h"
#include "filebrowser.h"
#include "gfx/fontstack.h"
#include "gfx/image.h"
#include "platform/respath.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace DRUMix;

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <output.png> [resource-dir] [--browser]\n", argv[0]);
        return 2;
    }
    const char *output = argv[1];
    std::string resources;
    bool withBrowser = false;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--browser") == 0)
            withBrowser = true;
        else
            resources = argv[i];
    }
    if (resources.empty())
        resources = resourceDir();
    if (resources.empty())
        fprintf(stderr, "panelrender: no resource directory - rendering the flat fallback\n");

    FontStack fonts;
    fonts.load(resources);
    ImageCache images;
    images.setResourceDir(resources);

    // A representative state rather than the defaults: one pad selected, a
    // second armed for MIDI learn, a third mid-flash, so every layer of the
    // pad art and every strip state appears in one sheet.
    PanelState state;
    state.selectedPad = 1; // Snare
    state.armedSlot = geo::kPads[5].slot;
    state.hitLevel[0] = 1.0f;
    state.hitLevel[8] = 0.5f;
    state.pulsePhase = 4;
    state.sampleLoaded = true;
    state.sampleName = "snare_center_hard.wav";
    state.note = 38;
    state.volume = 0.8;

    cairo_surface_t *surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, geo::kWinW, geo::kWinH);
    cairo_t *cr = cairo_create(surface);
    {
        Canvas c(cr, &fonts, static_cast<float>(geo::kWinW), static_cast<float>(geo::kWinH));
        drawPanel(c, images, state);
        if (withBrowser) {
            FileBrowser browser;
            browser.open(resources, "wav", "Load sample — Snare");
            browser.draw(c);
        }
    }

    const cairo_status_t status = cairo_surface_write_to_png(surface, output);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    if (status != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "panelrender: could not write %s (%s)\n", output,
                cairo_status_to_string(status));
        return 1;
    }
    printf("wrote %s (%dx%d)\n", output, geo::kWinW, geo::kWinH);
    return 0;
}
