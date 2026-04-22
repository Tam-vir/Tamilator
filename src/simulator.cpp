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

bool is_valid_tamilator_dir(const fs::path &dir)
{
    bool has_src = fs::exists(dir / "src");
    bool has_assets = fs::exists(dir / "assets");
    bool has_include = fs::exists(dir / "include");

    return (has_src || has_assets || has_include);
}

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

    while (std::getline(ss, path_dir, path_sep))
    {
        path_dir.erase(0, path_dir.find_first_not_of(" \t\r\n"));
        path_dir.erase(path_dir.find_last_not_of(" \t\r\n") + 1);

        if (path_dir.empty())
            continue;

        fs::path exe_path = fs::path(path_dir) / exe_name;
        if (fs::exists(exe_path))
        {
            fs::path real_path = exe_path;
            if (fs::is_symlink(exe_path))
            {
                real_path = fs::read_symlink(exe_path);
                if (real_path.is_relative())
                {
                    real_path = fs::path(path_dir) / real_path;
                    real_path = fs::canonical(real_path);
                }
            }

            fs::path exe_dir = real_path.parent_path();

            if (is_valid_tamilator_dir(exe_dir))
            {
                return exe_dir.string();
            }

            fs::path parent_dir = exe_dir.parent_path();
            if (is_valid_tamilator_dir(parent_dir))
            {
                return parent_dir.string();
            }

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
                    return install_dir.string();
                }
            }

            fs::path current = exe_dir;
            for (int i = 0; i < 5; i++)
            {
                if (is_valid_tamilator_dir(current))
                {
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

std::string get_tamilator_home()
{
    const char *tamilator_home = std::getenv("TAMILATOR_HOME");
    if (tamilator_home)
    {
        fs::path home_path(tamilator_home);
        if (fs::exists(home_path) && fs::is_directory(home_path) && is_valid_tamilator_dir(home_path))
        {
            return home_path.string();
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

    std::string install_dir = get_tamilator_home();

    if (install_dir.empty())
    {
        install_dir = find_tamilator_by_executable();
    }

    if (install_dir.empty())
    {
        std::cerr << "Error: Could not find tamilator installation directory\n";
        std::cerr << "Please either:\n";
        std::cerr << "  1. Set TAMILATOR_HOME environment variable to the tamilator installation directory\n";
        std::cerr << "  2. Ensure tamilator is installed with the expected directory structure\n";
        return 1;
    }

    std::string current_dir = fs::current_path().string();

    std::vector<std::string> verilog_files;
    for (int i = 1; i < argc; i++)
        verilog_files.push_back(argv[i]);

    std::string vfiles = join(verilog_files, " ");

    std::string bin_path = install_dir + "/bin";
    std::string include_path = install_dir + "/include";
    std::string share_path = install_dir + "/share";
    std::string imgui_path = install_dir + "/imgui";
    std::string assets_path = install_dir + "/assets";
    std::string src_path = install_dir + "/src";

    if (!fs::exists(src_path))
    {
        if (is_valid_tamilator_dir(install_dir))
        {
            src_path = install_dir + "/src";
            assets_path = install_dir + "/assets";
            include_path = install_dir + "/include";
            imgui_path = install_dir + "/imgui";
            bin_path = install_dir + "/bin";

            if (!fs::exists(src_path))
            {
                src_path = install_dir + "/src";
            }
        }
    }

    if (!fs::exists(src_path))
    {
        std::cerr << "Error: Required directories not found in " << install_dir << std::endl;
        return 1;
    }

    std::string sim_gui_src = src_path + "/sim_gui.cpp";

    std::string assets_src = assets_path;
    std::string assets_dst = current_dir + "/assets";

    if (fs::exists(assets_src))
    {
        fs::copy(assets_src, assets_dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    }

    if (!fs::exists(sim_gui_src))
    {
        std::cerr << "Error: " << sim_gui_src << " not found\n";
        return 1;
    }

    std::string sim_gui_dst = current_dir + "/sim_gui.cpp";
    fs::copy(sim_gui_src, sim_gui_dst, fs::copy_options::overwrite_existing);

    std::string constraints_path = current_dir + "/constraints.json";
    if (!fs::exists(constraints_path))
    {
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

    std::string cmd1 = "verilator -Wall --cc " + vfiles +
                       " --build --top-module top "
                       "-CFLAGS \"-DVL_STRCASECMP=strcasecmp\" "
                       "-CFLAGS \"-include string.h\"";

    if (system(cmd1.c_str()) != 0)
    {
        std::cerr << "Verilator build failed\n";
        return 1;
    }

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

    if (system(cmd2.c_str()) != 0)
    {
        std::cerr << "GUI build failed\n";
        return 1;
    }

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

    std::string sim_gui_path = current_dir + "/sim_gui";
    return system(sim_gui_path.c_str());
}