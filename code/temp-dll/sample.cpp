#include <cstdio>
#include <windows.h>

extern "C" const IMAGE_DOS_HEADER __ImageBase;

namespace foo
{
    EXTERN_C IMAGE_DOS_HEADER __ImageBase;
}

int main()
{
    std::printf("DLL Base Address: %p\n", &__ImageBase);
    std::printf("DLL Base Address (namespace foo): %p\n", &foo::__ImageBase);
    return 0;
}
