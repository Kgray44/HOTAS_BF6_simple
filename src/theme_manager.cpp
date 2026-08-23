#include "theme_manager.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace hotas {
namespace {

constexpr auto kThemeKey = "presentation/uiTheme";

QString defaultSettingsFilePath()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(directory);
    return directory + u"/settings.ini"_qs;
}

} // namespace

ThemeManager::ThemeManager(const QString &settingsFilePath, QObject *parent)
    : QObject(parent)
    , m_settingsFilePath(settingsFilePath.isEmpty() ? defaultSettingsFilePath() : settingsFilePath)
{
    const QSettings stored(m_settingsFilePath, QSettings::IniFormat);
    // A missing key is the explicit v1.7 migration path: existing installs
    // retain the concrete v1.6.3 surface until a theme is chosen explicitly.
    m_currentTheme = normalizedTheme(stored.value(QLatin1String(kThemeKey), u"Legacy"_qs).toString());
}

bool ThemeManager::isTopGun() const
{
    return m_currentTheme == u"Top Gun"_qs;
}

QStringList ThemeManager::themeChoices() const
{
    return {u"Legacy"_qs, u"Standard"_qs, u"Top Gun"_qs};
}

void ThemeManager::setCurrentTheme(const QString &theme)
{
    const QString normalized = normalizedTheme(theme);
    if (m_currentTheme == normalized) return;

    m_currentTheme = normalized;
    QSettings stored(m_settingsFilePath, QSettings::IniFormat);
    stored.setValue(QLatin1String(kThemeKey), m_currentTheme);
    stored.sync();
    emit themeChanged();
}

QString ThemeManager::normalizedTheme(const QString &theme)
{
    const QString normalized = theme.trimmed();
    if (normalized.compare(u"Legacy"_qs, Qt::CaseInsensitive) == 0) return u"Legacy"_qs;
    if (normalized.compare(u"Top Gun"_qs, Qt::CaseInsensitive) == 0) return u"Top Gun"_qs;
    // v1.7.0 development builds stored the name Classic. Preserve that
    // explicit in-progress selection as the revised Standard presentation.
    return u"Standard"_qs;
}

} // namespace hotas
