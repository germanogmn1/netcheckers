#import <Cocoa/Cocoa.h>

#include "startup.h"

static startup_info_t info;
static enum { OPEN, STARTED, CANCELED } window_state;

@interface PortValueFormatter : NSFormatter
@end

@implementation PortValueFormatter
- (NSString *)stringForObjectValue:(id)anObject {
    NSString *result = nil;
    if ([anObject isKindOfClass:[NSNumber class]]) {
        result = [NSString stringWithFormat:@"%ld", [anObject integerValue]];
    }
    return result;
}
- (BOOL)getObjectValue:(id *)obj forString:(NSString *)string errorDescription:(NSString  **)error {
    BOOL result = NO;
    if ([self stringIsValid:string]) {
        *obj = [NSNumber numberWithInt:string.intValue];
        result = YES;
    }
    return result;
}
- (BOOL)isPartialStringValid:(NSString *)value newEditingString:(NSString **)newString errorDescription:(NSString **)error {
    BOOL result = [self stringIsValid:value];
    if (!result)
        NSBeep();
    return result;
}
- (BOOL)stringIsValid:(NSString *)string {
    BOOL result = YES;
    if (string.length > 5) {
        result = NO;
    } else {
        for (int i = 0; i < string.length; i++) {
            unichar c = [string characterAtIndex:i];
            BOOL valid = ((i > 0 && c >= '1') || c >= '1') && c <= '9';
            if (!valid) {
                result = NO;
                break;
            }
        }
        if (result && [string intValue] > UINT16_MAX)
            result = NO;
    }
    return result;
}
@end

@interface StartupWindow : NSWindowController
@property (strong) IBOutlet NSSegmentedControl *modeControl;
@property (strong) IBOutlet NSTextField *hostField;
@property (strong) IBOutlet NSTextField *portField;
@property (strong) IBOutlet NSTextField *hostLabel;
@end

@implementation StartupWindow

@synthesize modeControl;
@synthesize hostField;
@synthesize portField;
@synthesize hostLabel;

- (void)windowDidLoad {
    [super windowDidLoad];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(willClose:)
                                                 name:NSWindowWillCloseNotification
                                               object:self.window];
    [portField setIntValue:2432];
    [self modeChanged:modeControl];
}

- (IBAction)modeChanged:(NSSegmentedControl *)sender {
    if (sender.selectedSegment == 0) {
        hostField.enabled = NO;
        hostLabel.textColor = [NSColor disabledControlTextColor];
    } else {
        hostField.enabled = YES;
        hostLabel.textColor = [NSColor labelColor];
    }
}

- (IBAction)startGame:(NSButton *)sender {
    strncpy(info.host, hostField.stringValue.UTF8String, sizeof(info.host));
    sprintf(info.port, "%d", portField.intValue);
    info.server_mode = (modeControl.selectedSegment == 0);

    window_state = STARTED;
    [self.window close];
}

- (void)willClose:(NSNotification*)note {
    if (window_state != STARTED)
        window_state = CANCELED;
}

- (void)terminate:(id)sender {
    window_state = CANCELED;
}

@end

extern startup_info_t startup_cocoa() {
    NSApplication *app = [NSApplication sharedApplication];
    NSNib *nib = [[NSNib alloc] initWithNibNamed:@"MainMenu" bundle:[NSBundle mainBundle]];
    [nib instantiateWithOwner:app topLevelObjects:nil];

    strcpy(info.assets_path, [NSBundle mainBundle].resourcePath.UTF8String);

    StartupWindow *startWindow = [[StartupWindow alloc] initWithWindowNibName:@"StartupWindow"];
    [startWindow showWindow:nil];

    while (window_state == OPEN) {
        NSEvent *event = [app nextEventMatchingMask:NSAnyEventMask
                                          untilDate:[NSDate distantFuture]
                                             inMode:NSDefaultRunLoopMode
                                            dequeue:YES];
        [app sendEvent:event];
        [app updateWindows];
    }

    if (window_state == STARTED) {
        info.success = true;
    } else {
        info.success = false;
    }

    return info;
}
