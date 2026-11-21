#include "updater.h"
#include <QByteArray>
#include <cstdio>
#include <cstdlib>

using kobalt::kindle::Updater;

namespace {
void require(bool condition, const char *message)
{
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}
} // namespace

// Checks what the update check reads: KPM's repository listing, the newest
// artifact of a manifest for one platform, and the version comparison.
int main()
{
    const auto urls = Updater::repositories(QStringLiteral(
            "\x1b[0mKindle Package Manager v0.2.4\n\x1b[0mRepositories:\n"
            "\x1b[0m  - one - First repo (https://repo.example.test/manifest.v2.json)\n"
            "\x1b[0m  - two - Home (builds) (http://192.0.2.7/owner/kpm/raw/branch/main/manifest.v2.json)\n"
            "\x1b[0m  - one - First repo (https://repo.example.test/manifest.v2.json)\n"));
    require(urls.size() == 2 && urls[0] == QUrl("https://repo.example.test/manifest.v2.json")
                    && urls[1].host() == "192.0.2.7",
            "The repository addresses were not read from the listing");
    require(Updater::repositories("No repositories.\n(not an address)\n").isEmpty(),
            "Text without an address gave a repository");

    const QByteArray manifest = R"({"packages": {"kobalt": {"description": "Build 5", "artifacts": [
        {"url": "a", "version": [1, 0, 209004], "supported_platforms": ["kindlehf"]},
        {"url": "b", "version": [1, 0, 209005], "supported_platforms": ["kindlehf"]},
        {"url": "c", "version": [1, 0, 209009], "supported_platforms": ["kindlepw2"]},
        {"url": "d", "version": [1, 0], "supported_platforms": ["kindlehf"]},
        {"url": "e", "version": [1, "x", 3], "supported_platforms": ["kindlehf"]}]},
        "other": {"artifacts": [{"version": [9, 9, 9], "supported_platforms": ["kindlehf"]}]}}})";
    const auto newest = Updater::newest(manifest, "kobalt", "kindlehf");
    require(newest.version == QVector<int>({ 1, 0, 209005 }) && newest.description == "Build 5",
            "The newest artifact for the platform was not chosen");
    require(Updater::newest(manifest, "missing", "kindlehf").version.isEmpty()
                    && Updater::newest("not json", "kobalt", "kindlehf").version.isEmpty()
                    && Updater::newest(manifest, "kobalt", "kindle-other").version.isEmpty(),
            "A missing package, platform or manifest gave a version");

    require(Updater::compare({ 1, 0, 209005 }, { 1, 0, 209004 }) > 0
                    && Updater::compare({ 1, 0, 209004 }, { 1, 0, 209004 }) == 0
                    && Updater::compare({ 1, 0, 900001 }, { 1, 1, 100001 }) < 0
                    && Updater::compare({ 2, 0, 0 }, { 1, 9, 999999 }) > 0,
            "Versions do not compare by major, minor and patch");
    require(Updater::installedVersion().size() == 3 && Updater::installedVersion()[2] > 0,
            "The build has no package version");
    return 0;
}
