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
extern volatile unsigned pcsim_frame_us;   /* duree d'une image de l'ecran : 16667 (60 Hz) ou 20000 (50 Hz, PAL) */
extern double pcsim_cpu_k;
extern __thread unsigned long long pcsim_gx_ns;   /* temps du dessin logiciel (pas compte au processeur) */
const char *pcsim_script_where(void);   /* derniere commande du scenario (journal) */
void pcsim_yield_now(void);
extern __thread volatile int pcsim_in_lib, pcsim_preempt_pending;   /* (preemption : pas au milieu du simulateur) */
#define PCSIM_LIB_ENTER() (pcsim_in_lib++)
#define PCSIM_LIB_EXIT() do { if (--pcsim_in_lib == 0 && pcsim_preempt_pending) { pcsim_preempt_pending = 0; pcsim_yield_now(); } } while (0)
#endif
