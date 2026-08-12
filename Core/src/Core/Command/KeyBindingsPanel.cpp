#include "corepch.h"

#include "Core/Command/KeyBindingsPanel.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Log.h"
#include "Core/Command/Binding.h"
#include "Core/Command/Command.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Command/Scope.h"
#include "Core/Command/Trigger.h"

#include <imgui.h>

#include <utility>
#include <vector>

namespace core
{
    namespace
    {
        constexpr const char* kConflictPopup = "Shortcut already taken##keybindings";

        /// A label may carry its own shortcut hint after a tab ("Exit\tAlt+F4"), which is
        /// for a menu row and would only be noise in a column of its own.
        [[nodiscard]] std::string LabelOf(const Command& command)
        {
            const std::size_t tab = command.label.find('\t');

            if (tab == std::string::npos)
            {
                return command.label;
            }

            return command.label.substr(0, tab);
        }

        [[nodiscard]] std::string TextOf(const Binding* binding, std::size_t slot)
        {
            if (binding == nullptr)
            {
                return {};
            }

            if (slot == 0)
            {
                return binding->primary.ToString();
            }

            if (slot - 1 < binding->alternates.size())
            {
                return binding->alternates[slot - 1].ToString();
            }

            return {};
        }

        /// Every chord a binding names, in one string, so the filter can be matched
        /// against what the user sees rather than against the primary alone.
        [[nodiscard]] std::string AllChordText(const Binding* binding)
        {
            if (binding == nullptr)
            {
                return {};
            }

            std::string text = binding->primary.ToString();

            for (const Chord& alternate : binding->alternates)
            {
                text.append(" ");
                text.append(alternate.ToString());
            }

            return text;
        }

        /// A binding as one list, primary first: the slot numbering the screen uses is an
        /// index into exactly this, so every edit is a list edit and the primary/alternate
        /// split is put back once, in Rebuild.
        [[nodiscard]] std::vector<Chord> Flatten(const Binding* binding)
        {
            if (binding == nullptr)
            {
                return {};
            }

            std::vector<Chord> chords;
            chords.reserve(binding->alternates.size() + 1);
            chords.push_back(binding->primary);
            chords.insert(chords.end(), binding->alternates.begin(), binding->alternates.end());

            return chords;
        }

        /// Empty chords are dropped, so whatever is left first becomes the primary. That
        /// is what promotes an alternate when the primary is taken away: the menu shows
        /// the primary, and a command that still answers to a chord must not look unbound.
        [[nodiscard]] Binding Rebuild(std::vector<Chord> chords)
        {
            std::erase_if(chords, [](const Chord& chord) { return chord.IsEmpty(); });

            Binding binding;

            if (chords.empty())
            {
                return binding;
            }

            binding.primary = chords.front();
            binding.alternates.assign(chords.begin() + 1, chords.end());

            return binding;
        }
    }

    KeyBindingsPanel::KeyBindingsPanel(CommandRegistry& commands, Keymap& keys,
                                       std::function<void()> onChanged)
        : Panel("keybindings", "Keyboard Shortcuts", false), m_commands(commands), m_keys(keys),
          m_onChanged(std::move(onChanged))
    {
    }

    void KeyBindingsPanel::OnUIRender()
    {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##keybindingsFilter", "Filter by command or shortcut",
                                 m_filter.data(), m_filter.size());

        constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                          ImGuiTableFlags_SizingStretchProp;

        if (ImGui::BeginTable("bindings", 4, flags))
        {
            ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("Alternates", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            m_commands.ForEach(
                [this](const Command& command)
                {
                    if (Matches(command))
                    {
                        RenderRow(command);
                    }
                });

            ImGui::EndTable();
        }

        RenderConflictPopup();
    }

    bool KeyBindingsPanel::Matches(const Command& command) const
    {
        const std::string needle = ascii::Lowered(m_filter.data());

        if (needle.empty())
        {
            return true;
        }

        if (ascii::Lowered(command.label).find(needle) != std::string::npos)
        {
            return true;
        }

        return ascii::Lowered(AllChordText(m_keys.For(command.id))).find(needle) !=
               std::string::npos;
    }

    void KeyBindingsPanel::RenderRow(const Command& command)
    {
        const Binding* binding = m_keys.For(command.id);

        ImGui::PushID(command.id.c_str());
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(LabelOf(command).c_str());

        ImGui::TableNextColumn();
        RenderSlot(command.id, 0, TextOf(binding, 0));

        ImGui::TableNextColumn();
        const std::size_t alternates = binding != nullptr ? binding->alternates.size() : 0;

        for (std::size_t index = 0; index < alternates; ++index)
        {
            RenderSlot(command.id, index + 1, TextOf(binding, index + 1));
        }

        // One slot past the end appends, so a command can gain a second way of being
        // reached without any other gesture for it.
        RenderSlot(command.id, alternates + 1, {});

        ImGui::TableNextColumn();

        const bool overridden = m_keys.IsOverridden(command.id);
        ImGui::BeginDisabled(!overridden);

        if (ImGui::SmallButton("Default"))
        {
            m_keys.Override(command.id, std::nullopt);
            m_onChanged();
        }

        ImGui::EndDisabled();
        ImGui::PopID();
    }

    void KeyBindingsPanel::RenderSlot(const std::string& id, std::size_t slot,
                                      const std::string& text)
    {
        ImGui::PushID(static_cast<int>(slot));

        const bool capturing = m_capturing == id && m_capturingSlot == slot;
        std::string label = capturing ? "Press a key..." : text;

        if (label.empty())
        {
            label = slot == 0 ? "(none)" : "+";
        }

        label.append("###slot");

        if (ImGui::Selectable(label.c_str(), capturing))
        {
            m_capturing = id;
            m_capturingSlot = slot;
        }

        const bool hovered = ImGui::IsItemHovered();

        // Both about the Selectable just drawn, so they have to be read before anything
        // else becomes "the last item".
        const bool selfActive = ImGui::IsItemActive();
        const bool anyActive = ImGui::IsAnyItemActive();

        // Not while the conflict question is up: the keys that answer it would otherwise
        // read as a fresh capture.
        if (capturing && !m_conflict.has_value())
        {
            // Capture reads raw key state, which no widget owns, so it has to be fenced by
            // hand: without this, typing into the filter box — or into any other window —
            // while a cell waits would bind whatever was typed. Anything else taking the
            // keyboard ends the capture rather than merely pausing it, because the user
            // who clicked elsewhere has plainly stopped answering the question.
            const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            const bool ownsKeyboard = !anyActive || selfActive;

            if (!focused || !ownsKeyboard)
            {
                m_capturing.clear();
            }
            else if (const std::optional<Chord> chord = CaptureChord(hovered); chord.has_value())
            {
                Offer(id, slot, *chord);
            }
        }

        ImGui::PopID();
    }

    std::optional<Chord> KeyBindingsPanel::CaptureChord(bool pointerOverCell)
    {
        const ImGuiIO& io = ImGui::GetIO();

        // Escape leaves capture without binding anything — which is why Escape cannot be
        // bound. The alternative is a cell the user cannot get out of, and that is the worse
        // trade: nothing here is worth reaching by Escape.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            m_capturing.clear();
            return std::nullopt;
        }

        // Without it the screen would show Ctrl+WheelUp beside Zoom In and have no way to
        // set it. Checked before the keys because a wheel gesture carries modifiers too,
        // and only over the cell itself: the table scrolls, and a scroll aimed at the list
        // must not overwrite whatever row happens to be waiting.
        if (pointerOverCell && io.MouseWheel != 0.0f)
        {
            return Chord::Wheel(io.KeyMods, io.MouseWheel > 0.0f ? Trigger::WheelUp : Trigger::WheelDown,
                                Scope::HoveredEditor);
        }

        for (int index = ImGuiKey_NamedKey_BEGIN; index < ImGuiKey_NamedKey_END; ++index)
        {
            const auto key = static_cast<ImGuiKey>(index);

            // A modifier alone is never the answer: every capture would otherwise end on the
            // first Shift the user pressed on the way to the chord they meant.
            if (key >= ImGuiKey_LeftCtrl && key <= ImGuiKey_RightSuper)
            {
                continue;
            }

            if (ImGui::IsKeyPressed(key, false))
            {
                return Chord::Key(io.KeyMods | key);
            }
        }

        return std::nullopt;
    }

    void KeyBindingsPanel::Offer(const std::string& id, std::size_t slot, const Chord& chord)
    {
        const std::string_view owner = m_keys.BoundTo(chord);

        if (owner.empty() || owner == id)
        {
            Apply(id, slot, chord);
            return;
        }

        // Held rather than applied: a user who lost Ctrl+S without being told would
        // discover it at the moment they wanted to save.
        m_conflict = chord;
        m_conflictOwner = std::string(owner);
        m_conflictAsked = false;
    }

    void KeyBindingsPanel::Apply(const std::string& id, std::size_t slot, const Chord& chord)
    {
        std::vector<Chord> chords = Flatten(m_keys.For(id));

        if (slot < chords.size())
        {
            chords[slot] = chord;
        }
        else
        {
            chords.push_back(chord);
            slot = chords.size() - 1;
        }

        // The same chord twice in one command reaches it no more often than once, and it
        // would sit there looking like a second way in. Moving it is what the user asked
        // for: they put it in this slot, so it leaves the one it was in.
        for (std::size_t index = chords.size(); index > 0; --index)
        {
            if (index - 1 != slot && chords[index - 1] == chord)
            {
                chords.erase(chords.begin() + static_cast<std::ptrdiff_t>(index - 1));

                if (index - 1 < slot)
                {
                    --slot;
                }
            }
        }

        m_keys.Override(id, Rebuild(std::move(chords)));
        m_capturing.clear();
        m_onChanged();
    }

    void KeyBindingsPanel::RenderConflictPopup()
    {
        if (!m_conflict.has_value())
        {
            return;
        }

        // Opened here rather than where the conflict was noticed: a popup's identifier is
        // relative to the ID stack it is opened under, and that one is inside the table's
        // per-row and per-cell PushIDs.
        if (!m_conflictAsked)
        {
            ImGui::OpenPopup(kConflictPopup);
            m_conflictAsked = true;
        }
        else if (!ImGui::IsPopupOpen(kConflictPopup))
        {
            // Asked and then closed by something other than the two buttons — Escape, or
            // the panel having been hidden and shown again. A dismissed question is a
            // "no": the chord stays with the command that already had it.
            ForgetConflict();
            return;
        }

        if (!ImGui::BeginPopupModal(kConflictPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        const Command* owner = m_commands.Find(m_conflictOwner);
        const std::string ownerLabel = owner != nullptr ? LabelOf(*owner) : m_conflictOwner;

        ImGui::Text("%s is already bound to %s.", m_conflict->ToString().c_str(),
                    ownerLabel.c_str());

        if (ImGui::Button("Take it"))
        {
            const std::string id = m_capturing;
            const std::size_t slot = m_capturingSlot;
            const Chord chord = *m_conflict;

            if (const Binding* current = m_keys.For(m_conflictOwner); current != nullptr)
            {
                std::vector<Chord> chords = Flatten(current);
                std::erase(chords, chord);

                // Stored even when nothing is left, as an override that binds nothing.
                // Clearing the override instead would hand the previous owner its factory
                // chord straight back, which is the conflict again with no one told.
                m_keys.Override(m_conflictOwner, Rebuild(std::move(chords)));
            }

            const std::string ownerId = m_conflictOwner;

            ForgetConflict();
            Apply(id, slot, chord);
            ImGui::CloseCurrentPopup();

            CORE_INFO("'{}' took {} from '{}'", id, chord.ToString(), ownerId);
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel"))
        {
            ForgetConflict();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void KeyBindingsPanel::ForgetConflict()
    {
        m_conflict.reset();
        m_conflictOwner.clear();
        m_conflictAsked = false;
        m_capturing.clear();
    }
}
