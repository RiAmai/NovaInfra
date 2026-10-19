#include <thread_pool/thread_pool.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using novainfra::threadpool::ThreadPool;
using novainfra::threadpool::ThreadPoolOptions;

// ============================================================================
// Scenario 1: Image Processing Pipeline
// ============================================================================

struct Image {
    int id;
    std::vector<uint8_t> pixels;
    int width;
    int height;

    Image(int imageId, int w, int h) : id(imageId), width(w), height(h)
    {
        pixels.resize(w * h * 3);  // RGB
        // Simulate image data
        std::mt19937 rng(imageId);
        std::uniform_int_distribution<int> dist(0, 255);
        for (auto &pixel : pixels) {
            pixel = dist(rng);
        }
    }
};

Image resizeImage(const Image &img, int newWidth, int newHeight)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(50));  // Simulate processing
    Image resized(img.id, newWidth, newHeight);
    // Simplified resize (not actual interpolation)
    return resized;
}

Image applyFilter(const Image &img, const std::string &filterName)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(30));  // Simulate processing
    Image filtered = img;
    // Simulate filter application
    for (size_t i = 0; i < filtered.pixels.size(); ++i) {
        filtered.pixels[i] = static_cast<uint8_t>((filtered.pixels[i] + 50) % 256);
    }
    return filtered;
}

void demonstrateImageProcessing()
{
    std::cout << "\n=== Scenario 1: Parallel Image Processing ===\n";

    ThreadPoolOptions options;
    options.workerCount = 4;
    ThreadPool pool(options);

    // Generate sample images
    std::vector<Image> images;
    for (int i = 0; i < 10; ++i) {
        images.emplace_back(i, 1920, 1080);
    }

    auto startTime = std::chrono::steady_clock::now();

    // Submit parallel resize tasks
    std::vector<std::future<Image>> resizedFutures;
    for (const auto &img : images) {
        resizedFutures.push_back(pool.submit([&img]() { return resizeImage(img, 800, 600); }));
    }

    // Wait for resize to complete
    std::vector<Image> resizedImages;
    for (auto &fut : resizedFutures) {
        resizedImages.push_back(fut.get());
    }

    // Apply filters in parallel
    std::vector<std::future<Image>> filteredFutures;
    for (const auto &img : resizedImages) {
        filteredFutures.push_back(pool.submit([&img]() { return applyFilter(img, "blur"); }));
    }

    // Collect results
    std::vector<Image> processedImages;
    for (auto &fut : filteredFutures) {
        processedImages.push_back(fut.get());
    }

    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    std::cout << "Processed " << processedImages.size() << " images in " << duration.count() << "ms\n";
    std::cout << "Average per image: " << duration.count() / processedImages.size() << "ms\n";
    std::cout << "Pool stats: workers=" << pool.workerCount() << ", queued=" << pool.queueSize() << "\n";
}

// ============================================================================
// Scenario 2: Batch File Processing (Log Analysis)
// ============================================================================

struct LogEntry {
    std::string timestamp;
    std::string level;
    std::string message;
};

struct LogStats {
    int errorCount = 0;
    int warningCount = 0;
    int infoCount = 0;
    int totalLines = 0;
};

std::vector<LogEntry> parseLogFile(int fileId)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(20));  // Simulate file I/O

    std::vector<LogEntry> entries;
    std::vector<std::string> levels = {"INFO", "WARN", "ERROR"};

    std::mt19937 rng(fileId);
    std::uniform_int_distribution<int> lineDist(50, 200);
    std::uniform_int_distribution<int> levelDist(0, 2);

    int lineCount = lineDist(rng);
    for (int i = 0; i < lineCount; ++i) {
        LogEntry entry;
        entry.timestamp = "2026-10-13T12:00:00";
        entry.level = levels[levelDist(rng)];
        entry.message = "Log message " + std::to_string(i);
        entries.push_back(entry);
    }

    return entries;
}

LogStats analyzeLogEntries(const std::vector<LogEntry> &entries)
{
    LogStats stats;
    stats.totalLines = entries.size();

    for (const auto &entry : entries) {
        if (entry.level == "ERROR") {
            ++stats.errorCount;
        } else if (entry.level == "WARN") {
            ++stats.warningCount;
        } else if (entry.level == "INFO") {
            ++stats.infoCount;
        }
    }

    return stats;
}

void demonstrateBatchLogProcessing()
{
    std::cout << "\n=== Scenario 2: Batch Log File Analysis ===\n";

    ThreadPoolOptions options;
    options.workerCount = 6;
    options.queueCapacity = 100;
    ThreadPool pool(options);

    constexpr int fileCount = 20;
    auto startTime = std::chrono::steady_clock::now();

    // Parse files in parallel
    std::vector<std::future<std::vector<LogEntry>>> parseFutures;
    for (int i = 0; i < fileCount; ++i) {
        parseFutures.push_back(pool.submit([i]() { return parseLogFile(i); }));
    }

    // Analyze each file's entries in parallel
    std::vector<std::future<LogStats>> analysisFutures;
    for (auto &fut : parseFutures) {
        auto entries = fut.get();
        analysisFutures.push_back(
            pool.submit([entries = std::move(entries)]() { return analyzeLogEntries(entries); }));
    }

    // Aggregate results
    LogStats totalStats;
    for (auto &fut : analysisFutures) {
        auto stats = fut.get();
        totalStats.errorCount += stats.errorCount;
        totalStats.warningCount += stats.warningCount;
        totalStats.infoCount += stats.infoCount;
        totalStats.totalLines += stats.totalLines;
    }

    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    std::cout << "Analyzed " << fileCount << " log files in " << duration.count() << "ms\n";
    std::cout << "Total lines: " << totalStats.totalLines << "\n";
    std::cout << "  - ERROR: " << totalStats.errorCount << "\n";
    std::cout << "  - WARN:  " << totalStats.warningCount << "\n";
    std::cout << "  - INFO:  " << totalStats.infoCount << "\n";
}

// ============================================================================
// Scenario 3: Web Server Request Handling
// ============================================================================

struct HttpRequest {
    int id;
    std::string method;
    std::string path;
    std::string body;
};

struct HttpResponse {
    int statusCode;
    std::string body;
    int processingTimeMs;
};

HttpResponse handleApiRequest(const HttpRequest &req)
{
    // Simulate API processing time
    std::mt19937 rng(req.id);
    std::uniform_int_distribution<int> timeDist(10, 100);
    int processingTime = timeDist(rng);
    std::this_thread::sleep_for(std::chrono::milliseconds(processingTime));

    HttpResponse response;
    response.statusCode = 200;
    response.body = "{\"request_id\": " + std::to_string(req.id) + ", \"status\": \"ok\"}";
    response.processingTimeMs = processingTime;
    return response;
}

void demonstrateWebServerSimulation()
{
    std::cout << "\n=== Scenario 3: Web Server Request Handling ===\n";

    ThreadPoolOptions options;
    options.workerCount = 8;  // 8 worker threads for handling requests
    options.queueCapacity = 200;
    ThreadPool pool(options);

    constexpr int requestCount = 100;
    std::vector<HttpRequest> requests;
    for (int i = 0; i < requestCount; ++i) {
        requests.push_back({i, "POST", "/api/data", "{\"data\": \"value\"}"});
    }

    auto startTime = std::chrono::steady_clock::now();

    // Submit all requests
    std::vector<std::future<HttpResponse>> responseFutures;
    for (const auto &req : requests) {
        responseFutures.push_back(pool.submit([req]() { return handleApiRequest(req); }));
    }

    // Collect responses and calculate stats
    std::vector<int> processingTimes;
    int successCount = 0;
    for (auto &fut : responseFutures) {
        auto response = fut.get();
        if (response.statusCode == 200) {
            ++successCount;
        }
        processingTimes.push_back(response.processingTimeMs);
    }

    auto endTime = std::chrono::steady_clock::now();
    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    // Calculate statistics
    int totalProcessingTime = std::accumulate(processingTimes.begin(), processingTimes.end(), 0);
    int avgProcessingTime = totalProcessingTime / processingTimes.size();
    int maxProcessingTime = *std::max_element(processingTimes.begin(), processingTimes.end());
    int minProcessingTime = *std::min_element(processingTimes.begin(), processingTimes.end());

    std::cout << "Handled " << requestCount << " requests in " << totalDuration.count() << "ms\n";
    std::cout << "Success rate: " << (successCount * 100 / requestCount) << "%\n";
    std::cout << "Processing time stats:\n";
    std::cout << "  - Average: " << avgProcessingTime << "ms\n";
    std::cout << "  - Min: " << minProcessingTime << "ms\n";
    std::cout << "  - Max: " << maxProcessingTime << "ms\n";
    std::cout << "Throughput: " << (requestCount * 1000 / totalDuration.count()) << " req/sec\n";
}

// ============================================================================
// Scenario 4: Data Processing Pipeline with Dependencies
// ============================================================================

void demonstrateDataPipeline()
{
    std::cout << "\n=== Scenario 4: Multi-Stage Data Processing Pipeline ===\n";

    ThreadPool pool;

    // Stage 1: Data extraction
    auto extractFuture = pool.submit([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "  [Stage 1] Data extracted\n";
        return std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    });

    // Stage 2: Data transformation (depends on stage 1)
    auto transformFuture = pool.submit([&extractFuture]() {
        auto data = extractFuture.get();
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        std::cout << "  [Stage 2] Data transformed\n";
        for (auto &val : data) {
            val *= 2;
        }
        return data;
    });

    // Stage 3: Data aggregation (depends on stage 2)
    auto aggregateFuture = pool.submit([&transformFuture]() {
        auto data = transformFuture.get();
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        int sum = std::accumulate(data.begin(), data.end(), 0);
        std::cout << "  [Stage 3] Data aggregated\n";
        return sum;
    });

    // Stage 4: Report generation (depends on stage 3)
    auto reportFuture = pool.submit([&aggregateFuture]() {
        int result = aggregateFuture.get();
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        std::cout << "  [Stage 4] Report generated\n";
        return "Final result: " + std::to_string(result);
    });

    std::cout << reportFuture.get() << "\n";
    std::cout << "Pipeline completed successfully\n";
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    std::cout << "=== ThreadPool Real-World Application Examples ===\n";

    demonstrateImageProcessing();
    demonstrateBatchLogProcessing();
    demonstrateWebServerSimulation();
    demonstrateDataPipeline();

    std::cout << "\n=== All scenarios completed ===\n";
    return 0;
}
