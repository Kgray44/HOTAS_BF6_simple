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
    Q_PROPERTY(QStringList themeChoices READ themeChoices CONSTANT)

public:
    explicit ThemeManager(const QString &settingsFilePath = {}, QObject *parent = nullptr);

    QString currentTheme() const { return m_currentTheme; }
    bool isTopGun() const;
    QStringList themeChoices() const;

    Q_INVOKABLE void setCurrentTheme(const QString &theme);
    static QString normalizedTheme(const QString &theme);

signals:
    void themeChanged();

private:
    QString m_settingsFilePath;
    QString m_currentTheme;
};

} // namespace hotas
