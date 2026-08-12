#pragma once

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace core
{
    /// What produces a chord. The wheel is here rather than in a separate type because
    /// Ctrl+wheel is already one of the ways the editor zooms, and a binding that could
    /// not hold it would leave that gesture outside every rebinding the user can make.
    enum class Trigger : std::uint8_t
    {
        Key,
        WheelUp,
        WheelDown
    };

    /// Where a chord counts. Global chords are routed once per frame after every window
    /// has closed; a scoped chord is fired by whoever owns the scope, from inside the
    /// window that defines it. Ctrl+wheel counts only over a hovered editor — zooming
    /// the code from over the file tree would be a change of behaviour, not a refactor.
    enum class Scope : std::uint8_t
    {
        Global,
        HoveredEditor
    };

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

        [[nodiscard]] Trigger GetTrigger() const noexcept { return m_trigger; }
        [[nodiscard]] Scope GetScope() const noexcept { return m_scope; }

        /// Modifiers ORed with the key for Trigger::Key; modifiers alone otherwise.
        [[nodiscard]] ImGuiKeyChord Keys() const noexcept { return m_keys; }

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
        ImGuiKeyChord m_keys = 0;
        Trigger m_trigger = Trigger::Key;
        Scope m_scope = Scope::Global;
    };
}
