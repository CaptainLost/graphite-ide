#include "corepch.h"

#include "Core/Command/Keymap.h"

#include <utility>

namespace Core
{
    namespace
    {
        [[nodiscard]] bool Holds(const Binding& binding, const Chord& chord)
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
        m_defaults[std::move(id)] = std::move(binding);
    }

    void Keymap::Override(std::string_view id, std::optional<Binding> binding)
    {
        if (!binding.has_value())
        {
            if (const auto found = m_overrides.find(id); found != m_overrides.end())
            {
                m_overrides.erase(found);
            }
            return;
        }

        m_overrides[std::string(id)] = std::move(*binding);
    }

    const Binding* Keymap::For(std::string_view id) const
    {
        if (const auto override = m_overrides.find(id); override != m_overrides.end())
        {
            return &override->second;
        }

        return DefaultFor(id);
    }

    const Binding* Keymap::DefaultFor(std::string_view id) const
    {
        const auto found = m_defaults.find(id);

        if (found == m_defaults.end())
        {
            return nullptr;
        }

        return &found->second;
    }

    bool Keymap::IsOverridden(std::string_view id) const
    {
        return m_overrides.find(id) != m_overrides.end();
    }

    std::string_view Keymap::BoundTo(const Chord& chord) const
    {
        std::string_view owner;

        // Through ForEachBinding rather than over the two maps directly, so this cannot
        // start disagreeing with For about which layer wins.
        ForEachBinding(
            [&owner, &chord](const std::string& id, const Binding& binding)
            {
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
            [&seen, &conflicts](const std::string& id, const Binding& binding)
            {
                const auto note = [&seen, &conflicts, &id](const Chord& chord)
                {
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
        for (const auto& [id, binding] : m_overrides)
        {
            action(id, binding);
        }
    }

    void Keymap::ForEachBinding(
        const std::function<void(const std::string&, const Binding&)>& action) const
    {
        for (const auto& [id, binding] : m_defaults)
        {
            const auto override = m_overrides.find(id);

            action(id, override != m_overrides.end() ? override->second : binding);
        }

        // Overrides on ids with no default at all — a command that shipped without a
        // shortcut and was given one.
        for (const auto& [id, binding] : m_overrides)
        {
            if (m_defaults.find(id) == m_defaults.end())
            {
                action(id, binding);
            }
        }
    }
}
