//  Copyright (C) 2004-2007 UCHINO Satoshi. All Rights Reserved.
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

#include "vncWindowMonitor.h"
#include "vncServer.h"
#include "vncService.h"
#include "vncLauncher.h"
#include "rfb.h"

vncWindowMonitor::vncWindowMonitor(vncServer* server, HWND hDesktopWnd) {
	m_server = server;
	m_launcher = new vncLauncher(m_server, hDesktopWnd);
	m_sharedWindowStack.push_front(m_server->GetWindowShared());
}

void vncWindowMonitor::refresh() {
    vnclog.Print(LL_INTINFO, VNCLOG("refreshing ...\n"));
	omni_mutex_lock l(m_refresh);
    m_server->UpdateWindowStateRefresh();
    {
        omni_mutex_lock tl(m_taskbarWindowsLock);
        m_taskbarWindows.clear();
    }
    refreshTpRgn();
	if (m_server->MetaWindowManagerEnabled())
        m_launcher->refresh();
    vnclog.Print(LL_INTINFO, VNCLOG("refreshing ... done.\n"));
}

void vncWindowMonitor::refreshTpRgn() {
    vnclog.Print(LL_INTINFO, VNCLOG("refreshing transparent region ...\n"));
	omni_mutex_lock l(m_refresh);
    {
        omni_mutex_lock vl(m_visibleWindowsLock);
        omni_mutex_lock cl(m_consoleWindowsLock);
        m_visibleWindows.clear();
        m_consoleWindows.clear();
    }
    ::EnumWindows(EnumWndCallback, (LPARAM)this);

    omni_mutex_lock l2(m_tpRgnLock);
    m_tpRgn.Clear();
    m_server->ClearTpRgn();
    updateTpRgn();
    vnclog.Print(LL_INTINFO, VNCLOG("refreshing transparent region ... done.\n"));
}

BOOL vncWindowMonitor::EnumWndCallback(HWND hWnd, LPARAM lParam) {
    vncWindowMonitor* pWinMon = (vncWindowMonitor*)lParam;
    pWinMon->updateVisibleWindows(hWnd);
	if (pWinMon->m_server->MetaWindowManagerEnabled())
        pWinMon->updateTaskbarWindows(hWnd);
    return TRUE;
}

// update m_visibleWindows and m_consoleWindows for specified hWnd
// returns true if m_visibleWindows is changed
BOOL vncWindowMonitor::updateVisibleWindows(HWND hWnd)
{
    BOOL bChanged = FALSE;
    if (!isVisibleWindow(hWnd)) {
        // not visible, so erase
        omni_mutex_lock l(m_visibleWindowsLock);
        if (m_visibleWindows.erase(hWnd) > 0) {
            omni_mutex_lock cl(m_consoleWindowsLock);
            if (m_consoleWindows.erase(hWnd) > 0)
                vnclog.Print(LL_INTINFO, VNCLOG("visible console window removed: hwnd 0x%X\n"), hWnd);
            else
                vnclog.Print(LL_INTINFO, VNCLOG("visible window removed: hwnd 0x%X\n"), hWnd);
            bChanged = TRUE;
        }
    } else {
        RECT rect;
        GetWindowRect(hWnd, &rect);
        omni_mutex_lock l(m_visibleWindowsLock);
        HWndRectMap::iterator p = m_visibleWindows.find(hWnd);
        if (p == m_visibleWindows.end()) {
            // not in the list
            m_visibleWindows.insert(HWndRectMap::value_type(hWnd, rect));
#if 1
            vnclog.Print(LL_INTINFO, VNCLOG("visible window added: hwnd 0x%X: (%d,%d)-(%d,%d)\n"), hWnd, rect.left, rect.top, rect.right, rect.bottom);
#else // debug
            DWORD dwStyle = GetWindowLong(hWnd, GWL_STYLE);
            DWORD dwExStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
            TCHAR classname[32];
            GetClassName(hWnd, classname, sizeof(classname)/sizeof(TCHAR));
            vnclog.Print(LL_INTINFO, VNCLOG("visible window added: hwnd 0x%X WS 0x%X ES 0x%X Class '%s' (%d,%d)-(%d,%d)\n"), hWnd, dwStyle, dwExStyle, classname, rect.left, rect.top, rect.right, rect.bottom);
#endif
            if (isConsoleWindow(hWnd)) {
                omni_mutex_lock cl(m_consoleWindowsLock);
                m_consoleWindows.insert(hWnd);
                vnclog.Print(LL_INTINFO, VNCLOG("hwnd 0x%X is a console window\n"), hWnd);
            }

			if (m_server->WindowShared()) {
				DWORD thisWndProcID, sharedWndProcID;
				GetWindowThreadProcessId(hWnd, &thisWndProcID);
				GetWindowThreadProcessId(m_sharedWindowStack.back(), &sharedWndProcID);
				if (thisWndProcID == sharedWndProcID && rect.left == 0 && rect.top == 0) {
					RECT rect2;
					GetWindowRect(GetDesktopWindow(), &rect2);
					if (rect.right == rect2.right && rect.bottom == rect2.bottom) {
						vnclog.Print(LL_INTINFO, VNCLOG("Switch to fullscreen"));
						m_sharedWindowStack.push_front(hWnd);
						m_server->SetWindowShared(hWnd);
					}
				}
			}

            bChanged = TRUE;
        } else {
            //if (p->second != rect) {
            if (!EqualRect(&p->second, &rect)) {
                // rect is changed
                vnclog.Print(LL_INTINFO, VNCLOG("visible window moved: hwnd 0x%X: (%d,%d)-(%d,%d) => (%d,%d)-(%d,%d)\n"), hWnd, 
                             p->second.left, p->second.top, p->second.right, p->second.bottom,
                             rect.left, rect.top, rect.right, rect.bottom);
                p->second = rect;
                bChanged = TRUE;
            }
        }
    }
    return bChanged;
}

// update m_taskbarWindows for specified hWnd
// returns true if m_taskbarWindows is changed
BOOL vncWindowMonitor::updateTaskbarWindows(HWND hWnd)
{
    BOOL bChanged = FALSE;
    if (!isTaskbarWindow(hWnd)) {
        // not in the taskbar, so erase
        omni_mutex_lock l(m_taskbarWindowsLock);
        if (m_taskbarWindows.erase(hWnd) > 0) {
            vnclog.Print(LL_INTINFO, VNCLOG("window closed (hwnd 0x%X)\n"), hWnd);
            m_server->UpdateWindowState(hWnd, rfbWindowStateClosed);
            bChanged = TRUE;
        }
    } else {
        TaskbarState tbState;
        tbState.state = rfbWindowStateRunning;
        if (IsIconic(hWnd))
            tbState.state |= rfbWindowStateMinimized;
        if (IsZoomed(hWnd))
            tbState.state |= rfbWindowStateMaximized;
        if (hWnd == GetForegroundWindow())
            tbState.state |= rfbWindowStateFocused;

        wchar_t name[128];
        size_t  namelen;
        namelen = GetWindowTextW(hWnd, name, 128);
        tbState.texthash = getWTextHash(name, namelen);

        omni_mutex_lock l(m_taskbarWindowsLock);
        HWndStateMap::iterator p = m_taskbarWindows.find(hWnd);
        if (p == m_taskbarWindows.end()) {
            // not in the list
            m_taskbarWindows.insert(HWndStateMap::value_type(hWnd, tbState));
#if 0
            vnclog.Print(LL_INTINFO, VNCLOG("window created (hwnd 0x%X)\n"), hWnd);
#else // debug
            DWORD dwStyle = GetWindowLong(hWnd, GWL_STYLE);
            DWORD dwExStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
            TCHAR classname[32];
            GetClassName(hWnd, classname, sizeof(classname)/sizeof(TCHAR));
            vnclog.Print(LL_INTINFO, VNCLOG("window created (hwnd 0x%X WS 0x%X ES 0x%X Class '%s')\n"), hWnd, dwStyle, dwExStyle, classname);
#endif
            m_server->UpdateWindowState(hWnd, tbState.state);
            m_server->UpdateWindowIcon(hWnd);
            bChanged = TRUE;
        } else {
            // check if its state or name is changed
            if (tbState.state    != p->second.state   ||
                tbState.texthash != p->second.texthash) {
                p->second = tbState;
                vnclog.Print(LL_INTINFO, VNCLOG("window state changed (hwnd 0x%X, state 0x%X)\n"), hWnd, tbState);
                m_server->UpdateWindowState(hWnd, tbState.state);
                bChanged = TRUE;
            }
        }
    }
    return bChanged;
}                         

// poll console windows
// this is necessary because the vnchooks.dll cannot detect changes of
// console windows
void vncWindowMonitor::pollConsoleWindows()
{
    HWND hwnd = NULL;

    // check if new console window is opened
    if (m_server->PollForeground()) {
        hwnd = GetForegroundWindow();
        if (isConsoleWindow(hwnd)) {
            updateWindow(hwnd);
            updateTaskbar(hwnd);
        }
    }
    if (m_server->PollUnderCursor()) {
        POINT mousepos;
        if (GetCursorPos(&mousepos)) {
            hwnd = WindowFromPoint(mousepos);
            if (isConsoleWindow(hwnd)) {
                updateWindow(hwnd);
                updateTaskbar(hwnd);
            }
        }
    }

    // poll other console windows
    HWndSet consoleWindows(m_consoleWindows);
    HWndSet::iterator p;
	for (p = consoleWindows.begin(); p != consoleWindows.end(); p++) {
        if (*p != hwnd) {
            updateWindow(*p);
            updateTaskbar(*p);
        }
    }
}

// update specified window
void vncWindowMonitor::updateWindow(HWND hWnd)
{
    //vnclog.Print(LL_INTINFO, VNCLOG("updateWindow: hWnd=%Xh\n"), hWnd);
    if (updateVisibleWindows(hWnd)) {
        vnclog.Print(LL_INTINFO, VNCLOG("visible windows are updated\n"));
        updateTpRgn();
    }
}

// update taskbar for specified window
void vncWindowMonitor::updateTaskbar(HWND hWnd)
{
    //vnclog.Print(LL_INTINFO, VNCLOG("updateTaskbar: hWnd=%Xh\n"), hWnd);
    if (m_server->MetaWindowManagerEnabled())
        updateTaskbarWindows(hWnd);
}

// remove specified window
void vncWindowMonitor::removeWindow(HWND hWnd) {
    vnclog.Print(LL_INTINFO, VNCLOG("removeWindow: hWnd=%Xh\n"), hWnd);
    omni_mutex_lock vl(m_visibleWindowsLock);
    omni_mutex_lock tl(m_taskbarWindowsLock);
    if (m_visibleWindows.erase(hWnd) > 0) {
        omni_mutex_lock cl(m_consoleWindowsLock);
        if (m_consoleWindows.erase(hWnd) > 0)
            vnclog.Print(LL_INTINFO, VNCLOG("removed: console hwnd 0x%X\n"), hWnd);
        else
            vnclog.Print(LL_INTINFO, VNCLOG("removed: hwnd 0x%X\n"), hWnd);
        updateTpRgn();
    }
	if (m_server->MetaWindowManagerEnabled() &&
        m_taskbarWindows.erase(hWnd) > 0) {
        vnclog.Print(LL_INTINFO, VNCLOG("window closed (hwnd 0x%X)\n"), hWnd);
        m_server->UpdateWindowState(hWnd, rfbWindowStateClosed);
    }

	if (m_server->WindowShared()){
		m_sharedWindowStack.remove(hWnd);
		if (m_sharedWindowStack.size() == 0) {
			vnclog.Print(LL_INTINFO, VNCLOG("Shared windows closed, exiting\n"));
			vncService::KillRunningCopy();
		}
		else
			m_server->SetWindowShared(m_sharedWindowStack.front());
	}
}

// process clients' requests
void vncWindowMonitor::windowControl(ULONG id, int control) {
	if (!m_server->MetaWindowManagerEnabled())
        return;

	HWND hWnd = (HWND)id;
    vnclog.Print(LL_INTINFO, VNCLOG("windowControl: id=0x%X, control=%d\n"), id, control);

    // check if the window really exists.
    switch (control) {
    case rfbWindowControlSetFocus:
        if (hWnd == 0)
            break;      /* skip check */
        /* continue */
    case rfbWindowControlClose:
    case rfbWindowControlRestore:
    case rfbWindowControlMinimize:
    case rfbWindowControlMaximize:
        if (!IsWindow(hWnd)) {
            vnclog.Print(LL_INTERR, VNCLOG("window (hWnd=0x%X) does not exist any more\n"), hWnd);
            // notify client that this window does not exist
            m_server->UpdateWindowState(hWnd, rfbWindowStateClosed);
            return;
        }
        break;
    }

	switch (control) {
	case rfbWindowControlNone:
		refresh();
		break;
	case rfbWindowControlLaunch:
		m_launcher->launch(id);
	    break;
	case rfbWindowControlClose:
        PostMessage(hWnd, WM_CLOSE, 0, 0);
		break;
	case rfbWindowControlRestore:
		if (IsIconic(hWnd) || IsZoomed(hWnd)) {
			ShowWindow(hWnd, SW_RESTORE);
            SetForegroundWindow(hWnd);
        } else {
            updateTaskbar(hWnd);	// send up-to-date state
        }
		break;
	case rfbWindowControlMinimize:
		if (!IsIconic(hWnd))
			ShowWindow(hWnd, SW_MINIMIZE);
		else
            updateTaskbar(hWnd);	// send up-to-date state
		break;
	case rfbWindowControlMaximize:
		if (!IsZoomed(hWnd))
			ShowWindow(hWnd, SW_MAXIMIZE);
		else
            updateTaskbar(hWnd);	// send up-to-date state
		break;
    case rfbWindowControlSetFocus:
        if (hWnd)
            SetForegroundWindow(hWnd);
        else
            SetForegroundWindow(GetDesktopWindow()); /* focus out */
        break;
	default:
		vnclog.Print(LL_INTERR, VNCLOG("windowControl: unsupported control %d\n"), control);
		break;
	}
}

// if the transparent region has been changed, send updates to clients
void vncWindowMonitor::updateTpRgn() {
    vncRegion   incRgn;
    vncRegion   decRgn;
	RECT sharedRect;
	sharedRect = m_server->GetSharedRect();
    {
        omni_mutex_lock l(m_tpRgnLock);
        omni_mutex_lock l2(m_visibleWindowsLock);

        vncRegion prevTpRgn = m_tpRgn;
        m_tpRgn.Clear();
        m_tpRgn.AddRect(sharedRect);

        // subtract regions in visibleWndSet
        HWndRectMap::iterator p;
        for (p = m_visibleWindows.begin(); p != m_visibleWindows.end(); p++) {
            HWND hWnd = p->first;
            if (IsIconic(hWnd))
                continue;

            //vncRegion wndRgn(hWnd);
            //vnclog.Print(LL_INTERR, VNCLOG("Window Region:\n"));
            //wndRgn.print();
            m_tpRgn.Subtract(vncRegion(hWnd));

            if (m_tpRgn.IsEmpty()) {
                //vnclog.Print(LL_INTERR, VNCLOG("getTransparentRegion: result is NULLREGION\n"));
                break;
            }
        }
        //vnclog.Print(LL_INTERR, VNCLOG("Transparent Region:\n"));
        //m_tpRgn.print();

        m_tpRgn.Offset(-sharedRect.left, -sharedRect.top);
        incRgn = m_tpRgn;
        decRgn = prevTpRgn;
        incRgn.Subtract(prevTpRgn);     // incRgn = m_tpRgn - prevTpRgn;
        decRgn.Subtract(m_tpRgn);       // decRgn = prevTpRgn - m_tpRgn;
    }
    if (incRgn.IsEmpty() && decRgn.IsEmpty()) {
        //vnclog.Print(LL_INTINFO, VNCLOG("transparent region NOT changed\n"));
    } else {
        vnclog.Print(LL_INTINFO, VNCLOG("updating transparent region ...\n"));
        m_server->UpdateTpRgn(incRgn, decRgn);
    }
}

// get the window name in UTF-8 encode
BOOL vncWindowMonitor::getWindowName(HWND hWnd, char *name, int *namelen)
{
    wchar_t wname[128];
    size_t insize;
    if ((insize = GetWindowTextW(hWnd, wname, 128)) == 0)
        return FALSE;

    *namelen = WideCharToMultiByte(CP_UTF8, 0,
                                   wname, wcslen(wname),
                                   name, *namelen, NULL, NULL);
    return *namelen > 0;
}

// create window icon of the specified preference
BOOL vncWindowMonitor::createWindowIcon(HDC hMemDC, HWND hWnd, rfbServerDataIconPrefReq iconPref, rfbServerDataIcon **iconData, ULONG *iconDataLength)
{
	if (!m_server->MetaWindowManagerEnabled())
        return FALSE;

    HICON hIcon;
    int iconsize;

    if (iconPref.size < 24) {
        iconsize = 16;
        hIcon = (HICON)GetClassLong(hWnd, GCL_HICONSM);
        if (hIcon == NULL) {
            iconsize = 32;
            hIcon = (HICON)GetClassLong(hWnd, GCL_HICON);
        }
    } else {
        iconsize = 32;
        hIcon = (HICON)GetClassLong(hWnd, GCL_HICON);
    }
    if (hIcon == NULL) {
        vnclog.Print(LL_INTERR, VNCLOG("failed to get window icon handle of hwnd 0x%X (error=%lu)\n"), hWnd, GetLastError());
        return FALSE;
    }
    return vncIconMgr::createIcon(hMemDC, hIcon, iconsize, iconData, iconDataLength);
}

// create launcher icon of the specified preference
BOOL vncWindowMonitor::createLauncherIcon(HDC hMemDC, ULONG id, rfbServerDataIconPrefReq iconPref, rfbServerDataIcon **iconData, ULONG *iconDataLength)
{
	if (!m_server->MetaWindowManagerEnabled())
        return FALSE;

    HICON hIcon;
    int iconsize;

    if (iconPref.size < 24) {
        iconsize = 16;
        hIcon = m_launcher->getSmallIcon(id);
    } else {
        iconsize = 32;
        hIcon = m_launcher->getLargeIcon(id);
    }
    if (hIcon == NULL) {
        vnclog.Print(LL_INTERR, VNCLOG("failed to get launcher icon handle of id %ul\n"), id);
        return FALSE;
    }
    return vncIconMgr::createIcon(hMemDC, hIcon, iconsize, iconData, iconDataLength);
}

// delete the icon data from vncIconMgr
BOOL vncWindowMonitor::deleteIcon(rfbServerDataIcon *iconData) {
    return m_server->MetaWindowManagerEnabled() &&
        vncIconMgr::deleteIcon(iconData);
}

// get the launcher item
BOOL vncWindowMonitor::getLauncherMenu(ULONG id, char **menupath, int *menupathlen)
{
	return m_server->MetaWindowManagerEnabled() &&
        m_launcher->getMenu(id, menupath, menupathlen);
}
