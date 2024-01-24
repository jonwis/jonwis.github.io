#include "pch.h"
#include <winrt/Windows.System.Profile.h>
#include <winrt/Windows.Security.Cryptography.h>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::System::Profile;

hstring to_base64(const Windows::Storage::Streams::IBuffer& buffer)
{
    return Windows::Security::Cryptography::CryptographicBuffer::EncodeToBase64String(buffer);
}

int main()
{
    init_apartment();
    auto id = SystemIdentification::GetSystemIdForUser(nullptr);
    printf("System ID: %ls\n", to_base64(id.Id()).c_str());
    printf("Source: %d\n", id.Source());
}
