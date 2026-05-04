#include <windows.h>
#include <unknwn.h>
#include <iostream>
#include <span>
#include <array>
#include "shared.h"

namespace another_attempt
{
    /*
        This produces similar codegen to the above; the inliner loves to inline the slot-check-then-call:

            00000001`40003340 488b4dc8        mov     rcx,qword ptr [rbp-38h]
            00000001`40003344 4885c9          test    rcx,rcx
            00000001`40003347 752e            jne     cppwinrt_proj!comparison<another_attempt::PropertySet>+0x97 (00000001`40003377)
            00000001`40003349 c745a86c010000  mov     dword ptr [rbp-58h],16Ch
            00000001`40003350 488975b0        mov     qword ptr [rbp-50h],rsi
            00000001`40003354 488b45b8        mov     rax,qword ptr [rbp-48h]
            00000001`40003358 488b08          mov     rcx,qword ptr [rax]
            00000001`4000335b 488b01          mov     rax,qword ptr [rcx]
            00000001`4000335e 4c8d45c8        lea     r8,[rbp-38h]
            00000001`40003362 488d15e75a0000  lea     rdx,[cppwinrt_proj!winrt::impl::guid_v<winrt::Windows::Foundation::Collections::IIterable<winrt::Windows::Foundation::Collections::IKeyValuePair<winrt::hstring,winrt::Windows::Foundation::IInspectable> > > (00000001`40008e50)]
            00000001`40003369 ff10            call    qword ptr [rax]
            00000001`4000336b 85c0            test    eax,eax
            00000001`4000336d 0f88b7020000    js      cppwinrt_proj!comparison<another_attempt::PropertySet>+0x34a (00000001`4000362a)
            00000001`40003373 488b4dc8        mov     rcx,qword ptr [rbp-38h]
            00000001`40003377 e894f5ffff      call    cppwinrt_proj!consume (00000001`40002910)

        So this is:
        
            * Test the cache slot [rbp-38h] for the requested interface; if it's non-null, jump to the call point
            * If it's null, prepare the parameters for the QueryInterface call:
                * Move the default-interface 'this' pointer from [rbp-40h] into rax
                * Dereference the vtable from rax, then dereference the QueryInterface slot from the vtable into rax
                * Load the address of the cache slot into r8
                * Load the IID of the requested interface into rdx
                * Call the QueryInterface method pointer with those parameters
                * Test the HRESULT for failure and jump to an exit point if it failed
                * Move the returned interface pointer from rax into rcx for the consume() call
            * Call consume() with the requested interface pointer
    */
    
    template<typename... Is> struct InterfaceCacheBlock
    {
        InterfaceCacheBlock(void* abi = nullptr) : cache{abi}
        {
        }

        using type_list_t = std::tuple<Is...>;
        mutable std::array<void*, sizeof...(Is)> cache{nullptr};
        template<typename I> static constexpr size_t index_of = type_index_v<I, type_list_t>;

        template<typename TIface> TIface const& get_interface() const
        {
            return *static_cast<TIface const*>(get_interface_abi(get_default(), cache[index_of<TIface>], winrt::guid_of<TIface>()));
        }

        static void* get_interface_abi(IInspectable const& default_iface, void* &stored, winrt::guid const& iid)
        {
            if (!stored)
            {
                winrt::check_hresult(default_iface.as(iid, &stored));
            }
            return stored;
        }

        auto& get_default() const
        {
            return *static_cast<std::tuple_element_t<0, type_list_t> const*>(cache[0]);
        }
    };

    struct PropertySet : protected InterfaceCacheBlock<IPropertySet, IMap<winrt::hstring, IInspectable>, IIterable<IKeyValuePair<winrt::hstring, IInspectable>>, IObservableMap<winrt::hstring, IInspectable>>
    {
    public:
        PropertySet() : InterfaceCacheBlock(winrt::detach_abi(winrt::Windows::Foundation::Collections::PropertySet{}))
        {
        }

        operator IPropertySet const&() const { return get_interface<IPropertySet>(); }
        operator IMap<winrt::hstring, IInspectable> const&() const { return get_interface<IMap<winrt::hstring, IInspectable>>(); }
        operator IIterable<IKeyValuePair<winrt::hstring, IInspectable>> const&() const { return get_interface<IIterable<IKeyValuePair<winrt::hstring, IInspectable>>>(); }
        operator IObservableMap<winrt::hstring, IInspectable> const&() const { return get_interface<IObservableMap<winrt::hstring, IInspectable>>(); }

        template<typename Q> auto as() const { return get_default().as<Q>(); }
        template<typename Q> auto try_as() const { return get_default().try_as<Q>(); }

        auto First() const
        {
            return get_interface<IIterable<IKeyValuePair<winrt::hstring, IInspectable>>>().First();
        }

        auto Size() const
        {
            return get_interface<IMap<winrt::hstring, IInspectable>>().Size();
        }

        auto Clear() const
        {
            return get_interface<IMap<winrt::hstring, IInspectable>>().Clear();
        }

        auto GetView() const
        {
            return get_interface<IMap<winrt::hstring, IInspectable>>().GetView();
        }

        auto HasKey(winrt::param::hstring key) const
        {
            return get_interface<IMap<winrt::hstring, IInspectable>>().HasKey(static_cast<winrt::hstring const&>(key));
        }

        auto Insert(winrt::param::hstring key, IInspectable const& value) const
        {
            return get_interface<IMap<winrt::hstring, IInspectable>>().Insert(static_cast<winrt::hstring const&>(key), value);
        }

        auto Lookup(winrt::param::hstring key) const
        {
            return get_interface<IMap<winrt::hstring, IInspectable>>().Lookup(static_cast<winrt::hstring const&>(key));
        }

        auto Remove(winrt::param::hstring key) const
        {
            return get_interface<IMap<winrt::hstring, IInspectable>>().Remove(static_cast<winrt::hstring const&>(key));
        }

        auto MapChanged(winrt::Windows::Foundation::Collections::MapChangedEventHandler<winrt::hstring, IInspectable> const& vhnd) const
        {
            return get_interface<IObservableMap<winrt::hstring, IInspectable>>().MapChanged(vhnd);
        }

        void MapChanged(winrt::event_token const& token) const noexcept
        {
            get_interface<IObservableMap<winrt::hstring, IInspectable>>().MapChanged(token);
        }
    };
}

