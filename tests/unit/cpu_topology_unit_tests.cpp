// Unit tests: Phase 7 universal CPU topology partitioning (Test 56).
// Exercises tests/vectors/cpu_topology.tsv via Rust FFI and HardwareProfiler::PartitionCpuSetsFromTopology
// (Core Ultra LP E-cores, Intel 8P+8E / 8P+16E, Strix Point 4+8, Dual-CCD 12C/16C, and 2-group >64T CPUs).

#include "../test_support.hpp"

#include "bridge/rust_bridge.h"
#include "detect/hardware_profiler.hpp"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace vftest {

namespace {

std::vector<uint32_t> ParseIdList(const std::string& spec) {
    std::vector<uint32_t> out;
    if (spec.empty() || spec == "-") return out;
    std::stringstream ss(spec);
    std::string part;
    while (std::getline(ss, part, ',')) {
        const size_t dots = part.find("..");
        if (dots != std::string::npos) {
            const uint32_t a = static_cast<uint32_t>(std::stoul(part.substr(0, dots)));
            const uint32_t b = static_cast<uint32_t>(std::stoul(part.substr(dots + 2)));
            for (uint32_t id = a; id <= b; ++id) out.push_back(id);
        } else {
            out.push_back(static_cast<uint32_t>(std::stoul(part)));
        }
    }
    return out;
}

std::vector<VfCpuCoreEntry> ParseSegments(const std::string& spec) {
    std::vector<VfCpuCoreEntry> entries;
    uint32_t logical_idx[8]{};
    std::stringstream ss(spec);
    std::string item;
    while (std::getline(ss, item, ';')) {
        const size_t at = item.find('@');
        if (at == std::string::npos) return {};
        const uint32_t core_count = static_cast<uint32_t>(std::stoul(item.substr(0, at)));
        std::vector<uint32_t> nums;
        std::stringstream rs(item.substr(at + 1));
        std::string n;
        while (std::getline(rs, n, ':')) nums.push_back(static_cast<uint32_t>(std::stoul(n)));
        if (nums.size() != 6) return {};
        const uint16_t group = static_cast<uint16_t>(nums[0]);
        const uint32_t smt = nums[1];
        const uint8_t llc = static_cast<uint8_t>(nums[2]);
        const uint8_t eff = static_cast<uint8_t>(nums[3]);
        const uint32_t start_core = nums[4];
        const uint32_t start_id = nums[5];
        for (uint32_t c = 0; c < core_count; ++c) {
            for (uint32_t t = 0; t < smt; ++t) {
                VfCpuCoreEntry e{};
                e.cpu_set_id = start_id + c * smt + t;
                e.core_index = start_core + c;
                e.logical_processor_index = (group < 8) ? logical_idx[group]++ : (c * smt + t);
                e.group = group;
                e.last_level_cache_index = llc;
                e.efficiency_class = eff;
                entries.push_back(e);
            }
        }
    }
    return entries;
}

} // namespace

void Test56() {
    int rows = 0;
    int fails = 0;
    std::string first_bad;

    ForEachTsvRow("tests/vectors/cpu_topology.tsv", [&](const std::vector<std::string>& col, const std::string&) {
        if (col.size() != 9) {
            ++fails;
            if (first_bad.empty()) first_bad = "bad columns";
            return;
        }
        ++rows;
        const std::string& name = col[0];
        const auto entries = ParseSegments(col[1]);
        const uint16_t target_group = static_cast<uint16_t>(std::stoul(col[2]));
        const auto expected_game = ParseIdList(col[3]);
        const auto expected_bg = ParseIdList(col[4]);
        const uint32_t expected_phys = static_cast<uint32_t>(std::stoul(col[5]));
        const uint32_t expected_logical = static_cast<uint32_t>(std::stoul(col[6]));
        const bool expected_multi_ccx = (col[7] == "1");
        const bool expected_hybrid = (col[8] == "1");

        VfTopologyPartitionOut out{};
        if (!vf_rust_partition_cpu_topology_group(entries.data(), static_cast<uint32_t>(entries.size()), target_group, &out)) {
            ++fails;
            if (first_bad.empty()) first_bad = name + " (ffi false)";
            return;
        }

        const std::vector<uint32_t> actual_game(out.game_cpu_set_ids, out.game_cpu_set_ids + out.game_count);
        const std::vector<uint32_t> actual_bg(out.background_cpu_set_ids, out.background_cpu_set_ids + out.background_count);

        const bool match = (out.physical_cores == expected_phys) &&
                           (out.logical_processors == expected_logical) &&
                           (out.has_multiple_ccx == expected_multi_ccx) &&
                           (out.has_hybrid_cores == expected_hybrid) &&
                           (actual_game == expected_game) &&
                           (actual_bg == expected_bg);
        if (!match) {
            ++fails;
            if (first_bad.empty()) first_bad = name;
        }

        // Also test HardwareProfiler::PartitionCpuSetsFromTopology for Group 0 entries (including 96T multi-group),
        // ensuring zero C++ fallback divergence and group-scoped CPU Set outputs even when > 64 total threads.
        if (target_group == 0) {
            vegaflow::CpuTopologyProfile topo{};
            for (const auto& e : entries) {
                vegaflow::CpuCoreSetInfo cs{};
                cs.cpu_set_id = e.cpu_set_id;
                cs.core_index = e.core_index;
                cs.logical_processor_index = e.logical_processor_index;
                cs.group = e.group;
                cs.last_level_cache_index = e.last_level_cache_index;
                cs.efficiency_class = e.efficiency_class;
                topo.all_cpu_sets.push_back(cs);
            }
            vegaflow::HardwareProfiler::PartitionCpuSetsFromTopology(topo);
            const std::vector<uint32_t> topo_game(topo.game_optimal_cpu_set_ids.begin(), topo.game_optimal_cpu_set_ids.end());
            const std::vector<uint32_t> topo_bg(topo.background_cpu_set_ids.begin(), topo.background_cpu_set_ids.end());
            if (topo_game != expected_game || topo_bg != expected_bg ||
                topo.has_multiple_ccx != expected_multi_ccx || topo.has_hybrid_cores != expected_hybrid) {
                ++fails;
                if (first_bad.empty()) first_bad = name + " (profiler)";
            }
        }
    });

    // Multi-group total core/thread count preservation on HardwareProfiler (>64T 96T & 128T)
    bool total_96t_ok = false;
    {
        const auto entries_96 = ParseSegments("12@0:2:0:0:0:256;12@0:2:1:0:12:280;12@1:2:0:0:0:512;12@1:2:1:0:12:536");
        vegaflow::CpuTopologyProfile topo{};
        for (const auto& e : entries_96) {
            vegaflow::CpuCoreSetInfo cs{};
            cs.cpu_set_id = e.cpu_set_id;
            cs.core_index = e.core_index;
            cs.logical_processor_index = e.logical_processor_index;
            cs.group = e.group;
            cs.last_level_cache_index = e.last_level_cache_index;
            cs.efficiency_class = e.efficiency_class;
            topo.all_cpu_sets.push_back(cs);
        }
        vegaflow::HardwareProfiler::PartitionCpuSetsFromTopology(topo);
        total_96t_ok = (topo.logical_processors == 96) && (topo.physical_cores == 48) &&
                       !topo.background_cpu_set_ids.empty() && !topo.game_optimal_cpu_set_ids.empty();
    }

    const bool passed = (rows == 18) && (fails == 0) && total_96t_ok;
    RecordResult(passed,
                 "Test 56: Universal CPU topology partitioning (Intel hybrid/Core Ultra LP-E, Strix Point, Dual-CCD, >64T multi-group)",
                 "vectors: " + std::to_string(rows) + ", fails: " + std::to_string(fails) +
                 (first_bad.empty() ? "" : ", first_bad: " + first_bad) +
                 ", 96T_totals: " + std::string(total_96t_ok ? "PASS" : "FAIL"));
}

} // namespace vftest
