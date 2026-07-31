#include "GameDetailsPanel.h"
#include "../database/DatabaseManager.h"
#include "../media/MediaStorageManager.h"
#include "../media/PlaceholderGenerator.h"

#include <QFileInfo>
#include <QDate>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>

namespace LudoShelf::UI {

GameDetailsPanel::GameDetailsPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);

    m_artworkLabel = new QLabel(this);
    m_artworkLabel->setFixedSize(220, 310);
    m_artworkLabel->setStyleSheet("border: 1px solid #444; background-color: #222; border-radius: 6px;");
    m_artworkLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_artworkLabel, 0, Qt::AlignCenter);

    m_titleLabel = new QLabel("Select a game", this);
    m_titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; margin-top: 4px;");
    m_titleLabel->setWordWrap(true);
    layout->addWidget(m_titleLabel);

    // 1. Year · Platform / 2. Genre · Player range
    m_headerMetaLabel = new QLabel(this);
    m_headerMetaLabel->setWordWrap(true);
    m_headerMetaLabel->setStyleSheet("font-size: 13px; color: #b9cde8; margin-top: 4px;");
    layout->addWidget(m_headerMetaLabel);

    // 3. Developer / Publisher
    m_creditsLabel = new QLabel(this);
    m_creditsLabel->setWordWrap(true);
    m_creditsLabel->setStyleSheet("color: #cccccc; margin-top: 4px;");
    layout->addWidget(m_creditsLabel);

    // 4. Description
    m_descriptionLabel = new QLabel(this);
    m_descriptionLabel->setWordWrap(true);
    m_descriptionLabel->setStyleSheet("color: #d0d0d0; margin-top: 6px;");
    layout->addWidget(m_descriptionLabel);

    m_moreMetadataButton = new QPushButton("More", this);
    m_moreMetadataButton->setFlat(true);
    m_moreMetadataButton->setStyleSheet("text-align: left; color: #75a7e8; padding: 0;");
    m_moreMetadataButton->hide();
    layout->addWidget(m_moreMetadataButton, 0, Qt::AlignLeft);

    // 5. Region · Language · Revision
    m_regionLangRevLabel = new QLabel(this);
    m_regionLangRevLabel->setWordWrap(true);
    m_regionLangRevLabel->setStyleSheet("color: #a0a0a0; margin-top: 6px;");
    layout->addWidget(m_regionLangRevLabel);

    // 6. ROM identification state / Signature source · Match method
    m_identityStateLabel = new QLabel(this);
    m_identityStateLabel->setWordWrap(true);
    m_identityStateLabel->setStyleSheet("font-weight: bold; color: #d5ad5c; margin-top: 8px;");
    layout->addWidget(m_identityStateLabel);

    m_identitySourceMatchLabel = new QLabel(this);
    m_identitySourceMatchLabel->setWordWrap(true);
    m_identitySourceMatchLabel->setStyleSheet("color: #929292;");
    layout->addWidget(m_identitySourceMatchLabel);

    m_retryMetadataButton = new QToolButton(this);
    m_retryMetadataButton->setText("Retry");
    m_retryMetadataButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_retryMetadataButton->setStyleSheet("color: #75a7e8; padding: 0; text-align: left;");
    m_retryMetadataButton->hide();
    layout->addWidget(m_retryMetadataButton, 0, Qt::AlignLeft);

    m_identityDetailsButton = new QToolButton(this);
    m_identityDetailsButton->setText("Identification details");
    m_identityDetailsButton->setCheckable(true);
    m_identityDetailsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_identityDetailsButton->setArrowType(Qt::RightArrow);
    m_identityDetailsButton->hide();
    layout->addWidget(m_identityDetailsButton);

    m_identityDetailsLabel = new QLabel(this);
    m_identityDetailsLabel->setWordWrap(true);
    m_identityDetailsLabel->setOpenExternalLinks(true);
    m_identityDetailsLabel->setStyleSheet("color: #929292; margin-left: 10px;");
    m_identityDetailsLabel->hide();
    layout->addWidget(m_identityDetailsLabel);

    // 7. Play count · Total duration · Last played
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("color: #a9a9a9; margin-top: 10px;");
    layout->addWidget(m_statusLabel);

    layout->addStretch();

    m_launchBtn = new QPushButton("▶ Launch Game", this);
    m_launchBtn->setStyleSheet("padding: 8px; font-weight: bold; background-color: #2b78e4; color: white; border-radius: 4px;");
    m_launchBtn->setEnabled(false);
    layout->addWidget(m_launchBtn);

    connect(m_launchBtn, &QPushButton::clicked, this, [this]() {
        if (!m_currentGame.id.isNull()) {
            emit launchRequested(m_currentGame.id);
        }
    });
    connect(m_moreMetadataButton, &QPushButton::clicked, this, [this] {
        m_descriptionExpanded = !m_descriptionExpanded;
        renderDescription();
    });
    connect(m_identityDetailsButton, &QToolButton::toggled, this, [this](bool expanded) {
        m_identityDetailsButton->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        m_identityDetailsLabel->setVisible(expanded && !m_identityDetailsLabel->text().isEmpty());
    });
    connect(m_retryMetadataButton, &QToolButton::clicked, this, [this] {
        if (!m_currentGame.id.isNull()) emit romMetadataRetryRequested(m_currentGame.id);
    });
}

void GameDetailsPanel::setGame(const Domain::Game& game) {
    m_currentGame = game;
    m_titleLabel->setText(game.title);

    const auto preferredAsset = Database::DatabaseManager::instance().getPreferredCoverAsset(game.id);
    QString sha256 = preferredAsset.mediaObjectSha256;
    if (sha256.isEmpty()) {
        const auto media = Database::DatabaseManager::instance().getMediaForGame(game.id);
        for (const auto& item : media) {
            if (!item.path.isEmpty()) {
                sha256 = QFileInfo(item.path).completeBaseName();
                if (item.preferred) break;
            }
        }
    }

    if (sha256.isEmpty()) {
        auto sys = Database::DatabaseManager::instance().getSystem(game.systemId);
        sha256 = Media::PlaceholderGenerator::generateAndStorePlaceholder(game.id, game.title, sys.name);
    }

    if (!sha256.isEmpty()) {
        QImage img = Media::MediaStorageManager::instance().loadThumbnail(sha256, 220, 310);
        if (!img.isNull()) {
            m_artworkLabel->setPixmap(QPixmap::fromImage(img).scaled(220, 310, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            m_artworkLabel->clear();
            m_artworkLabel->setText("No Cover");
        }
    } else {
        m_artworkLabel->clear();
        m_artworkLabel->setText("No Cover");
    }

    m_launchBtn->setEnabled(true);
    m_hasRomMetadata = false;
    m_romMetadata = {};
    m_romMetadataStale = false;
    m_romState.clear();
    m_romStateMessage.clear();
    m_fullDescription.clear();
    m_descriptionExpanded = false;

    renderTemplate();
}

void GameDetailsPanel::setRomMetadata(const Metadata::RomMetadata& metadata, bool stale) {
    if (m_currentGame.id.isNull()) return;
    m_hasRomMetadata = true;
    m_romMetadata = metadata;
    m_romMetadataStale = stale;
    m_romState = "matched";
    m_romStateMessage.clear();
    m_descriptionExpanded = false;

    renderTemplate();
}

void GameDetailsPanel::setRomMetadataState(const QString& state, const QString& message) {
    if (m_currentGame.id.isNull()) return;
    m_romState = state;
    m_romStateMessage = message;

    renderTemplate();
}

void GameDetailsPanel::renderDescription() {
    if (m_fullDescription.isEmpty()) {
        m_descriptionLabel->clear();
        m_descriptionLabel->hide();
        m_moreMetadataButton->hide();
        return;
    }
    constexpr int collapsedLength = 330;
    const bool collapsible = m_fullDescription.size() > collapsedLength;
    const QString visible = (!collapsible || m_descriptionExpanded)
        ? m_fullDescription
        : m_fullDescription.left(collapsedLength).trimmed() + QStringLiteral("…");

    m_descriptionLabel->setText(visible);
    m_descriptionLabel->show();
    m_moreMetadataButton->setText(m_descriptionExpanded ? tr("Less") : tr("More"));
    m_moreMetadataButton->setVisible(collapsible);
}

void GameDetailsPanel::renderTemplate() {
    if (m_currentGame.id.isNull()) return;

    const Metadata::RomMetadata* remote = m_hasRomMetadata ? &m_romMetadata : nullptr;

    // 1. Year · Platform
    const int releaseYear = (remote && remote->releaseYear > 0)
        ? remote->releaseYear
        : (m_currentGame.releaseDate.isValid() ? m_currentGame.releaseDate.year() : 0);

    QString platform = remote ? displayPlatform(remote->platform) : QString();
    if (platform.isEmpty()) {
        platform = displayPlatform(Database::DatabaseManager::instance().getSystem(m_currentGame.systemId).name);
    }

    QStringList line1Items;
    if (releaseYear > 0) line1Items.append(QString::number(releaseYear));
    if (!platform.isEmpty()) line1Items.append(platform);
    const QString line1 = line1Items.join(QStringLiteral(" · "));

    // 2. Genre · Player range
    const QStringList genres = (remote && !remote->genres.isEmpty()) ? remote->genres : m_currentGame.genres;

    QString playerRange = remote ? remote->playerCount.trimmed() : QString();
    if (playerRange.isEmpty() && m_currentGame.playersMin > 0 && m_currentGame.playersMax > 0) {
        playerRange = (m_currentGame.playersMin == m_currentGame.playersMax)
            ? tr("%1 player").arg(QLocale().toString(m_currentGame.playersMin))
            : tr("%1–%2 players").arg(QLocale().toString(m_currentGame.playersMin), QLocale().toString(m_currentGame.playersMax));
    } else if (!playerRange.isEmpty() && !playerRange.contains(QRegularExpression(QStringLiteral("player"), QRegularExpression::CaseInsensitiveOption))) {
        playerRange += playerRange.contains(QRegularExpression(QStringLiteral("[-–]"))) ? tr(" players") : tr(" player");
    }

    QStringList line2Items;
    if (!genres.isEmpty()) line2Items.append(genres.join(QStringLiteral(", ")));
    if (!playerRange.isEmpty()) line2Items.append(playerRange);
    const QString line2 = line2Items.join(QStringLiteral(" · "));

    QStringList headerMetaLines;
    if (!line1.isEmpty()) headerMetaLines.append(line1);
    if (!line2.isEmpty()) headerMetaLines.append(line2);

    if (!headerMetaLines.isEmpty()) {
        m_headerMetaLabel->setText(headerMetaLines.join(QStringLiteral("\n")));
        m_headerMetaLabel->show();
    } else {
        m_headerMetaLabel->clear();
        m_headerMetaLabel->hide();
    }

    // 3. Developer / Publisher
    const QString developer = (remote && !remote->developer.isEmpty()) ? remote->developer : m_currentGame.developer;
    const QString publisher = (remote && !remote->publisher.isEmpty()) ? remote->publisher : m_currentGame.publisher;

    QStringList creditsLines;
    if (!developer.isEmpty()) creditsLines.append(tr("Developer: %1").arg(developer));
    if (!publisher.isEmpty()) creditsLines.append(tr("Publisher: %1").arg(publisher));

    if (!creditsLines.isEmpty()) {
        m_creditsLabel->setText(creditsLines.join(QStringLiteral("\n")));
        m_creditsLabel->show();
    } else {
        m_creditsLabel->clear();
        m_creditsLabel->hide();
    }

    // 4. Description
    QString descriptionText;
    if (remote && !remote->description.isEmpty() && !remote->descriptionIsAiGenerated &&
        remote->normalizationVersion >= Metadata::RomMetadata::NormalizationVersion &&
        isMeaningfulDescription(remote->description, *remote)) {
        descriptionText = remote->description;
    } else if (!m_currentGame.description.isEmpty()) {
        descriptionText = m_currentGame.description;
    }
    m_fullDescription = descriptionText;
    renderDescription();

    // 5. Region · Language · Revision
    QStringList regionList;
    if (remote && !remote->regions.isEmpty()) {
        for (const QString& r : remote->regions) {
            const QString disp = displayRegion(r);
            if (!disp.isEmpty() && !regionList.contains(disp, Qt::CaseInsensitive)) {
                regionList.append(disp);
            }
        }
    } else if (!m_currentGame.region.isEmpty()) {
        const QString disp = displayRegion(m_currentGame.region);
        if (!disp.isEmpty()) regionList.append(disp);
    }

    QStringList langList;
    if (remote && !remote->languages.isEmpty()) {
        langList = remote->languages;
    } else if (!m_currentGame.languages.isEmpty()) {
        langList = m_currentGame.languages;
    }

    QString revisionText;
    if (remote && !remote->revision.isEmpty()) {
        revisionText = remote->revision;
    }

    QStringList line5Items;
    if (!regionList.isEmpty()) line5Items.append(regionList.join(QStringLiteral(", ")));
    if (!langList.isEmpty()) line5Items.append(compactLanguages(langList));
    if (!revisionText.isEmpty()) line5Items.append(revisionText);
    const QString line5 = line5Items.join(QStringLiteral(" · "));

    if (!line5.isEmpty()) {
        m_regionLangRevLabel->setText(line5);
        m_regionLangRevLabel->show();
    } else {
        m_regionLangRevLabel->clear();
        m_regionLangRevLabel->hide();
    }

    // 6. ROM identification state / Signature source · Match method
    QString idStateText;
    QString idSourceMatchText;
    bool showRetry = false;
    QString stateStyle = QStringLiteral("font-weight: bold; color: #d5ad5c; margin-top: 8px;");

    if (remote) {
        const bool exactIdentity = !remote->matchedHashAlgorithm.isEmpty();
        idStateText = exactIdentity
            ? QStringLiteral("ROM identified ✓")
            : QStringLiteral("ROM metadata matched ✓");
        stateStyle = QStringLiteral("font-weight: bold; color: #75c885; margin-top: 8px;");
        if (m_romMetadataStale) {
            idStateText += QStringLiteral(" (cached)");
        }
        QString provenance = remote->dumpSource.isEmpty()
            ? QStringLiteral("Libretro Database 1.22.1")
            : remote->dumpSource;
        QString matchMethod = displayConfidence(remote->identityConfidence);
        idSourceMatchText = QStringLiteral("%1 · %2").arg(provenance, matchMethod);
    } else if (m_romState == "loading") {
        idStateText = m_romStateMessage.isEmpty() ? QStringLiteral("Looking up ROM information…") : m_romStateMessage;
    } else if (m_romState == "no-match") {
        idStateText = QStringLiteral("No exact metadata match.");
    } else if (m_romState == "unsupported") {
        idStateText = m_romStateMessage;
    } else if (m_romState == "offline") {
        const QString unavailable = m_romStateMessage.isEmpty()
            ? QStringLiteral("ROM metadata is temporarily unavailable.")
            : m_romStateMessage;
        idStateText = m_hasRomMetadata
            ? unavailable + QStringLiteral(" Showing cached ROM information.")
            : unavailable;
        showRetry = true;
    } else if (!m_romStateMessage.isEmpty()) {
        idStateText = m_romStateMessage;
    } else {
        idStateText = QStringLiteral("Unidentified ROM");
    }

    m_identityStateLabel->setText(idStateText);
    m_identityStateLabel->setStyleSheet(stateStyle);
    m_identityStateLabel->show();

    if (!idSourceMatchText.isEmpty()) {
        m_identitySourceMatchLabel->setText(idSourceMatchText);
        m_identitySourceMatchLabel->show();
    } else {
        m_identitySourceMatchLabel->clear();
        m_identitySourceMatchLabel->hide();
    }

    m_retryMetadataButton->setVisible(showRetry);

    // Extended technical details drawer
    QStringList identity;
    if (remote) {
        identity.append(QString("Verification: %1").arg(displayConfidence(remote->identityConfidence)));
        if (!remote->dumpSource.isEmpty()) identity.append(QString("Signature source: %1").arg(remote->dumpSource));
        if (remote->fetchedAt.isValid()) identity.append(QString("Last refreshed: %1").arg(QLocale().toString(remote->fetchedAt.toLocalTime(), QLocale::ShortFormat)));
        if (remote->languages.size() > 2) identity.append(QString("Languages: %1").arg(remote->languages.join(", ")));
        const Domain::GameFile file = Database::DatabaseManager::instance().getPrimaryFileForGame(m_currentGame.id);
        if (!file.datMatchId.isNull()) identity.append(QString("DAT record: %1").arg(file.datMatchId.toString(QUuid::WithoutBraces)));
    }
    m_identityDetailsLabel->setText(identity.join('\n'));
    m_identityDetailsButton->setVisible(!identity.isEmpty());
    m_identityDetailsLabel->setVisible(m_identityDetailsButton->isChecked() && !identity.isEmpty());

    // 7. Play count · Total duration · Last played
    QStringList playStatsItems;
    const bool unplayed = (m_currentGame.playCount == 0 && m_currentGame.status.compare("Unplayed", Qt::CaseInsensitive) == 0);
    if (unplayed) {
        playStatsItems.append(tr("Unplayed"));
    }
    if (m_currentGame.playCount > 0) {
        playStatsItems.append(m_currentGame.playCount == 1 ? tr("1 launch") : tr("%1 launches").arg(QLocale().toString(m_currentGame.playCount)));
    }
    if (m_currentGame.totalPlaySeconds >= 60) {
        playStatsItems.append(tr("%1 total").arg(formatDuration(m_currentGame.totalPlaySeconds)));
    } else if (m_currentGame.playCount > 0) {
        playStatsItems.append(tr("Less than 1 min played"));
    }
    if (m_currentGame.lastPlayed.isValid()) {
        playStatsItems.append(tr("Last played %1").arg(lastPlayedText(m_currentGame.lastPlayed)));
    }
    m_statusLabel->setText(playStatsItems.join(QStringLiteral(" · ")));
    m_statusLabel->show();
}

bool GameDetailsPanel::sameValue(const QString& left, const QString& right) {
    QString normalizedLeft = left.toLower(), normalizedRight = right.toLower();
    normalizedLeft.remove(QRegularExpression("[^a-z0-9]")); normalizedRight.remove(QRegularExpression("[^a-z0-9]"));
    if (normalizedLeft == "us" || normalizedLeft == "usa") normalizedLeft = "unitedstates";
    if (normalizedRight == "us" || normalizedRight == "usa") normalizedRight = "unitedstates";
    if (normalizedLeft == "world") normalizedLeft = "worldwide";
    if (normalizedRight == "world") normalizedRight = "worldwide";
    return !normalizedLeft.isEmpty() && normalizedLeft == normalizedRight;
}

QString GameDetailsPanel::displayRegion(const QString& region) {
    const QString normalized = region.trimmed().toCaseFolded();
    if (normalized == QStringLiteral("us") || normalized == QStringLiteral("usa") || normalized == QStringLiteral("united states"))
        return QStringLiteral("United States");
    if (normalized == QStringLiteral("world") || normalized == QStringLiteral("worldwide")) return QStringLiteral("Worldwide");
    if (normalized == QStringLiteral("eu") || normalized == QStringLiteral("eur") || normalized == QStringLiteral("europe")) return QStringLiteral("Europe");
    if (normalized == QStringLiteral("jp") || normalized == QStringLiteral("jpn") || normalized == QStringLiteral("japan")) return QStringLiteral("Japan");
    return region.trimmed();
}

bool GameDetailsPanel::sameValues(const QStringList& left, const QStringList& right) {
    if (left.size() != right.size()) return false;
    for (const QString& item : left) {
        bool found = false;
        for (const QString& candidate : right) if (sameValue(item, candidate)) { found = true; break; }
        if (!found) return false;
    }
    return !left.isEmpty();
}

QString GameDetailsPanel::displayConfidence(const QString& confidence) {
    if (confidence.startsWith("exact-")) return confidence.mid(6).toUpper() + QStringLiteral(" exact hash");
    if (confidence == "crc-and-size") return QStringLiteral("CRC32 + file size");
    return confidence;
}

QString GameDetailsPanel::displayPlatform(const QString& platform) {
    QString value = platform.trimmed();
    if (value.compare(QStringLiteral("Sega Mega Drive / Genesis"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Mega Drive / Genesis");
    return value;
}

QString GameDetailsPanel::compactLanguages(const QStringList& languages) {
    if (languages.isEmpty()) return {};
    QStringList ordered = languages;
    std::stable_sort(ordered.begin(), ordered.end(), [](const QString& left, const QString& right) {
        return left.compare(QStringLiteral("English"), Qt::CaseInsensitive) == 0 &&
               right.compare(QStringLiteral("English"), Qt::CaseInsensitive) != 0;
    });
    const int visibleCount = qMin(2, static_cast<int>(ordered.size()));
    QString value = ordered.mid(0, visibleCount).join(QStringLiteral(", "));
    if (ordered.size() > visibleCount) value += QStringLiteral(" +%1").arg(ordered.size() - visibleCount);
    return value;
}

bool GameDetailsPanel::isMeaningfulDescription(const QString& description, const Metadata::RomMetadata& metadata) const {
    Q_UNUSED(metadata);
    const QString trimmed = description.trimmed();
    if (trimmed.isEmpty()) return false;
    const QString folded = trimmed.toCaseFolded();
    static const QStringList syntheticMarkers{
        QStringLiteral("identity lookup"), QStringLiteral("metadata lookup"),
        QStringLiteral("screenscraper identity lookup")
    };
    for (const QString& marker : syntheticMarkers) if (folded.contains(marker)) return false;
    return true;
}

QString GameDetailsPanel::lastPlayedText(const QDateTime& lastPlayed) {
    const QDate date = lastPlayed.toLocalTime().date();
    const QDate today = QDate::currentDate();
    if (date == today) return QObject::tr("today");
    if (date == today.addDays(-1)) return QObject::tr("yesterday");
    return QLocale().toString(date, QStringLiteral("MMM d, yyyy"));
}

QString GameDetailsPanel::formatDuration(int seconds) {
    const int hours = seconds / 3600, minutes = (seconds % 3600) / 60;
    const int remainingSeconds = seconds % 60;
    if (hours > 0) {
        const QString hourText = hours == 1 ? QObject::tr("1 hour") : QObject::tr("%1 hours").arg(QLocale().toString(hours));
        if (minutes == 0) return hourText;
        return hourText + QStringLiteral(" ") + (minutes == 1 ? QObject::tr("1 minute") : QObject::tr("%1 minutes").arg(QLocale().toString(minutes)));
    }
    if (minutes > 0) return minutes == 1 ? QObject::tr("1 minute") : QObject::tr("%1 minutes").arg(QLocale().toString(minutes));
    return remainingSeconds == 1 ? QObject::tr("1 second") : QObject::tr("%1 seconds").arg(QLocale().toString(remainingSeconds));
}

void GameDetailsPanel::clear() {
    m_currentGame = {};
    m_romMetadata = {};
    m_hasRomMetadata = false;
    m_romMetadataStale = false;
    m_romState.clear();
    m_romStateMessage.clear();
    m_fullDescription.clear();
    m_descriptionExpanded = false;

    m_artworkLabel->clear();
    m_titleLabel->setText("Select a game");
    m_headerMetaLabel->clear(); m_headerMetaLabel->hide();
    m_creditsLabel->clear(); m_creditsLabel->hide();
    m_descriptionLabel->clear(); m_descriptionLabel->hide();
    m_moreMetadataButton->hide();
    m_regionLangRevLabel->clear(); m_regionLangRevLabel->hide();
    m_identityStateLabel->clear(); m_identityStateLabel->hide();
    m_identitySourceMatchLabel->clear(); m_identitySourceMatchLabel->hide();
    m_retryMetadataButton->hide();
    m_identityDetailsButton->setChecked(false);
    m_identityDetailsButton->hide();
    m_identityDetailsLabel->clear();
    m_identityDetailsLabel->hide();
    m_statusLabel->clear(); m_statusLabel->hide();
    m_launchBtn->setEnabled(false);
}

} // namespace LudoShelf::UI
