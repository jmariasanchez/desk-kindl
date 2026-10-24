#!/bin/sh
killall desk-kindl 2>/dev/null
lipc-set-prop com.lab126.powerd preventScreenSaver 0
# Repaint the Kindle UI.
eips -c
lipc-set-prop com.lab126.appmgrd start app://com.lab126.booklet.home 2>/dev/null
