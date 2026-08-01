#include "fabgl/scene/component.h"

#include "fabgl/scene/entity.h"

namespace fabgl {

void Component::setEnabled(bool enabled) {
    if (enabled_ == enabled || destroyed_) {
        return;
    }
    enabled_ = enabled;
    if (owner_ != nullptr) {
        owner_->refreshComponentState(*this);
    }
}

} // namespace fabgl
