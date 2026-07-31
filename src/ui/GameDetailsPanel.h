#ifndef LUDOSHELF_UI_GAMEDETAILSPANEL_H
#define LUDOSHELF_UI_GAMEDETAILSPANEL_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QUuid>

#include "../domain/Game.h"
#include "../metadata/RomMetadataTypes.h"

namespace LudoShelf::UI {

class GameDetailsPanel : public QWidget {
    Q_OBJECT
public:
    explicit GameDetailsPanel(QWidget *parent = nullptr);

    void setGame(const Domain::Game& game);
    void setRomMetadata(const Metadata::RomMetadata& metadata, bool stale = false);
    void setRomMetadataState(const QString& state, const QString& message = {});
    void clear();

signals:
    void launchRequested(const QUuid& gameId);
    void romMetadataRetryRequested(const QUuid& gameId);

private:
    QLabel *m_artworkLabel;
    QLabel *m_titleLabel;
    QLabel *m_headerMetaLabel;           // Year · Platform \n Genre · Player range
    QLabel *m_creditsLabel;              // Developer \n Publisher
    QLabel *m_descriptionLabel;          // Description
    QPushButton *m_moreMetadataButton;    // Expandable Description button
    QLabel *m_regionLangRevLabel;        // Region · Language · Revision
    QLabel *m_identityStateLabel;        // ROM identification state
    QLabel *m_identitySourceMatchLabel;  // Signature source · Match method
    QToolButton *m_retryMetadataButton;
    QToolButton *m_identityDetailsButton;
    QLabel *m_identityDetailsLabel;
    QLabel *m_statusLabel;               // Play count · Total duration · Last played
    QPushButton *m_launchBtn;

    Domain::Game m_currentGame;
    Metadata::RomMetadata m_romMetadata;
    bool m_hasRomMetadata{false};
    bool m_romMetadataStale{false};
    QString m_romState;
    QString m_romStateMessage;

    QString m_fullDescription;
    bool m_descriptionExpanded{false};

    void renderTemplate();
    void renderDescription();
    static bool sameValue(const QString& left, const QString& right);
    static bool sameValues(const QStringList& left, const QStringList& right);
    static QString displayRegion(const QString& region);
    static QString displayConfidence(const QString& confidence);
    static QString displayPlatform(const QString& platform);
    static QString compactLanguages(const QStringList& languages);
    bool isMeaningfulDescription(const QString& description, const Metadata::RomMetadata& metadata) const;
    static QString lastPlayedText(const QDateTime& lastPlayed);
    static QString formatDuration(int seconds);
};

} // namespace LudoShelf::UI

#endif // LUDOSHELF_UI_GAMEDETAILSPANEL_H
