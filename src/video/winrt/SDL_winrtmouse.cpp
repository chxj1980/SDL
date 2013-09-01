/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2012 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include "SDL_config.h"

#if SDL_VIDEO_DRIVER_WINRT

/*
 * Windows includes:
 */
#include <Windows.h>
using namespace Windows::UI::Core;
using Windows::UI::Core::CoreCursor;

/*
 * SDL includes:
 */
extern "C" {
#include "SDL_assert.h"
#include "../../events/SDL_mouse_c.h"
#include "../../events/SDL_touch_c.h"
#include "../SDL_sysvideo.h"
#include "SDL_events.h"
#include "SDL_log.h"
}

#include "../../core/winrt/SDL_winrtapp.h"
#include "SDL_winrtvideo_cpp.h"
#include "SDL_winrtmouse.h"


static SDL_bool WINRT_UseRelativeMouseMode = SDL_FALSE;
static SDL_TouchID WINRT_TouchID = 1;
static unsigned int WINRT_LeftFingerDown = 0;


static SDL_Cursor *
WINRT_CreateSystemCursor(SDL_SystemCursor id)
{
    SDL_Cursor *cursor;
    CoreCursorType cursorType = CoreCursorType::Arrow;

    switch(id)
    {
    default:
        SDL_assert(0);
        return NULL;
    case SDL_SYSTEM_CURSOR_ARROW:     cursorType = CoreCursorType::Arrow; break;
    case SDL_SYSTEM_CURSOR_IBEAM:     cursorType = CoreCursorType::IBeam; break;
    case SDL_SYSTEM_CURSOR_WAIT:      cursorType = CoreCursorType::Wait; break;
    case SDL_SYSTEM_CURSOR_CROSSHAIR: cursorType = CoreCursorType::Cross; break;
    case SDL_SYSTEM_CURSOR_WAITARROW: cursorType = CoreCursorType::Wait; break;
    case SDL_SYSTEM_CURSOR_SIZENWSE:  cursorType = CoreCursorType::SizeNorthwestSoutheast; break;
    case SDL_SYSTEM_CURSOR_SIZENESW:  cursorType = CoreCursorType::SizeNortheastSouthwest; break;
    case SDL_SYSTEM_CURSOR_SIZEWE:    cursorType = CoreCursorType::SizeWestEast; break;
    case SDL_SYSTEM_CURSOR_SIZENS:    cursorType = CoreCursorType::SizeNorthSouth; break;
    case SDL_SYSTEM_CURSOR_SIZEALL:   cursorType = CoreCursorType::SizeAll; break;
    case SDL_SYSTEM_CURSOR_NO:        cursorType = CoreCursorType::UniversalNo; break;
    case SDL_SYSTEM_CURSOR_HAND:      cursorType = CoreCursorType::Hand; break;
    }

    cursor = (SDL_Cursor *) SDL_calloc(1, sizeof(*cursor));
    if (cursor) {
        /* Create a pointer to a COM reference to a cursor.  The extra
           pointer is used (on top of the COM reference) to allow the cursor
           to be referenced by the SDL_cursor's driverdata field, which is
           a void pointer.
        */
        CoreCursor ^* theCursor = new CoreCursor^(nullptr);
        *theCursor = ref new CoreCursor(cursorType, 0);
        cursor->driverdata = (void *) theCursor;
    } else {
        SDL_OutOfMemory();
    }

    return cursor;
}

static SDL_Cursor *
WINRT_CreateDefaultCursor()
{
    return WINRT_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
}

static void
WINRT_FreeCursor(SDL_Cursor * cursor)
{
    if (cursor->driverdata) {
        CoreCursor ^* theCursor = (CoreCursor ^*) cursor->driverdata;
        *theCursor = nullptr;       // Release the COM reference to the CoreCursor
        delete theCursor;           // Delete the pointer to the COM reference
    }
    SDL_free(cursor);
}

static int
WINRT_ShowCursor(SDL_Cursor * cursor)
{
    // TODO, WinRT, XAML: make WINRT_ShowCursor work when XAML support is enabled.
    if ( ! CoreWindow::GetForCurrentThread()) {
        return 0;
    }

    if (cursor) {
        CoreCursor ^* theCursor = (CoreCursor ^*) cursor->driverdata;
        CoreWindow::GetForCurrentThread()->PointerCursor = *theCursor;
    } else {
        CoreWindow::GetForCurrentThread()->PointerCursor = nullptr;
    }
    return 0;
}

static int
WINRT_SetRelativeMouseMode(SDL_bool enabled)
{
    WINRT_UseRelativeMouseMode = enabled;
    return 0;
}

void
WINRT_InitMouse(_THIS)
{
    SDL_Mouse *mouse = SDL_GetMouse();

    /* DLudwig, Dec 3, 2012: WinRT does not currently provide APIs for
       the following features, AFAIK:
        - custom cursors  (multiple system cursors are, however, available)
        - programmatically moveable cursors
    */

#if WINAPI_FAMILY != WINAPI_FAMILY_PHONE_APP
    //mouse->CreateCursor = WINRT_CreateCursor;
    mouse->CreateSystemCursor = WINRT_CreateSystemCursor;
    mouse->ShowCursor = WINRT_ShowCursor;
    mouse->FreeCursor = WINRT_FreeCursor;
    //mouse->WarpMouse = WINRT_WarpMouse;
    mouse->SetRelativeMouseMode = WINRT_SetRelativeMouseMode;

    SDL_SetDefaultCursor(WINRT_CreateDefaultCursor());
#endif

    /* Init touch: */
    SDL_AddTouch(WINRT_TouchID, "");
}

void
WINRT_QuitMouse(_THIS)
{
}

// Applies necessary geometric transformations to raw cursor positions:
Windows::Foundation::Point
WINRT_TransformCursorPosition(SDL_Window * window, Windows::Foundation::Point rawPosition)
{
    using namespace Windows::Graphics::Display;

    if (!window) {
        return rawPosition;
    }

    SDL_WindowData * windowData = (SDL_WindowData *) window->driverdata;
    if (windowData->coreWindow == nullptr) {
        // For some reason, the window isn't associated with a CoreWindow.
        // This might end up being the case as XAML support is extended.
        // For now, if there's no CoreWindow attached to the SDL_Window,
        // don't do any transforms.
        return rawPosition;
    }

    // The CoreWindow can only be accessed on certain thread(s).
    SDL_assert(CoreWindow::GetForCurrentThread() != nullptr);

    CoreWindow ^ nativeWindow = windowData->coreWindow.Get();
    Windows::Foundation::Point outputPosition;

#if WINAPI_FAMILY != WINAPI_FAMILY_PHONE_APP
    outputPosition.X = rawPosition.X * (((float32)window->w) / nativeWindow->Bounds.Width);
    outputPosition.Y = rawPosition.Y * (((float32)window->h) / nativeWindow->Bounds.Height);
#else
    switch (DisplayProperties::CurrentOrientation)
    {
        case DisplayOrientations::Portrait:
            outputPosition.X = rawPosition.X * (((float32)window->w) / nativeWindow->Bounds.Width);
            outputPosition.Y = rawPosition.Y * (((float32)window->h) / nativeWindow->Bounds.Height);
            break;
        case DisplayOrientations::PortraitFlipped:
            outputPosition.X = (float32)window->w - rawPosition.X * (((float32)window->w) / nativeWindow->Bounds.Width);
            outputPosition.Y = (float32)window->h - rawPosition.Y * (((float32)window->h) / nativeWindow->Bounds.Height);
            break;
        case DisplayOrientations::Landscape:
            outputPosition.X = rawPosition.Y * (((float32)window->w) / nativeWindow->Bounds.Height);
            outputPosition.Y = (float32)window->h - rawPosition.X * (((float32)window->h) / nativeWindow->Bounds.Width);
            break;
        case DisplayOrientations::LandscapeFlipped:
            outputPosition.X = (float32)window->w - rawPosition.Y * (((float32)window->w) / nativeWindow->Bounds.Height);
            outputPosition.Y = rawPosition.X * (((float32)window->h) / nativeWindow->Bounds.Width);
            break;
        default:
            break;
    }
#endif

    return outputPosition;
}

static inline int
_lround(float arg)
{
    if (arg >= 0.0f) {
        return (int)floor(arg + 0.5f);
    } else {
        return (int)ceil(arg - 0.5f);
    }
}

void
WINRT_ProcessMouseMovedEvent(SDL_Window * window, Windows::Devices::Input::MouseEventArgs ^args)
{
    if (!window || !WINRT_UseRelativeMouseMode) {
        return;
    }

    // DLudwig, 2012-12-28: On some systems, namely Visual Studio's Windows
    // Simulator, as well as Windows 8 in a Parallels 8 VM, MouseEventArgs'
    // MouseDelta field often reports very large values.  More information
    // on this can be found at the following pages on MSDN:
    //  - http://social.msdn.microsoft.com/Forums/en-US/winappswithnativecode/thread/a3c789fa-f1c5-49c4-9c0a-7db88d0f90f8
    //  - https://connect.microsoft.com/VisualStudio/Feedback/details/756515
    //
    // The values do not appear to be as large when running on some systems,
    // most notably a Surface RT.  Furthermore, the values returned by
    // CoreWindow's PointerMoved event, and sent to this class' OnPointerMoved
    // method, do not ever appear to be large, even when MouseEventArgs'
    // MouseDelta is reporting to the contrary.
    //
    // On systems with the large-values behavior, it appears that the values
    // get reported as if the screen's size is 65536 units in both the X and Y
    // dimensions.  This can be viewed by using Windows' now-private, "Raw Input"
    // APIs.  (GetRawInputData, RegisterRawInputDevices, WM_INPUT, etc.)
    //
    // MSDN's documentation on MouseEventArgs' MouseDelta field (at
    // http://msdn.microsoft.com/en-us/library/windows/apps/windows.devices.input.mouseeventargs.mousedelta ),
    // does not seem to indicate (to me) that its values should be so large.  It
    // says that its values should be a "change in screen location".  I could
    // be misinterpreting this, however a post on MSDN from a Microsoft engineer (see: 
    // http://social.msdn.microsoft.com/Forums/en-US/winappswithnativecode/thread/09a9868e-95bb-4858-ba1a-cb4d2c298d62 ),
    // indicates that these values are in DIPs, which is the same unit used
    // by CoreWindow's PointerMoved events (via the Position field in its CurrentPoint
    // property.  See http://msdn.microsoft.com/en-us/library/windows/apps/windows.ui.input.pointerpoint.position.aspx
    // for details.)
    //
    // To note, PointerMoved events are sent a 'RawPosition' value (via the
    // CurrentPoint property in MouseEventArgs), however these do not seem
    // to exhibit the same large-value behavior.
    //
    // The values passed via PointerMoved events can't always be used for relative
    // mouse motion, unfortunately.  Its values are bound to the cursor's position,
    // which stops when it hits one of the screen's edges.  This can be a problem in
    // first person shooters, whereby it is normal for mouse motion to travel far
    // along any one axis for a period of time.  MouseMoved events do not have the
    // screen-bounding limitation, and can be used regardless of where the system's
    // cursor is.
    //
    // One possible workaround would be to programmatically set the cursor's
    // position to the screen's center (when SDL's relative mouse mode is enabled),
    // however WinRT does not yet seem to have the ability to set the cursor's
    // position via a public API.  Win32 did this via an API call, SetCursorPos,
    // however WinRT makes this function be private.  Apps that use it won't get
    // approved for distribution in the Windows Store.  I've yet to be able to find
    // a suitable, store-friendly counterpart for WinRT.
    //
    // There may be some room for a workaround whereby OnPointerMoved's values
    // are compared to the values from OnMouseMoved in order to detect
    // when this bug is active.  A suitable transformation could then be made to
    // OnMouseMoved's values.  For now, however, the system-reported values are sent
    // to SDL with minimal transformation: from native screen coordinates (in DIPs)
    // to SDL window coordinates.
    //
    const Windows::Foundation::Point mouseDeltaInDIPs((float)args->MouseDelta.X, (float)args->MouseDelta.Y);
    const Windows::Foundation::Point mouseDeltaInSDLWindowCoords = WINRT_TransformCursorPosition(window, mouseDeltaInDIPs);
    SDL_SendMouseMotion(
        window,
        0,
        1,
        _lround(mouseDeltaInSDLWindowCoords.X),
        _lround(mouseDeltaInSDLWindowCoords.Y));
}

Uint8
WINRT_GetSDLButtonForPointerPoint(Windows::UI::Input::PointerPoint ^pt)
{
    using namespace Windows::UI::Input;

#if WINAPI_FAMILY == WINAPI_FAMILY_PHONE_APP
    return SDL_BUTTON_LEFT;
#else
    switch (pt->Properties->PointerUpdateKind)
    {
        case PointerUpdateKind::LeftButtonPressed:
        case PointerUpdateKind::LeftButtonReleased:
            return SDL_BUTTON_LEFT;

        case PointerUpdateKind::RightButtonPressed:
        case PointerUpdateKind::RightButtonReleased:
            return SDL_BUTTON_RIGHT;

        case PointerUpdateKind::MiddleButtonPressed:
        case PointerUpdateKind::MiddleButtonReleased:
            return SDL_BUTTON_MIDDLE;

        case PointerUpdateKind::XButton1Pressed:
        case PointerUpdateKind::XButton1Released:
            return SDL_BUTTON_X1;

        case PointerUpdateKind::XButton2Pressed:
        case PointerUpdateKind::XButton2Released:
            return SDL_BUTTON_X2;

        default:
            break;
    }
#endif

    return 0;
}

//const char *
//WINRT_ConvertPointerUpdateKindToString(Windows::UI::Input::PointerUpdateKind kind)
//{
//    using namespace Windows::UI::Input;
//
//    switch (kind)
//    {
//        case PointerUpdateKind::Other:
//            return "Other";
//        case PointerUpdateKind::LeftButtonPressed:
//            return "LeftButtonPressed";
//        case PointerUpdateKind::LeftButtonReleased:
//            return "LeftButtonReleased";
//        case PointerUpdateKind::RightButtonPressed:
//            return "RightButtonPressed";
//        case PointerUpdateKind::RightButtonReleased:
//            return "RightButtonReleased";
//        case PointerUpdateKind::MiddleButtonPressed:
//            return "MiddleButtonPressed";
//        case PointerUpdateKind::MiddleButtonReleased:
//            return "MiddleButtonReleased";
//        case PointerUpdateKind::XButton1Pressed:
//            return "XButton1Pressed";
//        case PointerUpdateKind::XButton1Released:
//            return "XButton1Released";
//        case PointerUpdateKind::XButton2Pressed:
//            return "XButton2Pressed";
//        case PointerUpdateKind::XButton2Released:
//            return "XButton2Released";
//    }
//
//    return "";
//}

static bool
WINRT_IsTouchEvent(Windows::UI::Input::PointerPoint ^pointerPoint)
{
#if WINAPI_FAMILY == WINAPI_FAMILY_PHONE_APP
    return true;
#else
    using namespace Windows::Devices::Input;
    switch (pointerPoint->PointerDevice->PointerDeviceType) {
        case PointerDeviceType::Touch:
        case PointerDeviceType::Pen:
            return true;
        default:
            return false;
    }
#endif
}

void
WINRT_ProcessPointerMovedEvent(SDL_Window *window, Windows::UI::Input::PointerPoint ^pointerPoint)
{
    if (!window || WINRT_UseRelativeMouseMode) {
        return;
    }

    Windows::Foundation::Point transformedPoint = WINRT_TransformCursorPosition(window, pointerPoint->Position);

    if (pointerPoint->PointerId == WINRT_LeftFingerDown) {
        SDL_SendMouseMotion(window, 0, 0, (int)transformedPoint.X, (int)transformedPoint.Y);
    }

    if (WINRT_IsTouchEvent(pointerPoint)) {
        SDL_SendTouchMotion(
            WINRT_TouchID,
            (SDL_FingerID) pointerPoint->PointerId,
            transformedPoint.X,
            transformedPoint.Y,
            pointerPoint->Properties->Pressure);
    }
}

void
WINRT_ProcessPointerWheelChangedEvent(SDL_Window *window, Windows::UI::Input::PointerPoint ^pointerPoint)
{
    if (!window) {
        return;
    }

    // FIXME: This may need to accumulate deltas up to WHEEL_DELTA
    short motion = pointerPoint->Properties->MouseWheelDelta / WHEEL_DELTA;
    SDL_SendMouseWheel(window, 0, 0, motion);
}

void WINRT_ProcessPointerReleasedEvent(SDL_Window *window, Windows::UI::Input::PointerPoint ^pointerPoint)
{
    if (!window) {
        return;
    }

    Windows::Foundation::Point transformedPoint = WINRT_TransformCursorPosition(window, pointerPoint->Position);

    if (WINRT_LeftFingerDown == pointerPoint->PointerId) {
        Uint8 button = WINRT_GetSDLButtonForPointerPoint(pointerPoint);
        if (button) {
            SDL_SendMouseButton(window, 0, SDL_RELEASED, button);
        }
        WINRT_LeftFingerDown = 0;
    }

    if (WINRT_IsTouchEvent(pointerPoint)) {
        SDL_SendTouch(
            WINRT_TouchID,
            (SDL_FingerID) pointerPoint->PointerId,
            SDL_FALSE,
            transformedPoint.X,
            transformedPoint.Y,
            pointerPoint->Properties->Pressure);
    }
}

void WINRT_ProcessPointerPressedEvent(SDL_Window *window, Windows::UI::Input::PointerPoint ^pointerPoint)
{
    if (!window) {
        return;
    }

    Windows::Foundation::Point transformedPoint = WINRT_TransformCursorPosition(window, pointerPoint->Position);

    if (!WINRT_LeftFingerDown) {
        Uint8 button = WINRT_GetSDLButtonForPointerPoint(pointerPoint);
        if (button) {
#if WINAPI_FAMILY == WINAPI_FAMILY_PHONE_APP
            SDL_SendMouseMotion(window, 0, 0, (int)transformedPoint.X, (int)transformedPoint.Y);
#endif
            SDL_SendMouseButton(window, 0, SDL_PRESSED, button);
        }

        WINRT_LeftFingerDown = pointerPoint->PointerId;
    }

    if (WINRT_IsTouchEvent(pointerPoint)) {
        SDL_SendTouch(
            WINRT_TouchID,
            (SDL_FingerID) pointerPoint->PointerId,
            SDL_TRUE,
            transformedPoint.X,
            transformedPoint.Y,
            pointerPoint->Properties->Pressure);
    }
}

#endif /* SDL_VIDEO_DRIVER_WINRT */

/* vi: set ts=4 sw=4 expandtab: */
