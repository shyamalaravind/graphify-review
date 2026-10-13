// Open a markdown file in the browser for inline review; print the feedback when submitted.
// Build: c++ -std=c++17 -O2 -pthread -o bin/review review.cpp
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#else
#include <limits.h>
#endif

// ---------------------------------------------------------------- files

static bool read_file(const std::string &path, std::string &out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

static std::string dir_of(const std::string &path) {
  size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

static std::string base_of(const std::string &path) {
  size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Directory holding page.html and marked.min.js: next to the binary, or one level up
// (the launcher builds into <skill>/bin/ while the assets stay in <skill>/).
static std::string asset_dir() {
  std::string exe;
#ifdef __APPLE__
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> buf(size + 1, 0);
  if (_NSGetExecutablePath(buf.data(), &size) == 0) exe = buf.data();
#else
  std::vector<char> buf(PATH_MAX + 1, 0);
  ssize_t n = readlink("/proc/self/exe", buf.data(), PATH_MAX);
  if (n > 0) exe.assign(buf.data(), static_cast<size_t>(n));
#endif
  std::string here = dir_of(exe);
  std::string probe;
  if (read_file(here + "/page.html", probe)) return here;
  return dir_of(here);
}

// ---------------------------------------------------------------- json out

static void json_escape_into(const std::string &s, std::string &out) {
  static const char *hex = "0123456789abcdef";
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      // Escaped so the plan can never close the <script> element it is embedded in.
      case '<': out += "\\u003c"; break;
      default:
        if (c < 0x20) {
          out += "\\u00";
          out += hex[c >> 4];
          out += hex[c & 0xf];
        } else {
          out += static_cast<char>(c);
        }
    }
  }
}

static std::string json_string(const std::string &s) {
  std::string out = "\"";
  json_escape_into(s, out);
  out += '"';
  return out;
}

// ---------------------------------------------------------------- json in

struct JVal {
  enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
  bool boolean = false;
  double number = 0;
  std::string str;
  std::vector<JVal> arr;
  std::vector<std::pair<std::string, JVal>> obj;

  const JVal *get(const std::string &key) const {
    for (const auto &kv : obj)
      if (kv.first == key) return &kv.second;
    return nullptr;
  }
  bool flag(const std::string &key) const {
    const JVal *v = get(key);
    return v && v->type == Bool && v->boolean;
  }
  std::string text(const std::string &key) const {
    const JVal *v = get(key);
    return v && v->type == Str ? v->str : std::string();
  }
};

struct JParser {
  const std::string &s;
  size_t i = 0;
  bool ok = true;
  explicit JParser(const std::string &src) : s(src) {}

  void ws() {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
  }
  bool lit(const char *word) {
    size_t n = std::strlen(word);
    if (s.compare(i, n, word) != 0) return false;
    i += n;
    return true;
  }
  void utf8(unsigned cp, std::string &out) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }
  unsigned hex4() {
    unsigned v = 0;
    for (int k = 0; k < 4; k++) {
      if (i >= s.size()) { ok = false; return 0; }
      char c = s[i++];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
      else { ok = false; return 0; }
    }
    return v;
  }
  std::string string() {
    std::string out;
    if (i >= s.size() || s[i] != '"') { ok = false; return out; }
    i++;
    while (i < s.size()) {
      char c = s[i++];
      if (c == '"') return out;
      if (c != '\\') { out += c; continue; }
      if (i >= s.size()) break;
      char e = s[i++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          unsigned cp = hex4();
          if (!ok) return out;
          if (cp >= 0xD800 && cp <= 0xDBFF && s.compare(i, 2, "\\u") == 0) {
            size_t save = i;
            i += 2;
            unsigned lo = hex4();
            if (!ok) return out;
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else {
              i = save;  // not a low surrogate; leave it for the next round
            }
          }
          utf8(cp, out);
          break;
        }
        default: ok = false; return out;
      }
    }
    ok = false;
    return out;
  }
  JVal value() {
    JVal v;
    ws();
    if (i >= s.size()) { ok = false; return v; }
    char c = s[i];
    if (c == '{') {
      v.type = JVal::Obj;
      i++;
      ws();
      if (i < s.size() && s[i] == '}') { i++; return v; }
      while (ok) {
        ws();
        std::string key = string();
        if (!ok) break;
        ws();
        if (i >= s.size() || s[i] != ':') { ok = false; break; }
        i++;
        v.obj.emplace_back(key, value());
        if (!ok) break;
        ws();
        if (i < s.size() && s[i] == ',') { i++; continue; }
        if (i < s.size() && s[i] == '}') { i++; break; }
        ok = false;
      }
    } else if (c == '[') {
      v.type = JVal::Arr;
      i++;
      ws();
      if (i < s.size() && s[i] == ']') { i++; return v; }
      while (ok) {
        v.arr.push_back(value());
        if (!ok) break;
        ws();
        if (i < s.size() && s[i] == ',') { i++; continue; }
        if (i < s.size() && s[i] == ']') { i++; break; }
        ok = false;
      }
    } else if (c == '"') {
      v.type = JVal::Str;
      v.str = string();
    } else if (lit("true")) {
      v.type = JVal::Bool;
      v.boolean = true;
    } else if (lit("false")) {
      v.type = JVal::Bool;
    } else if (lit("null")) {
      v.type = JVal::Null;
    } else {
      size_t start = i;
      while (i < s.size() && (std::strchr("+-.eE", s[i]) || (s[i] >= '0' && s[i] <= '9'))) i++;
      if (i == start) { ok = false; return v; }
      v.type = JVal::Num;
      v.number = std::strtod(s.substr(start, i - start).c_str(), nullptr);
    }
    return v;
  }
};

static bool parse_json(const std::string &src, JVal &out) {
  JParser p(src);
  out = p.value();
  return p.ok;
}

// ---------------------------------------------------------------- feedback

static std::string one_line(const std::string &s) {
  std::istringstream in(s);
  std::ostringstream out;
  std::string word;
  bool first = true;
  while (in >> word) {
    if (!first) out << ' ';
    out << word;
    first = false;
  }
  return out.str();
}

static std::vector<std::string> lines_of(const std::string &s) {
  std::vector<std::string> lines;
  std::string line;
  std::istringstream in(s);
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(line);
  }
  return lines;
}

static std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

static std::string render(const std::string &name, const JVal &r) {
  if (r.flag("ended"))
    return "ENDED: review of " + name +
           " — the user closed the review without feedback and left no instructions.";

  std::vector<std::pair<std::string, std::string>> comments;
  const JVal *list = r.get("comments");
  if (list && list->type == JVal::Arr)
    for (const JVal &c : list->arr)
      if (c.type == JVal::Obj) comments.emplace_back(c.text("quote"), c.text("note"));
  std::string general = trim(r.text("general"));
  bool approved = r.flag("approved");

  if (approved && comments.empty() && general.empty())
    return "APPROVED: " + name + " — no changes requested.";

  std::ostringstream out;
  out << (approved ? "APPROVED WITH NOTES" : "CHANGES REQUESTED") << ": review of " << name;
  int n = 0;
  for (const auto &c : comments) {
    out << "\n\n" << ++n << ". On: \"" << one_line(c.first) << "\"";
    for (const std::string &line : lines_of(c.second)) out << "\n   " << line;
  }
  if (!general.empty()) out << "\n\nGeneral:\n" << general;
  return out.str();
}

// ---------------------------------------------------------------- http

static std::atomic<bool> g_done{false};
static std::mutex g_mutex;
static std::condition_variable g_cv;
static JVal g_result;
// Set when the page goes away without feedback. Held briefly before it counts, so a
// reload — which re-fetches the page and clears it — doesn't end the review.
static bool g_leaving = false;
static const auto kLeaveGrace = std::chrono::milliseconds(2000);

static void send_all(int fd, const std::string &data) {
  size_t sent = 0;
  while (sent < data.size()) {
    ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
    if (n <= 0) return;
    sent += static_cast<size_t>(n);
  }
}

static void reply(int fd, const std::string &status, const std::string &ctype,
                  const std::string &body) {
  std::ostringstream head;
  head << "HTTP/1.1 " << status << "\r\n"
       << "Content-Type: " << ctype << "\r\n"
       << "Content-Length: " << body.size() << "\r\n"
       << "Cache-Control: no-store\r\n"
       << "Connection: close\r\n\r\n";
  send_all(fd, head.str() + body);
}

// Reads until `want` bytes are buffered, or the peer hangs up.
static bool fill(int fd, std::string &buf, size_t want) {
  char chunk[8192];
  while (buf.size() < want) {
    ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
    if (n <= 0) return false;
    buf.append(chunk, static_cast<size_t>(n));
  }
  return true;
}

static void finish(const JVal &result) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_done.exchange(true)) return;  // first submission wins
    g_result = result;
  }
  g_cv.notify_one();
}

static void set_leaving(bool leaving) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_leaving == leaving) return;
    g_leaving = leaving;
  }
  g_cv.notify_one();
}

static void serve(int fd, std::string base, std::string page) {
  std::string buf;
  size_t head_end;
  char chunk[8192];
  while ((head_end = buf.find("\r\n\r\n")) == std::string::npos) {
    ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
    if (n <= 0) { ::close(fd); return; }
    buf.append(chunk, static_cast<size_t>(n));
    if (buf.size() > (1u << 20)) { ::close(fd); return; }
  }

  std::string head = buf.substr(0, head_end);
  std::string first = head.substr(0, head.find("\r\n"));
  std::istringstream rl(first);
  std::string method, path;
  rl >> method >> path;

  if (method == "GET" && path == base) {
    set_leaving(false);  // the page is back (a reload) — cancel any pending end
    reply(fd, "200 OK", "text/html; charset=utf-8", page);
    ::close(fd);
    return;
  }

  if (method == "POST" && path == base + "submit") {
    std::string lower = head;
    for (char &c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    size_t len = 0;
    size_t at = lower.find("\r\ncontent-length:");
    if (at != std::string::npos)
      len = std::strtoul(head.c_str() + at + std::strlen("\r\ncontent-length:"), nullptr, 10);
    if (len > (16u << 20)) { ::close(fd); return; }
    std::string body = buf.substr(head_end + 4);
    if (body.size() < len && !fill(fd, body, len)) { ::close(fd); return; }
    body.resize(len);

    JVal parsed;
    if (!parse_json(body, parsed) || parsed.type != JVal::Obj) {
      reply(fd, "400 Bad Request", "application/json", "{}");
      ::close(fd);
      return;
    }
    reply(fd, "200 OK", "application/json", "{}");
    ::close(fd);
    // A tab closing reports an end that a reload may still take back; the End review
    // button marks it immediate, and feedback always counts right away.
    if (parsed.flag("ended") && !parsed.flag("immediate"))
      set_leaving(true);
    else
      finish(parsed);
    return;
  }

  reply(fd, "404 Not Found", "text/plain; charset=utf-8", "not found");
  ::close(fd);
}

// ---------------------------------------------------------------- main

static std::string token(size_t chars) {
  static const char *alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::random_device rd;
  std::string out;
  for (size_t k = 0; k < chars; k++) out += alphabet[rd() & 63];
  return out;
}

static void replace_first(std::string &haystack, const std::string &needle,
                          const std::string &sub) {
  size_t at = haystack.find(needle);
  if (at != std::string::npos) haystack.replace(at, needle.size(), sub);
}

static std::string shell_quote(const std::string &s) {
  std::string out = "'";
  for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return out + "'";
}

// PLAN_REVIEW_BROWSER picks the browser: an app name on macOS ("Brave Browser"), a
// command on Linux ("brave-browser"). Unset, or not found, the default browser opens.
static bool open_browser(const std::string &url) {
  std::string target = shell_quote(url) + " >/dev/null 2>&1";
  const char *browser = std::getenv("PLAN_REVIEW_BROWSER");
#ifdef __APPLE__
  if (browser && *browser) {
    if (std::system(("open -a " + shell_quote(browser) + " " + target).c_str()) == 0) return true;
    std::fprintf(stderr, "review: can't open %s; using the default browser\n", browser);
  }
  return std::system(("open " + target).c_str()) == 0;
#else
  if (browser && *browser) {
    std::string cmd = shell_quote(browser);
    if (std::system(("command -v " + cmd + " >/dev/null 2>&1").c_str()) == 0)
      // Backgrounded: a browser that wasn't already running stays in the foreground.
      return std::system((cmd + " " + target + " &").c_str()) == 0;
    std::fprintf(stderr, "review: can't find %s; using the default browser\n", browser);
  }
  return std::system(("xdg-open " + target).c_str()) == 0;
#endif
}

int main(int argc, char **argv) {
  // --title names the browser tab and the page header; without it they show the file's name.
  std::string file, title;
  bool usage_ok = true;
  for (int k = 1; k < argc && usage_ok; k++) {
    std::string arg = argv[k];
    if (arg == "--title" && k + 1 < argc)
      title = argv[++k];
    else if (file.empty() && arg.rfind("--", 0) != 0)
      file = arg;
    else
      usage_ok = false;
  }
  if (!usage_ok || file.empty()) {
    std::fprintf(stderr, "usage: review <file.md> [--title <tab title>]\n");
    return 2;
  }

  std::string plan;
  if (!read_file(file, plan)) {
    std::fprintf(stderr, "review: cannot read %s\n", file.c_str());
    return 1;
  }
  std::string name = base_of(file);

  std::string assets = asset_dir();
  std::string page, marked;
  if (!read_file(assets + "/page.html", page) ||
      !read_file(assets + "/marked.min.js", marked)) {
    std::fprintf(stderr, "review: page.html or marked.min.js not found in %s\n", assets.c_str());
    return 1;
  }
  replace_first(page, "/*MARKED*/", marked);
  replace_first(page, "/*PLAN*/",
                "{\"name\":" + json_string(name) + ",\"title\":" + json_string(title) +
                    ",\"text\":" + json_string(plan) + "}");

  int server = ::socket(AF_INET, SOCK_STREAM, 0);
  if (server < 0) {
    std::perror("review: socket");
    return 1;
  }
  int on = 1;
  ::setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (::bind(server, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0 ||
      ::listen(server, 16) < 0) {
    std::perror("review: bind");
    return 1;
  }
  socklen_t alen = sizeof(addr);
  ::getsockname(server, reinterpret_cast<sockaddr *>(&addr), &alen);
  int port = ntohs(addr.sin_port);

  // Random path prefix so other local pages can't read the plan or post fake feedback.
  std::string base = "/" + token(22) + "/";
  std::string url = "http://127.0.0.1:" + std::to_string(port) + base;

  std::thread([server, base, page]() {
    for (;;) {
      int client = ::accept(server, nullptr, nullptr);
      if (client < 0) {
        if (errno == EINTR) continue;
        return;
      }
      std::thread(serve, client, base, page).detach();
    }
  }).detach();

  std::fprintf(stderr, "Review open at %s\n", url.c_str());
  std::fflush(stderr);

  if (!open_browser(url))
    std::fprintf(stderr, "review: open the URL above in your browser\n");

  std::unique_lock<std::mutex> lock(g_mutex);
  while (!g_done.load()) {
    g_cv.wait(lock, [] { return g_done.load() || g_leaving; });
    if (g_done.load()) break;
    // The page went away with no feedback. End the review unless it comes back.
    if (!g_cv.wait_for(lock, kLeaveGrace, [] { return g_done.load() || !g_leaving; })) {
      JVal ended, yes;
      ended.type = JVal::Obj;
      yes.type = JVal::Bool;
      yes.boolean = true;
      ended.obj.emplace_back("ended", yes);
      g_result = ended;
      g_done = true;
    }
  }
  JVal result = g_result;
  lock.unlock();

  std::printf("%s\n", render(name, result).c_str());
  std::fflush(stdout);
  return 0;
}
