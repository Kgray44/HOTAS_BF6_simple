#include "theme_manager.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using namespace Qt::StringLiterals;

class ThemeManagerTests final : public QObject {
    Q_OBJECT

private slots:
    void missingValueMigratesToLegacy();
    void selectionPersistsAndNormalizes();
    void themeStateDoesNotTouchMapperPayload();
    void flightDeckExperienceIsPreviewGatedAndDoesNotTouchMapperPayload();
};

void ThemeManagerTests::missingValueMigratesToLegacy()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    hotas::ThemeManager manager(directory.filePath(u"settings.ini"_qs));
    QCOMPARE(manager.currentTheme(), u"Legacy"_qs);
    QVERIFY(!manager.isTopGun());
    QVERIFY(!manager.isDayOps());
    QCOMPARE(manager.themeChoices(), QStringList({u"Legacy"_qs, u"Standard"_qs, u"Top Gun"_qs,
                                                  u"Day Ops"_qs}));
}

void ThemeManagerTests::selectionPersistsAndNormalizes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(u"settings.ini"_qs);
    hotas::ThemeManager manager(path);
    manager.setCurrentTheme(u" top gun "_qs);
    QVERIFY(manager.isTopGun());
    QCOMPARE(manager.currentTheme(), u"Top Gun"_qs);

    hotas::ThemeManager restored(path);
    QCOMPARE(restored.currentTheme(), u"Top Gun"_qs);
    restored.setCurrentTheme(u"classic"_qs);
    QCOMPARE(restored.currentTheme(), u"Standard"_qs);
    restored.setCurrentTheme(u"unrecognized value"_qs);
    QCOMPARE(restored.currentTheme(), u"Standard"_qs);
    restored.setCurrentTheme(u" day ops "_qs);
    QCOMPARE(restored.currentTheme(), u"Day Ops"_qs);
    QVERIFY(restored.isDayOps());
}

void ThemeManagerTests::themeStateDoesNotTouchMapperPayload()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(u"settings.ini"_qs);
    {
        QSettings settings(path, QSettings::IniFormat);
        settings.setValue(u"mapper/config"_qs, QByteArrayLiteral("mapping-payload"));
        settings.sync();
    }

    hotas::ThemeManager manager(path);
    manager.setCurrentTheme(u"Day Ops"_qs);

    const QSettings settings(path, QSettings::IniFormat);
    QCOMPARE(settings.value(u"mapper/config"_qs).toByteArray(), QByteArrayLiteral("mapping-payload"));
    QCOMPARE(settings.value(u"presentation/uiTheme"_qs).toString(), u"Day Ops"_qs);
}

void ThemeManagerTests::flightDeckExperienceIsPreviewGatedAndDoesNotTouchMapperPayload()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(u"settings.ini"_qs);
    {
        QSettings settings(path, QSettings::IniFormat);
        settings.setValue(u"mapper/config"_qs, QByteArrayLiteral("mapping-payload"));
        settings.setValue(u"profiles/active"_qs, QByteArrayLiteral("profile-payload"));
        settings.sync();
    }

    hotas::ThemeManager normal(path);
    QCOMPARE(normal.experienceChoices(), QStringList({u"Existing"_qs}));
    normal.setCurrentExperience(u"Flight Deck"_qs);
    QCOMPARE(normal.currentExperience(), u"Existing"_qs);

    hotas::ThemeManager preview(path, true);
    QCOMPARE(preview.experienceChoices(), QStringList({u"Existing"_qs, u"Flight Deck"_qs}));
    preview.setCurrentExperience(u" flight deck "_qs);
    preview.setFlightDeckAppearance(u" light "_qs);
    QCOMPARE(preview.currentExperience(), u"Flight Deck"_qs);
    QCOMPARE(preview.flightDeckAppearance(), u"Light"_qs);

    const QSettings settings(path, QSettings::IniFormat);
    QCOMPARE(settings.value(u"mapper/config"_qs).toByteArray(), QByteArrayLiteral("mapping-payload"));
    QCOMPARE(settings.value(u"profiles/active"_qs).toByteArray(), QByteArrayLiteral("profile-payload"));
    QCOMPARE(settings.value(u"presentation/uxExperience"_qs).toString(), u"Flight Deck"_qs);
    QCOMPARE(settings.value(u"presentation/flightDeckAppearance"_qs).toString(), u"Light"_qs);
}

QTEST_APPLESS_MAIN(ThemeManagerTests)

#include "theme_manager_tests.moc"
