#pragma once
#include <QDir>
#include <QStringList>

namespace kobalt::kindle {
// The reading fonts of the Kindle's own reader. The firmware keeps them in one
// folder with its interface, handwriting and script fonts, which are left out.
inline QString systemFontDirectory()
{
    return qEnvironmentVariable("KOBALT_KINDLE_SYSTEM_FONTS",
                                QStringLiteral("/usr/java/lib/fonts"));
}

// Family names as the font files report them.
inline QStringList systemFontFamilies()
{
    return { QStringLiteral("Amazon Ember"), QStringLiteral("Atkinson Hyperlegible Next"),
             QStringLiteral("Baskerville"),  QStringLiteral("Bookerly"),
             QStringLiteral("Caecilia Regular"), QStringLiteral("Futura"),
             QStringLiteral("Helvetica Neue LT"), QStringLiteral("OpenDyslexic"),
             QStringLiteral("Palatino") };
}

// The files of those families that this firmware has. Each family lists its
// regular face first.
inline QStringList systemFontFiles()
{
    static const char *const names[] = {
        "Amazon-Ember-Regular.ttf",
        "Amazon-Ember-RegularItalic.ttf",
        "Amazon-Ember-Bold.ttf",
        "Amazon-Ember-BoldItalic.ttf",
        "AtkinsonHyperlegibleNext.ttf",
        "AtkinsonHyperlegibleNext-Italic.ttf",
        "AtkinsonHyperlegibleNext-Bold.ttf",
        "AtkinsonHyperlegibleNext-BoldItalic.ttf",
        "Baskerville-Regular.ttf",
        "Baskerville-Italic.ttf",
        "Baskerville-Bold.ttf",
        "Baskerville-BoldItalic.ttf",
        "Bookerly-Regular.ttf",
        "Bookerly-Italic.ttf",
        "Bookerly-Bold.ttf",
        "Bookerly-BoldItalic.ttf",
        "Caecilia_LT_65_Medium.ttf",
        "Caecilia_LT_66_Medium_Italic.ttf",
        "Caecilia_LT_75_Bold.ttf",
        "Caecilia_LT_76_Bold_Italic.ttf",
        "Futura-Medium.ttf",
        "Futura-MediumOblique.ttf",
        "Futura-Bold.ttf",
        "Futura-BoldOblique.ttf",
        "Helvetica_LT_65_Medium.ttf",
        "Helvetica_LT_66_Medium_Italic.ttf",
        "Helvetica_LT_75_Bold.ttf",
        "Helvetica_LT_76_Bold_Italic.ttf",
        "OpenDyslexic-Regular.otf",
        "OpenDyslexic-Italic.otf",
        "OpenDyslexic-Bold.otf",
        "OpenDyslexic-BoldItalic.otf",
        "Palatino-Regular.ttf",
        "Palatino-Italic.ttf",
        "Palatino-Bold.ttf",
        "Palatino-BoldItalic.ttf",
    };
    const QDir directory(systemFontDirectory());
    QStringList result;
    for (const auto *name : names)
        if (directory.exists(QLatin1String(name)))
            result.append(directory.filePath(QLatin1String(name)));
    return result;
}
} // namespace kobalt::kindle
