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
constexpr auto kTextSizeKey = "presentation/textSize";
constexpr auto kGuidanceLevelKey = "presentation/guidanceLevel";
constexpr auto kGuidanceOnboardingKey = "presentation/guidanceOnboardingVersion";
constexpr auto kGuidanceSectionsPrefix = "presentation/guidanceSections/";
constexpr auto kGuidanceOnboardingVersion = 1;

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
    QSettings stored(m_settingsFilePath, QSettings::IniFormat);
    // Flight Deck is the first-run presentation. Do not overwrite an
    // explicit existing-theme choice made by an earlier build that predates
    // the separate experience key: either persisted presentation key is
    // sufficient evidence of user intent.
    const bool hasExplicitPresentation = stored.contains(QLatin1String(kExperienceKey))
        || stored.contains(QLatin1String(kThemeKey));
    m_currentTheme = normalizedTheme(stored.value(QLatin1String(kThemeKey), u"Legacy"_qs).toString());
    m_currentExperience = normalizedExperience(
        stored.value(QLatin1String(kExperienceKey),
                     hasExplicitPresentation ? u"Existing"_qs : u"Flight Deck"_qs).toString());
    m_flightDeckAppearance = normalizedFlightDeckAppearance(
        stored.value(QLatin1String(kFlightDeckAppearanceKey), u"Dark"_qs).toString());
    // Previous versions used the current (smallest) measurements directly.
    // New installations are comfortably readable by default while existing
    // users can explicitly choose their preferred scale without touching a
    // mapping, profile, or device-rig setting.
    m_textSize = normalizedTextSize(
        stored.value(QLatin1String(kTextSizeKey), u"Medium"_qs).toString());
    // Do not write a default before guidance has classified the installation.
    // In particular, a backend's normal first-run defaults must not turn an
    // empty install into a fake returning user. Main constructs this object
    // before AppBackend for exactly that reason.
    const bool established = hasEstablishedInstallation(stored);
    const QString persistedGuidance = stored.value(QLatin1String(kGuidanceLevelKey)).toString();
    if (persistedGuidance.compare(u"Guided"_qs, Qt::CaseInsensitive) == 0
        || persistedGuidance.compare(u"Full"_qs, Qt::CaseInsensitive) == 0) {
        m_guidanceLevel = normalizedGuidanceLevel(persistedGuidance);
        m_guidanceLevelPersisted = true;
        m_guidanceOnboardingPending = false;
    } else if (established) {
        // An upgrade remains non-intrusive. It uses the compatible Full
        // default in memory and writes nothing until the owner changes it.
        m_guidanceLevel = u"Full"_qs;
        m_guidanceOnboardingPending = false;
    } else {
        // A genuine first use offers the two choices before any normal
        // startup default is written. Skip is a durable Guided choice.
        m_guidanceLevel = u"Guided"_qs;
        m_guidanceOnboardingPending = true;
    }
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

QStringList ThemeManager::guidanceChoices() const
{
    return {u"Guided"_qs, u"Full"_qs};
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
    QSettings stored(m_settingsFilePath, QSettings::IniFormat);
    // A returning installation can inherit the compatible default without a
    // presentation key. Choosing the already-visible experience is still an
    // explicit preference and should be retained for the next launch.
    if (m_currentExperience == normalized) {
        if (stored.value(QLatin1String(kExperienceKey)).toString() != normalized) {
            stored.setValue(QLatin1String(kExperienceKey), normalized);
            stored.sync();
        }
        return;
    }

    m_currentExperience = normalized;
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

qreal ThemeManager::textScale() const
{
    if (m_textSize == u"Small"_qs) return 1.0;
    if (m_textSize == u"Large"_qs) return 1.3;
    if (m_textSize == u"Extra Large"_qs) return 1.5;
    return 1.15;
}

QStringList ThemeManager::textSizeChoices() const
{
    return {u"Small"_qs, u"Medium"_qs, u"Large"_qs, u"Extra Large"_qs};
}

void ThemeManager::setTextSize(const QString &size)
{
    const QString normalized = normalizedTextSize(size);
    if (m_textSize == normalized) return;

    m_textSize = normalized;
    QSettings stored(m_settingsFilePath, QSettings::IniFormat);
    stored.setValue(QLatin1String(kTextSizeKey), m_textSize);
    stored.sync();
    emit textSizeChanged();
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

bool ThemeManager::chooseGuidanceLevel(const QString &level)
{
    const QString normalized = normalizedGuidanceLevel(level);
    // Unknown values must never become an accidental advanced mode.
    if (level.trimmed().compare(u"Guided"_qs, Qt::CaseInsensitive) != 0
        && level.trimmed().compare(u"Full"_qs, Qt::CaseInsensitive) != 0) {
        return false;
    }
    if (m_guidanceLevel == normalized && m_guidanceLevelPersisted
        && !m_guidanceOnboardingPending) {
        return true;
    }
    if (!persistGuidanceLevel(normalized, true)) return false;
    const bool levelChanged = m_guidanceLevel != normalized;
    const bool onboardingChanged = m_guidanceOnboardingPending;
    m_guidanceLevel = normalized;
    m_guidanceLevelPersisted = true;
    m_guidanceOnboardingPending = false;
    if (levelChanged) emit guidanceLevelChanged();
    if (onboardingChanged) emit guidanceOnboardingChanged();
    advanceGuidancePolicyRevision();
    return true;
}

bool ThemeManager::skipGuidanceOnboarding()
{
    return chooseGuidanceLevel(u"Guided"_qs);
}

bool ThemeManager::guidanceSectionExpanded(const QString &sectionId) const
{
    const QString normalizedSection = sectionId.trimmed();
    if (normalizedSection.isEmpty()) return false;
    QSettings stored(m_settingsFilePath, QSettings::IniFormat);
    const QString key = QLatin1String(kGuidanceSectionsPrefix) + normalizedSection;
    if (stored.contains(key)) return stored.value(key).toBool();
    return defaultGuidanceSectionExpanded(normalizedSection);
}

bool ThemeManager::setGuidanceSectionExpanded(const QString &sectionId, bool expanded)
{
    const QString normalizedSection = sectionId.trimmed();
    if (normalizedSection.isEmpty()) return false;
    QSettings stored(m_settingsFilePath, QSettings::IniFormat);
    const QString key = QLatin1String(kGuidanceSectionsPrefix) + normalizedSection;
    if (stored.contains(key) && stored.value(key).toBool() == expanded) return true;
    stored.setValue(key, expanded);
    stored.sync();
    if (stored.status() != QSettings::NoError) return false;
    advanceGuidancePolicyRevision();
    return true;
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

QString ThemeManager::normalizedTextSize(const QString &size)
{
    const QString normalized = size.trimmed();
    if (normalized.compare(u"Small"_qs, Qt::CaseInsensitive) == 0) return u"Small"_qs;
    if (normalized.compare(u"Large"_qs, Qt::CaseInsensitive) == 0) return u"Large"_qs;
    if (normalized.compare(u"Extra Large"_qs, Qt::CaseInsensitive) == 0
        || normalized.compare(u"ExtraLarge"_qs, Qt::CaseInsensitive) == 0) {
        return u"Extra Large"_qs;
    }
    return u"Medium"_qs;
}

QString ThemeManager::normalizedGuidanceLevel(const QString &level)
{
    if (level.trimmed().compare(u"Full"_qs, Qt::CaseInsensitive) == 0) return u"Full"_qs;
    return u"Guided"_qs;
}

bool ThemeManager::hasEstablishedInstallation(const QSettings &stored) const
{
    // This checks only durable UI/configuration markers, not hardware count
    // or a current device report. A controller being disconnected is not a
    // first-use signal. The keys are intentionally read-only here.
    return stored.contains(QLatin1String(kThemeKey))
        || stored.contains(QLatin1String(kExperienceKey))
        || stored.contains(QLatin1String(kFlightDeckAppearanceKey))
        || stored.contains(QLatin1String(kTextSizeKey))
        || stored.contains(u"mapper/config"_qs)
        || stored.contains(u"profiles/active"_qs)
        || stored.contains(u"setupAssistant/task-v1"_qs);
}

bool ThemeManager::persistGuidanceLevel(const QString &level, bool onboardingHandled)
{
    QSettings stored(m_settingsFilePath, QSettings::IniFormat);
    stored.setValue(QLatin1String(kGuidanceLevelKey), level);
    if (onboardingHandled)
        stored.setValue(QLatin1String(kGuidanceOnboardingKey), kGuidanceOnboardingVersion);
    stored.sync();
    return stored.status() == QSettings::NoError;
}

bool ThemeManager::defaultGuidanceSectionExpanded(const QString &sectionId) const
{
    Q_UNUSED(sectionId);
    // Full surfaces advanced context sooner. Guided retains every capability
    // behind an explicit, keyboard-accessible disclosure rather than hiding
    // or changing it. Explicit per-section settings always win above.
    return m_guidanceLevel == u"Full"_qs;
}

void ThemeManager::advanceGuidancePolicyRevision()
{
    ++m_guidancePolicyRevision;
    emit guidancePolicyChanged();
}

} // namespace hotas
