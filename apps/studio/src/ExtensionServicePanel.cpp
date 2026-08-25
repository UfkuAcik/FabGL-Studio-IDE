#include "ExtensionServicePanel.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace fgl::studio {
namespace {

constexpr int QualifiedIdRole = Qt::UserRole;
constexpr int KindRole = Qt::UserRole + 1;

[[nodiscard]] QString kindName(const fabgl::PackageEntryPointKind kind) {
    return QString::fromLatin1(fabgl::packageEntryPointKindName(kind).data(),
                               static_cast<qsizetype>(
                                   fabgl::packageEntryPointKindName(kind).size()));
}

} // namespace

ExtensionServicePanel::ExtensionServicePanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("extensionServicePanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    m_summary = new QLabel(tr("No project extension services are registered."), this);
    m_summary->setObjectName(QStringLiteral("extensionServiceSummary"));
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("extensionServiceTree"));
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({tr("Kind"), tr("Service"), tr("Provider"), tr("State")});
    m_tree->setRootIsDecorated(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setAlternatingRowColors(true);
    m_tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(m_tree, 1);

    m_invokeButton = new QPushButton(tr("Invoke Selected Service"), this);
    m_invokeButton->setObjectName(QStringLiteral("extensionServiceInvokeButton"));
    m_invokeButton->setEnabled(false);
    layout->addWidget(m_invokeButton);

    m_result = new QLabel(tr("Select a service to inspect its dispatch contract."), this);
    m_result->setObjectName(QStringLiteral("extensionServiceResult"));
    m_result->setWordWrap(true);
    m_result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_result);

    connect(m_tree, &QTreeWidget::itemSelectionChanged, this,
            &ExtensionServicePanel::updateSelection);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem*, int) { invokeSelection(); });
    connect(m_invokeButton, &QPushButton::clicked, this,
            &ExtensionServicePanel::invokeSelection);
}

bool ExtensionServicePanel::interactiveKind(const fabgl::PackageEntryPointKind kind) noexcept {
    return kind == fabgl::PackageEntryPointKind::EditorPlugin ||
           kind == fabgl::PackageEntryPointKind::AssetImporter ||
           kind == fabgl::PackageEntryPointKind::CustomInspector ||
           kind == fabgl::PackageEntryPointKind::CustomWindow;
}

void ExtensionServicePanel::setServices(
    const std::vector<fabgl::project::ProjectExtensionServiceState>& services,
    const bool executionEnabled) {
    const auto selected = m_tree->currentItem() != nullptr
                              ? m_tree->currentItem()->data(0, QualifiedIdRole).toString()
                              : QString{};
    m_executionEnabled = executionEnabled;
    m_tree->clear();
    qsizetype disabled = 0;
    QTreeWidgetItem* restoreSelection = nullptr;
    for (const auto& state : services) {
        const auto qualifiedId = QString::fromStdString(state.service.qualifiedId());
        auto* item = new QTreeWidgetItem(
            m_tree, {kindName(state.service.kind), QString::fromStdString(state.service.displayName),
                     QString::fromStdString(state.service.extensionId),
                     QString::fromLatin1(fabgl::project::projectExtensionServiceStateName(
                                             state.state)
                                             .data(),
                                         static_cast<qsizetype>(
                                             fabgl::project::projectExtensionServiceStateName(
                                                 state.state)
                                                 .size()))});
        item->setData(0, QualifiedIdRole, qualifiedId);
        item->setData(0, KindRole, static_cast<int>(state.service.kind));
        item->setToolTip(1, qualifiedId);
        if (!state.enabled()) {
            ++disabled;
            item->setDisabled(true);
            item->setToolTip(3, QString::fromStdString(state.lastError));
        }
        if (qualifiedId == selected)
            restoreSelection = item;
    }
    if (services.empty()) {
        m_summary->setText(
            executionEnabled
                ? tr("No native service descriptors were registered by this project.")
                : tr("Native extension services are disabled by Safe Mode, project trust, or "
                     "plugin settings."));
    } else {
        m_summary->setText(
            tr("%1 registered service(s); %2 disabled after a dispatch failure. Runtime, "
               "renderer, framework, and build services run automatically.")
                .arg(static_cast<qulonglong>(services.size()))
                .arg(disabled));
    }
    if (restoreSelection != nullptr)
        m_tree->setCurrentItem(restoreSelection);
    updateSelection();
}

void ExtensionServicePanel::setDispatchResult(const QString& qualifiedServiceId,
                                              const bool success,
                                              const QString& message) {
    m_result->setText(success ? tr("%1: %2").arg(qualifiedServiceId, message)
                              : tr("%1 was disabled: %2").arg(qualifiedServiceId, message));
}

void ExtensionServicePanel::updateSelection() {
    const auto* item = m_tree->currentItem();
    if (item == nullptr) {
        m_invokeButton->setEnabled(false);
        return;
    }
    const auto kind = static_cast<fabgl::PackageEntryPointKind>(item->data(0, KindRole).toInt());
    const bool interactive = interactiveKind(kind);
    m_invokeButton->setText(interactive ? tr("Invoke Selected Service")
                                        : tr("Automatic Lifecycle Service"));
    m_invokeButton->setEnabled(m_executionEnabled && interactive && !item->isDisabled());
}

void ExtensionServicePanel::invokeSelection() {
    auto* item = m_tree->currentItem();
    if (item == nullptr || !m_invokeButton->isEnabled())
        return;
    emit serviceInvocationRequested(
        item->data(0, QualifiedIdRole).toString(), item->data(0, KindRole).toInt());
}

} // namespace fgl::studio
