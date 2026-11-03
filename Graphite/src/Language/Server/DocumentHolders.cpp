#include "Language/Server/DocumentHolders.h"

#include "Language/Registry/LanguageRegistry.h"

#include <utility>

namespace Graphite {

	namespace {
		std::size_t SlotOf(DocumentHolder holder)
		{
			return static_cast<std::size_t>(holder);
		}
	}

	bool DocumentHolders::HasAnyHolder(const HeldDocument& document)
	{
		for (const bool holds : document.HeldBy)
		{
			if (holds)
			{
				return true;
			}
		}

		return false;
	}

	void DocumentHolders::Hold(DocumentHolder holder, const std::filesystem::path& path, std::string text)
	{
		const LanguageId language = LanguageOfPath(path);
		const auto [found, isNew] = m_Documents.try_emplace(path);
		HeldDocument& document = found->second;

		const bool isTab = holder == DocumentHolder::Tab;
		const bool tabHolds = document.HeldBy[SlotOf(DocumentHolder::Tab)];
		const bool alreadyHolds = document.HeldBy[SlotOf(holder)];

		document.HeldBy[SlotOf(holder)] = true;

		if (!isTab)
		{
			document.ConsultedText = text;
		}

		if (isNew)
		{
			// WARNING: EnsureStarted before the open, which is dropped while no server runs for the root.
			m_Server.EnsureStarted(path, language);
			m_Server.Open(path, language, std::move(text), isTab ? DocumentRole::Shown : DocumentRole::Consulted);
			return;
		}

		if (isTab && !alreadyHolds)
		{
			// WARNING: A document's role is fixed when it is opened, so the tab's arrival reopens it as shown.
			m_Server.Close(path, language);
			m_Server.Open(path, language, std::move(text), DocumentRole::Shown);
			return;
		}

		const bool refreshesItsOwnText = !isTab && alreadyHolds && !tabHolds;

		if (refreshesItsOwnText)
		{
			m_Server.Change(path, language, std::move(text));
		}
	}

	void DocumentHolders::Change(const std::filesystem::path& path, std::string text)
	{
		const auto found = m_Documents.find(path);

		if (found == m_Documents.end())
		{
			return;
		}

		m_Server.Change(path, LanguageOfPath(path), std::move(text));
	}

	void DocumentHolders::Release(DocumentHolder holder, const std::filesystem::path& path)
	{
		const auto found = m_Documents.find(path);

		if (found == m_Documents.end() || !found->second.HeldBy[SlotOf(holder)])
		{
			return;
		}

		HeldDocument& document = found->second;
		const LanguageId language = LanguageOfPath(path);

		document.HeldBy[SlotOf(holder)] = false;

		if (!HasAnyHolder(document))
		{
			m_Server.Close(path, language);
			m_Documents.erase(found);
			return;
		}

		if (holder == DocumentHolder::Tab)
		{
			m_Server.Close(path, language);
			m_Server.Open(path, language, document.ConsultedText, DocumentRole::Consulted);
		}
	}

	bool DocumentHolders::IsHeld(const std::filesystem::path& path) const
	{
		return m_Documents.contains(path);
	}

	bool DocumentHolders::IsHeldBy(DocumentHolder holder, const std::filesystem::path& path) const
	{
		const auto found = m_Documents.find(path);

		return found != m_Documents.end() && found->second.HeldBy[SlotOf(holder)];
	}

	bool DocumentHolders::IsHeldByAnyBut(DocumentHolder holder, const std::filesystem::path& path) const
	{
		const auto found = m_Documents.find(path);

		if (found == m_Documents.end())
		{
			return false;
		}

		for (std::size_t slot = 0; slot < kHolderCount; ++slot)
		{
			if (slot != SlotOf(holder) && found->second.HeldBy[slot])
			{
				return true;
			}
		}

		return false;
	}
}
