# Portability

It is recommended to start developing for Linux first to keep the code standards-compliant, simple and easy and focus on actual content. Once the basics are working, compatibility with Windows should be ensured to avoid surprises later in the development.

## Windows

Only compilation with MinGW is currently supported. If you don't have a matching build environment yet, you may want to have a look at the container provided as part of this project (either to use it directly or to base your own setup on it).

### How to debug

To check DLL linking and symbols (exported and imported) it is recommended to use [Dependencies](https://github.com/lucasg/Dependencies).

### MinGW needs specific order of link dependencies

When compiling with MinGW, as of version 10.3.0, linking will fail if dependencies are not provided in the order of

- importing modules first,
- depending modules later.

That means that if `MySource` depends on `SourceDependency` which in turn depends on `TransitiveDependency`, the correct order should be:

```
MySource SourceDependency TransitiveDependency
```

Any other order, such as

```
MySource TransitiveDependency SourceDependency
```

will fail to link as not-yet-required symbols from earlier modules are discarded by MinGW.

Compilation with GCC (for Linux) does not exhibit this issue, so it can only be noticed when trying to compile for Windows.

### incomplete C11 support

Frame Buffet tries to avoid dependencies for basic functionality while trying to reduce platform-specific code to an absolute minimum. Unfortunately, at time of writing this, MinGW only has incomplete C11 support as the MSVCRT it links to does not support it either:

- `threads.h` is missing completely (which is a huge problem as it is extensively relied upon by Frame Buffet)
- `time.h` might be missing some features as well

Development of the core project, at least of the framework, should still rely on C11 standards as much as possible. [Mesa](https://www.mesa3d.org/) contains a C11 compatibility wrapper for Windows. Frame Buffet extracts that wrapper as `libmesa_c11.a` during compilation. It should be linked statically into all code relying on affected C11 features when being compiled for Windows. Refer to existing `CMakeLists.txt` files for details on the setup (an additional define is needed for header inclusion).

### DLLs need to know where to get their symbols from at compile-time

If a Windows DLL references symbols that need to be imported from another DLL, those symbols can only be resolved by loading the exact same DLL (by file name, as compiled into the DLL). That is also the reason why MinGW requires all symbols to be known at compile-time (different to compilation for Linux).

Frame Buffet source modules access functions provided by the framework. Unless the Frame Buffet framework is statically linked together with all modules, that means the exact same framework DLL would be required if modules were simply linked to the framework DLL. Depending on the exact usage of Frame Buffet that may not be possible.

As a workaround, the framework provides a "DLL shim" which should be *statically* linked into source modules (`libframebuffet_dll_shim.a`) to break up the compile-time dependency to the framework. The shim proxies all framework functions through a lookup structure which is set up when the framework is instructed to load a module from a DLL (implementation: [framework/src/platform/windows](framework/src/platform/windows/)). The plugin side of the shim will protect against API incompatibility by checking a shared hash before accepting any function pointers.

When checking the DLL's dependencies, `framebuffet.dll` should not be listed by source modules if the shim has been linked correctly.

### memory management needs to be local to DLLs

On Windows, memory management is handled by unshared C runtime libraries. Depending on the exact compiler and environment being used to compile a program or module, the memory management may be handled by a different instance or implementation. This leads to issues such as an "Access Violation" when trying to free a (non-null) memory pointer from outside the module which allocated the memory. This has been explained quite well [on StackOverflow](https://stackoverflow.com/questions/19131511/how-to-free-allocated-memory-in-c-dll).

To avoid that issue, memory should only be freed by the same entity which did the allocation. The consequence is that proxies to call `free` might need to be provided by Frame Buffet.

This should not be an issue when implementing source modules unless they exchange data outside the framework in which case they need to take care of it themselves and possibly provide `free` proxies to keep the call local to their own module (and thus C runtime).
