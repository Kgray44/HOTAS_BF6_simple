#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

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
    Q_PROPERTY(QStringList experienceChoices READ experienceChoices CONSTANT)
    Q_PROPERTY(bool flightDeckPreviewEnabled READ flightDeckPreviewEnabled CONSTANT)

public:
    explicit ThemeManager(const QString &settingsFilePath = {},
                          bool flightDeckPreviewEnabled = false,
                          QObject *parent = nullptr);

    QString currentTheme() const { return m_currentTheme; }
    bool isTopGun() const;
    bool isDayOps() const;
    QStringList themeChoices() const;
    QString currentExperience() const { return m_currentExperience; }
    QString flightDeckAppearance() const { return m_flightDeckAppearance; }
    QStringList experienceChoices() const;
    bool flightDeckPreviewEnabled() const { return m_flightDeckPreviewEnabled; }

    Q_INVOKABLE void setCurrentTheme(const QString &theme);
    Q_INVOKABLE void setCurrentExperience(const QString &experience);
    Q_INVOKABLE void setFlightDeckAppearance(const QString &appearance);
    static QString normalizedTheme(const QString &theme);

signals:
    void themeChanged();
    void experienceChanged();
    void flightDeckAppearanceChanged();

private:
    QString normalizedExperience(const QString &experience) const;
    static QString normalizedFlightDeckAppearance(const QString &appearance);

    QString m_settingsFilePath;
    QString m_currentTheme;
    QString m_currentExperience;
    QString m_flightDeckAppearance;
    bool m_flightDeckPreviewEnabled = false;
};

} // namespace hotas
