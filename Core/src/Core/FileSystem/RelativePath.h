#pragma once

#include <string_view>

namespace Core {

	// Stricter than any one file system needs, on purpose: an archive or a record from the network names the
	// path, and it must not reach a drive, a stream, a device or a parent whatever platform reads it.
	bool IsSafeRelativePath(std::string_view path);
}
