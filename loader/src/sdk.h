// SDK dump: every reflected class, struct and enum of the running game written as readable
// C++-like text (field types, offsets and sizes; functions with their parameters), so mod authors
// can look up names instead of guessing. Game thread (reads live reflection data).
#pragma once
#include <string>

namespace sdk {

struct Result {
    bool ok = false;
    int classes = 0, structs = 0, enums = 0;
    std::string path;   // file written
    std::string error;
};

// `filter`: empty = everything, otherwise only types whose path contains it (case-insensitive),
// e.g. "ShooterGame" or "PrimalCharacter". Writes <dir>/sdk.txt or <dir>/sdk_<filter>.txt.
Result Dump(const std::string& dir, const std::string& filter);

}  // namespace sdk
