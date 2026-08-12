#pragma once

#include <string>
#include <string_view>

namespace Core
{
    /// Deliberately ASCII-only string operations — the byte a settings file hands back
    /// is not guaranteed to be anything more than that, and these helpers exist to be
    /// safe on any byte rather than correct on every locale.

    [[nodiscard]] std::string Lowered(std::string_view text);
    [[nodiscard]] std::string Trimmed(std::string_view text);
}
