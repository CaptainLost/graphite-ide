#pragma once

#include "ImGuiFrames.h"

#include "Settings/Panel/SettingsLayout.h"
#include "Settings/Panel/SettingsPage.h"

#include "Core/ImGui/Animation.h"

#include <imgui.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A settings page drawn as the panel draws it, in a window of a given width; and one row's control
// drawn alone at a known place, so a test can click it.
class SettingsPageFrames
{
public:
	explicit SettingsPageFrames(Graphite::SettingsPage& page) : m_Page(page) {}

	void DrawPage(float width = kDefaultWidth)
	{
		m_Frames.Next([this, width] { DrawPageInWindow(width); });
	}

	void DrawPageTwice(float width = kDefaultWidth)
	{
		DrawPage(width);
		DrawPage(width);
	}

	// Clicks the centre of the group the row's control drew, which is the control itself when it is
	// one item: a toggle or a single button.
	void ClickControl(std::string_view rowTitle)
	{
		m_Frames.Next([this, rowTitle] { DrawControlInWindow(rowTitle); });
		m_Frames.Click(m_ControlCentre, [this, rowTitle] { DrawControlInWindow(rowTitle); });
	}

	// Inside a frame of its own: a page measures its controls with the current font while it
	// describes them, and there is no font outside a frame.
	std::vector<Graphite::SettingsCard> DescribeInFrame()
	{
		std::vector<Graphite::SettingsCard> cards;
		m_Frames.Next([this, &cards] { cards = m_Page.Describe(); });
		return cards;
	}

	std::optional<Graphite::SettingsRow> FindRow(std::string_view title)
	{
		return FindRowIn(DescribeInFrame(), title);
	}

	std::vector<std::string> RowTitles()
	{
		std::vector<std::string> titles;

		for (const Graphite::SettingsCard& card : DescribeInFrame())
		{
			for (const Graphite::SettingsRow& row : card.Rows)
			{
				titles.push_back(row.Title);
			}
		}

		return titles;
	}

	std::vector<std::string> CardHeadings()
	{
		std::vector<std::string> headings;

		for (const Graphite::SettingsCard& card : DescribeInFrame())
		{
			headings.push_back(card.Heading);
		}

		return headings;
	}

	ImGuiFrames& GetFrames() { return m_Frames; }

private:
	static std::optional<Graphite::SettingsRow> FindRowIn(const std::vector<Graphite::SettingsCard>& cards, std::string_view title)
	{
		for (const Graphite::SettingsCard& card : cards)
		{
			for (const Graphite::SettingsRow& row : card.Rows)
			{
				if (row.Title == title)
				{
					return row;
				}
			}
		}

		return std::nullopt;
	}

	static constexpr float kDefaultWidth = 900.0f;
	static constexpr float kWindowHeight = 700.0f;
	static constexpr ImU32 kProblemColour = IM_COL32(255, 80, 80, 255);
	static constexpr float kFallbackControlWidth = 300.0f;

	void BeginWindow(const char* name, float width)
	{
		Core::Animation::BeginFrame();
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(width, kWindowHeight));
		ImGui::Begin(name);
		m_Page.Update();
	}

	void EndWindow()
	{
		m_Page.RenderDialogs();
		ImGui::End();
	}

	void DrawPageInWindow(float width)
	{
		BeginWindow("Settings", width);
		Graphite::SettingsLayout::DrawPageBody(m_Page, kProblemColour);
		EndWindow();
	}

	void DrawControlInWindow(std::string_view rowTitle)
	{
		BeginWindow("Control", kDefaultWidth);

		const std::optional<Graphite::SettingsRow> row = FindRowIn(m_Page.Describe(), rowTitle);

		if (row.has_value() && row->DrawControl)
		{
			const std::string key{row->GetKey()};
			ImGui::PushID(key.c_str());
			ImGui::SetCursorScreenPos(ImVec2(20.0f, 20.0f));
			ImGui::PushItemWidth(row->ControlWidth > 0.0f ? row->ControlWidth : kFallbackControlWidth);
			ImGui::BeginGroup();
			row->DrawControl();
			ImGui::EndGroup();
			ImGui::PopItemWidth();
			ImGui::PopID();

			const ImVec2 groupMin = ImGui::GetItemRectMin();
			const ImVec2 groupMax = ImGui::GetItemRectMax();
			m_ControlCentre = ImVec2((groupMin.x + groupMax.x) * 0.5f, (groupMin.y + groupMax.y) * 0.5f);
		}

		EndWindow();
	}

	Graphite::SettingsPage& m_Page;
	ImGuiFrames m_Frames;
	ImVec2 m_ControlCentre;
};
