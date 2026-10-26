#!/usr/bin/env python3
# v171 : correctif d'Unicorn pour emu_game.py (un vrai jeu dans l'emulateur) : avec UC_NO_MEM_EXIT=1, pas d'arret
# juste apres un acces memoire. Sinon un arret (fin du temps donne a emu_start) remettait le PC sur l'instruction
# deja faite : lwz r5,0(r5) refait avec r5 deja charge (faux defaut de page), lecture d'un registre du materiel
# faite deux fois. Sans la variable, rien ne change pour les autres essais du banc.
# Sortie 1 = correctif pose (il faut recompiler), 0 = deja la.
import sys
p = sys.argv[1] + '/qemu/tcg/tcg-op.c'
s = open(p).read()
if 'check_exit_request_mem' in s: sys.exit(0)
anchor = 'static void tcg_gen_req_mo(TCGContext *tcg_ctx, TCGBar type)'
helper = '''static void check_exit_request_mem(TCGContext *tcg_ctx)
{
    static int off = -1;
    if (off < 0) {
        const char *e = getenv("UC_NO_MEM_EXIT");
        off = e && *e == '1';
    }
    if (!off) {
        check_exit_request(tcg_ctx);
    }
}

'''
s = s.replace(anchor, helper + anchor, 1)
i = s.index(helper) + len(helper)
s = s[:i] + s[i:].replace('check_exit_request(tcg_ctx);', 'check_exit_request_mem(tcg_ctx);')
open(p, 'w').write(s)
sys.exit(1)
