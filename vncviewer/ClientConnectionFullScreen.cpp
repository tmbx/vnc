//  Copyright (C) 1999 AT&T Laboratories Cambridge. All Rights Reserved.
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
//
// TightVNC distribution homepage on the Web: http://www.tightvnc.com/
//
// If the source code for the VNC system is not available from the place 
// whence you received this file, check http://www.uk.research.att.com/vnc or 
// contact the authors on vnc@uk.research.att.com for information on obtaining it.
//
// Many thanks to Greg Hewgill <greg@hewgill.com> for providing the basis for 
// the full-screen mode.

#include "stdhdrs.h"
#include "vncviewer.h"
#include "ClientConnection.h"

// Parameters for scrolling in full screen mode
#define BUMPSCROLLBORDER 4
#define BUMPSCROLLAMOUNTX 8
#define BUMPSCROLLAMOUNTY 6

bool ClientConnection::InFullScreenMode() 
{
	return m_opts.m_FullScreen; 
};

// You can explicitly change mode by calling this
void ClientConnection::SetFullScreenMode(bool enable)
{	
	m_opts.m_FullScreen = enable;
	RealiseFullScreenMode(false);
}

// If the options have been changed other than by calling 
// SetFullScreenMode, you need to call this to make it happen.
void ClientConnection::RealiseFullScreenMode(bool suppressPrompt)
{
	LONG style = GetWindowLong(m_hwnd1, GWL_STYLE);
	if (m_opts.m_FullScreen) {
		if (!suppressPrompt && !pApp->m_options.m_skipprompt && !InTransRectMode()) {
			MessageBox(m_hwnd1, 
				_T("To exit from full-screen mode, press Ctrl-Alt-Shift-F.\r\n"
				"Alternatively, press Ctrl-Esc Esc and then right-click\r\n"
				"on the vncviewer taskbar icon to see the menu."),
				_T("VNCviewer full-screen mode"),
				MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
		}
		ShowWindow(m_hToolbar, SW_HIDE);
		EnableMenuItem(GetSystemMenu(m_hwnd1, FALSE), ID_TOOLBAR, MF_BYCOMMAND|MF_GRAYED);
        if (InTransRectMode() && m_opts.m_trayIcon) {
            AddTrayIcon();
            m_traytimer = SetTimer(m_hwnd1, IDT_TRAYTIMER,  10000, NULL);
            HideTaskbarButton();
        } else {
            KillTimer(m_hwnd1, m_traytimer);
            RemoveTrayIcon();
            ShowTaskbarButton();
        }
		ShowWindow(m_hwnd1, SW_MAXIMIZE);
		style = GetWindowLong(m_hwnd1, GWL_STYLE);
		style &= ~(WS_DLGFRAME | WS_THICKFRAME);
		
		SetWindowLong(m_hwnd1, GWL_STYLE, style);
        if (InTransRectMode()) {
		    SetWindowPos(m_hwnd1, HWND_TOP, -1, -1, m_fullwinwidth + 2, m_fullwinheight + 2, SWP_FRAMECHANGED);
		} else {
			int cx = GetSystemMetrics(SM_CXSCREEN);
			int cy = GetSystemMetrics(SM_CYSCREEN);
			SetWindowPos(m_hwnd1, HWND_TOPMOST, -1, -1, cx + 3, cy + 3, SWP_FRAMECHANGED);
		}
        CheckAction(ID_FULLSCREEN, true);
		
	} else {
		ShowWindow(m_hToolbar, SW_SHOW);
		EnableMenuItem(GetSystemMenu(m_hwnd1, FALSE), ID_TOOLBAR, MF_BYCOMMAND|MF_ENABLED);
		style |= (WS_DLGFRAME | WS_THICKFRAME);
		
		SetWindowLong(m_hwnd1, GWL_STYLE, style);
		SetWindowPos(m_hwnd1, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		ShowWindow(m_hwnd1, SW_NORMAL);
        KillTimer(m_hwnd1, m_traytimer);
        RemoveTrayIcon();
        ShowTaskbarButton();
        CheckAction(ID_FULLSCREEN, false);
		
	}
}

bool ClientConnection::BumpScroll(int x, int y)
{
	int dx = 0;
	int dy = 0;
	int rightborder = GetSystemMetrics(SM_CXSCREEN)-BUMPSCROLLBORDER;
	int bottomborder = GetSystemMetrics(SM_CYSCREEN)-BUMPSCROLLBORDER;
	if (x < BUMPSCROLLBORDER)
		dx = -BUMPSCROLLAMOUNTX * m_opts.m_scale_num / m_opts.m_scale_den;
	if (x >= rightborder)
		dx = +BUMPSCROLLAMOUNTX * m_opts.m_scale_num / m_opts.m_scale_den;;
	if (y < BUMPSCROLLBORDER)
		dy = -BUMPSCROLLAMOUNTY * m_opts.m_scale_num / m_opts.m_scale_den;;
	if (y >= bottomborder)
		dy = +BUMPSCROLLAMOUNTY * m_opts.m_scale_num / m_opts.m_scale_den;;
	if (dx || dy) {
		if (ScrollScreen(dx,dy)) {
			// If we haven't physically moved the cursor, artificially
			// generate another mouse event so we keep scrolling.
			POINT p;
			GetCursorPos(&p);
			if (p.x == x && p.y == y)
				SetCursorPos(x,y);
			return true;
		} 
	}
	return false;
}

void ClientConnection::ShowTaskbarButton() {
    vnclog.Print(4, _T("ShowTaskbarButton\n"));
    LONG style = GetWindowLong(m_hwnd1, GWL_EXSTYLE);
    style |=  WS_EX_APPWINDOW;
    style &= ~WS_EX_TOOLWINDOW;
    ShowWindow(m_hwnd1, SW_HIDE);
    SetWindowLong(m_hwnd1, GWL_EXSTYLE, style);
    ShowWindow(m_hwnd1, SW_NORMAL);
}

void ClientConnection::HideTaskbarButton() {
    vnclog.Print(4, _T("HideTaskbarButton\n"));
    LONG style = GetWindowLong(m_hwnd1, GWL_EXSTYLE);
    style &= ~WS_EX_APPWINDOW;
    style |=  WS_EX_TOOLWINDOW;
    ShowWindow(m_hwnd1, SW_HIDE);
    SetWindowLong(m_hwnd1, GWL_EXSTYLE, style);
    ShowWindow(m_hwnd1, SW_NORMAL);
}

void ClientConnection::AddTrayIcon() {
	vnclog.Print(4, _T("Adding tray icon\n"));
	SendTrayMsg(NIM_ADD);
}

void ClientConnection::CheckTrayIcon() {
	vnclog.Print(8, _T("Checking tray icon\n"));
	if (!SendTrayMsg(NIM_MODIFY)) {
		vnclog.Print(4, _T("Tray icon not there - reinstalling\n"));
		AddTrayIcon();
	};
}

void ClientConnection::RemoveTrayIcon() {
	vnclog.Print(4, _T("Deleting tray icon\n"));
	SendTrayMsg(NIM_DELETE);
}

bool ClientConnection::SendTrayMsg(DWORD msg)
{
	NOTIFYICONDATA nid;
	nid.hWnd = m_hwnd1;
	nid.cbSize = sizeof(nid);
	nid.uID = IDR_TRAY;	// never changes after construction
	//nid.hIcon = LoadIcon(pApp->m_instance, MAKEINTRESOURCE(IDR_TRAY));
	nid.hIcon = (HICON)LoadImage(pApp->m_instance, MAKEINTRESOURCE(IDR_TRAY), IMAGE_ICON, 16, 16, 0);
	nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	nid.uCallbackMessage = WM_TRAYNOTIFY;
    // load the window text
    if (GetWindowText(m_hwnd1, nid.szTip, 64) == 0) {
        // failed to load the window text
        nid.szTip[0] = '\0';
        // Use resource string as tip if there is one
        if (LoadString(pApp->m_instance, IDR_TRAY, nid.szTip, sizeof(nid.szTip)) == 0)
            nid.uFlags &= ~NIF_TIP;
    }
	return (bool) (Shell_NotifyIcon(msg, &nid) != 0);
}
