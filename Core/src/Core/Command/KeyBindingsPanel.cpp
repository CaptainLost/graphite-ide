#include "corepch.h"

#include "Core/Command/KeyBindingsPanel.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Log.h"
#include "Core/Command/Binding.h"
#include "Core/Command/Command.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/KeyName.h"
#include "Core/Command/Keymap.h"
#include "Core/Command/ModifierName.h"
#include "Core/Command/Trigger.h"

#include <imgui.h>

#include <utility>
#include <vector>

namespace Core {

	namespace {
		constexpr const char* kConflictPopup = "Shortcut already taken##keybindings";

		std::string LabelOf(const Command& command)
		{
			const std::size_t tab = command.Label.find('\t');

			if (tab == std::string::npos)
			{
				return command.Label;
			}

			return command.Label.substr(0, tab);
		}

		std::string TextOf(const Binding* binding, std::size_t slot)
		{
			if (binding == nullptr)
			{
				return {};
			}

			if (slot == 0)
			{
				return binding->Primary.ToString();
			}

			if (slot - 1 < binding->Alternates.size())
			{
				return binding->Alternates[slot - 1].ToString();
			}

			return {};
		}

		std::string AllChordText(const Binding* binding)
		{
			if (binding == nullptr)
			{
				return {};
			}

			std::string text = binding->Primary.ToString();

			for (const Chord& alternate : binding->Alternates)
			{
				text.append(" ");
				text.append(alternate.ToString());
			}

			return text;
		}

		std::string CaptureLabel(ImGuiKeyChord mods)
		{
			if (mods == ImGuiMod_None)
			{
				return "Press a key... (Esc cancels)";
			}

			std::string text;

			for (const ModifierName& modifier : kModifiers)
			{
				if ((mods & modifier.Bit) != 0)
				{
					text += modifier.Name;
					text += '+';
				}
			}

			text += "...";

			return text;
		}

		std::vector<Chord> Flatten(const Binding* binding)
		{
			if (binding == nullptr)
			{
				return {};
			}

			std::vector<Chord> chords;
			chords.reserve(binding->Alternates.size() + 1);
			chords.push_back(binding->Primary);
			chords.insert(chords.end(), binding->Alternates.begin(), binding->Alternates.end());

			return chords;
		}

		Binding Rebuild(std::vector<Chord> chords)
		{
			std::erase_if(chords, [](const Chord& chord) { return chord.IsEmpty(); });

			Binding binding;

			if (chords.empty())
			{
				return binding;
			}

			binding.Primary = chords.front();
			binding.Alternates.assign(chords.begin() + 1, chords.end());

			return binding;
		}
	}

	KeyBindingsPanel::KeyBindingsPanel(CommandRegistry& commands, Keymap& keys,
									   std::function<void()> onChanged)
		: Panel("keybindings", "Keyboard Shortcuts", false), m_Commands(commands), m_Keys(keys),
		  m_OnChanged(std::move(onChanged))
	{
	}

	void KeyBindingsPanel::OnImGuiRender()
	{
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextWithHint("##keybindingsFilter", "Filter by command or shortcut",
								 m_Filter.data(), m_Filter.size());

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

			m_Commands.ForEach(
				[this](const Command& command) {
					if (Matches(command))
					{
						RenderRow(command);
					}
				});

			ImGui::EndTable();
		}

		RenderConflictPopup();
	}

	void KeyBindingsPanel::OnNotDrawn()
	{
		ForgetConflict();
	}

	bool KeyBindingsPanel::Matches(const Command& command) const
	{
		const std::string needle = Lowered(m_Filter.data());

		if (needle.empty())
		{
			return true;
		}

		if (Lowered(command.Label).find(needle) != std::string::npos)
		{
			return true;
		}

		return Lowered(AllChordText(m_Keys.For(command.Id))).find(needle) !=
			   std::string::npos;
	}

	void KeyBindingsPanel::RenderRow(const Command& command)
	{
		const Binding* binding = m_Keys.For(command.Id);

		ImGui::PushID(command.Id.c_str());
		ImGui::TableNextRow();

		ImGui::TableNextColumn();
		ImGui::TextUnformatted(LabelOf(command).c_str());

		ImGui::TableNextColumn();
		RenderSlot(command.Id, 0, TextOf(binding, 0));

		ImGui::TableNextColumn();
		const std::size_t alternates = binding != nullptr ? binding->Alternates.size() : 0;

		for (std::size_t index = 0; index < alternates; ++index)
		{
			RenderSlot(command.Id, index + 1, TextOf(binding, index + 1));
		}

		// One slot past the end appends, so a command can gain a second way of being
		// reached without any other gesture for it.
		RenderSlot(command.Id, alternates + 1, {});

		ImGui::TableNextColumn();

		const bool overridden = m_Keys.IsOverridden(command.Id);
		ImGui::BeginDisabled(!overridden);

		if (ImGui::SmallButton("Default"))
		{
			m_Keys.Override(command.Id, std::nullopt);
			m_OnChanged();
		}

		ImGui::EndDisabled();
		ImGui::PopID();
	}

	void KeyBindingsPanel::RenderSlot(const std::string& id, std::size_t slot,
									  const std::string& text)
	{
		ImGui::PushID(static_cast<int>(slot));

		const bool capturing = m_Capturing == id && m_CapturingSlot == slot;
		std::string label = capturing ? CaptureLabel(ImGui::GetIO().KeyMods) : text;

		if (label.empty())
		{
			label = slot == 0 ? "(none)" : "+";
		}

		label.append("###slot");

		if (ImGui::Selectable(label.c_str(), capturing))
		{
			m_Capturing = id;
			m_CapturingSlot = slot;
		}

		const bool hovered = ImGui::IsItemHovered();

		// Both about the Selectable just drawn, so they have to be read before anything
		// else becomes "the last item".
		const bool selfActive = ImGui::IsItemActive();
		const bool anyActive = ImGui::IsAnyItemActive();

		// Not while the conflict question is up: the keys that answer it would otherwise
		// read as a fresh capture.
		if (capturing && !m_Conflict.has_value())
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
				m_Capturing.clear();
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
			m_Capturing.clear();
			return std::nullopt;
		}

		// Without it the screen would show Ctrl+WheelUp beside Zoom In and have no way to
		// set it. Checked before the keys because a wheel gesture carries modifiers too,
		// and only over the cell itself: the table scrolls, and a scroll aimed at the list
		// must not overwrite whatever row happens to be waiting.
		//
		// A modifier is required as well, because the cell the pointer is over is normally
		// the very one just clicked to arm the capture: scrolling the table to find another
		// row would otherwise bind a bare WheelUp, after which every scroll in an editor
		// runs that command once per notch. The modifiers are also what distinguish the
		// gesture from ordinary scrolling, so a bare wheel is not a shortcut anyone means
		// to set.
		if (pointerOverCell && io.MouseWheel != 0.0f && io.KeyMods != ImGuiMod_None)
		{
			return Chord::Wheel(io.KeyMods, io.MouseWheel > 0.0f ? Trigger::WheelUp : Trigger::WheelDown,
								Chord::Scope::HoveredEditor);
		}

		for (int index = ImGuiKey_NamedKey_BEGIN; index < ImGuiKey_NamedKey_END; ++index)
		{
			const auto key = static_cast<ImGuiKey>(index);

			// A capturable trigger is a keyboard key that is not a modifier, stated once
			// rather than as a growing list of spans to skip.
			//
			// Not a modifier, because every capture would otherwise end on the first Shift
			// the user pressed on the way to the chord they meant — and because ImGui has
			// two spans of those, which cost this screen a round of its own.
			//
			// A keyboard key, because the named range also carries the mouse: arming a
			// capture is a left click, so the scan used to find MouseLeft already pressed
			// on the very frame it started and bind it before the user could do anything.
			if (!IsKeyboardKey(key) || IsModifierKey(key))
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
		const std::string_view owner = m_Keys.BoundTo(chord);

		if (owner.empty() || owner == id)
		{
			Apply(id, slot, chord);
			return;
		}

		// Held rather than applied: a user who lost Ctrl+S without being told would
		// discover it at the moment they wanted to save.
		m_Conflict = chord;
		m_ConflictOwner = std::string(owner);
		m_ConflictAsked = false;
	}

	void KeyBindingsPanel::Apply(const std::string& id, std::size_t slot, const Chord& chord)
	{
		std::vector<Chord> chords = Flatten(m_Keys.For(id));

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

		m_Keys.Override(id, Rebuild(std::move(chords)));
		m_Capturing.clear();
		m_OnChanged();
	}

	void KeyBindingsPanel::RenderConflictPopup()
	{
		if (!m_Conflict.has_value())
		{
			return;
		}

		// Opened here rather than where the conflict was noticed: a popup's identifier is
		// relative to the ID stack it is opened under, and that one is inside the table's
		// per-row and per-cell PushIDs.
		if (!m_ConflictAsked)
		{
			ImGui::OpenPopup(kConflictPopup);
			m_ConflictAsked = true;
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

		const Command* owner = m_Commands.Find(m_ConflictOwner);
		const std::string ownerLabel = owner != nullptr ? LabelOf(*owner) : m_ConflictOwner;

		ImGui::Text("%s is already bound to %s.", m_Conflict->ToString().c_str(),
					ownerLabel.c_str());

		if (ImGui::Button("Take it"))
		{
			const std::string id = m_Capturing;
			const std::size_t slot = m_CapturingSlot;
			const Chord chord = *m_Conflict;

			if (const Binding* current = m_Keys.For(m_ConflictOwner); current != nullptr)
			{
				std::vector<Chord> chords = Flatten(current);
				std::erase(chords, chord);

				// Stored even when nothing is left, as an override that binds nothing.
				// Clearing the override instead would hand the previous owner its factory
				// chord straight back, which is the conflict again with no one told.
				m_Keys.Override(m_ConflictOwner, Rebuild(std::move(chords)));
			}

			const std::string ownerId = m_ConflictOwner;

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
		m_Conflict.reset();
		m_ConflictOwner.clear();
		m_ConflictAsked = false;
		m_Capturing.clear();
	}
}