#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace core
{
    /// The rules from one `.gitignore` file, in file order. Order is part of the
    /// semantics: git applies the LAST matching rule, which is how a negation can
    /// un-ignore something an earlier pattern caught.
    class GitignoreMatcher
    {
    public:
        /// Parses the file's contents. Unparseable lines are skipped rather than
        /// reported: a malformed rule must not stop the tree from loading.
        static GitignoreMatcher Parse(std::string_view contents);

        /// `relativePath` is forward-slash separated and relative to the directory that
        /// holds the rules file. Git's format uses forward slashes on every platform.
        [[nodiscard]] bool IsIgnored(std::string_view relativePath, bool isDirectory) const;

        [[nodiscard]] bool IsEmpty() const noexcept { return m_patterns.empty(); }

    private:
        struct Pattern
        {
            std::string text;           // the pattern with its markers removed
            bool negated = false;       // began with '!'
            bool directoryOnly = false; // ended with '/'
            bool anchored = false;      // began with '/', or contains an inner '/'
        };

        std::vector<Pattern> m_patterns;
    };
}
