# Holding WinRT (and C++/WinRT) Carefully

This document collects a set of issues I've discovered over the last 5+ years of
using WinRT as a daily interface definition language and over the last 4-ish
years of using C++/WinRT as a core implementation technology of those
interfaces, along with several more years of running API design for Windows.

This is meant to be a set of *guidelines*. Each category of issue comes with
guidelines and suggested fixes or approaches to reducing the problems associated
with the issue.  You may not have the same problems.

While most of the code is written in terms of
[C++/WinRT](https://github.com/microsoft/cppwinrt) the guidelines also apply to
any WinRT projection like Rust, C#, Python, etc. Readers should be familiar with
the MIDL3 interface definition language.

* [Using WinRT to define ABIs](./abi_definition.md)
* [Easy to use types vs performant types](./runtimeclass_composition.md)
* [Async APIs are Challenging](./async_is_hazardous.md)
* [Prefer your runtime's support libraries over WinRT](./system_interfaces.md)
* [Use the right compiler & linker switches for best results](./build_binaries_well.md)
* [Strong typing and property bags](./strong_vs_weak_typing.md)