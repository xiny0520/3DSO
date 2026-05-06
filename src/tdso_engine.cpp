#include "tdso/cli_args.hpp"
#include "tdso/csv_writer.hpp"
#include "tdso/results.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <numeric>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#if defined(_MSC_VER)
#include <immintrin.h>
#include <intrin.h>
#endif

#if defined(DSO_HAS_OPENMP)
#include <omp.h>
#endif

namespace fs = std::filesystem;

namespace tdso {

struct RotationMap {
    std::array<int, 3> axes;
    std::array<int, 3> flips;
};

struct Point3f {
    float x;
    float y;
    float z;
};

struct VoxelGrid {
    std::vector<uint8_t> data;
    std::array<int, 3> dims{1, 1, 1};
    std::array<double, 3> grid_range{0.0, 0.0, 0.0};
    double range_x = 0.0;
    double range_y = 0.0;
    bool has_valid_points = false;
};

struct PackedColumnGrid {
    std::vector<uint64_t> words;
    std::array<int, 3> dims{1, 1, 1};
    int words_per_column = 1;
};

struct ReadResult {
    std::vector<Point3f> pts;
    double hr98 = 0.0;
    std::size_t num_points = 0;
    double read_io_s = 0.0;
    double read_decode_s = 0.0;
    double read_percentile_s = 0.0;
};

struct LasDirectResult {
    VoxelGrid vg;
    double hr98 = 0.0;
    std::size_t num_points = 0;
    bool ok = false;
};

struct PackedLasResult {
    PackedColumnGrid packed;
    std::array<double, 3> grid_range{0.0, 0.0, 0.0};
    double range_x = 0.0;
    double range_y = 0.0;
    double hr98 = 0.0;
    std::size_t num_points = 0;
    bool ok = false;
    double read_io_s = 0.0;
    double read_decode_s = 0.0;
    double read_pack_write_s = 0.0;
    double read_percentile_s = 0.0;
};

struct LasHeader {
    uint16_t header_size = 0;
    uint32_t offset_to_point_data = 0;
    uint8_t point_format_id = 0;
    uint16_t point_record_length = 0;
    uint64_t number_of_points = 0;
    double scale_x = 0.0;
    double scale_y = 0.0;
    double scale_z = 0.0;
    double offset_x = 0.0;
    double offset_y = 0.0;
    double offset_z = 0.0;
};

#if defined(_WIN32)
struct MappedFileView {
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;
    const char* data = nullptr;
    std::size_t size = 0;

    MappedFileView() = default;
    MappedFileView(const MappedFileView&) = delete;
    MappedFileView& operator=(const MappedFileView&) = delete;

    MappedFileView(MappedFileView&& other) noexcept
        : file(other.file), mapping(other.mapping), data(other.data), size(other.size) {
        other.file = INVALID_HANDLE_VALUE;
        other.mapping = nullptr;
        other.data = nullptr;
        other.size = 0;
    }

    MappedFileView& operator=(MappedFileView&& other) noexcept {
        if (this != &other) {
            close();
            file = other.file;
            mapping = other.mapping;
            data = other.data;
            size = other.size;
            other.file = INVALID_HANDLE_VALUE;
            other.mapping = nullptr;
            other.data = nullptr;
            other.size = 0;
        }
        return *this;
    }

    ~MappedFileView() {
        close();
    }

    void close() noexcept {
        if (data != nullptr) {
            UnmapViewOfFile(data);
            data = nullptr;
        }
        if (mapping != nullptr) {
            CloseHandle(mapping);
            mapping = nullptr;
        }
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
            file = INVALID_HANDLE_VALUE;
        }
        size = 0;
    }
};
#endif

struct PatternStats {
    int count = 0;
    int ones = 0;
    std::size_t cell_offset = std::numeric_limits<std::size_t>::max();
    std::vector<int> cell_counts;
    std::vector<std::array<double, 3>> coords;
};

struct PackedFingerprint {
    std::vector<uint64_t> words;

    bool operator==(const PackedFingerprint& other) const noexcept {
        return words == other.words;
    }

    bool operator<(const PackedFingerprint& other) const noexcept {
        return words < other.words;
    }
};

static constexpr std::array<RotationMap, 8> kRotationMaps = {
    RotationMap{{0, 1, 2}, {-1, -1, -1}}, RotationMap{{1, 0, 2}, {1, -1, -1}},
    RotationMap{{0, 1, 2}, {1, 1, -1}},   RotationMap{{1, 0, 2}, {-1, 1, -1}},
    RotationMap{{1, 0, 2}, {-1, -1, -1}}, RotationMap{{0, 1, 2}, {-1, 1, -1}},
    RotationMap{{1, 0, 2}, {1, 1, -1}},   RotationMap{{0, 1, 2}, {1, -1, -1}},
};

static constexpr int flatten3_constexpr(int x, int y, int z, int k) {
    return (x * k + y) * k + z;
}

static constexpr std::array<std::array<uint8_t, 9>, 8> build_k3_column_maps() {
    std::array<std::array<uint8_t, 9>, 8> maps{};
    constexpr int k = 3;
    for (std::size_t rid = 0; rid < kRotationMaps.size(); ++rid) {
        const RotationMap& rm = kRotationMaps[rid];
        for (int nx = 0; nx < k; ++nx) {
            for (int ny = 0; ny < k; ++ny) {
                int idx_new[3] = {nx, ny, 0};
                for (int d = 0; d < 3; ++d) {
                    if (rm.flips[d] != -1) {
                        idx_new[d] = k - 1 - idx_new[d];
                    }
                }
                int old_idx[3] = {0, 0, 0};
                for (int d = 0; d < 3; ++d) {
                    old_idx[rm.axes[d]] = idx_new[d];
                }
                const int dst_col = nx * k + ny;
                const int src_col = old_idx[0] * k + old_idx[1];
                maps[rid][static_cast<std::size_t>(dst_col)] = static_cast<uint8_t>(src_col);
            }
        }
    }
    return maps;
}

static constexpr std::array<std::array<uint8_t, 9>, 8> kRotationColumnMapsK3 = build_k3_column_maps();

struct PackedFingerprintHash {
    std::size_t operator()(const PackedFingerprint& fp) const noexcept {
        std::size_t h = 1469598103934665603ull;
        for (uint64_t w : fp.words) {
            h ^= static_cast<std::size_t>(w + 0x9e3779b97f4a7c15ull);
            h *= 1099511628211ull;
        }
        return h;
    }
};

struct FastBlockEncoder {
    int k = 0;
    int bits = 0;
    std::array<std::vector<int>, 8> maps;

    explicit FastBlockEncoder(int voxel_k) : k(voxel_k), bits(voxel_k * voxel_k * voxel_k) {
        for (std::size_t rid = 0; rid < kRotationMaps.size(); ++rid) {
            maps[rid].resize(static_cast<std::size_t>(bits));
            for (int nx = 0; nx < k; ++nx) {
                for (int ny = 0; ny < k; ++ny) {
                    for (int nz = 0; nz < k; ++nz) {
                        const RotationMap& rm = kRotationMaps[rid];
                        int idx_new[3] = {nx, ny, nz};
                        for (int d = 0; d < 3; ++d) {
                            if (rm.flips[d] != -1) {
                                idx_new[d] = k - 1 - idx_new[d];
                            }
                        }
                        int old_idx[3] = {0, 0, 0};
                        for (int d = 0; d < 3; ++d) {
                            old_idx[rm.axes[d]] = idx_new[d];
                        }
                        const int new_linear = (nx * k + ny) * k + nz;
                        const int old_linear = (old_idx[0] * k + old_idx[1]) * k + old_idx[2];
                        maps[rid][static_cast<std::size_t>(new_linear)] = old_linear;
                    }
                }
            }
        }
    }
};

static inline std::size_t flatten3(int x, int y, int z, const std::array<int, 3>& dims) {
    return static_cast<std::size_t>((x * dims[1] + y) * dims[2] + z);
}

static bool cpu_supports_avx2() {
#if defined(__AVX2__) && defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int cpu_info[4] = {0, 0, 0, 0};
    __cpuid(cpu_info, 1);
    const bool has_avx = (cpu_info[2] & (1 << 28)) != 0;
    const bool has_osxsave = (cpu_info[2] & (1 << 27)) != 0;
    if (!has_avx || !has_osxsave) {
        return false;
    }

    const unsigned long long xcr0 = _xgetbv(0);
    if ((xcr0 & 0x6) != 0x6) {
        return false;
    }

    __cpuidex(cpu_info, 7, 0);
    return (cpu_info[1] & (1 << 5)) != 0;
#else
    return false;
#endif
}

static bool kHasAvx2 = cpu_supports_avx2();

template <typename T>
static inline T load_le(const char* ptr) {
    T value{};
    std::memcpy(&value, ptr, sizeof(T));
    return value;
}

static inline Point3f decode_las_point3f(int32_t xi, int32_t yi, int32_t zi, const LasHeader& header) {
    return {
        static_cast<float>(static_cast<double>(xi) * header.scale_x + header.offset_x),
        static_cast<float>(static_cast<double>(yi) * header.scale_y + header.offset_y),
        static_cast<float>(static_cast<double>(zi) * header.scale_z + header.offset_z),
    };
}

#if defined(__AVX2__)
static inline void decode_las_xyz8_stride30_avx2_double(
    const char* base_ptr,
    const LasHeader& header,
    double* xs,
    double* ys,
    double* zs
) {
    static const __m256i kXOffsets = _mm256_setr_epi32(0, 30, 60, 90, 120, 150, 180, 210);
    static const __m256i kYOffsets = _mm256_setr_epi32(4, 34, 64, 94, 124, 154, 184, 214);
    static const __m256i kZOffsets = _mm256_setr_epi32(8, 38, 68, 98, 128, 158, 188, 218);

    const __m256i xi = _mm256_i32gather_epi32(reinterpret_cast<const int*>(base_ptr), kXOffsets, 1);
    const __m256i yi = _mm256_i32gather_epi32(reinterpret_cast<const int*>(base_ptr), kYOffsets, 1);
    const __m256i zi = _mm256_i32gather_epi32(reinterpret_cast<const int*>(base_ptr), kZOffsets, 1);

    const __m256d scale_x = _mm256_set1_pd(header.scale_x);
    const __m256d scale_y = _mm256_set1_pd(header.scale_y);
    const __m256d scale_z = _mm256_set1_pd(header.scale_z);
    const __m256d offset_x = _mm256_set1_pd(header.offset_x);
    const __m256d offset_y = _mm256_set1_pd(header.offset_y);
    const __m256d offset_z = _mm256_set1_pd(header.offset_z);

    auto decode4 = [](__m128i vi, const __m256d& scale, const __m256d& offset, double* out) {
        const __m256d vd = _mm256_add_pd(_mm256_mul_pd(_mm256_cvtepi32_pd(vi), scale), offset);
        _mm256_storeu_pd(out, vd);
    };

    decode4(_mm256_castsi256_si128(xi), scale_x, offset_x, xs);
    decode4(_mm256_extracti128_si256(xi, 1), scale_x, offset_x, xs + 4);
    decode4(_mm256_castsi256_si128(yi), scale_y, offset_y, ys);
    decode4(_mm256_extracti128_si256(yi, 1), scale_y, offset_y, ys + 4);
    decode4(_mm256_castsi256_si128(zi), scale_z, offset_z, zs);
    decode4(_mm256_extracti128_si256(zi, 1), scale_z, offset_z, zs + 4);
}

static inline void decode_las_xyz8_stride30_avx2(
    const char* base_ptr,
    const LasHeader& header,
    Point3f* out_pts,
    float* out_z
) {
    alignas(32) double xs64[8];
    alignas(32) double ys64[8];
    alignas(32) double zs64[8];
    decode_las_xyz8_stride30_avx2_double(base_ptr, header, xs64, ys64, zs64);

    for (int i = 0; i < 8; ++i) {
        const float x = static_cast<float>(xs64[i]);
        const float y = static_cast<float>(ys64[i]);
        const float z = static_cast<float>(zs64[i]);
        out_pts[i] = {x, y, z};
        out_z[i] = z;
    }
}
#endif

static double percentile(std::vector<float> values, double q) {
    if (values.empty()) {
        return 0.0;
    }
    q = std::clamp(q, 0.0, 1.0);
    const double pos = q * static_cast<double>(values.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(pos));
    const std::size_t hi = static_cast<std::size_t>(std::ceil(pos));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(lo), values.end());
    const float vlo = values[lo];
    if (hi == lo) {
        return static_cast<double>(vlo);
    }
    std::nth_element(values.begin() + static_cast<std::ptrdiff_t>(lo + 1), values.begin() + static_cast<std::ptrdiff_t>(hi), values.end());
    const float vhi = values[hi];
    const double w = pos - static_cast<double>(lo);
    return static_cast<double>(vlo) * (1.0 - w) + static_cast<double>(vhi) * w;
}

static inline const char* skip_ws(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == ',')) {
        ++p;
    }
    return p;
}

static inline const char* fast_parse_float(const char* p, const char* end, float& out) {
    p = skip_ws(p, end);
    if (p >= end) {
        return nullptr;
    }

    double sign = 1.0;
    if (*p == '-') {
        sign = -1.0;
        ++p;
    } else if (*p == '+') {
        ++p;
    }
    if (p >= end) {
        return nullptr;
    }

    double value = 0.0;
    bool has_digits = false;
    while (p < end && *p >= '0' && *p <= '9') {
        value = value * 10.0 + static_cast<double>(*p - '0');
        ++p;
        has_digits = true;
    }
    if (p < end && *p == '.') {
        ++p;
        double frac = 0.1;
        while (p < end && *p >= '0' && *p <= '9') {
            value += static_cast<double>(*p - '0') * frac;
            frac *= 0.1;
            ++p;
            has_digits = true;
        }
    }
    if (!has_digits) {
        return nullptr;
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
        ++p;
        int exp_sign = 1;
        if (p < end && *p == '-') {
            exp_sign = -1;
            ++p;
        } else if (p < end && *p == '+') {
            ++p;
        }
        int exponent = 0;
        while (p < end && *p >= '0' && *p <= '9') {
            exponent = exponent * 10 + (*p - '0');
            ++p;
        }
        value *= std::pow(10.0, static_cast<double>(exp_sign * exponent));
    }

    out = static_cast<float>(sign * value);
    return p;
}

static bool parse_las_header_bytes(const char* buf, std::size_t size, LasHeader& header) {
    if (size < 300) {
        return false;
    }
    if (std::strncmp(buf, "LASF", 4) != 0) {
        return false;
    }

    const uint8_t version_major = static_cast<uint8_t>(buf[24]);
    const uint8_t version_minor = static_cast<uint8_t>(buf[25]);
    header.header_size = load_le<uint16_t>(buf + 94);
    header.offset_to_point_data = load_le<uint32_t>(buf + 96);
    header.point_format_id = static_cast<uint8_t>(buf[104]) & 0x3Fu;
    header.point_record_length = load_le<uint16_t>(buf + 105);
    header.scale_x = load_le<double>(buf + 131);
    header.scale_y = load_le<double>(buf + 139);
    header.scale_z = load_le<double>(buf + 147);
    header.offset_x = load_le<double>(buf + 155);
    header.offset_y = load_le<double>(buf + 163);
    header.offset_z = load_le<double>(buf + 171);

    const uint32_t legacy_count = load_le<uint32_t>(buf + 107);
    header.number_of_points = legacy_count;
    if ((version_major > 1 || (version_major == 1 && version_minor >= 4)) && header.header_size >= 255 && size >= 255) {
        const uint64_t extended_count = load_le<uint64_t>(buf + 247);
        if (extended_count > 0) {
            header.number_of_points = extended_count;
        }
    }
    if (header.point_record_length == 0) {
        header.point_record_length = 30;
    }
    return true;
}

static bool read_las_header(std::ifstream& fin, LasHeader& header) {
    std::array<char, 300> buf{};
    fin.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    if (!fin || fin.gcount() < static_cast<std::streamsize>(buf.size())) {
        return false;
    }
    return parse_las_header_bytes(buf.data(), buf.size(), header);
}

#if defined(_WIN32)
static bool try_map_file_readonly(const fs::path& path, MappedFileView& view) {
    view.file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr
    );
    if (view.file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER file_size{};
    if (!GetFileSizeEx(view.file, &file_size) || file_size.QuadPart <= 0) {
        return false;
    }
    view.size = static_cast<std::size_t>(file_size.QuadPart);

    view.mapping = CreateFileMappingW(view.file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (view.mapping == nullptr) {
        return false;
    }

    view.data = static_cast<const char*>(MapViewOfFile(view.mapping, FILE_MAP_READ, 0, 0, 0));
    return view.data != nullptr;
}

static bool try_read_las_mapped(const fs::path& path, ReadResult& rr) {
    using Clock = std::chrono::steady_clock;
    MappedFileView view;
    const auto io_start = Clock::now();
    if (!try_map_file_readonly(path, view)) {
        return false;
    }
    rr.read_io_s += std::chrono::duration<double>(Clock::now() - io_start).count();

    LasHeader header;
    if (!parse_las_header_bytes(view.data, view.size, header)) {
        return false;
    }

    const std::size_t num_points = static_cast<std::size_t>(header.number_of_points);
    if (num_points == 0 || num_points > 100000000ull) {
        return true;
    }

    const std::size_t record_size = static_cast<std::size_t>(header.point_record_length);
    const std::size_t point_data_offset = static_cast<std::size_t>(header.offset_to_point_data);
    if (record_size == 0 || point_data_offset > view.size) {
        return false;
    }

    const std::size_t max_records_by_size = (view.size - point_data_offset) / record_size;
    const std::size_t records_to_decode = std::min(num_points, max_records_by_size);
    if (records_to_decode == 0) {
        return true;
    }

    rr.pts.resize(records_to_decode);
    std::vector<float> z_vals(records_to_decode);
    const bool use_avx2_stride30 = kHasAvx2 && record_size == 30;
    const char* record_ptr = view.data + point_data_offset;

    const auto decode_start = Clock::now();
    std::size_t i = 0;
#if defined(__AVX2__)
    if (use_avx2_stride30) {
        for (; i + 8 <= records_to_decode; i += 8) {
            decode_las_xyz8_stride30_avx2(
                record_ptr + i * record_size,
                header,
                rr.pts.data() + i,
                z_vals.data() + i
            );
        }
    }
#endif
    for (; i < records_to_decode; ++i) {
        const char* point_ptr = record_ptr + i * record_size;
        const int32_t xi = load_le<int32_t>(point_ptr);
        const int32_t yi = load_le<int32_t>(point_ptr + 4);
        const int32_t zi = load_le<int32_t>(point_ptr + 8);

        const float x = static_cast<float>(static_cast<double>(xi) * header.scale_x + header.offset_x);
        const float y = static_cast<float>(static_cast<double>(yi) * header.scale_y + header.offset_y);
        const float z = static_cast<float>(static_cast<double>(zi) * header.scale_z + header.offset_z);
        rr.pts[i] = {x, y, z};
        z_vals[i] = z;
    }
    rr.read_decode_s += std::chrono::duration<double>(Clock::now() - decode_start).count();

    rr.num_points = rr.pts.size();
    const auto percentile_start = Clock::now();
    rr.hr98 = percentile(std::move(z_vals), 0.98);
    rr.read_percentile_s = std::chrono::duration<double>(Clock::now() - percentile_start).count();
    return true;
}
#endif

static ReadResult read_las_streamed(const fs::path& path) {
    using Clock = std::chrono::steady_clock;
    ReadResult rr;
    std::vector<char> stream_buffer(1 << 20);
    std::ifstream fin(path, std::ios::binary);
    if (!fin.is_open()) {
        return rr;
    }
    fin.rdbuf()->pubsetbuf(stream_buffer.data(), static_cast<std::streamsize>(stream_buffer.size()));

    LasHeader header;
    if (!read_las_header(fin, header)) {
        return rr;
    }

    const std::size_t num_points = static_cast<std::size_t>(header.number_of_points);
    if (num_points == 0 || num_points > 100000000ull) {
        return rr;
    }

    rr.pts.resize(num_points);
    std::vector<float> z_vals(num_points);

    fin.seekg(header.offset_to_point_data);
    const std::size_t record_size = static_cast<std::size_t>(header.point_record_length);
    const std::size_t target_chunk_bytes = 8ull * 1024ull * 1024ull;
    const std::size_t records_per_chunk = std::max<std::size_t>(1, target_chunk_bytes / record_size);
    std::vector<char> chunk_buffer(records_per_chunk * record_size);
    const bool use_avx2_stride30 = kHasAvx2 && record_size == 30;

    std::size_t points_remaining = num_points;
    std::size_t out_count = 0;
    while (points_remaining > 0) {
        const std::size_t records_to_read = std::min(points_remaining, records_per_chunk);
        const std::size_t bytes_to_read = records_to_read * record_size;
        const auto io_start = Clock::now();
        fin.read(chunk_buffer.data(), static_cast<std::streamsize>(bytes_to_read));
        rr.read_io_s += std::chrono::duration<double>(Clock::now() - io_start).count();
        const std::streamsize bytes_read = fin.gcount();
        if (bytes_read <= 0) {
            break;
        }

        const std::size_t records_read = static_cast<std::size_t>(bytes_read) / record_size;
        const char* record_ptr = chunk_buffer.data();
        std::size_t i = 0;
        const auto decode_start = Clock::now();

#if defined(__AVX2__)
        if (use_avx2_stride30) {
            for (; i + 8 <= records_read; i += 8) {
                decode_las_xyz8_stride30_avx2(
                    record_ptr + i * record_size,
                    header,
                    rr.pts.data() + out_count + i,
                    z_vals.data() + out_count + i
                );
            }
        }
#endif

        for (; i < records_read; ++i) {
            const char* point_ptr = record_ptr + i * record_size;
            const int32_t xi = load_le<int32_t>(point_ptr);
            const int32_t yi = load_le<int32_t>(point_ptr + 4);
            const int32_t zi = load_le<int32_t>(point_ptr + 8);

            const Point3f p = decode_las_point3f(xi, yi, zi, header);
            rr.pts[out_count + i] = p;
            z_vals[out_count + i] = p.z;
        }
        rr.read_decode_s += std::chrono::duration<double>(Clock::now() - decode_start).count();
        out_count += records_read;

        if (records_read < records_to_read) {
            break;
        }
        points_remaining -= records_read;
        if (!fin) {
            break;
        }
    }
    rr.pts.resize(out_count);
    z_vals.resize(out_count);
    rr.num_points = rr.pts.size();
    const auto percentile_start = Clock::now();
    rr.hr98 = percentile(std::move(z_vals), 0.98);
    rr.read_percentile_s = std::chrono::duration<double>(Clock::now() - percentile_start).count();
    return rr;
}

static ReadResult read_las(const fs::path& path) {
#if defined(_WIN32)
    ReadResult mapped_rr;
    if (try_read_las_mapped(path, mapped_rr)) {
        return mapped_rr;
    }
#endif
    return read_las_streamed(path);
}

template <typename PointCallback>
static std::size_t for_each_las_point_chunked(
    std::ifstream& fin,
    const LasHeader& header,
    std::vector<char>& chunk_buffer,
    std::size_t max_points,
    PointCallback&& callback
) {
    const std::size_t record_size = static_cast<std::size_t>(header.point_record_length);
    if (record_size == 0) {
        return 0;
    }

    const std::size_t records_per_chunk = chunk_buffer.size() / record_size;
    if (records_per_chunk == 0) {
        return 0;
    }

    std::size_t processed = 0;
    while (processed < max_points) {
        const std::size_t records_to_read = std::min(max_points - processed, records_per_chunk);
        const std::size_t bytes_to_read = records_to_read * record_size;
        fin.read(chunk_buffer.data(), static_cast<std::streamsize>(bytes_to_read));
        const std::streamsize bytes_read = fin.gcount();
        if (bytes_read <= 0) {
            break;
        }

        const std::size_t records_read = static_cast<std::size_t>(bytes_read) / record_size;
        const char* record_ptr = chunk_buffer.data();
        for (std::size_t i = 0; i < records_read; ++i, record_ptr += record_size) {
            const int32_t xi = load_le<int32_t>(record_ptr);
            const int32_t yi = load_le<int32_t>(record_ptr + 4);
            const int32_t zi = load_le<int32_t>(record_ptr + 8);
            callback(xi, yi, zi);
        }
        processed += records_read;
        if (records_read < records_to_read || !fin) {
            break;
        }
    }
    return processed;
}

static LasDirectResult read_las_direct_to_voxel_grid(
    const fs::path& path,
    double voxel_size,
    int plot_size_mode,
    double plot_size_x,
    double plot_size_y
) {
    LasDirectResult result;
    std::vector<char> stream_buffer(1 << 20);
    std::ifstream fin(path, std::ios::binary);
    if (!fin.is_open()) {
        return result;
    }
    fin.rdbuf()->pubsetbuf(stream_buffer.data(), static_cast<std::streamsize>(stream_buffer.size()));

    LasHeader header;
    if (!read_las_header(fin, header)) {
        return result;
    }

    const std::size_t header_points = static_cast<std::size_t>(header.number_of_points);
    if (header_points == 0 || header_points > 100000000ull) {
        return result;
    }

    const std::size_t record_size = static_cast<std::size_t>(header.point_record_length);
    const std::size_t target_chunk_bytes = 8ull * 1024ull * 1024ull;
    const std::size_t records_per_chunk = std::max<std::size_t>(1, target_chunk_bytes / record_size);
    std::vector<char> chunk_buffer(records_per_chunk * record_size);
    std::vector<float> z_vals;
    z_vals.reserve(header_points);

    double min_x = std::numeric_limits<double>::max();
    double min_y = std::numeric_limits<double>::max();
    double min_z = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    double max_y = std::numeric_limits<double>::lowest();
    double max_z = std::numeric_limits<double>::lowest();

    fin.seekg(header.offset_to_point_data);
    const std::size_t first_pass_points = for_each_las_point_chunked(
        fin, header, chunk_buffer, header_points,
        [&](int32_t xi, int32_t yi, int32_t zi) {
            const Point3f p = decode_las_point3f(xi, yi, zi, header);
            min_x = std::min(min_x, static_cast<double>(p.x));
            min_y = std::min(min_y, static_cast<double>(p.y));
            min_z = std::min(min_z, static_cast<double>(p.z));
            max_x = std::max(max_x, static_cast<double>(p.x));
            max_y = std::max(max_y, static_cast<double>(p.y));
            max_z = std::max(max_z, static_cast<double>(p.z));
            z_vals.push_back(p.z);
        }
    );

    if (first_pass_points == 0) {
        return result;
    }

    result.num_points = first_pass_points;
    result.hr98 = percentile(std::move(z_vals), 0.98);

    const double range_x = (plot_size_mode == 0) ? (max_x - min_x) : plot_size_x;
    const double range_y = (plot_size_mode == 0) ? (max_y - min_y) : plot_size_y;
    if (range_x <= 0.0 || range_y <= 0.0) {
        return result;
    }

    VoxelGrid vg;
    vg.range_x = range_x;
    vg.range_y = range_y;
    vg.grid_range = {range_x, range_y, std::max(0.0, max_z - min_z)};
    vg.dims = {
        std::max(1, static_cast<int>(std::ceil(vg.grid_range[0] / voxel_size))),
        std::max(1, static_cast<int>(std::ceil(vg.grid_range[1] / voxel_size))),
        std::max(1, static_cast<int>(std::ceil(vg.grid_range[2] / voxel_size))),
    };
    vg.data.assign(static_cast<std::size_t>(vg.dims[0]) * vg.dims[1] * vg.dims[2], 0);
    fin.clear();
    fin.seekg(header.offset_to_point_data);
    for_each_las_point_chunked(
        fin, header, chunk_buffer, first_pass_points,
        [&](int32_t xi, int32_t yi, int32_t zi) {
            const Point3f p = decode_las_point3f(xi, yi, zi, header);
            const double tx = static_cast<double>(p.x) - min_x;
            const double ty = static_cast<double>(p.y) - min_y;
            const double tz = static_cast<double>(p.z) - min_z;
            if (!(tx >= 0.0 && tx < range_x && ty >= 0.0 && ty < range_y)) {
                return;
            }

            int ix = static_cast<int>(std::floor(tx / voxel_size));
            int iy = static_cast<int>(std::floor(ty / voxel_size));
            int iz = static_cast<int>(std::floor(tz / voxel_size));
            ix = std::clamp(ix, 0, vg.dims[0] - 1);
            iy = std::clamp(iy, 0, vg.dims[1] - 1);
            iz = std::clamp(iz, 0, vg.dims[2] - 1);
            vg.data[flatten3(ix, iy, iz, vg.dims)] = 1;
            vg.has_valid_points = true;
        }
    );

    result.vg = std::move(vg);
    result.ok = result.vg.has_valid_points;
    return result;
}

#if defined(_WIN32)
static PackedLasResult read_las_direct_to_packed_grid_mapped(
    const fs::path& path,
    double voxel_size,
    int plot_size_mode,
    double plot_size_x,
    double plot_size_y
) {
    using Clock = std::chrono::steady_clock;
    PackedLasResult result;
    MappedFileView view;
    const auto io_start = Clock::now();
    if (!try_map_file_readonly(path, view)) {
        return result;
    }
    result.read_io_s += std::chrono::duration<double>(Clock::now() - io_start).count();

    LasHeader header;
    if (!parse_las_header_bytes(view.data, view.size, header)) {
        return result;
    }

    const std::size_t num_points = static_cast<std::size_t>(header.number_of_points);
    const std::size_t record_size = static_cast<std::size_t>(header.point_record_length);
    const std::size_t point_data_offset = static_cast<std::size_t>(header.offset_to_point_data);
    if (num_points == 0 || num_points > 100000000ull || record_size == 0 || point_data_offset > view.size) {
        return result;
    }

    const std::size_t records_available = (view.size - point_data_offset) / record_size;
    const std::size_t records_to_decode = std::min(num_points, records_available);
    if (records_to_decode == 0) {
        return result;
    }

    const char* record_ptr = view.data + point_data_offset;
    std::vector<float> z_vals;
    z_vals.reserve(records_to_decode);

    double min_x = std::numeric_limits<double>::max();
    double min_y = std::numeric_limits<double>::max();
    double min_z = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    double max_y = std::numeric_limits<double>::lowest();
    double max_z = std::numeric_limits<double>::lowest();

    const auto decode_start = Clock::now();
    for (std::size_t i = 0; i < records_to_decode; ++i) {
        const char* point_ptr = record_ptr + i * record_size;
        const int32_t xi = load_le<int32_t>(point_ptr);
        const int32_t yi = load_le<int32_t>(point_ptr + 4);
        const int32_t zi = load_le<int32_t>(point_ptr + 8);
        const Point3f p = decode_las_point3f(xi, yi, zi, header);
        min_x = std::min(min_x, static_cast<double>(p.x));
        min_y = std::min(min_y, static_cast<double>(p.y));
        min_z = std::min(min_z, static_cast<double>(p.z));
        max_x = std::max(max_x, static_cast<double>(p.x));
        max_y = std::max(max_y, static_cast<double>(p.y));
        max_z = std::max(max_z, static_cast<double>(p.z));
        z_vals.push_back(p.z);
    }
    result.read_decode_s += std::chrono::duration<double>(Clock::now() - decode_start).count();

    result.num_points = records_to_decode;
    const auto percentile_start = Clock::now();
    result.hr98 = percentile(std::move(z_vals), 0.98);
    result.read_percentile_s = std::chrono::duration<double>(Clock::now() - percentile_start).count();
    result.range_x = (plot_size_mode == 0)
        ? (max_x - min_x)
        : plot_size_x;
    result.range_y = (plot_size_mode == 0)
        ? (max_y - min_y)
        : plot_size_y;
    if (result.range_x <= 0.0 || result.range_y <= 0.0) {
        return result;
    }

    result.grid_range = {
        result.range_x,
        result.range_y,
        std::max(0.0, max_z - min_z)
    };
    result.packed.dims = {
        std::max(1, static_cast<int>(std::ceil(result.grid_range[0] / voxel_size))),
        std::max(1, static_cast<int>(std::ceil(result.grid_range[1] / voxel_size))),
        std::max(1, static_cast<int>(std::ceil(result.grid_range[2] / voxel_size))),
    };
    result.packed.words_per_column = std::max(1, (result.packed.dims[2] + 63) / 64);
    result.packed.words.assign(
        static_cast<std::size_t>(result.packed.dims[0]) * result.packed.dims[1] * result.packed.words_per_column,
        0ull
    );

    const auto pack_start = Clock::now();
    for (std::size_t i = 0; i < records_to_decode; ++i) {
        const char* point_ptr = record_ptr + i * record_size;
        const int32_t xi = load_le<int32_t>(point_ptr);
        const int32_t yi = load_le<int32_t>(point_ptr + 4);
        const int32_t zi = load_le<int32_t>(point_ptr + 8);
        const Point3f p = decode_las_point3f(xi, yi, zi, header);
        const double tx = static_cast<double>(p.x) - min_x;
        const double ty = static_cast<double>(p.y) - min_y;
        const double tz = static_cast<double>(p.z) - min_z;
        if (!(tx >= 0.0 && tx < result.range_x && ty >= 0.0 && ty < result.range_y)) {
            continue;
        }

        int ix = static_cast<int>(std::floor(tx / voxel_size));
        int iy = static_cast<int>(std::floor(ty / voxel_size));
        int iz = static_cast<int>(std::floor(tz / voxel_size));
        ix = std::clamp(ix, 0, result.packed.dims[0] - 1);
        iy = std::clamp(iy, 0, result.packed.dims[1] - 1);
        iz = std::clamp(iz, 0, result.packed.dims[2] - 1);
        const std::size_t word_base =
            (static_cast<std::size_t>(ix) * result.packed.dims[1] + static_cast<std::size_t>(iy)) *
            static_cast<std::size_t>(result.packed.words_per_column);
        result.packed.words[word_base + static_cast<std::size_t>(iz / 64)] |= 1ull << (iz % 64);
        result.ok = true;
    }
    result.read_pack_write_s += std::chrono::duration<double>(Clock::now() - pack_start).count();

    return result;
}
#endif

static PackedLasResult read_las_direct_to_packed_grid(
    const fs::path& path,
    double voxel_size,
    int plot_size_mode,
    double plot_size_x,
    double plot_size_y
) {
#if defined(_WIN32)
    PackedLasResult mapped = read_las_direct_to_packed_grid_mapped(
        path, voxel_size, plot_size_mode, plot_size_x, plot_size_y
    );
    if (mapped.num_points > 0 || mapped.ok) {
        return mapped;
    }
#endif
    using Clock = std::chrono::steady_clock;
    PackedLasResult result;
    std::vector<char> stream_buffer(1 << 20);
    std::ifstream fin(path, std::ios::binary);
    if (!fin.is_open()) {
        return result;
    }
    fin.rdbuf()->pubsetbuf(stream_buffer.data(), static_cast<std::streamsize>(stream_buffer.size()));

    LasHeader header;
    if (!read_las_header(fin, header)) {
        return result;
    }

    const std::size_t header_points = static_cast<std::size_t>(header.number_of_points);
    if (header_points == 0 || header_points > 100000000ull) {
        return result;
    }

    const std::size_t record_size = static_cast<std::size_t>(header.point_record_length);
    const std::size_t target_chunk_bytes = 8ull * 1024ull * 1024ull;
    const std::size_t records_per_chunk = std::max<std::size_t>(1, target_chunk_bytes / record_size);
    std::vector<char> chunk_buffer(records_per_chunk * record_size);
    std::vector<float> z_vals;
    z_vals.reserve(header_points);

    double min_x = std::numeric_limits<double>::max();
    double min_y = std::numeric_limits<double>::max();
    double min_z = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    double max_y = std::numeric_limits<double>::lowest();
    double max_z = std::numeric_limits<double>::lowest();

    fin.seekg(header.offset_to_point_data);
    const auto decode_start = Clock::now();
    const std::size_t first_pass_points = for_each_las_point_chunked(
        fin, header, chunk_buffer, header_points,
        [&](int32_t xi, int32_t yi, int32_t zi) {
            const Point3f p = decode_las_point3f(xi, yi, zi, header);
            min_x = std::min(min_x, static_cast<double>(p.x));
            min_y = std::min(min_y, static_cast<double>(p.y));
            min_z = std::min(min_z, static_cast<double>(p.z));
            max_x = std::max(max_x, static_cast<double>(p.x));
            max_y = std::max(max_y, static_cast<double>(p.y));
            max_z = std::max(max_z, static_cast<double>(p.z));
            z_vals.push_back(p.z);
        }
    );
    result.read_decode_s += std::chrono::duration<double>(Clock::now() - decode_start).count();

    if (first_pass_points == 0) {
        return result;
    }

    result.num_points = first_pass_points;
    const auto percentile_start = Clock::now();
    result.hr98 = percentile(std::move(z_vals), 0.98);
    result.read_percentile_s = std::chrono::duration<double>(Clock::now() - percentile_start).count();
    result.range_x = (plot_size_mode == 0)
        ? (max_x - min_x)
        : plot_size_x;
    result.range_y = (plot_size_mode == 0)
        ? (max_y - min_y)
        : plot_size_y;
    if (result.range_x <= 0.0 || result.range_y <= 0.0) {
        return result;
    }

    result.grid_range = {
        result.range_x,
        result.range_y,
        std::max(0.0, max_z - min_z)
    };
    result.packed.dims = {
        std::max(1, static_cast<int>(std::ceil(result.grid_range[0] / voxel_size))),
        std::max(1, static_cast<int>(std::ceil(result.grid_range[1] / voxel_size))),
        std::max(1, static_cast<int>(std::ceil(result.grid_range[2] / voxel_size))),
    };
    result.packed.words_per_column = std::max(1, (result.packed.dims[2] + 63) / 64);
    result.packed.words.assign(
        static_cast<std::size_t>(result.packed.dims[0]) * result.packed.dims[1] * result.packed.words_per_column,
        0ull
    );

    fin.clear();
    fin.seekg(header.offset_to_point_data);
    const auto pack_start = Clock::now();
    for_each_las_point_chunked(
        fin, header, chunk_buffer, first_pass_points,
        [&](int32_t xi, int32_t yi, int32_t zi) {
            const Point3f p = decode_las_point3f(xi, yi, zi, header);
            const double tx = static_cast<double>(p.x) - min_x;
            const double ty = static_cast<double>(p.y) - min_y;
            const double tz = static_cast<double>(p.z) - min_z;
            if (!(tx >= 0.0 && tx < result.range_x && ty >= 0.0 && ty < result.range_y)) {
                return;
            }

            int ix = static_cast<int>(std::floor(tx / voxel_size));
            int iy = static_cast<int>(std::floor(ty / voxel_size));
            int iz = static_cast<int>(std::floor(tz / voxel_size));
            ix = std::clamp(ix, 0, result.packed.dims[0] - 1);
            iy = std::clamp(iy, 0, result.packed.dims[1] - 1);
            iz = std::clamp(iz, 0, result.packed.dims[2] - 1);
            const std::size_t word_base =
                (static_cast<std::size_t>(ix) * result.packed.dims[1] + static_cast<std::size_t>(iy)) *
                static_cast<std::size_t>(result.packed.words_per_column);
            result.packed.words[word_base + static_cast<std::size_t>(iz / 64)] |= 1ull << (iz % 64);
            result.ok = true;
        }
    );
    result.read_pack_write_s += std::chrono::duration<double>(Clock::now() - pack_start).count();

    return result;
}

static ReadResult read_points(const fs::path& path) {
    using Clock = std::chrono::steady_clock;
    ReadResult rr;
    std::ifstream fin(path, std::ios::binary | std::ios::ate);
    if (!fin.is_open()) {
        return rr;
    }

    const auto size = fin.tellg();
    if (size <= 0) {
        return rr;
    }
    fin.seekg(0);

    std::string buf(static_cast<std::size_t>(size), '\0');
    const auto io_start = Clock::now();
    fin.read(buf.data(), size);
    rr.read_io_s = std::chrono::duration<double>(Clock::now() - io_start).count();
    if (!fin && !fin.eof()) {
        return rr;
    }

    rr.pts.reserve(buf.size() / 24);
    std::vector<float> z_vals;
    z_vals.reserve(buf.size() / 24);

    const char* scan = buf.data();
    const char* end = scan + buf.size();
    const auto decode_start = Clock::now();
    while (scan < end) {
        while (scan < end && (*scan == '\r' || *scan == '\n')) {
            ++scan;
        }
        if (scan >= end) {
            break;
        }

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        const char* r = fast_parse_float(scan, end, x);
        if (!r) {
            break;
        }
        r = fast_parse_float(r, end, y);
        if (!r) {
            break;
        }
        r = fast_parse_float(r, end, z);
        if (!r) {
            break;
        }
        rr.pts.push_back({x, y, z});
        z_vals.push_back(z);
        while (r < end && *r != '\n' && *r != '\r') {
            ++r;
        }
        scan = r;
    }
    rr.read_decode_s = std::chrono::duration<double>(Clock::now() - decode_start).count();

    rr.num_points = rr.pts.size();
    const auto percentile_start = Clock::now();
    rr.hr98 = percentile(std::move(z_vals), 0.98);
    rr.read_percentile_s = std::chrono::duration<double>(Clock::now() - percentile_start).count();
    return rr;
}

static VoxelGrid points_to_voxel_grid(const std::vector<Point3f>& points, double voxel_size, double range_x, double range_y) {
    VoxelGrid vg;
    vg.range_x = range_x;
    vg.range_y = range_y;
    vg.grid_range = {range_x, range_y, 0.0};

    if (points.empty()) {
        vg.data.assign(1, 0);
        return vg;
    }

    double min_x = std::numeric_limits<double>::max();
    double min_y = std::numeric_limits<double>::max();
    double min_z = std::numeric_limits<double>::max();
    double max_z = std::numeric_limits<double>::lowest();

    for (const auto& p : points) {
        min_x = std::min(min_x, static_cast<double>(p.x));
        min_y = std::min(min_y, static_cast<double>(p.y));
        min_z = std::min(min_z, static_cast<double>(p.z));
        max_z = std::max(max_z, static_cast<double>(p.z));
    }

    vg.grid_range[2] = std::max(0.0, max_z - min_z);
    vg.dims = {
        std::max(1, static_cast<int>(std::ceil(vg.grid_range[0] / voxel_size))),
        std::max(1, static_cast<int>(std::ceil(vg.grid_range[1] / voxel_size))),
        std::max(1, static_cast<int>(std::ceil(vg.grid_range[2] / voxel_size))),
    };
    vg.data.assign(static_cast<std::size_t>(vg.dims[0]) * vg.dims[1] * vg.dims[2], 0);

    for (const auto& p : points) {
        const double tx = static_cast<double>(p.x) - min_x;
        const double ty = static_cast<double>(p.y) - min_y;
        const double tz = static_cast<double>(p.z) - min_z;
        if (!(tx >= 0.0 && tx < range_x && ty >= 0.0 && ty < range_y)) {
            continue;
        }

        int ix = static_cast<int>(std::floor(tx / voxel_size));
        int iy = static_cast<int>(std::floor(ty / voxel_size));
        int iz = static_cast<int>(std::floor(tz / voxel_size));
        ix = std::clamp(ix, 0, vg.dims[0] - 1);
        iy = std::clamp(iy, 0, vg.dims[1] - 1);
        iz = std::clamp(iz, 0, vg.dims[2] - 1);
        vg.data[flatten3(ix, iy, iz, vg.dims)] = 1;
        vg.has_valid_points = true;
    }
    return vg;
}

static PackedColumnGrid build_packed_column_grid(const VoxelGrid& vg) {
    PackedColumnGrid packed;
    packed.dims = vg.dims;
    packed.words_per_column = std::max(1, (vg.dims[2] + 63) / 64);
    packed.words.assign(
        static_cast<std::size_t>(vg.dims[0]) * vg.dims[1] * packed.words_per_column,
        0ull
    );

    for (int x = 0; x < vg.dims[0]; ++x) {
        for (int y = 0; y < vg.dims[1]; ++y) {
            const std::size_t voxel_base = flatten3(x, y, 0, vg.dims);
            const std::size_t word_base =
                (static_cast<std::size_t>(x) * vg.dims[1] + static_cast<std::size_t>(y)) *
                static_cast<std::size_t>(packed.words_per_column);
            for (int z = 0; z < vg.dims[2]; ++z) {
                if (!vg.data[voxel_base + static_cast<std::size_t>(z)]) {
                    continue;
                }
                packed.words[word_base + static_cast<std::size_t>(z / 64)] |= 1ull << (z % 64);
            }
        }
    }
    return packed;
}

static inline uint64_t extract_packed_column_bits(
    const PackedColumnGrid& packed,
    int x,
    int y,
    int z
) {
    const std::size_t word_base =
        (static_cast<std::size_t>(x) * packed.dims[1] + static_cast<std::size_t>(y)) *
        static_cast<std::size_t>(packed.words_per_column);
    const int word_index = z / 64;
    const int bit_offset = z % 64;
    uint64_t bits = packed.words[word_base + static_cast<std::size_t>(word_index)] >> bit_offset;
    if (bit_offset > 61 && word_index + 1 < packed.words_per_column) {
        bits |= packed.words[word_base + static_cast<std::size_t>(word_index + 1)] << (64 - bit_offset);
    }
    return bits & 0x7ull;
}

static uint64_t encode_canonical_64_packed_k3(
    const PackedColumnGrid& packed,
    int x,
    int y,
    int z,
    int& ones
) {
    uint64_t block_bits = 0;
    ones = 0;
    for (int ix = 0; ix < 3; ++ix) {
        for (int iy = 0; iy < 3; ++iy) {
            const uint64_t column = extract_packed_column_bits(packed, x + ix, y + iy, z);
            block_bits |= column << ((ix * 3 + iy) * 3);
            ones += std::popcount(static_cast<unsigned int>(column));
        }
    }

    uint64_t best = std::numeric_limits<uint64_t>::max();
    for (const auto& column_map : kRotationColumnMapsK3) {
        uint64_t rotated = 0;
        for (int dst_col = 0; dst_col < 9; ++dst_col) {
            rotated |= ((block_bits >> (static_cast<uint64_t>(column_map[static_cast<std::size_t>(dst_col)]) * 3ull)) & 0x7ull)
                << (static_cast<uint64_t>(dst_col) * 3ull);
        }
        if (rotated < best) {
            best = rotated;
        }
    }
    return best;
}

static uint64_t encode_canonical_64(
    const VoxelGrid& vg,
    int x,
    int y,
    int z,
    const FastBlockEncoder& encoder,
    int& ones
) {
    if (encoder.k == 3) {
        const auto& dims = vg.dims;
        uint64_t block_bits = 0;
        ones = 0;
        for (int ix = 0; ix < 3; ++ix) {
            for (int iy = 0; iy < 3; ++iy) {
                const std::size_t base = flatten3(x + ix, y + iy, z, dims);
                const uint64_t column =
                    static_cast<uint64_t>(vg.data[base]) |
                    (static_cast<uint64_t>(vg.data[base + 1]) << 1) |
                    (static_cast<uint64_t>(vg.data[base + 2]) << 2);
                block_bits |= column << ((ix * 3 + iy) * 3);
                ones += std::popcount(static_cast<unsigned int>(column));
            }
        }

        // The column-map path remains fast and preserves exact multithreaded results for paper-grade runs.
        uint64_t best = std::numeric_limits<uint64_t>::max();
        for (const auto& column_map : kRotationColumnMapsK3) {
            uint64_t rotated = 0;
            for (int dst_col = 0; dst_col < 9; ++dst_col) {
                rotated |= ((block_bits >> (static_cast<uint64_t>(column_map[static_cast<std::size_t>(dst_col)]) * 3ull)) & 0x7ull)
                    << (static_cast<uint64_t>(dst_col) * 3ull);
            }
            if (rotated < best) {
                best = rotated;
            }
        }
        return best;
    }

    uint64_t block_bits = 0;
    ones = 0;
    const int k = encoder.k;
    const auto& dims = vg.dims;

    for (int i = 0; i < k; ++i) {
        for (int j = 0; j < k; ++j) {
            for (int kk = 0; kk < k; ++kk) {
                const uint8_t v = vg.data[flatten3(x + i, y + j, z + kk, dims)];
                const int linear = (i * k + j) * k + kk;
                block_bits |= static_cast<uint64_t>(v != 0) << linear;
                ones += (v != 0);
            }
        }
    }

    uint64_t best = std::numeric_limits<uint64_t>::max();
    for (const auto& map : encoder.maps) {
        uint64_t rotated = 0;
        for (int dst = 0; dst < encoder.bits; ++dst) {
            rotated |= ((block_bits >> map[static_cast<std::size_t>(dst)]) & 1ull) << dst;
        }
        if (rotated < best) {
            best = rotated;
        }
    }
    return best;
}

static PackedFingerprint encode_canonical_generic(
    const VoxelGrid& vg,
    int x,
    int y,
    int z,
    const FastBlockEncoder& encoder,
    int& ones
) {
    const int bits = encoder.bits;
    const int word_count = (bits + 63) / 64;
    PackedFingerprint source;
    source.words.assign(static_cast<std::size_t>(word_count), 0ull);
    ones = 0;

    const int k = encoder.k;
    const auto& dims = vg.dims;
    for (int i = 0; i < k; ++i) {
        for (int j = 0; j < k; ++j) {
            for (int kk = 0; kk < k; ++kk) {
                const uint8_t v = vg.data[flatten3(x + i, y + j, z + kk, dims)];
                if (!v) {
                    continue;
                }
                ++ones;
                const int linear = (i * k + j) * k + kk;
                source.words[static_cast<std::size_t>(linear / 64)] |= 1ull << (linear % 64);
            }
        }
    }

    PackedFingerprint best;
    bool has_best = false;
    for (const auto& map : encoder.maps) {
        PackedFingerprint rotated;
        rotated.words.assign(static_cast<std::size_t>(word_count), 0ull);
        for (int dst = 0; dst < bits; ++dst) {
            const int src = map[static_cast<std::size_t>(dst)];
            const uint64_t bit = (source.words[static_cast<std::size_t>(src / 64)] >> (src % 64)) & 1ull;
            rotated.words[static_cast<std::size_t>(dst / 64)] |= bit << (dst % 64);
        }
        if (!has_best || rotated < best) {
            best = std::move(rotated);
            has_best = true;
        }
    }
    return best;
}

template <typename CountsContainer>
static std::pair<double, double> spatial_entropy_from_counts(const CountsContainer& counts, std::size_t total_count, int cell_count) {
    if (total_count == 0 || cell_count <= 1) {
        return {0.0, 0.0};
    }

    const double inv_total = 1.0 / static_cast<double>(total_count);
    double entropy = 0.0;
    for (int c : counts) {
        if (c <= 0) {
            continue;
        }
        const double p = static_cast<double>(c) * inv_total;
        entropy -= p * std::log2(p);
    }
    const double max_entropy = std::log2(static_cast<double>(cell_count));
    return {entropy, (max_entropy > 0.0) ? (entropy / max_entropy) : 0.0};
}

static std::pair<double, double> spatial_entropy(
    const std::vector<std::array<double, 3>>& coords,
    const std::array<double, 3>& grid_range,
    const std::array<int, 3>& divisions
) {
    if (coords.empty()) {
        return {0.0, 0.0};
    }

    const int nx = divisions[0];
    const int ny = divisions[1];
    const int nz = divisions[2];
    const int cell_count = nx * ny * nz;

    const double sx = (grid_range[0] > 0.0) ? grid_range[0] : 1e-6;
    const double sy = (grid_range[1] > 0.0) ? grid_range[1] : 1e-6;
    const double sz = (grid_range[2] > 0.0) ? grid_range[2] : 1e-6;

    std::vector<int> counts(static_cast<std::size_t>(cell_count), 0);
    std::vector<int> touched;
    touched.reserve(coords.size());
    for (const auto& c : coords) {
        int ix = static_cast<int>(std::floor((c[0] / sx) * nx));
        int iy = static_cast<int>(std::floor((c[1] / sy) * ny));
        int iz = static_cast<int>(std::floor((c[2] / sz) * nz));
        ix = std::clamp(ix, 0, nx - 1);
        iy = std::clamp(iy, 0, ny - 1);
        iz = std::clamp(iz, 0, nz - 1);
        const int cell = ix + iy * nx + iz * nx * ny;
        if (counts[static_cast<std::size_t>(cell)]++ == 0) {
            touched.push_back(cell);
        }
    }

    std::vector<int> nonzero;
    nonzero.reserve(touched.size());
    for (int cell : touched) {
        nonzero.push_back(counts[static_cast<std::size_t>(cell)]);
    }
    return spatial_entropy_from_counts(nonzero, coords.size(), cell_count);
}

struct EntropyGridInfo {
    int nx = 1;
    int ny = 1;
    int nz = 1;
    int cell_count = 1;
    double x_scale = 1.0;
    double y_scale = 1.0;
    double z_scale = 1.0;
};

static EntropyGridInfo make_entropy_grid_info(const std::array<double, 3>& grid_range, int nx, int ny, int nz) {
    const double sx = (grid_range[0] > 0.0) ? grid_range[0] : 1e-6;
    const double sy = (grid_range[1] > 0.0) ? grid_range[1] : 1e-6;
    const double sz = (grid_range[2] > 0.0) ? grid_range[2] : 1e-6;
    return {
        nx,
        ny,
        nz,
        nx * ny * nz,
        static_cast<double>(nx) / sx,
        static_cast<double>(ny) / sy,
        static_cast<double>(nz) / sz,
    };
}

static inline int compute_entropy_cell(
    double coord_x,
    double coord_y,
    double coord_z,
    const EntropyGridInfo& info
) {
    int ix = static_cast<int>(std::floor(coord_x * info.x_scale));
    int iy = static_cast<int>(std::floor(coord_y * info.y_scale));
    int iz = static_cast<int>(std::floor(coord_z * info.z_scale));
    ix = std::clamp(ix, 0, info.nx - 1);
    iy = std::clamp(iy, 0, info.ny - 1);
    iz = std::clamp(iz, 0, info.nz - 1);
    return ix + iy * info.nx + iz * info.nx * info.ny;
}

static inline void increment_cell_count(PatternStats& stats, int cell_index, int cell_count) {
    if (stats.cell_counts.empty()) {
        stats.cell_counts.assign(static_cast<std::size_t>(cell_count), 0);
    }
    ++stats.cell_counts[static_cast<std::size_t>(cell_index)];
}

static inline void increment_cell_count(
    PatternStats& stats,
    std::vector<int>& count_pool,
    int cell_index,
    int cell_count
) {
    if (stats.cell_offset == std::numeric_limits<std::size_t>::max()) {
        stats.cell_offset = count_pool.size();
        count_pool.resize(count_pool.size() + static_cast<std::size_t>(cell_count), 0);
    }
    ++count_pool[stats.cell_offset + static_cast<std::size_t>(cell_index)];
}

static std::size_t count_block_positions_1d(int dim, int k_voxel) {
    if (dim < k_voxel || k_voxel <= 0) {
        return 0;
    }
    return 1u + static_cast<std::size_t>((dim - k_voxel) / k_voxel);
}

static std::size_t estimate_block_capacity(const std::array<int, 3>& dims, int k_voxel) {
    const std::size_t nx = count_block_positions_1d(dims[0], k_voxel);
    const std::size_t ny = count_block_positions_1d(dims[1], k_voxel);
    const std::size_t nz = count_block_positions_1d(dims[2], k_voxel);
    return nx * ny * nz;
}

static void reserve_pattern_cell_pool(
    std::vector<int>& count_pool,
    const std::array<int, 3>& dims,
    int k_voxel,
    int cell_count
) {
    if (cell_count <= 0) {
        return;
    }

    const std::size_t block_capacity = estimate_block_capacity(dims, k_voxel);
    if (block_capacity == 0) {
        return;
    }

    const std::size_t ints_per_pattern = static_cast<std::size_t>(cell_count);
    const std::size_t raw_reserve = block_capacity * ints_per_pattern;

    // Avoid eager multi-gigabyte reservations when many files are processed in parallel.
    constexpr std::size_t kMaxReserveInts = (64ull * 1024ull * 1024ull) / sizeof(int);
    count_pool.reserve(std::min(raw_reserve, kMaxReserveInts));
}

static std::vector<double> build_l_di_lookup(int voxel_block_size) {
    std::vector<double> lookup(static_cast<std::size_t>(voxel_block_size + 1), 0.0);
    if (voxel_block_size <= 0) {
        return lookup;
    }
    const double n = static_cast<double>(voxel_block_size);
    const double log2_base = std::log(2.0);
    for (int occupied = 1; occupied < voxel_block_size; ++occupied) {
        const double k = static_cast<double>(occupied);
        lookup[static_cast<std::size_t>(occupied)] =
            (std::lgamma(n + 1.0) - std::lgamma(k + 1.0) - std::lgamma(n - k + 1.0)) / log2_base;
    }
    return lookup;
}

static std::pair<double, double> spatial_entropy_dense(const std::vector<int>& counts, int total_count, int cell_count) {
    return spatial_entropy_from_counts(counts, static_cast<std::size_t>(total_count), cell_count);
}

static std::pair<double, double> spatial_entropy_dense(
    const std::vector<int>& count_pool,
    std::size_t offset,
    int total_count,
    int cell_count
) {
    if (offset == std::numeric_limits<std::size_t>::max()) {
        return {0.0, 0.0};
    }
    if (total_count == 0 || cell_count <= 1) {
        return {0.0, 0.0};
    }

    const double inv_total = 1.0 / static_cast<double>(total_count);
    double entropy = 0.0;
    for (int i = 0; i < cell_count; ++i) {
        const int c = count_pool[offset + static_cast<std::size_t>(i)];
        if (c <= 0) {
            continue;
        }
        const double p = static_cast<double>(c) * inv_total;
        entropy -= p * std::log2(p);
    }
    const double max_entropy = std::log2(static_cast<double>(cell_count));
    return {entropy, (max_entropy > 0.0) ? (entropy / max_entropy) : 0.0};
}

static double calc_l_di(int occupied_voxels, int voxel_block_size) {
    if (voxel_block_size <= 0) {
        return 0.0;
    }
    if (occupied_voxels <= 0 || occupied_voxels >= voxel_block_size) {
        return 0.0;
    }
    const double n = static_cast<double>(voxel_block_size);
    const double k = static_cast<double>(occupied_voxels);
    return (std::lgamma(n + 1.0) - std::lgamma(k + 1.0) - std::lgamma(n - k + 1.0)) / std::log(2.0);
}

static double calc_ldata_item(int ni, int total_blocks) {
    if (ni <= 0 || total_blocks <= 0) {
        return 0.0;
    }
    return static_cast<double>(ni) * (-std::log2(static_cast<double>(ni) / static_cast<double>(total_blocks)));
}

static FileResult make_file_error_result(const fs::path& fpath, const char* status) {
    FileResult result;
    result.source_file = fpath.filename().string();
    std::string stem = fpath.stem().string();
    const auto pos = stem.find("_merged");
    result.plot_id = (pos != std::string::npos) ? stem.substr(0, pos) : stem;
    result.status = status;
    return result;
}

static FileResult process_one_file(
    const fs::path& fpath,
    int k_voxel,
    double voxel_size,
    int block_ratio,
    int plot_size_mode,
    double plot_size_x,
    double plot_size_y,
    int layers,
    bool experimental_direct_las,
    bool experimental_packed_columns,
    bool disable_direct_packed_las,
    bool profile_phases
) {
    using Clock = std::chrono::steady_clock;
    const auto seconds_since = [](Clock::time_point start) {
        return std::chrono::duration<double>(Clock::now() - start).count();
    };

    FileResult result;
    result.source_file = fpath.filename().string();
    std::string stem = fpath.stem().string();
    const auto pos = stem.find("_merged");
    result.plot_id = (pos != std::string::npos) ? stem.substr(0, pos) : stem;

    std::string ext = fpath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    const bool can_use_direct_las =
        experimental_direct_las &&
        ext == ".las" &&
        layers == 0;
    const bool can_use_direct_packed_las =
        !disable_direct_packed_las &&
        ext == ".las" &&
        k_voxel == 3 &&
        layers == 0;

    ReadResult rr;
    VoxelGrid vg;
    PackedLasResult direct_packed;
    bool using_direct_packed = false;
    if (can_use_direct_packed_las) {
        const auto read_start = Clock::now();
        direct_packed = read_las_direct_to_packed_grid(
            fpath, voxel_size, plot_size_mode, plot_size_x, plot_size_y
        );
        if (profile_phases) {
            result.phase_read_s += seconds_since(read_start);
            result.phase_read_io_s += direct_packed.read_io_s;
            result.phase_read_decode_s += direct_packed.read_decode_s;
            result.phase_read_pack_write_s += direct_packed.read_pack_write_s;
            result.phase_read_percentile_s += direct_packed.read_percentile_s;
        }
        result.num_points = direct_packed.num_points;
        result.hr98 = direct_packed.hr98;
        if (direct_packed.num_points == 0) {
            result.status = "empty_input";
            return result;
        }
        if (!direct_packed.ok) {
            result.status = "no_valid_points";
            return result;
        }
        using_direct_packed = true;
    } else if (can_use_direct_las) {
        const auto read_start = Clock::now();
        const LasDirectResult direct = read_las_direct_to_voxel_grid(
            fpath, voxel_size, plot_size_mode, plot_size_x, plot_size_y
        );
        if (profile_phases) {
            result.phase_read_s += seconds_since(read_start);
        }
        result.num_points = direct.num_points;
        result.hr98 = direct.hr98;
        if (direct.num_points == 0) {
            result.status = "empty_input";
            return result;
        }
        vg = direct.vg;
        if (!direct.ok) {
            result.status = "no_valid_points";
            return result;
        }
    } else {
        const auto read_start = Clock::now();
        rr = (ext == ".las") ? read_las(fpath) : read_points(fpath);
        if (profile_phases) {
            result.phase_read_s += seconds_since(read_start);
            result.phase_read_io_s += rr.read_io_s;
            result.phase_read_decode_s += rr.read_decode_s;
            result.phase_read_pack_write_s += 0.0;
            result.phase_read_percentile_s += rr.read_percentile_s;
        }
        result.num_points = rr.num_points;
        result.hr98 = rr.hr98;
        if (rr.num_points == 0) {
            result.status = "empty_input";
            return result;
        }

        double range_x = plot_size_x;
        double range_y = plot_size_y;
        if (plot_size_mode == 0) {
            const auto prep_start = Clock::now();
            double min_x = std::numeric_limits<double>::max();
            double min_y = std::numeric_limits<double>::max();
            double max_x = std::numeric_limits<double>::lowest();
            double max_y = std::numeric_limits<double>::lowest();
            for (const auto& p : rr.pts) {
                min_x = std::min(min_x, static_cast<double>(p.x));
                min_y = std::min(min_y, static_cast<double>(p.y));
                max_x = std::max(max_x, static_cast<double>(p.x));
                max_y = std::max(max_y, static_cast<double>(p.y));
            }
            range_x = max_x - min_x;
            range_y = max_y - min_y;
            if (profile_phases) {
                result.phase_prep_s += seconds_since(prep_start);
            }
        }

        if (range_x <= 0.0 || range_y <= 0.0) {
            result.status = "invalid_plot_range";
            return result;
        }

        const auto voxelize_start = Clock::now();
        vg = points_to_voxel_grid(rr.pts, voxel_size, range_x, range_y);
        if (profile_phases) {
            result.phase_voxelize_s += seconds_since(voxelize_start);
        }
        if (!vg.has_valid_points) {
            result.status = "no_valid_points";
            return result;
        }
    }

    const std::array<int, 3>& active_dims = using_direct_packed ? direct_packed.packed.dims : vg.dims;
    const double range_x = using_direct_packed ? direct_packed.range_x : vg.range_x;
    const double range_y = using_direct_packed ? direct_packed.range_y : vg.range_y;

    const FastBlockEncoder encoder(k_voxel);
    const int voxel_block_size = k_voxel * k_voxel * k_voxel;
    const int half_k = k_voxel / 2;
    const bool use_u64 = voxel_block_size <= 64;
    const auto l_di_lookup = build_l_di_lookup(voxel_block_size);
    const bool use_packed_k3 =
        !using_direct_packed &&
        experimental_packed_columns &&
        use_u64 &&
        k_voxel == 3 &&
        active_dims[2] > 0;
    PackedColumnGrid packed_vg;
    if (use_packed_k3) {
        const auto pack_start = Clock::now();
        packed_vg = build_packed_column_grid(vg);
        if (profile_phases) {
            result.phase_voxelize_s += seconds_since(pack_start);
        }
    }

    int total_blocks = 0;
    const double block_length = static_cast<double>(k_voxel) * voxel_size;
    const double stat_grid_size = static_cast<double>(block_ratio) * block_length;
    const int nx = std::max(1, static_cast<int>(std::ceil(range_x / stat_grid_size)));
    const int ny = std::max(1, static_cast<int>(std::ceil(range_y / stat_grid_size)));
    const int nz = std::max(1, static_cast<int>(std::ceil(result.hr98 / stat_grid_size)));
    result.grid_nx = nx;
    result.grid_ny = ny;
    result.grid_nz = nz;
    result.base_area = range_x * range_y;

    double soe_sum = 0.0;
    const std::array<double, 3> entropy_grid_range = {
        range_x,
        range_y,
        std::max(result.hr98, stat_grid_size)
    };
    const EntropyGridInfo entropy_info = make_entropy_grid_info(entropy_grid_range, nx, ny, nz);
    std::vector<int> global_cell_counts(static_cast<std::size_t>(entropy_info.cell_count), 0);
    const double block_center_offset = (static_cast<double>(half_k) + 0.5) * voxel_size;

    if (use_u64) {
        std::unordered_map<uint64_t, PatternStats> patterns;
        patterns.reserve(static_cast<std::size_t>(active_dims[0] * active_dims[1]));
        std::vector<int> pattern_cell_counts;
        reserve_pattern_cell_pool(pattern_cell_counts, active_dims, k_voxel, entropy_info.cell_count);
        const auto scan_start = Clock::now();

        for (int x = 0; x <= active_dims[0] - k_voxel; x += k_voxel) {
            for (int y = 0; y <= active_dims[1] - k_voxel; y += k_voxel) {
                for (int z = 0; z <= active_dims[2] - k_voxel; z += k_voxel) {
                    int ones = 0;
                    const uint64_t fp = using_direct_packed
                        ? encode_canonical_64_packed_k3(direct_packed.packed, x, y, z, ones)
                        : (use_packed_k3
                        ? encode_canonical_64_packed_k3(packed_vg, x, y, z, ones)
                        : encode_canonical_64(vg, x, y, z, encoder, ones));
                    if (ones == 0) {
                        continue;
                    }

                    ++total_blocks;
                    const double cx = static_cast<double>(x) * voxel_size + block_center_offset;
                    const double cy = static_cast<double>(y) * voxel_size + block_center_offset;
                    const double cz = static_cast<double>(z) * voxel_size + block_center_offset;
                    const int cell_index = compute_entropy_cell(cx, cy, cz, entropy_info);
                    auto [it, inserted] = patterns.try_emplace(fp);
                    auto& rec = it->second;
                    if (inserted) {
                        rec.ones = ones;
                    }
                    ++rec.count;
                    increment_cell_count(rec, pattern_cell_counts, cell_index, entropy_info.cell_count);
                    ++global_cell_counts[static_cast<std::size_t>(cell_index)];
                }
            }
        }

        if (total_blocks == 0 || patterns.empty()) {
            result.status = "no_patterns";
            return result;
        }
        if (profile_phases) {
            result.phase_scan_blocks_s += seconds_since(scan_start);
        }

        const auto reduce_start = Clock::now();
        for (auto& kv : patterns) {
            auto& rec = kv.second;
            const double l_dict_i = l_di_lookup[static_cast<std::size_t>(rec.ones)];
            const double l_data_i = calc_ldata_item(rec.count, total_blocks);
            const auto bi_pair = spatial_entropy_dense(
                pattern_cell_counts,
                rec.cell_offset,
                rec.count,
                entropy_info.cell_count
            );
            soe_sum += (l_dict_i + l_data_i) * bi_pair.second;
            result.Iw_total += l_dict_i;
            result.Ib_total += l_data_i;
        }
        result.num_patterns = static_cast<int>(patterns.size());
        if (profile_phases) {
            result.phase_reduce_entropy_s += seconds_since(reduce_start);
        }
    } else {
        std::unordered_map<PackedFingerprint, PatternStats, PackedFingerprintHash> patterns;
        patterns.reserve(static_cast<std::size_t>(active_dims[0] * active_dims[1]));
        std::vector<int> pattern_cell_counts;
        reserve_pattern_cell_pool(pattern_cell_counts, active_dims, k_voxel, entropy_info.cell_count);
        const auto scan_start = Clock::now();

        for (int x = 0; x <= active_dims[0] - k_voxel; x += k_voxel) {
            for (int y = 0; y <= active_dims[1] - k_voxel; y += k_voxel) {
                for (int z = 0; z <= active_dims[2] - k_voxel; z += k_voxel) {
                    int ones = 0;
                    PackedFingerprint fp = encode_canonical_generic(vg, x, y, z, encoder, ones);
                    if (ones == 0) {
                        continue;
                    }

                    ++total_blocks;
                    const double cx = static_cast<double>(x) * voxel_size + block_center_offset;
                    const double cy = static_cast<double>(y) * voxel_size + block_center_offset;
                    const double cz = static_cast<double>(z) * voxel_size + block_center_offset;
                    const int cell_index = compute_entropy_cell(cx, cy, cz, entropy_info);
                    auto [it, inserted] = patterns.try_emplace(std::move(fp));
                    auto& rec = it->second;
                    if (inserted) {
                        rec.ones = ones;
                    }
                    ++rec.count;
                    increment_cell_count(rec, pattern_cell_counts, cell_index, entropy_info.cell_count);
                    ++global_cell_counts[static_cast<std::size_t>(cell_index)];
                }
            }
        }

        if (total_blocks == 0 || patterns.empty()) {
            result.status = "no_patterns";
            return result;
        }
        if (profile_phases) {
            result.phase_scan_blocks_s += seconds_since(scan_start);
        }

        const auto reduce_start = Clock::now();
        for (auto& kv : patterns) {
            auto& rec = kv.second;
            const double l_dict_i = l_di_lookup[static_cast<std::size_t>(rec.ones)];
            const double l_data_i = calc_ldata_item(rec.count, total_blocks);
            const auto bi_pair = spatial_entropy_dense(
                pattern_cell_counts,
                rec.cell_offset,
                rec.count,
                entropy_info.cell_count
            );
            soe_sum += (l_dict_i + l_data_i) * bi_pair.second;
            result.Iw_total += l_dict_i;
            result.Ib_total += l_data_i;
        }
        result.num_patterns = static_cast<int>(patterns.size());
        if (profile_phases) {
            result.phase_reduce_entropy_s += seconds_since(reduce_start);
        }
    }

    result.total_blocks = total_blocks;
    result.Ic_total = result.Iw_total + result.Ib_total;
    result.DSO_raw = soe_sum;
    result.DSO = (result.base_area > 0.0) ? (soe_sum / result.base_area) : 0.0;
    const auto global_entropy_start = Clock::now();
    const auto global_pair = spatial_entropy_dense(global_cell_counts, total_blocks, entropy_info.cell_count);
    if (profile_phases) {
        result.phase_reduce_entropy_s += seconds_since(global_entropy_start);
    }
    result.H_sp_Global = global_pair.first;
    result.H_sp_Global_norm = global_pair.second;
    result.status = "ok";

    if (layers > 0 && !rr.pts.empty()) {
        const auto layers_start = Clock::now();
        std::vector<float> z_values;
        z_values.reserve(rr.pts.size());
        for (const auto& p : rr.pts) {
            z_values.push_back(p.z);
        }
        const double rh2 = percentile(z_values, 0.02);
        const double rh98 = percentile(std::move(z_values), 0.98);
        const double span = rh98 - rh2;

        result.layers.resize(static_cast<std::size_t>(layers));
        if (span > 0.0) {
            for (int li = 0; li < layers; ++li) {
                const double z_lo = rh2 + span * (static_cast<double>(li) / static_cast<double>(layers));
                const double z_hi = rh2 + span * (static_cast<double>(li + 1) / static_cast<double>(layers));
                std::vector<Point3f> layer_points;
                layer_points.reserve(rr.pts.size() / static_cast<std::size_t>(layers) + 16);
                for (const auto& p : rr.pts) {
                    const bool in_layer =
                        (li == layers - 1) ? (p.z >= z_lo && p.z <= z_hi) : (p.z >= z_lo && p.z < z_hi);
                    if (in_layer) {
                        layer_points.push_back(p);
                    }
                }
                auto& layer = result.layers[static_cast<std::size_t>(li)];
                layer.num_points = layer_points.size();
                if (layer_points.empty()) {
                    continue;
                }

                VoxelGrid lvg = points_to_voxel_grid(layer_points, voxel_size, range_x, range_y);
                if (!lvg.has_valid_points) {
                    continue;
                }
                const bool use_layer_packed_k3 =
                    experimental_packed_columns &&
                    use_u64 &&
                    k_voxel == 3 &&
                    lvg.dims[2] > 0;
                PackedColumnGrid layer_packed_vg;
                if (use_layer_packed_k3) {
                    layer_packed_vg = build_packed_column_grid(lvg);
                }

                const double lstat = static_cast<double>(block_ratio) * block_length;
                const int lnx = std::max(1, static_cast<int>(std::ceil(lvg.range_x / lstat)));
                const int lny = std::max(1, static_cast<int>(std::ceil(lvg.range_y / lstat)));
                const int lnz = std::max(1, static_cast<int>(std::ceil((z_hi - z_lo) / lstat)));
                std::vector<std::array<double, 3>> layer_all_coords;
                double layer_soe = 0.0;
                double layer_ic = 0.0;
                int layer_total_blocks = 0;

                if (use_u64) {
                    std::unordered_map<uint64_t, PatternStats> layer_patterns;
                    for (int x = 0; x <= lvg.dims[0] - k_voxel; x += k_voxel) {
                        for (int y = 0; y <= lvg.dims[1] - k_voxel; y += k_voxel) {
                            for (int z = 0; z <= lvg.dims[2] - k_voxel; z += k_voxel) {
                                int ones = 0;
                                const uint64_t fp = use_layer_packed_k3
                                    ? encode_canonical_64_packed_k3(layer_packed_vg, x, y, z, ones)
                                    : encode_canonical_64(lvg, x, y, z, encoder, ones);
                                if (ones == 0) {
                                    continue;
                                }
                                ++layer_total_blocks;
                                auto& rec = layer_patterns[fp];
                                if (rec.count == 0) {
                                    rec.ones = ones;
                                }
                                ++rec.count;
                                rec.coords.push_back({
                                    (static_cast<double>(x) + half_k + 0.5) * voxel_size,
                                    (static_cast<double>(y) + half_k + 0.5) * voxel_size,
                                    (static_cast<double>(z) + half_k + 0.5) * voxel_size,
                                });
                            }
                        }
                    }
                    if (layer_total_blocks == 0 || layer_patterns.empty()) {
                        continue;
                    }

                    layer_all_coords.reserve(static_cast<std::size_t>(layer_total_blocks));
                    for (auto& kv : layer_patterns) {
                        auto& rec = kv.second;
                        layer_all_coords.insert(layer_all_coords.end(), rec.coords.begin(), rec.coords.end());
                        const double ldi = l_di_lookup[static_cast<std::size_t>(rec.ones)];
                        const double ldata = calc_ldata_item(rec.count, layer_total_blocks);
                        const auto lbi = spatial_entropy(rec.coords, lvg.grid_range, {lnx, lny, lnz});
                        layer_soe += (ldi + ldata) * lbi.second;
                        layer_ic += ldi + ldata;
                    }
                } else {
                    std::unordered_map<PackedFingerprint, PatternStats, PackedFingerprintHash> layer_patterns;
                    for (int x = 0; x <= lvg.dims[0] - k_voxel; x += k_voxel) {
                        for (int y = 0; y <= lvg.dims[1] - k_voxel; y += k_voxel) {
                            for (int z = 0; z <= lvg.dims[2] - k_voxel; z += k_voxel) {
                                int ones = 0;
                                PackedFingerprint fp = encode_canonical_generic(lvg, x, y, z, encoder, ones);
                                if (ones == 0) {
                                    continue;
                                }
                                ++layer_total_blocks;
                                auto& rec = layer_patterns[fp];
                                if (rec.count == 0) {
                                    rec.ones = ones;
                                }
                                ++rec.count;
                                rec.coords.push_back({
                                    (static_cast<double>(x) + half_k + 0.5) * voxel_size,
                                    (static_cast<double>(y) + half_k + 0.5) * voxel_size,
                                    (static_cast<double>(z) + half_k + 0.5) * voxel_size,
                                });
                            }
                        }
                    }
                    if (layer_total_blocks == 0 || layer_patterns.empty()) {
                        continue;
                    }

                    layer_all_coords.reserve(static_cast<std::size_t>(layer_total_blocks));
                    for (auto& kv : layer_patterns) {
                        auto& rec = kv.second;
                        layer_all_coords.insert(layer_all_coords.end(), rec.coords.begin(), rec.coords.end());
                        const double ldi = l_di_lookup[static_cast<std::size_t>(rec.ones)];
                        const double ldata = calc_ldata_item(rec.count, layer_total_blocks);
                        const auto lbi = spatial_entropy(rec.coords, lvg.grid_range, {lnx, lny, lnz});
                        layer_soe += (ldi + ldata) * lbi.second;
                        layer_ic += ldi + ldata;
                    }
                }

                const auto global_bi = spatial_entropy(layer_all_coords, lvg.grid_range, {lnx, lny, lnz});
                layer.DSO = (result.base_area > 0.0) ? (layer_soe / result.base_area) : 0.0;
                layer.Ic = layer_ic;
                layer.Hsp_norm = global_bi.second;
            }
        }
        if (profile_phases) {
            result.phase_layers_s += seconds_since(layers_start);
        }
    }

    return result;
}

int run_cli(int argc, char* argv[]) {
    std::ios::sync_with_stdio(false);
    std::cout.tie(nullptr);

    CliArgs args;
    try {
        args = parse_args(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n\n";
        print_usage(std::cerr);
        return 1;
    }

    if (args.show_help) {
        print_usage(std::cout);
        return 0;
    }

    if (!fs::exists(args.input_dir)) {
        std::cerr << "Input directory not found: " << args.input_dir << "\n";
        return 1;
    }
    if (args.k_voxel <= 0) {
        std::cerr << "--k-voxel must be positive\n";
        return 1;
    }
    if (args.voxel_size <= 0.0) {
        std::cerr << "--voxel-size must be positive\n";
        return 1;
    }
    if (args.block_ratio <= 0) {
        std::cerr << "--block-ratio must be positive\n";
        return 1;
    }
    if (args.plot_size_mode != 0 && args.plot_size_mode != 1) {
        std::cerr << "--plot-size MODE must be 0=auto or 1=manual\n";
        return 1;
    }
    if (args.plot_size_mode == 1 && (args.plot_size_x <= 0.0 || args.plot_size_y <= 0.0)) {
        std::cerr << "--plot-size X and Y must be positive in manual mode\n";
        return 1;
    }
    if (args.threads < 0) {
        std::cerr << "--threads must be >= 0\n";
        return 1;
    }
    if (args.limit < 0) {
        std::cerr << "--limit must be >= 0\n";
        return 1;
    }
    if (args.layers < 0) {
        std::cerr << "--layers must be >= 0\n";
        return 1;
    }

    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(args.input_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (ext == ".xyz" || ext == ".txt" || ext == ".las") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    if (args.limit > 0 && files.size() > static_cast<std::size_t>(args.limit)) {
        files.resize(static_cast<std::size_t>(args.limit));
    }

    if (files.empty()) {
        std::cout << "No .xyz, .txt, or .las files found in: " << args.input_dir << "\n";
        return 0;
    }

    std::cout << "Found " << files.size() << " files\n";

    std::vector<FileResult> results(files.size());
    std::mutex print_mutex;

#if defined(DSO_HAS_OPENMP)
    const int thread_count = (args.threads > 0) ? args.threads : omp_get_max_threads();
    std::cout << "Using " << thread_count << " threads\n";
    #pragma omp parallel for num_threads(thread_count) schedule(dynamic)
#else
    std::cout << "Using 1 thread (OpenMP not available)\n";
#endif
    for (int i = 0; i < static_cast<int>(files.size()); ++i) {
        try {
            results[static_cast<std::size_t>(i)] = process_one_file(
                files[static_cast<std::size_t>(i)],
                args.k_voxel,
                args.voxel_size,
                args.block_ratio,
                args.plot_size_mode,
                args.plot_size_x,
                args.plot_size_y,
                args.layers,
                args.experimental_direct_las,
                args.experimental_packed_columns,
                args.disable_direct_packed_las,
                args.profile_phases
            );
        } catch (const std::bad_alloc&) {
            results[static_cast<std::size_t>(i)] =
                make_file_error_result(files[static_cast<std::size_t>(i)], "alloc_error");
        } catch (const std::exception&) {
            results[static_cast<std::size_t>(i)] =
                make_file_error_result(files[static_cast<std::size_t>(i)], "runtime_error");
        } catch (...) {
            results[static_cast<std::size_t>(i)] =
                make_file_error_result(files[static_cast<std::size_t>(i)], "unknown_error");
        }
        if (args.verbose || results[static_cast<std::size_t>(i)].status != "ok") {
            std::lock_guard<std::mutex> lock(print_mutex);
            std::cout << "[" << results[static_cast<std::size_t>(i)].status << "] "
                      << results[static_cast<std::size_t>(i)].source_file << "\n";
        }
    }

    if (!write_results_csv(args.output_csv, results, args.layers, std::cerr)) {
        return 1;
    }

    std::cout << "Saved results to: " << args.output_csv << "\n";
    if (args.profile_phases) {
        double total_read = 0.0;
        double total_read_io = 0.0;
        double total_read_decode = 0.0;
        double total_read_pack_write = 0.0;
        double total_read_percentile = 0.0;
        double total_prep = 0.0;
        double total_voxelize = 0.0;
        double total_scan = 0.0;
        double total_reduce = 0.0;
        double total_layers = 0.0;
        for (const auto& r : results) {
            total_read += r.phase_read_s;
            total_read_io += r.phase_read_io_s;
            total_read_decode += r.phase_read_decode_s;
            total_read_pack_write += r.phase_read_pack_write_s;
            total_read_percentile += r.phase_read_percentile_s;
            total_prep += r.phase_prep_s;
            total_voxelize += r.phase_voxelize_s;
            total_scan += r.phase_scan_blocks_s;
            total_reduce += r.phase_reduce_entropy_s;
            total_layers += r.phase_layers_s;
        }
        const double total_profile =
            total_read + total_prep + total_voxelize + total_scan + total_reduce + total_layers;
        const auto print_phase = [total_profile](const char* name, double seconds) {
            const double pct = (total_profile > 0.0) ? (seconds * 100.0 / total_profile) : 0.0;
            std::cout << name << ": " << seconds << " s (" << pct << "%)\n";
        };
        std::cout << "Phase Profile\n";
        print_phase("  read", total_read);
        if (total_read_io > 0.0 || total_read_decode > 0.0 || total_read_pack_write > 0.0 || total_read_percentile > 0.0) {
            print_phase("    read_io", total_read_io);
            print_phase("    read_decode", total_read_decode);
            print_phase("    read_pack_write", total_read_pack_write);
            print_phase("    read_percentile", total_read_percentile);
        }
        print_phase("  prep", total_prep);
        print_phase("  voxelize", total_voxelize);
        print_phase("  scan_blocks", total_scan);
        print_phase("  reduce_entropy", total_reduce);
        print_phase("  layers", total_layers);
        std::cout << "  total_profiled: " << total_profile << " s\n";
    }
    return 0;
}

} // namespace tdso

