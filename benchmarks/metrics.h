#pragma once

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include <sys/resource.h>

namespace benchmark {
using Clock = std::chrono::steady_clock;
inline double microseconds(Clock::duration duration)
{
    return std::chrono::duration<double, std::micro>(duration).count();
}
inline void report(const std::string &name, std::vector<double> samples, Clock::duration elapsed,
                   std::size_t operations)
{
    std::sort(samples.begin(), samples.end());
    const auto percentile = [&](std::size_t percent) {
        if (samples.empty())
            return 0.0;
        return samples[std::min((samples.size() * percent + 99) / 100 - 1, samples.size() - 1)];
    };
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    std::cout << std::fixed << std::setprecision(3) << name << " operations=" << operations
              << " ops/s=" << operations / std::chrono::duration<double>(elapsed).count()
              << " samples=" << samples.size();
    if (!samples.empty())
        std::cout << " latency_us p50=" << percentile(50) << " p95=" << percentile(95)
                  << " p99=" << percentile(99);
    std::cout << " process_peak_rss_KiB=" << usage.ru_maxrss << '\n';
}
}  // namespace benchmark
