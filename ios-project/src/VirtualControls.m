// license:BSD-3-Clause
#import <UIKit/UIKit.h>
#import <SDL.h>

// On-screen Start/Select + D-pad/face buttons that inject SDL keyboard events
// matching MAME's default SNES bindings (same mapping as Android VirtualControlsView).
//
// Also watches Documents/mamehub_inject.log for e2e key injects so the Mac host
// and iOS guest can be driven concurrently without stealing Simulator focus
// (Android equivalent: org.mamedev.mame.INJECT_KEY broadcast).
// Each append-only line: "<key> [hold_ms]"
// keys: up down left right a b x y start select

enum {
	kKeyUp = SDL_SCANCODE_UP,
	kKeyDown = SDL_SCANCODE_DOWN,
	kKeyLeft = SDL_SCANCODE_LEFT,
	kKeyRight = SDL_SCANCODE_RIGHT,
	kKeyY = SDL_SCANCODE_LCTRL,
	kKeyB = SDL_SCANCODE_LALT,
	kKeyA = SDL_SCANCODE_SPACE,
	kKeyX = SDL_SCANCODE_LSHIFT,
	kKeyStart = SDL_SCANCODE_1,
	kKeySelect = SDL_SCANCODE_5,
};

static void inject_scancode(SDL_Scancode scancode, SDL_bool pressed)
{
	SDL_Event event;
	SDL_zero(event);
	event.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
	event.key.state = pressed ? SDL_PRESSED : SDL_RELEASED;
	event.key.keysym.scancode = scancode;
	event.key.keysym.sym = SDL_GetKeyFromScancode(scancode);
	event.key.keysym.mod = KMOD_NONE;
	SDL_PushEvent(&event);
}

static SDL_Scancode scancode_for_name(NSString *name)
{
	name = name.lowercaseString;
	if ([name isEqualToString:@"up"]) return kKeyUp;
	if ([name isEqualToString:@"down"]) return kKeyDown;
	if ([name isEqualToString:@"left"]) return kKeyLeft;
	if ([name isEqualToString:@"right"]) return kKeyRight;
	if ([name isEqualToString:@"y"] || [name isEqualToString:@"btn1"]) return kKeyY;
	if ([name isEqualToString:@"b"] || [name isEqualToString:@"btn2"]) return kKeyB;
	if ([name isEqualToString:@"a"] || [name isEqualToString:@"space"] || [name isEqualToString:@"btn3"]) return kKeyA;
	if ([name isEqualToString:@"x"]) return kKeyX;
	if ([name isEqualToString:@"start"] || [name isEqualToString:@"1"]) return kKeyStart;
	if ([name isEqualToString:@"select"] || [name isEqualToString:@"coin"] || [name isEqualToString:@"5"]) return kKeySelect;
	return (SDL_Scancode)0;
}

static void hold_scancode(SDL_Scancode scancode, NSInteger holdMs)
{
	if (scancode == 0)
		return;
	if (holdMs < 40)
		holdMs = 40;
	inject_scancode(scancode, SDL_TRUE);
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(holdMs * NSEC_PER_MSEC)),
		dispatch_get_main_queue(), ^{
			inject_scancode(scancode, SDL_FALSE);
		});
}

@interface MAMEHubPadButton : UIButton
@property (nonatomic, assign) SDL_Scancode scancode;
@end

@implementation MAMEHubPadButton
@end

@interface MAMEHubVirtualControls : UIView
@end

@implementation MAMEHubVirtualControls {
	NSMutableArray<MAMEHubPadButton *> *_buttons;
	NSFileHandle *_injectHandle;
	unsigned long long _injectOffset;
	dispatch_source_t _injectTimer;
	dispatch_source_t _menuModeTimer;
	BOOL _menuMode;
}

+ (void)installIfNeeded
{
	static dispatch_once_t once;
	dispatch_once(&once, ^{
		dispatch_async(dispatch_get_main_queue(), ^{
			UIWindow *window = nil;
			for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
				if (![scene isKindOfClass:[UIWindowScene class]])
					continue;
				for (UIWindow *candidate in ((UIWindowScene *)scene).windows) {
					if (candidate.isKeyWindow) {
						window = candidate;
						break;
					}
				}
				if (window)
					break;
			}
			if (!window)
				window = UIApplication.sharedApplication.windows.firstObject;
			if (!window)
				return;
			MAMEHubVirtualControls *pad = [[MAMEHubVirtualControls alloc] initWithFrame:window.bounds];
			pad.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
			pad.userInteractionEnabled = YES;
			[window addSubview:pad];
			[pad startInjectWatcher];
		});
	});
}

- (instancetype)initWithFrame:(CGRect)frame
{
	self = [super initWithFrame:frame];
	if (!self)
		return nil;
	self.backgroundColor = UIColor.clearColor;
	_buttons = [NSMutableArray array];
	[self addButton:@"◀" scancode:kKeyLeft];
	[self addButton:@"▲" scancode:kKeyUp];
	[self addButton:@"▼" scancode:kKeyDown];
	[self addButton:@"▶" scancode:kKeyRight];
	[self addButton:@"Y" scancode:kKeyY];
	[self addButton:@"B" scancode:kKeyB];
	[self addButton:@"A" scancode:kKeyA];
	[self addButton:@"X" scancode:kKeyX];
	[self addButton:@"START" scancode:kKeyStart];
	[self addButton:@"SELECT" scancode:kKeySelect];
	_menuMode = SDL_GetHintBoolean("MAMEHUB_MENU_ACTIVE", SDL_FALSE) ? YES : NO;
	[self startMenuModeWatcher];
	return self;
}

- (NSString *)injectPath
{
	NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
	return [docs stringByAppendingPathComponent:@"mamehub_inject.log"];
}

- (void)startInjectWatcher
{
	NSString *path = [self injectPath];
	if (![[NSFileManager defaultManager] fileExistsAtPath:path])
		[[NSFileManager defaultManager] createFileAtPath:path contents:[NSData data] attributes:nil];
	_injectHandle = [NSFileHandle fileHandleForReadingAtPath:path];
	// Start at EOF so leftover lines from a prior run are ignored; e2e truncates
	// the file before launch when it wants a clean slate.
	_injectOffset = [_injectHandle seekToEndOfFile];
	_injectTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
	dispatch_source_set_timer(_injectTimer, dispatch_time(DISPATCH_TIME_NOW, 0),
		(uint64_t)(16 * NSEC_PER_MSEC), (uint64_t)(4 * NSEC_PER_MSEC));
	__weak MAMEHubVirtualControls *weakSelf = self;
	dispatch_source_set_event_handler(_injectTimer, ^{
		[weakSelf pollInjectFile];
	});
	dispatch_resume(_injectTimer);
	NSLog(@"MAMEHub: watching %@ for e2e key injects (offset=%llu)", path, _injectOffset);
}

- (void)pollInjectFile
{
	NSString *path = [self injectPath];
	NSDictionary *attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil];
	unsigned long long size = [attrs fileSize];
	if (size < _injectOffset) {
		// File was truncated/replaced.
		_injectOffset = 0;
		[_injectHandle seekToFileOffset:0];
	}
	if (size <= _injectOffset)
		return;
	[_injectHandle seekToFileOffset:_injectOffset];
	NSData *data = [_injectHandle readDataToEndOfFile];
	_injectOffset = [_injectHandle offsetInFile];
	if (data.length == 0)
		return;
	NSString *text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
	for (NSString *raw in [text componentsSeparatedByCharactersInSet:[NSCharacterSet newlineCharacterSet]]) {
		NSString *line = [raw stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
		if (line.length == 0)
			continue;
		NSArray<NSString *> *parts = [line componentsSeparatedByCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
		NSMutableArray<NSString *> *toks = [NSMutableArray array];
		for (NSString *p in parts) {
			if (p.length)
				[toks addObject:p];
		}
		if (toks.count < 1)
			continue;
		SDL_Scancode sc = scancode_for_name(toks[0]);
		NSInteger hold = 120;
		if (toks.count >= 2)
			hold = toks[1].integerValue;
		hold_scancode(sc, hold);
	}
}

- (void)addButton:(NSString *)title scancode:(SDL_Scancode)scancode
{
	MAMEHubPadButton *button = [MAMEHubPadButton buttonWithType:UIButtonTypeSystem];
	button.scancode = scancode;
	[button setTitle:title forState:UIControlStateNormal];
	button.titleLabel.font = [UIFont boldSystemFontOfSize:14];
	[button setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
	button.backgroundColor = [[UIColor blackColor] colorWithAlphaComponent:0.45];
	button.layer.cornerRadius = 10;
	button.layer.borderColor = [UIColor.whiteColor colorWithAlphaComponent:0.55].CGColor;
	button.layer.borderWidth = 1;
	[button addTarget:self action:@selector(press:) forControlEvents:UIControlEventTouchDown];
	[button addTarget:self action:@selector(release:) forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside | UIControlEventTouchCancel];
	[self addSubview:button];
	[_buttons addObject:button];
}

- (void)layoutSubviews
{
	[super layoutSubviews];
	CGFloat w = CGRectGetWidth(self.bounds);
	CGFloat h = CGRectGetHeight(self.bounds);
	CGFloat size = MIN(56.0, h * 0.18);
	CGFloat pad = 10;
	CGFloat left = pad + self.safeAreaInsets.left;
	CGFloat right = w - pad - self.safeAreaInsets.right;
	CGFloat bottom = h - pad - self.safeAreaInsets.bottom;

	// D-pad cluster
	_buttons[0].frame = CGRectMake(left, bottom - size * 2, size, size);                 // left
	_buttons[1].frame = CGRectMake(left + size, bottom - size * 3, size, size);           // up
	_buttons[2].frame = CGRectMake(left + size, bottom - size, size, size);              // down
	_buttons[3].frame = CGRectMake(left + size * 2, bottom - size * 2, size, size);       // right

	// Face buttons
	_buttons[4].frame = CGRectMake(right - size * 3, bottom - size * 2, size, size);      // Y
	_buttons[5].frame = CGRectMake(right - size * 2, bottom - size, size, size);           // B
	_buttons[6].frame = CGRectMake(right - size, bottom - size * 2, size, size);           // A
	_buttons[7].frame = CGRectMake(right - size * 2, bottom - size * 3, size, size);      // X

	CGFloat mid = w * 0.5;
	_buttons[8].frame = CGRectMake(mid - 70, bottom - size * 0.9, 65, 36);               // START
	_buttons[9].frame = CGRectMake(mid + 5, bottom - size * 0.9, 70, 36);                // SELECT

	if (_menuMode)
	{
		inject_scancode(kKeyStart, SDL_FALSE);
		inject_scancode(kKeySelect, SDL_FALSE);
		for (NSUInteger i = 4; i <= 7; ++i)
		{
			_buttons[i].hidden = YES;
			_buttons[i].enabled = NO;
		}
		_buttons[9].hidden = YES;
		_buttons[9].enabled = NO;
		_buttons[8].hidden = NO;
		_buttons[8].enabled = YES;
		_buttons[8].scancode = kKeySelect;
		[_buttons[8] setTitle:@"SELECT" forState:UIControlStateNormal];
		_buttons[8].frame = CGRectMake(mid - 70, bottom - size * 0.9, 140, 36);
	}
	else
	{
		inject_scancode(kKeyStart, SDL_FALSE);
		inject_scancode(kKeySelect, SDL_FALSE);
		for (NSUInteger i = 4; i <= 7; ++i)
		{
			_buttons[i].hidden = NO;
			_buttons[i].enabled = YES;
		}
		_buttons[9].hidden = NO;
		_buttons[9].enabled = YES;
		_buttons[8].scancode = kKeyStart;
		[_buttons[8] setTitle:@"START" forState:UIControlStateNormal];
	}
}

- (void)press:(MAMEHubPadButton *)sender
{
	inject_scancode(sender.scancode, SDL_TRUE);
}

- (void)release:(MAMEHubPadButton *)sender
{
	inject_scancode(sender.scancode, SDL_FALSE);
}

- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event
{
	UIView *hit = [super hitTest:point withEvent:event];
	return (hit == self) ? nil : hit;
}

- (void)startMenuModeWatcher
{
	_menuModeTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
	dispatch_source_set_timer(_menuModeTimer, dispatch_time(DISPATCH_TIME_NOW, 0),
		(uint64_t)(120 * NSEC_PER_MSEC), (uint64_t)(20 * NSEC_PER_MSEC));
	__weak MAMEHubVirtualControls *weakSelf = self;
	dispatch_source_set_event_handler(_menuModeTimer, ^{
		[weakSelf refreshMenuMode];
	});
	dispatch_resume(_menuModeTimer);
}

- (void)refreshMenuMode
{
	BOOL menuMode = SDL_GetHintBoolean("MAMEHUB_MENU_ACTIVE", SDL_FALSE) ? YES : NO;
	if (menuMode != _menuMode)
	{
		for (NSUInteger i = 4; i <= 9; ++i)
			inject_scancode(_buttons[i].scancode, SDL_FALSE);
		_menuMode = menuMode;
		[self setNeedsLayout];
	}
}

- (void)dealloc
{
	if (_injectTimer)
		dispatch_source_cancel(_injectTimer);
	if (_menuModeTimer)
		dispatch_source_cancel(_menuModeTimer);
}

@end

__attribute__((constructor))
static void mamehub_install_virtual_controls(void)
{
	// Delay until UIApplication exists.
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
		[MAMEHubVirtualControls installIfNeeded];
	});
}
