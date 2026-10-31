#include "pbstore/store/progress_text.h"

#include <cstdio>

namespace pbstore {
namespace store {

namespace {

constexpr uint64_t kMiB = 1024 * 1024;

// Both numbers of "3.1 of 7.4 MB" use the unit of the larger one; below 1 MB it is "12 of 48 KB" (a catalog is a few KB: "0.0 of 0.0 MB" says nothing).
std::string size_number(uint64_t bytes, bool in_kb) {
    char buf[32];
    if (in_kb) std::snprintf(buf, sizeof buf, "%llu", static_cast<unsigned long long>(bytes / 1024));
    else std::snprintf(buf, sizeof buf, "%.1f", static_cast<double>(bytes) / static_cast<double>(kMiB));
    return buf;
}
std::string size_unit(uint64_t bytes) { return bytes < kMiB ? "KB" : "MB"; }

}  // namespace

std::string progress_figure(const install::Progress& p) {
    const bool bytes = p.unit == "bytes";
    if (p.total > 0) {
        if (bytes) return size_number(p.done, p.total < kMiB) + " of " + size_number(p.total, p.total < kMiB) + " " + size_unit(p.total);
        if (p.unit == "files") return "file " + std::to_string(p.done) + " of " + std::to_string(p.total);
        if (p.unit == "steps") return "step " + std::to_string(p.done) + " of " + std::to_string(p.total);
        return std::to_string(p.done) + " of " + std::to_string(p.total) + (p.unit.empty() ? "" : " " + p.unit);
    }
    if (p.done == 0) return "";
    if (bytes) return size_number(p.done, p.done < kMiB) + " " + size_unit(p.done);
    if (p.unit == "files") return std::to_string(p.done) + (p.done == 1 ? " file" : " files");
    return std::to_string(p.done) + (p.unit.empty() ? "" : " " + p.unit);
}

std::string progress_line(const install::Progress& p) {
    const std::string figure = progress_figure(p);
    return figure.empty() ? p.text : p.text + " (" + figure + ")";
}

}  // namespace store
}  // namespace pbstore
