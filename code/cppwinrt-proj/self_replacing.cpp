#include <windows.h>
#include <unknwn.h>
#include <iostream>
#include <span>
#include <array>
#include "shared.h"

#if 0
namespace self_replacing
{
    /*
        This is a sketch of a more complex design that would use self-replacing trampoline objects to cache the interfaces.
        The idea is that the first time an interface is requested, the trampoline queries for it and then replaces itself
        in the cache with the real interface pointer. Subsequent calls would then hit the real interface directly from the cache.

        This design relies on "racy-set" lock-free replacement of the cache slot, which is safe in this scenario. The trampoline type
        is generic. It's passed a raw `void*` abi pointer for the default object, and a reference to the `void*` cache slot, and
        the address of the GUID to query for. Each vtable slot is a thunk that calls a replacement function that does the QI and
        modifies the cache slot.
    */

    __declspec(novtable) struct ManyManyTrampolineVtableSlots :
        winrt::impl::abi_t<IMap<winrt::hstring, IInspectable>>,
        winrt::impl::abi_t<IIterable<IKeyValuePair<winrt::hstring, IInspectable>>>,
        winrt::impl::abi_t<IObservableMap<winrt::hstring, IInspectable>>
    {
        virtual IIterator<IKeyValuePair<winrt::hstring, IInspectable>> First() const = 0;
        virtual uint32_t Size() const = 0;
        virtual void Clear() const = 0;
        virtual IMapView<winrt::hstring, IInspectable> GetView() const = 0;
        virtual bool HasKey(winrt::param::hstring key) const = 0;
        virtual bool Insert(winrt::param::hstring key, IInspectable const& value) const = 0;
        virtual IInspectable Lookup(winrt::param::hstring key) const = 0;
        virtual void Remove(winrt::param::hstring key) const = 0;
        virtual winrt::event_token MapChanged(winrt::Windows::Foundation::Collections::MapChangedEventHandler<winrt::hstring, IInspectable> const& vhnd) const = 0;
        virtual void MapChanged(winrt::event_token const& token) const noexcept = 0;
    };

    struct PropertySet
    {
        using tuple_t = std::tuple<IPropertySet, IMap<winrt::hstring, IInspectable>, IIterable<IKeyValuePair<winrt::hstring, IInspectable>>, IObservableMap<winrt::hstring, IInspectable>>;
        tuple_t cache;

        struct trampoline : winrt::implements<trampoline>,
            winrt::impl::abi_t<IIterable<IKeyValuePair<winrt::hstring, IInspectable>>>,
            winrt::impl::abi_t<IMap<winrt::hstring, IInspectable>>,
            winrt::impl::abi_t<IObservableMap<winrt::hstring, IInspectable>>
        {
            trampoline(tuple_t& cache) : cache{cache} {}

            tuple_t& cache;

            template<typename T> int32_t upgrade(winrt::impl::abi_t<T>*& slot) const
            {
                auto& default_if = std::get<0>(cache);
                auto& target_if = std::get<T>(cache);
                if (!target_if)
                {
                    auto default_if_abi = static_cast<::IUnknown*>(winrt::get_abi(default_if));
                    int32_t hr = default_if_abi->QueryInterface(winrt::guid_of<T>(), reinterpret_cast<void**>(&target_if));
                    if (FAILED(hr))
                    {
                        return hr;
                    }
                }
                slot = static_cast<winrt::impl::abi_t<T>*>(winrt::get_abi(target_if));
                return S_OK;
            }

            template<typename T, typename TMethod, typename... TArgs>
            int32_t call_and_cache(TMethod method, TArgs&&... args) const
            {
                winrt::impl::abi_t<T>* slot = nullptr;
                int32_t hr = upgrade(&slot);
                if (SUCCEEDED(hr))
                {
                    hr = std::invoke(method, slot, std::forward<TArgs>(args)...);
                }
                return hr;
            }

            int32_t __stdcall winrt::impl::abi_t<IIterable<IKeyValuePair<winrt::hstring, IInspectable>>>::First(void** a) noexcept
            {
                return call_and_cache<IIterable<IKeyValuePair<winrt::hstring, IInspectable>>>([](auto* slot, void** a) { return slot->First(a); }, a);
            }

            int32_t __stdcall winrt::impl::abi_t<IMap<winrt::hstring, IInspectable>>::Size(uint32_t* size) noexcept
            {
                return upgrade<IMap<winrt::hstring, IInspectable>>()->Size(size);
            }

            int32_t __stdcall winrt::impl::abi_t<IMap<winrt::hstring, IInspectable>>::Clear() noexcept
            {
                return upgrade<IMap<winrt::hstring, IInspectable>>()->Clear();
            }

            // Similar for other methods and interfaces...

        };

        PropertySet()
        {
            std::get<IPropertySet>(cache) = winrt::Windows::Foundation::Collections::PropertySet{};
            std::get<IMap<winrt::hstring, IInspectable>>(cache) = ReplacementTrampoline<IMap<winrt::hstring, IInspectable>>(&std::get<IPropertySet>(cache), &std::get<IMap<winrt::hstring, IInspectable>(cache));
            cache[0] = winrt::detach_abi(winrt::Windows::Foundation::Collections::PropertySet{});
            cache[1] = new ReplacementTrampoline<IMap<winrt::hstring, IInspectable>>(&cache[0], &cache[1]);
            cache[2] = new ReplacementTrampoline<IIterable<IKeyValuePair<winrt::hstring, IInspectable>>>(&cache[0], &cache[2]);
            cache[3] = new ReplacementTrampoline<IObservableMap<winrt::hstring, IInspectable>>(&cache[0], &cache[3]);
        }
    };    
}
#endif
