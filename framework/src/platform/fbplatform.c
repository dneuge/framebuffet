// FIXME: make this a proper module (requires framework to be split up)

#ifdef TARGET_UNIX
#include "linux/framebuffet.c"
#endif

#ifdef TARGET_WINDOWS
#include "windows/framebuffet.c"
#endif
