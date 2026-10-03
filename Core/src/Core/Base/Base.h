#pragma once

#include "Core/Base/Assert.h"
#include "Core/Base/Log.h"
#include "Core/Base/PlatformDetection.h"
#include "Core/Base/Pointers.h"

#include <utility>

// Binds a member function as an event handler without spelling out the signature at the
// call site, which is what lets EventDispatcher deduce the event type from the handler.
#define CORE_BIND_EVENT_FN(fn)                                  \
	[this](auto&&... args) -> decltype(auto) {                  \
		return this->fn(std::forward<decltype(args)>(args)...); \
	}
