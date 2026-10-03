#include "corepch.h"

#include "Core/Platform/WindowFrame.h"

#include "Core/Base/Base.h"

#include "Core/Base/Log.h"

#include "Core/Platform/Window.h"

#ifdef CORE_PLATFORM_WINDOWS

#include <windows.h>

#include <dwmapi.h>

#include <windowsx.h>

#define GLFW_EXPOSE_NATIVE_WIN32

#include <GLFW/glfw3.h>

#include <GLFW/glfw3native.h>
#endif

namespace Core {

#ifdef CORE_PLATFORM_WINDOWS
	namespace {
		constexpr const wchar_t* kFrameProperty = L"GraphiteWindowFrame";

		// The frame is only recomputed on a frame change, so a caption that came or went
		// stays on screen until the user next resizes the window.
		void RequestFrameChange(HWND hwnd)
		{
			SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
						 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
							 SWP_NOACTIVATE);
		}

		// Asked per monitor: GLFW makes the process per-monitor DPI aware, so the plain
		// GetSystemMetrics would answer for the primary monitor and be wrong on any other.
		UINT DpiOf(HWND hwnd)
		{
			const UINT dpi = GetDpiForWindow(hwnd);

			return dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi;
		}

		void Inset(RECT& rectangle, int x, int y)
		{
			rectangle.left += x;
			rectangle.right -= x;
			rectangle.top += y;
			rectangle.bottom -= y;
		}

		// A maximized window is placed one frame thickness outside the work area on every
		// side, because the system expects the frame to cover the overhang. With no frame
		// left to cover it the top of the caption would fall off the screen.
		void InsetByResizeFrame(RECT& rectangle, UINT dpi)
		{
			// There is no SM_CYPADDEDBORDER — the one padded border spans both axes.
			const int padding = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);

			Inset(rectangle, GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + padding,
				  GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + padding);
		}

		// DWM draws the window's border into the band between the window rectangle and the
		// client rectangle, so handing back the proposed rectangle untouched leaves that band
		// zero pixels wide and nothing appears. Giving up one SM_CXBORDER per edge is the
		// least that can still be seen, and it is asked per monitor rather than hard-coded to
		// the one pixel it answers at 96 DPI.
		void InsetByBorder(RECT& rectangle, UINT dpi)
		{
			Inset(rectangle, GetSystemMetricsForDpi(SM_CXBORDER, dpi),
				  GetSystemMetricsForDpi(SM_CYBORDER, dpi));
		}

		class Win32Frame final : public WindowFrame
		{
		public:
			Win32Frame(HWND hwnd, COLORREF border) : m_Hwnd(hwnd), m_Border(border) {}

			~Win32Frame() override
			{
				if (!IsInstalled())
				{
					return;
				}

				RestoreSystemBorderColour();

				SetWindowLongPtrW(m_Hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_Previous));

				RequestFrameChange(m_Hwnd);
				RemovePropW(m_Hwnd, kFrameProperty);
			}

			Result<void> Install()
			{
				// The property goes on before the subclass so that the procedure below can
				// never find itself running without its frame.
				if (SetPropW(m_Hwnd, kFrameProperty, this) == 0)
				{
					return Error{ErrorCode::Unsupported,
								 "the window would not hold the frame's property"};
				}

				// A live window's procedure is never null, so a zero here is a failure and
				// not a previous value worth keeping.
				m_Previous = reinterpret_cast<WNDPROC>(
					SetWindowLongPtrW(m_Hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&Proc)));

				if (m_Previous == nullptr)
				{
					RemovePropW(m_Hwnd, kFrameProperty);
					return Error{ErrorCode::Unsupported,
								 "the window procedure could not be subclassed"};
				}

				ApplyBorderColour();
				RequestFrameChange(m_Hwnd);

				return Result<void>::Ok();
			}

			void SetCaptionMetrics(const CaptionMetrics& metrics) override
			{
				m_Metrics = metrics;
			}

			void SetBorderColor(const Color& color) override
			{
				m_Border = static_cast<COLORREF>(PackBgr(color));

				// Only once installed: before that, Install's own call is what puts the colour
				// on, and DWM has no subclassed window to answer about yet.
				if (IsInstalled())
				{
					ApplyBorderColour();
				}
			}

		private:
			// Install leaves nothing behind on any path that does not set m_Previous.
			bool IsInstalled() const { return m_Previous != nullptr; }

			bool Maximized() const { return IsZoomed(m_Hwnd) != FALSE; }

			void ApplyBorderColour() const
			{
				const HRESULT set = DwmSetWindowAttribute(m_Hwnd, DWMWA_BORDER_COLOR, &m_Border,
														  sizeof(m_Border));

				if (FAILED(set))
				{
					CORE_WARN("The window border keeps the system colour: DWM refused "
							  "DWMWA_BORDER_COLOR (0x{:08X}), which needs Windows 11.",
							  static_cast<unsigned>(set));
				}
			}

			void RestoreSystemBorderColour() const
			{
				const COLORREF none = DWMWA_COLOR_DEFAULT;
				DwmSetWindowAttribute(m_Hwnd, DWMWA_BORDER_COLOR, &none, sizeof(none));
			}

			LRESULT OnCalcSize(WPARAM wParam, LPARAM lParam)
			{
				// A zero wParam means lParam is a bare RECT: the system is only asking what
				// client area a proposed window rectangle would give. Nothing to correct.
				if (wParam == 0u)
				{
					return CallWindowProcW(m_Previous, m_Hwnd, WM_NCCALCSIZE, wParam, lParam);
				}

				auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
				RECT& target = params->rgrc[0];
				const UINT dpi = DpiOf(m_Hwnd);

				// Either inset is far short of a caption's height, so no room is left for the
				// system to paint one into. A maximized window needs no border of its own:
				// every edge it has is off the screen already.
				if (Maximized())
				{
					InsetByResizeFrame(target, dpi);
				}
				else
				{
					InsetByBorder(target, dpi);
				}

				// Zero means "the client area is rgrc[0], keep none of the old one", which
				// costs a repaint of the client area on resize and nothing else.
				return 0;
			}

			LRESULT OnHitTest(LPARAM lParam) const
			{
				// WM_NCHITTEST asks about a point in screen coordinates — it has to, since the
				// point may lie outside the client area.
				POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
				ScreenToClient(m_Hwnd, &point);

				RECT client{};
				GetClientRect(m_Hwnd, &client);

				const CaptionHit hit =
					Classify(m_Metrics, static_cast<float>(client.right - client.left),
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

				// Unreachable, and kept for what it would cost: DefWindowProcW skips GLFW's
				// procedure entirely, and a window whose events never reach GLFW is deaf.
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

				return CallWindowProcW(self->m_Previous, hwnd, message, wParam, lParam);
			}

			HWND m_Hwnd = nullptr;
			COLORREF m_Border = 0;
			WNDPROC m_Previous = nullptr;
			CaptionMetrics m_Metrics;
		};
	}
#endif

	Result<Scope<WindowFrame>> WindowFrame::Hide([[maybe_unused]] Window& window,
												 [[maybe_unused]] const Color& border)
	{
#ifdef CORE_PLATFORM_WINDOWS
		auto* glfwHandle = static_cast<GLFWwindow*>(window.GetNativeHandle());
		if (glfwHandle == nullptr)
		{
			return Error{ErrorCode::Unsupported, "the window has no native handle"};
		}

		HWND hwnd = glfwGetWin32Window(glfwHandle);
		if (hwnd == nullptr)
		{
			return Error{ErrorCode::Unsupported, "the window is not a Win32 window"};
		}

		auto frame = CreateScope<Win32Frame>(hwnd, static_cast<COLORREF>(PackBgr(border)));

		const Result<void> installed = frame->Install();
		if (!installed)
		{
			return installed.GetError();
		}

		return Result<Scope<WindowFrame>>{std::move(frame)};
#else
		return Error{ErrorCode::Unsupported,
					 "removing the system title bar is only implemented on Windows"};
#endif
	}
}