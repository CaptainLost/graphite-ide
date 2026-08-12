#pragma once

#include "Core/Command/Scope.h"
#include "Core/Command/Trigger.h"

#include <imgui.h>

#include <optional>
#include <string>
#include <string_view>

namespace Core
{
    /// One trigger with its modifiers: what a binding is made of, and the only thing in
    /// this design that is written to disk verbatim. Regular (ADR 0049) — copied,
    /// compared and stored in containers freely.
    class Chord
    {
    public:
        Chord() = default;

        /// Key chords are always global: that is what every one of them is today, and a
        /// scoped one would need the router to know which window defines the scope.
        [[nodiscard]] static Chord Key(ImGuiKeyChord keys);

        /// `mods` carries modifier bits only; the direction is the trigger.
        [[nodiscard]] static Chord Wheel(ImGuiKeyChord mods, Trigger direction, Scope scope);

        [[nodiscard]] Trigger GetTrigger() const noexcept { return m_Trigger; }
        [[nodiscard]] Scope GetScope() const noexcept { return m_Scope; }

        /// Modifiers ORed with the key for Trigger::Key; modifiers alone otherwise.
        [[nodiscard]] ImGuiKeyChord Keys() const noexcept { return m_Keys; }

        [[nodiscard]] bool IsEmpty() const noexcept;

        /// "Ctrl+Shift+S", "Ctrl+=", "Ctrl+WheelUp". Empty for an empty chord. This is
        /// both what the menu shows beside an item and what is stored, so the two cannot
        /// drift apart.
        [[nodiscard]] std::string ToString() const;

        /// Empty when the text names no key this build knows. A hand-edited settings
        /// file must not be able to crash the IDE, so this reports rather than asserts.
        [[nodiscard]] static std::optional<Chord> Parse(std::string_view text);

        [[nodiscard]] friend bool operator==(const Chord&, const Chord&) noexcept = default;

    private:
        ImGuiKeyChord m_Keys = 0;
        Trigger m_Trigger = Trigger::Key;
        Scope m_Scope = Scope::Global;
    };
}
