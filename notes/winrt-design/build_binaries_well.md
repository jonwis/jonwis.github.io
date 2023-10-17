# Ensure your Binaries are Built Properly

For release binaries - and for
[analysis of binary bloat using SizeBench](http://aka.ms/sizebench) - make sure
your DLLs and EXEs are built with these flags set. The
[standard BinSkim rules](https://github.com/microsoft/binskim) will catch many
of these issues:

**/OPT:REF** - Causes the linker to only emit referenced symbols. C++/WinRT's
`name_of<>` produces a lot of constant string data that is usually not
referenced - except for the set used for activating runtime classes and
responding to your objects' `GetRuntimeClassName`.

**/OPT:ICF** - C++ generates many identical comdats (chunks of code or data) due
to template specialization. Use this switch to merge all of those comdats as
much as possible.

**/GR-** - Disable RTTI; C++/WinRT does not need it, but would generate enormous
amounts of type information if enabled. Most RTTI systems can be replaced with
either “try_as<>” or other COM-like conversion systems.

**PGO** - All released binaries should be linked with
[profile-guided optimization](https://learn.microsoft.com/cpp/build/profile-guided-optimizations);
many C++/WinRT helpers that the normal optimizer inlines are re-converted to
method calls after PGO analysis, reducing binary size.
