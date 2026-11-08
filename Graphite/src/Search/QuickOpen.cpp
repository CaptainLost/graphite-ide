#include "Search/QuickOpen.h"

#include "Git/GitIgnoreRules.h"
#include "Search/FileEnumerator.h"
#include "Search/QuickOpenMatch.h"
#include "UI/Lists/ListRow.h"

#include "Core/App/Redraw.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/TextInput.h"
#include "Core/ImGui/UiScale.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <optional>
#include <utility>

namespace Graphite {

	namespace {

		constexpr float kWidth = 640.0f;
		constexpr float kTopGap = 48.0f;
		constexpr std::size_t kLargestListing = 200000;
		constexpr std::size_t kHandOverEvery = 2048;
		constexpr std::size_t kVisibleRows = 14;

		bool IsInsideGitDirectory(const std::filesystem::path& path)
		{
			for (const std::filesystem::path& part : path)
			{
				if (part == ".git")
				{
					return true;
				}
			}

			return false;
		}
	}

	QuickOpen::QuickOpen(Core::JobSubmitter submit, OpenFile open)
		: m_Submit(std::move(submit)), m_OpenFile(std::move(open))
	{
	}

	QuickOpen::~QuickOpen()
	{
		m_Token.Cancel();
	}

	void QuickOpen::Open(const std::filesystem::path& root)
	{
		m_Token.Cancel();
		m_Token = Core::CancellationToken{};

		m_Open = true;
		m_JustOpened = true;
		m_Root = root;
		m_Typed.clear();
		m_Paths.clear();
		m_ListingDone = false;
		m_Matches.clear();
		m_Selected = 0;
		m_RankedFor.clear();
		m_RankedCount = 0;

		m_Listing = Core::CreateRef<Listing>();

		m_Submit([listing = m_Listing, token = m_Token, root] {
			const GitIgnoreRules ignoreRules{root};

			EnumerationInput input;
			input.Root = root;
			input.Ignored = [&ignoreRules](const std::filesystem::path& path, bool) {
				return IsInsideGitDirectory(path) || ignoreRules.IsIgnored(path);
			};

			std::vector<std::string> found;

			EnumerateFiles(input, token, [&](const std::filesystem::path& file) {
				if (found.size() >= kLargestListing)
				{
					return;
				}

				const std::optional<std::filesystem::path> relative = Core::RelativeTo(file, root);
				found.push_back(Core::ToGenericUtf8(relative ? *relative : file));

				// Handed over in batches, so the first matches show before a large folder is read through.
				if (found.size() % kHandOverEvery == 0)
				{
					const std::lock_guard<std::mutex> lock(listing->Mutex);
					listing->Paths = found;
					Core::Redraw::Request();
				}
			});

			const std::lock_guard<std::mutex> lock(listing->Mutex);
			listing->Paths = std::move(found);
			listing->Done = true;
			Core::Redraw::Request();
		});
	}

	void QuickOpen::Close()
	{
		m_Open = false;
		m_Token.Cancel();
		m_Listing.reset();
	}

	void QuickOpen::Rank()
	{
		if (m_Listing && !m_ListingDone)
		{
			const std::lock_guard<std::mutex> lock(m_Listing->Mutex);

			if (m_Listing->Paths.size() != m_Paths.size())
			{
				m_Paths = m_Listing->Paths;
			}

			m_ListingDone = m_Listing->Done;
		}

		if (m_RankedFor == m_Typed && m_RankedCount == m_Paths.size())
		{
			return;
		}

		m_Matches = RankQuickOpenMatches(m_Typed, m_Paths, kShownMatches);
		m_RankedFor = m_Typed;
		m_RankedCount = m_Paths.size();
		m_Selected = m_Matches.empty() ? 0 : std::min(m_Selected, m_Matches.size() - 1);
	}

	void QuickOpen::Render()
	{
		constexpr const char* kPopupId = "##QuickOpen";

		if (!m_Open)
		{
			return;
		}

		if (m_JustOpened)
		{
			ImGui::OpenPopup(kPopupId);
		}

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const float width = std::min(Core::Scaled(kWidth), viewport->WorkSize.x - Core::Scaled(kTopGap));
		ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + (viewport->WorkSize.x - width) * 0.5f,
									   viewport->WorkPos.y + Core::Scaled(kTopGap)));
		ImGui::SetNextWindowSize(ImVec2(width, 0.0f));

		if (!ImGui::BeginPopup(kPopupId, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
		{
			// ImGui closed it: a click outside, or Escape.
			if (!m_JustOpened)
			{
				Close();
			}

			return;
		}

		Rank();

		if (m_JustOpened)
		{
			ImGui::SetKeyboardFocusHere();
			m_JustOpened = false;
		}

		ImGui::SetNextItemWidth(-FLT_MIN);
		const bool entered = Core::InputTextWithHint("##quickOpenQuery", "Go to file by name", m_Typed,
													 ImGuiInputTextFlags_EnterReturnsTrue);

		if (ImGui::IsItemEdited())
		{
			m_Selected = 0;
		}

		m_SelectionMoved = false;

		if (!m_Matches.empty())
		{
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
			{
				m_Selected = (m_Selected + 1) % m_Matches.size();
				m_SelectionMoved = true;
			}

			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
			{
				m_Selected = (m_Selected + m_Matches.size() - 1) % m_Matches.size();
				m_SelectionMoved = true;
			}
		}

		std::optional<std::size_t> chosen;

		if (entered && !m_Matches.empty())
		{
			chosen = m_Matches[m_Selected];
		}

		const float rowHeight = ListRowLineHeight();
		const std::size_t rows = std::min(std::max<std::size_t>(m_Matches.size(), 1), kVisibleRows);
		const float listHeight = rowHeight * static_cast<float>(rows) + ImGui::GetStyle().WindowPadding.y;

		if (ImGui::BeginChild("##quickOpenMatches", ImVec2(0.0f, listHeight)))
		{
			const ListBodyScope listBody;

			if (m_Matches.empty())
			{
				ListRowSelectable("##nothing", false, ImGuiSelectableFlags_Disabled);
				ImGui::SameLine(ImGui::GetStyle().FramePadding.x);
				ImGui::TextDisabled("%s", m_ListingDone ? "No file matches" : "Listing the folder's files...");
			}

			for (std::size_t row = 0; row < m_Matches.size(); ++row)
			{
				const std::string& path = m_Paths[m_Matches[row]];
				const std::size_t slash = path.find_last_of('/');
				const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
				const std::string folder = slash == std::string::npos ? std::string{} : path.substr(0, slash);

				ImGui::PushID(static_cast<int>(row));
				const bool selected = row == m_Selected;

				if (ListRowSelectable("##match", selected, ImGuiSelectableFlags_AllowOverlap))
				{
					chosen = m_Matches[row];
				}

				if (selected && m_SelectionMoved)
				{
					ImGui::SetScrollHereY();
				}

				ImGui::SameLine(ImGui::GetStyle().FramePadding.x);
				ImGui::TextUnformatted(name.c_str());

				if (!folder.empty())
				{
					ImGui::SameLine();
					ImGui::TextDisabled("%s", folder.c_str());
				}

				ImGui::PopID();
			}
		}

		ImGui::EndChild();

		if (chosen.has_value())
		{
			const std::filesystem::path absolute = (m_Root / Core::FromUtf8(m_Paths[*chosen])).lexically_normal();
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			Close();
			m_OpenFile(absolute);
			return;
		}

		ImGui::EndPopup();
	}
}
