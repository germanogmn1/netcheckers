//
//  osx_setup.m
//  NetCheckers
//
//  Created by Germano Nicolini on 01/11/15.
//  Copyright © 2015 Germano Nicolini. All rights reserved.
//

#import <Cocoa/Cocoa.h>

static startup_info_t info;
enum { OPEN, STARTED, CANCELED } window_state;

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
    strcpy(info.host, hostField.stringValue.UTF8String);
    strcpy(info.port, portField.stringValue.UTF8String);
    info.server_mode = (modeControl.selectedSegment == 0);

    window_state = STARTED;
    [self.window close];
}

- (void)willClose:(NSNotification*)note {
    if (window_state != STARTED)
        window_state = CANCELED;
}

@end

extern startup_info_t osx_startup() {
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