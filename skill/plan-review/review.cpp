// The review broker: holds one round of a plan's review, serves it to a viewer (the
// browser page, or an editor's), and prints the feedback when it comes back.
// Build: c++ -std=c++17 -O2 -pthread -o bin/review review.cpp
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cerrno>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
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

// ---------------------------------------------------------------- feedback text

static std::string number_of(const JVal &obj, const std::string &key) {
  const JVal *v = obj.get(key);
  return v && v->type == JVal::Num ? std::to_string(static_cast<long>(v->number)) : "";
}

// What a comment is about, the way the agent reads it. A viewer anchors each comment to
// prose it quotes, to a diagram (and the node in it), or to lines of code.
static std::string where(const JVal &anchor) {
  std::string kind = anchor.text("kind");
  std::string quote = one_line(anchor.text("quote"));
  std::string quoted = quote.empty() ? "" : " \"" + quote + "\"";
  if (kind == "diagram") {
    std::string n = number_of(anchor, "diagram");
    std::string diagram = "On diagram" + (n.empty() ? "" : " " + n);
    std::string node = one_line(anchor.text("node"));
    return node.empty() ? diagram + ":" + quoted : diagram + ", node \"" + node + "\":";
  }
  if (kind == "code") {
    std::string start = number_of(anchor, "start"), end = number_of(anchor, "end");
    std::string at = anchor.text("file");
    if (!start.empty()) at += ":" + start + (!end.empty() && end != start ? "-" + end : "");
    return "On " + at + ":" + quoted;
  }
  return "On:" + quoted;
}

static std::string render(const std::string &name, const JVal &r) {
  std::string verdict = r.text("verdict");
  if (verdict == "end")
    return "ENDED: review of " + name +
           " — the user closed the review without feedback and left no instructions.";

  std::vector<std::pair<std::string, std::string>> comments;  // where, note
  const JVal *list = r.get("comments");
  if (list && list->type == JVal::Arr)
    for (const JVal &c : list->arr) {
      if (c.type != JVal::Obj) continue;
      const JVal *anchor = c.get("anchor");
      comments.emplace_back(anchor && anchor->type == JVal::Obj ? where(*anchor) : "On:",
                            c.text("note"));
    }
  std::string general = trim(r.text("general"));
  bool approved = verdict == "approve";

  if (approved && comments.empty() && general.empty())
    return "APPROVED: " + name + " — no changes requested.";

  std::ostringstream out;
  out << (approved ? "APPROVED WITH NOTES" : "CHANGES REQUESTED") << ": review of " << name;
  int n = 0;
  for (const auto &c : comments) {
    out << "\n\n" << ++n << ". " << c.first;
    for (const std::string &line : lines_of(c.second)) out << "\n   " << line;
  }
  if (!general.empty()) out << "\n\nGeneral:\n" << general;
  return out.str();
}

// ---------------------------------------------------------------- shell

static std::string shell_quote(const std::string &s) {
  std::string out = "'";
  for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return out + "'";
}

// Runs a shell command and returns its stdout, or "" if it fails.
static std::string capture(const std::string &cmd) {
  std::string out;
  FILE *p = ::popen(cmd.c_str(), "r");
  if (!p) return out;
  char chunk[65536];
  size_t n;
  while ((n = std::fread(chunk, 1, sizeof(chunk), p)) > 0) out.append(chunk, n);
  if (::pclose(p) != 0) out.clear();
  return out;
}

static std::string real_path(const std::string &path) {
  char *resolved = ::realpath(path.c_str(), nullptr);
  std::string out = resolved ? resolved : "";
  std::free(resolved);
  return out;
}

// --root if given. Otherwise the top of the git repository the review runs in, or the
// directory itself outside one: the code the plan is about, which an editor viewer opens.
static std::string find_root(const std::string &given) {
  if (!given.empty()) return real_path(given);
  std::string top = trim(capture("git rev-parse --show-toplevel 2>/dev/null"));
  return top.empty() ? real_path(".") : top;
}

// ---------------------------------------------------------------- sessions

// A waiting review is announced in $XDG_STATE_HOME/plan-review/sessions/<key>.json, so a
// viewer that is already running can find it. The file goes when the round ends.
static std::string sessions_dir() {
  const char *state = std::getenv("XDG_STATE_HOME");
  const char *home = std::getenv("HOME");
  std::string base = state && *state ? std::string(state)
                                     : std::string(home ? home : "/tmp") + "/.local/state";
  return base + "/plan-review/sessions";
}

// The plan's name without .md, in characters that are safe in a file name and a URI.
static std::string session_key(const std::string &name) {
  std::string stem = name.size() > 3 && name.compare(name.size() - 3, 3, ".md") == 0
                         ? name.substr(0, name.size() - 3)
                         : name;
  std::string key;
  for (char c : stem) key += std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' ? c : '-';
  return key.empty() ? "plan" : key;
}

// Written beside the target and renamed over it, so a viewer never reads half a file.
static bool write_file(const std::string &path, const std::string &text) {
  std::string tmp = path + ".tmp" + std::to_string(::getpid());
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f || !(f << text)) return false;
  }
  return std::rename(tmp.c_str(), path.c_str()) == 0;
}

// The same plan coming back within a day is the next round of its session.
static int next_round(const std::string &dir, const std::string &key) {
  std::string path = dir + "/" + key + ".round", text;
  struct stat st;
  int round = 0;
  if (::stat(path.c_str(), &st) == 0 && std::time(nullptr) - st.st_mtime < 24 * 60 * 60 &&
      read_file(path, text))
    round = std::atoi(text.c_str());
  write_file(path, std::to_string(round + 1) + "\n");
  return round + 1;
}

// The session file's path, kept where a signal handler can reach it without allocating.
static char g_session_path[4096];

static void remove_session(int) {
  if (g_session_path[0]) ::unlink(g_session_path);
}

static void on_signal(int sig) {
  remove_session(sig);
  ::_exit(128 + sig);
}

// ---------------------------------------------------------------- http

static std::atomic<bool> g_done{false};
static std::mutex g_mutex;
static std::condition_variable g_cv;
static JVal g_result;
// Set when the viewer goes away without feedback. Held briefly before it counts, so a
// reload — which fetches the page or the session again and clears it — doesn't end it.
static bool g_leaving = false;
static const auto kLeaveGrace = std::chrono::milliseconds(2000);
static std::string g_assets;  // page.html and the scripts it loads

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

// The session protocol, under a random path prefix so other local pages can't read the
// plan or post fake feedback:
//   GET  <base>                the browser viewer
//   GET  <base>session         {key, name, title, text, root, round}
//   GET  <base>mermaid.min.js  for a viewer whose plan has diagrams
//   POST <base>feedback        {verdict: approve | changes | end, leaving?, general,
//                               comments: [{note, anchor: {kind, ...}}]}
static void serve(int fd, std::string base, std::string page, std::string session) {
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

  if (method == "GET" && (path == base || path == base + "session")) {
    set_leaving(false);  // the viewer is back (a reload): cancel any pending end
    if (path == base)
      reply(fd, "200 OK", "text/html; charset=utf-8", page);
    else
      reply(fd, "200 OK", "application/json; charset=utf-8", session);
    ::close(fd);
    return;
  }

  // Mermaid is too big to build into every page; a viewer with diagrams asks for it.
  if (method == "GET" && path == base + "mermaid.min.js") {
    std::string js;
    if (read_file(g_assets + "/mermaid.min.js", js))
      reply(fd, "200 OK", "text/javascript; charset=utf-8", js);
    else
      reply(fd, "404 Not Found", "text/plain; charset=utf-8", "mermaid.min.js not found");
    ::close(fd);
    return;
  }

  if (method == "POST" && path == base + "feedback") {
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
    std::string verdict;
    if (!parse_json(body, parsed) || parsed.type != JVal::Obj ||
        ((verdict = parsed.text("verdict")) != "approve" && verdict != "changes" &&
         verdict != "end")) {
      reply(fd, "400 Bad Request", "application/json", "{}");
      ::close(fd);
      return;
    }
    reply(fd, "200 OK", "application/json", "{}");
    ::close(fd);
    // A viewer going away reports an end that a reload may still take back; ending on
    // purpose, and feedback, count right away.
    if (verdict == "end" && parsed.flag("leaving"))
      set_leaving(true);
    else
      finish(parsed);
    return;
  }

  reply(fd, "404 Not Found", "text/plain; charset=utf-8", "not found");
  ::close(fd);
}

// ---------------------------------------------------------------- viewers

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

// VS Code's language servers only answer for a folder it has open, so its window for the
// root comes first (focused, or opened); the session then goes to that window by URI.
static bool open_vscode(const std::string &root, const std::string &key) {
  std::string uri = "vscode://shyamalaravind.plan-viewer/session?key=" + key;
  if (std::system("command -v code >/dev/null 2>&1") != 0 ||
      std::system(("code " + shell_quote(root) + " >/dev/null 2>&1").c_str()) != 0)
    return false;
#ifdef __APPLE__
  return std::system(("open " + shell_quote(uri) + " >/dev/null 2>&1").c_str()) == 0;
#else
  return std::system(("xdg-open " + shell_quote(uri) + " >/dev/null 2>&1").c_str()) == 0;
#endif
}

// PLAN_REVIEW_VIEWER picks where the review opens: "browser" (the default) or "vscode".
// A viewer that is already running can also pick the session up from its file.
static void launch_viewer(const std::string &url, const std::string &root, const std::string &key) {
  const char *viewer = std::getenv("PLAN_REVIEW_VIEWER");
  std::string want = viewer ? viewer : "";
  if (want == "vscode") {
    if (open_vscode(root, key)) return;
    std::fprintf(stderr, "review: can't open VS Code; using the browser\n");
  } else if (!want.empty() && want != "browser") {
    std::fprintf(stderr, "review: unknown PLAN_REVIEW_VIEWER %s; using the browser\n", viewer);
  }
  if (!open_browser(url))
    std::fprintf(stderr, "review: open the URL above in your browser\n");
}

// ---------------------------------------------------------------- main

int main(int argc, char **argv) {
  // --title names the review; without it, the file's name does.
  // --root is the code the plan is about; without it, the git repository it runs in.
  std::string file, title, root;
  bool usage_ok = true;
  for (int k = 1; k < argc && usage_ok; k++) {
    std::string arg = argv[k];
    if (arg == "--title" && k + 1 < argc)
      title = argv[++k];
    else if (arg == "--root" && k + 1 < argc)
      root = argv[++k];
    else if (file.empty() && arg.rfind("--", 0) != 0)
      file = arg;
    else
      usage_ok = false;
  }
  if (!usage_ok || file.empty()) {
    std::fprintf(stderr, "usage: review <file.md> [--title <title>] [--root <dir>]\n");
    return 2;
  }

  std::string plan;
  if (!read_file(file, plan)) {
    std::fprintf(stderr, "review: cannot read %s\n", file.c_str());
    return 1;
  }
  std::string name = base_of(file), key = session_key(name);
  std::string code_root = find_root(root);
  if (code_root.empty()) {
    std::fprintf(stderr, "review: no such directory %s\n", root.c_str());
    return 2;
  }

  g_assets = real_path(asset_dir());
  std::string page, marked;
  if (!read_file(g_assets + "/page.html", page) ||
      !read_file(g_assets + "/marked.min.js", marked)) {
    std::fprintf(stderr, "review: page.html or marked.min.js not found in %s\n", g_assets.c_str());
    return 1;
  }
  replace_first(page, "/*MARKED*/", marked);

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

  std::string base = "/" + token(22) + "/";
  std::string url = "http://127.0.0.1:" + std::to_string(port) + base;

  std::string dir = sessions_dir();
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  int round = next_round(dir, key);
  std::string session = "{\"key\":" + json_string(key) + ",\"name\":" + json_string(name) +
                        ",\"title\":" + json_string(title) + ",\"text\":" + json_string(plan) +
                        ",\"root\":" + json_string(code_root) +
                        ",\"round\":" + std::to_string(round) + "}";
  // What a viewer needs to attach: where the broker is, and where the viewer files are.
  std::string announce = "{\"key\":" + json_string(key) + ",\"name\":" + json_string(name) +
                         ",\"title\":" + json_string(title) + ",\"url\":" + json_string(url) +
                         ",\"root\":" + json_string(code_root) +
                         ",\"assets\":" + json_string(g_assets) +
                         ",\"round\":" + std::to_string(round) +
                         ",\"pid\":" + std::to_string(::getpid()) + "}\n";
  std::string session_path = dir + "/" + key + ".json";
  if (!ec && session_path.size() < sizeof(g_session_path) && write_file(session_path, announce)) {
    std::strcpy(g_session_path, session_path.c_str());
    for (int sig : {SIGINT, SIGTERM, SIGHUP}) std::signal(sig, on_signal);
  } else {
    std::fprintf(stderr, "review: can't write %s; only the launched viewer will find it\n",
                 session_path.c_str());
  }

  std::thread([server, base, page, session]() {
    for (;;) {
      int client = ::accept(server, nullptr, nullptr);
      if (client < 0) {
        if (errno == EINTR) continue;
        return;
      }
      std::thread(serve, client, base, page, session).detach();
    }
  }).detach();

  std::fprintf(stderr, "Review open at %s\n", url.c_str());
  std::fflush(stderr);
  launch_viewer(url, code_root, key);

  std::unique_lock<std::mutex> lock(g_mutex);
  while (!g_done.load()) {
    g_cv.wait(lock, [] { return g_done.load() || g_leaving; });
    if (g_done.load()) break;
    // The viewer went away with no feedback. End the review unless it comes back.
    if (!g_cv.wait_for(lock, kLeaveGrace, [] { return g_done.load() || !g_leaving; })) {
      JVal ended, end;
      ended.type = JVal::Obj;
      end.type = JVal::Str;
      end.str = "end";
      ended.obj.emplace_back("verdict", end);
      g_result = ended;
      g_done = true;
    }
  }
  JVal result = g_result;
  lock.unlock();

  // Unless the plan's next round has already announced itself in its place.
  std::string current;
  if (g_session_path[0] && read_file(g_session_path, current) && current.find(url) != std::string::npos)
    remove_session(0);
  std::printf("%s\n", render(name, result).c_str());
  std::fflush(stdout);
  return 0;
}
