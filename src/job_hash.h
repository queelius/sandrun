#pragma once
#include <string>
#include <vector>

namespace sandrun {

// Represents a job definition with all parameters that affect job identity
struct JobDefinition {
    std::string entrypoint;
    std::string interpreter;
    std::string environment;
    std::vector<std::string> args;
    std::string code;  // entrypoint content

    // Calculate deterministic job hash from all job parameters
    // This hash uniquely identifies the job specification
    std::string calculate_hash() const;
};

} // namespace sandrun
