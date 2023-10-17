# System Interfaces

WinRT remains our primary ABI definition language. It provides necessary
primitives for interoperating with Windows APIs and functionality. Much of that
functionality also has a "lower level" access path that can be started from
WinRT and used more efficiently.

## Avoid StorageFile, StorageFolder, StorageItem

These APIs were originally designed on top of the shell data model as a way to
grant standardized access to brokered applications and features that needed to
hop from a low-IL environment to the medium-IL user data environment of Windows.
Whenever possible, **do not use** these APIs in your application. Instead, use
the C++ equivalent or use the Win32 flat ABI instead.

If you...

-   Have a `StorageFile` or `StorageFolder`, use the `.Path` property to
    initialize a `std::filesystem::path`
-   Need to open and read content, use `CreateFile`+`ReadFile` or
    `SHCreateStreamOnFile`
-   Need to walk directories, use `FindFirstFile`/`FindNextFile` or
    `std::filesystem`'s iterators
-   Need the path to a folder, start with `AppDataPaths.SomeFolder` or
    `ApplicationData.SomeFolder.Path` then use your runtime's path primitives

## JSON, XML

These APIs should not have been part of Windows directly. Do not use
`Windows.Data.Json` or `Windows.Data.Xml` directly. There are excellent
open-source C++ JSON parsers available, and both MSXML and XmlLite are easy to
use.

## Avoid WinRT FileIO::\*

Use your language's built in primitives or support library. For C#, System.IO.\*
has many helpful features given a path. For C++, use `std::ifstream` or
`fread`/`fwrite`. Generally you should be operating on a structured type with an
existing parser that has file IO methods. Many Windows APIs also accept
`IStream` as a currency through a `–Interop` interface.
