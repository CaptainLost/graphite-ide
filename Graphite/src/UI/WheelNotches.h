#pragma once

namespace Graphite {

	// A touchpad reports a fraction of a notch at a time: `wheel` is added to `pending`, the whole notches in it are
	// taken out and returned, and the fraction left waits in `pending` for the next turn.
	int TakeWholeNotches(float& pending, float wheel);
}
