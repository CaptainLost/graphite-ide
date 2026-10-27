#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

struct ImGuiWindow;

namespace Core {
	struct BlameHunk;
}

namespace Graphite {

	struct LineAuthors;
	struct InteractiveTooltipPlace;
	struct InteractiveTooltipState;

	inline constexpr float kAuthorCardLeaveGraceSeconds = 0.25f;

	struct AuthorCardInput
	{
		bool OverText = false;
		bool OverCard = false;
		bool ClickedText = false;
		bool Dismissed = false;   // a key, a click outside the card, or the wheel
		bool TextShown = true;    // the author text was drawn, so a mouse not over it has left it
		std::size_t Line = 0;
		float Seconds = 0.0f;     // since the last Update
	};

	// When the card over the line's author shows. Seconds count towards resting on the text or being away from
	// both only when the previous Update saw the mouse in the same place: the first frame after the loop slept
	// carries the whole sleep.
	class AuthorCardTracker
	{
	public:
		void Update(const AuthorCardInput& input, float delaySeconds);

		// Hides the card as a dismissing input does, so it waits for the mouse to leave the text before showing again.
		void Dismiss();

		std::optional<std::size_t> ShownLine() const;

		std::optional<float> SecondsUntilShown() const;    // for Core::Redraw::RequestIn
		std::optional<float> SecondsUntilHidden() const;   // likewise, while the leave grace runs

	private:
		enum class Place : std::uint8_t
		{
			Elsewhere,
			Text,
			Card
		};

		void Hide();

		std::optional<std::size_t> m_Line;
		Place m_Place = Place::Elsewhere;
		float m_SecondsOnText = 0.0f;
		float m_SecondsAway = 0.0f;
		float m_Delay = 0.0f;
		bool m_Shown = false;
		bool m_WaitingToLeave = false;
	};

	struct AuthorCardContent
	{
		std::string Name;
		std::string Email;
		std::string When;   // the age and the reader's local time at that moment, joined by kMiddleDotSeparator
		std::string Title;
		std::string Body;
		std::string ShortHash;
		std::string FullHash;
		bool Uncommitted = false;
	};

	// Empty for an Unknown line, a line past the table, or when the blame or the line's commit is missing.
	std::optional<AuthorCardContent> BuildAuthorCard(const LineAuthors& authors, std::size_t bufferLine,
													 std::chrono::sys_seconds now, const std::chrono::time_zone* zone);

	// The blame hunk a committed buffer line came from; null for any other line.
	const Core::BlameHunk* CommittedHunkOf(const LineAuthors& authors, std::size_t bufferLine);

	struct AuthorCardAction
	{
		enum class Kind : std::uint8_t
		{
			LineHistory,
			ShowCommit,
			CopyHash
		};

		Kind Action = Kind::LineHistory;
		std::size_t Line = 0;

		friend bool operator==(const AuthorCardAction&, const AuthorCardAction&) = default;
	};

	// Draws the card in the interactive window ##AuthorCard{&state}, under or over the place's anchor; the action
	// clicked this frame, if any.
	std::optional<AuthorCardAction::Kind> DrawAuthorCard(const AuthorCardContent& content, InteractiveTooltipState& state,
														 const InteractiveTooltipPlace& place, ImGuiWindow* returnFocusTo);
}
