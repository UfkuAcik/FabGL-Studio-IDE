#pragma once

#include <QString>

namespace fgl::studio {

class ProjectTrustStore final {
  public:
    [[nodiscard]] bool isTrusted(const QString& projectPath) const;
    [[nodiscard]] bool setTrusted(const QString& projectPath, bool trusted,
                                  QString& errorMessage) const;
    [[nodiscard]] bool clearDecision(const QString& projectPath, QString& errorMessage) const;
    [[nodiscard]] static QString normalizedProjectPath(const QString& projectPath);
    [[nodiscard]] static QString decisionKey(const QString& projectPath);
};

} // namespace fgl::studio
