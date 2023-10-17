# Holding WinRT (and C++/WinRT) Carefully

This document collects a set of issues I've discovered over the last 5+ years of
using WinRT as a daily interface definition language and over the last 4-ish
years of using C++/WinRT as a core implementation technology of those
interfaces, along with several more years of running API design for Windows.

This is meant to be a set of *guidelines*. Each category of issue comes with
guidelines and suggested fixes or approaches to reducing the problems associated
with the issue.  You may not have the same problems.

While most of the code is written in terms of
[https://github.com/microsoft/cppwinrt](C++/WinRT) the guidelines also apply to
any WinRT projection like Rust, C#, Python, etc. Readers should be familiar with
the MIDL3 interface definition language.

* [./abi_definition.md](Using WinRT to define ABIs)
* [./runtimeclass_composition.md](Easy to use types vs performant types)
* [./async_is_hazardous.md](Async APIs are Challenging)
* [./system_interfaces.md](Prefer your runtime's support libraries over WinRT)
* [./build_binaries_well.md](Use the right compiler & linker switches for best results)
* [./strong_vs_weak_typing.md](Strong typing and property bags)