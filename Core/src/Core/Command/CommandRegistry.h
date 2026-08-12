#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Command/Command.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace core
{
    /// Every command the application knows, addressed by id.
    class CommandRegistry final : NonCopyable
    {
    public:
        /// Returns nullptr if a command with that id already exists — the same contract
        /// PanelManager::Add has. The returned pointer stays valid for the registry's
        /// lifetime, which is why the commands are held indirectly.
        Command* Add(Command command);

        [[nodiscard]] const Command* Find(std::string_view id) const;

        /// Runs the command if it exists and its `enabled` predicate allows it, and
        /// reports whether it ran. The gate lives here rather than at each call site so
        /// a menu item and a key binding cannot disagree about whether an action is
        /// available.
        bool Run(std::string_view id);

        /// Members of `group`, in registration order.
        [[nodiscard]] std::vector<const Command*> InGroup(std::string_view group) const;

        void ForEach(const std::function<void(const Command&)>& action) const;

    private:
        /// unique_ptr rather than Command by value: Add hands out a pointer the caller
        /// keeps, and a reallocating vector would leave it dangling on the next Add.
        std::vector<std::unique_ptr<Command>> m_commands;
        std::unordered_map<std::string, std::size_t> m_byId;
    };
}
