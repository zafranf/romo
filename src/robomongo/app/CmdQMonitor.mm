// Cmd+Q: close the open modal dialog first, quit on the second press.
//
// When a modal dialog is open, Qt disables the ENTIRE native menu bar
// (QCocoaMenuBar::shouldDisable in qcocoamenubar.mm), so the Quit item's
// Cmd+Q key equivalent is swallowed by AppKit before it ever reaches Qt.
// A local NSEvent monitor sees the keystroke FIRST — before NSApp's
// dispatch — so we can turn the first Cmd+Q into an Esc-like close of the
// active dialog, and let the second one fall through to the normal Quit
// path (by then the dialog is gone and the menu bar is enabled again).

#import <AppKit/AppKit.h>

#include <QApplication>
#include <QDialog>
#include <QWidget>

static id g_cmdqMonitor = nil;

void romoInstallCmdQMonitor()
{
    if (g_cmdqMonitor)
        return;

    g_cmdqMonitor = [[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
        handler:^NSEvent *(NSEvent *event) {
            // Plain Cmd+Q only (no Shift/Ctrl/Option — those are other shortcuts).
            if ((event.modifierFlags & NSEventModifierFlagCommand) == 0)
                return event;
            const NSEventModifierFlags extra = NSEventModifierFlagShift |
                                               NSEventModifierFlagControl |
                                               NSEventModifierFlagOption;
            if (event.modifierFlags & extra)
                return event;
            if ([[[event charactersIgnoringModifiers] lowercaseString] isEqualToString:@"q"] == NO)
                return event;

            QWidget *modal = QApplication::activeModalWidget();

            // No modal dialog: pass the event through untouched so the
            // normal Quit menu item handles it (quit the app as usual).
            if (!modal)
                return event;

            // Modal dialog up: close it (same semantics as Esc) and consume
            // the keystroke so it does not additionally trigger a quit.
            if (QDialog *dialog = qobject_cast<QDialog *>(modal))
                dialog->reject();
            else
                modal->close();

            return nil;
        }] retain];

    (void)g_cmdqMonitor;
}
