#include "Git/Lines/LinesOnScreen.h"

#include "Core/Git/Ref.h"

#include <utility>
#include <vector>

namespace Graphite {

	namespace {

		std::optional<std::size_t> CheckedOutRefOf(const LineRead& read)
		{
			if (!read.Branches)
			{
				return std::nullopt;
			}

			const std::vector<Core::Branch>& branches = *read.Branches;

			for (std::size_t ref = 0; ref < branches.size(); ++ref)
			{
				if (branches[ref].IsHead)
				{
					return ref;
				}
			}

			return std::nullopt;
		}
	}

	void LinesOnScreen::Follow(Core::Ref<const LineRead> read)
	{
		if (read == m_Read)
		{
			return;
		}

		Reset();

		if (!read)
		{
			return;
		}

		m_Read = std::move(read);
		m_Lines.emplace(*m_Read);
		m_HeadRef = CheckedOutRefOf(*m_Read);
	}

	void LinesOnScreen::Reset()
	{
		m_Lines.reset();
		m_HeadRef.reset();
		m_Read.reset();
	}

	std::string_view LinesOnScreen::CheckedOutName() const
	{
		return m_Read && m_HeadRef.has_value() ? std::string_view{m_Read->Refs[*m_HeadRef].FullName} : std::string_view{};
	}
}
