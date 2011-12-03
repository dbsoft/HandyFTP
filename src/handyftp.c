/* $Id: handyftp.c,v 1.269 2005/05/20 11:32:41 bsmith Exp $ */

#include "compat.h"
#include "dw.h"
#include "handyftp.h"
#include "site.h"
#include "hftpextr.h"
#include "datetime.h"
#include <errno.h>

HWND  hwndNBK;			/* Notebook Window Handle		*/
HWND hwndFrame;
HWND refreshbutton;
HEV hev;

HMENUI menubar, sort_menu;

#define TB_SEPARATOR -1

ULONG mainItems[]=
{
	NEWTAB      , REMOVETAB   , TB_SEPARATOR, CONNECT     , DISCONNECT  ,
	TB_SEPARATOR, IDM_EXIT    , TB_SEPARATOR, REMOVEFROMQ , ADDTOQ      ,
	FLUSHQ      , TB_SEPARATOR, SAVETITLE   , UNSAVETITLE , TB_SEPARATOR,
	PB_CHANGE   , TB_SEPARATOR, IDM_PREFERENCES , ADMIN       , TB_SEPARATOR,
	IDM_GENERALHELP, IDM_ABOUT   , 0
};

char mainHelpItems[][100]=
{
	"New Tab", "Remove Tab", "", "Connect", "Disconnect",
	"", "Exit", "", "Remove From Queue", "Add to Queue",
	"Flush Queue", "", "Save Title", "Unsave Title", "",
	"Refresh", "", "Preferences", "Site Administration", "",
	"General Help", "About"
};

void *mainFunctions[] = { (void *)newtab, (void *)removetab, NULL, (void *)connecttab, (void *)disconnecttab,
						  NULL, (void *)exittab, NULL, (void *)removefromqtab, (void *)addtoqtab,
						  (void *)flushtab, NULL, (void *)savetitletab, (void *)unsavetitletab, NULL,
						  (void *)refreshtab, NULL, (void *)preferencestab, (void *)administratetab, NULL,
						  (void *)generalhelp, (void *)abouttab };

/* Up to 256 simultaneous connections */
#define CONNECTION_LIMIT 256
#define URL_LIMIT 4000

#define NAT_DELAY 1000

/* Select an editor for the current build platform. */
#if defined(__OS2__) || defined(__EMX__)
#define EDITOR "epm"
#define EDMODE DW_EXEC_GUI
#elif defined(__WIN32__) || defined(WINNT)
#define EDITOR "notepad"
#define EDMODE DW_EXEC_GUI
#elif defined(__MAC__)
#define EDITOR "TextEdit"
#define EDMODE DW_EXEC_GUI
#else
#define EDITOR "vi"
#define EDMODE DW_EXEC_CON
#endif

/* Note currently DW has a limit of 4095 (4096 - NULL) */

SiteTab *site[CONNECTION_LIMIT];
int alive[CONNECTION_LIMIT];
SavedSites *SSroot;
SiteTypes *STroot;
AdminEntry *AERoot = NULL;

HMTX mutex;
char empty_string[] = "";

/* Options */
int nofail = FALSE, autoconnect = FALSE, openall = FALSE, reversefxp = TRUE, optimize = TRUE;
int ftptimeout = 60, retrymax = 10, default_sort = SORT_FILE, handyftp_locale = 0;
int idletimeout=120, userlevel=100, bandwidthlimit = 0;
int showpassword=FALSE, urlsave=FALSE, cachemax=40;

int currentpage=-1, pagescreated = 0, newpage = -1, firstpage = TRUE;

int proppage = -1;

HWND in_properties = 0, in_about = 0, in_preferences = 0, in_administration = 0, in_IPS = 0;
HWND IPS_handle;
int IPS_page = 0;
time_t IPS_time = 0;

HICN fileicon, foldericon, linkicon;

volatile struct DebugInfo_st 
{
	int	linenumb;
	char	*procname;
} DebugInfo;

#define DBUG_POINT(a) {						\
		DebugInfo.linenumb = __LINE__;	\
		DebugInfo.procname = a;	\
	}

char sitestatus[40][40] = {
	"Idle",
	"Flushing",
	"Connecting",
	"Sending",
	"Receiving",
	"Ident",
	"Login",
	"Password",
	"Change Working Directory",
	"List",
	"List Port",
	"Directory",
	"Directory Accept",
	"Data",
	"Data Accept",
	"Retrieve",
	"Store",
	"Store Wait",
	"Transmit",
	"FXP Retreive",
	"FXP Store",
	"FXP Transfer",
	"FXP Wait",
	"Next",
	"Retry",
	"Wait for Idle",
	"Passive Retrieve",
	"Passive List"
};

/* 0=CONN, 1=LOGIN, 10=IDLE, 11=RETR, 12=STOR, 13=LIST, 14=SITE */
char *IPSstatus[15] = {
	"Connect",
	"Login",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"Idle",
	"Retrieve",
	"Store",
	"List",
	"Site"
};

/* Used for popup menus */
char *contexttext = NULL;

void handyftp_crash(int signal)
{
	char tmpbuf[1024];
	sprintf(tmpbuf, locale_string("Crash in %s, line %d of %s. Please report to brian@dbsoft.org", 32), DebugInfo.procname, DebugInfo.linenumb, __FILE__);
	dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_INFORMATION, tmpbuf);
	dw_exit(0);
}

/* Checks if a string is a valid IP address */
int isip(char *buf)
{
	int z = 0, dotcount = 0, value;
	char *start = buf;

	while(buf[z])
	{
		if(buf[z] == '.')
		{
			value = atoi(start);
			if(value > 255 || value < 0)
				return 0;
			start = &buf[z+1];
			dotcount++;
		}
		else if(!isdigit(buf[z]))
			return 0;
		z++;
	}

	value = atoi(start);
	if(value > 255 || value < 0)
		return 0;

	if(dotcount != 3 || z < 7 || z > 15)
		return 0;
	return 1;
}
/* Return the total number of active pages */
int countpages(void)
{
	int z, count = 0;
	for(z=0;z<CONNECTION_LIMIT;z++)
		if(alive[z] == TRUE)
			count++;
	return count;
}

/* Return the page ID of the first page that isn't in use */
int finddead(void)
{
	int z;
	for(z=0;z<CONNECTION_LIMIT;z++)
		if(alive[z] == FALSE)
			return z;
	return -1;
}

/* Set the page count on all of the tabs */
void settabs(void)
{
	char *tmpptr, tmpbuf[100];
	int z, x, count = 0;
	for(z=0;z<CONNECTION_LIMIT;z++)
	{
		if(alive[z] == TRUE)
		{
			count++;
			sprintf(tmpbuf, locale_string("Page %d of %d", 33), count, countpages());

			if(!(tmpptr = dw_window_get_text(site[z]->host_title)))
				return;

			dw_listbox_clear(site[z]->destsite);
			for(x=0;x<CONNECTION_LIMIT;x++)
			{
				if(alive[x] == TRUE)
				{
					if(z != x)
					{
						char *tmpptr2 = dw_window_get_text(site[x]->host_title);
						if(tmpptr2)
						{
							dw_listbox_append(site[z]->destsite, tmpptr2);
							dw_free(tmpptr2);
						}
					}
				}
			}

			if(strcmp(tmpptr, "")==0)
			{
				char tmpbuf[100];
				sprintf(tmpbuf, "Site %d", count);

				dw_notebook_page_set_text(hwndNBK, site[z]->pageid, tmpbuf);
				dw_notebook_page_set_status_text(hwndNBK, site[z]->pageid, tmpbuf);
			}
			else
			{
				dw_notebook_page_set_text(hwndNBK, site[z]->pageid, tmpptr);
				dw_notebook_page_set_status_text(hwndNBK, site[z]->pageid, tmpptr);
			}
			dw_free(tmpptr);
		}
	}
}

/* Write the handyftp.ini file with all of the current settings */
void saveconfig(void)
{
	SavedSites *tmp;
	FILE *f;
	char *tmppath = INIDIR, *inidir, *inipath, *home = dw_user_dir();

	if(strcmp(INIDIR, ".") == 0)
	{
		inipath = strdup("handyftp.ini");
		inidir = strdup(INIDIR);
	}
	else
	{
		if(home && tmppath[0] == '~')
		{
			inipath = malloc(strlen(home) + strlen(INIDIR) + 14);
			inidir = malloc(strlen(home) + strlen(INIDIR));
			strcpy(inipath, home);
			strcpy(inidir, home);
			strcat(inipath, &tmppath[1]);
			strcat(inidir, &tmppath[1]);
		}
		else
		{
			inipath = malloc(strlen(INIDIR) + 14);
			strcat(inipath, INIDIR);
			inidir = strdup(INIDIR);
		}
		strcat(inipath, DIRSEP);
		strcat(inipath, "handyftp.ini");
	}

	tmp = SSroot;

	f=fopen(inipath, FOPEN_WRITE_TEXT);

	if(f==NULL)
	{
		if(strcmp(INIDIR, ".") != 0)
		{
			makedir(inidir);
			f=fopen(inipath, FOPEN_WRITE_TEXT);
		}
		if(f==NULL)
		{
			dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("Could not save settings. Inipath = \"%s\"", 34), inipath);
			free(inipath);
			free(inidir);
			return;
		}
	}

	free(inipath);
	free(inidir);

	if(nofail)
		fprintf(f, "NOFAIL=TRUE\n");
	else
		fprintf(f, "NOFAIL=FALSE\n");

	if(urlsave)
		fprintf(f, "URLSAVE=TRUE\n");
	else
		fprintf(f, "URLSAVE=FALSE\n");

	if(autoconnect)
		fprintf(f, "AUTOCONNECT=TRUE\n");
	else
		fprintf(f, "AUTOCONNECT=FALSE\n");

	if(openall)
		fprintf(f, "OPENALL=TRUE\n");
	else
		fprintf(f, "OPENALL=FALSE\n");

	if(reversefxp)
		fprintf(f, "REVERSEFXP=TRUE\n");
	else
		fprintf(f, "REVERSEFXP=FALSE\n");

	if(showpassword)
		fprintf(f, "SHOWPASSWORD=TRUE\n");
	else
		fprintf(f, "SHOWPASSWORD=FALSE\n");

	if(optimize)
		fprintf(f, "OPTIMIZE=TRUE\n");
	else
		fprintf(f, "OPTIMIZE=FALSE\n");

	fprintf(f, "TIMEOUT=%d\n", ftptimeout);
	fprintf(f, "RETRIES=%d\n", retrymax);
	fprintf(f, "CACHEMAX=%d\n", cachemax);
	fprintf(f, "BANDWIDTH=%d\n", bandwidthlimit);
	fprintf(f, "SORT=%d\n", default_sort);
	fprintf(f, "LOCALE=%d\n", handyftp_locale);

	while (tmp != NULL)
	{
		fprintf(f, "HOSTTITLE=%s\n", tmp->hosttitle);
		fprintf(f, "HOSTNAME=%s\n", tmp->hostname);
		fprintf(f, "PORT=%d\n", tmp->port);
		fprintf(f, "URL=%s\n", tmp->url);
		fprintf(f, "USERNAME=%s\n", tmp->username);
		fprintf(f, "PASSWORD=%s\n", tmp->password);
		tmp = tmp->next;
	}
	fclose(f);
}

/* Update the listbox containing destination sites,
 * this needs to happen each time a page is added/changed/removed.
 */
void updatesites(int page)
{
	SavedSites *tmp;

	tmp = SSroot;

	dw_listbox_clear(site[page]->host_title);
	while (tmp != NULL)
	{
		dw_listbox_append(site[page]->host_title, tmp->hosttitle);
		tmp = tmp->next;
	}
}

/* Calls updatesites for all alive pages. */
void updateallsites(void)
{
	int z;
	for(z=0;z<CONNECTION_LIMIT;z++)
		if(alive[z] == TRUE)
			updatesites(z);
}

/* Send a command to a page via the Unix domain socket */
int sendthread(char msgid, int page)
{
	return (sockwrite(site[page]->pipefd[1], (char *)&msgid, 1, 0) == 1);
}

/* Free a site directory listing */
void freedir(Directory *dir)
{
	Directory *tmp, *next;

	tmp = dir;
	dir = NULL;

	while(tmp != NULL)
	{
		next = tmp->next;
		if(tmp->entry)
			free(tmp->entry);
		free(tmp);
		tmp=next;
	}
}

/* Clears a given file queue list */
void freequeue(Queue *queue)
{
	Queue *tmp, *next;

	tmp = queue;
	queue = NULL;

	while(tmp != NULL)
	{
		next = tmp->next;
		if(tmp->srcdirectory)
			free(tmp->srcdirectory);
		if(tmp->srcfilename)
			free(tmp->srcfilename);
		if(tmp->destdirectory)
			free(tmp->destdirectory);
		if(tmp->site)
			free(tmp->site);
		free(tmp);
		tmp=next;
	}
}

/* Save the contents of the site's queue to a file */
void savequeue(SiteTab *thissite, char *filename)
{
	FILE *fp;

	if((fp = fopen(filename, "w")) != NULL)
	{
		Queue *tmp = thissite->queue;

		fprintf(fp, "%s\n", thissite->hosttitle);

		while(tmp)
		{
			fprintf(fp, "%s\n%s\n%s\n%s\n%lu\n",
					tmp->srcfilename,
					tmp->srcdirectory,
					tmp->destdirectory,
					tmp->site,
					tmp->size);
			tmp = tmp->next;
		}
		fclose(fp);
	}
}

/* Removes the newline char from a string */
void trimnewline(char *buf)
{
	int len;

	len = strlen(buf);
	if(len > 0 && buf[len-1] == '\n')
		buf[len-1] = 0;
}

/* Add a preallocated queue entry to the site queue */
void addtoq2(SiteTab *thissite, Queue *thisq)
{
	thisq->next = NULL;

	if(!thissite->queue)
	{
		thissite->queue = thisq;
	}
	else
	{
		Queue *qend = thissite->queue;

		while(qend->next)
		{
			qend = qend->next;
		}
		qend->next = thisq;
	}
}

/* Add a preallocated queue entry to the top of site queue */
void requeue(SiteTab *thissite, Queue *thisq)
{
	thisq->next = thissite->queue;

	thissite->queue = thisq;
}

/* Increase the reference (use) count */
void site_ref(SiteTab *site)
{
	if(site)
		site->refcount++;
}

/* Decrease the reference (use) count, when
 * no longer in use free.
 */
void site_unref(SiteTab *site)
{
	if(site)
	{
		site->refcount--;
		if(site->refcount < 1)
			free(site);
	}
}

/* Load the contents of the saved queue to the site queue */
void loadqueue(SiteTab *thissite, char *filename)
{
	FILE *fp;
	char buf[1001] = "";

	if((fp = fopen(filename, "r")) != NULL && fgets(buf, 1000, fp) != NULL)
	{
		trimnewline(buf);

		if(strcmp(buf, thissite->hosttitle) != 0)
		{
			dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("Saved host \"%s\" is not the same!", 35), buf);
			fclose(fp);
			return;
		}


		while(!feof(fp))
		{
			Queue *thisq;

			thisq = (Queue *)calloc(sizeof(Queue), 1);

			thisq-> size = 0;

			if(fgets(buf, 1000, fp))
			{
				trimnewline(buf);
				thisq->srcfilename = strdup(buf);

				if(fgets(buf, 1000, fp))
				{
					trimnewline(buf);
					thisq->srcdirectory = strdup(buf);

					if(fgets(buf, 1000, fp))
					{
						trimnewline(buf);
						thisq->destdirectory = strdup(buf);

						if(fgets(buf, 1000, fp))
						{
							trimnewline(buf);
							thisq->site = strdup(buf);

							if(fgets(buf, 1000, fp))
								thisq->size = atoi(buf);

							addtoq2(thissite, thisq);
						}
						else
						{
							free(thisq->srcfilename);
							free(thisq->srcdirectory);
							free(thisq->destdirectory);
							free(thisq);
						}
					}
					else
					{
						free(thisq->srcfilename);
						free(thisq->srcdirectory);
						free(thisq);
					}
				}
				else
				{
					free(thisq->srcfilename);
					free(thisq);
				}
			}
			else
				free(thisq);
		}
		drawq(thissite);
	}
	if(fp)
		fclose(fp);
}

/* Adds a directory listing to the current cache, removing
 * the oldest entry if we are past the cache limit.
 */
void addtocache(SiteTab *lsite)
{
	DirCache *tmp = NULL, *prev = NULL;

	if(lsite->cache)
	{
		tmp = lsite->cache->next;

		/* If we are over the cache limit remove the oldest entry */
		if(lsite->cachecount >= cachemax)
		{
			if(lsite->cache->url)
				free(lsite->cache->url);
			if(lsite->cache->site)
				free(lsite->cache->site);
			if(lsite->cache->user)
				free(lsite->cache->user);
			if(lsite->cache->dir)
				freedir(lsite->cache->dir);
			free(lsite->cache);
			lsite->cache = tmp;
			lsite->cachecount--;
		}
	}
	tmp = lsite->cache;
	while(tmp != NULL)
	{
		prev = tmp;
		tmp = tmp->next;
	}
	if(!prev)
	{
		lsite->cache = malloc(sizeof(DirCache));
		tmp = lsite->cache;
	}
	else
	{
		prev->next = malloc(sizeof(DirCache));
		tmp = prev->next;
	}
	tmp->dir = lsite->dir;
	tmp->url = strdup(lsite->url);
	tmp->site = strdup(lsite->hostname);
	tmp->user = strdup(lsite->username);
	tmp->next = NULL;
	lsite->cachecount++;
}

/* Checks to see if a specified directory is in
 * our directory cache.  If so it sets the directory
 * pointer as current and returns TRUE.
 */
int findincache(SiteTab *lsite)
{
	DirCache *tmp;
	int len1;

	if(!lsite->cache)
		return FALSE;

	len1 = strlen(lsite->url);

	if(len1 > 0 && (lsite->url[len1-1] == '/' || lsite->url[len1-1] == '\\'))
		len1--;

	tmp = lsite->cache;
	while(tmp != NULL)
	{
		int len2 = strlen(tmp->url);

		if(len2 > 0 && (tmp->url[len2-1] == '/' || tmp->url[len2-1] == '\\'))
			len2--;

		if(len1 == len2 &&
		   strncasecmp(lsite->url, tmp->url, len1) == 0 &&
		   strcasecmp(lsite->hostname, tmp->site) == 0 &&
		   strcasecmp(lsite->username, tmp->user) == 0)
		{
			lsite->dir = tmp->dir;
			return TRUE;
		}
		tmp = tmp->next;
	}

	return FALSE;
}

/* Checks to see if a specified directory is in
 * our directory cache.  If so it free's it's
 * entry and decrements the cache count.
 */
int removefromcache(SiteTab *lsite)
{
	DirCache *tmp, *prev = NULL;

	if(!lsite->cache)
		return FALSE;

	tmp = lsite->cache;
	while(tmp != NULL)
	{
		if(strcasecmp(lsite->url, tmp->url) == 0 &&
		   strcasecmp(lsite->hostname, tmp->site) == 0 &&
		   strcasecmp(lsite->username, tmp->user) == 0)
		{
			if(!prev)
				lsite->cache = tmp->next;
			else
				prev->next = tmp->next;

			if(tmp->url)
				free(tmp->url);
			if(tmp->site)
				free(tmp->site);
			if(tmp->user)
				free(tmp->user);
			if(tmp->dir)
				freedir(tmp->dir);
			free(tmp);
			return TRUE;
		}
		prev = tmp;
		tmp = tmp->next;
	}

	return FALSE;
}

/* Removes all cached directory listings for a given site */
void freecache(SiteTab *lsite)
{
	DirCache *tmp, *next = NULL;

	lsite->cachecount = 0;

	if(!lsite->cache)
		return;

	tmp = lsite->cache;

	lsite->cache = NULL;
	lsite->dir = NULL;

	while(tmp != NULL)
	{
		next = tmp->next;
		if(tmp->url)
			free(tmp->url);
		if(tmp->site)
			free(tmp->site);
		if(tmp->user)
			free(tmp->user);
		if(tmp->dir)
			freedir(tmp->dir);
		free(tmp);
		tmp = next;
	}
}

/* Add an entry to the administration user list */
void addtoadmin(char *user, char *addr, char *act, int idle, char *sock)
{
	AdminEntry *tmp = AERoot;

	if(!AERoot)
		tmp = AERoot = malloc(sizeof(AdminEntry));
	else
	{
		while(tmp->next)
			tmp = tmp->next;

		tmp->next = malloc(sizeof(AdminEntry));
		tmp = tmp->next;
	}
	tmp->user = strdup(user);
	tmp->addr = strdup(addr);
	tmp->act = strdup(act);
	tmp->sock = strdup(sock);
	tmp->idle = idle;
	tmp->next = NULL;
}

/* Clear the admininstration user list */
void clearadmin(void)
{
	AdminEntry *tmp = AERoot, *next;

	AERoot = NULL;

	while(tmp)
	{
		if(tmp->user)
			free(tmp->user);
		if(tmp->addr)
			free(tmp->addr);
		if(tmp->act)
			free(tmp->act);
		if(tmp->sock)
			free(tmp->sock);
		next = tmp->next;
		free(tmp);
		tmp = next;
	}
}

/* Inserts the list of items into the IPS administration list */
void IPS_update(void)
{
	AdminEntry *tmp = AERoot;
	int count = 0, z;
	void *containerinfo;
	unsigned long idle, sock;

	if(!tmp || !in_IPS)
		return;

	while(tmp != NULL)
	{
		count++;
		tmp=tmp->next;
	}
	tmp = AERoot;

	dw_container_clear(IPS_handle, FALSE);

	containerinfo = dw_container_alloc(IPS_handle, count);

	for(z=0;z<count;z++)
	{
		dw_container_set_item(IPS_handle, containerinfo, 0, z, &tmp->user);
		dw_container_set_item(IPS_handle, containerinfo, 1, z, &tmp->addr);
		dw_container_set_item(IPS_handle, containerinfo, 2, z, &tmp->act);

		idle = (unsigned long)tmp->idle;
		sock = atoi(tmp->sock);

		dw_container_set_item(IPS_handle, containerinfo, 3, z, &idle);
		dw_container_set_item(IPS_handle, containerinfo, 4, z, &sock);

		dw_container_set_row_title(containerinfo, z, tmp->sock);

		tmp=tmp->next;
	}

	dw_container_insert(IPS_handle, containerinfo, count);
	dw_container_optimize(IPS_handle);
}

/* Returns the size of a file in the site's directory listing
 * or returns 0 if it was not found.
 */
unsigned long findfilesize(char *filename, SiteTab *lsite)
{
	Directory *tmp = lsite->dir;

	while(tmp)
	{
		if(strcmp(filename, tmp->entry) == 0 && tmp->type == DIRFILE)
			return tmp->size;
		tmp = tmp->next;
	}
	return 0L;
}

/* Returns the size of a file in a local directory listing
 * or returns 0 if it was not found.
 */
unsigned long findlocalfilesize(char *filename, char *path)
{
	char *tmpbuf = malloc(strlen(filename) + strlen(path) + 3);
	struct stat bleah;

	strcpy(tmpbuf, path);
	if(tmpbuf[strlen(tmpbuf)-1] != '\\' && tmpbuf[strlen(tmpbuf)-1] != '/')
		strcat(tmpbuf, DIRSEP);
	strcat(tmpbuf, filename);

	if(stat(tmpbuf, &bleah) == 0)
		return bleah.st_size;
	return 0;
}

/* Returns DIRFILE, DIRDIR or DIRLINK for the file specified
 * or returns -1 if it is not found.
 */
int findfiletype(char *filename, SiteTab *lsite)
{
	Directory *tmp = lsite->dir;

	while(tmp)
	{
		if(strcmp(filename, tmp->entry) == 0)
			return tmp->type;
		tmp = tmp->next;
	}
	return -1;
}

/* Apparently I can't remember why this doesn't use the filesize
 * parameter, but I was pretty sure it was supposed to set the
 * file size listed in the directory list.
 */
void setfilesize(char *filename, SiteTab *lsite, unsigned long filesize)
{
	Directory *tmp = lsite->dir;

	while(tmp)
	{
		if(strcmp(filename, tmp->entry) == 0 && tmp->type == DIRFILE)
		{
			tmp->size = filesize;
			return;
		}
		tmp = tmp->next;
	}
	return;
}

/* Write a message to the scrollable console for the specified page. */
int writeconsole(SiteTab *lsite, char *format, ...) {
	va_list args;
	int t, z;
	char outbuf[512];

	va_start(args, format);
	vsprintf(outbuf, format, args);
	va_end(args);

	for(z=0;z<strlen(outbuf);z++)
		if(outbuf[z] == '\n' || outbuf[z] == '\r')
			outbuf[z] = ' ';

	dw_listbox_append(lsite->server, outbuf);
	t = dw_listbox_count(lsite->server);

	/* Make sure we don't get over 1000 lines in the console */
	while(t>1000)
	{
		dw_listbox_delete(lsite->server, 0);
		t = dw_listbox_count(lsite->server);
	}
	dw_listbox_set_top(lsite->server, t-1);


	return strlen(outbuf);
}

/* Set the text on the status line of the specified page */
int setstatustext(SiteTab *lsite, char *format, ...) {
	va_list args;
	char outbuf[512];

	va_start(args, format);
	vsprintf(outbuf, format, args);
	va_end(args);

	dw_window_set_text(lsite->cstatus, outbuf);

	return strlen(outbuf);
}

/* Set the text on the secondary status line of the specified page */
int setmorestatustext(SiteTab *lsite, char *format, ...) {
	va_list args;
	char outbuf[512];

	va_start(args, format);
	vsprintf(outbuf, format, args);
	va_end(args);

	dw_window_set_text(lsite->cmorestatus, outbuf);

	return strlen(outbuf);
}

/* Remove commas from a string so that atoi() will be correct */
void removecommas(char *source, char *target, int len)
{
	int z, cz=0;

	/* Make sure we are terminated no matter what */
	target[0] = 0;

	for(z=0;z<strlen(source) && z < len;z++)
	{
		if(source[z] == ' ')
		{
			/* Only terminate when we have some numbers
			 * to use. :)
			 */
			if(cz)
			{
				target[cz] = 0;
				return;
			}
		}
		else if(source[z] != ',')
		{
			target[cz] = source[z];
			cz++;
		}
	}
	target[cz] = 0;
}

/* Return a pointer to the SiteType information that coresponds to the
 * ID that was passed to it.
 */
SiteTypes *getsitetype(int id)
{
	int count=0;
	SiteTypes *tmp;

	tmp=STroot;
	while(tmp != NULL)
	{
		if(count == id)
			return tmp;

		count++;
		tmp=tmp->next;
	}
	return NULL;
}

/* Attempt to determin which SiteType information would be most
 * appropriate for decoding the FTP listing information.
 */
int determinesitetype(char *banner)
{
	int count=0;
	SiteTypes *tmp;
	SiteIds *tmp2;

	tmp=STroot;
	while(tmp != NULL)
	{
		tmp2 = tmp->siteids;
		while(tmp2 != NULL)
		{
			if(tmp2->id)
			{
				if(instring(banner, tmp2->id) == TRUE)
					return count;
			}
			tmp2=tmp2->next;
		}
		count++;
		tmp=tmp->next;
	}
	return 0;
}

/* Return the pointer to the  site's last directory entry */
Directory *lastdir(SiteTab *lsite)
{
	Directory *tmp = lsite->dir, *prev = NULL;

	while(tmp)
	{
		prev = tmp;
		tmp = tmp->next;
	}
	return prev;
}

/* Compare 2 dates... returns 1 if a is bigger than b
 * returns -1 if a is smaller that b
 * returns 0 if they are equal
 */
int datecmp(CDATE a, CDATE b)
{
	if(a.year >= b.year)
	{
		if(a.year > b.year)
			return 1;
		else if(a.month >= b.month)
		{
			if(a.month > b.month)
				return 1;
			else if(a.day >= b.day)
			{
				if(a.day > b.day)
					return 1;
				return 0;
			}
		}
	}
	return -1;
}

/* Compare 2 times... returns 1 if a is bigger than b
 * returns -1 if a is smaller that b
 * returns 0 if they are equal
 */
int timecmp(CTIME a, CTIME b)
{
	if(a.hours >= b.hours)
	{
		if(a.hours > b.hours)
			return 1;
		else if(a.minutes >= b.minutes)
		{
			if(a.minutes > b.minutes)
				return 1;
			else if(a.seconds >= b.seconds)
			{
				if(a.seconds > b.seconds)
					return 1;
				return 0;
			}
		}
	}
	return -1;
}

/* Compare two date and time pairs */
int datetimecmp(CDATE a1, CTIME a2, CDATE b1, CTIME b2)
{
	int c = datecmp(a1, b1);
	if(c >= 0)
	{
		int d = timecmp(a2, b2);

		if(c > 0)
			return 1;
		else if(d >= 0)
		{
			if(d > 0)
				return 1;
			return 0;
		}
	}
	return -1;
}

/* Sort the directory list */
void sortdir(SiteTab *lsite)
{
	Directory *tmp, **stem;
	int working = TRUE;

	if(!lsite->sort)
		return;

	while(working)
	{
		Directory *prev = NULL;
		stem = &lsite->dir;

		tmp = lsite->dir;

		while(tmp)
		{

			if(prev)
			{
				switch(abs(lsite->sort))
				{
				case SORT_FILE:
					if((lsite->sort > 0 && strcasecmp(prev->entry, tmp->entry) > 0) ||
					   (lsite->sort < 0 && strcasecmp(prev->entry, tmp->entry) < 0))
					{
						prev->next = tmp->next;
						*stem = tmp;
						tmp->next = prev;
						tmp=NULL;
					}
					break;
				case SORT_DATE:
					if((lsite->sort > 0 && datetimecmp(prev->date, prev->time, tmp->date, tmp->time) > 0) ||
					   (lsite->sort < 0 && datetimecmp(prev->date, prev->time, tmp->date, tmp->time) < 0))
					{
						prev->next = tmp->next;
						*stem = tmp;
						tmp->next = prev;
						tmp=NULL;
					}
					break;
				case SORT_SIZE:
					if((lsite->sort > 0 && prev->size > tmp->size) ||
					   (lsite->sort < 0 && prev->size < tmp->size))
					{
						prev->next = tmp->next;
						*stem = tmp;
						tmp->next = prev;
						tmp=NULL;
					}
					break;
				}
				stem = &prev->next;
			}
			if(tmp)
			{
				if(!tmp->next)
					return;
				prev = tmp;
				tmp = tmp->next;
			}
		}
	}
}

int is_valid(char **columns, int increment, SiteTypes *sitetype)
{
	int thisinc;

	if(sitetype->linkcol < sitetype->sizecol)
		thisinc = increment;
	else
		thisinc = 0;

	if(isdigit(*columns[abs(sitetype->sizecol) - 1 - thisinc]))
	{
		if(sitetype->linkcol < sitetype->monthcol)
			thisinc = increment;
		else
			thisinc = 0;

		if(!isdigit(*columns[abs(sitetype->monthcol) - 1 - thisinc]))
		{
			if(sitetype->linkcol < sitetype->daycol)
				thisinc = increment;
			else
				thisinc = 0;

			if(isdigit(*columns[abs(sitetype->daycol) - 1 - thisinc]))
			{
				if(sitetype->linkcol < sitetype->yearcol)
					thisinc = increment;
				else
					thisinc = 0;

				if(isdigit(*columns[abs(sitetype->yearcol) - 1 - thisinc]))
					return TRUE;
			}
		}
	}
	return FALSE;
}

/* Takes a buffer that is filled with the FTP listing, and uses the
 * SiteType entry specified by the "type" parameter to create a Directory
 * listing which is attached to the specified site.
 */
void loadremotedir(SiteTab *lsite, char *buffer, int bufferlen, int type)
{
	int z=0, linestart = 0;
	SiteTypes *sitetype;
	Directory *tmp;
	char line[40096], tmpbuf[100];

	memset(line, 0, 40096);

	lsite->dir = NULL;

	if(STroot == NULL)
		return;

	sitetype = STroot;
	while(sitetype != NULL)
	{
		if(z == type)
			break;
		sitetype=sitetype->next;
		z++;
	}

	if(sitetype == NULL)
	{
		writeconsole(lsite, locale_string("Site type not found! Using the default.", 36));
		sitetype = STroot;
	}

	lsite->dir = (Directory *)malloc(sizeof(Directory));
	tmp = lsite->dir;
	tmp->type = DIRDIR;
	tmp->entry = strdup("..");
	tmp->size = 0;
	tmp->time.hours = 0;
	tmp->time.minutes = 0;
	tmp->time.seconds = 0;
	tmp->date.day = 0;
	tmp->date.month = 0;
	tmp->date.year = 0;
	tmp->next = NULL;

	for(z=0;z<bufferlen;z++)
	{
		if(buffer[z] == '\r' || buffer[z] == '\n' || z == bufferlen-1)
		{
			int l;

			if(z == bufferlen-1)
				z++;

			if(z-linestart > 4095)
			{
				strncpy(line, &buffer[linestart], 4095);
				line[4095] = 0;
				l = 4095;
			}
			else
			{
				strncpy(line, &buffer[linestart], z-linestart);
				line[z-linestart] = 0;
				l = z-linestart;
			}
			/* We are in columnized mode. */
			if(sitetype->column == TRUE)
			{
				int increment = 0, s = 0, c = 0, j, columnpos[20];
				char *columns[20], *line2 = strdup(line);

				memset(columns, 0, sizeof(char *)*20);
				memset(columnpos, 0, sizeof(int)*20);

				for(j=0;j<l;j++)
				{
					if(line[j] == ' ')
					{
						line2[j] = 0;
						if(strlen(&line2[s])>0)
						{
							columnpos[c] = s;
							columns[c] = &line2[s];
							c++;
						}
						s=j+1;
						if(c==19)
							j=l;
					}
				}
				columnpos[c] = s;
				columns[c] = &line2[s];

				/* Figure out if we have the optional link column */
				if(sitetype->linkcol && c >= abs(sitetype->sizecol)-1 && c >= abs(sitetype->linkcol)-1)
				{
					if(isdigit(buffer[linestart + columnpos[abs(sitetype->linkcol)-1]]))
					{
						if(!is_valid(columns, 0, sitetype))
						{
							if(is_valid(columns, 1, sitetype))
								increment = 1;
						}
					}
				}

				/* If we have an entry with a sufficient number of columns */
				if(c >= (abs(sitetype->filecol) - 1 - increment) && c >= (abs(sitetype->sizecol) - 1 - increment) && c >= (abs(sitetype->modecol) - increment))
				{
					int thisinc, filelen;

					if(sitetype->linkcol < abs(sitetype->filecol))
					{
						thisinc = increment;
						filelen = (l - columnpos[abs(sitetype->filecol) - 1 - thisinc]);
					}
					else
					{
						thisinc = 0;
						filelen = (l - columnpos[abs(sitetype->filecol) - 1]);
					}

					if(strcmp(columns[abs(sitetype->filecol) - 1 - thisinc], "..") != 0 && strcmp(columns[abs(sitetype->filecol) - 1 - thisinc], "."))
					{
						tmp->next = (Directory *)calloc(1, sizeof(Directory));
						tmp = tmp->next;

						tmp->entry = malloc(filelen+1);

						strncpy(tmp->entry, &buffer[linestart + columnpos[labs(sitetype->filecol) - 1 - thisinc]], filelen);
						tmp->entry[filelen] = 0;

						if(sitetype->linkcol < abs(sitetype->sizecol))
							thisinc = increment;
						else
							thisinc = 0;

						removecommas(&line[columnpos[abs(sitetype->sizecol) - 1 - thisinc]], tmpbuf, 100);
						tmp->size = atoi(tmpbuf);

						if(sitetype->linkcol < abs(sitetype->timecol))
							thisinc = increment;
						else
							thisinc = 0;

						if(sitetype->timecol < 0)
							tmp->time.hours = findhour(columns[abs(sitetype->timecol) - 1 - thisinc]);
						else if(sitetype->timecol > 0)
							tmp->time.hours = findhour(columns[sitetype->timecol - 1 - thisinc]);

						if(sitetype->linkcol < abs(sitetype->timecol))
							thisinc = increment;
						else
							thisinc = 0;

						if(sitetype->timecol < 0)
							tmp->time.minutes = findmin(&buffer[linestart + columnpos[abs(sitetype->timecol) - 1 - thisinc]]);
						else if(sitetype->timecol > 0)
							tmp->time.minutes = findmin(&buffer[linestart + columnpos[sitetype->timecol - 1 - thisinc]]);
						if(sitetype->timecol < 0)
							tmp->time.seconds = findsec(&buffer[linestart + columnpos[abs(sitetype->timecol) - 1 - thisinc]]);
						else if(sitetype->timecol > 0)
							tmp->time.seconds = findsec(&buffer[linestart + columnpos[sitetype->timecol - 1 - thisinc]]);

						if(sitetype->linkcol < abs(sitetype->daycol))
							thisinc = increment;
						else
							thisinc = 0;

						if(sitetype->daycol < 0)
							tmp->date.day = findday(&buffer[linestart + columnpos[abs(sitetype->daycol) - 1 - thisinc]]);
						else if(sitetype->daycol > 0)
							tmp->date.day = findday(&buffer[linestart + columnpos[sitetype->daycol - 1 - thisinc]]);

						if(sitetype->linkcol < abs(sitetype->monthcol))
							thisinc = increment;
						else
							thisinc = 0;

						if(sitetype->monthcol < 0)
							tmp->date.month = findmonth(&buffer[linestart + columnpos[abs(sitetype->monthcol) - 1 - thisinc]]);
						else if(sitetype->monthcol > 0)
							tmp->date.month = findmonth(&buffer[linestart + columnpos[abs(sitetype->monthcol) - 1 - thisinc]]);

						if(sitetype->linkcol < abs(sitetype->yearcol))
							thisinc = increment;
						else
							thisinc = 0;

						if(sitetype->yearcol < 0)
							tmp->date.year = findyear(&buffer[linestart + columnpos[abs(sitetype->yearcol) - 1 - thisinc]]);
						else if(sitetype->yearcol > 0)
							tmp->date.year = findyear(&buffer[linestart + columnpos[abs(sitetype->yearcol) - 1 - thisinc]]);

						if(sitetype->modecol != 0)
						{
							if(sitetype->linkcol < sitetype->modecol)
								thisinc = increment;
							else
								thisinc = 0;

							sitetype->modecol = abs(sitetype->modecol);
							if(columns[sitetype->modecol - 1 - thisinc][sitetype->dir-1] == sitetype->dirchar)
								tmp->type = DIRDIR;
							else if(columns[sitetype->modecol - 1 - thisinc][sitetype->dir-1] == sitetype->linkchar)
								tmp->type = DIRLINK;
							else
								tmp->type = DIRFILE;
						}
						tmp->next = NULL;
					}
				}
				free(line2);
			}
			else /* We aren't in columnized mode */
			{
				if(strlen(line) > sitetype->filestart && strlen(line) > sitetype->sizestart)
				{
					if(sitetype->fileend > -1)
						line[sitetype->fileend-1] = 0;
					line[sitetype->sizeend-1] = 0;
					if(strcmp(&line[sitetype->filestart-1], ".") != 0 && strcmp(&line[sitetype->filestart-1], "..") != 0)
					{
						tmp->next = (Directory *)malloc(sizeof(Directory));
						tmp = tmp->next;
						tmp->entry = strdup(&line[sitetype->filestart-1]);
						removecommas(&line[sitetype->sizestart-1], tmpbuf, (sitetype->sizeend - sitetype->sizestart) + 1);
						tmp->size = atoi(tmpbuf);
						tmp->time.hours = findhour(&line[sitetype->timestart-1]);
						tmp->time.minutes = findmin(&line[sitetype->timestart-1]);
						tmp->time.seconds = findsec(&line[sitetype->timestart-1]);
						tmp->date.day = findday(&line[sitetype->datestart-1]);
						tmp->date.month = findmonth(&line[sitetype->datestart-1]);
						tmp->date.year = findyear(&line[sitetype->datestart-1]) + 1980;
						if(line[sitetype->dir-1] == sitetype->dirchar)
							tmp->type = DIRDIR;
						else if(line[sitetype->dir-1] == sitetype->linkchar)
							tmp->type = DIRLINK;
						else
							tmp->type = DIRFILE;

						tmp->next=NULL;
					}
				}

			}

			linestart = z+1;
		}
	}
	writeconsole(lsite, locale_string("Retrieved remote directory.", 37));
	setstatustext(lsite, locale_string("Remote directory, connected.", 38));
	sortdir(lsite);
}

/* Creates a Directory listing on the current site from the local filesystem. */
void loadlocaldir(SiteTab *lsite)
{
	Directory *tmp = NULL;
	char tmpbuf[1024];
	DIR *dir;
	struct dirent *ent;
	struct stat bleah;
	struct tm *filetime;
#if defined(__EMX__) || defined(__OS2__) || defined(__WIN32__) || defined(WINNT)
	int j;
#endif

	writeconsole(lsite, locale_string("Sending local LIST command.", 39));
	lsite->dir = NULL;
	if(!lsite->url || !*(lsite->url))
	{
		char *mycwd = (char *)getcwd(NULL, 1024);

		if(mycwd)
		{
			strcpy(tmpbuf, mycwd);
			if(lsite->url)
				free(lsite->url);
			lsite->url = mycwd;
			dw_window_set_text(lsite->directory, mycwd);
		}
		else
			strcpy(tmpbuf, ".");
	}
	else
		strcpy(tmpbuf, lsite->url);

	if(!(dir = opendir(tmpbuf)))
	{
		writeconsole(lsite, locale_string("Error retrieving local directory!", 40));
		lsite->connected = FALSE;
		setstatustext(lsite, locale_string("Local directory, disconnected.", 41));
		return;
	}

	while((ent = readdir(dir)) != 0)
	{
		if(strcmp(ent->d_name, ".") != 0
#ifdef __WIN32__
			/* Filter out hidden and system files on Windows */
			&& !(ent->d_attribute & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN))
#endif
		  )
		{
			if(lsite->dir == NULL)
			{
				lsite->dir = (Directory *)malloc(sizeof(Directory));
				tmp = lsite->dir;
			}
			else
			{
				tmp->next = (Directory *)malloc(sizeof(Directory));
				tmp = tmp->next;
			}


			tmp->entry = strdup(ent->d_name);

			if(strcmp(ent->d_name, "..") == 0)
			{
				tmp->time.hours = 0;
				tmp->time.minutes = 0;
				tmp->time.seconds = 0;
				tmp->date.day = 0;
				tmp->date.month = 0;
				tmp->date.year = 0;
				tmp->type = DIRDIR;
				tmp->next=NULL;
			}
			else
			{
				strcpy(tmpbuf, lsite->url);
				if(tmpbuf[strlen(tmpbuf)-1] != '\\' && tmpbuf[strlen(tmpbuf)-1] != '/')
					strcat(tmpbuf, DIRSEP);
				strcat(tmpbuf, ent->d_name);

				stat(tmpbuf, &bleah);

				tmp->size = bleah.st_size;
				filetime = localtime(&bleah.st_mtime);

				if(filetime)
				{
					tmp->time.hours = filetime->tm_hour;
					tmp->time.minutes = filetime->tm_min;
					tmp->time.seconds = filetime->tm_sec;
					tmp->date.day = filetime->tm_mday;
					tmp->date.month = filetime->tm_mon + 1;
					tmp->date.year = filetime->tm_year + 1900;
				}

				if(S_ISDIR(bleah.st_mode))
				{
					tmp->type = DIRDIR;
					tmp->size = 0;
				}
#if !defined(__OS2__) && !defined(__EMX__) && !defined(__WIN32__) && !defined(WINNT)
				else if(S_ISLNK(bleah.st_mode))
					tmp->type = DIRLINK;
#endif
				else
					tmp->type = DIRFILE;
			}

			tmp->next=NULL;
		}
	}
	closedir(dir);
	sortdir(lsite);
	tmp = lastdir(lsite);
#if defined(__EMX__) || defined(__OS2__) || defined(__WIN32__) || defined(WINNT)
	for(j=2;j<27;j++)
	{
		if(isdrive(j) > 0)
		{
			sprintf(tmpbuf, locale_string("Drive %c:", 42), ('A'+j)-1);
			if(lsite->dir == NULL)
			{
				lsite->dir = (Directory *)malloc(sizeof(Directory));
				tmp = lsite->dir;
			}
			else
			{
				tmp->next = (Directory *)malloc(sizeof(Directory));
				tmp = tmp->next;
			}

			tmp->entry = strdup(tmpbuf);
			tmp->size = 0;
			tmp->time.hours = 0;
			tmp->time.minutes = 0;
			tmp->time.seconds = 0;
			tmp->date.day = 0;
			tmp->date.month = 0;
			tmp->date.year = 0;
			tmp->type = DIRDIR;
			tmp->next=NULL;
		}
	}
#endif

	lsite->connected = TRUE;
	writeconsole(lsite, locale_string("Retrieved local directory.", 43));
	setstatustext(lsite, locale_string("Local directory, connected.", 44));
}

/* Configures the columns of the directory listing on the left */
void setdir(SiteTab *lsite)
{
	char *titles[3];
	unsigned long flags[3] = {  DW_CFA_ULONG | DW_CFA_RIGHT | DW_CFA_HORZSEPARATOR | DW_CFA_SEPARATOR,
	DW_CFA_TIME | DW_CFA_LEFT | DW_CFA_HORZSEPARATOR | DW_CFA_SEPARATOR,
	DW_CFA_DATE | DW_CFA_LEFT | DW_CFA_HORZSEPARATOR | DW_CFA_SEPARATOR };

	titles[0] = locale_string("Size", 45);
	titles[1] = locale_string("Time", 46);
	titles[2] = locale_string("Date", 47);

	if(dw_filesystem_setup(lsite->ldir, flags, titles, 3))
		dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("Error Creating Container!", 48));
}

/* Configures the columns of the queue listing on the right */
void setqueue(SiteTab *lsite)
{
	char *titles[5];
	unsigned long flags[5] = {  DW_CFA_STRING | DW_CFA_LEFT | DW_CFA_HORZSEPARATOR | DW_CFA_SEPARATOR,
								DW_CFA_STRING | DW_CFA_LEFT | DW_CFA_HORZSEPARATOR | DW_CFA_SEPARATOR,
								DW_CFA_ULONG | DW_CFA_RIGHT | DW_CFA_HORZSEPARATOR | DW_CFA_SEPARATOR,
								DW_CFA_TIME | DW_CFA_LEFT | DW_CFA_HORZSEPARATOR | DW_CFA_SEPARATOR,
								DW_CFA_DATE | DW_CFA_RIGHT | DW_CFA_HORZSEPARATOR | DW_CFA_SEPARATOR };

	titles[0] = locale_string("Destination", 49);
	titles[1] = locale_string("Directory", 50);
	titles[2] = locale_string("Size",51);
	titles[3] = locale_string("Time", 52);
	titles[4] = locale_string("Date", 53);

	if(dw_filesystem_setup(lsite->rqueue, flags, titles, 5))
		dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("Error Creating Container!", 48));
}

/* Clears all items in the queue list */
void clearq(SiteTab *lsite)
{
	dw_container_clear(lsite->rqueue, TRUE);
}

/* Inserts a list of items into the queue list */
void drawq(SiteTab *lsite)
{
	Queue *tmp;
	int count = 0, z;
	void *containerinfo;
	unsigned long size;
	HICN thisicon;
	CTIME time;
	CDATE date;

	tmp = lsite->queue;

	while(tmp != NULL)
	{
		count++;
		tmp=tmp->next;
	}
	tmp = lsite->queue;

	clearq(lsite);

	containerinfo = dw_container_alloc(lsite->rqueue, count);

	for(z=0;z<count;z++)
	{
		if(tmp->type == DIRDIR)
		{
			size = 0;
			thisicon = foldericon;
		}
		else if(tmp->type == DIRLINK)
		{
			size = 0;
			thisicon = linkicon;
		}
		else
		{
			size = tmp->size;
			thisicon = fileicon;
		}

		dw_filesystem_set_file(lsite->rqueue, containerinfo, z, tmp->srcfilename, thisicon);
		dw_filesystem_set_item(lsite->rqueue, containerinfo, 0, z, &tmp->site);
		dw_filesystem_set_item(lsite->rqueue, containerinfo, 1, z, &tmp->destdirectory);
		dw_filesystem_set_item(lsite->rqueue, containerinfo, 2, z, &size);

		time.seconds = tmp->time.seconds;
		time.minutes = tmp->time.minutes;
		time.hours = tmp->time.hours;
		dw_filesystem_set_item(lsite->rqueue, containerinfo, 3, z, &time);

		date.day = tmp->date.day;
		date.month = tmp->date.month;
		date.year = tmp->date.year;
		dw_filesystem_set_item(lsite->rqueue, containerinfo, 4, z, &date);

		dw_container_set_row_title(containerinfo, z, tmp->srcfilename);
		tmp=tmp->next;
	}

	dw_container_insert(lsite->rqueue, containerinfo, count);
	if(optimize)
		dw_container_optimize(lsite->rqueue);
}

/* Clears all items from the directory list */
void cleardir(SiteTab *lsite)
{
	dw_container_clear(lsite->ldir, TRUE);
}

/* Inserts the list of items into the directory listing */
void drawdir(SiteTab *lsite)
{
	Directory *tmp;
	int count = 0, z;
	void *containerinfo;
	unsigned long size;
	HICN thisicon;
	CTIME time;
	CDATE date;

	tmp = lsite->dir;

	while(tmp != NULL)
	{
		count++;
		tmp=tmp->next;
	}
	tmp = lsite->dir;

	cleardir(lsite);

	containerinfo = dw_container_alloc(lsite->ldir, count);

	for(z=0;z<count;z++)
	{
		if(tmp->type == DIRDIR)
		{
			size = 0;
			thisicon = foldericon;
		}
		else if(tmp->type == DIRLINK)
		{
			size = 0;
			thisicon = linkicon;
		}
		else
		{
			size = tmp->size;
			thisicon = fileicon;
		}

		dw_filesystem_set_file(lsite->ldir, containerinfo, z, tmp->entry, thisicon);
		dw_filesystem_set_item(lsite->ldir, containerinfo, 0, z, &size);

		time.seconds = tmp->time.seconds;
		time.minutes = tmp->time.minutes;
		time.hours = tmp->time.hours;
		dw_filesystem_set_item(lsite->ldir, containerinfo, 1, z, &time);

		date.day = tmp->date.day;
		date.month = tmp->date.month;
		date.year = tmp->date.year;
		dw_filesystem_set_item(lsite->ldir, containerinfo, 2, z, &date);

		dw_container_set_row_title(containerinfo, z, tmp->entry);

		tmp=tmp->next;
	}

	dw_container_insert(lsite->ldir, containerinfo, count);
	if(optimize)
		dw_container_optimize(lsite->ldir);
}

/* Returns a pointer to a sitetab based on the title text or NULL if not found */
SiteTab *findsite(char *title)
{
	int z;
	char *buffer;

	for(z=0;z<CONNECTION_LIMIT;z++)
	{
		if(alive[z] == TRUE)
		{
			buffer = dw_window_get_text(site[z]->host_title);

			if(buffer && strcmp(buffer, title) == 0)
			{
				dw_free(buffer);
				return site[z];
			}
			if(buffer)
				dw_free(buffer);
		}
	}
	return NULL;
}

/* Return true if the passed title exists on a page other than pageid */
int testtitle(char *title, int pageid)
{
	int z;
	char *buffer;

	for(z=0;z<CONNECTION_LIMIT;z++)
	{
		if(alive[z] == TRUE)
		{
			if(site[z]->pageid != pageid)
			{
				buffer = dw_window_get_text(site[z]->host_title);

				if(buffer && strcmp(buffer, title) == 0)
				{
					dw_free(buffer);
					return TRUE;
				}
				dw_free(buffer);
			}
		}
	}
	return FALSE;
}

/* Make sure this tab title is unique, if not make it unique */
void verifytitle(SiteTab *thissite)
{
	int num = 2;
	char *oldname = dw_window_get_text(thissite->host_title);
	char *newname = (oldname ? strdup(oldname) : NULL);
	char *tmp;

	if(!newname)
	{
		if(oldname)
			dw_free(oldname);
		return;
	}

	while(testtitle(newname, thissite->pageid))
	{
		free(newname);
		newname = malloc(strlen(oldname)+5);

		sprintf(newname, "%s #%d", oldname, num);
		num++;
	}

	if(strcmp(newname, oldname) == 0)
	{
		free(newname);
		dw_free(oldname);
		return;
	}
	dw_window_set_text(thissite->host_title, newname);
	tmp = thissite->hosttitle;
	thissite->hosttitle = newname;
	free(tmp);
	settabs();
}

/* A generic callback function to handle freeing of memory when
 * the user presses cancel in a dialog.
 */
int DWSIGNAL generic_cancel(HWND window, void *data)
{
	UserEntry *param = (UserEntry *)data;

	if(param)
	{
		dw_window_destroy(param->window);
		if(param->busy)
			*param->busy = 0;
		if(param->data)
			free(param->data);
		free(param);
	}
	return FALSE;
}

/* Callback to handle an OK press in the preferences dialog */
void DWSIGNAL preferences_ok(HWND window, void *data)
{
	UserEntry *param = (UserEntry *)data;

	if(param)
	{
		HWND *handles = (HWND *)param->data;

		if(handles)
		{
			openall = dw_checkbox_get(handles[0]);
			reversefxp = dw_checkbox_get(handles[1]);
			showpassword = dw_checkbox_get(handles[2]);
			ftptimeout = dw_spinbutton_get_pos(handles[3]);
			retrymax = dw_spinbutton_get_pos(handles[4]);
			cachemax = dw_spinbutton_get_pos(handles[5]);
			urlsave = dw_checkbox_get(handles[6]);
			bandwidthlimit = dw_spinbutton_get_pos(handles[7]);
			optimize = dw_checkbox_get(handles[8]);
			handyftp_locale = dw_listbox_selected(handles[9]);
			saveconfig();
			free(handles);
		}

		if(param->busy)
			*param->busy = 0;

		dw_window_destroy(param->window);
		free(param);
	}
}

/* Callback to handle an OK press in the properties dialog */
void updatecurrentsettings(int page)
{
	char *tempbuffer;
	SiteTab *propsite = site[page];

	if(!alive[page] || !propsite)
		return;

	tempbuffer = dw_window_get_text(propsite->host_title);
	if(propsite->hosttitle)
		free(propsite->hosttitle);
	propsite->hosttitle = strdup(tempbuffer);
	dw_free(tempbuffer);

	tempbuffer = dw_window_get_text(propsite->host_name);
	if(propsite->hostname)
		free(propsite->hostname);
	propsite->hostname = strdup(tempbuffer);
	dw_free(tempbuffer);

	tempbuffer = dw_window_get_text(propsite->user_name);
	if(propsite->username)
		free(propsite->username);
	propsite->username = strdup(tempbuffer);
	dw_free(tempbuffer);

	tempbuffer = dw_window_get_text(propsite->pass_word);
	if(propsite->password)
		free(propsite->password);
	propsite->password = strdup(tempbuffer);
	dw_free(tempbuffer);

	tempbuffer = dw_window_get_text(propsite->directory);
	if(propsite->url)
		free(propsite->url);
	propsite->url = strdup(tempbuffer);
	dw_free(tempbuffer);
	propsite->port = dw_spinbutton_get_pos(propsite->port_num);
}

/* Create the about box */
void about(void)
{
	HWND infowindow, mainbox, logo, okbutton, buttonbox, stext, mle;
	UserEntry *param = malloc(sizeof(UserEntry));
	ULONG flStyle = DW_FCF_TITLEBAR | DW_FCF_SHELLPOSITION | DW_FCF_DLGBORDER;
	ULONG point = -1;
	char *greets = "Thanks to the OS/2 Netlabs, OS2.org, Narayan Desai, Terje Flaaronning, " \
		"Geoff Freimark, Vidas Simkus, Jeff LeClere, Valerie Smith, Gene Akins, " \
		"Bob McLennan, Samuel Audet, Colten Edwards, Achim Hasenmueller, " \
		"Taneli Leppa, Mellissa Bachorek, Timur Tabi, Kendall Bennett, Peter Nielsen, " \
		"Tom Ryan, Patrick Haller, Markus Montkowski, Sander van Leeuwen, " \
		"Nenad Milenkovic, Adrian Gschwend, Bart van Leeuwen, Andreas Linde, " \
		"Paul Smedley, Eirik Overby, Cristy Wojdac, Corey Glodek, Barry McGhee, " \
		"Dean Jens, Gantry Zettler, Jennifer Needham, David Walluck, dink, " \
		"Jason Slagle, Brian Weiss, Antony Curtis, Matt Watson, David Ronis, " \
		"Warren Rees, Jennifer Canterbury, Rob Campbell, Nicholas Hockey, " \
		"Dimitris 'sehh' Michelinakis, Phucilage, Danny Elias, Damion Rodriguez, " \
		"Keith Simonsen, Joan Condal, Ken Ames, Elbert Pol, Henk Pol, " \
		"Christian Langanke, my parents, the love of my life,\r\n" \
		"\r\nAnd of course Handy for inspiration. ;)";

	if(in_about)
	{
		dw_window_show(in_about);
		return;
	}

	in_about = infowindow = dw_window_new(HWND_DESKTOP, locale_string("About HandyFTP", 54), flStyle);

	mainbox = dw_box_new(BOXVERT, 5);

	dw_box_pack_start(infowindow, mainbox, 0, 0, TRUE, TRUE, 0);

	buttonbox = dw_box_new(BOXHORZ, 0);

	dw_box_pack_start(mainbox, buttonbox, 0, 0, TRUE, FALSE, 0);

	logo = dw_bitmap_new(100);

	dw_window_set_bitmap(logo, LOGO, NULL);

	dw_box_pack_start(buttonbox, 0, 50, 30, TRUE, FALSE, 0);
	dw_box_pack_start(buttonbox, logo, 337, 131, FALSE, FALSE, 2);
	dw_box_pack_start(buttonbox, 0, 50, 30, TRUE, FALSE, 0);

	stext = dw_text_new("HandyFTP (c) 2000-2011 Brian Smith", 0);
	dw_window_set_style(stext, DW_DT_CENTER | DW_DT_VCENTER, DW_DT_CENTER | DW_DT_VCENTER);
	dw_box_pack_start(mainbox, stext, 10, 20, TRUE, TRUE, 0);

	mle = dw_mle_new(100L);

	dw_box_pack_start(mainbox, mle, 130, 150, TRUE, TRUE, 2);

	dw_mle_set_editable(mle, FALSE);
#if __WIN32__
	dw_window_set_color(mle, DW_CLR_BLACK, DW_CLR_WHITE);
#endif
	dw_mle_set_word_wrap(mle, TRUE);

	/* Buttons */
	buttonbox = dw_box_new(BOXHORZ, 0);

	dw_box_pack_start(mainbox, buttonbox, 0, 0, TRUE, TRUE, 0);

	okbutton = dw_button_new("Ok", 1001L);

	dw_box_pack_start(buttonbox, 0, 50, 30, TRUE, FALSE, 0);
	dw_box_pack_start(buttonbox, okbutton, 50, 40, FALSE, FALSE, 2);
	dw_box_pack_start(buttonbox, 0, 50, 30, TRUE, FALSE, 0);

	param->window = infowindow;
	param->data = NULL;
	param->busy = &in_about;

	point = dw_mle_import(mle, greets, point);
	dw_mle_set_cursor(mle, 0);
	dw_mle_set_visible(mle, 0);

	dw_signal_connect(okbutton, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(generic_cancel), (void *)param);

	dw_window_set_size(infowindow, 460, 400);

	dw_window_show(infowindow);
}

/* Create the preferences dialog */
void preferences(void)
{
	HWND entrywindow, mainbox, hbox, cancelbutton, okbutton, buttonbox,
		xbox, stext, *handles = malloc(10 * sizeof(HWND));
	UserEntry *param = malloc(sizeof(UserEntry));
	ULONG flStyle = DW_FCF_TITLEBAR | DW_FCF_SHELLPOSITION | DW_FCF_DLGBORDER;

	if(in_preferences)
	{
		dw_window_show(in_preferences);
		return;
	}

	in_preferences = entrywindow = dw_window_new(HWND_DESKTOP, locale_string("Preferences", 55), flStyle);

	xbox = dw_box_new(BOXVERT, 5);

	dw_box_pack_start(entrywindow, xbox, 0, 0, TRUE, TRUE, 0);

	mainbox = dw_groupbox_new(BOXVERT, 8, locale_string("General", 56));

	dw_box_pack_start(xbox, mainbox, 0, 0, TRUE, FALSE, 0);

	/* First Line */
	hbox = dw_box_new(BOXHORZ, 0);

	dw_box_pack_start(mainbox, hbox, 0, 0, TRUE, FALSE, 0);

	handles[0] = dw_checkbox_new(locale_string("Open all servers", 57), 0);
	dw_checkbox_set(handles[0],openall);

	dw_box_pack_start(hbox, handles[0], 40, 22, TRUE, FALSE, 0);

	stext = dw_text_new(locale_string("Timeout", 62), 0);

	dw_window_set_style(stext, DW_DT_VCENTER, DW_DT_VCENTER);

	dw_box_pack_start(hbox, stext, 120, 22, FALSE, FALSE, 0);

	/* Timeout */
	handles[3] = dw_spinbutton_new("", 100L);

	dw_spinbutton_set_limits(handles[3], 60L, 0L);
	dw_spinbutton_set_pos(handles[3], ftptimeout);

	dw_box_pack_start(hbox, handles[3], 60, 22, FALSE, FALSE, 1);

	/* Second Line */
	hbox = dw_box_new(BOXHORZ, 0);

	dw_box_pack_start(mainbox, hbox, 0, 0, TRUE, FALSE, 0);

	handles[1] = dw_checkbox_new(locale_string("Passive FTP", 58), 0);
	dw_checkbox_set(handles[1],reversefxp);

	dw_box_pack_start(hbox, handles[1], 40, 22, TRUE, FALSE, 0);

	stext = dw_text_new(locale_string("Retries", 63), 0);

	dw_window_set_style(stext, DW_DT_VCENTER, DW_DT_VCENTER);

	dw_box_pack_start(hbox, stext, 120, 22, FALSE, FALSE, 0);

	/* Retries */
	handles[4] = dw_spinbutton_new("", 100L);

	dw_spinbutton_set_limits(handles[4], 60L, 0L);
	dw_spinbutton_set_pos(handles[4], retrymax);

	dw_box_pack_start(hbox, handles[4], 60, 22, FALSE, FALSE, 1);

	/* Third Line */
	hbox = dw_box_new(BOXHORZ, 0);

	dw_box_pack_start(mainbox, hbox, 0, 0, TRUE, FALSE, 0);

	handles[2] = dw_checkbox_new(locale_string("Show password", 59), 0);
	dw_checkbox_set(handles[2],showpassword);

	dw_box_pack_start(hbox, handles[2], 40, 22, TRUE, FALSE, 0);

	stext = dw_text_new(locale_string("Cache Limit", 64), 0);

	dw_window_set_style(stext, DW_DT_VCENTER, DW_DT_VCENTER);

	dw_box_pack_start(hbox, stext, 120, 22, FALSE, FALSE, 0);

	/* Cache limit */
	handles[5] = dw_spinbutton_new("", 100L);

	dw_spinbutton_set_limits(handles[5], 100L, 0L);
	dw_spinbutton_set_pos(handles[5], cachemax);

	dw_box_pack_start(hbox, handles[5], 60, 22, FALSE, FALSE, 1);

	/* Fourth Line */
	hbox = dw_box_new(BOXHORZ, 0);

	dw_box_pack_start(mainbox, hbox, 0, 0, TRUE, FALSE, 0);

	handles[6] = dw_checkbox_new(locale_string("Save URLs", 60), 0);
	dw_checkbox_set(handles[6],urlsave);

	dw_box_pack_start(hbox, handles[6], 40, 22, TRUE, FALSE, 0);

	stext = dw_text_new(locale_string("Bandwidth Limit", 65), 0);

	dw_window_set_style(stext, DW_DT_VCENTER, DW_DT_VCENTER);

	dw_box_pack_start(hbox, stext, 120, 22, FALSE, FALSE, 0);

	/* Bandwidth limit */
	handles[7] = dw_spinbutton_new("", 100L);

	dw_spinbutton_set_limits(handles[7], 1000L, 0L);
	dw_spinbutton_set_pos(handles[7], bandwidthlimit);

	dw_box_pack_start(hbox, handles[7], 60, 22, FALSE, FALSE, 1);

	/* Fifth Line */
	hbox = dw_box_new(BOXHORZ, 0);

	dw_box_pack_start(mainbox, hbox, 0, 0, TRUE, FALSE, 0);

	handles[8] = dw_checkbox_new(locale_string("Optimize columns", 61), 0);
	dw_checkbox_set(handles[8],optimize);

	dw_box_pack_start(hbox, handles[8], 40, 22, TRUE, FALSE, 0);

	stext = dw_text_new(locale_string("Locale", 66), 0);

	dw_window_set_style(stext, DW_DT_VCENTER, DW_DT_VCENTER);

	dw_box_pack_start(hbox, stext, 70, 22, FALSE, FALSE, 0);

	/* Locale */
	handles[9] = dw_combobox_new("", 100L);

	dw_listbox_append(handles[9], "English");
	dw_listbox_append(handles[9], "Deutsch");
	dw_listbox_append(handles[9], "Dansk");

	dw_listbox_select(handles[9], handyftp_locale, TRUE);

	dw_box_pack_start(hbox, handles[9], 110, 22, FALSE, FALSE, 0);

	/* Buttons */
	buttonbox = dw_box_new(BOXHORZ, 5);

	dw_box_pack_start(xbox, buttonbox, 0, 0, TRUE, TRUE, 2);

	okbutton = dw_button_new(locale_string("Ok", 67), 1001L);

	dw_box_pack_start(buttonbox, okbutton, 40, 30, TRUE, TRUE, 2);

	cancelbutton = dw_button_new(locale_string("Cancel", 68), 1002L);

	dw_box_pack_start(buttonbox, cancelbutton, 40, 30, TRUE, TRUE, 2);

	/* Padding to compensate for the button box on the left. */
	dw_box_pack_start(buttonbox, 0, 40, 30, TRUE, TRUE, 4);

	param->page = currentpage;
	param->window = entrywindow;
	param->filename = NULL;
	param->data = (void *)handles;
	param->busy = &in_preferences;

	dw_signal_connect(okbutton, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(preferences_ok), (void *)param);
	dw_signal_connect(cancelbutton, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(generic_cancel), (void *)param);

	dw_window_set_size(entrywindow, 375, 240);

	dw_window_show(entrywindow);
}

void DWSIGNAL IPS_kill_user(HWND window, void *data)
{
	if(site[IPS_page]->status == STATUSIDLE)
	{
		char *text = dw_container_query_start(IPS_handle, DW_CRA_SELECTED);

		if(text)
		{
			int sock = atoi(text);

			sprintf(site[IPS_page]->thrdcommand, "SITE RADM KILL %d\r\nSITE RADM LIST sockets\r\n", sock);
			sendthread(THRDRAW, IPS_page);
			clearadmin();
		}
	}
	else
		dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("IPS site is not idle.", 69));
}

void DWSIGNAL IPS_restart(HWND window, void *data)
{
	if(site[IPS_page]->status == STATUSIDLE)
	{
		strcpy(site[IPS_page]->thrdcommand, "SITE RADM RESTART\r\n");
		sendthread(THRDRAW, IPS_page);
	}
	else
		dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("IPS site is not idle.", 70));
}

void DWSIGNAL IPS_shutdown(HWND window, void *data)
{
	if(site[IPS_page]->status == STATUSIDLE)
	{
		strcpy(site[IPS_page]->thrdcommand, "SITE RADM SHUTDOWN\r\n");
		sendthread(THRDRAW, IPS_page);
	}
	else
		dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("IPS site is not idle.", 71));
}

/* IPS Administration dialog */
void IPS(void)
{
	HWND entrywindow, mainbox, closebutton, killbutton, shutdownbutton, restartbutton,
		container, xbox, *handles = malloc(6 * sizeof(HWND));
	UserEntry *param = malloc(sizeof(UserEntry));
	ULONG flStyle = DW_FCF_TITLEBAR | DW_FCF_SHELLPOSITION | DW_FCF_SIZEBORDER;
	char *titles[5] = { "User",	"Address", "Activity", "Idle", "Socket" };
	unsigned long flags[5] = {  DW_CFA_STRING | DW_CFA_RIGHT | DW_CFA_HORZSEPARATOR | DW_CFA_SEPARATOR,
								DW_CFA_STRING | DW_CFA_LEFT | DW_CFA_HORZSEPARATOR | DW_CFA_SEPARATOR,
								DW_CFA_STRING | DW_CFA_LEFT | DW_CFA_HORZSEPARATOR | DW_CFA_SEPARATOR,
								DW_CFA_ULONG | DW_CFA_RIGHT | DW_CFA_HORZSEPARATOR | DW_CFA_SEPARATOR,
								DW_CFA_ULONG | DW_CFA_RIGHT | DW_CFA_HORZSEPARATOR | DW_CFA_SEPARATOR };


	titles[0] = locale_string("User", 72);
	titles[1] = locale_string("Address", 73);
	titles[2] = locale_string("Activity", 74);
	titles[3] = locale_string("Idle", 75);
	titles[4] = locale_string("Socket", 76);

	if(in_IPS)
	{
		dw_window_show(in_IPS);
		return;
	}

	if(validatecurrentpage()==FALSE || site[(IPS_page = currentpage)]->connected == FALSE)
	{
		dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("Could not validate IPS site.", 77));
		return;
	}

	in_IPS = entrywindow = dw_window_new(HWND_DESKTOP, locale_string("IPS Administration", 78), flStyle);

	xbox = dw_box_new(BOXHORZ, 0);

	dw_box_pack_start(entrywindow, xbox, 0, 0, TRUE, TRUE, 0);

	/* Buttons */
	mainbox = dw_box_new(BOXVERT,0);

	dw_box_pack_start(xbox, mainbox, 0, 0, FALSE, TRUE, 4);

	closebutton = dw_button_new(locale_string("Close", 79), 1001L);

	dw_box_pack_start(mainbox, closebutton, 100, 30, FALSE, FALSE, 4);

	killbutton = dw_button_new(locale_string("Kill", 80), 1002L);

	dw_box_pack_start(mainbox, killbutton, 100, 30, FALSE, FALSE, 4);

	shutdownbutton = dw_button_new(locale_string("Shutdown", 81), 1003L);

	dw_box_pack_start(mainbox, shutdownbutton, 100, 30, FALSE, FALSE, 4);

	restartbutton = dw_button_new(locale_string("Restart", 82), 1003L);

	dw_box_pack_start(mainbox, restartbutton, 100, 30, FALSE, FALSE, 4);

	/* Pack in some blank space */
	dw_box_pack_start(mainbox, 0, 10, 10, TRUE, TRUE, 0);

	IPS_handle = handles[0] = container = dw_container_new(0L, FALSE);

	dw_box_pack_start(xbox, container, 300, 200, TRUE,TRUE, 4);

	if(dw_container_setup(container, flags, titles, 5, 2))
		dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("Error Creating Container!", 48));

	param->page = currentpage;
	param->window = entrywindow;
	param->filename = NULL;
	param->data = (void *)handles;
	param->busy = &in_IPS;

	dw_signal_connect(killbutton, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(IPS_kill_user), (void *)param);
	dw_signal_connect(shutdownbutton, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(IPS_shutdown), (void *)param);
	dw_signal_connect(restartbutton, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(IPS_restart), (void *)param);
	dw_signal_connect(closebutton, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(generic_cancel), (void *)param);

	dw_window_set_size(entrywindow, 450, 350);

	dw_window_show(entrywindow);

	dw_container_clear(container, TRUE);
}

/* When OK is pressed in the administration dialog */
void DWSIGNAL administration_ok(HWND window, void *data)
{
	UserEntry *param = (UserEntry *)data;

	if(param)
	{
		HWND *handles = param->data;
		int state = 0;

		if(param->data)
		{
			state = dw_checkbox_get(handles[0]);
			free(param->data);
		}
		dw_window_destroy(param->window);
		if(param->busy)
			*param->busy = 0;
		free(param);
		if(state)
			IPS();
		else
			dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_INFORMATION, "Currently only IPS is supported.");
	}
}

/* Create the site administration dialog */
void administrate(void)
{
	HWND entrywindow, mainbox, cancelbutton, okbutton, buttonbox,
		xbox, *handles = malloc(6 * sizeof(HWND));
	UserEntry *param = malloc(sizeof(UserEntry));
	ULONG flStyle = DW_FCF_TITLEBAR | DW_FCF_SHELLPOSITION | DW_FCF_DLGBORDER;

	if(in_administration)
	{
		dw_window_show(in_administration);
		return;
	}

	in_administration = entrywindow = dw_window_new(HWND_DESKTOP, locale_string("Administration", 83), flStyle);

	xbox = dw_box_new(BOXVERT, 5);

	dw_box_pack_start(entrywindow, xbox, 0, 0, TRUE, TRUE, 0);

	mainbox = dw_groupbox_new(BOXVERT, 8, locale_string("Site Type", 84));

	dw_box_pack_start(xbox, mainbox, 0, 0, TRUE, TRUE, 0);

	handles[0] = dw_radiobutton_new(locale_string("InetPowerServer", 85), 0);

	dw_box_pack_start(mainbox, handles[0], 50, 20, TRUE, TRUE, 4);

	handles[1] = dw_radiobutton_new(locale_string("Other", 86), 0);

	dw_box_pack_start(mainbox, handles[1], 50, 20, TRUE, TRUE, 4);

	/* Pack in some blank space */
	dw_box_pack_start(mainbox, 0, 50, 40, TRUE, TRUE, 4);

	/* Buttons */
	buttonbox = dw_box_new(BOXHORZ, 10);

	dw_box_pack_start(xbox, buttonbox, 0, 0, TRUE, TRUE, 2);

	okbutton = dw_button_new(locale_string("Ok", 67), 1001L);

	dw_box_pack_start(buttonbox, okbutton, 40, 30, TRUE, TRUE, 2);

	cancelbutton = dw_button_new(locale_string("Cancel", 68), 1002L);

	dw_box_pack_start(buttonbox, cancelbutton, 40, 30, TRUE, TRUE, 2);

	/* Padding to compensate for the button box on the left. */
	dw_box_pack_start(buttonbox, 0, 40, 30, TRUE, TRUE, 4);

	param->page = currentpage;
	param->window = entrywindow;
	param->filename = NULL;
	param->data = (void *)handles;
	param->busy = &in_administration;

	dw_signal_connect(okbutton, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(administration_ok), (void *)param);
	dw_signal_connect(cancelbutton, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(generic_cancel), (void *)param);

	dw_window_set_size(entrywindow, 350, 225);

	dw_window_show(entrywindow);

	/* OS/2 (and possibly Windows) don't like setting the
	 * button's check state before it's laid out.
	 */
	dw_checkbox_set(handles[0],1);
}

/* Remove a user selection from a container (queue or directory) */
void unselectcontainer(ULONG id, int page)
{

}

/* Find the index of the file that the user has chosen */
int finddirindex(char *text, int page)
{
	Directory *tmp;
	int count=0;

	tmp = site[page]->dir;
	while(tmp != NULL)
	{
		if(strcmp(tmp->entry, text)==0)
			return count;
		count++;
		tmp=tmp->next;
	}
	return -1;
}

/* Find the index of the queued file that the user has chosen */
int findqueueindex(char *text, int page)
{
	Queue *tmp;
	int count=0;

	tmp = site[page]->queue;
	while(tmp != NULL)
	{
		if(strcmp(tmp->srcfilename, text)==0)
			return count;
		count++;
		tmp=tmp->next;
	}
	return -1;
}

/* Returns the file info about given index in the file list */
Directory *finddir(int index, int page)
{
	Directory *tmp;
	int count = 0;

	tmp = site[page]->dir;

	while(tmp != NULL)
	{
		if(index == count)
			return tmp;

		count++;
		tmp=tmp->next;
	}
	return NULL;
}

/* Normalizes a path to the appropriate platform */
void make_local(char *buf, char *dirsep)
{
	int z, len = strlen(buf);

	for(z=0;z<len;z++)
	{
		if(buf[z] == '/' || buf[z] == '\\')
			buf[z] = dirsep[0];
	}
}

/* Adds a file to the queue from the index in the given directory listing */
void addtoq(int index, int page)
{
	Queue *tmp, *last = NULL;
	Directory *tmp2;
	SiteTab *destsite;
	int count = 0;

	tmp = site[page]->queue;
	tmp2 = site[page]->dir;

	while(tmp2 != NULL)
	{
		if(count == index)
		{
			char *dwbuf;

			while(tmp != NULL)
			{
				last = tmp;
				tmp=tmp->next;
			}
			if(last == NULL)
			{
				site[page]->queue = (Queue *)malloc(sizeof(Queue));
				tmp = site[page]->queue;
			}
			else
			{
				last->next = (Queue *)malloc(sizeof(Queue));
				tmp = last->next;
			}
			tmp->srcdirectory = (char *)malloc(strlen(site[page]->url)+1);
			strcpy(tmp->srcdirectory, site[page]->url);
			tmp->srcfilename = (char *)malloc(strlen(tmp2->entry)+1);
			strcpy(tmp->srcfilename, tmp2->entry);
			tmp->type = tmp2->type;
			tmp->size = tmp2->size;
			tmp->time = tmp2->time;
			tmp->date = tmp2->date;
			dwbuf = dw_window_get_text(site[page]->destsite);
			tmp->site = strdup(dwbuf);
			dw_free(dwbuf);
			if((destsite = findsite(tmp->site)) != NULL)
			{
				if(site[page]->queuing)
				{
					int startlen = strlen(site[page]->queuing);
					int currlen = strlen(site[page]->url);

					/* Normalize the paths (minus the ending / or \) */
					if(site[page]->url[currlen-1] == '/' || site[page]->url[currlen-1] == '\\')
						currlen--;

					if(site[page]->queuing[startlen-1] == '/' || site[page]->queuing[startlen-1] == '\\')
						startlen--;

					/* If we aren't in the initial directory */
					if(currlen > startlen)
					{
						int destlen = strlen(destsite->url);

						if(destsite->url[destlen-1] == '/' || destsite->url[destlen-1] == '\\')
							destlen--;

						tmp->destdirectory = malloc(destlen + (currlen - startlen) + 1);
						strncpy(tmp->destdirectory, destsite->url, destlen);
						tmp->destdirectory[destlen] = 0;
						strcat(tmp->destdirectory, &site[page]->url[startlen]);

						/* Make sure the destination directory exits */
						if(strcmp(destsite->hostname, "local") == 0)
						{
							make_local(tmp->destdirectory, DIRSEP);
							makedir(tmp->destdirectory);
						}
						else
						{
							make_local(tmp->destdirectory, "/");
							strcpy(destsite->thrdcommand, tmp->destdirectory);
							sendthread(THRDMKDIR, destsite->page);
						}
					}
					else
						tmp->destdirectory = strdup(destsite->url);
				}
				else
					tmp->destdirectory = strdup(destsite->url);
			}
			else
				tmp->destdirectory = NULL;
			tmp->next = NULL;
			return;
		}
		count++;
		tmp2 = tmp2->next;
	}
}

/* Remove a queue entry by it's index */
void removefromq(int index, int page)
{
	Queue *tmp, *last = NULL;
	int count = 0;

	tmp = site[page]->queue;

	while(tmp != NULL)
	{
		if(count == index)
		{
			if(last)
				last->next = tmp->next;
			else
				site[page]->queue = tmp->next;
			free(tmp->srcdirectory);
			free(tmp->srcfilename);
			free(tmp->site);
			if(tmp->destdirectory)
				free(tmp->destdirectory);
			free(tmp);
			return;
		}
		count++;
		last = tmp;
		tmp = tmp->next;
	}
}

/* A generic dialog to get information from the user */
void user_query(char *entrytext, int page, char *filename, void *okfunctionname, void *cancelfunctionname)
{
	HWND entrywindow, mainbox, entryfield, cancelbutton, okbutton, buttonbox, stext;
	UserEntry *param = malloc(sizeof(UserEntry));
	ULONG flStyle = DW_FCF_TITLEBAR | DW_FCF_SHELLPOSITION | DW_FCF_DLGBORDER;

	entrywindow = dw_window_new(HWND_DESKTOP, "HandyFTP", flStyle);

	mainbox = dw_box_new(BOXVERT, 10);

	dw_box_pack_start(entrywindow, mainbox, 0, 0, TRUE, TRUE, 0);

	/* Archive Name */
	stext = dw_text_new(entrytext, 0);

	dw_window_set_style(stext, DW_DT_VCENTER, DW_DT_VCENTER);

	dw_box_pack_start(mainbox, stext, 130, 20, TRUE, TRUE, 2);

	entryfield = dw_entryfield_new("", 100L);
	dw_entryfield_set_limit(entryfield, _MAX_PATH);

	dw_box_pack_start(mainbox, entryfield, 130, 20, TRUE, TRUE, 4);

	/* Buttons */
	buttonbox = dw_box_new(BOXHORZ, 10);

	dw_box_pack_start(mainbox, buttonbox, 0, 0, TRUE, TRUE, 0);

	okbutton = dw_button_new(locale_string("Ok", 67), 1001L);

	dw_box_pack_start(buttonbox, okbutton, 130, 30, TRUE, TRUE, 2);

	cancelbutton = dw_button_new(locale_string("Cancel", 68), 1002L);

	param->page = page;
	param->window = entrywindow;
	param->entryfield = entryfield;
	param->filename = filename;
	param->data = NULL;
	param->busy = NULL;

	dw_box_pack_start(buttonbox, cancelbutton, 130, 30, TRUE, TRUE, 2);

	dw_signal_connect(okbutton, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(okfunctionname), (void *)param);
	dw_signal_connect(cancelbutton, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(cancelfunctionname), (void *)param);

	dw_window_default(entrywindow, entryfield);

	dw_window_click_default(entryfield, okbutton);

	dw_window_set_size(entrywindow, 340, 150);

	dw_window_show(entrywindow);
}

/* Callback to handle an OK press in the make directory dialog */
void DWSIGNAL mkdir_ok(HWND window, void *data)
{
	UserEntry *param = (UserEntry *)data;

	if(param)
	{
		char *tmp = dw_window_get_text(param->entryfield);

		if(tmp && strlen(tmp) > 0)
		{
			if(strcasecmp(site[param->page]->hostname, "local") == 0)
			{
				char path[2048];

				if(site[param->page]->url[strlen(site[param->page]->url)-1] == '\\')
					sprintf(path, "%s%s", site[param->page]->url, tmp);
				else
					sprintf(path, "%s%s%s", site[param->page]->url, DIRSEP, tmp);
				writeconsole(site[param->page], locale_string("Creating directory \"%s\"", 87), path);
				makedir(path);
				sendthread(THRDHARDREFRESH, param->page);
			}
			else
			{
				strcpy(site[param->page]->thrdcommand, tmp);
				sendthread(THRDMKDIR, param->page);
			}
		}

		if(tmp)
			dw_free(tmp);

		dw_window_destroy(param->window);
		free(param);
	}
}

/* Callback to handle an OK press in the rename file dialog */
void DWSIGNAL rename_ok(HWND window, void *data)
{
	UserEntry *param = (UserEntry *)data;

	if(param)
	{
		char *tmp = dw_window_get_text(param->entryfield);

		if(tmp && strlen(tmp) > 0)
		{
			if(strcasecmp(site[param->page]->hostname, "local") == 0)
			{
				char oldpath[2048], newpath[2048];

				if(site[param->page]->url[strlen(site[param->page]->url)-1] == '\\')
				{
					sprintf(oldpath, "%s%s", site[param->page]->url, param->filename);
					sprintf(newpath, "%s%s", site[param->page]->url, tmp);
				}
				else
				{
					sprintf(oldpath, "%s%s%s", site[param->page]->url, DIRSEP, param->filename);
					sprintf(newpath, "%s%s%s", site[param->page]->url, DIRSEP, tmp);
				}
				writeconsole(site[param->page], locale_string("Renaming \"%s\" to \"%s\"", 88), oldpath, newpath);
				rename(oldpath, newpath);
				sendthread(THRDHARDREFRESH, param->page);
			}
			else
			{
				sprintf(site[param->page]->thrdcommand, "RNFR %s\r\nRNTO %s\r\n", param->filename, tmp);
				sendthread(THRDREN, param->page);
			}
		}

		if(tmp)
			dw_free(tmp);

		dw_window_destroy(param->window);
		free(param);
	}
}

/* Creates an information box dialog */
void info_box(void)
{
	HWND infowindow, mainbox, mle, okbutton, buttonbox;
	UserEntry *param = malloc(sizeof(UserEntry));
	ULONG flStyle = DW_FCF_SYSMENU | DW_FCF_TITLEBAR | DW_FCF_SIZEBORDER | DW_FCF_MINMAX |
		DW_FCF_SHELLPOSITION | DW_FCF_TASKLIST | DW_FCF_DLGBORDER;
	char buffer[1024];
	ULONG point = -1;
	DWEnv env;

	infowindow = dw_window_new(HWND_DESKTOP, locale_string("System Information", 89), flStyle);

	mainbox = dw_box_new(BOXVERT, 5);

	dw_box_pack_start(infowindow, mainbox, 0, 0, TRUE, TRUE, 0);

	mle = dw_mle_new(100L);

	dw_box_pack_start(mainbox, mle, 130, 350, TRUE, TRUE, 4);

	dw_mle_set_editable(mle, FALSE);
#if __WIN32__
	dw_window_set_color(mle, DW_CLR_BLACK, DW_CLR_WHITE);
#endif

	/* Buttons */
	buttonbox = dw_box_new(BOXHORZ, 0);

	dw_box_pack_start(mainbox, buttonbox, 0, 0, TRUE, FALSE, 0);

	okbutton = dw_button_new(locale_string("Ok", 67), 1001L);

	dw_box_pack_start(buttonbox, 0, 50, 30, TRUE, FALSE, 0);
	dw_box_pack_start(buttonbox, okbutton, 50, 40, FALSE, FALSE, 2);
	dw_box_pack_start(buttonbox, 0, 50, 30, TRUE, FALSE, 0);

	param->window = infowindow;

	sprintf(buffer, locale_string("HandyFTP:\r\n\r\nVersion: %d.%d.%d\r\nBuild date: %s\r\nBuild time: %s\r\n\r\n", 90), VER_MAJ, VER_MIN, VER_REV, __DATE__, __TIME__);
	point = dw_mle_import(mle, buffer, point);

	sprintf(buffer, locale_string("System:\r\n\r\nColor depth: %lu\r\n", 91), dw_color_depth_get());
	point = dw_mle_import(mle, buffer, point);

	sprintf(buffer, locale_string("Screen width: %d\r\nScreen height: %d\r\n\r\n", 92),
			dw_screen_width(),
			dw_screen_height());
	point = dw_mle_import(mle, buffer, point);

	dw_environment_query(&env);
	sprintf(buffer, locale_string("Dynamic Windows:\r\n\r\nVersion: %d.%d.%d\r\nBuild date: %s\r\nBuild time: %s\r\n\r\nOperating System:\r\n\r\nSystem: %s\r\nVersion: %d.%d", 93),
			env.DWMajorVersion, env.DWMinorVersion, env.DWSubVersion, env.buildDate, env.buildTime, env.osName, env.MajorVersion, env.MinorVersion);
	point = dw_mle_import(mle, buffer, point);
	if(env.MajorBuild)
	{
		sprintf(buffer, locale_string("\r\nBuild: %d", 94), env.MajorBuild);
		point = dw_mle_import(mle, buffer, point);
		if(env.MinorBuild)
		{
			sprintf(buffer, ".%03d", env.MinorBuild);
			point = dw_mle_import(mle, buffer, point);
		}

	}

	param->data = NULL;
	param->busy = NULL;

	dw_signal_connect(okbutton, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(generic_cancel), (void *)param);

	dw_window_set_size(infowindow, 300, 420);

	dw_window_show(infowindow);
}


/* Generic function for updating the transfer statistics on the status line */
void update_eta(SiteTab *threadsite, int send_or_receive, long sent_or_received, long total_size, time_t curtime, time_t mytimer, time_t *lastupdate, int filesize)
{
	unsigned long sliderpos = 0;
	float myrate = 0;

	if((curtime - mytimer) != 0)
	{
		if(bandwidthlimit)
		{
			myrate = (float)((sent_or_received-filesize)/1024)/(curtime-mytimer);

			/* Do bandwidth shaping */
			while(myrate > bandwidthlimit && bandwidthlimit)
			{
				dw_mutex_unlock(mutex);
				msleep(10);
				dw_mutex_lock(mutex);
				myrate = (float)((sent_or_received-filesize)/1024)/(time(NULL)-mytimer);
			}
		}
	}

	if(*lastupdate != curtime)
	{
		if(curtime - mytimer == 0)
			setstatustext(threadsite, (send_or_receive ? locale_string("Started sending...", 95) : locale_string("Started receiving...", 96)));
		else
		{
			long total_size_K = (long)(total_size/1024);
			long sent_or_received_K = (long)(sent_or_received/1024);
			long minutes_left = 0, seconds_left = 0, elapsed_time = (long)(curtime-mytimer);
			long minutes_elapsed = elapsed_time/60;
			long seconds_elapsed = elapsed_time - (minutes_elapsed*60);

			if(!bandwidthlimit)
				myrate = (float)((sent_or_received-filesize)/1024)/(curtime-mytimer);

			if(myrate > 0)
			{
				minutes_left = (long)((total_size_K-sent_or_received_K)/myrate)/60;
				seconds_left = (long)((total_size_K-sent_or_received_K)/myrate)-(minutes_left*60);
			}

			setstatustext(threadsite, locale_string("%d bytes %s in %d:%.2d (%.2fK/s ETA %d:%.2d).", 97),
						  sent_or_received,
						  send_or_receive ? locale_string("sent", 98) : locale_string("received", 99),
						  minutes_elapsed,
						  seconds_elapsed,
						  (float)((sent_or_received-filesize)/1024)/elapsed_time,
						  minutes_left,
						  seconds_left);
		}
		if(total_size != 0)
		{
			sliderpos = (int)(((float)sent_or_received/(float)total_size)*100);
			if(sliderpos)
				dw_percent_set_pos(threadsite->percent, sliderpos);
		}
		*lastupdate = curtime;
	}
}

typedef struct _ip {
#ifdef _BIG_ENDIAN
	unsigned char ip4, ip3, ip2, ip1;
#else
	unsigned char ip1, ip2, ip3, ip4;
#endif
} IP4;

union ip4_32 {
	IP4 ip4;
	unsigned long ip32;
};

/* Parses out IP information from a string returned from the server */
void getipport(char *buffer, union ip4_32 *ip, union ip4_32 *port)
{
	char *buf2;
	int z, pos=0, num=0, len = strlen(buffer);

	buf2 = malloc(len+1);
	strcpy(buf2, buffer);

	port->ip32 = 0;
	ip->ip32 = 0;

	for(z=0;z<len;z++)
	{
		if(buf2[z] == '(')
			pos=z+1;
		if(buf2[z] == ',' || buffer[z] == ')')
		{
			unsigned char blah;
			buf2[z] = 0;
			blah = (unsigned char)atoi(&buf2[pos]);
			pos=z+1;
			switch(num)
			{
			case 0:
				ip->ip4.ip4 = blah;
				break;
			case 1:
				ip->ip4.ip3 = blah;
				break;
			case 2:
				ip->ip4.ip2 = blah;
				break;
			case 3:
				ip->ip4.ip1 = blah;
				break;
			case 4:
				port->ip4.ip2 = blah;
				break;
			case 5:
				port->ip4.ip1 = blah;
				break;
			}
			num++;
		}
	}
	free(buf2);
}

/* Wait until the state is no longer true */
void wait_site(int *state, int waitstate)
{
	while(*state == waitstate)
	{
		dw_mutex_unlock(mutex);
		msleep(1);
		dw_mutex_lock(mutex);
	}
}

/* Change the current status of a site and set the
 * timestamp of the change.  This is so we can cleanly
 * timeout commands that are taking too long.
 */
void set_status(SiteTab *thissite, int status)
{
	thissite->status = status;
	thissite->commandstart = time(NULL);
}

typedef struct _ftpdata {
	SiteTab *destsite;
	int listenfd;
	char *url;
	char *filename;
	int transferdone;
	int thissitetype;
	int transbufsize;
	int laststatus;
	int in_200;
	unsigned long received;
	unsigned long transmitted;
	FILE *localfile;
	time_t mytimer;
	time_t lastupdate;
	SiteTypes *thissitetypes;
	char *dirbuffer;
	int waitcount;
	int dirlen;
	int currentqueuesize;
	unsigned long filesize;
	/* State variables */
	int currentfxpstate;
	int commandready;
	int retrycount;
	int retrywhat;
	int originalcommand;
	union ip4_32 our_ip, pasvport, pasvip;
	char transbuf[4096];
} FTPData;

int FTPIteration(SiteTab *threadsite, int threadtab, HMTX h, FTPData *ftd)
{
	struct timeval slowtv = { 5, 0 };
	struct timeval fasttv = { 1, 0 };
	struct timeval tv;
	fd_set readset, writeset;
	struct sockaddr_in server, client;
	char cmd;
	int amnt, selectres, maxfd = 0, exitthread = FALSE;

	do {

		if(threadsite->connected == TRUE)
			dw_window_disable(threadsite->host_title);
		else
		{
			if(strcasecmp(threadsite->hostname, "local") != 0)
				setstatustext(threadsite, locale_string("Remote directory, disconnected.", 100));
			dw_window_enable(threadsite->host_title);
		}

		FD_ZERO(&readset);
		FD_ZERO(&writeset);
		FD_SET(threadsite->pipefd[0], &readset);

		/* This may need to be done for transmitting as well ... */
		if(ftd->transferdone == TRUE)
		{
			if(threadsite->status != STATUSTRANSMIT && ftd->received >= threadsite->sent)
			{
				if(strcasecmp(threadsite->hostname, "local") == 0)
				{
					time_t curtime = time(NULL);

					if(ftd->localfile)
					{
						fclose(ftd->localfile);
						ftd->localfile = NULL;
						if(ftd->url && ftd->filename)
						{
							if(urlsave)
								setfileinfo(ftd->filename, ftd->url, __TARGET__ ".his");

							free(ftd->filename);
							free(ftd->url);
							ftd->filename = ftd->url = NULL;
						}
					}
					ftd->originalcommand = -1;
					if(curtime-ftd->mytimer == 0)
						curtime++;
					writeconsole(threadsite, locale_string("Transfer completed. %d bytes received in %d seconds (%.2fK/s).", 101), ftd->received, (int)(curtime - ftd->mytimer), (double)((ftd->received-ftd->filesize)/1024)/((long)curtime-ftd->mytimer));
					ftd->currentqueuesize = 0;
					setstatustext(threadsite, locale_string("Local directory, connected.", 44));
				}
				else
					setstatustext(threadsite, locale_string("Remote directory, connected.", 38));

				set_status(threadsite, STATUSIDLE);
				ftd->transferdone = FALSE;
				dw_percent_set_pos(threadsite->percent, 0);
			}
		}

		/* Alright now we close any fds that aren't being used (prevents 100% CPU) */
		if(threadsite->status == STATUSIDLE)
		{
			if(ftd->listenfd)
				sockclose(ftd->listenfd);
			if(threadsite->datafd)
				sockclose(threadsite->datafd);
			if(threadsite->connected == FALSE)
			{
				if(threadsite->controlfd)
					sockclose(threadsite->controlfd);
				threadsite->controlfd = 0;
			}
			if(ftd->localfile)
			{
				fclose(ftd->localfile);
				ftd->localfile = NULL;
			}
			ftd->currentqueuesize = threadsite->datafd = ftd->listenfd = 0;
			dw_percent_set_pos(threadsite->percent, 0);
		}

		if(threadsite->controlfd)
			FD_SET(threadsite->controlfd, &readset);
		if(threadsite->datafd)
		{
			if(threadsite->status == STATUSTRANSMIT)
			{
				if(ftd->transbufsize > 0 || ftd->transferdone == TRUE)
					FD_SET(threadsite->datafd, &writeset);
			}
			else if(ftd->transbufsize < 4096)
				FD_SET(threadsite->datafd, &readset);
		}
		if(ftd->listenfd && (threadsite->status == STATUSDIRACCEPT || threadsite->status == STATUSDATAACCEPT))
			FD_SET(ftd->listenfd, &readset);

		if(threadsite->controlfd >= maxfd)
			maxfd = threadsite->controlfd + 1;

		if(threadsite->datafd >= maxfd)
			maxfd = threadsite->datafd + 1;

		if(ftd->listenfd >= maxfd)
			maxfd = ftd->listenfd + 1;

		if(threadsite->pipefd[0] >= maxfd)
			maxfd = threadsite->pipefd[0] + 1;

		if(ftd->destsite && (threadsite->status == STATUSSENDING || (threadsite->status == STATUSDATA && ftd->transbufsize > 0)))
		{
			FD_SET(ftd->destsite->tpipefd[1], &writeset);
			if(ftd->destsite->tpipefd[1] >= maxfd)
				maxfd = ftd->destsite->tpipefd[1] + 1;
		}

		if(threadsite->status == STATUSRECEIVING || (threadsite->status == STATUSTRANSMIT && ftd->transbufsize < 4096))
		{
			FD_SET(threadsite->tpipefd[0], &readset);
			if(threadsite->tpipefd[0] >= maxfd)
				maxfd = threadsite->tpipefd[0] + 1;
		}


		if(threadsite->status != ftd->laststatus)
		{
			int len = 10;
			char *text, *buf = (threadsite->currentqueue) ? threadsite->currentqueue->srcfilename : NULL;

			if(!buf)
				buf = ftd->filename;

			if(buf)
				len += strlen(buf);

			len += strlen(sitestatus[threadsite->status]);
			text = malloc(len);

			if(buf)
				sprintf(text, "%s (%s)", sitestatus[threadsite->status], buf);
			else
				sprintf(text, "%s", sitestatus[threadsite->status]);

			setmorestatustext(threadsite, text);

			free(text);
			ftd->laststatus = threadsite->status;
		}

		/* If we are waiting for idle only pause for 1 second instead of 5 */
		if(threadsite->status == STATUSWAITIDLE)
			memcpy(&tv, &fasttv, sizeof(struct timeval));
		else
			memcpy(&tv, &slowtv, sizeof(struct timeval));

		DBUG_POINT("tab_thread");

		dw_mutex_unlock(h);

		selectres = select(maxfd, &readset, &writeset, NULL, &tv);
		if(selectres == -1)
		{
			/* Lets hope it doesn't get here. */
			msleep(1);
			dw_beep(1000*(threadsite->page+1),100);
			dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, "maxfd %d, controlfd %d datafd %d listenfd %d pipe0 %d pipe1 %d tpipe0 %d tpipe1 %d page %d errno %d", maxfd, threadsite->controlfd, threadsite->datafd, ftd->listenfd, threadsite->pipefd[0], threadsite->pipefd[1], threadsite->tpipefd[0], threadsite->tpipefd[1], threadsite->page, errno);
		}
	} while(selectres < 0);


#ifdef DEBUG
	if(threadsite->status != STATUSIDLE)
		dw_debug("[%s] Done Select() - %s\n", threadsite->hosttitle, sitestatus[threadsite->status]);
#endif

	dw_mutex_lock(h);

	/* Deal with the case of a timeout */
	if(selectres == 0)
	{
		switch(threadsite->status)
		{
		case STATUSIDENT:
		case STATUSLOGIN:
		case STATUSPASSWORD:
		case STATUSCWD:
		case STATUSLS:
		case STATUSLSPORT:
		case STATUSRETR:
		case STATUSSTORE:
		case STATUSFXPRETR:
		case STATUSFXPSTOR:
			{
				if((time(NULL) - threadsite->commandstart) > ftptimeout)
				{
					/* We aren't idle and a command we were attempting has timed out...
					 * so we will now retry the command.
					 */
					ftd->retrywhat = threadsite->status;
					set_status(threadsite, STATUSRETRY);
				}
			}
			break;
		default:
			/* Otherwise it is a harmless timeout */
			break;
		}
	}

	DBUG_POINT("tab_thread");

	if(threadsite->status == STATUSWAITIDLE)
	{
		ftd->destsite = findsite(threadsite->queue->site);

		if(ftd->destsite && ftd->destsite->status != STATUSIDLE && ftd->waitcount < ftptimeout)
			ftd->waitcount++;
		else if(ftd->destsite && ftd->destsite->status == STATUSIDLE)
		{
			set_status(threadsite, STATUSIDLE);
			site_ref(ftd->destsite);
			sendthread(THRDFLUSH, threadsite->page);
		}
		else
		{
			set_status(threadsite, STATUSIDLE);
			dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("Timeout expired while waiting for destination to become idle.", 102));
		}
	}

	/* Request an update to the IPS list */
	if(in_IPS && threadsite->status == STATUSIDLE && threadsite->page == IPS_page && ftd->in_200 == FALSE && (time(NULL) - IPS_time) > 60)
	{
		if(threadsite->controlfd && socksprint(threadsite->controlfd, "SITE RADM LIST sockets\r\n") > 0)
			clearadmin();
		IPS_time = time(NULL);
	}

	/* Command from the user or another thread */
	if(FD_ISSET(threadsite->pipefd[0], &readset))
	{
		if(sockread(threadsite->pipefd[0], (void *)&cmd, 1, 0)<1)
			cmd = -1;

		switch((int)cmd)
		{
		case THRDEXIT:
#ifdef DEBUG
			dw_debug("[%s] THRDEXIT\n", threadsite->hosttitle);
#endif
			exitthread = TRUE;
			break;
		case THRDCONNECT:
#ifdef DEBUG
			dw_debug("[%s] THRDCONNECT\n", threadsite->hosttitle);
#endif
			if(threadsite->connected == FALSE)
			{
				verifytitle(threadsite);

				if(strcasecmp(threadsite->hostname, "local") == 0)
				{
					if(threadsite->connected == FALSE)
					{
						threadsite->connected = TRUE;
						sendthread(THRDREFRESH, threadsite->page);
					}
				}
				else
				{
					struct sockaddr_in si;
					int ipaddr = 0;
					socklen_t sisize;

					dw_mutex_unlock(h);

					ftd->originalcommand = THRDCONNECT;
					setstatustext(threadsite, locale_string("Attempting to establish connection... Please wait.", 103));
					threadsite->connected = FALSE;

					if(isip(threadsite->hostname))
						ipaddr = inet_addr(threadsite->hostname);
					else
					{
						struct hostent *hostnm = gethostbyname(threadsite->hostname);

						if(!hostnm)
						{
							writeconsole(threadsite, locale_string("Unable to resolve hostname.", 104));
							setstatustext(threadsite, locale_string("Remote directory, disconnected.", 100));
						}
						else
							ipaddr = *((unsigned long *)hostnm->h_addr);
					}

					if(ipaddr && ipaddr != -1)
					{
						server.sin_family      = AF_INET;
						server.sin_port        = htons(threadsite->port);
						threadsite->serverip = server.sin_addr.s_addr = ipaddr;
						if((threadsite->controlfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 || connect(threadsite->controlfd, (struct sockaddr *)&server, sizeof(server)))
						{
							writeconsole(threadsite, locale_string("Failed to establish connection.", 105));
							setstatustext(threadsite, locale_string("Remote directory, disconnected.", 100));
							threadsite->controlfd = 0;
						} else
						{
							sisize = sizeof(si);
							memset(&si, 0, sisize);
							getsockname(threadsite->controlfd, (struct sockaddr *)&si, &sisize);
							ftd->our_ip.ip32 = ntohl(si.sin_addr.s_addr);
							writeconsole(threadsite, locale_string("Connected to %s port %d.", 106), threadsite->hostname, threadsite->port);
							setstatustext(threadsite, locale_string("Connected.", 107));
							nonblock(threadsite->controlfd);
							threadsite->connected = TRUE;
							set_status(threadsite, STATUSLOGIN);
						}
					}
					dw_mutex_lock(h);
				}
			}
			break;
		case THRDDISCONNECT:
#ifdef DEBUG
			dw_debug("[%s] THRDDISCONNECT\n", threadsite->hosttitle);
#endif
			if(strcasecmp(threadsite->hostname, "local") == 0)
			{
				/* If we are in the process of doing something tell the
				 * buddy thread to abort.
				 */
				if(threadsite->status != STATUSIDLE && ftd->destsite)
					sendthread(THRDABORT, ftd->destsite->page);

				site_unref(ftd->destsite);
				ftd->destsite = NULL;

				set_status(threadsite, STATUSIDLE);
				threadsite->connected = FALSE;

				freecache(threadsite);
				cleardir(threadsite);
				setstatustext(threadsite, locale_string("Local directory, disconnected.", 41));
			}
			else
			{
				if(threadsite->status == STATUSIDLE && threadsite->connected == TRUE)
				{
					ftd->originalcommand = THRDDISCONNECT;
					if(threadsite->controlfd)
					{
						if(sockwrite(threadsite->controlfd, "QUIT\r\n", 6, 0) > 0)
						{
							setstatustext(threadsite, locale_string("Sending QUIT message to remote host...", 108));

							/* Pause briefly to insure receipt. */
							dw_mutex_unlock(h);
							msleep(1000);
							dw_mutex_lock(h);
						}

						sockclose(threadsite->controlfd);
						writeconsole(threadsite, locale_string("Closed connection to %s port %d.", 109), threadsite->hostname, threadsite->port);
					}

					/* If we are in the process of doing something tell the
					 * buddy thread to abort.
					 */
					if(threadsite->status != STATUSIDLE && ftd->destsite)
						sendthread(THRDABORT, ftd->destsite->page);

					site_unref(ftd->destsite);
					ftd->destsite = NULL;

					threadsite->connected = FALSE;
					set_status(threadsite, STATUSIDLE);

					setstatustext(threadsite, locale_string("Remote directory, disconnected.", 100));
					ftd->thissitetypes = NULL;
					freecache(threadsite);
					cleardir(threadsite);
				}
			}
			break;
		case THRDFLUSH:
#ifdef DEBUG
			dw_debug("[%s] THRDFLUSH\n", threadsite->hosttitle);
#endif
			{
				dw_percent_set_pos(threadsite->percent, DW_PERCENT_INDETERMINATE);
				ftd->filesize = 0;

				if(threadsite->connected == TRUE && threadsite->status == STATUSIDLE)
				{
					int failed = FALSE;

					ftd->originalcommand = THRDFLUSH;
					if(threadsite->currentqueue)
					{
						free(threadsite->currentqueue->srcdirectory);
						free(threadsite->currentqueue->srcfilename);
						free(threadsite->currentqueue->site);
						free(threadsite->currentqueue->destdirectory);
						free(threadsite->currentqueue);
					}
					threadsite->currentqueue = NULL;

					ftd->destsite = findsite(threadsite->queue->site);

					if(ftd->destsite)
					{
						if(ftd->destsite->status != STATUSIDLE)
						{
							/* If the destination is not idle we must wait in select to
							 * avoid the mutex surrounding much of the thread code.
							 */
							writeconsole(threadsite, locale_string("Waiting for destination to become idle. (%ds)", 110), ftptimeout);
							set_status(threadsite, STATUSWAITIDLE);
							failed = TRUE;
							ftd->waitcount = 0;
						}
					}

					if(!ftd->destsite || failed == TRUE)
					{
						if(failed == FALSE)
							dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("Destination site not found!", 111));
						ftd->destsite = NULL;
					}
					else
					{
						site_ref(ftd->destsite);
						if(strcasecmp(ftd->destsite->hostname, "local") == 0)
							ftd->filesize = findlocalfilesize(threadsite->queue->srcfilename, threadsite->queue->destdirectory);
						else
							ftd->filesize = findfilesize(threadsite->queue->srcfilename, ftd->destsite);

						/* If the remote file is the same size as the local file skip it. */
						if(ftd->filesize == threadsite->queue->size)
						{
							threadsite->currentqueue = threadsite->queue;
							threadsite->queue = threadsite->queue->next;
							if(threadsite->currentqueue)
							{
								free(threadsite->currentqueue->srcdirectory);
								free(threadsite->currentqueue->srcfilename);
								free(threadsite->currentqueue->site);
								free(threadsite->currentqueue->destdirectory);
								free(threadsite->currentqueue);
							}
							threadsite->currentqueue = NULL;

							if(threadsite->queue)
								set_status(threadsite, STATUSNEXT);
							else
								set_status(threadsite, STATUSIDLE);
						}
						else
						{
							threadsite->currentqueue = threadsite->queue;
							threadsite->queue = threadsite->queue->next;
							ftd->destsite->sent = 0;
							if(strcasecmp(threadsite->hostname, "local") == 0)
							{
								char lfile[500];

								strcpy(lfile, threadsite->currentqueue->srcdirectory);
								if(lfile[strlen(lfile)-1] != '\\' && lfile[strlen(lfile)-1] != '/')
									strcat(lfile, "/");
								strcat(lfile, threadsite->currentqueue->srcfilename);

								set_status(threadsite, STATUSSENDING);

								if((ftd->localfile = fopen(lfile, FOPEN_READ_BINARY)) == NULL)
								{
									dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("Could not open local file, \"%s\"", 112), lfile);
									set_status(threadsite, STATUSIDLE);
								}
								else
								{
									/* If we can try to resume download */
									ftd->filesize = findfilesize(threadsite->currentqueue->srcfilename, ftd->destsite);
									fseek(ftd->localfile, ftd->filesize, SEEK_SET);
									ftd->destsite->sent = ftd->filesize;
								}
							}
							else
							{
								union ip4_32 port4;

								ftd->currentfxpstate = !reversefxp;
								if(strcasecmp(ftd->destsite->hostname, "local")!=0)
								{
									/* FXP Code */
									if(ftd->currentfxpstate == FALSE)
									{
										if(socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "CWD %s\r\n",
											threadsite->currentqueue->srcdirectory)) > 0 &&
											sockwrite(threadsite->controlfd, "PASV\r\n", 6, 0) > 0)
										{
											ftd->commandready = FALSE;
											set_status(threadsite, STATUSFXPRETR);
										}
									}
									else
									{
										if(socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "CWD %s\r\n", threadsite->currentqueue->srcdirectory)) > 0)
										{
											sprintf(ftd->destsite->thrdcommand, "%d", threadsite->page);
											sendthread(THRDFXP, ftd->destsite->page);
											set_status(threadsite, STATUSFXPWAIT);
										}
									}
								}
								else
								{
									if(ftd->currentfxpstate == FALSE)
									{
										/* Passive FTP */
										if(socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "CWD %s\r\n",
											threadsite->currentqueue->srcdirectory)) > 0 &&
											sockwrite(threadsite->controlfd, "PASV\r\n", 6, 0) > 0)
										{
											ftd->commandready = FALSE;
											set_status(threadsite, STATUSPASVRETR);
										}
									}
									else
									{
										/* Standard FTP Code */
										memset(&server, 0, sizeof(server));
										server.sin_family = AF_INET;
										server.sin_port   = 0;
										server.sin_addr.s_addr = INADDR_ANY;
										if ((ftd->listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ||  
											bind(ftd->listenfd, (struct sockaddr *)&server, sizeof(server)) < 0 || 
											listen(ftd->listenfd, 0) < 0)
												writeconsole(threadsite, locale_string("Error binding to port of data connection.", 113));
										else
										{
											struct sockaddr_in listen_addr = { 0 };
											socklen_t len = sizeof(struct sockaddr_in);

											getsockname(ftd->listenfd, (struct sockaddr *)&listen_addr, &len);

											port4.ip32 = ntohs(listen_addr.sin_port);

											if(socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "CWD %s\r\n",
												threadsite->currentqueue->srcdirectory)) > 0 &&
												sockwrite(threadsite->controlfd, "TYPE I\r\n", 8, 0) > 0)
											{
												dw_mutex_unlock(h);
												msleep(NAT_DELAY);
												dw_mutex_lock(h);
												
												if(socksprint(threadsite->controlfd, 
													vargs(alloca(1024), 1023, "PORT %d,%d,%d,%d,%d,%d\r\n", 
													ftd->our_ip.ip4.ip4,ftd->our_ip.ip4.ip3,
													ftd->our_ip.ip4.ip2,ftd->our_ip.ip4.ip1,
													port4.ip4.ip2,port4.ip4.ip1)) > 0)
													{
														/* If we can try to resume download */
														if(ftd->filesize)
														{
															if(socksprint(threadsite->controlfd, 
																vargs(alloca(101), 100, "REST %lu\r\n", ftd->filesize)) > 0)
																	ftd->destsite->sent = ftd->filesize;
														}
														set_status(threadsite, STATUSRETR);
													}
											}
										}
									}
								}

							}
						}

						if(threadsite->status != STATUSIDLE && threadsite->status != STATUSFXPWAIT)
						{
							sprintf(ftd->destsite->thrdcommand, "%d", threadsite->page);
							if(threadsite->status == STATUSFXPRETR)
								sendthread(THRDFXPRECEIVE, ftd->destsite->page);
							else
								sendthread(THRDRECEIVE, ftd->destsite->page);
							ftd->mytimer = time(NULL);
						}
					}
					drawq(threadsite);
				}
			}
			break;
		case THRDFXPRECEIVE:
		case THRDRECEIVE:
#ifdef DEBUG
			dw_debug("[%s] THRDRECEIVE\n", threadsite->hosttitle);
#endif
			ftd->filesize = 0;

			if(threadsite->connected == TRUE && threadsite->status == STATUSIDLE)
			{
				int sendpage;

				dw_percent_set_pos(threadsite->percent, DW_PERCENT_INDETERMINATE);
				ftd->originalcommand = cmd;
				sendpage = atoi(threadsite->thrdcommand);
				ftd->destsite = site[sendpage];

				ftd->currentfxpstate = !reversefxp;

				set_status(threadsite, STATUSRECEIVING);
				if(!ftd->destsite || !ftd->destsite->currentqueue)
				{
					dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("Possibly incorrect page id received.", 114));
					sendthread(THRDABORT, sendpage);
					set_status(threadsite, STATUSIDLE);
					ftd->originalcommand = -1;
					ftd->destsite = NULL;
				}
				else
				{
					site_ref(ftd->destsite);
					ftd->transferdone = FALSE;
					ftd->received = 0;
					if(strcasecmp(threadsite->hostname, "local") == 0)
					{
						char lfile[1000];
						struct stat buf;

						strcpy(lfile, ftd->destsite->currentqueue->destdirectory);
						if(lfile[strlen(lfile)-1] != '\\' && lfile[strlen(lfile)-1] != '/')
							strcat(lfile, "/");
						strcat(lfile, ftd->destsite->currentqueue->srcfilename);

							/* If we can try to resume download */
						if(stat(lfile, &buf) == 0 && buf.st_size < ftd->destsite->currentqueue->size)
						{
							ftd->localfile = fopen(lfile, FOPEN_APPEND_BINARY);
							ftd->filesize = ftd->received = buf.st_size;
						}
						else
						{
							ftd->localfile = fopen(lfile, FOPEN_WRITE_BINARY);
							setfilesize(ftd->destsite->currentqueue->srcfilename, threadsite, 0);
						}

						/* Size is "ftp://" (6) + hostname + path + "/" (1) + filename + port (6 max) */
						ftd->url = malloc(14+strlen(ftd->destsite->currentqueue->srcfilename)+strlen(ftd->destsite->hostname)+
									 strlen(ftd->destsite->currentqueue->srcdirectory));
						if(ftd->destsite->port != 21)
						{
							sprintf(ftd->url, "ftp://%s:%d%s/%s", ftd->destsite->hostname, ftd->destsite->port,
									ftd->destsite->currentqueue->srcdirectory, ftd->destsite->currentqueue->srcfilename);
						}
						else
						{
							sprintf(ftd->url, "ftp://%s%s/%s", ftd->destsite->hostname,
									ftd->destsite->currentqueue->srcdirectory, ftd->destsite->currentqueue->srcfilename);
						}
						ftd->filename = strdup(lfile);

						if(ftd->localfile == NULL)
						{
							dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("Could not open local file, \"%s\"", 115), lfile);
							sendthread(THRDABORT, sendpage);
							set_status(threadsite, STATUSIDLE);
							ftd->originalcommand = -1;
						}
					}
					else
					{
						union ip4_32 port4;

						ftd->filesize = findfilesize(ftd->destsite->currentqueue->srcfilename, threadsite);

						ftd->transmitted = 0;

						if(socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "CWD %s\r\n", 
							ftd->destsite->currentqueue->destdirectory)) > 0)
						{
							/* Deal with the 3 cases, FXP, Standard FTP and Passive FTP */
							if(ftd->currentfxpstate == TRUE && cmd == THRDFXPRECEIVE &&
								sockwrite(threadsite->controlfd, "PASV\r\n", 6, 0) > 0)
							{
								/* We could probably do resume on FXP too */
							}
							else if(cmd == THRDRECEIVE)
							{
								if(ftd->currentfxpstate == TRUE)
								{
									/* Standard FTP Code */
									memset(&server, 0, sizeof(server));
									server.sin_family = AF_INET;
									server.sin_port   = 0;
									server.sin_addr.s_addr = INADDR_ANY;
									if ((ftd->listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ||  
										bind(ftd->listenfd, (struct sockaddr *)&server, sizeof(server)) < 0 || 
										listen(ftd->listenfd, 0) < 0)
											writeconsole(threadsite, locale_string("Error binding to port of data connection.", 113));
									else
									{
										struct sockaddr_in listen_addr = { 0 };
										socklen_t len = sizeof(struct sockaddr_in);

										getsockname(ftd->listenfd, (struct sockaddr *)&listen_addr, &len);

										port4.ip32 = ntohs(listen_addr.sin_port);

										dw_mutex_unlock(h);
										msleep(NAT_DELAY);
										dw_mutex_lock(h);
										if(socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "PORT %d,%d,%d,%d,%d,%d\r\n", 
											ftd->our_ip.ip4.ip4,ftd->our_ip.ip4.ip3,ftd->our_ip.ip4.ip2,
											ftd->our_ip.ip4.ip1,port4.ip4.ip2,port4.ip4.ip1)) > 0)
										{
											/* If we can try to resume download */
											if(ftd->filesize)
											{
												if(socksprint(threadsite->controlfd, vargs(alloca(101), 100, "REST %lu\r\n", 
													ftd->filesize)) > 0)
														ftd->received = ftd->transmitted = ftd->filesize;
											}
											ftd->commandready = FALSE;
											set_status(threadsite, STATUSSTORE);
										}
									}
								}
								else
								{
									if(sockwrite(threadsite->controlfd, "PASV\r\n", 6, 0) > 0)
									{
										/* If we can try to resume download */
										if(ftd->filesize && socksprint(threadsite->controlfd, 
											vargs(alloca(101), 100, "REST %lu\r\n", ftd->filesize)) > 0)
												ftd->received = ftd->transmitted = ftd->filesize;
										set_status(threadsite, STATUSSTORE);
									}
								}
							}
						}
					}
				}
				if(threadsite->status != STATUSIDLE)
					ftd->mytimer = time(NULL);
			}
			break;
		case THRDDONE:
#ifdef DEBUG
			dw_debug("[%s] THRDDONE\n", threadsite->hosttitle);
#endif
			ftd->transferdone = TRUE;
			if(strcasecmp(threadsite->hostname, "local") == 0)
			{
				if(ftd->received >= threadsite->sent)
				{
					if(ftd->localfile)
					{
						fclose(ftd->localfile);
						ftd->localfile = NULL;
						if(ftd->url && ftd->filename)
						{
							if(urlsave)
								setfileinfo(ftd->filename, ftd->url, __TARGET__ ".his");

							free(ftd->filename);
							free(ftd->url);
							ftd->filename = ftd->url = NULL;
						}
					}
				}
			}
			break;
		case THRDHARDREFRESH:
		case THRDREFRESH:
#ifdef DEBUG
			dw_debug("[%s] THRDREFRESH\n", threadsite->hosttitle);
#endif
			if(threadsite->status != STATUSIDLE)
			{
				dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("This site is busy.", 116));
				break;
			}

			dw_window_set_text(threadsite->directory, threadsite->url);

			if((int)cmd == THRDREFRESH && findincache(threadsite) == TRUE)
			{
				drawdir(threadsite);
			}
			else
			{
				ftd->originalcommand = THRDREFRESH;
				removefromcache(threadsite);
				if(strcasecmp(threadsite->hostname, "local") == 0)
				{
					dw_mutex_unlock(h);
					loadlocaldir(threadsite);
					dw_mutex_lock(h);
					addtocache(threadsite);
					drawdir(threadsite);
					/* If we are recursing into directories
					 * queue up all the files in this directory
					 * listing with the exception of ".."
					 */
					if(threadsite->queuing)
					{
						Directory *tmp = threadsite->dir;
						int counter = 0;

						while(tmp)
						{
							if(strcmp(tmp->entry, "..") != 0)
								addtoq(counter, threadsite->page);
							tmp = tmp->next;
							counter++;
						}
						drawq(threadsite);
						recurseq(threadsite);
					}
				}
				else
				{
					ftd->currentfxpstate = !reversefxp;
					if(threadsite->connected == FALSE)
						sendthread(THRDCONNECT, threadsite->page);
					else
					{
						if(socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "CWD %s\r\n", threadsite->url)) > 0 &&
							sockwrite(threadsite->controlfd, "PWD\r\n", 5, 0) > 0)
						{
							writeconsole(threadsite, locale_string("Changing working directory to \"%s\".", 117), threadsite->url);
							set_status(threadsite, STATUSLSPORT);
						}
					}
				}
			}
			break;
		case THRDFXP:
			{
				int pagenumber = -1;
				ftd->currentfxpstate = !reversefxp;

#ifdef DEBUG
				dw_debug("[%s] THRDFXP\n", threadsite->hosttitle);
#endif
				pagenumber = atoi(threadsite->thrdcommand);

				/* The source page was passed in the thrdcommand entry in the site structure */
				if(pagenumber > -1 && pagenumber < CONNECTION_LIMIT && (ftd->destsite = site[pagenumber]))
				{
					if(sockwrite(threadsite->controlfd, "PASV\r\n", 6, 0) > 0)
					{
						site_ref(ftd->destsite);
						ftd->commandready = FALSE;
						set_status(threadsite, STATUSFXPSTOR);
					}
				}
				else
				{
					dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("Fatal FXP error! (THRDFXP) Aborting.", 118));
					sendthread(THRDABORT, threadsite->page);
				}
			}
			break;
		case THRDFXPSTART:
#ifdef DEBUG
			dw_debug("[%s] THRDFXPSTART: PORT %s\n", threadsite->hosttitle, threadsite->thrdcommand);
#endif
			dw_mutex_unlock(h);
			msleep(NAT_DELAY);
			dw_mutex_lock(h);
			if(socksprint(threadsite->controlfd, vargs(alloca(101), 100, "PORT %s\r\n", threadsite->thrdcommand)) > 0)
			{
				if(ftd->currentfxpstate == FALSE)
					set_status(threadsite, STATUSFXPSTOR);
				else
					set_status(threadsite, STATUSFXPRETR);
			}
			break;
		case THRDDEL:
#ifdef DEBUG
			dw_debug("[%s] THRDDEL\n", threadsite->hosttitle);
#endif
			if(contexttext)
			{
				int filetype = findfiletype(contexttext, threadsite);
				int result = -1;

				if(filetype == DIRFILE || filetype == DIRLINK)
				{
					if(socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "CWD %s\r\n", threadsite->url)) > 0)
						result = socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "DELE %s\r\n", contexttext));
				}
				else if(filetype == DIRDIR)
				{
					if(socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "CWD %s\r\n", threadsite->url)) > 0)
						result = socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "RMD %s\r\n", contexttext));
				}
				if(result > 0)
					sendthread(THRDHARDREFRESH, threadsite->page);
			}
			break;
		case THRDREN:
#ifdef DEBUG
			dw_debug("[%s] THRDREN\n", threadsite->hosttitle);
#endif
			{
				if(socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "CWD %s\r\n", threadsite->url)) > 0 &&
					sockwrite(threadsite->controlfd, threadsite->thrdcommand, strlen(threadsite->thrdcommand), 0) > 0)
						sendthread(THRDHARDREFRESH, threadsite->page);
			}
			break;
		case THRDVIEW:
#ifdef DEBUG
			dw_debug("[%s] THRDVIEW\n", threadsite->hosttitle);
#endif
			break;
		case THRDMKDIR:
#ifdef DEBUG
			dw_debug("[%s] THRDMKDIR\n", threadsite->hosttitle);
#endif
			{
				if(socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "CWD %s\r\n", threadsite->url)) > 0 &&
					socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "MKD %s\r\n", threadsite->thrdcommand)) > 0)
						sendthread(THRDHARDREFRESH, threadsite->page);
			}
			break;
		case THRDRAW:
			if(threadsite->status == STATUSIDLE && threadsite->connected == TRUE &&
			   threadsite->controlfd && threadsite->thrdcommand[0])
			{
				if(socksprint(threadsite->controlfd, threadsite->thrdcommand) > 0)
					threadsite->thrdcommand[0] = '\0';
			}
			break;
		case THRDABORT:
#ifdef DEBUG
			dw_debug("[%s] THRDABORT\n", threadsite->hosttitle);
#endif
			set_status(threadsite, STATUSIDLE);
			ftd->originalcommand = -1;
			if(ftd->localfile)
			{
				fclose(ftd->localfile);
				ftd->localfile = NULL;
			}
			if(threadsite->datafd)
				sockclose(threadsite->datafd);
			if(ftd->listenfd)
				sockclose(ftd->listenfd);
			threadsite->datafd = ftd->listenfd = 0;
			break;
		}
	}

#ifdef DEBUG
	if(threadsite->status != STATUSIDLE)
		dw_debug("[%s] Controlfd - %s\n", threadsite->hosttitle, sitestatus[threadsite->status]);
#endif
	DBUG_POINT("tab_thread");
	if(threadsite->controlfd && FD_ISSET(threadsite->controlfd, &readset))
	{
		char controlbuffer[1025] = "";
		static char nexttime[513] = "";
		int z, gah, start;

		start = 0;
		strncpy(controlbuffer, nexttime, 512);
		amnt = sockread(threadsite->controlfd, &controlbuffer[strlen(nexttime)], 512, 0);
		if(amnt == 0)
		{
			sockclose(threadsite->controlfd);
			if(threadsite->datafd)
				sockclose(threadsite->datafd);
			if(ftd->listenfd)
				sockclose(ftd->listenfd);
			ftd->listenfd = threadsite->controlfd = threadsite->datafd = 0;
			ftd->retrywhat = threadsite->status;
			set_status(threadsite, STATUSRETRY);
			threadsite->connected = FALSE;
		}
		else
		{
			controlbuffer[amnt+strlen(nexttime)] = 0;
			nexttime[0] = 0;
			gah = strlen(controlbuffer);
			for(z=0;z<gah;z++)
			{
				if(controlbuffer[z] == '\r' || controlbuffer[z] == '\n')
				{
					controlbuffer[z] = 0;

					if(ftd->in_200 == TRUE)
					{
						int z, count = 0;
						char *adminbuf = &controlbuffer[start];
						char *user = NULL, *addr = NULL, *act = NULL, *sock = NULL, *last = &adminbuf[1];
						int idle = 0, len = strlen(adminbuf);

						if(strncmp(adminbuf, "200", 3) == 0)
						{
							ftd->in_200 = FALSE;
							if(in_IPS)
								IPS_update();
						}
						else
						{
							for(z=0;z<len;z++)
							{
								if(adminbuf[z] == '|')
								{
									count++;
									adminbuf[z] = '\0';
									if(count == 1)
										sock = last;
									if(count == 2)
										user = last;
									if(count == 3)
									{
										int status = atoi(last);

										if(status > -1 && status < 15)
											act = IPSstatus[status];
										else
											act = "Unknown";
									}
									if(count == 4)
										idle = atoi(last);
									if(count == 5)
										addr = last;
									last = &adminbuf[z+1];
								}
							}
							if(count > 4 && sock && atoi(sock))
								addtoadmin(user, addr, act, idle, sock);
						}
					}
					else if(strlen(&controlbuffer[start]) > 0)
					{
						char sitecode[4];
						int k;

						strncpy(sitecode, &controlbuffer[start], 3);
						sitecode[3] = 0;

						/* Translation support */
						if(ftd->thissitetypes && ftd->thissitetypes->translation[0])
						{
							for(k=0;k<40;k++)
							{
								if(ftd->thissitetypes->translation[k] && instring(&controlbuffer[start], ftd->thissitetypes->translation[k]) == TRUE)
									strncpy(sitecode, ftd->thissitetypes->translation[k], 3);
							}
						}

						if(strncmp(sitecode, "215", 3) == 0)
						{
							ftd->thissitetype = determinesitetype(&controlbuffer[start]);
							if((ftd->thissitetypes = getsitetype(ftd->thissitetype)) != NULL)
								writeconsole(threadsite, locale_string("Using site type \"%s\" (%d)", 119), ftd->thissitetypes->type, ftd->thissitetype);
							else
							{
								writeconsole(threadsite, locale_string("Unable to load site types, disconnecting.", 120));
								sendthread(THRDDISCONNECT, threadsite->page);
								set_status(threadsite, STATUSIDLE);
								ftd->originalcommand = -1;
							}
						} else if(strncmp(sitecode, "500", 3) == 0 || strncmp(sitecode, "501", 3) ==  0)
						{
							/* Syntax Error */
							if(threadsite->status == STATUSLSPORT)
								{
									set_status(threadsite, STATUSIDLE);
									sendthread(THRDHARDREFRESH, threadsite->page);
								}
							else
							{
								ftd->retrywhat = threadsite->status;
								set_status(threadsite, STATUSRETRY);
							}

						} else if(strncmp(sitecode, "211", 3) == 0)
						{
							/* System status */
						} else if(strncmp(sitecode, "IPS", 3) == 0)
						{
							/* IPS Extra status information */
							if(threadsite->status ==  STATUSFXPTRANSFER)
							{
								time_t curtime = time(NULL);
								ftd->received = atoi(&controlbuffer[start+3]);
								if((ftd->originalcommand == THRDFLUSH && ftd->currentfxpstate == TRUE) || (ftd->originalcommand != THRDFLUSH && ftd->currentfxpstate == FALSE))
									update_eta(threadsite, FALSE, ftd->received, ((ftd->destsite && ftd->destsite->currentqueue) ? ftd->destsite->currentqueue->size : 0), curtime, ftd->mytimer, &ftd->lastupdate, ftd->filesize);
								else
									update_eta(threadsite, TRUE, ftd->received, threadsite->currentqueue->size, curtime, ftd->mytimer, &ftd->lastupdate, ftd->filesize);
							}
						} else if(strncmp(sitecode, "530", 3) == 0)
						{
							/* Not logged in */
							ftd->retrywhat = threadsite->status;
							set_status(threadsite, STATUSRETRY);
						} else if(strncmp(sitecode, "421", 3) == 0)
						{
							/* Login timeout */
							threadsite->connected = FALSE;
							ftd->retrywhat = threadsite->status;
							set_status(threadsite, STATUSRETRY);
						} else if(strncmp(sitecode, "212", 3) == 0)
						{
							/* Directory Status */
						} else if(strncmp(sitecode, "213", 3) == 0)
						{
							/* File Status */
						} else if(strncmp(sitecode, "110", 3) == 0)
						{
							/* Resume request (restart mark) */
						} else if(strncmp(sitecode, "120", 3) == 0)
						{
							/* Service Ready in nnn minutes. */
							threadsite->connected = FALSE;
						} else if(strncmp(sitecode, "220", 3) == 0)
						{
							/* Service Ready for new user. */
							if(threadsite->status == STATUSLOGIN)
								ftd->commandready = TRUE;
						} else if(strncmp(sitecode, "221", 3) == 0)
						{
							/* Service closing connection. */
							threadsite->connected = FALSE;
						} else if(strncmp(sitecode, "230", 3) == 0)
						{
							/* User logged in. */
							if(threadsite->status == STATUSIDENT)
								ftd->commandready = TRUE;
						} else if(strncmp(sitecode, "257", 3) == 0)
						{
							/* Current working directory. */
							int x, cwdstart = start, cwdend = start;

							for (x=start;x<gah;x++)
							{
								if(controlbuffer[x] == '"')
								{
									if(cwdstart == start)
										cwdstart = x+1;
									else if(cwdend == start)
									{
										cwdend = x-1;
										if((cwdend-cwdstart) >= 0)
										{
											char *tmp1, *tmp2;

											/* Create a new string with the returned CWD */
											tmp1 = malloc((cwdend-cwdstart)+2);
											strncpy(tmp1, &controlbuffer[cwdstart], (cwdend-cwdstart)+1);
											tmp1[cwdend-cwdstart+1] = 0;

											/* Replace the old URL making sure the pointer is always valid */
											tmp2 = threadsite->url;
											threadsite->url = tmp1;

											writeconsole(threadsite, locale_string("Server says working directory is \"%s\".", 121), threadsite->url);

											/* Update the entryfield on the tab page */
											dw_window_set_text(threadsite->directory, threadsite->url);

											if(tmp2)
												free(tmp2);
										}
									}
								}
							}

						} else if(strncmp(sitecode, "530", 3) == 0)
						{
							/* Not logged in. */
							ftd->retrywhat = threadsite->status;
							set_status(threadsite, STATUSRETRY);
						} else if(strncmp(sitecode, "450", 3) == 0)
						{
							/* File unavailable */
							dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("%s says Filename unavailable.", 122), threadsite->hosttitle);
							set_status(threadsite, STATUSNEXT);
							if(ftd->destsite)
								sendthread(THRDABORT, ftd->destsite->page);
						} else if(strncmp(sitecode, "451", 3) == 0)
						{
							/* Local error in processing */
							dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("%s says Local error in processing.", 123), threadsite->hosttitle);
							set_status(threadsite, STATUSNEXT);
							if(ftd->destsite)
								sendthread(THRDABORT, ftd->destsite->page);
						} else if(strncmp(sitecode, "452", 3) == 0)
						{
							/* Insufficient storage space */
							dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("%s says Insufficient storage space.", 124), threadsite->hosttitle);
							set_status(threadsite, STATUSNEXT);
							if(ftd->destsite)
								sendthread(THRDABORT, ftd->destsite->page);
						} else if(strncmp(sitecode, "550", 3) == 0)
						{
							/* File unavailable */
							dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("%s says File unavailable.", 125), threadsite->hosttitle);
							set_status(threadsite, STATUSNEXT);
							if(ftd->destsite)
								sendthread(THRDABORT, ftd->destsite->page);
						} else if(strncmp(sitecode, "551", 3) == 0)
						{
							/* Page type unknown */
							dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("%s says Page type unknown.", 126), threadsite->hosttitle);
							set_status(threadsite, STATUSNEXT);
							if(ftd->destsite)
								sendthread(THRDABORT, ftd->destsite->page);
						} else if(strncmp(sitecode, "552", 3) == 0)
						{
							/* Exceeded storage allocation */
							dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("%s says Exceeded storage allocation.", 127), threadsite->hosttitle);
							set_status(threadsite, STATUSNEXT);
							if(ftd->destsite)
								sendthread(THRDABORT, ftd->destsite->page);
						} else if(strncmp(sitecode, "553", 3) == 0)
						{
							/* Filename not allowed */
							dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("%s says Filename not allowed.", 128), threadsite->hosttitle);
							set_status(threadsite, STATUSNEXT);
							if(ftd->destsite)
								sendthread(THRDABORT, ftd->destsite->page);
						} else if(strncmp(sitecode, "331", 3) == 0)
						{
							/* Need password */
						} else if(strncmp(sitecode, "332", 3) == 0)
						{
							/* Need account */
							ftd->retrywhat = threadsite->status;
							set_status(threadsite, STATUSRETRY);
						} else if(strncmp(sitecode, "532", 3) == 0)
						{
							/* Need account for storing files. */
							ftd->retrywhat = threadsite->status;
							set_status(threadsite, STATUSRETRY);
						} else if(strncmp(sitecode, "227", 3) == 0)
						{
							/* Entering passive mode */
							getipport(&controlbuffer[start], &ftd->pasvip, &ftd->pasvport);
							writeconsole(threadsite, locale_string("Entering passive mode.", 129));
							ftd->commandready = TRUE;
						} else if(strncmp(sitecode, "200", 3) == 0)
						{
							/* Command ok */
							ftd->commandready = TRUE;

							if(controlbuffer[start+3] == '-')
							{
								ftd->commandready = FALSE;
								ftd->in_200 = TRUE;
							}
						} else if(strncmp(sitecode, "226", 3) == 0)
						{
							if(threadsite->status == STATUSFXPTRANSFER)
							{
								setstatustext(threadsite, locale_string("FXP Transfer completed successfully.", 130));
								if(ftd->originalcommand == THRDFLUSH)
									set_status(threadsite, STATUSNEXT);
								else
								{
									set_status(threadsite, STATUSIDLE);
									if(!ftd->destsite || !(ftd->destsite->queue || ftd->destsite->currentqueue))
										sendthread(THRDHARDREFRESH, threadsite->page);
								}
							}
						} else if(strncmp(sitecode, "125", 3) == 0 && threadsite->status == STATUSSTOREWAIT)
						{
							ftd->commandready = TRUE;
						} else if(strncmp(sitecode, "425", 3) == 0 || strncmp(sitecode, "426", 3) == 0)
						{
							/* Transfer failed  - Can't open connection. */
							Queue *current = threadsite->currentqueue;

							/* Move current item back into the queue */
							threadsite->currentqueue = NULL;
							if(current)
								requeue(threadsite, current);

							ftd->commandready = FALSE;
							set_status(threadsite, STATUSRETRY);
							if(threadsite->datafd)
								sockclose(threadsite->datafd);
							if(ftd->listenfd)
								sockclose(ftd->listenfd);
							threadsite->datafd = ftd->listenfd = 0;
							if(ftd->destsite)
								sendthread(THRDABORT, ftd->destsite->page);
						}
						writeconsole(threadsite, &controlbuffer[start]);
					}
					start = z+1;
				}
			}
			if(controlbuffer[z] == '\n')
			{
				controlbuffer[z] = 0;
				if(strlen(&controlbuffer[start]) > 0)
					writeconsole(threadsite, &controlbuffer[start]);
			}
			else
			{
				if(strlen(&controlbuffer[start]) > 0)
					strncpy(nexttime, &controlbuffer[start], 512);
			}

			if(threadsite->status == STATUSIDENT && ftd->commandready == TRUE)
			{
				ftd->thissitetype = determinesitetype(controlbuffer);
				if(sockwrite(threadsite->controlfd, "SYST\r\n", 6, 0) > 0)
				{
					ftd->commandready = FALSE;
					set_status(threadsite, STATUSCWD);
				}
			} else if(threadsite->status == STATUSLOGIN)
			{
				int result;
				
				if(strcmp(threadsite->username, "") == 0)
					result = socksprint(threadsite->controlfd, "USER anonymous\r\n");
				else
					result = socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "USER %s\r\n", threadsite->username));
				if(result > 0)
					set_status(threadsite, STATUSPASSWORD);
			} else if(threadsite->status == STATUSPASSWORD)
			{
				int result;
				
				if(strcmp(threadsite->password, "") == 0)
					result = socksprint(threadsite->controlfd, "PASS handyftp@netlabs.org\r\n");
				else
					result = socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "PASS %s\r\n", threadsite->password));
				if(result > 0)
					set_status(threadsite, STATUSIDENT);
			} else if(threadsite->status == STATUSCWD)
			{
				if(threadsite->url && strlen(threadsite->url) > 0)
				{
					if(socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "CWD %s\r\n", threadsite->url)) > 0)
						writeconsole(threadsite, locale_string("Changing working directory to \"%s\".", 131), threadsite->url);
				}
				if(sockwrite(threadsite->controlfd, "PWD\r\n", 5, 0) > 0)
				{
					ftd->currentfxpstate = !reversefxp;
					set_status(threadsite, STATUSLSPORT);
				}
			} else if(threadsite->status == STATUSLSPORT)
			{
				ftd->dirlen = 0;
				ftd->dirbuffer = NULL;
				if(ftd->currentfxpstate == FALSE)
				{
					/* Passive FTP */
					if(sockwrite(threadsite->controlfd, "PASV\r\n", 6, 0) > 0)
					{
						ftd->commandready = FALSE;
						set_status(threadsite, STATUSPASVLS);
					}
				}
				else
				{
					/* Standard FTP */
					union ip4_32 port4;
					memset(&server, 0, sizeof(server));
					server.sin_family = AF_INET;
					server.sin_port   = 0;
					server.sin_addr.s_addr = INADDR_ANY;
					if ((ftd->listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 || 
						bind(ftd->listenfd, (struct sockaddr *)&server, sizeof(server)) < 0 || listen(ftd->listenfd, 0) < 0)
					{
						writeconsole(threadsite, locale_string("Error binding to port of data connection.", 132));
						return exitthread;
					}
					else
					{
						struct sockaddr_in listen_addr = { 0 };
						socklen_t len = sizeof(struct sockaddr_in);

						getsockname(ftd->listenfd, (struct sockaddr *)&listen_addr, &len);

						port4.ip32 = ntohs(listen_addr.sin_port);
					}
					dw_mutex_unlock(h);
					msleep(NAT_DELAY);
					dw_mutex_lock(h);
					if(socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "PORT %d,%d,%d,%d,%d,%d\r\n", 
						ftd->our_ip.ip4.ip4,ftd->our_ip.ip4.ip3,ftd->our_ip.ip4.ip2,
						ftd->our_ip.ip4.ip1,port4.ip4.ip2,port4.ip4.ip1)) > 0)
							set_status(threadsite, STATUSLS);
				}
			} else if(threadsite->status == STATUSLS)
			{
				if(sockwrite(threadsite->controlfd, "LIST\r\n", 6, 0) > 0)
					set_status(threadsite, STATUSDIRACCEPT);
			} else if(threadsite->status == STATUSRETR)
			{
				if(socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "RETR %s\r\n", threadsite->currentqueue->srcfilename)) > 0)
					set_status(threadsite, STATUSDATAACCEPT);
			} else if(threadsite->status == STATUSSTORE && ftd->commandready == TRUE)
			{
				ftd->commandready = FALSE;
				if(!ftd->destsite->currentqueue)
				{
					writeconsole(threadsite, locale_string("Other site has aborted, retrying.", 133));
					ftd->retrywhat = threadsite->status;
					set_status(threadsite, STATUSRETRY);
				}
				else
				{
					if(sockwrite(threadsite->controlfd, "TYPE I\r\n", 8, 0) > 0 &&
						socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "STOR %s\r\n", 
						ftd->destsite->currentqueue->srcfilename)) > 0)
					{
						/* If we are in standard FTP listenfd will be greater than zero */
						if(ftd->listenfd > 0)
							set_status(threadsite, STATUSDATAACCEPT);
						else
							set_status(threadsite, STATUSSTOREWAIT);
					}
				}
			} else if(threadsite->status == STATUSSTOREWAIT /*&& ftd->commandready == TRUE*/)
			{
				ftd->commandready = FALSE;
				memset(&server, 0, sizeof(server));
				server.sin_family      = AF_INET;
				server.sin_port        = htons(ftd->pasvport.ip32);
				server.sin_addr.s_addr = htonl(ftd->pasvip.ip32);
				if((threadsite->datafd = socket(AF_INET, SOCK_STREAM, 0)) < 0 || 
					connect(threadsite->datafd, (struct sockaddr *)&server, sizeof(server)))
				{
					writeconsole(threadsite, locale_string("Failed to establish connection.", 134));
					ftd->retrywhat = threadsite->status;
					set_status(threadsite, STATUSRETRY);
					threadsite->datafd = 0;
				}
				else
				{
					set_status(threadsite, STATUSTRANSMIT);
					writeconsole(threadsite, locale_string("Data connection established.", 135));
					ftd->mytimer = time(NULL);
					nonblock(threadsite->datafd);
				}
			} else if(threadsite->status == STATUSPASVLS && ftd->commandready == TRUE &&
					sockwrite(threadsite->controlfd, "LIST\r\n", 6, 0) > 0)
			{
				server.sin_family      = AF_INET;
				server.sin_port        = htons(ftd->pasvport.ip32);
				server.sin_addr.s_addr = htonl(ftd->pasvip.ip32);
				if((threadsite->datafd = socket(AF_INET, SOCK_STREAM, 0)) < 0 || 
					connect(threadsite->datafd, (struct sockaddr *)&server, sizeof(server)))
				{
					writeconsole(threadsite, locale_string("Failed to establish data connection.", 186));
					ftd->retrywhat = threadsite->status;
					set_status(threadsite, STATUSRETRY);
					threadsite->datafd = 0;
				} else
				{
					nonblock(threadsite->datafd);
				}
				ftd->mytimer = time(NULL);
				set_status(threadsite, STATUSDIR);
			} else if(threadsite->status == STATUSPASVRETR && ftd->commandready == TRUE &&
					sockwrite(threadsite->controlfd, "TYPE I\r\n", 8, 0) > 0)
			{
				/* If we can try to resume download */
				if(ftd->filesize)
				{
					if(socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "REST %lu\r\n", ftd->filesize)) > 0)
						ftd->destsite->sent = ftd->filesize;
				}
				
				if(socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "RETR %s\r\n", 
					threadsite->currentqueue->srcfilename)) > 0)
				{
					server.sin_family      = AF_INET;
					server.sin_port        = htons(ftd->pasvport.ip32);
					server.sin_addr.s_addr = htonl(ftd->pasvip.ip32);
					if((threadsite->datafd = socket(AF_INET, SOCK_STREAM, 0)) < 0 || 
						connect(threadsite->datafd, (struct sockaddr *)&server, sizeof(server)))
					{
						writeconsole(threadsite, locale_string("Failed to establish data connection.", 186));
						ftd->retrywhat = threadsite->status;
						set_status(threadsite, STATUSRETRY);
						threadsite->datafd = 0;
					} else
					{
						nonblock(threadsite->datafd);
					}
					ftd->mytimer = time(NULL);
					set_status(threadsite, STATUSDATA);
				}
			} else if(threadsite->status == STATUSFXPRETR && ftd->commandready == TRUE)
			{
				if(ftd->destsite)
				{
					if(sockwrite(threadsite->controlfd, "TYPE I\r\n", 8, 0) > 0 &&
						socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "RETR %s\r\n", 
						threadsite->currentqueue->srcfilename)) > 0)
					{
						if(ftd->currentfxpstate == FALSE)
						{
							sprintf(ftd->destsite->thrdcommand, "%d,%d,%d,%d,%d,%d", ftd->pasvip.ip4.ip1,ftd->pasvip.ip4.ip2,ftd->pasvip.ip4.ip3,ftd->pasvip.ip4.ip4,ftd->pasvport.ip4.ip2,ftd->pasvport.ip4.ip1);
							sendthread(THRDFXPSTART, ftd->destsite->page);
						}
						set_status(threadsite, STATUSFXPTRANSFER);
						ftd->mytimer = time(NULL);
					}
				}
				else
				{
					dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("Fatal FXP error! (STATUSFXPRETR) Aborting.", 136));
					if(ftd->destsite)
						sendthread(THRDABORT, ftd->destsite->page);
					sendthread(THRDABORT, threadsite->page);
				}
			} else if(threadsite->status == STATUSFXPSTOR && ftd->commandready == TRUE)
			{
				if(ftd->destsite)
				{
					if(sockwrite(threadsite->controlfd, "TYPE I\r\n", 8, 0) > 0 &&
						socksprint(threadsite->controlfd, vargs(alloca(1024), 1023, "STOR %s\r\n", 
						ftd->destsite->currentqueue->srcfilename)) > 0)
					{
						if((ftd->currentfxpstate == FALSE && ftd->originalcommand == THRDFLUSH) || ftd->currentfxpstate == TRUE)
						{
							sprintf(ftd->destsite->thrdcommand, "%d,%d,%d,%d,%d,%d", ftd->pasvip.ip4.ip1,ftd->pasvip.ip4.ip2,
									ftd->pasvip.ip4.ip3,ftd->pasvip.ip4.ip4,ftd->pasvport.ip4.ip2,ftd->pasvport.ip4.ip1);
							sendthread(THRDFXPSTART, ftd->destsite->page);
						}
						set_status(threadsite, STATUSFXPTRANSFER);
						ftd->mytimer = time(NULL);
					}
				}
				else
				{
					dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("Fatal FXP error! (STATUSFXPSTOR) Aborting.", 137));
					if(ftd->destsite)
						sendthread(THRDABORT, ftd->destsite->page);
					sendthread(THRDABORT, threadsite->page);
				}
			}
		}
	}
	DBUG_POINT("tab_thread");
	if(ftd->listenfd && FD_ISSET(ftd->listenfd, &readset))
	{
		if(threadsite->status == STATUSDIRACCEPT || threadsite->status == STATUSDATAACCEPT)
		{
			socklen_t clientsize = sizeof(client);
			threadsite->datafd = accept(ftd->listenfd, (struct sockaddr *)&client, &clientsize);
			nonblock(threadsite->datafd);
			if(threadsite->status == STATUSDIRACCEPT)
				set_status(threadsite, STATUSDIR);
			else
			{
				if(ftd->originalcommand == THRDRECEIVE)
					set_status(threadsite, STATUSTRANSMIT);
				else
					set_status(threadsite, STATUSDATA);
			}
			ftd->mytimer = time(NULL);
		}
	}

#ifdef DEBUG
	if(threadsite->status != STATUSIDLE)
		dw_debug("[%s] Datafd - %s\n", threadsite->hosttitle, sitestatus[threadsite->status]);
#endif
	DBUG_POINT("tab_thread");
	if(threadsite->datafd && ((threadsite->status == STATUSTRANSMIT && FD_ISSET(threadsite->datafd, &writeset)) ||
		 (FD_ISSET(threadsite->datafd, &readset) && ftd->transbufsize < 4096)))
	{
		time_t curtime = time(NULL);
		if(threadsite->status == STATUSTRANSMIT)
		{
			int amt;
			
			if(ftd->transferdone == TRUE && ftd->transmitted >= threadsite->sent)
			{
				if(threadsite->datafd)
					sockclose(threadsite->datafd);
				if(ftd->listenfd)
					sockclose(ftd->listenfd);
				ftd->listenfd = threadsite->datafd = 0;
				set_status(threadsite, STATUSIDLE);
				if(curtime-ftd->mytimer == 0)
					curtime++;
				writeconsole(threadsite, locale_string("Transfer completed. %d bytes sent in %d seconds (%.2fK/s).", 138), threadsite->sent, (int)(curtime - ftd->mytimer), (double)((threadsite->sent-ftd->filesize)/1024)/((long)(curtime-ftd->mytimer)));
				setstatustext(threadsite, locale_string("Remote directory, connected.", 38));
				dw_percent_set_pos(threadsite->percent, 0);
			}
			/* Empty the transmission buffer */
			while(ftd->transbufsize > 0 &&
				(amt = sockwrite(threadsite->datafd, ftd->transbuf, ftd->transbufsize, 0)) != -1)
			{
				ftd->transmitted += amt;
				ftd->transbufsize -= amt;
				memmove(ftd->transbuf, &ftd->transbuf[amt], ftd->transbufsize);
				if(ftd->transbufsize > 0)
				{
					/* If we didn't empty the buffer... 
					 * release the mutex briefly... then try again.
					 */
					dw_mutex_unlock(h);
					msleep(10);
					dw_mutex_lock(h);
				}
			}
		}
		else
		{
			amnt = sockread(threadsite->datafd, &ftd->transbuf[ftd->transbufsize], 4096-ftd->transbufsize, 0);
			if(amnt > 0)
				ftd->transbufsize += amnt;
			if(threadsite->status == STATUSDIR && ftd->transbufsize > 0)
			{
				int olddirlen = ftd->dirlen;

				ftd->dirlen += ftd->transbufsize;
				if(ftd->dirbuffer == NULL)
					ftd->dirbuffer = malloc(ftd->dirlen);
				else
				{
					char *tmppointer = malloc(ftd->dirlen);
					memcpy(tmppointer, ftd->dirbuffer, olddirlen);
					free(ftd->dirbuffer);
					ftd->dirbuffer = tmppointer;
				}
				memcpy(&ftd->dirbuffer[ftd->dirlen-ftd->transbufsize], ftd->transbuf, ftd->transbufsize);
				ftd->transbufsize = 0;
			}
			if(amnt < 1)
			{
				if(threadsite->datafd)
					sockclose(threadsite->datafd);
				if(ftd->listenfd)
					sockclose(ftd->listenfd);
				ftd->listenfd = threadsite->datafd = 0;
				if(threadsite->status == STATUSDIR)
				{
					loadremotedir(threadsite, ftd->dirbuffer, ftd->dirlen, ftd->thissitetype);
					addtocache(threadsite);
					drawdir(threadsite);
					free(ftd->dirbuffer);
					ftd->dirbuffer = NULL;
					ftd->dirlen = 0;
					/* If we are recursing into directories
					 * queue up all the files in this directory
					 * listing with the exception of ".."
					 */
					if(threadsite->queuing)
					{
						Directory *tmp = threadsite->dir;
						int counter = 0;

						while(tmp)
						{
							if(strcmp(tmp->entry, "..") != 0)
								addtoq(counter, threadsite->page);
							tmp = tmp->next;
							counter++;
						}
						drawq(threadsite);
						recurseq(threadsite);
					}
					set_status(threadsite, STATUSIDLE);
				}
				else
				{
					if(ftd->destsite && ftd->destsite->tpipefd[1])
					{
						int amt;
						
						/* Empty the transmission buffer */
						while(ftd->transbufsize > 0 &&
							(amt = sockwrite(ftd->destsite->tpipefd[1], ftd->transbuf, ftd->transbufsize, 0)) != -1)
						{
							ftd->destsite->sent += amt;
							ftd->transbufsize -= amt;
							memmove(ftd->transbuf, &ftd->transbuf[amt], ftd->transbufsize);
							if(ftd->transbufsize > 0)
							{
								/* If we didn't empty the buffer... 
								 * release the mutex briefly... then try again.
								 */
								dw_mutex_unlock(h);
								msleep(10);
								dw_mutex_lock(h);
							}
						}
					}
					if(ftd->destsite)
						sendthread(THRDDONE, ftd->destsite->page);
					if(curtime-ftd->mytimer == 0)
						curtime++;

					if(ftd->destsite)
						writeconsole(threadsite, locale_string("Transfer completed. %d bytes sent in %d seconds (%.2fK/s).", 138), ftd->destsite->sent, (int)(curtime - ftd->mytimer), (double)((ftd->destsite->sent-ftd->filesize)/1024)/((long)curtime-ftd->mytimer));
					else
						writeconsole(threadsite, locale_string("Transfer completed.", 139));

					setstatustext(threadsite, locale_string("Remote directory, connected.", 38));
					dw_percent_set_pos(threadsite->percent, DW_PERCENT_INDETERMINATE);
				}
				set_status(threadsite, STATUSNEXT);
			}
		}
	}
#ifdef DEBUG
	if(threadsite->status != STATUSIDLE)
		dw_debug("[%s] Tpipefd - %s\n", threadsite->hosttitle, sitestatus[threadsite->status]);
#endif
	DBUG_POINT("tab_thread");

	if(FD_ISSET(threadsite->tpipefd[0], &readset)  && (threadsite->status == STATUSRECEIVING || (ftd->transbufsize < 4096 && threadsite->status == STATUSTRANSMIT)))
	{
		time_t curtime = time(NULL);

		amnt = sockread(threadsite->tpipefd[0], &ftd->transbuf[ftd->transbufsize], 4096-ftd->transbufsize, 0);
		ftd->received += amnt;
		ftd->transbufsize += amnt;
		if(strcasecmp(threadsite->hostname, "local") == 0)
		{
			fwrite(ftd->transbuf, 1, amnt, ftd->localfile);
			ftd->transbufsize=0;
		}
		if(threadsite->status != STATUSTRANSMIT && ftd->transferdone == TRUE && ftd->received >= threadsite->sent)
		{
			if(strcasecmp(threadsite->hostname, "local") == 0)
			{
				fclose(ftd->localfile);
				ftd->localfile = NULL;
			}
			set_status(threadsite, STATUSIDLE);
			ftd->originalcommand = -1;
		}
		if(threadsite->status != STATUSIDLE)
		{
			/* Save the size of the current transfer */
			if(!ftd->currentqueuesize && ftd->destsite && ftd->destsite->currentqueue)
				ftd->currentqueuesize = ftd->destsite->currentqueue->size;

			update_eta(threadsite, FALSE, ftd->received, ftd->currentqueuesize, curtime, ftd->mytimer, &ftd->lastupdate, ftd->filesize);
		}
	}

#ifdef DEBUG
	if(threadsite->status != STATUSIDLE)
		dw_debug("[%s] Tpipefd 2 - %s\n", threadsite->hosttitle, sitestatus[threadsite->status]);
#endif
	if((threadsite->status == STATUSSENDING || threadsite->status == STATUSDATA) && ftd->destsite && FD_ISSET(ftd->destsite->tpipefd[1], &writeset))
	{
		time_t curtime = time(NULL);

		amnt = 0;
		if(strcasecmp(threadsite->hostname, "local") == 0)
		{
			if(!feof(ftd->localfile))
			{
				int amt = 0;
				
				amnt = fread(ftd->transbuf, 1, 4096, ftd->localfile);
				
				/* Empty the transmission buffer */
				while(amnt > 0 &&
					(amt = sockwrite(ftd->destsite->tpipefd[1], ftd->transbuf, amnt, 0)) != -1)
				{
					ftd->destsite->sent += amt;
					amnt -= amt;
					memmove(ftd->transbuf, &ftd->transbuf[amt], ftd->transbufsize);
					if(amnt > 0)
					{
						/* If we didn't empty the buffer... 
						 * release the mutex briefly... then try again.
						 */
						dw_mutex_unlock(h);
						msleep(10);
						dw_mutex_lock(h);
					}
				}
			}
			else
			{
				fclose(ftd->localfile);
				ftd->localfile = NULL;

				/* Make sure the other thread still isn't getting ready */
				wait_site(&ftd->destsite->status, STATUSSTORE);

				threadsite->currentqueue = NULL;
				set_status(threadsite, STATUSNEXT);
				sendthread(THRDDONE, ftd->destsite->page);
			}
			if(threadsite->status == STATUSNEXT)
			{
				if(curtime-ftd->mytimer == 0)
					curtime++;
				writeconsole(threadsite, locale_string("Transfer completed. %d bytes sent in %d seconds (%.2fK/s).", 138), ftd->destsite->sent, curtime - ftd->mytimer, (double)((ftd->destsite->sent-ftd->filesize)/1024)/(curtime-ftd->mytimer));
				if(strcasecmp(threadsite->hostname, "local") == 0)
					setstatustext(threadsite, locale_string("Local directory, connected.", 44));
				else
					setstatustext(threadsite, locale_string("Remote directory, connected.", 38));
				dw_percent_set_pos(threadsite->percent, DW_PERCENT_INDETERMINATE);
			}
			else
				update_eta(threadsite, TRUE, ftd->destsite->sent, threadsite->currentqueue->size, curtime, ftd->mytimer, &ftd->lastupdate, ftd->filesize);
		}
		if(threadsite->status == STATUSDATA)
		{
			int amt;
			
			/* Empty the transmission buffer */
			while(ftd->transbufsize > 0 &&
				(amt = sockwrite(ftd->destsite->tpipefd[1], ftd->transbuf, ftd->transbufsize, 0)) != -1)
			{
				ftd->destsite->sent += amt;
				ftd->transbufsize -= amt;
				memmove(ftd->transbuf, &ftd->transbuf[amt], ftd->transbufsize);
				if(ftd->transbufsize > 0)
				{
					/* If we didn't empty the buffer... 
					 * release the mutex briefly... then try again.
					 */
					dw_mutex_unlock(h);
					msleep(10);
					dw_mutex_lock(h);
				}
			}
			update_eta(threadsite, TRUE, ftd->destsite->sent, threadsite->currentqueue->size, curtime, ftd->mytimer, &ftd->lastupdate, ftd->filesize);
		}
	}

	/* If the previous command failed we should retry it until the retry max is exceeded */
	if(threadsite->status == STATUSRETRY)
	{
		if(ftd->retrycount > retrymax)
		{
			writeconsole(threadsite, locale_string("Retry maximum exceeded, proceeding to next item.", 140));
			if(threadsite->status == STATUSSENDING || threadsite->status == STATUSRETR ||
			   threadsite->status == STATUSDATA || threadsite->status == STATUSDATAACCEPT)
				set_status(threadsite, STATUSNEXT);
			else if(threadsite->status == STATUSLS || threadsite->status == STATUSLSPORT ||
					threadsite->status == STATUSDIR || threadsite->status == STATUSDIRACCEPT)
			{
				ftd->originalcommand = -1;
				set_status(threadsite, STATUSIDLE);
				if(ftd->destsite)
					sendthread(THRDABORT, ftd->destsite->page);
			}
			else
			{
				ftd->originalcommand = -1;
				set_status(threadsite, STATUSIDLE);
				if(ftd->destsite)
					sendthread(THRDABORT, ftd->destsite->page);
			}
		}

		/* This is going to need some work */
		if(ftd->retrywhat == STATUSLS || ftd->retrywhat == STATUSLSPORT || ftd->retrywhat == STATUSDIR)
			set_status(threadsite, STATUSLS);
		else
			set_status(threadsite, STATUSIDLE);

	}

	/* The last command completed or ran out of retries */
	if(threadsite->status == STATUSNEXT)
	{
		set_status(threadsite, STATUSIDLE);

		if(threadsite->queue && ftd->originalcommand == THRDFLUSH)
			sendthread(THRDFLUSH, threadsite->page);
		else
		{
			if(ftd->originalcommand == THRDFLUSH)
			{
				dw_beep(1000, 100);
				sendthread(THRDHARDREFRESH, threadsite->page);
				if(ftd->destsite && ftd->destsite->status == STATUSIDLE)
					sendthread(THRDHARDREFRESH, ftd->destsite->page);
			}
			ftd->originalcommand = -1;
		}
	}
	return exitthread;
}

void DWSIGNAL tab_thread(void)
{
	int threadpage = newpage, exitthread = FALSE;
	SiteTab *threadsite;
	HMTX h = mutex;
	FTPData ftd = { NULL, 0, NULL, NULL, FALSE, 0, 0, -1, FALSE,
	0, 0, NULL, 0, 0, NULL, NULL, 0, 0, 0, 0, 0, FALSE, 0, 0, 0 };

	dw_event_wait(hev, -1);

	DBUG_POINT("tab_thread");

	/* Each thread has it's own signal handlers */
	signal(SIGSEGV, handyftp_crash);

	if(threadpage == -1)
		return;

	newpage = -1;

	dw_mutex_lock(h);

	DBUG_POINT("tab_thread");
	threadsite = site[threadpage];

	if(!threadsite->hosttitle || !threadsite->hostname || !threadsite->initialdir ||
	   !threadsite->url || !threadsite->username || !threadsite->password)
	{
		/* Make sure variables are valid pointers */
		if(threadpage == 0)
		{
			threadsite->hosttitle = strdup("Local");
			threadsite->hostname = strdup("local");
			threadsite->initialdir = strdup(dw_user_dir());
			threadsite->url = strdup(dw_user_dir());

			/* Set the entries on the current page */
			dw_window_set_text(threadsite->host_title, threadsite->hosttitle);
			dw_window_set_text(threadsite->host_name, threadsite->hostname);
			dw_window_set_text(threadsite->directory, threadsite->url);
		}
		else
		{
			threadsite->hosttitle = strdup(empty_string);
			threadsite->hostname = strdup(empty_string);
			threadsite->initialdir = strdup(empty_string);
			threadsite->url = strdup(empty_string);
		}

		threadsite->username = strdup(empty_string);
		threadsite->password = strdup(empty_string);

	}

	set_status(threadsite, STATUSIDLE);

	DBUG_POINT("tab_thread");
	if(strcasecmp(threadsite->hostname, "local") == 0)
	{
		dw_mutex_unlock(h);
		loadlocaldir(threadsite);
		dw_mutex_lock(h);
		addtocache(threadsite);
		drawdir(threadsite);
	}
	else
		threadsite->connected = FALSE;

	DBUG_POINT("tab_thread");
	while(exitthread == FALSE)
	{
		exitthread = FTPIteration(threadsite, threadpage, h, &ftd);
	}  /* End main loop */


	DBUG_POINT("tab_thread");
	if(ftd.localfile)
		fclose(ftd.localfile);
	if(threadsite->datafd)
		sockclose(threadsite->datafd);
	if(threadsite->controlfd)
		sockclose(threadsite->controlfd);
	if(ftd.listenfd)
		sockclose(ftd.listenfd);
	sockclose(threadsite->pipefd[0]);
	sockclose(threadsite->pipefd[1]);
	sockclose(threadsite->tpipefd[0]);
	sockclose(threadsite->tpipefd[1]);
	freecache(threadsite);
	threadsite->dir = NULL;
	threadsite->cache = NULL;
	freequeue(threadsite->queue);
	threadsite->queue = NULL;
	site_unref(threadsite);
	dw_mutex_unlock(h);
}

/* Removes the current tab */
void remove_tab(void)
{
	int thispage;

	if (countpages() == 1)
	{
		dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("You cannot remove the last tab.", 141));
		return;
	}

	thispage = currentpage;

	alive[thispage] = FALSE;

	dw_notebook_page_destroy(hwndNBK, site[thispage]->pageid);

	sendthread(THRDEXIT, thispage);
	site[thispage] = NULL;
	settabs();
}

/* Handle sort request */
int DWSIGNAL column_clicked(HWND window, int column_num, void *data)
{
	if(validatecurrentpage())
	{
		int oldsort = site[currentpage]->sort;

		/* Pick the sort mode based on the column clicked */
		if(column_num == 0)
			site[currentpage]->sort = SORT_FILE;
		else if(column_num == 1)
			site[currentpage]->sort = SORT_SIZE;
		else
			site[currentpage]->sort = SORT_DATE;

		if(abs(oldsort) == site[currentpage]->sort)
			site[currentpage]->sort = -oldsort;

		/* If the site is connected then refresh */
		if(site[currentpage]->connected)
			sendthread(THRDHARDREFRESH, currentpage);
	}
	return FALSE;
}

/* Creates a new tab and makes it current */
void new_tab(void *data)
{
	char szBuffer[200];
	int thispage;
	int previouspage = currentpage;
	HMTX h = mutex;
	HWND stext, pagebox, splitbar, vsplitbar, percentbox,
		controlbox, lcontainer, rcontainer, hostcombo, destcombo, percent, status;

	DBUG_POINT("new_tab");

	dw_mutex_lock(h);

	thispage = currentpage = finddead();
	site[thispage] = (SiteTab *)calloc(1, sizeof(SiteTab));
	site_ref(site[thispage]);

	sockpipe(site[thispage]->pipefd);
	if(site[thispage]->pipefd[0] < 0 || site[thispage]->pipefd[1] < 0)
	{
		/* Just to be certain :) */
		site[thispage]->pipefd[0] = -1;
		site[thispage]->pipefd[1] = -1;
	}
	else
	{
		nonblock(site[thispage]->pipefd[1]);
		nonblock(site[thispage]->pipefd[0]);
	}

	sockpipe(site[thispage]->tpipefd);
	if(site[thispage]->tpipefd[0] < 0 || site[thispage]->tpipefd[1] < 0)
	{
		/* Just to be certain :) */
		site[thispage]->tpipefd[0] = -1;
		site[thispage]->tpipefd[1] = -1;
	}
	else
	{
		nonblock(site[thispage]->tpipefd[1]);
		nonblock(site[thispage]->tpipefd[0]);
	}

	if(site[thispage]->pipefd[0] < 1 || site[thispage]->pipefd[1] < 1 ||
	   site[thispage]->tpipefd[0] < 1 || site[thispage]->tpipefd[1] < 1)
	{
		if(site[thispage]->pipefd[0])
			sockclose(site[thispage]->pipefd[0]);
		if(site[thispage]->pipefd[1])
			sockclose(site[thispage]->pipefd[1]);
		if(site[thispage]->tpipefd[0])
			sockclose(site[thispage]->tpipefd[0]);
		if(site[thispage]->tpipefd[1])
			sockclose(site[thispage]->tpipefd[1]);
		free(site[thispage]);
		site[thispage]=NULL;
		dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("Error creating data pipes! tab creation aborted.", 142));
		return;
	}

	site[thispage]->controlfd = 0;
	site[thispage]->datafd = 0;
	site[thispage]->queue = NULL;
	site[thispage]->currentqueue = NULL;
	site[thispage]->dir = NULL;
	site[thispage]->status = STATUSIDLE;
	site[thispage]->connected = FALSE;
	site[thispage]->page = thispage;
	site[thispage]->cache = NULL;
	site[thispage]->cachecount = 0;
	site[thispage]->queuing = NULL;
	site[thispage]->sort = default_sort;

	if(!(site[thispage]->hwnd = dw_box_new(BOXVERT, 2)))
	{
		dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("Could not create new tab!", 143));
		currentpage = previouspage;
		return;
	}

	pagebox = site[thispage]->hwnd;

	site[thispage]->pageid = dw_notebook_page_new(hwndNBK, 0L, FALSE);

	dw_notebook_pack(hwndNBK, site[thispage]->pageid, pagebox);

	/* Due to a GTK limitiation the page text must be set after the page is packed */
	dw_notebook_page_set_text(hwndNBK, site[thispage]->pageid, "Local");
	dw_notebook_page_set_status_text(hwndNBK, site[thispage]->pageid, locale_string("Page 1 of 1", 144));

	/* Control line #1 */
	controlbox = dw_box_new(BOXHORZ, 1);

	dw_box_pack_start(pagebox, controlbox, 0, 0, TRUE, FALSE, 0);

	stext = dw_text_new(locale_string("Host Title:", 145), 0);

	dw_window_set_style(stext, DW_DT_VCENTER, DW_DT_VCENTER);

	dw_box_pack_start(controlbox, stext, -1, 22, FALSE, FALSE, 0);

	site[thispage]->host_title = hostcombo = dw_combobox_new("Local", HOST_TITLE);

	dw_signal_connect(hostcombo, DW_SIGNAL_LIST_SELECT, DW_SIGNAL_FUNC(listboxselect), NULL);

	dw_box_pack_start(controlbox, hostcombo, 200, 22, TRUE, FALSE, 0);
    dw_window_set_tooltip(hostcombo, "Name of the site on this tab, pick from saved sites here.");

	stext = dw_text_new(locale_string("Hostname:", 146), 0);

	dw_window_set_style(stext, DW_DT_CENTER | DW_DT_VCENTER, DW_DT_CENTER | DW_DT_VCENTER);

	dw_box_pack_start(controlbox, stext, -1, 22, FALSE, FALSE, 0);

	site[thispage]->host_name = dw_entryfield_new("", EF_HOSTNAME);

	dw_box_pack_start(controlbox, site[thispage]->host_name, 160, 22, TRUE, FALSE, 0);
    dw_window_set_tooltip(site[thispage]->host_name, "Internet name or address of the server; or local.");

	stext = dw_text_new(locale_string("Port:", 147), 0);

	dw_window_set_style(stext, DW_DT_CENTER | DW_DT_VCENTER, DW_DT_CENTER | DW_DT_VCENTER);

	dw_box_pack_start(controlbox, stext, -1, 22, FALSE, FALSE, 0);

	site[thispage]->port_num = dw_spinbutton_new("", SPB_PORT);

	dw_spinbutton_set_limits(site[thispage]->port_num, 65535L, 0L);
	dw_spinbutton_set_pos(site[thispage]->port_num, 21);

	dw_box_pack_start(controlbox, site[thispage]->port_num, 65, 22, FALSE, FALSE, 0);
    dw_window_set_tooltip(site[thispage]->port_num, "TCP/IP port number from 0 to 65535");

	stext = dw_text_new(locale_string("Username:", 148), 0);

	dw_window_set_style(stext, DW_DT_CENTER | DW_DT_VCENTER, DW_DT_CENTER | DW_DT_VCENTER);

	dw_box_pack_start(controlbox, stext, -1, 22, FALSE, FALSE, 0);

	site[thispage]->user_name = dw_entryfield_new("", EF_USERNAME);

	dw_box_pack_start(controlbox, site[thispage]->user_name, 160, 22, TRUE, FALSE, 0);

	stext = dw_text_new(locale_string("Password:", 149), 0);

	dw_window_set_style(stext, DW_DT_CENTER | DW_DT_VCENTER, DW_DT_CENTER | DW_DT_VCENTER);

	dw_box_pack_start(controlbox, stext, -1, 22, FALSE, FALSE, 0);

	if(showpassword)
		site[thispage]->pass_word = dw_entryfield_new("", EF_PASSWORD);
	else
		site[thispage]->pass_word = dw_entryfield_password_new("", EF_PASSWORD);

	dw_box_pack_start(controlbox, site[thispage]->pass_word, 160,22, TRUE, FALSE, 0);

	/* Control line #2 */
	controlbox = dw_box_new(BOXHORZ, 1);

	dw_box_pack_start(pagebox, controlbox, 0, 0, TRUE, FALSE, 0);

	stext = dw_text_new(locale_string("Directory:", 150), 0);

	dw_window_set_style(stext, DW_DT_VCENTER, DW_DT_VCENTER);

	dw_box_pack_start(controlbox, stext, -1, 22, FALSE, FALSE, 0);

	site[thispage]->directory = dw_entryfield_new("", EF_DIRECTORY);

	dw_entryfield_set_limit(site[thispage]->directory, URL_LIMIT);

	dw_box_pack_start(controlbox, site[thispage]->directory, 240, 22, TRUE, FALSE, 0);
    dw_window_set_tooltip(site[thispage]->directory, "Path on the current site, displayed below if connected.");

	stext = dw_text_new(locale_string("Destination:", 151), 0);

	dw_window_set_style(stext, DW_DT_CENTER | DW_DT_VCENTER, DW_DT_CENTER | DW_DT_VCENTER);

	dw_box_pack_start(controlbox, stext, -1, 22, FALSE, FALSE, 0);

	site[thispage]->destsite = destcombo = dw_combobox_new("", SITE);

	dw_box_pack_start(controlbox, destcombo, 240, 22, TRUE, FALSE, 0);
    dw_window_set_tooltip(destcombo, "Destination tab to add files to the transfer queue displayed below.");

	site[thispage]->ldir = lcontainer = dw_container_new(LDIR, TRUE);
	
	site[thispage]->rqueue = rcontainer = dw_container_new(QUEUE, TRUE);

	splitbar = dw_splitbar_new(BOXHORZ, lcontainer, rcontainer, 0);

	percentbox = dw_box_new(BOXVERT, 0);

	site[thispage]->percent = percent = dw_percent_new(PERCENT);

	dw_box_pack_start(percentbox, percent, 600, 20, TRUE, FALSE, 0);

	site[thispage]->server = status = dw_listbox_new(SERVER, FALSE);

#ifdef __OS2__
	dw_window_set_font(status, "5.System VIO");
#elif defined(__WIN32__)
	dw_window_set_font(status, "9.Terminal");
#elif defined(__MAC__)
	dw_window_set_font(status, "9.Monaco");
#else
	dw_window_set_font(status, "fixed");
#endif

	dw_window_set_color(status, DW_CLR_WHITE, DW_CLR_BLACK);

	dw_box_pack_start(percentbox, status, 600, 45, TRUE, TRUE, 0);

	vsplitbar = dw_splitbar_new(BOXVERT, splitbar, percentbox, 0);

	dw_splitbar_set(vsplitbar, 80.0);

	dw_box_pack_start(pagebox, vsplitbar, 100, 100, TRUE, TRUE, 0);

	controlbox = dw_box_new(BOXHORZ, 0);

	dw_box_pack_start(pagebox, controlbox, 0, 0, TRUE, FALSE, 0);

	site[thispage]->cstatus = stext = dw_status_text_new(locale_string("Welcome to HandyFTP.", 152), CSTATUS);

	dw_window_set_style(stext, DW_DT_VCENTER, DW_DT_VCENTER);

	dw_box_pack_start(controlbox, stext, 300, 20, TRUE, FALSE, 2);

	site[thispage]->cmorestatus = stext = dw_status_text_new(locale_string("Idle", 153), CSTATUS);

	dw_window_set_style(stext, DW_DT_VCENTER, DW_DT_VCENTER);

	dw_box_pack_start(controlbox, stext, 300, 20, TRUE, FALSE, 2);

	alive[thispage] = TRUE;

	if(firstpage != TRUE)
	{
		sprintf(szBuffer, locale_string("Site %d", 154), thispage+1);
		dw_window_set_text(site[thispage]->host_title, szBuffer);
	}

	settabs();

	updatesites(thispage);

	setdir(site[thispage]);
	setqueue(site[thispage]);

	/* Set the colors after the containers have been setup */
	dw_container_set_stripe(rcontainer, DW_CLR_DEFAULT, DW_CLR_DEFAULT);
	dw_container_set_stripe(lcontainer, DW_CLR_DEFAULT, DW_CLR_DEFAULT);

	/* Set up the defaults */
	dw_window_click_default(site[thispage]->host_title, site[thispage]->host_name);
	dw_window_click_default(site[thispage]->host_name, site[thispage]->port_num);
	dw_window_click_default(site[thispage]->port_num, site[thispage]->user_name);
	dw_window_click_default(site[thispage]->user_name, site[thispage]->pass_word);
	dw_window_click_default(site[thispage]->pass_word, site[thispage]->directory);
	dw_window_click_default(site[thispage]->directory, refreshbutton);

	/* Containers must be setup before conntecting signals to them */
	dw_signal_connect(lcontainer, DW_SIGNAL_ITEM_ENTER, DW_SIGNAL_FUNC(containerselect), NULL);
	dw_signal_connect(lcontainer, DW_SIGNAL_ITEM_CONTEXT, DW_SIGNAL_FUNC(containercontextmenu), NULL);
	dw_signal_connect(rcontainer, DW_SIGNAL_ITEM_CONTEXT, DW_SIGNAL_FUNC(containercontextmenu), NULL);
	dw_signal_connect(lcontainer, DW_SIGNAL_COLUMN_CLICK, DW_SIGNAL_FUNC(column_clicked), NULL);

	DBUG_POINT("new_tab");
	if(firstpage == TRUE)
	{
		firstpage = FALSE;
		dw_window_set_text(site[currentpage]->host_title, "Local");

		if(autoconnect == TRUE)
			dw_menu_item_set_check(menubar, IDM_AUTOCONNECT, TRUE);
		else
			dw_menu_item_set_check(menubar, IDM_AUTOCONNECT, FALSE);
		if(nofail == TRUE)
			dw_menu_item_set_check(menubar, IDM_NOFAIL, TRUE);
		else
			dw_menu_item_set_check(menubar, IDM_NOFAIL, FALSE);
	}

	DBUG_POINT("new_tab");
	dw_notebook_page_set(hwndNBK, site[thispage]->pageid);

	newpage = thispage;

	DBUG_POINT("new_tab");
	dw_thread_new((void *)tab_thread, NULL, (8*0xFFFF));

	dw_mutex_unlock(h);

	DBUG_POINT("new_tab");
}

/* Delete the current entry from the HandyFTP.ini file */
void unsavetitle(void)
{
	SavedSites *tmp, *last = NULL;
	char *buffer;

	tmp = SSroot;

	if(!(buffer = dw_window_get_text(site[currentpage]->host_title)))
		return;

	while(tmp != NULL)
	{
		if(strcasecmp(tmp->hosttitle, buffer)==0)
		{
			if(last)
				last->next = tmp->next;
			else
				SSroot = tmp->next;

			free(tmp->hosttitle);
			free(tmp->hostname);
			free(tmp->url);
			free(tmp->username);
			free(tmp->password);
			free(tmp);
			saveconfig();
			updateallsites();
			dw_free(buffer);
			return;
		}
		last=tmp;
		tmp=tmp->next;
	}
	dw_free(buffer);
}

/* Reads the saved site info from the HandyFTP.ini file */
void loadsaved(int page, int i)
{
	SavedSites *tmp;
	int count = 0;

	tmp = SSroot;

	if(i != DW_LIT_NONE)
	{
		while(tmp != NULL)
		{
			if(count == i)
			{
				if(site[page]->hosttitle)
					free(site[page]->hosttitle);
				site[page]->hosttitle = strdup(tmp->hosttitle);
				if(site[page]->hostname)
					free(site[page]->hostname);
				site[page]->hostname = strdup(tmp->hostname);
				if(site[page]->username)
					free(site[page]->username);
				site[page]->username = strdup(tmp->username);
				if(site[page]->password)
					free(site[page]->password);
				site[page]->password = strdup(tmp->password);
				if(site[page]->initialdir)
					free(site[page]->initialdir);
				site[page]->initialdir = strdup(tmp->url);
				if(site[page]->url)
					free(site[page]->url);
				site[page]->url = strdup(tmp->url);
				site[page]->port = tmp->port;

				dw_window_set_text(site[page]->host_title, site[page]->hosttitle);
				dw_window_set_text(site[page]->host_name, site[page]->hostname);
				dw_window_set_text(site[page]->user_name, site[page]->username);
				dw_window_set_text(site[page]->pass_word, site[page]->password);
				dw_window_set_text(site[page]->directory, site[page]->initialdir);
				dw_spinbutton_set_pos(site[page]->port_num, site[page]->port);
				settabs();
				return;
			}
			count++;
			tmp = tmp->next;
		}
	}

}

/* Saves the current site to the HandyFTP.ini file */
void savetitle(void)
{
	SavedSites *tmp, *last = NULL;
	char *buffer;

	tmp = SSroot;

	if(!(buffer = dw_window_get_text(site[currentpage]->host_title)))
		return;

	while(tmp != NULL)
	{
		if(strcasecmp(tmp->hosttitle, buffer)==0)
		{
			free(tmp->hosttitle);
			tmp->hosttitle = strdup(buffer);
			free(tmp->hostname);
			tmp->hostname = strdup(site[currentpage]->hostname);
			tmp->port = site[currentpage]->port;
			free(tmp->url);
			tmp->url = strdup(site[currentpage]->initialdir);
			free(tmp->username);
			tmp->username = strdup(site[currentpage]->username);
			free(tmp->password);
			tmp->password = strdup(site[currentpage]->password);
			saveconfig();
			dw_free(buffer);
			return;
		}
		last = tmp;
		tmp = tmp->next;
	}
	if(last)
	{
		last->next = (SavedSites *)malloc(sizeof(SavedSites));
		tmp = last->next;
	}
	else
	{
		SSroot = (SavedSites *)malloc(sizeof(SavedSites));
		tmp = SSroot;
	}
	tmp->next = NULL;
	tmp->hosttitle = (char *)malloc(strlen(buffer)+1);
	strcpy(tmp->hosttitle, buffer);
	tmp->hostname = (char *)malloc(strlen(site[currentpage]->hostname)+1);
	strcpy(tmp->hostname, site[currentpage]->hostname);
	tmp->port = site[currentpage]->port;
	tmp->url = (char *)malloc(strlen(site[currentpage]->initialdir)+1);
	strcpy(tmp->url, site[currentpage]->initialdir);
	tmp->username = (char *)malloc(strlen(site[currentpage]->username)+1);
	strcpy(tmp->username, site[currentpage]->username);
	tmp->password = (char *)malloc(strlen(site[currentpage]->password)+1);
	strcpy(tmp->password, site[currentpage]->password);
	saveconfig();
	updateallsites();
	dw_free(buffer);
}

/* Generic function to parse information from a config file */
void handyftp_getline(FILE *f, char *entry, char *entrydata)
{
	char in[256];
	int z;

	memset(in, 0, 256);
	
	if(fgets(in, 255, f))
	{
		if(in[strlen(in)-1] == '\n')
			in[strlen(in)-1] = 0;

		if(in[0] != '#')
		{
			for(z=0;z<strlen(in);z++)
			{
				if(in[z] == '=')
				{
					in[z] = 0;
					strcpy(entry, in);
					strcpy(entrydata, &in[z+1]);
					return;
				}
			}
		}
	}
	strcpy(entry, "");
	strcpy(entrydata, "");
}

/* Load the HandyFTP.ini file from disk setting all the necessary flags */
void loadconfig(void)
{
	char entry[256], entrydata[256];
	FILE *f;
	SavedSites *currentsite = SSroot;
	char *tmppath = INIDIR, *inipath, *home = dw_user_dir();

	if(strcmp(INIDIR, ".") == 0)
		inipath = strdup("handyftp.ini");
	else
	{
		if(home && tmppath[0] == '~')
		{
			inipath = malloc(strlen(home) + strlen(INIDIR) + 14);
			strcpy(inipath, home);
			strcat(inipath, &tmppath[1]);
		}
		else
		{
			inipath = malloc(strlen(INIDIR) + 14);
			strcat(inipath, INIDIR);
		}
		strcat(inipath, DIRSEP);
		strcat(inipath, "handyftp.ini");
	}

	f = fopen(inipath, FOPEN_READ_TEXT);

	free(inipath);

	if(f==NULL)
		return;

	while(!feof(f))
	{
		handyftp_getline(f, entry, entrydata);
		if(strcasecmp(entry, "nofail")==0 && strcasecmp(entrydata, "true") == 0)
			nofail = TRUE;
		if(strcasecmp(entry, "urlsave")==0 && strcasecmp(entrydata, "true") == 0)
			urlsave = TRUE;
		if(strcasecmp(entry, "autoconnect")==0 && strcasecmp(entrydata, "true") == 0)
			autoconnect = TRUE;
		if(strcasecmp(entry, "openall")==0 && strcasecmp(entrydata, "true") == 0)
			openall = TRUE;
		if(strcasecmp(entry, "reversefxp")==0 && strcasecmp(entrydata, "false") == 0)
			reversefxp = FALSE;
		if(strcasecmp(entry, "showpassword")==0 && strcasecmp(entrydata, "true") == 0)
			showpassword = TRUE;
		if(strcasecmp(entry, "optimize")==0 && strcasecmp(entrydata, "false") == 0)
			optimize = FALSE;
		if(strcasecmp(entry, "timeout")==0)
			ftptimeout = atoi(entrydata);
		if(strcasecmp(entry, "retries")==0)
			retrymax = atoi(entrydata);
		if(strcasecmp(entry, "cachemax")==0)
			cachemax = atoi(entrydata);
		if(strcasecmp(entry, "bandwidth")==0)
			bandwidthlimit = atoi(entrydata);
		if(strcasecmp(entry, "sort")==0)
			default_sort = atoi(entrydata);
		if(strcasecmp(entry, "locale")==0)
			handyftp_locale = atoi(entrydata);
		if(currentsite)
		{
			if(strcasecmp(entry, "hostname")==0)
				currentsite->hostname = strdup(entrydata);
			if(strcasecmp(entry, "url")==0)
				currentsite->url = strdup(entrydata);
			if(strcasecmp(entry, "username")==0)
				currentsite->username = strdup(entrydata);
			if(strcasecmp(entry, "password")==0)
				currentsite->password = strdup(entrydata);
			if(strcasecmp(entry, "port")==0)
				currentsite->port =  atoi(entrydata);
		}
		if(strcasecmp(entry, "hosttitle")==0)
		{
			if(!currentsite)
				currentsite = SSroot = (SavedSites *)malloc(sizeof(SavedSites));
			else
			{
				currentsite->next = (SavedSites *)malloc(sizeof(SavedSites));
				currentsite = currentsite->next;
			}
			currentsite->hosttitle = strdup(entrydata);
			currentsite->next = NULL;
		}

	}
	fclose(f);
}

/* Check the current directory for .typ files and load them into memory for use */
void loadsitetypes(void)
{
	char entry[256], entrydata[256];
	SiteTypes *currenttype = NULL;
	SiteIds *currentid = NULL;
	DIR *dir;
	char *typdir = dw_app_dir();
	struct dirent *ent;
	FILE *f;
	int z;

	if(!(dir = opendir(typdir)))
	{
		dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("Could not open site type files.", 155));
		return;
	}

	while((ent = readdir(dir)) != 0)
	{
		char *tmppath = malloc(strlen(ent->d_name)+strlen(typdir)+2);

		strcpy(tmppath, typdir);
		strcat(tmppath, DIRSEP);
		strcat(tmppath, ent->d_name);

		if(instring(ent->d_name, ".typ") == TRUE && (f = fopen(tmppath, FOPEN_READ_TEXT))!=NULL)
		{
			if(!currenttype)
				STroot = currenttype = (SiteTypes *)malloc(sizeof(SiteTypes));
			else
			{
				currenttype->next = (SiteTypes *)malloc(sizeof(SiteTypes));
				currenttype = currenttype->next;
			}
			currenttype->next = NULL;
			currenttype->siteids = NULL;
			currenttype->extended = 0;
			for(z=0;z<40;z++)
				currenttype->translation[z] = NULL;
			while(!feof(f))
			{
				handyftp_getline(f, entry, entrydata);
				if(strcasecmp(entry, "type")==0)
					currenttype->type = strdup(entrydata);

				if(strcasecmp(entry, "id") == 0)
				{
					if(!currenttype->siteids)
						currenttype->siteids = currentid = (SiteIds *)malloc(sizeof(SiteIds));
					else
					{
						currentid->next = (SiteIds *)malloc(sizeof(SiteIds));
						currentid = currentid->next;
					}
					currentid->next = NULL;
					currentid->id = strdup(entrydata);
				}
				if(strcasecmp(entry, "trans") == 0)
				{
					FILE *k;
					char buffer[256];
					int count = 0;

					if((k=fopen(entrydata, FOPEN_READ_TEXT)) != NULL)
					{
						while(!feof(k) && count < 20)
						{
							if(fgets(buffer, 255, k))
							{
								if(buffer[strlen(buffer)-1] == '\n')
									buffer[strlen(buffer)-1] = 0;
								currenttype->translation[count] = strdup(buffer);
							}
						}
						fclose(k);
					}
				}
				if(strcasecmp(entry, "column")==0)
				{
					if(strcasecmp(entrydata, "TRUE")==0)
						currenttype->column = TRUE;
					else
						currenttype->column = FALSE;
				}
				if(strcasecmp(entry, "filecol")==0)
					currenttype->filecol = atoi(entrydata);
				if(strcasecmp(entry, "sizecol")==0)
					currenttype->sizecol = atoi(entrydata);
				if(strcasecmp(entry, "monthcol")==0)
					currenttype->monthcol = atoi(entrydata);
				if(strcasecmp(entry, "daycol")==0)
					currenttype->daycol = atoi(entrydata);
				if(strcasecmp(entry, "yearcol")==0)
					currenttype->yearcol = atoi(entrydata);
				if(strcasecmp(entry, "timecol")==0)
					currenttype->timecol = atoi(entrydata);
				if(strcasecmp(entry, "linkcol")==0)
					currenttype->linkcol = abs(atoi(entrydata));
				if(strcasecmp(entry, "modecol")==0)
					currenttype->modecol = atoi(entrydata);
				if(strcasecmp(entry, "modeoffset")==0)
					currenttype->modeoffset = atoi(entrydata);
				if(strcasecmp(entry, "filestart")==0)
					currenttype->filestart = atoi(entrydata);
				if(strcasecmp(entry, "fileend")==0)
					currenttype->fileend = atoi(entrydata);
				if(strcasecmp(entry, "sizestart")==0)
					currenttype->sizestart = atoi(entrydata);
				if(strcasecmp(entry, "sizeend")==0)
					currenttype->sizeend = atoi(entrydata);
				if(strcasecmp(entry, "timestart")==0)
					currenttype->timestart = atoi(entrydata);
				if(strcasecmp(entry, "datestart")==0)
					currenttype->datestart = atoi(entrydata);
				if(strcasecmp(entry, "dir")==0)
					currenttype->dir = atoi(entrydata);
				if(strcasecmp(entry, "dirchar")==0)
					currenttype->dirchar = entrydata[0];
				if(strcasecmp(entry, "linkchar")==0)
					currenttype->linkchar = entrydata[0];
				if(strcasecmp(entry, "extended")==0)
				{
					if(strcasecmp(entrydata, "IPS") == 0)
						currenttype->extended = 1;
				}

			}
			fclose(f);
		}
		free(tmppath);
	}
	closedir(dir);
	if(!currenttype)
		dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("No Site Type files found! Running in local mode!", 156));
}

/* This gets called to recursively queue directories */
void recurseq(SiteTab *thissite)
{
	Queue *tmp = thissite->queue;
	int counter = 0;

	while(tmp)
	{
		if(tmp->type == DIRDIR)
		{
			int urllen = strlen(tmp->srcdirectory);
			char *tmpptr, *newdir = malloc(urllen + strlen(tmp->srcfilename) + 2);

			if(!thissite->queuing)
				thissite->queuing = strdup(thissite->url);

			strcpy(newdir, tmp->srcdirectory);
			if(newdir[urllen-1] != '/' && newdir[urllen-1] != '\\')
				strcat(newdir, "/");
			strcat(newdir, tmp->srcfilename);

			if(strcmp(thissite->hostname, "local") == 0)
				make_local(newdir, DIRSEP);
			else
				make_local(newdir, "/");

			dw_window_set_text(thissite->directory, newdir);
			tmpptr = thissite->url;
			thissite->url = newdir;

			free(tmpptr);

			removefromq(counter, thissite->page);
			drawq(thissite);
			sendthread(THRDHARDREFRESH, thissite->page);
			return;
		}
		tmp = tmp->next;
		counter++;
	}
	if(thissite->queuing)
	{
		char *tmpptr = thissite->url;

		thissite->url = thissite->queuing;
		free(tmpptr);
		thissite->queuing = NULL;
		dw_window_set_text(thissite->directory, thissite->url);
		sendthread(THRDREFRESH, thissite->page);
	}
}

/* Make a DOS style path into something a web browser can handle. */
void urlify(char *buf)
{
	int z, len = strlen(buf);

	for(z=0;z<len;z++)
	{
		if(buf[z] == '\\')
			buf[z] = '/';
		if(buf[z] == ':')
			buf[z] = '|';
	}
}

/* Make sure we are on a valid page and that the site index is correct */
int validatecurrentpage(void)
{
	int z, currentpageid = 0;

	if(pagescreated && hwndNBK)
	{
		currentpageid = dw_notebook_page_get(hwndNBK);
	}

	for(z=0;z<CONNECTION_LIMIT;z++)
	{
		if(alive[z])
		{
			if(site[z] && site[z]->pageid == currentpageid)
			{
				currentpage = z;
				break;
			}
		}
	}
	if(currentpage > -1 && currentpage < CONNECTION_LIMIT && alive[currentpage])
		return TRUE;

	return FALSE;
}

/* IDM_SAVEQ */
int DWSIGNAL saveq(HWND hwnd, void *data)
{
	if(validatecurrentpage())
	{
		char *filename;

		if((filename = dw_file_browse(locale_string("Save Queue to", 157), NULL, "lst", DW_FILE_SAVE)) != NULL)
		{
			savequeue(site[currentpage], filename);
			dw_free(filename);
		}
	}
	return FALSE;
}

/* IDM_LOADQ */
int DWSIGNAL loadq(HWND hwnd, void *data)
{
	if(validatecurrentpage())
	{
		char *filename;

		if((filename = dw_file_browse(locale_string("Load Queue from", 158), NULL, "lst", DW_FILE_OPEN)) != NULL)
		{
			loadqueue(site[currentpage], filename);
			dw_free(filename);
		}
	}
	return FALSE;
}

/* IDM_DEL2 */
int DWSIGNAL queuedel(HWND hwnd, void *data)
{
	int i;

	if(contexttext)
	{
		i = findqueueindex(contexttext, currentpage);
		removefromq(i, currentpage);
		unselectcontainer(QUEUE, currentpage);
		drawq(site[currentpage]);
	}
	return FALSE;
}

/* Context menus */
int DWSIGNAL contextmenus(HWND hwnd, void *data)
{
	if(validatecurrentpage())
	{
		if(site[currentpage]->connected == FALSE)
			dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("You must be connected.", 159));
		else
		{
			switch(DW_POINTER_TO_INT(data))
			{
			case IDP_SORTF:
				site[currentpage]->sort = SORT_FILE;
				sendthread(THRDHARDREFRESH, currentpage);
				break;
			case IDP_SORTD:
				site[currentpage]->sort = SORT_DATE;
				sendthread(THRDHARDREFRESH, currentpage);
				break;
			case IDP_SORTS:
				site[currentpage]->sort = SORT_SIZE;
				sendthread(THRDHARDREFRESH, currentpage);
				break;
			case IDP_SORTN:
				site[currentpage]->sort = SORT_NONE;
				sendthread(THRDHARDREFRESH, currentpage);
				break;
			case IDM_MKDIR:
				user_query(locale_string("Directory to create", 160), currentpage, contexttext, DW_SIGNAL_FUNC(mkdir_ok), DW_SIGNAL_FUNC(generic_cancel));
				break;
			case IDM_REN:
				{
					char tmpbuf[500];

					sprintf(tmpbuf, locale_string("Rename \"%s\" to", 161), contexttext);
					user_query(tmpbuf, currentpage, contexttext, DW_SIGNAL_FUNC(rename_ok), DW_SIGNAL_FUNC(generic_cancel));
				}
				break;
			case IDM_DEL:
				if(strcasecmp(site[currentpage]->hostname, "local") == 0)
				{
					char tmpbuf[2048];
					int filetype = findfiletype(contexttext, site[currentpage]);

					if(site[currentpage]->url[strlen(site[currentpage]->url)-1] == '\\')
						sprintf(tmpbuf, "%s%s", site[currentpage]->url, contexttext);
					else
						sprintf(tmpbuf, "%s%s%s", site[currentpage]->url, DIRSEP, contexttext);
					writeconsole(site[currentpage], locale_string("Deleting \"%s\"", 162), tmpbuf);

					if(filetype == DIRFILE)
						remove(tmpbuf);
					else if(filetype == DIRDIR)
						rmdir(tmpbuf);

					sendthread(THRDHARDREFRESH, currentpage);
				}
				else
					sendthread(THRDDEL, currentpage);
				break;
			case IDM_VIEW:
				if(strcasecmp(site[currentpage]->hostname, "local") == 0)
				{
					char tmpbuf[2048];
					int filetype = findfiletype(contexttext, site[currentpage]);

					if(filetype == DIRFILE)
					{
						char *execargs[3];

						if(site[currentpage]->url[strlen(site[currentpage]->url)-1] == '\\')
							sprintf(tmpbuf, "%s%s", site[currentpage]->url, contexttext);
						else
							sprintf(tmpbuf, "%s%s%s", site[currentpage]->url, DIRSEP, contexttext);

						execargs[0] = EDITOR;
						execargs[1] = tmpbuf;
						execargs[2] = NULL;

						dw_exec(EDITOR, EDMODE, execargs);
					}
				}
				else
					sendthread(THRDVIEW, currentpage);
				break;
			}
		}
	}
	return FALSE;
}

/* NEWTAB */
int DWSIGNAL newtab(HWND hwnd, void *data)
{
	if(validatecurrentpage())
		new_tab(NULL);
	return FALSE;
}

/* REMOVETAB */
int DWSIGNAL removetab(HWND hwnd, void *data)
{
	if(validatecurrentpage())
		remove_tab();
	return FALSE;
}

/* CONNECT */
int DWSIGNAL connecttab(HWND hwnd, void *data)
{
	if(validatecurrentpage())
	{
		char *tempbuffer;

		updatecurrentsettings(currentpage);

		/* Update current settings doesn't save the initial dir
		 * so we will have to do this seperately.
		 */

		tempbuffer = dw_window_get_text(site[currentpage]->directory);
		if(site[currentpage]->initialdir)
			free(site[currentpage]->initialdir);
		site[currentpage]->initialdir = strdup(tempbuffer);
		dw_free(tempbuffer);

		settabs();

		/* Tell the tab thread to actually connect */
		sendthread(THRDCONNECT, currentpage);
	}
	return FALSE;
}

/* DISCONNECT */
int DWSIGNAL disconnecttab(HWND hwnd, void *data)
{
	if(validatecurrentpage())
		sendthread(THRDDISCONNECT, currentpage);
	return FALSE;
}

/* PB_CHANGE */
int DWSIGNAL refreshtab(HWND hwnd, void *data)
{
	if(validatecurrentpage())
	{
		char *tempbuffer = dw_window_get_text(site[currentpage]->directory);

		/* Update the url from the entryfield in case someone edited it. */
		if(site[currentpage]->url)
			free(site[currentpage]->url);
		site[currentpage]->url = strdup(tempbuffer);

		dw_free(tempbuffer);
		sendthread(THRDHARDREFRESH, currentpage);
	}
	return FALSE;
}

/* FLUSHQ */
int DWSIGNAL flushtab(HWND hwnd, void *data)
{
	if(validatecurrentpage())
	{
		SiteTab *dest;

		if(!site[currentpage]->queue)
			dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_INFORMATION, locale_string("Nothing queued.", 163));
		else
		{
			dest = findsite(site[currentpage]->queue->site);
			if(site[currentpage] && site[currentpage]->connected == TRUE && dest && dest->connected == TRUE)
				sendthread(THRDFLUSH, currentpage);
			else
				dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("Source or destination not connected.", 164));
		}
	}
	return FALSE;
}

/* UNSAVETITLE */
int DWSIGNAL unsavetitletab(HWND hwnd, void *data)
{
	if(validatecurrentpage())
		unsavetitle();
	return FALSE;
}

/* SAVETITLE */
int DWSIGNAL savetitletab(HWND hwnd, void *data)
{
	if(validatecurrentpage())
	{
		char *tempbuffer;

		updatecurrentsettings(currentpage);

		/* Update current settings doesn't save the initial dir
		 * so we will have to do this seperately.
		 */

		tempbuffer = dw_window_get_text(site[currentpage]->directory);
		if(site[currentpage]->initialdir)
			free(site[currentpage]->initialdir);
		site[currentpage]->initialdir = strdup(tempbuffer);
		dw_free(tempbuffer);

		/* Finally do the actual save */
		savetitle();
	}
	return FALSE;
}

/* ADMIN */
int DWSIGNAL administratetab(HWND hwnd, void *data)
{
	if(validatecurrentpage())
		administrate();
	return FALSE;
}

/* ADDTOQ */
int DWSIGNAL addtoqtab(HWND hwnd, void *data)
{
	if(validatecurrentpage())
	{
		int i;
		char *tmpptr = dw_window_get_text(site[currentpage]->destsite);

		if(!tmpptr || strcmp(tmpptr, "") == 0)
		{
			dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_INFORMATION, locale_string("You need to select a site to queue the files to.", 165));
		}
		else
		{
			char *text = dw_container_query_start(site[currentpage]->ldir, DW_CRA_SELECTED);

			while(text != NULL)
			{
				if(strcmp(text, "..") != 0)
				{
					i = finddirindex(text, currentpage);
					addtoq(i, currentpage);
				}

				text = dw_container_query_next(site[currentpage]->ldir, DW_CRA_SELECTED);
			}

			unselectcontainer(LDIR, currentpage);
			drawq(site[currentpage]);
			recurseq(site[currentpage]);
		}
		if(tmpptr)
			dw_free(tmpptr);
	}
	return FALSE;
}

/* REMOVEFROMQ */
int DWSIGNAL removefromqtab(HWND hwnd, void *data)
{
	if(validatecurrentpage())
	{
		int i;
		char *text = dw_container_query_start(site[currentpage]->rqueue, DW_CRA_SELECTED);

		while(text != NULL)
		{
			i = findqueueindex(text, currentpage);
			removefromq(i, currentpage);

			text = dw_container_query_next(site[currentpage]->rqueue, DW_CRA_SELECTED);
		}

		unselectcontainer(QUEUE, currentpage);
		drawq(site[currentpage]);
	}
	return FALSE;
}

/* IDM_ABOUT */
int DWSIGNAL abouttab(HWND hwnd, void *data)
{
	if(validatecurrentpage())
		about();
	return FALSE;
}

/* IDM_ABOUT */
int DWSIGNAL generalhelp(HWND hwnd, void *data)
{
	char *path = dw_app_dir(), *templ = "file://%s/help.html";

	if(path && path[0])
	{
		char *url = malloc(strlen(path) + strlen(templ));

		urlify(path);

		sprintf(url, templ, path);

		if(dw_browse(url)<0)
			dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("Error starting web browser.", 166));

		free(url);
	}
	return FALSE;
}

int DWSIGNAL contentshelp(HWND hwnd, void *data)
{
	char *path = dw_app_dir(), *templ = "file://%s/contents.html";

	if(path && path[0])
	{
		char *url = malloc(strlen(path) + strlen(templ));

		urlify(path);

		sprintf(url, templ, path);

		if(dw_browse(url)<0)
			dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("Error starting web browser.", 166));

		free(url);
	}
	return FALSE;
}

/* IDM_SYSINFO */
int DWSIGNAL sysinfotab(HWND hwnd, void *data)
{
	if(validatecurrentpage())
		info_box();
	return FALSE;
}

/* IDM_HOME */
int DWSIGNAL handyftphome(HWND hwnd, void *data)
{
	if(dw_browse("http://handyftp.netlabs.org/")<0)
		dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, locale_string("Error starting web browser.", 166));
	return FALSE;
}

/* IDM_EXIT */
int DWSIGNAL exittab(HWND hwnd, void *data)
{
	int z;

	if(validatecurrentpage())
	{
		if(dw_messagebox("HandyFTP", DW_MB_YESNO | DW_MB_QUESTION, locale_string("Are you sure you want to exit HandyFTP?", 167)))
		{
			for(z=0;z<CONNECTION_LIMIT;z++)
				if(alive[z] == TRUE)
					sendthread(THRDEXIT, z);
			msleep(500);
			/* Destroy the main window */
			dw_window_destroy(hwndFrame);
			dw_exit(0);
		}
	}
	return FALSE;
}

/* IDM_PREFERENCES */
int DWSIGNAL preferencestab(HWND hwnd, void *data)
{
	if(validatecurrentpage())
		preferences();
	return FALSE;
}

/* IDM_AUTOCONNECT */
int DWSIGNAL autoconnecttab(HWND hwnd, void *data)
{
	if(validatecurrentpage())
	{
		if(autoconnect == TRUE)
		{
			autoconnect = FALSE;
			dw_menu_item_set_check(menubar, IDM_AUTOCONNECT, FALSE);
		}
		else
		{
			autoconnect = TRUE;
			dw_menu_item_set_check(menubar, IDM_AUTOCONNECT, TRUE);
		}
	}
	return FALSE;
}

/* IDM_NOFAIL */
int DWSIGNAL nofailtab(HWND hwnd, void *data)
{
	if(validatecurrentpage())
	{
		if(nofail == TRUE)
		{
			nofail = FALSE;
			dw_menu_item_set_check(menubar, IDM_NOFAIL, FALSE);
		}
		else
		{
			nofail = TRUE;
			dw_menu_item_set_check(menubar, IDM_NOFAIL, TRUE);
		}
	}
	return FALSE;
}

/* Delete event */
int DWSIGNAL deleteevent(HWND hwnd, void *data)
{
	int z;

	if(validatecurrentpage())
	{
		if(dw_messagebox("HandyFTP", DW_MB_YESNO | DW_MB_QUESTION, locale_string("Are you sure you want to exit HandyFTP?", 167)))
		{
			for(z=0;z<CONNECTION_LIMIT;z++)
				if(alive[z] == TRUE)
					sendthread(THRDEXIT, z);
			msleep(500);
			dw_window_destroy(hwndFrame);
			dw_exit(0);
		}
	}
	return TRUE;
}

/* Set all the menu items on the sort menu */
void validate_sort_menu(HMENUI menu, int sort)
{
	dw_menu_item_set_check(menu, IDM_SORTF, sort == SORT_FILE ? TRUE : FALSE);
	dw_menu_item_set_check(menu, IDM_SORTS, sort == SORT_SIZE ? TRUE : FALSE);
	dw_menu_item_set_check(menu, IDM_SORTD, sort == SORT_DATE ? TRUE : FALSE);
	dw_menu_item_set_check(menu, IDM_SORTN, sort == SORT_NONE ? TRUE : FALSE);
}

/* Callback to handle right clicks on the container */
int DWSIGNAL containercontextmenu(HWND hwnd, char *text, int x, int y, void *data)
{
	HMENUI hwndMenu, hwndSubMenu;
	HWND menuitem;

	if(validatecurrentpage())
	{
		if(hwnd == site[currentpage]->ldir)
		{
			hwndMenu = dw_menu_new(0L);

			hwndSubMenu = dw_menu_new(0L);
			menuitem = dw_menu_append_item(hwndSubMenu, locale_string("File", 15), IDP_SORTF, 0L, TRUE, TRUE, DW_NOMENU);
			dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(contextmenus), (void *)IDP_SORTF);
			menuitem = dw_menu_append_item(hwndSubMenu, locale_string("Date", 16), IDP_SORTD, 0L, TRUE, TRUE, DW_NOMENU);
			dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(contextmenus), (void *)IDP_SORTD);
			menuitem = dw_menu_append_item(hwndSubMenu, locale_string("Size", 17), IDP_SORTS, 0L, TRUE, TRUE, DW_NOMENU);
			dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(contextmenus), (void *)IDP_SORTS);
			menuitem = dw_menu_append_item(hwndSubMenu, locale_string("None", 18), IDP_SORTN, 0L, TRUE, TRUE, DW_NOMENU);
			dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(contextmenus), (void *)IDP_SORTN);

			menuitem = dw_menu_append_item(hwndMenu, locale_string("Sort", 24), IDP_SORT, 0L, TRUE, FALSE, hwndSubMenu);

			menuitem = dw_menu_append_item(hwndMenu, locale_string("Make Directory", 25), IDM_MKDIR, 0L, TRUE, FALSE, DW_NOMENU);
			dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(contextmenus), (void *)IDM_MKDIR);

			if(text)
			{
				dw_menu_append_item(hwndMenu, "", 0L, 0L, TRUE, FALSE, DW_NOMENU);
				menuitem = dw_menu_append_item(hwndMenu, locale_string("Rename Entry", 26), IDM_REN, 0L, TRUE, FALSE, DW_NOMENU);
				dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(contextmenus), (void *)IDM_REN);

				menuitem = dw_menu_append_item(hwndMenu, locale_string("Delete Entry", 27), IDM_DEL, 0L, TRUE, FALSE, DW_NOMENU);
				dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(contextmenus), (void *)IDM_DEL);

				dw_menu_append_item(hwndMenu, "", 0L, 0L, TRUE, FALSE, DW_NOMENU);
				menuitem = dw_menu_append_item(hwndMenu, locale_string("View File", 28), IDM_VIEW, 0L, TRUE, FALSE, DW_NOMENU);
				dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(contextmenus), (void *)IDM_VIEW);
			}
		}
		else
		{
			hwndMenu = dw_menu_new(0L);

			menuitem = dw_menu_append_item(hwndMenu, locale_string("Save Queue", 29), IDM_SAVEQ, 0L, TRUE, FALSE, DW_NOMENU);
			dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(saveq), (void *)IDM_DEL2);
			menuitem = dw_menu_append_item(hwndMenu, locale_string("Load Queue", 30), IDM_LOADQ, 0L, TRUE, FALSE, DW_NOMENU);
			dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(loadq), (void *)IDM_DEL2);

			if(text)
			{
				dw_menu_append_item(hwndMenu, "", 0L, 0L, TRUE, FALSE, DW_NOMENU);

				menuitem = dw_menu_append_item(hwndMenu, locale_string("Remove File", 31), IDM_DEL2, 0L, TRUE, FALSE, DW_NOMENU);
				dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(queuedel), (void *)IDM_DEL2);
			}
		}

		contexttext = text;

		dw_menu_item_set_check(hwndMenu, IDP_SORTF, abs(site[currentpage]->sort) == SORT_FILE ? TRUE : FALSE);
		dw_menu_item_set_check(hwndMenu, IDP_SORTS, abs(site[currentpage]->sort) == SORT_SIZE ? TRUE : FALSE);
		dw_menu_item_set_check(hwndMenu, IDP_SORTD, abs(site[currentpage]->sort) == SORT_DATE ? TRUE : FALSE);
		dw_menu_item_set_check(hwndMenu, IDP_SORTN, abs(site[currentpage]->sort) == SORT_NONE ? TRUE : FALSE);
		dw_menu_popup(&hwndMenu, hwndFrame, x, y);
	}
	return FALSE;  
}

/* Callback to handle user selection in the site combobox */
int DWSIGNAL listboxselect(HWND hwnd, int item, void *data)
{
	if(validatecurrentpage())
	{
		loadsaved(currentpage, dw_listbox_selected(site[currentpage]->host_title));
		if(autoconnect)
		{
			if(site[currentpage]->connected == TRUE)
				sendthread(THRDDISCONNECT, currentpage);
			sendthread(THRDCONNECT, currentpage);
		}
#if 0
		dw_container_clear(site[currentpage]->ldir, TRUE);
#endif
	}
	return FALSE;
}

/* Handle changes in selection status in the container */
int DWSIGNAL containerselect(HWND hwnd, char *text, void *data)
{
	int i;
	Directory *dir;
	char buffer[4096];

	if(text && validatecurrentpage())
	{
		if(site[currentpage]->status != STATUSIDLE)
			dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_ERROR, "Current site is busy.");
		else
		{
			i = finddirindex(text, currentpage);

			dir = finddir(i, currentpage);

			if(dir->type == DIRDIR || dir->type == DIRLINK)
			{
				if(!site[currentpage]->url)
					site[currentpage]->url = strdup("/");

				strcpy(buffer, site[currentpage]->url);
				if(strlen(buffer) > 0 && (buffer[strlen(buffer)-1] == '\\' || buffer[strlen(buffer)-1] == '/'))
					buffer[strlen(buffer)-1] = 0;
				if(strcmp(dir->entry, ".") == 0)
				{
					/* Do nothing if . is selected */
				}
				else if(strcmp(dir->entry, "..") == 0)
				{
					for(i=strlen(buffer);i>-1;i--)
					{
						if(buffer[i] == '/' || buffer[i] == '\\')
						{
							char *tmp = site[currentpage]->url;

							buffer[i] = 0;
							if((strlen(buffer) == 2 && buffer[1] == ':'))
								strcat(buffer, "\\");

							if(strlen(buffer) == 0)
								strcat(buffer, "/");

							site[currentpage]->url = strdup(buffer);

							if(tmp)
								free(tmp);

							sendthread(THRDREFRESH, currentpage);
							break;
						}
					}
				}
				else if(strcasecmp(site[currentpage]->hostname, "local") == 0 && strlen(dir->entry) > 7 && strncmp(dir->entry, locale_string("Drive ", 168), strlen(locale_string("Drive ", 168))) == 0)
				{
					char newdrivebuf[4];
					strncpy(newdrivebuf, &dir->entry[strlen(locale_string("Drive ", 168))], 2);
					newdrivebuf[2] = 0;
					strcat(newdrivebuf, DIRSEP);
					if(site[currentpage]->url)
						free(site[currentpage]->url);
					site[currentpage]->url = strdup(newdrivebuf);
					sendthread(THRDREFRESH, currentpage);
				}
				else
				{
					char *tmpptr = dir->entry;

					if(strlen(buffer) > 1 && buffer[1] == ':')
						strcat(buffer, DIRSEP);
					else
						strcat(buffer, "/");

					/* If we have a link find out what it's pointing to */
					if(dir->type == DIRLINK)
					{
						tmpptr = strstr(dir->entry, "-> ");
						if(!tmpptr)
							tmpptr = dir->entry;
						else
							tmpptr+=3;
					}

					if(*tmpptr == '/' || *tmpptr == '\\')
						strcpy(buffer, tmpptr);
					else
						strcat(buffer, tmpptr);

					/* We save the pointer so site[]->url is always a valid pointer */
					tmpptr = site[currentpage]->url;

					site[currentpage]->url = strdup(buffer);
					sendthread(THRDREFRESH, currentpage);

					if(tmpptr)
						free(tmpptr);
				}
			}
			else
			{
				char *tmpptr = dw_window_get_text(site[currentpage]->destsite);

				if(!tmpptr || strcmp(tmpptr, "") == 0)
				{
					dw_messagebox("HandyFTP", DW_MB_OK | DW_MB_INFORMATION, locale_string("You need to select a site to queue the files to.", 169));
				}
				else
				{
					addtoq(i, currentpage);
					unselectcontainer(QUEUE, currentpage);
					drawq(site[currentpage]);
					recurseq(site[currentpage]);
				}
				dw_free(tmpptr);
			}
		}
	}
	return FALSE;
}

void DWSIGNAL sortmenu(HWND hwnd, void *data)
{
	switch(DW_POINTER_TO_INT(data))
	{
	case IDM_SORTF:
		default_sort = SORT_FILE;
		break;
	case IDM_SORTD:
		default_sort = SORT_DATE;
		break;
	case IDM_SORTS:
		default_sort = SORT_SIZE;
		break;
	case IDM_SORTN:
		default_sort = SORT_NONE;
		break;
	}
	validate_sort_menu(sort_menu, default_sort);
}

/* Create the main window with a notebook and a menu. */
void handyftp_init(void)
{
	HWND mainbox, toolbox, tempbutton, menuitem;
	HMENUI menu;
	ULONG flStyle = DW_FCF_SYSMENU | DW_FCF_TITLEBAR | DW_FCF_SIZEBORDER | DW_FCF_MINMAX |
		DW_FCF_SHELLPOSITION | DW_FCF_TASKLIST | DW_FCF_DLGBORDER;
	int z = 0, m = 0;
	char msgbuf[1025];
    
	strncpy(msgbuf, dw_app_dir(), 1024);
	strcat(msgbuf, "/handyftp.msg");

	locale_init(msgbuf, handyftp_locale);

	hwndFrame = dw_window_new(HWND_DESKTOP, "HandyFTP", flStyle);

	dw_window_set_icon(hwndFrame, DW_RESOURCE(MAIN_FRAME));
    
	mainbox = dw_box_new(BOXVERT, 0);

	dw_box_pack_start(hwndFrame, mainbox, 0, 0, TRUE, TRUE, 0);

	toolbox = dw_box_new(BOXHORZ, 1);

	dw_box_pack_start(mainbox, toolbox, 0, 0, TRUE, FALSE, 0);

	/* Create the toolbar */
	while(mainItems[z])
	{
		if(mainItems[z] != -1)
		{
			tempbutton = dw_bitmapbutton_new(locale_string(mainHelpItems[z], 170 + m), mainItems[z]);
			dw_window_set_style(tempbutton, DW_BS_NOBORDER, DW_BS_NOBORDER);

			if(mainItems[z] == PB_CHANGE)
				refreshbutton = tempbutton;

			m++;

			dw_signal_connect(tempbutton, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(mainFunctions[z]), (void *)mainItems[z]);
			dw_box_pack_start(toolbox, tempbutton, 30, 30, FALSE, FALSE, 0);
		}
		else
			dw_box_pack_start(toolbox, 0, 5, 30, FALSE, FALSE, 0);
		z++;
	}
	dw_box_pack_start(toolbox, 0, 3, 30, TRUE, FALSE, 0);

	hwndNBK = dw_notebook_new(1050L, TRUE);

	dw_box_pack_start(mainbox, hwndNBK, 100, 100, TRUE, TRUE, 0);

	menubar = dw_menubar_new(hwndFrame);

	menu = dw_menu_new(0L);

	menuitem = dw_menu_append_item(menu, locale_string("~New Connection Tab", 3), IDM_NEW, 0L, TRUE, FALSE, DW_NOMENU);
	dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(newtab), NULL);

	menuitem = dw_menu_append_item(menu, locale_string("~Remove Connection Tab", 4), IDM_REMOVE, 0L, TRUE, FALSE, DW_NOMENU);
	dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(removetab), NULL);

	dw_menu_append_item(menu, "", 0L, 0L, TRUE, FALSE, DW_NOMENU);
	menuitem = dw_menu_append_item(menu, locale_string("~Connect", 5), IDM_CONNECT, 0L, TRUE, FALSE, DW_NOMENU);
	dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(connecttab), NULL);

	menuitem = dw_menu_append_item(menu, locale_string("~Disconnect", 6), IDM_DISCONNECT, 0L, TRUE, FALSE, DW_NOMENU);
	dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(disconnecttab), NULL);

	dw_menu_append_item(menu, "", 0L, 0L, TRUE, FALSE, DW_NOMENU);
	menuitem = dw_menu_append_item(menu, locale_string("~Save Host", 7), IDM_SAVE, 0L, TRUE, FALSE, DW_NOMENU);
	dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(savetitletab), NULL);

	menuitem = dw_menu_append_item(menu, locale_string("~Unsave Host", 8), IDM_UNSAVE, 0L, TRUE, FALSE, DW_NOMENU);
	dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(unsavetitletab), NULL);

	dw_menu_append_item(menu, "", 0L, 0L, TRUE, FALSE, DW_NOMENU);
	menuitem = dw_menu_append_item(menu, locale_string("~Exit", 9), IDM_EXIT, 0L, TRUE, FALSE, DW_NOMENU);
	dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(exittab), NULL);


	dw_menu_append_item(menubar, locale_string("~File", 0), IDM_FILE, 0L, TRUE, FALSE, menu);

	menu = dw_menu_new(0L);

	menuitem = dw_menu_append_item(menu, locale_string("~Preferences", 10), IDM_PREFERENCES, 0L, TRUE, FALSE, DW_NOMENU);
	dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(preferencestab), NULL);

	menuitem = dw_menu_append_item(menu, locale_string("Administrate ~Site", 11), IDM_ADMIN, 0L, TRUE, FALSE, DW_NOMENU);
	dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(administratetab), NULL);

	menuitem = dw_menu_append_item(menu, locale_string("~Autoconnect", 12), IDM_AUTOCONNECT, 0L, TRUE, TRUE, DW_NOMENU);
	dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(autoconnecttab), NULL);

	menuitem = dw_menu_append_item(menu, locale_string("~No Fail", 13), IDM_NOFAIL, 0L, TRUE, TRUE, DW_NOMENU);
	dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(nofailtab), NULL);

	sort_menu = dw_menu_new(0L);

	menuitem = dw_menu_append_item(sort_menu, locale_string("File", 15), IDM_SORTF, 0L, TRUE, TRUE, DW_NOMENU);
	dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(sortmenu), (void *)IDM_SORTF);
	menuitem = dw_menu_append_item(sort_menu, locale_string("Date", 16), IDM_SORTD, 0L, TRUE, TRUE, DW_NOMENU);
	dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(sortmenu), (void *)IDM_SORTD);
	menuitem = dw_menu_append_item(sort_menu, locale_string("Size", 17), IDM_SORTS, 0L, TRUE, TRUE, DW_NOMENU);
	dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(sortmenu), (void *)IDM_SORTS);
	menuitem = dw_menu_append_item(sort_menu, locale_string("None", 18), IDM_SORTN, 0L, TRUE, TRUE, DW_NOMENU);
	dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(sortmenu), (void *)IDM_SORTN);

	menuitem = dw_menu_append_item(menu, locale_string("~Default Sort", 14), IDM_SORT, 0L, TRUE, FALSE, sort_menu);

	dw_menu_append_item(menubar, locale_string("~Options", 1), IDM_OPTIONS, 0L, TRUE, FALSE, menu);

	menu = dw_menu_new(0L);

	menuitem = dw_menu_append_item(menu, locale_string("~Help Index", 19), IDM_HELPINDEX, 0L, TRUE, FALSE, DW_NOMENU);
	dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(contentshelp), NULL);
	menuitem = dw_menu_append_item(menu, locale_string("~General Help", 20), IDM_MYHELP, 0L, TRUE, FALSE, DW_NOMENU);
	dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(generalhelp), NULL);

	dw_menu_append_item(menu, "", 0L, 0L, TRUE, FALSE, DW_NOMENU);
	menuitem = dw_menu_append_item(menu, locale_string("~System Info", 21), IDM_SYSINFO, 0L, TRUE, FALSE, DW_NOMENU);
	dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(sysinfotab), NULL);
	menuitem = dw_menu_append_item(menu, locale_string("Handy~FTP Home", 22), IDM_HOME, 0L, TRUE, FALSE, DW_NOMENU);
	dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(handyftphome), NULL);

	dw_menu_append_item(menu, "", 0L, 0L, TRUE, FALSE, DW_NOMENU);
	menuitem = dw_menu_append_item(menu, locale_string("~About", 23), IDM_ABOUT, 0L, TRUE, FALSE, DW_NOMENU);
	dw_signal_connect(menuitem, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(abouttab), NULL);

	menuitem = dw_menu_append_item(menubar, locale_string("~Help", 2), IDM_HELP, 0L, TRUE, FALSE, menu);

	validate_sort_menu(sort_menu, default_sort);
}

/* The main entry point.  Notice we don't use WinMain() on Windows */
int main(int argc, char *argv[])
{
	int cx, cy;

	dw_init(TRUE, argc, argv);

#ifdef __MAC__
	dw_font_set_default("10.Geneva");
#endif

	signal(SIGSEGV, handyftp_crash);

	sockinit();

	loadconfig();
	loadsitetypes();

	srand(time(NULL));

	cx = dw_screen_width();
	cy = dw_screen_height();

	memset((void *)&alive, 0, sizeof(int) * CONNECTION_LIMIT);
	memset(site, 0, sizeof(SiteTab *) * CONNECTION_LIMIT);

	mutex = dw_mutex_new();
	hev = dw_event_new();
	dw_event_reset(hev);

	handyftp_init();

	fileicon = dw_icon_load(0,FILEICON);
	foldericon = dw_icon_load(0,FOLDERICON);
	linkicon = dw_icon_load(0,LINKICON);

	if(hwndFrame)
	{

		dw_window_set_icon(hwndFrame, DW_RESOURCE(MAIN_FRAME));

		dw_signal_connect(hwndFrame, DW_SIGNAL_DELETE, DW_SIGNAL_FUNC(deleteevent), NULL);

		new_tab(NULL);
		pagescreated = 1;

		/* Tell it to create the tabs */
		if(openall)
		{
			SavedSites *tmp = SSroot;
			int count = 0;

			while(tmp)
			{
				if(tmp != SSroot)
					dw_window_function(hwndFrame, (void *)new_tab, NULL);

				loadsaved(count, count);
				count++;
				tmp = tmp->next;

				if(count >= CONNECTION_LIMIT)
					break;
			}
		}
		settabs();

		dw_window_set_pos_size(hwndFrame, cx / 8,
							   cy / 8,
							   (cx / 4) * 3,
							   (cy / 4) * 3);

		dw_window_show(hwndFrame);

		dw_event_post(hev);

		dw_main();

		dw_window_destroy(hwndFrame);
	}

	sockshutdown();

	dw_icon_free(fileicon);
	dw_icon_free(foldericon);
	dw_icon_free(linkicon);

	dw_mutex_close(mutex);
	dw_event_close(&hev);

	return 0;
}
