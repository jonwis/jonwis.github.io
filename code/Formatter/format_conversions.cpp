#include <iostream>
#include <format>
#include <locale>

template<> struct std::formatter<std::basic_string_view<wchar_t>, char>
{
    struct convert_t : std::codecvt<wchar_t, char, std::mbstate_t>
    {
        template<typename... Args> convert_t(Args&&... args) : std::codecvt<wchar_t, char, std::mbstate_t>(std::forward<Args>(args)...) {}
    };

    template<typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        // No control over conversion
        return ctx.begin();
    }

    template<class OutputContext>
    auto format(std::basic_string_view<wchar_t> s, OutputContext& ctx) const
    {
        static convert_t instance;
        auto state = std::mbstate_t{ 0 };
        wchar_t const* srcNext = s.data();
        wchar_t const* srcEnd = s.data() + s.size();
        auto ctxOutIt = ctx.out();
        while (srcNext < srcEnd)
        {
            char tempBuffer[2];
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
}
