#pragma once

#include <TraceLoggingProvider.h>
#include <string_view>
#include <winrt/base.h>

template<typename TView> struct _tlgWrapperForStringView
{
    TView const& m_view;

    static const unsigned DataDescCount = 2;

    TLG_INLINE _Ret_ void* Fill(_Out_writes_(2) EVENT_DATA_DESCRIPTOR* pDesc) const
    {
        EventDataDescCreate(&pDesc[0], &pDesc[1].Size, 2);
        EventDataDescCreate(&pDesc[1], m_view.data(), static_cast<unsigned long>(m_view.size() * sizeof(m_view[0])));
        return pDesc;
    }
};

TLG_INLINE auto _tlg_CALL _tlgWrapAuto(std::wstring_view const& v)
{
    return _tlgWrapperForStringView<std::wstring_view>{ v };
}

template<> struct _tlgTypeMapBase<std::wstring_view>
{
    typedef uint8_t _tlgTypeType0; /* No field tags: Don't need to store outtype. */
    typedef uint16_t _tlgTypeType1; /* Yes field tags: Need to store outtype = 0. */
    static bool const _tlgIsSimple = true;
    static _tlgTypeType0 const _tlgType0 = TlgInCOUNTEDSTRING | 0x0000;
    static _tlgTypeType1 const _tlgType1 = TlgInCOUNTEDSTRING | 0x8080;
};

TLG_INLINE auto _tlg_CALL _tlgWrapAuto(std::string_view const& v)
{
    return _tlgWrapperForStringView<std::string_view>{ v };
}

template<> struct _tlgTypeMapBase<std::string_view>
{
    typedef uint8_t _tlgTypeType0; /* No field tags: Don't need to store outtype. */
    typedef uint16_t _tlgTypeType1; /* Yes field tags: Need to store outtype = 0. */
    static bool const _tlgIsSimple = true;
    static _tlgTypeType0 const _tlgType0 = TlgInCOUNTEDANSISTRING | 0x0000;
    static _tlgTypeType1 const _tlgType1 = TlgInCOUNTEDANSISTRING | 0x8080;
};

#define TraceLoggingStringView(val, ...) _tlgArgAuto(val, __VA_ARGS__)

TLG_INLINE auto _tlg_CALL _tlgWrapAuto(winrt::hstring const& v)
{
    return _tlgWrapperForStringView<std::wstring_view>{ v };
}

template<> struct _tlgTypeMapBase<winrt::hstring>
{
    typedef uint8_t _tlgTypeType0; /* No field tags: Don't need to store outtype. */
    typedef uint16_t _tlgTypeType1; /* Yes field tags: Need to store outtype = 0. */
    static bool const _tlgIsSimple = true;
    static _tlgTypeType0 const _tlgType0 = TlgInCOUNTEDSTRING | 0x0000;
    static _tlgTypeType1 const _tlgType1 = TlgInCOUNTEDSTRING | 0x8080;
};

#define TraceLoggingHString(val, ...) _tlgArgAuto(val, __VA_ARGS__)

