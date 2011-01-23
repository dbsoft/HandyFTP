/* $Id: hftpextr.h,v 1.9 2002/12/01 13:56:02 bsmith Exp $ */

int DWSIGNAL containerselect(HWND hwnd, char *text, void *data);
int DWSIGNAL listboxselect(HWND hwnd, int item, void *data);
int DWSIGNAL containercontextmenu(HWND hwnd, char *text, int x, int y, void *data);
int DWSIGNAL nofailtab(HWND hwnd, void *data);
int DWSIGNAL autoconnecttab(HWND hwnd, void *data);
int DWSIGNAL preferencestab(HWND hwnd, void *data);
int DWSIGNAL exittab(HWND hwnd, void *data);
int DWSIGNAL sysinfotab(HWND hwnd, void *data);
int DWSIGNAL abouttab(HWND hwnd, void *data);
int DWSIGNAL generalhelp(HWND hwnd, void *data);
int DWSIGNAL removefromqtab(HWND hwnd, void *data);
int DWSIGNAL addtoqtab(HWND hwnd, void *data);
int DWSIGNAL administratetab(HWND hwnd, void *data);
int DWSIGNAL savetitletab(HWND hwnd, void *data);
int DWSIGNAL unsavetitletab(HWND hwnd, void *data);
int DWSIGNAL flushtab(HWND hwnd, void *data);
int DWSIGNAL refreshtab(HWND hwnd, void *data);
int DWSIGNAL disconnecttab(HWND hwnd, void *data);
int DWSIGNAL connecttab(HWND hwnd, void *data);
int DWSIGNAL removetab(HWND hwnd, void *data);
int DWSIGNAL newtab(HWND hwnd, void *data);
int DWSIGNAL queuedel(HWND hwnd, void *data);
int DWSIGNAL siteproperties(HWND hwnd, void *data);
void recurseq(SiteTab *thissite);
int validatecurrentpage(void);
void drawq(SiteTab *lsite);
