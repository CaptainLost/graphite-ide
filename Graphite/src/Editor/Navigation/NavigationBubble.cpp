#include "Editor/Navigation/NavigationBubble.h"

#include "Editor/Hover/HoverLayout.h"
#include "Language/Server/ServerWaitingText.h"
#include "UI/TypographicMarks.h"

#include "Core/ImGui/LoadingState.h"

#include <imgui_internal.h>

#include <utility>

namespace Graphite {

	namespace {
		constexpr float kAnchorWidth = 1.0f;
		constexpr int kStyleVarsPushed = 3;

		constexpr ImGuiWindowFlags kBubbleWindowFlags =
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing |
			ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize;

		ImGuiViewport* ViewportOf(const CaretPlacement& caret)
		{
			ImGuiViewport* const viewport = ImGui::FindViewportByID(caret.Viewport);

			return viewport != nullptr ? viewport : ImGui::GetMainViewport();
		}
	}

	bool IsCaretInSurface(const CaretPlacement& caret)
	{
		const bool insideAcross = caret.X >= caret.SurfaceMin.x && caret.X <= caret.SurfaceMax.x;
		const bool insideDown = caret.Y >= caret.SurfaceMin.y && caret.Y + caret.LineHeight <= caret.SurfaceMax.y;

		return insideAcross && insideDown;
	}

	std::string NoDefinitionFoundText(const ServerStatus& status)
	{
		if (IsStillIndexing(status))
		{
			return "No definition found yet" + std::string{kMiddleDotSeparator} + StillIndexingText(status);
		}

		return "No definition found";
	}

	void NavigationBubble::BeginWaiting(NavigationCommand command, const std::filesystem::path& path,
										std::size_t caretOffset, std::size_t revision, std::string text, double now)
	{
		m_Phase = Phase::Waiting;
		m_Command = command;
		m_Path = path;
		m_CaretOffset = caretOffset;
		m_Revision = revision;
		m_Text = std::move(text);
		m_StartedAt = now;
		m_DocumentHasBeenActive = false;
	}

	void NavigationBubble::SetWaitingText(std::string text)
	{
		if (m_Phase == Phase::Waiting)
		{
			m_Text = std::move(text);
		}
	}

	void NavigationBubble::End(NavigationCommand command, std::string message, double now)
	{
		if (m_Phase == Phase::Closed || m_Command != command)
		{
			return;
		}

		m_Phase = Phase::Ended;
		m_Text = std::move(message);
		m_EndsAt = now + kOutcomeSeconds;
	}

	void NavigationBubble::Close(NavigationCommand command)
	{
		if (m_Command == command)
		{
			Reset();
		}
	}

	void NavigationBubble::Reset()
	{
		m_Phase = Phase::Closed;
		m_Text.clear();
	}

	// WARNING: Another document being active counts only once the bubble's own has been: until its pane takes the focus,
	// which ImGui gives later in the frame of the click, the document that was active before still is.
	bool NavigationBubble::ReaderMovedOn(const ReaderPosition& reader)
	{
		const bool movedWithinDocument = reader.CaretOffset != m_CaretOffset || reader.Revision != m_Revision;

		if (!reader.BubbleDocumentShown || movedWithinDocument || reader.EscapePressed)
		{
			return true;
		}

		if (reader.ActiveDocument == m_Path)
		{
			m_DocumentHasBeenActive = true;
			return false;
		}

		return m_DocumentHasBeenActive;
	}

	std::optional<NavigationCommand> NavigationBubble::AbandonIfReaderMovedOn(const ReaderPosition& reader)
	{
		if (m_Phase == Phase::Closed || !ReaderMovedOn(reader))
		{
			return std::nullopt;
		}

		const bool wasWaiting = m_Phase == Phase::Waiting;
		Reset();

		return wasWaiting ? std::optional<NavigationCommand>{m_Command} : std::nullopt;
	}

	bool NavigationBubble::IsWaitingFor(NavigationCommand command) const
	{
		return m_Phase == Phase::Waiting && m_Command == command;
	}

	bool NavigationBubble::IsOpen() const
	{
		return m_Phase != Phase::Closed;
	}

	std::optional<ShownBubble> NavigationBubble::ShownAt(double now) const
	{
		switch (m_Phase)
		{
		case Phase::Waiting:
			if (now < m_StartedAt + kShowAfterSeconds)
			{
				return std::nullopt;
			}

			return ShownBubble{.Text = m_Text, .Waiting = true};

		case Phase::Ended:
			if (now >= m_EndsAt)
			{
				return std::nullopt;
			}

			return ShownBubble{.Text = m_Text, .Waiting = false};

		case Phase::Closed:
			break;
		}

		return std::nullopt;
	}

	std::optional<double> NavigationBubble::NextChangeAfter(double now) const
	{
		if (m_Phase == Phase::Waiting && now < m_StartedAt + kShowAfterSeconds)
		{
			return m_StartedAt + kShowAfterSeconds;
		}

		if (m_Phase == Phase::Ended && now < m_EndsAt)
		{
			return m_EndsAt;
		}

		return std::nullopt;
	}

	ImVec2 DrawNavigationBubble(const ShownBubble& shown, const CaretPlacement& caret, ImVec2 lastSize)
	{
		// WARNING: A wait is not drawn over a modal, as the bubble is raised to the front every frame. How the command
		// ended still is, because "Close the open dialog" is said while a dialog is open.
		const bool waitBehindModal = shown.Waiting && ImGui::GetTopMostPopupModal() != nullptr;

		if (!IsCaretInSurface(caret) || waitBehindModal)
		{
			return lastSize;
		}

		const ImGuiStyle& style = ImGui::GetStyle();

		const ImVec2 anchorMin(caret.X, caret.Y);
		const ImVec2 anchorMax(caret.X + kAnchorWidth, caret.Y + caret.LineHeight);
		const TooltipPlacement placement = PlaceTooltip(anchorMin, anchorMax, lastSize, caret.SurfaceMin, caret.SurfaceMax);

		ImGui::SetNextWindowViewport(ViewportOf(caret)->ID);
		ImGui::SetNextWindowPos(placement.Position, ImGuiCond_Always, placement.Pivot);
		ImGui::SetNextWindowBgAlpha(1.0f);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, style.WindowPadding);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, style.PopupRounding);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, style.PopupBorderSize);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));

		ImGui::Begin("##NavigationBubble", nullptr, kBubbleWindowFlags);

		// WARNING: A window never focused falls behind whatever is focused after it, so the editor would cover the
		// bubble. Raising it every frame moves no focus.
		ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

		if (shown.Waiting)
		{
			Core::LoadingLine(shown.Text.c_str(), Core::WhileLoaderWaits::ShowText);
		}
		else
		{
			ImGui::TextUnformatted(shown.Text.c_str());
		}

		const ImVec2 size = ImGui::GetWindowSize();

		ImGui::End();

		ImGui::PopStyleColor();
		ImGui::PopStyleVar(kStyleVarsPushed);

		return size;
	}
}
