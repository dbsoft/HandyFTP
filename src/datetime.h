#ifndef __DATETIME_H__
#define __DATETIME_H__

#define DTBUFSIZE 10

int instring(char *in, char *this);
int findday(char *in);
int findmonth(char *in);
int findyear(char *in);
int findhour(char *in);
int findmin(char *in);
int findsec(char *in);

#endif
