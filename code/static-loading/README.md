# static-loading

This sample shows how native side-by-side activation can combine two different deployment models in one process.

- `testing.exe` has an embedded application manifest with dependencies on `panmap` and `core`.
- `panmap.dll` is deployed as a loose DLL next to `testing.exe`.
- `batmeter.dll` is deployed as `core\batmeter.dll` and described by the private assembly manifest `core\core.manifest`.
- `testing.exe` statically imports both `panmap.dll` and `batmeter.dll` by module name.

When the sample runs, each DLL prints `hello from <full path to loaded DLL>` from `DllMain` during process attach. A successful run demonstrates two things:

1. The dependency on `panmap` resolves to `testing.exe`'s application directory and loads `panmap.dll` from beside the EXE.
2. The dependency on private assembly `core` resolves the static import of `batmeter.dll` to `core\batmeter.dll` through `core\core.manifest`.

Expected deployed layout under `build\deploy`:

```text
testing.exe
panmap.dll
core\batmeter.dll
core\core.manifest
```

The EXE manifest is not deployed as a sidecar file. It is compiled into `testing.exe` as resource
ID 1 from `src\testing.rc`.

Expected console output looks like this:

```text
hello from ...\build\deploy\panmap.dll
hello from ...\build\deploy\core\batmeter.dll
testing.exe calling exports
```

That output proves the loader bound the static import of `batmeter.dll` through the private assembly in `core`, not by searching only beside the EXE.
