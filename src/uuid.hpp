// Generator for default claim ids: a UUID that sorts lexicographically by
// creation time, so plain filename listings (`ls .rctx/claims`) and JSON
// output order newest-first with no extra sort step.
#pragma once

#include <string>

namespace rctx {

// A version-8 (RFC 9562 "custom format") UUID: a 48-bit millisecond
// timestamp, inverted so later timestamps sort earlier as plain text, then
// random bits for uniqueness. Version/variant nibbles are standard, so it
// still round-trips through any UUID parser as syntactically valid.
std::string new_sortable_uuid();

}  // namespace rctx
