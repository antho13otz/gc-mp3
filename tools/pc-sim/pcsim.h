/* GC Station sur PC : fonctions internes du simulateur */
#ifndef PCSIM_H
#define PCSIM_H
void pcsim_lock(void);
void pcsim_unlock(void);
void pcsim_finish(int code);
extern int pcsim_capture;                 /* 1 : dessiner et enregistrer l'image en cours */
extern char pcsim_capture_name[96];
void pcsim_limits_init(void);
void pcsim_limits_report(void);
size_t pcsim_heap_left(void);
int pcsim_net_allow(int want);
void pcsim_net_used(int n);
extern int pcsim_fast;
extern double pcsim_cpu_k;
extern __thread unsigned long long pcsim_gx_ns;   /* temps du dessin logiciel (pas compte au processeur) */
const char *pcsim_script_where(void);   /* derniere commande du scenario (journal) */
#endif
