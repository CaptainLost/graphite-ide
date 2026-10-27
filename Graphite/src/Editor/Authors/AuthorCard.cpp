#include "Editor/Authors/AuthorCard.h"

#include "Editor/Authors/AuthorText.h"
#include "Editor/Hover/InteractiveTooltip.h"
#include "Git/Blame/LineAuthors.h"
#include "Git/History/CommitDetails.h"
#include "Git/History/CommitLabels.h"
#include "UI/TypographicMarks.h"

#include "Core/Git/Blame.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/TextButton.h"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <string_view>
#include <utility>

namespace Graphite {

	namespace {

		constexpr std::string_view kNotCommittedYet = "These changes are not committed yet.";

		constexpr float kTextWidthEms = 32.0f;
		constexpr float kBodyLines = 12.0f;
		constexpr float kBothSides = 2.0f;
		constexpr float kKeepFontSize = 0.0f;

		void MutedText(const std::string& text)
		{
			ImGui::TextDisabled("%s", text.c_str());
		}

		void SemiboldText(const std::string& text)
		{
			ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), kKeepFontSize);
			ImGui::TextUnformatted(text.c_str());
			ImGui::PopFont();
		}

		// WARNING: A body taller than its cap scrolls in a child of its own, so the child is given its width: one
		// sized to the space left would hold an auto-resizing card at whatever width it had last frame.
		void DrawBody(const std::string& body, float maxContentWidth, bool appearing)
		{
			const float height = ImGui::CalcTextSize(body.c_str(), nullptr, false, maxContentWidth).y;
			const float cap = ImGui::GetTextLineHeight() * kBodyLines;

			if (height <= cap)
			{
				ImGui::TextUnformatted(body.c_str());
				return;
			}

			if (appearing)
			{
				ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));
			}

			if (ImGui::BeginChild("##AuthorCardBody", ImVec2(maxContentWidth + ImGui::GetStyle().ScrollbarSize, cap)))
			{
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + maxContentWidth);
				ImGui::TextUnformatted(body.c_str());
				ImGui::PopTextWrapPos();
			}

			ImGui::EndChild();
		}

		std::optional<AuthorCardAction::Kind> DrawActions(const AuthorCardContent& content)
		{
			std::optional<AuthorCardAction::Kind> clicked;

			if (!content.ShortHash.empty())
			{
				ImGui::AlignTextToFramePadding();
				MutedText(content.ShortHash);
				ImGui::SameLine();
			}

			if (Core::TextButton("History of this line"))
			{
				clicked = AuthorCardAction::Kind::LineHistory;
			}

			if (content.Uncommitted)
			{
				return clicked;
			}

			ImGui::SameLine();

			if (Core::TextButton("Show commit"))
			{
				clicked = AuthorCardAction::Kind::ShowCommit;
			}

			ImGui::SameLine();

			if (Core::TextButton("Copy hash"))
			{
				clicked = AuthorCardAction::Kind::CopyHash;
			}

			return clicked;
		}
	}

	void AuthorCardTracker::Update(const AuthorCardInput& input, float delaySeconds)
	{
		const Place place = input.OverText ? Place::Text : input.OverCard ? Place::Card : Place::Elsewhere;
		const bool stayed = std::exchange(m_Place, place) == place;
		const float seconds = stayed ? input.Seconds : 0.0f;

		m_Delay = delaySeconds;

		if (input.Dismissed)
		{
			Dismiss();
			return;
		}

		if (input.ClickedText)
		{
			m_Line = input.Line;
			m_Shown = true;
			m_WaitingToLeave = false;
			m_SecondsAway = 0.0f;
			return;
		}

		if (m_WaitingToLeave)
		{
			if (input.OverText || !input.TextShown)
			{
				return;
			}

			m_WaitingToLeave = false;
		}

		if (input.OverText)
		{
			if (m_Line != input.Line)
			{
				Hide();
				m_Line = input.Line;
			}
			else
			{
				m_SecondsOnText += seconds;
			}

			m_SecondsAway = 0.0f;
			m_Shown = m_Shown || m_SecondsOnText >= delaySeconds;
			return;
		}

		if (!m_Shown)
		{
			Hide();
			return;
		}

		if (input.OverCard)
		{
			m_SecondsAway = 0.0f;
			return;
		}

		m_SecondsAway += seconds;

		if (m_SecondsAway >= kAuthorCardLeaveGraceSeconds)
		{
			Hide();
		}
	}

	void AuthorCardTracker::Dismiss()
	{
		Hide();
		m_WaitingToLeave = true;
	}

	std::optional<std::size_t> AuthorCardTracker::ShownLine() const
	{
		return m_Shown ? m_Line : std::nullopt;
	}

	std::optional<float> AuthorCardTracker::SecondsUntilShown() const
	{
		if (m_Shown || m_WaitingToLeave || m_Place != Place::Text || !m_Line)
		{
			return std::nullopt;
		}

		return std::max(0.0f, m_Delay - m_SecondsOnText);
	}

	std::optional<float> AuthorCardTracker::SecondsUntilHidden() const
	{
		if (!m_Shown || m_Place != Place::Elsewhere)
		{
			return std::nullopt;
		}

		return std::max(0.0f, kAuthorCardLeaveGraceSeconds - m_SecondsAway);
	}

	void AuthorCardTracker::Hide()
	{
		m_Line.reset();
		m_Shown = false;
		m_SecondsOnText = 0.0f;
		m_SecondsAway = 0.0f;
	}

	const Core::BlameHunk* CommittedHunkOf(const LineAuthors& authors, std::size_t bufferLine)
	{
		if (bufferLine >= authors.Lines.size() || !authors.Head || !authors.Head->Blame)
		{
			return nullptr;
		}

		const LineAuthor& line = authors.Lines[bufferLine];
		const Core::Blame& blame = *authors.Head->Blame;

		if (line.Kind != LineAuthorKind::Committed || line.Hunk >= blame.Hunks.size())
		{
			return nullptr;
		}

		return &blame.Hunks[line.Hunk];
	}

	std::optional<AuthorCardContent> BuildAuthorCard(const LineAuthors& authors, std::size_t bufferLine,
													 std::chrono::sys_seconds now, const std::chrono::time_zone* zone)
	{
		if (bufferLine < authors.Lines.size() && authors.Lines[bufferLine].Kind == LineAuthorKind::Uncommitted)
		{
			return AuthorCardContent{.Name = std::string{kYou},
									 .Title = std::string{kUncommittedTitle},
									 .Body = std::string{kNotCommittedYet},
									 .Uncommitted = true};
		}

		const Core::BlameHunk* const hunk = CommittedHunkOf(authors, bufferLine);

		if (hunk == nullptr)
		{
			return std::nullopt;
		}

		const auto commit = authors.Head->Blame->Commits.find(hunk->Commit);

		if (commit == authors.Head->Blame->Commits.end())
		{
			return std::nullopt;
		}

		const Core::Signature& author = commit->second.Author;

		AuthorCardContent content;
		content.Name = AuthorName(author);
		content.Email = author.Email;
		content.When = RelativeAge(author.When, now) + std::string{kMiddleDotSeparator} + FormattedTimestamp(author.When, zone);
		content.Title = commit->second.Summary;
		content.Body = commit->second.Body;
		content.ShortHash = hunk->Commit.ShortHex();
		content.FullHash = hunk->Commit.ToHex();
		return content;
	}

	std::optional<AuthorCardAction::Kind> DrawAuthorCard(const AuthorCardContent& content, InteractiveTooltipState& state,
														 const InteractiveTooltipPlace& place, ImGuiWindow* returnFocusTo)
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const bool appearing = !state.Shown;

		Core::PushInterfaceFont();
		const float frameHeight = ImGui::GetFrameHeight();
		const float fontSize = ImGui::GetFontSize();
		ImGui::PopFont();

		InteractiveTooltipPlace cardPlace = place;
		cardPlace.LeastHeight = frameHeight + style.WindowPadding.y * kBothSides;

		const TooltipBounds bounds = InteractiveTooltipBounds(cardPlace);
		const float maxContentWidth =
			std::max(fontSize, std::min(kTextWidthEms * fontSize,
										bounds.Max.x - bounds.Min.x - style.WindowPadding.x * kBothSides - style.ScrollbarSize));

		const std::string name = std::format("##AuthorCard{}", static_cast<const void*>(&state));

		if (!BeginInteractiveTooltip(name.c_str(), state, cardPlace))
		{
			return std::nullopt;
		}

		Core::PushInterfaceFont();
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + maxContentWidth);

		ImGui::TextUnformatted(content.Name.c_str());

		if (!content.Email.empty())
		{
			ImGui::SameLine();
			MutedText(content.Email);
		}

		if (!content.When.empty())
		{
			MutedText(content.When);
		}

		if (!content.Title.empty())
		{
			SemiboldText(content.Title);
		}

		if (!content.Body.empty())
		{
			DrawBody(content.Body, maxContentWidth, appearing);
		}

		const std::optional<AuthorCardAction::Kind> clicked = DrawActions(content);

		ImGui::PopTextWrapPos();
		ImGui::PopFont();

		EndInteractiveTooltip(state, returnFocusTo);

		return clicked;
	}
}
