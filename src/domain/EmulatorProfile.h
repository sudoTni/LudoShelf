#ifndef LUDOSHELF_DOMAIN_EMULATORPROFILE_H
#define LUDOSHELF_DOMAIN_EMULATORPROFILE_H

#include <QString>
#include <QUuid>
#include <QStringList>
#include <QMap>

namespace LudoShelf::Domain {

enum class LaunchType {
    Native = 0,
    Flatpak,
    AppImage,
    Wine,
    Custom
};

enum class HidePolicy {
    KeepVisible = 0,
    Minimize,
    Hide
};

struct ArgumentTemplate {
    QUuid id{QUuid::createUuid()};
    int position{0};
    QString templateString;
    bool optional{false};
};

struct EmulatorProfile {
    QUuid id{QUuid::createUuid()};
    QString name;
    LaunchType launchType{LaunchType::Native};
    QString program;
    QString workingDirectory;
    QMap<QString, QString> environment;
    QList<ArgumentTemplate> arguments;
    bool detach{false};
    bool captureOutput{true};
    HidePolicy hidePolicy{HidePolicy::KeepVisible};
    bool shellMode{false};
    bool enabled{true};
};

} // namespace LudoShelf::Domain

#endif // LUDOSHELF_DOMAIN_EMULATORPROFILE_H
