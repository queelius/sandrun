#include "job_hash.h"
#include "file_utils.h"
#include <sstream>

namespace sandrun {

std::string JobDefinition::calculate_hash() const {
    std::ostringstream job_data;
    job_data << entrypoint << "|"
             << interpreter << "|"
             << environment << "|";
    for (const auto& arg : args) {
        job_data << arg << "|";
    }
    job_data << code;
    return FileUtils::sha256_string(job_data.str());
}

} // namespace sandrun
