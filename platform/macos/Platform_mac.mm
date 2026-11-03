#include "Platform.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>

#import <AppKit/AppKit.h>
#import <Security/Security.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <UserNotifications/UserNotifications.h>

#include <sys/xattr.h>

#include "Formats.h"

namespace {

NSString* ns(const QString& s)
{
    return s.toNSString();
}

std::function<void(const QStringList&)> g_onArchive;

const QString kAgentLabel = QStringLiteral("com.umid.packix");

QString agentPlist()
{
    return QDir::homePath() + QStringLiteral("/Library/LaunchAgents/") + kAgentLabel + QStringLiteral(".plist");
}

} // namespace

// Finder hands selected files to this object (Info.plist: NSServices, NSMessage = archiveFiles).
@interface PackixServices : NSObject
- (void)archiveFiles:(NSPasteboard*)pboard userData:(NSString*)userData error:(NSString**)error;
@end

@implementation PackixServices
- (void)archiveFiles:(NSPasteboard*)pboard userData:(NSString*)userData error:(NSString**)error
{
    (void)userData;
    (void)error;
    NSArray<NSURL*>* urls = [pboard readObjectsForClasses:@[ NSURL.class ]
                                                  options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
    QStringList files;
    for (NSURL* u in urls)
        files << QString::fromNSString(u.path);
    if (!files.isEmpty() && g_onArchive)
        g_onArchive(files);
}
@end

// Clicking "Архив готов" reveals the file.
@interface PackixNotifications : NSObject <UNUserNotificationCenterDelegate>
@end

@implementation PackixNotifications
- (void)userNotificationCenter:(UNUserNotificationCenter*)center
    willPresentNotification:(UNNotification*)notification
      withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler
{
    (void)center;
    (void)notification;
    completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionSound);
}

- (void)userNotificationCenter:(UNUserNotificationCenter*)center
    didReceiveNotificationResponse:(UNNotificationResponse*)response
             withCompletionHandler:(void (^)(void))completionHandler
{
    (void)center;
    NSString* path = response.notification.request.content.userInfo[@"path"];
    if (path.length)
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:path] ]];
    completionHandler();
}
@end

namespace packix::platform {

QString fileManagerName()
{
    return QStringLiteral("Finder");
}

void reveal(const QString& path)
{
    [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:ns(path)] ]];
}

void quickLook(const QStringList& paths)
{
    QProcess::startDetached(QStringLiteral("/usr/bin/qlmanage"), QStringList{QStringLiteral("-p")} + paths);
}

bool reduceMotion()
{
    return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion];
}

QString keys(QString text)
{
    return text;
}

void setForeground(bool hasWindows)
{
    [NSApp setActivationPolicy:hasWindows ? NSApplicationActivationPolicyRegular : NSApplicationActivationPolicyAccessory];
}

void activate()
{
    [NSApp activate];
}

// ---------------------------------------------------------------------------
// Keychain: one item, a JSON list of passwords.

namespace {

NSDictionary* keychainQuery()
{
    return @{
        (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService : @"Packix",
        (__bridge id)kSecAttrAccount : @"passwords",
    };
}

void storePasswords(const QStringList& list)
{
    const QByteArray json = QJsonDocument(QJsonArray::fromStringList(list)).toJson(QJsonDocument::Compact);
    NSData* data = [NSData dataWithBytes:json.constData() length:NSUInteger(json.size())];
    NSDictionary* query = keychainQuery();
    OSStatus st = SecItemUpdate((__bridge CFDictionaryRef)query,
                                (__bridge CFDictionaryRef) @{(__bridge id)kSecValueData : data});
    if (st == errSecItemNotFound) {
        NSMutableDictionary* add = [query mutableCopy];
        add[(__bridge id)kSecValueData] = data;
        add[(__bridge id)kSecAttrLabel] = @"Packix — сохранённые пароли архивов";
        SecItemAdd((__bridge CFDictionaryRef)add, nullptr);
    }
}

} // namespace

QStringList savedPasswords()
{
    NSMutableDictionary* query = [keychainQuery() mutableCopy];
    query[(__bridge id)kSecReturnData] = @YES;
    query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
    CFTypeRef result = nullptr;
    if (SecItemCopyMatching((__bridge CFDictionaryRef)query, &result) != errSecSuccess || !result)
        return {};
    NSData* data = (__bridge_transfer NSData*)result;
    const QByteArray json(static_cast<const char*>(data.bytes), qsizetype(data.length));
    QStringList out;
    for (const QJsonValue& v : QJsonDocument::fromJson(json).array())
        out << v.toString();
    return out;
}

void savePassword(const QString& password)
{
    QStringList list = savedPasswords();
    list.removeAll(password);
    list.prepend(password);   // most recent first: tried first
    storePasswords(list);
}

void forgetPassword(const QString& password)
{
    QStringList list = savedPasswords();
    list.removeAll(password);
    storePasswords(list);
}

// ---------------------------------------------------------------------------

void inheritQuarantine(const QString& archive, const QString& createdPath)
{
    const QByteArray from = QFile::encodeName(archive);
    char value[1024];
    const ssize_t len = getxattr(from.constData(), "com.apple.quarantine", value, sizeof(value), 0, 0);
    if (len <= 0)
        return;
    auto mark = [&](const QString& p) {
        setxattr(QFile::encodeName(p).constData(), "com.apple.quarantine", value, size_t(len), 0, XATTR_NOFOLLOW);
    };
    mark(createdPath);
    if (QFileInfo(createdPath).isDir() && !QFileInfo(createdPath).isSymLink()) {
        QDirIterator it(createdPath, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
        while (it.hasNext())
            mark(it.next());
    }
}

// ---------------------------------------------------------------------------
// Start at login: a LaunchAgent that starts Packix with --background (no window).

bool loginItemEnabled()
{
    return QFileInfo::exists(agentPlist());
}

bool setLoginItem(bool enabled)
{
    if (!enabled)
        return QFile::remove(agentPlist()) || !QFileInfo::exists(agentPlist());
    QDir().mkpath(QFileInfo(agentPlist()).absolutePath());
    NSDictionary* plist = @{
        @"Label" : ns(kAgentLabel),
        @"ProgramArguments" : @[ ns(QCoreApplication::applicationFilePath()), @"--background" ],
        @"RunAtLoad" : @YES,
        @"ProcessType" : @"Interactive",
    };
    return [plist writeToURL:[NSURL fileURLWithPath:ns(agentPlist())] error:nil];
}

// ---------------------------------------------------------------------------

namespace {

NSArray<UTType*>* archiveTypes()
{
    NSMutableArray<UTType*>* types = [NSMutableArray array];
    for (const packix::Format& f : packix::Formats::instance().all()) {
        if (!f.defaultHandler)
            continue;
        for (const QString& ext : f.extensions) {
            UTType* t = [UTType typeWithFilenameExtension:ns(ext)];
            if (t && !t.dynamic && ![types containsObject:t])
                [types addObject:t];
        }
    }
    return types;
}

} // namespace

bool isDefaultArchiver()
{
    NSURL* me = [[NSBundle mainBundle] bundleURL];
    UTType* zip = [UTType typeWithFilenameExtension:@"zip"];
    NSURL* handler = [[NSWorkspace sharedWorkspace] URLForApplicationToOpenContentType:zip];
    return handler && [handler.URLByStandardizingPath isEqual:me.URLByStandardizingPath];
}

void makeDefaultArchiver()
{
    NSURL* me = [[NSBundle mainBundle] bundleURL];
    for (UTType* t in archiveTypes())
        [[NSWorkspace sharedWorkspace] setDefaultApplicationAtURL:me toOpenContentType:t completionHandler:^(NSError*){}];
}

// ---------------------------------------------------------------------------

void notify(const QString& title, const QString& text, const QString& revealPath)
{
    static PackixNotifications* delegate = [PackixNotifications new];
    UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
    center.delegate = delegate;
    NSString* t = ns(title);
    NSString* body = ns(text);
    NSString* path = ns(revealPath);
    [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                          completionHandler:^(BOOL granted, NSError*) {
                              if (!granted)
                                  return;
                              UNMutableNotificationContent* c = [UNMutableNotificationContent new];
                              c.title = t;
                              c.body = body;
                              c.sound = [UNNotificationSound defaultSound];
                              c.userInfo = @{@"path" : path};
                              UNNotificationRequest* r = [UNNotificationRequest requestWithIdentifier:[NSUUID UUID].UUIDString
                                                                                              content:c
                                                                                              trigger:nil];
                              [center addNotificationRequest:r withCompletionHandler:nil];
                          }];
}

void installServices(std::function<void(const QStringList&)> onArchive)
{
    static PackixServices* provider = [PackixServices new];
    g_onArchive = std::move(onArchive);
    [NSApp setServicesProvider:provider];
    NSUpdateDynamicServices();
    // Keep the login agent pointing at this copy of the app.
    if (loginItemEnabled())
        setLoginItem(true);
}

} // namespace packix::platform
