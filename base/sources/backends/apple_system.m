// System functions shared by the macOS and iOS backends
#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>
#include <iron_system.h>
#include <limits.h>
#include <mach/mach_time.h>
#include <string.h>

bool with_autoreleasepool(bool (*f)(void)) {
	@autoreleasepool {
		return f();
	}
}

double iron_frequency(void) {
	mach_timebase_info_data_t info;
	mach_timebase_info(&info);
	return (double)info.denom / (double)info.numer / 1e-9;
}

uint64_t iron_timestamp(void) {
	return mach_absolute_time();
}

// Two letter code of the preferred language
const char *iron_language(void) {
	static char language[3];
	NSString   *nsstr = [[NSLocale preferredLanguages] firstObject];
	const char *lang  = nsstr != nil ? [nsstr UTF8String] : "en";
	snprintf(language, sizeof(language), "%s", lang);
	return language;
}

// File of the system UI font (SF on Apple platforms), NULL when it can not be found
const char *iron_system_font_path(void) {
	static char path[1024] = {0};
	static bool looked     = false;
	if (!looked) {
		looked         = true;
		CTFontRef font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, 0.0, NULL);
		if (font != NULL) {
			CFURLRef url = (CFURLRef)CTFontCopyAttribute(font, kCTFontURLAttribute);
			if (url != NULL) {
				if (!CFURLGetFileSystemRepresentation(url, true, (UInt8 *)path, sizeof(path))) {
					path[0] = 0;
				}
				CFRelease(url);
			}
			CFRelease(font);
		}
	}
	return path[0] != 0 ? path : NULL;
}

const char *iron_get_resource_path(void) {
	static char path[PATH_MAX] = {0};
	if (path[0] == 0) {
		snprintf(path, sizeof(path), "%s", [[[NSBundle mainBundle] resourcePath] UTF8String]);
	}
	return path;
}

// Application Support/<app name>/, created on first use
const char *iron_internal_save_path(void) {
	static char path[PATH_MAX]      = {0};
	static char path_name[PATH_MAX] = {0};
	const char *name                = iron_application_name();
	if (path[0] == 0 || strcmp(path_name, name) != 0) {
		snprintf(path_name, sizeof(path_name), "%s", name);
		NSArray  *paths    = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
		NSString *resolved = [[paths objectAtIndex:0] stringByAppendingPathComponent:[NSString stringWithUTF8String:name]];
		[[NSFileManager defaultManager] createDirectoryAtPath:resolved withIntermediateDirectories:YES attributes:nil error:nil];
		snprintf(path, sizeof(path), "%s/", [resolved UTF8String]);
	}
	return path;
}
