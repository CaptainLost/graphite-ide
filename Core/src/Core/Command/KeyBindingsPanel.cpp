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
#include "Core/Events/KeyEvent.h"
#include "Core/ImGui/ImGuiKeys.h"

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
		m_WasFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

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

	void KeyBindingsPanel::OnEvent(Event& event)
	{
		// Only while a cell is actually waiting, and only while this screen has the focus.
		// The event reached here at all because nothing above consumed it — a focused filter
		// box makes the GUI claim the keyboard, so typing into it never arrives.
		if (m_Capturing.empty() || m_Conflict.has_value() || !m_WasFocused)
		{
			return;
		}

		EventDispatcher dispatcher{event};

		dispatcher.Dispatch<KeyPressedEvent>(
			[this](KeyPressedEvent& pressed) {
				const ImGuiKey key = ToImGuiKey(pressed.GetKeyCode());

				// Escape leaves the capture without binding anything — which is why Escape
				// cannot be bound. The alternative is a cell the user cannot get out of.
				if (key == ImGuiKey_Escape)
				{
					m_Capturing.clear();
					m_TapMods = 0;
					return true;
				}

				// A modifier going down is not an answer yet: it may be the start of a chord
				// or a tap of its own, and only what happens next tells them apart.
				if (const ImGuiKeyChord bit = ModifierBitOf(pressed.GetKeyCode()); bit != 0)
				{
					if (!pressed.IsRepeat())
					{
						m_TapMods |= bit;
					}

					return false;
				}

				m_TapMods = 0;

				if (key == ImGuiKey_None)
				{
					return false;
				}

				const std::string id = m_Capturing;
				const std::size_t slot = m_CapturingSlot;

				Offer(id, slot, Chord::Key(ToImGuiModifiers(pressed.GetModifiers()) | key));
				return true;
			});

		// Modifiers released with nothing pressed in between: the gesture is the modifiers
		// themselves. Recorded here rather than on the press for the same reason the router
		// fires it here — until the release, this is indistinguishable from the start of an
		// ordinary chord.
		dispatcher.Dispatch<KeyReleasedEvent>(
			[this](KeyReleasedEvent& released) {
				if (ModifierBitOf(released.GetKeyCode()) == 0 || m_TapMods == 0)
				{
					return false;
				}

				const ImGuiKeyChord tapped = m_TapMods;
				m_TapMods = 0;

				const std::string id = m_Capturing;
				const std::size_t slot = m_CapturingSlot;

				Offer(id, slot, Chord::ModifierTap(tapped));
				return true;
			});
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
			else if (const std::optional<Chord> chord = CaptureWheelChord(hovered); chord.has_value())
			{
				Offer(id, slot, *chord);
			}
		}

		ImGui::PopID();
	}

	std::optional<Chord> KeyBindingsPanel::CaptureWheelChord(bool pointerOverCell)
	{
		const ImGuiIO& io = ImGui::GetIO();

		// The keys arrive as events; the wheel does not, because it is the one gesture whose
		// meaning depends on what it is pointing at, and that is known only while drawing.
		//
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