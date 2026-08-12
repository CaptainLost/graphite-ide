#pragma once

#include "Editor/ModalGate.h"

#include <imgui.h>

#include <algorithm>
#include <concepts>
#include <optional>
#include <utility>
#include <vector>

namespace Graphite
{
    /// One question the application asks through a modal popup, from the request to the
    /// answer: what still has to be asked, what is on screen now, and the whole of the
    /// ceremony ImGui and ModalGate need around it.
    ///
    /// `Payload` is the state that must not outlive one asking of the question, and it is
    /// required to be movable because it travels from the caller into the queue and out of
    /// it again. Usually that state is what the question is about - which file - but not
    /// always: the exit question's payload is the close its answer has to carry out, which
    /// is context rather than subject. The test is not "does this name the subject" but
    /// "would this be wrong to still hold once the question is gone", because dropping it
    /// is exactly what Begin and Answer guarantee.
    ///
    /// Whatever it is, it has to survive frames: a question stays on screen for as long as
    /// the user leaves it there, so a path is a payload and an index into a container is
    /// not, and what it names is looked up again when the answer arrives rather than being
    /// held onto.
    ///
    /// A payload is not a way to give one question several forms. Where an answer differs
    /// only in what it then does, one question with a payload saying which is right; where
    /// the wording or the buttons differ, that is a second question and belongs in a second
    /// ModalQuestion, not in another enumerator.
    ///
    /// Questions are queued, never replaced. Several files changing in one batch (a git
    /// checkout), or a second tab's close button pressed before the first question
    /// resolves, must each get their own question, in order, instead of the later one
    /// silently dropping the earlier - and only one of them may be on screen at a time,
    /// because every question in this application competes for ImGui's single modal popup
    /// level, where a second OpenPopup destroys the first. See ModalGate.
    ///
    /// A popup can also go away without being answered - Escape, or something opening over
    /// it - and a question left behind by one would wedge the mechanism it guards for the
    /// rest of the session. Begin's false path notices that for every question there is,
    /// so a site cannot be written without the check. ADR 0475 records what each of those
    /// wedges cost when this was five hand-written copies.
    ///
    /// Used the way ImGui's own windows are, and carrying the same obligation - a true
    /// Begin must be matched by End:
    ///
    ///     if (question.Begin())
    ///     {
    ///         // draw the body; whichever button answers calls question.Answer()
    ///         question.End();
    ///     }
    template <std::movable Payload>
    class ModalQuestion
    {
    public:
        /// `title` is both the ImGui popup id and what the gate records as the owner of
        /// the modal level, so it must be one unchanging string - a literal outliving this
        /// object, never anything built per frame.
        ModalQuestion(ModalGate& gate, const char* title) noexcept
            : m_gate(gate), m_title(title)
        {
        }

        /// Queues a question. Several may wait; they are asked one at a time, in order,
        /// and never dropped.
        void Ask(Payload payload) { m_queue.push_back(std::move(payload)); }

        /// Whether this question is on screen or waiting for its turn. What a caller
        /// refusing to act until the user has answered tests - waiting counts, because a
        /// question still queued is every bit as unanswered as one the user is looking at.
        [[nodiscard]] bool IsAsked() const noexcept
        {
            return m_current.has_value() || !m_queue.empty();
        }

        /// Whether this exact question is already asked or waiting - the test a site makes
        /// before asking again, so a file named twice by two sweeps is one question rather
        /// than two identical ones in a row.
        [[nodiscard]] bool Asked(const Payload& payload) const
        {
            if (m_current && *m_current == payload)
            {
                return true;
            }

            return std::find(m_queue.begin(), m_queue.end(), payload) != m_queue.end();
        }

        /// The payload of the question currently on screen, or nullptr when none is.
        ///
        /// Answer() destroys what this points at, so a body holding the pointer across its
        /// buttons is relying on at most one of them being pressed per frame. That holds -
        /// ImGui activates one widget at a time - but it is ImGui's invariant and not this
        /// class's, so a body doing anything more than reading the payload before it
        /// answers should take a copy instead of holding the pointer.
        [[nodiscard]] const Payload* Current() const noexcept
        {
            return m_current ? &*m_current : nullptr;
        }

        /// Opens the next question when the gate is free and `stillWorthAsking` accepts it,
        /// and returns true when the caller must draw the body and then call End().
        ///
        /// `stillWorthAsking` is applied at the moment a question is taken off the queue,
        /// not when it joined: what it was about may have been dealt with some other way
        /// while it waited, and asking then would be asking about nothing. A rejected
        /// question is dropped and the next one considered.
        ///
        /// Rejecting is allowed to act, and at both sites here it does. A question is
        /// raised by a user asking for something - close this tab, exit - and only the
        /// grounds for *asking* go stale; the request itself does not. So a predicate that
        /// finds nothing left to ask about typically carries the request out and then
        /// returns false, rather than leaving the user's click answered by silence. It
        /// runs while no question is on screen, which is what makes that safe.
        ///
        /// The false path is where a popup that went away without being answered is
        /// noticed and both the gate and the payload released.
        template <std::predicate<const Payload&> Predicate>
        [[nodiscard]] bool Begin(Predicate stillWorthAsking)
        {
            OpenNext(stillWorthAsking);

            if (ImGui::BeginPopupModal(m_title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                return true;
            }

            // Believed on screen by the gate and yet not open: dismissed with Escape, or
            // destroyed by something opening over it. Treated as no answer at all - what
            // the question was about is untouched, and the site is free to ask again.
            if (m_gate.HoldsFor(m_title) && !ImGui::IsPopupOpen(m_title))
            {
                m_current.reset();
                m_gate.Close(m_title);
            }

            return false;
        }

        /// The same, for a question that is always worth asking once it has been raised.
        [[nodiscard]] bool Begin()
        {
            return Begin([](const Payload&) { return true; });
        }

        /// The user answered: the gate is released, the payload dropped, and the popup
        /// closed. The gate goes first on purpose - a question waiting its turn can then
        /// open in this same frame rather than a frame later. See ModalGate::Close.
        void Answer()
        {
            m_current.reset();
            m_gate.Close(m_title);
            ImGui::CloseCurrentPopup();
        }

        /// Matches a Begin() that returned true, exactly as ImGui::EndPopup does.
        void End() { ImGui::EndPopup(); }

    private:
        /// Takes the next question the gate will actually let onto the screen.
        ///
        /// Nothing is taken off the queue while the gate is busy - which includes another
        /// question having claimed it earlier this very frame. A question taken off the
        /// queue whose modal never opened is a question asked of nobody, and it is the
        /// payload left behind that would stop every later one from being asked; the
        /// question is delayed instead, never lost.
        template <std::predicate<const Payload&> Predicate>
        void OpenNext(Predicate& stillWorthAsking)
        {
            while (!m_current && !m_queue.empty() && !m_gate.IsBusy())
            {
                Payload candidate = std::move(m_queue.front());
                m_queue.erase(m_queue.begin());

                if (!stillWorthAsking(std::as_const(candidate)))
                {
                    continue;
                }

                // Asked again even though the loop condition just found the gate free,
                // because stillWorthAsking runs in between and runs application code: a
                // predicate that acts on the question having gone stale - closing the tab
                // it was about, carrying out the close it was asking permission for - can
                // reach something that opens a question of its own and takes the level.
                if (!m_gate.TryOpen(m_title))
                {
                    // It did. Put the question back where it was rather than dropping it,
                    // and wait for whichever question now holds the level.
                    m_queue.insert(m_queue.begin(), std::move(candidate));
                    return;
                }

                m_current = std::move(candidate);
                ImGui::OpenPopup(m_title);
            }
        }

        ModalGate& m_gate;

        /// A pointer, not a copy: every title is a constexpr literal that outlives this.
        const char* m_title;

        /// Questions waiting their turn, oldest first.
        std::vector<Payload> m_queue;

        /// The payload of the question on screen, or nullopt when none is.
        std::optional<Payload> m_current;
    };
}
