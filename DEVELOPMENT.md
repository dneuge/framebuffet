# General debugging

## Log where possible

FrameBuffet offers `FBLOG_*` compiler macros which get removed if the log level is deactivated on compilation, so production code compiled without `TRACE` or `DEBUG` levels does not take any performance penalty from detailed log messages.

Log messages will automatically be prefixed by your source module and instance name such as `[Crop:MySource] `. If you work with multiple threads or forward messages from included libraries you may want to use an additional prefix.

In addition to that compile-time optimization, log messages are only forwarded to applications if a log receiver callback has been provided to the framework instance and the runtime log level is set accordingly. Processing of messages below the runtime log level will be avoided within the framework as best as possible.

Typical usage of log levels:
- Use `INFO` to inform users about steps they would probably want to know about that do not require a deeper understanding of actual code, such as information about startup or connections made to remote servers etc.
- `WARN` should be used whenever something has gone wrong which breaks something or may cause other issues (unintended restarts/reconfigurations to recover from issues etc.)
- `ERROR` should only be used if overall system stability is in danger, the application is likely to crash etc. Less severe issues should be logged as `WARN` instead.
- `DEBUG` should be used for detailed information about processing steps which are only relevant for developers or users asked to help debugging; performance is expected to be worse when enabled but usage within mutexes etc. should be kept to a minimum nevertheless
- `TRACE` should be used for really detailed information required to search for issues that are less likely to occur or require frequent logging; performance is expected to be much worse when enabled, users should generally not need this level of detail unless asked by developers
- comment out `TRACE` log messages in code if within critical sections if unlikely to be needed, remove them when no longer needed

# Performance

## Set custom thread names

To help in performance analysis (also by users), it is recommended to use `fbutil_set_current_thread_name` from `fbutils.h` on threads you create. If supported by the platform, the function will register a name for the current thread. This allows threads to be identified in process monitoring tools which support custom thread names.

**Do not to modify threads you don't own!**

Unfortunately, the maximum length of thread names is very limited (15 characters payload with POSIX threads), so the name should be kept as short as possible. It is recommended to use `FB` as a prefix to identify FrameBuffet, followed by up to 4 characters to identify your module, a separator (e.g. `/`) plus some thread identification (like the source instance name).

Example usage:

```c
fbutil_set_current_thread_name("FBCapX/%s", framebuffet_get_source_name_reference(instance->ext_instance_ref));
```

Example names:

- `FBCapX/MySrc`: `FB` means FrameBuffet and `CapX` is short for `capture_x11`, `MySrc` is the source module's instance name as set by the user
- `FBMrgS/MySrc`: `FB` means FrameBuffet and `Mrg` is short for `merge`, `S` refers to a **s**ource worker thread. `MySrc` is the source module's instance name as set by the user.
- `FBMrgM/MySrc`: `FB` means FrameBuffet and `Mrg` is short for `merge`, `M` refers to a **m**ain thread. `MySrc` is the source module's instance name as set by the user.

# Portability

It is recommended to start developing for Linux first to keep the code standards-compliant, simple and easy and focus on actual content. Once the basics are working, compatibility with Windows should be ensured to avoid surprises later in the development.

## Windows

Compilation is supported with MinGW and Visual Studio. If you don't have a build environment for MinGW yet, you may want to have a look at the Linux-based container provided as part of this project (either to use it directly or to base your own setup on it).

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

Development of the core project, at least of the framework, should still rely on C11 standards as much as possible. [Mesa](https://www.mesa3d.org/) contains a C11 compatibility wrapper for MinGW. Frame Buffet extracts that wrapper as `libmesa_c11.a` during compilation. It should be linked statically into all code relying on affected C11 features when being compiled with MinGW. Refer to existing `CMakeLists.txt` files for details on the setup (an additional define is needed for header inclusion).

Note that Visual Studio supports all necessary C11 features out of the box as it links to more recent system libraries. Production builds are recommended to be done using Visual Studio, if possible (e.g. using a CI provider in the cloud).

### DLLs need to know where to get their symbols from at compile-time

If a Windows DLL references symbols that need to be imported from another DLL, those symbols can only be resolved by loading the exact same DLL (by file name, as compiled into the DLL). That is also the reason why MinGW requires all symbols to be known at compile-time (different to compilation for Linux).

Frame Buffet source modules access functions provided by the framework. Unless the Frame Buffet framework is statically linked together with all modules, that means the exact same framework DLL would be required if modules were simply linked to the framework DLL. Depending on the exact usage of Frame Buffet that may not be possible.

As a workaround, the framework provides a "DLL shim" which should be *statically* linked into source modules (`libframebuffet_dll_shim.a`) to break up the compile-time dependency to the framework. The shim proxies all framework functions through a lookup structure which is set up when the framework is instructed to load a module from a DLL (implementation: [framework/src/platform/windows](framework/src/platform/windows/)). The plugin side of the shim will protect against API incompatibility by checking a shared hash before accepting any function pointers.

When checking the DLL's dependencies, `framebuffet.dll` should not be listed by source modules if the shim has been linked correctly.

### memory management needs to be local to DLLs

On Windows, memory management is handled by unshared C runtime libraries. Depending on the exact compiler and environment being used to compile a program or module, the memory management may be handled by a different instance or implementation. This leads to issues such as an "Access Violation" when trying to free a (non-null) memory pointer from outside the module which allocated the memory. This has been explained quite well [on StackOverflow](https://stackoverflow.com/questions/19131511/how-to-free-allocated-memory-in-c-dll).

To avoid that issue, memory should only be freed by the same entity which did the allocation. The consequence is that proxies to call `free` might need to be provided by Frame Buffet.

This should not be an issue when implementing source modules unless they exchange data outside the framework in which case they need to take care of it themselves and possibly provide `free` proxies to keep the call local to their own module (and thus C runtime).

### symbols must be flagged for DLL export

When compiled with Visual Studio, all symbols that should be exported must be flagged, unless *all* should be exported in which case CMake's `SET(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS On)` helps to a certain degree (VS appears to still not export symbols imported from other libraries/earlier compiled artifacts). [`framebuffet.h`](framework/src/framebuffet.h) defines a macro `FBLINK_EXPORT` which can be put in front of symbols to be exported.

## compatibility with the macOS® operating system

### dynamically linking source modules with the framework

Source modules need framework symbols but should not actually try to import the framework on their own, instead they are to be loaded into a runtime environment where the framework already exists and that existing "instance" should be used. This is usually achieved by simply leaving those symbols "undefined" when linking. When using the Xcode® development suite for compilation, linker flag `-undefined dynamic_lookup` must be set to disable the default sanity check for complete symbol availability.
