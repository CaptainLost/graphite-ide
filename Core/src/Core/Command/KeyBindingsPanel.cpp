#include "corepch.h"

#include "Core/Command/KeyBindingsPanel.h"

#include "Core/Base/Ascii.h"
#include "Core/Command/Binding.h"
#include "Core/Command/Command.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Command/Scope.h"
#include "Core/Command/Trigger.h"

#include <imgui.h>

#include <utility>

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

        void Erase(Binding& binding, const Chord& chord)
        {
            for (std::size_t index = binding.alternates.size(); index > 0; --index)
            {
                if (binding.alternates[index - 1] == chord)
                {
                    binding.alternates.erase(binding.alternates.begin() +
                                             static_cast<std::ptrdiff_t>(index - 1));
                }
            }

            if (binding.primary == chord)
            {
                // An alternate is promoted rather than leaving an empty primary with live
                // alternates behind it: the menu shows the primary, and a command that
                // still answers to a chord should not look unbound.
                if (binding.alternates.empty())
                {
                    binding.primary = Chord{};
                }
                else
                {
                    binding.primary = binding.alternates.front();
                    binding.alternates.erase(binding.alternates.begin());
                }
            }
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

        // Not while the conflict question is up: the keys that answer it would otherwise
        // read as a fresh capture.
        if (capturing && !m_conflict.has_value())
        {
            if (const std::optional<Chord> chord = CaptureChord(); chord.has_value())
            {
                Offer(id, slot, *chord);
            }
        }

        ImGui::PopID();
    }

    std::optional<Chord> KeyBindingsPanel::CaptureChord()
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
        // set it. Checked before the keys because a wheel gesture carries modifiers too.
        if (io.MouseWheel != 0.0f)
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
    }

    void KeyBindingsPanel::Apply(const std::string& id, std::size_t slot, const Chord& chord)
    {
        Binding binding;

        if (const Binding* current = m_keys.For(id); current != nullptr)
        {
            binding = *current;
        }

        if (slot == 0)
        {
            binding.primary = chord;
        }
        else if (slot - 1 < binding.alternates.size())
        {
            binding.alternates[slot - 1] = chord;
        }
        else
        {
            binding.alternates.push_back(chord);
        }

        m_keys.Override(id, binding);
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
        if (!ImGui::IsPopupOpen(kConflictPopup))
        {
            ImGui::OpenPopup(kConflictPopup);
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
                Binding stripped = *current;
                Erase(stripped, chord);
                m_keys.Override(m_conflictOwner, stripped);
            }

            m_conflict.reset();
            m_conflictOwner.clear();
            Apply(id, slot, chord);
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel"))
        {
            m_conflict.reset();
            m_conflictOwner.clear();
            m_capturing.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}
