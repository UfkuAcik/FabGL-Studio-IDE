#include "demo.h"

#if FGL_HAS_NATIVE_WINDOW
#include "win32_window.h"
#endif

#include <fabgl/rendering/framebuffer.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

struct Options final {
    fabgl::player::DemoKind demo = fabgl::player::DemoKind::Platformer2D;
    int frames = 180;
    bool headless = false;
    std::string output;
};

[[nodiscard]] Options parseOptions(int argc, char** argv) {
    Options options;
    for (auto index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help") {
            std::cout << "Usage: fabgl_player_pc [--demo "
                         "empty|2d|topdown|raycast|racer|lowpoly|ui|audio|animation|streaming] "
                         "[--headless] [--frames N] [--output frame.ppm]\n";
            std::exit(EXIT_SUCCESS);
        }
        if (argument == "--headless") {
            options.headless = true;
            continue;
        }
        if (argument == "--demo" && index + 1 < argc) {
            options.demo = fabgl::player::parseDemoKind(argv[++index]);
            continue;
        }
        if (argument == "--frames" && index + 1 < argc) {
            options.frames = std::stoi(argv[++index]);
            if (options.frames < 1 || options.frames > 36000) {
                throw std::invalid_argument("--frames must be between 1 and 36000");
            }
            continue;
        }
        if (argument == "--output" && index + 1 < argc) {
            options.output = argv[++index];
            continue;
        }
        throw std::invalid_argument("unknown or incomplete argument: " + argument);
    }
    return options;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parseOptions(argc, argv);
        fabgl::rendering::Framebuffer framebuffer(320, 180);
        fabgl::player::Demo demo(framebuffer, options.demo);

#if FGL_HAS_NATIVE_WINDOW
        if (!options.headless) {
            fabgl::player::Win32Window window;
            std::string error;
            if (!window.open(framebuffer.width(), framebuffer.height(), 3, demo.title(), error)) {
                std::cerr << error << '\n';
                return EXIT_FAILURE;
            }
            auto previous = std::chrono::steady_clock::now();
            fabgl::player::InputState input;
            while (window.poll(input)) {
                const auto now = std::chrono::steady_clock::now();
                const auto delta = std::chrono::duration<float>(now - previous).count();
                previous = now;
                demo.update(delta, input);
                demo.render();
                window.present(framebuffer);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } else
#endif
        {
            fabgl::player::InputState input;
            input.forward = true;
            input.right = options.demo == fabgl::player::DemoKind::Racer;
            for (auto frame = 0; frame < options.frames; ++frame) {
                input.action = (frame % 90) == 4;
                demo.update(1.0F / 60.0F, input);
                demo.render();
            }
        }

        if (!options.output.empty()) {
            std::string error;
            if (!framebuffer.savePpm(options.output, error)) {
                std::cerr << error << '\n';
                return EXIT_FAILURE;
            }
        }
        std::cout << demo.title() << " checksum=" << framebuffer.checksum() << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "fabgl_player_pc: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
