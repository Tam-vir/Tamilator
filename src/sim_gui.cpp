#include "Vtop.h"
#include "verilated.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <type_traits>
#include <cstring>
#include <chrono>
#include <cstdint>

using json = nlohmann::json;

// Verilator
Vtop *top;
using Sig = CData;

// FPGA STATE
struct FPGAState
{
    bool btn[8] = {0};
    bool led[8] = {0};
    int bar_values[3] = {0};
    bool clk = false;
    uint8_t dip[3] = {0};
};

FPGAState fpga;

// Forward declaration
void add_available_signals(std::unordered_map<std::string, Sig *> &signal_db, Vtop *top);

// SIGNAL MAP
struct SignalMap
{
    Vtop *top;

    std::unordered_map<std::string, Sig *> inputs;
    std::unordered_map<std::string, Sig *> outputs;

    struct BarGraphConnection
    {
        std::string name;
        std::string signal_name;
        Sig *value_ptr;
        bool connected;
        int current_value;
        int width;
    };
    BarGraphConnection bar_graphs[3];

    struct DipswitchConnection
    {
        std::string name;
        std::string signal_name;
        Sig *value_ptr;
        bool connected;
        uint8_t current_value;
    };
    DipswitchConnection dipswitches[3];

    std::unordered_map<std::string, std::string> in_map;
    std::unordered_map<std::string, std::string> out_map;
    std::unordered_map<std::string, std::string> bar_map;
    std::unordered_map<std::string, std::string> dip_map;
    std::unordered_map<std::string, Sig *> signal_db;

    bool btn_connected[8] = {false};
    bool led_connected[8] = {false};
    bool clk_connected = false;
    Sig *clk_signal = nullptr;

    void load_and_build(const json &j)
    {
        bar_graphs[0] = {"BAR0", "", nullptr, false, 0, 8};
        bar_graphs[1] = {"BAR1", "", nullptr, false, 0, 8};
        bar_graphs[2] = {"BAR2", "", nullptr, false, 0, 8};

        dipswitches[0] = {"DIP0", "", nullptr, false, 0};
        dipswitches[1] = {"DIP1", "", nullptr, false, 0};
        dipswitches[2] = {"DIP2", "", nullptr, false, 0};

        if (j.contains("inputs"))
        {
            for (auto &[k, v] : j["inputs"].items())
                in_map[k] = v;
        }

        if (j.contains("outputs"))
        {
            for (auto &[k, v] : j["outputs"].items())
                out_map[k] = v;
        }

        if (j.contains("bargraphs"))
        {
            for (auto &[k, v] : j["bargraphs"].items())
                bar_map[k] = v;
        }

        if (j.contains("dipswitches"))
        {
            for (auto &[k, v] : j["dipswitches"].items())
                dip_map[k] = v;
        }

        add_available_signals(signal_db, top);

        if (j.contains("clock") && j["clock"].is_string())
        {
            std::string clk_signal_name = j["clock"].get<std::string>();
            auto it = signal_db.find(clk_signal_name);
            if (it != signal_db.end() && it->second != nullptr)
            {
                clk_signal = it->second;
                clk_connected = true;
            }
        }

        for (int i = 0; i < 3; i++)
        {
            std::string bar_name = "BAR" + std::to_string(i);
            if (bar_map.find(bar_name) != bar_map.end())
            {
                std::string verilog_signal = bar_map[bar_name];
                std::string base_name = verilog_signal;
                size_t bracket_start = verilog_signal.find('[');
                if (bracket_start != std::string::npos)
                {
                    base_name = verilog_signal.substr(0, bracket_start);
                }

                auto it = signal_db.find(base_name);
                if (it != signal_db.end() && it->second != nullptr)
                {
                    bar_graphs[i].signal_name = base_name;
                    bar_graphs[i].value_ptr = it->second;
                    bar_graphs[i].connected = true;
                }
            }
        }

        for (int i = 0; i < 3; i++)
        {
            std::string dip_name = "DIP" + std::to_string(i);
            if (dip_map.find(dip_name) != dip_map.end())
            {
                std::string verilog_signal = dip_map[dip_name];
                std::string base_name = verilog_signal;
                size_t bracket_start = verilog_signal.find('[');
                if (bracket_start != std::string::npos)
                {
                    base_name = verilog_signal.substr(0, bracket_start);
                }

                auto it = signal_db.find(base_name);
                if (it != signal_db.end() && it->second != nullptr)
                {
                    dipswitches[i].signal_name = base_name;
                    dipswitches[i].value_ptr = it->second;
                    dipswitches[i].connected = true;
                }
            }
        }
    }

    void resolve()
    {
        for (auto &p : in_map)
        {
            auto it = signal_db.find(p.second);
            if (it != signal_db.end() && it->second != nullptr)
            {
                inputs[p.first] = it->second;
                if (p.first.substr(0, 3) == "BTN")
                {
                    int idx = std::stoi(p.first.substr(3));
                    if (idx >= 0 && idx < 8)
                    {
                        btn_connected[idx] = true;
                    }
                }
            }
        }

        for (auto &p : out_map)
        {
            auto it = signal_db.find(p.second);
            if (it != signal_db.end() && it->second != nullptr)
            {
                outputs[p.first] = it->second;
                if (p.first.substr(0, 3) == "LED")
                {
                    int idx = std::stoi(p.first.substr(3));
                    if (idx >= 0 && idx < 8)
                    {
                        led_connected[idx] = true;
                    }
                }
            }
        }
    }

    void apply()
    {
        for (auto &p : inputs)
        {
            if (p.first.substr(0, 3) == "BTN")
            {
                int idx = std::stoi(p.first.substr(3));
                if (idx >= 0 && idx < 8 && btn_connected[idx] && p.second)
                {
                    *p.second = fpga.btn[idx];
                }
            }
        }

        for (int i = 0; i < 3; i++)
        {
            if (dipswitches[i].connected && dipswitches[i].value_ptr)
            {
                *dipswitches[i].value_ptr = fpga.dip[i];
            }
        }

        if (clk_connected && clk_signal)
        {
            *clk_signal = fpga.clk;
        }
    }

    void read()
    {
        for (auto &p : outputs)
        {
            if (p.first.substr(0, 3) == "LED")
            {
                int idx = std::stoi(p.first.substr(3));
                if (idx >= 0 && idx < 8 && led_connected[idx] && p.second)
                {
                    fpga.led[idx] = *p.second;
                }
            }
        }

        for (int i = 0; i < 3; i++)
        {
            if (bar_graphs[i].connected && bar_graphs[i].value_ptr)
            {
                fpga.bar_values[i] = *bar_graphs[i].value_ptr;
                bar_graphs[i].current_value = *bar_graphs[i].value_ptr;
            }
            else
            {
                fpga.bar_values[i] = 0;
            }
        }
    }
};

SignalMap mapc;
#include "signal_checks.h"

// LOAD JSON
json load_json(const std::string &file)
{
    std::ifstream f(file);
    if (!f.is_open())
    {
        return json::object();
    }
    json j;
    f >> j;
    return j;
}

// SIM STEP
void step()
{
    mapc.apply();
    top->eval();
    mapc.read();
}

// TEXT RENDERING HELPERS
TTF_Font *font = nullptr;

void init_font()
{
    TTF_Init();
    font = TTF_OpenFont("./assets/DejaVuSans.ttf", 12);
    if (!font)
    {
        std::cerr << "Error: Failed to load font\n";
        return;
    }
    
}

void draw_text(SDL_Renderer *renderer, int x, int y, const std::string &text, SDL_Color color)
{
    if (!font)
        return;

    SDL_Surface *surface = TTF_RenderText_Blended(font, text.c_str(), color);
    if (!surface)
        return;

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_Rect dest = {x, y, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, NULL, &dest);

    SDL_FreeSurface(surface);
    SDL_DestroyTexture(texture);
}

// CLOCK BLINKER DRAW
void draw_clock_blinker(SDL_Renderer *r, int x, int y, bool state, bool connected)
{
    const int blinker_size = 6;

    if (!connected)
    {
        SDL_SetRenderDrawColor(r, 150, 150, 150, 255);
        SDL_Rect blinker = {x, y, blinker_size, blinker_size};
        SDL_RenderFillRect(r, &blinker);
        SDL_SetRenderDrawColor(r, 100, 100, 100, 255);
        SDL_RenderDrawRect(r, &blinker);
        return;
    }

    if (state)
    {
        SDL_SetRenderDrawColor(r, 0, 255, 0, 255);
    }
    else
    {
        SDL_SetRenderDrawColor(r, 255, 0, 0, 255);
    }

    SDL_Rect blinker = {x, y, blinker_size, blinker_size};
    SDL_RenderFillRect(r, &blinker);
    SDL_SetRenderDrawColor(r, 0, 150, 0, 255);
    SDL_RenderDrawRect(r, &blinker);
}

// DIPSWITCH DRAW
void draw_dipswitch(SDL_Renderer *r, int x, int y, uint8_t value, bool connected, const std::string &label)
{
    const int switch_width = 8;
    const int switch_height = 25;
    const int spacing = 12;
    const int num_switches = 8;
    const int inner_square_size = 6;

    draw_text(r, x - 35, y, label, (SDL_Color){60, 60, 80, 255});

    if (!connected)
    {
        for (int i = 0; i < num_switches; i++)
        {
            int switch_x = x + i * spacing;
            int switch_y = y;

            SDL_SetRenderDrawColor(r, 150, 150, 150, 255);
            SDL_Rect outer = {switch_x, switch_y, switch_width, switch_height};
            SDL_RenderFillRect(r, &outer);

            SDL_SetRenderDrawColor(r, 200, 200, 200, 255);
            SDL_Rect inner = {switch_x + (switch_width - inner_square_size) / 2,
                              switch_y + (switch_height - inner_square_size) / 2,
                              inner_square_size, inner_square_size};
            SDL_RenderFillRect(r, &inner);

            SDL_SetRenderDrawColor(r, 100, 100, 100, 255);
            SDL_RenderDrawRect(r, &outer);
        }
        draw_text(r, x + 130, y + 8, "DISABLED", (SDL_Color){150, 150, 150, 255});
        return;
    }

    for (int i = 0; i < num_switches; i++)
    {
        int bit_position = num_switches - 1 - i;
        bool is_on = (value >> bit_position) & 1;
        int switch_x = x + i * spacing;
        int switch_y = y;

        if (is_on)
        {
            SDL_SetRenderDrawColor(r, 0, 180, 0, 255);
        }
        else
        {
            SDL_SetRenderDrawColor(r, 150, 150, 150, 255);
        }
        SDL_Rect outer = {switch_x, switch_y, switch_width, switch_height};
        SDL_RenderFillRect(r, &outer);

        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);

        if (is_on)
        {
            SDL_Rect inner_up = {switch_x + (switch_width - inner_square_size) / 2,
                                 switch_y + 2,
                                 inner_square_size, inner_square_size};
            SDL_RenderFillRect(r, &inner_up);
        }
        else
        {
            SDL_Rect inner_down = {switch_x + (switch_width - inner_square_size) / 2,
                                   switch_y + switch_height - inner_square_size - 2,
                                   inner_square_size, inner_square_size};
            SDL_RenderFillRect(r, &inner_down);
        }

        SDL_SetRenderDrawColor(r, 80, 80, 80, 255);
        SDL_RenderDrawRect(r, &outer);
    }

    for (int i = 0; i < num_switches; i++)
    {
        int bit_num = num_switches - 1 - i;
        char bit_str[4];
        snprintf(bit_str, sizeof(bit_str), "%d", bit_num);
        draw_text(r, x + i * spacing + 2, y + switch_height + 2, bit_str, (SDL_Color){120, 120, 140, 255});
    }

    char value_str[32];
    snprintf(value_str, sizeof(value_str), "%d (0x%02X)", value, value);
    draw_text(r, x + 130, y + 8, value_str, (SDL_Color){100, 100, 120, 255});
}

// BAR GRAPH DRAW
void draw_bar_graph(SDL_Renderer *r, int x, int y, int value, bool connected, const std::string &label)
{
    const int num_leds = 8;
    const int led_width = 12;
    const int led_height = 25;
    const int spacing = 14;

    draw_text(r, x - 35, y + 8, label, (SDL_Color){60, 60, 80, 255});

    if (connected)
    {
        char value_str[32];
        snprintf(value_str, sizeof(value_str), "%d (0x%02X)", value, value);
        draw_text(r, x + 130, y + 8, value_str, (SDL_Color){100, 100, 120, 255});
    }

    if (!connected)
    {
        for (int i = 0; i < num_leds; i++)
        {
            int led_x = x + 5 + i * spacing;
            int led_y = y;

            SDL_SetRenderDrawColor(r, 220, 220, 220, 255);
            SDL_Rect led = {led_x, led_y, led_width, led_height};
            SDL_RenderFillRect(r, &led);

            SDL_SetRenderDrawColor(r, 150, 150, 150, 255);
            SDL_RenderDrawLine(r, led_x, led_y, led_x + led_width, led_y + led_height);
            SDL_RenderDrawLine(r, led_x + led_width, led_y, led_x, led_y + led_height);
            SDL_RenderDrawRect(r, &led);
        }

        draw_text(r, x + 130, y + 8, "DISABLED", (SDL_Color){150, 150, 150, 255});
        return;
    }

    for (int i = 0; i < num_leds; i++)
    {
        int bit_position = num_leds - 1 - i;
        bool is_lit = (value >> bit_position) & 1;
        int led_x = x + 5 + i * spacing;
        int led_y = y;

        if (is_lit)
        {
            for (int glow = 3; glow > 0; glow--)
            {
                SDL_SetRenderDrawColor(r, 0, 200, 0, 20);
                SDL_Rect glow_rect = {led_x - glow, led_y - glow, led_width + glow * 2, led_height + glow * 2};
                SDL_RenderFillRect(r, &glow_rect);
            }
            SDL_SetRenderDrawColor(r, 0, 200, 0, 255);
        }
        else
        {
            SDL_SetRenderDrawColor(r, 200, 200, 200, 255);
        }

        SDL_Rect led = {led_x, led_y, led_width, led_height};
        SDL_RenderFillRect(r, &led);
        SDL_SetRenderDrawColor(r, 100, 100, 100, 255);
        SDL_RenderDrawRect(r, &led);
    }

    for (int i = 0; i < num_leds; i++)
    {
        int bit_num = num_leds - 1 - i;
        char bit_str[4];
        snprintf(bit_str, sizeof(bit_str), "%d", bit_num);
        draw_text(r, x + i * spacing + 3, y + led_height + 2, bit_str, (SDL_Color){120, 120, 140, 255});
    }
}

// LED DRAW
void draw_led(SDL_Renderer *r, int x, int y, bool on, bool connected)
{
    const int led_width = 12;
    const int led_height = 25;

    if (!connected)
    {
        SDL_SetRenderDrawColor(r, 220, 220, 220, 255);
        SDL_Rect led = {x, y, led_width, led_height};
        SDL_RenderFillRect(r, &led);
        SDL_SetRenderDrawColor(r, 150, 150, 150, 255);
        SDL_RenderDrawLine(r, x, y, x + led_width, y + led_height);
        SDL_RenderDrawLine(r, x + led_width, y, x, y + led_height);
        SDL_RenderDrawRect(r, &led);
        return;
    }

    if (on)
    {
        for (int i = 3; i > 0; i--)
        {
            SDL_SetRenderDrawColor(r, 0, 200, 0, 15);
            SDL_Rect glow = {x - i, y - i, led_width + i * 2, led_height + i * 2};
            SDL_RenderFillRect(r, &glow);
        }
        SDL_SetRenderDrawColor(r, 0, 200, 0, 255);
    }
    else
    {
        SDL_SetRenderDrawColor(r, 200, 200, 200, 255);
    }

    SDL_Rect led = {x, y, led_width, led_height};
    SDL_RenderFillRect(r, &led);
    SDL_SetRenderDrawColor(r, 100, 100, 100, 255);
    SDL_RenderDrawRect(r, &led);
}

// BUTTON DRAW
void draw_button(SDL_Renderer *ren, int x, int y, int w, int h, bool pressed, bool connected, int btn_num)
{
    if (!connected)
    {
        SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);
    }
    else
    {
        SDL_SetRenderDrawColor(ren, pressed ? 180 : 100, pressed ? 180 : 100, pressed ? 180 : 100, 255);
    }

    SDL_Rect btn = {x, y, w, h};
    SDL_RenderFillRect(ren, &btn);
    SDL_SetRenderDrawColor(ren, 80, 80, 80, 255);
    SDL_RenderDrawRect(ren, &btn);

    if (!connected)
    {
        SDL_SetRenderDrawColor(ren, 120, 120, 120, 255);
        SDL_RenderDrawLine(ren, x + 5, y + 5, x + w - 5, y + h - 5);
        SDL_RenderDrawLine(ren, x + w - 5, y + 5, x + 5, y + h - 5);
    }
}

// DRAW SECTION HEADER
void draw_section_header(SDL_Renderer *r, int x, int y, const std::string &title)
{
    draw_text(r, x - 35, y - 15, title, (SDL_Color){80, 80, 100, 255});
}

// MAIN
int main(int argc, char **argv)
{
    Verilated::commandArgs(argc, argv);
    top = new Vtop;
    mapc.top = top;

    json cfg = load_json("constraints.json");
    mapc.load_and_build(cfg);
    mapc.resolve();

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    init_font();

    SDL_Window *win = SDL_CreateWindow(
        "Virtual FPGA Simulator",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        950, 550,
        SDL_WINDOW_RESIZABLE);

    if (!win)
    {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren)
    {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    bool running = true;
    int spacing = 45;
    int btn_size = 30;
    int start_x = 120;
    int dip_start_x = 650;

    auto last_clock_time = std::chrono::steady_clock::now();
    auto last_render_time = std::chrono::steady_clock::now();
    const int clock_interval_us = 500;
    const int render_interval_ms = 16;

    auto last_perf_time = std::chrono::steady_clock::now();
    long long total_clock_cycles = 0;
    int frame_count = 0;
    auto last_step_time = std::chrono::steady_clock::now();

    while (running)
    {
        auto now = std::chrono::steady_clock::now();

        if (mapc.clk_connected)
        {
            auto clock_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - last_clock_time).count();
            int cycles_to_run = clock_elapsed / clock_interval_us;
            if (cycles_to_run > 0)
            {
                for (int i = 0; i < cycles_to_run; i++)
                {
                    fpga.clk = !fpga.clk;
                    step();
                    total_clock_cycles++;
                }
                last_clock_time += std::chrono::microseconds(cycles_to_run * clock_interval_us);
            }
        }
        else
        {
            auto step_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_step_time).count();
            if (step_elapsed >= render_interval_ms)
            {
                mapc.apply();
                top->eval();
                mapc.read();
                last_step_time = now;
            }
        }

        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT)
                running = false;

            if (e.type == SDL_MOUSEBUTTONDOWN)
            {
                int mx = e.button.x;
                int my = e.button.y;

                for (int i = 0; i < 8; i++)
                {
                    if (!mapc.btn_connected[i])
                        continue;
                    SDL_Rect b = {start_x + i * spacing, 80, btn_size, btn_size};
                    if (mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h)
                    {
                        fpga.btn[i] = !fpga.btn[i];
                        if (!mapc.clk_connected)
                        {
                            mapc.apply();
                            top->eval();
                            mapc.read();
                        }
                    }
                }

                const int switch_width = 8;
                const int switch_height = 25;
                const int switch_spacing = 12;
                int dip_y_start = 80;
                int dip_y_spacing = 55;

                for (int dip_idx = 0; dip_idx < 3; dip_idx++)
                {
                    if (!mapc.dipswitches[dip_idx].connected)
                        continue;

                    int dip_y = dip_y_start + dip_idx * dip_y_spacing;

                    for (int bit = 0; bit < 8; bit++)
                    {
                        int switch_x = dip_start_x + bit * switch_spacing;
                        int switch_y = dip_y;
                        SDL_Rect sw_rect = {switch_x, switch_y, switch_width, switch_height};

                        if (mx >= sw_rect.x && mx <= sw_rect.x + sw_rect.w &&
                            my >= sw_rect.y && my <= sw_rect.y + sw_rect.h)
                        {
                            int bit_pos = 7 - bit;
                            fpga.dip[dip_idx] ^= (1 << bit_pos);
                            if (!mapc.clk_connected)
                            {
                                mapc.apply();
                                top->eval();
                                mapc.read();
                            }
                            break;
                        }
                    }
                }
            }
        }

        auto render_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_render_time).count();
        if (render_elapsed >= render_interval_ms)
        {
            SDL_SetRenderDrawColor(ren, 245, 245, 245, 255);
            SDL_RenderClear(ren);

            SDL_SetRenderDrawColor(ren, 80, 80, 100, 255);
            SDL_Rect title_bar = {0, 0, 950, 35};
            SDL_RenderFillRect(ren, &title_bar);

            draw_text(ren, 10, 10, "FPGA Simulator", (SDL_Color){245, 245, 245, 255});

            draw_clock_blinker(ren, 720, 17, fpga.clk, mapc.clk_connected);

            std::string clock_text;
            if (mapc.clk_connected)
            {
                clock_text = "Clock: 1 kHz";
            }
            else
            {
                clock_text = "Clock: 1 kHz (Disabled)";
            }
            draw_text(ren, 730, 12, clock_text, (SDL_Color){245, 245, 245, 255});

            draw_section_header(ren, start_x, 65, "BUTTONS");
            for (int i = 0; i < 8; i++)
            {
                draw_button(ren, start_x + i * spacing, 80, btn_size, btn_size,
                            fpga.btn[i], mapc.btn_connected[i], i);

                char num_text[4];
                snprintf(num_text, sizeof(num_text), "%d", i);
                draw_text(ren, start_x + i * spacing + 11, 115, num_text, (SDL_Color){50, 50, 70, 255});
            }

            draw_section_header(ren, start_x, 145, "LEDS");
            for (int i = 0; i < 8; i++)
                draw_led(ren, start_x + i * spacing, 160, fpga.led[i], mapc.led_connected[i]);

            draw_section_header(ren, start_x, 225, "BAR GRAPHS");
            int bar_y = 245;
            int bar_spacing = 55;
            const char *bar_names[3] = {"BAR0", "BAR1", "BAR2"};
            for (int i = 0; i < 3; i++)
            {
                draw_bar_graph(ren, start_x, bar_y + i * bar_spacing, fpga.bar_values[i],
                               mapc.bar_graphs[i].connected, bar_names[i]);
            }

            draw_section_header(ren, dip_start_x, 65, "DIPSWITCHES");
            int dip_y = 80;
            int dip_spacing = 55;
            const char *dip_names[3] = {"DIP0", "DIP1", "DIP2"};
            for (int i = 0; i < 3; i++)
            {
                draw_dipswitch(ren, dip_start_x, dip_y + i * dip_spacing, fpga.dip[i],
                               mapc.dipswitches[i].connected, dip_names[i]);
            }

            char perf_text[128];
            if (mapc.clk_connected)
            {
                snprintf(perf_text, sizeof(perf_text), "Clock Cycles: %lld", total_clock_cycles);
            }
            else
            {
                snprintf(perf_text, sizeof(perf_text), "Clock Disabled - IDLE");
            }
            draw_text(ren, 10, 520, perf_text, (SDL_Color){100, 100, 120, 255});

            SDL_RenderPresent(ren);
            last_render_time = now;
            frame_count++;
        }

        SDL_Delay(1);
    }

    if (font)
        TTF_CloseFont(font);
    TTF_Quit();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    delete top;

    return 0;
}