#pragma once

#include "Core/Command/Trigger.h"

#include <imgui.h>

#include <cstdint>
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
        /// Where a chord counts. Global chords are routed once per frame after every
        /// window has closed; a scoped chord is fired by whoever owns the scope, from
        /// inside the window that defines it. Ctrl+wheel counts only over a hovered
        /// editor — zooming the code from over the file tree would be a change of
        /// behaviour, not a refactor.
        enum class Scope : std::uint8_t
        {
            Global,
            HoveredEditor
        };

        Chord() = default;

        /// Key chords are always global: that is what every one of them is today, and a
        /// scoped one would need the router to know which window defines the scope.
        [[nodiscard]] static Chord Key(ImGuiKeyChord keys);

        /// `mods` carries modifier bits only; the direction is the trigger.
        [[nodiscard]] static Chord Wheel(ImGuiKeyChord mods, Trigger direction, Chord::Scope scope);

        [[nodiscard]] Trigger GetTrigger() const noexcept { return m_Trigger; }
        [[nodiscard]] Chord::Scope GetScope() const noexcept { return m_Scope; }

        /// Modifiers ORed with the key for Trigger::Key; modifiers alone otherwise.
        [[nodiscard]] ImGuiKeyChord GetKeys() const noexcept { return m_Keys; }

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
        Chord::Scope m_Scope = Chord::Scope::Global;
    };
}
