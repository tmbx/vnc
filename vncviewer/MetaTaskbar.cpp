//  Copyright (C) 2006-2007 UCHINO Satoshi. All Rights Reserved.
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
#include "vncviewer.h"
#include "MetaTaskbar.h"
#include "IconUtil.h"
#include "shobjidl.h"

const char *MetaTaskbar::TaskSwClassNameTemplate = "MetaVNC Taskbar %08X";
const int   MetaTaskbar::TaskSwClassNameLen      = 25;

MetaTaskbar::MetaTaskbar(ClientConnection *pCC, VNCviewerApp *pApp)
{
	m_clientconn = pCC;
	m_pApp = pApp;
}

MetaTaskbar::~MetaTaskbar()
{
	clear();
}

void MetaTaskbar::clear()
{
	omni_mutex_lock l(m_taskSwitchesLock);
    IdStateMap::iterator p;
    for (p = m_taskSwitches.begin(); p != m_taskSwitches.end(); p++) {
        destroyTaskSw(p->second.hwnd);
    }
    m_taskSwitches.clear();
}

void MetaTaskbar::update(ULONG id, ULONG state)
{
vnclog.Print(1, _T("%s: id=0x%X state=0x%X\n"), __FUNCTION__, id, state);
    if (state == rfbWindowStateClosed) {
        // closed 
        omni_mutex_lock l(m_taskSwitchesLock);
        IdStateMap::iterator p = m_taskSwitches.find(id);
        if (p != m_taskSwitches.end()) {
            destroyTaskSw(p->second.hwnd);
            m_taskSwitches.erase(id);
        }
    } else if (state & rfbWindowStateRunning) {
        HWND hwnd = NULL;
        {
            omni_mutex_lock l(m_taskSwitchesLock);
            IdStateMap::iterator p = m_taskSwitches.find(id);
            if (p != m_taskSwitches.end()) {
                // update state
                p->second.state = state;
                hwnd = p->second.hwnd;
            }
        } // release lock here to avoid deadlock

        if (hwnd) {
            if (state & rfbWindowStateFocused) {
                SetForegroundWindow(m_clientconn->m_hwnd1);
                taskbarControl(hwnd, TASKBAR_ACTIVATE);
            }
            // update tasksw state
            if (state & rfbWindowStateMinimized)
                ShowWindow(hwnd, SW_MINIMIZE);
            else if (state & rfbWindowStateMaximized)
                ShowWindow(hwnd, SW_MAXIMIZE);
            else
                ShowWindow(hwnd, SW_RESTORE);
        } else {
            // new taskbar switch

            // Note: make the client window create task switch windows,
            // otherwise task switch windows cannot receive necessary
            // window messages.
            //PostMessage(m_clientconn->m_hwnd1, RFB_METATASKSW_CREATE, id, state);
            SendMessage(m_clientconn->m_hwnd1, RFB_METATASKSW_CREATE, id, state);
        }
    }
}

void MetaTaskbar::updateText(ULONG id, const char *name, size_t nameLength)
{
    HWND hwnd = getTaskSwWnd(id);
    if (hwnd == NULL)
        return;

    // convert encoding from UTF-8 to UCS-2
    wchar_t *wname = new wchar_t[nameLength + 1 + wcslen(m_clientconn->m_desktopNameW) + 1];
    int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                   name, (int)nameLength,
                                   wname, (int)nameLength);
    if (wlen <= 0) {
        vnclog.Print(0, _T("MultiByteToWideChar failed. (error %d)\n"), GetLastError());
        delete[] wname;
        return;
    }
    wname[wlen] = L'\0';
    // add '@' and the remote desktop name
    wcscat(wname, L"@");
    wcscat(wname, m_clientconn->m_desktopNameW);

    SetWindowTextW(hwnd, wname);
    delete[] wname;
}

void MetaTaskbar::setIcon(ULONG id, rfbServerDataIcon *iconData, ULONG iconDataLength)
{
vnclog.Print(1, _T("%s: id=0x%X\n"), __FUNCTION__, id);
    HWND hwnd = getTaskSwWnd(id);
    if (hwnd == NULL)
        return;

    HICON hIcon = IconUtil::createIcon(iconData, iconDataLength);
    HICON hOldIcon = (HICON)GetClassLong(hwnd, GCL_HICON);
    SetClassLong(hwnd, GCL_HICON, (LONG)hIcon);
    if (hOldIcon != NULL)
        DestroyIcon(hOldIcon);
}

HWND MetaTaskbar::createTaskSw(ULONG id, ULONG state)
{
vnclog.Print(1, _T("%s: id=0x%X state=0x%X\n"), __FUNCTION__, id, state);
    char className[TaskSwClassNameLen];
    sprintf(className, TaskSwClassNameTemplate, id);

	// Create a dummy window to handle taskbar messages
	WNDCLASSEX wndclass;
	wndclass.cbSize			= sizeof(wndclass);
	wndclass.style			= 0;
	wndclass.lpfnWndProc	= MetaTaskbar::WndProc;
	wndclass.cbClsExtra		= 0;
	wndclass.cbWndExtra		= 0;
	wndclass.hInstance		= m_pApp->m_instance;
	wndclass.hIcon			= NULL;
	wndclass.hCursor		= NULL;
	wndclass.hbrBackground	= (HBRUSH) GetStockObject(WHITE_BRUSH);
	wndclass.lpszMenuName	= (const char *) NULL;
	wndclass.lpszClassName	= className;
	wndclass.hIconSm		= NULL;
	RegisterClassEx(&wndclass);

    HWND hwnd = CreateWindow(className,
                className,
                WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX,
				0, 0, 0, 0,
				NULL,
				NULL,
				m_pApp->m_instance,
				NULL);

    if (hwnd == NULL) {
        vnclog.Print(0, TEXT("MetaTaskbar : ERROR : failed to create task switch window\n"));
        return NULL;
    }

    HRGN hRgn = CreateRectRgn(0, 0, 0, 0);
    SetWindowRgn(hwnd, hRgn, FALSE);     // set this window transparent

	SetWindowLong(hwnd, GWL_USERDATA, (LONG) this);

    HMENU hsysmenu = GetSystemMenu(hwnd, FALSE);
    if (hsysmenu == NULL) {
        vnclog.Print(0, TEXT("MetaTaskbar : ERROR : failed to get system menu of task switch window\n"));
    } else {
        RemoveMenu(hsysmenu, SC_SIZE, MF_BYCOMMAND);
        RemoveMenu(hsysmenu, SC_MOVE, MF_BYCOMMAND);
    }

    taskbarControl(hwnd, TASKBAR_ADD);

    TaskSwState tswstate;
    tswstate.hwnd  = hwnd;
    tswstate.state = state;
	omni_mutex_lock l(m_taskSwitchesLock);
    m_taskSwitches.insert(IdStateMap::value_type(id, tswstate));

    if (state & rfbWindowStateMinimized)
        ShowWindow(hwnd, SW_MINIMIZE);
    else if (state & rfbWindowStateMaximized)
        ShowWindow(hwnd, SW_MAXIMIZE);

    return hwnd;
}

void MetaTaskbar::destroyTaskSw(HWND hwnd)
{
    char className[TaskSwClassNameLen];
    taskbarControl(hwnd, TASKBAR_DEL);
    GetClassName(hwnd, className, TaskSwClassNameLen);
    HICON hOldIcon = (HICON)GetClassLong(hwnd, GCL_HICON);
    DestroyWindow(hwnd);
    UnregisterClass(className, m_pApp->m_instance);
    if (hOldIcon != NULL)
        DestroyIcon(hOldIcon);
}

ULONG MetaTaskbar::getTaskSwState(ULONG id)
{
	omni_mutex_lock l(m_taskSwitchesLock);
    IdStateMap::iterator p = m_taskSwitches.find(id);
	if (p == m_taskSwitches.end()) {
        return rfbWindowStateUnknown;
	}
    return p->second.state;
}

HWND MetaTaskbar::getTaskSwWnd(ULONG id)
{
	omni_mutex_lock l(m_taskSwitchesLock);
    IdStateMap::iterator p = m_taskSwitches.find(id);
	if (p == m_taskSwitches.end()) {
        return NULL;
	}
    return p->second.hwnd;
}

ULONG MetaTaskbar::getId(HWND hwnd)
{
	omni_mutex_lock l(m_taskSwitchesLock);
    IdStateMap::iterator p;
    for (p = m_taskSwitches.begin(); p != m_taskSwitches.end(); p++)
        if (p->second.hwnd == hwnd)
            return p->first;
    return 0;
}

BOOL MetaTaskbar::taskbarControl(HWND hwnd, TaskbarAction action)
{
    ITaskbarList *pTaskbarList;
    HRESULT         hr;

    CoInitialize(NULL);
    hr = CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER,
             IID_ITaskbarList, (void**)&pTaskbarList);
    pTaskbarList->HrInit();
    switch (action) {
    case TASKBAR_ADD:
        vnclog.Print(1, TEXT("%s: TASKBAR_ADD (0x%X)\n"), __FUNCTION__, hwnd);
        hr = pTaskbarList->AddTab(hwnd);
        break;
    case TASKBAR_DEL:
        vnclog.Print(1, TEXT("%s: TASKBAR_DEL (0x%X)\n"), __FUNCTION__, hwnd);
        hr = pTaskbarList->DeleteTab(hwnd);
        break;
    case TASKBAR_ACTIVATE:
        vnclog.Print(1, TEXT("%s: TASKBAR_ACTIVATE (0x%X)\n"), __FUNCTION__, hwnd);
        hr = pTaskbarList->ActivateTab(hwnd);
        break;
    }
    pTaskbarList->Release();
    CoUninitialize();
    return hr == NOERROR ? TRUE : FALSE;
}

LRESULT CALLBACK MetaTaskbar::WndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	vnclog.Print(1, TEXT("%s: hwnd=0x%X, iMsg=0x%X, wParam=0x%X, lParam=0x%X\n"), __FUNCTION__, hwnd, iMsg, wParam, lParam);

    MetaTaskbar *_this = (MetaTaskbar *)GetWindowLong(hwnd, GWL_USERDATA);
    ULONG id = 0;
    if (_this)
        id = _this->getId(hwnd);
    
    //vnclog.Print(1, TEXT("%s: _this=0x%X, id=0x%X\n"), __FUNCTION__, _this, id);
    if (id == 0) {
        vnclog.Print(1, TEXT("%s: id=0\n"), __FUNCTION__);
        return DefWindowProc(hwnd, iMsg, wParam, lParam);
    }

    switch (iMsg) {
    case WM_SETFOCUS:
vnclog.Print(1, TEXT("%s: WM_SETFOCUS\n"), __FUNCTION__);
        _this->m_clientconn->SendWindowControl(id, rfbWindowControlSetFocus);
        SetForegroundWindow(_this->m_clientconn->m_hwnd1);
        break;

    case WM_KILLFOCUS:
vnclog.Print(1, TEXT("%s: WM_KILLFOCUS\n"), __FUNCTION__);
        break;

    case WM_ACTIVATE:
vnclog.Print(1, TEXT("%s: WM_ACTIVATE\n"), __FUNCTION__);
        break;

    case WM_ACTIVATEAPP:
vnclog.Print(1, TEXT("%s: WM_ACTIVATEAPP\n"), __FUNCTION__);
        break;

    case WM_SYSCOMMAND:
        vnclog.Print(1, TEXT("%s: WM_SYSCOMMAND: %s\n"), __FUNCTION__,
                     wParam == SC_SIZE ? TEXT("SC_SIZE") :
                     wParam == SC_MOVE ? TEXT("SC_MOVE") :
                     wParam == SC_MINIMIZE ? TEXT("SC_MINIMIZE") :
                     wParam == SC_MAXIMIZE ? TEXT("SC_MAXIMIZE") :
                     wParam == SC_NEXTWINDOW ? TEXT("SC_NEXTWINDOW") :
                     wParam == SC_PREVWINDOW ? TEXT("SC_PREVWINDOW") :
                     wParam == SC_CLOSE ? TEXT("SC_CLOSE") :
                     wParam == SC_VSCROLL ? TEXT("SC_VSCROLL") :
                     wParam == SC_HSCROLL ? TEXT("SC_HSCROLL") :
                     wParam == SC_MOUSEMENU ? TEXT("SC_MOUSEMENU") :
                     wParam == SC_KEYMENU ? TEXT("SC_KEYMENU") :
                     wParam == SC_ARRANGE ? TEXT("SC_ARRANGE") :
                     wParam == SC_RESTORE ? TEXT("SC_RESTORE") :
                     wParam == SC_TASKLIST ? TEXT("SC_TASKLIST") :
                     wParam == SC_SCREENSAVE ? TEXT("SC_SCREENSAVE") :
                     wParam == SC_HOTKEY ? TEXT("SC_HOTKEY") :
                     wParam == SC_DEFAULT ? TEXT("SC_DEFAULT") :
                     wParam == SC_MONITORPOWER ? TEXT("SC_MONITORPOWER") :
                     wParam == SC_CONTEXTHELP ? TEXT("SC_CONTEXTHELP") :
                     wParam == SC_SEPARATOR ? TEXT("SC_SEPARATOR") :
                     TEXT("unknown"));
        {
            ULONG state = _this->getTaskSwState(id);
            switch (wParam) {
            case SC_MINIMIZE:
                if (!(state & rfbWindowStateMinimized))
                    _this->m_clientconn->SendWindowControl(id, rfbWindowControlMinimize);
                else
                    vnclog.Print(0, TEXT("MetaTaskbar : ERROR : window 0x%X is already minimized\n"), id);
                break;
            case SC_MAXIMIZE:
                if (!(state & rfbWindowStateMaximized))
                    _this->m_clientconn->SendWindowControl(id, rfbWindowControlMaximize);
                else
                    vnclog.Print(0, TEXT("MetaTaskbar : ERROR : window 0x%X is already maximized\n"), id);
                break;
            case SC_RESTORE:
                if (state & (rfbWindowStateMinimized | rfbWindowStateMaximized))
                    _this->m_clientconn->SendWindowControl(id, rfbWindowControlRestore);
                else
                    _this->m_clientconn->SendWindowControl(id, rfbWindowControlMinimize);       // the task switch is clicked
                break;
            case SC_CLOSE:
                _this->m_clientconn->SendWindowControl(id, rfbWindowControlClose);
                break;
            }
        }
        break;

    default:
        break;
    }
	return DefWindowProc(hwnd, iMsg, wParam, lParam);
}
