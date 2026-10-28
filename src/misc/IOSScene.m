// UIScene lifecycle for SDL2 on iOS.
//
// iOS 27 stops apps built with its SDK from launching unless they adopt the
// UIScene lifecycle. SDL2 predates scenes: it keeps its UIApplication delegate
// and creates every UIWindow with -initWithFrame:, and such a window belongs to
// no scene and is never shown once scenes are in use. Info.plist therefore
// declares a single application scene served by DuneCitySceneDelegate, and
// every UIWindow is attached to that scene: windows created after it connects
// immediately, windows created earlier (SDL's launch screen, or the game window
// if SDL_main gets there first) when it connects.

#import <UIKit/UIKit.h>
#import <objc/runtime.h>

static UIWindowScene* connectedScene = nil;
static NSHashTable<UIWindow*>* windowsAwaitingScene = nil;
static IMP originalInitWithFrame = NULL;

static id initWithFrameAttachingScene(UIWindow* self, SEL selector, CGRect frame) {
    self = ((id (*)(id, SEL, CGRect))originalInitWithFrame)(self, selector, frame);
    if(self != nil && self.windowScene == nil) {
        if(connectedScene != nil) {
            self.windowScene = connectedScene;
        } else {
            [windowsAwaitingScene addObject:self];
        }
    }
    return self;
}

@interface DuneCitySceneDelegate : UIResponder <UIWindowSceneDelegate>
@end

@implementation DuneCitySceneDelegate

+ (void)load {
    windowsAwaitingScene = [NSHashTable weakObjectsHashTable];
    Method method = class_getInstanceMethod([UIWindow class], @selector(initWithFrame:));
    originalInitWithFrame = method_setImplementation(method, (IMP)initWithFrameAttachingScene);
}

- (void)scene:(UIScene*)scene willConnectToSession:(UISceneSession*)session options:(UISceneConnectionOptions*)connectionOptions {
    if(![scene isKindOfClass:[UIWindowScene class]]) {
        return;
    }
    connectedScene = (UIWindowScene*)scene;
    for(UIWindow* window in windowsAwaitingScene.allObjects) {
        if(window.windowScene == nil) {
            window.windowScene = connectedScene;
        }
    }
    [windowsAwaitingScene removeAllObjects];
}

- (void)sceneDidDisconnect:(UIScene*)scene {
    if(scene == connectedScene) {
        connectedScene = nil;
    }
}

// UIKit asks the scene delegate for its main window; SDL's app delegate owns it.
- (UIWindow*)window {
    return [UIApplication sharedApplication].delegate.window;
}

- (void)setWindow:(UIWindow*)window {
}

@end
