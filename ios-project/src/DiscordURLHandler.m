// license:BSD-3-Clause
#import <UIKit/UIKit.h>

// Forward Discord OAuth deep-link callbacks into the Social SDK when possible.
// Redirect URI registered in Discord portal / Info.plist:
//   discord-1545444437482676284:/authorize/callback

static BOOL mamehub_handle_discord_url(NSURL *url)
{
	if (!url)
		return NO;
	NSString *scheme = url.scheme ?: @"";
	if (![scheme hasPrefix:@"discord-"])
		return NO;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
	Class cls = NSClassFromString(@"DiscordSocialSdk");
	if (cls && [cls respondsToSelector:@selector(handleURL:)]) {
		[cls performSelector:@selector(handleURL:) withObject:url];
		return YES;
	}
#pragma clang diagnostic pop

	[[NSNotificationCenter defaultCenter] postNotificationName:@"MAMEHubDiscordURL"
	                                                    object:nil
	                                                  userInfo:@{@"url": url.absoluteString ?: @""}];
	return YES;
}

BOOL MAMEHubHandleDiscordURL(NSURL *url)
{
	return mamehub_handle_discord_url(url);
}

__attribute__((constructor))
static void mamehub_register_discord_url_observer(void)
{
	[[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationDidFinishLaunchingNotification
	                                                  object:nil
	                                                   queue:nil
	                                              usingBlock:^(NSNotification *note) {
		NSDictionary *info = note.userInfo;
		NSURL *url = info[UIApplicationLaunchOptionsURLKey];
		if (url)
			mamehub_handle_discord_url(url);
	}];
}
