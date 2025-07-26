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
    auto length = collectionContent.size();

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
    std::cout << std::setw(25) << "Search Count: " << length << ", Total space: " << collectionContent.size() + unFoundContent.size() << ", Unfound items: " << unFoundCount << ", Cycle count: " << cycleCount << "\n";

    // Start a precise timer. Look up the strings in the map, and determine the time difference.
    auto map_timer = timer();
    int sum = 0;
    int missed = 0;
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
    sum = 0;
    missed = 0;
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
    std::cout << "Sum: " << sum << "\n";

    // Start a precise timer. Look up the strings in the sorted vector, and determine the time difference.
    auto sorted_vector_timer = timer();
    sum = 0;
    missed = 0;
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
    std::cout << "Sum: " << sum << ", Missed: " << missed << "\n";

    // Output the time differences, and a "cycles per second" metric.
    double map_time = map_timer.duration();
    double unordered_map_time = unordered_map_timer.duration();
    double sorted_vector_time = sorted_vector_timer.duration();

    // Set the double-formatting output to be 15 characters long, with 2 decimal places.
    std::cout << std::setw(25) << "Map time: " << map_time << "s, " << std::setw(25) << ((double)length / map_time) << " cycles per second\n";
    std::cout << std::setw(25) << "Sorted vector time: " << sorted_vector_time << "s, " << std::setw(25) << ((double)length / sorted_vector_time) << " cycles per second\n";
    std::cout << std::setw(25) << "Unordered map time: " << unordered_map_time << "s, " << std::setw(25) << ((double)length / unordered_map_time) << " cycles per second\n";
    std::cout << "\n";
}

template<typename T> void driver(std::string_view subject)
{
    std::cout << "Testing " << subject << "...\n";
    generateMaps<T>(5, 2, 3);
    generateMaps<T>(50, 5, 3);
    generateMaps<T>(500, 50, 3);
    generateMaps<T>(5000, 500, 3);
    generateMaps<T>(500000, 1000, 3);
}

int main()
{
    std::cout << "In cycles per second, more is better; in time, less is better.\n";
    driver<std::string>("std::string");
    driver<uint32_t>("uint32_t");
}
