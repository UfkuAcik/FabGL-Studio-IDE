#include <fabgl/assets/image_pipeline.h>

namespace fabgl::assets {

Result<Image> loadImage(const std::string& utf8Path) {
    return Result<Image>::failure(
        Error(ErrorCode::InvalidState,
              "this build has no image decoder; enable the platform PNG/JPEG decoder")
            .addContext("path", utf8Path));
}

} // namespace fabgl::assets
