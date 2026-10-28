
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <Foundation/Foundation.h>
#else
#import <Cocoa/Cocoa.h>
#endif
#import <misc/MacFunctions.h>

static char macLanguage[3] = "  ";

const char* getMacLanguage() {
	NSUserDefaults * userDefaults = [NSUserDefaults standardUserDefaults];
	NSArray * languageArray = [userDefaults objectForKey:@"AppleLanguages"];
	NSString * firstLanguage = [languageArray objectAtIndex:0];
	
	macLanguage[0] = [firstLanguage characterAtIndex:0];
	macLanguage[1] = [firstLanguage characterAtIndex:1];
		
	return macLanguage;
}

void getMacApplicationSupportFolder(char* buffer, int len) {
#if TARGET_OS_IPHONE
	// iOS keeps settings, saves and logs in the app's Documents folder, which
	// UIFileSharingEnabled exposes in the Files app and Finder so players can
	// back up saves and add their own maps or PAK files.
	NSString* appSupportFolderString =
		[NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
	if(appSupportFolderString == nil) {
		appSupportFolderString = [NSHomeDirectory() stringByAppendingPathComponent:@"Documents"];
	}
#else
	FSRef appSupportFolder;
	OSErr error = noErr;
	NSString* appSupportFolderString;
	
	error = FSFindFolder(kUserDomain, kApplicationSupportFolderType, true, &appSupportFolder);

	if(error == noErr) {
		CFURLRef url = CFURLCreateFromFSRef(kCFAllocatorDefault, &appSupportFolder);
		appSupportFolderString = [(__bridge NSURL*) url path];
		CFRelease(url);
	} else {
		appSupportFolderString = [@"~/Library/Application Support" stringByExpandingTildeInPath];
	}
#endif
	
	[appSupportFolderString getCString:buffer maxLength:len-1 encoding:NSUTF8StringEncoding];
}
