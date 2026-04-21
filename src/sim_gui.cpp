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
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <dirent.h>
#include <sys/wait.h>
#include <unistd.h>
#include <thread>
#include <atomic>

using json = nlohmann::json;

// Verilator
Vtop *top;

// Global simulation control
bool simulation_running = false;
long long total_clock_cycles = 0;

// File dialog control
std::atomic<bool> file_dialog_open(false);
std::atomic<bool> file_selected(false);
std::string pending_filename;

// Memory Manager Class
class MemoryManager
{
private:
    std::vector<uint8_t> memory;
    size_t size;
    bool enabled;

public:
    MemoryManager(size_t mem_size = 4 * 1024 * 1024) : size(mem_size), enabled(false)
    {
        memory.resize(size, 0);
    }

    uint8_t read(uint32_t address)
    {
        if (!enabled || address >= size)
            return 0;
        return memory[address];
    }

    void write(uint32_t address, uint8_t value)
    {
        if (!enabled || address >= size)
            return;
        memory[address] = value;
    }

    uint8_t *getData() { return memory.data(); }
    size_t getSize() const { return size; }
    void setEnabled(bool e) { enabled = e; }
    bool isEnabled() const { return enabled; }
    void clear() { std::fill(memory.begin(), memory.end(), 0); }

    bool loadHexFile(const std::string &filename)
    {
        std::ifstream file(filename);
        if (!file.is_open())
        {
            std::cerr << "Failed to open hex file: " << filename << std::endl;
            return false;
        }

        std::string line;
        uint32_t current_address = 0;
        int bytes_loaded = 0;

        while (std::getline(file, line))
        {
            size_t comment_pos = line.find(';');
            if (comment_pos != std::string::npos)
            {
                line = line.substr(0, comment_pos);
            }
            line.erase(remove_if(line.begin(), line.end(), ::isspace), line.end());

            if (line.empty())
                continue;

            if (line[0] == '@')
            {
                current_address = std::stoul(line.substr(1), nullptr, 16);
            }
            else
            {
                for (size_t i = 0; i < line.length(); i += 2)
                {
                    if (i + 1 < line.length())
                    {
                        uint8_t byte = std::stoul(line.substr(i, 2), nullptr, 16);
                        if (current_address < size)
                        {
                            memory[current_address++] = byte;
                            bytes_loaded++;
                        }
                    }
                }
            }
        }

        file.close();
        std::cout << "Loaded hex file: " << filename << " (" << bytes_loaded << " bytes)" << std::endl;
        return true;
    }
};

// Async file dialog
void openFileDialogAsync()
{
    file_dialog_open = true;
    std::string filename;

    // Use zenity for file dialog
    FILE *pipe = popen("zenity --file-selection --title='Select Hex File' --file-filter='Hex files | *.hex' --file-filter='Text files | *.txt' 2>/dev/null", "r");

    if (pipe)
    {
        char buffer[1024];
        if (fgets(buffer, sizeof(buffer), pipe) != NULL)
        {
            filename = buffer;
            filename.erase(std::remove(filename.begin(), filename.end(), '\n'), filename.end());
            filename.erase(std::remove(filename.begin(), filename.end(), '\r'), filename.end());
        }
        pclose(pipe);
    }

    if (!filename.empty())
    {
        pending_filename = filename;
        file_selected = true;
    }

    file_dialog_open = false;
}

union SignalPtr
{
    CData *cdata;
    IData *idata;
    void *ptr;

    SignalPtr() : ptr(nullptr) {}
    SignalPtr(CData *p) : cdata(p) {}
    SignalPtr(IData *p) : idata(p) {}
};

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
void add_available_signals(std::unordered_map<std::string, SignalPtr> &signal_db, Vtop *top);

// Memory Display Configuration
struct MemoryDisplay
{
    static constexpr int CELL_SIZE = 30;
    static constexpr int CELLS_PER_ROW = 4;
    static constexpr int ROWS_DISPLAYED = 4;

    int scroll_offset = 0;
    int selected_cell = -1;
    bool show_memory = true;
    SDL_Rect memory_rect;

    int getAddressFromPosition(int x, int y)
    {
        if (!show_memory)
            return -1;

        int rel_x = x - memory_rect.x;
        int rel_y = y - memory_rect.y;

        if (rel_x < 0 || rel_y < 0)
            return -1;

        int col = rel_x / CELL_SIZE;
        int row = rel_y / CELL_SIZE;

        if (col >= 0 && col < CELLS_PER_ROW &&
            row >= 0 && row < ROWS_DISPLAYED)
        {
            return (scroll_offset + row) * CELLS_PER_ROW + col;
        }

        return -1;
    }
};

// SIGNAL MAP
struct SignalMap
{
    Vtop *top;

    std::unordered_map<std::string, SignalPtr> inputs;
    std::unordered_map<std::string, SignalPtr> outputs;

    struct BarGraphConnection
    {
        std::string name;
        std::string signal_name;
        SignalPtr value_ptr;
        bool connected;
        int current_value;
        int width;
    };
    BarGraphConnection bar_graphs[3];

    struct DipswitchConnection
    {
        std::string name;
        std::string signal_name;
        SignalPtr value_ptr;
        bool connected;
        uint8_t current_value;
    };
    DipswitchConnection dipswitches[3];

    struct MemoryConnection
    {
        std::string addr_signal_name;
        std::string data_signal_name;
        std::string we_signal_name;
        std::string re_signal_name;
        SignalPtr addr_ptr;
        SignalPtr data_ptr;
        SignalPtr we_ptr;
        SignalPtr re_ptr;
        bool connected = false;
        int addr_width = 22;
        int data_width = 8;
    };
    MemoryConnection memory_conn;

    std::unordered_map<std::string, std::string> in_map;
    std::unordered_map<std::string, std::string> out_map;
    std::unordered_map<std::string, std::string> bar_map;
    std::unordered_map<std::string, std::string> dip_map;
    std::unordered_map<std::string, SignalPtr> signal_db;

    bool btn_connected[8] = {false};
    bool led_connected[8] = {false};
    bool clk_connected = false;
    SignalPtr clk_signal;

    void load_and_build(const json &j)
    {
        bar_graphs[0] = {"BAR0", "", SignalPtr(), false, 0, 8};
        bar_graphs[1] = {"BAR1", "", SignalPtr(), false, 0, 8};
        bar_graphs[2] = {"BAR2", "", SignalPtr(), false, 0, 8};

        dipswitches[0] = {"DIP0", "", SignalPtr(), false, 0};
        dipswitches[1] = {"DIP1", "", SignalPtr(), false, 0};
        dipswitches[2] = {"DIP2", "", SignalPtr(), false, 0};

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

        if (j.contains("memory"))
        {
            auto &mem = j["memory"];
            if (mem.contains("address_bus"))
                memory_conn.addr_signal_name = mem["address_bus"].get<std::string>();
            if (mem.contains("data_bus"))
                memory_conn.data_signal_name = mem["data_bus"].get<std::string>();
            if (mem.contains("write_enable"))
                memory_conn.we_signal_name = mem["write_enable"].get<std::string>();
            if (mem.contains("read_enable"))
                memory_conn.re_signal_name = mem["read_enable"].get<std::string>();
            if (mem.contains("addr_width"))
                memory_conn.addr_width = mem["addr_width"].get<int>();
        }

        add_available_signals(signal_db, top);

        if (j.contains("clock") && j["clock"].is_string())
        {
            std::string clk_signal_name = j["clock"].get<std::string>();
            auto it = signal_db.find(clk_signal_name);
            if (it != signal_db.end() && it->second.ptr != nullptr)
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
                if (it != signal_db.end() && it->second.ptr != nullptr)
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
                if (it != signal_db.end() && it->second.ptr != nullptr)
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
            if (it != signal_db.end() && it->second.ptr != nullptr)
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
            if (it != signal_db.end() && it->second.ptr != nullptr)
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

        if (!memory_conn.addr_signal_name.empty())
        {
            auto it = signal_db.find(memory_conn.addr_signal_name);
            if (it != signal_db.end() && it->second.ptr != nullptr)
            {
                memory_conn.addr_ptr = it->second;
                memory_conn.connected = true;
                std::cout << "  ✓ Memory address bus connected: " << memory_conn.addr_signal_name << std::endl;
            }
            else
            {
                std::cout << "  ✗ Memory address bus not found: " << memory_conn.addr_signal_name << std::endl;
            }
        }

        if (!memory_conn.data_signal_name.empty())
        {
            auto it = signal_db.find(memory_conn.data_signal_name);
            if (it != signal_db.end() && it->second.ptr != nullptr)
            {
                memory_conn.data_ptr = it->second;
                std::cout << "  ✓ Memory data bus connected: " << memory_conn.data_signal_name << std::endl;
            }
            else
            {
                std::cout << "  ✗ Memory data bus not found: " << memory_conn.data_signal_name << std::endl;
            }
        }

        if (!memory_conn.we_signal_name.empty())
        {
            auto it = signal_db.find(memory_conn.we_signal_name);
            if (it != signal_db.end() && it->second.ptr != nullptr)
            {
                memory_conn.we_ptr = it->second;
                std::cout << "  ✓ Memory write enable connected: " << memory_conn.we_signal_name << std::endl;
            }
        }

        if (!memory_conn.re_signal_name.empty())
        {
            auto it = signal_db.find(memory_conn.re_signal_name);
            if (it != signal_db.end() && it->second.ptr != nullptr)
            {
                memory_conn.re_ptr = it->second;
                std::cout << "  ✓ Memory read enable connected: " << memory_conn.re_signal_name << std::endl;
            }
        }
    }

    void apply(MemoryManager &memory)
    {
        if (simulation_running)
        {
            for (auto &p : inputs)
            {
                if (p.first.substr(0, 3) == "BTN")
                {
                    int idx = std::stoi(p.first.substr(3));
                    if (idx >= 0 && idx < 8 && btn_connected[idx] && p.second.cdata)
                    {
                        *p.second.cdata = fpga.btn[idx];
                    }
                }
            }

            for (int i = 0; i < 3; i++)
            {
                if (dipswitches[i].connected && dipswitches[i].value_ptr.cdata)
                {
                    *dipswitches[i].value_ptr.cdata = fpga.dip[i];
                }
            }

            if (clk_connected && clk_signal.cdata)
            {
                *clk_signal.cdata = fpga.clk;
            }

            if (memory_conn.connected && memory_conn.addr_ptr.ptr && memory_conn.data_ptr.ptr && memory.isEnabled())
            {
                uint32_t address = 0;
                if (memory_conn.addr_ptr.idata)
                {
                    address = *memory_conn.addr_ptr.idata;
                }
                else if (memory_conn.addr_ptr.cdata)
                {
                    address = *memory_conn.addr_ptr.cdata;
                }

                if (address < memory.getSize())
                {
                    bool we = false;
                    bool re = false;

                    if (memory_conn.we_ptr.cdata)
                    {
                        we = (*memory_conn.we_ptr.cdata != 0);
                    }
                    if (memory_conn.re_ptr.cdata)
                    {
                        re = (*memory_conn.re_ptr.cdata != 0);
                    }

                    if (we && memory_conn.data_ptr.cdata)
                    {
                        uint8_t data = *memory_conn.data_ptr.cdata;
                        memory.write(address, data);
                    }
                    else if (re && memory_conn.data_ptr.cdata)
                    {
                        uint8_t data = memory.read(address);
                        *memory_conn.data_ptr.cdata = data;
                    }
                }
            }
        }
    }

    void read()
    {
        for (auto &p : outputs)
        {
            if (p.first.substr(0, 3) == "LED")
            {
                int idx = std::stoi(p.first.substr(3));
                if (idx >= 0 && idx < 8 && led_connected[idx] && p.second.cdata)
                {
                    fpga.led[idx] = *p.second.cdata;
                }
            }
        }

        for (int i = 0; i < 3; i++)
        {
            if (bar_graphs[i].connected && bar_graphs[i].value_ptr.cdata)
            {
                fpga.bar_values[i] = *bar_graphs[i].value_ptr.cdata;
                bar_graphs[i].current_value = *bar_graphs[i].value_ptr.cdata;
            }
            else
            {
                fpga.bar_values[i] = 0;
            }
        }
    }

    void reset_fpga_state()
    {
        for (int i = 0; i < 8; i++)
        {
            fpga.btn[i] = 0;
            fpga.led[i] = 0;
        }
        for (int i = 0; i < 3; i++)
        {
            fpga.bar_values[i] = 0;
            fpga.dip[i] = 0;
        }
        fpga.clk = false;
    }
};

SignalMap mapc;
MemoryManager memory(4 * 1024 * 1024);
MemoryDisplay mem_display;

#include "signal_checks.h"

// LOAD JSON
json load_json(const std::string &file)
{
    std::ifstream f(file);
    if (!f.is_open())
    {
        std::cerr << "Warning: Could not open " << file << std::endl;
        return json::object();
    }
    json j;
    f >> j;
    return j;
}

// SIM STEP
void step()
{
    if (simulation_running)
    {
        mapc.apply(memory);
        top->eval();
        mapc.read();
    }
}

// Reset simulation by recreating the Verilator instance
void reset_simulation()
{
    // Save memory contents
    std::vector<uint8_t> saved_memory;
    if (memory.isEnabled())
    {
        saved_memory.resize(memory.getSize());
        memcpy(saved_memory.data(), memory.getData(), memory.getSize());
        std::cout << "Saving memory contents (" << saved_memory.size() << " bytes)..." << std::endl;
    }

    // Delete and recreate the Verilator instance
    delete top;
    top = new Vtop;
    mapc.top = top;

    // Rebuild signal connections
    json cfg = load_json("constraints.json");
    mapc.load_and_build(cfg);
    mapc.resolve();

    // Restore memory contents
    if (memory.isEnabled() && !saved_memory.empty())
    {
        memcpy(memory.getData(), saved_memory.data(), saved_memory.size());
        std::cout << "Memory restored (" << saved_memory.size() << " bytes)" << std::endl;
    }

    // Reset FPGA state (buttons, LEDs, dipswitches, etc.)
    mapc.reset_fpga_state();

    // Reset clock cycle counter
    total_clock_cycles = 0;

    // Run one evaluation to initialize the new instance
    top->eval();
    mapc.read();

    std::cout << "Simulation reset complete - Verilator instance recreated, memory preserved" << std::endl;
}

// TEXT RENDERING HELPERS
TTF_Font *font = nullptr;

void init_font()
{
    TTF_Init();
    font = TTF_OpenFont("./assets/DejaVuSans.ttf", 12);
    if (!font)
    {
        std::cerr << "Warning: Failed to load font, using fallback rendering\n";
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

// RUN/STOP BUTTONS DRAW
void draw_run_button(SDL_Renderer *renderer, int x, int y, bool hover, bool running)
{
    int width = 80;
    int height = 30;

    if (running)
    {
        SDL_SetRenderDrawColor(renderer, hover ? 200 : 180, hover ? 80 : 60, hover ? 80 : 60, 255);
    }
    else
    {
        SDL_SetRenderDrawColor(renderer, hover ? 80 : 60, hover ? 200 : 180, hover ? 80 : 60, 255);
    }

    SDL_Rect button = {x, y, width, height};
    SDL_RenderFillRect(renderer, &button);

    SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
    SDL_RenderDrawRect(renderer, &button);

    std::string text = running ? "STOP" : "RUN";
    draw_text(renderer, x + 25, y + 8, text, (SDL_Color){255, 255, 255, 255});
}

void draw_reset_button(SDL_Renderer *renderer, int x, int y, bool hover)
{
    int width = 80;
    int height = 30;

    SDL_SetRenderDrawColor(renderer, hover ? 200 : 180, hover ? 200 : 180, hover ? 80 : 60, 255);

    SDL_Rect button = {x, y, width, height};
    SDL_RenderFillRect(renderer, &button);

    SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
    SDL_RenderDrawRect(renderer, &button);

    draw_text(renderer, x + 15, y + 8, "RESET", (SDL_Color){255, 255, 255, 255});
}

// DIPSWITCH DRAW
void draw_dipswitch(SDL_Renderer *r, int x, int y, uint8_t value, bool connected, const std::string &label, bool enabled)
{
    const int switch_width = 8;
    const int switch_height = 25;
    const int spacing = 12;
    const int num_switches = 8;
    const int inner_square_size = 6;

    draw_text(r, x - 35, y, label, enabled ? (SDL_Color){60, 60, 80, 255} : (SDL_Color){150, 150, 150, 255});

    if (!connected || !enabled)
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
void draw_button(SDL_Renderer *ren, int x, int y, int w, int h, bool pressed, bool connected, int btn_num, bool enabled)
{
    if (!connected || !enabled)
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

    if (!connected || !enabled)
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

// MEMORY GRID DRAW
void draw_memory_grid(SDL_Renderer *renderer, MemoryManager &memory, MemoryDisplay &display)
{
    if (!display.show_memory)
        return;

    int start_x = 650;
    int start_y = 300;
    int header_height = 35;

    SDL_SetRenderDrawColor(renderer, 245, 245, 245, 255);
    SDL_Rect bg = {start_x - 15, start_y - header_height - 10,
                   MemoryDisplay::CELLS_PER_ROW * MemoryDisplay::CELL_SIZE + 80,
                   MemoryDisplay::ROWS_DISPLAYED * MemoryDisplay::CELL_SIZE + header_height + 20};
    SDL_RenderFillRect(renderer, &bg);

    std::string title = memory.isEnabled() ? "Memory (4MB) - Connected" : "Memory (4MB) - Disabled";
    SDL_Color title_color = memory.isEnabled() ? (SDL_Color){50, 50, 70, 255} : (SDL_Color){150, 150, 150, 255};
    draw_text(renderer, start_x, start_y - header_height, title, title_color);

    for (int col = 0; col < MemoryDisplay::CELLS_PER_ROW; col++)
    {
        char header[8];
        snprintf(header, sizeof(header), "[%d]", col);
        draw_text(renderer, start_x + col * MemoryDisplay::CELL_SIZE + MemoryDisplay::CELL_SIZE / 2 - 10,
                  start_y - 20, header, (SDL_Color){80, 80, 100, 255});
    }

    for (int row = 0; row < MemoryDisplay::ROWS_DISPLAYED; row++)
    {
        int address_base = (display.scroll_offset + row) * MemoryDisplay::CELLS_PER_ROW;

        char row_header[16];
        snprintf(row_header, sizeof(row_header), " [%d]", row);
        draw_text(renderer, start_x - 35, start_y + row * MemoryDisplay::CELL_SIZE + MemoryDisplay::CELL_SIZE / 2 - 8,
                  row_header, (SDL_Color){80, 80, 100, 255});

        char full_addr[32];
        snprintf(full_addr, sizeof(full_addr), "0x%04X", address_base);
        draw_text(renderer, start_x - 80, start_y + row * MemoryDisplay::CELL_SIZE + MemoryDisplay::CELL_SIZE / 2 - 8,
                  full_addr, (SDL_Color){100, 100, 120, 255});

        for (int col = 0; col < MemoryDisplay::CELLS_PER_ROW; col++)
        {
            int address = address_base + col;
            int x = start_x + col * MemoryDisplay::CELL_SIZE;
            int y = start_y + row * MemoryDisplay::CELL_SIZE;

            bool selected = (display.selected_cell == address);
            if (selected && memory.isEnabled())
            {
                SDL_SetRenderDrawColor(renderer, 200, 230, 200, 255);
            }
            else
            {
                SDL_SetRenderDrawColor(renderer, 245, 245, 245, 255);
            }

            SDL_Rect cell = {x, y, MemoryDisplay::CELL_SIZE, MemoryDisplay::CELL_SIZE};
            SDL_RenderFillRect(renderer, &cell);

            SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
            SDL_RenderDrawRect(renderer, &cell);

            if (address < memory.getSize() && memory.isEnabled())
            {
                uint8_t value = memory.read(address);
                char hex_str[4];
                snprintf(hex_str, sizeof(hex_str), "%02X", value);
                draw_text(renderer, x + MemoryDisplay::CELL_SIZE / 2 - 10,
                          y + MemoryDisplay::CELL_SIZE / 2 - 8,
                          hex_str, (SDL_Color){0, 0, 0, 255});
            }
            else if (!memory.isEnabled())
            {
                draw_text(renderer, x + MemoryDisplay::CELL_SIZE / 2 - 10,
                          y + MemoryDisplay::CELL_SIZE / 2 - 8,
                          "--", (SDL_Color){150, 150, 150, 255});
            }
        }
    }

    display.memory_rect = {start_x, start_y,
                           MemoryDisplay::CELLS_PER_ROW * MemoryDisplay::CELL_SIZE,
                           MemoryDisplay::ROWS_DISPLAYED * MemoryDisplay::CELL_SIZE};

    int max_scroll = (memory.getSize() / MemoryDisplay::CELLS_PER_ROW) - MemoryDisplay::ROWS_DISPLAYED;
    if (max_scroll > 0)
    {
        char nav_text[128];
        snprintf(nav_text, sizeof(nav_text), "Page %d/%d (Scroll wheel)",
                 display.scroll_offset / MemoryDisplay::ROWS_DISPLAYED + 1,
                 max_scroll / MemoryDisplay::ROWS_DISPLAYED + 1);
        draw_text(renderer, start_x, start_y + MemoryDisplay::ROWS_DISPLAYED * MemoryDisplay::CELL_SIZE + 10,
                  nav_text, (SDL_Color){100, 100, 120, 255});
    }
}

void draw_load_button(SDL_Renderer *renderer, int x, int y, bool hover, bool enabled, bool dialog_open)
{
    int width = 120;
    int height = 30;

    if (dialog_open)
    {
        SDL_SetRenderDrawColor(renderer, 100, 100, 150, 255);
    }
    else if (!enabled)
    {
        SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
    }
    else if (hover && enabled)
    {
        SDL_SetRenderDrawColor(renderer, 100, 150, 100, 255);
    }
    else
    {
        SDL_SetRenderDrawColor(renderer, 80, 120, 80, 255);
    }

    SDL_Rect button = {x, y, width, height};
    SDL_RenderFillRect(renderer, &button);

    SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
    SDL_RenderDrawRect(renderer, &button);

    SDL_Color text_color = enabled ? (SDL_Color){255, 255, 255, 255} : (SDL_Color){200, 200, 200, 255};
    std::string button_text = dialog_open ? "Loading..." : "Load Memory";
    draw_text(renderer, x + 15, y + 8, button_text, text_color);
}

void draw_clear_button(SDL_Renderer *renderer, int x, int y, bool hover, bool enabled)
{
    int width = 100;
    int height = 30;

    if (!enabled)
    {
        SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
    }
    else if (hover && enabled)
    {
        SDL_SetRenderDrawColor(renderer, 200, 100, 100, 255);
    }
    else
    {
        SDL_SetRenderDrawColor(renderer, 180, 80, 80, 255);
    }

    SDL_Rect button = {x, y, width, height};
    SDL_RenderFillRect(renderer, &button);

    SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
    SDL_RenderDrawRect(renderer, &button);

    SDL_Color text_color = enabled ? (SDL_Color){255, 255, 255, 255} : (SDL_Color){200, 200, 200, 255};
    draw_text(renderer, x + 15, y + 8, "Clear", text_color);
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

    if (mapc.memory_conn.connected)
    {
        memory.setEnabled(true);
        std::cout << "Memory enabled (connected to FPGA)" << std::endl;
    }
    else
    {
        std::cout << "Memory disabled (not connected in constraints)" << std::endl;
    }

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
        1100, 700,
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

    auto last_step_time = std::chrono::steady_clock::now();

    bool load_btn_hover = false;
    bool clear_btn_hover = false;
    bool run_btn_hover = false;
    bool reset_btn_hover = false;

    SDL_Rect load_btn_rect = {650, 530, 120, 30};
    SDL_Rect clear_btn_rect = {780, 530, 100, 30};
    SDL_Rect run_btn_rect = {110, 2, 80, 25};
    SDL_Rect reset_btn_rect = {200, 2, 80, 25};

    while (running)
    {
        auto now = std::chrono::steady_clock::now();

        if (mapc.clk_connected && simulation_running)
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
        else if (!simulation_running)
        {
            auto step_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_step_time).count();
            if (step_elapsed >= render_interval_ms)
            {
                mapc.apply(memory);
                top->eval();
                mapc.read();
                last_step_time = now;
            }
        }
        else
        {
            auto step_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_step_time).count();
            if (step_elapsed >= render_interval_ms)
            {
                mapc.apply(memory);
                top->eval();
                mapc.read();
                last_step_time = now;
            }
        }

        // Check if file was selected in the background thread
        if (file_selected)
        {
            memory.loadHexFile(pending_filename);
            file_selected = false;
            pending_filename.clear();
        }

        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT)
                running = false;

            if (e.type == SDL_MOUSEMOTION)
            {
                int mx = e.motion.x;
                int my = e.motion.y;

                load_btn_hover = (mx >= load_btn_rect.x && mx <= load_btn_rect.x + load_btn_rect.w &&
                                  my >= load_btn_rect.y && my <= load_btn_rect.y + load_btn_rect.h) &&
                                 !simulation_running && memory.isEnabled() && !file_dialog_open;

                clear_btn_hover = (mx >= clear_btn_rect.x && mx <= clear_btn_rect.x + clear_btn_rect.w &&
                                   my >= clear_btn_rect.y && my <= clear_btn_rect.y + clear_btn_rect.h) &&
                                  !simulation_running && memory.isEnabled();

                run_btn_hover = (mx >= run_btn_rect.x && mx <= run_btn_rect.x + run_btn_rect.w &&
                                 my >= run_btn_rect.y && my <= run_btn_rect.y + run_btn_rect.h);

                reset_btn_hover = (mx >= reset_btn_rect.x && mx <= reset_btn_rect.x + reset_btn_rect.w &&
                                   my >= reset_btn_rect.y && my <= reset_btn_rect.y + reset_btn_rect.h);
            }

            if (e.type == SDL_MOUSEBUTTONDOWN)
            {
                int mx = e.button.x;
                int my = e.button.y;

                if (!simulation_running && memory.isEnabled())
                {
                    int address = mem_display.getAddressFromPosition(mx, my);
                    if (address >= 0 && address < memory.getSize())
                    {
                        mem_display.selected_cell = address;
                    }
                }

                if (mx >= run_btn_rect.x && mx <= run_btn_rect.x + run_btn_rect.w &&
                    my >= run_btn_rect.y && my <= run_btn_rect.y + run_btn_rect.h)
                {
                    simulation_running = !simulation_running;
                    if (simulation_running)
                    {
                        std::cout << "Simulation started" << std::endl;
                        last_clock_time = std::chrono::steady_clock::now();
                    }
                    else
                    {
                        std::cout << "Simulation stopped" << std::endl;
                    }
                }

                if (mx >= reset_btn_rect.x && mx <= reset_btn_rect.x + reset_btn_rect.w &&
                    my >= reset_btn_rect.y && my <= reset_btn_rect.y + reset_btn_rect.h)
                {
                    reset_simulation();
                }

                if (mx >= load_btn_rect.x && mx <= load_btn_rect.x + load_btn_rect.w &&
                    my >= load_btn_rect.y && my <= load_btn_rect.y + load_btn_rect.h &&
                    !simulation_running && memory.isEnabled() && !file_dialog_open)
                {
                    // Start file dialog in a separate thread (non-blocking)
                    std::thread file_thread(openFileDialogAsync);
                    file_thread.detach();
                }

                if (mx >= clear_btn_rect.x && mx <= clear_btn_rect.x + clear_btn_rect.w &&
                    my >= clear_btn_rect.y && my <= clear_btn_rect.y + clear_btn_rect.h &&
                    !simulation_running && memory.isEnabled())
                {
                    memory.clear();
                    std::cout << "Memory cleared" << std::endl;
                }

                if (simulation_running)
                {
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
                                mapc.apply(memory);
                                top->eval();
                                mapc.read();
                            }
                        }
                    }
                }

                if (simulation_running)
                {
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
                                    mapc.apply(memory);
                                    top->eval();
                                    mapc.read();
                                }
                                break;
                            }
                        }
                    }
                }
            }

            if (e.type == SDL_MOUSEWHEEL && !simulation_running && memory.isEnabled())
            {
                if (mem_display.show_memory)
                {
                    int max_scroll = (memory.getSize() / MemoryDisplay::CELLS_PER_ROW) - MemoryDisplay::ROWS_DISPLAYED;
                    if (max_scroll > 0)
                    {
                        mem_display.scroll_offset -= e.wheel.y * 4;
                        if (mem_display.scroll_offset < 0)
                            mem_display.scroll_offset = 0;
                        if (mem_display.scroll_offset > max_scroll)
                            mem_display.scroll_offset = max_scroll;
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
            SDL_Rect title_bar = {0, 0, 1100, 35};
            SDL_RenderFillRect(ren, &title_bar);

            draw_text(ren, 10, 10, "FPGA Simulator", (SDL_Color){245, 245, 245, 255});

            draw_run_button(ren, run_btn_rect.x, run_btn_rect.y, run_btn_hover, simulation_running);
            draw_reset_button(ren, reset_btn_rect.x, reset_btn_rect.y, reset_btn_hover);

            draw_clock_blinker(ren, 720, 17, fpga.clk, mapc.clk_connected);

            std::string clock_text;
            if (mapc.clk_connected)
            {
                clock_text = simulation_running ? "Clock: 1 kHz (Running)" : "Clock: 1 kHz (Stopped)";
            }
            else
            {
                clock_text = "Clock: Disabled";
            }
            draw_text(ren, 730, 12, clock_text, (SDL_Color){245, 245, 245, 255});

            std::string status_text = simulation_running ? "● RUNNING" : "■ STOPPED";
            SDL_Color status_color = simulation_running ? (SDL_Color){100, 255, 100, 255} : (SDL_Color){255, 100, 100, 255};
            draw_text(ren, 900, 12, status_text, status_color);

            draw_section_header(ren, start_x, 65, "BUTTONS");
            for (int i = 0; i < 8; i++)
            {
                draw_button(ren, start_x + i * spacing, 80, btn_size, btn_size,
                            fpga.btn[i], mapc.btn_connected[i], i, simulation_running);

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
                               mapc.dipswitches[i].connected, dip_names[i], simulation_running);
            }

            if (mapc.memory_conn.connected)
            {
                draw_memory_grid(ren, memory, mem_display);
                draw_load_button(ren, load_btn_rect.x, load_btn_rect.y, load_btn_hover,
                                 !simulation_running && memory.isEnabled(), file_dialog_open);
                draw_clear_button(ren, clear_btn_rect.x, clear_btn_rect.y, clear_btn_hover,
                                  !simulation_running && memory.isEnabled());
            }
            else
            {
                draw_text(ren, 650, 350, "Memory not connected in constraints.json",
                          (SDL_Color){150, 150, 150, 255});
            }

            char perf_text[128];
            if (mapc.clk_connected && simulation_running)
            {
                snprintf(perf_text, sizeof(perf_text), "Clock Cycles: %lld", total_clock_cycles);
            }
            else if (!simulation_running)
            {
                snprintf(perf_text, sizeof(perf_text), "Simulation STOPPED - %lld total cycles", total_clock_cycles);
            }
            else
            {
                snprintf(perf_text, sizeof(perf_text), "Clock Disabled - IDLE");
            }
            draw_text(ren, 10, 670, perf_text, (SDL_Color){100, 100, 120, 255});

            SDL_RenderPresent(ren);
            last_render_time = now;
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