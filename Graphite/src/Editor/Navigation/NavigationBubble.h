#pragma once

#include "Language/Api/LanguageService.h"

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	enum class NavigationCommand : std::uint8_t
	{
		Definition,
		SwitchSourceHeader
	};

	struct CaretPlacement
	{
		float X = 0.0f;
		float Y = 0.0f;
		float LineHeight = 0.0f;

		ImVec2 SurfaceMin{};
		ImVec2 SurfaceMax{};

		ImGuiID Viewport = 0;
		int DrawnInFrame = -1;
	};

	bool IsCaretInSurface(const CaretPlacement& caret);

	// WARNING: The caret and revision are the bubble's own document's, not the active document's: a Ctrl+click in a pane
	// that is not active yet gives its command before that pane becomes the active one.
	struct ReaderPosition
	{
		std::optional<std::filesystem::path> ActiveDocument;
		bool BubbleDocumentShown = false;
		std::size_t CaretOffset = 0;
		std::size_t Revision = 0;
		bool EscapePressed = false;
	};

	struct ShownBubble
	{
		std::string Text;
		bool Waiting = false;
	};

	inline constexpr std::string_view kFindingDefinition = "Finding definition\xE2\x80\xA6";
	inline constexpr std::string_view kLookingForCounterpart = "Looking for the header/source counterpart\xE2\x80\xA6";
	inline constexpr std::string_view kNoCounterpart = "No header/source counterpart";
	inline constexpr std::string_view kCloseTheDialogFirst = "Close the open dialog, then press F12 again";

	std::string NoDefinitionFoundText(const ServerStatus& status);

	class NavigationBubble
	{
	public:
		static constexpr double kShowAfterSeconds = 0.2;
		static constexpr double kOutcomeSeconds = 2.5;

		void BeginWaiting(NavigationCommand command, const std::filesystem::path& path, std::size_t caretOffset,
						  std::size_t revision, std::string text, double now);

		void SetWaitingText(std::string text);

		void End(NavigationCommand command, std::string message, double now);

		void Close(NavigationCommand command);

		std::optional<NavigationCommand> AbandonIfReaderMovedOn(const ReaderPosition& reader);

		bool IsWaitingFor(NavigationCommand command) const;

		bool IsOpen() const;

		// WARNING: Open is not seen. A wait shows only after kShowAfterSeconds and an outcome fades after
		// kOutcomeSeconds, yet both stay open until the reader moves on; and nothing is drawn at a caret off its
		// editor's surface or not drawn in `frame`, nor a wait while a modal is open. Whatever takes a key from in front
		// of the editor asks this, not IsOpen.
		bool IsSeenAt(double now, const CaretPlacement& caret, int frame, bool modalOpen) const;

		std::optional<ShownBubble> ShownAt(double now) const;

		std::optional<double> NextChangeAfter(double now) const;

		const std::filesystem::path& GetPath() const { return m_Path; }

	private:
		enum class Phase : std::uint8_t
		{
			Closed,
			Waiting,
			Ended
		};

		bool ReaderMovedOn(const ReaderPosition& reader);

		void Reset();

		Phase m_Phase = Phase::Closed;
		NavigationCommand m_Command = NavigationCommand::Definition;

		std::filesystem::path m_Path;
		std::size_t m_CaretOffset = 0;
		std::size_t m_Revision = 0;

		bool m_DocumentHasBeenActive = false;

		std::string m_Text;

		double m_StartedAt = 0.0;
		double m_EndsAt = 0.0;
	};

	// Whether a shown bubble is drawn at this caret: one inside its editor's surface, and for a wait, no modal open.
	bool IsBubbleDrawnAt(const ShownBubble& shown, const CaretPlacement& caret, bool modalOpen);

	// WARNING: The bubble is placed by the size it had last frame, so the caller keeps what this returns for the next.
	ImVec2 DrawNavigationBubble(const ShownBubble& shown, const CaretPlacement& caret, ImVec2 lastSize);
}
