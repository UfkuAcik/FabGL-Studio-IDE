#include "test_harness.h"

#include <demo.h>
#include <fabgl/rendering/framebuffer.h>

#include <array>
#include <cstdint>

namespace {

std::uint64_t renderDemo(fabgl::player::DemoKind kind) {
    fabgl::rendering::Framebuffer framebuffer(320, 180);
    fabgl::player::Demo demo(framebuffer, kind);
    fabgl::player::InputState input;
    input.forward = true;
    input.right = kind == fabgl::player::DemoKind::Racer;
    for (auto frame = 0; frame < 180; ++frame) {
        input.action = (frame % 90) == 4;
        demo.update(1.0F / 60.0F, input);
        demo.render();
    }
    return framebuffer.checksum();
}

} // namespace

FGL_TEST(framebuffer_clips_and_blends_deterministically) {
    fabgl::rendering::Framebuffer framebuffer(4, 3);
    framebuffer.clear({0, 0, 0, 255});
    framebuffer.fillRect(-2, 1, 5, 3, {100, 50, 25, 255});
    framebuffer.blendPixel(0, 1, {200, 100, 50, 128});
    framebuffer.drawLine(0, 0, 3, 2, {255, 255, 255, 255});
    FGL_CHECK(framebuffer.pixel(-1, 0) == fabgl::Color{});
    FGL_CHECK(framebuffer.pixel(3, 2) == (fabgl::Color{255, 255, 255, 255}));
    FGL_CHECK(framebuffer.checksum() == 1580809287497516863ULL);
}

FGL_TEST(reference_renderers_match_golden_checksums) {
    const std::array<std::pair<fabgl::player::DemoKind, std::uint64_t>, 10> references = {{
        {fabgl::player::DemoKind::Empty, 3684892159837637615ULL},
        {fabgl::player::DemoKind::Platformer2D, 4674214439384696037ULL},
        {fabgl::player::DemoKind::TopDown, 6529423119449967289ULL},
        {fabgl::player::DemoKind::RaycastFps, 14763779848664866702ULL},
        {fabgl::player::DemoKind::Racer, 6713019085718644632ULL},
        {fabgl::player::DemoKind::LowPolyExperimental, 13254507645075047209ULL},
        {fabgl::player::DemoKind::UiShowcase, 6744144588786156117ULL},
        {fabgl::player::DemoKind::AudioShowcase, 4061273253253862341ULL},
        {fabgl::player::DemoKind::AnimationShowcase, 13752941473616129720ULL},
        {fabgl::player::DemoKind::AssetStreaming, 15684697036675318470ULL},
    }};
    for (const auto& reference : references) {
        FGL_CHECK(renderDemo(reference.first) == reference.second);
    }
}
