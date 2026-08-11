#include "corepch.h"

#include "Core/Platform/WindowFrame.h"

#include "Core/Platform/Window.h"

#ifdef CORE_PLATFORM_WINDOWS
    #include <windows.h>

    #include <windowsx.h>

    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3.h>
    #include <GLFW/glfw3native.h>
#endif

namespace core
{
#ifdef CORE_PLATFORM_WINDOWS
    namespace
    {
        /// GLFW keeps its own _GLFWwindow* on this very HWND, in the window property named
        /// L"GLFW" (vendor/glfw/src/win32_window.c), and reads it back on every message it
        /// handles. GWLP_USERDATA is in fact free on a GLFW window — GLFW never writes it —
        /// but it is a single unnamed slot that any other subclasser could claim without
        /// either side noticing, and losing it would take GLFW's whole event loop with it.
        /// A named property cannot collide, so this frame follows GLFW's own convention
        /// rather than taking the one slot everybody reaches for first.
        constexpr const wchar_t* kFrameProperty = L"GraphiteWindowFrame";

        /// The window keeps its WS_OVERLAPPEDWINDOW style — that is what buys the drop
        /// shadow, the snap animations and Aero Snap. Only the *drawing* of the caption is
        /// taken away, by handing WM_NCCALCSIZE a client rectangle that covers it.
        class Win32Frame final : public WindowFrame
        {
        public:
            /// Constructing costs nothing and changes nothing about the window — see Install,
            /// which is where a frame either takes or reports why it did not.
            explicit Win32Frame(HWND hwnd) : m_hwnd(hwnd) {}

            ~Win32Frame() override
            {
                // A non-null m_previous is exactly the record that the subclass took, so it
                // is also the record of what there is to undo: Install leaves nothing behind
                // on any path that does not set it.
                if (m_previous == nullptr)
                {
                    return;
                }

                SetWindowLongPtrW(m_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_previous));

                // The caption comes back the way it went away: only on a frame change.
                SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                             SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                                 SWP_NOACTIVATE);

                RemovePropW(m_hwnd, kFrameProperty);
            }

            /// Takes the caption, or reports why it could not and leaves the window exactly
            /// as it was found. Half of an install is worse than none of one: the caller
            /// would go on to draw a title bar over a window that still has the system's.
            [[nodiscard]] Result<void> Install()
            {
                // The property goes on before the subclass, so that the procedure below can
                // never find itself running without its frame. That ordering is only sound
                // because the failure is caught here: a property that did not take would
                // otherwise leave a subclassed window whose every message reached no one.
                if (SetPropW(m_hwnd, kFrameProperty, this) == 0)
                {
                    return Error{ErrorCode::Unsupported,
                                 "the window would not hold the frame's property"};
                }

                // A live window's procedure is never null, so a zero here is a failure and
                // not a previous value worth keeping.
                m_previous = reinterpret_cast<WNDPROC>(
                    SetWindowLongPtrW(m_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&Proc)));

                if (m_previous == nullptr)
                {
                    RemovePropW(m_hwnd, kFrameProperty);
                    return Error{ErrorCode::Unsupported,
                                 "the window procedure could not be subclassed"};
                }

                // The frame is only recomputed on a frame change, so ask for one: without
                // it the caption stays on screen until the user first resizes the window.
                SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                             SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                                 SWP_NOACTIVATE);

                return Result<void>::Ok();
            }

            void SetCaptionMetrics(const CaptionMetrics& metrics) override
            {
                m_metrics = metrics;
            }

        private:
            /// IsZoomed rather than GetWindowPlacement: a window minimized *from* maximized
            /// reports SW_SHOWMINIMIZED there, and the frame would then measure it as if it
            /// were restored. The style bit IsZoomed reads is also already set by the time
            /// WM_NCCALCSIZE arrives for the maximize itself, which is when it matters most.
            [[nodiscard]] bool Maximized() const { return IsZoomed(m_hwnd) != FALSE; }

            LRESULT OnCalcSize(WPARAM wParam, LPARAM lParam)
            {
                // A zero wParam means lParam is a bare RECT: the system is only asking what
                // client area a proposed window rectangle would give, with no valid-area
                // bookkeeping attached. Nothing to correct there.
                if (wParam == 0u)
                {
                    return CallWindowProcW(m_previous, m_hwnd, WM_NCCALCSIZE, wParam, lParam);
                }

                auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                RECT& target = params->rgrc[0];

                if (Maximized())
                {
                    // A maximized window is placed one frame thickness outside the work area
                    // on every side, because the system expects the frame to cover the
                    // overhang. With no frame left to cover it the top of the caption would
                    // fall off the screen, so the client rectangle hands the overhang back.
                    // The metrics have to be asked for per monitor: GLFW makes the process
                    // per-monitor DPI aware, so the plain GetSystemMetrics would answer for
                    // the primary monitor and be wrong on any other one.
                    UINT dpi = GetDpiForWindow(m_hwnd);
                    if (dpi == 0)
                    {
                        dpi = USER_DEFAULT_SCREEN_DPI;
                    }

                    // There is no SM_CYPADDEDBORDER — the one padded border spans both axes.
                    const int padding = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                    const int frameX = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + padding;
                    const int frameY = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + padding;

                    target.left += frameX;
                    target.right -= frameX;
                    target.top += frameY;
                    target.bottom -= frameY;
                }

                // Returning the rectangle otherwise unchanged is the whole trick: the client
                // area now reaches the top edge, so nothing is left for the system to paint a
                // caption into. Returning 0 says "the client area is rgrc[0], keep none of the
                // old one", which costs a repaint of the client area on resize and nothing else.
                return 0;
            }

            [[nodiscard]] LRESULT OnHitTest(LPARAM lParam) const
            {
                // WM_NCHITTEST asks about a point in screen coordinates — it has to, since the
                // point it asks about may lie outside the client area.
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(m_hwnd, &point);

                RECT client{};
                GetClientRect(m_hwnd, &client);

                const CaptionHit hit =
                    Classify(m_metrics, static_cast<float>(client.right - client.left),
                             static_cast<float>(client.bottom - client.top),
                             static_cast<float>(point.x), static_cast<float>(point.y),
                             Maximized());

                switch (hit)
                {
                case CaptionHit::Caption:
                    return HTCAPTION;
                case CaptionHit::Left:
                    return HTLEFT;
                case CaptionHit::Right:
                    return HTRIGHT;
                case CaptionHit::Top:
                    return HTTOP;
                case CaptionHit::Bottom:
                    return HTBOTTOM;
                case CaptionHit::TopLeft:
                    return HTTOPLEFT;
                case CaptionHit::TopRight:
                    return HTTOPRIGHT;
                case CaptionHit::BottomLeft:
                    return HTBOTTOMLEFT;
                case CaptionHit::BottomRight:
                    return HTBOTTOMRIGHT;
                case CaptionHit::Client:
                    break;
                }

                return HTCLIENT;
            }

            static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
            {
                auto* self = static_cast<Win32Frame*>(GetPropW(hwnd, kFrameProperty));

                // Unreachable: Install refuses to subclass unless the property took, and the
                // destructor restores the original procedure before removing it again, so
                // this procedure only ever runs between those two points. The branch matters
                // anyway because of what it would cost — DefWindowProcW skips GLFW's
                // procedure entirely, and a window whose events never reach GLFW is a deaf
                // window. That is the failure Install exists to make impossible.
                if (self == nullptr)
                {
                    return DefWindowProcW(hwnd, message, wParam, lParam);
                }

                switch (message)
                {
                case WM_NCCALCSIZE:
                    return self->OnCalcSize(wParam, lParam);
                case WM_NCHITTEST:
                    return self->OnHitTest(lParam);
                default:
                    break;
                }

                return CallWindowProcW(self->m_previous, hwnd, message, wParam, lParam);
            }

            HWND m_hwnd = nullptr;
            WNDPROC m_previous = nullptr;
            CaptionMetrics m_metrics;
        };
    }
#endif

    Result<std::unique_ptr<WindowFrame>> WindowFrame::Hide([[maybe_unused]] Window& window)
    {
#ifdef CORE_PLATFORM_WINDOWS
        auto* glfwHandle = static_cast<GLFWwindow*>(window.NativeHandle());
        if (glfwHandle == nullptr)
        {
            return Error{ErrorCode::Unsupported, "the window has no native handle"};
        }

        HWND hwnd = glfwGetWin32Window(glfwHandle);
        if (hwnd == nullptr)
        {
            return Error{ErrorCode::Unsupported, "the window is not a Win32 window"};
        }

        auto frame = std::make_unique<Win32Frame>(hwnd);

        const Result<void> installed = frame->Install();
        if (!installed)
        {
            return installed.GetError();
        }

        return Result<std::unique_ptr<WindowFrame>>{std::move(frame)};
#else
        return Error{ErrorCode::Unsupported,
                     "removing the system title bar is only implemented on Windows"};
#endif
    }
}
