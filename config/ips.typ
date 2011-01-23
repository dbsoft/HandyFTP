#
# IPS entry, basically just UNIX.
#
# Site type
TYPE=IPS
# String which will identify host type (Can be more than one)
ID=InetPowerServer
# Character which idicates this
DIRCHAR=d
# Character indicates a (symbolic) link
LINKCHAR=l
#Columnized mode
COLUMN=TRUE
#
# If columnized mode is TRUE these entries apply
#
# Column in which the filename is.
FILECOL=-9
# Column in which the file size is.
SIZECOL=5
# Column in which the file month is.
MONTHCOL=6
# Column in which the file day is.
DAYCOL=7
# Column in which the file year is.
YEARCOL=8
# Column in which the file time is.
TIMECOL=8
# Column in which the file mode is.
MODECOL=1
# Character offset in the mode column.
MODEOFFSET=1
#
# If columnized mode is FALSE these entries apply
#
# Directory entry start character 
FILESTART=55
# Directory end character (or -1 for end of line)
FILEEND=-1
# Size start
SIZESTART=34
# Size end
SIZEEND=41
# Character that determines if entry is a directory
DIR=1
# Date start 
DATESTART=42
# Time start
TIMESTART=49
#
# Translation table (optional)
#
#TRANS=unix.tbl
#
# Extenteded command format
#
EXTENDED=IPS
