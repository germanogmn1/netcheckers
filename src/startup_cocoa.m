#import <Cocoa/Cocoa.h>

#include "startup.h"

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
            BOOL valid = ((i > 0 && c >= '0') || c >= '1') && c <= '9';
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

static startup_info_t info;

@interface StartupWindow : NSWindowController
{
    NSTimer *timer;
}
@property (strong) IBOutlet NSSegmentedControl *modeControl;
@property (strong) IBOutlet NSTextField *hostField;
@property (strong) IBOutlet NSTextField *portField;
@property (strong) IBOutlet NSTextField *hostLabel;
@property (strong) IBOutlet NSWindow *loadingSheet;
@property (strong) IBOutlet NSProgressIndicator *loadingBar;
@property (strong) IBOutlet NSTextField *loadingMessage;
@property (strong) IBOutlet NSButton *cancelWaitingButtton;
@end

@implementation StartupWindow
@synthesize modeControl;
@synthesize hostField;
@synthesize portField;
@synthesize hostLabel;
@synthesize loadingSheet;
@synthesize loadingBar;
@synthesize loadingMessage;
@synthesize cancelWaitingButtton;
- (void)windowDidLoad {
    [super windowDidLoad];
    [portField setIntValue:2432];
    [self modeChanged:modeControl];

    self.window.releasedWhenClosed = YES;
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(terminate:)
                                                 name:NSWindowWillCloseNotification
                                               object:self.window];
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
    info.net_mode = (modeControl.selectedSegment == 0) ? NET_SERVER : NET_CLIENT;
    strncpy(info.host, hostField.stringValue.UTF8String, sizeof(info.host));
    sprintf(info.port, "%d", portField.intValue);
    strcpy(info.assets_path, [NSBundle mainBundle].resourcePath.UTF8String);

    NSString *message;
    if (info.net_mode == NET_SERVER)
        message = [NSString stringWithFormat:NSLocalizedString(@"server_waiting", nil), info.port];
    else
        message = [NSString stringWithFormat:NSLocalizedString(@"client_waiting", nil), info.host, info.port];

    [loadingMessage setStringValue:message];
    [loadingBar startAnimation:nil];

    cancelWaitingButtton.enabled = true;
    [self.window beginSheet:loadingSheet completionHandler:nil];

    net_start(info.network, info.net_mode, info.host, info.port);

    timer = [NSTimer scheduledTimerWithTimeInterval:0.1
                                             target:self
                                           selector:@selector(pollNetwork)
                                           userInfo:nil
                                            repeats:true];
}
- (void)pollNetwork {
    net_state_t state = net_get_state(info.network);
    if (state != NET_CONNECTING) {
        [timer invalidate];
        [self.window endSheet:loadingSheet];

        if (state == NET_RUNNING) {
            [self close];
        } else if (state == NET_ERROR) {
            NSAlert *alert = [[NSAlert alloc] init];
            switch (net_get_error(info.network)) {
                case NET_ENONE:
                    [NSException raise:@"Invalid state"
                                format:@"error state but no error is set"];
                    break;
                case NET_EUNKNOWN:
                    [NSException raise:@"Failed to start network"
                                format:@"%s", net_error_str(info.network)];
                    break;
                case NET_EPORTINUSE:
                    [alert setInformativeText:@"NET_EPORTINUSE"];
                    break;
                case NET_EPORTNOACCESS:
                    [alert setInformativeText:@"NET_EPORTNOACCESS"];
                    break;
                case NET_ECONNREFUSED:
                    [alert setInformativeText:@"NET_ECONNREFUSED"];
                    break;
                case NET_EDNSFAIL:
                    [alert setInformativeText:@"NET_EDNSFAIL"];
                    break;
            }
            [alert setMessageText:@"Error"];

            [alert setAlertStyle:NSWarningAlertStyle];
            [alert beginSheetModalForWindow:self.window
                          completionHandler:nil];
        }
    }
}
- (IBAction)cancelWaiting:(NSButton *)sender {
    net_stop(info.network);
}
- (void)terminate:(id)sender {
    [NSApp stop:self];
    [NSApp activateIgnoringOtherApps:YES];
}
@end

extern startup_info_t startup(int argc, char **argv) {
    info.network = net_init();

    NSApplication *app = [NSApplication sharedApplication];

    NSNib *nib = [[NSNib alloc] initWithNibNamed:@"MainMenu" bundle:[NSBundle mainBundle]];
    [nib instantiateWithOwner:app topLevelObjects:nil];

    StartupWindow *startWindow = [[StartupWindow alloc] initWithWindowNibName:@"StartupWindow"];
    [startWindow showWindow:nil];
    [startWindow.window makeKeyAndOrderFront:nil];

    [app run];

    net_state_t state = net_get_state(info.network);
    if (state == NET_RUNNING) {
        info.success = true;
    } else if (state == NET_CLOSED || state == NET_ERROR) {
        info.success = false;
    } else {
        [NSException raise:@"Invalid state"
                    format:@"Startup window closed but networking is running"];
    }

    return info;
}
