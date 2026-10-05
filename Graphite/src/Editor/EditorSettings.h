#pragma once

#include "Core/Text/Editing.h"

#include <cstddef>
#include <cstdint>

namespace Core {

	class CommandRegistry;
	class Keymap;
}
namespace Graphite {

	class AppSettings;

	enum class DefinitionPreviewMode : std::uint8_t
	{
		Off,
		WithCtrl,
		Always
	};

	struct HoverSettings
	{
		bool ShowInfo = true;
		DefinitionPreviewMode Preview = DefinitionPreviewMode::WithCtrl;
		int DelayMs = 500;
	};

	class EditorSettings final
	{
	public:
		explicit EditorSettings(AppSettings& settings) : m_Store(settings) {}

		void Load();

		void RegisterCommands(Core::CommandRegistry& commands, Core::Keymap& keys);

		float GetFontSize() const { return m_FontSize; }

		void SetFontSize(float size);

		void SetIndentWithSpaces(bool useSpaces);

		void SetIndentWidth(std::size_t width);

		Core::IndentStyle GetIndentStyle() const
		{
			return {m_IndentWithSpaces, m_IndentWidth};
		}

		bool IsWordWrap() const { return m_WordWrap; }

		// Whether a diff wraps its long lines. A switch apart from the editor's: a diff is read
		// side by side in half the width, where wrapping earns its keep in a way it may not in
		// the editor, and the reader turns it on or off from the diff's own toolbar.
		bool IsDiffWordWrap() const { return m_DiffWordWrap; }
		void SetDiffWordWrap(bool wrap);

		// The git change markers, in the gutter (with the tooltip over them) and on the
		// scrollbar. Two switches because they are two places on screen; the tooltip has no
		// switch of its own, since without the gutter's marks there is nothing to hover.
		bool HasGutterChangeMarkers() const { return m_GutterChangeMarkers; }
		bool HasScrollbarChangeMarkers() const { return m_ScrollbarChangeMarkers; }

		void SetGutterChangeMarkers(bool show);
		void SetScrollbarChangeMarkers(bool show);

		HoverSettings GetHoverSettings() const { return m_Hover; }

		void SetHoverInfo(bool show);
		void SetDefinitionPreview(DefinitionPreviewMode mode);
		// Clamped to the range the settings slider offers.
		void SetHoverDelayMs(int ms);

	private:
		void SetWordWrap(bool wrap);

		AppSettings& m_Store;

		static constexpr float kMinimumFontSize = 8.0f;
		static constexpr float kMaximumFontSize = 32.0f;
		static constexpr float kDefaultFontSize = 13.0f;

		static constexpr float kFontSizeStep = 1.0f;

		static constexpr std::size_t kMinimumIndentWidth = 1;
		static constexpr std::size_t kMaximumIndentWidth = 8;

		float m_FontSize = kDefaultFontSize;

		bool m_IndentWithSpaces = true;
		std::size_t m_IndentWidth = 4;

		bool m_WordWrap = false;
		bool m_DiffWordWrap = false;

		bool m_GutterChangeMarkers = true;
		bool m_ScrollbarChangeMarkers = true;

		static constexpr int kMinimumHoverDelayMs = 200;
		static constexpr int kMaximumHoverDelayMs = 1500;

		HoverSettings m_Hover;
	};
}