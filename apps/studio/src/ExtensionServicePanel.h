#pragma once

#include <fabgl/project/project_extension_service_host.h>

#include <QWidget>

#include <vector>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace fgl::studio {

class ExtensionServicePanel final : public QWidget {
    Q_OBJECT

  public:
    explicit ExtensionServicePanel(QWidget* parent = nullptr);

    void setServices(
        const std::vector<fabgl::project::ProjectExtensionServiceState>& services,
        bool executionEnabled);
    void setDispatchResult(const QString& qualifiedServiceId, bool success,
                           const QString& message);

  signals:
    void serviceInvocationRequested(const QString& qualifiedServiceId, int kind);

  private:
    void updateSelection();
    void invokeSelection();
    [[nodiscard]] static bool interactiveKind(fabgl::PackageEntryPointKind kind) noexcept;

    QTreeWidget* m_tree = nullptr;
    QLabel* m_summary = nullptr;
    QLabel* m_result = nullptr;
    QPushButton* m_invokeButton = nullptr;
    bool m_executionEnabled = false;
};

} // namespace fgl::studio
