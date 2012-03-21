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

#include "stdhdrs.h"
#include "WinVNC.h"
#include "vncLauncher.h"
#include "vncService.h"
#include <wchar.h>
#include <shlobj.h>

const char *LAUNCHER_CLASS_NAME = "WinVNC.MetaVNC Launcher";

// Implementation

static LRESULT CALLBACK vncLauncherWndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam);

// create the Launcher (service mode only)
// Note: this routine must be called before vncLauncherMain() is called.
BOOL vncLauncherCreate(void)
{
    if (FindWindow(LAUNCHER_CLASS_NAME, NULL))
        return FALSE;

	// Create a dummy window to handle launcher messages
	WNDCLASSEX wndclass;
	wndclass.cbSize			= sizeof(wndclass);
	wndclass.style			= 0;
	wndclass.lpfnWndProc	= vncLauncherWndProc;
	wndclass.cbClsExtra		= 0;
	wndclass.cbWndExtra		= 0;
	wndclass.hInstance		= hAppInstance;
	wndclass.hIcon			= NULL;
	wndclass.hCursor		= NULL;
	wndclass.hbrBackground	= (HBRUSH) GetStockObject(WHITE_BRUSH);
	wndclass.lpszMenuName	= (const char *) NULL;
	wndclass.lpszClassName	= LAUNCHER_CLASS_NAME;
	wndclass.hIconSm		= NULL;
	RegisterClassEx(&wndclass);

    HWND hwnd = CreateWindow(LAUNCHER_CLASS_NAME,
				LAUNCHER_CLASS_NAME,
				WS_OVERLAPPEDWINDOW,
				CW_USEDEFAULT,
				CW_USEDEFAULT,
				200, 200,
				NULL,
				NULL,
				hAppInstance,
				NULL);

//  if (hwnd)
//		MessageBox(NULL, "Launcher window is created", szAppName, MB_OK);

    return hwnd != NULL;        /* succeeded ? */
}

// Main loop (service mode only)
// Note: this routine runs in a different process from WinVNC service
int vncLauncherMain(void)
{
	MSG msg;
	while (GetMessage(&msg, NULL, 0,0) ) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
//  MessageBox(NULL, "Launcher window is terminated", szAppName, MB_OK);
	return msg.wParam;
}

// Process window messages (service mode only)
// Note: this routine runs in a different process from WinVNC service
static LRESULT CALLBACK vncLauncherWndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	switch (iMsg) {
	case WM_COPYDATA:
		{
			COPYDATASTRUCT *pcds = (COPYDATASTRUCT *)lParam;
			ShellExecuteW(hwnd, NULL, (wchar_t *)pcds->lpData, NULL, NULL, SW_SHOWNORMAL);
		}
        return 0;
	case WM_DESTROY:
		// The user wants WinVNC to quit cleanly...
		//vnclog.Print(LL_INTERR, VNCLOG("quitting from WM_DESTROY\n"));
		PostQuitMessage(0);
		return 0;

	}
	// Message not recognised
	return DefWindowProc(hwnd, iMsg, wParam, lParam);
}

// Close running launcher
// Note: this routine runs in a different process from WinVNC service
BOOL vncLauncherClose() {
    HWND hwnd = FindWindow(LAUNCHER_CLASS_NAME, NULL);
    if (hwnd) {
        PostMessage(hwnd, WM_CLOSE, 0, 0);
        return TRUE;
    }
    return FALSE;
}

///////////////////////////////////////////////////////////////////////////////
// vncLauncher class 

HWND vncLauncher::hLauncherWnd = NULL;
BOOL vncLauncher::enabled = FALSE;

const char *vncLauncher::menuPathSeparator = "/";
#define menuPathSeparatorLen    1

wchar_t vncLauncher::commonProgramFolder[MAX_PATH+1] = L"";//CSIDL_COMMON_PROGRAMS
wchar_t vncLauncher::userProgramFolder[MAX_PATH+1] = L"";  //CSIDL_PROGRAMS

vncLauncher::vncLauncher(vncServer *server, HWND hDesktopWnd) {
    m_server = server;
	m_lastid = 0;
	if (wcslen(commonProgramFolder) == 0 &&
        wcslen(userProgramFolder) == 0) {
        updateSpecialFolders(NULL);
    }
    m_hDesktopWnd = hDesktopWnd;
    if (!vncService::RunningAsService())
        enabled = TRUE;
}

vncLauncher::~vncLauncher() {
}

void vncLauncher::updateSpecialFolders(HANDLE userToken) {
    if (SHGetFolderPathW(NULL, CSIDL_COMMON_PROGRAMS, NULL, SHGFP_TYPE_CURRENT, commonProgramFolder) != S_OK) {
        wcscpy(commonProgramFolder, L"");
        vnclog.Print(LL_INTERR, VNCLOG("SHGetFolderPathW Failed to find CSIDL_COMMON_PROGRAMS\n"));
    }
    if (SHGetFolderPathW(NULL, CSIDL_PROGRAMS, userToken, SHGFP_TYPE_CURRENT, userProgramFolder) != S_OK) {
        wcscpy(userProgramFolder, L"");
        vnclog.Print(LL_INTERR, VNCLOG("SHGetFolderPathW Failed to find CSIDL_PROGRAMS\n"));
    }
    if (vncService::RunningAsService()) {
        // find launcher window here
        hLauncherWnd = FindWindow(LAUNCHER_CLASS_NAME, NULL);
        vnclog.Print(LL_INTERR, VNCLOG("hLauncherWnd = 0x%X\n"), hLauncherWnd);
    }
}

void vncLauncher::addLauncher(char *vncMenuPath, int vncMenuPathLen, wchar_t *filename) {
    if (_wcsicmp(filename, L"desktop.ini") == 0) {
        return; // ignore "desktop.ini"
    }

    wchar_t commandPath[MAX_PATH+1];
    if (GetCurrentDirectoryW(MAX_PATH, commandPath) == 0) {
        return;
    }
    wcscat(commandPath, L"\\");
    wcscat(commandPath, filename);

    vncLauncherItem* launcherItem = new vncLauncherItem(++m_lastid, commandPath, vncMenuPath, vncMenuPathLen);
    {
        omni_mutex_lock l(m_launcherMapLock);
        m_launcherMap.insert(LauncherMap::value_type(launcherItem->id(), launcherItem));
    }
	// send launcher to client
    m_server->UpdateLauncher(launcherItem->id());
}

vncLauncherItem* vncLauncher::getItem(ULONG id) {
    LauncherMap::iterator it;
	omni_mutex_lock l(m_launcherMapLock);
    it = m_launcherMap.find(id);
    if (it == m_launcherMap.end()) {    // not found
        return NULL;
    }
    return (*it).second;
}

void vncLauncher::launch(ULONG id) {
	omni_mutex_lock l(m_launcherMapLock);
    vncLauncherItem* launcherItem = getItem(id);
    if (launcherItem) {
        wchar_t *commandpath = launcherItem->commandPath();
        vnclog.Print(LL_INTERR, VNCLOG("launching id %u\n"), id);
        if (vncService::RunningAsService()) {
            if (!hLauncherWnd) {
                hLauncherWnd = FindWindow(LAUNCHER_CLASS_NAME, NULL);
                if (!hLauncherWnd) {
                    vnclog.Print(LL_INTERR, VNCLOG("could not find launcher window\n"));
                    return;
                }
            }

            COPYDATASTRUCT cds;
            // let the launcher window launch the app
            cds.dwData = 0;     // not used
            cds.cbData = (wcslen(commandpath) + 1) * sizeof(wchar_t);
            cds.lpData = (PVOID)commandpath;
            vnclog.Print(LL_INTERR, VNCLOG("sending launch message\n"));
            SendMessage(hLauncherWnd, WM_COPYDATA, (WPARAM)m_hDesktopWnd, (LPARAM)&cds);
        } else {
            // launch the app by myself
            ShellExecuteW(GetDesktopWindow(), NULL, commandpath, NULL, NULL, SW_SHOWNORMAL);
        }
    }
}

HICON vncLauncher::getSmallIcon(ULONG id) {
    vncLauncherItem* launcherItem = getItem(id);
    if (launcherItem)
        return launcherItem->getSmallIcon();
    return NULL;
}

HICON vncLauncher::getLargeIcon(ULONG id) {
    vncLauncherItem* launcherItem = getItem(id);
    if (launcherItem)
        return launcherItem->getLargeIcon();
    return NULL;
}

BOOL vncLauncher::getMenu(ULONG id, char **m_menupath, int *m_menupathlen)
{
    vncLauncherItem* launcherItem = getItem(id);
    if (!launcherItem)
        return FALSE;
    *m_menupath    = launcherItem->menuPath();
    *m_menupathlen = launcherItem->menuPathLen();
    return TRUE;
}

void vncLauncher::refresh() {
    if (!enabled)
        return;

    char vncMenuPath[MAX_PATH];
    omni_mutex_lock l(m_refreshLock);
    int vncMenuPathLen = 0;
    int vncMenuPathRest = MAX_PATH;

    vnclog.Print(LL_INTINFO, VNCLOG("refreshing ...\n"));
    {
        omni_mutex_lock l(m_launcherMapLock);
        m_launcherMap.clear();
    }
	if (wcslen(commonProgramFolder) > 0) { /* initialized? */
        // retrieve the common program menus
        strncpy(vncMenuPath, menuPathSeparator, menuPathSeparatorLen);
        vncMenuPathLen  = menuPathSeparatorLen;
        vncMenuPathRest = MAX_PATH - vncMenuPathLen;

        if (!SetCurrentDirectoryW(commonProgramFolder)) {
            vnclog.Print(LL_INTERR, VNCLOG("Failed to change directory to '%s'. (error %d)\n"), commonProgramFolder, GetLastError ());
            return;
        }
        refresh(vncMenuPath, vncMenuPathLen, vncMenuPathRest);
    }

	if (wcslen(userProgramFolder) > 0) { /* initialized? */
        // retrieve the user's program menus
        strncpy(vncMenuPath, menuPathSeparator, menuPathSeparatorLen);
        vncMenuPathLen  = menuPathSeparatorLen;
        vncMenuPathRest = MAX_PATH - vncMenuPathLen;

        if (!SetCurrentDirectoryW(userProgramFolder)) {
            vnclog.Print(LL_INTERR, VNCLOG("Failed to change directory to '%s'. (error %d)\n"), userProgramFolder, GetLastError ());
            return;
        }
        refresh(vncMenuPath, vncMenuPathLen, vncMenuPathRest);
    }
    vnclog.Print(LL_INTINFO, VNCLOG("refreshing ... done.\n"));
}

void vncLauncher::refreshSubdir(char *vncMenuPath, int vncMenuPathLen, int vncMenuPathRest, wchar_t *subdir) {
    /* search sub directory */
	if (!SetCurrentDirectoryW(subdir)) {
		vnclog.Print(LL_INTERR, VNCLOG("Failed to change directory to '%s'. (error %d)\n"), subdir, GetLastError ());
		return;
	}
    refresh(vncMenuPath, vncMenuPathLen, vncMenuPathRest);
    if (!SetCurrentDirectoryW(L"..")) {
        vnclog.Print(LL_INTERR, VNCLOG("Failed to change directory to '..'. (error %d)\n"), GetLastError());
        return;
    }
}

void vncLauncher::refresh(char *vncMenuPath, int vncMenuPathLen, int vncMenuPathRest)
{
	WIN32_FIND_DATAW FindFileData;
	HANDLE hFind;
	int err;
    int prevVncMenuPathLen;
    int prevVncMenuPathRest;

	hFind = FindFirstFileW(L"*.*", &FindFileData);
	if (hFind == INVALID_HANDLE_VALUE) {
		vnclog.Print(LL_INTERR, VNCLOG("Invalid File Handle. (error %d)\n"), GetLastError ());
		return;
	}
    prevVncMenuPathLen  = vncMenuPathLen;
    prevVncMenuPathRest = vncMenuPathRest;

    do {
        if (wcscmp(FindFileData.cFileName, L".") == 0 ||
            wcscmp(FindFileData.cFileName, L"..") == 0)
            continue;

        vncMenuPathLen  = prevVncMenuPathLen;
        vncMenuPathRest = prevVncMenuPathRest;
        /* append foldername to vnc menu path */
        appendPath(vncMenuPath, &vncMenuPathLen, &vncMenuPathRest,
                   FindFileData.cFileName, wcslen(FindFileData.cFileName));
        if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            /* append path separator */
            strncpy(&vncMenuPath[vncMenuPathLen], menuPathSeparator, menuPathSeparatorLen);
            vncMenuPathLen  += menuPathSeparatorLen;
            vncMenuPathRest -= menuPathSeparatorLen;

            refreshSubdir(vncMenuPath, vncMenuPathLen, vncMenuPathRest, FindFileData.cFileName);
        } else {
            addLauncher(vncMenuPath, vncMenuPathLen, FindFileData.cFileName);
        }
    } while (FindNextFileW(hFind, &FindFileData));

    if ((err = GetLastError()) != ERROR_NO_MORE_FILES) {
        FindClose(hFind);
        return;
    }
    FindClose(hFind);
}

void vncLauncher::appendPath(char *vncMenuPath, int *vncMenuPathLen, int *vncMenuPathRest, const wchar_t *addStr, int addStrLen)
{
    vncMenuPath += *vncMenuPathLen;
    addStrLen = WideCharToMultiByte(CP_UTF8, 0,
                                    addStr, addStrLen, 
                                    vncMenuPath, *vncMenuPathRest,
                                    NULL, NULL);
    *vncMenuPathLen  += addStrLen;
    *vncMenuPathRest -= addStrLen;
}
