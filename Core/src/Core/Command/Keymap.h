#pragma once

#include "Core/Command/Binding.h"
#include "Core/Command/Chord.h"
#include "Core/Command/Conflict.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	class Keymap final
	{
	public:
		void SetDefault(std::string id, Binding binding);

		void Override(std::string_view id, std::optional<Binding> binding);

		const Binding* For(std::string_view id) const;

		const Binding* DefaultFor(std::string_view id) const;
		bool IsOverridden(std::string_view id) const;

		std::string_view BoundTo(const Chord& chord) const;

		std::vector<Conflict> FindConflicts() const;

		void ForEachOverride(const std::function<void(const std::string&, const Binding&)>& action) const;

		void ForEachBinding(const std::function<void(const std::string&, const Binding&)>& action) const;

	private:
		std::map<std::string, Binding, std::less<>> m_Defaults;
		std::map<std::string, Binding, std::less<>> m_Overrides;
	};
}