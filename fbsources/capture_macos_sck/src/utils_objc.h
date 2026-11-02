#ifndef FBSOURCE_CAPTURE_SCK_UTILS_OBJC_H
#define FBSOURCE_CAPTURE_SCK_UTILS_OBJC_H

#include <stdbool.h>
#include <stdint.h>

#include <Foundation/Foundation.h>

typedef int (^IntSupplier)(void);

@interface Utils : NSObject {
    // no instance needed
}

+ (bool)waitWithTimeout:(uint32_t)timeout_millis
        completionCheck:(IntSupplier)checkComplete;

+ (char*)createCString:(NSString*)s;

+ (char*)createCString:(NSString*)s
         usingEncoding:(NSStringEncoding)encoding;

@end

#endif //FBSOURCE_CAPTURE_SCK_UTILS_OBJC_H
