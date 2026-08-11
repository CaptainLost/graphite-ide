#include "corepch.h"

#include "Core/Platform/WindowFrame.h"

#include "Core/Base/Log.h"
#include "Core/Platform/Window.h"

#include <algorithm>
#include <cmath>

#ifdef CORE_PLATFORM_WINDOWS
    #include <windows.h>

    #include <dwmapi.h>
    #include <windowsx.h>

    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3.h>
    #include <GLFW/glfw3native.h>
#endif

namespace core
{
    namespace
    {
        /// One channel of the packed integer. The rounding this does, and why it is
        /// rounding and not truncation, is documented on PackBgr.
        [[nodiscard]] std::uint32_t Channel(float value)
        {
            const float scaled = std::clamp(value, 0.0f, 1.0f) * 255.0f;
            return static_cast<std::uint32_t>(std::lround(scaled));
        }
    }

    std::uint32_t PackBgr(const Color& color)
    {
        return Channel(color.r) | (Channel(color.g) << 8u) | (Channel(color.b) << 16u);
    }

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
            Win32Frame(HWND hwnd, COLORREF border) : m_hwnd(hwnd), m_border(border) {}

            ~Win32Frame() override
            {
                // A non-null m_previous is exactly the record that the subclass took, so it
                // is also the record of what there is to undo: Install leaves nothing behind
                // on any path that does not set it.
                if (m_previous == nullptr)
                {
                    return;
                }

                // The window is about to have the system's caption back, and that caption
                // must be framed the way the system frames every other window: this is the
                // one place DWMWA_COLOR_DEFAULT does something, because it undoes the
                // colour Install set. A failure is as ignorable here as it is there.
                const COLORREF none = DWMWA_COLOR_DEFAULT;
                DwmSetWindowAttribute(m_hwnd, DWMWA_BORDER_COLOR, &none, sizeof(none));

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

                RestoreBorder();

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

            /// Puts back an outline for the window, which reclaiming the caption left it
            /// without. DWM draws it into the frame — the band OnCalcSize leaves between the
            /// window rectangle and the client rectangle — so this colour only shows where
            /// that band exists: one pixel on each edge of a restored window, and nothing at
            /// all on a maximized one, which needs none because every edge it has is off the
            /// screen. Setting the colour alone is not enough and never was; the two have to
            /// be read together.
            ///
            /// The colour is the caller's — Core::Theme::BorderColor, the line every panel
            /// is already outlined with, carried down through WindowSpec — so the window's
            /// edge and the edges inside it come from one definition. It arrives as a value
            /// rather than being looked up because this runs before ImGui exists, and it
            /// applies to a window rather than to anything ImGui draws. A single explicit
            /// colour rather than DWMWA_COLOR_DEFAULT, which only asks for the state an
            /// untouched window is already in.
            ///
            /// DWMWA_BORDER_COLOR exists from build 22000. Every older system answers with
            /// a failure HRESULT, which is deliberately not fatal: the caption is already
            /// hidden by then and a window with an uncoloured frame is still a working
            /// window, so this is no reason to fail Hide and hand the system bar back. It
            /// is said out loud, though — the border is the one part of this frame with no
            /// automated coverage and no way to assert it from inside the process, so the
            /// log line is what turns "there is no outline" from a mystery into a fact.
            void RestoreBorder() const
            {
                const HRESULT set = DwmSetWindowAttribute(m_hwnd, DWMWA_BORDER_COLOR, &m_border,
                                                          sizeof(m_border));

                if (FAILED(set))
                {
                    CORE_WARN("The window border keeps the system colour: DWM refused "
                              "DWMWA_BORDER_COLOR (0x{:08X}), which needs Windows 11.",
                              static_cast<unsigned>(set));
                }
            }

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

                // The metrics have to be asked for per monitor: GLFW makes the process
                // per-monitor DPI aware, so the plain GetSystemMetrics would answer for the
                // primary monitor and be wrong on any other one.
                UINT dpi = GetDpiForWindow(m_hwnd);
                if (dpi == 0)
                {
                    dpi = USER_DEFAULT_SCREEN_DPI;
                }

                if (Maximized())
                {
                    // A maximized window is placed one frame thickness outside the work area
                    // on every side, because the system expects the frame to cover the
                    // overhang. With no frame left to cover it the top of the caption would
                    // fall off the screen, so the client rectangle hands the overhang back.
                    // That inset is also why a maximized window needs no border of its own:
                    // every edge it has is off the screen already.

                    // There is no SM_CYPADDEDBORDER — the one padded border spans both axes.
                    const int padding = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                    const int frameX = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + padding;
                    const int frameY = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + padding;

                    target.left += frameX;
                    target.right -= frameX;
                    target.top += frameY;
                    target.bottom -= frameY;
                }
                else
                {
                    // DWM draws the window's border into the frame — the band between the
                    // window rectangle and the client rectangle — and colours it with
                    // DWMWA_BORDER_COLOR. Handing back the proposed rectangle untouched
                    // leaves that band zero pixels wide on every edge, and a border zero
                    // pixels wide is the same thing as no border: the call still succeeds,
                    // and nothing appears. So the client area stops one border short of the
                    // window on all four edges, which is the least that can be given up and
                    // still be seen. It costs one physical pixel of client area per edge.
                    //
                    // SM_CXBORDER/SM_CYBORDER is that width by definition, and it is asked
                    // per monitor like the frame metrics above rather than hard-coded to the
                    // one pixel it answers at 96 DPI on build 26200 — a hard-coded 1 would
                    // be right today and silently wrong on a Windows that scaled its border.
                    const int borderX = GetSystemMetricsForDpi(SM_CXBORDER, dpi);
                    const int borderY = GetSystemMetricsForDpi(SM_CYBORDER, dpi);

                    target.left += borderX;
                    target.right -= borderX;
                    target.top += borderY;
                    target.bottom -= borderY;
                }

                // The caption is gone either way: nothing above is anywhere near the caption's
                // height, so no room is left for the system to paint one into. Returning 0
                // says "the client area is rgrc[0], keep none of the old one", which costs a
                // repaint of the client area on resize and nothing else.
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
            COLORREF m_border = 0;
            WNDPROC m_previous = nullptr;
            CaptionMetrics m_metrics;
        };
    }
#endif

    Result<std::unique_ptr<WindowFrame>> WindowFrame::Hide([[maybe_unused]] Window& window,
                                                           [[maybe_unused]] const Color& border)
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

        auto frame = std::make_unique<Win32Frame>(hwnd, static_cast<COLORREF>(PackBgr(border)));

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
