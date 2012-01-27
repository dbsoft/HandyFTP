/* $Id: site.h,v 1.21 2002/10/08 04:02:29 bsmith Exp $ */

typedef struct _directory {
	char *entry;
	struct _directory *next;
	unsigned long long size;
	char sizebuf[255];
	CTIME time;
	CDATE date;
	int type;
} Directory;

typedef struct _queue {
	char *srcdirectory;
	char *srcfilename;
	/*SiteTab *dest;*/
	char *site;
	char *destdirectory;
	unsigned long long size;
	char sizebuf[255];
	CTIME time;
	CDATE date;
	int type;
	struct _queue *next;
} Queue;

typedef struct _dircache {
	char *url;
	char *site;
	char *user;
	Directory *dir;
	struct _dircache *next;
} DirCache;


typedef struct _sitetab {
	HWND hwnd, ldir, rqueue, host_title, destsite, cstatus, server, percent,
    host_name, user_name, pass_word, directory, port_num, cmorestatus;
	/* We'll let each site know what page they are */
	int page, refcount;
	int controlfd, datafd, pipefd[2], tpipefd[2];
	unsigned long long sent;
	unsigned long pageid, ip, serverip;
	unsigned long fxpserver, fxpport;
	int fxpready;
	/* Used for recursive queuing */
	char *queuing;
	char *url;
	int connected, status, cachecount, sort;
	DirCache *cache;
	Directory *dir;
	Queue *queue, *currentqueue;
	time_t commandstart;
	char thrdcommand[500];
	/* Replaces the Window fields */
	char *hosttitle, *hostname, *username, *password, *initialdir;
	int port;
} SiteTab;

typedef struct _savedsites {
	char *hosttitle;
	char *hostname;
	int port;
	char *url;
	char *username;
	char *password;
	struct _savedsites *next;
} SavedSites;

typedef struct _siteids {
	char *id;
	struct _siteids *next;
} SiteIds;

typedef struct _sitetypes {
	char *type;
	SiteIds *siteids;
	int column;
	/* Columnized FALSE */
	int filestart;
	int fileend;
	int sizestart;
	int sizeend;
	int datestart;
	int timestart;
	int dir;
	/* Columnized TRUE */
	int filecol;
	int sizecol;
	int daycol;
	int monthcol;
	int yearcol;
	int timecol;
	int linkcol;
	int modecol;
	int modeoffset;
	/* Both */
	char dirchar;
	char linkchar;
	char *translation[40];
    int extended;
	struct _sitetypes *next;
} SiteTypes;

typedef struct _userentry {
	HWND window, entryfield;
	int page;
	char *filename;
	void *data;
	HWND *busy;
} UserEntry;

typedef struct _adminentry {
	char *user;
	char *addr;
	char *act;
	char *sock;
	int idle;
	struct _adminentry *next;
} AdminEntry;

#define DIRFILE         0
#define DIRDIR          1
#define DIRLINK         2

#define THRDEXIT        0
#define THRDCONNECT     1
#define THRDDISCONNECT  2
#define THRDFLUSH       3
#define THRDREFRESH     4
#define THRDRECEIVE     5
#define THRDABORT       6
#define THRDDONE        7
#define THRDFXPSTART    8
#define THRDFXP         9
#define THRDDEL         10
#define THRDREN         11
#define THRDMKDIR       12
#define THRDVIEW        13
#define THRDHARDREFRESH 14
#define THRDRAW         15
#define THRDFXPRECEIVE  16

#define MAINUPDATELISTS 0
#define MAINPRECENT     1
#define MAINDONE        2
#define MAINCONSOLE     3

#define SORT_NONE 0
#define SORT_FILE 1
#define SORT_DATE 2
#define SORT_SIZE 3

#define STATUSIDLE              0
#define STATUSFLUSHING          1
#define STATUSCONNECTING        2
#define STATUSSENDING           3
#define STATUSRECEIVING         4
#define STATUSIDENT             5
#define STATUSLOGIN             6
#define STATUSPASSWORD          7
#define STATUSCWD               8
#define STATUSLS                9
#define STATUSLSPORT            10
#define STATUSDIR               11
#define STATUSDIRACCEPT         12
#define STATUSDATA              13
#define STATUSDATAACCEPT        14
#define STATUSRETR              15
#define STATUSSTORE             16
#define STATUSSTOREWAIT         17
#define STATUSTRANSMIT          18
#define STATUSFXPRETR           19
#define STATUSFXPSTOR           20
#define STATUSFXPTRANSFER       21
#define STATUSFXPWAIT           22
#define STATUSNEXT              23
#define STATUSRETRY             24
#define STATUSWAITIDLE          25
#define STATUSPASVRETR          26
#define STATUSPASVLS            27
