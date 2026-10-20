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

		constexpr int kIgnoredPosition = 0;
		constexpr int kIgnoredSize = 0;
		constexpr UINT kFrameChangeOnly = SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE;

		constexpr UINT kDpiOfInvalidWindow = 0;

		constexpr LRESULT kClientAreaIsFirstRectangle = 0;

		// The frame is only recomputed on a frame change, so a caption that came or went stays on
		// screen until the user next resizes the window.
		void RequestFrameChange(HWND windowHandle)
		{
			SetWindowPos(windowHandle, nullptr, kIgnoredPosition, kIgnoredPosition, kIgnoredSize, kIgnoredSize, kFrameChangeOnly);
		}

		// Asked per monitor: GLFW makes the process per-monitor DPI aware, so the plain
		// GetSystemMetrics would answer for the primary monitor and be wrong on any other.
		UINT DpiOf(HWND windowHandle)
		{
			const UINT windowDpi = GetDpiForWindow(windowHandle);

			return windowDpi == kDpiOfInvalidWindow ? USER_DEFAULT_SCREEN_DPI : windowDpi;
		}

		void Inset(RECT& rectangle, int x, int y)
		{
			rectangle.left += x;
			rectangle.right -= x;
			rectangle.top += y;
			rectangle.bottom -= y;
		}

		// A maximized window is placed one frame thickness outside the work area on every side,
		// because the system expects the frame to cover the overhang; with no frame left to cover it,
		// the top of the caption would fall off the screen.
		void InsetByResizeFrame(RECT& rectangle, UINT dpi)
		{
			// There is no SM_CYPADDEDBORDER: the one padded border spans both axes.
			const int paddedBorder = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
			const int horizontalFrame = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + paddedBorder;
			const int verticalFrame = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + paddedBorder;

			Inset(rectangle, horizontalFrame, verticalFrame);
		}

		// DWM draws the border in the band between the window and client rectangles, so the client
		// area gives up one SM_CXBORDER per edge, the least that still shows. It is asked per monitor
		// rather than hard-coded to the one pixel it is at 96 DPI.
		void InsetByBorder(RECT& rectangle, UINT dpi)
		{
			const int horizontalBorder = GetSystemMetricsForDpi(SM_CXBORDER, dpi);
			const int verticalBorder = GetSystemMetricsForDpi(SM_CYBORDER, dpi);

			Inset(rectangle, horizontalBorder, verticalBorder);
		}

		class Win32Frame final : public WindowFrame
		{
		public:
			Win32Frame(HWND windowHandle, COLORREF borderColour, FrameBorder border)
				: m_WindowHandle(windowHandle), m_BorderColour(borderColour), m_Border(border)
			{
			}

			~Win32Frame() override
			{
				if (!IsInstalled())
				{
					return;
				}

				RestoreSystemBorderColour();

				SetWindowLongPtrW(m_WindowHandle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_ReplacedWindowProcedure));

				RequestFrameChange(m_WindowHandle);
				RemovePropW(m_WindowHandle, kFrameProperty);
			}

			Result<void> Install()
			{
				constexpr LONG_PTR kFrameStyles = WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
				const LONG_PTR style = GetWindowLongPtrW(m_WindowHandle, GWL_STYLE);
				SetWindowLongPtrW(m_WindowHandle, GWL_STYLE, style | kFrameStyles);

				// The property goes on before the subclass, so the procedure never runs without
				// finding its frame.
				const BOOL propertySet = SetPropW(m_WindowHandle, kFrameProperty, this);

				if (propertySet == FALSE)
				{
					return Error{.Code = ErrorCode::Unsupported, .Message = "the window would not hold the frame's property"};
				}

				// A live window's procedure is never null, so zero from SetWindowLongPtrW is a failure
				// and not a previous procedure worth keeping.
				m_ReplacedWindowProcedure = reinterpret_cast<WNDPROC>(
					SetWindowLongPtrW(m_WindowHandle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProcedure)));

				if (m_ReplacedWindowProcedure == nullptr)
				{
					RemovePropW(m_WindowHandle, kFrameProperty);
					return Error{.Code = ErrorCode::Unsupported, .Message = "the window procedure could not be subclassed"};
				}

				ApplyBorderColour();
				RequestFrameChange(m_WindowHandle);

				return Result<void>::Ok();
			}

			void SetCaptionMetrics(const CaptionMetrics& metrics) override
			{
				m_Metrics = metrics;
			}

			void SetBorderColor(const Color& color) override
			{
				m_BorderColour = static_cast<COLORREF>(PackBgr(color));

				if (IsInstalled())
				{
					ApplyBorderColour();
				}
			}

		private:
			bool IsInstalled() const { return m_ReplacedWindowProcedure != nullptr; }

			bool IsWindowMaximized() const { return IsZoomed(m_WindowHandle) != FALSE; }

			void ApplyBorderColour() const
			{
				const HRESULT attributeResult = DwmSetWindowAttribute(m_WindowHandle, DWMWA_BORDER_COLOR, &m_BorderColour,
																	  sizeof(m_BorderColour));

				if (FAILED(attributeResult))
				{
					CORE_WARN("The window border keeps the system colour: DWM refused "
							  "DWMWA_BORDER_COLOR (0x{:08X}), which needs Windows 11.",
							  static_cast<unsigned>(attributeResult));
				}
			}

			void RestoreSystemBorderColour() const
			{
				const COLORREF systemColour = DWMWA_COLOR_DEFAULT;
				DwmSetWindowAttribute(m_WindowHandle, DWMWA_BORDER_COLOR, &systemColour, sizeof(systemColour));
			}

			LRESULT OnCalcSize(WPARAM asksForValidArea, LPARAM rectanglesAddress)
			{
				const bool addressesBareRectangle = asksForValidArea == 0u;

				// A bare RECT only asks what client area a proposed window rectangle would give, so the replaced procedure answers it.
				if (addressesBareRectangle)
				{
					return CallWindowProcW(m_ReplacedWindowProcedure, m_WindowHandle, WM_NCCALCSIZE, asksForValidArea, rectanglesAddress);
				}

				auto* calcSizeParameters = reinterpret_cast<NCCALCSIZE_PARAMS*>(rectanglesAddress);
				RECT& proposedClientArea = calcSizeParameters->rgrc[0];
				const UINT dpi = DpiOf(m_WindowHandle);

				// Either inset is far short of a caption's height, so no room is left for the system to
				// paint one. A maximized window needs no border of its own: every edge it has is off the
				// screen already.
				if (IsWindowMaximized())
				{
					InsetByResizeFrame(proposedClientArea, dpi);
				}
				else if (m_Border == FrameBorder::Inset)
				{
					InsetByBorder(proposedClientArea, dpi);
				}

				return kClientAreaIsFirstRectangle;
			}

			LRESULT OnHitTest(LPARAM packedScreenPoint) const
			{
				POINT pointer{.x = GET_X_LPARAM(packedScreenPoint), .y = GET_Y_LPARAM(packedScreenPoint)};
				ScreenToClient(m_WindowHandle, &pointer);

				RECT clientArea{};
				GetClientRect(m_WindowHandle, &clientArea);

				const float clientWidth = static_cast<float>(clientArea.right - clientArea.left);
				const float clientHeight = static_cast<float>(clientArea.bottom - clientArea.top);
				const float pointerX = static_cast<float>(pointer.x);
				const float pointerY = static_cast<float>(pointer.y);

				const WindowState windowState = IsWindowMaximized() ? WindowState::Maximized : WindowState::Restored;

				const CaptionHit captionHit = Classify(m_Metrics, clientWidth, clientHeight, pointerX, pointerY, windowState);

				switch (captionHit)
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

			static LRESULT CALLBACK WindowProcedure(HWND windowHandle, UINT message, WPARAM messageWordParameter, LPARAM messageLongParameter)
			{
				auto* frame = static_cast<Win32Frame*>(GetPropW(windowHandle, kFrameProperty));

				// Unreachable, since the property outlives the subclass. Were it reached, DefWindowProcW
				// would skip GLFW's procedure and the window would no longer hear its events.
				if (frame == nullptr)
				{
					return DefWindowProcW(windowHandle, message, messageWordParameter, messageLongParameter);
				}

				switch (message)
				{
				case WM_NCCALCSIZE:
					return frame->OnCalcSize(messageWordParameter, messageLongParameter);
				case WM_NCHITTEST:
					return frame->OnHitTest(messageLongParameter);
				default:
					break;
				}

				return CallWindowProcW(frame->m_ReplacedWindowProcedure, windowHandle, message, messageWordParameter, messageLongParameter);
			}

			HWND m_WindowHandle = nullptr;
			COLORREF m_BorderColour = 0;
			FrameBorder m_Border = FrameBorder::Inset;
			WNDPROC m_ReplacedWindowProcedure = nullptr;
			CaptionMetrics m_Metrics;
		};
	}
#endif

	Result<Scope<WindowFrame>> WindowFrame::Hide(Window& window, const Color& borderColor)
	{
		return HideCaptionOf(window.GetNativeHandle(), borderColor);
	}

	Result<Scope<WindowFrame>> WindowFrame::HideCaptionOf([[maybe_unused]] void* nativeWindow,
	                                                      [[maybe_unused]] const Color& borderColor,
	                                                      [[maybe_unused]] FrameBorder border)
	{
#ifdef CORE_PLATFORM_WINDOWS
		auto* glfwWindow = static_cast<GLFWwindow*>(nativeWindow);
		if (glfwWindow == nullptr)
		{
			return Error{.Code = ErrorCode::Unsupported, .Message = "the window has no native handle"};
		}

		HWND windowHandle = glfwGetWin32Window(glfwWindow);
		if (windowHandle == nullptr)
		{
			return Error{.Code = ErrorCode::Unsupported, .Message = "the window is not a Win32 window"};
		}

		auto frame = CreateScope<Win32Frame>(windowHandle, static_cast<COLORREF>(PackBgr(borderColor)), border);

		const Result<void> installResult = frame->Install();
		if (!installResult)
		{
			return installResult.GetError();
		}

		return Result<Scope<WindowFrame>>{std::move(frame)};
#else
		return Error{.Code = ErrorCode::Unsupported, .Message = "removing the system title bar is only implemented on Windows"};
#endif
	}
}