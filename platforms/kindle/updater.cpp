#include "updater.h"
#include "support/strings.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

namespace kobalt::kindle {
namespace {
const auto kpm = QStringLiteral("/var/local/kmc/bin/kpm");
const auto package = QStringLiteral("kobalt");
// The platform of every Kindle package manifest; see kpm/manifest.json.in.
const auto platform = QStringLiteral("kindlehf");
constexpr int transferTimeoutMilliseconds = 30000;
constexpr int listingTimeoutMilliseconds = 20000;
} // namespace

Updater::Updater(QString appDirectory, QString runDirectory, QNetworkAccessManager *network,
                 QObject *parent)
    : QObject(parent),
      appDirectory(std::move(appDirectory)),
      runDirectory(std::move(runDirectory)),
      network(network)
{
}

bool Updater::available() const
{
    return !appDirectory.isEmpty() && !runDirectory.isEmpty() && QFileInfo(kpm).isExecutable();
}

QVector<int> Updater::installedVersion()
{
    return { KOBALT_KINDLE_VERSION_MAJOR, KOBALT_KINDLE_VERSION_MINOR,
             KOBALT_KINDLE_VERSION_PATCH };
}

int Updater::compare(const QVector<int> &left, const QVector<int> &right)
{
    for (int index = 0; index < 3; ++index) {
        const int difference = left.value(index) - right.value(index);
        if (difference)
            return difference;
    }
    return 0;
}

QVector<QUrl> Updater::repositories(const QString &listing)
{
    // Each repository line ends with its manifest address in parentheses.
    static const QRegularExpression address(QStringLiteral("\\((https?://[^()\\s]+)\\)\\s*$"),
                                            QRegularExpression::MultilineOption);
    QVector<QUrl> result;
    auto matches = address.globalMatch(listing);
    while (matches.hasNext()) {
        const QUrl url(matches.next().captured(1));
        if (url.isValid() && !result.contains(url))
            result.append(url);
    }
    return result;
}

Updater::Candidate Updater::newest(const QByteArray &manifest, const QString &name,
                                   const QString &target)
{
    Candidate result;
    const auto entry = QJsonDocument::fromJson(manifest)
                               .object()
                               .value(QStringLiteral("packages"))
                               .toObject()
                               .value(name)
                               .toObject();
    const auto artifacts = entry.value(QStringLiteral("artifacts")).toArray();
    for (const auto &value : artifacts) {
        const auto artifact = value.toObject();
        const auto numbers = artifact.value(QStringLiteral("version")).toArray();
        bool supported = false;
        for (const auto &supportedPlatform :
             artifact.value(QStringLiteral("supported_platforms")).toArray())
            supported = supported || supportedPlatform.toString() == target;
        if (!supported || numbers.size() != 3)
            continue;
        const QVector<int> version{ numbers[0].toInt(-1), numbers[1].toInt(-1),
                                    numbers[2].toInt(-1) };
        if (version.contains(-1))
            continue;
        if (result.version.isEmpty() || compare(version, result.version) > 0)
            result.version = version;
    }
    if (!result.version.isEmpty())
        result.description = entry.value(QStringLiteral("description")).toString();
    return result;
}

void Updater::check()
{
    const auto id = ++generation;
    best = { };
    pending = 0;
    reached = false;
    auto *listing = new QProcess(this);
    listing->setProcessChannelMode(QProcess::MergedChannels);
    connect(listing, &QProcess::errorOccurred, this, [this, listing, id](QProcess::ProcessError) {
        listing->deleteLater();
        if (id == generation && std::exchange(pending, -1) >= 0)
            emit failed(strings::get("update.no_kpm"));
    });
    connect(listing, &QProcess::finished, this, [this, listing, id](int) {
        listing->deleteLater();
        if (id != generation || pending < 0)
            return;
        const auto urls = repositories(QString::fromUtf8(listing->readAll()));
        if (urls.isEmpty()) {
            emit failed(strings::get("update.no_repository"));
            return;
        }
        pending = urls.size();
        for (const auto &url : urls) {
            QNetworkRequest request(url);
            request.setTransferTimeout(transferTimeoutMilliseconds);
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::NoLessSafeRedirectPolicy);
            auto *reply = network->get(request);
            reply->setParent(this);
            connect(reply, &QNetworkReply::finished, this, [this, reply, id] {
                reply->deleteLater();
                if (id != generation)
                    return;
                if (reply->error() == QNetworkReply::NoError) {
                    reached = true;
                    const auto candidate = newest(reply->readAll(), package, platform);
                    if (!candidate.version.isEmpty()
                        && (best.version.isEmpty() || compare(candidate.version, best.version) > 0))
                        best = candidate;
                }
                if (--pending == 0)
                    finish();
            });
        }
    });
    listing->start(kpm, { QStringLiteral("list-repo") });
    // A stuck KPM must not leave the check open for ever.
    QTimer::singleShot(listingTimeoutMilliseconds, listing, [listing] { listing->kill(); });
}

void Updater::finish()
{
    if (!reached)
        emit failed(strings::get("update.unreachable"));
    else if (best.version.isEmpty() || compare(best.version, installedVersion()) <= 0)
        emit upToDate();
    else
        emit found(best.description);
}

bool Updater::start()
{
    if (!available())
        return false;
    requested_ = true;
    return true;
}
} // namespace kobalt::kindle
