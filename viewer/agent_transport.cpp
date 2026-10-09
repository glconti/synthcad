#include "agent_transport.h"
#include "agent_cli.h"
#include "project_contract.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <shlobj.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace synthcad {
namespace {
using nlohmann::json;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
constexpr size_t kMaxMessage = 16 * 1024 * 1024;
void Pause() { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
Clock::time_point Deadline(int timeout) {
  return Clock::now() + std::chrono::milliseconds(std::max(0, timeout));
}
std::string Hash(const std::string& value) {
  uint64_t hash = 14695981039346656037ull;
  for (unsigned char byte : value) { hash ^= byte; hash *= 1099511628211ull; }
  std::ostringstream out;
  out << std::hex << hash;
  return out.str();
}
std::string Nonce() {
  std::random_device random;
  return Hash(std::to_string(random()) + std::to_string(random()) +
              std::to_string(Clock::now().time_since_epoch().count()));
}
std::string Canonical(const std::string& path) {
  return CanonicalPath(fs::u8path(path));
}
enum class ProcessState { Alive, Exited, Unknown };
struct ProcessProbe {
  ProcessState state;
  std::string identity;
  std::string diagnostic;
};
bool ConfirmedStale(const ProcessProbe& probe, const std::string& identity) {
  return probe.state == ProcessState::Exited ||
      (probe.state == ProcessState::Alive && !identity.empty() && probe.identity != identity);
}
std::string NativeFailure(const std::string& operation, unsigned long code) {
#ifdef _WIN32
  return operation + " (Windows error " + std::to_string(code) + "). Check access permissions in this execution context; keep the same session registry.";
#else
  return operation + " (errno " + std::to_string(code) + "). Check access permissions in this execution context; keep the same session registry.";
#endif
}
#ifdef _WIN32
std::wstring Wide(const std::string& value) {
  if (value.empty()) return {};
  int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0);
  if (!size) throw std::runtime_error("Invalid UTF-8 path or session name");
  std::wstring result(size, 0);
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), result.data(), size);
  return result;
}
struct UserSecurity {
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  SECURITY_ATTRIBUTES attributes{};
  std::string identity;
  UserSecurity() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
      throw std::runtime_error("Cannot read current user identity");
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> buffer(size);
    bool ok = GetTokenInformation(token, TokenUser, buffer.data(), size, &size) != 0;
    CloseHandle(token);
    LPWSTR sid = nullptr;
    if (!ok || !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid))
      throw std::runtime_error("Cannot read current user SID");
    std::wstring sidString(sid);
    LocalFree(sid);
    for (wchar_t character : sidString) identity.push_back(static_cast<char>(character));
    std::wstring sddl = L"D:P(A;OICI;GA;;;" + sidString + L")";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
      throw std::runtime_error("Cannot restrict session access to current user");
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = descriptor;
    attributes.bInheritHandle = FALSE;
  }
  ~UserSecurity() { if (descriptor) LocalFree(descriptor); }
};
unsigned long ProcessId() { return GetCurrentProcessId(); }
ProcessProbe ProbeProcess(unsigned long pid) {
  const bool self = pid == ProcessId();
  HANDLE process = self ? GetCurrentProcess() : OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) {
    const auto code = GetLastError();
    return {code == ERROR_INVALID_PARAMETER ? ProcessState::Exited : ProcessState::Unknown, {},
            NativeFailure("Cannot inspect viewer process " + std::to_string(pid), code)};
  }
  FILETIME created{}, exit{}, kernel{}, user{};
  DWORD status = 0;
  if (!GetExitCodeProcess(process, &status)) {
    const auto code = GetLastError();
    if (!self) CloseHandle(process);
    return {ProcessState::Unknown, {}, NativeFailure("Cannot read viewer process state", code)};
  }
  if (status != STILL_ACTIVE) {
    if (!self) CloseHandle(process);
    return {ProcessState::Exited, {}, {}};
  }
  const bool ok = GetProcessTimes(process, &created, &exit, &kernel, &user) != 0;
  const auto code = ok ? ERROR_SUCCESS : GetLastError();
  if (!self) CloseHandle(process);
  if (!ok) return {ProcessState::Unknown, {}, NativeFailure("Cannot read viewer process identity", code)};
  return {ProcessState::Alive, std::to_string((uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime), {}};
}
#else
unsigned long ProcessId() { return static_cast<unsigned long>(getpid()); }
ProcessProbe ProbeProcess(unsigned long pid) {
  if (!pid || pid > static_cast<unsigned long>(std::numeric_limits<pid_t>::max()))
    return {ProcessState::Exited, {}, {}};
  if (kill(static_cast<pid_t>(pid), 0) != 0 && errno != EPERM) {
    const auto code = errno;
    return {code == ESRCH ? ProcessState::Exited : ProcessState::Unknown, {},
            NativeFailure("Cannot inspect viewer process " + std::to_string(pid), code)};
  }
  std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
  std::string line;
  std::getline(stat, line);
  auto close = line.rfind(')');
  if (close == std::string::npos)
    return {ProcessState::Unknown, {}, "Cannot read /proc process identity; retaining the session record."};
  std::istringstream fields(line.substr(close + 2));
  std::string value;
  // The first field following comm is field 3, the start time is field 22.
  for (int field = 3; field <= 22; ++field) {
    if (!(fields >> value)) return {ProcessState::Unknown, {}, "Incomplete /proc process identity; retaining the session record."};
    if (field == 3 && (value == "Z" || value == "X")) return {ProcessState::Exited, {}, {}};
  }
  return {ProcessState::Alive, value, {}};
}
#endif

fs::path Root() {
  const char* overridePath = std::getenv("SYNTHCAD_SESSION_DIR");
  fs::path root;
  if (overridePath && *overridePath) root = fs::absolute(fs::u8path(overridePath));
  else {
#ifdef _WIN32
    UserSecurity security;
    // Agent sandboxes can redirect TEMP and LOCALAPPDATA differently for every
    // tool invocation. Keep the usual per-user registry independent of that
    // redirection, so short-lived callers discover the same persistent viewer.
    PWSTR localAppData = nullptr;
    const auto result = SHGetKnownFolderPath(FOLDERID_LocalAppData,
        KF_FLAG_NO_PACKAGE_REDIRECTION, nullptr, &localAppData);
    if (FAILED(result)) throw std::runtime_error("Cannot locate stable per-user session storage");
    root = fs::path(localAppData) / "Temp" / ("synthcad-" + Hash(security.identity));
    CoTaskMemFree(localAppData);
#else
    root = fs::temp_directory_path() / ("synthcad-" + std::to_string(getuid()));
#endif
  }
#ifdef _WIN32
  UserSecurity security;
  if (!CreateDirectoryW(root.c_str(), &security.attributes) && GetLastError() != ERROR_ALREADY_EXISTS)
    throw std::runtime_error(NativeFailure("Cannot create private session directory " + root.u8string(), GetLastError()));
  // Apply a protected owner-only DACL even when the directory already exists.
  if (!SetFileSecurityW(root.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                        security.descriptor))
    throw std::runtime_error(NativeFailure("Cannot secure session directory " + root.u8string(), GetLastError()));
#else
  if (mkdir(root.c_str(), 0700) != 0 && errno != EEXIST)
    throw std::runtime_error("Cannot create private session directory: " + root.u8string());
  struct stat info{};
  if (lstat(root.c_str(), &info) || !S_ISDIR(info.st_mode) || info.st_uid != getuid())
    throw std::runtime_error("Session directory is not owned by the current user");
  if (chmod(root.c_str(), 0700)) throw std::runtime_error("Cannot secure session directory");
#endif
  return root;
}
fs::path RecordPath(const fs::path& root, const std::string& name) {
  return root / (Hash(name) + ".json");
}

class OpenLock {
#ifdef _WIN32
  HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
  int handle_ = -1;
#endif
 public:
  OpenLock(const fs::path& root, Clock::time_point deadline) {
    fs::path path = root / "open.lock";
    do {
#ifdef _WIN32
      handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (handle_ != INVALID_HANDLE_VALUE) return;
      const auto code = GetLastError();
      if (code != ERROR_SHARING_VIOLATION && code != ERROR_LOCK_VIOLATION)
        throw std::runtime_error(NativeFailure("Cannot open session launch lock " + path.u8string(), code));
#else
      if (handle_ < 0) handle_ = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
      if (handle_ < 0) throw std::runtime_error(NativeFailure("Cannot open session launch lock " + path.u8string(), errno));
      if (!flock(handle_, LOCK_EX | LOCK_NB)) return;
      if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) {
        const auto code = errno;
        close(handle_); handle_ = -1;
        throw std::runtime_error(NativeFailure("Cannot acquire session launch lock", code));
      }
#endif
      Pause();
    } while (Clock::now() < deadline);
#ifndef _WIN32
    if (handle_ >= 0) { close(handle_); handle_ = -1; }
#endif
    throw std::runtime_error("Timed out acquiring session launch lock");
  }
  ~OpenLock() {
#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
    if (handle_ >= 0) { flock(handle_, LOCK_UN); close(handle_); }
#endif
  }
};

#ifdef _WIN32
using Connection = HANDLE;
const Connection kInvalid = INVALID_HANDLE_VALUE;
void Close(Connection connection) { CloseHandle(connection); }
#else
using Connection = int;
const Connection kInvalid = -1;
void Close(Connection connection) { close(connection); }
#endif
bool WriteMessage(Connection connection, const std::string& message, Clock::time_point deadline) {
  size_t offset = 0;
  do {
    bool madeProgress = false;
#ifdef _WIN32
    DWORD sent = 0;
    // PIPE_NOWAIT writes larger than the named-pipe buffer can repeatedly fail
    // with ERROR_NO_DATA without transferring any bytes. Keep each attempt
    // below the configured 64 KiB buffer so the peer can drain it.
    const DWORD request = static_cast<DWORD>(std::min<size_t>(
        message.size() - offset, 16 * 1024));
    if (!WriteFile(connection, message.data() + offset, request, &sent, nullptr)) {
      DWORD error = GetLastError();
      if (error != ERROR_NO_DATA && error != ERROR_PIPE_BUSY) return false;
    }
    madeProgress = sent > 0;
#else
    ssize_t sent = send(connection, message.data() + offset, message.size() - offset, MSG_NOSIGNAL);
    if (sent < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return false;
      sent = 0;
    }
    madeProgress = sent > 0;
#endif
    offset += static_cast<size_t>(sent);
    if (offset == message.size()) return true;
    if (!madeProgress) Pause();
  } while (Clock::now() < deadline);
  return false;
}
bool ReadMessage(Connection connection, std::string& message, Clock::time_point deadline,
                 const std::atomic<bool>* stop = nullptr) {
  char buffer[8192];
  do {
    if (stop && stop->load()) return false;
    size_t received = 0;
#ifdef _WIN32
    DWORD read = 0;
    if (!ReadFile(connection, buffer, sizeof(buffer), &read, nullptr)) {
      DWORD error = GetLastError();
      if (error != ERROR_NO_DATA && error != ERROR_PIPE_LISTENING) return false;
    }
#else
    ssize_t read = recv(connection, buffer, sizeof(buffer), 0);
    if (read == 0) return false;
    if (read < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return false;
      read = 0;
    }
#endif
    received = static_cast<size_t>(read);
    message.append(buffer, received);
    if (message.size() > kMaxMessage) return false;
    auto newline = message.find('\n');
    if (newline != std::string::npos) { message.resize(newline); return true; }
    if (received == 0) Pause();
  } while (Clock::now() < deadline);
  return false;
}

Connection Connect(const std::string& endpoint, Clock::time_point deadline, std::string& diagnostic) {
  do {
#ifdef _WIN32
    Connection result = CreateFileW(Wide(endpoint).c_str(), GENERIC_READ | GENERIC_WRITE,
                                     0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (result != kInvalid) {
      DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
      if (SetNamedPipeHandleState(result, &mode, nullptr, nullptr)) return result;
      diagnostic = NativeFailure("Cannot configure session connection", GetLastError());
      Close(result);
      return kInvalid;
    }
    DWORD error = GetLastError();
    if (error != ERROR_PIPE_BUSY && error != ERROR_FILE_NOT_FOUND) {
      diagnostic = NativeFailure("Cannot connect to existing session", error);
      return kInvalid;
    }
#else
    Connection result = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (result < 0) { diagnostic = NativeFailure("Cannot create session connection", errno); return kInvalid; }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (endpoint.size() >= sizeof(address.sun_path)) { Close(result); return kInvalid; }
    std::copy(endpoint.begin(), endpoint.end(), address.sun_path);
    if (!connect(result, reinterpret_cast<sockaddr*>(&address), sizeof(address))) return result;
    const auto error = errno;
    Close(result);
    if (error != ECONNREFUSED && error != ENOENT && error != EAGAIN) {
      diagnostic = NativeFailure("Cannot connect to existing session", error); return kInvalid;
    }
#endif
    Pause();
  } while (Clock::now() < deadline);
  return kInvalid;
}
json Exchange(const json& record, const json& request, Clock::time_point deadline) {
  const std::string command = request.value("command", "request");
  const std::string name = record.value("session", "");
  std::string diagnostic;
  Connection connection = Connect(record.at("endpoint").get<std::string>(), deadline, diagnostic);
  if (connection == kInvalid) {
    if (!diagnostic.empty()) return Error(command, "io_error", diagnostic, {}, name);
    if (Clock::now() >= deadline)
      return Error(command, "timeout", "Session connection did not become available within the timeout", {}, name);
    return Error(command, "io_error", "Existing session could not be reached; retain its registry and retry the same session", {}, name);
  }
  json authenticated = request;
  authenticated["sessionNonce"] = record.at("nonce");
  std::string text;
  bool ok = WriteMessage(connection, authenticated.dump() + "\n", deadline) &&
            ReadMessage(connection, text, deadline);
  if (ok) WriteMessage(connection, "\n", deadline);
  Close(connection);
  if (!ok) return Error(command, "timeout", "Session did not respond within the timeout", {}, name);
  try {
    json response = json::parse(text);
    if (!response.is_object() || response.value("protocolVersion", 0) != 1 || !response.contains("ok"))
      throw std::runtime_error("Malformed response envelope");
    return response;
  } catch (const std::exception& error) {
    return Error(command, "io_error", std::string("Invalid session response: ") + error.what(), {}, name);
  }
}

std::vector<json> Records(const fs::path& root, bool includeUnreachable = false) {
  std::vector<json> records;
  for (const auto& entry : fs::directory_iterator(root)) {
    if (entry.path().extension() != ".json" || !entry.is_regular_file()) continue;
    try {
      std::ifstream input(entry.path());
      json record;
      input >> record;
      input.close();
      if (record.at("protocolVersion") != 1 || !record.at("session").is_string() ||
          !record.at("projectPath").is_string() || !record.at("endpoint").is_string() ||
          !record.at("nonce").is_string()) continue;
      const auto probe = ProbeProcess(record.at("pid").get<unsigned long>());
      if (ConfirmedStale(probe, record.at("processIdentity").get<std::string>())) {
        std::error_code ignored;
        fs::remove(entry.path(), ignored);
#ifndef _WIN32
        // Only unlink socket entries within our private registry directory.
        const fs::path endpoint = fs::u8path(record.at("endpoint").get<std::string>());
        if (endpoint.parent_path() == root && endpoint.extension() == ".sock") fs::remove(endpoint, ignored);
#endif
        continue;
      }
      auto ping = Exchange(record, {{"command", "__ping"}}, Deadline(100));
      const bool reachable = ping.value("ok", false) && ping.value("session", "") == record.at("session");
      if (reachable || includeUnreachable) {
        record["reachable"] = reachable;
        record["processStatus"] = probe.state == ProcessState::Alive ? "alive" : "unknown";
        if (!probe.diagnostic.empty()) record["processDiagnostic"] = probe.diagnostic;
        if (!reachable) record["connectionError"] = ping.value("error", json::object());
        records.push_back(record);
      }
    } catch (const std::exception&) { /* Ignore incomplete/unrelated registry entries. */ }
  }
  return records;
}
json PublicRecord(json record) {
  record.erase("nonce");
  record.erase("processIdentity");
  record.erase("protocolVersion");
  return record;
}

#ifdef _WIN32
std::wstring Quote(const std::wstring& argument) {
  std::wstring result = L"\"";
  size_t slashes = 0;
  for (wchar_t c : argument) {
    if (c == L'\\') { ++slashes; continue; }
    if (c == L'\"') result.append(slashes * 2 + 1, L'\\');
    else result.append(slashes, L'\\');
    slashes = 0;
    result.push_back(c);
  }
  result.append(slashes * 2, L'\\');
  result.push_back(L'\"');
  return result;
}
#endif
bool Spawn(const std::string& viewer, const std::string& session,
           const std::string& project, bool hidden, unsigned long& pid, std::string& identity, std::string& error, int evaluationTimeoutMs) {
#ifdef _WIN32
  std::wstring exe = Wide(viewer);
  std::wstring command = Quote(exe) + L" --agent-session " + Quote(Wide(session)) +
      L" --agent-project " + Quote(Wide(project));
  if(evaluationTimeoutMs>0)command+=L" --agent-evaluation-timeout "+std::to_wstring(evaluationTimeoutMs);
  if (hidden) command += L" --agent-hidden";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = hidden ? SW_HIDE : SW_SHOWNORMAL;
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE,
                       CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS, nullptr, nullptr, &startup, &process)) {
    error = "Cannot launch viewer (Windows error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  CloseHandle(process.hThread);
  pid = process.dwProcessId;
  FILETIME created{}, exited{}, kernel{}, user{};
  if (GetProcessTimes(process.hProcess, &created, &exited, &kernel, &user))
    identity = std::to_string((uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime);
  CloseHandle(process.hProcess);
  return true;
#else
  // Double fork detaches the persistent viewer from the short-lived CLI.
  int identityPipe[2];
  if (pipe2(identityPipe, O_CLOEXEC)) { error = "Cannot create launch identity pipe"; return false; }
  pid_t child = fork();
  if (child < 0) { close(identityPipe[0]); close(identityPipe[1]); error = "Cannot launch viewer"; return false; }
  if (child == 0) {
    close(identityPipe[0]);
    if (setsid() < 0) _exit(127);
    pid_t grandchild = fork();
    if (grandchild < 0) _exit(127);
    if (grandchild > 0) _exit(0);
    unsigned long identity = ProcessId();
    if (write(identityPipe[1], &identity, sizeof(identity)) != sizeof(identity)) _exit(127);
    close(identityPipe[1]);
    int null = open("/dev/null", O_RDWR);
    if (null >= 0) { dup2(null, 0); dup2(null, 1); dup2(null, 2); if (null > 2) close(null); }
    std::vector<std::string> args = {viewer, "--agent-session", session, "--agent-project", project};
    if(evaluationTimeoutMs>0){args.push_back("--agent-evaluation-timeout");args.push_back(std::to_string(evaluationTimeoutMs));}
    if (hidden) args.push_back("--agent-hidden");
    std::vector<char*> pointers;
    for (auto& arg : args) pointers.push_back(arg.data());
    pointers.push_back(nullptr);
    execv(viewer.c_str(), pointers.data());
    _exit(127);
  }
  close(identityPipe[1]);
  ssize_t received = read(identityPipe[0], &pid, sizeof(pid));
  close(identityPipe[0]);
  int status = 0;
  while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
  if (!WIFEXITED(status) || WEXITSTATUS(status) || received != sizeof(pid)) { error = "Cannot detach viewer process"; return false; }
  identity = ProbeProcess(pid).identity;
  return true;
#endif
}
}  // namespace

struct SessionServer::Impl {
  std::atomic<bool> stop{false};
  std::thread worker;
  json record;
  fs::path recordPath;
  Connection listener = kInvalid;
  std::function<json(const json&)> handler;

  void Handle(Connection connection) {
    std::string text;
    if (ReadMessage(connection, text, Deadline(1000), &stop)) {
      json response;
      int timeout = 10000;
      try {
        json request = json::parse(text);
        timeout = std::clamp(request.value("timeoutMs", 10000), 0, request.value("command","")=="events"?301000:300000);
        request["timeoutMs"] = timeout;
        std::string command = request.value("command", "request");
        if (request.value("sessionNonce", "") != record.at("nonce"))
          response = Error(command, "no_session", "Session identity does not match", {}, record.at("session"));
        else if (command == "__ping") response = Success(command, {}, record.at("session"));
        else response = handler(request);
      } catch (const std::exception& error) {
        response = Error("request", "io_error", error.what(), {}, record.at("session"));
      }
      if (WriteMessage(connection, response.dump() + "\n", Deadline(std::min(timeout + 100, 5000)))) {
        // DisconnectNamedPipe discards unread bytes. A bounded acknowledgement
        // ensures delivery without a blocking FlushFileBuffers on an untrusted client.
        std::string acknowledgement;
        // A stopping server must still drain a response it already wrote.
        // On Windows DisconnectNamedPipe discards unread bytes, so cancelling
        // this bounded acknowledgement wait can lose the final close events.
        ReadMessage(connection, acknowledgement, Deadline(1000));
      }
    }
#ifdef _WIN32
    DisconnectNamedPipe(connection);
#endif
    Close(connection);
  }

  void Serve() {
    std::vector<std::future<void>> clients;
    while (!stop.load()) {
      clients.erase(std::remove_if(clients.begin(), clients.end(), [](std::future<void>& client) {
        return client.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
      }), clients.end());
      if (clients.size() >= 16) { Pause(); continue; }
#ifdef _WIN32
      bool connected = ConnectNamedPipe(listener, nullptr) != 0;
      DWORD code = GetLastError();
      // In PIPE_NOWAIT mode a successful call makes a disconnected instance
      // available; ERROR_PIPE_CONNECTED acknowledges the actual client.
      if (connected || code != ERROR_PIPE_CONNECTED) { Pause(); continue; }
      Connection connection = listener;
      UserSecurity security;
      listener = CreateNamedPipeW(Wide(record.at("endpoint").get<std::string>()).c_str(), PIPE_ACCESS_DUPLEX,
          PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
          17, 65536, 65536, 0, &security.attributes);
      if (listener == kInvalid) {
        DisconnectNamedPipe(connection);
        listener = connection;
        Pause();
        continue;
      }
#else
      Connection connection = accept4(listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
      if (connection == kInvalid) { Pause(); continue; }
#endif
      clients.push_back(std::async(std::launch::async, [this, connection] { Handle(connection); }));
    }
  }
};

SessionServer::SessionServer() : impl_(std::make_unique<Impl>()) {}
SessionServer::~SessionServer() { Stop(); }

bool SessionServer::Start(const std::string& session, const std::string& projectPath,
                          std::function<json(const json&)> handler, std::string& error) {
  if (impl_->worker.joinable()) { error = "Session server is already running"; return false; }
  if (session.empty()) { error = "Session name must not be empty"; return false; }
  try {
    fs::path root = Root();
    fs::path recordPath = RecordPath(root, session);
    if (fs::exists(recordPath)) {
      std::ifstream input(recordPath);
      json previous;
      input >> previous;
      const auto probe = ProbeProcess(previous.at("pid").get<unsigned long>());
      if (!ConfirmedStale(probe, previous.at("processIdentity").get<std::string>()))
        throw std::runtime_error("Session name is already in use or its process cannot be inspected: " + session + ". " + probe.diagnostic);
    }
    std::string nonce = Nonce();
    std::string endpoint;
#ifdef _WIN32
    UserSecurity security;
    endpoint = "\\\\.\\pipe\\synthcad-" + Hash(security.identity) + "-" + nonce;
    impl_->listener = CreateNamedPipeW(Wide(endpoint).c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
        17, 65536, 65536, 0, &security.attributes);
    if (impl_->listener == kInvalid) throw std::runtime_error("Cannot create private session named pipe");
#else
    endpoint = (root / (nonce + ".sock")).u8string();
    sockaddr_un address{};
    if (endpoint.size() >= sizeof(address.sun_path)) throw std::runtime_error("Session socket path is too long");
    address.sun_family = AF_UNIX;
    std::copy(endpoint.begin(), endpoint.end(), address.sun_path);
    impl_->listener = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (impl_->listener < 0 || bind(impl_->listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ||
        chmod(endpoint.c_str(), 0600) || listen(impl_->listener, 16))
      throw std::runtime_error("Cannot create private session socket");
#endif
    impl_->record = {{"protocolVersion", 1}, {"session", session}, {"projectPath", Canonical(projectPath)},
        {"pid", ProcessId()}, {"processIdentity", ProbeProcess(ProcessId()).identity}, {"endpoint", endpoint}, {"nonce", nonce}};
    impl_->recordPath = recordPath;
    impl_->handler = std::move(handler);
    impl_->stop.store(false);
    impl_->worker = std::thread([this] { impl_->Serve(); });
    const fs::path temporary = root / (nonce + ".tmp");
    {
      std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
      output << impl_->record.dump();
      output.close();
      if (!output) throw std::runtime_error("Cannot write session registry");
    }
    std::error_code ignored;
    fs::remove(recordPath, ignored);
    fs::rename(temporary, recordPath);
    return true;
  } catch (const std::exception& failure) {
    error = failure.what();
    Stop();
    return false;
  }
}

void SessionServer::Stop() {
  impl_->stop.store(true);
  if (impl_->worker.joinable()) impl_->worker.join();
  if (impl_->listener != kInvalid) { Close(impl_->listener); impl_->listener = kInvalid; }
  std::error_code ignored;
  if (!impl_->recordPath.empty()) {
    // Do not remove a newer process's record if a session name has been reused.
    try {
      std::ifstream input(impl_->recordPath);
      json record;
      input >> record;
      input.close();
      if (record.value("nonce", "") == impl_->record.value("nonce", "")) fs::remove(impl_->recordPath, ignored);
    } catch (const std::exception&) {}
  }
#ifndef _WIN32
  if (impl_->record.contains("endpoint")) fs::remove(fs::u8path(impl_->record.at("endpoint").get<std::string>()), ignored);
#endif
  impl_->record = json::object();
  impl_->recordPath.clear();
}

json ListSessions() {
  try {
    json sessions = json::array();
    for (const auto& record : Records(Root(), true)) sessions.push_back(PublicRecord(record));
    return Success("sessions", {{"sessions", sessions}});
  } catch (const std::exception& error) { return Error("sessions", "io_error", error.what()); }
}

json Request(const std::string& session, const json& request, int timeoutMs) {
  std::string command = request.value("command", "request");
  try {
    // The CLI reserves up to 500 ms for response delivery after the viewer's
    // own deadline. Preserve that distinction rather than extending actions.
    const int maxRequestTimeout=command=="events"?301000:300000;
    if (timeoutMs < 0 || timeoutMs > maxRequestTimeout+500)
      return Error(command, "invalid_argument", "Transport timeout exceeds the command limit", {}, session);
    json timedRequest = request;
    if (!timedRequest.contains("timeoutMs")) timedRequest["timeoutMs"] = std::min(timeoutMs, maxRequestTimeout);
    if (!timedRequest["timeoutMs"].is_number_integer() || timedRequest["timeoutMs"].get<int64_t>() < 0 ||
        timedRequest["timeoutMs"].get<int64_t>() > maxRequestTimeout)
      return Error(command, "invalid_argument", "Request timeout exceeds the command limit", {}, session);
    const auto deadline = Deadline(timeoutMs);
    auto records = Records(Root(), true);
    if (records.empty()) return Error(command, "no_session", "No live session; run synthcad-cli open PATH first");
    json selected;
    if (session.empty()) {
      if (records.size() != 1) {
        json names = json::array();
        for (const auto& record : records) names.push_back(record.at("session"));
        return Error(command, "ambiguous_session", "Several sessions are running; use --session NAME", {{"sessions", names}});
      }
      selected = records.front();
    } else {
      for (const auto& record : records) if (record.at("session") == session) { selected = record; break; }
      if (selected.is_null()) return Error(command, "no_session", "No live session named " + session, {}, session);
    }
    return Exchange(selected, timedRequest, deadline);
  } catch (const std::exception& error) { return Error(command, "io_error", error.what(), {}, session); }
}

json OpenSession(const std::string& projectPath, const std::string& requestedName,
                 bool hidden, const std::string& viewerExe, int timeoutMs, int evaluationTimeoutMs) {
  try {
    if (timeoutMs < 0 || timeoutMs > 300000)
      return Error("open", "invalid_argument", "Open timeout must be from 0 to 300000 milliseconds");
    if (!fs::exists(fs::u8path(projectPath))) return Error("open", "not_found", "Project path does not exist: " + projectPath);
    const std::string project = Canonical(projectPath);
    const fs::path root = Root();
    const auto deadline = Deadline(timeoutMs);
    OpenLock lock(root, deadline);
    for (const auto& record : Records(root, true)) {
      if (record.at("projectPath") == project) {
        if (!record.value("reachable", false)) {
          const bool access = record.value("processStatus", "") == "unknown" ||
              record.value("connectionError", json::object()).value("code", "") == "io_error";
          return Error("open", access ? "io_error" : "busy",
                       "An existing project session cannot currently be reached. Keep its registry and retry in the same execution context with the required access.",
                       PublicRecord(record), record.at("session"));
        }
        if (!requestedName.empty() && record.at("session") != requestedName)
          return Error("open", "invalid_argument", "Project is already open as session " + record.at("session").get<std::string>(),
                       {{"existingSession", record.at("session")}});
        json data = PublicRecord(record);
        data["reused"] = true;
        return Success("open", data, record.at("session"));
      }
      if (!requestedName.empty() && record.at("session") == requestedName)
        return Error("open", "invalid_argument", "Session name belongs to another project: " + requestedName);
    }
    const std::string name = requestedName.empty() ? "project-" + Hash(project) : requestedName;
    const fs::path pendingPath = root / (Hash(project) + ".launch");
    bool alreadyLaunching = false;
    unsigned long launchedPid = 0;
    std::string launchedIdentity;
    if (fs::exists(pendingPath)) {
      try {
        std::ifstream input(pendingPath);
        json pending;
        input >> pending;
        launchedPid = pending.at("pid").get<unsigned long>();
        launchedIdentity = pending.at("processIdentity").get<std::string>();
        alreadyLaunching = !ConfirmedStale(ProbeProcess(launchedPid), launchedIdentity);
        if (alreadyLaunching && pending.at("session") != name)
          return Error("open", "busy", "Project is already opening as session " + pending.at("session").get<std::string>());
      } catch (const std::exception& failure) {
        return Error("open", "io_error", std::string("Cannot inspect pending viewer launch; retaining it to avoid a duplicate: ") + failure.what(), {}, name);
      }
    }
    std::string error;
    if (!alreadyLaunching) {
      if (!Spawn(viewerExe, name, project, hidden, launchedPid, launchedIdentity, error, evaluationTimeoutMs)) return Error("open", "io_error", error);
      std::ofstream output(pendingPath, std::ios::binary | std::ios::trunc);
      output << json({{"pid", launchedPid}, {"processIdentity", launchedIdentity}, {"session", name}}).dump();
      output.close();
      if (!output) return Error("open", "io_error", "Cannot record pending viewer launch");
    }
    do {
      for (const auto& record : Records(root)) {
        if (record.at("projectPath") == project && record.at("session") == name) {
          std::error_code ignored;
          fs::remove(pendingPath, ignored);
          json data = PublicRecord(record);
          data["reused"] = alreadyLaunching;
          return Success("open", data, name);
        }
      }
      if (ConfirmedStale(ProbeProcess(launchedPid), launchedIdentity)) {
        std::error_code ignored;
        fs::remove(pendingPath, ignored);
        return Error("open", "io_error", "Viewer exited before publishing its session", {}, name);
      }
      Pause();
    } while (Clock::now() < deadline);
    const auto probe = ProbeProcess(launchedPid);
    return Error("open", probe.state == ProcessState::Unknown ? "io_error" : "timeout",
                 "Viewer launch is still recorded; check sessions and retry the same project without changing its registry.",
                 {{"pid", launchedPid}, {"processDiagnostic", probe.diagnostic}}, name);
  } catch (const std::exception& error) {
    const bool timeout = std::string(error.what()).find("Timed out") != std::string::npos;
    return Error("open", timeout ? "timeout" : "io_error", error.what());
  }
}

}  // namespace synthcad
