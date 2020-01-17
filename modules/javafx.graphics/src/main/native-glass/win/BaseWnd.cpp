/*
 * Copyright (c) 2011, 2013, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include "common.h"

#include "BaseWnd.h"
#include "GlassApplication.h"


//NOTE: it's not thread-safe
unsigned int BaseWnd::sm_classNameCounter = 0;

static LPCTSTR szBaseWndProp = TEXT("BaseWndProp");

BaseWnd::BaseWnd(HWND ancestor) :
    m_hWnd(NULL),
    m_ancestor(ancestor),
    m_wndClassAtom(0),
    m_isCommonDialogOwner(false),
    m_hCursor(NULL)
{

}

BaseWnd::~BaseWnd()
{
    if (m_wndClassAtom)
    {
        // This is called from WM_NCDESTROY, and ::UnregisterClass() will fail.
        // Schedule the operation for later time when the HWND is dead and
        // the window class is really free already.
        ENTER_MAIN_THREAD()
        {
            if (!::UnregisterClass(reinterpret_cast<LPCTSTR>(wndClassAtom),
                        ::GetModuleHandle(NULL)))
            {
                _tprintf_s(L"BaseWnd::UnregisterClass(%i) error: %u\n",
                        (int)wndClassAtom, ::GetLastError());
            }
        }
        ATOM wndClassAtom;
        LEAVE_MAIN_THREAD_LATER;

        ARG(wndClassAtom) = m_wndClassAtom;

        PERFORM_LATER();
    }
}

/*static*/
BaseWnd* BaseWnd::FromHandle(HWND hWnd)
{
    return (BaseWnd *)::GetProp(hWnd, szBaseWndProp);
}

HWND BaseWnd::Create(HWND hParent, int x, int y, int width, int height,
        LPCTSTR lpWindowName, DWORD dwExStyle, DWORD dwStyle, HBRUSH hbrBackground)
{
    fprintf(stderr, "[JSDBG] BaseWnd.Create() A: x = %d, y = %d, width = %d, height = %d, dwExStyle = %d, dwStyle = %d\n", x, y, width, height, dwExStyle, dwStyle);
    ::EnumWindows(StaticEnumWindowsProc, reinterpret_cast<LPARAM>(this));
    fprintf(stderr, "Found existing hWnd? %p\n", GetHWND());

    if (GetHWND() == NULL) {
        HINSTANCE hInst = ::GetModuleHandle(NULL);
        fprintf(stderr, "[JSDBG] BaseWnd.Create() B\n");
        TCHAR szClassName[256];
        fprintf(stderr, "[JSDBG] BaseWnd.Create() C\n");

        ::ZeroMemory(szClassName, sizeof(szClassName));
        _stprintf_s(szClassName, sizeof(szClassName)/sizeof(szClassName[0]),
                _T("GlassWndClass-%s-%u"), GetWindowClassNameSuffix(), ++BaseWnd::sm_classNameCounter);
        fprintf(stderr, "[JSDBG] BaseWnd.Create() D: szClassName = %s\n", szClassName);

        WNDCLASSEX wndcls;
        wndcls.cbSize           = sizeof(WNDCLASSEX);
        wndcls.style            = CS_HREDRAW | CS_VREDRAW;
        wndcls.lpfnWndProc      = StaticWindowProc;
        wndcls.cbClsExtra       = 0;
        wndcls.cbWndExtra       = 0;
        wndcls.hInstance        = hInst;
        wndcls.hIcon            = NULL;
        wndcls.hCursor          = ::LoadCursor(NULL, IDC_ARROW);
        wndcls.hbrBackground    = hbrBackground;
        wndcls.lpszMenuName     = NULL;
        wndcls.lpszClassName    = szClassName;
        wndcls.hIconSm          = NULL;
        fprintf(stderr, "[JSDBG] BaseWnd.Create() E\n");

        m_hCursor               = wndcls.hCursor;

        m_wndClassAtom = ::RegisterClassEx(&wndcls);
        fprintf(stderr, "[JSDBG] BaseWnd.Create() F\n");

        if (!m_wndClassAtom) {
            fprintf(stderr, "[JSDBG] BaseWnd.Create() G\n");
            _tprintf_s(L"BaseWnd::RegisterClassEx(%s) error: %u\n", szClassName, ::GetLastError());
        } else {
            fprintf(stderr, "[JSDBG] BaseWnd.Create() H\n");
            if (lpWindowName == NULL) {
                lpWindowName = TEXT("");
            }
            fprintf(stderr, "[JSDBG] BaseWnd.Create() I: lpWindowName = %s\n", lpWindowName);
            ::CreateWindowEx(dwExStyle, szClassName, lpWindowName,
                    dwStyle, x, y, width, height, hParent,
                    NULL, hInst, (void *)this);
            fprintf(stderr, "[JSDBG] BaseWnd.Create() J\n");

            if (GetHWND() == NULL) {
                fprintf(stderr, "[JSDBG] BaseWnd.Create() K\n");
                _tprintf_s(L"BaseWnd::Create(%s) error: %u\n", szClassName, ::GetLastError());
            }

            fprintf(stderr, "[JSDBG] BaseWnd.Create() L\n");
        }

        fprintf(stderr, "[JSDBG] BaseWnd.Create() M: m_hWnd = %p\n", m_hWnd);
    }

    return m_hWnd;
}

/*static*/
BOOL BaseWnd::GetDefaultWindowBounds(LPRECT r)
{
    HINSTANCE hInst = ::GetModuleHandle(NULL);
    TCHAR* szClassName = L"GLASSDEFAULTWINDOW";

    WNDCLASS wndcls;
    ::ZeroMemory(&wndcls, sizeof(WNDCLASS));
    wndcls.lpfnWndProc      = StaticWindowProc;
    wndcls.hInstance        = hInst;
    wndcls.lpszClassName    = szClassName;
    ::RegisterClass(&wndcls);

    HWND hwnd = ::CreateWindow(szClassName, L"", WS_OVERLAPPED,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                               0, 0, 0, 0);
    BOOL res = ::GetWindowRect(hwnd, r);
    ::DestroyWindow(hwnd);
    ::UnregisterClass(szClassName, hInst);

    return res;
}

/*static*/
BOOL CALLBACK BaseWnd::StaticEnumWindowsProc(HWND hWnd, LPARAM lParam)
{
    DWORD dwCurrentProcessId = ::GetCurrentProcessId();
    DWORD dwProcessId;
    ::GetWindowThreadProcessId(hWnd, &dwProcessId);
    if (dwProcessId == dwCurrentProcessId) {
        BaseWnd *pThis = (BaseWnd *) lParam;
        ::SetProp(hWnd, szBaseWndProp, (HANDLE)pThis);
        if (pThis != NULL) {
            pThis->m_hWnd = hWnd;
        }
    }

    return TRUE;
}

LRESULT CALLBACK BaseWnd::StaticWindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
//    fprintf(stderr, "[JSDBG] BaseWnd.StaticWindowProc() A: hWnd = %p, msg = %d\n", hWnd, msg);
    BaseWnd *pThis = NULL;
    if (msg == WM_CREATE) {
        fprintf(stderr, "[JSDBG] BaseWnd.StaticWindowProc() Aaa\n");
        pThis = (BaseWnd *)((CREATESTRUCT *)lParam)->lpCreateParams;
        fprintf(stderr, "[JSDBG] BaseWnd.StaticWindowProc() Aab\n");
        ::SetProp(hWnd, szBaseWndProp, (HANDLE)pThis);
        fprintf(stderr, "[JSDBG] BaseWnd.StaticWindowProc() Aac\n");
        if (pThis != NULL) {
            fprintf(stderr, "[JSDBG] BaseWnd.StaticWindowProc() Aad\n");
            pThis->m_hWnd = hWnd;
            fprintf(stderr, "[JSDBG] BaseWnd.StaticWindowProc() Aae\n");
        }
        fprintf(stderr, "[JSDBG] BaseWnd.StaticWindowProc() Aaf\n");
    } else {
        pThis = (BaseWnd *)::GetProp(hWnd, szBaseWndProp);
    }

    if (pThis != NULL) {
        fprintf(stderr, "[JSDBG] BaseWnd.StaticWindowProc() Ba\n");
        LRESULT result = pThis->WindowProc(msg, wParam, lParam);
        fprintf(stderr, "[JSDBG] BaseWnd.StaticWindowProc() Bb\n");
        if (msg == WM_NCDESTROY) {
            fprintf(stderr, "[JSDBG] BaseWnd.StaticWindowProc() Bba\n");
            ::RemoveProp(hWnd, szBaseWndProp);
            fprintf(stderr, "[JSDBG] BaseWnd.StaticWindowProc() Bbb\n");
            delete pThis;
            fprintf(stderr, "[JSDBG] BaseWnd.StaticWindowProc() Bbc\n");
        }
        fprintf(stderr, "[JSDBG] BaseWnd.StaticWindowProc() Bc\n");
        return result;
    }
    fprintf(stderr, "[JSDBG] BaseWnd.StaticWindowProc() C\n");
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}

/*virtual*/
MessageResult BaseWnd::CommonWindowProc(UINT msg, WPARAM wParam, LPARAM lParam)
{
    static const MessageResult NOT_PROCESSED;

    switch (msg) {
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                ::SetCursor(m_hCursor);
                return TRUE;
            }
            break;
    }

    return NOT_PROCESSED;
}

void BaseWnd::SetCursor(HCURSOR cursor)
{
    m_hCursor = cursor;

    // Might be worth checking the current cursor position.
    // However, we've always set cursor unconditionally relying on the caller
    // invoking this method only when it processes mouse_move or alike events.
    // As long as there's no bugs filed, let it be.
    ::SetCursor(m_hCursor);
}

