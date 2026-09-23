#include "Core/Base/Base.h"

#include "Panels/SessionWindows.h"

#include "Sessions/ClaudeSettings.h"
#include "Sessions/SessionText.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <utility>

namespace Graphite {

	namespace {
		// A tab shows the start of a title; the whole of it is in the session's list row.
		constexpr std::size_t kTabTitleBytes = 60;
	}

	SessionWindows::SessionWindows(Core::EventBus& events, const ClaudeSettings& settings)
		: m_Events(events), m_Settings(settings)
	{
	}

	SessionWindows::Entry* SessionWindows::Find(const std::string& id)
	{
		const auto found = std::find_if(m_Open.begin(), m_Open.end(),
										[&id](const Core::Scope<Entry>& entry) { return SameSessionId(entry->Id, id); });
		return found == m_Open.end() ? nullptr : found->get();
	}

	void SessionWindows::Attach(Entry& entry, const SessionSummary& summary)
	{
		entry.Title = summary.DisplayTitle();
		entry.Summary = summary;

		if (!entry.Live)
		{
			entry.Live = Core::CreateScope<LiveTranscript>(summary.File);
			entry.Live->Poll();
		}
	}

	void SessionWindows::Open(const SessionSummary& summary)
	{
		if (Entry* existing = Find(summary.Id))
		{
			// A pending "New Claude session" tab reaches here with no reader yet when the user
			// clicks it in the list before the index has delivered a summary for it: start
			// reading now rather than waiting for UpdateSummary, which may be a poll away.
			if (!existing->Live)
				Attach(*existing, summary);

			existing->Focus = true;
			return;
		}

		auto entry = Core::CreateScope<Entry>();
		entry->Id = summary.Id;
		Attach(*entry, summary);
		m_Open.push_back(std::move(entry));
	}

	void SessionWindows::OpenPending(const std::string& id, std::string title)
	{
		if (Entry* existing = Find(id))
		{
			existing->Focus = true;
			return;
		}

		auto entry = Core::CreateScope<Entry>();
		entry->Id = id;
		entry->Title = std::move(title);
		m_Open.push_back(std::move(entry));
	}

	void SessionWindows::UpdateSummary(const SessionSummary& summary)
	{
		if (Entry* entry = Find(summary.Id))
			Attach(*entry, summary);
	}

	void SessionWindows::NotifyChanged(const std::vector<std::filesystem::path>& directories)
	{
		for (const Core::Scope<Entry>& entry : m_Open)
		{
			if (!entry->Live)
				continue;

			const std::filesystem::path& file = entry->Live->GetFile();

			// The session's own project directory, or anything inside its folder — which is
			// every directory the watcher reports that could hold something of this session.
			const bool mine = std::any_of(directories.begin(), directories.end(),
										  [&file](const std::filesystem::path& directory)
										  {
											  return directory == file.parent_path() ||
													 IsWithinDirectory(directory, file.parent_path() / file.stem()) ||
													 IsWithinDirectory(file, directory);
										  });

			if (mine)
				entry->Live->Poll();
		}
	}

	void SessionWindows::Update()
	{
		for (const Core::Scope<Entry>& entry : m_Open)
		{
			if (!entry->Live)
				continue;

			entry->Live->Take(entry->Data);
			entry->Live->TakeSubagents(entry->Subagents);
		}
	}

	void SessionWindows::Render(unsigned int centreNode, const SessionPalette& palette)
	{
		for (std::size_t index = 0; index < m_Open.size();)
		{
			Entry& entry = *m_Open[index];
			const std::string name =
				ICON_MDI_ROBOT_OUTLINE "  " + SingleLineLabel(entry.Title, kTabTitleBytes) + "###session-" + entry.Id;

			// Docked only on the frame this identity is new, as DiffWindows does: every frame
			// would fight a reader who dragged the tab elsewhere.
			if (std::exchange(entry.Fresh, false) && centreNode != 0)
				ImGui::DockBuilderDockWindow(name.c_str(), centreNode);

			if (std::exchange(entry.Focus, false))
				ImGui::SetNextWindowFocus();

			bool open = true;

			if (ImGui::Begin(name.c_str(), &open))
			{
				if (!entry.Live)
				{
					ImGui::TextDisabled("Waiting for Claude to write the session's first lines...");
				}
				else if (ImGui::BeginTabBar("##sessionTabs"))
				{
					if (ImGui::BeginTabItem("Conversation"))
					{
						if (const std::size_t skipped = entry.Live->GetSkipped(); skipped > 0)
							ImGui::TextDisabled("%zu lines could not be read and were skipped.", skipped);

						entry.Conversation.Draw(
							entry.Data, ConversationContext{m_Settings.GetPricing(), entry.Subagents, palette}, m_Events);
						ImGui::EndTabItem();
					}

					if (ImGui::BeginTabItem("Cost"))
					{
						DrawCost(entry.Data, entry.Summary, m_Settings.GetPricing());
						ImGui::EndTabItem();
					}

					ImGui::EndTabBar();
				}
			}

			// Begin() returning false still needs its End().
			ImGui::End();

			if (open)
				++index;
			else
				m_Open.erase(m_Open.begin() + static_cast<std::ptrdiff_t>(index));
		}
	}
}
