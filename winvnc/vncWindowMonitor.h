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

#if !defined(_WINVNC_VNCWINDOWMONITOR)
#define _WINVNC_VNCWINDOWMONITOR
#pragma once

#include "stdhdrs.h"
//class vncWindowMonitor;
//#include "vncserver.h"
class vncLauncher;
class vncServer;

#include <algorithm>
#include <set>
#include <map>
#include <list>
#include <omnithread.h>
#include "rfb.h"
#include "vncRegion.h"
#include "vncIconMgr.h"

class vncWindowMonitor {
    struct TaskbarState {
        ULONG state:8;
        ULONG texthash:24;
    };
    typedef std::map<HWND, TaskbarState> HWndStateMap;
    typedef std::map<HWND, RECT> HWndRectMap;
    typedef std::set<HWND> HWndSet;

private:
	vncServer*	m_server;
	vncLauncher* m_launcher;
	HWndRectMap	m_visibleWindows;	// visible windows and their rects
	HWndStateMap m_taskbarWindows;	// taskbar windows and their texts
	HWndSet     m_consoleWindows;	// console windows
	omni_mutex	m_visibleWindowsLock;
    omni_mutex	m_taskbarWindowsLock;
    omni_mutex	m_consoleWindowsLock;
    omni_mutex  m_refresh;
    vncRegion   m_tpRgn;
    omni_mutex  m_tpRgnLock;

	std::list<HWND> m_sharedWindowStack;

public:
	vncWindowMonitor(vncServer* server, HWND hDesktopWnd);
	void refresh();
	void refreshTpRgn();
	void pollConsoleWindows();
	void updateWindow(HWND hWnd);
	void updateTaskbar(HWND hWnd);
	void removeWindow(HWND hWnd);
	void windowControl(ULONG id, int control);
    BOOL getWindowName(HWND hWnd, char *name, int *namelen);
    BOOL getLauncherMenu(ULONG id, char **menupath, int *menupathlen);
	void updateTpRgn();
    BOOL createWindowIcon(HDC hMemDC, HWND hWnd, rfbServerDataIconPrefReq iconPref, rfbServerDataIcon **iconData, ULONG *iconDataLength);
    BOOL createLauncherIcon(HDC hMemDC, ULONG id, rfbServerDataIconPrefReq iconPref, rfbServerDataIcon **iconData, ULONG *iconDataLength);
    BOOL deleteIcon(rfbServerDataIcon *iconData);

private:
	void sendWindowState(HWND hWnd);
	void sendWindowState(HWND hWnd, int state);
	static BOOL CALLBACK EnumWndCallback(HWND hWnd, LPARAM lParam);

    BOOL updateVisibleWindows(HWND hWnd);
    BOOL updateTaskbarWindows(HWND hWnd);

	inline BOOL isVisibleWindow(HWND hWnd) {
        DWORD dwStyle, dwExStyle;
        dwStyle = GetWindowLong(hWnd, GWL_STYLE);
        if (!(dwStyle & WS_VISIBLE))
            return FALSE;
        dwExStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
        if (!(dwStyle & WS_CHILD) && !(dwExStyle & WS_EX_TOOLWINDOW))
            return TRUE;
        else {
            TCHAR classname[32];    // 32 > strlen("SysShadow")
            GetClassName(hWnd, classname, sizeof(classname)/sizeof(TCHAR));
            return ((!(dwStyle & WS_CHILD) &&
                     lstrcmp(classname, TEXT("Progman")) != 0) ||
                    (lstrcmp(classname, TEXT("SysShadow")) == 0) ||
                    (lstrcmp(classname, TEXT("#32768")) == 0));
        }
	};

	inline BOOL isTaskbarWindow(HWND hWnd) {
        DWORD dwStyle = GetWindowLong(hWnd, GWL_STYLE);
        DWORD dwExStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
        return 
			((dwStyle & WS_VISIBLE) && !(dwStyle & WS_CHILD) &&
			 !(dwExStyle & WS_EX_TOOLWINDOW) &&
			 (!(dwStyle & WS_POPUP) || (dwStyle & WS_SYSMENU)) &&
			 GetWindowTextLength(hWnd) > 0);
	};

    inline BOOL isConsoleWindow(HWND hWnd) {
        TCHAR classname[32];    // 32 > strlen("ConsoleWindowClass")
        return (GetClassName(hWnd, classname, sizeof(classname)/sizeof(TCHAR)) &&
                (lstrcmp(classname, TEXT("tty")) == 0 ||
                 lstrcmp(classname, TEXT("ConsoleWindowClass")) == 0));
    };

    inline ULONG getWTextHash(wchar_t *wname, size_t len) {
        ULONG ulHash = 0;
        while (len-- > 0)
            ulHash += (ULONG)*wname++;
        return ulHash;
    }
};

#endif // _WINVNC_VNCWINDOWMONITOR
