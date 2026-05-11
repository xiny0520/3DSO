#include "so3d/csv_writer.hpp"

#include <fstream>
#include <ostream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace so3d {

static std::string csv_escape(std::string_view value) {
    bool needs_quotes = false;
    for (char c : value) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) {
        return std::string(value);
    }

    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (char c : value) {
        if (c == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(c);
    }
    escaped.push_back('"');
    return escaped;
}

static constexpr const char* kCsvHeader =
    "PlotID,SourceFile,3DSO,NumPatterns,TotalBlocks,HR98,Nx,Ny,Nz,NumPoints,Status";

bool write_results_csv(
    const fs::path& output_csv,
    const std::vector<FileResult>& results,
    std::ostream& err
) {
    if (!output_csv.parent_path().empty()) {
        fs::create_directories(output_csv.parent_path());
    }

    std::ofstream fout(output_csv);
    if (!fout.is_open()) {
        err << "Failed to open output CSV: " << output_csv << "\n";
        return false;
    }

    fout << "\xEF\xBB\xBF";
    fout << kCsvHeader << "\n";
    fout << std::fixed;

    for (const auto& result : results) {
        fout << csv_escape(result.plot_id) << "," << csv_escape(result.source_file) << ","
             << result.DSO << ","
             << result.num_patterns << "," << result.total_blocks << ","
             << result.hr98 << ","
             << result.grid_nx << "," << result.grid_ny << "," << result.grid_nz << ","
             << result.num_points << "," << csv_escape(result.status);
        fout << "\n";
    }
    return true;
}

} // namespace so3d

