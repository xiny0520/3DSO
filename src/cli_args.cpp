#include "tdso/cli_args.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace tdso {

void print_usage(std::ostream& os) {
    os
        << "Usage: 3dso [options]\n"
        << "  --input-dir DIR        Input directory with .xyz, .txt, or .las files\n"
        << "  --output-csv FILE      Output CSV path\n"
        << "  --k-voxel INT          Block size, supported 3, 5, or 7 (default: 3; >3 is expensive)\n"
        << "  --voxel-size FLOAT     Voxel size in meters (default: 0.1)\n"
        << "  --block-ratio INT      Spatial entropy grid ratio (default: 5)\n"
        << "  --plot-size MODE X Y   Plot size: MODE 0=auto, 1=manual (default: 1 25 25)\n"
        << "  --layers INT           Split into N height layers, 0=off\n"
        << "  --verbose              Print per-file status lines during processing\n"
        << "  --profile-phases       Print aggregated phase timings after processing\n"
        << "  --threads INT          Thread count, 0=all\n"
        << "  --limit INT            Max file count, 0=all\n";
}

static int parse_int_arg(const std::string& value, const char* name) {
    try {
        std::size_t parsed = 0;
        const int result = std::stoi(value, &parsed);
        if (parsed != value.size()) {
            throw std::invalid_argument("trailing characters");
        }
        return result;
    } catch (const std::exception&) {
        throw std::runtime_error(std::string(name) + " expects an integer value");
    }
}

static double parse_double_arg(const std::string& value, const char* name) {
    try {
        std::size_t parsed = 0;
        const double result = std::stod(value, &parsed);
        if (parsed != value.size() || !std::isfinite(result)) {
            throw std::invalid_argument("invalid floating-point value");
        }
        return result;
    } catch (const std::exception&) {
        throw std::runtime_error(std::string(name) + " expects a finite numeric value");
    }
}

CliArgs parse_args(int argc, char* argv[]) {
    CliArgs args;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&](const char* option) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error(std::string(option) + " requires a value");
            }
            return argv[++i];
        };

        if (arg == "--input-dir") {
            args.input_dir = next("--input-dir");
        } else if (arg == "--output-csv") {
            args.output_csv = next("--output-csv");
        } else if (arg == "--k-voxel") {
            args.k_voxel = parse_int_arg(next("--k-voxel"), "--k-voxel");
        } else if (arg == "--voxel-size") {
            args.voxel_size = parse_double_arg(next("--voxel-size"), "--voxel-size");
        } else if (arg == "--block-ratio") {
            args.block_ratio = parse_int_arg(next("--block-ratio"), "--block-ratio");
        } else if (arg == "--plot-size") {
            args.plot_size_mode = parse_int_arg(next("--plot-size MODE"), "--plot-size MODE");
            args.plot_size_x = parse_double_arg(next("--plot-size X"), "--plot-size X");
            args.plot_size_y = parse_double_arg(next("--plot-size Y"), "--plot-size Y");
        } else if (arg == "--threads") {
            args.threads = parse_int_arg(next("--threads"), "--threads");
        } else if (arg == "--limit") {
            args.limit = parse_int_arg(next("--limit"), "--limit");
        } else if (arg == "--layers") {
            args.layers = parse_int_arg(next("--layers"), "--layers");
        } else if (arg == "--verbose") {
            args.verbose = true;
        } else if (arg == "--profile-phases") {
            args.profile_phases = true;
        } else if (arg == "--help" || arg == "-h") {
            args.show_help = true;
        } else {
            throw std::runtime_error("Unknown option: " + arg);
        }
    }
    return args;
}

} // namespace tdso

