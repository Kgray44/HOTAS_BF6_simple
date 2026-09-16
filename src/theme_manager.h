#pragma once

#include <QObject>
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
    QString currentPresentationId() const;
    QVariantList presentationChoices() const;

    Q_INVOKABLE void setCurrentTheme(const QString &theme);
    Q_INVOKABLE void setCurrentExperience(const QString &experience);
    Q_INVOKABLE void setFlightDeckAppearance(const QString &appearance);
    Q_INVOKABLE void setTextSize(const QString &size);
    Q_INVOKABLE void selectPresentation(const QString &presentationId);
    static QString normalizedTheme(const QString &theme);

signals:
    void themeChanged();
    void experienceChanged();
    void flightDeckAppearanceChanged();
    void textSizeChanged();
    void presentationChanged();

private:
    QString normalizedExperience(const QString &experience) const;
    static QString normalizedFlightDeckAppearance(const QString &appearance);
    static QString normalizedTextSize(const QString &size);

    QString m_settingsFilePath;
    QString m_currentTheme;
    QString m_currentExperience;
    QString m_flightDeckAppearance;
    QString m_textSize;
};

} // namespace hotas
