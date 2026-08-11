#pragma once

namespace graphite
{
    /// The version of the IDE itself. It lives here and not in Core because Core is a
    /// library that knows nothing about the product built on top of it; were Core ever
    /// versioned, that would be a different number.
    class Version
    {
    public:
        static const char* String() noexcept;
    };
}
