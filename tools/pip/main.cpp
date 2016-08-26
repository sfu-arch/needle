
#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using PathID = std::pair<uint64_t, uint64_t>;

// This is absolutely horrible in its memory behavior, but let's see if it
// works well enough for our purposes.

struct PredictionBuffer {
    using IDCount = std::pair<PathID, uint64_t>;
    std::unique_ptr<IDCount[]> buffer;
    size_t start;
    size_t end;
    size_t count;
    const PathID target;
    const size_t capacity;

    PredictionBuffer(size_t historySize, PathID target)
        : buffer{new IDCount[historySize + 1]}, start{0}, end{0}, count{0},
          target{target}, capacity{historySize + 1} {}

    void add(PathID id, size_t pathCount) {
        buffer[end] = {id, pathCount};
        end         = (end + 1) % capacity;
        count += pathCount;
    }

    bool hasSample() const { return count >= capacity; }

    std::tuple<std::string, PathID, bool> takeSample() {
        // Compute the history & follower for the sample
        PathID follower;
        bool containsTarget = false;
        uint64_t history[2 * (capacity - 1)];
        unsigned historyIndex = 0;
        unsigned bufferIndex  = start;
        while (historyIndex < 2 * (capacity - 1)) {
            auto &idCount  = buffer[bufferIndex];
            auto remaining = idCount.second;
            containsTarget |= idCount.first == target;
            while (remaining && historyIndex < 2 * (capacity - 1)) {
                history[historyIndex + 1] = idCount.first.first;
                history[historyIndex]     = idCount.first.second;
                historyIndex += 2;
                --remaining;
            }
            if (historyIndex == 2 * (capacity - 1)) {
                auto &next =
                    remaining ? idCount : buffer[(bufferIndex + 1) % capacity];
                follower = next.first;
            }
            bufferIndex = (bufferIndex + 1) % capacity;
        }

        // Remove the consumed sample from the buffer
        IDCount &consumedId = buffer[start];
        --consumedId.second;
        if (!consumedId.second) {
            start = (start + 1) % capacity;
        }
        --count;

        return std::make_tuple(
            std::string{reinterpret_cast<char *>(history),
                        2 * sizeof(uint64_t) * (capacity - 1)},
            follower, containsTarget);
    }
};

// NOTE: This invalidates the passed in string.
PathID parsePathID(char *str) {
    PathID pathID;
    pathID.first  = strtoul(str + 8, nullptr, 16);
    str[8]        = '\0';
    pathID.second = strtoul(str, nullptr, 16);
    return pathID;
}

void printHistory(const std::string &history) {
    const char *buffer = history.data();
    for (unsigned i = 0, e = history.size() / 8; i < e; ++i, buffer += 8) {
        printf("%016lx", *reinterpret_cast<const uint64_t *>(buffer));
    }
}

const uint64_t THRESHOLD = 100000000;

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s <history size> <target path ID>  < <trace file>\n",
               argv[0]);
        return -1;
    }

    std::string idString;
    uint64_t count;
    PathID targetID = parsePathID(argv[2]);
    PredictionBuffer buffer{std::strtoul(argv[1], nullptr, 0), targetID};
    std::unordered_map<std::string, std::vector<std::pair<PathID, uint64_t>>>
        samples;

    uint64_t statusCount  = 0;
    bool tracking         = false;
    uint64_t trackedCount = 0;
    while (trackedCount < THRESHOLD && (std::cin >> idString >> count)) {
        PathID pathID = parsePathID(&idString.front());
        buffer.add(pathID, count);

        while (buffer.hasSample()) {
            auto sample = buffer.takeSample();
            if (tracking) {
                ++trackedCount;
            }
            if (!std::get<2>(sample)) {
                continue;
            }
            tracking = true;

            auto &followers = samples[std::get<0>(sample)];
            auto found =
                std::find_if(followers.begin(), followers.end(),
                             [&sample](std::pair<PathID, uint64_t> &pathCount) {
                                 return pathCount.first == std::get<1>(sample);
                             });
            if (found != followers.end()) {
                ++found->second;
            } else {
                followers.emplace_back(std::get<1>(sample), 1);
            }
        }
        ++statusCount;
        if (statusCount % 100000 == 0) {
            fprintf(stderr, "\rProgress: %lu", statusCount);
        }
    }
    fprintf(stderr, " ... Done!\n");

    printf("Number of histories: %lu\n", samples.size());
    for (auto &historyFollowers : samples) {
        auto &followers = historyFollowers.second;
        auto maxPath =
            std::max_element(followers.begin(), followers.end(),
                             [](std::pair<PathID, uint64_t> &path1,
                                std::pair<PathID, uint64_t> &path2) {
#ifdef BENCH_GCC
                                 return path1.first.first == 3069UL ||
                                        path1.first.first == 3076UL ||
                                        path1.first.first == 2927UL ||
                                        path1.first.first == 3087UL ||
                                        path1.first.first == 2916UL ||
                                        path1.first.first == 2863UL ||
                                        path1.first.first == 2852UL ||
                                        path1.second < path2.second &&
                                            path2.first.first != 3069UL &&
                                            path2.first.first != 3076UL &&
                                            path2.first.first != 3087UL &&
                                            path2.first.first != 2927UL &&
                                            path2.first.first != 2916UL &&
                                            path2.first.first != 2863UL &&
                                            path2.first.first != 2852UL;
#else
                                 return path1.second < path2.second;
#endif
                             });

        uint64_t total = std::accumulate(
            followers.begin(), followers.end(), 0,
            [](uint64_t sum, std::pair<PathID, uint64_t> &path) {
                return sum + path.second;
            });

        printHistory(historyFollowers.first);
        printf(", %lu, %lu, ", maxPath->second, total);
        PathID predicted{maxPath->first.second, maxPath->first.first};
        printHistory(std::string{reinterpret_cast<char *>(&predicted),
                                 2 * sizeof(uint64_t)});
        printf("\n");
    }

    return 0;
}
