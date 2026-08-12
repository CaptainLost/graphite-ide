#pragma once

#include <string_view>

namespace Graphite
{
    /// Which question, if any, currently owns ImGui's single modal popup level.
    ///
    /// Every ImGui::OpenPopup call in this application happens with an empty
    /// BeginPopupStack, so they all compete for level 0. Opening a second popup there
    /// does not stack it: ImGui calls ClosePopupToLevel and tears the first one down
    /// (imgui.cpp, OpenPopupEx). The bool that asked for the destroyed popup has already
    /// been consumed by then, so its BeginPopupModal never returns true again, while the
    /// state identifying its question stays set forever - and that state is what guards
    /// the mechanism behind it. A destroyed reload question stops every later one from
    /// being asked; a destroyed overwrite question makes Ctrl+S do nothing for the rest
    /// of the session; a destroyed close question leaves the window unclosable.
    ///
    /// So the questions are serialised rather than allowed to race: one is on screen at a
    /// time, and a question that cannot be asked now is retained by its owner and asked
    /// once the gate frees, never dropped. This is the braces; the belt is each site
    /// clearing its own pending state when BeginPopupModal returns false for a popup ImGui
    /// no longer has open, so nothing can wedge even if some ordering slips past the gate.
    ///
    /// Held by GraphiteLayer and shared by reference with DocumentWindows, which owns four
    /// of the five questions - neither type has to know about the other's, only that some
    /// question is up.
    class ModalGate
    {
    public:
        /// Claims the modal level for `title`, which must be the same string the caller
        /// passes to ImGui::OpenPopup. False when another question holds it, in which case
        /// the caller must keep its request pending rather than discarding it.
        [[nodiscard]] bool TryOpen(std::string_view title) noexcept
        {
            if (!m_Owner.empty())
                return false;

            m_Owner = title;
            return true;
        }

        /// Releases the level, if `title` is what holds it. Called both when the user
        /// answers - immediately before ImGui::CloseCurrentPopup, so a question waiting
        /// its turn can open in this same frame - and when a popup is found to have gone
        /// away without an answer.
        void Close(std::string_view title) noexcept
        {
            if (m_Owner == title)
                m_Owner = {};
        }

        /// Whether any question is on screen or has been opened this frame.
        [[nodiscard]] bool IsBusy() const noexcept { return !m_Owner.empty(); }

        /// Whether `title` is the question this gate believes is on screen. The guard on
        /// the dead-state check: only the owner may conclude from a false BeginPopupModal
        /// that its own popup was destroyed.
        [[nodiscard]] bool HoldsFor(std::string_view title) const noexcept
        {
            return m_Owner == title;
        }

    private:
        /// A view, not a copy: every title is a constexpr literal that outlives the gate.
        std::string_view m_Owner;
    };
}
