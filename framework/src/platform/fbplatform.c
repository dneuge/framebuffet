// FIXME: make this a proper module (requires framework to be split up)

#ifdef TARGET_LINUX
#include "linux/framebuffet.c"
#endif

#ifdef TARGET_MACOS
#include "linux/framebuffet.c"
#endif

#ifdef TARGET_WINDOWS
#include "windows/framebuffet.c"
#endif
