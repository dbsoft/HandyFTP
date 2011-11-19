HandyFTP 1.0 beta 3

These clients have not been thoroughly tested, they are under development, I am
dropping the latest binaries and source code every couple of days.  They
are not claimed to work as intended, they are here so I can get feedback during 
development and debugging.  The source code is available also so if something
is not working you are welcome to fix it, or if you want something to work 
differently feel free to change it and submit the changes to me.  Feel 
free to do with this as you please as long as credit is given where deserved.

Unfinished:
Proxy Support

Changes:

Gave up on tracking changes in this file, there are just way
too many.  If you really want to know about all the changes
connect to the Dynamic Windows SVN at:

http://svn.netlabs.org/dwindows/timeline

and the HandyFTP Mercurial repository at:

http://hg.dbsoft.org/HandyFTP/

Changes since 1.0b2:

Boatloads considering it has been 8 years since the last release.

Rewrote the splitbar code.  It now works on all platforms and is
accurate, unlike before. :)

Renamed the "Reverse FXP" option "Passive FTP."  Which puts the remote
server into passive mode, causing HandyFTP to create active connections.

Passive FTP option now applies to FXP, Download *and* Upload.

Translations into German and Danish.

Added sorting of the directory list.

Added quit confirmation dialog.

Heavily optimized the redraw code on OS/2 and Windows.

Added code to rename tabs so they have unique names, so you can't get
conflicting tab names.  If you have a duplicate tab such as "Local"
a new local tab will be named "Local #2" and so on.

Finally readded the open all code and got it working.

End:

Question, comments, fixes, suggestions welcome: brian@dbsoft.org

Thanks! 

Brian Smith
