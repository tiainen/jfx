/*
 * Copyright (c) 2011, 2016, Oracle and/or its affiliates. All rights reserved.
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

#include "GlassApplication.h"
#include "GlassClipboard.h"
#include "GlassScreen.h"
#include "GlassWindow.h"
#include "Timer.h"

#include "com_sun_glass_ui_win_WinApplication.h"
#include "com_sun_glass_ui_win_WinSystemClipboard.h"


/**********************************
 * GlassApplication
 **********************************/

static LPCTSTR szGlassToolkitWindow = TEXT("GlassToolkitWindowClass");

GlassApplication *GlassApplication::pInstance = NULL;
bool GlassApplication::sm_shouldLeaveNestedLoop = false;
JGlobalRef<jobject> GlassApplication::sm_nestedLoopReturnValue;

jobject GlassApplication::sm_glassClassLoader;
HINSTANCE GlassApplication::hInstace = NULL;
unsigned int GlassApplication::sm_mouseLLHookCounter = 0;
HHOOK GlassApplication::sm_hMouseLLHook = NULL;

jfloat GlassApplication::overrideUIScale = -1.0f;

/* static */
void GlassApplication::SetGlassClassLoader(JNIEnv *env, jobject classLoader)
{
    sm_glassClassLoader = env->NewGlobalRef(classLoader);
}

/*
 * Function to find a glass class using the glass class loader. All glass
 * classes except those called from initIDs must be looked up using this
 * function rather than FindClass so that the correct ClassLoader is used.
 *
 * Note that the className passed to this function must use "." rather than "/"
 * as a package separator.
 */
/* static */
jclass GlassApplication::ClassForName(JNIEnv *env, char *className)
{
    // TODO: cache classCls as JNI global ref
    jclass classCls = env->FindClass("java/lang/Class");
    if (CheckAndClearException(env) || !classCls) {
        fprintf(stderr, "ClassForName error: classCls == NULL");
        return NULL;
    }

    // TODO: cache forNameMID as static
    jmethodID forNameMID =
        env->GetStaticMethodID(classCls, "forName", "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;");
    if (CheckAndClearException(env) || !forNameMID) {
        fprintf(stderr, "ClassForName error: forNameMID == NULL");
        return NULL;
    }

    jstring classNameStr = env->NewStringUTF(className);
    if (CheckAndClearException(env) || classNameStr == NULL) {
        fprintf(stderr, "ClassForName error: classNameStrs == NULL");
        return NULL;
    }

    jclass foundClass = (jclass)env->CallStaticObjectMethod(classCls,
        forNameMID, classNameStr, JNI_TRUE, sm_glassClassLoader);
    if (CheckAndClearException(env)) return NULL;

    env->DeleteLocalRef(classNameStr);
    env->DeleteLocalRef(classCls);

    return foundClass;
}

GlassApplication::GlassApplication(jobject jrefThis) : BaseWnd()
{
    m_grefThis = GetEnv()->NewGlobalRef(jrefThis);
    m_clipboard = NULL;
    m_hNextClipboardView = NULL;
    m_mainThreadId = ::GetCurrentThreadId();

    Create(NULL, 0, 0, 400, 300, TEXT(""), 0, 0, NULL);
}

GlassApplication::~GlassApplication()
{
    if (m_grefThis) {
        GetEnv()->DeleteGlobalRef(m_grefThis);
    }
    if (m_clipboard) {
        GetEnv()->DeleteGlobalRef(m_clipboard);
    }
}

LPCTSTR GlassApplication::GetWindowClassNameSuffix()
{
    return szGlassToolkitWindow;
}

jstring GlassApplication::GetThemeName(JNIEnv* env)
{
    HIGHCONTRAST contrastInfo;
    contrastInfo.cbSize = sizeof(HIGHCONTRAST);
    ::SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof(HIGHCONTRAST), &contrastInfo, 0);
    if (contrastInfo.dwFlags & HCF_HIGHCONTRASTON) {
        jsize length = (jsize) wcslen(contrastInfo.lpszDefaultScheme);
        jstring jstr = env->NewString((jchar*) contrastInfo.lpszDefaultScheme, length);
        if (CheckAndClearException(env)) return NULL;
        return jstr;
    }
    return NULL;
}

LRESULT GlassApplication::WindowProc(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_DO_ACTION:
        case WM_DO_ACTION_LATER:
            {
                Action * action = (Action *)wParam;
                action->Do();
                if (msg == WM_DO_ACTION_LATER) {
                    delete action;
                }
            }
            return 0;
        case WM_CREATE:
            fprintf(stderr, "GlassApplication::WindowProc(WM_CREATE)\n");
            pInstance = this;
            STRACE(_T("GlassApplication: created."));
            break;
        case WM_DESTROY:
            //Alarm clipboard dispose if any.
            //Please, use RegisterClipboardViewer(NULL) instead of UnregisterClipboardViewer.
            fprintf(stderr, "GlassApplication::WindowProc(WM_DESTROY)\n");
            RegisterClipboardViewer(NULL);
            return 0;
        case WM_NCDESTROY:
            // pInstance is deleted in BaseWnd::StaticWindowProc
            fprintf(stderr, "GlassApplication::WindowProc(WM_NCDESTROY)\n");
            pInstance = NULL;
            STRACE(_T("GlassApplication: destroyed."));
            return 0;
        case WM_CHANGECBCHAIN:
            fprintf(stderr, "GlassApplication::WindowProc(WM_CHANGECBCHAIN)\n");
            if ((HWND)wParam == m_hNextClipboardView) {
                m_hNextClipboardView = (HWND)lParam;
            } else if (NULL != m_hNextClipboardView) {
                ::SendMessage(m_hNextClipboardView, WM_CHANGECBCHAIN, wParam, lParam);
            }
            break;
        case WM_DRAWCLIPBOARD:
            fprintf(stderr, "GlassApplication::WindowProc(WM_DRAWCLIPBOARD)\n");
            if (NULL != m_clipboard) {
                GetEnv()->CallVoidMethod(m_clipboard, midContentChanged);
                CheckAndClearException(GetEnv());
            }
            if (NULL != m_hNextClipboardView) {
                ::SendMessage(m_hNextClipboardView, WM_DRAWCLIPBOARD, wParam, lParam);
            }
            break;
        case WM_SETTINGCHANGE:
            fprintf(stderr, "GlassApplication::WindowProc(WM_SETTINGCHANGE)\n");
            if ((UINT)wParam != SPI_SETWORKAREA) {
                break;
            }
            // Fall through
        case WM_DISPLAYCHANGE:
            fprintf(stderr, "GlassApplication::WindowProc(WM_DISPLAYCHANGE)\n");
            GlassScreen::HandleDisplayChange();
            break;
        case WM_THEMECHANGED: {
            fprintf(stderr, "GlassApplication::WindowProc(WM_THEMECHANGED)\n");
            JNIEnv* env = GetEnv();
            jstring themeName = GlassApplication::GetThemeName(env);
            jboolean result = env->CallBooleanMethod(m_grefThis, javaIDs.Application.notifyThemeChangedMID, themeName);
            fprintf(stderr, "GlassApplication::WindowProc(WM_THEMECHANGED): %u\n", result);
            if (CheckAndClearException(env)) return 1;
            return !result;
        }
    }
    return ::DefWindowProc(GetHWND(), msg, wParam, lParam);
}

LRESULT CALLBACK GlassApplication::MouseLLHook(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0) {
        switch (wParam) {
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
            case WM_NCLBUTTONDOWN:
            case WM_NCMBUTTONDOWN:
            case WM_NCRBUTTONDOWN:
            case WM_NCXBUTTONDOWN:
            case WM_MOUSEACTIVATE:
                {
                    POINT pt = ((MSLLHOOKSTRUCT*)lParam)->pt;
                    HWND hwnd = ::GetAncestor(::WindowFromPoint(pt), GA_ROOT);

                    BaseWnd *pWindow = BaseWnd::FromHandle(hwnd);
                    if (!pWindow) {
                        // A click on a non-Glass, supposedly browser window
                        GlassWindow::ResetGrab();
                    }
                }
                break;
        }
    }
    return ::CallNextHookEx(GlassApplication::sm_hMouseLLHook, nCode, wParam, lParam);
}

void GlassApplication::InstallMouseLLHook()
{
    fprintf(stderr, "GlassApplication::InstallMouseLLHook() (counter = %d)\n", GlassApplication::sm_mouseLLHookCounter);
    if (++GlassApplication::sm_mouseLLHookCounter == 1) {
        fprintf(stderr, "GlassApplication::InstallMouseLLHook() HInstance = %p\n", GlassApplication::GetHInstance());
        GlassApplication::sm_hMouseLLHook =
            ::SetWindowsHookEx(WH_MOUSE_LL,
                    (HOOKPROC)GlassApplication::MouseLLHook,
                    GlassApplication::GetHInstance(), 0);
    }
}

void GlassApplication::UninstallMouseLLHook()
{
    if (--GlassApplication::sm_mouseLLHookCounter == 0) {
        ::UnhookWindowsHookEx(GlassApplication::sm_hMouseLLHook);
    }
}

/* static */
void GlassApplication::RegisterClipboardViewer(jobject clipboard)
{
    JNIEnv *env = GetEnv();
    if (NULL != m_clipboard) {
        //Alarm dispose. We need to release all native resources
        //of previous instance.
        //It means that user skipped ClipboardAssistance close.
        JLObject _clipboard(env, env->NewLocalRef(m_clipboard));
        Java_com_sun_glass_ui_win_WinSystemClipboard_dispose(env, _clipboard);
    }
    if (NULL != clipboard) {
        m_clipboard = env->NewGlobalRef(clipboard);
        m_hNextClipboardView = ::SetClipboardViewer(GetHWND()) ;
        STRACE(_T("RegisterClipboardViewer"));
    }
}

/* static */
void GlassApplication::UnregisterClipboardViewer()
{
    if (NULL != m_hNextClipboardView) {
        ::ChangeClipboardChain(GetHWND(), m_hNextClipboardView);
        m_hNextClipboardView = NULL;
        STRACE(_T("UnregisterClipboardViewer"));
    }
    if (NULL != m_clipboard) {
        GetEnv()->DeleteGlobalRef(m_clipboard);
        m_clipboard = NULL;
    }
}

/* static */
void GlassApplication::ExecAction(Action *action)
{
    if (!pInstance) {
        return;
    }
    ::SendMessage(pInstance->GetHWND(), WM_DO_ACTION, (WPARAM)action, (LPARAM)0);
}

/* static */
void GlassApplication::ExecActionLater(Action *action)
{
    if (!pInstance) {
        delete action;
        return;
    }
    ::PostMessage(pInstance->GetHWND(), WM_DO_ACTION_LATER, (WPARAM)action, (LPARAM)0);
}

/* static */
jobject GlassApplication::EnterNestedEventLoop(JNIEnv * env)
{
    sm_shouldLeaveNestedLoop = false;

    MSG msg;
    while (GlassApplication::GetInstance()
            && !sm_shouldLeaveNestedLoop
            && ::GetMessage(&msg, NULL, 0, 0) > 0)
    {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }

    sm_shouldLeaveNestedLoop = false;

    if (!sm_nestedLoopReturnValue) {
        return NULL;
    }

    jobject ret = env->NewLocalRef(sm_nestedLoopReturnValue);
    sm_nestedLoopReturnValue.Attach(env, NULL);
    return ret;
}

/* static */
void GlassApplication::LeaveNestedEventLoop(JNIEnv * env, jobject retValue)
{
    sm_nestedLoopReturnValue.Attach(env, retValue);
    sm_shouldLeaveNestedLoop = true;
}

ULONG GlassApplication::s_accessibilityCount = 0;

/* static */
ULONG GlassApplication::IncrementAccessibility()
{
    return InterlockedIncrement(&GlassApplication::s_accessibilityCount);
}

/* static */
ULONG GlassApplication::DecrementAccessibility()
{
    return InterlockedDecrement(&GlassApplication::s_accessibilityCount);
}

/* static */
ULONG GlassApplication::GetAccessibilityCount()
{
    return GlassApplication::s_accessibilityCount;
}

/*******************************************************
 * JNI section
 *******************************************************/

extern "C" {

#ifndef STATIC_BUILD
BOOL WINAPI DllMain(HANDLE hinstDLL, DWORD dwReason, LPVOID lpvReserved)
{
    if (dwReason == DLL_PROCESS_ATTACH) {
        GlassApplication::SetHInstance((HINSTANCE)hinstDLL);
    }
    return TRUE;
}
#endif

typedef unsigned __int32 juint;
typedef unsigned __int64 julong;
#define CONST64(x)  (x ## LL)

static void set_low(jlong* value, jint low) {
    *value &= (jlong)0xffffffff << 32;
    *value |= (jlong)(julong)(juint)low;
}

static void set_high(jlong* value, jint high) {
    *value &= (jlong)(julong)(juint)0xffffffff;
    *value |= (jlong)high       << 32;
}

static jlong jlong_from(jint h, jint l) {
    jlong result = 0; // initialization to avoid warning
    set_high(&result, h);
    set_low(&result,  l);
    return result;
}

static jlong  _offset   = 116444736000000000;
jlong offset() {
  return _offset;
}

// Returns time ticks in (10th of micro seconds)
jlong windows_to_time_ticks(FILETIME wt) {
  jlong a = jlong_from(wt.dwHighDateTime, wt.dwLowDateTime);
  return (a - offset());
}

void javaTimeSystemUTC(jlong &seconds, jlong &nanos) {
  FILETIME wt;
  GetSystemTimeAsFileTime(&wt);
  jlong ticks = windows_to_time_ticks(wt); // 10th of micros
  jlong secs = jlong(ticks / 10000000); // 10000 * 1000
  seconds = secs;
  nanos = jlong(ticks - (secs*10000000)) * 100;
}

const jlong MAX_DIFF_SECS = CONST64(0x0100000000); //  2^32
const jlong MIN_DIFF_SECS = -MAX_DIFF_SECS; // -2^32
JNIEXPORT jlong JNICALL JVM_GetNanoTimeAdjustment(void *env, void * ignored, jlong offset_secs) {
    jlong seconds;
    jlong nanos;

    javaTimeSystemUTC(seconds, nanos);

    jlong diff = seconds - offset_secs;
    if (diff >= MAX_DIFF_SECS || diff <= MIN_DIFF_SECS) {
        return -1; // sentinel value: the offset is too far off the target
    }
    return (diff * (jlong)1000000000) + nanos;
}

JNIEXPORT jlong JNICALL Java_jdk_internal_misc_VM_getNanoTimeAdjustment(void *env, void * ignored, jlong offset_secs) {
    return JVM_GetNanoTimeAdjustment(env, ignored, offset_secs);
}

/*
 * Class:     com_sun_glass_ui_win_WinApplication
 * Method:    initIDs
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_com_sun_glass_ui_win_WinApplication_initIDs
  (JNIEnv *env, jclass cls, jfloat overrideUIScale)
{
#ifdef STATIC_BUILD
    HINSTANCE hInstExe = ::GetModuleHandle(NULL);
    fprintf(stderr, "GlassApplication::_initIDs hInstExe = %p\n", hInstExe);
    GlassApplication::SetHInstance(hInstExe);
#endif

    GlassApplication::overrideUIScale = overrideUIScale;

    javaIDs.Application.reportExceptionMID =
        env->GetStaticMethodID(cls, "reportException", "(Ljava/lang/Throwable;)V");
    ASSERT(javaIDs.Application.reportExceptionMID);
    if (CheckAndClearException(env)) return;

    javaIDs.Application.notifyThemeChangedMID =
        env->GetMethodID(cls, "notifyThemeChanged", "(Ljava/lang/String;)Z");
    ASSERT(javaIDs.Application.notifyThemeChangedMID);
    if (CheckAndClearException(env)) return;

    //NOTE: substitute the cls
    cls = (jclass)env->FindClass("java/lang/Runnable");
    if (CheckAndClearException(env)) return;

    javaIDs.Runnable.run = env->GetMethodID(cls, "run", "()V");
    ASSERT(javaIDs.Runnable.run);
    if (CheckAndClearException(env)) return;
}

/*
 * Class:     com_sun_glass_ui_win_WinApplication
 * Method:    _init
 * Signature: (I)J
 */
JNIEXPORT jlong JNICALL Java_com_sun_glass_ui_win_WinApplication__1init
  (JNIEnv *env, jobject _this, jint awareRequested)
{
    // TODO: if/when we introduce JavaFX launcher, DPI awareness should
    // be specified in its manifest instead of this call below
    // Specifying awareness in the manifest ensures that it happens before
    // any system calls that might depend on it.  The downside is losing
    // the ability to control the awareness level programmatically via
    // property settings.
    if (IS_WINVISTA) {
        GlassScreen::LoadDPIFuncs(awareRequested);
    }

    fprintf(stderr, "[JSDBG] GlassApplication.init() A\n");
    GlassApplication *pApp = new GlassApplication(_this);
    fprintf(stderr, "[JSDBG] GlassApplication.init() B\n");

    HWND hWnd = GlassApplication::GetToolkitHWND();
    fprintf(stderr, "[JSDBG] GlassApplication.init() C: toolkitHWND = %p\n", hWnd);
    if (hWnd == NULL) {
        delete pApp;
    }

    return (jlong)hWnd;
}

/*
 * Class:     com_sun_glass_ui_win_WinApplication
 * Method:    _setClassLoader
 * Signature: (Ljava/lang/ClassLoader;)V
 */
JNIEXPORT void JNICALL Java_com_sun_glass_ui_win_WinApplication__1setClassLoader
  (JNIEnv * env, jobject self, jobject jClassLoader)
{
    GlassApplication::SetGlassClassLoader(env, jClassLoader);
}

/*
 * Class:     com_sun_glass_ui_win_WinApplication
 * Method:    _runLoop
 * Signature: (Ljava/lang/Runnable;)V
 */
JNIEXPORT void JNICALL Java_com_sun_glass_ui_win_WinApplication__1runLoop
  (JNIEnv * env, jobject self, jobject jLaunchable)
{
    fprintf(stderr, "[JSDBG] GlassApplication.runLoop() A\n");
    OLEHolder _ole_;
    if (jLaunchable != NULL) {
        fprintf(stderr, "[JSDBG] GlassApplication.runLoop() B\n");
        env->CallVoidMethod(jLaunchable, javaIDs.Runnable.run);
        fprintf(stderr, "[JSDBG] GlassApplication.runLoop() C\n");
        CheckAndClearException(env);
    }
    fprintf(stderr, "[JSDBG] GlassApplication.runLoop() D\n");

    MSG msg;
    // The GlassApplication instance may be destroyed in a nested loop.
    // Note that we leave the WM_QUIT message on the queue but who cares?
    while (GlassApplication::GetInstance() && ::GetMessage(&msg, NULL, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }

    fprintf(stderr, "[JSDBG] GlassApplication.runLoop() E\n");
    if (GlassApplication::GetAccessibilityCount() > 0 && !IS_WIN8) {
        // Bug in Windows 7. For some reason, JavaFX crashes when the application
        // is shutting down while Narrator (the screen reader) is running. It is
        // suspected the crash happens because the event thread is finalized while
        // accessible objects are still receiving release messages. Not all the
        // circumstances around this crash are well understood,  but calling
        // GetMessage() one last time fixes the crash.
        fprintf(stderr, "[JSDBG] GlassApplication.runLoop() Ea\n");
        UINT_PTR timerId = ::SetTimer(NULL, NULL, 1000, NULL);
        fprintf(stderr, "[JSDBG] GlassApplication.runLoop() Eb\n");
        ::GetMessage(&msg, NULL, 0, 0);
        fprintf(stderr, "[JSDBG] GlassApplication.runLoop() Ec\n");
        ::KillTimer(NULL, timerId);
        fprintf(stderr, "[JSDBG] GlassApplication.runLoop() Ed\n");
    }
    fprintf(stderr, "[JSDBG] GlassApplication.runLoop() F\n");
}

/*
 * Class:     com_sun_glass_ui_win_WinApplication
 * Method:    _terminateLoop
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_com_sun_glass_ui_win_WinApplication__1terminateLoop
  (JNIEnv * env, jobject self)
{
    fprintf(stderr, "[JSDBG] GlassApplication.terminateLoop() A\n");
    HWND hWnd = GlassApplication::GetToolkitHWND();
    fprintf(stderr, "[JSDBG] GlassApplication.terminateLoop() B: hWnd = %p\n", hWnd);
    if (::IsWindow(hWnd)) {
        fprintf(stderr, "[JSDBG] GlassApplication.terminateLoop() C\n");
        ::DestroyWindow(hWnd);
        fprintf(stderr, "[JSDBG] GlassApplication.terminateLoop() D\n");
    }
    fprintf(stderr, "[JSDBG] GlassApplication.terminateLoop() E\n");
}

/*
 * Class:     com_sun_glass_ui_win_WinApplication
 * Method:    _enterNestedEventLoopImpl
 * Signature: ()Ljava/lang/Object;
 */
JNIEXPORT jobject JNICALL Java_com_sun_glass_ui_win_WinApplication__1enterNestedEventLoopImpl
  (JNIEnv * env, jobject self)
{
    return GlassApplication::EnterNestedEventLoop(env);
}

/*
 * Class:     com_sun_glass_ui_win_WinApplication
 * Method:    _getHighContrastTheme
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_sun_glass_ui_win_WinApplication__1getHighContrastTheme
  (JNIEnv * env, jobject self)
{
    return GlassApplication::GetThemeName(env);
}

/*
 * Class:     com_sun_glass_ui_win_WinApplication
 * Method:    _leaveNestedEventLoopImpl
 * Signature: (Ljava/lang/Object;)V
 */
JNIEXPORT void JNICALL Java_com_sun_glass_ui_win_WinApplication__1leaveNestedEventLoopImpl
  (JNIEnv * env, jobject self, jobject retValue)
{
    GlassApplication::LeaveNestedEventLoop(env, retValue);
}

/*
 * Class:     com_sun_glass_ui_Application
 * Method:    _invokeAndWait
 * Signature: (Ljava/lang/Runnable;)V
 */
JNIEXPORT void JNICALL Java_com_sun_glass_ui_win_WinApplication__1invokeAndWait
  (JNIEnv * env, jobject japplication, jobject runnable)
{
    ENTER_MAIN_THREAD()
    {
        GetEnv()->CallVoidMethod(runnable, javaIDs.Runnable.run);
        CheckAndClearException(GetEnv());
    }
    DECL_jobject(runnable);
    LEAVE_MAIN_THREAD;

    ARG(runnable) = runnable;
    PERFORM();
}

/*
 * Class:     com_sun_glass_ui_Application
 * Method:    _submitForLaterInvocation
 * Signature: (Ljava/lang/Runnable;)V
 */
JNIEXPORT void JNICALL Java_com_sun_glass_ui_win_WinApplication__1submitForLaterInvocation
  (JNIEnv * env, jobject japplication, jobject runnable)
{
    ENTER_MAIN_THREAD()
    {
        GetEnv()->CallVoidMethod(runnable, javaIDs.Runnable.run);
        CheckAndClearException(GetEnv());
    }
    DECL_jobject(runnable);
    LEAVE_MAIN_THREAD_LATER;

    ARG(runnable) = runnable;
    PERFORM_LATER();
}

/*
 * Class:     com_sun_glass_ui_Application
 * Method:    _supportsUnifiedWindows
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_com_sun_glass_ui_win_WinApplication__1supportsUnifiedWindows
    (JNIEnv * env, jobject japplication)
{
    return (IS_WINVISTA);
}

/*
 * Class:     com_sun_glass_ui_Application
 * Method:    staticScreen_getScreens
 * Signature: ()[Lcom/sun/glass/ui/Screen;
 */
JNIEXPORT jobjectArray JNICALL Java_com_sun_glass_ui_win_WinApplication_staticScreen_1getScreens
    (JNIEnv * env, jobject japplication)
{
    return GlassScreen::CreateJavaScreens(env);
}

} // extern "C"

