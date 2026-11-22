/*
 * hpcos_options.c -- Harry Potter and the Chamber of Secrets's rows in the overlay
 * menu (overlay_options.h). Built from the NFSU2 port's nfsu2_options.c. Only
 * the host's and the Metal renderer's settings: this game's own Options screen
 * is left as it is. Every row is kept in settings.ini.
 *
 * Live: everything but Frame Rate, Picture, Keyboard as Controller and Skip
 * Logo Movies, which are read at start.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "overlay_options.h"

#ifndef NFSU2_PORT_VERSION
#define NFSU2_PORT_VERSION "dev"
#endif
#ifndef NFSU2_PORT_COMMIT
#define NFSU2_PORT_COMMIT "local"
#endif

/* The renderer's and the runtime's switches. */
void nv2a_mtl_post_reload(void);
void xa2_set_master_volume(float v);
extern volatile int nv2a_mtl_fps_overlay;
extern volatile int nv2a_vk_scale_pct;
extern volatile int nv2a_vk_fxaa;
extern volatile int nv2a_vk_aniso;
extern volatile int usb_gamepad_rumble_pct;

/* An environment variable the game reads: at start only where nothing set it
 * by hand (a variable set by hand wins for that run); on a change, always. */
static void env(const char *k, const char *v, int at_start)
{
    setenv(k, v, !at_start);
}

static const char *const k_onoff[] = { "Off", "On" };
static const char *const k_01[] = { "0", "1" };

/* Display */
static const char *const k_display[] = { "Window", "Full Screen" };
static void apply_display(int c, int at_start)
{
    if (at_start)
        env("NFSU2_FULLSCREEN", c ? "1" : "0", 1);
    else
        overlay_request(c ? OVERLAY_REQ_FULLSCREEN_ON : OVERLAY_REQ_FULLSCREEN_OFF);
}

static const char *const k_scale[] = { "1x (640 x 480)", "1.5x (960 x 720)", "2x (1280 x 960)",
                                       "3x (1920 x 1440)", "4x (2560 x 1920)" };
static const char *const k_scale_v[] = { "100", "150", "200", "300", "400" };
static void apply_scale(int c, int at_start)
{
    (void)at_start;
    nv2a_vk_scale_pct = atoi(k_scale_v[c]);   /* the renderer takes it at the next flip */
}

static const char *const k_picture[] = { "Fit (4:3, bars at the sides)", "Fill (stretch to the window)" };
static const char *const k_picture_v[] = { "fit", "fill" };
static void apply_picture(int c, int at_start) { env("NFSU2_SCREEN", k_picture_v[c], at_start); }

static const char *const k_fps[] = { "Original", "60 fps" };
static const char *const k_fps_v[] = { "title", "60" };
static void apply_fps(int c, int at_start) { env("HPCOS_FPS", k_fps_v[c], at_start); }

/* Graphics */
static void apply_fxaa(int c, int at_start) { (void)at_start; nv2a_vk_fxaa = c; }
static const char *const k_aniso[] = { "Off", "2x", "4x", "8x", "16x" };
static const char *const k_aniso_v[] = { "1", "2", "4", "8", "16" };
static void apply_aniso(int c, int at_start) { (void)at_start; nv2a_vk_aniso = atoi(k_aniso_v[c]); }

static const char *const k_gamma[] = { "Original", "Modern (brighter)", "Warm" };
static const char *const k_gamma_v[] = { "original", "modern", "warm" };
static void apply_gamma(int c, int at_start)
{
    env("NFSU2_GAMMA", k_gamma_v[c], at_start);
    if (!at_start)
        nv2a_mtl_post_reload();
}

/* Audio */
static const char *const k_pct[] = { "0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%" };
static const char *const k_pct_v[] = { "0", "10", "20", "30", "40", "50", "60", "70", "80", "90", "100" };
static void apply_volume(int c, int at_start)
{
    (void)at_start;
    xa2_set_master_volume((float)c / 10.0f);
}

/* Input */
static const char *const k_vib[] = { "Off", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%" };
static void apply_vib(int c, int at_start) { (void)at_start; usb_gamepad_rumble_pct = c * 10; }

static const char *const k_kbd[] = { "Without a controller", "Always", "Off" };
static const char *const k_kbd_v[] = { "1", "2", "0" };
static void apply_kbd(int c, int at_start) { env("NFSU2_KEYBOARD", k_kbd_v[c], at_start); }

extern void nv2a_mtl_post_reload(void);
static const char *const k_fx[] = { "Bloom", "Bloom + Ambient Occlusion", "Off" };
static const char *const k_fx_v[] = { "bloom", "ssao", "off" };
static void apply_fx(int c, int at_start)
{
    env("NFSU2_POST_MIN_DRAWS", "40", at_start);   /* TODO(CoS): tune to its scenes' draw counts */
    env("NFSU2_AO", c == 1 ? "1" : "0", at_start);
    env("NFSU2_BLOOM", c == 2 ? "0" : "0.35", at_start);
    if (!at_start)
        nv2a_mtl_post_reload();
}

/* Tweaks */
static void apply_skip_logos(int c, int at_start) { env("HPCOS_SKIP_LOGOS", c ? "1" : "0", at_start); }
/* read by recomp_manual.c at every hold, so a change applies at once */
static void apply_skip_cutscenes(int c, int at_start) { env("HPCOS_SKIP_CUTSCENES", c ? "1" : "0", at_start); }

/* Advanced */
static void apply_fps_counter(int c, int at_start)
{
    (void)at_start;
    nv2a_mtl_fps_overlay = c;
}
static void open_logs(void) { overlay_request(OVERLAY_REQ_OPEN_LOGS); }

#define CHOICES(a) a, (int)(sizeof a / sizeof a[0])

static const OverlayOption k_options[] = {
    { "Display", "Display", "Full screen over the whole panel, or a window. F11 switches it for this run.",
      "The window: at once.",
      CHOICES(k_display), 1, 0, "fullscreen", k_01, apply_display },
    { "Display", "Resolution Scale", "How many pixels the game draws: more is sharper and costs the GPU.",
      "Everything the game draws, from the next frame. Movies keep their own size.",
      CHOICES(k_scale), 3, 0, "scale", k_scale_v, apply_scale },
    { "Display", "Picture", "The Xbox's 4:3 picture with bars at the sides, or stretched over the window.",
      "The whole picture: from the next start.",
      CHOICES(k_picture), 0, 1, "picture", k_picture_v, apply_picture },
    { "Display", "Frame Rate", "The game's own pace, or every frame at 60 fps where the game can.",
      "Gameplay and menus: from the next start.",
      CHOICES(k_fps), 0, 1, "fps", k_fps_v, apply_fps },

    { "Graphics", "Anti-Aliasing", "FXAA over the finished picture: smoother edges.",
      "The whole picture, menus too: at once.",
      CHOICES(k_onoff), 1, 0, "fxaa", k_01, apply_fxaa },
    { "Graphics", "Anisotropic Filtering", "Sharper floors and walls at an angle.",
      "Floors, walls and the ground seen at an angle: at once.",
      CHOICES(k_aniso), 4, 0, "aniso", k_aniso_v, apply_aniso },
    { "Graphics", "Effects", "Bloom on bright lights; ambient occlusion darkens corners and contacts "
      "(costs GPU time at high scales).", "3D scenes: at once. Not the menus, books or movies.",
      CHOICES(k_fx), 0, 0, "effects", k_fx_v, apply_fx },
    { "Graphics", "Gamma", "The picture's brightness curve: the Xbox's, a brighter one for modern "
      "displays, or a warmer one.", "The whole picture, menus and movies too: at once.",
      CHOICES(k_gamma), 0, 0, "gamma", k_gamma_v, apply_gamma },

    { "Audio", "Master Volume", "The whole game's sound, before the Mac's volume. The game's own "
      "sliders are in its Options.", "All the game's sound (music, speech, effects): at once.",
      CHOICES(k_pct), 10, 0, "volume", k_pct_v, apply_volume },

    { "Input", "Vibration", "Controller rumble strength.",
      "The controller's rumble: at once.",
      CHOICES(k_vib), 10, 0, "vibration", k_pct_v, apply_vib },
    { "Input", "Keyboard as Controller", "Arrows/WASD move, Enter is A and Start, Esc is B, P is Start.",
      "The keyboard in the game (not in this menu): from the next start.",
      CHOICES(k_kbd), 0, 1, "keyboard", k_kbd_v, apply_kbd },

    { "Tweaks", "Skip Logo Movies", "Start without the EA and Warner Bros. logo movies.",
      "The logo movies before the title screen: from the next start.",
      CHOICES(k_onoff), 0, 1, "skip_logos", k_01, apply_skip_logos },
    { "Tweaks", "Cutscene Fast-Forward", "Hold Start or Back for half a second in a cutscene: it plays "
      "to its end about 8 times as fast, muted. Nothing is left out.", "Every cutscene: at once.",
      CHOICES(k_onoff), 1, 0, "skip_cutscenes", k_01, apply_skip_cutscenes },

    { "Advanced", "Performance Overlay", "Frames per second and the slowest frame, top left "
      "(also Cmd+Option+F).", "Top left of the screen: at once.",
      CHOICES(k_onoff), 0, 0, "fps_counter", k_01, apply_fps_counter },
    { "Advanced", "Logs", "The game's and the installer's logs, in Finder (for a bug report).",
      "Opens a Finder window.",
      NULL, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, open_logs, "Open" },
};

const OverlayOption *overlay_game_options(int *count)
{
    *count = (int)(sizeof k_options / sizeof k_options[0]);
    return k_options;
}

const char *overlay_game_title(void)
{
    return "Harry Potter and the Chamber of Secrets";
}

const char *const *overlay_game_about(void)
{
    static const char *const lines[] = {
        "Harry Potter and the Chamber of Secrets for Mac v" NFSU2_PORT_VERSION " (" NFSU2_PORT_COMMIT "), built " __DATE__ ", macOS arm64, Metal",
        "Free software under the GPL-3.0-or-later, with no warranty. Built on xboxrecomp (sp00nz), nfsu2-sw (antoxa2584x),",
        "nfsu2-ios (myUdav4iik), HP-COS-Xbox-Recomp (MiddleDucky): see NOTICE.",
        "Not affiliated with Warner Bros., Electronic Arts or Microsoft. Your own disc, built on your Mac.",
        NULL,
    };
    return lines;
}
