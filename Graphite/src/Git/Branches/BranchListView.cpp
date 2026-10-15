#include "Git/Branches/BranchListView.h"

#include "Git/Branches/BranchAge.h"
#include "Git/Branches/RefSectionHeading.h"
#include "Git/GitView.h"
#include "UI/HighlightedRow.h"
#include "UI/IconButton.h"

#include "Core/App/Redraw.h"
#include "Core/ImGui/LoadingState.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <chrono>
#include <format>

namespace Graphite {

	namespace {

		constexpr const char* kMenuId = "##branchListMenu";
		constexpr const char* kWidestAge = "12mo";
		constexpr const char* kMineChip = "Mine";
		constexpr const char* kMessageChip = "Message";
		constexpr const char* kMessageChipId = "##messageChip";
		constexpr const char* kReadingAuthors = "Reading authors";
		constexpr const char* kSetUserEmail = "Set user.email in git config to use this";
		constexpr float kFailureWidthInFontSizes = 24.0f;
		constexpr double kSecondsBeforeLoaderShows = 0.2;

		// True once the wait has lasted long enough for a loader; until then asks for the frame on
		// which it will have.
		bool LoaderIsDue(std::optional<double>& waitingSince)
		{
			const double now = ImGui::GetTime();

			if (!waitingSince.has_value())
			{
				waitingSince = now;
			}

			const double secondsWaited = now - *waitingSince;

			if (secondsWaited < kSecondsBeforeLoaderShows)
			{
				Core::Redraw::RequestIn(std::chrono::duration<double>(kSecondsBeforeLoaderShows - secondsWaited));
				return false;
			}

			return true;
		}

		float IndentOf(const BranchRow& row)
		{
			return static_cast<float>(row.Depth) * ImGui::GetStyle().IndentSpacing;
		}

		void EmptyRow()
		{
			ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
		}

		std::string OnlyMineTooltipOf(const BranchListFrame& frame)
		{
			if (frame.Authors)
			{
				return kSetUserEmail;
			}

			if (frame.AuthorsFailure.has_value())
			{
				return AuthorsFailureText(*frame.AuthorsFailure);
			}

			return kReadingAuthors;
		}
	}

	BranchListView::BranchListView(BranchListState& options) : m_Options(options)
	{
	}

	void BranchListView::Reset()
	{
		m_TextBuffer = {};
		m_Filter = BranchListFilter{};
		m_Words.clear();
		m_ExpandedGroups.clear();
		m_RowsBuiltFrom.reset();
		m_Rows.clear();
		m_Choices.clear();
		m_BranchesWaitingSince.reset();
		m_AuthorsWaitingSince.reset();
		m_AuthorsMenuWaitingSince.reset();
	}

	BranchListView::RowsBuiltFrom BranchListView::InputsOf(const BranchListFrame& frame) const
	{
		return RowsBuiltFrom{.Branches = frame.Branches,
							 .Authors = frame.Authors,
							 .AuthorsFailure = frame.Authors ? std::nullopt : frame.AuthorsFailure,
							 .Filter = m_Filter,
							 .Options = m_Options,
							 .CollapsedSections = m_CollapsedSections,
							 .ExpandedGroups = m_ExpandedGroups};
	}

	BranchRowsInput BranchListView::RowsInputOf(const RowsBuiltFrom& inputs)
	{
		return BranchRowsInput{.Branches = inputs.Branches.get(),
							   .Authors = inputs.Authors.get(),
							   .AuthorsFailure = inputs.AuthorsFailure,
							   .Filter = inputs.Filter,
							   .Options = inputs.Options,
							   .CollapsedSections = inputs.CollapsedSections,
							   .ExpandedGroups = inputs.ExpandedGroups};
	}

	void BranchListView::RebuildRowsWhenOutOfDate(const BranchListFrame& frame)
	{
		RowsBuiltFrom inputs = InputsOf(frame);

		if (m_RowsBuiltFrom == inputs)
		{
			return;
		}

		const BranchRowsInput rowsInput = RowsInputOf(inputs);
		m_Rows = BuildBranchRows(rowsInput);
		m_Choices = ListAuthorChoices(rowsInput);
		m_Words = LoweredWordsOf(m_Filter.Text);
		m_RowsBuiltFrom = std::move(inputs);
	}

	void BranchListView::DrawHeader(const BranchListFrame& frame)
	{
		if (frame.Branches)
		{
			RebuildRowsWhenOutOfDate(frame);
		}

		if (frame.Authors || frame.AuthorsFailure.has_value())
		{
			m_AuthorsMenuWaitingSince.reset();
		}

		const float buttonRoom = IconButtonSize() + ImGui::GetStyle().ItemSpacing.x;

		ImGui::SetNextItemWidth(-buttonRoom);

		if (ImGui::InputTextWithHint("##branchFilter", ICON_MDI_MAGNIFY "  Filter branches", m_TextBuffer.data(),
									 m_TextBuffer.size()))
		{
			m_Filter.Text = m_TextBuffer.data();
		}

		ImGui::SameLine();

		const bool narrowed = !m_Filter.Authors.empty() || m_Filter.AlsoMessage || m_Options != BranchListState{};

		if (DrawIconButton("##branchListMenuButton", ICON_MDI_TUNE_VARIANT, narrowed, "Sort and filter"))
		{
			ImGui::OpenPopup(kMenuId);
		}

		DrawMenu(frame);
		DrawChips(frame);
	}

	void BranchListView::DrawMenu(const BranchListFrame& frame)
	{
		if (!ImGui::BeginPopup(kMenuId))
		{
			return;
		}

		ImGui::SeparatorText("Sort");
		DrawSortItem("Newest", BranchSort::Newest);
		DrawSortItem("Oldest", BranchSort::Oldest);
		DrawSortItem("Name", BranchSort::Name);

		ImGui::SeparatorText("Group by");
		DrawGroupingItem("None", BranchGrouping::None);
		DrawGroupingItem("Author", BranchGrouping::Author);

		ImGui::Separator();
		DrawAuthorsMenu(frame);
		DrawOnlyMineItem(frame);

		const char* const noShortcut = nullptr;

		if (ImGui::MenuItem("Also commit message", noShortcut, m_Filter.AlsoMessage))
		{
			m_Filter.AlsoMessage = !m_Filter.AlsoMessage;
		}

		ImGui::EndPopup();
	}

	void BranchListView::DrawSortItem(const char* label, BranchSort sort)
	{
		const char* const noShortcut = nullptr;

		if (ImGui::MenuItem(label, noShortcut, m_Options.Sort == sort))
		{
			m_Options.Sort = sort;
		}
	}

	void BranchListView::DrawGroupingItem(const char* label, BranchGrouping grouping)
	{
		const char* const noShortcut = nullptr;

		if (ImGui::MenuItem(label, noShortcut, m_Options.Group == grouping))
		{
			m_Options.Group = grouping;
		}
	}

	void BranchListView::DrawAuthorsMenu(const BranchListFrame& frame)
	{
		if (!ImGui::BeginMenu("Authors"))
		{
			return;
		}

		if (!frame.Authors && frame.AuthorsFailure.has_value())
		{
			const std::string failure = AuthorsFailureText(*frame.AuthorsFailure);

			// WARNING: A menu grows to fit what it holds, so the line is cut to a fixed width; cut to
			// the menu's own width it would never grow past its first frame.
			bool cut = false;
			const std::string fitted = FitToWidth(failure, ImGui::GetFontSize() * kFailureWidthInFontSizes, &cut);
			ImGui::TextDisabled("%s", fitted.c_str());

			if (cut)
			{
				ImGui::SetItemTooltip("%s", failure.c_str());
			}

			ImGui::EndMenu();
			return;
		}

		if (!frame.Authors)
		{
			if (LoaderIsDue(m_AuthorsMenuWaitingSince))
			{
				Core::LoadingLine(kReadingAuthors);
			}
			else
			{
				ImGui::TextDisabled("%s", kReadingAuthors);
			}

			ImGui::EndMenu();
			return;
		}

		if (m_Choices.empty())
		{
			ImGui::TextDisabled("No authors");
		}

		// WARNING: A menu item closes its popup when clicked; ticking several authors in a row
		// needs the popup to stay.
		const bool closesPopup = false;
		ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, closesPopup);

		const char* const noShortcut = nullptr;
		std::optional<std::string> toggledKey;

		for (const AuthorChoice& choice : m_Choices)
		{
			ImGui::PushID(choice.Key.c_str());

			if (ImGui::MenuItem(AuthorChoiceText(choice).c_str(), noShortcut, choice.Ticked))
			{
				toggledKey = choice.Key;
			}

			ImGui::PopID();
		}

		ImGui::PopItemFlag();
		ImGui::EndMenu();

		if (toggledKey.has_value())
		{
			ToggleAuthor(*toggledKey);
		}
	}

	void BranchListView::DrawOnlyMineItem(const BranchListFrame& frame)
	{
		const bool emailKnown = frame.Authors && frame.Authors->UserEmail.has_value();
		const std::string userKey = emailKnown ? AuthorKeyOf(*frame.Authors->UserEmail) : std::string{};
		const bool ticked = emailKnown && m_Filter.Authors.contains(userKey);

		const char* const noShortcut = nullptr;

		if (ImGui::MenuItem("Only mine", noShortcut, ticked, emailKnown))
		{
			ToggleAuthor(userKey);
		}

		if (!emailKnown && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("%s", OnlyMineTooltipOf(frame).c_str());
		}
	}

	std::string BranchListView::ChipTextOf(const std::string& key, const BranchListFrame& frame) const
	{
		if (frame.Authors && IsUserKey(key, *frame.Authors))
		{
			return kMineChip;
		}

		for (const AuthorChoice& choice : m_Choices)
		{
			if (choice.Key == key)
			{
				return choice.Name;
			}
		}

		return key.empty() ? std::string{kUnknownAuthorName} : key;
	}

	void BranchListView::DrawChips(const BranchListFrame& frame)
	{
		if (m_Filter.Authors.empty() && !m_Filter.AlsoMessage)
		{
			return;
		}

		const float rowRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
		bool isFirstChip = true;

		std::optional<std::string> removedKey;
		bool messageRemoved = false;

		for (const std::string& key : m_Filter.Authors)
		{
			const std::string label = ChipTextOf(key, frame) + "  " ICON_MDI_CLOSE;
			PlaceChip(label, isFirstChip, rowRight);

			ImGui::PushID(key.c_str());

			if (ImGui::SmallButton(label.c_str()))
			{
				removedKey = key;
			}

			ImGui::PopID();
		}

		if (m_Filter.AlsoMessage)
		{
			const std::string label = std::string{kMessageChip} + "  " ICON_MDI_CLOSE;
			PlaceChip(label, isFirstChip, rowRight);

			ImGui::PushID(kMessageChipId);
			messageRemoved = ImGui::SmallButton(label.c_str());
			ImGui::PopID();
		}

		if (removedKey.has_value())
		{
			m_Filter.Authors.erase(*removedKey);
		}

		if (messageRemoved)
		{
			m_Filter.AlsoMessage = false;
		}
	}

	void BranchListView::PlaceChip(const std::string& label, bool& isFirstChip, float rowRight) const
	{
		const float chipWidth = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;

		if (!isFirstChip)
		{
			ImGui::SameLine();

			if (ImGui::GetCursorScreenPos().x + chipWidth > rowRight)
			{
				ImGui::NewLine();
			}
		}

		isFirstChip = false;
	}

	void BranchListView::ToggleAuthor(const std::string& key)
	{
		if (m_Filter.Authors.contains(key))
		{
			m_Filter.Authors.erase(key);
		}
		else
		{
			m_Filter.Authors.insert(key);
		}
	}

	void BranchListView::DrawRows(const BranchListFrame& frame)
	{
		const bool waitingForBranches = !frame.Branches && !frame.BranchesFailed;

		if (!waitingForBranches)
		{
			m_BranchesWaitingSince.reset();
		}

		if (!frame.Branches)
		{
			if (waitingForBranches && LoaderIsDue(m_BranchesWaitingSince))
			{
				Core::LoadingState("Loading branches");
			}

			return;
		}

		RebuildRowsWhenOutOfDate(frame);

		const bool waitingForAuthors =
			m_RowsBuiltFrom.has_value() && ViewNeedsAuthors(RowsInputOf(*m_RowsBuiltFrom)) && !frame.Authors &&
			!frame.AuthorsFailure.has_value();

		if (!waitingForAuthors)
		{
			m_AuthorsWaitingSince.reset();
		}

		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(m_Rows.size()), ImGui::GetTextLineHeightWithSpacing());

		while (clipper.Step())
		{
			for (int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex)
			{
				DrawRow(m_Rows[static_cast<std::size_t>(rowIndex)], frame);
			}
		}

		clipper.End();
	}

	void BranchListView::DrawRow(const BranchRow& row, const BranchListFrame& frame)
	{
		switch (row.Kind)
		{
		case BranchRowKind::Section:
		{
			bool& collapsed = m_CollapsedSections[SectionIndexOf(row.Section)];
			DrawRefSectionHeading(row.Label.c_str(), SectionHeadingText(row), collapsed);
			break;
		}
		case BranchRowKind::AuthorGroup:
			DrawAuthorGroupRow(row);
			break;
		case BranchRowKind::Branch:
			DrawBranchRow(row, frame);
			break;
		case BranchRowKind::Loading:
		case BranchRowKind::Failed:
		case BranchRowKind::NoMatches:
			DrawWaitingRow(row);
			break;
		}
	}

	void BranchListView::DrawAuthorGroupRow(const BranchRow& row)
	{
		ImGui::PushID(static_cast<int>(SectionIndexOf(row.Section)));
		ImGui::PushID(row.AuthorKey.c_str());

		const ImVec2 rowStart = ImGui::GetCursorPos();
		const bool notSelected = false;

		if (ImGui::Selectable("##group", notSelected, ImGuiSelectableFlags_AllowOverlap))
		{
			const AuthorGroupKey key{.Section = row.Section, .Author = row.AuthorKey};

			if (m_ExpandedGroups.contains(key))
			{
				m_ExpandedGroups.erase(key);
			}
			else
			{
				m_ExpandedGroups.insert(key);
			}
		}

		ImGui::SetCursorPos(ImVec2(rowStart.x + IndentOf(row), rowStart.y));
		ImGui::TextDisabled("%s", row.Collapsed ? ICON_MDI_CHEVRON_RIGHT : ICON_MDI_CHEVRON_DOWN);
		ImGui::SameLine();

		const std::string fitted = FitToWidth(AuthorGroupText(row), ImGui::GetContentRegionAvail().x);
		ImGui::TextUnformatted(fitted.c_str());

		ImGui::PopID();
		ImGui::PopID();
	}

	void BranchListView::DrawWaitingRow(const BranchRow& row)
	{
		ImGui::PushID(static_cast<int>(SectionIndexOf(row.Section)));

		if (row.Kind == BranchRowKind::Loading)
		{
			if (LoaderIsDue(m_AuthorsWaitingSince))
			{
				Core::LoadingLine(row.Label.c_str());
			}
			else
			{
				EmptyRow();
			}
		}
		else
		{
			bool cut = false;
			const std::string fitted = FitToWidth(row.Label, ImGui::GetContentRegionAvail().x, &cut);
			ImGui::TextDisabled("%s", fitted.c_str());

			if (cut)
			{
				ImGui::SetItemTooltip("%s", row.Label.c_str());
			}
		}

		ImGui::PopID();
	}

	void BranchListView::DrawBranchRow(const BranchRow& row, const BranchListFrame& frame)
	{
		const Core::Branch& branch = (*frame.Branches)[row.BranchIndex];
		const bool selected = frame.SelectedRef == branch.FullName;

		ImGui::PushID(row.AuthorKey.c_str());
		ImGui::PushID(branch.FullName.c_str());

		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const ImVec2 rowStart = ImGui::GetCursorScreenPos();
		const float rowWidth = ImGui::GetContentRegionAvail().x;
		const float indent = IndentOf(row);

		const std::string age =
			branch.TipCommit.has_value() ? FormatBranchAge(branch.TipCommit->Committer.When, frame.Now) : std::string{};
		const std::string badge = BadgeOf ? BadgeOf(branch) : std::string{};

		const float ageRoom = ImGui::CalcTextSize(kWidestAge).x;
		const float badgeWidth = badge.empty() ? 0.0f : ImGui::CalcTextSize(badge.c_str()).x;
		const float badgeRoom = badge.empty() ? 0.0f : badgeWidth + spacing;
		const float labelRoom = rowWidth - indent - ageRoom - spacing - badgeRoom;

		const std::string label = RefLabel(branch);
		const std::string fitted = FitToWidth(label, labelRoom);

		// WARNING: The group makes the menu below ask about the whole row. With the filter marked,
		// HighlightedSelectable ends on a piece of its label, and after the group the menu would
		// ask about the age instead.
		ImGui::BeginGroup();

		ImGui::SetCursorScreenPos(ImVec2(rowStart.x + indent, rowStart.y));
		const bool clicked = HighlightedSelectable("##branch", selected, fitted, FirstWordInName(branch.Name, m_Words));

		if (!badge.empty())
		{
			ImGui::SetCursorScreenPos(ImVec2(rowStart.x + rowWidth - ageRoom - spacing - badgeWidth, rowStart.y));
			ImGui::TextDisabled("%s", badge.c_str());
		}

		if (!age.empty())
		{
			const float ageWidth = ImGui::CalcTextSize(age.c_str()).x;
			ImGui::SetCursorScreenPos(ImVec2(rowStart.x + rowWidth - ageWidth, rowStart.y));
			ImGui::TextDisabled("%s", age.c_str());
		}

		// WARNING: A Selectable reports a box half the item spacing taller than its row, and a group
		// that ends on it grows by that much, which the clipper over these rows does not count. The
		// group ends on an empty item exactly one text row high instead.
		ImGui::SetCursorScreenPos(rowStart);
		EmptyRow();

		ImGui::EndGroup();

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		{
			const BranchAuthors noAnswer;
			const BranchAuthors& authors = frame.Authors ? *frame.Authors : noAnswer;
			const std::string whole = badge.empty() ? label : std::format("{}  {}", label, badge);

			ImGui::SetTooltip("%s",
							  BranchTooltipText(branch, whole, AuthorsOfBranch(branch, authors), frame.LocalOffset).c_str());
		}

		if (DrawBranchMenu)
		{
			DrawBranchMenu(branch);
		}

		ImGui::PopID();
		ImGui::PopID();

		if (clicked && OnBranchClicked)
		{
			OnBranchClicked(branch);
		}
	}
}