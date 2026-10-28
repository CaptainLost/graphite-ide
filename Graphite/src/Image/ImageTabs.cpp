#include "Image/ImageTabs.h"

#include <utility>
#include <vector>

namespace Graphite {

	ImageTabs::ImageTabs(Core::JobSubmitter submit, Core::TextureFactory& textures, PictureBudget budget)
		: m_Submit(std::move(submit)), m_Textures(textures), m_Budget(std::move(budget))
	{
	}

	ImageTab& ImageTabs::For(const std::filesystem::path& path)
	{
		auto entry = m_Tabs.find(path);

		// Looked up first, because a tab made only to be thrown away would still cost its mailbox.
		if (entry == m_Tabs.end())
		{
			entry = m_Tabs.emplace(path, Entry{ImageTab(path, m_Submit, m_Textures)}).first;
		}

		return entry->second.Tab;
	}

	const ImageTab* ImageTabs::Find(const std::filesystem::path& path) const
	{
		const auto entry = m_Tabs.find(path);

		return entry != m_Tabs.end() ? &entry->second.Tab : nullptr;
	}

	void ImageTabs::Prune(const std::set<std::filesystem::path>& open)
	{
		for (auto entry = m_Tabs.begin(); entry != m_Tabs.end();)
		{
			if (open.contains(entry->first))
			{
				++entry;
				continue;
			}

			m_Budget.Forget(entry->first);
			entry = m_Tabs.erase(entry);
		}
	}

	void ImageTabs::CheckDisk(const std::filesystem::path& path)
	{
		if (const auto entry = m_Tabs.find(path); entry != m_Tabs.end())
		{
			entry->second.Tab.CheckDisk();
		}
	}

	void ImageTabs::Update(std::chrono::steady_clock::time_point now, const std::set<std::filesystem::path>& shown)
	{
		++m_Frame;

		for (auto& [path, entry] : m_Tabs)
		{
			const bool onScreen = shown.contains(path);
			entry.Tab.Update(now, onScreen);

			if (onScreen)
			{
				entry.LastShownFrame = m_Frame;
			}

			m_Budget.Note(path, entry.Tab.GetDecodedBytes(), entry.LastShownFrame);
		}

		for (const std::filesystem::path& path : m_Budget.OverBudget(shown))
		{
			Entry& entry = m_Tabs.at(path);
			entry.Tab.Release();
			m_Budget.Note(path, entry.Tab.GetDecodedBytes(), entry.LastShownFrame);
		}
	}
}
