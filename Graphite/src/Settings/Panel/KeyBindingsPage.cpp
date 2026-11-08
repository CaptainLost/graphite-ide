#include "Settings/Panel/KeyBindingsPage.h"

#include "Core/Base/Log.h"
#include "Core/Base/Strings.h"
#include "Core/Command/Binding.h"
#include "Core/Command/Command.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/KeyName.h"
#include "Core/Command/Keymap.h"
#include "Core/Command/ModifierName.h"
#include "Core/Command/Trigger.h"
#include "Core/Command/TypingRules.h"
#include "Core/Events/KeyEvent.h"
#include "Core/ImGui/ImGuiKeys.h"
#include "Core/ImGui/TextButton.h"
#include "Core/ImGui/UiScale.h"

#include "Settings/Panel/SettingsLayout.h"
#include "Settings/Panel/SettingsSearch.h"
#include "Settings/Panel/ShortcutWhen.h"
#include "Theme/EditorPalette.h"
#include "Theme/Palettes.h"
#include "UI/Dialogs/DialogLayout.h"
#include "UI/Lists/FilterField.h"
#include "UI/Lists/ListRow.h"
#include "UI/TickBox.h"

#include <IconsLucide.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		constexpr const char* kConflictPopup = "Shortcut already taken##keybindings";

		constexpr const char* kRestorePopup = "Restore all defaults##keybindings";

		const std::array<std::string, 1> kRestoreLabels{"Restore All Defaults"};

		constexpr std::size_t kPrimarySlotIndex = 0;

		constexpr int kTableColumnCount = 5;

		constexpr float kCommandColumnWeight = 3.0f;
		constexpr float kShortcutColumnWeight = 2.0f;
		constexpr float kAlternatesColumnWeight = 3.0f;
		constexpr float kWhenColumnWeight = 2.5f;
		constexpr float kDefaultColumnWeight = 1.0f;

		constexpr const char* kEditorOnly = "Editor only";
		constexpr const char* kRepeatLabel = "Repeat while held";
		constexpr const char* kRepeatHeading = "Repeat";
		constexpr const char* kEditorOnlyNote = "Fires only while the editor holds the keyboard.";
		constexpr const char* kDefaultRestoresNote = "The row's Default restores these too.";

		constexpr float kWhenMenuWidth = 300.0f;
		constexpr int kWhenGridColumnCount = static_cast<int>(kTypingTargets.size()) + 2;

		constexpr const char* kWindowSwitcherReason = "the window switcher uses it";

		constexpr std::array kSlotFillColours{ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive};
		constexpr ImVec4 kNoFill(0.0f, 0.0f, 0.0f, 0.0f);

		// WARNING: A slot is a Selectable so that it is clicked and reached by the keyboard as one, but the row shows
		// its hover and the recording as one highlight across the table, so the slot's own square fill is cleared;
		// its keyboard mark is kept, as it tells which slot the keyboard is on.
		void PushClearedSlotFill()
		{
			for (const ImGuiCol colour : kSlotFillColours)
			{
				ImGui::PushStyleColor(colour, kNoFill);
			}
		}

		void PopClearedSlotFill()
		{
			ImGui::PopStyleColor(static_cast<int>(kSlotFillColours.size()));
		}

		// WARNING: ImGui knows which of a table's rows the pointer is over only from the frame before, so the row's
		// hover follows the pointer a frame late; the row has no Selectable of its own to ask.
		bool IsRowUnderThePointer()
		{
			return ImGui::TableGetHoveredRow() == ImGui::TableGetRowIndex();
		}

		std::string ListLabelOf(const Core::Command& command)
		{
			const std::string& label = command.ListLabel.empty() ? command.Label : command.ListLabel;
			const std::size_t tabPosition = label.find('\t');

			if (tabPosition == std::string::npos)
			{
				return label;
			}

			return label.substr(0, tabPosition);
		}

		std::string ChordTextInSlot(const Core::Binding* binding, std::size_t slotIndex)
		{
			if (binding == nullptr)
			{
				return {};
			}

			if (slotIndex == kPrimarySlotIndex)
			{
				return binding->Primary.ToString();
			}

			const std::size_t alternateIndex = slotIndex - 1;

			if (alternateIndex < binding->Alternates.size())
			{
				return binding->Alternates[alternateIndex].ToString();
			}

			return {};
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

		std::string AllChordText(const Core::Binding* binding)
		{
			std::vector<std::string> chordTexts;

			for (const Core::Chord& chord : Flatten(binding))
			{
				chordTexts.push_back(chord.ToString());
			}

			return Core::JoinedWith(chordTexts, " ");
		}

		// WARNING: ImGui routes these chords itself before any window sees them, so binding one would
		// make both actions happen on the same keystroke. They are refused when bound, not when fired.
		struct ReservedChord
		{
			ImGuiKeyChord Keys;
			const char* Reason;
		};

		constexpr ReservedChord kReservedChords[] = {
			{ImGuiMod_Ctrl | ImGuiKey_Tab, kWindowSwitcherReason},
			{ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Tab, kWindowSwitcherReason},
		};

		std::string_view ReservedReasonFor(const Core::Chord& chord)
		{
			if (chord.GetTrigger() != Core::Trigger::Key)
			{
				return {};
			}

			for (const ReservedChord& reserved : kReservedChords)
			{
				if (chord.GetKeys() == reserved.Keys)
				{
					return reserved.Reason;
				}
			}

			return {};
		}

		std::string CaptureLabel(ImGuiKeyChord heldModifiers)
		{
			if (heldModifiers == ImGuiMod_None)
			{
				return "Press a key... (Esc cancels)";
			}

			std::string text;

			for (const Core::ModifierName& modifier : Core::kModifiers)
			{
				if ((heldModifiers & modifier.Bit) != 0)
				{
					text += modifier.Name;
					text += '+';
				}
			}

			text += "...";

			return text;
		}

		std::string ShortcutsOf(const Core::Binding* binding)
		{
			std::vector<std::string> chordTexts;

			for (const Core::Chord& chord : Flatten(binding))
			{
				if (!chord.IsEmpty())
				{
					chordTexts.push_back(chord.ToString());
				}
			}

			const std::string text = Core::JoinedWith(chordTexts, ", ");

			return text.empty() ? "No shortcut" : text;
		}

		bool CanAppendAlternate(const Core::Binding* binding)
		{
			return binding != nullptr && !binding->Primary.IsEmpty();
		}

		void DropDuplicatesOfChord(std::vector<Core::Chord>& chords, const Core::Chord& chord, std::size_t& keptSlotIndex)
		{
			for (std::size_t reverseIndex = chords.size(); reverseIndex > 0; --reverseIndex)
			{
				const std::size_t chordIndex = reverseIndex - 1;

				if (chordIndex != keptSlotIndex && chords[chordIndex].IsSameGesture(chord))
				{
					chords.erase(chords.begin() + static_cast<std::ptrdiff_t>(chordIndex));

					if (chordIndex < keptSlotIndex)
					{
						--keptSlotIndex;
					}
				}
			}
		}

		bool IsEmptyChord(const Core::Chord& chord)
		{
			return chord.IsEmpty();
		}

		// What the When menu offers for a chord: an editor command fires only in the editor, so all it can be told is
		// whether it repeats.
		bool HasWhenFlags(const Core::Chord& chord, bool editorOnly)
		{
			return editorOnly ? Core::CanRepeat(chord) : Core::HasTypingFlags(chord);
		}

		bool DrawFiresInTick(const char* label, Core::Chord& chord, Core::TypingTarget target)
		{
			bool fires = Core::FiresIn(chord, target);

			if (!DrawTickBox(label, &fires))
			{
				return false;
			}

			Core::ChordFlags flags = chord.GetFlags();
			Core::SetFiresIn(flags, chord, target, fires);
			chord = chord.WithFlags(flags);

			return true;
		}

		bool DrawRepeatTick(const char* label, Core::Chord& chord, bool commandRepeats)
		{
			bool repeats = Core::RepeatsWhileHeld(chord, commandRepeats);

			if (!DrawTickBox(label, &repeats))
			{
				return false;
			}

			Core::ChordFlags flags = chord.GetFlags();
			Core::SetRepeatsWhileHeld(flags, repeats, commandRepeats);
			chord = chord.WithFlags(flags);

			return true;
		}

		void DrawMutedWrapped(const std::string& text, float wrapWidth)
		{
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapWidth);
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextUnformatted(text.c_str());
			ImGui::PopStyleColor();
			ImGui::PopTextWrapPos();
		}

		// One chord: a tick per place it may fire in, then whether it repeats.
		bool DrawWhenList(Core::Chord& chord, bool editorOnly, bool commandRepeats)
		{
			bool changed = false;

			if (!editorOnly)
			{
				for (const Core::TypingTarget target : kTypingTargets)
				{
					changed |= DrawFiresInTick(TypingTargetLabel(target), chord, target);
				}
			}

			if (Core::CanRepeat(chord))
			{
				if (!editorOnly)
				{
					ImGui::Separator();
				}

				changed |= DrawRepeatTick(kRepeatLabel, chord, commandRepeats);
			}

			return changed;
		}

		void DrawEmptyCell()
		{
			ImGui::TableNextColumn();
		}

		// Several chords: a row each, so F can wait while Ctrl+Shift+E of the same command does not.
		bool DrawWhenGrid(std::vector<Core::Chord>& chords, bool editorOnly, bool commandRepeats)
		{
			if (!ImGui::BeginTable("##whenGrid", kWhenGridColumnCount, ImGuiTableFlags_SizingFixedFit))
			{
				return false;
			}

			ImGui::TableNextRow();
			DrawEmptyCell();

			for (const Core::TypingTarget target : kTypingTargets)
			{
				ImGui::TableNextColumn();

				if (!editorOnly)
				{
					ImGui::TextDisabled("%s", TypingTargetHeading(target));
				}
			}

			ImGui::TableNextColumn();
			ImGui::TextDisabled("%s", kRepeatHeading);

			bool changed = false;

			for (std::size_t index = 0; index < chords.size(); ++index)
			{
				Core::Chord& chord = chords[index];

				if (!HasWhenFlags(chord, editorOnly))
				{
					continue;
				}

				ImGui::PushID(static_cast<int>(index));
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(chord.ToString().c_str());

				for (const Core::TypingTarget target : kTypingTargets)
				{
					ImGui::TableNextColumn();

					if (!editorOnly && Core::HasTypingFlags(chord))
					{
						ImGui::PushID(static_cast<int>(target));
						changed |= DrawFiresInTick("##fires", chord, target);
						ImGui::PopID();
					}
				}

				ImGui::TableNextColumn();

				if (Core::CanRepeat(chord))
				{
					changed |= DrawRepeatTick("##repeat", chord, commandRepeats);
				}

				ImGui::PopID();
			}

			ImGui::EndTable();

			return changed;
		}

		Core::Binding Rebuild(std::vector<Core::Chord> chords)
		{
			std::erase_if(chords, IsEmptyChord);

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

	KeyBindingsPage::KeyBindingsPage(Core::CommandRegistry& commands, Core::Keymap& keys, ModalGate& modals,
										const ActiveScheme& scheme, std::function<void()> onChanged)
		: m_Commands(commands), m_Keys(keys), m_Scheme(scheme), m_OnChanged(std::move(onChanged)), m_ConflictQuestion(modals, kConflictPopup),
		  m_RestoreQuestion(modals, kRestorePopup)
	{
	}

	void KeyBindingsPage::OnDeactivated()
	{
		m_ConflictQuestion.Withdraw();
		m_RestoreQuestion.Withdraw();
		m_IsConflictAwaitingAnswer = false;
		CancelCapture();
	}

	const char* KeyBindingsPage::GetIcon() const
	{
		return ICON_LC_KEYBOARD;
	}

	std::vector<SettingsCard> KeyBindingsPage::Describe()
	{
		SettingsCard restore;
		restore.Rows.push_back(SettingsRow{.Title = "Restore all defaults",
										   .Description = "Drops every shortcut you changed and asks first.",
										   .Keywords = {"reset", "shortcut"},
										   .ControlWidth = SettingsLayout::ButtonsWidth(kRestoreLabels),
										   .DrawControl = [this] { DrawRestoreAllDefaults(); }});

		SettingsCard shortcuts{.Heading = "Shortcuts"};
		shortcuts.Rows.push_back(SettingsRow{.Title = "Shortcuts",
											 .Keywords = {"shortcut", "hotkey", "keyboard", "key"},
											 .DrawBlock = [this] { DrawShortcuts(); },
											 .FindInside = [this](std::span<const std::string> words) { return FindCommands(words); }});

		return {restore, shortcuts};
	}

	void KeyBindingsPage::DrawRestoreAllDefaults()
	{
		bool anyOverridden = false;
		m_Keys.ForEachOverride([&anyOverridden](const std::string&, const Core::Binding&) { anyOverridden = true; });

		ImGui::BeginDisabled(!anyOverridden);

		if (Core::TextButton(kRestoreLabels.front().c_str()) && !m_RestoreQuestion.IsAsked())
		{
			CancelCapture();

			const bool restoreEveryBinding = true;
			m_RestoreQuestion.Ask(restoreEveryBinding);
		}

		ImGui::EndDisabled();
	}

	void KeyBindingsPage::DrawShortcuts()
	{
		m_WasFocusedLastFrame = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		DrawFilterField("##keybindingsFilter", m_Filter, {.Hint = "Filter by command or shortcut"});

		constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingStretchProp;

		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ListTableCellPadding());

		if (!ImGui::BeginTable("bindings", kTableColumnCount, tableFlags))
		{
			ImGui::PopStyleVar();
			return;
		}

		PadListTableEnds();

		ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch, kCommandColumnWeight);
		ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthStretch, kShortcutColumnWeight);
		ImGui::TableSetupColumn("Alternates", ImGuiTableColumnFlags_WidthStretch, kAlternatesColumnWeight);
		ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthStretch, kWhenColumnWeight);
		ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthStretch, kDefaultColumnWeight);
		ImGui::TableHeadersRow();

		const std::vector<std::string> words = Core::LoweredWords(m_Filter);

		m_Commands.ForEach([this, &words](const Core::Command& command) {
			if (CommandMatches(command, words))
			{
				RenderRow(command);
			}
		});

		ImGui::EndTable();
		ImGui::PopStyleVar();
	}

	void KeyBindingsPage::RenderDialogs()
	{
		RenderConflictQuestion();
		RenderRestoreQuestion();
	}

	void KeyBindingsPage::OnOpenedFromSearch(std::string_view query)
	{
		m_Filter = std::string{query};
	}

	std::vector<SettingsRow> KeyBindingsPage::FindCommands(std::span<const std::string> words) const
	{
		std::vector<SettingsRow> rows;

		if (words.empty())
		{
			return rows;
		}

		m_Commands.ForEach([this, words, &rows](const Core::Command& command) { AppendIfMatching(command, words, rows); });

		return rows;
	}

	void KeyBindingsPage::AppendIfMatching(const Core::Command& command, std::span<const std::string> words, std::vector<SettingsRow>& rows) const
	{
		if (!CommandMatches(command, words))
		{
			return;
		}

		rows.push_back(SettingsRow{.Key = command.Id,
								   .Title = ListLabelOf(command),
								   .Description = ShortcutsOf(m_Keys.For(command.Id)),
								   .OpenWith = ListLabelOf(command)});
	}

	void KeyBindingsPage::OnEvent(Core::Event& event)
	{
		if (!IsWaitingForChord())
		{
			return;
		}

		Core::EventDispatcher dispatcher{event};

		dispatcher.Dispatch<Core::KeyPressedEvent>([this](Core::KeyPressedEvent& pressed) {
			const ImGuiKey key = Core::ToImGuiKey(pressed.GetKeyCode());

			if (key == ImGuiKey_Escape)
			{
				CancelCapture();
				return true;
			}

			if (const ImGuiKeyChord modifierBit = Core::ModifierBitOf(pressed.GetKeyCode()); modifierBit != ImGuiMod_None)
			{
				if (!pressed.IsRepeat())
				{
					m_PendingTapModifiers |= modifierBit;
				}

				return false;
			}

			m_PendingTapModifiers = ImGuiMod_None;

			if (key == ImGuiKey_None)
			{
				return false;
			}

			const std::string capturingCommandId = m_CapturingCommandId;
			const std::size_t capturingSlotIndex = m_CapturingSlotIndex;
			const ImGuiKeyChord chordKeys = Core::ToImGuiModifiers(pressed.GetModifiers()) | key;

			Offer(capturingCommandId, capturingSlotIndex, Core::Chord::Key(chordKeys));
			return true;
		});

		dispatcher.Dispatch<Core::KeyReleasedEvent>([this](Core::KeyReleasedEvent& released) {
			const bool releasedKeyIsModifier = Core::ModifierBitOf(released.GetKeyCode()) != ImGuiMod_None;

			if (!releasedKeyIsModifier || m_PendingTapModifiers == ImGuiMod_None)
			{
				return false;
			}

			const ImGuiKeyChord tappedModifiers = m_PendingTapModifiers;
			m_PendingTapModifiers = ImGuiMod_None;

			const std::string capturingCommandId = m_CapturingCommandId;
			const std::size_t capturingSlotIndex = m_CapturingSlotIndex;

			Offer(capturingCommandId, capturingSlotIndex, Core::Chord::ModifierTap(tappedModifiers));
			return true;
		});
	}

	bool KeyBindingsPage::IsWaitingForChord() const
	{
		return !m_CapturingCommandId.empty() && !m_ConflictQuestion.IsAsked() && m_WasFocusedLastFrame;
	}

	void KeyBindingsPage::CancelCapture()
	{
		m_CapturingCommandId.clear();
		m_PendingTapModifiers = ImGuiMod_None;
	}

	bool KeyBindingsPage::CommandMatches(const Core::Command& command, std::span<const std::string> words) const
	{
		const std::string label = ListLabelOf(command);
		const std::string shortcuts = AllChordText(m_Keys.For(command.Id));
		const std::array<std::string_view, 2> texts{label, shortcuts};

		return EveryWordFound(words, texts);
	}

	void KeyBindingsPage::RenderRow(const Core::Command& command)
	{
		const Core::Binding* binding = m_Keys.For(command.Id);

		ImGui::PushID(command.Id.c_str());
		ImGui::TableNextRow(ImGuiTableRowFlags_None, ListRowPitch());

		ImGui::TableNextColumn();
		ImGui::TextUnformatted(ListLabelOf(command).c_str());

		ImGui::TableNextColumn();
		RenderSlot(command.Id, kPrimarySlotIndex, ChordTextInSlot(binding, kPrimarySlotIndex));

		ImGui::TableNextColumn();
		const std::size_t alternateCount = binding != nullptr ? binding->Alternates.size() : 0;

		for (std::size_t alternateIndex = 0; alternateIndex < alternateCount; ++alternateIndex)
		{
			const std::size_t slotIndex = alternateIndex + 1;
			RenderSlot(command.Id, slotIndex, ChordTextInSlot(binding, slotIndex));
		}

		if (CanAppendAlternate(binding))
		{
			const std::size_t appendSlotIndex = alternateCount + 1;
			const std::string noChordText;
			RenderSlot(command.Id, appendSlotIndex, noChordText);
		}

		ImGui::TableNextColumn();
		RenderWhen(command, binding);

		ImGui::TableNextColumn();

		const bool overridden = m_Keys.IsOverridden(command.Id);
		ImGui::BeginDisabled(!overridden);

		if (Core::SmallTextButton("Default"))
		{
			m_Keys.RestoreDefault(command.Id);
			m_OnChanged();
		}

		ImGui::EndDisabled();

		const bool recordingInThisRow = m_CapturingCommandId == command.Id;
		DrawTableRowHighlight(recordingInThisRow, IsRowUnderThePointer());

		ImGui::PopID();
	}

	void KeyBindingsPage::RenderWhen(const Core::Command& command, const Core::Binding* binding)
	{
		std::vector<Core::Chord> chords = Flatten(binding);
		const bool editorOnly = command.KeyScope == Core::ShortcutScope::FocusedEditor;
		const auto hasFlags = [editorOnly](const Core::Chord& chord) { return HasWhenFlags(chord, editorOnly); };
		const std::size_t flaggedCount = static_cast<std::size_t>(std::ranges::count_if(chords, hasFlags));

		if (flaggedCount == 0)
		{
			return;
		}

		const std::string summary = editorOnly ? std::string{kEditorOnly} : DescribeWhen(chords);
		const bool waitsSomewhere = !editorOnly && summary != kFiresEverywhere;

		// WARNING: No taller than a line of text, or every row with a When menu would stand taller than the rows
		// without one.
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0.0f));

		// WARNING: The summary is drawn over an empty preview rather than as it, so the warning colour reaches its
		// words and not the arrow beside them.
		ImDrawList* const drawList = ImGui::GetWindowDrawList();
		const ImVec2 frameTopLeft = ImGui::GetCursorScreenPos();
		const float frameWidth = ImGui::GetContentRegionAvail().x;
		const float frameHeight = ImGui::GetFrameHeight();
		const ImVec2 padding = ImGui::GetStyle().FramePadding;
		const float previewRight = frameTopLeft.x + frameWidth - frameHeight;

		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::SetNextWindowSizeConstraints(ImVec2(Core::Scaled(kWhenMenuWidth), 0.0f), ImVec2(FLT_MAX, FLT_MAX));
		const bool open = ImGui::BeginCombo("##when", "");

		// WARNING: Popped inside the menu when it is open, which ImGui allows: the menu's window began with it
		// pushed, and a window only checks that nothing pushed inside it is left behind.
		ImGui::PopStyleVar();

		const ImU32 summaryColour = waitsSomewhere ? ReadEditorPalette(m_Scheme.Get()).DiagnosticWarning : ImGui::GetColorU32(ImGuiCol_Text);
		const ImVec2 summaryAt(frameTopLeft.x + padding.x, frameTopLeft.y + padding.y);
		drawList->PushClipRect(frameTopLeft, ImVec2(previewRight, frameTopLeft.y + frameHeight), true);
		drawList->AddText(summaryAt, summaryColour, summary.c_str());
		drawList->PopClipRect();

		if (!open)
		{
			return;
		}

		const float wrapWidth = Core::Scaled(kWhenMenuWidth) - ImGui::GetStyle().WindowPadding.x * 2.0f;

		ImGui::TextUnformatted(std::format("{} fires", ListLabelOf(command)).c_str());

		if (editorOnly)
		{
			DrawMutedWrapped(kEditorOnlyNote, wrapWidth);
		}

		bool changed = false;

		if (flaggedCount == 1)
		{
			const auto flagged = std::ranges::find_if(chords, hasFlags);
			changed = DrawWhenList(*flagged, editorOnly, command.RepeatsWhileHeld);
		}
		else
		{
			changed = DrawWhenGrid(chords, editorOnly, command.RepeatsWhileHeld);
		}

		if (!editorOnly)
		{
			ImGui::Separator();

			const std::string defaultWait = DescribeDefaultWait(chords);
			DrawMutedWrapped(defaultWait.empty() ? kDefaultRestoresNote : defaultWait + " " + kDefaultRestoresNote, wrapWidth);
		}

		if (changed)
		{
			m_Keys.Override(command.Id, Rebuild(std::move(chords)));
			m_OnChanged();
		}

		ImGui::EndCombo();
	}

	void KeyBindingsPage::RenderSlot(const std::string& commandId, std::size_t slotIndex, const std::string& chordText)
	{
		ImGui::PushID(static_cast<int>(slotIndex));

		const bool capturing = m_CapturingCommandId == commandId && m_CapturingSlotIndex == slotIndex;
		std::string label = capturing ? CaptureLabel(ImGui::GetIO().KeyMods) : chordText;

		const bool placeholder = label.empty();

		if (placeholder)
		{
			label = slotIndex == kPrimarySlotIndex ? "(none)" : "+ Add";
		}

		label.append("###slot");

		if (placeholder)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		}

		PushClearedSlotFill();
		const bool clicked = ImGui::Selectable(label.c_str(), capturing);
		PopClearedSlotFill();

		if (placeholder)
		{
			ImGui::PopStyleColor();
		}

		if (clicked)
		{
			m_CapturingCommandId = commandId;
			m_CapturingSlotIndex = slotIndex;
		}

		// WARNING: These read the Selectable just drawn; ImGui's last-item state belongs to the next
		// widget as soon as one is submitted.
		const bool pointerOverCell = ImGui::IsItemHovered();
		const bool cellIsActive = ImGui::IsItemActive();
		const bool anyItemIsActive = ImGui::IsAnyItemActive();

		if (capturing && !m_ConflictQuestion.IsAsked())
		{
			if (!CaptureOwnsKeyboard(cellIsActive, anyItemIsActive))
			{
				m_CapturingCommandId.clear();
			}
			else if (const std::optional<Core::Chord> wheelChord = CaptureWheelChord(pointerOverCell); wheelChord.has_value())
			{
				Offer(commandId, slotIndex, *wheelChord);
			}
		}

		ImGui::PopID();
	}

	bool KeyBindingsPage::CaptureOwnsKeyboard(bool cellIsActive, bool anyItemIsActive)
	{
		const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		const bool noOtherItemIsActive = !anyItemIsActive || cellIsActive;

		return focused && noOtherItemIsActive;
	}

	std::optional<Core::Chord> KeyBindingsPage::CaptureWheelChord(bool pointerOverCell)
	{
		const ImGuiIO& io = ImGui::GetIO();

		const bool wheelTurned = io.MouseWheel != 0.0f;
		const bool modifierHeld = io.KeyMods != ImGuiMod_None;

		if (pointerOverCell && wheelTurned && modifierHeld)
		{
			const Core::Trigger wheelTrigger = io.MouseWheel > 0.0f ? Core::Trigger::WheelUp : Core::Trigger::WheelDown;

			return Core::Chord::Wheel(io.KeyMods, wheelTrigger, Core::ShortcutScope::HoveredEditor);
		}

		return std::nullopt;
	}

	void KeyBindingsPage::Offer(const std::string& commandId, std::size_t slotIndex, const Core::Chord& chord)
	{
		if (const std::string_view reason = ReservedReasonFor(chord); !reason.empty())
		{
			AskConflict(ContestedChord{chord, {}, std::string(reason), commandId, slotIndex});
			return;
		}

		const std::string_view owner = m_Keys.BoundTo(chord);

		if (owner.empty() || owner == commandId)
		{
			Apply(commandId, slotIndex, chord);
			return;
		}

		AskConflict(ContestedChord{chord, std::string(owner), {}, commandId, slotIndex});
	}

	void KeyBindingsPage::AskConflict(ContestedChord conflict)
	{
		m_ConflictQuestion.Ask(std::move(conflict));
		m_IsConflictAwaitingAnswer = true;
	}

	void KeyBindingsPage::Apply(const std::string& commandId, std::size_t slotIndex, const Core::Chord& chord)
	{
		std::vector<Core::Chord> chords = Flatten(m_Keys.For(commandId));

		if (slotIndex < chords.size())
		{
			chords[slotIndex] = chord;
		}
		else
		{
			chords.push_back(chord);
			slotIndex = chords.size() - 1;
		}

		DropDuplicatesOfChord(chords, chord, slotIndex);

		m_Keys.Override(commandId, Rebuild(std::move(chords)));
		m_CapturingCommandId.clear();
		m_OnChanged();
	}

	// WARNING: Asked from here rather than where the conflict was noticed: a popup's id is relative to
	// the ID stack it is opened under, and where the conflict is noticed that stack holds the table's
	// per-row and per-cell ids.
	void KeyBindingsPage::RenderConflictQuestion()
	{
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);
		const ContestedChord* asked = m_ConflictQuestion.Begin();
		DialogLayout::AfterBegin();

		if (asked != nullptr)
		{
			// WARNING: Answer releases the question's payload, so the conflict is copied out first.
			const ContestedChord conflict = *asked;

			const bool isRefusal = !conflict.Reason.empty();

			if (isRefusal)
			{
				DialogLayout::Text(std::format("{} cannot be bound: {}.", conflict.Chord.ToString(), conflict.Reason));
				DialogLayout::Gap();

				if (DialogLayout::AcknowledgeRow("OK"))
				{
					m_CapturingCommandId.clear();
					m_IsConflictAwaitingAnswer = false;
					m_ConflictQuestion.Answer();
				}

				m_ConflictQuestion.End();
				return;
			}

			const Core::Command* owner = m_Commands.Find(conflict.OwnerCommandId);
			const std::string ownerLabel = owner != nullptr ? ListLabelOf(*owner) : conflict.OwnerCommandId;

			DialogLayout::Text(std::format("{} is already bound to {}.", conflict.Chord.ToString(), ownerLabel));
			DialogLayout::Gap();

			const DialogLayout::Choice choice = DialogLayout::ButtonRow({.Label = "Take It"});

			if (choice == DialogLayout::Choice::Safe)
			{
				TakeFromOwner(conflict);
				m_IsConflictAwaitingAnswer = false;
				m_ConflictQuestion.Answer();

				APP_INFO("'{}' took {} from '{}'", conflict.CommandId, conflict.Chord.ToString(), conflict.OwnerCommandId);
			}
			else if (choice == DialogLayout::Choice::Cancel)
			{
				m_CapturingCommandId.clear();
				m_IsConflictAwaitingAnswer = false;
				m_ConflictQuestion.Answer();
			}

			m_ConflictQuestion.End();
			return;
		}

		const bool questionDismissedWithoutAnswer = m_IsConflictAwaitingAnswer && !m_ConflictQuestion.IsAsked();

		if (questionDismissedWithoutAnswer)
		{
			m_CapturingCommandId.clear();
			m_IsConflictAwaitingAnswer = false;
		}
	}

	void KeyBindingsPage::RenderRestoreQuestion()
	{
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);
		const bool open = m_RestoreQuestion.Begin() != nullptr;
		DialogLayout::AfterBegin();

		if (!open)
		{
			return;
		}

		DialogLayout::Text("Every shortcut you changed goes back to its default.");
		DialogLayout::Gap();

		const DialogLayout::ButtonSpec absentButton{};
		const DialogLayout::Choice choice =
			DialogLayout::ButtonRow(absentButton, {.Label = "Restore"}, DangerColourOf(m_Scheme.Get()));

		if (choice == DialogLayout::Choice::Destructive)
		{
			m_Keys.ClearOverrides();
			m_OnChanged();
			m_RestoreQuestion.Answer();

			APP_INFO("All key bindings restored to their defaults");
		}
		else if (choice == DialogLayout::Choice::Cancel)
		{
			m_RestoreQuestion.Answer();
		}

		m_RestoreQuestion.End();
	}

	void KeyBindingsPage::TakeFromOwner(const ContestedChord& conflict)
	{
		if (const Core::Binding* current = m_Keys.For(conflict.OwnerCommandId); current != nullptr)
		{
			std::vector<Core::Chord> chords = Flatten(current);
			std::erase_if(chords, [&conflict](const Core::Chord& chord) { return chord.IsSameGesture(conflict.Chord); });

			// WARNING: Stored even when empty, as an override that binds nothing: clearing the override
			// would hand the previous owner its factory chord straight back.
			m_Keys.Override(conflict.OwnerCommandId, Rebuild(std::move(chords)));
		}

		Apply(conflict.CommandId, conflict.SlotIndex, conflict.Chord);
	}
}
