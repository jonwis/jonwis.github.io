#include <iostream>
#include <format>
#include <locale>
#include <icu.h>
#include <array>

template<typename TTraits> struct std::formatter<std::basic_string_view<wchar_t, TTraits>, char>
{
    struct convert_t : std::codecvt<wchar_t, char, std::mbstate_t>
    {
        template<typename... Args> convert_t(Args&&... args) : std::codecvt<wchar_t, char, std::mbstate_t>(std::forward<Args>(args)...) {}
    };

    template<typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        auto it = ctx.begin();
        while (it != ctx.end() && *it != '}')
        {
            if (*it == 'u')
            {
                use_icu = true;
            }
            ++it;
        }
        return it;
    }

    template<class OutputContext>
    auto format(std::basic_string_view<wchar_t, TTraits> s, OutputContext& ctx) const
    {
        if (use_icu)
        {
            return format_icu(s, ctx);
        }
        else
        {
            return format_ccvt(s, ctx);
        }
    }

    template<class OutputContext>
    auto format_icu(std::basic_string_view<wchar_t, TTraits> input, OutputContext& ctx) const
    {
        auto outIter = ctx.out();
        const UChar* inputPtr = reinterpret_cast<const UChar*>(input.data());
        auto inputLength = static_cast<int32_t>(input.size());
        int32_t inputRead = 0;
        const size_t minCapacity = 4;
        std::array<uint8_t, 256 + minCapacity> utf8Data;
        size_t utf8WriteIndex = 0;

        while (inputRead < inputLength)
        {
            auto capacity = utf8Data.size() - utf8WriteIndex;
            if (capacity < minCapacity)
            {
                outIter = std::copy_n(utf8Data.begin(), utf8WriteIndex, outIter);
                utf8WriteIndex = 0;
                capacity = utf8Data.size();
            }

            UChar32 c;
            bool encodeError = false;
            U16_NEXT(inputPtr, inputRead, inputLength, c);
            U8_APPEND(utf8Data.data(), utf8WriteIndex, capacity, c, encodeError);
        }

        if (utf8WriteIndex != 0)
        {
            outIter = std::copy_n(utf8Data.begin(), utf8WriteIndex, outIter);
        }

        return outIter;
    }

    template<class OutputContext>
    auto format_ccvt(std::basic_string_view<wchar_t, TTraits> s, OutputContext& ctx) const
    {
        static convert_t instance;
        auto state = std::mbstate_t{ 0 };
        wchar_t const* srcNext = s.data();
        wchar_t const* srcEnd = s.data() + s.size();
        auto ctxOutIt = ctx.out();
        while (srcNext < srcEnd)
        {
            char tempBuffer[256];
            char* destBegin = tempBuffer;
            char* destEnd = tempBuffer + sizeof(tempBuffer);
            char* destNext = destBegin;
            auto result = instance.out(state, srcNext, srcEnd, srcNext, destBegin, destEnd, destNext);
            if (result == convert_t::error)
            {
                throw std::runtime_error("Conversion error");
            }

            ctxOutIt = std::copy(destBegin, destNext, ctxOutIt);
        }

        return ctxOutIt;
    }

    bool use_icu = false;
};

template<std::size_t N> struct std::formatter<wchar_t[N], char> : std::formatter<std::wstring_view, char> {};
template<> struct std::formatter<wchar_t const*, char> : std::formatter<std::wstring_view, char> {};
template<typename traits, typename allocator> struct std::formatter<std::basic_string<wchar_t, traits, allocator>> : std::formatter<std::basic_string_view<wchar_t>, char> {};

int main()
{
    std::print(std::cout, "{}, {}, {}, {}",
        std::wstring_view{ L"woop" },
        L"woop",
        (wchar_t const*)L"woop",
        std::wstring{ L"woop" });

    std::print(std::cout, "{:u}, {:u}, {:u}, {:u}",
        std::wstring_view{ L"woop" },
        L"woop",
        (wchar_t const*)L"woop",
        std::wstring{ L"woop" });
}
