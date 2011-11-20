#!/bin/sh
PLATFORM=`uname -s`
APPNAME=$1
BINNAME=$2

if [ $PLATFORM = "Darwin" ]
then
    rm -rf "$APPNAME.app"
    mkdir -p "$APPNAME.app/Contents/MacOS"
    mkdir -p "$APPNAME.app/Contents/Resources"

    cp -f mac/Info.plist "$APPNAME.app/Contents"
    cp -f mac/PkgInfo "$APPNAME.app/Contents"
    cp -f mac/logo.png "$APPNAME.app/Contents/Resources/1300.png"
    cp -f mac/handyftp.png "$APPNAME.app/Contents/Resources/155.png"
    cp -f mac/exit.png "$APPNAME.app/Contents/Resources/288.png"
    cp -f mac/connect.png "$APPNAME.app/Contents/Resources/335.png"
    cp -f mac/disconnect.png "$APPNAME.app/Contents/Resources/336.png"
    cp -f mac/queue.png "$APPNAME.app/Contents/Resources/337.png"
    cp -f mac/unqueue.png "$APPNAME.app/Contents/Resources/338.png"
    cp -f mac/flush.png "$APPNAME.app/Contents/Resources/346.png"
    cp -f mac/save.png "$APPNAME.app/Contents/Resources/350.png"
    cp -f mac/unsave.png "$APPNAME.app/Contents/Resources/351.png"
    cp -f mac/admin.png "$APPNAME.app/Contents/Resources/349.png"
    cp -f mac/change.png "$APPNAME.app/Contents/Resources/309.png"
    cp -f mac/help.png "$APPNAME.app/Contents/Resources/268.png"
    cp -f mac/about.png "$APPNAME.app/Contents/Resources/269.png"
    cp -f mac/preferences.png "$APPNAME.app/Contents/Resources/290.png"
    cp -f mac/remtab.png "$APPNAME.app/Contents/Resources/353.png"
    cp -f mac/newtab.png "$APPNAME.app/Contents/Resources/355.png"
    cp -f mac/FILE.png "$APPNAME.app/Contents/Resources/356.png"
    cp -f mac/FOLDER.png "$APPNAME.app/Contents/Resources/357.png"
    cp -f mac/LINK.png "$APPNAME.app/Contents/Resources/365.png"
    cp -f mac/*.icns "$APPNAME.app/Contents/Resources"
    cp -f $BINNAME "$APPNAME.app/Contents/MacOS"
fi
