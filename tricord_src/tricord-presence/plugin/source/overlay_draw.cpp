#include "overlay_draw.h"
#include <CTRPluginFramework.hpp>
#include <string.h>
#include <stdio.h>

using namespace CTRPluginFramework;

// Durée d'affichage du toast et animation d'entrée (glissement depuis le haut).
static const Time kToastDuration = Seconds(4);
static const Time kSlideDuration = Milliseconds(250);

static char s_title[32];
static char s_body[64];
static volatile bool s_visible = false;
static Clock s_shownAt;

// Callback OSD : appelé par CTRPF à chaque frame, pour chaque écran
// (top puis bottom). Retourner true = "j'ai dessiné" (CTRPF force alors le
// flush du framebuffer). On ne dessine que sur l'écran du haut.
static bool drawToast(const Screen &screen) {
    if (!s_visible || !screen.IsTop) return false;

    Time elapsed = s_shownAt.GetElapsedTime();
    if (elapsed >= kToastDuration) { s_visible = false; return false; }

    // Glissement : y va de -h à 6 px pendant kSlideDuration
    const int h = 30;
    int y = 6;
    if (elapsed < kSlideDuration)
        y = -h + (int)((h + 6) * elapsed.AsSeconds() / kSlideDuration.AsSeconds());
    if (y < 0) y = 0;

    // Largeur : police Linux 6x10 de Screen::Draw -> 6 px par caractère
    size_t chars = strlen(s_title) > strlen(s_body) ? strlen(s_title) : strlen(s_body);
    int w = (int)chars * 6 + 16;
    if (w > 392) w = 392;
    int x = (400 - w) / 2;

    screen.DrawRect(x, y, w, h, Color(20, 22, 30, 220));          // fond
    screen.DrawRect(x, y, 3, h, Color(88, 101, 242));            // liseré "blurple"
    screen.Draw(s_title, x + 8, y + 4, Color(180, 184, 200), Color(0, 0, 0, 0));
    screen.Draw(s_body,  x + 8, y + 16, Color::White, Color(0, 0, 0, 0));
    return true;
}

extern "C" void overlayInit(void) {
    OSD::Run(drawToast);
}

// La police 6x10 de Screen::Draw est ASCII : on remplace les séquences UTF-8
// (noms japonais, accents) par '?' pour éviter des glyphes aléatoires.
static void copyAscii(char *dst, size_t dstSize, const char *src) {
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p && o + 1 < dstSize; p++) {
        if (*p < 0x80) dst[o++] = (char)*p;
        else if ((*p & 0xC0) != 0x80) dst[o++] = '?'; // 1er octet d'une séquence multi-octets
    }
    dst[o] = '\0';
}

extern "C" void overlayShowToast(const char *title, const char *body) {
    copyAscii(s_title, sizeof(s_title), title);
    copyAscii(s_body, sizeof(s_body), body);
    s_shownAt.Restart();   // (re)démarre le minuteur de disparition
    s_visible = true;
}

extern "C" void overlayRenderFrame(void) {
    // Le rendu est piloté par le hook GSP de CTRPF (drawToast) — rien ici.
}

extern "C" void overlayExit(void) {
    s_visible = false;
    OSD::Stop(drawToast);
}
