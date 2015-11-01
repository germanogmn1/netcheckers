#import <Cocoa/Cocoa.h>

#include "startup.h"

static startup_info_t info;
static enum { OPEN, STARTED, CANCELED } window_state;

@interface PortValueFormatter : NSNumberFormatter
@end

@implementation PortValueFormatter
- (BOOL)isPartialStringValid:(NSString*)value newEditingString:(NSString**)newString errorDescription:(NSString**)error
{
    bool result = YES;
    if (value.length > 5) {
        result = NO;
    } else {
        for (int i = 0; i < value.length; i++) {
            unichar c = [value characterAtIndex:i];
            if (c < '0' || c > '9') {
                result = NO;
                break;
            }
        }
    }
    if (!result)
        NSBeep();
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
