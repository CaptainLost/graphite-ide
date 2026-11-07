#pragma once

#include <cstdint>
#include <string>

// The tags' part of the acceptance on a real repository (GRAPHITE_TAGS=1 beside GRAPHITE_LINES_REPO), run from its test
// case:
// - the tags listed, cold and with the cache, beside the branch listing they share a job with;
// - the line read with and without them from fresh stores: the model compared field by field, the commits only tags add
//   to the graph, and the time they add;
// - the first-tag pass, a tags-only change read again from the graph in hand, and the UI thread's share of that change;
// - the sidebar's rows with the Tags section open.
// With `sampleOutPath` it writes 200 commits spread over the graph and their first tag, one per line
// ("<commit>\t<full name or empty>\t<when>"), for a comparison with git for-each-ref --contains.
void RunTagAcceptance(const std::string& repositoryPath, std::int64_t now, const std::string& sampleOutPath);
