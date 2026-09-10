#include "theme_manager.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QVariantMap>

using namespace Qt::StringLiterals;

namespace hotas {
namespace {

constexpr auto kThemeKey = "presentation/uiTheme";
constexpr auto kExperienceKey = "presentation/uxExperience";
constexpr auto kFlightDeckAppearanceKey = "presentation/flightDeckAppearance";

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
    m_currentExperience = normalizedExperience(
        stored.value(QLatin1String(kExperienceKey), u"Existing"_qs).toString());
    m_flightDeckAppearance = normalizedFlightDeckAppearance(
        stored.value(QLatin1String(kFlightDeckAppearanceKey), u"Dark"_qs).toString());
}

bool ThemeManager::isTopGun() const
{
    return m_currentTheme == u"Top Gun"_qs;
}

bool ThemeManager::isDayOps() const
{
    return m_currentTheme == u"Day Ops"_qs;
}

QStringList ThemeManager::themeChoices() const
{
    return {u"Legacy"_qs, u"Standard"_qs, u"Top Gun"_qs, u"Day Ops"_qs};
}

QStringList ThemeManager::experienceChoices() const
{
    return {u"Existing"_qs, u"Flight Deck"_qs};
}

QString ThemeManager::currentPresentationId() const
{
    if (m_currentExperience == u"Flight Deck"_qs) return u"flight-deck"_qs;
    return u"theme:"_qs + m_currentTheme;
}

QVariantList ThemeManager::presentationChoices() const
{
    QVariantList choices;
    for (const QString &theme : themeChoices()) {
        QVariantMap choice{
            {u"id"_qs, u"theme:"_qs + theme},
            {u"label"_qs, theme},
            {u"kind"_qs, u"existing-theme"_qs},
            {u"preview"_qs, false},
        };
        if (theme == u"Legacy"_qs) {
            choice.insert(u"description"_qs, u"Original interface"_qs);
        } else if (theme == u"Top Gun"_qs) {
            choice.insert(u"description"_qs, u"Aircraft-inspired current theme"_qs);
        } else {
            choice.insert(u"description"_qs, u"Current standard experience"_qs);
        }
        choices.push_back(choice);
    }
    choices.push_back(QVariantMap{
        {u"id"_qs, u"flight-deck"_qs},
        {u"label"_qs, u"Flight Deck"_qs},
        {u"description"_qs, u"Modern simplified aircraft-controls interface"_qs},
        {u"kind"_qs, u"alternate-shell"_qs},
    });
    return choices;
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
    emit presentationChanged();
}

void ThemeManager::setCurrentExperience(const QString &experience)
{
    const QString normalized = normalizedExperience(experience);
    if (m_currentExperience == normalized) return;

    m_currentExperience = normalized;
    QSettings stored(m_settingsFilePath, QSettings::IniFormat);
    stored.setValue(QLatin1String(kExperienceKey), m_currentExperience);
    stored.sync();
    emit experienceChanged();
    emit presentationChanged();
}

void ThemeManager::setFlightDeckAppearance(const QString &appearance)
{
    const QString normalized = normalizedFlightDeckAppearance(appearance);
    if (m_flightDeckAppearance == normalized) return;

    m_flightDeckAppearance = normalized;
    QSettings stored(m_settingsFilePath, QSettings::IniFormat);
    stored.setValue(QLatin1String(kFlightDeckAppearanceKey), m_flightDeckAppearance);
    stored.sync();
    emit flightDeckAppearanceChanged();
}

void ThemeManager::selectPresentation(const QString &presentationId)
{
    const QString trimmed = presentationId.trimmed();
    const QString themePrefix = u"theme:"_qs;
    if (trimmed.startsWith(themePrefix, Qt::CaseInsensitive)) {
        setCurrentTheme(trimmed.mid(themePrefix.size()));
        setCurrentExperience(u"Existing"_qs);
        return;
    }
    if (trimmed.compare(u"flight-deck"_qs, Qt::CaseInsensitive) == 0) {
        setCurrentExperience(u"Flight Deck"_qs);
    }
}

QString ThemeManager::normalizedTheme(const QString &theme)
{
    const QString normalized = theme.trimmed();
    if (normalized.compare(u"Legacy"_qs, Qt::CaseInsensitive) == 0) return u"Legacy"_qs;
    if (normalized.compare(u"Top Gun"_qs, Qt::CaseInsensitive) == 0) return u"Top Gun"_qs;
    if (normalized.compare(u"Day Ops"_qs, Qt::CaseInsensitive) == 0) return u"Day Ops"_qs;
    // v1.7.0 development builds stored the name Classic. Preserve that
    // explicit in-progress selection as the revised Standard presentation.
    return u"Standard"_qs;
}

QString ThemeManager::normalizedExperience(const QString &experience) const
{
    if (experience.trimmed().compare(u"Flight Deck"_qs, Qt::CaseInsensitive) == 0) {
        return u"Flight Deck"_qs;
    }
    return u"Existing"_qs;
}

QString ThemeManager::normalizedFlightDeckAppearance(const QString &appearance)
{
    if (appearance.trimmed().compare(u"Light"_qs, Qt::CaseInsensitive) == 0) return u"Light"_qs;
    return u"Dark"_qs;
}

} // namespace hotas
