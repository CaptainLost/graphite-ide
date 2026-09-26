#pragma once

#include "Core/Text/Editing.h"

#include <cstddef>

namespace Core {

	class CommandRegistry;
	class Keymap;
}
namespace Graphite {

	class EditorSettings final
	{
	public:
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

		// The git change markers, in the gutter (with the tooltip over them) and on the
		// scrollbar. Two switches because they are two places on screen; the tooltip has no
		// switch of its own, since without the gutter's marks there is nothing to hover.
		bool ShowsGutterChangeMarkers() const { return m_GutterChangeMarkers; }
		bool ShowsScrollbarChangeMarkers() const { return m_ScrollbarChangeMarkers; }

		void SetGutterChangeMarkers(bool show);
		void SetScrollbarChangeMarkers(bool show);

	private:
		void SetWordWrap(bool wrap);

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

		bool m_GutterChangeMarkers = true;
		bool m_ScrollbarChangeMarkers = true;
	};
}