#include "corepch.h"

#include "Core/Command/Keymap.h"

#include <utility>

namespace Core {

	namespace {
		bool Holds(const Binding& binding, const Chord& chord)
		{
			if (binding.primary == chord)
			{
				return true;
			}

			for (const Chord& alternate : binding.alternates)
			{
				if (alternate == chord)
				{
					return true;
				}
			}

			return false;
		}
	}

	void Keymap::SetDefault(std::string id, Binding binding)
	{
		m_Defaults[std::move(id)] = std::move(binding);
	}

	void Keymap::Override(std::string_view id, std::optional<Binding> binding)
	{
		if (!binding.has_value())
		{
			if (const auto found = m_Overrides.find(id); found != m_Overrides.end())
			{
				m_Overrides.erase(found);
			}
			return;
		}

		m_Overrides[std::string(id)] = std::move(*binding);
	}

	const Binding* Keymap::For(std::string_view id) const
	{
		if (const auto override = m_Overrides.find(id); override != m_Overrides.end())
		{
			return &override->second;
		}

		return DefaultFor(id);
	}

	const Binding* Keymap::DefaultFor(std::string_view id) const
	{
		const auto found = m_Defaults.find(id);

		if (found == m_Defaults.end())
		{
			return nullptr;
		}

		return &found->second;
	}

	bool Keymap::IsOverridden(std::string_view id) const
	{
		return m_Overrides.find(id) != m_Overrides.end();
	}

	std::string_view Keymap::BoundTo(const Chord& chord) const
	{
		std::string_view owner;

		// Through ForEachBinding rather than over the two maps directly, so this cannot
		// start disagreeing with For about which layer wins.
		ForEachBinding(
			[&owner, &chord](const std::string& id, const Binding& binding) {
				if (owner.empty() && Holds(binding, chord))
				{
					owner = id;
				}
			});

		return owner;
	}

	std::vector<Conflict> Keymap::FindConflicts() const
	{
		std::vector<std::pair<Chord, std::string>> seen;
		std::vector<Conflict> conflicts;

		ForEachBinding(
			[&seen, &conflicts](const std::string& id, const Binding& binding) {
				const auto note = [&seen, &conflicts, &id](const Chord& chord) {
					// Two commands the user unbound both hold an empty primary, and they
					// are not in conflict: neither can be reached at all.
					if (chord.IsEmpty())
					{
						return;
					}

					for (const auto& [known, owner] : seen)
					{
						// Same command twice is harmless: a chord listed as primary and
						// as an alternate of one command reaches one command either way.
						if (known == chord && owner != id)
						{
							conflicts.push_back({.chord = chord, .first = owner, .second = id});
							return;
						}
					}

					seen.emplace_back(chord, id);
				};

				note(binding.primary);

				for (const Chord& alternate : binding.alternates)
				{
					note(alternate);
				}
			});

		return conflicts;
	}

	void Keymap::ForEachOverride(
		const std::function<void(const std::string&, const Binding&)>& action) const
	{
		for (const auto& [id, binding] : m_Overrides)
		{
			action(id, binding);
		}
	}

	void Keymap::ForEachBinding(
		const std::function<void(const std::string&, const Binding&)>& action) const
	{
		for (const auto& [id, binding] : m_Defaults)
		{
			const auto override = m_Overrides.find(id);

			action(id, override != m_Overrides.end() ? override->second : binding);
		}

		// Overrides on ids with no default at all — a command that shipped without a
		// shortcut and was given one.
		for (const auto& [id, binding] : m_Overrides)
		{
			if (m_Defaults.find(id) == m_Defaults.end())
			{
				action(id, binding);
			}
		}
	}
}