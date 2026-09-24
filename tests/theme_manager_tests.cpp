#include "theme_manager.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

using namespace Qt::StringLiterals;

class ThemeManagerTests final : public QObject {
    Q_OBJECT

private slots:
    void missingPresentationDefaultsToFlightDeck();
    void selectionPersistsAndNormalizes();
    void themeStateDoesNotTouchMapperPayload();
    void flightDeckExperienceIsProductionSelectableAndDoesNotTouchMapperPayload();
    void presentationChoicesCentralizeAllFiveExperiences();
    void textSizeDefaultsPersistsAndDoesNotTouchMapperPayload();
    void guidanceFirstUsePersistsAndExistingInstallStaysNonIntrusive();
    void guidancePolicyPreservesExplicitSectionsAndMapperPayload();
    void guidanceSectionProvenanceUsesCuratedDefaultsAndTemporaryReveals();
    void guidancePersistenceFailureLeavesTheVisibleSelectionUntouched();
};

void ThemeManagerTests::missingPresentationDefaultsToFlightDeck()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    hotas::ThemeManager manager(directory.filePath(u"settings.ini"_qs));
    QCOMPARE(manager.currentTheme(), u"Legacy"_qs);
    QCOMPARE(manager.currentExperience(), u"Flight Deck"_qs);
    QCOMPARE(manager.currentPresentationId(), u"flight-deck"_qs);
    QVERIFY(!manager.isTopGun());
    QVERIFY(!manager.isDayOps());
    QCOMPARE(manager.guidanceLevel(), u"Guided"_qs);
    QVERIFY(manager.guidanceOnboardingPending());
    QCOMPARE(manager.themeChoices(), QStringList({u"Legacy"_qs, u"Standard"_qs, u"Top Gun"_qs,
                                                  u"Day Ops"_qs}));
    QVERIFY(!manager.themeChoices().contains(u"Flight Deck"_qs));
    {
        const QSettings firstUseSettings(directory.filePath(u"settings.ini"_qs), QSettings::IniFormat);
        QVERIFY(!firstUseSettings.contains(u"presentation/uxExperience"_qs));
        QVERIFY(!firstUseSettings.contains(u"presentation/guidanceLevel"_qs));
    }

    // Older builds persisted only the theme key.  It remains a user-selected
    // existing presentation, rather than being overridden by the new default.
    const QString existingPath = directory.filePath(u"existing-theme.ini"_qs);
    {
        QSettings settings(existingPath, QSettings::IniFormat);
        settings.setValue(u"presentation/uiTheme"_qs, u"Day Ops"_qs);
        settings.sync();
    }
    hotas::ThemeManager existing(existingPath);
    QCOMPARE(existing.currentTheme(), u"Day Ops"_qs);
    QCOMPARE(existing.currentExperience(), u"Existing"_qs);
    QCOMPARE(existing.currentPresentationId(), u"theme:Day Ops"_qs);
    QCOMPARE(existing.guidanceLevel(), u"Full"_qs);
    QVERIFY(!existing.guidanceOnboardingPending());
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
    QCOMPARE(normal.currentPresentationId(), u"flight-deck"_qs);
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

void ThemeManagerTests::textSizeDefaultsPersistsAndDoesNotTouchMapperPayload()
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
    QCOMPARE(manager.textSize(), u"Medium"_qs);
    QCOMPARE(manager.textScale(), 1.15);
    QCOMPARE(manager.textSizeChoices(), QStringList({u"Small"_qs, u"Medium"_qs,
                                                      u"Large"_qs, u"Extra Large"_qs}));
    manager.setTextSize(u"extra large"_qs);
    QCOMPARE(manager.textSize(), u"Extra Large"_qs);
    QCOMPARE(manager.textScale(), 1.5);

    hotas::ThemeManager restored(path);
    QCOMPARE(restored.textSize(), u"Extra Large"_qs);
    const QSettings settings(path, QSettings::IniFormat);
    QCOMPARE(settings.value(u"presentation/textSize"_qs).toString(), u"Extra Large"_qs);
    QCOMPARE(settings.value(u"mapper/config"_qs).toByteArray(), QByteArrayLiteral("mapping-payload"));
}

void ThemeManagerTests::guidanceFirstUsePersistsAndExistingInstallStaysNonIntrusive()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString freshPath = directory.filePath(u"fresh.ini"_qs);
    hotas::ThemeManager fresh(freshPath);
    QCOMPARE(fresh.guidanceLevel(), u"Guided"_qs);
    QVERIFY(fresh.guidanceOnboardingPending());
    QVERIFY(!fresh.chooseGuidanceLevel(u"unknown"_qs));
    QVERIFY(fresh.chooseGuidanceLevel(u" full "_qs));
    QCOMPARE(fresh.guidanceLevel(), u"Full"_qs);
    QVERIFY(!fresh.guidanceOnboardingPending());

    hotas::ThemeManager restored(freshPath);
    QCOMPARE(restored.guidanceLevel(), u"Full"_qs);
    QVERIFY(!restored.guidanceOnboardingPending());
    const int persistedRevision = restored.guidancePolicyRevision();
    QVERIFY(restored.chooseGuidanceLevel(u"Full"_qs));
    QCOMPARE(restored.guidancePolicyRevision(), persistedRevision);
    {
        const QSettings saved(freshPath, QSettings::IniFormat);
        QCOMPARE(saved.value(u"presentation/guidanceLevel"_qs).toString(), u"Full"_qs);
        QCOMPARE(saved.value(u"presentation/guidanceOnboardingVersion"_qs).toInt(), 1);
    }

    const QString skippedPath = directory.filePath(u"skipped.ini"_qs);
    hotas::ThemeManager skipped(skippedPath);
    QVERIFY(skipped.skipGuidanceOnboarding());
    QCOMPARE(skipped.guidanceLevel(), u"Guided"_qs);
    QVERIFY(!skipped.guidanceOnboardingPending());

    const QString upgradePath = directory.filePath(u"upgrade.ini"_qs);
    {
        QSettings existing(upgradePath, QSettings::IniFormat);
        existing.setValue(u"mapper/config"_qs, QByteArrayLiteral("existing-mapper"));
        existing.setValue(u"profiles/active"_qs, QByteArrayLiteral("existing-profile"));
        existing.sync();
    }
    hotas::ThemeManager upgrade(upgradePath);
    QCOMPARE(upgrade.guidanceLevel(), u"Full"_qs);
    QVERIFY(!upgrade.guidanceOnboardingPending());
    const QSettings preserved(upgradePath, QSettings::IniFormat);
    QCOMPARE(preserved.value(u"mapper/config"_qs).toByteArray(), QByteArrayLiteral("existing-mapper"));
    QCOMPARE(preserved.value(u"profiles/active"_qs).toByteArray(), QByteArrayLiteral("existing-profile"));
    QVERIFY(!preserved.contains(u"presentation/guidanceLevel"_qs));
}

void ThemeManagerTests::guidancePolicyPreservesExplicitSectionsAndMapperPayload()
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
    QCOMPARE(manager.guidanceLevel(), u"Full"_qs);
    QVERIFY(manager.guidanceSectionExpanded(u"axes-processing"_qs));
    QVERIFY(!manager.guidanceSectionExpanded(u"unknown-technical-section"_qs));
    const int firstRevision = manager.guidancePolicyRevision();
    QVERIFY(manager.setGuidanceSectionExpanded(u"axes-processing"_qs, false));
    QVERIFY(manager.guidanceSectionHasExplicitPreference(u"axes-processing"_qs));
    QVERIFY(!manager.guidanceSectionExpanded(u"axes-processing"_qs));
    QVERIFY(manager.guidancePolicyRevision() > firstRevision);
    QVERIFY(manager.chooseGuidanceLevel(u"Guided"_qs));
    QVERIFY(!manager.guidanceSectionExpanded(u"axes-processing"_qs));
    QVERIFY(manager.chooseGuidanceLevel(u"Full"_qs));
    QVERIFY(!manager.guidanceSectionExpanded(u"axes-processing"_qs));

    hotas::ThemeManager restored(path);
    QCOMPARE(restored.guidanceLevel(), u"Full"_qs);
    QVERIFY(!restored.guidanceSectionExpanded(u"axes-processing"_qs));
    const QSettings saved(path, QSettings::IniFormat);
    QCOMPARE(saved.value(u"mapper/config"_qs).toByteArray(), QByteArrayLiteral("mapping-payload"));
    QCOMPARE(saved.value(u"presentation/guidanceSections/axes-processing"_qs).toBool(), false);
}

void ThemeManagerTests::guidanceSectionProvenanceUsesCuratedDefaultsAndTemporaryReveals()
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
    QCOMPARE(manager.guidanceLevel(), u"Full"_qs);
    QVERIFY(manager.guidanceSectionExpanded(u"devices-virtual-details"_qs));
    QVERIFY(!manager.guidanceSectionHasExplicitPreference(u"devices-virtual-details"_qs));
    QVERIFY(manager.temporarilyRevealGuidanceSection(u"devices-virtual-details"_qs));
    QVERIFY(manager.guidanceSectionExpanded(u"devices-virtual-details"_qs));
    QVERIFY(!manager.guidanceSectionHasExplicitPreference(u"devices-virtual-details"_qs));
    QVERIFY(manager.clearTemporaryGuidanceSectionReveal(u"devices-virtual-details"_qs));
    QVERIFY(manager.setGuidanceSectionExpanded(u"devices-virtual-details"_qs, false));
    QVERIFY(!manager.guidanceSectionExpanded(u"devices-virtual-details"_qs));
    QVERIFY(manager.temporarilyRevealGuidanceSection(u"devices-virtual-details"_qs));
    QVERIFY(manager.guidanceSectionExpanded(u"devices-virtual-details"_qs));
    QVERIFY(manager.clearTemporaryGuidanceSectionReveal(u"devices-virtual-details"_qs));
    QVERIFY(!manager.guidanceSectionExpanded(u"devices-virtual-details"_qs));
    QVERIFY(manager.followGuidanceLevelForSection(u"devices-virtual-details"_qs));
    QVERIFY(!manager.guidanceSectionHasExplicitPreference(u"devices-virtual-details"_qs));
    QVERIFY(manager.guidanceSectionExpanded(u"devices-virtual-details"_qs));

    const QSettings saved(path, QSettings::IniFormat);
    QVERIFY(!saved.contains(u"presentation/guidanceSections/devices-virtual-details"_qs));
    QCOMPARE(saved.value(u"mapper/config"_qs).toByteArray(), QByteArrayLiteral("mapping-payload"));
}

void ThemeManagerTests::guidancePersistenceFailureLeavesTheVisibleSelectionUntouched()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    // An existing directory cannot be synchronized as an INI file. This
    // yields an isolated write failure without touching the user store.
    const QString inaccessiblePath = directory.path();
    hotas::ThemeManager manager(inaccessiblePath);
    QCOMPARE(manager.guidanceLevel(), u"Guided"_qs);
    QVERIFY(manager.guidanceOnboardingPending());
    QVERIFY(!manager.chooseGuidanceLevel(u"Full"_qs));
    QCOMPARE(manager.guidanceLevel(), u"Guided"_qs);
    QVERIFY(manager.guidanceOnboardingPending());
}

QTEST_APPLESS_MAIN(ThemeManagerTests)

#include "theme_manager_tests.moc"
