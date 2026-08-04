#pragma once

#include <string>

// NfInstanceId (TS29571_CommonData.yaml) requires UUID version 4. Every NF needs one for itself;
// this lives in sbi-core rather than per-NF because it's a cross-cutting concern, not business
// logic specific to any one NF.

namespace sbi_core {

std::string generate_uuid_v4();

} // namespace sbi_core
