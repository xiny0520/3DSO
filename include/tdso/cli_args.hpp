#pragma once

#include <filesystem>
#include <iosfwd>

namespace tdso {

struct CliArgs {
    std::filesystem::path input_dir = "data";
    std::filesystem::path output_csv = std::filesystem::path("results") / "3DSO_results.csv";
    int k_voxel = 3;
    double voxel_size = 0.3;
    int block_ratio = 5;
    int plot_size_mode = 1;
    double plot_size_x = 25.0;
    double plot_size_y = 25.0;
    int threads = 0;
    int limit = 0;
    int layers = 0;
    bool experimental_direct_las = false;
    bool experimental_packed_columns = false;
    bool disable_direct_packed_las = false;
    bool verbose = false;
    bool profile_phases = false;
    bool show_help = false;
};

void print_usage(std::ostream& os);
CliArgs parse_args(int argc, char* argv[]);

} // namespace tdso

