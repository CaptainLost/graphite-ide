#include "Panels/KeyBindingsPanel.h"

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

#include <algorithm>
#include <string_view>

#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		constexpr const char* kConflictPopup = "Shortcut already taken##keybindings";

		std::string LabelOf(const Core::Command& command)
		{
			const std::size_t tab = command.Label.find('\t');

			if (tab == std::string::npos)
			{
				return command.Label;
			}

			return command.Label.substr(0, tab);
		}

		std::string TextOf(const Core::Binding* binding, std::size_t slot)
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

		std::string AllChordText(const Core::Binding* binding)
		{
			if (binding == nullptr)
			{
				return {};
			}

			std::string text = binding->Primary.ToString();

			for (const Core::Chord& alternate : binding->Alternates)
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

			for (const Core::ModifierName& modifier : Core::kModifiers)
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

		std::vector<Core::Chord> Flatten(const Core::Binding* binding)
		{
			if (binding == nullptr)
			{
				return {};
			}

			std::vector<Core::Chord> chords;
			chords.reserve(binding->Alternates.size() + 1);
			chords.push_back(binding->Primary);
			chords.insert(chords.end(), binding->Alternates.begin(), binding->Alternates.end());

			return chords;
		}

		bool CanAppendAlternate(const Core::Binding* binding)
		{
			return binding != nullptr && !binding->Primary.IsEmpty();
		}

		// The same chord twice in one command reaches it no more often than once. The user
		// put it in keptSlot, so it leaves the slot it was in.
		void DropDuplicatesOfChord(std::vector<Core::Chord>& chords, const Core::Chord& chord,
								   std::size_t& keptSlot)
		{
			for (std::size_t index = chords.size(); index > 0; --index)
			{
				if (index - 1 != keptSlot && chords[index - 1] == chord)
				{
					chords.erase(chords.begin() + static_cast<std::ptrdiff_t>(index - 1));

					if (index - 1 < keptSlot)
					{
						--keptSlot;
					}
				}
			}
		}

		Core::Binding Rebuild(std::vector<Core::Chord> chords)
		{
			std::erase_if(chords, [](const Core::Chord& chord) { return chord.IsEmpty(); });

			Core::Binding binding;

			if (chords.empty())
			{
				return binding;
			}

			binding.Primary = chords.front();
			binding.Alternates.assign(chords.begin() + 1, chords.end());

			return binding;
		}
	}

	KeyBindingsPanel::KeyBindingsPanel(Core::CommandRegistry& commands, Core::Keymap& keys,
									   std::function<void()> onChanged)
		: Core::Panel("keybindings", "Keyboard Shortcuts", false), m_Commands(commands), m_Keys(keys),
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
				[this](const Core::Command& command) {
					if (Matches(command))
					{
						RenderRow(command);
					}
				});

			ImGui::EndTable();
		}

		RenderConflictPopup();
	}

	void KeyBindingsPanel::OnEvent(Core::Event& event)
	{
		if (!IsWaitingForChord())
		{
			return;
		}

		Core::EventDispatcher dispatcher{event};

		dispatcher.Dispatch<Core::KeyPressedEvent>(
			[this](Core::KeyPressedEvent& pressed) {
				const ImGuiKey key = Core::ToImGuiKey(pressed.GetKeyCode());

				// Escape leaves the capture, which is why Escape cannot itself be bound: the
				// alternative is a cell the user cannot get out of.
				if (key == ImGuiKey_Escape)
				{
					CancelCapture();
					return true;
				}

				// A modifier going down is not an answer yet: only what happens next tells a
				// chord from a tap.
				if (const ImGuiKeyChord bit = Core::ModifierBitOf(pressed.GetKeyCode()); bit != 0)
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

				Offer(id, slot, Core::Chord::Key(Core::ToImGuiModifiers(pressed.GetModifiers()) | key));
				return true;
			});

		// Modifiers released with nothing pressed in between: the gesture is the modifiers
		// themselves, and only the release tells it apart from the start of a chord.
		dispatcher.Dispatch<Core::KeyReleasedEvent>(
			[this](Core::KeyReleasedEvent& released) {
				if (Core::ModifierBitOf(released.GetKeyCode()) == 0 || m_TapMods == 0)
				{
					return false;
				}

				const ImGuiKeyChord tapped = m_TapMods;
				m_TapMods = 0;

				const std::string id = m_Capturing;
				const std::size_t slot = m_CapturingSlot;

				Offer(id, slot, Core::Chord::ModifierTap(tapped));
				return true;
			});
	}

	bool KeyBindingsPanel::IsWaitingForChord() const
	{
		return !m_Capturing.empty() && !m_Conflict.has_value() && m_WasFocused;
	}

	void KeyBindingsPanel::CancelCapture()
	{
		m_Capturing.clear();
		m_TapMods = 0;
	}

	bool KeyBindingsPanel::Matches(const Core::Command& command) const
	{
		const std::string needle = Core::Lowered(m_Filter.data());

		if (needle.empty())
		{
			return true;
		}

		if (Core::Lowered(command.Label).find(needle) != std::string::npos)
		{
			return true;
		}

		return Core::Lowered(AllChordText(m_Keys.For(command.Id))).find(needle) !=
			   std::string::npos;
	}

	void KeyBindingsPanel::RenderRow(const Core::Command& command)
	{
		const Core::Binding* binding = m_Keys.For(command.Id);

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

		// One slot past the end appends. With no primary the appended chord would land at
		// index 0 and fill the Shortcut column instead of this one.
		if (CanAppendAlternate(binding))
		{
			RenderSlot(command.Id, alternates + 1, {});
		}

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

		const bool placeholder = label.empty();

		if (placeholder)
		{
			label = slot == 0 ? "(none)" : "+ Add";
		}

		label.append("###slot");

		if (placeholder)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		}

		const bool clicked = ImGui::Selectable(label.c_str(), capturing);

		if (placeholder)
		{
			ImGui::PopStyleColor();
		}

		if (clicked)
		{
			m_Capturing = id;
			m_CapturingSlot = slot;
		}

		// All three are about the Selectable just drawn: ImGui's last-item state belongs to
		// the next widget as soon as one is submitted.
		const bool hovered = ImGui::IsItemHovered();
		const bool selfActive = ImGui::IsItemActive();
		const bool anyActive = ImGui::IsAnyItemActive();

		// Not while the conflict question is up: the keys that answer it would otherwise
		// read as a fresh capture.
		if (capturing && !m_Conflict.has_value())
		{
			// Capture reads raw key state, which no widget owns: without this fence, typing
			// into the filter box while a cell waits would bind whatever was typed.
			if (!CaptureOwnsKeyboard(selfActive, anyActive))
			{
				m_Capturing.clear();
			}
			else if (const std::optional<Core::Chord> chord = CaptureWheelChord(hovered); chord.has_value())
			{
				Offer(id, slot, *chord);
			}
		}

		ImGui::PopID();
	}

	bool KeyBindingsPanel::CaptureOwnsKeyboard(bool selfActive, bool anyActive)
	{
		const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		return focused && (!anyActive || selfActive);
	}

	std::optional<Core::Chord> KeyBindingsPanel::CaptureWheelChord(bool pointerOverCell)
	{
		const ImGuiIO& io = ImGui::GetIO();

		// The keys arrive as events; the wheel does not, because its meaning depends on what
		// it points at, which is known only while drawing. Only over the cell itself, so a
		// scroll aimed at the table does not overwrite the row that is waiting, and only
		// with a modifier: a bare WheelUp would be bound while scrolling to find another
		// row, and would then fire once per notch in every editor.
		if (pointerOverCell && io.MouseWheel != 0.0f && io.KeyMods != ImGuiMod_None)
		{
			return Core::Chord::Wheel(io.KeyMods, io.MouseWheel > 0.0f ? Core::Trigger::WheelUp : Core::Trigger::WheelDown,
									  Core::Chord::Scope::HoveredEditor);
		}

		return std::nullopt;
	}

	void KeyBindingsPanel::Offer(const std::string& id, std::size_t slot, const Core::Chord& chord)
	{
		const std::string_view owner = m_Keys.BoundTo(chord);

		if (owner.empty() || owner == id)
		{
			Apply(id, slot, chord);
			return;
		}

		m_Conflict = chord;
		m_ConflictOwner = std::string(owner);
		m_ConflictAsked = false;
	}

	void KeyBindingsPanel::Apply(const std::string& id, std::size_t slot, const Core::Chord& chord)
	{
		std::vector<Core::Chord> chords = Flatten(m_Keys.For(id));

		if (slot < chords.size())
		{
			chords[slot] = chord;
		}
		else
		{
			chords.push_back(chord);
			slot = chords.size() - 1;
		}

		DropDuplicatesOfChord(chords, chord, slot);

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

		// Opened here rather than where the conflict was noticed: a popup's id is relative
		// to the ID stack it is opened under, and there it sits inside the table's per-row
		// and per-cell PushIDs.
		if (!m_ConflictAsked)
		{
			ImGui::OpenPopup(kConflictPopup);
			m_ConflictAsked = true;
		}
		else if (!ImGui::IsPopupOpen(kConflictPopup))
		{
			// Closed by something other than the two buttons — Escape, or the panel being
			// hidden and shown again. A dismissed question is a "no".
			ForgetConflict();
			return;
		}

		if (!ImGui::BeginPopupModal(kConflictPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			return;
		}

		const Core::Command* owner = m_Commands.Find(m_ConflictOwner);
		const std::string ownerLabel = owner != nullptr ? LabelOf(*owner) : m_ConflictOwner;

		ImGui::Text("%s is already bound to %s.", m_Conflict->ToString().c_str(),
					ownerLabel.c_str());

		if (ImGui::Button("Take it"))
		{
			const std::string id = m_Capturing;
			const std::size_t slot = m_CapturingSlot;
			const Core::Chord chord = *m_Conflict;

			if (const Core::Binding* current = m_Keys.For(m_ConflictOwner); current != nullptr)
			{
				std::vector<Core::Chord> chords = Flatten(current);
				std::erase(chords, chord);

				// Stored even when empty, as an override that binds nothing: clearing the
				// override would hand the previous owner its factory chord straight back.
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