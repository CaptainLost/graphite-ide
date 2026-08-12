#pragma once

#include "Core/Command/Chord.h"

namespace Core
{
    class CommandRegistry;
    class Keymap;

    /// Turns key presses and wheel gestures into commands. Stateless — a class rather
    /// than free functions only so that a future router with state (chord sequences,
    /// "waiting for the second key") has somewhere to keep it.
    class ShortcutRouter final
    {
    public:
        /// Every global chord in `keys`, once per frame. Must be called at a point where
        /// no window is open — after every Begin/End this frame — because that is what
        /// ImGuiInputFlags_RouteGlobal expects.
        void Dispatch(CommandRegistry& commands, const Keymap& keys);

        /// Chords scoped to `scope`, fired by whoever owns that scope from inside the
        /// window defining it. `wheel` is ImGuiIO::MouseWheel: its sign picks the
        /// direction and its magnitude is how many notches arrived this frame.
        void DispatchWheel(CommandRegistry& commands, const Keymap& keys, Chord::Scope scope, float wheel);
    };
}
