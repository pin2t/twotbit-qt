#include "ownerauth.h"

#include <QEventLoop>
#include <QTimer>
#include <QWidget>

#import <Foundation/Foundation.h>
#import <LocalAuthentication/LocalAuthentication.h>

#include <atomic>
#include <memory>

#include "util/log.h"

namespace OwnerAuth {

// Touch ID, or the account's password where there is no Touch ID, as the
// Swift app asks before it shows the phrase. Where the Mac can check
// neither, as on an account without a password, the phrase is shown.
bool confirmOwner(QWidget* parent, const QString& reason) {
    @autoreleasepool {
        LAContext* context = [[LAContext alloc] init];
        NSError* error = nil;
        if (![context canEvaluatePolicy:LAPolicyDeviceOwnerAuthentication error:&error]) {
            tb::Log::info("settings: owner check unavailable, " + QString::fromNSString(error.localizedDescription).toStdString());
            return true;
        }
        QWidget* top = parent ? parent->window() : nullptr;
        if (top) top->setEnabled(false);
        // The reply comes on a queue of its own; the window keeps painting
        // until it does.
        struct Reply {
            std::atomic<bool> done{false};
            std::atomic<bool> confirmed{false};
        };
        auto reply = std::make_shared<Reply>();
        [context evaluatePolicy:LAPolicyDeviceOwnerAuthentication
                localizedReason:reason.toNSString()
                          reply:^(BOOL success, NSError*) {
                              reply->confirmed = success;
                              reply->done = true;
                          }];
        QEventLoop loop;
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
            if (reply->done) loop.quit();
        });
        poll.start(50);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
        if (top) {
            top->setEnabled(true);
            top->activateWindow();
        }
        return reply->confirmed;
    }
}

} // namespace OwnerAuth
