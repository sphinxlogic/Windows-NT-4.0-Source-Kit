#
#   to be included in makefile
#

VERSION=WIN_RELEASE
C_DEFINE0=-DWIN32 -D_WIN32 -D_WINDOWS -DNT -D_NTSDK -DSDK_CODE
C_DEFINE1=-DCHAR_NARROW -DNDEBUG -DNO_SMARTHEAP -DNT_EDITION
C_DEFINES=-DVERSION=WIN_RELEASE -DLANG=USA $(C_DEFINE0) $(C_DEFINE1)
MFC_FLAGS=/J
