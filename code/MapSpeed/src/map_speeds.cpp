// ConsoleApplication1.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include <iostream>
#include <map>
#include <chrono>
#include <unordered_map>
#include <array>
#include <string>
#include <span>
#include <functional>
#include <algorithm>
#include <vector>
#include <random>
#include <frozen/unordered_map.h>
#include <frozen/random.h>
#include <frozen/map.h>

std::random_device g_randomDevice;
std::mt19937 g_random(g_randomDevice());

template<typename T>
auto generateRandom();



// Generates a random string of length between 5 and 20
template<> auto generateRandom<std::string>()
{
    std::string randomString;
    int length = g_random() % 15 + 5;
    for (int i = 0; i < length; i++)
    {
        randomString += g_random() % 26 + 65;
    }
    return randomString;
}

// Generates a random integer
template<> auto generateRandom<uint32_t>()
{
    return g_random();
}

// Generates a vector of random strings, given an input length
template<typename T> std::vector<T> generateContent(uint64_t length)
{
    std::vector<T> randomContent;
    for (int i = 0; i < length; i++)
    {
        randomContent.push_back(generateRandom<T>());
    }

    // Sort the set, then remove duplicates.
    std::sort(randomContent.begin(), randomContent.end());
    randomContent.erase(std::unique(randomContent.begin(), randomContent.end()), randomContent.end());
    std::shuffle(randomContent.begin(), randomContent.end(), g_random);

    return randomContent;
}

struct timer {
    std::chrono::high_resolution_clock::time_point start;
    std::chrono::high_resolution_clock::time_point end;

    timer() : start(std::chrono::high_resolution_clock::now()) {}

    void stop()
    {
        end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;
    }

    double duration()
    {
        return std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
    }
};

// Given an input length, generates a vector of random strings. Creates a map, an unordered_map, and a sorted vector of
// the strings.
//
// PossibleSpace is the maximum number of unique items that will be generated.
// SearchCount is the number of items to search for in the map, unordered_map, and sorted vector.
// CycleCount is the number of times to repeat the search operation of random items
// unFoundCount is the set of unique items that will be excluded from the search operations, to simulate a "not found" condition.
template<typename T> void generateMaps(uint64_t possibleSpace, uint64_t unFoundCount, uint64_t cycleCount)
{
    // Generate all the content, then split into possible & unfound chunks.
    auto allContent = generateContent<T>(possibleSpace + unFoundCount);
    auto collectionContent = std::vector(allContent.begin(), allContent.begin() + possibleSpace);
    auto unFoundContent = std::vector(allContent.begin() + possibleSpace, allContent.end());
    const auto totalOperations = cycleCount * allContent.size();
    int sum = 0;
    int missed = 0;

    // Build the collections to search
    std::map<T, int> map;
    std::unordered_map<T, int> unorderedMap;
    std::vector<std::pair<T, size_t>> sortedVector;
    for (int i = 0; i < collectionContent.size(); i++)
    {
        sortedVector.push_back({ collectionContent[i], i });
        map[collectionContent[i]] = i;
        unorderedMap[collectionContent[i]] = i;
    }

    // Ensure the sorted vector is sorted
    std::sort(sortedVector.begin(), sortedVector.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    // And re-shuffle all the search content
    std::shuffle(allContent.begin(), allContent.end(), g_random);

    std::cout.precision(5);
    std::cout << std::fixed;
    std::cout << std::setw(25) << "Search Count: " << totalOperations << ", Total space: " << collectionContent.size() + unFoundContent.size() << ", Unfound items: " << unFoundCount << ", Cycle count: " << cycleCount << "\n";

    // Start a precise timer. Look up the strings in the map, and determine the time difference.
    auto map_timer = timer();
    for (int i = 0; i < cycleCount; i++)
    {
        for (auto& j : allContent)
        {
            auto it = map.find(j);
            if (it != map.end())
            {
                sum += it->second;
            }
            else
            {
                missed++;
            }
        }
    }
    map_timer.stop();
    std::cout << "Sum: " << sum << "\n";

    // Start a precise timer. Look up the strings in the unordered_map, and determine the time difference.
    auto unordered_map_timer = timer();
    for (int i = 0; i < cycleCount; i++)
    {
        for (auto& j : allContent)
        {
            auto it = unorderedMap.find(j);
            if (it != unorderedMap.end())
            {
                sum += it->second;
            }
            else
            {
                missed++;
            }
        }
    }
    unordered_map_timer.stop();

    // Start a precise timer. Look up the strings in the sorted vector, and determine the time difference.
    auto sorted_vector_timer = timer();
    for (int i = 0; i < cycleCount; i++)
    {
        for (auto& j : allContent)
        {
            auto it = std::lower_bound(sortedVector.begin(), sortedVector.end(), j, [](auto& pair, auto& value) {
                return pair.first < value;
            });
            if ((it != sortedVector.end()) && (it->first == j))
            {
                sum += it->second;
            }
            else
            {
                missed++;
            }
        }
    }
    sorted_vector_timer.stop();

    // Output the time differences, and a "cycles per second" metric.
    double map_time = map_timer.duration();
    double unordered_map_time = unordered_map_timer.duration();
    double sorted_vector_time = sorted_vector_timer.duration();

    // Set the double-formatting output to be 15 characters long, with 2 decimal places.
    std::cout << "Sum: " << sum << ", Missed: " << missed << "\n";
    std::cout << std::setw(25) << "Map time: " << map_time << "s, " << std::setw(25) << ((double)totalOperations / map_time) << " cycles per second\n";
    std::cout << std::setw(25) << "Sorted vector time: " << sorted_vector_time << "s, " << std::setw(25) << ((double)totalOperations / sorted_vector_time) << " cycles per second\n";
    std::cout << std::setw(25) << "Unordered map time: " << unordered_map_time << "s, " << std::setw(25) << ((double)totalOperations / unordered_map_time) << " cycles per second\n";
    std::cout << "\n";
}

template<typename T> void driver(std::string_view subject)
{
    std::cout << "Testing " << subject << "...\n";
    generateMaps<T>(5, 2, 15);
    generateMaps<T>(50, 5, 8);
    generateMaps<T>(500, 50, 3);
    generateMaps<T>(5000, 500, 3);
    generateMaps<T>(500000, 1000, 3);
}


namespace compile_time
{
    template<size_t totalSize, size_t missCount> constexpr auto make_static_map_test()
    {
        frozen::default_prg_t rng;
        // Generate input random array
        std::array<std::pair<uint32_t, uint32_t>, totalSize> random_array;
        for (size_t i = 0; i < totalSize; ++i)
        {
            random_array[i].first = static_cast<uint32_t>(rng());
            random_array[i].second = i;
        }

        // Make a sorted array subset based on possibleSpace
        std::array<std::pair<uint32_t, uint32_t>, totalSize - missCount> sorted_array;
        std::copy_n(random_array.begin(), totalSize - missCount, sorted_array.begin());
        std::sort(sorted_array.begin(), sorted_array.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
            });

        // Make a struct holding the various data:

        struct map_test_data
        {
            size_t totalSize = totalSize;
            size_t missCount = missCount;
            decltype(sorted_array) sortedArray;
            decltype(random_array) allContent;
            decltype(frozen::make_unordered_map(sorted_array)) unorderedMap;
            decltype(frozen::make_map(sorted_array)) map;
        };

        return map_test_data{
            totalSize,
            missCount,
            sorted_array,
            random_array,
            frozen::make_unordered_map(sorted_array),
            frozen::make_map(sorted_array)
        };
    };

    template<typename T> void run_tests(T const& testData, uint32_t runs)
    {
        std::cout << "\nRunning (static) tests for totalSize = " << testData.totalSize << ", missCount = " << testData.missCount << ", runs = " << runs << "\n";

        uint32_t totalLookups = runs * testData.allContent.size();
        int sum = 0;
        int missed = 0;

        auto sorted_vector_timer = timer();
        for (int i = 0; i < runs; i++)
        {
            for (const auto& j : testData.allContent)
            {
                auto it = std::lower_bound(testData.sortedArray.begin(), testData.sortedArray.end(), j.first, [](const auto& pair, const auto& value) {
                    return pair.first < value;
                });
                if ((it != testData.sortedArray.end()) && (it->first == j.first))
                {
                    sum += it->second;
                }
                else
                {
                    missed++;
                }
            }
        }
        sorted_vector_timer.stop();

        auto map_timer = timer();
        for (int i = 0; i < runs; i++)
        {
            for (const auto& j : testData.allContent)
            {
                auto it = testData.map.find(j.first);
                if (it != testData.map.end())
                {
                    sum += it->second;
                }
                else
                {
                    missed++;
                }
            }
        }
        map_timer.stop();

        auto unordered_map_timer = timer();
        for (int i = 0; i < runs; i++)
        {
            for (const auto& j : testData.allContent)
            {
                auto it = testData.unorderedMap.find(j.first);
                if (it != testData.unorderedMap.end())
                {
                    sum += it->second;
                }
                else
                {
                    missed++;
                }
            }
        }
        unordered_map_timer.stop();

        double map_time = map_timer.duration();
        double unordered_map_time = unordered_map_timer.duration();
        double sorted_vector_time = sorted_vector_timer.duration();
        std::cout << "(Dump values: " << missed << ", " << sum << ")\n";
        std::cout << std::setw(25) << "Map time: " << map_time << "s, " << std::setw(25) << ((double)totalLookups / map_time) << " cycles per second\n";
        std::cout << std::setw(25) << "Sorted vector time: " << sorted_vector_time << "s, " << std::setw(25) << ((double)totalLookups / sorted_vector_time) << " cycles per second\n";
        std::cout << std::setw(25) << "Unordered map time: " << unordered_map_time << "s, " << std::setw(25) << ((double)totalLookups / unordered_map_time) << " cycles per second\n";
        std::cout << "\n";
    }

    constexpr auto dataset_5_2 = make_static_map_test<5, 2>();
    constexpr auto dataset_50_5 = make_static_map_test<50, 5>();
    constexpr auto dataset_500_50 = make_static_map_test<500, 50>();
    constexpr auto dataset_5000_500 = make_static_map_test<1000, 500>();

    void driver()
    {
        std::cout << "Running compile-time tests...\n";
        run_tests(dataset_5_2, 3);
        run_tests(dataset_50_5, 3);
        run_tests(dataset_500_50, 3);
        run_tests(dataset_5000_500, 3);
    }
}

int main()
{
    std::cout << "In cycles per second, more is better; in time, less is better.\n";
    driver<std::string>("std::string");
    driver<uint32_t>("uint32_t");
    compile_time::driver();
}
