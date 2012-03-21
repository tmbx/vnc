//  Copyright (C) 2004-2007 UCHINO Satoshi. All Rights Reserved.
//  Copyright (C) 2003-2006 Constantin Kaplinsky. All Rights Reserved.
//  Copyright (C) 2000 Tridia Corporation. All Rights Reserved.
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
// whence you received this file, check http://www.uk.research.att.com/vnc or contact
// the authors on vnc@uk.research.att.com for information on obtaining it.


// Many thanks to Randy Brown <rgb@inven.com> for providing the 3-button
// emulation code.

// This is the main source for a ClientConnection object.
// It handles almost everything to do with a connection to a server.
// The decoding of specific rectangle encodings is done in separate files.

#include "stdhdrs.h"
#include "winnls32.h"

#include "vncviewer.h"

#ifdef UNDER_CE
#include "omnithreadce.h"
#define SD_BOTH 0x02
#else
#include "omnithread.h"
#endif

#include "ClientConnection.h"
#include "SessionDialog.h"
#include "LoginAuthDialog.h"
#include "AboutBox.h"
#include "FileTransfer.h"
#include "commctrl.h"
#include "Exception.h"
extern "C" {
#include "vncauth.h"
#include "d3des.h"
}

#define INITIALNETBUFSIZE 4096
#define MAX_ENCODINGS 20
#define VWR_WND_CLASS_NAME _T("VNCviewer")

// Messages for Meta Window Manager
const UINT RFB_METATASKSW_CREATE = RegisterWindowMessage("VNCviewer.MetaWM.TaskSw.Create");
const UINT RFB_METALAUNCH        = RegisterWindowMessage("VNCviewer.MetaWM.Launch");

/*
 * Macro to compare pixel formats.
 */

#define PF_EQ(x,y)							\
	((x.bitsPerPixel == y.bitsPerPixel) &&				\
	 (x.depth == y.depth) &&					\
	 ((x.bigEndian == y.bigEndian) || (x.bitsPerPixel == 8)) &&	\
	 (x.trueColour == y.trueColour) &&				\
	 (!x.trueColour || ((x.redMax == y.redMax) &&			\
			    (x.greenMax == y.greenMax) &&		\
			    (x.blueMax == y.blueMax) &&			\
			    (x.redShift == y.redShift) &&		\
			    (x.greenShift == y.greenShift) &&		\
			    (x.blueShift == y.blueShift))))

const rfbPixelFormat vnc8bitFormat = {8, 8, 0, 1, 7,7,3, 0,3,6,0,0};
const rfbPixelFormat vnc16bitFormat = {16, 16, 0, 1, 63, 31, 31, 0,6,11,0,0};


// *************************************************************************
//  A Client connection involves two threads - the main one which sets up
//  connections and processes window messages and inputs, and a 
//  client-specific one which receives, decodes and draws output data 
//  from the remote server.
//  This first section contains bits which are generally called by the main
//  program thread.
// *************************************************************************

ClientConnection::ClientConnection(VNCviewerApp *pApp) 
{
	Init(pApp);
}

ClientConnection::ClientConnection(VNCviewerApp *pApp, SOCKET sock) 
{
	Init(pApp);
	m_sock = sock;
	m_serverInitiated = true;
	struct sockaddr_in svraddr;
	int sasize = sizeof(svraddr);
	if (getpeername(sock, (struct sockaddr *) &svraddr, 
		&sasize) != SOCKET_ERROR) {
		_stprintf(m_host, _T("%d.%d.%d.%d"), 
			svraddr.sin_addr.S_un.S_un_b.s_b1, 
			svraddr.sin_addr.S_un.S_un_b.s_b2, 
			svraddr.sin_addr.S_un.S_un_b.s_b3, 
			svraddr.sin_addr.S_un.S_un_b.s_b4);
		m_port = svraddr.sin_port;
	} else {
		_tcscpy(m_host,_T("(unknown)"));
		m_port = 0;
	};
}

ClientConnection::ClientConnection(VNCviewerApp *pApp, LPTSTR host, int port)
{
	Init(pApp);
	_tcsncpy(m_host, host, MAX_HOST_NAME_LEN);
	m_port = port;
}

void ClientConnection::Init(VNCviewerApp *pApp)
{
	m_hwnd = NULL;
	m_hwnd1 = NULL;
	m_hwndscroll = NULL;
	m_hToolbar = NULL;
	m_desktopName = NULL;
	m_port = -1;
	m_serverInitiated = false;
	m_netbuf = NULL;
	m_netbufsize = 0;
	m_zlibbuf = NULL;
	m_zlibbufsize = 0;
	m_hwndNextViewer = NULL;	
	m_pApp = pApp;
	m_dormant = false;
	m_hBitmapDC = NULL;
	m_hBitmap = NULL;
	m_hPalette = NULL;
	m_encPasswd[0] = '\0';

	m_connDlg = NULL;

	m_enableFileTransfers = false;
	m_fileTransferDialogShown = false;
	m_pFileTransfer = new FileTransfer(this, m_pApp);

    // MetaVNC
    m_hTransRgn = NULL;
	m_enableTransRect = false;
	m_enableMetaWM = false;
	m_mouseButtonPressed = false;
    m_draggingDesktop = false;
    m_prevmousepos = 0; // XXX

    m_metaTaskbar  = NULL;
    m_metaLauncher = NULL;

	// We take the initial conn options from the application defaults
	m_opts = m_pApp->m_options;
	
	m_sock = INVALID_SOCKET;
	m_bKillThread = false;
	m_threadStarted = true;
	m_running = false;
	m_pendingFormatChange = false;

	m_hScrollPos = 0; m_vScrollPos = 0;

	m_waitingOnEmulateTimer = false;
	m_emulatingMiddleButton = false;

	m_decompStreamInited = false;

	m_decompStreamRaw.total_in = ZLIBHEX_DECOMP_UNINITED;
	m_decompStreamEncoded.total_in = ZLIBHEX_DECOMP_UNINITED;

	for (int i = 0; i < 4; i++)
		m_tightZlibStreamActive[i] = false;

	prevCursorSet = false;
	rcCursorX = 0;
	rcCursorY = 0;

	// Create a buffer for various network operations
	CheckBufferSize(INITIALNETBUFSIZE);

	m_pApp->RegisterConnection(this);

	// Load a popup menu
	m_htraymenu = LoadMenu(pApp->m_instance, MAKEINTRESOURCE(IDR_TRAYMENU2));
}

void ClientConnection::InitCapabilities()
{
	// Supported authentication methods
	m_authCaps.Add(rfbAuthNone, rfbStandardVendor, sig_rfbAuthNone,
				   "No authentication");
	m_authCaps.Add(rfbAuthVNC, rfbStandardVendor, sig_rfbAuthVNC,
				   "Standard VNC password authentication");

	// Known server->client message types
	m_serverMsgCaps.Add(rfbFileListData, rfbTightVncVendor,
						sig_rfbFileListData, "File list data");
	m_serverMsgCaps.Add(rfbFileDownloadData, rfbTightVncVendor,
						sig_rfbFileDownloadData, "File download data");
	m_serverMsgCaps.Add(rfbFileUploadCancel, rfbTightVncVendor,
						sig_rfbFileUploadCancel, "File upload cancel request");
	m_serverMsgCaps.Add(rfbFileDownloadFailed, rfbTightVncVendor,
						sig_rfbFileDownloadFailed, "File download failure notification");
    if (m_opts.m_MetaWM) {
        m_serverMsgCaps.Add(rfbWindowState, rfbMetaVncVendor,
						sig_rfbWindowState, "Window State message");
        m_serverMsgCaps.Add(rfbServerData, rfbMetaVncVendor,
						sig_rfbServerData, "Server Data message");
    }

	// Known client->server message types
	m_clientMsgCaps.Add(rfbFileListRequest, rfbTightVncVendor,
						sig_rfbFileListRequest, "File list request");
	m_clientMsgCaps.Add(rfbFileDownloadRequest, rfbTightVncVendor,
						sig_rfbFileDownloadRequest, "File download request");
	m_clientMsgCaps.Add(rfbFileUploadRequest, rfbTightVncVendor,
						sig_rfbFileUploadRequest, "File upload request");
	m_clientMsgCaps.Add(rfbFileUploadData, rfbTightVncVendor,
						sig_rfbFileUploadData, "File upload data");
	m_clientMsgCaps.Add(rfbFileDownloadCancel, rfbTightVncVendor,
						sig_rfbFileDownloadCancel, "File download cancel request");
	m_clientMsgCaps.Add(rfbFileUploadFailed, rfbTightVncVendor,
						sig_rfbFileUploadFailed, "File upload failure notification");
    if (m_opts.m_MetaWM) {
        m_clientMsgCaps.Add(rfbKeyFocus, rfbMetaVncVendor,
						sig_rfbKeyFocus, "Key Focus message");
        m_clientMsgCaps.Add(rfbWindowControl, rfbMetaVncVendor,
						sig_rfbWindowControl, "Window Control message");
        m_clientMsgCaps.Add(rfbServerDataReq, rfbMetaVncVendor,
						sig_rfbServerDataReq, "Server Data request message");
    }

	// Supported encoding types
	m_encodingCaps.Add(rfbEncodingCopyRect, rfbStandardVendor,
					   sig_rfbEncodingCopyRect, "Standard CopyRect encoding");
	m_encodingCaps.Add(rfbEncodingRRE, rfbStandardVendor,
					   sig_rfbEncodingRRE, "Standard RRE encoding");
	m_encodingCaps.Add(rfbEncodingCoRRE, rfbStandardVendor,
					   sig_rfbEncodingCoRRE, "Standard CoRRE encoding");
	m_encodingCaps.Add(rfbEncodingHextile, rfbStandardVendor,
					   sig_rfbEncodingHextile, "Standard Hextile encoding");
	m_encodingCaps.Add(rfbEncodingZlib, rfbTridiaVncVendor,
					   sig_rfbEncodingZlib, "Zlib encoding from TridiaVNC");
	m_encodingCaps.Add(rfbEncodingZlibHex, rfbTridiaVncVendor,
					   sig_rfbEncodingZlibHex, "ZlibHex encoding from TridiaVNC");
	m_encodingCaps.Add(rfbEncodingTight, rfbTightVncVendor,
					   sig_rfbEncodingTight, "Tight encoding by Constantin Kaplinsky");

	// Supported "fake" encoding types
	m_encodingCaps.Add(rfbEncodingCompressLevel0, rfbTightVncVendor,
					   sig_rfbEncodingCompressLevel0, "Compression level");
	m_encodingCaps.Add(rfbEncodingQualityLevel0, rfbTightVncVendor,
					   sig_rfbEncodingQualityLevel0, "JPEG quality level");
	m_encodingCaps.Add(rfbEncodingXCursor, rfbTightVncVendor,
					   sig_rfbEncodingXCursor, "X-style cursor shape update");
	m_encodingCaps.Add(rfbEncodingRichCursor, rfbTightVncVendor,
					   sig_rfbEncodingRichCursor, "Rich-color cursor shape update");
	m_encodingCaps.Add(rfbEncodingPointerPos, rfbTightVncVendor,
					   sig_rfbEncodingPointerPos, "Pointer position update");
	m_encodingCaps.Add(rfbEncodingLastRect, rfbTightVncVendor,
					   sig_rfbEncodingLastRect, "LastRect protocol extension");
	m_encodingCaps.Add(rfbEncodingNewFBSize, rfbTightVncVendor,
					   sig_rfbEncodingNewFBSize, "Framebuffer size change");
	m_encodingCaps.Add(rfbEncodingTransRect, rfbMetaVncVendor,
	                   sig_rfbEncodingTransRect, "Transparent region");
}

// 
// Run() creates the connection if necessary, does the initial negotiations
// and then starts the thread running which does the output (update) processing.
// If Run throws an Exception, the caller must delete the ClientConnection object.
//

void ClientConnection::Run()
{
	// Get the host name and port if we haven't got it

	if (m_port == -1) {
		GetConnectDetails();
	} else {
		if (m_pApp->m_options.m_listening) {
			m_opts.LoadOpt(m_opts.m_display, 
							KEY_VNCVIEWER_HISTORI);
		}
	}

    // If we're not already connected, wait for an incoming connexion
	if (m_sock == INVALID_SOCKET)
	{

       // Create a listening socket
       struct sockaddr_in addr;
       memset(&addr, 0, sizeof(addr));

       addr.sin_family = AF_INET;
       
       // Let the system choose a free port
       addr.sin_port = 0;
       /* Listen on loopback only, since we want only the
        * ktlstunnel process to connect to us */
       addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
       //addr.sin_addr.s_addr = htonl(INADDR_ANY);
       m_sock = socket(AF_INET, SOCK_STREAM, 0);
       if (!m_sock)
               throw WarningException("Error creating client's listening socket");

       int one = 1, res = 0;

       res = bind(m_sock, (struct sockaddr *)&addr, sizeof(addr));
       if (res == SOCKET_ERROR) {
               closesocket(m_sock);
               m_sock = 0;

               char outStr[1000];
               memset(outStr, 0, sizeof(outStr));

               sprintf(outStr, "Error binding client's listening socket, error = %i", WSAGetLastError());
               throw WarningException(outStr);
       }

       res = listen(m_sock, 5);
       if (res == SOCKET_ERROR) {
               closesocket(m_sock);
               m_sock = 0;
               throw WarningException("Error when client's listening socket listen");

       }

       

       int name_len = sizeof(addr);
       if (getsockname(m_sock, (struct sockaddr *) &addr, &name_len) < 0)
           return;

       // Write to the registry the free port we found
        HKEY kwmKey;
        DWORD port = (DWORD) htons(addr.sin_port);
        DWORD portWritten = 1;

        if(RegOpenKeyEx(HKEY_CURRENT_USER, "Software\\teambox\\kcs\\kwm", 0, KEY_WRITE, &kwmKey) == ERROR_SUCCESS)
        {
            if(!(RegSetValueEx(kwmKey, "ListeningPortForVNC", 0, REG_DWORD, reinterpret_cast<BYTE *>(&port),sizeof(port)) == ERROR_SUCCESS &&
               RegSetValueEx(kwmKey, "ListeningPortForVNCWritten", 0, REG_DWORD, reinterpret_cast<BYTE *>(&portWritten), sizeof(portWritten)) == ERROR_SUCCESS))
            {
                throw ErrorException("Error writing client's listening port.");
            }
        }

        if(kwmKey != NULL)
        {
            RegCloseKey(kwmKey);
        }

       SOCKET hNewSock;
       hNewSock = accept(m_sock, NULL, NULL);

       if (hNewSock == SOCKET_ERROR) {
               closesocket(m_sock);
               m_sock = 0;
               throw WarningException("Error when client's listening socket listen");
       }

       unsigned long nbarg = 0;
       /* Set socket to non-blocking */
       ioctlsocket(hNewSock, FIONBIO, &nbarg);

       closesocket(m_sock);
       m_sock = hNewSock;
    }

	SetSocketOptions();

	NegotiateProtocolVersion();

	PerformAuthentication();

	// Set up windows etc 
	CreateDisplay();

	SendClientInit();
	ReadServerInit();

	// Only for protocol version 3.7t
	if (m_tightVncProtocol) {
		// Determine which protocol messages and encodings are supported.
		ReadInteractionCaps();
		// Enable file transfers only if the server supports that.
		m_enableFileTransfers = false;
		if ( m_clientMsgCaps.IsEnabled(rfbFileListRequest) &&
			 m_serverMsgCaps.IsEnabled(rfbFileListData) ) {
			m_enableFileTransfers = true;
		}

		// Enable Key Focus message if the server supports this feature.
		m_enableKeyFocus = m_clientMsgCaps.IsEnabled(rfbKeyFocus);
        // Enable TransRect if the server supports this feature.
        m_enableTransRect = m_encodingCaps.IsEnabled(rfbEncodingTransRect);
        // Enable MetaWM if the server supports this feature.
        m_enableMetaWM =
             (m_clientMsgCaps.IsEnabled(rfbWindowControl) &&
			  m_clientMsgCaps.IsEnabled(rfbServerDataReq) &&
			  m_serverMsgCaps.IsEnabled(rfbWindowState) &&
			  m_serverMsgCaps.IsEnabled(rfbServerData));
        // Initialize Meta Window Manager
        if (m_enableMetaWM) {
            m_metaTaskbar  = new MetaTaskbar(this, m_pApp);
            m_metaLauncher = new MetaLauncher(this);
        }
	}
    EnableAction(ID_TRANSRECT, m_enableTransRect);
    CheckAction(ID_TRANSRECT, m_opts.m_UseTransRect);
    CheckAction(ID_DRAGDESKTOP, m_opts.m_dragDesktop);

	// Close the "Connecting..." dialog box if not closed yet.
	if (m_connDlg != NULL) {
		delete m_connDlg;
		m_connDlg = NULL;
	}

	EnableFullControlOptions();

	CreateLocalFramebuffer();
	
	SetupPixelFormat();
	
	SetFormatAndEncodings();

	// This starts the worker thread.
	// The rest of the processing continues in run_undetached.
	start_undetached();
}

static WNDCLASS wndclass;	// FIXME!

void ClientConnection::CreateDisplay() 
{
	// Create the window
	
	WNDCLASS wndclass;
	
	wndclass.style			= 0;
	wndclass.lpfnWndProc	= ClientConnection::Proc;
	wndclass.cbClsExtra		= 0;
	wndclass.cbWndExtra		= 0;
	wndclass.hInstance		= m_pApp->m_instance;
	wndclass.hIcon			= (HICON)LoadIcon(m_pApp->m_instance,
												MAKEINTRESOURCE(IDI_MAINICON));
	wndclass.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wndclass.hbrBackground	= (HBRUSH) GetSysColorBrush(COLOR_BTNFACE);
    wndclass.lpszMenuName	= (LPCTSTR)NULL;
	wndclass.lpszClassName	= VWR_WND_CLASS_NAME;

	RegisterClass(&wndclass);

	wndclass.style			= 0;
	wndclass.lpfnWndProc	= ClientConnection::ScrollProc;
	wndclass.cbClsExtra		= 0;
	wndclass.cbWndExtra		= 0;
	wndclass.hInstance		= m_pApp->m_instance;
	wndclass.hIcon			= NULL;
	wndclass.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wndclass.hbrBackground	= (HBRUSH) GetStockObject(BLACK_BRUSH);
    wndclass.lpszMenuName	= (LPCTSTR)NULL;
	wndclass.lpszClassName	= "ScrollClass";

	RegisterClass(&wndclass);
	 
	wndclass.style			= 0;
	wndclass.lpfnWndProc	= ClientConnection::Proc;
	wndclass.cbClsExtra		= 0;
	wndclass.cbWndExtra		= 0;
	wndclass.hInstance		= m_pApp->m_instance;
	wndclass.hIcon			= NULL;
	switch (m_pApp->m_options.m_localCursor) {
	case NOCURSOR:
		wndclass.hCursor	= LoadCursor(m_pApp->m_instance, 
										MAKEINTRESOURCE(IDC_NOCURSOR));
		break;
	case SMALLCURSOR:
		wndclass.hCursor	= LoadCursor(m_pApp->m_instance, 
										MAKEINTRESOURCE(IDC_SMALLDOT));
		break;
	case NORMALCURSOR:
		wndclass.hCursor	=LoadCursor(NULL,IDC_ARROW);
		break;
	case DOTCURSOR:
	default:
		wndclass.hCursor	= LoadCursor(m_pApp->m_instance, 
										MAKEINTRESOURCE(IDC_DOTCURSOR));
	}
	wndclass.hbrBackground	= (HBRUSH) GetStockObject(BLACK_BRUSH);
    wndclass.lpszMenuName	= (LPCTSTR)NULL;
	wndclass.lpszClassName	= "ChildClass";

	RegisterClass(&wndclass);
	
	m_hwnd1 = CreateWindow(VWR_WND_CLASS_NAME,
			      _T("VNCviewer"),
			      WS_BORDER|WS_CAPTION|WS_SYSMENU|WS_SIZEBOX|
				  WS_MINIMIZEBOX|WS_MAXIMIZEBOX|
				  WS_CLIPCHILDREN,
			      CW_USEDEFAULT,
			      CW_USEDEFAULT,
			      CW_USEDEFAULT,       // x-size
			      CW_USEDEFAULT,       // y-size
			      NULL,                // Parent handle
			      NULL,                // Menu handle
			      m_pApp->m_instance,
			      NULL);
	SetWindowLong(m_hwnd1, GWL_USERDATA, (LONG) this);
	SetWindowLong(m_hwnd1, GWL_WNDPROC, (LONG)ClientConnection::WndProc1);
	ShowWindow(m_hwnd1, SW_HIDE);

	// if use IME, disable input IME.
	WINNLSEnableIME(m_hwnd1, FALSE);

	m_hwndscroll = CreateWindow("ScrollClass",
			      NULL,
			      WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_BORDER,
			      CW_USEDEFAULT,
			      CW_USEDEFAULT,
			      CW_USEDEFAULT,       // x-size
			      CW_USEDEFAULT,       // y-size
			      m_hwnd1,                // Parent handle
			      NULL,                // Menu handle
			      m_pApp->m_instance,
			      NULL);
	SetWindowLong(m_hwndscroll, GWL_USERDATA, (LONG) this);
	ShowWindow(m_hwndscroll, SW_HIDE);
	
	// Create a memory DC which we'll use for drawing to
	// the local framebuffer
	m_hBitmapDC = CreateCompatibleDC(NULL);

	// Set a suitable palette up
	if (GetDeviceCaps(m_hBitmapDC, RASTERCAPS) & RC_PALETTE) {
		vnclog.Print(3, _T("Palette-based display - %d entries, %d reserved\n"), 
			GetDeviceCaps(m_hBitmapDC, SIZEPALETTE), GetDeviceCaps(m_hBitmapDC, NUMRESERVED));
		BYTE buf[sizeof(LOGPALETTE)+216*sizeof(PALETTEENTRY)];
		LOGPALETTE *plp = (LOGPALETTE *) buf;
		int pepos = 0;
		for (int r = 5; r >= 0; r--) {
			for (int g = 5; g >= 0; g--) {
				for (int b = 5; b >= 0; b--) {
					plp->palPalEntry[pepos].peRed   = r * 255 / 5; 	
					plp->palPalEntry[pepos].peGreen = g * 255 / 5;
					plp->palPalEntry[pepos].peBlue  = b * 255 / 5;
					plp->palPalEntry[pepos].peFlags  = NULL;
					pepos++;
				}
			}
		}
		plp->palVersion = 0x300;
		plp->palNumEntries = 216;
		m_hPalette = CreatePalette(plp);
	}

	// Add stuff to System menu
	HMENU hsysmenu = GetSystemMenu(m_hwnd1, FALSE);
	if (!m_opts.m_restricted) {
		bool save_item_flags = (m_serverInitiated) ? MF_GRAYED : 0;
		AppendMenu(hsysmenu, MF_SEPARATOR, NULL, NULL);
		AppendMenu(hsysmenu, MF_STRING, IDC_OPTIONBUTTON,
				   _T("Connection &options...\tCtrl-Alt-Shift-O"));
		AppendMenu(hsysmenu, MF_STRING, ID_CONN_ABOUT,
				   _T("Connection &info\tCtrl-Alt-Shift-I"));
		AppendMenu(hsysmenu, MF_STRING, ID_REQUEST_REFRESH,
				   _T("Request screen &refresh\tCtrl-Alt-Shift-R"));
		AppendMenu(hsysmenu, MF_SEPARATOR, NULL, NULL);
		AppendMenu(hsysmenu, MF_STRING, ID_FULLSCREEN,
				   _T("&Full screen\tCtrl-Alt-Shift-F"));
		AppendMenu(hsysmenu, MF_STRING, ID_TOOLBAR,
				   _T("Show &toolbar\tCtrl-Alt-Shift-T"));
		AppendMenu(hsysmenu, MF_SEPARATOR, NULL, NULL);
		AppendMenu(hsysmenu, MF_STRING, ID_CONN_CTLALTDEL,
				   _T("Send Ctrl-Alt-&Del"));
		AppendMenu(hsysmenu, MF_STRING, ID_CONN_CTLESC,
				   _T("Send Ctrl-Esc"));
		AppendMenu(hsysmenu, MF_STRING, ID_CONN_CTLDOWN,
				   _T("Ctrl key down"));
		AppendMenu(hsysmenu, MF_STRING, ID_CONN_ALTDOWN,
				   _T("Alt key down"));
		AppendMenu(hsysmenu, MF_SEPARATOR, NULL, NULL);
		AppendMenu(hsysmenu, MF_STRING | MF_GRAYED, IDD_FILETRANSFER,
				   _T("Transf&er files...\tCtrl-Alt-Shift-E"));
		AppendMenu(hsysmenu, MF_SEPARATOR, NULL, NULL);
		AppendMenu(hsysmenu, MF_STRING, ID_NEWCONN,
				   _T("&New connection...\tCtrl-Alt-Shift-N"));
		AppendMenu(hsysmenu, save_item_flags, ID_CONN_SAVE_AS,
				   _T("&Save connection info as...\tCtrl-Alt-Shift-S"));
		AppendMenu(hsysmenu, MF_SEPARATOR, NULL, NULL);
		AppendMenu(hsysmenu, MF_STRING, ID_TRANSRECT,
                   _T("Transparent background"));
		AppendMenu(hsysmenu, MF_STRING, ID_DRAGDESKTOP,
                   _T("Enable dragging desktop"));
	}

	AppendMenu(hsysmenu, MF_SEPARATOR, NULL, NULL);
	AppendMenu(hsysmenu, MF_STRING, IDD_APP_ABOUT,
			   _T("&About VNC Viewer..."));
	if (m_opts.m_listening) {
		AppendMenu(hsysmenu, MF_SEPARATOR, NULL, NULL);
		AppendMenu(hsysmenu, MF_STRING, ID_CLOSEDAEMON,
				   _T("Close &listening daemon"));
	}
	DrawMenuBar(m_hwnd1);

	m_hToolbar = CreateToolbar();

    /* update menu and toolbar states */
    CheckAction(ID_TRANSRECT, m_opts.m_UseTransRect);
    CheckAction(ID_DRAGDESKTOP, m_opts.m_dragDesktop);
	m_hwnd = CreateWindow("ChildClass",
			      NULL,
			      WS_CHILD | WS_CLIPSIBLINGS,
			      CW_USEDEFAULT,
			      CW_USEDEFAULT,
			      CW_USEDEFAULT,       // x-size
			      CW_USEDEFAULT,	   // y-size
			      m_hwndscroll,             // Parent handle
			      NULL,                // Menu handle
			      m_pApp->m_instance,
			      NULL);
	m_opts.m_hWindow = m_hwnd;
	hotkeys.SetWindow(m_hwnd1);
    ShowWindow(m_hwnd, SW_HIDE);
		
	SetWindowLong(m_hwnd, GWL_USERDATA, (LONG) this);
	SetWindowLong(m_hwnd, GWL_WNDPROC, (LONG)ClientConnection::WndProc);
	
	if(pApp->m_options.m_toolbar) {
		CheckMenuItem(GetSystemMenu(m_hwnd1, FALSE),
					ID_TOOLBAR, MF_BYCOMMAND|MF_CHECKED);
	}
	SaveListConnection();

    // create transparent region handle
    m_hTransRgn = CreateRectRgn(0, 0, 0, 0);
	
	// record which client created this window
	
#ifndef _WIN32_WCE
	// We want to know when the clipboard changes, so
	// insert ourselves in the viewer chain. But doing
	// this will cause us to be notified immediately of
	// the current state.
	// We don't want to send that.
	m_initialClipboardSeen = false;
	m_hwndNextViewer = SetClipboardViewer(m_hwnd);
#endif
}

HWND ClientConnection::CreateToolbar()
{
	const int MAX_TOOLBAR_BUTTONS = 20;
	TBBUTTON but[MAX_TOOLBAR_BUTTONS];
	memset(but, 0, sizeof(but));
	int i = 0;

	but[i].iBitmap		= 0;
	but[i].idCommand	= IDC_OPTIONBUTTON;
	but[i].fsState		= TBSTATE_ENABLED;
	but[i++].fsStyle	= TBSTYLE_BUTTON;

	but[i].iBitmap		= 1;
	but[i].idCommand	= ID_CONN_ABOUT;
	but[i].fsState		= TBSTATE_ENABLED;
	but[i++].fsStyle	= TBSTYLE_BUTTON;

	but[i++].fsStyle	= TBSTYLE_SEP;

	but[i].iBitmap		= 2;
	but[i].idCommand	= ID_FULLSCREEN;
	but[i].fsState		= TBSTATE_ENABLED;
	but[i++].fsStyle	= TBSTYLE_BUTTON;

	but[i].iBitmap		= 3;
	but[i].idCommand	= ID_REQUEST_REFRESH;
	but[i].fsState		= TBSTATE_ENABLED;
	but[i++].fsStyle	= TBSTYLE_BUTTON;

	but[i++].fsStyle	= TBSTYLE_SEP;

	but[i].iBitmap		= 4;
	but[i].idCommand	= ID_CONN_CTLALTDEL;
	but[i].fsState		= TBSTATE_ENABLED;
	but[i++].fsStyle	= TBSTYLE_BUTTON;

	but[i].iBitmap		= 5;
	but[i].idCommand	= ID_CONN_CTLESC;
	but[i].fsState		= TBSTATE_ENABLED;
	but[i++].fsStyle	= TBSTYLE_BUTTON;

	but[i].iBitmap		= 6;
	but[i].idCommand	= ID_CONN_CTLDOWN;
	but[i].fsState		= TBSTATE_ENABLED;
	but[i++].fsStyle	= TBSTYLE_CHECK;

	but[i].iBitmap		= 7;
	but[i].idCommand	= ID_CONN_ALTDOWN;
	but[i].fsState		= TBSTATE_ENABLED;
	but[i++].fsStyle	= TBSTYLE_CHECK;

	but[i++].fsStyle	= TBSTYLE_SEP;

	but[i].iBitmap		= 8;
	but[i].idCommand	= IDD_FILETRANSFER;
	but[i].fsState		= TBSTATE_INDETERMINATE;
	but[i++].fsStyle	= TBSTYLE_BUTTON;

	but[i++].fsStyle	= TBSTYLE_SEP;

	but[i].iBitmap		= 9;
	but[i].idCommand	= ID_NEWCONN;
	but[i].fsState		= TBSTATE_ENABLED;
	but[i++].fsStyle	= TBSTYLE_BUTTON;

	but[i].iBitmap		= 10;
	but[i].idCommand	= ID_CONN_SAVE_AS;
	but[i].fsState		= TBSTATE_ENABLED;
	but[i++].fsStyle	= TBSTYLE_BUTTON;

	but[i].iBitmap		= 11;
	but[i].idCommand	= ID_DISCONNECT;
	but[i].fsState		= TBSTATE_ENABLED;
	but[i++].fsStyle	= TBSTYLE_BUTTON;

	but[i++].fsStyle	= TBSTYLE_SEP;

	but[i].iBitmap		= 12;
	but[i].idCommand	= ID_TRANSRECT;
	but[i].fsState		= TBSTATE_INDETERMINATE;
	but[i++].fsStyle	= TBSTYLE_CHECK;

	int numButtons = i;
	assert(numButtons <= MAX_TOOLBAR_BUTTONS);

	HWND hwndToolbar = CreateToolbarEx(m_hwnd1,
		WS_CHILD | TBSTYLE_TOOLTIPS | 
		WS_CLIPSIBLINGS | TBSTYLE_FLAT,
		ID_TOOLBAR, 13, m_pApp->m_instance,
		IDB_BITMAP1, but, numButtons, 0, 0, 0, 0, sizeof(TBBUTTON));

	if (hwndToolbar != NULL)
		SendMessage(hwndToolbar, TB_SETINDENT, 4, 0);

	return hwndToolbar;
}

void ClientConnection::SaveListConnection()
{
	if (!m_serverInitiated) {
		TCHAR  valname[3];
		int dwbuflen = 255;
		int i, j;
		int maxEntries = pApp->m_options.m_historyLimit;
		TCHAR list[80];
		HKEY m_hRegKey;
		TCHAR  buf[256];
		DWORD dispos;
		TCHAR  buf1[256];
				
		itoa(maxEntries, list, 10);
		RegCreateKeyEx(HKEY_CURRENT_USER,
					KEY_VNCVIEWER_HISTORI, 0, NULL, 
					REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, 
					NULL, &m_hRegKey, &dispos);
		_tcscpy(buf1, m_opts.m_display);
							
		for ( i = 0; i < maxEntries; i++) {
			j = i;
			itoa(i, valname, 10);
			dwbuflen = 255;
			if ((RegQueryValueEx( m_hRegKey, (LPTSTR)valname, 
						NULL, NULL, 
						(LPBYTE) buf, (LPDWORD) &dwbuflen) != ERROR_SUCCESS) ||
						(_tcscmp(buf, m_opts.m_display) == NULL)) {
				RegSetValueEx( m_hRegKey, valname, 
							NULL, REG_SZ, 
							(CONST BYTE *)buf1, (_tcslen(buf1)+1));
				break;
			}
			RegSetValueEx(m_hRegKey, valname, 
						NULL, REG_SZ, 
						(CONST BYTE *)buf1, (_tcslen(buf1)+1)); 
				_tcscpy(buf1, buf);
		}
		if (j == maxEntries) {
			dwbuflen = 255;
			_tcscpy(valname, list);
			_tcscpy(buf, "");
			RegQueryValueEx( m_hRegKey, (LPTSTR)valname, 
						NULL, NULL, 
						(LPBYTE)buf, (LPDWORD)&dwbuflen);
			m_opts.delkey(buf, KEY_VNCVIEWER_HISTORI);
		}
		RegCloseKey(m_hRegKey);
		m_opts.SaveOpt(m_opts.m_display,
						KEY_VNCVIEWER_HISTORI);
	}		
}

void ClientConnection::EnableFullControlOptions()
{
	if (m_opts.m_ViewOnly) {
		SwitchOffKey();
		EnableAction(IDD_FILETRANSFER, false);
		EnableAction(ID_CONN_CTLALTDEL, false);
		EnableAction(ID_CONN_CTLDOWN, false);
		EnableAction(ID_CONN_ALTDOWN, false);
		EnableAction(ID_CONN_CTLESC, false);
	} else {
		EnableAction(IDD_FILETRANSFER, m_enableFileTransfers);
		EnableAction(ID_CONN_CTLALTDEL, true);
		EnableAction(ID_CONN_CTLDOWN, true);
		EnableAction(ID_CONN_ALTDOWN, true);
		EnableAction(ID_CONN_CTLESC, true);
	}
}

void ClientConnection::EnableAction(int id, bool enable)
{
	if (enable) {
		EnableMenuItem(GetSystemMenu(m_hwnd1, FALSE), id,
					   MF_BYCOMMAND | MF_ENABLED);
		EnableMenuItem(m_htraymenu, id,
					   MF_BYCOMMAND | MF_ENABLED);
		SendMessage(m_hToolbar, TB_SETSTATE, (WPARAM)id,
					(LPARAM)MAKELONG(TBSTATE_ENABLED, 0));
	} else {
		EnableMenuItem(GetSystemMenu(m_hwnd1, FALSE), id,
					   MF_BYCOMMAND | MF_GRAYED);
		EnableMenuItem(m_htraymenu, id,
					   MF_BYCOMMAND | MF_GRAYED);
		SendMessage(m_hToolbar, TB_SETSTATE, (WPARAM)id,
					(LPARAM)MAKELONG(TBSTATE_INDETERMINATE, 0));
	}
}

void ClientConnection::CheckAction(int id, bool checked)
{
	if (checked) {
		CheckMenuItem(GetSystemMenu(m_hwnd1, FALSE), id,
                      MF_BYCOMMAND | MF_CHECKED);
		CheckMenuItem(m_htraymenu, id,
                      MF_BYCOMMAND | MF_CHECKED);
		SendMessage(m_hToolbar, TB_SETSTATE, (WPARAM)id,
					(LPARAM)MAKELONG(TBSTATE_ENABLED | TBSTATE_CHECKED, 0));
	} else {
		CheckMenuItem(GetSystemMenu(m_hwnd1, FALSE), id,
                      MF_BYCOMMAND | MF_UNCHECKED);
		CheckMenuItem(m_htraymenu, id,
                      MF_BYCOMMAND | MF_UNCHECKED);
		SendMessage(m_hToolbar, TB_SETSTATE, (WPARAM)id,
					(LPARAM)MAKELONG(TBSTATE_ENABLED, 0));
	}
}

void ClientConnection::SwitchOffKey()
{
    CheckAction(ID_CONN_ALTDOWN, false);
    CheckAction(ID_CONN_CTLDOWN, false);
	SendKeyEvent(XK_Alt_L,     false);
	SendKeyEvent(XK_Control_L, false);
	SendKeyEvent(XK_Shift_L,   false);
	SendKeyEvent(XK_Alt_R,     false);
	SendKeyEvent(XK_Control_R, false);
	SendKeyEvent(XK_Shift_R,   false);
}

void ClientConnection::GetConnectDetails()
{
	
	if (m_opts.m_configSpecified) {
		LoadConnection(m_opts.m_configFilename, false);
	} else {
		SessionDialog sessdlg(&m_opts, this);
		if (!sessdlg.DoDialog()) {
			throw QuietException("User Cancelled");
		}		
	}
	// This is a bit of a hack: 
	// The config file may set various things in the app-level defaults which 
	// we don't want to be used except for the first connection. So we clear them
	// in the app defaults here.
	m_pApp->m_options.m_host[0] = '\0';
	m_pApp->m_options.m_port = -1;
	m_pApp->m_options.m_connectionSpecified = false;
	m_pApp->m_options.m_configSpecified = false;
#ifndef _WIN32_WCE
	// We want to know when the clipboard changes, so
	// insert ourselves in the viewer chain. But doing
	// this will cause us to be notified immediately of
	// the current state.
	// We don't want to send that.
	m_initialClipboardSeen = false;
	m_hwndNextViewer = SetClipboardViewer(m_hwnd); 	
#endif
}

void ClientConnection::Connect()
{
	struct sockaddr_in thataddr;
	int res;

	if (m_connDlg != NULL)
		m_connDlg->SetStatus("Connection initiated");

	m_sock = socket(PF_INET, SOCK_STREAM, 0);
	if (m_sock == INVALID_SOCKET) throw WarningException(_T("Error creating socket"));
	int one = 1;
	
	// The host may be specified as a dotted address "a.b.c.d"
	// Try that first
	thataddr.sin_addr.s_addr = inet_addr(m_host);
	
	// If it wasn't one of those, do gethostbyname
	if (thataddr.sin_addr.s_addr == INADDR_NONE) {
		LPHOSTENT lphost;
		lphost = gethostbyname(m_host);
		
		if (lphost == NULL) { 
			char msg[512];
			sprintf(msg, "Failed to get server address (%s).\n"
					"Did you type the host name correctly?", m_host);
			throw WarningException(msg);
		};
		thataddr.sin_addr.s_addr = ((LPIN_ADDR) lphost->h_addr)->s_addr;
	};
	
	thataddr.sin_family = AF_INET;
	thataddr.sin_port = htons(m_port);
	res = connect(m_sock, (LPSOCKADDR) &thataddr, sizeof(thataddr));
	if (res == SOCKET_ERROR) {
		char msg[512];
		sprintf(msg, "Failed to connect to server (%.255s)", m_opts.m_display);
		throw WarningException(msg);
	}
	vnclog.Print(0, _T("Connected to %s port %d\n"), m_host, m_port);

	if (m_connDlg != NULL)
		m_connDlg->SetStatus("Connection established");
}

void ClientConnection::SetSocketOptions() {
	// Disable Nagle's algorithm
	BOOL nodelayval = TRUE;
	if (setsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY, (const char *) &nodelayval, sizeof(BOOL)))
		throw WarningException("Error disabling Nagle's algorithm");
}


void ClientConnection::NegotiateProtocolVersion()
{
	rfbProtocolVersionMsg pv;

	ReadExact(pv, sz_rfbProtocolVersionMsg);

    pv[sz_rfbProtocolVersionMsg] = 0;

	if (m_connDlg != NULL)
		m_connDlg->SetStatus("Server protocol version received");

	// XXX This is a hack.  Under CE we just return to the server the
	// version number it gives us without parsing it.  
	// Too much hassle replacing sscanf for now. Fix this!
#ifdef UNDER_CE
	m_minorVersion = 8;
#else
	int majorVersion, minorVersion;
	if (sscanf(pv, rfbProtocolVersionFormat, &majorVersion, &minorVersion) != 2) {
		throw WarningException(_T("Invalid protocol"));
	}
	vnclog.Print(0, _T("RFB server supports protocol version 3.%d\n"),
				 minorVersion);

	if (majorVersion == 3 && minorVersion >= 8) {
		m_minorVersion = 8;
	} else if (majorVersion == 3 && minorVersion == 7) {
		m_minorVersion = 7;
	} else {
		m_minorVersion = 3;
	}

	m_tightVncProtocol = false;

    sprintf(pv, rfbProtocolVersionFormat, 3, m_minorVersion);
#endif

    WriteExact(pv, sz_rfbProtocolVersionMsg);

	if (m_connDlg != NULL)
		m_connDlg->SetStatus("Protocol version negotiated");

	vnclog.Print(0, _T("Connected to RFB server, using protocol version 3.%d\n"),
				 m_minorVersion);
}

//
// Negotiate authentication scheme and authenticate if necessary
//

void ClientConnection::PerformAuthentication()
{
	int secType;
	if (m_minorVersion >= 7) {
		secType = SelectSecurityType();
	} else {
		secType = ReadSecurityType();
	}

	switch (secType) {
    case rfbSecTypeNone:
		Authenticate(rfbAuthNone);
		m_authScheme = rfbAuthNone;
		break;
    case rfbSecTypeVncAuth:
		Authenticate(rfbAuthVNC);
		m_authScheme = rfbAuthVNC;
		break;
    case rfbSecTypeTight:
		m_tightVncProtocol = true;
		InitCapabilities();
		SetupTunneling();
		PerformAuthenticationTight();
		break;
	default:	// should never happen
		vnclog.Print(0, _T("Internal error: Invalid security type\n"));
		throw ErrorException("Internal error: Invalid security type");
    }
}

//
// Read security type from the server (protocol 3.3)
//

int ClientConnection::ReadSecurityType()
{
	// Read the authentication scheme.
	CARD32 secType;
	ReadExact((char *)&secType, sizeof(secType));
	secType = Swap32IfLE(secType);

    if (secType == rfbSecTypeInvalid)
		throw WarningException(ReadFailureReason());

	if (secType != rfbSecTypeNone && secType != rfbSecTypeVncAuth) {
		vnclog.Print(0, _T("Unknown security type from RFB server: %d\n"),
					 (int)secType);
		throw ErrorException("Unknown security type requested!");
    }

	if (m_connDlg != NULL)
		m_connDlg->SetStatus("Security type received");

	return (int)secType;
}

//
// Select security type from the server's list (protocol 3.7 and above)
//

int ClientConnection::SelectSecurityType()
{
	// Read the list of secutiry types.
	CARD8 nSecTypes;
	ReadExact((char *)&nSecTypes, sizeof(nSecTypes));
	if (nSecTypes == 0)
		throw WarningException(ReadFailureReason());

	char *secTypeNames[] = {"None", "VncAuth"};
	CARD8 knownSecTypes[] = {rfbSecTypeNone, rfbSecTypeVncAuth};
	int nKnownSecTypes = sizeof(knownSecTypes);
	CARD8 *secTypes = new CARD8[nSecTypes];
	ReadExact((char *)secTypes, nSecTypes);
	CARD8 secType = rfbSecTypeInvalid;

	if (m_connDlg != NULL)
		m_connDlg->SetStatus("List of security types received");

	// Find out if the server supports TightVNC protocol extensions
	int j;
	for (j = 0; j < (int)nSecTypes; j++) {
		if (secTypes[j] == rfbSecTypeTight) {
			secType = rfbSecTypeTight;
			WriteExact((char *)&secType, sizeof(secType));
			if (m_connDlg != NULL)
				m_connDlg->SetStatus("TightVNC protocol extensions enabled");
			vnclog.Print(8, _T("Enabling TightVNC protocol extensions\n"));
			return rfbSecTypeTight;
		}
	}

	// Find first supported security type
	for (j = 0; j < (int)nSecTypes; j++) {
		for (int i = 0; i < nKnownSecTypes; i++) {
			if (secTypes[j] == knownSecTypes[i]) {
				secType = secTypes[j];
				WriteExact((char *)&secType, sizeof(secType));
				if (m_connDlg != NULL)
					m_connDlg->SetStatus("Security type requested");
				vnclog.Print(8, _T("Choosing security type %s(%d)\n"),
							 secTypeNames[i], (int)secType);
				break;
			}
		}
		if (secType != rfbSecTypeInvalid) break;
    }

    if (secType == rfbSecTypeInvalid) {
		vnclog.Print(0, _T("Server did not offer supported security type\n"));
		throw ErrorException("Server did not offer supported security type!");
	}

	return (int)secType;
}

//
// Setup tunneling (protocol 3.7t, 3.8t)
//

void ClientConnection::SetupTunneling()
{
	rfbTunnelingCapsMsg caps;
	ReadExact((char *)&caps, sz_rfbTunnelingCapsMsg);
	caps.nTunnelTypes = Swap32IfLE(caps.nTunnelTypes);

	if (caps.nTunnelTypes) {
		ReadCapabilityList(&m_tunnelCaps, caps.nTunnelTypes);
		if (m_connDlg != NULL)
			m_connDlg->SetStatus("List of tunneling capabilities received");

		// We cannot do tunneling yet.
		CARD32 tunnelType = Swap32IfLE(rfbNoTunneling);
		WriteExact((char *)&tunnelType, sizeof(tunnelType));
		if (m_connDlg != NULL)
			m_connDlg->SetStatus("Tunneling type requested");
	}
}

//
// Negotiate authentication scheme (protocol 3.7t, 3.8t)
//

void ClientConnection::PerformAuthenticationTight()
{
	rfbAuthenticationCapsMsg caps;
	ReadExact((char *)&caps, sz_rfbAuthenticationCapsMsg);
	caps.nAuthTypes = Swap32IfLE(caps.nAuthTypes);

	if (m_connDlg != NULL)
		m_connDlg->SetStatus("Header of authentication capability list received");

	if (!caps.nAuthTypes) {
		vnclog.Print(0, _T("No authentication needed\n"));
		if (m_connDlg != NULL)
			m_connDlg->SetStatus("No authentication needed");
		Authenticate(rfbAuthNone);
		m_authScheme = rfbAuthNone;
	} else {
		ReadCapabilityList(&m_authCaps, caps.nAuthTypes);
		if (m_connDlg != NULL)
			m_connDlg->SetStatus("Authentication capability list received");
		if (!m_authCaps.NumEnabled()) {
			vnclog.Print(0, _T("No suitable authentication schemes offered by the server\n"));
			throw ErrorException("No suitable authentication schemes offered by the server");
		}

		// Use server's preferred authentication scheme.
		CARD32 authScheme = m_authCaps.GetByOrder(0);
		authScheme = Swap32IfLE(authScheme);
		WriteExact((char *)&authScheme, sizeof(authScheme));
		authScheme = Swap32IfLE(authScheme);	// convert it back
		if (m_connDlg != NULL)
			m_connDlg->SetStatus("Authentication scheme requested");
		Authenticate(authScheme);
		m_authScheme = authScheme;
	}
}

// The definition of a function implementing some authentication scheme.
// For an example, see ClientConnection::AuthenticateVNC, below.

typedef bool (ClientConnection::*AuthFunc)(char *, int);

// A wrapper function for different authentication schemes.

void ClientConnection::Authenticate(CARD32 authScheme)
{
	AuthFunc authFuncPtr;

	// Uncomment this if the "Connecting..." dialog box should be
	// closed prior to authentication.
	/***
	if (m_connDlg != NULL) {
		delete m_connDlg;
		m_connDlg = NULL;
	}
	***/

	switch(authScheme) {
	case rfbAuthNone:
		authFuncPtr = &ClientConnection::AuthenticateNone;
		break;
	case rfbAuthVNC:
		authFuncPtr = &ClientConnection::AuthenticateVNC;
		break;
	default:
		vnclog.Print(0, _T("Unknown authentication scheme: %d\n"),
					 (int)authScheme);
		throw ErrorException("Unknown authentication scheme!");
	}

	vnclog.Print(0, _T("Authentication scheme: %s\n"),
				 m_authCaps.GetDescription(authScheme));

	const int errorMsgSize = 256;
	CheckBufferSize(errorMsgSize);
	char *errorMsg = m_netbuf;
	bool wasError = !(this->*authFuncPtr)(errorMsg, errorMsgSize);

	// Report authentication error.
	if (wasError) {
		vnclog.Print(0, _T("%s\n"), errorMsg);
		if (m_connDlg != NULL)
			m_connDlg->SetStatus("Error during authentication");
		throw AuthException(errorMsg);
	}

	CARD32 authResult;
	if (authScheme == rfbAuthNone && m_minorVersion < 8) {
		// In protocol versions prior to 3.8, "no authentication" is a
		// special case - no "security result" is sent by the server.
		authResult = rfbAuthOK;
	} else {
		ReadExact((char *) &authResult, 4);
		authResult = Swap32IfLE(authResult);
	}

	switch (authResult) {
	case rfbAuthOK:
		if (m_connDlg != NULL)
			m_connDlg->SetStatus("Authentication successful");
		vnclog.Print(0, _T("Authentication successful\n"));
		return;
	case rfbAuthFailed:
		if (m_minorVersion >= 8) {
			errorMsg = ReadFailureReason();
		} else {
			errorMsg = "Authentication failure";
		}
		break;
	case rfbAuthTooMany:
		errorMsg = "Authentication failure, too many tries";
		break;
	default:
		_snprintf(m_netbuf, 256, "Unknown authentication result (%d)",
				 (int)authResult);
		errorMsg = m_netbuf;
		break;
	}

	// Report authentication failure.
	vnclog.Print(0, _T("%s\n"), errorMsg);
	if (m_connDlg != NULL)
		m_connDlg->SetStatus(errorMsg);
	throw AuthException(errorMsg);
}

// "Null" authentication.

bool ClientConnection::AuthenticateNone(char *errBuf, int errBufSize)
{
	return true;
}

// The standard VNC authentication.
//
// An authentication function should return false on error and true if
// the authentication process was successful. Note that returning true
// does not mean that authentication was passed by the server, the
// server's result will be received and analyzed later.
// If false is returned, then a text error message should be copied
// to errorBuf[], no more than errBufSize bytes should be copied into
// that buffer.

bool ClientConnection::AuthenticateVNC(char *errBuf, int errBufSize)
{
    CARD8 challenge[CHALLENGESIZE];
	ReadExact((char *)challenge, CHALLENGESIZE);

	char passwd[MAXPWLEN + 1];
	// Was the password already specified in a config file?
	if (strlen((const char *) m_encPasswd) > 0) {
		char *pw = vncDecryptPasswd(m_encPasswd);
		strcpy(passwd, pw);
		free(pw);
	} else {
		LoginAuthDialog ad(m_opts.m_display, "Standard VNC Authentication");
		ad.DoDialog();
#ifndef UNDER_CE
		strncpy(passwd, ad.m_passwd, MAXPWLEN);
		passwd[MAXPWLEN]= '\0';
#else
		// FIXME: Move wide-character translations to a separate class
		int origlen = _tcslen(ad.m_passwd);
		int newlen = WideCharToMultiByte(
			CP_ACP,    // code page
			0,         // performance and mapping flags
			ad.m_passwd, // address of wide-character string
			origlen,   // number of characters in string
			passwd,    // address of buffer for new string
			255,       // size of buffer
			NULL, NULL);

		passwd[newlen]= '\0';
#endif
		if (strlen(passwd) == 0) {
			_snprintf(errBuf, errBufSize, "Empty password");
			return false;
		}
		if (strlen(passwd) > 8) {
			passwd[8] = '\0';
		}
		vncEncryptPasswd(m_encPasswd, passwd);
	}				

	vncEncryptBytes(challenge, passwd);

	/* Lose the plain-text password from memory */
	memset(passwd, 0, strlen(passwd));

	WriteExact((char *) challenge, CHALLENGESIZE);

	return true;
}

void ClientConnection::SendClientInit()
{
    rfbClientInitMsg ci;
	ci.shared = m_opts.m_Shared;

    WriteExact((char *)&ci, sz_rfbClientInitMsg);

	if (m_connDlg != NULL)
		m_connDlg->SetStatus("Client initialization message sent");
}

void ClientConnection::ReadServerInit()
{
    ReadExact((char *)&m_si, sz_rfbServerInitMsg);
	
	if (m_connDlg != NULL)
		m_connDlg->SetStatus("Server initialization message received");

    m_si.framebufferWidth = Swap16IfLE(m_si.framebufferWidth);
    m_si.framebufferHeight = Swap16IfLE(m_si.framebufferHeight);
    m_si.format.redMax = Swap16IfLE(m_si.format.redMax);
    m_si.format.greenMax = Swap16IfLE(m_si.format.greenMax);
    m_si.format.blueMax = Swap16IfLE(m_si.format.blueMax);
    m_si.nameLength = Swap32IfLE(m_si.nameLength);
	
	m_desktopName = new TCHAR[m_si.nameLength + 2];

#ifdef UNDER_CE
    char *deskNameBuf = new char[m_si.nameLength + 2];
    ReadString(deskNameBuf, m_si.nameLength);
    MultiByteToWideChar( CP_ACP,   MB_PRECOMPOSED,
                            deskNameBuf, m_si.nameLength,
                            m_desktopName, m_si.nameLength+1);
	delete deskNameBuf;	
#else
    ReadString(m_desktopName, m_si.nameLength);

    // MetaVNC
    m_desktopNameW = new wchar_t[m_si.nameLength + 1];
    MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED,
                        m_desktopName, m_si.nameLength,
                        m_desktopNameW, m_si.nameLength + 1);
	/* Because we overrite this */
	delete m_desktopNameW;
#endif
	/* Because we overrite this */
	delete m_desktopName;	

    m_desktopName = "KWM - Application Sharing Viewer";
	m_desktopNameW = L"KWM - Application Sharing Viewer";
    
	SetWindowText(m_hwnd1, m_desktopName);	

	vnclog.Print(0, _T("Desktop name \"%s\"\n"),m_desktopName);
	vnclog.Print(1, _T("Geometry %d x %d depth %d\n"),
		m_si.framebufferWidth, m_si.framebufferHeight, m_si.format.depth );

	SizeWindow(true);
}

//
// In protocols 3.7t/3.8t, the server informs us about supported
// protocol messages and encodings. Here we read this information.
//

void ClientConnection::ReadInteractionCaps()
{
	// Read the counts of list items following
	rfbInteractionCapsMsg intr_caps;
	ReadExact((char *)&intr_caps, sz_rfbInteractionCapsMsg);
	intr_caps.nServerMessageTypes = Swap16IfLE(intr_caps.nServerMessageTypes);
	intr_caps.nClientMessageTypes = Swap16IfLE(intr_caps.nClientMessageTypes);
	intr_caps.nEncodingTypes = Swap16IfLE(intr_caps.nEncodingTypes);
	if (m_connDlg != NULL)
		m_connDlg->SetStatus("Interaction capability list header received");

	// Read the lists of server- and client-initiated messages
	ReadCapabilityList(&m_serverMsgCaps, intr_caps.nServerMessageTypes);
	ReadCapabilityList(&m_clientMsgCaps, intr_caps.nClientMessageTypes);
	ReadCapabilityList(&m_encodingCaps, intr_caps.nEncodingTypes);
	if (m_connDlg != NULL)
		m_connDlg->SetStatus("Interaction capability list received");
}

//
// Read the list of rfbCapabilityInfo structures and enable corresponding
// capabilities in the specified container. The count argument specifies how
// many records to read from the socket.
//

void ClientConnection::ReadCapabilityList(CapsContainer *caps, int count)
{
	rfbCapabilityInfo msginfo;
	for (int i = 0; i < count; i++) {
		ReadExact((char *)&msginfo, sz_rfbCapabilityInfo);
		msginfo.code = Swap32IfLE(msginfo.code);
		caps->Enable(&msginfo);
	}
}

void ClientConnection::SizeWindow(bool centered)
{
	// Find how large the desktop work area is
	RECT workrect;
	SystemParametersInfo(SPI_GETWORKAREA, 0, &workrect, 0);
	int workwidth = workrect.right -  workrect.left;
	int workheight = workrect.bottom - workrect.top;
	vnclog.Print(2, _T("Screen work area is %d x %d\n"),
				 workwidth, workheight);

	RECT fullwinrect;
	
	if (m_opts.m_scaling) {
		SetRect(&fullwinrect, 0, 0,
				m_si.framebufferWidth * m_opts.m_scale_num / m_opts.m_scale_den,
				m_si.framebufferHeight * m_opts.m_scale_num / m_opts.m_scale_den);
	} else {
		SetRect(&fullwinrect, 0, 0,
				m_si.framebufferWidth, m_si.framebufferHeight);
	}	

	AdjustWindowRectEx(&fullwinrect, 
			   GetWindowLong(m_hwnd, GWL_STYLE ), 
			   FALSE, GetWindowLong(m_hwnd, GWL_EXSTYLE));

	m_fullwinwidth = fullwinrect.right - fullwinrect.left;
	m_fullwinheight = fullwinrect.bottom - fullwinrect.top;

	AdjustWindowRectEx(&fullwinrect, 
			   GetWindowLong(m_hwndscroll, GWL_STYLE ) & ~WS_HSCROLL & 
			   ~WS_VSCROLL & ~WS_BORDER, 
			   FALSE, GetWindowLong(m_hwndscroll, GWL_EXSTYLE));
	AdjustWindowRectEx(&fullwinrect, 
			   GetWindowLong(m_hwnd1, GWL_STYLE ), 
			   FALSE, GetWindowLong(m_hwnd1, GWL_EXSTYLE));

	if (GetMenuState(GetSystemMenu(m_hwnd1, FALSE),
					 ID_TOOLBAR, MF_BYCOMMAND) == MF_CHECKED) {
		RECT rtb;
		GetWindowRect(m_hToolbar, &rtb);
		fullwinrect.bottom = fullwinrect.bottom + rtb.bottom - rtb.top - 3;
	}

	m_winwidth  = min(fullwinrect.right - fullwinrect.left,  workwidth);
	m_winheight = min(fullwinrect.bottom - fullwinrect.top, workheight);
	if ((fullwinrect.right - fullwinrect.left > workwidth) &&
		(workheight - m_winheight >= 16)) {
		m_winheight = m_winheight + 16;
	} 
	if ((fullwinrect.bottom - fullwinrect.top > workheight) && 
		(workwidth - m_winwidth >= 16)) {
		m_winwidth = m_winwidth + 16;
	}

	int x,y;
	WINDOWPLACEMENT winplace;
	winplace.length = sizeof(WINDOWPLACEMENT);
	GetWindowPlacement(m_hwnd1, &winplace);
	if (centered) {
		x = (workwidth - m_winwidth) / 2;		
		y = (workheight - m_winheight) / 2;		
	} else {
		// Try to preserve current position if possible
		GetWindowPlacement(m_hwnd1, &winplace);
		if ((winplace.showCmd == SW_SHOWMAXIMIZED) || (winplace.showCmd == SW_SHOWMINIMIZED)) {
			x = winplace.rcNormalPosition.left;
			y = winplace.rcNormalPosition.top;
		} else {
			RECT tmprect;
			GetWindowRect(m_hwnd1, &tmprect);
			x = tmprect.left;
			y = tmprect.top;
		}
		if (x + m_winwidth > workrect.right)
			x = workrect.right - m_winwidth;
		if (y + m_winheight > workrect.bottom)
			y = workrect.bottom - m_winheight;
	}
	winplace.rcNormalPosition.top = y;
	winplace.rcNormalPosition.left = x;
	winplace.rcNormalPosition.right = x + m_winwidth;
	winplace.rcNormalPosition.bottom = y + m_winheight;
	SetWindowPlacement(m_hwnd1, &winplace);
	SetForegroundWindow(m_hwnd1);
	PositionChildWindow();
}

void ClientConnection::PositionChildWindow()
{	
	RECT rparent;
	GetClientRect(m_hwnd1, &rparent);
	
	int parentwidth = rparent.right - rparent.left;
	int parentheight = rparent.bottom - rparent.top; 
				
	if (GetMenuState(GetSystemMenu(m_hwnd1, FALSE),
				ID_TOOLBAR, MF_BYCOMMAND) == MF_CHECKED) {
		RECT rtb;
		GetWindowRect(m_hToolbar, &rtb);
		int rtbheight = rtb.bottom - rtb.top - 3;
		SetWindowPos(m_hToolbar, HWND_TOP, rparent.left, rparent.top,
					parentwidth, rtbheight, SWP_SHOWWINDOW);		
		parentheight = parentheight - rtbheight;
		rparent.top = rparent.top + rtbheight;
	} else {
		ShowWindow(m_hToolbar, SW_HIDE);
	}
	
	SetWindowPos(m_hwndscroll, HWND_TOP, rparent.left - 1, rparent.top - 1,
					parentwidth + 2, parentheight + 2, SWP_SHOWWINDOW);
	if (!m_opts.m_FitWindow) {
		if (InFullScreenMode()) {				
			ShowScrollBar(m_hwndscroll, SB_HORZ, FALSE);
			ShowScrollBar(m_hwndscroll, SB_VERT, FALSE);
		} else {
			ShowScrollBar(m_hwndscroll, SB_VERT, parentheight < m_fullwinheight);
			ShowScrollBar(m_hwndscroll, SB_HORZ, parentwidth  < m_fullwinwidth);
			GetClientRect(m_hwndscroll, &rparent);	
			parentwidth = rparent.right - rparent.left;
			parentheight = rparent.bottom - rparent.top;
			ShowScrollBar(m_hwndscroll, SB_VERT, parentheight < m_fullwinheight);
			ShowScrollBar(m_hwndscroll, SB_HORZ, parentwidth  < m_fullwinwidth);
			GetClientRect(m_hwndscroll, &rparent);	
			parentwidth = rparent.right - rparent.left;
			parentheight = rparent.bottom - rparent.top;		
		}
	} else {
		if (!IsIconic(m_hwnd1)) {
			ShowScrollBar(m_hwndscroll, SB_HORZ, FALSE);
			ShowScrollBar(m_hwndscroll, SB_VERT, FALSE);
			GetClientRect(m_hwndscroll, &rparent);	
			parentwidth = rparent.right - rparent.left;
			parentheight = rparent.bottom - rparent.top;
			if ((parentwidth < 1) || (parentheight < 1))
				return;
			RECT fullwinrect;		
			int den = max(m_si.framebufferWidth * 100 / parentwidth,
							m_si.framebufferHeight * 100 / parentheight);
			SetRect(&fullwinrect, 0, 0, (m_si.framebufferWidth * 100 + den - 1) / den,
					(m_si.framebufferHeight * 100 + den - 1) / den);						
			while ((fullwinrect.right - fullwinrect.left > parentwidth) ||
					(fullwinrect.bottom - fullwinrect.top > parentheight)) {
				den++;
				SetRect(&fullwinrect, 0, 0, (m_si.framebufferWidth * 100 + den - 1) / den,
					(m_si.framebufferHeight * 100 + den - 1) / den);								
			}

			m_opts.m_scale_num = 100;
			m_opts.m_scale_den = den;
				
			m_opts.FixScaling();

			m_fullwinwidth = fullwinrect.right - fullwinrect.left;
			m_fullwinheight = fullwinrect.bottom - fullwinrect.top;
		}
	}

	
	int x, y;
	if (parentwidth  > m_fullwinwidth) {
		x = (parentwidth - m_fullwinwidth) / 2;
	} else {
		x = rparent.left;
	}
	if (parentheight > m_fullwinheight) {
		y = (parentheight - m_fullwinheight) / 2;
	} else {
		y = rparent.top;
	}
	
	SetWindowPos(m_hwnd, HWND_TOP, x, y,
					min(parentwidth, m_fullwinwidth),
					min(parentheight, m_fullwinheight),
					SWP_SHOWWINDOW);

	m_cliwidth = min( (int)parentwidth, (int)m_fullwinwidth);
	m_cliheight = min( (int)parentheight, (int)m_fullwinheight);

	m_hScrollMax = m_fullwinwidth;
	m_vScrollMax = m_fullwinheight;
           
		int newhpos, newvpos;
	if (!m_opts.m_FitWindow) {
		newhpos = max(0, min(m_hScrollPos, 
								 m_hScrollMax - max(m_cliwidth, 0)));
		newvpos = max(0, min(m_vScrollPos, 
				                 m_vScrollMax - max(m_cliheight, 0)));
	} else {
		newhpos = 0;
		newvpos = 0;
	}
	RECT clichild;
	GetClientRect(m_hwnd, &clichild);
	ScrollWindowEx(m_hwnd, m_hScrollPos-newhpos, m_vScrollPos-newvpos,
					NULL, &clichild, NULL, NULL,  SW_INVALIDATE);
								
	m_hScrollPos = newhpos;
	m_vScrollPos = newvpos;
	if (!m_opts.m_FitWindow) {
		UpdateScrollbars();
	} else {
		InvalidateRect(m_hwnd, NULL, FALSE);
	}
	UpdateWindow(m_hwnd);
    SetWindowShape();
}

void ClientConnection::CreateLocalFramebuffer() {
	omni_mutex_lock l(m_bitmapdcMutex);

	// Remove old bitmap object if it already exists
	bool bitmapExisted = false;
	if (m_hBitmap != NULL) {
		DeleteObject(m_hBitmap);
		bitmapExisted = true;
	}

	// We create a bitmap which has the same pixel characteristics as
	// the local display, in the hope that blitting will be faster.
	
	TempDC hdc(m_hwnd);
	m_hBitmap = ::CreateCompatibleBitmap(hdc, m_si.framebufferWidth,
										 m_si.framebufferHeight);
	
	if (m_hBitmap == NULL)
		throw WarningException("Error creating local image of screen.");
	
	// Select this bitmap into the DC with an appropriate palette
	ObjectSelector b(m_hBitmapDC, m_hBitmap);
	PaletteSelector p(m_hBitmapDC, m_hPalette);
	
	// Put a "please wait" message up initially
	RECT rect;
	SetRect(&rect, 0,0, m_si.framebufferWidth, m_si.framebufferHeight);
	COLORREF bgcol = RGB(0xcc, 0xcc, 0xcc);
	FillSolidRect(&rect, bgcol);
	
	if (!bitmapExisted) {
		COLORREF oldbgcol  = SetBkColor(m_hBitmapDC, bgcol);
		COLORREF oldtxtcol = SetTextColor(m_hBitmapDC, RGB(0,0,64));
		rect.right = m_si.framebufferWidth / 2;
		rect.bottom = m_si.framebufferHeight / 2;
	
		DrawText (m_hBitmapDC, _T("Please wait - initial screen loading"), -1, &rect,
				  DT_SINGLELINE | DT_CENTER | DT_VCENTER);
		SetBkColor(m_hBitmapDC, oldbgcol);
		SetTextColor(m_hBitmapDC, oldtxtcol);
	}
	
	InvalidateRect(m_hwnd, NULL, FALSE);
}

void ClientConnection::SetupPixelFormat() {
	// Have we requested a reduction to 8-bit?
    if (m_opts.m_Use8Bit) {		
      
		vnclog.Print(2, _T("Requesting 8-bit truecolour\n"));  
		m_myFormat = vnc8bitFormat;
    
		// We don't support colormaps so we'll ask the server to convert
    } else if (!m_si.format.trueColour) {
        
        // We'll just request a standard 16-bit truecolor
        vnclog.Print(2, _T("Requesting 16-bit truecolour\n"));
        m_myFormat = vnc16bitFormat;
        
    } else {

		// Normally we just use the sever's format suggestion
		m_myFormat = m_si.format;

		// It's silly requesting more bits than our current display has, but
		// in fact it doesn't usually amount to much on the network.
		// Windows doesn't support 8-bit truecolour.
		// If our display is palette-based, we want more than 8 bit anyway,
		// unless we're going to start doing palette stuff at the server.
		// So the main use would be a 24-bit true-colour desktop being viewed
		// on a 16-bit true-colour display, and unless you have lots of images
		// and hence lots of raw-encoded stuff, the size of the pixel is not
		// going to make much difference.
		//   We therefore don't bother with any restrictions, but here's the
		// start of the code if we wanted to do it.

		if (false) {
		
			// Get a DC for the root window
			TempDC hrootdc(NULL);
			int localBitsPerPixel = GetDeviceCaps(hrootdc, BITSPIXEL);
			int localRasterCaps	  = GetDeviceCaps(hrootdc, RASTERCAPS);
			vnclog.Print(2, _T("Memory DC has depth of %d and %s pallete-based.\n"), 
				localBitsPerPixel, (localRasterCaps & RC_PALETTE) ? "is" : "is not");
			
			// If we're using truecolor, and the server has more bits than we do
			if ( (localBitsPerPixel > m_myFormat.depth) && 
				! (localRasterCaps & RC_PALETTE)) {
				m_myFormat.depth = localBitsPerPixel;

				// create a bitmap compatible with the current display
				// call GetDIBits twice to get the colour info.
				// set colour masks and shifts
				
			}
		}
	}

	// The endian will be set before sending
}

void ClientConnection::SetFormatAndEncodings()
{
	// Set pixel format to myFormat

	rfbSetPixelFormatMsg spf;

	spf.type = rfbSetPixelFormat;
	spf.format = m_myFormat;
	spf.format.redMax = Swap16IfLE(spf.format.redMax);
	spf.format.greenMax = Swap16IfLE(spf.format.greenMax);
	spf.format.blueMax = Swap16IfLE(spf.format.blueMax);
	spf.format.bigEndian = 0;

	WriteExact((char *)&spf, sz_rfbSetPixelFormatMsg);

	// The number of bytes required to hold at least one pixel.
	m_minPixelBytes = (m_myFormat.bitsPerPixel + 7) >> 3;

	// Set encodings
	char buf[sz_rfbSetEncodingsMsg + MAX_ENCODINGS * 4];
	rfbSetEncodingsMsg *se = (rfbSetEncodingsMsg *)buf;
	CARD32 *encs = (CARD32 *)(&buf[sz_rfbSetEncodingsMsg]);
	int len = 0;

	se->type = rfbSetEncodings;
	se->nEncodings = 0;

	bool useCompressLevel = false;
	int i;

	// Put the preferred encoding first, and change it if the
	// preferred encoding is not actually usable.
	for (i = LASTENCODING; i >= rfbEncodingRaw; i--)
	{
		if (m_opts.m_PreferredEncoding == i) {
			if (m_opts.m_UseEnc[i]) {
				encs[se->nEncodings++] = Swap32IfLE(i);
				if ( i == rfbEncodingZlib ||
					 i == rfbEncodingTight ||
					 i == rfbEncodingZlibHex ) {
					useCompressLevel = true;
				}
			} else {
				m_opts.m_PreferredEncoding--;
			}
		}
	}

	// Now we go through and put in all the other encodings in order.
	// We do rather assume that the most recent encoding is the most
	// desirable!
	for (i = LASTENCODING; i >= rfbEncodingRaw; i--)
	{
		if ( (m_opts.m_PreferredEncoding != i) &&
			 (m_opts.m_UseEnc[i]))
		{
			encs[se->nEncodings++] = Swap32IfLE(i);
			if ( i == rfbEncodingZlib ||
				 i == rfbEncodingTight ||
				 i == rfbEncodingZlibHex ) {
				useCompressLevel = true;
			}
		}
	}

	// Request desired compression level if applicable
	if ( useCompressLevel && m_opts.m_useCompressLevel &&
		 m_opts.m_compressLevel >= 0 &&
		 m_opts.m_compressLevel <= 9) {
		encs[se->nEncodings++] = Swap32IfLE( rfbEncodingCompressLevel0 +
											 m_opts.m_compressLevel );
	}

	// Request cursor shape updates if enabled by user
	if (m_opts.m_requestShapeUpdates) {
		encs[se->nEncodings++] = Swap32IfLE(rfbEncodingXCursor);
		encs[se->nEncodings++] = Swap32IfLE(rfbEncodingRichCursor);
		if (!m_opts.m_ignoreShapeUpdates)
			encs[se->nEncodings++] = Swap32IfLE(rfbEncodingPointerPos);
	}

	// Request JPEG quality level if JPEG compression was enabled by user
	if ( m_opts.m_enableJpegCompression &&
		 m_opts.m_jpegQualityLevel >= 0 &&
		 m_opts.m_jpegQualityLevel <= 9) {
		encs[se->nEncodings++] = Swap32IfLE( rfbEncodingQualityLevel0 +
											 m_opts.m_jpegQualityLevel );
	}

	// Notify the server that we support LastRect and NewFBSize encodings
	encs[se->nEncodings++] = Swap32IfLE(rfbEncodingLastRect);
	encs[se->nEncodings++] = Swap32IfLE(rfbEncodingNewFBSize);

    // TransRect encoding
    if (InTransRectMode())
        encs[se->nEncodings++] = Swap32IfLE(rfbEncodingTransRect);

	len = sz_rfbSetEncodingsMsg + se->nEncodings * 4;

	se->nEncodings = Swap16IfLE(se->nEncodings);

	WriteExact((char *) buf, len);
}

// Closing down the connection.
// Close the socket, kill the thread.
void ClientConnection::KillThread()
{
	m_bKillThread = true;
	m_running = false;

	if (m_sock != INVALID_SOCKET) {
		shutdown(m_sock, SD_BOTH);
		closesocket(m_sock);
		m_sock = INVALID_SOCKET;
	}
}

// Get the RFB options from another connection.
void ClientConnection::CopyOptions(ClientConnection *source)
{
	this->m_opts = source->m_opts;
}

ClientConnection::~ClientConnection()
{
	if (m_hwnd1 != 0)
		DestroyWindow(m_hwnd1);

	if (m_connDlg != NULL)
		delete m_connDlg;

	if (m_sock != INVALID_SOCKET) {
		shutdown(m_sock, SD_BOTH);
		closesocket(m_sock);
		m_sock = INVALID_SOCKET;
	}

	if (m_desktopName != NULL) delete [] m_desktopName;
	delete [] m_netbuf;
	delete m_pFileTransfer;
	DeleteDC(m_hBitmapDC);
	if (m_hBitmap != NULL)
		DeleteObject(m_hBitmap);
	if (m_hPalette != NULL)
		DeleteObject(m_hPalette);

	delete m_metaTaskbar;
	delete m_metaLauncher;
    DeleteObject(m_hTransRgn);
	
	m_pApp->DeregisterConnection(this);
}

// You can specify a dx & dy outside the limits; the return value will
// tell you whether it actually scrolled.
bool ClientConnection::ScrollScreen(int dx, int dy) 
{
	dx = max(dx, -m_hScrollPos);
	//dx = min(dx, m_hScrollMax-(m_cliwidth-1)-m_hScrollPos);
	dx = min(dx, m_hScrollMax-(m_cliwidth)-m_hScrollPos);
	dy = max(dy, -m_vScrollPos);
	//dy = min(dy, m_vScrollMax-(m_cliheight-1)-m_vScrollPos);
	dy = min(dy, m_vScrollMax-(m_cliheight)-m_vScrollPos);
	if (dx || dy) {
		m_hScrollPos += dx;
		m_vScrollPos += dy;
		RECT clirect;
		GetClientRect(m_hwnd, &clirect);
		ScrollWindowEx(m_hwnd, -dx, -dy,
				NULL, &clirect, NULL, NULL,  SW_INVALIDATE);
		UpdateScrollbars();
		UpdateWindow(m_hwnd);
        SetWindowShape();
		return true;
	}
	return false;
}

// Process windows messages
LRESULT CALLBACK ClientConnection::ScrollProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{	// This is a static method, so we don't know which instantiation we're 
	// dealing with.  But we've stored a 'pseudo-this' in the window data.
	ClientConnection *_this = (ClientConnection *) GetWindowLong(hwnd, GWL_USERDATA);
		
	switch (iMsg) {
	case WM_HSCROLL:
		{				
			int dx = 0;
			int pos = HIWORD(wParam);
			switch (LOWORD(wParam)) {
			case SB_LINEUP:
				dx = - 2; break;
			case SB_LINEDOWN:
				dx = 2; break;
			case SB_PAGEUP:
				dx = _this->m_cliwidth * -1/4; break;
			case SB_PAGEDOWN:
				dx = _this->m_cliwidth * 1/4; break;
			case SB_THUMBPOSITION:
				dx = pos - _this->m_hScrollPos;
			case SB_THUMBTRACK:
				dx = pos - _this->m_hScrollPos;
			}
			if (!_this->m_opts.m_FitWindow) 
				_this->ScrollScreen(dx,0);
			return 0;
		}
	case WM_VSCROLL:
		{
			int dy = 0;
			int pos = HIWORD(wParam);
			switch (LOWORD(wParam)) {
			case SB_LINEUP:
				dy =  - 2; break;
			case SB_LINEDOWN:
				dy = 2; break;
			case SB_PAGEUP:
				dy =  _this->m_cliheight * -1/4; break;
			case SB_PAGEDOWN:
				dy = _this->m_cliheight * 1/4; break;
			case SB_THUMBPOSITION:
				dy = pos - _this->m_vScrollPos;
			case SB_THUMBTRACK:
				dy = pos - _this->m_vScrollPos;
			}
			if (!_this->m_opts.m_FitWindow) 
				_this->ScrollScreen(0,dy);
			return 0;
		}
	}
	return DefWindowProc(hwnd, iMsg, wParam, lParam);
}
LRESULT CALLBACK ClientConnection::WndProc1(HWND hwnd, UINT iMsg, 
					   WPARAM wParam, LPARAM lParam) 
{
	
	// This is a static method, so we don't know which instantiation we're 
	// dealing with.  But we've stored a 'pseudo-this' in the window data.
	ClientConnection *_this = (ClientConnection *) GetWindowLong(hwnd, GWL_USERDATA);
	//vnclog.Print(1, TEXT("%s: hwnd=0x%X, iMsg=0x%X, wParam=0x%X, lParam=0x%X\n"), __FUNCTION__, hwnd, iMsg, wParam, lParam);
		
	switch (iMsg) {
	
	case WM_NOTIFY:
	{		
		LPTOOLTIPTEXT TTStr = (LPTOOLTIPTEXT)lParam;
		if (TTStr->hdr.code != TTN_NEEDTEXT)
			return 0;

		switch (TTStr->hdr.idFrom) {
		case IDC_OPTIONBUTTON:
			TTStr->lpszText = "Connection options...";
			break;
		case ID_CONN_ABOUT:
			TTStr->lpszText = "Connection info";
			break;
		case ID_FULLSCREEN:
			TTStr->lpszText = "Full screen";
			break;
		case ID_REQUEST_REFRESH:
			TTStr->lpszText = "Request screen refresh";
			break;
		case ID_CONN_CTLALTDEL:
			TTStr->lpszText = "Send Ctrl-Alt-Del";
			break;
		case ID_CONN_CTLESC:
			TTStr->lpszText = "Send Ctrl-Esc";
			break;
		case ID_CONN_CTLDOWN:
			TTStr->lpszText = "Send Ctrl key press/release";
			break;
		case ID_CONN_ALTDOWN:
			TTStr->lpszText = "Send Alt key press/release";
			break;
		case IDD_FILETRANSFER:
			TTStr->lpszText = "Transfer files...";
			break;
		case ID_NEWCONN:
			TTStr->lpszText = "New connection...";
			break;
		case ID_CONN_SAVE_AS:
			TTStr->lpszText = "Save connection info as...";
			break;
		case ID_DISCONNECT:
			TTStr->lpszText = "Disconnect";
			break;
		case ID_TRANSRECT:
			TTStr->lpszText = "Toggle Transparency";
			break;
		}
		return 0;
	}
	case WM_SETFOCUS:		
		hotkeys.SetWindow(hwnd);
		SetFocus(_this->m_hwnd);
		return 0;
	case WM_COMMAND:
	case WM_SYSCOMMAND:
		switch (LOWORD(wParam)) {
		case SC_MINIMIZE:
			_this->SetDormant(true);
			break;
		case SC_RESTORE:			
			_this->SetDormant(false);
			break;
		case ID_NEWCONN:
			_this->m_pApp->NewConnection();
			return 0;
		case ID_DISCONNECT:
			SendMessage(hwnd, WM_CLOSE, 0, 0);
			return 0;
		case ID_TRANSRECT:
            _this->m_opts.m_UseTransRect = 
                (GetMenuState(GetSystemMenu(_this->m_hwnd1, FALSE),
                              ID_TRANSRECT, MF_BYCOMMAND) != MF_CHECKED);
            _this->SetWindowShape();
            _this->CheckAction(ID_TRANSRECT, _this->m_opts.m_UseTransRect);
            _this->m_pendingFormatChange = true;
            if (_this->InFullScreenMode())
                _this->RealiseFullScreenMode(true);
			_this->SendFullFramebufferUpdateRequest();
			return 0;
		case ID_DRAGDESKTOP:
            _this->m_opts.m_dragDesktop =
                (GetMenuState(GetSystemMenu(_this->m_hwnd1, FALSE),
                              ID_DRAGDESKTOP, MF_BYCOMMAND) != MF_CHECKED);
            _this->CheckAction(ID_DRAGDESKTOP, _this->m_opts.m_dragDesktop);
			return 0;
		case ID_TOOLBAR:
			if (GetMenuState(GetSystemMenu(_this->m_hwnd1, FALSE),
				ID_TOOLBAR,MF_BYCOMMAND) == MF_CHECKED) {
				CheckMenuItem(GetSystemMenu(_this->m_hwnd1, FALSE),
					ID_TOOLBAR, MF_BYCOMMAND|MF_UNCHECKED);
			} else {
				CheckMenuItem(GetSystemMenu(_this->m_hwnd1, FALSE),
					ID_TOOLBAR, MF_BYCOMMAND|MF_CHECKED);
			}			
			_this->SizeWindow(false);			
			return 0;
		case ID_CONN_SAVE_AS:			
			_this->SaveConnection();
			return 0;			
		case IDC_OPTIONBUTTON:
			{
				if (SetForegroundWindow(_this->m_opts.m_hParent) != 0) return 0;
				int prev_scale_num = _this->m_opts.m_scale_num;
				int prev_scale_den = _this->m_opts.m_scale_den;
                bool prev_useTransRect = _this->m_opts.m_UseTransRect;
                bool prev_fullScreen = _this->m_opts.m_FullScreen;
                int prev_menuLoc = _this->m_opts.m_menuLoc;
				
				if (_this->m_opts.DoDialog(true)) {
					_this->m_pendingFormatChange = true;
					if (_this->m_opts.m_FitWindow) {
						_this->m_opts.m_scaling = true;
						_this->PositionChildWindow();
					} else {
						if (prev_scale_num != _this->m_opts.m_scale_num ||
							prev_scale_den != _this->m_opts.m_scale_den) {
							// Resize the window if scaling factors were changed
                            _this->SizeWindow(false);
							InvalidateRect(_this->m_hwnd, NULL, FALSE);
						}
					}

                    /* update menu and toolbar states */
                    _this->CheckAction(ID_TRANSRECT, _this->m_opts.m_UseTransRect);
                    _this->CheckAction(ID_DRAGDESKTOP, _this->m_opts.m_dragDesktop);

                    if (prev_useTransRect != _this->m_opts.m_UseTransRect) {
                        _this->SetWindowShape();
                        if (_this->InFullScreenMode())
                            _this->RealiseFullScreenMode(true);
                        _this->SendFullFramebufferUpdateRequest();
                    }
                    if (prev_fullScreen != _this->m_opts.m_FullScreen) {
                        _this->RealiseFullScreenMode(false);
                    }
                    if (prev_menuLoc != _this->m_opts.m_menuLoc) {
                        // refresh meta window manager
                        _this->RefreshWindowState();
                    }
				}
				
				if (_this->m_serverInitiated) {
					_this->m_opts.SaveOpt(".listen", 
										KEY_VNCVIEWER_HISTORI);
				} else {
					_this->m_opts.SaveOpt(_this->m_opts.m_display,
										KEY_VNCVIEWER_HISTORI);
				}
				_this->EnableFullControlOptions();
				return 0;
			}
		case IDD_APP_ABOUT:
			ShowAboutBox();
			return 0;
		case IDD_FILETRANSFER:
			if (_this->m_clientMsgCaps.IsEnabled(rfbFileListRequest)) {
				if (!_this->m_fileTransferDialogShown) {
					_this->m_fileTransferDialogShown = true;
					_this->m_pFileTransfer->CreateFileTransferDialog();
				}
			}
			return 0;
		case ID_CONN_ABOUT:
			_this->ShowConnInfo();
			return 0;
		case ID_FULLSCREEN:
			// Toggle full screen mode
			_this->SetFullScreenMode(!_this->InFullScreenMode());
			return 0;
		case ID_REQUEST_REFRESH: 
			// Request a full-screen update
			_this->SendFullFramebufferUpdateRequest();
            // refresh meta window manager
            if (_this->InMetaWMMode()) {
                _this->RefreshWindowState();
            }
			return 0;
		case ID_CONN_CTLESC:
			_this->SendKeyEvent(XK_Control_L, true);
			_this->SendKeyEvent(XK_Escape,     true);
			_this->SendKeyEvent(XK_Escape,     false);
			_this->SendKeyEvent(XK_Control_L, false);
			return 0;
		case ID_CONN_CTLALTDEL:
			_this->SendKeyEvent(XK_Control_L, true);
			_this->SendKeyEvent(XK_Alt_L,     true);
			_this->SendKeyEvent(XK_Delete,    true);
			_this->SendKeyEvent(XK_Delete,    false);
			_this->SendKeyEvent(XK_Alt_L,     false);
			_this->SendKeyEvent(XK_Control_L, false);
			return 0;
		case ID_CONN_CTLDOWN:
			if (GetMenuState(GetSystemMenu(_this->m_hwnd1, FALSE),
				ID_CONN_CTLDOWN, MF_BYCOMMAND) == MF_CHECKED) {
				_this->SendKeyEvent(XK_Control_L, false);
				CheckMenuItem(GetSystemMenu(_this->m_hwnd1, FALSE),
					ID_CONN_CTLDOWN, MF_BYCOMMAND|MF_UNCHECKED);
				SendMessage(_this->m_hToolbar, TB_SETSTATE, (WPARAM)ID_CONN_CTLDOWN,
					(LPARAM)MAKELONG(TBSTATE_ENABLED, 0));
			} else {
				CheckMenuItem(GetSystemMenu(_this->m_hwnd1, FALSE),
					ID_CONN_CTLDOWN, MF_BYCOMMAND|MF_CHECKED);
				SendMessage(_this->m_hToolbar, TB_SETSTATE, (WPARAM)ID_CONN_CTLDOWN,
					(LPARAM)MAKELONG(TBSTATE_CHECKED|TBSTATE_ENABLED, 0));
				_this->SendKeyEvent(XK_Control_L, true);
			}
			return 0;
		case ID_CONN_ALTDOWN:
			if(GetMenuState(GetSystemMenu(_this->m_hwnd1, FALSE),
				ID_CONN_ALTDOWN,MF_BYCOMMAND) == MF_CHECKED) {
				_this->SendKeyEvent(XK_Alt_L, false);
				CheckMenuItem(GetSystemMenu(_this->m_hwnd1, FALSE),
					ID_CONN_ALTDOWN, MF_BYCOMMAND|MF_UNCHECKED);
				SendMessage(_this->m_hToolbar, TB_SETSTATE, (WPARAM)ID_CONN_ALTDOWN,
					(LPARAM)MAKELONG(TBSTATE_ENABLED, 0));
			} else {
				CheckMenuItem(GetSystemMenu(_this->m_hwnd1, FALSE),
					ID_CONN_ALTDOWN, MF_BYCOMMAND|MF_CHECKED);
				SendMessage(_this->m_hToolbar, TB_SETSTATE, (WPARAM)ID_CONN_ALTDOWN,
					(LPARAM)MAKELONG(TBSTATE_CHECKED|TBSTATE_ENABLED, 0));
				_this->SendKeyEvent(XK_Alt_L, true);
			}
			return 0;
		case ID_CLOSEDAEMON:
			if (MessageBox(NULL, _T("Are you sure you want to exit?"), 
				_T("Closing VNCviewer"), 
				MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES){
				PostQuitMessage(0);
			}
			return 0;
		}
		break;		
	case WM_TRAYNOTIFY:
		{
			HMENU hSubMenu = GetSubMenu(_this->m_htraymenu, 0);
			if (lParam==WM_LBUTTONDBLCLK) {
				// double click: execute first menu item
				::SendMessage(_this->m_hwnd1, WM_COMMAND, 
					GetMenuItemID(hSubMenu, 0), 0);
			} else if (lParam==WM_RBUTTONUP) {
				if (hSubMenu == NULL) { 
					vnclog.Print(2, _T("No systray submenu\n"));
					return 0;
				}
				// Make first menu item the default (bold font)
				::SetMenuDefaultItem(hSubMenu, 0, TRUE);
				
				// Display the menu at the current mouse location. There's a "bug"
				// (Microsoft calls it a feature) in Windows 95 that requires calling
				// SetForegroundWindow. To find out more, search for Q135788 in MSDN.
				//
				POINT mouse;
				GetCursorPos(&mouse);
				::SetForegroundWindow(_this->m_hwnd1);
				::TrackPopupMenu(hSubMenu, 0, mouse.x, mouse.y, 0,
					_this->m_hwnd1, NULL);
				
			} 
			return 0;
		}
	case WM_TIMER:
		if (wParam == _this->m_traytimer)
            _this->CheckTrayIcon();
        return 0;
	case WM_KILLFOCUS:
		if ( _this->m_opts.m_ViewOnly) return 0;
		_this->SwitchOffKey();
		return 0;
	case WM_GETMINMAXINFO:	/* XXX */
		MINMAXINFO win;
		win = *(LPMINMAXINFO) lParam;
		if (_this->InFullScreenMode() && _this->InTransRectMode()) {
			win.ptMaxSize.x = _this->m_fullwinwidth + 2;
			win.ptMaxSize.y = _this->m_fullwinheight + 2;
			win.ptMaxTrackSize.x = _this->m_fullwinwidth + 2;
			win.ptMaxTrackSize.y = _this->m_fullwinheight + 2;
		}

		*(LPMINMAXINFO) lParam = win;
		return 0;
	case WM_SIZE:		
		_this->PositionChildWindow();			
		return 0;	
	case WM_CLOSE:		
		// Close the worker thread as well
		_this->KillThread();
		DestroyWindow(hwnd);
		return 0;					  
	case WM_DESTROY: 			
	{
#ifndef UNDER_CE
		// Remove us from the clipboard viewer chain
		BOOL res = ChangeClipboardChain( _this->m_hwnd, _this->m_hwndNextViewer);
#endif
		if (_this->m_serverInitiated) {
			_this->m_opts.SaveOpt(".listen", 
								KEY_VNCVIEWER_HISTORI);
		} else {
			_this->m_opts.SaveOpt(_this->m_opts.m_display,
								KEY_VNCVIEWER_HISTORI);
		}
		if (_this->m_waitingOnEmulateTimer) {
			
			KillTimer(_this->m_hwnd, _this->m_emulate3ButtonsTimer);
			_this->m_waitingOnEmulateTimer = false;
		}
        KillTimer(_this->m_hwnd1, _this->m_traytimer);
        _this->RemoveTrayIcon();
			
		_this->m_hwnd1 = 0;
		_this->m_hwnd = 0;
		_this->m_opts.m_hWindow = 0;
		// We are currently in the main thread.
		// The worker thread should be about to finish if
		// it hasn't already. Wait for it.
		try {
			void *p;
			_this->join(&p);  // After joining, _this is no longer valid
		} catch (omni_thread_invalid) {
		// The thread probably hasn't been started yet,
		}	
		return 0;						 
	}
    default:
        if (iMsg == RFB_METATASKSW_CREATE) {
            if (_this->InMetaWMMode()) {
                vnclog.Print(1, _T("RFB_METATASKSW_CREATE: id=0x%X, state=0x%X\n"), wParam, lParam);
                _this->m_metaTaskbar->createTaskSw((ULONG)wParam, (ULONG)lParam);
            }
        } else if (iMsg == RFB_METALAUNCH) {
            if (_this->InMetaWMMode()) {
                vnclog.Print(1, _T("RFB_METALAUNCH: id=0x%X\n"), wParam);
                _this->SendWindowControl(wParam, rfbWindowControlLaunch);
            }
        }
        break;
	}
	return DefWindowProc(hwnd, iMsg, wParam, lParam);
}	

LRESULT CALLBACK ClientConnection::Proc(HWND hwnd, UINT iMsg,
										WPARAM wParam, LPARAM lParam)
{
	//vnclog.Print(1, TEXT("%s: hwnd=0x%X, iMsg=0x%X, wParam=0x%X, lParam=0x%X\n"), __FUNCTION__, hwnd, iMsg, wParam, lParam);
	return DefWindowProc(hwnd, iMsg, wParam, lParam);
}

LRESULT CALLBACK ClientConnection::WndProc(HWND hwnd, UINT iMsg, 
					   WPARAM wParam, LPARAM lParam) 
{	
	// This is a static method, so we don't know which instantiation we're 
	// dealing with.  But we've stored a 'pseudo-this' in the window data.
	ClientConnection *_this = (ClientConnection *) GetWindowLong(hwnd, GWL_USERDATA);
	//vnclog.Print(1, TEXT("%s: hwnd=0x%X, iMsg=0x%X, wParam=0x%X, lParam=0x%X\n"), __FUNCTION__, hwnd, iMsg, wParam, lParam);

	switch (iMsg) {
	case WM_REGIONUPDATED:
		_this->DoBlit();
		_this->SendAppropriateFramebufferUpdateRequest();		
		return 0;
	case WM_PAINT:
		_this->DoBlit();		
		return 0;
	case WM_TIMER:
		if (wParam == _this->m_emulate3ButtonsTimer) {
			_this->SubProcessPointerEvent( 
										_this->m_emulateButtonPressedX,
										 _this->m_emulateButtonPressedY,
										_this->m_emulateKeyFlags);
			KillTimer(hwnd, _this->m_emulate3ButtonsTimer);
			 _this->m_waitingOnEmulateTimer = false;
		}
		return 0; 
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_MOUSEMOVE:
	case WM_MOUSEWHEEL:
		{
//vnclog.Print(9, TEXT("m_running=%d\n"), _this->m_running);
			if (!_this->m_running)
				return 0;
//vnclog.Print(9, TEXT("GetFocus=0x%X, hwnd=0x%X, m_hwnd1=0x%X\n"), GetFocus(), hwnd, _this->m_hwnd1);
			if (GetFocus() != hwnd && GetFocus() != _this->m_hwnd1)
				return 0;
            // XXX workaround to ignore false mouse event
            if (iMsg == WM_MOUSEMOVE) {
                if (lParam == _this->m_prevmousepos) {
vnclog.Print(9, TEXT("ignoring false mouse event (%d, %d)\n"), (short)LOWORD(lParam), (short)HIWORD(lParam));
                    return 0;
                }
                _this->m_prevmousepos = lParam;
            }
			SetFocus(hwnd);

			// keep capturing until all mouse buttons are released
			if (iMsg == WM_LBUTTONDOWN || iMsg == WM_MBUTTONDOWN || iMsg == WM_RBUTTONDOWN) {
                _this->m_mouseButtonPressed = true;
				SetCapture(hwnd);
			}
			if (iMsg == WM_LBUTTONUP || iMsg == WM_MBUTTONUP || iMsg == WM_RBUTTONUP) {
                if ((GetAsyncKeyState(VK_LBUTTON) & 0x80000000) == 0 &&
                    (GetAsyncKeyState(VK_MBUTTON) & 0x80000000) == 0 &&
                    (GetAsyncKeyState(VK_RBUTTON) & 0x80000000) == 0) {
                    ReleaseCapture();
                    _this->m_mouseButtonPressed = false;
                    _this->m_draggingDesktop = false;
vnclog.Print(9, TEXT("All mouse buttons are released\n"));
				}
			}

			POINT coords;
			coords.x = (short)LOWORD(lParam);
			coords.y = (short)HIWORD(lParam);

			if (iMsg == WM_MOUSEWHEEL) {
				// Convert coordinates to position in our client area,
				// make sure the pointer is inside the client area.
				if ( WindowFromPoint(coords) != hwnd ||
					 !ScreenToClient(hwnd, &coords) ||
					 coords.x < 0 || coords.y < 0 ||
					 coords.x >= _this->m_cliwidth ||
					 coords.y >= _this->m_cliheight ) {
					return 0;
				}
			} else {
				// Make sure the high-order word in wParam is zero.
				wParam = MAKEWPARAM(LOWORD(wParam), 0);
			}

            if (_this->m_opts.m_dragDesktop && !_this->m_draggingDesktop &&
                _this->m_mouseButtonPressed &&
                _this->InFullScreenMode() && _this->InTransRectMode()) {
                _this->m_draggingDesktop = 
                    (coords.x < 0 || _this->m_cliwidth <= coords.x ||
                     coords.y < 0 || _this->m_cliheight <= coords.y);
                if (_this->m_draggingDesktop) {
vnclog.Print(9, TEXT("Start Dragging Screen\n"));
                    if (coords.x < 0)
                        _this->m_dragpoint.x = 0;
                    else if (coords.x >= _this->m_cliwidth)
                        _this->m_dragpoint.x = _this->m_cliwidth;
                    else
                        _this->m_dragpoint.x = coords.x;
                    if (coords.y < 0)
                        _this->m_dragpoint.y = 0;
                    else if (coords.y >= _this->m_cliheight)
                        _this->m_dragpoint.y = _this->m_cliheight;
                    else
                        _this->m_dragpoint.y = coords.y;
                }
            }
            if (_this->m_draggingDesktop) {
                int dx = 0, dy = 0;
                dx = coords.x - _this->m_dragpoint.x;
                dy = coords.y - _this->m_dragpoint.y;
                if (dx != 0 || dy != 0) {
                    RECT rect1;
                    if (!GetWindowRect(_this->m_hwnd1, &rect1)) {
                        vnclog.Print(9, TEXT("Error %d in GetWindowRect\n"), GetLastError());
                    } else {
                        // drag the screen
                        rect1.left   += dx;
                        rect1.right  += dx;
                        rect1.top    += dy;
                        rect1.bottom += dy;
                        SetWindowPos(_this->m_hwnd1, HWND_TOP,
                                     rect1.left, rect1.top,
                                     rect1.right - rect1.left, rect1.bottom - rect1.top,
                                     SWP_SHOWWINDOW);
                    }
                }
                return 0;
            }

			if (_this->InFullScreenMode() && !_this->InTransRectMode()) {
                if (_this->BumpScroll(coords.x, coords.y))
                    return 0;
			}
			if ( _this->m_opts.m_ViewOnly)
				return 0;

			_this->ProcessPointerEvent(coords.x, coords.y, wParam, iMsg);
			return 0;
		}

	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
		{
			if (!_this->m_running) return 0;
			if ( _this->m_opts.m_ViewOnly) return 0;
			bool down = (((DWORD) lParam & 0x80000000l) == 0);
			if ((int) wParam == 0x11)
                _this->CheckAction(ID_CONN_CTLDOWN, down);
			if ((int) wParam == 0x12)
                _this->CheckAction(ID_CONN_ALTDOWN, down);
			
            _this->ProcessKeyEvent((int) wParam, (DWORD) lParam);
			return 0;
		}
	case WM_CHAR:
	case WM_SYSCHAR:
#ifdef UNDER_CE
        {
            int key = wParam;
            vnclog.Print(4,_T("CHAR msg : %02x\n"), key);
            // Control keys which are in the Keymap table will already
            // have been handled.
            if (key == 0x0D  ||  // return
                key == 0x20 ||   // space
                key == 0x08)     // backspace
                return 0;

            if (key < 32) key += 64;  // map ctrl-keys onto alphabet
            if (key > 32 && key < 127) {
                _this->SendKeyEvent(wParam & 0xff, true);
                _this->SendKeyEvent(wParam & 0xff, false);
            }
            return 0;
        }
#endif
	case WM_DEADCHAR:
	case WM_SYSDEADCHAR:
	  return 0;
	case WM_SETFOCUS:
		if (_this->InFullScreenMode() && !_this->InTransRectMode())
			SetWindowPos(hwnd, HWND_TOPMOST, 0,0,100,100, SWP_NOMOVE | SWP_NOSIZE);
        if (_this->InMetaWMMode() && _this->m_enableKeyFocus)
            _this->SendKeyFocusMsg(rfbKeyFocusIn);
		vnclog.Print(6, _T("Gaining focus\n"));
		return 0;
	// Cancel modifiers when we lose focus
	case WM_KILLFOCUS:
		{
			if (!_this->m_running) return 0;
			if (_this->InFullScreenMode() && !_this->InTransRectMode()) {
				// We must top being topmost, but we want to choose our
				// position carefully.
				HWND foreground = GetForegroundWindow();
				HWND hwndafter = NULL;
				if ((foreground == NULL) || 
					(GetWindowLong(foreground, GWL_EXSTYLE) & WS_EX_TOPMOST)) {
					hwndafter = HWND_NOTOPMOST;
				} else {
					hwndafter = GetNextWindow(foreground, GW_HWNDNEXT); 
				}

				SetWindowPos(_this->m_hwnd1, hwndafter, 0,0,100,100, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			}
            if (_this->InMetaWMMode() && _this->m_enableKeyFocus)
                _this->SendKeyFocusMsg(rfbKeyFocusOut);
			vnclog.Print(6, _T("Losing focus - cancelling modifiers\n"));
			return 0;
		}	
    case WM_QUERYNEWPALETTE:
        {
			TempDC hDC(hwnd);
			
			// Select and realize hPalette
			PaletteSelector p(hDC, _this->m_hPalette);
			InvalidateRect(hwnd, NULL, FALSE);
			UpdateWindow(hwnd);

			return TRUE;
        }

	case WM_PALETTECHANGED:
		// If this application did not change the palette, select
		// and realize this application's palette
		if ((HWND) wParam != hwnd)
		{
			// Need the window's DC for SelectPalette/RealizePalette
			TempDC hDC(hwnd);
			PaletteSelector p(hDC, _this->m_hPalette);
			// When updating the colors for an inactive window,
			// UpdateColors can be called because it is faster than
			// redrawing the client area (even though the results are
			// not as good)
				#ifndef UNDER_CE
				UpdateColors(hDC);
				#else
				InvalidateRect(hwnd, NULL, FALSE);
				UpdateWindow(hwnd);
				#endif

		}
        break;

#ifndef UNDER_CE 
		
	case WM_SETCURSOR:
		{
			// if we have the focus, let the cursor change as normal
			if (GetFocus() == hwnd) 
				break;

			// if not, set to default system cursor
			SetCursor( LoadCursor(NULL, IDC_ARROW));
			return 0;
		}

	case WM_DRAWCLIPBOARD:
		_this->ProcessLocalClipboardChange();
		return 0;

	case WM_CHANGECBCHAIN:
		{
			// The clipboard chain is changing
			HWND hWndRemove = (HWND) wParam;     // handle of window being removed 
			HWND hWndNext = (HWND) lParam;       // handle of next window in chain 
			// If next window is closing, update our pointer.
			if (hWndRemove == _this->m_hwndNextViewer)  
				_this->m_hwndNextViewer = hWndNext;  
			// Otherwise, pass the message to the next link.  
			else if (_this->m_hwndNextViewer != NULL) 
				::SendMessage(_this->m_hwndNextViewer, WM_CHANGECBCHAIN, 
				(WPARAM) hWndRemove,  (LPARAM) hWndNext );  
			return 0;
		}
	
#endif
	}

	return DefWindowProc(hwnd, iMsg, wParam, lParam);
}


// ProcessPointerEvent handles the delicate case of emulating 3 buttons
// on a two button mouse, then passes events off to SubProcessPointerEvent.

void
ClientConnection::ProcessPointerEvent(int x, int y, DWORD keyflags, UINT msg) 
{
	if (m_opts.m_Emul3Buttons) {
		// XXX To be done:
		// If this is a left or right press, the user may be 
		// about to press the other button to emulate a middle press.
		// We need to start a timer, and if it expires without any
		// further presses, then we send the button press. 
		// If a press of the other button, or any release, comes in
		// before timer has expired, we cancel timer & take different action.
		if (m_waitingOnEmulateTimer) {
			if (msg == WM_LBUTTONUP || msg == WM_RBUTTONUP ||
				abs(x - m_emulateButtonPressedX) > m_opts.m_Emul3Fuzz ||
				abs(y - m_emulateButtonPressedY) > m_opts.m_Emul3Fuzz) {
				// if button released or we moved too far then cancel.
				// First let the remote know where the button was down
				SubProcessPointerEvent(
					m_emulateButtonPressedX, 
					m_emulateButtonPressedY, 
					m_emulateKeyFlags);
				// Then tell it where we are now
				SubProcessPointerEvent(x, y, keyflags);
			} else if (
				(msg == WM_LBUTTONDOWN && (m_emulateKeyFlags & MK_RBUTTON))
				|| (msg == WM_RBUTTONDOWN && (m_emulateKeyFlags & MK_LBUTTON)))	{
				// Triggered an emulate; remove left and right buttons, put
				// in middle one.
				DWORD emulatekeys = keyflags & ~(MK_LBUTTON|MK_RBUTTON);
				emulatekeys |= MK_MBUTTON;
				SubProcessPointerEvent(x, y, emulatekeys);
				
				m_emulatingMiddleButton = true;
			} else {
				// handle movement normally & don't kill timer.
				// just remove the pressed button from the mask.
				DWORD keymask = m_emulateKeyFlags & (MK_LBUTTON|MK_RBUTTON);
				DWORD emulatekeys = keyflags & ~keymask;
				SubProcessPointerEvent(x, y, emulatekeys);
				return;
			}
			
			// if we reached here, we don't need the timer anymore.
			KillTimer(m_hwnd, m_emulate3ButtonsTimer);
			m_waitingOnEmulateTimer = false;
		} else if (m_emulatingMiddleButton) {
			if ((keyflags & MK_LBUTTON) == 0 && (keyflags & MK_RBUTTON) == 0)
			{
				// We finish emulation only when both buttons come back up.
				m_emulatingMiddleButton = false;
				SubProcessPointerEvent(x, y, keyflags);
			} else {
				// keep emulating.
				DWORD emulatekeys = keyflags & ~(MK_LBUTTON|MK_RBUTTON);
				emulatekeys |= MK_MBUTTON;
				SubProcessPointerEvent(x, y, emulatekeys);
			}
		} else {
			// Start considering emulation if we've pressed a button
			// and the other isn't pressed.
			if ( (msg == WM_LBUTTONDOWN && !(keyflags & MK_RBUTTON))
				|| (msg == WM_RBUTTONDOWN && !(keyflags & MK_LBUTTON)))	{
				// Start timer for emulation.
				m_emulate3ButtonsTimer = 
					SetTimer(
					m_hwnd, 
					IDT_EMULATE3BUTTONSTIMER, 
					m_opts.m_Emul3Timeout, 
					NULL);
				
				if (!m_emulate3ButtonsTimer) {
					vnclog.Print(0, _T("Failed to create timer for emulating 3 buttons"));
					PostMessage(m_hwnd1, WM_CLOSE, 0, 0);
					return;
				}
				
				m_waitingOnEmulateTimer = true;
				
				// Note that we don't send the event here; we're batching it for
				// later.
				m_emulateKeyFlags = keyflags;
				m_emulateButtonPressedX = x;
				m_emulateButtonPressedY = y;
			} else {
				// just send event noramlly
				SubProcessPointerEvent(x, y, keyflags);
			}
		}
	} else {
		SubProcessPointerEvent(x, y, keyflags);
	}
}

// SubProcessPointerEvent takes windows positions and flags and converts 
// them into VNC ones.

inline void
ClientConnection::SubProcessPointerEvent(int x, int y, DWORD keyflags)
{
	int mask;
  
	if (m_opts.m_SwapMouse) {
		mask = ( ((keyflags & MK_LBUTTON) ? rfbButton1Mask : 0) |
				 ((keyflags & MK_MBUTTON) ? rfbButton3Mask : 0) |
				 ((keyflags & MK_RBUTTON) ? rfbButton2Mask : 0) );
	} else {
		mask = ( ((keyflags & MK_LBUTTON) ? rfbButton1Mask : 0) |
				 ((keyflags & MK_MBUTTON) ? rfbButton2Mask : 0) |
				 ((keyflags & MK_RBUTTON) ? rfbButton3Mask : 0) );
	}

	if ((short)HIWORD(keyflags) > 0) {
		mask |= rfbButton4Mask;
	} else if ((short)HIWORD(keyflags) < 0) {
		mask |= rfbButton5Mask;
	}
	
	try {
		int x_scaled =
			(x + m_hScrollPos) * m_opts.m_scale_den / m_opts.m_scale_num;
		int y_scaled =
			(y + m_vScrollPos) * m_opts.m_scale_den / m_opts.m_scale_num;

		SendPointerEvent(x_scaled, y_scaled, mask);

		if ((short)HIWORD(keyflags) != 0) {
			// Immediately send a "button-up" after mouse wheel event.
			mask &= !(rfbButton4Mask | rfbButton5Mask);
			SendPointerEvent(x_scaled, y_scaled, mask);
		}
	} catch (Exception &e) {
		e.Report();
		PostMessage(m_hwnd1, WM_CLOSE, 0, 0);
	}
}

//
// SendPointerEvent.
//

inline void
ClientConnection::SendPointerEvent(int x, int y, int buttonMask)
{
    rfbPointerEventMsg pe;

    pe.type = rfbPointerEvent;
    pe.buttonMask = buttonMask;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
	SoftCursorMove(x, y);
    pe.x = Swap16IfLE(x);
    pe.y = Swap16IfLE(y);
	WriteExact((char *)&pe, sz_rfbPointerEventMsg);
}

//
// ProcessKeyEvent
//
// Normally a single Windows key event will map onto a single RFB
// key message, but this is not always the case.  Much of the stuff
// here is to handle AltGr (=Ctrl-Alt) on international keyboards.
// Example cases:
//
//    We want Ctrl-F to be sent as:
//      Ctrl-Down, F-Down, F-Up, Ctrl-Up.
//    because there is no keysym for ctrl-f, and because the ctrl
//    will already have been sent by the time we get the F.
//
//    On German keyboards, @ is produced using AltGr-Q, which is
//    Ctrl-Alt-Q.  But @ is a valid keysym in its own right, and when
//    a German user types this combination, he doesn't mean Ctrl-@.
//    So for this we will send, in total:
//
//      Ctrl-Down, Alt-Down,   
//                 (when we get the AltGr pressed)
//
//      Alt-Up, Ctrl-Up, @-Down, Ctrl-Down, Alt-Down 
//                 (when we discover that this is @ being pressed)
//
//      Alt-Up, Ctrl-Up, @-Up, Ctrl-Down, Alt-Down
//                 (when we discover that this is @ being released)
//
//      Alt-Up, Ctrl-Up
//                 (when the AltGr is released)
//
// Modified by utino:
//   When key is released, this function has to send the same key event
//   as it was pressed.  Otherwise, characters will be garbled if non-US
//   keyboard, e.g. Japanese-106, is used.

inline void ClientConnection::ProcessKeyEvent(int virtkey, DWORD keyData)
{
    bool down = ((keyData & 0x80000000l) == 0);

    if (down) {
        KeyActionSpec kas = m_keymap.PCtoX(virtkey, keyData);
		vnclog.Print(4, _T("press vkey 0x%x\n"), virtkey);
		ProcessKeyEvent(kas, virtkey, keyData);
    } else {
        if (m_downKeysym.find(virtkey) != m_downKeysym.end()) {
            vnclog.Print(4, _T("release vkey 0x%x\n"), virtkey);
            ProcessKeyEvent(m_downKeysym[virtkey], virtkey, keyData);
        }
    }
}

void ClientConnection::ProcessKeyEvent(KeyActionSpec kas, int virtkey, DWORD keyData)
{
    bool down = ((keyData & 0x80000000l) == 0);

#ifdef _DEBUG
#ifdef UNDER_CE
	char *keyname = "";
#else
    char keyname[32];
    if (GetKeyNameText(  keyData,keyname, 31)) {
        vnclog.Print(4, _T("Process key: %s (keyData %04x): "), keyname, keyData);
    };
#endif
#endif

	try {
		if (kas.releaseModifiers & KEYMAP_LCONTROL) {
			SendKeyEvent(XK_Control_L, false );
			vnclog.Print(5, _T("fake L Ctrl raised\n"));
		}
		if (kas.releaseModifiers & KEYMAP_LALT) {
			SendKeyEvent(XK_Alt_L, false );
			vnclog.Print(5, _T("fake L Alt raised\n"));
		}
		if (kas.releaseModifiers & KEYMAP_RCONTROL) {
			SendKeyEvent(XK_Control_R, false );
			vnclog.Print(5, _T("fake R Ctrl raised\n"));
		}
		if (kas.releaseModifiers & KEYMAP_RALT) {
			SendKeyEvent(XK_Alt_R, false );
			vnclog.Print(5, _T("fake R Alt raised\n"));
		}
		
		for (int i = 0; kas.keycodes[i] != XK_VoidSymbol && i < MaxKeysPerKey; i++) {
			SendKeyEvent(kas.keycodes[i], down );
			vnclog.Print(4, _T("Sent keysym %04x (%s)\n"), 
				kas.keycodes[i], down ? _T("press") : _T("release"));
		}
		
		if (kas.releaseModifiers & KEYMAP_RALT) {
			SendKeyEvent(XK_Alt_R, true );
			vnclog.Print(5, _T("fake R Alt pressed\n"));
		}
		if (kas.releaseModifiers & KEYMAP_RCONTROL) {
			SendKeyEvent(XK_Control_R, true );
			vnclog.Print(5, _T("fake R Ctrl pressed\n"));
		}
		if (kas.releaseModifiers & KEYMAP_LALT) {
			SendKeyEvent(XK_Alt_L, false );
			vnclog.Print(5, _T("fake L Alt pressed\n"));
		}
		if (kas.releaseModifiers & KEYMAP_LCONTROL) {
			SendKeyEvent(XK_Control_L, false );
			vnclog.Print(5, _T("fake L Ctrl pressed\n"));
		}
		if (virtkey) {
			if (down)
		        m_downKeysym[virtkey] = kas;
			else
				m_downKeysym.erase(virtkey);
		}
	} catch (Exception &e) {
		e.Report();
		PostMessage(m_hwnd1, WM_CLOSE, 0, 0);
	}

}

//
// SendKeyEvent
//

inline void
ClientConnection::SendKeyEvent(CARD32 key, bool down)
{
    rfbKeyEventMsg ke;

    ke.type = rfbKeyEvent;
    ke.down = down ? 1 : 0;
    ke.key = Swap32IfLE(key);
    WriteExact((char *)&ke, sz_rfbKeyEventMsg);
    vnclog.Print(6, _T("SendKeyEvent: key = x%04x status = %s\n"), key, 
        down ? _T("down") : _T("up"));
}

//
// SendKeyFocusMsg (MetaVNC)
//

inline void
ClientConnection::SendKeyFocusMsg(CARD8 state)
{
    rfbKeyFocusMsg kf;

    kf.type = rfbKeyFocus;
    kf.state = state;
    WriteExact((char *)&kf, sz_rfbKeyFocusMsg);
    vnclog.Print(6, _T("SendKeyFocusMsg: state = %d\n"), state);
}

//
// SendWindowControlMsg (MetaVNC)
//

void
ClientConnection::SendWindowControl(CARD32 id, CARD16 control)
{
    rfbWindowControlMsg wc;
    wc.type    = rfbWindowControl;
    wc.control = Swap16IfLE(control);
    wc.id      = Swap32IfLE(id);
    WriteExact((char *)&wc, sz_rfbWindowControlMsg);
    vnclog.Print(6, _T("SendWindowControl: id=%u, control=%u\n"), id, control);
}

//
// SendServerDataReqMsg (MetaVNC)
//

void
ClientConnection::SendServerDataReq(CARD32 id, CARD16 dataType, void *data, CARD32 dataLength)
{
    rfbServerDataReqMsg sdq;
    sdq.type     = rfbServerDataReq;
    sdq.dataType = Swap16IfLE(dataType);
    sdq.id       = Swap32IfLE(id);
    sdq.dataLength = Swap32IfLE(dataLength);
    WriteExact((char *)&sdq, sz_rfbServerDataReqMsg);
    WriteExact((char *)data, dataLength);
    vnclog.Print(6, _T("SendServerDataReq: id=%u, dataType=%u\n"), id, dataType);
}

#ifndef UNDER_CE
//
// SendClientCutText
//

void ClientConnection::SendClientCutText(char *str, int len)
{
    rfbClientCutTextMsg cct;

    cct.type = rfbClientCutText;
    cct.length = Swap32IfLE(len);
    WriteExact((char *)&cct, sz_rfbClientCutTextMsg);
	WriteExact(str, len);
	vnclog.Print(6, _T("Sent %d bytes of clipboard\n"), len);
}
#endif

// Copy any updated areas from the bitmap onto the screen.

inline void ClientConnection::DoBlit() 
{
	if (m_hBitmap == NULL) return;
	if (!m_running) return;
				
	// No other threads can use bitmap DC
	omni_mutex_lock l(m_bitmapdcMutex);

	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(m_hwnd, &ps);

	// Select and realize hPalette
	PaletteSelector p(hdc, m_hPalette);
	ObjectSelector b(m_hBitmapDC, m_hBitmap);
			
	if (m_opts.m_delay) {
		// Display the area to be updated for debugging purposes
		COLORREF oldbgcol = SetBkColor(hdc, RGB(0,0,0));
		::ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &ps.rcPaint, NULL, 0, NULL);
		SetBkColor(hdc,oldbgcol);
		::Sleep(m_pApp->m_options.m_delay);
	}
	
	if (m_opts.m_scaling) {
		int n = m_opts.m_scale_num;
		int d = m_opts.m_scale_den;
		
		// We're about to do some scaling on these values in the StretchBlt
		// We want to make sure that they divide nicely by n so we round them
		// down and up appropriately.
		ps.rcPaint.left =   ((ps.rcPaint.left   + m_hScrollPos) / n * n)         - m_hScrollPos;
		ps.rcPaint.right =  ((ps.rcPaint.right  + m_hScrollPos + n - 1) / n * n) - m_hScrollPos;
		ps.rcPaint.top =    ((ps.rcPaint.top    + m_vScrollPos) / n * n)         - m_vScrollPos;
		ps.rcPaint.bottom = ((ps.rcPaint.bottom + m_vScrollPos + n - 1) / n * n) - m_vScrollPos;
		
		// This is supposed to give better results.  I think my driver ignores it?
		SetStretchBltMode(hdc, HALFTONE);
		// The docs say that you should call SetBrushOrgEx after SetStretchBltMode, 
		// but not what the arguments should be.
		SetBrushOrgEx(hdc, 0,0, NULL);
		
		if (!StretchBlt(
			hdc, 
			ps.rcPaint.left, 
			ps.rcPaint.top, 
			ps.rcPaint.right-ps.rcPaint.left, 
			ps.rcPaint.bottom-ps.rcPaint.top, 
			m_hBitmapDC, 
			(ps.rcPaint.left+m_hScrollPos)     * d / n, 
			(ps.rcPaint.top+m_vScrollPos)      * d / n,
			(ps.rcPaint.right-ps.rcPaint.left) * d / n, 
			(ps.rcPaint.bottom-ps.rcPaint.top) * d / n, 
			SRCCOPY)) 
		{
			vnclog.Print(0, _T("Blit error %d\n"), GetLastError());
			// throw ErrorException("Error in blit!\n");
		};
	} else {
		if (!BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top, 
			ps.rcPaint.right-ps.rcPaint.left, ps.rcPaint.bottom-ps.rcPaint.top, 
			m_hBitmapDC, ps.rcPaint.left+m_hScrollPos, ps.rcPaint.top+m_vScrollPos, SRCCOPY)) 
		{
			vnclog.Print(0, _T("Blit error %d\n"), GetLastError());
			// throw ErrorException("Error in blit!\n");
		}
	}
	
	EndPaint(m_hwnd, &ps);
}

inline void ClientConnection::UpdateScrollbars() 
{
	// We don't update the actual scrollbar info in full-screen mode
	// because it causes them to flicker.
	bool setInfo = !InFullScreenMode();

	SCROLLINFO scri;
	scri.cbSize = sizeof(scri);
	scri.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
	scri.nMin = 0;
	scri.nMax = m_hScrollMax; 
	scri.nPage= m_cliwidth;
	scri.nPos = m_hScrollPos; 
	
	if (setInfo) 
		SetScrollInfo(m_hwndscroll, SB_HORZ, &scri, TRUE);
	
	scri.cbSize = sizeof(scri);
	scri.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
	scri.nMin = 0;
	scri.nMax = m_vScrollMax;     
	scri.nPage= m_cliheight;
	scri.nPos = m_vScrollPos; 
	
	if (setInfo) 
		SetScrollInfo(m_hwndscroll, SB_VERT, &scri, TRUE);
}


void ClientConnection::ShowConnInfo()
{
	TCHAR buf[2048];
#ifndef UNDER_CE
	char kbdname[9];
	GetKeyboardLayoutName(kbdname);
#else
	TCHAR *kbdname = _T("(n/a)");
#endif
	_stprintf(
		buf,
		_T("Connected to: %s\n\r")
		_T("Host: %s port: %d\n\r\n\r")
		_T("Desktop geometry: %d x %d x %d\n\r")
		_T("Using depth: %d\n\r")
		_T("Current protocol version: 3.%d%s\n\r\n\r")
		_T("Current keyboard name: %s\n\r"),
		m_desktopName, m_host, m_port,
		m_si.framebufferWidth, m_si.framebufferHeight, m_si.format.depth,
		m_myFormat.depth,
		m_minorVersion, (m_tightVncProtocol ? "tight" : ""),
		kbdname);
	MessageBox(NULL, buf, _T("VNC connection info"), MB_ICONINFORMATION | MB_OK);
}

// ********************************************************************
//  Methods after this point are generally called by the worker thread.
//  They finish the initialisation, then chiefly read data from the server.
// ********************************************************************


void* ClientConnection::run_undetached(void* arg) {

	vnclog.Print(9, _T("Update-processing thread started\n"));

	m_threadStarted = true;

	try {

		SendFullFramebufferUpdateRequest();

		RealiseFullScreenMode(false);

		m_running = true;
		UpdateWindow(m_hwnd1);

        if (InMetaWMMode()) {
            SetMenuPref();
            SetIconPref();
            RefreshWindowState();
        }
		
		while (!m_bKillThread) {
			
			// Look at the type of the message, but leave it in the buffer 
			CARD8 msgType;
			{
			  omni_mutex_lock l(m_readMutex);  // we need this if we're not using ReadExact
			  int bytes = recv(m_sock, (char *) &msgType, 1, MSG_PEEK);
			  if (bytes == 0) {
                m_pFileTransfer->CloseUndoneFileTransfers();
			    vnclog.Print(0, _T("Connection closed\n") );
			    throw WarningException(_T("Connection with the Application Sharing Session has been closed."));
			  }
			  if (bytes < 0) {
                m_pFileTransfer->CloseUndoneFileTransfers();
			    vnclog.Print(3, _T("Socket error reading message: %d\n"), WSAGetLastError() );
			    throw WarningException("Error while waiting for server message");
			  }
			}
				
			switch (msgType) {
			case rfbFramebufferUpdate:
				ReadScreenUpdate();
				break;
			case rfbSetColourMapEntries:
		        vnclog.Print(3, _T("rfbSetColourMapEntries read but not supported\n") );
				throw WarningException("Unhandled SetColormap message type received!\n");
				break;
			case rfbBell:
				ReadBell();
				break;
			case rfbServerCutText:
				ReadServerCutText();
				break;
			case rfbFileListData:
				m_pFileTransfer->ShowServerItems();
				break;
			case rfbFileDownloadData:
				m_pFileTransfer->FileTransferDownload();
				break;
			case rfbFileUploadCancel:
				m_pFileTransfer->ReadUploadCancel();
				break;
			case rfbFileDownloadFailed:
				m_pFileTransfer->ReadDownloadFailed();
				break;
            case rfbWindowState: // MetaVNC
                ReadWindowState();
                break;
            case rfbServerData: // MetaVNC
                ReadServerData();
                break;
			default:
				vnclog.Print(3, _T("Unknown message type x%02x\n"), msgType );
				throw WarningException("Unhandled message type received!\n");
			}

		}
        
        vnclog.Print(4, _T("Update-processing thread finishing\n") );

	} catch (WarningException &e) {
		m_running = false;
		PostMessage(m_hwnd1, WM_CLOSE, 0, 0);
		if (!m_bKillThread) {
			e.Report();
		}
	} catch (QuietException &e) {
		m_running = false;
		e.Report();
		PostMessage(m_hwnd1, WM_CLOSE, 0, 0);
	} 
	return this;
}


//
// Requesting screen updates from the server
//

inline void
ClientConnection::SendFramebufferUpdateRequest(int x, int y, int w, int h, bool incremental)
{
    rfbFramebufferUpdateRequestMsg fur;

    if (!incremental)
        ResetTransparentRegion(x, y, w, h);

    fur.type = rfbFramebufferUpdateRequest;
    fur.incremental = incremental ? 1 : 0;
    fur.x = Swap16IfLE(x);
    fur.y = Swap16IfLE(y);
    fur.w = Swap16IfLE(w);
    fur.h = Swap16IfLE(h);

	vnclog.Print(10, _T("Request %s update\n"), incremental ? _T("incremental") : _T("full"));
    WriteExact((char *)&fur, sz_rfbFramebufferUpdateRequestMsg);
}

inline void ClientConnection::SendIncrementalFramebufferUpdateRequest()
{
    SendFramebufferUpdateRequest(0, 0, m_si.framebufferWidth,
					m_si.framebufferHeight, true);
}

inline void ClientConnection::SendFullFramebufferUpdateRequest()
{
    SendFramebufferUpdateRequest(0, 0, m_si.framebufferWidth,
					m_si.framebufferHeight, false);
}


void ClientConnection::SendAppropriateFramebufferUpdateRequest()
{
	if (m_pendingFormatChange) {
		vnclog.Print(3, _T("Requesting new pixel format\n") );
		rfbPixelFormat oldFormat = m_myFormat;
		SetupPixelFormat();
		SoftCursorFree();
		SetFormatAndEncodings();
		m_pendingFormatChange = false;
		// If the pixel format has changed, request whole screen
		if (!PF_EQ(m_myFormat, oldFormat)) {
			SendFullFramebufferUpdateRequest();	
		} else {
			SendIncrementalFramebufferUpdateRequest();
		}
	} else {
		if (!m_dormant)
			SendIncrementalFramebufferUpdateRequest();
	}
}


// A ScreenUpdate message has been received

void ClientConnection::ReadScreenUpdate() {

	rfbFramebufferUpdateMsg sut;
	ReadExact((char *) &sut, sz_rfbFramebufferUpdateMsg);
    sut.nRects = Swap16IfLE(sut.nRects);
	if (sut.nRects == 0) return;
	
	for (int i=0; i < sut.nRects; i++) {

		rfbFramebufferUpdateRectHeader surh;
		ReadExact((char *) &surh, sz_rfbFramebufferUpdateRectHeader);

		surh.encoding = Swap32IfLE(surh.encoding);
		surh.r.x = Swap16IfLE(surh.r.x);
		surh.r.y = Swap16IfLE(surh.r.y);
		surh.r.w = Swap16IfLE(surh.r.w);
		surh.r.h = Swap16IfLE(surh.r.h);

		if (surh.encoding == rfbEncodingLastRect)
			break;
		if (surh.encoding == rfbEncodingNewFBSize) {
			ReadNewFBSize(&surh);
			break;
		}

		if ( surh.encoding == rfbEncodingXCursor ||
			 surh.encoding == rfbEncodingRichCursor ) {
			ReadCursorShape(&surh);
			continue;
		}

		if (surh.encoding == rfbEncodingPointerPos) {
			ReadCursorPos(&surh);
			continue;
		}

		// If *Cursor encoding is used, we should prevent collisions
		// between framebuffer updates and cursor drawing operations.
		SoftCursorLockArea(surh.r.x, surh.r.y, surh.r.w, surh.r.h);

		switch (surh.encoding) {
		case rfbEncodingRaw:
			ReadRawRect(&surh);
			break;
		case rfbEncodingCopyRect:
			ReadCopyRect(&surh);
			break;
        case rfbEncodingTransRect:
            ReadTransRect(&surh);
            break;
		case rfbEncodingRRE:
			ReadRRERect(&surh);
			break;
		case rfbEncodingCoRRE:
			ReadCoRRERect(&surh);
			break;
		case rfbEncodingHextile:
			ReadHextileRect(&surh);
			break;
		case rfbEncodingZlib:
			ReadZlibRect(&surh);
			break;
		case rfbEncodingTight:
			ReadTightRect(&surh);
			break;
		case rfbEncodingZlibHex:
			ReadZlibHexRect(&surh);
			break;
		default:
			vnclog.Print(0, _T("Unknown encoding %d - not supported!\n"), surh.encoding);
			break;
		}

		// Tell the system to update a screen rectangle. Note that
		// InvalidateScreenRect member function knows about scaling.
		RECT rect;
		SetRect(&rect, surh.r.x, surh.r.y,
				surh.r.x + surh.r.w, surh.r.y + surh.r.h);
		InvalidateScreenRect(&rect);

		// Now we may discard "soft cursor locks".
		SoftCursorUnlockScreen();
	}	

	// Inform the other thread that an update is needed.
	PostMessage(m_hwnd, WM_REGIONUPDATED, NULL, NULL);
}	

void ClientConnection::SetDormant(bool newstate)
{
	vnclog.Print(5, _T("%s dormant mode\n"), newstate ? _T("Entering") : _T("Leaving"));
	m_dormant = newstate;
	if (!m_dormant)
		SendIncrementalFramebufferUpdateRequest();
}

// The server has copied some text to the clipboard - put it 
// in the local clipboard too.

void ClientConnection::ReadServerCutText() {
	rfbServerCutTextMsg sctm;
	vnclog.Print(6, _T("Read remote clipboard change\n"));
	ReadExact((char *) &sctm, sz_rfbServerCutTextMsg);
	int len = Swap32IfLE(sctm.length);
	
	CheckBufferSize(len);
	if (len == 0) {
		m_netbuf[0] = '\0';
	} else {
		ReadString(m_netbuf, len);
	}
	UpdateLocalClipboard(m_netbuf, len);
}


void ClientConnection::ReadBell() {
	rfbBellMsg bm;
	ReadExact((char *) &bm, sz_rfbBellMsg);

	#ifdef UNDER_CE
	MessageBeep( MB_OK );
	#else

	if (! ::PlaySound("VNCViewerBell", NULL, 
		SND_APPLICATION | SND_ALIAS | SND_NODEFAULT | SND_ASYNC) ) {
		::Beep(440, 125);
	}
	#endif
	if (m_opts.m_DeiconifyOnBell) {
		if (IsIconic(m_hwnd1)) {
			SetDormant(false);
			ShowWindow(m_hwnd1, SW_SHOWNORMAL);
		}
	}
	vnclog.Print(6, _T("Bell!\n"));
}


// MetaVNC
void ClientConnection::ReadWindowState() {
	rfbWindowStateMsg ws;
    ULONG id, state, len;
	ReadExact((char *) &ws, sz_rfbWindowStateMsg);
    id    = Swap32IfLE(ws.id);
    state = Swap16IfLE(ws.state);
    len   = ws.nameLength;
	vnclog.Print(6, _T("ReadWindowState: id=0x%X, state=0x%X, namelen=%u\n"), id, state, len);
    if (len > 0) {
        CheckBufferSize(len);
        ReadExact(m_netbuf, len);
    }
    if (InMetaWMMode()) {
        if (state & rfbWindowStateUnknown) {
            m_metaLauncher->clear();
            m_metaTaskbar->clear();
        } else if (state & rfbWindowStateLauncher) {
            vnclog.Print(6, _T("ReadWindowState: updating launcher\n"));
            m_metaLauncher->createMenuItem(id, m_netbuf, len);
        } else {
            vnclog.Print(6, _T("ReadWindowState: updating meta taskbar\n"));
            m_metaTaskbar->update(id, state);
            if (len > 0)
                m_metaTaskbar->updateText(id, m_netbuf, len);
        }
    }
}

// MetaVNC
void ClientConnection::ReadServerData() {
	rfbServerDataMsg sd;
    ULONG id, dataType, dataLength;
	ReadExact((char *) &sd, sz_rfbServerDataMsg);
    id         = Swap32IfLE(sd.id);
    dataType   = Swap16IfLE(sd.dataType);
    dataLength = Swap32IfLE(sd.dataLength);
	vnclog.Print(6, _T("ReadServerData: id=0x%X, dataType=%u, dataLength=%u\n"), id, dataType, dataLength);
    if (dataLength > 0) {
        CheckBufferSize(dataLength);
        ReadExact(m_netbuf, dataLength);
    }
    if (InMetaWMMode()) {
        switch (dataType) {
        case rfbServerDataMenuIcon:
            vnclog.Print(6, _T("ReadServerData: updating menu icon\n"));
            m_metaLauncher->createIconFile(id, (rfbServerDataIcon *)m_netbuf, dataLength);
            break;
        case rfbServerDataWindowIcon:
            vnclog.Print(6, _T("ReadServerData: updating taskbar icon\n"));
            m_metaTaskbar->setIcon(id, (rfbServerDataIcon *)m_netbuf, dataLength);
            break;
        }
    }
}


// General utilities -------------------------------------------------

// Reads the number of bytes specified into the buffer given
void ClientConnection::ReadExact(char *inbuf, int wanted)
{
	if (m_sock == INVALID_SOCKET && m_bKillThread)
		throw QuietException("Connection closed.");

	omni_mutex_lock l(m_readMutex);

	int offset = 0;
    vnclog.Print(10, _T("  reading %d bytes\n"), wanted);
	
	while (wanted > 0) {

		int bytes = recv(m_sock, inbuf+offset, wanted, 0);
		if (bytes == 0) throw WarningException("Connection closed.");
		if (bytes == SOCKET_ERROR) {
			int err = ::GetLastError();
			vnclog.Print(1, _T("Socket error while reading %d\n"), err);
			m_running = false;
			throw WarningException("ReadExact: Socket error while reading.");
		}
		wanted -= bytes;
		offset += bytes;

	}
}

// Read the number of bytes and return them zero terminated in the buffer 
inline void ClientConnection::ReadString(char *buf, int length)
{
	if (length > 0)
		ReadExact(buf, length);
	buf[length] = '\0';
    vnclog.Print(10, _T("Read a %d-byte string\n"), length);
}


// Sends the number of bytes specified from the buffer
inline void ClientConnection::WriteExact(char *buf, int bytes)
{
	if (bytes == 0 || m_sock == INVALID_SOCKET)
		return;

	omni_mutex_lock l(m_writeMutex);
	vnclog.Print(10, _T("  writing %d bytes\n"), bytes);

	int i = 0;
    int j;

    while (i < bytes) {

		j = send(m_sock, buf+i, bytes-i, 0);
		if (j == SOCKET_ERROR || j==0) {
			LPVOID lpMsgBuf;
			int err = ::GetLastError();
			FormatMessage(     
				FORMAT_MESSAGE_ALLOCATE_BUFFER | 
				FORMAT_MESSAGE_FROM_SYSTEM |     
				FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
				err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
				(LPTSTR) &lpMsgBuf, 0, NULL ); // Process any inserts in lpMsgBuf.
			vnclog.Print(1, _T("Socket error %d: %s\n"), err, lpMsgBuf);
			LocalFree( lpMsgBuf );
			m_running = false;

			throw WarningException("WriteExact: Socket error while writing.");
		}
		i += j;
    }
}

// Read the string describing the reason for a connection failure.
// This function reads the data into m_netbuf, and returns that pointer
// as the beginning of the reason string.
char *ClientConnection::ReadFailureReason()
{
	CARD32 reasonLen;
	ReadExact((char *)&reasonLen, sizeof(reasonLen));
	reasonLen = Swap32IfLE(reasonLen);

	CheckBufferSize(reasonLen + 1);
	ReadString(m_netbuf, reasonLen);

	vnclog.Print(0, _T("RFB connection failed, reason: %s\n"), m_netbuf);
	return m_netbuf;
}

// Makes sure netbuf is at least as big as the specified size.
// Note that netbuf itself may change as a result of this call.
// Throws an exception on failure.
void ClientConnection::CheckBufferSize(int bufsize)
{
	if (m_netbufsize > bufsize) return;

	omni_mutex_lock l(m_bufferMutex);

	char *newbuf = new char[bufsize+256];;
	if (newbuf == NULL) {
		throw ErrorException("Insufficient memory to allocate network buffer.");
	}

	// Only if we're successful...

	if (m_netbuf != NULL)
		delete [] m_netbuf;
	m_netbuf = newbuf;
	m_netbufsize=bufsize + 256;
	vnclog.Print(4, _T("bufsize expanded to %d\n"), m_netbufsize);
}

// Makes sure zlibbuf is at least as big as the specified size.
// Note that zlibbuf itself may change as a result of this call.
// Throws an exception on failure.
void ClientConnection::CheckZlibBufferSize(int bufsize)
{
	unsigned char *newbuf;

	if (m_zlibbufsize > bufsize) return;

	// omni_mutex_lock l(m_bufferMutex);

	newbuf = (unsigned char *)new char[bufsize+256];
	if (newbuf == NULL) {
		throw ErrorException("Insufficient memory to allocate zlib buffer.");
	}

	// Only if we're successful...

	if (m_zlibbuf != NULL)
		delete [] m_zlibbuf;
	m_zlibbuf = newbuf;
	m_zlibbufsize=bufsize + 256;
	vnclog.Print(4, _T("zlibbufsize expanded to %d\n"), m_zlibbufsize);


}

//
// Invalidate a screen rectangle respecting scaling set by user.
//

void ClientConnection::InvalidateScreenRect(const RECT *pRect) {
	RECT rect;

	// If we're scaling, we transform the coordinates of the rectangle
	// received into the corresponding window coords, and invalidate
	// *that* region.

	if (m_opts.m_scaling) {
		// First, we adjust coords to avoid rounding down when scaling.
		int n = m_opts.m_scale_num;
		int d = m_opts.m_scale_den;
		int left   = (pRect->left / d) * d;
		int top    = (pRect->top  / d) * d;
		int right  = (pRect->right  + d - 1) / d * d; // round up
		int bottom = (pRect->bottom + d - 1) / d * d; // round up

		// Then we scale the rectangle, which should now give whole numbers.
		rect.left   = (left   * n / d) - m_hScrollPos;
		rect.top    = (top    * n / d) - m_vScrollPos;
		rect.right  = (right  * n / d) - m_hScrollPos;
		rect.bottom = (bottom * n / d) - m_vScrollPos;
	} else {
		rect.left   = pRect->left   - m_hScrollPos;
		rect.top    = pRect->top    - m_vScrollPos;
		rect.right  = pRect->right  - m_hScrollPos;
		rect.bottom = pRect->bottom - m_vScrollPos;
	}
	InvalidateRect(m_hwnd, &rect, FALSE);
}

//
// Processing NewFBSize pseudo-rectangle. Create new framebuffer of
// the size specified in pfburh->r.w and pfburh->r.h, and change the
// window size correspondingly.
//

void ClientConnection::ReadNewFBSize(rfbFramebufferUpdateRectHeader *pfburh)
{
	m_si.framebufferWidth = pfburh->r.w;
	m_si.framebufferHeight = pfburh->r.h;

	CreateLocalFramebuffer();

	SizeWindow(false);
	RealiseFullScreenMode(true);
}

//
// reset transparent region
//
bool ClientConnection::ResetTransparentRegion(int x, int y, int w, int h)
{
    HRGN hRgn = CreateRectRgn(0, 0, 0, 0);
    if (m_opts.m_scaling) {
        SetRectRgn(hRgn,
                   x     * m_opts.m_scale_num / m_opts.m_scale_den,
                   y     * m_opts.m_scale_num / m_opts.m_scale_den,
                   (x+w) * m_opts.m_scale_num / m_opts.m_scale_den,
                   (y+h) * m_opts.m_scale_num / m_opts.m_scale_den);
    } else {
        SetRectRgn(hRgn, x, y, x+w, y+h);
    }
    if (CombineRgn(m_hTransRgn, m_hTransRgn, hRgn, RGN_DIFF) == ERROR) {
        vnclog.Print(0, TEXT("Error %d in CombineRgn\n"), GetLastError());
        DeleteObject(hRgn);
        return false;
    }
    DeleteObject(hRgn);
    return true;
}

//
// set window shape
// transparentize the transparent region
//
bool ClientConnection::SetWindowShape()
{
    RECT scrnRect;
    RECT clipRect;
    RECT clipOffset;
    HRGN hScrnRgn;
    HRGN hClipRgn;
    int dx, dy;

    if (!(GetWindowRect(m_hwnd1, &scrnRect) &&
          GetWindowRect(m_hwnd, &clipOffset) &&
          GetClientRect(m_hwnd, &clipRect))) {
        vnclog.Print(9, TEXT("Error %d in GetWindowRect\n"), GetLastError());
        return false;
    }

    /* get offset (dx, dy) */
    dx = clipOffset.left - scrnRect.left;
    dy = clipOffset.top  - scrnRect.top;

    /* set hScrnRgn -- XXX does not support fancy frames */
    hScrnRgn = CreateRectRgn(0, 0, 0, 0);
    if (!SetRectRgn(hScrnRgn, 1, 1, scrnRect.right - scrnRect.left - 2, scrnRect.bottom - scrnRect.top - 2)) {
        vnclog.Print(9, TEXT("Error %d in SetRectRgn\n"), GetLastError());
        DeleteObject(hScrnRgn);
        return false;
    }

    /* check if TransRect is enabled */
    if (!m_opts.m_UseTransRect) {
        SetWindowRgn(m_hwnd1, hScrnRgn, TRUE);
        return true;
    }

    /* hClipRgn = clipRect */
    hClipRgn = CreateRectRgnIndirect(&clipRect);
    if (hClipRgn == NULL) {
        vnclog.Print(9, TEXT("Error %d in CreateRectRgn\n"), GetLastError());
        DeleteObject(hScrnRgn);
        return false;
    }

    /* hClipRgn += (dx, dy) */
    if (OffsetRgn(hClipRgn, dx, dy) == ERROR) {
        vnclog.Print(9, TEXT("Error %d in OffsetRgn\n"), GetLastError());
        DeleteObject(hScrnRgn);
        DeleteObject(hClipRgn);
        return false;
    }

    /* hScrnRgn -= hClipRgn */
    if (CombineRgn(hScrnRgn, hScrnRgn, hClipRgn, RGN_DIFF) == ERROR) {
        vnclog.Print(9, TEXT("Error %d in CombineRgn\n"), GetLastError());
        DeleteObject(hScrnRgn);
        DeleteObject(hClipRgn);
        return false;
    }

    /* hClipRgn += (m_hScrollPos - dx, m_vScrollPos - dy) */
    if (OffsetRgn(hClipRgn, m_hScrollPos - dx, m_vScrollPos - dy) == ERROR) {
        vnclog.Print(9, TEXT("Error %d in OffsetRgn\n"), GetLastError());
        DeleteObject(hScrnRgn);
        DeleteObject(hClipRgn);
        return false;
    }

    /* hClipRgn -= m_hTransRgn */
    if (CombineRgn(hClipRgn, hClipRgn, m_hTransRgn, RGN_DIFF) == ERROR) {
        vnclog.Print(9, TEXT("Error %d in CombineRgn\n"), GetLastError());
        DeleteObject(hScrnRgn);
        DeleteObject(hClipRgn);
        return false;
    }

    /* hClipRgn += (dx - m_hScrollPos, dy - m_vScrollPos) */
    if (OffsetRgn(hClipRgn, dx - m_hScrollPos, dy - m_vScrollPos) == ERROR) {
        vnclog.Print(9, TEXT("Error %d in OffsetRgn\n"), GetLastError());
        DeleteObject(hScrnRgn);
        DeleteObject(hClipRgn);
        return false;
    }

    /* hScrnRgn += hClipRgn */
    if (CombineRgn(hScrnRgn, hScrnRgn, hClipRgn, RGN_OR) == ERROR) {
        vnclog.Print(9, TEXT("Error %d in CombineRgn\n"), GetLastError());
        DeleteObject(hScrnRgn);
        DeleteObject(hClipRgn);
        return false;
    }

    /* update window region */
    SetWindowRgn(m_hwnd1, hScrnRgn, TRUE);
    DeleteObject(hClipRgn);
    return true;
}
