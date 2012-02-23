#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dw.h>
#include <dwcompat.h>
#include "datetime.h"

int instring(char *in, char *this)
{
	int thislen, inlen, z;

	thislen = strlen(this);
	inlen = strlen(in);

	if(inlen < thislen)
		return FALSE;

	for(z=0;z<(inlen-thislen)+1;z++)
	{
		if(strncasecmp(in+z, this, thislen) == 0)
			return TRUE;
	}
	return FALSE;
}

int fourdigits(char *in)
{
	int z;

	for(z=1;z<DTBUFSIZE-4;z++)
	{
		if(in[z-1] != '-' &&
		   isdigit(in[z]) &&
		   isdigit(in[z+1]) &&
		   isdigit(in[z+2]) &&
		   isdigit(in[z+3]))
			return atoi(&in[z]);
	}
	return 0;
}

/* Return the # coresponding to the day in the file date */
int findday(char *in)
{
	char tmp[DTBUFSIZE + 1];
	int z;

	memset(tmp, 0, DTBUFSIZE + 1);
	strncpy(tmp, in, DTBUFSIZE);

	for(z=0;z<DTBUFSIZE-2;z++)
	{
		if(tmp[z] == '-' || tmp[z] == '/')
		{
			if(tmp[z+2] == '-' || tmp[z+2] == '/')
				return atoi(&tmp[z+1]);
		}
	}
	for(z=0;z<DTBUFSIZE;z++)
	{
		if(isdigit(tmp[z]))
			return atoi(&tmp[z]);
	}
	return 0;
}

/* Return the # coresponding to the month in the file date */
int findmonth(char *in)
{
	char tmp[DTBUFSIZE + 1];
	int z;

	memset(tmp, 0, DTBUFSIZE + 1);
	strncpy(tmp, in, DTBUFSIZE);

	if(instring(tmp, "jan"))
		return 1;
	if(instring(tmp, "feb"))
		return 2;
	if(instring(tmp, "mar"))
		return 3;
	if(instring(tmp, "apr"))
		return 4;
	if(instring(tmp, "may"))
		return 5;
	if(instring(tmp, "jun"))
		return 6;
	if(instring(tmp, "jul"))
		return 7;
	if(instring(tmp, "aug"))
		return 8;
	if(instring(tmp, "sep"))
		return 9;
	if(instring(tmp, "oct"))
		return 10;
	if(instring(tmp, "nov"))
		return 11;
	if(instring(tmp, "dec"))
		return 12;

	for(z=2;z<DTBUFSIZE-3;z++)
	{
		if(tmp[z] == '-' || tmp[z] == '/')
		{
			if(tmp[z+3] == tmp[z])
				return atoi(&tmp[z-2]);
		}
	}
	return atoi(tmp);
}

/* Return the # coresponding to the year in the file date */
int findyear(char *in)
{
	char tmp[DTBUFSIZE + 1];
	time_t t;
	int z=0, lastdash=0, y;

	memset(tmp, 0, DTBUFSIZE + 1);
	strncpy(tmp, in, DTBUFSIZE);

	y = fourdigits(tmp);

	if(!instring(tmp, "-") && !instring(tmp, "/") && !y)
	{
		t = time(NULL);
		return localtime(&t)->tm_year + 1900;
	}
	else if(!y)
	{
		for(z=0;z<DTBUFSIZE;z++)
		{
			if(tmp[z] == '-' || tmp[z] == '/')
				lastdash = z;
		}
		y = atoi(&tmp[lastdash+1]);
	}
	if(y<80)
		y+=80;
	if(y<1900)
		y+=1900;

	return y;
}

/* Return the # coresponding to the hour in the file time */
int findhour(char *in)
{
	char tmp[DTBUFSIZE + 1];
	int h=0, z;

	memset(tmp, 0, DTBUFSIZE + 1);
	strncpy(tmp, in, DTBUFSIZE);

	if(!instring(tmp, ":"))
		return 0;

	for(z=2;z<DTBUFSIZE;z++)
	{
		if(tmp[z] == ':')
		{
			h = atoi(&tmp[z-2]);
			break;
		}
	}

	if(instring(&tmp[z], "p"))
		h+=12;

	return h;
}

/* Return the # coresponding to the minute in the file time */
int findmin(char *in)
{
	char tmp[DTBUFSIZE + 1];
	int z;

	memset(tmp, 0, DTBUFSIZE + 1);
	strncpy(tmp, in, DTBUFSIZE);

	if(!instring(tmp, ":"))
		return 0;

	for(z=0;z<DTBUFSIZE;z++)
	{
		if(tmp[z] == ':')
			return atoi(&tmp[z+1]);
	}

	return 0;
}

/* Return the # coresponding to the second in the file time */
int findsec(char *in)
{
	char tmp[DTBUFSIZE + 1];
	int z, lastcolon=0;

	memset(tmp, 0, DTBUFSIZE + 1);
	strncpy(tmp, in, DTBUFSIZE);

	if(!instring(tmp, ":"))
		return 0;

	for(z=0;z<DTBUFSIZE;z++)
	{
		if(tmp[z] == ':')
			lastcolon=z;
	}

	return atoi(&tmp[lastcolon+1]);
}

