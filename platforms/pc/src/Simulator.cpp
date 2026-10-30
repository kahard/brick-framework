#include "brick/platform/pc/Simulator.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#if BRICK_PC_HAS_SDL2
#    define SDL_MAIN_HANDLED
#    include <SDL.h>
#endif

namespace brick::platform::pc
{

namespace
{

    void print_usage(const char* program)
    {
        std::printf("Usage: %s [options]\n"
                    "  --headless              run without a window\n"
                    "  --frames N              stop after N application updates\n"
                    "  --manual-time [MS]      deterministic time, each update advances MS (default 16)\n"
                    "  --scale N               window zoom factor\n"
                    "  --screenshot FILE.ppm   save the last frame when stopping\n"
                    "  --fs DIR                directory used as the SD card (default: sdcard)\n"
                    "  --serial-in FILE        bytes the serial port receives\n"
                    "  --touch X,Y@MS[+DUR]    touch at MS milliseconds for DUR ms (repeatable)\n"
                    "  --press MS[+DUR]        press the button at MS for DUR ms (repeatable)\n"
                    "Window: mouse = touch, space = button, Esc = quit.\n",
                    program);
    }

    bool parse_u32(const char* text, std::uint32_t& value)
    {
        char*               end    = nullptr;
        const unsigned long parsed = std::strtoul(text, &end, 10);
        if (end == text || *end != '\0')
            return false;
        value = static_cast<std::uint32_t>(parsed);
        return true;
    }

    bool parse_timing(const char* text, std::uint32_t& at_ms, std::uint32_t& duration_ms)
    {
        unsigned at = 0, duration = 100;
        int      consumed = 0;
        if (std::sscanf(text, "%u%n", &at, &consumed) != 1)
            return false;
        if (text[consumed] == '+' && std::sscanf(text + consumed + 1, "%u", &duration) != 1)
            return false;
        at_ms       = at;
        duration_ms = duration;
        return true;
    }

    bool parse_touch(const char* text, ScriptedTouch& touch)
    {
        int x = 0, y = 0, consumed = 0;
        if (std::sscanf(text, "%d,%d@%n", &x, &y, &consumed) != 2 || consumed == 0)
            return false;
        touch.x = static_cast<std::int16_t>(x);
        touch.y = static_cast<std::int16_t>(y);
        return parse_timing(text + consumed, touch.at_ms, touch.duration_ms);
    }

}  // namespace

bool SimulatorOptions::parse(int argc, char** argv, SimulatorOptions& options)
{
    for (int index = 1; index < argc; ++index)
    {
        const char* option = argv[index];
        const char* value  = index + 1 < argc ? argv[index + 1] : nullptr;
        bool        ok     = true;
        if (std::strcmp(option, "--headless") == 0)
            options.headless = true;
        else if (std::strcmp(option, "--frames") == 0 && value != nullptr)
            ok = parse_u32(argv[++index], options.frames);
        else if (std::strcmp(option, "--manual-time") == 0)
        {
            options.manual_time = true;
            if (value != nullptr && value[0] != '-')
                ok = parse_u32(argv[++index], options.step_ms);
        }
        else if (std::strcmp(option, "--scale") == 0 && value != nullptr)
            ok = parse_u32(argv[++index], options.scale);
        else if (std::strcmp(option, "--screenshot") == 0 && value != nullptr)
            options.screenshot = argv[++index];
        else if (std::strcmp(option, "--fs") == 0 && value != nullptr)
            options.fs_root = argv[++index];
        else if (std::strcmp(option, "--serial-in") == 0 && value != nullptr)
            options.serial_input = argv[++index];
        else if (std::strcmp(option, "--touch") == 0 && value != nullptr)
        {
            ScriptedTouch touch;
            ok = parse_touch(argv[++index], touch);
            options.touches.push_back(touch);
        }
        else if (std::strcmp(option, "--press") == 0 && value != nullptr)
        {
            ScriptedPress press;
            ok = parse_timing(argv[++index], press.at_ms, press.duration_ms);
            options.presses.push_back(press);
        }
        else
            ok = false;
        if (!ok)
        {
            if (std::strcmp(option, "--help") != 0 && std::strcmp(option, "-h") != 0)
                std::fprintf(stderr, "Invalid option: %s\n", option);
            print_usage(argv[0]);
            return false;
        }
    }
    return true;
}

#if BRICK_PC_HAS_SDL2

struct Simulator::Window
{
    SDL_Window*   window   = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture*  texture  = nullptr;
    std::uint16_t width    = 0;
    std::uint16_t height   = 0;
    std::uint32_t scale    = 1;
    std::uint32_t shown    = 0xFFFFFFFFU;
    bool          mouse    = false;

    ~Window()
    {
        if (texture != nullptr)
            SDL_DestroyTexture(texture);
        if (renderer != nullptr)
            SDL_DestroyRenderer(renderer);
        if (window != nullptr)
            SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

#else

struct Simulator::Window
{
};

#endif

Simulator::Simulator(const SimulatorOptions& options, PcFrameBufferDisplay& display, PcTime& time, const char* title)
    : options_(options), display_(display), time_(time), title_(title != nullptr ? title : "BRICK simulator")
{
}

Simulator::~Simulator() = default;

bool Simulator::has_window() const
{
    return window_ != nullptr;
}

bool Simulator::begin()
{
    if (options_.headless)
        return true;
#if BRICK_PC_HAS_SDL2
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    window_         = std::make_unique<Window>();
    const auto size = display_.size();
    window_->scale =
        options_.scale != 0 ? options_.scale : std::max<std::uint32_t>(1U, 900U / std::max(size.width, size.height));
    window_->window = SDL_CreateWindow(title_.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       static_cast<int>(size.width * window_->scale),
                                       static_cast<int>(size.height * window_->scale), 0);
    if (window_->window != nullptr)
        window_->renderer = SDL_CreateRenderer(window_->window, -1, SDL_RENDERER_PRESENTVSYNC);
    if (window_->renderer == nullptr)
    {
        std::fprintf(stderr, "Cannot open the simulator window: %s\n", SDL_GetError());
        window_.reset();
        return false;
    }
    return true;
#else
    std::fprintf(stderr, "Built without SDL2: running headless\n");
    options_.headless = true;
    return true;
#endif
}

void Simulator::apply_script()
{
    const std::uint32_t now = time_.millis();
    if (touch_ != nullptr && !options_.touches.empty())
    {
        const ScriptedTouch* active = nullptr;
        for (const auto& touch : options_.touches)
            if (now >= touch.at_ms && now < touch.at_ms + touch.duration_ms)
                active = &touch;
        if (active != nullptr)
            touch_->set(true, active->x, active->y);
        else if (script_touching_)
            touch_->set(false, 0, 0);
        script_touching_ = active != nullptr;
    }
    if (button_ != nullptr && !options_.presses.empty())
    {
        bool pressed = false;
        for (const auto& press : options_.presses)
            pressed = pressed || (now >= press.at_ms && now < press.at_ms + press.duration_ms);
        if (pressed || script_pressing_)
            button_->set_pressed(pressed);
        script_pressing_ = pressed;
    }
}

bool Simulator::step()
{
    if (options_.frames != 0 && updates_ >= options_.frames)
        return false;
    if (options_.manual_time && updates_ != 0)
        time_.advance_ms(options_.step_ms);
    apply_script();
#if BRICK_PC_HAS_SDL2
    if (window_ != nullptr)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            const auto to_display = [this](int value) {  // brick-rules: allow(lambda) host-only simulator code
                return static_cast<std::int16_t>(value / static_cast<int>(window_->scale));
            };
            switch (event.type)
            {
                case SDL_QUIT:
                    return false;
                case SDL_KEYDOWN:
                case SDL_KEYUP:
                    if (event.key.keysym.sym == SDLK_ESCAPE)
                        return false;
                    if (event.key.keysym.sym == SDLK_SPACE && button_ != nullptr)
                        button_->set_pressed(event.type == SDL_KEYDOWN);
                    break;
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP:
                    window_->mouse = event.type == SDL_MOUSEBUTTONDOWN;
                    if (touch_ != nullptr)
                        touch_->set(window_->mouse, to_display(event.button.x), to_display(event.button.y));
                    break;
                case SDL_MOUSEMOTION:
                    if (window_->mouse && touch_ != nullptr)
                        touch_->set(true, to_display(event.motion.x), to_display(event.motion.y));
                    break;
                default:
                    break;
            }
        }
        const auto size = display_.size();
        if (window_->texture == nullptr || window_->width != size.width || window_->height != size.height)
        {
            if (window_->texture != nullptr)
                SDL_DestroyTexture(window_->texture);
            window_->texture = SDL_CreateTexture(window_->renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                                                 size.width, size.height);
            window_->width   = size.width;
            window_->height  = size.height;
            window_->shown   = 0xFFFFFFFFU;
            SDL_SetWindowSize(window_->window, static_cast<int>(size.width * window_->scale),
                              static_cast<int>(size.height * window_->scale));
        }
        if (window_->shown != display_.frame_counter())
        {
            SDL_UpdateTexture(window_->texture, nullptr, display_.screen(), size.width * 2);
            window_->shown = display_.frame_counter();
        }
        const std::uint8_t level =
            backlight_ != nullptr ? static_cast<std::uint8_t>(backlight_->brightness_percent() * 255U / 100U) : 255U;
        SDL_SetTextureColorMod(window_->texture, level, level, level);
        SDL_RenderClear(window_->renderer);
        SDL_RenderCopy(window_->renderer, window_->texture, nullptr, nullptr);
        SDL_RenderPresent(window_->renderer);
    }
#endif
    ++updates_;
    return true;
}

bool Simulator::finish()
{
    window_.reset();
    if (options_.screenshot.empty())
        return true;
    if (!write_ppm(display_, options_.screenshot))
    {
        std::fprintf(stderr, "Cannot write %s\n", options_.screenshot.c_str());
        return false;
    }
    std::printf("Screenshot written to %s\n", options_.screenshot.c_str());
    return true;
}

bool Simulator::write_ppm(const PcFrameBufferDisplay& display, const std::string& path)
{
    std::ofstream file(path, std::ios::binary);
    if (!file)
        return false;
    const auto size = display.size();
    file << "P6\n" << size.width << ' ' << size.height << "\n255\n";
    const std::uint16_t* pixels = display.screen();
    std::vector<char>    row(static_cast<std::size_t>(size.width) * 3U);
    for (std::uint32_t y = 0; y < size.height; ++y)
    {
        for (std::uint32_t x = 0; x < size.width; ++x)
        {
            const std::uint16_t pixel = pixels[y * size.width + x];
            const auto          red   = static_cast<std::uint8_t>((pixel >> 11) & 0x1FU);
            const auto          green = static_cast<std::uint8_t>((pixel >> 5) & 0x3FU);
            const auto          blue  = static_cast<std::uint8_t>(pixel & 0x1FU);
            row[x * 3U]               = static_cast<char>((red << 3) | (red >> 2));
            row[x * 3U + 1U]          = static_cast<char>((green << 2) | (green >> 4));
            row[x * 3U + 2U]          = static_cast<char>((blue << 3) | (blue >> 2));
        }
        file.write(row.data(), static_cast<std::streamsize>(row.size()));
    }
    return static_cast<bool>(file);
}

}  // namespace brick::platform::pc
