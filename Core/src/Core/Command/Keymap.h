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

namespace core
{
    /// Which chords reach which commands, in two layers: a factory table and whatever
    /// the user changed on top of it. Separate layers are what make "restore the
    /// default" a deletion, and what keep the settings file to the handful of bindings
    /// somebody actually touched rather than a copy of the whole table.
    class Keymap final
    {
    public:
        void SetDefault(std::string id, Binding binding);

        /// std::nullopt clears the override, restoring the default.
        void Override(std::string_view id, std::optional<Binding> binding);

        /// The binding in force: the override if there is one, otherwise the default.
        /// Null when neither exists.
        [[nodiscard]] const Binding* For(std::string_view id) const;

        [[nodiscard]] const Binding* DefaultFor(std::string_view id) const;
        [[nodiscard]] bool IsOverridden(std::string_view id) const;

        /// The id `chord` currently reaches, or empty. Answers about bindings in force,
        /// so a chord freed by an override reads as free. This is what lets a rebinding
        /// screen name the command it is about to take a chord away from, and what the
        /// factory table's own test uses to prove it binds nothing twice.
        [[nodiscard]] std::string_view BoundTo(const Chord& chord) const;

        /// Every chord two commands both answer to, with both ids. Empty is the healthy
        /// state. The application checks this once at startup because the table it
        /// checks is assembled across several files, each of which believes it is
        /// registering a free chord.
        [[nodiscard]] std::vector<Conflict> FindConflicts() const;

        void ForEachOverride(const std::function<void(const std::string&, const Binding&)>& action) const;

        /// Every id with a binding in force, defaults and overrides alike.
        void ForEachBinding(const std::function<void(const std::string&, const Binding&)>& action) const;

    private:
        /// std::map, not unordered: iteration order is what the settings file is written
        /// in, and a stable order keeps a diff of that file readable.
        std::map<std::string, Binding, std::less<>> m_defaults;
        std::map<std::string, Binding, std::less<>> m_overrides;
    };
}
