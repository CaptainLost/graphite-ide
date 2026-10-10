#pragma once

#include <functional>

namespace Core {

	// How work reaches a worker. Injected rather than taken as a JobQueue so a test can run a job where it was
	// submitted and stay free of threads.
	using JobSubmitter = std::function<void(std::function<void()>)>;
}
