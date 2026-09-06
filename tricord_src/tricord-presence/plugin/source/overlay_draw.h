#pragma once

// Dessin du HUD par-dessus le rendu du jeu hôte.
//
// Implémentation : CTRPluginFramework OSD. OSD::Run(cb) enregistre un
// callback appelé par CTRPF juste avant chaque swap de framebuffer du jeu
// (hook MITM posé par OSDImpl::Update sur la routine GSPGPU_SetBufferSwap /
// gsp interne du jeu, cf Library/source/CTRPluginFrameworkImpl/Graphics/
// OSDImpl.cpp). Le callback reçoit un objet Screen donnant accès direct au
// framebuffer (Screen::Draw / DrawRect) : c'est le mécanisme d'overlay
// "safe" utilisé par tous les plugins CTRPF (notifications OSD::Notify).
//
// TODO(hardware): comportement à valider sur console avec un jeu 3D
// stéréoscopique actif (CTRPF ne dessine que sur le framebuffer gauche par
// défaut) et un jeu en 800px "wide mode".

#ifdef __cplusplus
extern "C" {
#endif

void overlayInit(void);
void overlayShowToast(const char *title, const char *body);
void overlayRenderFrame(void); // conservé pour compat : no-op (rendu par callback OSD)
void overlayExit(void);

#ifdef __cplusplus
}
#endif
