#include "Settings/LanguageSection.h"

#include "Settings/AnalysisSettings.h"
#include "Settings/AppSettings.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/TextInput.h"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <utility>

namespace Graphite {

	LanguageSection::LanguageSection(AnalysisSettings& settings, AppSettings& store,
										 std::function<void()> onCppServerChanged)
		: m_Settings(settings), m_Store(store), m_OnCppServerChanged(std::move(onCppServerChanged))
	{
	}

	void LanguageSection::RenderServerPath()
	{
		ImGui::SeparatorText("Language server");

		if (!m_EditingServerPath)
		{
			m_PendingServerPath = Core::ToUtf8(m_Store.GetLanguageServerBinary());
		}

		ImGui::TextUnformatted("C++ language server");
		ImGui::SetNextItemWidth(-1.0f);
		Core::InputTextWithHint("##languageServerBinary", "clangd next to Graphite, then on PATH", m_PendingServerPath);

		m_EditingServerPath = ImGui::IsItemActive();

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			const std::filesystem::path editedPath = Core::FromUtf8(m_PendingServerPath);

			if (editedPath != m_Store.GetLanguageServerBinary())
			{
				m_Store.SetLanguageServerBinary(editedPath);
				m_OnCppServerChanged();
			}
		}

		ImGui::TextDisabled("Left empty, clangd is looked for next to Graphite, then on PATH.");
	}

	void LanguageSection::OnImGuiRender()
	{
		RenderServerPath();

		ImGui::SeparatorText("Background analysis");

		bool enabled = m_Settings.IsEnabled();

		if (ImGui::Checkbox("Analyse the workspace in the background", &enabled))
		{
			m_Settings.SetEnabled(enabled);
		}

		ImGui::TextWrapped("A few files at a time are sent to the language server, ahead of being "
						   "opened, so that a file opens already coloured. The pass waits while "
						   "the file you are reading has questions of its own outstanding.");

		// Disabled rather than hidden, the same reasoning SearchSection gives for its own
		// project-only checkbox: a control that vanishes reads as a bug, and there is nothing to
		// budget once the pass itself is off.
		ImGui::BeginDisabled(!enabled);

		int budget = static_cast<int>(m_Settings.GetBudgetMegabytes());

		// Set on release, not on every frame of the drag: SetBudgetMegabytes changes AppSettings,
		// which GraphiteLayer writes out at the end of any frame that changed it, and a drag across
		// the range produces a new value every frame. Same shape EditorSection
		// uses for its font-size and tab-width sliders, and the same reason.
		if (ImGui::SliderInt("Analysis kept (MB)", &budget,
							 static_cast<int>(AnalysisSettings::kMinimumBudgetMegabytes),
							 static_cast<int>(AnalysisSettings::kMaximumBudgetMegabytes),
							 "%d", ImGuiSliderFlags_AlwaysClamp))
		{
			m_PendingBudgetMegabytes = budget;
		}

		if (ImGui::IsItemDeactivatedAfterEdit() && m_PendingBudgetMegabytes.has_value())
		{
			m_Settings.SetBudgetMegabytes(static_cast<std::size_t>(*m_PendingBudgetMegabytes));
			m_PendingBudgetMegabytes.reset();
		}

		bool concurrencyAutomatic = m_Settings.IsConcurrencyAutomatic();

		if (ImGui::Checkbox("Set the number of files automatically", &concurrencyAutomatic))
		{
			m_Settings.SetConcurrencyAutomatic(concurrencyAutomatic);
		}

		// Disabled whenever the checkbox above is ticked, on top of the section's own condition:
		// automatic means the number is not this control's to set, not that it stops existing.
		ImGui::BeginDisabled(concurrencyAutomatic);

		int concurrency = static_cast<int>(m_Settings.GetConcurrency());

		// The label carries the automatic figure while that is what governs, so ticking the box
		// does not also make the number a mystery — a fixed "Files analysed at once" would leave
		// nothing on screen saying what "automatically" actually chose.
		const std::string label =
			concurrencyAutomatic ? std::format("Files analysed at once (automatic: {})###FilesAtOnce", concurrency)
								 : std::string{"Files analysed at once###FilesAtOnce"};

		// An input rather than a slider, whose grab covers a range this short. It reports a change
		// when the field is left or a step button is clicked, not per keystroke, so the settings
		// file is written once. Clamped here because a typed value is not bounded by the widget.
		if (ImGui::InputInt(label.c_str(), &concurrency))
		{
			const int clamped = std::clamp(concurrency, static_cast<int>(AnalysisSettings::kMinimumConcurrency),
										   static_cast<int>(AnalysisSettings::kMaximumConcurrency));
			m_Settings.SetConcurrency(static_cast<std::size_t>(clamped));
		}

		ImGui::EndDisabled();

		ImGui::EndDisabled();
	}
}
