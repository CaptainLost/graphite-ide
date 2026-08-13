#include "corepch.h"

#include "Core/Command/ShortcutRouter.h"

#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace core
{
    namespace
    {
        /// Whether this chord fired this frame. ImGui::Shortcut, not IsKeyPressed plus a
        /// manual modifier check: it matches the modifier set exactly (so Ctrl+Alt+= no
        /// longer also zooms) and routes through the same key-ownership machinery
        /// InputText uses to claim the keyboard, so these do not fire while a rename
        /// field is active.
        [[nodiscard]] bool Fired(const Chord& chord)
        {
            if (chord.GetTrigger() != Trigger::Key || chord.GetScope() != Scope::Global)
            {
                return false;
            }

            // A command the user took every chord away from holds an empty primary, and
            // an empty chord names no key: offering it to ImGui::Shortcut would ask about
            // a chord of modifiers alone, which is not what "unbound" means.
            if (chord.IsEmpty())
            {
                return false;
            }

            return ImGui::Shortcut(chord.Keys(), ImGuiInputFlags_RouteGlobal);
        }
    }

    void ShortcutRouter::Dispatch(CommandRegistry& commands, const Keymap& keys)
    {
        // Collected first, run afterwards. Every chord has to be offered to
        // ImGui::Shortcut unconditionally, even once something has already matched this
        // frame: routing is registered by the call itself, so skipping the rest after a
        // hit would corrupt their routing for the next frame. Running a command inside
        // the walk would also let it register a command mid-iteration.
        std::vector<std::string> fired;

        keys.ForEachBinding(
            [&fired](const std::string& id, const Binding& binding)
            {
                bool hit = Fired(binding.primary);

                for (const Chord& alternate : binding.alternates)
                {
                    hit = Fired(alternate) || hit;
                }

                if (hit)
                {
                    fired.push_back(id);
                }
            });

        for (const std::string& id : fired)
        {
            commands.Run(id);
        }
    }

    void ShortcutRouter::DispatchWheel(CommandRegistry& commands, const Keymap& keys, Scope scope,
                                       float wheel)
    {
        if (wheel == 0.0f)
        {
            return;
        }

        const Trigger direction = wheel > 0.0f ? Trigger::WheelUp : Trigger::WheelDown;

        // A fast spin reports several notches in one frame, and the zoom this replaces
        // added the raw value — so a single notch per frame would make wheel zoom
        // visibly slower, a behaviour change smuggled into a refactor. At least one,
        // since any non-zero wheel is a gesture the user made.
        const auto notches = static_cast<int>(std::max(1.0f, std::round(std::fabs(wheel))));

        const ImGuiKeyChord mods = ImGui::GetIO().KeyMods;
        std::vector<std::string> fired;

        keys.ForEachBinding(
            [&fired, scope, direction, mods](const std::string& id, const Binding& binding)
            {
                const auto matches = [&](const Chord& chord)
                {
                    return chord.GetTrigger() == direction && chord.GetScope() == scope
                           && chord.Keys() == mods;
                };

                if (matches(binding.primary))
                {
                    fired.push_back(id);
                    return;
                }

                for (const Chord& alternate : binding.alternates)
                {
                    if (matches(alternate))
                    {
                        fired.push_back(id);
                        return;
                    }
                }
            });

        for (const std::string& id : fired)
        {
            for (int notch = 0; notch < notches; ++notch)
            {
                commands.Run(id);
            }
        }
    }
}
