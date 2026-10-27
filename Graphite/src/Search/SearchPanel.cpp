#include "Search/SearchPanel.h"

#include "Core/Base/Strings.h"

#include "Editor/Navigation/SelectInDocument.h"

#include "Search/SearchPreview.h"
#include "Search/FileEnumerator.h"
#include "Search/HitList.h"
#include "Search/SearchSummary.h"
#include "UI/SplitResults.h"
#include "UI/TickBox.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/TextButton.h"
#include "Core/Text/Caret.h"
#include "Core/UI/PanelMarginScope.h"

#include <IconsLucide.h>

#include <imgui.h>

#include <functional>
#include <string>

namespace Graphite {

	namespace {

		constexpr bool OpenAsPermanentTab = true;
		constexpr const char* kReadingTheSolution = "Reading the solution";
		constexpr const char* kProjectsUnreadable = "The solution could not be read, so Projects only has no files to search.";
		constexpr float FillAvailableWidth = -1.0f;
		constexpr float MaskFieldWidthInFontSizes = 10.0f;
	}

	SearchPanel::SearchPanel(SearchService& service, const SearchSettings& settings,
							 Workspace& workspace, Core::DocumentSet& documents,
							 const ActiveScheme& scheme, const EditorSettings& editor,
							 PreviewSemantics& semantics)
		// WARNING: The id stays "search" while the title changes: imgui.ini keys the dock layout on it.
		: Core::Panel("search", "Find in Files", "navigate", ICON_LC_TEXT_SEARCH, StartsAs::Hidden, Sides::Flush),
		  m_Service(service), m_Settings(settings),
		  m_Workspace(workspace), m_Documents(documents), m_Scheme(scheme), m_Editor(editor),
		  m_Preview(&semantics)
	{
	}

	void SearchPanel::OnUpdate(Core::Timestep)
	{
		ResolveHeldSearch();
		ForgetUnreadableSolutionOnceOutdated();

		m_Drained.clear();
		m_Service.Drain(m_Drained, m_Progress);

		for (SearchHit& hit : m_Drained)
		{
			m_Groups.Add(std::move(hit));
		}
	}

	void SearchPanel::OnImGuiRender()
	{
		{
			const Core::PanelMarginScope margin;
			RenderQuery();
			RenderSummary();
		}

		const std::function<void()> drawResults = [this] { RenderResults(); };
		const std::function<void()> drawPreview = [this] { RenderPreview(); };

		DrawSplitResults(SplitResultsInput{.HasSelection = m_Selected.has_value(),
										   .ResultsHeight = m_ResultsHeight,
										   .DrawResults = drawResults,
										   .DrawPreview = drawPreview,
										   .ResultsChildId = "##searchResults",
										   .PreviewChildId = "##searchPreview",
										   .SplitterId = "##searchSplit"});
	}

	SearchStart SearchPanel::BuildRequest() const
	{
		SearchStart request;

		request.Query.Needle = m_QueryBuffer.data();
		request.Query.Mask = m_MaskBuffer.data();
		request.Query.MatchCase = m_MatchCase;
		request.Query.WholeWord = m_WholeWord;

		request.Options = m_Settings.Get();
		request.Root = m_Workspace.GetRoot();

		for (std::size_t index = 0; index < m_Documents.GetCount(); ++index)
		{
			const Core::OpenItem& item = m_Documents.At(index);

			if (item.Document)
			{
				request.Unsaved.push_back(
					UnsavedFile{item.Path, item.Document->GetBuffer().Materialize()});
			}
		}

		request.Options.ProjectsOnly = false;

		return request;
	}

	void SearchPanel::AddProjectFiles(SearchStart& request) const
	{
		request.Options.ProjectsOnly = true;

		EnumerateProjectFiles(*m_Workspace.GetLoadedSolution(),
							  [&request](const std::filesystem::path& file) { request.ProjectFiles.push_back(file); });
	}

	std::optional<std::string> SearchPanel::GetStatusMessage() const
	{
		if (!m_Progress.Running)
		{
			return std::nullopt;
		}

		return "Searching... " + Core::Counted(m_Progress.FilesScanned, "file");
	}

	void SearchPanel::Focus()
	{
		SetVisible(true);

		const std::string window = GetWindowName();

		ImGui::SetWindowFocus(window.c_str());

		m_FocusQueryNextFrame = true;
	}

	void SearchPanel::RenderQuery()
	{
		if (m_FocusQueryNextFrame)
		{
			ImGui::SetKeyboardFocusHere();
			m_FocusQueryNextFrame = false;
		}

		ImGui::SetNextItemWidth(FillAvailableWidth);

		const bool enterPressed = ImGui::InputTextWithHint(
			"##searchQuery", "Find in files", m_QueryBuffer.data(), m_QueryBuffer.size(),
			ImGuiInputTextFlags_EnterReturnsTrue);

		ImGui::SetNextItemWidth(ImGui::GetFontSize() * MaskFieldWidthInFontSizes);
		ImGui::InputTextWithHint("##searchMask", "*.cpp;*.h", m_MaskBuffer.data(),
								 m_MaskBuffer.size());

		ImGui::SameLine();
		DrawTickBox("Match case", &m_MatchCase);
		ImGui::SameLine();
		DrawTickBox("Whole word", &m_WholeWord);
		ImGui::SameLine();

		if (Core::TextButton("Search") || enterPressed)
		{
			RunSearch();
		}
	}

	SearchScope SearchPanel::CurrentScope() const
	{
		return ScopeOfSearch(SearchScopeInputs{.ProjectsOnly = m_Settings.Get().ProjectsOnly,
											   .SolutionSelected = !m_Workspace.GetSelectedSolution().empty(),
											   .SolutionLoaded = m_Workspace.GetLoadedSolution().has_value(),
											   .SolutionBeingRead = m_Workspace.IsReadingSolution()});
	}

	void SearchPanel::RunSearch()
	{
		m_Groups.Clear();
		m_Groups.SetRoot(m_Workspace.GetRoot());

		m_Selected.reset();
		m_Preview.Clear();

		m_Drained.clear();
		m_Progress = SearchProgress{};
		m_SearchedNeedle.reset();
		m_HeldSearch.reset();
		m_ProjectsUnreadableInFolder.reset();

		SearchStart request = BuildRequest();

		switch (CurrentScope())
		{
		case SearchScope::WholeFolder:
			StartSearch(std::move(request));
			return;
		case SearchScope::Projects:
			AddProjectFiles(request);
			StartSearch(std::move(request));
			return;
		case SearchScope::WaitForSolution:
			m_HeldSearch = HeldSearch{.Request = std::move(request), .FolderGeneration = m_Workspace.GetFolderGeneration()};
			return;
		case SearchScope::SolutionUnreadable:
			m_ProjectsUnreadableInFolder = m_Workspace.GetFolderGeneration();
			return;
		}
	}

	void SearchPanel::StartSearch(SearchStart request)
	{
		m_SearchedNeedle = request.Query.Needle;
		m_Progress.Running = !request.Query.Needle.empty();
		m_Service.Start(std::move(request));
	}

	void SearchPanel::ResolveHeldSearch()
	{
		if (!m_HeldSearch)
		{
			return;
		}

		const bool folderChanged = m_HeldSearch->FolderGeneration != m_Workspace.GetFolderGeneration();

		if (folderChanged)
		{
			m_HeldSearch.reset();
			return;
		}

		if (m_Workspace.IsReadingSolution())
		{
			return;
		}

		SearchStart request = std::move(m_HeldSearch->Request);
		m_HeldSearch.reset();

		if (!m_Workspace.GetLoadedSolution())
		{
			m_ProjectsUnreadableInFolder = m_Workspace.GetFolderGeneration();
			return;
		}

		AddProjectFiles(request);
		StartSearch(std::move(request));
	}

	void SearchPanel::ForgetUnreadableSolutionOnceOutdated()
	{
		if (!m_ProjectsUnreadableInFolder)
		{
			return;
		}

		const bool projectsOnlyUnticked = !m_Settings.Get().ProjectsOnly;
		const bool solutionReadSince = m_Workspace.GetLoadedSolution().has_value();
		const bool anotherFolder = *m_ProjectsUnreadableInFolder != m_Workspace.GetFolderGeneration();

		if (projectsOnlyUnticked || solutionReadSince || anotherFolder)
		{
			m_ProjectsUnreadableInFolder.reset();
		}
	}

	void SearchPanel::RenderSummary()
	{
		const std::optional<std::string> summary =
			SearchSummaryText(m_Groups.GetCount(), m_Progress, m_SearchedNeedle, m_QueryBuffer.data());

		if (!summary.has_value())
		{
			ImGui::TextDisabled("%s", std::string{kSearchHint}.c_str());
			return;
		}

		ImGui::TextUnformatted(summary->c_str());

		if (m_Progress.Running)
		{
			ImGui::SameLine();
			Core::LoadingLine("Searching");
		}
	}

	void SearchPanel::RenderResults()
	{
		if (m_HeldSearch)
		{
			Core::LoadingState(kReadingTheSolution);
			return;
		}

		if (m_ProjectsUnreadableInFolder)
		{
			Core::EmptyState(kProjectsUnreadable);
			return;
		}

		const std::function<void(const SearchHit&)> onOpen = [this](const SearchHit& hit) { OpenAndSelect(hit); };

		DrawHitList(HitListInput{
			.Groups = m_Groups.Get(), .Selected = m_Selected, .OnOpen = onOpen});
	}

	void SearchPanel::RenderPreview()
	{
		const HitGroup* group = nullptr;

		for (const HitGroup& candidate : m_Groups.Get())
		{
			if (candidate.Id == m_Selected->Group)
			{
				group = &candidate;
				break;
			}
		}

		if (group == nullptr || m_Selected->Hit >= group->Hits.size())
		{
			m_Selected.reset();
			m_Preview.Clear();
			return;
		}

		const SearchHit& hit = group->Hits[m_Selected->Hit];

		// WARNING: SetTabWidth must run before Show, because Show declares the file to the server
		// and the first declaration has to carry the user's tab width.
		m_Preview.SetTabWidth(m_Editor.GetIndentStyle().Width);

		m_Preview.Show(hit.File, m_Documents);

		const PreviewRequest request = DrawSearchPreview(m_Preview,
														 SearchPreviewInput{.Hits = group->Hits,
																			.Current = m_Selected->Hit,
																			.Scheme = m_Scheme.Get(),
																			.TabWidth = m_Editor.GetIndentStyle().Width,
																			.FontSize = m_Editor.GetFontSize()});

		if (request == PreviewRequest::OpenFile)
		{
			OpenAndSelect(hit);
		}
	}

	void SearchPanel::OpenAndSelect(const SearchHit& hit)
	{
		SelectInDocument(m_Documents, hit.File, hit.Line, hit.From, hit.To,
						 [this](const std::filesystem::path& file) { m_Workspace.RequestOpenFile(file, OpenAsPermanentTab); });
	}
}
