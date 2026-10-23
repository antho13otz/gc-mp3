/* GC Station sur PC : fonctions internes du simulateur */
#ifndef PCSIM_H
#define PCSIM_H
void pcsim_lock(void);
void pcsim_unlock(void);
void pcsim_finish(int code);
extern int pcsim_capture;                 /* 1 : dessiner et enregistrer l'image en cours */
extern char pcsim_capture_name[96];
#endif
