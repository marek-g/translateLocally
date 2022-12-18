#include "NativeMsgManager.h"
#include <QCoreApplication>
#include <QDebug> // For qWarning()
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>


// Firefox has a strict pattern extension IDs have to match, and these are
// checked when Firefox parses the manifest file. Chrome extension IDs don't
// match this pattern, nor does Chrome care much about the pattern of the IDs.
static QRegularExpression firefoxExtensionPattern("^\\{[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\}$|^[a-z0-9-._]*@[a-z0-9-._]+$", QRegularExpression::CaseInsensitiveOption);

bool NativeMsgManager::writeNativeMessagingAppManifests(QSet<QString> nativeMessagingClients) {
    // do not register itself with Firefox and Chrome
    return true;

    // See https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/Native_manifests
    // Intentionally lower case to avoid any issues/confusion with case-sensitive filesystems
    QString name = "translatelocally";

    QJsonObject manifest({
        {"name", name},
        {"description", "Fast and secure translation on your local machine, powered by marian and Bergamot."},
        {"type", "stdio"},
        {"path", QCoreApplication::applicationFilePath()},
    });

    enum ManifestVariant {
        Firefox,
        Chromium
    };

    QMap<ManifestVariant, QJsonObject> manifests;

    // For Firefox-based browsers (they do not like seeing "allowed_origins")
    // only add the ones that match Firefox' schema definition. Otherwise it will
    // fail to parse the manifest JSON.
    // See https://searchfox.org/mozilla-central/rev/3419858c997f422e3e70020a46baae7f0ec6dacc/toolkit/components/extensions/schemas/manifest.json#571-583
    QStringList extensions;
    for (QString const &extension : nativeMessagingClients.values())
        if (firefoxExtensionPattern.match(extension).hasMatch())
            extensions << extension;

    manifests.insert(Firefox, manifest);
    manifests[Firefox]["allowed_extensions"] = QJsonArray::fromStringList(extensions);

    // Chromium-based browsers look for full url origins
    QStringList origins;
    for (QString const &extension : nativeMessagingClients.values())
        origins << QString("chrome-extension://%1/").arg(extension);

    manifests.insert(Chromium, manifest);
    manifests[Chromium]["allowed_origins"] = QJsonArray::fromStringList(origins);

    QList<QPair<ManifestVariant,QString>> manifestPaths;

#if defined(Q_OS_MACOS)
    manifestPaths << qMakePair(Firefox,  QString("%1/Mozilla/NativeMessagingHosts/%2.json").arg(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).arg(name));
    manifestPaths << qMakePair(Chromium, QString("%1/Google/Chrome/NativeMessagingHosts/%2.json").arg(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).arg(name));
    manifestPaths << qMakePair(Chromium, QString("%1/Chromium/NativeMessagingHosts/%2.json").arg(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).arg(name));
#elif defined (Q_OS_LINUX)
    manifestPaths << qMakePair(Firefox,  QString("%1/.mozilla/native-messaging-hosts/%2.json").arg(QStandardPaths::writableLocation(QStandardPaths::HomeLocation)).arg(name));
    manifestPaths << qMakePair(Chromium, QString("%1/.config/google-chrome/NativeMessagingHosts/%2.json").arg(QStandardPaths::writableLocation(QStandardPaths::HomeLocation)).arg(name));
    manifestPaths << qMakePair(Chromium, QString("%1/.config/chromium/NativeMessagingHosts/%2.json").arg(QStandardPaths::writableLocation(QStandardPaths::HomeLocation)).arg(name));
#elif defined (Q_OS_WIN)
    // On Windows, we write the manifest to some safe directory, and then point to it from the Registry.
    manifestPaths << qMakePair(Firefox,  QString("%1/%2-firefox.json").arg(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).arg(name));
    // All Chromium based browsers can understand the same file but we'll write different registry keys for each of them.
    manifestPaths << qMakePair(Chromium, QString("%1/%2-chromium.json").arg(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).arg(name));
#else
    return false;
#endif

    for (auto &&manifestPath : manifestPaths) {
        QFileInfo manifestInfo(manifestPath.second);

        if (!manifestInfo.dir().exists()) {
            if (!QDir().mkpath(manifestInfo.absolutePath())) {
                qWarning() << "Cannot create directory:" << manifestInfo.absolutePath();
                return false;
            }
        }

        QFile manifestFile(manifestPath.second);
        manifestFile.open(QFile::WriteOnly);
        manifestFile.write(QJsonDocument(manifests[manifestPath.first]).toJson());
    
#if defined (Q_OS_WIN)
        // For windows, we make the registry keys for all browsers point to this manifest file.
        QStringList registryKeys;

        // Since we have different manifest files for different browsers, only
        // set the registry entries for that variant.
        switch (manifestPath.first) {
            case Firefox:
                registryKeys << QString("HKEY_CURRENT_USER\\Software\\Mozilla\\NativeMessagingHosts\\%1").arg(name);
                break;
            case Chromium:
                registryKeys << QString("HKEY_CURRENT_USER\\Software\\Google\\Chrome\\NativeMessagingHosts\\%1").arg(name);
                registryKeys << QString("HKEY_CURRENT_USER\\Software\\Chromium\\NativeMessagingHosts\\%1").arg(name);
                break;    
        }

        for (QString const &key : registryKeys)
            QSettings(key, QSettings::NativeFormat).setValue("Default", manifestPath.second);
#endif
    }

    return true;
}
