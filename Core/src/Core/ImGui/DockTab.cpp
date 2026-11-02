#include "corepch.h"

#include "Core/ImGui/DockTab.h"

#include "Core/ImGui/DetachedWindows.h"

#include "Core/ImGui/IconButton.h"
#include "Core/ImGui/TabCloseRequests.h"
#include "Core/ImGui/Theme.h"
#include "Core/ImGui/UiScale.h"

#include <IconsLucide.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <optional>

namespace Core {

	namespace {
		constexpr float kLabelPaddingLeft = 12.0f;
		constexpr float kLabelPaddingRight = 8.0f;
		constexpr float kPartGap = 6.0f;
		constexpr float kCloseBoxSize = 16.0f;
		constexpr float kCloseBoxRounding = 3.0f;
		constexpr float kCloseGlyphSize = 11.0f;
		constexpr float kDotSize = 6.0f;
		constexpr float kHairline = 1.0f;
		constexpr float kHalf = 0.5f;
		constexpr float kNoBorder = 0.0f;
		constexpr float kNeverShown = FLT_MAX;
		// A crowded bar shrinks its tabs no narrower than this and scrolls the rest. ImGui's own floor leaves nothing
		// of a name beside its glyph and close button; this one leaves about a dozen characters.
		constexpr float kNarrowestShrunkenTab = 160.0f;
		constexpr const char* kFloatingTabBarsHook = "Core::KeepFloatingTabBarsFlat";
		constexpr unsigned int kFirstPrivateUseCodepoint = 0xE000;
		constexpr unsigned int kLastPrivateUseCodepoint = 0xF8FF;
		constexpr std::string_view kIdMarker = "##";
		constexpr const char* kCloseButtonId = "#DockTabClose";

		// NewFrame's span opens in one hook and closes in another, so the style it is laid out in lives here between them.
		std::optional<DockTabBarStyle>& FloatingTabBarStyle()
		{
			static std::optional<DockTabBarStyle> style;
			return style;
		}

		// The frame padding and shrink floor the dock space's bars were last laid out with, and the context they were
		// laid out in.
		struct DockSpacePadding
		{
			const ImGuiContext* Context = nullptr;
			float PaddingY = 0.0f;
			float NarrowestShrunkenTab = 0.0f;
		};

		std::optional<DockSpacePadding>& LastDockSpacePadding()
		{
			static std::optional<DockSpacePadding> padding;
			return padding;
		}

		// WARNING: The padding is the one the dock space applied, not one worked out here. Before NewFrame, EndFrame has
		// put the font size back to its unscaled base and left no viewport current, which drops the monitor's scale
		// from Scaled, so the font and the scale read here stand on different footings. NewFrame lays each bar out at
		// the font of the window its node draws round itself, while the windows in the node begin, in the frame, at
		// the dock space's padding; the two agree only at the same padding. Until a dock space has been laid out in
		// this context there is none, and the floating bars keep the global style.
		void FlattenFloatingTabBars()
		{
			const std::optional<DockSpacePadding>& padding = LastDockSpacePadding();

			if (padding.has_value() && padding->Context == ImGui::GetCurrentContext())
			{
				FloatingTabBarStyle().emplace(DockTabBarEdge::InsideTheWindowBorder, padding->PaddingY,
											  padding->NarrowestShrunkenTab);
			}
		}

		ImFont* FaceOf(const DockTabLook& look)
		{
			return look.Face != nullptr ? look.Face : ImGui::GetFont();
		}

		float NameWidth(const DockTabLook& look)
		{
			const char* const nameStart = look.Name.data();

			return FaceOf(look)->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, nameStart, nameStart + look.Name.size()).x;
		}

		float LeadWidth(const DockTabLook& look)
		{
			if (look.Lead.has_value())
			{
				return look.Lead->Width;
			}

			return look.Glyph.empty() ? 0.0f : ImGui::CalcTextSize(look.Glyph.c_str()).x;
		}

		// WARNING: ImGui widens a lone tab that does not fit its bar to the shrink floor, past the bar's end, where its
		// close button would be cut off, so such a tab is drawn only as wide as the bar.
		ImRect WithinLoneTabBar(const ImGuiTabBar& bar, const ImRect& tab)
		{
			if (bar.Tabs.Size != 1)
			{
				return tab;
			}

			return ImRect(tab.Min, ImVec2(std::min(tab.Max.x, bar.BarRect.Max.x), tab.Max.y));
		}

		bool IsDrawnTabBar(const ImGuiDockNode& node)
		{
			const bool hasBar = node.TabBar != nullptr && node.HostWindow != nullptr && !node.IsHiddenTabBar() && !node.IsNoTabBar();

			return hasBar && node.TabBar->CurrFrameVisible == ImGui::GetFrameCount();
		}

		void DrawBottomLine(ImDrawList& drawList, float left, float right, float bottom, ImU32 line)
		{
			drawList.AddRectFilled(ImVec2(left, bottom - kHairline), ImVec2(right, bottom), line);
		}

		void DrawLinesUnder(ImGuiDockNode& node)
		{
			if (node.IsSplitNode())
			{
				for (ImGuiDockNode* child : node.ChildNodes)
				{
					if (child != nullptr)
					{
						DrawLinesUnder(*child);
					}
				}

				return;
			}

			if (!IsDrawnTabBar(node) || !ImGui::DockNodeBeginAmendTabBar(&node))
			{
				return;
			}

			DrawBottomLine(*ImGui::GetWindowDrawList(), node.Pos.x, node.Pos.x + node.Size.x, node.TabBar->BarRect.Max.y,
						   ImGui::GetColorU32(ImGuiCol_Border));
			ImGui::DockNodeEndAmendTabBar();
		}

		struct TabState
		{
			bool Selected = false;
			bool Hovered = false;
			bool Dirty = false;
		};

		ImU32 LabelColour(const TabState& state)
		{
			if (state.Selected)
			{
				return EmphasisText();
			}

			return ImGui::GetColorU32(state.Hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
		}

		// WARNING: The name is drawn in its own face, pushed for the call: the ellipsis a cut name ends in takes the
		// current face and the current text colour.
		void DrawName(ImDrawList& drawList, const DockTabLook& look, ImVec2 lineTop, float right, ImU32 colour)
		{
			const char* const nameStart = look.Name.data();
			const char* const nameEnd = nameStart + look.Name.size();

			ImGui::PushFont(FaceOf(look), 0.0f);
			ImGui::PushStyleColor(ImGuiCol_Text, colour);
			ImGui::RenderTextEllipsis(&drawList, lineTop, ImVec2(right, lineTop.y + ImGui::GetFontSize()), right, nameStart,
									  nameEnd, nullptr);
			ImGui::PopStyleColor();
			ImGui::PopFont();
		}

		bool DrawCloseButton(ImDrawList& drawList, const ImRect& box, ImGuiID id)
		{
			if (!ImGui::ItemAdd(box, id))
			{
				return false;
			}

			bool hovered = false;
			bool held = false;
			const bool pressed = ImGui::ButtonBehavior(box, id, &hovered, &held);

			if (hovered || held)
			{
				drawList.AddRectFilled(box.Min, box.Max, ImGui::GetColorU32(ImGuiCol_ButtonHovered), Scaled(kCloseBoxRounding));
			}

			// WARNING: The size is the unscaled one: ImGui multiplies a pushed size by the interface scale itself.
			ImGui::PushFont(nullptr, kCloseGlyphSize);
			DrawCentredGlyph(&drawList, box, ICON_LC_X, ImGui::GetColorU32(hovered || held ? ImGuiCol_Text : ImGuiCol_TextDisabled));
			ImGui::PopFont();

			return pressed;
		}

		void DrawTab(const ImGuiWindow& window, const ImGuiTabBar& bar, const ImRect& tab, const DockTabLook& look,
					 TabCloseRequests& closeRequests)
		{
			const ImGuiContext& context = *ImGui::GetCurrentContext();
			const ImGuiID closeId = ImGui::GetIDWithSeed(kCloseButtonId, nullptr, window.ID);

			TabState state;
			state.Selected = bar.VisibleTabId == window.TabId;
			state.Hovered = context.HoveredId == window.TabId || context.HoveredId == closeId;
			state.Dirty = (window.Flags & ImGuiWindowFlags_UnsavedDocument) != 0;
			const bool held = context.ActiveId == window.TabId || context.ActiveId == closeId;

			// WARNING: Clipped as ImGui clips a tab that scrolls in a bar too narrow for its tabs.
			const float clipLeft = std::max(tab.Min.x, bar.ScrollingRectMinX);
			const float clipRight = std::min(tab.Max.x, bar.ScrollingRectMaxX);

			if (clipRight <= clipLeft)
			{
				return;
			}

			constexpr bool kWithinTheWindow = true;
			ImGui::PushClipRect(ImVec2(clipLeft, tab.Min.y), ImVec2(clipRight, tab.Max.y), kWithinTheWindow);

			ImDrawList& drawList = *ImGui::GetWindowDrawList();
			const ImU32 line = ImGui::GetColorU32(ImGuiCol_Border);

			// WARNING: The fill is opaque and covers everything ImGui drew for the tab: its label, its unsaved bullet and
			// its hover fill. A selected tab takes the colour its window was begun with, the view's own. ImGui keeps a
			// window's ImGuiCol_Tab in the dock style slot it names TabFocused.
			const ImU32 fill =
				window.DockStyle.Colors[state.Selected ? ImGuiWindowDockStyleCol_TabSelected : ImGuiWindowDockStyleCol_TabFocused];
			drawList.AddRectFilled(tab.Min, tab.Max, fill);
			drawList.AddRectFilled(ImVec2(tab.Max.x - kHairline, tab.Min.y), tab.Max, line);

			if (!state.Selected)
			{
				DrawBottomLine(drawList, tab.Min.x, tab.Max.x, tab.Max.y, line);
			}

			const float gap = Scaled(kPartGap);
			const float closeSize = Scaled(kCloseBoxSize);
			const float closeLeft = tab.Max.x - Scaled(kLabelPaddingRight) - closeSize;
			const float closeTop = std::floor(tab.Min.y + (tab.GetHeight() - closeSize) * kHalf);
			const ImRect closeBox(ImVec2(closeLeft, closeTop), ImVec2(closeLeft + closeSize, closeTop + closeSize));

			const ImU32 labelColour = LabelColour(state);
			const ImVec2 lineTop(tab.Min.x + Scaled(kLabelPaddingLeft),
								 std::floor(tab.Min.y + (tab.GetHeight() - ImGui::GetFontSize()) * kHalf));
			float nameLeft = lineTop.x;

			if (look.Lead.has_value())
			{
				look.Lead->Draw(lineTop);
				nameLeft += look.Lead->Width + gap;
			}
			else if (!look.Glyph.empty())
			{
				drawList.AddText(lineTop, labelColour, look.Glyph.c_str());
				nameLeft += LeadWidth(look) + gap;
			}

			const float dotRoom = look.Dot.has_value() ? Scaled(kDotSize) + gap : 0.0f;
			const float nameRight = closeLeft - gap - dotRoom;
			DrawName(drawList, look, ImVec2(nameLeft, lineTop.y), nameRight, labelColour);

			if (look.Dot.has_value())
			{
				const float dotRadius = Scaled(kDotSize) * kHalf;
				const float dotLeft = std::min(nameLeft + NameWidth(look), nameRight) + gap;
				drawList.AddCircleFilled(ImVec2(dotLeft + dotRadius, tab.GetCenter().y), dotRadius, *look.Dot);
			}

			// The close button shows on the selected tab and under the pointer; an unsaved tab shows its bullet in its
			// place until the pointer comes.
			const bool closeShown = state.Selected || state.Hovered || held;
			const bool bulletShown = state.Dirty && (!closeShown || !(state.Hovered || held));

			if (bulletShown)
			{
				ImGui::RenderBullet(&drawList, closeBox.GetCenter(), window.DockStyle.Colors[ImGuiWindowDockStyleCol_UnsavedMarker]);
			}
			else if (closeShown && DrawCloseButton(drawList, closeBox, closeId))
			{
				closeRequests.Request(window.Name);
			}

			if (state.Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
			{
				closeRequests.Request(window.Name);
			}

			ImGui::PopClipRect();
		}
	}

	DockTabLook DockTabLookOf(std::string_view windowName)
	{
		std::string_view shown = windowName.substr(0, windowName.find(kIdMarker));
		DockTabLook look;

		if (!shown.empty())
		{
			unsigned int codepoint = 0;
			const int glyphBytes = ImTextCharFromUtf8(&codepoint, shown.data(), shown.data() + shown.size());
			const bool startsWithAGlyph = codepoint >= kFirstPrivateUseCodepoint && codepoint <= kLastPrivateUseCodepoint;

			if (startsWithAGlyph)
			{
				look.Glyph = std::string(shown.substr(0, static_cast<std::size_t>(glyphBytes)));
				shown.remove_prefix(static_cast<std::size_t>(glyphBytes));
				shown.remove_prefix(std::min(shown.find_first_not_of(' '), shown.size()));
			}
		}

		look.Name = std::string(shown);
		return look;
	}

	float DockTabWidth(const DockTabLook& look)
	{
		const float gap = Scaled(kPartGap);
		const float lead = LeadWidth(look);

		float width = Scaled(kLabelPaddingLeft) + NameWidth(look) + gap + Scaled(kCloseBoxSize) + Scaled(kLabelPaddingRight);

		if (lead > 0.0f)
		{
			width += lead + gap;
		}

		if (look.Dot.has_value())
		{
			width += gap + Scaled(kDotSize);
		}

		return std::ceil(width);
	}

	float DockTabBarFramePaddingY()
	{
		return (std::round(Scaled(kDockTabBarHeight)) - ImGui::GetFontSize()) * kHalf;
	}

	// WARNING: ImGui lays a dock node's tab bar out from the style then in effect: across, it sets the first tab in by
	// the window border and the frame padding, spaces the tabs by the inner item spacing, puts its own close button the
	// padding from a tab's end, and draws its overflow arrows as bordered frames. The close button's reach and its
	// visibility have no style var, and the floating nodes' bars are laid out in NewFrame, where no style var may stay
	// pushed, so the style is written here and put back as it was.
	DockTabBarStyle::DockTabBarStyle(DockTabBarEdge edge)
		: DockTabBarStyle(edge, DockTabBarFramePaddingY())
	{
		if (edge == DockTabBarEdge::AtTheNode)
		{
			const ImGuiStyle& style = ImGui::GetStyle();

			LastDockSpacePadding() = DockSpacePadding{.Context = ImGui::GetCurrentContext(),
													  .PaddingY = style.FramePadding.y,
													  .NarrowestShrunkenTab = style.TabMinWidthShrink};
		}
	}

	DockTabBarStyle::DockTabBarStyle(DockTabBarEdge edge, float framePaddingY)
		: DockTabBarStyle(edge, framePaddingY, Scaled(kNarrowestShrunkenTab))
	{
	}

	DockTabBarStyle::DockTabBarStyle(DockTabBarEdge edge, float framePaddingY, float narrowestShrunkenTab)
	{
		ImGuiStyle& style = ImGui::GetStyle();

		m_FramePadding = style.FramePadding;
		m_ItemInnerSpacingX = style.ItemInnerSpacing.x;
		m_FrameBorderSize = style.FrameBorderSize;
		m_WindowBorderSize = style.WindowBorderSize;
		m_CloseButtonMinWidthSelected = style.TabCloseButtonMinWidthSelected;
		m_CloseButtonMinWidthUnselected = style.TabCloseButtonMinWidthUnselected;
		m_TabMinWidthShrink = style.TabMinWidthShrink;

		style.FramePadding = ImVec2(0.0f, framePaddingY);
		style.ItemInnerSpacing.x = 0.0f;
		style.FrameBorderSize = kNoBorder;
		style.TabCloseButtonMinWidthSelected = kNeverShown;
		style.TabCloseButtonMinWidthUnselected = kNeverShown;
		style.TabMinWidthShrink = narrowestShrunkenTab;

		if (edge == DockTabBarEdge::AtTheNode)
		{
			style.WindowBorderSize = kNoBorder;
		}
	}

	DockTabBarStyle::~DockTabBarStyle()
	{
		ImGuiStyle& style = ImGui::GetStyle();

		style.FramePadding = m_FramePadding;
		style.ItemInnerSpacing.x = m_ItemInnerSpacingX;
		style.FrameBorderSize = m_FrameBorderSize;
		style.WindowBorderSize = m_WindowBorderSize;
		style.TabCloseButtonMinWidthSelected = m_CloseButtonMinWidthSelected;
		style.TabCloseButtonMinWidthUnselected = m_CloseButtonMinWidthUnselected;
		style.TabMinWidthShrink = m_TabMinWidthShrink;
	}

	void KeepFloatingTabBarsFlat()
	{
		ImGuiContext& context = *ImGui::GetCurrentContext();
		const ImGuiID owner = ImHashStr(kFloatingTabBarsHook);

		for (const ImGuiContextHook& hook : context.Hooks)
		{
			if (hook.Owner == owner)
			{
				return;
			}
		}

		ImGuiContextHook before;
		before.Type = ImGuiContextHookType_NewFramePre;
		before.Owner = owner;
		before.Callback = [](ImGuiContext*, ImGuiContextHook*) { FlattenFloatingTabBars(); };
		ImGui::AddContextHook(&context, &before);

		ImGuiContextHook after;
		after.Type = ImGuiContextHookType_NewFramePost;
		after.Owner = owner;
		after.Callback = [](ImGuiContext*, ImGuiContextHook*) { FloatingTabBarStyle().reset(); };
		ImGui::AddContextHook(&context, &after);
	}

	void DrawDockTabBarLines(ImGuiID dockSpaceId)
	{
		if (ImGuiDockNode* root = ImGui::DockBuilderGetNode(dockSpaceId); root != nullptr)
		{
			DrawLinesUnder(*root);
		}

		for (const ImGuiStoragePair& entry : ImGui::GetCurrentContext()->DockContext.Nodes.Data)
		{
			ImGuiDockNode* const node = static_cast<ImGuiDockNode*>(entry.val_p);
			const bool floatingRoot = node != nullptr && node->IsFloatingNode();

			if (floatingRoot)
			{
				DrawLinesUnder(*node);
			}
		}

		DrawDetachedWindowButtons();
	}

	// WARNING: A docked window's title bar is what its content starts below, and Begin sizes it from the frame padding
	// in effect, apart from the node's tab bar, which DockSpace laid out. The two agree only with the same padding.
	bool BeginDockedWindow(const char* name, bool* open, ImGuiWindowFlags flags)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, DockTabBarFramePaddingY()));
		const bool visible = ImGui::Begin(name, open, flags);
		ImGui::PopStyleVar();

		return visible;
	}

	void DrawDockTab(const DockTabLook& look, TabCloseRequests& closeRequests)
	{
		// WARNING: The window is read, not taken with GetCurrentWindow: that marks it written to, and ImGui then stops
		// reporting its tab as hovered to the tab's context menu.
		ImGuiContext& context = *ImGui::GetCurrentContext();
		const ImGuiWindow* const window = context.CurrentWindow;

		if (window == nullptr || !window->DockIsActive || window->DockNode == nullptr || window->DockNode->TabBar == nullptr)
		{
			return;
		}

		ImGuiDockNode& node = *window->DockNode;
		ImGuiTabItem* const tab = ImGui::TabBarFindTabByID(node.TabBar, window->TabId);

		if (tab == nullptr)
		{
			return;
		}

		// WARNING: ImGui lays a tab bar out at its first tab, from each tab's requested width, and forgets the request
		// once the tab is drawn, so it is asked again every frame for the next layout.
		tab->RequestedWidth = DockTabWidth(look);

		const ImRect tabRect = window->DC.DockTabItemRect;
		const bool drawnThisFrame = tab->LastFrameVisible == context.FrameCount && tabRect.GetWidth() > 0.0f;

		if (!drawnThisFrame || !IsDrawnTabBar(node))
		{
			return;
		}

		// WARNING: Whoever asks about the tab after Begin, the context menu and a double click among them, reads the last
		// item, which the close button would otherwise have become.
		const ImGuiLastItemData tabItem = context.LastItemData;

		if (!ImGui::DockNodeBeginAmendTabBar(&node))
		{
			return;
		}

		DrawTab(*window, *node.TabBar, WithinLoneTabBar(*node.TabBar, tabRect), look, closeRequests);
		ImGui::DockNodeEndAmendTabBar();

		context.LastItemData = tabItem;
	}
}
