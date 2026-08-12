#pragma once

#include "Core/Command/Chord.h"
#include "Core/Ui/Panel.h"

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>

namespace Core
{
    class CommandRegistry;
    class Keymap;
    struct Command;

    /// The screen for rebinding shortcuts. Docks where documents open, so it can sit
    /// beside the code rather than over it, and starts hidden — it is opened from the
    /// View menu like any other panel.
    ///
    /// `onChanged` is called after every change that sticks. It is how this stays free
    /// of any particular settings file: the panel knows when a binding moved, and the
    /// application knows where bindings are kept.
    class KeyBindingsPanel final : public Panel
    {
    public:
        KeyBindingsPanel(CommandRegistry& commands, Keymap& keys, std::function<void()> onChanged);

        void OnUIRender() override;

        /// A capture is only ever answered from a drawn cell, so a frame this screen did
        /// not draw is a frame the user cannot have answered in — and it is one click away
        /// (a document tab in the same node) from every frame after it. Ending the capture
        /// here is what makes it impossible to leave stuck.
        void OnNotDrawn() override;

        /// True from the click on a cell until the capture ends, so the application can
        /// stop routing shortcuts: the chord being recorded must not also run the command
        /// it is being taken from.
        [[nodiscard]] bool IsCapturingInput() const override { return !m_Capturing.empty(); }

    private:
        /// Reads whatever chord the user produces this frame, or nothing. Escape cancels
        /// — which is why Escape cannot be bound, and the alternative is a cell nobody
        /// can leave. A modifier alone is never an answer: every capture would otherwise
        /// end on the first Shift. `pointerOverCell` is what allows the wheel: it is a
        /// gesture the user aims, and reading it from anywhere would turn scrolling the
        /// list into a rebinding.
        [[nodiscard]] std::optional<Chord> CaptureChord(bool pointerOverCell);

        void RenderRow(const Command& command);

        /// Draws one capturable cell and, when it is the one being captured, runs the
        /// capture. `slot` is the index into the binding: 0 is the primary, 1..n the
        /// alternates, one past the end appends.
        void RenderSlot(const std::string& id, std::size_t slot, const std::string& text);

        /// Answers whether the chord is free, and when it is not, parks it in
        /// m_Conflict for the question the user has to answer first.
        void Offer(const std::string& id, std::size_t slot, const Chord& chord);

        void Apply(const std::string& id, std::size_t slot, const Chord& chord);

        /// The question that stands between a taken chord and its new owner.
        void RenderConflictPopup();

        void ForgetConflict();

        [[nodiscard]] bool Matches(const Command& command) const;

        CommandRegistry& m_Commands;
        Keymap& m_Keys;
        std::function<void()> m_OnChanged;

        /// The command id whose binding is being captured, and which slot of it — 0 is
        /// the primary, 1..n the alternates, and one past the end appends a new one.
        std::string m_Capturing;
        std::size_t m_CapturingSlot = 0;

        /// A chord the user chose that is already taken, held while they answer whether
        /// to take it away from its current owner. Never applied silently: the user would
        /// discover the loss of Ctrl+S at the moment they wanted to save.
        std::optional<Chord> m_Conflict;
        std::string m_ConflictOwner;

        /// Whether the modal has been opened for the conflict now held. It is what tells
        /// "not asked yet" apart from "asked, and dismissed with Escape or a click away",
        /// which look the same to ImGui::IsPopupOpen.
        bool m_ConflictAsked = false;

        /// A char buffer rather than std::string because ImGui's std::string overloads
        /// live in misc/cpp, which this build does not compile.
        std::array<char, 128> m_Filter{};
    };
}
