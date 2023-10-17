#pragma once

#include <TraceLoggingProvider.h>
#include <string_view>
#include <winrt/base.h>

template<typename TString> struct _tlgWrapperForStdString
{
    TString const& m_view;

    static const unsigned DataDescCount = 2;

    TLG_INLINE _Ret_ void* Fill(_Out_writes_(2) EVENT_DATA_DESCRIPTOR* pDesc) const
    {
        EventDataDescCreate(&pDesc[0], &pDesc[1].Size, 2);
        EventDataDescCreate(&pDesc[1], m_view.data(), static_cast<unsigned long>(m_view.size() * sizeof(m_view[0])));
        return pDesc;
    }
};

template<class Elem, class Traits> TLG_INLINE auto _tlg_CALL _tlgWrapAuto(std::basic_string_view<Elem, Traits> const& v) { return _tlgWrapperForStdString<std::basic_string_view<Elem, Traits>>{ v }; }
template<class Elem, class Traits, class Alloc> TLG_INLINE auto _tlg_CALL _tlgWrapAuto(std::basic_string<Elem, Traits, Alloc> const& v) { return _tlgWrapperForStdString<std::basic_string<Elem, Traits, Alloc>>{ v }; }

template<class Elem> struct _tlgTypeMapStringBase
{
    typedef uint8_t _tlgTypeType0; /* No field tags: Don't need to store outtype. */
    typedef uint16_t _tlgTypeType1; /* Yes field tags: Need to store outtype = 0. */
    static bool const _tlgIsSimple = true;
    static auto const _tlgBaseType = std::is_same_v<Elem, wchar_t> ? TlgInCOUNTEDSTRING : TlgInCOUNTEDANSISTRING;
    static _tlgTypeType0 const _tlgType0 = _tlgBaseType | 0x0000;
    static _tlgTypeType1 const _tlgType1 = _tlgBaseType | 0x8080;
};

template<class Elem, class Traits> struct _tlgTypeMapBase<std::basic_string_view<Elem, Traits>> : _tlgTypeMapStringBase<Elem> {};
template<class Elem, class Traits, class Alloc> struct _tlgTypeMapBase<std::basic_string<Elem, Traits, Alloc>> : _tlgTypeMapStringBase<Elem> {};

#define TraceLoggingStringView(val, ...) _tlgArgAuto(val, __VA_ARGS__)

TLG_INLINE auto _tlg_CALL _tlgWrapAuto(winrt::hstring const& v)
{
    return _tlgWrapperForStdString<std::wstring_view>{ v };
}

template<> struct _tlgTypeMapBase<winrt::hstring> : _tlgTypeMapStringBase<wchar_t> {};

#define TraceLoggingHString(val, ...) _tlgArgAuto(val, __VA_ARGS__)

