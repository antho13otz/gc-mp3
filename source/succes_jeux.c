/* GC Station v268 : succes dans les jeux GameCube (inclus dans main.c, avant natif.c).
   Un fichier par jeu : SD:/gc-mp3/succes/<ID>.txt, une ligne par succes :
       Titre | Description | condition [| images]
   condition : m8(adresse), m16(adresse) ou m32(adresse) compare a une valeur (== != >= <= > <), plusieurs reunies
   par && ; pointeur : m16(p32(0x80xxxxxx)+0x10) ; v389 : m8(adresse) & 0x04 = ces bits a 1 (drapeaux) ; images : la condition doit rester vraie N images de suite (30 par
   defaut). Pulse verifie les conditions une fois par image pendant le jeu (natres.c, ach_run) ; un succes n'est
   obtenu qu'apres avoir vu sa condition fausse au moins une fois (pas d'un coup au demarrage).
   Quelques jeux ont des succes fournis (sj_builtin) : ecrits d'apres des adresses connues des codes de triche,
   PAS VERIFIES SUR CONSOLE ; le fichier est cree sur la carte au 1er lancement du jeu et peut etre modifie.
   Obtenus : mes-parties/succes-jeux.txt (ou profils/<nom>/succes-jeux.txt) : "ID|titre|date". */
#define SJ_DIR    GCD "/succes"
#define SJ_LANCE  GCD "/natif/succes-lance.txt"           /* (au lancement : profil, jeu, titres dans l'ordre de Pulse) */
#define SJ_MAX    48
#define SJ_TERMS  8
typedef struct { char title[48], desc[96]; u32 need; int nt; u32 t[SJ_TERMS][4]; } sj_def_t;

static const char sj_head[] =
    "# Succes GC Station pour ce jeu. Une ligne par succes :  Titre | Description | condition [| images]\n"
    "# condition : m8(adresse), m16(adresse) ou m32(adresse) compare (== != >= <= > <) a une valeur ; plusieurs : &&\n"
    "#   pointeur : m16(p32(0x80xxxxxx)+0x10) ; bits a 1 : m8(adresse) & 0x04 ; images : vraie N images de suite (30 par defaut, 60 = 1 s)\n"
    "# Pulse les verifie pendant le jeu ; un succes n'est obtenu qu'apres avoir vu sa condition fausse une fois.\n";
static const struct { const char *id, *text; } sj_builtin[] = {
    { "GZLP01",
      "# The Legend of Zelda : The Wind Waker (PAL). Adresses du compagnon de l'appli (dSv_info_c en 0x803CC530), v389.\n"
      "Coeur vaillant | Avoir 5 coeurs | m16(0x803CC530) >= 20 && m16(0x803CC530) <= 80\n"
      "Coeur de heros | Avoir 10 coeurs | m16(0x803CC530) >= 40 && m16(0x803CC530) <= 80\n"
      "Coeur legendaire | Avoir 20 coeurs | m16(0x803CC530) == 80\n"
      "Bourse bien garnie | Avoir 200 rubis | m16(0x803CC534) >= 200 && m16(0x803CC534) <= 5000\n"
      "Fortune | Avoir 1000 rubis | m16(0x803CC534) >= 1000 && m16(0x803CC534) <= 5000\n"
      "Tresor de pirate | Avoir 5000 rubis | m16(0x803CC534) == 5000\n"
      "Grand portefeuille | Obtenir la bourse de 1000 rubis | m8(0x803CC542) >= 1 && m8(0x803CC542) <= 2\n"
      "Portefeuille geant | Obtenir la bourse de 5000 rubis | m8(0x803CC542) == 2\n"
      "Magicien | Obtenir la magie | m8(0x803CC543) >= 16 && m8(0x803CC543) <= 32\n"
      "Double magie | Doubler sa jauge de magie | m8(0x803CC543) == 32\n"
      "Perle de Nayru | Obtenir la perle de Nayru | m8(0x803CC5EF) & 0x01\n"
      "Perle de Din | Obtenir la perle de Din | m8(0x803CC5EF) & 0x02\n"
      "Perle de Farore | Obtenir la perle de Farore | m8(0x803CC5EF) & 0x04\n"
      "Les trois perles | Reunir les trois perles des deesses | m8(0x803CC5EF) & 0x07\n"
      "Premier chant | Apprendre un chant de la Baguette du Vent | m8(0x803CC5ED) != 0 && m8(0x803CC5ED) <= 0x3F\n"
      "Chef d'orchestre | Apprendre les 6 chants | m8(0x803CC5ED) & 0x3F\n"
      "Fragment de Triforce | Trouver un fragment de la Triforce | m8(0x803CC5EE) != 0\n"
      "Triforce du courage | Reunir les 8 fragments de la Triforce | m8(0x803CC5EE) == 0xFF\n"
      "Epee de legende | Rendre toute sa puissance a l'Epee de legende | m8(0x803CC53E) == 0x3E\n"
      "Bouclier miroir | Obtenir le Bouclier miroir | m8(0x803CC53F) == 0x3C\n"
      "Force herculeenne | Obtenir les Bracelets de force | m8(0x803CC540) == 0x28\n"
      "Carquois de 99 | Pouvoir porter 99 fleches | m8(0x803CC59F) == 99\n"
      "Artificier | Pouvoir porter 99 bombes | m8(0x803CC5A0) == 99\n"},
    { "GZLE01",
      "# The Legend of Zelda : The Wind Waker (USA). Adresses du compagnon de l'appli (dSv_info_c en 0x803C4C08), v389.\n"
      "Coeur vaillant | Avoir 5 coeurs | m16(0x803C4C08) >= 20 && m16(0x803C4C08) <= 80\n"
      "Coeur de heros | Avoir 10 coeurs | m16(0x803C4C08) >= 40 && m16(0x803C4C08) <= 80\n"
      "Coeur legendaire | Avoir 20 coeurs | m16(0x803C4C08) == 80\n"
      "Bourse bien garnie | Avoir 200 rubis | m16(0x803C4C0C) >= 200 && m16(0x803C4C0C) <= 5000\n"
      "Fortune | Avoir 1000 rubis | m16(0x803C4C0C) >= 1000 && m16(0x803C4C0C) <= 5000\n"
      "Tresor de pirate | Avoir 5000 rubis | m16(0x803C4C0C) == 5000\n"
      "Grand portefeuille | Obtenir la bourse de 1000 rubis | m8(0x803C4C1A) >= 1 && m8(0x803C4C1A) <= 2\n"
      "Portefeuille geant | Obtenir la bourse de 5000 rubis | m8(0x803C4C1A) == 2\n"
      "Magicien | Obtenir la magie | m8(0x803C4C1B) >= 16 && m8(0x803C4C1B) <= 32\n"
      "Double magie | Doubler sa jauge de magie | m8(0x803C4C1B) == 32\n"
      "Perle de Nayru | Obtenir la perle de Nayru | m8(0x803C4CC7) & 0x01\n"
      "Perle de Din | Obtenir la perle de Din | m8(0x803C4CC7) & 0x02\n"
      "Perle de Farore | Obtenir la perle de Farore | m8(0x803C4CC7) & 0x04\n"
      "Les trois perles | Reunir les trois perles des deesses | m8(0x803C4CC7) & 0x07\n"
      "Premier chant | Apprendre un chant de la Baguette du Vent | m8(0x803C4CC5) != 0 && m8(0x803C4CC5) <= 0x3F\n"
      "Chef d'orchestre | Apprendre les 6 chants | m8(0x803C4CC5) & 0x3F\n"
      "Fragment de Triforce | Trouver un fragment de la Triforce | m8(0x803C4CC6) != 0\n"
      "Triforce du courage | Reunir les 8 fragments de la Triforce | m8(0x803C4CC6) == 0xFF\n"
      "Epee de legende | Rendre toute sa puissance a l'Epee de legende | m8(0x803C4C16) == 0x3E\n"
      "Bouclier miroir | Obtenir le Bouclier miroir | m8(0x803C4C17) == 0x3C\n"
      "Force herculeenne | Obtenir les Bracelets de force | m8(0x803C4C18) == 0x28\n"
      "Carquois de 99 | Pouvoir porter 99 fleches | m8(0x803C4C77) == 99\n"
      "Artificier | Pouvoir porter 99 bombes | m8(0x803C4C78) == 99\n"},
    { "GZ2P01",
      "# The Legend of Zelda : Twilight Princess (PAL). Adresses du compagnon de l'appli (g_dComIfG_gameInfo en 0x80408160), v389.\n"
      "Coeur solide | Avoir 5 coeurs | m16(0x80408160) >= 25 && m16(0x80408160) <= 100\n"
      "Coeur de heros | Avoir 10 coeurs | m16(0x80408160) >= 50 && m16(0x80408160) <= 100\n"
      "Coeur legendaire | Avoir 20 coeurs | m16(0x80408160) == 100\n"
      "Economies | Avoir 300 rubis | m16(0x80408164) >= 300 && m16(0x80408164) <= 1000\n"
      "Fortune | Avoir 1000 rubis | m16(0x80408164) == 1000\n"
      "Bourse moyenne | Obtenir la bourse de 600 rubis | m8(0x80408179) >= 1 && m8(0x80408179) <= 2\n"
      "Grande bourse | Obtenir la bourse de 1000 rubis | m8(0x80408179) == 2\n"
      "Premiere ombre | Trouver une Ombre fusionnee | m8(0x80408269) != 0 && m8(0x80408269) <= 7\n"
      "Ombres fusionnees | Reunir les 3 Ombres fusionnees | m8(0x80408269) & 0x07\n"
      "Eclat du miroir | Trouver un eclat du Miroir des Ombres | m8(0x8040826A) != 0 && m8(0x8040826A) <= 15\n"
      "Miroir des Ombres | Reunir les 4 eclats du miroir | m8(0x8040826A) & 0x0F\n"
      "Chasseur d'esprits | Recuperer 10 ames d'Esprits | m8(0x8040826C) >= 10 && m8(0x8040826C) <= 60\n"
      "Exorciste | Recuperer 30 ames d'Esprits | m8(0x8040826C) >= 30 && m8(0x8040826C) <= 60\n"
      "Ame en paix | Recuperer les 60 ames d'Esprits | m8(0x8040826C) == 60\n"
      "Pecheur | Pecher un premier poisson | m16(0x804082CC) >= 1 && m16(0x804082CC) <= 9999\n"},
    { "GZ2E01",
      "# The Legend of Zelda : Twilight Princess (USA). Adresses du compagnon de l'appli (g_dComIfG_gameInfo en 0x804061C0), v389.\n"
      "Coeur solide | Avoir 5 coeurs | m16(0x804061C0) >= 25 && m16(0x804061C0) <= 100\n"
      "Coeur de heros | Avoir 10 coeurs | m16(0x804061C0) >= 50 && m16(0x804061C0) <= 100\n"
      "Coeur legendaire | Avoir 20 coeurs | m16(0x804061C0) == 100\n"
      "Economies | Avoir 300 rubis | m16(0x804061C4) >= 300 && m16(0x804061C4) <= 1000\n"
      "Fortune | Avoir 1000 rubis | m16(0x804061C4) == 1000\n"
      "Bourse moyenne | Obtenir la bourse de 600 rubis | m8(0x804061D9) >= 1 && m8(0x804061D9) <= 2\n"
      "Grande bourse | Obtenir la bourse de 1000 rubis | m8(0x804061D9) == 2\n"
      "Premiere ombre | Trouver une Ombre fusionnee | m8(0x804062C9) != 0 && m8(0x804062C9) <= 7\n"
      "Ombres fusionnees | Reunir les 3 Ombres fusionnees | m8(0x804062C9) & 0x07\n"
      "Eclat du miroir | Trouver un eclat du Miroir des Ombres | m8(0x804062CA) != 0 && m8(0x804062CA) <= 15\n"
      "Miroir des Ombres | Reunir les 4 eclats du miroir | m8(0x804062CA) & 0x0F\n"
      "Chasseur d'esprits | Recuperer 10 ames d'Esprits | m8(0x804062CC) >= 10 && m8(0x804062CC) <= 60\n"
      "Exorciste | Recuperer 30 ames d'Esprits | m8(0x804062CC) >= 30 && m8(0x804062CC) <= 60\n"
      "Ame en paix | Recuperer les 60 ames d'Esprits | m8(0x804062CC) == 60\n"
      "Pecheur | Pecher un premier poisson | m16(0x8040632C) >= 1 && m16(0x8040632C) <= 9999\n"},
    { "GPIP01",
      "# Pikmin (PAL). Adresses du compagnon de l'appli, v389.\n"
      "Premiere piece | Retrouver une piece du vaisseau | m32(p32(0x803ECB4C)+0x17C) >= 1 && m32(p32(0x803ECB4C)+0x17C) <= 30\n"
      "Mecanicien | Retrouver 10 pieces du vaisseau | m32(p32(0x803ECB4C)+0x17C) >= 10 && m32(p32(0x803ECB4C)+0x17C) <= 30\n"
      "Presque pret | Retrouver 25 pieces du vaisseau | m32(p32(0x803ECB4C)+0x17C) >= 25 && m32(p32(0x803ECB4C)+0x17C) <= 30\n"
      "Retour sur Hocotate | Retrouver les 30 pieces du vaisseau | m32(p32(0x803ECB4C)+0x17C) == 30\n"
      "Pepiniere | 100 Pikmin nes | m32(p32(0x803ECB4C)+0x1A4) >= 100 && m32(p32(0x803ECB4C)+0x1A4) <= 999999\n"
      "Grande famille | 1000 Pikmin nes | m32(p32(0x803ECB4C)+0x1A4) >= 1000 && m32(p32(0x803ECB4C)+0x1A4) <= 999999\n"
      "Armee rouge | 50 Pikmin rouges sur la carte | m32(0x803D6D48) >= 50 && m32(0x803D6D48) <= 9999\n"
      "Grande troupe | 100 Pikmin bleus sur la carte | m32(0x803D6D44) >= 100 && m32(0x803D6D44) <= 9999\n"},
    { "GPVP01",
      "# Pikmin 2 (PAL). Adresses du compagnon de l'appli, v389.\n"
      "Premier tresor | Ramasser un tresor | m32(p32(0x80516810)+0xBC) >= 1 && m32(p32(0x80516810)+0xBC) <= 999\n"
      "Collectionneur | Ramasser 50 tresors | m32(p32(0x80516810)+0xBC) >= 50 && m32(p32(0x80516810)+0xBC) <= 999\n"
      "Antiquaire | Ramasser 100 tresors | m32(p32(0x80516810)+0xBC) >= 100 && m32(p32(0x80516810)+0xBC) <= 999\n"
      "Tout le tresor | Ramasser les 201 tresors | m32(p32(0x80516810)+0xBC) >= 201 && m32(p32(0x80516810)+0xBC) <= 999\n"
      "Premiers Pokos | Avoir 1000 Pokos | m32(p32(0x80516810)+0xE8) >= 1000 && m32(p32(0x80516810)+0xE8) <= 9999999\n"
      "Dette remboursee | Avoir 10100 Pokos (la dette de Hocotate Fret) | m32(p32(0x80516810)+0xE8) >= 10100 && m32(p32(0x80516810)+0xE8) <= 9999999\n"
      "Magnat | Avoir 30000 Pokos | m32(p32(0x80516810)+0xE8) >= 30000 && m32(p32(0x80516810)+0xE8) <= 9999999\n"},
    { "GPVE01",
      "# Pikmin 2 (USA). Adresses du compagnon de l'appli, v389.\n"
      "Premier tresor | Ramasser un tresor | m32(p32(0x80515B10)+0xBC) >= 1 && m32(p32(0x80515B10)+0xBC) <= 999\n"
      "Collectionneur | Ramasser 50 tresors | m32(p32(0x80515B10)+0xBC) >= 50 && m32(p32(0x80515B10)+0xBC) <= 999\n"
      "Antiquaire | Ramasser 100 tresors | m32(p32(0x80515B10)+0xBC) >= 100 && m32(p32(0x80515B10)+0xBC) <= 999\n"
      "Tout le tresor | Ramasser les 201 tresors | m32(p32(0x80515B10)+0xBC) >= 201 && m32(p32(0x80515B10)+0xBC) <= 999\n"
      "Premiers Pokos | Avoir 1000 Pokos | m32(p32(0x80515B10)+0xE8) >= 1000 && m32(p32(0x80515B10)+0xE8) <= 9999999\n"
      "Dette remboursee | Avoir 10100 Pokos (la dette de Hocotate Fret) | m32(p32(0x80515B10)+0xE8) >= 10100 && m32(p32(0x80515B10)+0xE8) <= 9999999\n"
      "Magnat | Avoir 30000 Pokos | m32(p32(0x80515B10)+0xE8) >= 30000 && m32(p32(0x80515B10)+0xE8) <= 9999999\n"},
    { "GMPE01",
      "# Mario Party 4 (USA). Adresses des codes de triche connus : PAS ENCORE VERIFIE SUR CONSOLE.\n"
      "Tirelire | Joueur 1 : avoir 50 pieces | m16(0x8018FC54) >= 50\n"
      "Premiere etoile | Joueur 1 : obtenir une etoile | m16(0x8018FC62) >= 1 && m16(0x8018FC62) <= 99\n"
      "Superstar | Joueur 1 : avoir 5 etoiles | m16(0x8018FC62) >= 5 && m16(0x8018FC62) <= 99\n" },
    { "GMSP01",
      "# Super Mario Sunshine (PAL). Adresses du compagnon de l'appli (TFlagManager en 0x80405828), v389.\n"
      "Premier Soleil | Recuperer un Soleil | m32(p32(0x80405828)+0xD0) >= 1 && m32(p32(0x80405828)+0xD0) <= 120\n"
      "Dix Soleils | Recuperer 10 Soleils | m32(p32(0x80405828)+0xD0) >= 10 && m32(p32(0x80405828)+0xD0) <= 120\n"
      "Isle Delfino s'eclaire | Recuperer 30 Soleils | m32(p32(0x80405828)+0xD0) >= 30 && m32(p32(0x80405828)+0xD0) <= 120\n"
      "Soleil radieux | Recuperer 50 Soleils | m32(p32(0x80405828)+0xD0) >= 50 && m32(p32(0x80405828)+0xD0) <= 120\n"
      "Grand nettoyage | Recuperer 70 Soleils | m32(p32(0x80405828)+0xD0) >= 70 && m32(p32(0x80405828)+0xD0) <= 120\n"
      "Centenaire | Recuperer 100 Soleils | m32(p32(0x80405828)+0xD0) >= 100 && m32(p32(0x80405828)+0xD0) <= 120\n"
      "Isle Delfino brille | Recuperer les 120 Soleils | m32(p32(0x80405828)+0xD0) == 120\n"
      "Piece bleue | Trouver une piece bleue | m32(p32(0x80405828)+0xD4) >= 1 && m32(p32(0x80405828)+0xD4) <= 240\n"
      "Chasseur de bleues | Trouver 50 pieces bleues | m32(p32(0x80405828)+0xD4) >= 50 && m32(p32(0x80405828)+0xD4) <= 240\n"
      "Collection bleue | Trouver 100 pieces bleues | m32(p32(0x80405828)+0xD4) >= 100 && m32(p32(0x80405828)+0xD4) <= 240\n"
      "Toutes les bleues | Trouver les 240 pieces bleues | m32(p32(0x80405828)+0xD4) == 240\n"
      "Increvable | Avoir 30 vies | m32(p32(0x80405828)+0x7C) >= 30 && m32(p32(0x80405828)+0x7C) <= 99\n"
      "Chat a neuf vies | Avoir 99 vies | m32(p32(0x80405828)+0x7C) == 99\n"
    },
    { "GMSE01",
      "# Super Mario Sunshine (USA). Adresses du compagnon de l'appli (TFlagManager en 0x8040E160), v389.\n"
      "Premier Soleil | Recuperer un Soleil | m32(p32(0x8040E160)+0xD0) >= 1 && m32(p32(0x8040E160)+0xD0) <= 120\n"
      "Dix Soleils | Recuperer 10 Soleils | m32(p32(0x8040E160)+0xD0) >= 10 && m32(p32(0x8040E160)+0xD0) <= 120\n"
      "Isle Delfino s'eclaire | Recuperer 30 Soleils | m32(p32(0x8040E160)+0xD0) >= 30 && m32(p32(0x8040E160)+0xD0) <= 120\n"
      "Soleil radieux | Recuperer 50 Soleils | m32(p32(0x8040E160)+0xD0) >= 50 && m32(p32(0x8040E160)+0xD0) <= 120\n"
      "Grand nettoyage | Recuperer 70 Soleils | m32(p32(0x8040E160)+0xD0) >= 70 && m32(p32(0x8040E160)+0xD0) <= 120\n"
      "Centenaire | Recuperer 100 Soleils | m32(p32(0x8040E160)+0xD0) >= 100 && m32(p32(0x8040E160)+0xD0) <= 120\n"
      "Isle Delfino brille | Recuperer les 120 Soleils | m32(p32(0x8040E160)+0xD0) == 120\n"
      "Piece bleue | Trouver une piece bleue | m32(p32(0x8040E160)+0xD4) >= 1 && m32(p32(0x8040E160)+0xD4) <= 240\n"
      "Chasseur de bleues | Trouver 50 pieces bleues | m32(p32(0x8040E160)+0xD4) >= 50 && m32(p32(0x8040E160)+0xD4) <= 240\n"
      "Collection bleue | Trouver 100 pieces bleues | m32(p32(0x8040E160)+0xD4) >= 100 && m32(p32(0x8040E160)+0xD4) <= 240\n"
      "Toutes les bleues | Trouver les 240 pieces bleues | m32(p32(0x8040E160)+0xD4) == 240\n"
      "Increvable | Avoir 30 vies | m32(p32(0x8040E160)+0x7C) >= 30 && m32(p32(0x8040E160)+0x7C) <= 99\n"
      "Chat a neuf vies | Avoir 99 vies | m32(p32(0x8040E160)+0x7C) == 99\n"
    },
    { "GP5P01",
      "# Mario Party 5 (PAL). Adresses du compagnon de l'appli (joueur 1 en 0x80248DB0), v389.\n"
      "Tirelire | Joueur 1 : avoir 50 pieces | m16(0x80248DD0) >= 50 && m16(0x80248DD0) <= 999\n"
      "Coffre-fort | Joueur 1 : avoir 100 pieces | m16(0x80248DD0) >= 100 && m16(0x80248DD0) <= 999\n"
      "Premiere etoile | Joueur 1 : obtenir une etoile | m16(0x80248DE4) >= 1 && m16(0x80248DE4) <= 99\n"
      "Collection d'etoiles | Joueur 1 : avoir 5 etoiles | m16(0x80248DE4) >= 5 && m16(0x80248DE4) <= 99\n"
      "Superstar | Joueur 1 : avoir 10 etoiles | m16(0x80248DE4) >= 10 && m16(0x80248DE4) <= 99\n"
      "As des mini-jeux | Joueur 1 : gagner 100 pieces en mini-jeux dans une partie | m16(0x80248DD2) >= 100 && m16(0x80248DD2) <= 9999\n"
      "Banquier | Joueur 1 : 200 pieces gagnees dans une partie | m16(0x80248DD4) >= 200 && m16(0x80248DD4) <= 9999\n"
    },
    { "GP5E01",
      "# Mario Party 5 (USA). Adresses du compagnon de l'appli (joueur 1 en 0x8022A070), v389.\n"
      "Tirelire | Joueur 1 : avoir 50 pieces | m16(0x8022A090) >= 50 && m16(0x8022A090) <= 999\n"
      "Coffre-fort | Joueur 1 : avoir 100 pieces | m16(0x8022A090) >= 100 && m16(0x8022A090) <= 999\n"
      "Premiere etoile | Joueur 1 : obtenir une etoile | m16(0x8022A0A4) >= 1 && m16(0x8022A0A4) <= 99\n"
      "Collection d'etoiles | Joueur 1 : avoir 5 etoiles | m16(0x8022A0A4) >= 5 && m16(0x8022A0A4) <= 99\n"
      "Superstar | Joueur 1 : avoir 10 etoiles | m16(0x8022A0A4) >= 10 && m16(0x8022A0A4) <= 99\n"
      "As des mini-jeux | Joueur 1 : gagner 100 pieces en mini-jeux dans une partie | m16(0x8022A092) >= 100 && m16(0x8022A092) <= 9999\n"
      "Banquier | Joueur 1 : 200 pieces gagnees dans une partie | m16(0x8022A094) >= 200 && m16(0x8022A094) <= 9999\n"
    },
};
#define SJ_NB ((int)(sizeof(sj_builtin) / sizeof(sj_builtin[0])))

static char *sj_trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}
static int sj_num(const char **p, u32 *v) {               /* 0x... ou decimal */
    const char *s = *p;
    while (*s == ' ') s++;
    char *e;
    unsigned long x = strtoul(s, &e, 0);
    if (e == s) return 0;
    *v = (u32)x; *p = e;
    return 1;
}
static int sj_lit(const char **p, const char *w) {         /* mot attendu (espaces avant ignores) */
    const char *s = *p;
    while (*s == ' ') s++;
    size_t n = strlen(w);
    if (strncasecmp(s, w, n)) return 0;
    *p = s + n;
    return 1;
}
/* un terme : m16(0x803C4C0C) >= 200 ou m16(p32(0x80xxxxxx)+0x10) == 3 */
static int sj_term(const char *s, u32 *t) {
    u32 sz = 0, ptr = 0, ad = 0, off = 0, val = 0, cmp;
    if (sj_lit(&s, "m8(")) sz = 1; else if (sj_lit(&s, "m16(")) sz = 2; else if (sj_lit(&s, "m32(")) sz = 4; else return 0;
    if (sj_lit(&s, "p32(")) {
        ptr = 1;
        if (!sj_num(&s, &ad) || !sj_lit(&s, ")")) return 0;
        if (sj_lit(&s, "+") && !sj_num(&s, &off)) return 0;
    } else if (!sj_num(&s, &ad)) return 0;
    if (!sj_lit(&s, ")")) return 0;
    /* (pour Pulse : masque des resultats qui rendent le terme vrai : bit 0 plus petit, bit 1 egal, bit 2 plus grand) */
    u32 bits = 0;
    if (sj_lit(&s, "&")) { bits = 1; cmp = 2; }         /* v389 : m8(a) & 0x04 : ces bits a 1 (drapeaux du jeu) */
    else if (sj_lit(&s, "==")) cmp = 2; else if (sj_lit(&s, "!=")) cmp = 5; else if (sj_lit(&s, ">=")) cmp = 6;
    else if (sj_lit(&s, "<=")) cmp = 3; else if (sj_lit(&s, ">")) cmp = 4; else if (sj_lit(&s, "<")) cmp = 1; else return 0;
    if (!sj_num(&s, &val)) return 0;
    while (*s == ' ') s++;
    if (*s) return 0;
    if (!ptr && (ad < 0x80000000u || ad >= 0x81800000u)) return 0;
    if (ptr && (ad < 0x80000000u || ad >= 0x81800000u || (ad & 3))) return 0;
    if (bits && !val) return 0;
    t[0] = bits << 25 | ptr << 24 | sz << 16 | cmp << 8; t[1] = ad; t[2] = off; t[3] = val;
    return 1;
}
/* une ligne -> un succes (0 : commentaire / vide, -1 : erreur) */
static int sj_line(char *l, sj_def_t *d) {
    char *s = sj_trim(l);
    if (!*s || *s == '#') return 0;
    char *f[4] = { s, NULL, NULL, NULL };
    int nf = 1;
    for (char *p = s; *p && nf < 4; p++) if (*p == '|') { *p = 0; f[nf++] = p + 1; }
    if (nf < 3) return -1;
    memset(d, 0, sizeof(*d));
    snprintf(d->title, sizeof(d->title), "%s", sj_trim(f[0]));
    snprintf(d->desc, sizeof(d->desc), "%s", sj_trim(f[1]));
    d->need = 30;
    if (nf == 4) { u32 v = 0; const char *q = sj_trim(f[3]); sj_lit(&q, "images"); if (sj_num(&q, &v) && v) d->need = v; }
    char *c = f[2];
    while (c && d->nt < SJ_TERMS) {
        char *a = strstr(c, "&&");
        if (a) *a = 0;
        if (!sj_term(sj_trim(c), d->t[d->nt])) return -1;
        d->nt++;
        c = a ? a + 2 : NULL;
    }
    return d->title[0] && d->nt ? 1 : -1;
}
static const char *sj_builtin_text(const char *id) {
    for (int i = 0; i < SJ_NB; i++) if (!strncmp(sj_builtin[i].id, id, 6)) return sj_builtin[i].text;
    return NULL;
}
/* succes d'un jeu (fichier de la carte, cree d'apres sj_builtin s'il n'existe pas) ; rend le nombre, *bad = lignes refusees */
static int sj_nowrite = 0;                                /* v389 : (liste pour l'appli : pas de fichier cree) */
static int sj_load(const char *id, sj_def_t *out, int cap, int *bad) {
    char path[96];
    snprintf(path, sizeof(path), SJ_DIR "/%.6s.txt", id);
    if (bad) *bad = 0;
    long n = 0;
    char *t = sd_ok ? (char *)read_file(path, 64 * 1024, &n) : NULL;
    if (!t) {
        const char *b = sj_builtin_text(id);
        if (!b) return 0;
        if (sd_ok && !sj_nowrite) {                                       /* (sur la carte : modifiable, et lisible par le telephone) */
            mkdir(SJ_DIR, 0777);
            FILE *f = fopen(path, "wb");
            if (f) { fputs(sj_head, f); fputs(b, f); fclose(f); }
        }
        n = (long)strlen(b);
        t = (char *)malloc(n + 1);
        if (!t) return 0;
        memcpy(t, b, n);
    } else {
        char *q = (char *)realloc(t, n + 1);
        if (!q) { free(t); return 0; }
        t = q;
    }
    t[n] = 0;
    int k = 0;
    for (char *l = t; l && *l && k < cap; ) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        int r = sj_line(l, &out[k]);
        if (r > 0) k++; else if (r < 0 && bad) (*bad)++;
        l = e ? e + 1 : NULL;
    }
    free(t);
    return k;
}

/* ---------- au lancement (natif.c) : section "ACH1" pour Pulse ---------- */
static u32 na_ach[2 + SJ_MAX * (5 + SJ_TERMS * 4)];   /* (SJ_MAX <= 63 : les etats tiennent dans un secteur) */
static int na_ach_w = 0, na_ach_n = 0, na_ach_bad = 0;
static char na_ach_tit[SJ_MAX][48];                       /* v397 : titres (bandeau sur la tele, natif.c) */
static u8 na_ach_deja[SJ_MAX];                            /* v397 : deja obtenus avant (pas de bandeau pour eux) */
static void sj_got_path(const char *prof, char *o, int cap);
static int sj_have(const char *gotf, const char *id, const char *title);
static void sj_prepare(const char *id) {
    na_ach_w = na_ach_n = na_ach_bad = 0;
    static sj_def_t d[SJ_MAX];
    int n = sj_load(id, d, SJ_MAX, &na_ach_bad);
    if (n <= 0) return;
    u32 *w = na_ach;
    *w++ = 0x41434831u;                                   /* "ACH1" */
    *w++ = (u32)n;
    for (int i = 0; i < n; i++) { *w++ = 0; *w++ = 0; }   /* (etats : ecrits tels quels dans le journal par Pulse) */
    for (int i = 0; i < n; i++) {
        *w++ = (u32)d[i].nt; *w++ = d[i].need; *w++ = 0;
        for (int k = 0; k < d[i].nt; k++) for (int j = 0; j < 4; j++) *w++ = d[i].t[k][j];
    }
    na_ach_w = (int)(w - na_ach);
    na_ach_n = n;
    {   /* v397 : pour le bandeau de la tele */
        char gotf[200];
        sj_got_path(gp_name, gotf, sizeof(gotf));
        for (int i = 0; i < n; i++) {
            snprintf(na_ach_tit[i], sizeof(na_ach_tit[i]), "%s", d[i].title);
            na_ach_deja[i] = sj_have(gotf, id, d[i].title) ? 1 : 0;
        }
    }
    FILE *f = fopen(SJ_LANCE, "wb");                      /* (pour le retour : quels titres, quel profil) */
    if (f) {
        fprintf(f, "%s\n%.6s\n", gp_name, id);
        for (int i = 0; i < n; i++) fprintf(f, "%s\n", d[i].title);
        fclose(f);
    }
}

/* ---------- au retour (traduction du journal) ---------- */
static u32 sj_new_t = 0;                                  /* (date des derniers succes obtenus : notification du telephone) */
static char sj_new_txt[200] = "";
static void sj_got_path(const char *prof, char *o, int cap) {
    if (prof && prof[0]) snprintf(o, cap, GCD_PROF "/%s/succes-jeux.txt", prof); else snprintf(o, cap, GCD_PARTIES "/succes-jeux.txt");
}
/* v369 : dans le fichier des succes obtenus deja lu (t, n octets) */
static int sj_have_in(const char *t, long n, const char *id, const char *title) {
    if (!t) return 0;
    char key[64];
    int kl = snprintf(key, sizeof(key), "%.6s|%s|", id, title), found = 0;
    for (long i = 0; i + kl <= n && !found; ) {
        if (!memcmp(t + i, key, kl)) found = 1;
        while (i < n && t[i] != '\n') i++;
        i++;
    }
    return found;
}
static int sj_have(const char *gotf, const char *id, const char *title) {
    long n = 0;
    char *t = (char *)read_file(gotf, 128 * 1024, &n);
    if (!t) return 0;
    char key[64];
    int kl = snprintf(key, sizeof(key), "%.6s|%s|", id, title), found = 0;
    for (long i = 0; i + kl <= n && !found; ) {
        if (!memcmp(t + i, key, kl)) found = 1;
        while (i < n && t[i] != '\n') i++;
        i++;
    }
    free(t);
    return found;
}
/* sec = 6e secteur du journal (debut de la section "ACH1" : etats) ; f = journal en texte (section ajoutee) */
static void sj_after_game(const u8 *sec, FILE *f) {
    long n = 0;
    char *t = (char *)read_file(SJ_LANCE, 32 * 1024, &n);
    if (!t) return;
    char *q = (char *)realloc(t, n + 1);
    if (!q) { free(t); return; }
    t = q; t[n] = 0;
    char *line[2 + SJ_MAX];
    int nl = 0;
    for (char *l = t; l && nl < 2 + SJ_MAX; ) { char *e = strchr(l, '\n'); if (e) *e = 0; line[nl++] = l; l = e ? e + 1 : NULL; }
    if (nl < 2) { free(t); return; }
    const char *prof = line[0], *id = line[1];
    char gotf[160];
    sj_got_path(prof, gotf, sizeof(gotf));
    u32 cnt = be32(sec + 4), hz = id[3] == 'P' ? 50u : 60u;
    int nnew = 0, nok = 0;
    char last[48] = "";
    if (f) fprintf(f, "\n========== Succes (%u surveilles par Pulse) ==========\n", (unsigned)cnt);
    for (u32 i = 0; i < cnt && i < 63 && (int)i + 2 < nl; i++) {
        u32 st = be32(sec + 8 + i * 8), fr = be32(sec + 12 + i * 8);
        const char *ti = line[2 + i];
        if (f) fprintf(f, "  %-40s %s", ti, st & 2 ? "OBTENU" : st & 1 ? "pas encore" : "pas surveille (condition jamais fausse ?)");
        if (st & 2) {
            nok++;
            if (f) fprintf(f, " (apres %u min %02u s de jeu)", (unsigned)(fr / hz / 60), (unsigned)(fr / hz % 60));
            if (!sj_have(gotf, id, ti)) {
                if (prof[0]) { char d[96]; snprintf(d, sizeof(d), GCD_PROF "/%s", prof); mkdir(GCD_PROF, 0777); mkdir(d, 0777); }
                else mkdir(GCD_PARTIES, 0777);
                FILE *g = fopen(gotf, "ab");
                if (g) { fprintf(g, "%.6s|%s|%u\n", id, ti, (unsigned)time(NULL)); fclose(g); }
                nnew++;
                snprintf(last, sizeof(last), "%s", ti);
                if (f) fprintf(f, "  <- NOUVEAU");
            }
        }
        if (f) fputc('\n', f);
    }
    if (nnew) {
        sj_new_t = (u32)time(NULL);
        if (nnew == 1) snprintf(sj_new_txt, sizeof(sj_new_txt), "Succ\xC3\xA8s obtenu : %s", last);
        else snprintf(sj_new_txt, sizeof(sj_new_txt), "%d succ\xC3\xA8s obtenus (dernier : %s)", nnew, last);
        lib_note(sj_new_txt);
    }
    (void)nok;
    free(t);
}

/* ---------- liste (START > Outils > Succes) : succes des jeux qui en ont ---------- */
typedef struct { char id[8], title[48], desc[96]; u8 got; } sj_row_t;
static sj_row_t *sj_rows = NULL;
static int sj_nrows = 0;
static void sj_list_load(void) {
    free(sj_rows); sj_rows = NULL; sj_nrows = 0;
    if (!sd_ok) return;
    for (int i = 0; i < SJ_NB; i++) {                      /* (les jeux fournis : fichiers crees s'ils manquent) */
        char p[96]; struct stat st;
        snprintf(p, sizeof(p), SJ_DIR "/%s.txt", sj_builtin[i].id);
        if (stat(p, &st) != 0) { int b; static sj_def_t tmp[SJ_MAX]; sj_load(sj_builtin[i].id, tmp, SJ_MAX, &b); }
    }
    DIR *dd = opendir(SJ_DIR);
    if (!dd) return;
    char gotf[160];
    sj_got_path(gp_name, gotf, sizeof(gotf));
    struct dirent *e;
    static sj_def_t d[SJ_MAX];
    int cap = 0;
    long gn = 0;
    char *gt = (char *)read_file(gotf, 128 * 1024, &gn);   /* v369 : lu une fois (avant : relu pour chaque succes : 180 ms a l'ouverture) */
    while ((e = readdir(dd)) != NULL) {
        const char *nm = e->d_name;
        if (strlen(nm) != 10 || strcasecmp(nm + 6, ".txt")) continue;
        char id[8]; memcpy(id, nm, 6); id[6] = 0;
        int b, n = sj_load(id, d, SJ_MAX, &b);
        for (int i = 0; i < n; i++) {
            if (sj_nrows >= cap) { int nc = cap ? cap * 2 : 32; sj_row_t *r = (sj_row_t *)realloc(sj_rows, nc * sizeof(sj_row_t)); if (!r) break; sj_rows = r; cap = nc; }
            sj_row_t *r = &sj_rows[sj_nrows++];
            snprintf(r->id, sizeof(r->id), "%s", id);
            snprintf(r->title, sizeof(r->title), "%s", d[i].title);
            snprintf(r->desc, sizeof(r->desc), "%s", d[i].desc);
            r->got = (u8)sj_have_in(gt, gn, id, d[i].title);
        }
    }
    closedir(dd);
    free(gt);
}

/* (fil du serveur du telephone) [["ID","titre","description",obtenu (0/1)],...] : fichiers seulement, rien de partage */
/* v306 : succes cree par le chercheur de codes de l'appli : une ligne ajoutee au fichier du jeu (verifiee d'abord) */
static char *sj_add(const char *id, const char *line) {
    char l[400], t[400], path[96];
    snprintf(l, sizeof(l), "%s", line);
    for (char *p = l; *p; p++) if (*p == '\r' || *p == '\n') *p = ' ';
    sj_def_t d, *all = (sj_def_t *)malloc(SJ_MAX * sizeof(sj_def_t));
    snprintf(t, sizeof(t), "%s", l);
    const char *err = NULL;
    int b = 0, n = all ? sj_load(id, all, SJ_MAX, &b) : 0;   /* (cree aussi le fichier d'un jeu fourni) */
    free(all);
    if (!sd_ok) err = "Pas de carte SD";
    else if (sj_line(t, &d) <= 0) err = "Ligne refus\xC3\xA9" "e (Titre | Description | condition)";
    else if (n >= SJ_MAX) err = "Trop de succ\xC3\xA8s pour ce jeu (48 au plus)";
    if (!err) {
        snprintf(path, sizeof(path), SJ_DIR "/%.6s.txt", id);
        mkdir(SJ_DIR, 0777);
        struct stat st;
        int had = stat(path, &st) == 0 && st.st_size > 0, nl = 1;
        if (had) { FILE *r = fopen(path, "rb"); if (r) { fseek(r, -1, SEEK_END); nl = fgetc(r) == '\n'; fclose(r); } }
        FILE *f = fopen(path, "ab");
        if (!f) err = "\xC3\x89" "criture sur la carte SD impossible";
        else { if (!had) fputs(sj_head, f); else if (!nl) fputc('\n', f); fprintf(f, "%s\n", sj_trim(l)); fclose(f); }
    }
    char m[200];
    if (err) snprintf(m, sizeof(m), "{\"ok\":0,\"err\":\"%s\"}", err); else snprintf(m, sizeof(m), "{\"ok\":1,\"n\":%d}", n + 1);
    return strdup(m);
}
static char *sj_json(void) {
    sbuf_t s = { NULL, 0, 0 };
    sb_add(&s, "[");
    DIR *dd = sd_ok ? opendir(SJ_DIR) : NULL;
    char gotf[160];
    sj_got_path(gp_name, gotf, sizeof(gotf));
    sj_def_t *d = (sj_def_t *)malloc(SJ_MAX * sizeof(sj_def_t));
    int first = 1;
    struct dirent *e;
    while (dd && d && (e = readdir(dd)) != NULL) {
        const char *nm = e->d_name;
        if (strlen(nm) != 10 || strcasecmp(nm + 6, ".txt")) continue;
        char id[8]; memcpy(id, nm, 6); id[6] = 0;
        int b, n = sj_load(id, d, SJ_MAX, &b);
        for (int i = 0; i < n; i++) {
            char u8[300];
            sb_add(&s, first ? "[" : ",["); first = 0;
            sb_json_str(&s, id); sb_add(&s, ",");
            tp_any_utf8(d[i].title, u8, sizeof(u8)); sb_json_str(&s, u8); sb_add(&s, ",");
            tp_any_utf8(d[i].desc, u8, sizeof(u8)); sb_json_str(&s, u8);
            sb_add(&s, sj_have(gotf, id, d[i].title) ? ",1]" : ",0]");
        }
    }
    if (dd) closedir(dd);
    /* v389 : aussi les jeux qui ont des succes fournis mais pas encore de fichier (jamais lances avec Pulse) */
    for (int k = 0; k < SJ_NB && d; k++) {
        char path[96];
        snprintf(path, sizeof(path), SJ_DIR "/%.6s.txt", sj_builtin[k].id);
        if (sd_ok && gcd_exists(path)) continue;
        sj_nowrite = 1;
        int b, n = sj_load(sj_builtin[k].id, d, SJ_MAX, &b);
        sj_nowrite = 0;
        for (int i = 0; i < n; i++) {
            char u8[300];
            sb_add(&s, first ? "[" : ",["); first = 0;
            sb_json_str(&s, sj_builtin[k].id); sb_add(&s, ",");
            tp_any_utf8(d[i].title, u8, sizeof(u8)); sb_json_str(&s, u8); sb_add(&s, ",");
            tp_any_utf8(d[i].desc, u8, sizeof(u8)); sb_json_str(&s, u8);
            sb_add(&s, sj_have(gotf, sj_builtin[k].id, d[i].title) ? ",1]" : ",0]");
        }
    }
    free(d);
    sb_add(&s, "]");
    return s.b;
}
