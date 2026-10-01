#include "Sessions/ClaudeSettings.h"

#include "Sessions/ClaudeLaunch.h"
#include "Settings/AppSettings.h"

#include "Core/Platform/ShellProfiles.h"

namespace Graphite {

	void ClaudeSettings::Load()
	{
		m_Binary = m_Store.GetClaudeBinary();
		m_Pricing = Pricing::Defaults();

		for (const auto& [model, text] : m_Store.GetClaudePrices())
		{
			// A price that does not parse is ignored rather than zeroed: zero would show a
			// confident, wrong cost.
			if (std::optional<Price> price = ParsePrice(text))
				m_Pricing.Set(model, *price);
		}
	}

	void ClaudeSettings::SetBinary(std::filesystem::path binary)
	{
		m_Binary = std::move(binary);
		m_Store.SetClaudeBinary(m_Binary);
	}

	void ClaudeSettings::SetPrice(const std::string& model, const Price& price)
	{
		m_Pricing.Set(model, price);
		m_Store.SetClaudePrice(model, FormatPrice(price));
	}

	void ClaudeSettings::ResetPrice(const std::string& model)
	{
		m_Pricing.Reset(model);
		m_Store.RemoveClaudePrice(model);
	}

	std::filesystem::path ClaudeSettings::ResolveProgram() const
	{
		return ResolveClaudeProgram(m_Binary, [](const std::string& name) { return Core::FindOnPath(name); });
	}
}
