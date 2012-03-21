//  Copyright (C) 2004 UCHINO Satoshi. All Rights Reserved.
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
//
// TransRect Encoding

#include "stdhdrs.h"
#include "vncviewer.h"
#include "ClientConnection.h"

void ClientConnection::ReadTransRect(rfbFramebufferUpdateRectHeader *pfburh) {
	rfbTransRectHeader trh;
	ReadExact((char *) &trh, sz_rfbTransRectHeader);
	trh.nIncRects = Swap32IfLE(trh.nIncRects);
	trh.nDecRects = Swap32IfLE(trh.nDecRects);

vnclog.Print(9, _T("TransRect: (%d,%d)-(%d,%d), #inc:%d, #dec:%d\n"), pfburh->r.x, pfburh->r.y, pfburh->r.w, pfburh->r.h, trh.nIncRects, trh.nDecRects);

    // read IncRects and DecRects into the buffer
    CheckBufferSize(sz_rfbRectangle * (trh.nIncRects + trh.nDecRects));
    ReadExact(m_netbuf, sz_rfbRectangle * (trh.nIncRects + trh.nDecRects));
	rfbRectangle *pRect;
    unsigned int i, x1, y1, x2, y2;
    HRGN hRgn = CreateRectRgn(0, 0, 0, 0);
    
    // update screen region
    pRect = (rfbRectangle *)m_netbuf;

    // concat IncRects to m_hTransRgn
    for (i = 0; i < trh.nIncRects; i++) {
		x1 = Swap16IfLE(pRect->x) + pfburh->r.x;
		y1 = Swap16IfLE(pRect->y) + pfburh->r.y;
		x2 = Swap16IfLE(pRect->w) + x1;
		y2 = Swap16IfLE(pRect->h) + y1;
        vnclog.Print(9, _T("IncRect[%d]: (%d,%d)-(%d,%d)\n"), i, x1, y1, x2, y2);
        if (m_opts.m_scaling) {
            SetRectRgn(hRgn,
                       x1 * m_opts.m_scale_num / m_opts.m_scale_den,
                       y1 * m_opts.m_scale_num / m_opts.m_scale_den,
                       x2 * m_opts.m_scale_num / m_opts.m_scale_den,
                       y2 * m_opts.m_scale_num / m_opts.m_scale_den);
        } else {
            SetRectRgn(hRgn, x1, y1, x2, y2);
        }
        if (CombineRgn(m_hTransRgn, m_hTransRgn, hRgn, RGN_OR) == ERROR) {
            vnclog.Print(0, TEXT("Error %d in CombineRgn\n"), GetLastError());
        }
        pRect++;
    }

    // subtract DecRects from m_hTransRgn
    for (i = 0; i < trh.nDecRects; i++) {
		x1 = Swap16IfLE(pRect->x) + pfburh->r.x;
		y1 = Swap16IfLE(pRect->y) + pfburh->r.y;
		x2 = Swap16IfLE(pRect->w) + x1;
		y2 = Swap16IfLE(pRect->h) + y1;
        vnclog.Print(9, _T("DecRect[%d]: (%d,%d)-(%d,%d)\n"), i, x1, y1, x2, y2);
        if (m_opts.m_scaling) {
            SetRectRgn(hRgn,
                       x1 * m_opts.m_scale_num / m_opts.m_scale_den,
                       y1 * m_opts.m_scale_num / m_opts.m_scale_den,
                       x2 * m_opts.m_scale_num / m_opts.m_scale_den,
                       y2 * m_opts.m_scale_num / m_opts.m_scale_den);
        } else {
            SetRectRgn(hRgn, x1, y1, x2, y2);
        }
        if (CombineRgn(m_hTransRgn, m_hTransRgn, hRgn, RGN_DIFF) == ERROR) {
            vnclog.Print(0, TEXT("Error %d in CombineRgn\n"), GetLastError());
        }
        pRect++;
    }
    DeleteObject(hRgn);

    SetWindowShape();
#if 0
	{
    // get region data
    DWORD dwSize;
    RGNDATA *pRgn;
    dwSize = GetRegionData(m_hScrnRgn, 0, NULL);
    pRgn = (RGNDATA*)GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, dwSize);
    if (pRgn == NULL) {
        vnclog.Print(9, TEXT("memory allocation failed\n"));
        return;
    }
    if (GetRegionData(m_hScrnRgn, dwSize, pRgn) == 0) {
        vnclog.Print(9, TEXT("no region data exists\n"));
        GlobalFree((HGLOBAL)pRgn);
        return;
    }

    vnclog.Print(9, TEXT("Region: (%d,%d)-(%d,%d), #subrect=%d\n"),
                 pRgn->rdh.rcBound.left,
                 pRgn->rdh.rcBound.top,
                 pRgn->rdh.rcBound.right,
                 pRgn->rdh.rcBound.bottom,
                 pRgn->rdh.nCount);
    RECT *rect = (RECT *)pRgn->Buffer;
    for (int i = 0; i < pRgn->rdh.nCount; i++) {
        vnclog.Print(9, TEXT("subregion %d: (%d,%d)-(%d,%d)\n"),
                     i,
                     rect->left,
                     rect->top,
                     rect->right,
                     rect->bottom);
        rect++;
    }
    GlobalFree((HGLOBAL)pRgn);
	}
#endif
}
