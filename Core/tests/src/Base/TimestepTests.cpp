#include <doctest/doctest.h>

#include "Core/App/FramePacer.h"
#include "Core/App/Redraw.h"
#include "Core/Base/Timestep.h"
#include "Core/FileSystem/ChangeCollector.h"
#include "Core/ImGui/ToastStack.h"

#include <chrono>
#include <type_traits>

TEST_CASE("Every clock Core keeps deadlines in is the one steady clock")
{
	static_assert(std::is_same_v<Core::SteadyClock, std::chrono::steady_clock>);
	static_assert(std::is_same_v<Core::FramePacer::Clock, Core::SteadyClock>);
	static_assert(std::is_same_v<Core::Redraw::Clock, Core::SteadyClock>);
	static_assert(std::is_same_v<Core::ChangeCollector::Clock, Core::SteadyClock>);
	static_assert(std::is_same_v<Core::ToastStack::Clock, Core::SteadyClock>);
	CHECK(Core::SteadyClock::is_steady);
}
