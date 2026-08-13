#pragma once

#include "Core/Base/KeyCodes.h"

#include <vector>

namespace Core {

	// One key press exactly as it arrived, modifiers and all. The modifiers travel with the
	// stroke rather than being polled when it is read: a queue is drained after the fact,
	// and by then the keyboard may be holding something else entirely.
	struct KeyStroke
	{
		KeyCode Key = KeyCode::Space;
		KeyModifier Modifiers = KeyModifier::None;
		bool IsRepeat = false;
	};

	// The keyboard input nobody upstream consumed, held until something drawing this frame
	// can say it was meant for it. Events are dispatched the moment they arrive, which is
	// before any window has been submitted and so before it is known which one has focus;
	// a widget that only learns it has the keyboard while drawing needs the input to still
	// be there when it does.
	//
	// Owned by whoever clears it, once per frame, at the end of the frame — clearing at the
	// start would drop everything that arrived while the previous frame was being built.
	class FrameInput
	{
	public:
		void PushKey(KeyCode key, KeyModifier modifiers, bool isRepeat)
		{
			m_Strokes.push_back(KeyStroke{key, modifiers, isRepeat});
		}

		// A codepoint, not a key: what the layout and any dead key actually produced.
		void PushCharacter(unsigned int codepoint) { m_Characters.push_back(codepoint); }

		void Clear()
		{
			m_Strokes.clear();
			m_Characters.clear();
		}

		const std::vector<KeyStroke>& GetStrokes() const { return m_Strokes; }
		const std::vector<unsigned int>& GetCharacters() const { return m_Characters; }

	private:
		std::vector<KeyStroke> m_Strokes;
		std::vector<unsigned int> m_Characters;
	};
}
