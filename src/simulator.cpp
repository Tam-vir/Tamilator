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

// Check if a directory looks like a valid tamilator installation
bool is_valid_tamilator_dir(const fs::path &dir)
{
    // Check for key subdirectories that should exist in a tamilator installation
    bool has_src = fs::exists(dir / "src");
    bool has_assets = fs::exists(dir / "assets");
    bool has_include = fs::exists(dir / "include");

    return (has_src || has_assets || has_include);
}

// Find the actual tamilator installation directory by following the executable
std::string find_tamilator_by_executable()
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
    const char *exe_name = "tamilator.exe";
#else
    char path_sep = ':';
    const char *exe_name = "tamilator";
#endif

    // Search each directory in PATH for tamilator executable
    while (std::getline(ss, path_dir, path_sep))
    {
        // Trim whitespace
        path_dir.erase(0, path_dir.find_first_not_of(" \t\r\n"));
        path_dir.erase(path_dir.find_last_not_of(" \t\r\n") + 1);

        if (path_dir.empty())
            continue;

        fs::path exe_path = fs::path(path_dir) / exe_name;
        if (fs::exists(exe_path))
        {
            std::cout << "   Found tamilator executable at: " << exe_path.string() << std::endl;

            // Try to resolve symlink if it's a symlink
            fs::path real_path = exe_path;
            if (fs::is_symlink(exe_path))
            {
                real_path = fs::read_symlink(exe_path);
                std::cout << "   Resolved symlink to: " << real_path.string() << std::endl;

                // If relative symlink, resolve against the directory
                if (real_path.is_relative())
                {
                    real_path = fs::path(path_dir) / real_path;
                    real_path = fs::canonical(real_path);
                    std::cout << "   Full resolved path: " << real_path.string() << std::endl;
                }
            }

            // Get the directory containing the executable
            fs::path exe_dir = real_path.parent_path();
            std::cout << "   Executable directory: " << exe_dir.string() << std::endl;

            // Check if the executable directory itself is the installation
            if (is_valid_tamilator_dir(exe_dir))
            {
                std::cout << "   Executable directory is a valid tamilator installation" << std::endl;
                return exe_dir.string();
            }

            // Check parent directory (common pattern: bin/tamilator, installation is parent)
            fs::path parent_dir = exe_dir.parent_path();
            if (is_valid_tamilator_dir(parent_dir))
            {
                std::cout << "   Parent directory is a valid tamilator installation: " << parent_dir.string() << std::endl;
                return parent_dir.string();
            }

            // Check sibling directory (common pattern: /usr/bin/tamilator, installation is /usr/share/tamilator)
            std::vector<fs::path> possible_install_dirs = {
                parent_dir / "share" / "tamilator",
                parent_dir / "local" / "share" / "tamilator",
                parent_dir / "opt" / "tamilator",
                fs::path("/usr") / "share" / "tamilator",
                fs::path("/usr") / "local" / "share" / "tamilator",
                fs::path("/opt") / "tamilator",
                fs::path(std::getenv("HOME")) / ".local" / "share" / "tamilator",
                fs::path(std::getenv("HOME")) / "tamilator"};

            for (const auto &install_dir : possible_install_dirs)
            {
                if (fs::exists(install_dir) && is_valid_tamilator_dir(install_dir))
                {
                    std::cout << "   Found tamilator installation at: " << install_dir.string() << std::endl;
                    return install_dir.string();
                }
            }

            // If we found the executable but no installation directory,
            // maybe the installation is where the executable came from originally
            // Try to find by looking for src/ directory upwards
            fs::path current = exe_dir;
            for (int i = 0; i < 5; i++) // Look up to 5 levels up
            {
                if (is_valid_tamilator_dir(current))
                {
                    std::cout << "   Found tamilator installation by searching up from executable: " << current.string() << std::endl;
                    return current.string();
                }
                current = current.parent_path();
                if (current.empty() || current == current.root_path())
                    break;
            }
        }
    }

    return "";
}

// Alternative: Look for TAMILATOR_HOME environment variable
std::string get_tamilator_home()
{
    const char *tamilator_home = std::getenv("TAMILATOR_HOME");
    if (tamilator_home)
    {
        fs::path home_path(tamilator_home);
        if (fs::exists(home_path) && fs::is_directory(home_path) && is_valid_tamilator_dir(home_path))
        {
            std::cout << "   Found TAMILATOR_HOME: " << home_path.string() << std::endl;
            return home_path.string();
        }
        else
        {
            std::cerr << "   Warning: TAMILATOR_HOME points to invalid directory: " << tamilator_home << std::endl;
        }
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

    // First try TAMILATOR_HOME environment variable
    std::string install_dir = get_tamilator_home();

    // If not found, find by following the executable
    if (install_dir.empty())
    {
        install_dir = find_tamilator_by_executable();
    }

    if (install_dir.empty())
    {
        std::cerr << "\n❌ Error: Could not find tamilator installation directory\n";
        std::cerr << "\nPlease either:\n";
        std::cerr << "  1. Set TAMILATOR_HOME environment variable to the tamilator installation directory\n";
        std::cerr << "  2. Ensure tamilator is installed with the expected directory structure\n";
        std::cerr << "\nExpected structure:\n";
        std::cerr << "  tamilator/\n";
        std::cerr << "    ├── bin/\n";
        std::cerr << "    ├── src/\n";
        std::cerr << "    ├── assets/\n";
        std::cerr << "    └── include/\n";
        return 1;
    }

    std::cout << "\n📁 Installation directory: " << install_dir << std::endl;

    // Get current working directory (full path)
    std::string current_dir = fs::current_path().string();
    std::cout << "📁 Current directory: " << current_dir << std::endl;

    std::vector<std::string> verilog_files;
    for (int i = 1; i < argc; i++)
        verilog_files.push_back(argv[i]);

    std::string vfiles = join(verilog_files, " ");

    // Paths to bundled dependencies (relative to tamilator installation directory)
    std::string bin_path = install_dir + "/bin";
    std::string include_path = install_dir + "/include";
    std::string share_path = install_dir + "/share";
    std::string imgui_path = install_dir + "/imgui";
    std::string assets_path = install_dir + "/assets";
    std::string src_path = install_dir + "/src";

    // If the above paths don't exist, try without subdirectories (everything in root)
    if (!fs::exists(src_path))
    {
        // Try root directory itself
        if (is_valid_tamilator_dir(install_dir))
        {
            src_path = install_dir + "/src";
            assets_path = install_dir + "/assets";
            include_path = install_dir + "/include";
            imgui_path = install_dir + "/imgui";
            bin_path = install_dir + "/bin";

            // If still not found, check if src exists directly in install_dir
            if (!fs::exists(src_path))
            {
                src_path = install_dir + "/src";
            }
        }
    }

    // Final check
    if (!fs::exists(src_path))
    {
        std::cerr << "\n❌ Error: Required directories not found in " << install_dir << std::endl;
        std::cerr << "Expected to find a 'src' directory\n";
        std::cerr << "\nContents of " << install_dir << ":\n";
        try
        {
            for (const auto &entry : fs::directory_iterator(install_dir))
            {
                std::cerr << "  - " << entry.path().filename().string() << std::endl;
            }
        }
        catch (...)
        {
            std::cerr << "  (Unable to list directory contents)" << std::endl;
        }
        return 1;
    }

    std::cout << "   Source path: " << src_path << std::endl;
    std::cout << "   Assets path: " << assets_path << std::endl;

    // Copy sim_gui.cpp
    std::string sim_gui_src = src_path + "/sim_gui.cpp";

    // Assets source and destination with full paths
    std::string assets_src = assets_path;
    std::string assets_dst = current_dir + "/assets";

    // Copy assets folder to current directory using full paths
    std::cout << "\n📄 Copying assets folder to current directory...\n";
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
    std::cout << "\n📄 Copying sim_gui.cpp to current directory...\n";
    std::cout << "   From: " << sim_gui_src << std::endl;
    std::cout << "   To: " << sim_gui_dst << std::endl;
    fs::copy(sim_gui_src, sim_gui_dst, fs::copy_options::overwrite_existing);

    // Generate signal_checks.h
    std::cout << "\n📝 Generating signal checks...\n";

    std::string constraints_path = current_dir + "/constraints.json";
    if (!fs::exists(constraints_path))
    {
        std::cout << "   Creating default constraints.json\n";
        std::ofstream default_json(constraints_path);
        default_json << R"({})";
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

    std::cout << "\n🔧 Building Verilator model...\n";

    // Add bundled bin to PATH temporarily if it exists
    if (fs::exists(bin_path))
    {
        std::string old_path = std::getenv("PATH") ? std::getenv("PATH") : "";
        std::string path_separator =
#ifdef _WIN32
            ";";
#else
            ":";
#endif
        std::string new_path = bin_path + path_separator + old_path;
        setenv("PATH", new_path.c_str(), 1);
    }

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

    std::cout << "\n🧱 Compiling simulator...\n";

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
    std::cout << "\n🧹 Cleaning up...\n";
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

    std::cout << "\n✅ Build successful!\n";
    std::cout << "🚀 Running simulator...\n";

    std::string sim_gui_path = current_dir + "/sim_gui";
    return system(sim_gui_path.c_str());
}