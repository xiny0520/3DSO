#pragma once

#include <cstddef>
#include <string>

namespace so3d {

struct FileResult {
    std::string plot_id;
    std::string source_file;
    double DSO = 0.0;
    int num_patterns = 0;
    int total_blocks = 0;
    double hr98 = 0.0;
    std::size_t num_points = 0;
    int grid_nx = 0;
    int grid_ny = 0;
    int grid_nz = 0;
    std::string status;
    double phase_read_s = 0.0;
    double phase_read_io_s = 0.0;
    double phase_read_decode_s = 0.0;
    double phase_read_percentile_s = 0.0;
    double phase_prep_s = 0.0;
    double phase_voxelize_s = 0.0;
    double phase_scan_blocks_s = 0.0;
    double phase_reduce_entropy_s = 0.0;
};

} // namespace so3d

