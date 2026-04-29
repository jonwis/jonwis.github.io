#include <windows.h>
#include <unknwn.h>
#include <numeric>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.Collections.h>

// Self-upgrading inspectable experiment notes extracted from experiment.cpp.

// An "inspectable" may contain either a default interface or the correct "derived" interface, based on whether
// the low-bit of the abi field is set. When it's set, it's the "default" interface and must be redeemed for a
// derived interface.
struct IInspectable
{
protected:
    IInspectable(void* abi_to_own, winrt::take_ownership_from_abi_t) : abi(abi_to_own)
    {
    }

    IInspectable() = default;

    IInspectable(IInspectable const& t)
    {
        abi = t.abi;
        if (auto p = to_unknown(abi))
        {
            p->AddRef();
        }
    }

    IInspectable(IInspectable&& t) : abi(std::exchange(t.abi, nullptr)) { }

    IInspectable& operator=(IInspectable const &t)
    {
        if (this != std::addressof(t))
        {
            auto incoming = to_unknown(t.abi);
            if (incoming)
            {
                incoming->AddRef();
            }
            if (auto self = to_unknown(abi))
            {
                self->Release();
            }
            abi = incoming;
        }
        return *this;
    }

    IInspectable& operator=(IInspectable&& t)
    {
        if (this != std::addressof(t))
        {
            std::exchange(abi, t.abi);
            if (auto p = to_unknown(t.abi))
            {
                p->Release();
                t.abi = nullptr;
            }
        }
    }

    ~IInspectable()
    {
        if (auto p = to_unknown(abi))
        {
            p->Release();
        }
    }

    template<typename TIFace, typename TCallback> void invoke_abi(TCallback&& c)
    {
        check_hresult(c(upgrade_abi<TIFace>()));
    }

    template<typename TIFace, typename TMethod, typename... TArgs> void call_abi(TMethod method, TArgs&&... args)
    {
        winrt::check_hresult(std::invoke(upgrade_abi<TIFace>(), method, std::forward<TArgs>(args)));
    }

private:
    static ::IUnknown* to_unknown(void* pv)
    {
        return std::bit_cast<::IUnknown *>(std::bit_cast<uintptr_t>(pv) & ~0x1);
    }

    template<typename TIFace> auto upgrade_abi()
    {
        if (std::bit_cast<uintptr_t>(abi) & 0x1)
        {
            // Must upgrade to the 'real' interface here; we own one strong reference here
            void* upgraded = nullptr;
            to_unknown(abi)->QueryInterface(winrt::guid_of<TIFace>(), &upgraded);
            abi = upgraded;
        }

        return static_cast<TIFace*>(abi);
    }

    void* abi;
};

struct IStringable : IInspectable
{
    template<typename... T> IStringable(T&&... args) : IInspectable(std::forward<T>(args)...) {}
    IStringable() = default;

    auto ToString() const
    {
        winrt::hstring result;
        call_abi<IStringable>(ABI::IStringable::ToString, winrt::put_abi(result));
        return result;
    }
};
