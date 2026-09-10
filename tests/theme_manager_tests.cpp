#include "theme_manager.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

using namespace Qt::StringLiterals;

class ThemeManagerTests final : public QObject {
    Q_OBJECT

private slots:
    void missingValueMigratesToLegacy();
    void selectionPersistsAndNormalizes();
    void themeStateDoesNotTouchMapperPayload();
    void flightDeckExperienceIsProductionSelectableAndDoesNotTouchMapperPayload();
    void presentationChoicesCentralizeAllFiveExperiences();
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
    QVERIFY(!manager.themeChoices().contains(u"Flight Deck"_qs));
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

void ThemeManagerTests::flightDeckExperienceIsProductionSelectableAndDoesNotTouchMapperPayload()
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

    hotas::ThemeManager manager(path);
    QCOMPARE(manager.experienceChoices(), QStringList({u"Existing"_qs, u"Flight Deck"_qs}));
    manager.setCurrentExperience(u" flight deck "_qs);
    manager.setFlightDeckAppearance(u" light "_qs);
    QCOMPARE(manager.currentExperience(), u"Flight Deck"_qs);
    QCOMPARE(manager.flightDeckAppearance(), u"Light"_qs);

    hotas::ThemeManager restored(path);
    QCOMPARE(restored.currentExperience(), u"Flight Deck"_qs);

    const QSettings settings(path, QSettings::IniFormat);
    QCOMPARE(settings.value(u"mapper/config"_qs).toByteArray(), QByteArrayLiteral("mapping-payload"));
    QCOMPARE(settings.value(u"profiles/active"_qs).toByteArray(), QByteArrayLiteral("profile-payload"));
    QCOMPARE(settings.value(u"presentation/uxExperience"_qs).toString(), u"Flight Deck"_qs);
    QCOMPARE(settings.value(u"presentation/flightDeckAppearance"_qs).toString(), u"Light"_qs);
}

void ThemeManagerTests::presentationChoicesCentralizeAllFiveExperiences()
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
    QCOMPARE(normal.currentPresentationId(), u"theme:Legacy"_qs);
    const QVariantList normalChoices = normal.presentationChoices();
    QCOMPARE(normalChoices.size(), 5);
    QCOMPARE(normalChoices.constLast().toMap().value(u"id"_qs).toString(), u"flight-deck"_qs);
    QVERIFY(!normalChoices.constLast().toMap().contains(u"preview"_qs));
    normal.selectPresentation(u"flight-deck"_qs);
    QCOMPARE(normal.currentExperience(), u"Flight Deck"_qs);
    normal.selectPresentation(u"theme:Top Gun"_qs);
    QCOMPARE(normal.currentTheme(), u"Top Gun"_qs);
    QCOMPARE(normal.currentPresentationId(), u"theme:Top Gun"_qs);

    normal.selectPresentation(u"flight-deck"_qs);
    QCOMPARE(normal.currentExperience(), u"Flight Deck"_qs);
    QCOMPARE(normal.currentPresentationId(), u"flight-deck"_qs);
    normal.selectPresentation(u"theme:Standard"_qs);
    QCOMPARE(normal.currentExperience(), u"Existing"_qs);
    QCOMPARE(normal.currentTheme(), u"Standard"_qs);

    const QSettings settings(path, QSettings::IniFormat);
    QCOMPARE(settings.value(u"mapper/config"_qs).toByteArray(), QByteArrayLiteral("mapping-payload"));
    QCOMPARE(settings.value(u"profiles/active"_qs).toByteArray(), QByteArrayLiteral("profile-payload"));
}

QTEST_APPLESS_MAIN(ThemeManagerTests)

#include "theme_manager_tests.moc"
