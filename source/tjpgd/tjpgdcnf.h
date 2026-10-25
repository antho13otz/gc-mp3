/*----------------------------------------------*/
/* TJpgDec System Configurations R0.03          */
/* GC Station v118 : sortie RGB888, reduction    */
/* 1/2 1/4 1/8 au decodage, version rapide.     */
/*----------------------------------------------*/
#define JD_SZBUF        4096
#define JD_FORMAT       0
#define JD_USE_SCALE    1
#define JD_TBLCLIP      1
#define JD_FASTDECODE   2
#define TJPGD_WORKSPACE_SIZE (3500 + 6144 + 4096)
