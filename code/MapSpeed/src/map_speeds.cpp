// ConsoleApplication1.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include <iostream>
#include <map>
#include <chrono>
#include <unordered_map>
#include <array>
#include <string>
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

    return randomContent;
}

// Given an input length, generates a vector of random strings. Creates a map, an unordered_map, and a sorted vector of
// the strings.
template<typename T> void generateMaps(uint64_t length)
{
    auto randomContent = generateContent<T>(length);
    std::map<T, int> map;
    std::unordered_map<T, int> unorderedMap;
    std::vector<std::pair<T, size_t>> sortedVector;
    for (int i = 0; i < randomContent.size(); i++)
    {
        sortedVector.push_back({ randomContent[i], i });
        map[randomContent[i]] = i;
        unorderedMap[randomContent[i]] = i;
    }
    std::sort(sortedVector.begin(), sortedVector.end());

    // Shuffle the input random strings vector
    std::shuffle(randomContent.begin(), randomContent.end(), g_random);

    // Start a precise timer. Look up the strings in the map, and determine the time difference.
    std::chrono::high_resolution_clock::time_point map_start = std::chrono::high_resolution_clock::now();
    int sum = 0;
    for (auto& i : randomContent)
    {
        sum += map.find(i)->second;
    }
    std::chrono::high_resolution_clock::time_point map_end = std::chrono::high_resolution_clock::now();
    std::cout << "Sum: " << sum << "\n";

    // Start a precise timer. Look up the strings in the unordered_map, and determine the time difference.
    std::chrono::high_resolution_clock::time_point unordered_map_start = std::chrono::high_resolution_clock::now();
    sum = 0;
    for (auto& i : randomContent)
    {
        sum += unorderedMap.find(i)->second;
    }
    std::chrono::high_resolution_clock::time_point unordered_map_end = std::chrono::high_resolution_clock::now();
    std::cout << "Sum: " << sum << "\n";

    // Start a precise timer. Look up the strings in the sorted vector, and determine the time difference.
    std::chrono::high_resolution_clock::time_point sorted_vector_start = std::chrono::high_resolution_clock::now();
    sum = 0;
    for (auto& i : randomContent)
    {
        auto it = std::lower_bound(sortedVector.begin(), sortedVector.end(), i, [](auto& pair, auto& value) {
            return pair.first < value;
        });
        if ((it != sortedVector.end()) && (it->first == i))
        {
            sum += std::distance(sortedVector.begin(), it);
        }
    }
    std::chrono::high_resolution_clock::time_point sorted_vector_end = std::chrono::high_resolution_clock::now();
    std::cout << "Sum: " << sum << "\n";

    // Output the time differences, and a "cycles per second" metric.
    std::chrono::duration<double> map_time = std::chrono::duration_cast<std::chrono::duration<double>>(map_end - map_start);
    std::chrono::duration<double> unordered_map_time = std::chrono::duration_cast<std::chrono::duration<double>>(unordered_map_end - unordered_map_start);
    std::chrono::duration<double> sorted_vector_time = std::chrono::duration_cast<std::chrono::duration<double>>(sorted_vector_end - sorted_vector_start);

    // Set the double-formatting output to be 15 characters long, with 2 decimal places.
    std::cout.precision(5);
    std::cout << std::fixed;
    std::cout << "Length: " << length << "\n";
    std::cout << "Map time: " << map_time.count() << "s, " << ((double)length / map_time.count()) << " cycles per second\n";
    std::cout << "Unordered map time: " << unordered_map_time.count() << "s, " << ((double)length / unordered_map_time.count()) << " cycles per second\n";
    std::cout << "Sorted vector time: " << sorted_vector_time.count() << "s, " << ((double)length / sorted_vector_time.count()) << " cycles per second\n";
}

template<typename T> void driver(std::string_view subject)
{
    std::cout << "Testing " << subject << "...\n";
    generateMaps<T>(5);
    generateMaps<T>(50);
    generateMaps<T>(500);
    generateMaps<T>(5000);
    generateMaps<T>(500000);
}

int main()
{
    driver<std::string>("std::string");
    driver<uint32_t>("uint32_t");
}
