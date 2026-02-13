#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Buyruqni ishga tushirish uchun yordamchi funksiya
static int run_command(const std::string &cmd) {
  std::cout << "Executing: " << cmd << std::endl;
  return std::system(cmd.c_str());
}

static std::string trim(const std::string &s) {
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
    start++;
  size_t end = s.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(s[end - 1])))
    end--;
  return s.substr(start, end - start);
}

static std::string quote(const std::string &s) { return "\"" + s + "\""; }

static std::string quote_if_needed(const std::string &cmd) {
  if (cmd.find(' ') == std::string::npos) {
    return cmd;
  }

  // Agar bu path bo'lsa (va mavjud bo'lsa), qo'shtirnoqqa olamiz.
  // Aks holda bu "py -3" kabi buyruq bo'lishi mumkin — uni quote qilmaymiz.
  if (fs::exists(fs::path(cmd))) {
    return quote(cmd);
  }
  return cmd;
}

static bool command_exists(const std::string &cmd) {
#ifdef _WIN32
  std::string check = cmd + " --version > NUL 2>&1";
#else
  std::string check = cmd + " --version > /dev/null 2>&1";
#endif
  return std::system(check.c_str()) == 0;
}

static std::string find_python() {
#ifdef _WIN32
  const char *home_env = std::getenv("USERPROFILE");
  if (home_env && std::strlen(home_env) > 0) {
    fs::path pio_python =
        fs::path(home_env) / ".platformio" / "penv" / "Scripts" / "python.exe";
    if (fs::exists(pio_python)) {
      return pio_python.string();
    }
  }
#else
  const char *home_env = std::getenv("HOME");
  if (home_env && std::strlen(home_env) > 0) {
    fs::path pio_python =
        fs::path(home_env) / ".platformio" / "penv" / "bin" / "python";
    if (fs::exists(pio_python)) {
      return pio_python.string();
    }
  }
#endif

  const char *env_py = std::getenv("PYTHON");
  if (env_py && std::strlen(env_py) > 0) {
    std::string candidate(env_py);
    if (command_exists(quote_if_needed(candidate))) {
      return candidate;
    }
  }

#ifdef _WIN32
  if (command_exists("python"))
    return "python";
  if (command_exists("py -3"))
    return "py -3";
#else
  if (command_exists("python3"))
    return "python3";
  if (command_exists("python"))
    return "python";
#endif

  return "";
}

static std::vector<std::string> list_envs_with_firmware(
    const fs::path &project_root) {
  std::vector<std::string> envs;
  fs::path build_root = project_root / ".pio" / "build";
  if (!fs::exists(build_root)) {
    return envs;
  }

  for (const auto &entry : fs::directory_iterator(build_root)) {
    if (!entry.is_directory()) {
      continue;
    }
    fs::path env_dir = entry.path();
    if (fs::exists(env_dir / "firmware.bin")) {
      envs.push_back(env_dir.filename().string());
    }
  }
  return envs;
}

static std::string detect_default_env(const fs::path &project_root) {
  fs::path ini_path = project_root / "platformio.ini";
  if (!fs::exists(ini_path)) {
    return "";
  }

  std::ifstream f(ini_path);
  if (!f) {
    return "";
  }

  std::string line;
  while (std::getline(f, line)) {
    // Remove inline comments
    size_t sem = line.find(';');
    if (sem != std::string::npos)
      line = line.substr(0, sem);
    size_t hash = line.find('#');
    if (hash != std::string::npos)
      line = line.substr(0, hash);

    std::string t = trim(line);
    if (t.rfind("default_envs", 0) != 0) {
      continue;
    }
    size_t eq = t.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    std::string rhs = trim(t.substr(eq + 1));
    if (rhs.empty()) {
      continue;
    }
    size_t comma = rhs.find(',');
    std::string first = trim(comma == std::string::npos ? rhs : rhs.substr(0, comma));
    return first;
  }

  return "";
}

static void print_usage(const char *argv0) {
  std::cout
      << "Usage:\n"
      << "  " << argv0 << " [--env <platformio_env>] [--out <output_bin>]\n\n"
      << "Examples:\n"
      << "  " << argv0 << " --env esp32_main\n"
      << "  " << argv0 << " --env esp32_payment\n";
}

int main(int argc, char *argv[]) {
  std::string env;
  std::string outPath;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      return 0;
    }
    if (arg == "--env" && i + 1 < argc) {
      env = argv[++i];
      continue;
    }
    if (arg == "--out" && i + 1 < argc) {
      outPath = argv[++i];
      continue;
    }
    std::cerr << "Unknown arg: " << arg << std::endl;
    print_usage(argv[0]);
    return 1;
  }

  // 1. Foydalanuvchi papkasini aniqlash (Windows/Linux)
#ifdef _WIN32
  const char *home_env = std::getenv("USERPROFILE");
#else
  const char *home_env = std::getenv("HOME");
#endif

  if (!home_env) {
    std::cerr << "Xato: Foydalanuvchi papkasi (Home directory) aniqlanmadi."
              << std::endl;
    return 1;
  }

  fs::path user_home(home_env);
  fs::path pio_packages = user_home / ".platformio" / "packages";

  // 2. esptool.py ni qidirish
  fs::path esptool_path;
  fs::path direct_path = pio_packages / "tool-esptoolpy" / "esptool.py";

  if (fs::exists(direct_path)) {
    esptool_path = direct_path;
  } else if (fs::exists(pio_packages)) {
    for (const auto &entry : fs::directory_iterator(pio_packages)) {
      if (!entry.is_directory()) {
        continue;
      }
      std::string dirname = entry.path().filename().string();
      if (dirname.find("tool-esptoolpy") == std::string::npos) {
        continue;
      }
      fs::path candidate = entry.path() / "esptool.py";
      if (fs::exists(candidate)) {
        esptool_path = candidate;
        break;
      }
    }
  }

  if (esptool_path.empty()) {
    std::cerr << "Xato: esptool.py topilmadi. Manzil: " << pio_packages
              << std::endl;
    std::cerr << "Iltimos, PlatformIO va esp32 platformasi o'rnatilganligini "
                 "tekshiring."
              << std::endl;
    return 1;
  }

  std::cout << "esptool.py topildi: " << esptool_path << std::endl;

  // 3. Project rootni topish (".pio" papkasi orqali)
  fs::path exe_path = (argc > 0) ? fs::absolute(argv[0]) : fs::current_path();
  fs::path exe_dir = exe_path.parent_path();

  fs::path project_root;
  if (fs::exists(exe_dir / ".pio")) {
    project_root = exe_dir;
  } else if (fs::exists(exe_dir.parent_path() / ".pio")) {
    project_root = exe_dir.parent_path();
  } else if (fs::exists(exe_dir.parent_path().parent_path() / ".pio")) {
    project_root = exe_dir.parent_path().parent_path();
  } else {
    fs::path current = fs::current_path();
    if (fs::exists(current / ".pio")) {
      project_root = current;
    } else {
      std::cerr << "Xato: Loyiha ildizi (Project Root) topilmadi. Iltimos, "
                   "loyiha ichida ishga tushiring."
                << std::endl;
      return 1;
    }
  }

  std::cout << "Project Root: " << project_root << std::endl;

  // 4. PlatformIO env aniqlash
  if (env.empty()) {
    env = detect_default_env(project_root);
  }
  if (env.empty()) {
    std::vector<std::string> built = list_envs_with_firmware(project_root);
    if (built.size() == 1) {
      env = built[0];
    } else {
      std::cerr << "Xato: PlatformIO environment aniqlanmadi. "
                   "--env <name> ni bering."
                << std::endl;
      if (!built.empty()) {
        std::cerr << "Build qilingan envlar: ";
        for (size_t i = 0; i < built.size(); i++) {
          if (i)
            std::cerr << ", ";
          std::cerr << built[i];
        }
        std::cerr << std::endl;
      }
      return 1;
    }
  }

  fs::path build_dir = project_root / ".pio" / "build" / env;
  fs::path bootloader_bin = build_dir / "bootloader.bin";
  fs::path partitions_bin = build_dir / "partitions.bin";
  fs::path firmware_bin = build_dir / "firmware.bin";

  // Output directory: scripts/build/
  fs::path output_dir = project_root / "scripts" / "build";
  if (!fs::exists(output_dir)) {
    fs::create_directories(output_dir);
  }

  fs::path output_bin = outPath.empty()
                            ? (output_dir / ("full_firmware_" + env + ".bin"))
                            : fs::path(outPath);
  if (!outPath.empty()) {
    fs::path parent = output_bin.parent_path();
    if (!parent.empty() && !fs::exists(parent)) {
      fs::create_directories(parent);
    }
  }

  // 5. Fayllar mavjudligini tekshirish
  std::vector<fs::path> required_files = {bootloader_bin, partitions_bin,
                                          firmware_bin};
  bool missing = false;
  for (const auto &f : required_files) {
    if (!fs::exists(f)) {
      std::cerr << "Xato: Fayl topilmadi: " << f << std::endl;
      missing = true;
    }
  }

  if (missing) {
    std::cerr << "Iltimos, avval loyihani 'pio run -e " << env
              << "' orqali build qiling." << std::endl;
    return 1;
  }

  // 6. Buyruqni shakllantirish
  // esptool.py --chip esp32 merge_bin -o out.bin --flash_mode dio --flash_freq
  // 40m --flash_size 4MB 0x1000 bootloader.bin 0x8000 partitions.bin 0x10000
  // firmware.bin

  std::string python_cmd = find_python();
  if (python_cmd.empty()) {
    std::cerr << "Xato: Python topilmadi. Iltimos Python 3 o'rnating yoki "
                 "PYTHON env ni belgilang."
              << std::endl;
    return 1;
  }

  std::string cmd = quote_if_needed(python_cmd) + " " + quote(esptool_path.string()) +
                    " --chip esp32 merge_bin" +
                    " -o " + quote(output_bin.string()) +
                    " --flash_mode dio" +
                    " --flash_freq 40m" +
                    " --flash_size 4MB" +
                    " 0x1000 " + quote(bootloader_bin.string()) +
                    " 0x8000 " + quote(partitions_bin.string()) +
                    " 0x10000 " + quote(firmware_bin.string());

  std::cout << "--------------------------------------------------" << std::endl;
  std::cout << "Using env: " << env << std::endl;

  int result = run_command(cmd);

  std::cout << "--------------------------------------------------" << std::endl;

  if (result == 0) {
    std::cout << "Muvaffaqiyatli! To'liq proshivka tayyor: " << output_bin
              << std::endl;
  } else {
    std::cerr << "Xatolik yuz berdi. Kod: " << result << std::endl;
  }

  return result;
}
