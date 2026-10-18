#include <misc/GameActivity.h>
#import <Foundation/Foundation.h>

namespace GameActivity {

Scope::Scope() {
    @autoreleasepool {
        const NSActivityOptions options = NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical;
        id activity = [[NSProcessInfo processInfo] beginActivityWithOptions:options reason:@"DuneCity match in progress"];
        token = (__bridge_retained void*)activity;
    }
}

Scope::~Scope() {
    if (!token) return;
    @autoreleasepool {
        id activity = (__bridge_transfer id)token;
        [[NSProcessInfo processInfo] endActivity:activity];
    }
}

} // namespace GameActivity
