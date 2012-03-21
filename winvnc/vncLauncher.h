//  Copyright (C) 2004-2006 UCHINO Satoshi. All Rights Reserved.
//
//  This file is part of the VNC system.
//
//  The VNC system is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
//  USA.

#if !defined(_WINVNC_VNCLAUNCHER)
#define _WINVNC_VNCLAUNCHER

#include "stdhdrs.h"
#include <map>
#include <omnithread.h>
#include "vncserver.h"
#include "vncLauncherItem.h"

extern const char *LAUNCHER_CLASS_NAME;
BOOL vncLauncherCreate();
int  vncLauncherMain();
BOOL vncLauncherClose();

typedef std::map<ULONG, vncLauncherItem*> LauncherMap;

class vncLauncher {
 private:
    static const char *menuPathSeparator;
    static wchar_t commonProgramFolder[MAX_PATH+1];//CSIDL_COMMON_PROGRAMS
    static wchar_t userProgramFolder[MAX_PATH+1];  //CSIDL_PROGRAMS
    static HWND hLauncherWnd;   // launcher window (service mode)
    static BOOL enabled;

    omni_mutex m_launcherMapLock;
    LauncherMap m_launcherMap; // holds id-vncLauncher pair
    omni_mutex m_refreshLock;
	vncServer *m_server;
    ULONG m_lastid;
    HWND m_hDesktopWnd;

    void refresh(char *vncMenuPath, int vncMenuPathLen, int vncMenuPathRest);
    void appendPath(char *vncMenuPath, int *vncMenuPathLen, int *vncMenuPathRest, 
                    const wchar_t *addStr, int addStrLen);
    void refreshSubdir(char *vncMenuPath, int vncMenuPathLen, int vncMenuPathRest, wchar_t *subdir);
    void addLauncher(char *vncMenuPath, int vncMenuPathLen, wchar_t *filename);
    vncLauncherItem* getItem(ULONG id);
 public:
    vncLauncher(vncServer *server, HWND hDesktopWnd);
	~vncLauncher();
    static void updateSpecialFolders(HANDLE userToken);
    static void setEnabled(BOOL flag) {
        enabled = flag;
    };
    void refresh();
    void launch(ULONG id);
    BOOL getMenu(ULONG id, char **menupath, int *menupathlen);
    HICON getSmallIcon(ULONG id);
    HICON getLargeIcon(ULONG id);
};

#endif // !defined(_WINVNC_VNCLAUNCHER)
