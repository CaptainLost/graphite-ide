#include "Settings/Panel/SettingsPanel.h"

#include "Settings/Panel/SettingsLayout.h"
#include "Theme/ActiveScheme.h"
#include "Theme/Palettes.h"

#include "Core/Base/Ascii.h"
#include "Core/ImGui/UiScale.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <array>
#include <utility>

namespace Graphite {

	namespace {

		constexpr float kSidebarPadding = 10.0f;

		const std::array<std::string, 1> kOpenLabels{"Open"};

		// The padding is read by Begin, so it is popped before anything inside the child is drawn: the
		// combos, tooltips and dialogs drawn there copy the window padding.
		bool BeginPaddedChild(const char* id, ImVec2 size, float padding)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Core::Scaled(ImVec2(padding, padding)));
			const bool visible = ImGui::BeginChild(id, size, ImGuiChildFlags_AlwaysUseWindowPadding);
			ImGui::PopStyleVar();

			return visible;
		}

		std::string JoinPath(const std::vector<std::string>& path)
		{
			std::string joined;

			for (const std::string& id : path)
			{
				if (!joined.empty())
				{
					joined += '/';
				}

				joined += id;
			}

			return joined;
		}
	}

	SettingsPanel::SettingsPanel(SettingsDirectory directory, const ActiveScheme& scheme)
		: Core::Panel("settings", "Settings", "window", ICON_MDI_COG_OUTLINE, StartsAs::Hidden), m_Directory(std::move(directory)), m_Scheme(scheme)
	{
		if (const SettingsPage* first = m_Directory.First(); first != nullptr)
		{
			if (std::optional<std::vector<std::string>> path = ResolvePath({std::string{first->GetId()}}); path.has_value())
			{
				m_OpenPath = std::move(*path);
			}
		}
	}

	std::optional<std::vector<std::string>> SettingsPanel::ResolvePath(const std::vector<std::string>& path) const
	{
		if (path.empty())
		{
			return std::nullopt;
		}

		SettingsPage* top = m_Directory.Find(path.front());

		if (top == nullptr)
		{
			return std::nullopt;
		}

		const SettingsDirectory* children = top->GetChildren();

		if (children == nullptr)
		{
			if (path.size() == 1)
			{
				return path;
			}

			return std::nullopt;
		}

		if (path.size() == 1)
		{
			return std::vector<std::string>{path.front(), ChildToShow(*top, *children)};
		}

		if (path.size() == 2 && children->Find(path.back()) != nullptr)
		{
			return path;
		}

		return std::nullopt;
	}

	std::string SettingsPanel::ChildToShow(const SettingsPage& parent, const SettingsDirectory& children) const
	{
		for (const auto& [parentId, childId] : m_LastChildOf)
		{
			if (parentId == parent.GetId() && children.Find(childId) != nullptr)
			{
				return childId;
			}
		}

		return std::string{children.First()->GetId()};
	}

	SettingsPage* SettingsPanel::GetOpenPage() const
	{
		if (m_OpenPath.empty())
		{
			return nullptr;
		}

		SettingsPage* top = m_Directory.Find(m_OpenPath.front());

		if (top == nullptr || m_OpenPath.size() == 1)
		{
			return top;
		}

		const SettingsDirectory* children = top->GetChildren();

		return children != nullptr ? children->Find(m_OpenPath.back()) : nullptr;
	}

	bool SettingsPanel::Open(const std::vector<std::string>& path)
	{
		std::optional<std::vector<std::string>> resolved = ResolvePath(path);

		if (!resolved.has_value())
		{
			return false;
		}

		m_Query.clear();
		ShowPage(std::move(*resolved));

		return true;
	}

	void SettingsPanel::ShowPage(std::vector<std::string> path)
	{
		SettingsPage* leaving = GetOpenPage();
		m_OpenPath = std::move(path);

		if (m_OpenPath.size() == 2)
		{
			m_LastChildOf[m_OpenPath.front()] = m_OpenPath.back();
		}

		if (leaving != nullptr && leaving != GetOpenPage())
		{
			leaving->OnDeactivated();
		}
	}

	void SettingsPanel::SetQuery(std::string query)
	{
		const bool wasSearching = IsSearching();
		m_Query = std::move(query);

		if (wasSearching || !IsSearching())
		{
			return;
		}

		if (SettingsPage* open = GetOpenPage(); open != nullptr)
		{
			open->OnDeactivated();
		}
	}

	bool SettingsPanel::IsSearching() const
	{
		return IsSearchQuery(m_Query);
	}

	void SettingsPanel::OpenSearchHit(const std::vector<std::string>& path, std::string_view matchesInside)
	{
		const std::string query = Core::Trimmed(m_Query);

		if (!Open(path) || matchesInside.empty())
		{
			return;
		}

		if (SettingsPage* open = GetOpenPage(); open != nullptr)
		{
			open->OnOpenedFromSearch(query);
		}
	}

	void SettingsPanel::OnUpdate(Core::Timestep)
	{
		const bool drawn = HasBeenDrawnOnLatestFrame();

		// Every page, not only the open one: a search shows rows of pages that are not open, and one of
		// them may have left a question waiting that only its own OnDeactivated can withdraw.
		if (m_WasDrawnLastFrame && !drawn)
		{
			for (SettingsPage* page : CollectPages(m_Directory))
			{
				page->OnDeactivated();
			}
		}

		m_WasDrawnLastFrame = drawn;
	}

	void SettingsPanel::OnEvent(Core::Event& event)
	{
		if (IsSearching())
		{
			return;
		}

		if (SettingsPage* open = GetOpenPage(); open != nullptr)
		{
			open->OnEvent(event);
		}
	}

	void SettingsPanel::OnImGuiRender()
	{
		if (m_Directory.IsEmpty())
		{
			return;
		}

		const std::vector<SettingsPage*> pages = CollectPages(m_Directory);

		for (SettingsPage* page : pages)
		{
			page->Update();
		}

		DrawSidebar();
		ImGui::SameLine(0.0f, 0.0f);
		DrawContent();
		RenderDialogs(pages);
		RunPendingOpen();
	}

	void SettingsPanel::DrawSidebar()
	{
		const ImVec2 size(Core::Scaled(SettingsLayout::kSidebarWidth), 0.0f);

		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
		const bool visible = BeginPaddedChild("##sidebar", size, kSidebarPadding);
		ImGui::PopStyleColor();

		if (visible)
		{
			std::string query = m_Query;

			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
			const bool edited = SettingsLayout::DrawSearchField(query);
			ImGui::PopStyleColor();

			if (edited)
			{
				SetQuery(std::move(query));
			}

			m_Hits = IsSearching() ? FindSettings(m_Directory, m_Query) : std::vector<SettingsPageHits>{};

			const std::string_view openTopId = m_OpenPath.empty() ? std::string_view{} : std::string_view{m_OpenPath.front()};

			if (std::optional<std::string> clicked = DrawNavigation(m_Directory, openTopId, m_Hits); clicked.has_value())
			{
				m_PendingOpen = PendingOpen{.Path = {*clicked}};
			}
		}

		ImGui::EndChild();
	}

	std::optional<std::string> SettingsPanel::DrawNavigation(const SettingsDirectory& directory, std::string_view selectedId,
															 const std::vector<SettingsPageHits>& hits) const
	{
		std::optional<std::string> clicked;
		bool firstGroup = true;

		for (const SettingsGroup& group : directory.GetGroups())
		{
			if (!group.Title.empty())
			{
				SettingsLayout::DrawGroupTitle(group.Title);
			}
			else if (!firstGroup)
			{
				SettingsLayout::DrawGroupDivider();
			}

			firstGroup = false;

			for (const Core::Scope<SettingsPage>& page : group.Pages)
			{
				const bool selected = !IsSearching() && page->GetId() == selectedId;
				const bool dimmed = IsSearching() && !HasHitsUnder(hits, page->GetId());

				if (SettingsLayout::DrawNavigationEntry(page->GetIcon(), page->GetTitle(), selected, dimmed))
				{
					clicked = std::string{page->GetId()};
				}
			}
		}

		return clicked;
	}

	void SettingsPanel::DrawContent()
	{
		if (IsSearching())
		{
			DrawResults();
		}
		else
		{
			DrawOpenPage();
		}
	}

	void SettingsPanel::DrawOpenPage()
	{
		SettingsPage* top = m_Directory.Find(m_OpenPath.front());
		const SettingsDirectory* children = top != nullptr ? top->GetChildren() : nullptr;

		if (children != nullptr)
		{
			DrawInnerList(*children);
			ImGui::SameLine(0.0f, 0.0f);
		}

		SettingsPage* page = GetOpenPage();

		// One child per page, so each page keeps its own scroll position.
		const std::string childId = "##page/" + JoinPath(m_OpenPath);

		if (BeginPaddedChild(childId.c_str(), ImVec2(0.0f, 0.0f), SettingsLayout::kPageMargin) && page != nullptr)
		{
			SettingsLayout::DrawPageBody(*page, ProblemColour());
		}

		ImGui::EndChild();
	}

	void SettingsPanel::DrawInnerList(const SettingsDirectory& children)
	{
		const ImVec2 size(Core::Scaled(SettingsLayout::kInnerListWidth), 0.0f);
		const std::vector<SettingsPageHits> noHits;

		if (BeginPaddedChild("##innerList", size, kSidebarPadding))
		{
			// Drawn inside the child: the page beside it paints its background from the child's right edge.
			const ImVec2 windowTopLeft = ImGui::GetWindowPos();
			const ImVec2 windowSize = ImGui::GetWindowSize();
			const float dividerX = windowTopLeft.x + windowSize.x - 1.0f;
			ImGui::GetWindowDrawList()->AddLine(ImVec2(dividerX, windowTopLeft.y), ImVec2(dividerX, windowTopLeft.y + windowSize.y),
												ImGui::GetColorU32(ImGuiCol_Separator));

			if (std::optional<std::string> clicked = DrawNavigation(children, m_OpenPath.back(), noHits); clicked.has_value())
			{
				m_PendingOpen = PendingOpen{.Path = {m_OpenPath.front(), *clicked}};
			}
		}

		ImGui::EndChild();
	}

	void SettingsPanel::DrawResults()
	{
		if (BeginPaddedChild("##results", ImVec2(0.0f, 0.0f), SettingsLayout::kPageMargin))
		{
			if (m_Hits.empty())
			{
				ImGui::TextDisabled("No settings match.");
			}

			for (const SettingsPageHits& page : m_Hits)
			{
				const std::string pageId = JoinPath(page.Path);
				ImGui::PushID(pageId.c_str());
				SettingsLayout::DrawResultsLabel(page.Label);

				for (std::size_t cardIndex = 0; cardIndex < page.Cards.size(); ++cardIndex)
				{
					ImGui::PushID(static_cast<int>(cardIndex));
					SettingsLayout::DrawCard(ResultCard(page, page.Cards[cardIndex]), ProblemColour());
					ImGui::PopID();
				}

				ImGui::PopID();
			}
		}

		ImGui::EndChild();
	}

	SettingsCard SettingsPanel::ResultCard(const SettingsPageHits& page, const SettingsCardHits& card)
	{
		SettingsCard result{.Heading = card.Heading, .Note = card.Note};

		for (const SettingsRowHit& hit : card.Rows)
		{
			if (!hit.Row.IsBlock())
			{
				result.Rows.push_back(hit.Row);
				continue;
			}

			result.Rows.push_back(SettingsRow{
				.Key = std::string{hit.Row.GetKey()},
				.Title = hit.Row.Title,
				.Description = hit.MatchesInside,
				.ControlWidth = SettingsLayout::ButtonsWidth(kOpenLabels),
				.DrawControl = [this, path = page.Path, matchesInside = hit.MatchesInside] { DrawOpenButton(path, matchesInside); }});
		}

		return result;
	}

	void SettingsPanel::DrawOpenButton(const std::vector<std::string>& path, const std::string& matchesInside)
	{
		if (ImGui::Button(kOpenLabels.front().c_str()))
		{
			m_PendingOpen = PendingOpen{.Path = path, .MatchesInside = matchesInside};
		}
	}

	// In the panel's own window and under the page's address, the same every frame: a popup's id is
	// relative to the id stack it is opened under.
	void SettingsPanel::RenderDialogs(const std::vector<SettingsPage*>& pages)
	{
		for (SettingsPage* page : pages)
		{
			ImGui::PushID(page);
			page->RenderDialogs();
			ImGui::PopID();
		}
	}

	// After the frame is drawn: opening a page mid-draw would change the rows being walked.
	void SettingsPanel::RunPendingOpen()
	{
		if (!m_PendingOpen.has_value())
		{
			return;
		}

		const PendingOpen pending = std::move(*m_PendingOpen);
		m_PendingOpen.reset();
		OpenSearchHit(pending.Path, pending.MatchesInside);
	}

	ImU32 SettingsPanel::ProblemColour() const
	{
		return ReadEditorPalette(m_Scheme.Get()).DiagnosticError;
	}
}
