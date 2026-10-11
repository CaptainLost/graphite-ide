#pragma once

#include "Settings/Panel/SettingsDirectory.h"
#include "Settings/Panel/SettingsPage.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// A settings page that shows the cards it is given and counts what the panel tells it.
class FakeSettingsPage final : public Graphite::SettingsPage
{
public:
	FakeSettingsPage(std::string id, std::string title, std::vector<Graphite::SettingsCard> cards = {})
		: m_Id(std::move(id)), m_Title(std::move(title)), m_Cards(std::move(cards))
	{
	}

	std::string_view GetId() const override { return m_Id; }

	std::string_view GetTitle() const override { return m_Title; }

	const char* GetIcon() const override { return ""; }

	std::vector<Graphite::SettingsCard> Describe() override { return m_Cards; }

	Graphite::SettingsDirectory* GetChildren() override { return m_Children.IsEmpty() ? nullptr : &m_Children; }

	void Update() override { ++m_Updates; }

	void RenderDialogs() override { ++m_DialogRenders; }

	void OnEvent(Core::Event&) override { ++m_Events; }

	void OnDeactivated() override { ++m_Deactivations; }

	void OnOpenedFromSearch(std::string_view query) override { m_OpenedFromSearch.emplace_back(query); }

	void SetChildren(Graphite::SettingsDirectory children) { m_Children = std::move(children); }

	std::size_t GetUpdateCount() const { return m_Updates; }

	std::size_t GetDialogRenderCount() const { return m_DialogRenders; }

	std::size_t GetEventCount() const { return m_Events; }

	std::size_t GetDeactivationCount() const { return m_Deactivations; }

	const std::vector<std::string>& GetOpenedFromSearch() const { return m_OpenedFromSearch; }

private:
	std::string m_Id;
	std::string m_Title;
	std::vector<Graphite::SettingsCard> m_Cards;
	Graphite::SettingsDirectory m_Children;

	std::size_t m_Updates = 0;
	std::size_t m_DialogRenders = 0;
	std::size_t m_Events = 0;
	std::size_t m_Deactivations = 0;
	std::vector<std::string> m_OpenedFromSearch;
};

inline Graphite::SettingsGroup GroupOf(std::string title)
{
	return Graphite::SettingsGroup{.Title = std::move(title)};
}
