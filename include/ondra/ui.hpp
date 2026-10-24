// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ondra/options.hpp>
#include <memory>
struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;
namespace ondra {
// SDL/ImGui desktop frontend; the emulated machine remains GUI-independent.
class Desktop {
public:
    static void restorePreferences(Options& options);
    Desktop(SDL_Window* window, SDL_Renderer* renderer, Machine& machine, const Options& options);
    ~Desktop();
    Desktop(const Desktop&) = delete;
    Desktop& operator=(const Desktop&) = delete;
    void event(const SDL_Event& event);
    void draw();
    void render(const std::string& capture = "");
    bool running() const;
    bool paused() const;
    bool capturesKeyboard() const;
    bool soundEnabled() const;
    void pause();
    void quit();
    void report(const std::string& error);
    void droppedFile(const std::string& path);
    void applyVirtualKeys();
    void flushRecording();
    // Bounds are local to widgetWindow(), useful for accessibility/UI tests.
    bool widgetBounds(const std::string& name, float& x, float& y, float& width, float& height) const;
    SDL_Window* widgetWindow(const std::string& name) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
