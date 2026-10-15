#include "Git/Branches/BranchListView.h"

#include "Git/Branches/BranchAge.h"
#include "Git/Branches/RefSectionHeading.h"
#include "Git/GitView.h"
#include "UI/HighlightedRow.h"
#include "UI/IconButton.h"
#include "UI/TickBox.h"

#include "Core/App/Redraw.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/Toggle.h"
#include "Core/ImGui/UiScale.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <string>

namespace Graphite {

	namespace {

		constexpr const char* kPanelId = "##branchListMenu";
		constexpr const char* kWidestAge = "12mo";
		constexpr const char* kMineChip = "Mine";
		constexpr const char* kMessageChip = "Message";
		constexpr const char* kMessageChipId = "##messageChip";
		constexpr const char* kReadingAuthors = "Reading authors";
		constexpr const char* kSetUserEmail = "Set user.email in git config to use this";
		constexpr const char* kNoAuthors = "No authors";
		constexpr const char* kNoAuthorsMatch = "No authors match";
		constexpr const char* kAuthorListId = "##authorList";
		constexpr const char* kTickBoxId = "###tick";
		constexpr double kSecondsBeforeLoaderShows = 0.2;

		constexpr float kPanelWidthInFontSizes = 22.0f;
		constexpr std::size_t kMostAuthorRowsShown = 10;
		constexpr bool kHideTextAfterDoubleHash = true;

		constexpr int kSelectedSegmentColourCount = 4;
		constexpr ImVec4 kSelectedSegmentText(1.0f, 1.0f, 1.0f, 1.0f);

		template <typename Choice>
		struct Segment
		{
			const char* Label;
			Choice Value;
		};

		constexpr std::array<Segment<BranchSort>, 3> kSortSegments = {{
			{.Label = "Newest", .Value = BranchSort::Newest},
			{.Label = "Oldest", .Value = BranchSort::Oldest},
			{.Label = "Name", .Value = BranchSort::Name},
		}};

		constexpr std::array<Segment<BranchGrouping>, 2> kGroupingSegments = {{
			{.Label = "None", .Value = BranchGrouping::None},
			{.Label = "Author", .Value = BranchGrouping::Author},
		}};

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

		// WARNING: A Selectable or MenuItem closes the popup it sits in when clicked; the panel's
		// segments are buttons so that choosing a sort leaves it open.
		bool DrawSegment(const char* label, bool selected, float width)
		{
			const float defaultHeight = 0.0f;
			const ImVec2 size(width, defaultHeight);

			if (!selected)
			{
				return ImGui::Button(label, size);
			}

			const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);

			ImGui::PushStyleColor(ImGuiCol_Button, accent);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent);
			ImGui::PushStyleColor(ImGuiCol_Text, kSelectedSegmentText);
			const bool clicked = ImGui::Button(label, size);
			ImGui::PopStyleColor(kSelectedSegmentColourCount);

			return clicked;
		}

		template <typename Choice, std::size_t SegmentCount>
		void DrawSegments(const char* id, const std::array<Segment<Choice>, SegmentCount>& segments, Choice& chosen)
		{
			const float noOffset = 0.0f;
			const float noSpacing = 0.0f;
			const float rowWidth = ImGui::GetContentRegionAvail().x;
			const float segmentWidth = std::floor(rowWidth / static_cast<float>(SegmentCount));

			ImGui::PushID(id);

			for (std::size_t segmentIndex = 0; segmentIndex < SegmentCount; ++segmentIndex)
			{
				if (segmentIndex > 0)
				{
					ImGui::SameLine(noOffset, noSpacing);
				}

				const bool isLast = segmentIndex + 1 == SegmentCount;
				const float width =
					isLast ? rowWidth - segmentWidth * static_cast<float>(SegmentCount - 1) : segmentWidth;
				const Segment<Choice>& segment = segments[segmentIndex];

				if (DrawSegment(segment.Label, segment.Value == chosen, width))
				{
					chosen = segment.Value;
				}
			}

			ImGui::PopID();
		}

		bool DrawToggleRow(const char* label, const char* toggleId, bool* isOn)
		{
			const float toggleX =
				ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - Core::Scaled(Core::kToggleWidth);

			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);

			// WARNING: SameLine with an offset adds the group's offset, so inside a group the toggle
			// would land a window padding too far right; SetCursorPosX does not.
			ImGui::SameLine();
			ImGui::SetCursorPosX(toggleX);

			return Core::Toggle(toggleId, isOn);
		}

		void DrawAuthorsFailure(const std::string& message)
		{
			const std::string failure = AuthorsFailureText(message);

			bool cut = false;
			const std::string fitted = FitToWidth(failure, ImGui::GetContentRegionAvail().x, &cut);
			ImGui::TextDisabled("%s", fitted.c_str());

			if (cut)
			{
				ImGui::SetItemTooltip("%s", failure.c_str());
			}
		}

		bool DrawAuthorChoice(const AuthorChoice& choice)
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const float rowStartX = ImGui::GetCursorPosX();
			const float rowWidth = ImGui::GetContentRegionAvail().x;

			const std::string count = std::to_string(choice.Branches);
			const float countWidth = ImGui::CalcTextSize(count.c_str()).x;
			const float boxRoom = Core::Scaled(kTickBoxSize) + style.ItemInnerSpacing.x;
			const float nameRoom = rowWidth - boxRoom - style.ItemSpacing.x - countWidth;

			const std::string label = FitToWidth(AuthorChoiceText(choice), nameRoom) + kTickBoxId;
			bool ticked = choice.Ticked;

			ImGui::PushID(choice.Key.c_str());

			const bool clicked = DrawTickBox(label.c_str(), &ticked);
			ImGui::SameLine(rowStartX + rowWidth - countWidth);
			ImGui::TextDisabled("%s", count.c_str());

			ImGui::PopID();

			return clicked;
		}
	}

	BranchListView::BranchListView(BranchListState& options) : m_Options(options)
	{
	}

	void BranchListView::Reset()
	{
		m_TextBuffer = {};
		m_AuthorQueryBuffer = {};
		m_Filter = BranchListFilter{};
		m_Words.clear();
		m_ExpandedGroups.clear();
		m_RowsBuiltFrom.reset();
		m_Rows.clear();
		m_Choices.clear();
		m_BranchesWaitingSince.reset();
		m_AuthorsWaitingSince.reset();
		m_AuthorsPanelWaitingSince.reset();
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

	// WARNING: A new branch list lands before the authors asked for it. Built against the older
	// answer, a moved tip falls back to its new commit's author, leaving its groups and no longer
	// matching a typed author's name until the answer lands, so while the view needs authors or
	// the filter has words the rows from before stay until then. Only a new branch list is held
	// back; a change the user made rebuilds at once.
	bool BranchListView::HoldsRowsFor(const RowsBuiltFrom& inputs, bool authorsInFlight) const
	{
		if (!authorsInFlight || !m_RowsBuiltFrom.has_value() || !m_RowsBuiltFrom->Authors)
		{
			return false;
		}

		const bool wordsMayMatchAuthors = !LoweredWordsOf(inputs.Filter.Text).empty();

		if (!ViewNeedsAuthors(RowsInputOf(inputs)) && !wordsMayMatchAuthors)
		{
			return false;
		}

		RowsBuiltFrom inputsOverHeldAnswer = inputs;
		inputsOverHeldAnswer.Branches = m_RowsBuiltFrom->Branches;
		inputsOverHeldAnswer.Authors = m_RowsBuiltFrom->Authors;
		inputsOverHeldAnswer.AuthorsFailure = m_RowsBuiltFrom->AuthorsFailure;

		return inputsOverHeldAnswer == *m_RowsBuiltFrom;
	}

	void BranchListView::RebuildRowsWhenOutOfDate(const BranchListFrame& frame)
	{
		RowsBuiltFrom inputs = InputsOf(frame);

		if (m_RowsBuiltFrom == inputs || HoldsRowsFor(inputs, frame.AuthorsInFlight))
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
			m_AuthorsPanelWaitingSince.reset();
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
			ImGui::OpenPopup(kPanelId);
		}

		DrawPanel(frame);
		DrawChips(frame);
	}

	void BranchListView::DrawPanel(const BranchListFrame& frame)
	{
		const float fitHeight = 0.0f;
		ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * kPanelWidthInFontSizes, fitHeight));

		if (!ImGui::BeginPopup(kPanelId))
		{
			return;
		}

		ImGui::TextDisabled("Sort");
		DrawSegments("##sort", kSortSegments, m_Options.Sort);

		ImGui::TextDisabled("Group by");
		DrawSegments("##group", kGroupingSegments, m_Options.Group);

		ImGui::Separator();
		DrawOnlyMineRow(frame);
		DrawToggleRow("Also commit message", "##alsoMessage", &m_Filter.AlsoMessage);

		ImGui::Separator();
		DrawAuthorsSection(frame);

		ImGui::EndPopup();
	}

	void BranchListView::DrawOnlyMineRow(const BranchListFrame& frame)
	{
		const bool emailKnown = frame.Authors && frame.Authors->UserEmail.has_value();
		const std::string userKey = emailKnown ? AuthorKeyOf(*frame.Authors->UserEmail) : std::string{};
		bool onlyMine = emailKnown && m_Filter.Authors.contains(userKey);

		ImGui::BeginDisabled(!emailKnown);
		ImGui::BeginGroup();
		const bool toggled = DrawToggleRow("Only mine", "##onlyMine", &onlyMine);
		ImGui::EndGroup();
		ImGui::EndDisabled();

		if (!emailKnown && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("%s", OnlyMineTooltipOf(frame).c_str());
		}

		if (toggled)
		{
			ToggleAuthor(userKey);
		}
	}

	void BranchListView::DrawAuthorsSection(const BranchListFrame& frame)
	{
		DrawAuthorsHeading();

		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##authorFilter", ICON_MDI_MAGNIFY "  Filter authors", m_AuthorQueryBuffer.data(),
								 m_AuthorQueryBuffer.size());

		if (!frame.Authors && frame.AuthorsFailure.has_value())
		{
			DrawAuthorsFailure(*frame.AuthorsFailure);
			return;
		}

		if (!frame.Authors)
		{
			if (LoaderIsDue(m_AuthorsPanelWaitingSince))
			{
				Core::LoadingLine(kReadingAuthors);
			}
			else
			{
				ImGui::TextDisabled("%s", kReadingAuthors);
			}

			return;
		}

		if (m_Choices.empty())
		{
			ImGui::TextDisabled("%s", kNoAuthors);
			return;
		}

		const std::vector<AuthorChoice> shown = FilterAuthorChoices(m_Choices, m_AuthorQueryBuffer.data());

		if (shown.empty())
		{
			ImGui::TextDisabled("%s", kNoAuthorsMatch);
			return;
		}

		DrawAuthorList(shown);
	}

	void BranchListView::DrawAuthorsHeading()
	{
		const float rowStartX = ImGui::GetCursorPosX();
		const float rowWidth = ImGui::GetContentRegionAvail().x;

		ImGui::TextDisabled("Authors");

		if (m_Filter.Authors.empty())
		{
			return;
		}

		const std::string clearLabel = std::format("Clear ({})###clearAuthors", m_Filter.Authors.size());
		const char* const wholeLabel = nullptr;
		const float clearWidth = ImGui::CalcTextSize(clearLabel.c_str(), wholeLabel, kHideTextAfterDoubleHash).x;

		ImGui::SameLine(rowStartX + rowWidth - clearWidth);

		ImGui::PushStyleColor(ImGuiCol_TextLink, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
		const bool cleared = ImGui::TextLink(clearLabel.c_str());
		ImGui::PopStyleColor();

		if (cleared)
		{
			m_Filter.Authors.clear();
		}
	}

	void BranchListView::DrawAuthorList(const std::vector<AuthorChoice>& shown)
	{
		const float rowHeight = ImGui::GetFrameHeightWithSpacing();
		const std::size_t visibleRows = std::min(shown.size(), kMostAuthorRowsShown);
		const float listHeight = static_cast<float>(visibleRows) * rowHeight - ImGui::GetStyle().ItemSpacing.y;
		const float fullWidth = 0.0f;

		std::optional<std::string> toggledKey;

		// WARNING: A child is filled with the window colour, darker than the panel around it, so the
		// list draws no background of its own and reads as part of the panel.
		const float noBackground = 0.0f;
		ImGui::SetNextWindowBgAlpha(noBackground);
		const bool listVisible = ImGui::BeginChild(kAuthorListId, ImVec2(fullWidth, listHeight));

		if (listVisible)
		{
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(shown.size()), rowHeight);

			while (clipper.Step())
			{
				for (int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex)
				{
					const AuthorChoice& choice = shown[static_cast<std::size_t>(rowIndex)];

					if (DrawAuthorChoice(choice))
					{
						toggledKey = choice.Key;
					}
				}
			}

			clipper.End();
		}

		ImGui::EndChild();

		if (toggledKey.has_value())
		{
			ToggleAuthor(*toggledKey);
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
		// WARNING: Held rows index the branch list they were built from, not the frame's newer one.
		const Core::Ref<const std::vector<Core::Branch>> builtFrom = m_RowsBuiltFrom->Branches;
		const Core::Ref<const BranchAuthors> builtWithAuthors = m_RowsBuiltFrom->Authors;

		const Core::Branch& branch = (*builtFrom)[row.BranchIndex];
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

		// WARNING: The child these rows scroll in has no padding of its own; the label's margin is the
		// padding of the window around it. The age keeps the same margin from the scrollbar, or the
		// row looks lopsided.
		const float rightMargin = ImGui::GetStyle().WindowPadding.x;
		const float ageRight = rowStart.x + rowWidth - rightMargin;

		const float ageRoom = ImGui::CalcTextSize(kWidestAge).x;
		const float badgeWidth = badge.empty() ? 0.0f : ImGui::CalcTextSize(badge.c_str()).x;
		const float badgeRoom = badge.empty() ? 0.0f : badgeWidth + spacing;
		const float labelRoom = rowWidth - rightMargin - indent - ageRoom - spacing - badgeRoom;

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
			ImGui::SetCursorScreenPos(ImVec2(ageRight - ageRoom - spacing - badgeWidth, rowStart.y));
			ImGui::TextDisabled("%s", badge.c_str());
		}

		if (!age.empty())
		{
			const float ageWidth = ImGui::CalcTextSize(age.c_str()).x;
			ImGui::SetCursorScreenPos(ImVec2(ageRight - ageWidth, rowStart.y));
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
			const BranchAuthors& authors = builtWithAuthors ? *builtWithAuthors : noAnswer;
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
