#pragma once

#include "Settings/Panel/SettingsDirectory.h"
#include "Settings/Panel/SettingsSearch.h"

#include "Core/UI/Panel.h"

#include <imgui.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	class ActiveScheme;

	class SettingsPanel final : public Core::Panel
	{
	public:
		SettingsPanel(SettingsDirectory directory, const ActiveScheme& scheme);

		void OnImGuiRender() override;

		void OnUpdate(Core::Timestep timestep) override;

		void OnEvent(Core::Event& event) override;

		bool Open(const std::vector<std::string>& path);

		const std::vector<std::string>& GetOpenPath() const { return m_OpenPath; }

		void SetQuery(std::string query);

		const std::string& GetQuery() const { return m_Query; }

		bool IsSearching() const;

		void OpenSearchHit(const std::vector<std::string>& path, std::string_view openWith);

	private:
		struct PendingOpen
		{
			std::vector<std::string> Path;
			std::string OpenWith;
			bool FromResults = false;
		};

		std::optional<std::vector<std::string>> ResolvePath(const std::vector<std::string>& path) const;

		std::string ChildToShow(const SettingsPage& parent, const SettingsDirectory& children) const;

		SettingsPage* GetOpenPage() const;

		void ShowPage(std::vector<std::string> path);

		void DrawSidebar();

		std::optional<std::string> DrawNavigation(const SettingsDirectory& directory, std::string_view selectedId,
												  const std::vector<SettingsPageHits>& hits) const;

		void DrawContent();

		void DrawOpenPage();

		void DrawInnerList(const SettingsDirectory& children);

		void DrawResults();

		SettingsCard ResultCard(const SettingsPageHits& page, const SettingsCardHits& card);

		SettingsRow RowWithOpenButton(const SettingsPageHits& page, const SettingsRow& hit);

		SettingsRow RowWithControlAndOpenButton(const SettingsPageHits& page, const SettingsRow& hit);

		void DrawOpenButton(const std::vector<std::string>& path, const std::string& openWith);

		void RenderDialogs(const std::vector<SettingsPage*>& pages);

		void RunPendingOpen();

		ImU32 ProblemColour() const;

		SettingsDirectory m_Directory;
		const ActiveScheme& m_Scheme;

		std::vector<std::string> m_OpenPath;
		std::map<std::string, std::string, std::less<>> m_LastChildOf;
		std::string m_Query;
		std::vector<SettingsPageHits> m_Hits;
		std::optional<PendingOpen> m_PendingOpen;
		std::string m_ResultsQuery;
		bool m_WasDrawnLastFrame = false;
	};
}
