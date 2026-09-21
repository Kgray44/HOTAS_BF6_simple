#pragma once

#include <QObject>
#include <QHash>
#include <QSettings>
#include <QSet>
#include <QtGlobal>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace hotas {

// Presentation-only state. Keeping this separate from MapperConfiguration
// guarantees a visual preference never republishes the worker configuration.
class ThemeManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentTheme READ currentTheme WRITE setCurrentTheme NOTIFY themeChanged)
    Q_PROPERTY(bool topGun READ isTopGun NOTIFY themeChanged)
    Q_PROPERTY(bool dayOps READ isDayOps NOTIFY themeChanged)
    Q_PROPERTY(QStringList themeChoices READ themeChoices CONSTANT)
    // An experience owns shell composition; a theme continues to own only
    // visual tokens for the established presentations.
    Q_PROPERTY(QString currentExperience READ currentExperience WRITE setCurrentExperience NOTIFY experienceChanged)
    Q_PROPERTY(QString flightDeckAppearance READ flightDeckAppearance WRITE setFlightDeckAppearance NOTIFY flightDeckAppearanceChanged)
    // Text size is deliberately a presentation preference, not mapper
    // configuration.  It is shared by every shell and never republishes a
    // controller profile or runtime mapping.
    Q_PROPERTY(QString textSize READ textSize WRITE setTextSize NOTIFY textSizeChanged)
    Q_PROPERTY(qreal textScale READ textScale NOTIFY textSizeChanged)
    Q_PROPERTY(QStringList textSizeChoices READ textSizeChoices CONSTANT)
    Q_PROPERTY(QStringList experienceChoices READ experienceChoices CONSTANT)
    // Guidance is a small presentation preference.  It never publishes a
    // MapperConfiguration, changes a Profile/Rig, or dispatches a backend
    // command.  The policy revision lets QML retain a local explicit choice
    // while reacting to a level/default change without rebuilding a page.
    Q_PROPERTY(QString guidanceLevel READ guidanceLevel NOTIFY guidanceLevelChanged)
    Q_PROPERTY(QStringList guidanceChoices READ guidanceChoices CONSTANT)
    Q_PROPERTY(bool guidanceOnboardingPending READ guidanceOnboardingPending NOTIFY guidanceOnboardingChanged)
    Q_PROPERTY(int guidancePolicyRevision READ guidancePolicyRevision NOTIFY guidancePolicyChanged)
    // The existing theme family and the alternate Flight Deck shell are
    // combined here only for presentation selection, never mapper state.
    Q_PROPERTY(QString currentPresentationId READ currentPresentationId NOTIFY presentationChanged)
    Q_PROPERTY(QVariantList presentationChoices READ presentationChoices CONSTANT)

public:
    explicit ThemeManager(const QString &settingsFilePath = {}, QObject *parent = nullptr);

    QString currentTheme() const { return m_currentTheme; }
    bool isTopGun() const;
    bool isDayOps() const;
    QStringList themeChoices() const;
    QString currentExperience() const { return m_currentExperience; }
    QString flightDeckAppearance() const { return m_flightDeckAppearance; }
    QString textSize() const { return m_textSize; }
    qreal textScale() const;
    QStringList textSizeChoices() const;
    QStringList experienceChoices() const;
    QString guidanceLevel() const { return m_guidanceLevel; }
    QStringList guidanceChoices() const;
    bool guidanceOnboardingPending() const { return m_guidanceOnboardingPending; }
    int guidancePolicyRevision() const { return m_guidancePolicyRevision; }
    QString currentPresentationId() const;
    QVariantList presentationChoices() const;

    Q_INVOKABLE void setCurrentTheme(const QString &theme);
    Q_INVOKABLE void setCurrentExperience(const QString &experience);
    Q_INVOKABLE void setFlightDeckAppearance(const QString &appearance);
    Q_INVOKABLE void setTextSize(const QString &size);
    Q_INVOKABLE void selectPresentation(const QString &presentationId);
    // Use this explicit command from QML when a chooser needs to know whether
    // its small presentation preference was durably saved.  On a write
    // failure, the in-memory value is deliberately left unchanged.
    Q_INVOKABLE bool chooseGuidanceLevel(const QString &level);
    Q_INVOKABLE bool skipGuidanceOnboarding();
    Q_INVOKABLE bool guidanceSectionExpanded(const QString &sectionId) const;
    Q_INVOKABLE bool setGuidanceSectionExpanded(const QString &sectionId, bool expanded);
    Q_INVOKABLE bool guidanceSectionHasExplicitPreference(const QString &sectionId) const;
    Q_INVOKABLE bool followGuidanceLevelForSection(const QString &sectionId);
    // Exact-target navigation may reveal a local group without converting a
    // one-time route into a durable owner preference.  Callers clear this
    // scoped reveal when their route or page context is complete.
    Q_INVOKABLE bool temporarilyRevealGuidanceSection(const QString &sectionId);
    Q_INVOKABLE bool clearTemporaryGuidanceSectionReveal(const QString &sectionId);
    static QString normalizedTheme(const QString &theme);

signals:
    void themeChanged();
    void experienceChanged();
    void flightDeckAppearanceChanged();
    void textSizeChanged();
    void presentationChanged();
    void guidanceLevelChanged();
    void guidanceOnboardingChanged();
    void guidancePolicyChanged();

private:
    QString normalizedExperience(const QString &experience) const;
    static QString normalizedFlightDeckAppearance(const QString &appearance);
    static QString normalizedTextSize(const QString &size);
    static QString normalizedGuidanceLevel(const QString &level);
    bool hasEstablishedInstallation(const QSettings &stored) const;
    bool persistGuidanceLevel(const QString &level, bool onboardingHandled);
    bool defaultGuidanceSectionExpanded(const QString &sectionId) const;
    void advanceGuidancePolicyRevision();

    QString m_settingsFilePath;
    QString m_currentTheme;
    QString m_currentExperience;
    QString m_flightDeckAppearance;
    QString m_textSize;
    QString m_guidanceLevel;
    bool m_guidanceOnboardingPending = false;
    bool m_guidanceLevelPersisted = false;
    int m_guidancePolicyRevision = 0;
    QHash<QString, bool> m_explicitGuidanceSections;
    QSet<QString> m_temporaryGuidanceSections;
};

} // namespace hotas
