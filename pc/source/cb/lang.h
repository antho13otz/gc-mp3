/* lang.h - GC Station : langue de Cube Browser (Reglages > Affichage > Langue).
 * tr("texte anglais") rend le texte francais (Latin-1) quand la langue est le francais. */
#pragma once
const char *tr(const char *en) __attribute__((format_arg(1)));
