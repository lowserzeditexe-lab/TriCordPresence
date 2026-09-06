#pragma once
// Journal texte sur SD (sdmc:/3ds/tricord-presence/log.txt) : un sysmodule
// n'a aucune sortie visible, c'est le seul moyen de déboguer sur console.
void logInit(void);
void logPrintf(const char *fmt, ...);
