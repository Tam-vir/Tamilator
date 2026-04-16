#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

std::string join(const std::vector<std::string> &v, const std::string &sep)
{
    std::ostringstream oss;
    for (size_t i = 0; i < v.size(); i++)
    {
        oss << v[i];
        if (i + 1 < v.size())
            oss << sep;
    }
    return oss.str();
}

// Find tamilator in PATH and return its directory
std::string find_tamilator_in_path()
{
    const char *path_env = std::getenv("PATH");
    if (!path_env)
    {
        return "";
    }

    std::string path_str(path_env);
    std::stringstream ss(path_str);
    std::string path_dir;

#ifdef _WIN32
    char path_sep = ';';
#else
    char path_sep = ':';
#endif

    // Search each directory in PATH for tamilator executable
    while (std::getline(ss, path_dir, path_sep))
    {
        // Trim whitespace
        path_dir.erase(0, path_dir.find_first_not_of(" \t\r\n"));
        path_dir.erase(path_dir.find_last_not_of(" \t\r\n") + 1);

        if (path_dir.empty())
            continue;

        fs::path exe_path = fs::path(path_dir) / "tamilator";

#ifdef _WIN32
        // Check with .exe extension on Windows
        if (fs::exists(exe_path.string() + ".exe"))
        {
            std::cout << "   Found tamilator at: " << path_dir << std::endl;
            return path_dir;
        }
#else
        if (fs::exists(exe_path))
        {
            std::cout << "   Found tamilator at: " << path_dir << std::endl;
            return path_dir;
        }
#endif
    }

    return "";
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cout << "Usage: tamilator file1.v file2.v ...\n";
        return 1;
    }

    // Find installation directory from PATH
    std::string install_dir = find_tamilator_in_path();
    if (install_dir.empty())
    {
        std::cerr << "Error: Could not find tamilator in PATH\n";
        std::cerr << "Current PATH: " << (std::getenv("PATH") ? std::getenv("PATH") : "empty") << std::endl;
        return 1;
    }

    std::cout << "📁 Installation directory (from PATH): " << install_dir << std::endl;

    // Get current working directory (full path)
    std::string current_dir = fs::current_path().string();
    std::cout << "📁 Current directory: " << current_dir << std::endl;

    std::vector<std::string> verilog_files;
    for (int i = 1; i < argc; i++)
        verilog_files.push_back(argv[i]);

    std::string vfiles = join(verilog_files, " ");

    // Paths to bundled dependencies (relative to install directory)
    // Since tamilator is in the root directory, adjust paths
    std::string bin_path = install_dir;
    std::string include_path = install_dir + "/include";
    std::string share_path = install_dir + "/share";
    std::string imgui_path = install_dir + "/imgui";
    std::string assets_path = install_dir + "/assets";
    std::string src_path = install_dir + "/src";

    // Check if directories exist, if not, they might be relative to parent
    if (!fs::exists(src_path))
    {
        // Try parent directory (if tamilator is in bin subdirectory)
        fs::path parent = fs::path(install_dir).parent_path();
        src_path = parent.string() + "/src";
        include_path = parent.string() + "/include";
        share_path = parent.string() + "/share";
        imgui_path = parent.string() + "/imgui";
        assets_path = parent.string() + "/assets";

        std::cout << "   Using parent directory: " << parent.string() << std::endl;
    }

    std::cout << "   Source path: " << src_path << std::endl;
    std::cout << "   Assets path: " << assets_path << std::endl;

    // Copy sim_gui.cpp
    std::string sim_gui_src = src_path + "/sim_gui.cpp";

    // Assets source and destination with full paths
    std::string assets_src = assets_path;
    std::string assets_dst = current_dir + "/assets";

    // Copy assets folder to current directory using full paths
    std::cout << "📄 Copying assets folder to current directory...\n";
    std::cout << "   From: " << assets_src << std::endl;
    std::cout << "   To: " << assets_dst << std::endl;

    if (fs::exists(assets_src))
    {
        fs::copy(assets_src, assets_dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        std::cout << "   ✅ Assets copied successfully" << std::endl;
    }
    else
    {
        std::cerr << "   ⚠️ Warning: Assets not found at " << assets_src << std::endl;
    }

    if (!fs::exists(sim_gui_src))
    {
        std::cerr << "Error: " << sim_gui_src << " not found\n";
        return 1;
    }

    std::string sim_gui_dst = current_dir + "/sim_gui.cpp";
    std::cout << "📄 Copying sim_gui.cpp to current directory...\n";
    std::cout << "   From: " << sim_gui_src << std::endl;
    std::cout << "   To: " << sim_gui_dst << std::endl;
    fs::copy(sim_gui_src, sim_gui_dst, fs::copy_options::overwrite_existing);

    // Generate signal_checks.h
    std::cout << "📝 Generating signal checks...\n";

    std::string constraints_path = current_dir + "/constraints.json";
    if (!fs::exists(constraints_path))
    {
        std::cout << "   Creating default constraints.json\n";
        std::ofstream default_json(constraints_path);
        default_json << R"({
  "inputs": {
    "BTN0": "btn0",
    "BTN1": "btn1",
    "BTN2": "btn2"
  },
  "outputs": {
    "LED0": "led0",
    "LED1": "led1",
    "LED2": "led2",
    "LED3": "led3"
  }
})";
        default_json.close();
    }

    std::string generate_script = src_path + "/generate_checks.py";
    if (!fs::exists(generate_script))
    {
        std::cerr << "Error: " << generate_script << " not found\n";
        return 1;
    }

    std::string cmd_python = "python3 \"" + generate_script + "\"";
    if (system(cmd_python.c_str()) != 0)
    {
        std::cerr << "Failed to generate signal checks\n";
        return 1;
    }

    std::cout << "🔧 Building Verilator model...\n";

    // Add bundled bin to PATH temporarily
    std::string old_path = std::getenv("PATH") ? std::getenv("PATH") : "";
    std::string path_separator =
#ifdef _WIN32
        ";";
#else
        ":";
#endif
    std::string new_path = bin_path + path_separator + old_path;

    setenv("PATH", new_path.c_str(), 1);

    // Pass CFLAGS to Verilator to fix strcasecmp issue
    std::string cmd1 = "verilator -Wall --cc " + vfiles +
                       " --build --top-module top "
                       "-CFLAGS \"-DVL_STRCASECMP=strcasecmp\" "
                       "-CFLAGS \"-include string.h\"";

    std::cout << "Running: " << cmd1 << "\n";
    if (system(cmd1.c_str()) != 0)
    {
        std::cerr << "Verilator build failed\n";
        return 1;
    }

    std::cout << "🧱 Compiling simulator...\n";

    // Compile sim_gui with bundled dependencies using full paths
    std::string cmd2 =
        "g++ -std=c++17 -O2 " + current_dir + "/sim_gui.cpp obj_dir/*.o "
                                              "-Iobj_dir "
                                              "-I\"" +
        imgui_path + "\" "
                     "-I/usr/share/verilator/include "
                     "-I/usr/share/verilator/include/vltstd "
                     "-I\"" +
        include_path + "/SDL2\" "
                       "-I\"" +
        include_path + "\" "
                       "-L/usr/lib/ "
                       "-lSDL2 -lSDL2_ttf "
                       "-DTAMILATOR_ASSETS_PATH=\\\"" +
        assets_dst + "\\\" "
                     "-o " +
        current_dir + "/sim_gui";

    std::cout << "Running: " << cmd2 << "\n";
    if (system(cmd2.c_str()) != 0)
    {
        std::cerr << "❌ GUI build failed\n";
        return 1;
    }

    // Cleanup using full paths
    std::cout << "🧹 Cleaning up...\n";
    fs::remove(current_dir + "/sim_gui.cpp");
    fs::remove(current_dir + "/signal_checks.h");

    std::string compat_path = current_dir + "/compat.h";
    if (fs::exists(compat_path))
    {
        fs::remove(compat_path);
    }

    std::string obj_dir_path = current_dir + "/obj_dir";
    if (fs::exists(obj_dir_path))
    {
        fs::remove_all(obj_dir_path);
    }

    std::cout << "✅ Build successful!\n";
    std::cout << "🚀 Running simulator...\n";

    std::string sim_gui_path = current_dir + "/sim_gui";
    return system(sim_gui_path.c_str());
}