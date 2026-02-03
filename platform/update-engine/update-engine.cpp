#include <archive.h>
#include <archive_entry.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include <thread>
#include <chrono>

static const char *STATUS_DIR = "/run/update-engine";
static const char *EXPECTED_DOWNLOAD_SIZE_FILE = "/run/update-engine/expected-download-size";
static const char *EXPECTED_WRITE_SIZE_FILE = "/run/update-engine/expected-size";
static const char *PROGRESS_FILE = "/run/update-engine/progress";
static const char *PHASE_FILE = "/run/update-engine/phase";
static const char *ERROR_FILE = "/run/update-engine/error";
static const char *DONE_FILE = "/run/update-engine/done";
static const char *MANIFEST_FILE = "/run/update-engine/manifest.ini";
static const char *BOOT_STAGING = "/run/update-engine/boot.img";
static const char *OTA_PAS = "/anki/etc/ota.pas";

static bool verbose = false;

// cheaty mccheat-cheat
class Pipeline
{
public:
  Pipeline(const std::string &command) : pid_(-1), in_fd_(-1), out_fd_(-1)
  {

    int in_pipe[2];
    int out_pipe[2];

    if (pipe(in_pipe) < 0 || pipe(out_pipe) < 0)
    {
      perror("pipe");
      return;
    }

    pid_ = fork();
    if (pid_ < 0)
    {
      perror("fork");
      return;
    }

    if (pid_ == 0)
    { // child
      dup2(in_pipe[0], STDIN_FILENO);
      dup2(out_pipe[1], STDOUT_FILENO);

      close(in_pipe[0]);
      close(in_pipe[1]);
      close(out_pipe[0]);
      close(out_pipe[1]);

      execl("/bin/sh", "sh", "-c", command.c_str(), (char *)NULL);
      perror("execl");
      _exit(127);
    }

    in_fd_ = in_pipe[1];
    out_fd_ = out_pipe[0];

    close(in_pipe[0]);
    close(out_pipe[1]);
  }

  ~Pipeline()
  {
    if (in_fd_ != -1)
      close(in_fd_);
    if (out_fd_ != -1)
      close(out_fd_);
    if (pid_ > 0)
      wait();
  }

  Pipeline(const Pipeline &) = delete;
  Pipeline &operator=(const Pipeline &) = delete;

  ssize_t write(const void *buf, size_t count)
  {
    return ::write(in_fd_, buf, count);
  }

  ssize_t read(void *buf, size_t count)
  {
    return ::read(out_fd_, buf, count);
  }

  void close_input()
  {
    if (in_fd_ != -1)
    {
      close(in_fd_);
      in_fd_ = -1;
    }
  }

  int wait()
  {
    if (pid_ <= 0)
      return -1;
    int status = 0;
    if (waitpid(pid_, &status, 0) == -1)
    {
      perror("waitpid");
      return -1;
    }
    pid_ = -1;
    return status;
  }

  bool is_valid() const { return pid_ > 0 && in_fd_ != -1 && out_fd_ != -1; }

private:
  pid_t pid_;
  int in_fd_;
  int out_fd_;
};

static void write_status(const char *path, const std::string &v)
{
  std::ofstream f(path);
  if (f)
  {
    f << v;
    f.flush();
  }
}

static void die(int code, const std::string &text)
{
  write_status(ERROR_FILE, text);
  std::cerr << "error: " << text << std::endl;
  ::unlink(BOOT_STAGING);
  exit(code);
}

static bool file_exists(const std::string &p)
{
  struct stat st;
  return (stat(p.c_str(), &st) == 0);
}

static bool ensure_status_dir()
{
  struct stat st;
  if (stat(STATUS_DIR, &st) != 0)
  {
    if (mkdir(STATUS_DIR, 0755) != 0)
    {
      std::cerr << "failed to mkdir " << STATUS_DIR << std::endl;
      return false;
    }
  }
  return true;
}

static std::string run_command_capture_stdout(const std::string &cmd)
{
  std::string out;
  FILE *f = popen(cmd.c_str(), "r");
  if (!f)
    return out;
  char buf[4096];
  while (true)
  {
    size_t n = fread(buf, 1, sizeof(buf), f);
    if (n > 0)
      out.append(buf, buf + n);
    if (n < sizeof(buf))
      break;
  }
  pclose(f);
  return out;
}

static std::string get_prop(const std::string &prop)
{
  std::string cmd = "/usr/bin/getprop " + prop;
  std::string o = run_command_capture_stdout(cmd);
  while (!o.empty() && (o.back() == '\n' || o.back() == '\r'))
    o.pop_back();
  return o;
}

static void append_device_info_to_url(std::string &url)
{
  std::string osv = get_prop("ro.anki.version");
  std::string vv = get_prop("ro.anki.victor.version");
  std::string vt = get_prop("ro.build.target");
  std::string esn = get_prop("ro.serialno");

  if (url.find('?') == std::string::npos)
  {
    url += '?';
  }
  else if (url.back() != '&')
  {
    url += '&';
  }

  url += "emresn=" + esn + "&ankiversion=" + osv + "&victorversion=" + vv + "&victortarget=" + vt;
}

static std::string construct_auto_update_url()
{
  const char *dev_base_url_env = getenv("UPDATE_ENGINE_ANKIDEV_BASE_URL");
  const char *base_url_env = getenv("UPDATE_ENGINE_BASE_URL");

  std::string base_url;
  if (dev_base_url_env)
  {
    base_url = dev_base_url_env;
  }
  else if (base_url_env)
  {
    base_url = base_url_env;
  }
  else
  {
    return "";
  }

  const char *ota_type_env = getenv("UPDATE_ENGINE_OTA_TYPE");
  std::string ota_type = ota_type_env ? ota_type_env : "diff";

  std::string os_version = get_prop("ro.anki.version");
  if (os_version.empty())
  {
    return "";
  }

  // .rstrip("ud")
  if (os_version.length() >= 2 && os_version.substr(os_version.length() - 2) == "ud")
  {
    os_version.resize(os_version.length() - 2);
  }

  return base_url + ota_type + "/" + os_version + ".ota";
}

static std::map<std::string, std::map<std::string, std::string>> parse_ini(const std::string &content)
{
  std::istringstream iss(content);
  std::string line;
  std::string cur;
  std::map<std::string, std::map<std::string, std::string>> ret;
  while (std::getline(iss, line))
  {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
      line.pop_back();
    std::string s = line;
    auto lpos = s.find_first_not_of(" \t");
    if (lpos == std::string::npos)
      continue;
    s = s.substr(lpos);
    if (s.empty())
      continue;
    if (s[0] == ';' || s[0] == '#')
      continue;
    if (s[0] == '[')
    {
      auto e = s.find(']');
      if (e != std::string::npos)
      {
        cur = s.substr(1, e - 1);
      }
    }
    else
    {
      auto eq = s.find('=');
      if (eq != std::string::npos && !cur.empty())
      {
        std::string k = s.substr(0, eq);
        std::string v = s.substr(eq + 1);
        auto trim = [](std::string &x)
        {
          auto a = x.find_first_not_of(" \t");
          if (a == std::string::npos)
          {
            x.clear();
            return;
          }
          auto b = x.find_last_not_of(" \t");
          x = x.substr(a, b - a + 1);
        };
        trim(k);
        trim(v);
        ret[cur][k] = v;
      }
    }
  }
  return ret;
}

bool should_inhibit()
{
  if (file_exists("/anki-devtools") || file_exists("/data/data/user-do-not-auto-update") || file_exists("/etc/do-not-auto-update"))
  {
    return true;
  }
  return false;
}

static std::string get_slot_name(const std::string &partition, char slot)
{
  if (slot == 'f')
  {
    // in case i decide to make a WireOS recovery
    if (partition == "system")
      return std::string("/dev/block/bootdevice/by-name/system_a");
    if (partition == "boot")
      return std::string("/dev/block/bootdevice/by-name/boot_a");
  }
  std::string label = partition + "_" + std::string(1, slot);
  return std::string("/dev/block/bootdevice/by-name/") + label;
}

static std::pair<char, char> get_slot_from_cmdline()
{
  std::ifstream f("/proc/cmdline");
  std::string raw;
  std::getline(f, raw);
  std::istringstream iss(raw);
  std::string arg;
  std::map<std::string, std::string> kv;
  while (iss >> arg)
  {
    auto eq = arg.find('=');
    if (eq != std::string::npos)
    {
      kv[arg.substr(0, eq)] = arg.substr(eq + 1);
    }
    else
      kv[arg] = "";
  }
  std::string sfx = "_f";
  if (kv.find("androidboot.slot_suffix") != kv.end())
    sfx = kv["androidboot.slot_suffix"];
  if (sfx == "_a")
    return {'a', 'b'};
  if (sfx == "_b")
    return {'b', 'a'};
  return {'f', 'a'};
}

static bool stream_and_process_entry(struct archive *a,
                                     const std::string &pipeline_cmd,
                                     const std::string &dest_path,
                                     uint64_t expected_bytes,
                                     uint64_t &written_so_far,
                                     uint64_t expected_total_for_write)
{
  Pipeline p(pipeline_cmd);
  if (!p.is_valid())
  {
    std::cerr << "failed to start pipeline: " << pipeline_cmd << std::endl;
    return false;
  }

  int dest_fd = open(dest_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (dest_fd < 0)
  {
    std::cerr << "failed to open destination " << dest_path << ": " << strerror(errno) << std::endl;
    return false;
  }

  bool success = true;
  uint64_t total_uncompressed_out = 0;

  std::thread writer([&]
                     {
        const size_t BUFSZ = 64*1024;
        void* buf = malloc(BUFSZ);
        while (true) {
            ssize_t r = archive_read_data(a, buf, BUFSZ);
            if (r < 0) {
                std::cerr << "archive_read_data error: " << archive_error_string(a) << std::endl;
                success = false;
                break;
            }
            if (r == 0) break;
            if (p.write(buf, r) != r) {
                break;
            }
        }
        free(buf);
        p.close_input(); });

  const size_t BUFSZ = 64 * 1024;
  void *buf = malloc(BUFSZ);
  while (success)
  {
    ssize_t r = p.read(buf, BUFSZ);
    if (r < 0)
    {
      perror("pipeline read");
      success = false;
      break;
    }
    if (r == 0)
      break;

    if (::write(dest_fd, buf, r) != r)
    {
      std::cerr << "short write to destination " << dest_path << std::endl;
      success = false;
      break;
    }
    total_uncompressed_out += r;
    written_so_far += r;
    write_status(PROGRESS_FILE, std::to_string(written_so_far));
    if (verbose)
    {
      std::cout << "\rprogress: " << written_so_far << "/" << expected_total_for_write << " bytes" << std::flush;
    }
  }
  free(buf);
  writer.join();
  close(dest_fd);

  int status = p.wait();
  if (WIFEXITED(status))
  {
    int exitstatus = WEXITSTATUS(status);
    if (exitstatus != 0)
    {
      std::cerr << "\npipeline exited with status " << exitstatus << std::endl;
      success = false;
    }
  }
  else
  {
    std::cerr << "\npipeline did not exit normally" << std::endl;
    success = false;
  }

  if (success && expected_bytes > 0 && total_uncompressed_out != expected_bytes)
  {
    std::cerr << "\nwarning: bytes processed (" << total_uncompressed_out << ") != manifest bytes (" << expected_bytes << ")\n";
  }

  if (verbose)
    std::cout << "\n";
  return success;
}

int main(int argc, char **argv)
{
  std::string url;
  for (int i = 1; i < argc; i++)
  {
    std::string arg = argv[i];
    if (arg == "-v")
      verbose = true;
    else
      url = arg;
  }
  if (url.empty())
  {
    const char* env = getenv("UPDATE_ENGINE_URL");
    if (env)
    {
      url = env;
    } else {
      std::cout << "performing auto-update...\n";
      url = "auto";
    }
  }

  if (url == "auto")
  {
    if (should_inhibit()) {
      die(200, "auto-update inhibited");
    }
    url = construct_auto_update_url();
    if (url.empty())
    {
      die(2, "UPDATE_ENGINE_BASE_URL empty or ro.anki.version bad");
    }
    if (verbose)
    {
      std::cout << "auto-constructed URL: " << url << std::endl;
    }
  }
  
  system("/usr/bin/rm -rf /run/update-engine");
  system("/usr/bin/mkdir /run/update-engine");

  append_device_info_to_url(url);
  if (verbose)
  {
    std::cout << "final URL with params: " << url << std::endl;
  }

  if (!ensure_status_dir())
  {
    die(220, "Unable to ensure status dir");
  }
  write_status(PHASE_FILE, "download");

  // get content-length
  {
    std::string curl_head = "curl -sI \"" + url + "\" 2>/dev/null";
    std::string hdrs = run_command_capture_stdout(curl_head);
    std::string content_length = "0";
    std::istringstream iss(hdrs);
    std::string line;
    while (std::getline(iss, line))
    {
      std::string low = line;
      std::transform(low.begin(), low.end(), low.begin(), ::tolower);
      auto pos = low.find("content-length:");
      if (pos != std::string::npos)
      {
        auto val = line.substr(pos + strlen("content-length:"));
        auto a = val.find_first_not_of(" \t");
        if (a != std::string::npos)
        {
          auto b = val.find_last_not_of(" \t\r\n");
          val = val.substr(a, b - a + 1);
        }
        content_length = val;
        break;
      }
    }
    write_status(EXPECTED_DOWNLOAD_SIZE_FILE, content_length);
  }

  // my dorm wifi is shitty
  std::string curl_cmd = "curl -L --silent --show-error --fail --retry 3 --connect-timeout 20 \"" + url + "\"";
  FILE *curl_pipe = popen(curl_cmd.c_str(), "r");
  if (!curl_pipe)
  {
    die(203, "failed to open URL");
  }

  int curl_status = 0;

  struct archive *a = archive_read_new();
  archive_read_support_format_tar(a);
  archive_read_support_filter_all(a);

  if (archive_read_open_FILE(a, curl_pipe) != ARCHIVE_OK)
  {
    curl_status = pclose(curl_pipe);

    if (WIFEXITED(curl_status) && WEXITSTATUS(curl_status) != 0)
    {
      die(204, "download failed. likely a bad URL");
    }

    die(204, std::string("couldn't open contents as tar file: ") + archive_error_string(a));
  }

  struct archive_entry *entry;
  bool got_manifest = false;
  std::string manifest_content;
  while (archive_read_next_header(a, &entry) == ARCHIVE_OK)
  {
    const char *name = archive_entry_pathname(entry);
    std::string nm(name ? name : "");
    // if signed, ignore
    if (nm.size() >= 14 && nm.substr(nm.size() - 14) == "manifest.sha256")
    {
      const size_t BUF = 32 * 1024;
      char *tmp = (char *)malloc(BUF);
      while (archive_read_data(a, tmp, BUF) > 0)
      {
      }
      free(tmp);
      continue;
    }
    if (nm.size() >= 12 && nm.substr(nm.size() - 12) == "manifest.ini")
    {
      std::ostringstream sb;
      const size_t BUF = 64 * 1024;
      char *tmp = (char *)malloc(BUF);
      int r;
      while ((r = archive_read_data(a, tmp, BUF)) > 0)
      {
        sb.write(tmp, r);
      }
      free(tmp);
      manifest_content = sb.str();
      std::ofstream mf(MANIFEST_FILE, std::ios::binary);
      mf << manifest_content;
      mf.close();
      got_manifest = true;
      break;
    }
    else
    {
      die(200, std::string("expected manifest.ini at beginning of download, found ") + nm);
    }
  }

  if (!got_manifest)
    die(200, "manifest.ini not found");

  auto manifest = parse_ini(manifest_content);
  int num_images = 0;
  if (manifest["META"].count("num_images"))
    num_images = std::stoi(manifest["META"]["num_images"]);
  if (num_images != 2)
  {
    die(201, "wire is lazy and only implemented boot+system updates");
  }

  uint64_t boot_bytes = 0, system_bytes = 0;
  int boot_encryption = 0, system_encryption = 0;
  std::string boot_compression, system_compression;
  if (manifest.count("BOOT"))
  {
    if (manifest["BOOT"].count("bytes"))
      boot_bytes = std::stoull(manifest["BOOT"]["bytes"]);
    if (manifest["BOOT"].count("encryption"))
      boot_encryption = std::stoi(manifest["BOOT"]["encryption"]);
    if (manifest["BOOT"].count("compression"))
      boot_compression = manifest["BOOT"]["compression"];
  }
  else
    die(201, "BOOT section missing");
  if (manifest.count("SYSTEM"))
  {
    if (manifest["SYSTEM"].count("bytes"))
      system_bytes = std::stoull(manifest["SYSTEM"]["bytes"]);
    if (manifest["SYSTEM"].count("encryption"))
      system_encryption = std::stoi(manifest["SYSTEM"]["encryption"]);
    if (manifest["SYSTEM"].count("compression"))
      system_compression = manifest["SYSTEM"]["compression"];
  }
  else
    die(201, "SYSTEM section missing");

  uint64_t total_expected_write = boot_bytes + system_bytes;
  write_status(EXPECTED_WRITE_SIZE_FILE, std::to_string(total_expected_write));
  write_status(PROGRESS_FILE, "0");

  bool got_boot = false, got_system = false;
  auto slots = get_slot_from_cmdline();
  char current_slot = slots.first, target_slot = slots.second;
  uint64_t written_so_far = 0;

  std::string cmd = "/bin/bootctl-anki ";
  cmd += current_slot;
  cmd += " set_unbootable ";
  cmd += target_slot;
  system(cmd.c_str());

  while (archive_read_next_header(a, &entry) == ARCHIVE_OK)
  {
    const char *name = archive_entry_pathname(entry);
    std::string nm(name ? name : "");
    std::string pipeline;
    if (nm.size() >= 25 && nm.substr(nm.size() - 25) == "apq8009-robot-boot.img.gz")
    {
      if (boot_encryption == 1)
      {
        pipeline = std::string("openssl enc -d -aes-256-ctr -md md5 -pass file:") + OTA_PAS + " 2>/dev/null";
        if (boot_compression == "gz")
          pipeline += " | gunzip";
      }
      else
      {
        if (boot_compression == "gz")
          pipeline = "gunzip";
        else
          pipeline = "cat";
      }
      if (!stream_and_process_entry(a, pipeline, BOOT_STAGING, boot_bytes, written_so_far, total_expected_write))
        die(209, "Boot image pipeline failed");

      int fdst = open(get_slot_name("boot", target_slot).c_str(), O_WRONLY | O_CREAT, 0600);
      if (fdst < 0)
        die(202, "Could not open boot slot device");
      int fsrc = open(BOOT_STAGING, O_RDONLY);
      if (fsrc < 0)
      {
        close(fdst);
        die(202, "Could not open boot staging file");
      }

      const size_t BUFSZ = 64 * 1024;
      char *buf = (char *)malloc(BUFSZ);
      ssize_t r;
      while ((r = read(fsrc, buf, BUFSZ)) > 0)
      {
        ssize_t w = write(fdst, buf, r);
        if (w != r)
        {
          free(buf);
          close(fsrc);
          close(fdst);
          die(208, "short write when copying boot to slot");
        }
      }
      free(buf);
      close(fsrc);
      close(fdst);
      ::unlink(BOOT_STAGING);
      got_boot = true;
    }
    else if (nm.size() >= 26 && nm.substr(nm.size() - 26) == "apq8009-robot-sysfs.img.gz")
    {
      std::string system_slot = get_slot_name("system", target_slot);
      if (system_encryption == 1)
      {
        pipeline = std::string("openssl enc -d -aes-256-ctr -md md5 -pass file:") + OTA_PAS + " 2>/dev/null";
        if (system_compression == "gz")
          pipeline += " | gunzip";
      }
      else
      {
        if (system_compression == "gz")
          pipeline = "gunzip";
        else
          pipeline = "cat";
      }
      if (!stream_and_process_entry(a, pipeline, system_slot, system_bytes, written_so_far, total_expected_write))
        die(209, "System image pipeline failed");
      got_system = true;
    }
    else if (nm.size() >= 14 && nm.substr(nm.size() - 14) == "manifest.sha256")
    {
      const size_t BUF = 32 * 1024;
      char *tmp = (char *)malloc(BUF);
      while (archive_read_data(a, tmp, BUF) > 0)
      {
      }
      free(tmp);
      continue;
    }
    else
    {
      const size_t BUF = 32 * 1024;
      char *tmp = (char *)malloc(BUF);
      while (archive_read_data(a, tmp, BUF) > 0)
      {
      }
      free(tmp);
      continue;
    }
    if (got_boot && got_system)
      break;
  }

  archive_read_close(a);
  archive_read_free(a);
  pclose(curl_pipe);
  if (!got_boot || !got_system)
    die(201, "missing boot or system in OTA");

  system("/bin/sync");
  cmd = "/bin/bootctl-anki ";
  cmd += current_slot;
  cmd += " set_active ";
  cmd += target_slot;
  system(cmd.c_str());

  if (url == "auto") {
    system("/sbin/reboot");
  }

  ::unlink(ERROR_FILE);
  write_status(DONE_FILE, "1");
  write_status(PHASE_FILE, "done");
  write_status(PROGRESS_FILE, "0");
  std::cout << "update complete\n";
  return 0;
}
