#include "batmeter.h"
#include "panmap.h"

#include <iostream>

int main()
{
    std::cout << "testing.exe calling exports" << std::endl;
    RunPanMap();
    RunBatMeter();
    return 0;
}
