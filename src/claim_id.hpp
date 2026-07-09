// Generator for default claim ids: a UTC date/time stamp plus a short random
// suffix for uniqueness, so the id itself is legible without any tool and
// sorts in creation order (oldest to newest) alongside other claim files.
#pragma once

#include <string>

namespace rctx {

// e.g. "20260709-153245-a1b2c3".
std::string new_claim_id();

}  // namespace rctx
