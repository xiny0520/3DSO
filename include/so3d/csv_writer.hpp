#pragma once

#include "so3d/results.hpp"

#include <filesystem>
#include <iosfwd>
#include <vector>

namespace so3d {

bool write_results_csv(
    const std::filesystem::path& output_csv,
    const std::vector<FileResult>& results,
    int layer_count,
    std::ostream& err
);

} // namespace so3d

