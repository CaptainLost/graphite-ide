#include "corepch.h"

#include "Core/Fs/GitignoreMatcher.h"

namespace core
{
    namespace
    {
        /// Matches one path segment run against a pattern run. `*` and `?` never cross a
        /// separator; `**` is handled by the caller, which splits on it.
        bool MatchSegment(std::string_view pattern, std::string_view text)
        {
            std::size_t p = 0;
            std::size_t t = 0;
            std::size_t starPattern = std::string_view::npos;
            std::size_t starText = 0;

            while (t < text.size())
            {
                if (p < pattern.size() && pattern[p] == '[')
                {
                    const std::size_t close = pattern.find(']', p + 1);
                    if (close == std::string_view::npos)
                        return false;

                    const std::string_view set = pattern.substr(p + 1, close - p - 1);
                    bool matched = false;

                    for (std::size_t i = 0; i < set.size(); ++i)
                    {
                        // A dash between two characters is a range, unless it is first
                        // or last, where it stands for itself.
                        if (i + 2 < set.size() && set[i + 1] == '-')
                        {
                            if (text[t] >= set[i] && text[t] <= set[i + 2])
                                matched = true;

                            i += 2;
                            continue;
                        }

                        if (set[i] == text[t])
                            matched = true;
                    }

                    if (!matched)
                        return false;

                    p = close + 1;
                    ++t;
                    continue;
                }

                if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t]))
                {
                    ++p;
                    ++t;
                    continue;
                }

                if (p < pattern.size() && pattern[p] == '*')
                {
                    starPattern = p++;
                    starText = t;
                    continue;
                }

                if (starPattern != std::string_view::npos)
                {
                    // Backtrack: let the star swallow one more character.
                    p = starPattern + 1;
                    t = ++starText;
                    continue;
                }

                return false;
            }

            while (p < pattern.size() && pattern[p] == '*')
                ++p;

            return p == pattern.size();
        }

        /// Matches a whole path against a pattern, honouring `**` as the only construct
        /// allowed to cross separators.
        bool MatchPath(std::string_view pattern, std::string_view path)
        {
            const std::size_t doubleStar = pattern.find("**");

            if (doubleStar == std::string_view::npos)
            {
                // Segment counts must agree when no ** is present.
                std::size_t patternPos = 0;
                std::size_t pathPos = 0;

                while (true)
                {
                    const std::size_t patternEnd = pattern.find('/', patternPos);
                    const std::size_t pathEnd = path.find('/', pathPos);

                    const std::string_view patternPart = pattern.substr(
                        patternPos, patternEnd == std::string_view::npos
                                        ? std::string_view::npos
                                        : patternEnd - patternPos);
                    const std::string_view pathPart = path.substr(
                        pathPos,
                        pathEnd == std::string_view::npos ? std::string_view::npos
                                                          : pathEnd - pathPos);

                    if (!MatchSegment(patternPart, pathPart))
                        return false;

                    if (patternEnd == std::string_view::npos || pathEnd == std::string_view::npos)
                        return patternEnd == std::string_view::npos &&
                               pathEnd == std::string_view::npos;

                    patternPos = patternEnd + 1;
                    pathPos = pathEnd + 1;
                }
            }

            // Split once on the first **: the head must match a prefix, the tail must
            // match somewhere after it.
            std::string_view head = pattern.substr(0, doubleStar);
            std::string_view tail = pattern.substr(doubleStar + 2);

            if (!head.empty() && head.back() == '/')
                head.remove_suffix(1);

            if (!tail.empty() && tail.front() == '/')
                tail.remove_prefix(1);

            if (!head.empty())
            {
                if (path.size() < head.size() || !MatchPath(head, path.substr(0, head.size())))
                    return false;

                if (path.size() == head.size())
                    return tail.empty();

                if (path[head.size()] != '/')
                    return false;

                path.remove_prefix(head.size() + 1);
            }

            if (tail.empty())
                return true;

            // Try the tail against every suffix that starts at a segment boundary.
            std::size_t start = 0;
            while (true)
            {
                if (MatchPath(tail, path.substr(start)))
                    return true;

                const std::size_t next = path.find('/', start);
                if (next == std::string_view::npos)
                    return false;

                start = next + 1;
            }
        }

        std::string_view LastSegment(std::string_view path)
        {
            const std::size_t slash = path.rfind('/');
            return slash == std::string_view::npos ? path : path.substr(slash + 1);
        }
    }

    GitignoreMatcher GitignoreMatcher::Parse(std::string_view contents)
    {
        GitignoreMatcher matcher;

        std::size_t start = 0;
        while (start <= contents.size())
        {
            const std::size_t end = contents.find('\n', start);
            std::string line{contents.substr(
                start, end == std::string_view::npos ? std::string_view::npos : end - start)};

            start = end == std::string_view::npos ? contents.size() + 1 : end + 1;

            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            // Trailing spaces are dropped unless escaped. Walk the whole run backwards:
            // stopping at the first escaped space would leave the backslashes of the earlier
            // ones baked into the pattern.
            std::size_t trimEnd = line.size();
            while (trimEnd > 0 && line[trimEnd - 1] == ' ')
            {
                // An odd number of backslashes immediately before a space escapes it.
                std::size_t backslashes = 0;
                while (backslashes + 2 <= trimEnd && line[trimEnd - 2 - backslashes] == '\\')
                    ++backslashes;

                if (backslashes % 2 == 1)
                    break;

                --trimEnd;
            }

            line.erase(trimEnd);

            // An escaped space is a literal space, so drop the backslash that escaped it.
            std::size_t write = 0;
            for (std::size_t read = 0; read < line.size(); ++read)
            {
                if (line[read] == '\\' && read + 1 < line.size() && line[read + 1] == ' ')
                    continue;

                line[write++] = line[read];
            }

            line.erase(write);

            if (line.empty() || line.front() == '#')
                continue;

            Pattern pattern;

            if (line.front() == '!')
            {
                pattern.negated = true;
                line.erase(0, 1);
            }
            else if (line.size() >= 2 && line.front() == '\\' &&
                     (line[1] == '#' || line[1] == '!'))
            {
                line.erase(0, 1);
            }

            if (line.empty())
                continue;

            if (line.back() == '/')
            {
                pattern.directoryOnly = true;
                line.pop_back();
            }

            if (!line.empty() && line.front() == '/')
            {
                pattern.anchored = true;
                line.erase(0, 1);
            }
            else if (line.find('/') != std::string::npos)
            {
                // A slash anywhere but at the end anchors the pattern, per gitignore(5).
                pattern.anchored = true;
            }

            if (line.empty())
                continue;

            pattern.text = std::move(line);
            matcher.m_patterns.push_back(std::move(pattern));
        }

        return matcher;
    }

    bool GitignoreMatcher::IsIgnored(std::string_view relativePath, bool isDirectory) const
    {
        bool ignored = false;

        // Walk every pattern: the last one that matches decides, so an early match
        // cannot short-circuit a later negation.
        for (const Pattern& pattern : m_patterns)
        {
            if (pattern.directoryOnly && !isDirectory)
                continue;

            const bool matched = pattern.anchored
                                     ? MatchPath(pattern.text, relativePath)
                                     : MatchPath(pattern.text, LastSegment(relativePath)) ||
                                           MatchPath(pattern.text, relativePath);

            if (matched)
                ignored = !pattern.negated;
        }

        return ignored;
    }
}
