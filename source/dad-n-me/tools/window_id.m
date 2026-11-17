#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const int pid = atoi(argv[1]);
    @autoreleasepool {
        NSArray* windows = CFBridgingRelease(CGWindowListCopyWindowInfo(kCGWindowListOptionAll,kCGNullWindowID));
        for (NSDictionary* window in windows) {
            if ([window[(NSString*)kCGWindowOwnerPID] intValue] != pid ||
                [window[(NSString*)kCGWindowLayer] intValue] != 0) continue;
            CGRect rect = CGRectZero;
            CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)window[(NSString*)kCGWindowBounds],&rect);
            if (rect.size.width < 400 || rect.size.height < 220) continue;
            printf("%u\n",[window[(NSString*)kCGWindowNumber] unsignedIntValue]);
            return 0;
        }
    }
    return 1;
}
