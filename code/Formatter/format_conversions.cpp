#include <iostream>
#include <format>
#include <locale>
#include <icu.h>
#include <array>

struct uchar_iterator : UCharIterator
{
    using value_type = UChar32;
    using difference_type = int32_t;
    bool end_iterator_{ false };

    uchar_iterator(std::string_view s)
    {
        uiter_setUTF8(this, s.data(), static_cast<int32_t>(s.size()));
    }

    uchar_iterator(std::wstring_view s)
    {
        uiter_setUTF16BE(this, reinterpret_cast<char const*>(s.data()), static_cast<int32_t>(s.size() * sizeof(wchar_t)));
    }

    struct end_tag {};

    uchar_iterator(end_tag const) : end_iterator_(true)
    {
    }

    UChar32 operator*() const
    {
        return uiter_current32(as_iter());
    }

    uchar_iterator& operator++()
    {
        if (uiter_next32(this) == U_SENTINEL)
        {
            end_iterator_ = true;
        }
        return *this;
    }

    uchar_iterator operator++(int)
    {
        uchar_iterator i = *this;
        ++(*this);
        return i;
    }

    bool operator==(uchar_iterator const& other) const
    {
        auto otherPtr = std::addressof(other);
        if (this == otherPtr)
        {
            return true;
        }
        else if (end_iterator_ != other.end_iterator_)
        {
            return false;
        }
        else
        {
            return (this->context == other.context) &&
                (this->index) == (other.index);
        }
    }

    bool operator!=(uchar_iterator const& other) const
    {
        return !(*this == other);
    }

private:
    int32_t position() const
    {
        auto self = as_iter();
        return (*self->getIndex)(self, UITER_CURRENT);
    }

    UCharIterator* as_iter() const
    {
        return const_cast<UCharIterator*>(static_cast<UCharIterator const*>(this));
    }
};


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

struct uchar_range
{
    uchar_iterator begin_;

    template<typename TString> uchar_range(TString&& view) : begin_(view)
    {
    }

    uchar_iterator begin() { return begin_; };
    uchar_iterator end() { return uchar_iterator(uchar_iterator::end_tag{}); }
};

int main()
{
#if FAILS_CONVERSION_STATE_NOT_SET_TO_CODEPAGE    
    std::print(std::cout, "{}, {}, {}, {}",
        std::wstring_view{ L"♻️" },
        L"♻️",
        (wchar_t const*)L"♻️",
        std::wstring{ L"♻️" });
#endif

    std::print(std::cout, "{:u}, {:u}, {:u}, {:u}",
        std::wstring_view{ L"♻️" },
        L"♻️",
        (wchar_t const*)L"♻️",
        std::wstring{ L"♻️" });

    std::wstring foo_wchar = L"this is some text";
    std::string foo_char = "this is some text";

    auto r1 = uchar_range(foo_char);
    auto r2 = uchar_range(foo_wchar);

    auto f = uchar_range(L"pups");
    std::println(std::cout, "Len {}", std::distance(std::begin(f), std::end(f)));

    for (auto ch : r1)
    {
        std::println(std::cout, "Char {:x} {:c}", ch, static_cast<char>(ch));
    }

    for (auto ch : r2)
    {
        std::println(std::cout, "Char {:x}", ch);
    }

    bool lxc = std::lexicographical_compare(std::begin(r1), std::end(r1), std::begin(r2), std::end(r2));
    std::cout << std::boolalpha << lxc << std::endl;

    std::cout << std::equal(std::begin(r1), std::end(r1), std::begin(r2)) << std::endl;
}
