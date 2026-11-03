#include "Platform.h"

#include "Lang.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSettings>
#include <QUrl>
#include <QWidget>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <wincred.h>

#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif

namespace packix::platform {

namespace {

const wchar_t* kCredential = L"Packix/passwords";
const QString kRunKey = QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");

} // namespace

QString fileManagerName()
{
    return ui::L("platform.fileManager");
}

void reveal(const QString& path)
{
    QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,") + QDir::toNativeSeparators(path)});
}

void quickLook(const QStringList& paths)
{
    // No Quick Look on Windows: open the file in its program.
    if (!paths.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(paths.first()));
}

bool reduceMotion()
{
    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    return !animations;
}

QString keys(QString text)
{
    text.replace(QStringLiteral("⌘⌫"), QStringLiteral("Ctrl+Backspace"));
    text.replace(QStringLiteral("⌫"), QStringLiteral("Delete"));
    text.replace(QStringLiteral("⇧⌘"), QStringLiteral("Ctrl+Shift+"));
    text.replace(QStringLiteral("⌥⌘"), QStringLiteral("Ctrl+Alt+"));
    text.replace(QStringLiteral("⌘"), QStringLiteral("Ctrl+"));
    text.replace(QStringLiteral("⇧"), QStringLiteral("Shift+"));
    text.replace(QStringLiteral("⌥"), QStringLiteral("Alt+"));
    return text;
}

void setForeground(bool)
{
    // Windows shows open windows in the taskbar by itself.
}

void tintTitleBar(QWidget* window, const QColor& c)
{
    // Windows 11 colours the caption; older versions ignore the call.
    const COLORREF color = RGB(c.red(), c.green(), c.blue());
    DwmSetWindowAttribute(reinterpret_cast<HWND>(window->window()->winId()), DWMWA_CAPTION_COLOR, &color, sizeof(color));
}

void activate() {}

// ---------------------------------------------------------------------------
// Credential Manager: one generic credential holding a JSON list.

QStringList savedPasswords()
{
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(kCredential, CRED_TYPE_GENERIC, 0, &cred))
        return {};
    const QByteArray json(reinterpret_cast<const char*>(cred->CredentialBlob), qsizetype(cred->CredentialBlobSize));
    CredFree(cred);
    QStringList out;
    for (const QJsonValue& v : QJsonDocument::fromJson(json).array())
        out << v.toString();
    return out;
}

namespace {

void storePasswords(const QStringList& list)
{
    QByteArray json = QJsonDocument(QJsonArray::fromStringList(list)).toJson(QJsonDocument::Compact);
    CREDENTIALW cred = {};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<wchar_t*>(kCredential);
    cred.CredentialBlobSize = DWORD(json.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(json.data());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName = const_cast<wchar_t*>(L"Packix");
    CredWriteW(&cred, 0);
}

} // namespace

void savePassword(const QString& password)
{
    QStringList list = savedPasswords();
    list.removeAll(password);
    list.prepend(password);
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
    // Mark of the Web lives in the "Zone.Identifier" stream next to the file's data.
    QFile zone(QDir::toNativeSeparators(archive) + QStringLiteral(":Zone.Identifier"));
    if (!zone.open(QIODevice::ReadOnly))
        return;
    const QByteArray mark = zone.readAll();
    auto stamp = [&](const QString& file) {
        QFile out(QDir::toNativeSeparators(file) + QStringLiteral(":Zone.Identifier"));
        if (out.open(QIODevice::WriteOnly))
            out.write(mark);
    };
    if (QFileInfo(createdPath).isDir()) {
        QDirIterator it(createdPath, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (it.hasNext())
            stamp(it.next());
    } else {
        stamp(createdPath);
    }
}

bool loginItemEnabled()
{
    return QSettings(kRunKey, QSettings::NativeFormat).contains(QStringLiteral("Packix"));
}

bool setLoginItem(bool enabled)
{
    QSettings run(kRunKey, QSettings::NativeFormat);
    if (enabled)
        run.setValue(QStringLiteral("Packix"), QStringLiteral("\"%1\" --background")
                                                   .arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
    else
        run.remove(QStringLiteral("Packix"));
    run.sync();
    return run.status() == QSettings::NoError;
}

bool isDefaultArchiver()
{
    const QSettings choice(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.zip\\UserChoice"),
                           QSettings::NativeFormat);
    return choice.value(QStringLiteral("ProgId")).toString() == QLatin1String("Packix.Archive");
}

void makeDefaultArchiver()
{
    // Since Windows 8 a program cannot make itself the default; Settings opens on Packix instead.
    QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:defaultapps?registeredAppUser=Packix")));
}

void notify(const QString&, const QString&, const QString&)
{
    // Notifications are a macOS-only feature of Packix.
}

void installServices(std::function<void(const QStringList&)>)
{
    // Explorer's "Archive with Packix" comes from the installer; it starts Packix with --archive.
}

} // namespace packix::platform
