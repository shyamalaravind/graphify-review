// The review broker: holds one round of a plan's review, serves it to a viewer (the
// browser page, or an editor's), and prints the feedback when it comes back.
// Build: c++ -std=c++17 -O2 -pthread -o bin/review review.cpp
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
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
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <regex>
#include <sstream>
#include <string_view>
#include <string>
#include <thread>
#include <unordered_map>
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
// directory itself outside one: the code the plan is about, where its names are looked up.
static std::string find_root(const std::string &given) {
  if (!given.empty()) return real_path(given);
  std::string top = trim(capture("git rev-parse --show-toplevel 2>/dev/null"));
  return top.empty() ? real_path(".") : top;
}

// ---------------------------------------------------------------- sessions

// $XDG_STATE_HOME/plan-review, where the sessions are.
static std::string state_dir() {
  const char *state = std::getenv("XDG_STATE_HOME");
  const char *home = std::getenv("HOME");
  std::string base = state && *state ? std::string(state)
                                     : std::string(home ? home : "/tmp") + "/.local/state";
  return base + "/plan-review";
}

// A waiting review is announced in $XDG_STATE_HOME/plan-review/sessions/<key>.json, so a
// viewer that is already running can find it. The file goes when the round ends.
static std::string sessions_dir() { return state_dir() + "/sessions"; }

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
// Readable by the user alone: a session file holds the review's URL, whose token guards it.
static bool write_file(const std::string &path, const std::string &text) {
  std::string tmp = path + ".tmp" + std::to_string(::getpid());
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f || !(f << text)) return false;
  }
  ::chmod(tmp.c_str(), 0600);
  return std::rename(tmp.c_str(), path.c_str()) == 0;
}

// <key>.round holds the session's round, and once that round has ended, its verdict on a
// second line, so a viewer can tell a finished review from one coming back revised.
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

// ---------------------------------------------------------------- code lookup

// In the browser the broker answers the page's questions about names itself, from the
// code's text: where a name is defined, the lines there for a hover, and opening them in
// the user's editor. No language server runs, so a hover has no types, but nothing starts.

static std::string g_root;  // the code the plan is about

struct Place {
  std::string file;  // relative to g_root
  int line = 0;      // from 1; 0 for a whole file
  int end = 0;       // the last line of a range the plan gave
};

// A name as the page asks about it: `file: name()`, `name()`, or `file:12-20`.
struct NameKey {
  std::string file, name;
  int line = 0, end = 0;
  std::string id() const {
    return file + '\x1f' + name + '\x1f' + std::to_string(line) + '\x1f' + std::to_string(end);
  }
};

static NameKey key_of(const JVal &v) {
  NameKey k;
  k.file = v.text("file");
  k.name = v.text("name");
  if (const JVal *l = v.get("line")) if (l->type == JVal::Num) k.line = static_cast<int>(l->number);
  if (const JVal *e = v.get("end")) if (e->type == JVal::Num) k.end = static_cast<int>(e->number);
  return k;
}

static std::mutex g_lookup_mutex;  // guards everything below
static std::vector<std::string> g_files;
static bool g_files_listed = false;
static std::map<std::string, std::optional<Place>> g_places;  // name key → where, if anywhere

static bool on_path(const std::string &cmd) {
  return std::system(("command -v " + cmd + " >/dev/null 2>&1").c_str()) == 0;
}

// The root's files: git's, tracked or not ignored, or else a walk that skips hidden,
// dependency and build folders.
static const std::vector<std::string> &root_files() {
  if (g_files_listed) return g_files;
  g_files_listed = true;
  std::istringstream git(capture("git -C " + shell_quote(g_root) +
                                 " ls-files -co --exclude-standard 2>/dev/null"));
  for (std::string line; std::getline(git, line);)
    if (!line.empty()) g_files.push_back(line);
  if (!g_files.empty()) return g_files;  // from git
  namespace fs = std::filesystem;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(g_root, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::recursive_directory_iterator() && g_files.size() < 50000; it.increment(ec)) {
    std::string name = it->path().filename().string();
    if (it->is_directory(ec)) {
      static const char *skip[] = {"node_modules", "dist", "build", "out", "bin", "obj", "target", "vendor"};
      if (name[0] == '.' || std::find(std::begin(skip), std::end(skip), name) != std::end(skip))
        it.disable_recursion_pending();
    } else if (it->is_regular_file(ec)) {
      g_files.push_back(fs::relative(it->path(), g_root, ec).string());
    }
  }
  return g_files;
}

// The files a plan's file name can mean: the path from the root, or else every path that
// ends in it, shortest first.
static std::vector<std::string> find_files(std::string given) {
  if (!given.empty() && given[0] == '/') {
    if (given.compare(0, g_root.size() + 1, g_root + "/") != 0) return {};
    given = given.substr(g_root.size() + 1);
  }
  if (given.empty() || given.find("..") != std::string::npos) return {};
  struct stat st;
  if (::stat((g_root + "/" + given).c_str(), &st) == 0 && S_ISREG(st.st_mode)) return {given};
  std::vector<std::string> found;
  std::string tail = "/" + given;
  for (const auto &f : root_files())
    if (f.size() > tail.size() && f.compare(f.size() - tail.size(), tail.size(), tail) == 0) found.push_back(f);
  std::stable_sort(found.begin(), found.end(), [](const std::string &a, const std::string &b) { return a.size() < b.size(); });
  return found;
}

static std::string find_file(const std::string &given) {
  std::vector<std::string> found = find_files(given);
  return found.empty() ? "" : found[0];
}

static std::vector<std::string> file_lines(const std::string &file) {
  std::string text;
  std::vector<std::string> lines;
  if (!read_file(g_root + "/" + file, text) || text.size() > (4u << 20)) return lines;
  std::istringstream in(text);
  for (std::string line; std::getline(in, line);) lines.push_back(line);
  return lines;
}

// Where `id` first stands on a line as a whole word, or npos.
static size_t word_at(const std::string &line, const std::string &id) {
  auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$'; };
  for (size_t at = line.find(id); at != std::string::npos; at = line.find(id, at + 1))
    if ((at == 0 || !word(line[at - 1])) && (at + id.size() >= line.size() || !word(line[at + id.size()])))
      return at;
  return std::string::npos;
}

// A line that defines a name, and how surely: 0 a declaration keyword (function, class,
// def, fn…, or a variable given a function), 1 a signature whose body opens on the line or
// whose parameters go on below, 2 a variable or field given anything else.
struct Shapes {
  std::regex keyword, variable, assigned, field;
};

static const Shapes &shapes_for(const std::string &id) {
  static std::map<std::string, Shapes> made;
  auto found = made.find(id);
  if (found != made.end()) return found->second;
  std::string q;
  for (char c : id) q += std::isalnum(static_cast<unsigned char>(c)) || c == '_' ? std::string(1, c) : std::string("\\") + c;
  const std::string after = "(?![\\w$])";
  Shapes s{
      std::regex("\\b(?:function\\*?|class|interface|type|enum|struct|union|trait|impl|def|fn|func|fun|"
                 "sub|module|namespace|record|object|macro)\\s+(?:\\([^)]*\\)\\s*)?" + q + after),
      std::regex("\\b(?:const|let|var|val)\\s+" + q + after + "(\\s*(?::[^=]*)?=\\s*(?:async\\s*)?"
                 "(?:function\\b|\\(|<|[\\w$]+\\s*=>))?"),
      std::regex("^\\s*(?:(?:export|static|readonly|private|public|protected|async)\\s+)*" + q +
                 "\\s*[:=]\\s*(?:async\\s*)?(?:function\\b|\\(|[\\w$]+\\s*=>)"),
      // A class field or interface member: after a modifier, or typed and ended by `;`.
      std::regex("^\\s*(?:@[\\w$.]+(?:\\([^)]*\\))?\\s+)*(?:(?:(?:public|private|protected|readonly|static|declare|"
                 "override|abstract)\\s+)+" + q + "\\s*[?!]?\\s*[:=]|" + q + "\\s*[?!]?\\s*:[^=;]+;\\s*$)"),
  };
  return made.emplace(id, std::move(s)).first->second;
}

// What may follow a definition's parameters on its line: its body, a return type, or a
// qualifier. A call goes on with `.`, `,`, `)` or `;` instead.
static const std::regex kAfterParams("^\\s*(?:\\{|:|->|(?:const|override|final|noexcept|mutable|throws|where)\\b)");

// Words after which a name is used, not defined: `return foo(`, `new Foo(`, `if foo`.
// Not `void`: JavaScript's operator is rare, and C, C++, Java and C# define with it.
static bool control_word(const std::string &w) {
  static const char *words[] = {"return", "await", "new", "throw", "yield", "typeof", "delete", "case", "else",
                                "if", "while", "for", "switch", "do", "in", "of", "and", "or", "not", "is", "as"};
  return std::any_of(std::begin(words), std::end(words), [&](const char *x) { return w == x; });
}

static int definition_rank(const std::string &line, const std::string &id) {
  size_t at = word_at(line, id);
  if (at == std::string::npos) return -1;
  // Most lines that mention a name only use it, so cheap checks come first. Before a
  // definition's name comes nothing, or a keyword, a modifier or a type: never `.`, `(`
  // or `=`, and no `return` or `new`.
  size_t b = at ? line.find_last_not_of(" \t", at - 1) : std::string::npos;
  char prev = b == std::string::npos ? 0 : line[b];
  bool word_before = prev && (std::isalnum(static_cast<unsigned char>(prev)) || prev == '_');
  if (prev && !word_before && !std::strchr("$)>*&:]@", prev)) return -1;
  if (word_before) {
    size_t w = line.find_last_of(" \t", b);
    w = w == std::string::npos ? 0 : w + 1;
    if (control_word(line.substr(w, b + 1 - w))) return -1;
  }
  const Shapes &s = shapes_for(id);
  size_t open = line.find_first_not_of(" \t", at + id.size());
  if (open != std::string::npos && line[open] == '<') {  // type parameters: foo<T>(
    size_t gt = line.find('>', open);
    open = gt == std::string::npos ? gt : line.find_first_not_of(" \t", gt + 1);
  }
  if (open != std::string::npos && line[open] == '(') {
    bool keyword = prev && std::regex_search(line, s.keyword);
    size_t close = open;
    for (int depth = 0; close < line.size(); close++) {
      if (line[close] == '(') depth++;
      else if (line[close] == ')' && --depth == 0) break;
    }
    // An arrow among the arguments is a call handing over a callback, `foo(x => {`.
    if (line.substr(open, close - open).find("=>") != std::string::npos) return keyword ? 0 : -1;
    // A signature's parameters close here and its body, return type or a qualifier
    // follows; or they go on below, after a modifier or a type that a call doesn't have.
    bool signature = close < line.size() ? std::regex_search(line.substr(close + 1), kAfterParams) : prev != 0;
    return keyword ? 0 : signature ? 1 : -1;
  }
  std::smatch m;
  if (prev && std::regex_search(line, s.keyword)) return 0;
  if (prev && std::regex_search(line, m, s.variable)) return m[1].matched ? 0 : 2;
  if (std::regex_search(line, s.assigned)) return 0;
  if (std::regex_search(line, s.field)) return 2;
  return -1;
}

struct Candidate {
  std::string file;
  int line = 0, rank = 0;
  std::string container;  // the nearest class, struct… above it
  bool qualified = false; // written as Parent::name or Parent.name
};

static const std::regex kContainer(
    "\\b(?:class|struct|interface|object|impl|trait|enum|namespace|module|record)\\s+([\\w$]+)");

// The container is followed only for a qualified name, and the regex only runs on a line
// with one of its keywords, since this scans every line of a file.
static bool may_open_container(const std::string &line) {
  static const char *words[] = {"class", "struct", "interface", "object", "impl", "trait", "enum", "namespace", "module", "record"};
  return std::any_of(std::begin(words), std::end(words), [&](const char *w) { return line.find(w) != std::string::npos; });
}

static void scan_lines(const std::string &file, const std::vector<std::string> &lines,
                       const std::string &id, const std::string &parent, std::vector<Candidate> &out) {
  std::string container;
  for (size_t n = 0; n < lines.size(); n++) {
    const std::string &line = lines[n];
    std::smatch m;
    if (!parent.empty() && may_open_container(line) && std::regex_search(line, m, kContainer))
      container = m[1];
    int rank = definition_rank(line, id);
    if (rank < 0) continue;
    bool qualified = !parent.empty() && (line.find(parent + "::" + id) != std::string::npos ||
                                         line.find(parent + "." + id) != std::string::npos);
    out.push_back({file, static_cast<int>(n + 1), rank, container, qualified});
  }
}

// The likeliest definition: inside the plan's qualifier when it gave one (only there, when
// `strict`), then the surest shape, the shortest path, the earliest line.
static std::optional<Candidate> pick(std::vector<Candidate> found, const std::string &parent,
                                     bool strict = false) {
  if (!parent.empty()) {
    std::vector<Candidate> inside;
    for (const auto &c : found)
      if (c.qualified || c.container == parent) inside.push_back(c);
    if (!inside.empty() || strict) found = inside;
  }
  if (found.empty()) return std::nullopt;
  return *std::min_element(found.begin(), found.end(), [](const Candidate &a, const Candidate &b) {
    if (a.rank != b.rank) return a.rank < b.rank;
    if (a.file.size() != b.file.size()) return a.file.size() < b.file.size();
    return a.line < b.line;
  });
}

static bool skipped(const std::string &file) {
  auto ends = [&](const char *s) {
    size_t n = std::strlen(s);
    return file.size() >= n && file.compare(file.size() - n, n, s) == 0;
  };
  return file.find("node_modules/") != std::string::npos || ends(".min.js") || ends(".map") ||
         ends(".lock") || ends("package-lock.json");
}

// The definitions found under the root, by name, for the names asked about so far.
static std::map<std::string, std::vector<Candidate>> g_root_hits;

// Reads the root's files once for all the names not searched yet, on a few threads: each
// file is split into identifiers, and only a line holding a wanted one is checked for a
// definition. A pass costs the same for one name or forty, where git grep slows down
// with every pattern added.
static void search_root_for(const std::vector<std::string> &ids) {
  const std::vector<std::string> &files = root_files();
  std::vector<std::string> batch;
  for (const auto &id : ids)
    if (!g_root_hits.count(id) && std::find(batch.begin(), batch.end(), id) == batch.end()) batch.push_back(id);
  if (batch.empty()) return;
  std::unordered_map<std::string_view, size_t> wanted;
  for (size_t k = 0; k < batch.size(); k++) {
    wanted.emplace(batch[k], k);
    shapes_for(batch[k]);  // made here, so the threads only read them
  }
  auto ident = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$'; };
  unsigned threads = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
  std::vector<std::vector<std::vector<Candidate>>> found(threads, std::vector<std::vector<Candidate>>(batch.size()));
  std::atomic<size_t> next{0};
  auto work = [&](unsigned t) {
    std::string text;
    for (size_t i; (i = next++) < files.size();) {
      const std::string &file = files[i];
      if (skipped(file) || !read_file(g_root + "/" + file, text) || text.size() > (4u << 20) ||
          std::memchr(text.data(), 0, std::min<size_t>(text.size(), 8000)))
        continue;
      std::vector<size_t> seen(batch.size(), 0);  // the last line each name was checked on
      size_t line = 1, start = 0;
      for (size_t at = 0; at < text.size();) {
        if (text[at] == '\n') { line++; start = ++at; continue; }
        if (!ident(text[at])) { at++; continue; }
        size_t from = at;
        while (at < text.size() && ident(text[at])) at++;
        auto hit = wanted.find(std::string_view(text).substr(from, at - from));
        if (hit == wanted.end() || seen[hit->second] == line) continue;
        seen[hit->second] = line;
        size_t eol = text.find('\n', at);
        int rank = definition_rank(text.substr(start, eol == std::string::npos ? eol : eol - start), batch[hit->second]);
        if (rank >= 0) found[t][hit->second].push_back({file, static_cast<int>(line), rank, "", false});
      }
    }
  };
  std::vector<std::thread> pool;
  for (unsigned t = 0; t < threads; t++) pool.emplace_back(work, t);
  for (auto &th : pool) th.join();
  for (size_t k = 0; k < batch.size(); k++) {
    auto &hits = g_root_hits[batch[k]];
    for (auto &per : found) hits.insert(hits.end(), per[k].begin(), per[k].end());
  }
}

static std::vector<std::string> name_parts(const std::string &name) {
  std::vector<std::string> parts;
  std::string bare = name.substr(0, name.find('('));
  for (size_t at = 0; at <= bare.size();) {
    size_t next = bare.find_first_of(":.#", at);
    std::string part = bare.substr(at, next == std::string::npos ? std::string::npos : next - at);
    if (!part.empty()) parts.push_back(part);
    if (next == std::string::npos) break;
    at = next + 1;
  }
  return parts;
}

// Every definition of `id` under the root.
static std::vector<Candidate> search_root(const std::string &id, const std::string &parent) {
  search_root_for({id});
  std::vector<Candidate> out = g_root_hits[id];
  if (!parent.empty()) {
    // Containers come from the files themselves, for the few candidates there are.
    std::map<std::string, std::vector<Candidate>> by_file;
    for (const auto &c : out) by_file[c.file];
    out.clear();
    for (auto &f : by_file) scan_lines(f.first, file_lines(f.first), id, parent, out);
  }
  return out;
}

// Where a name is: in its file when the plan gave one; else in the files the plan
// mentions, in order; else anywhere under the root.
static std::optional<Place> locate(const NameKey &k, const std::vector<std::string> &mentioned) {
  if (k.name.empty()) {
    std::string f = find_file(k.file);
    if (f.empty()) return std::nullopt;
    return Place{f, k.line, k.end};
  }
  std::vector<std::string> parts = name_parts(k.name);
  if (parts.empty()) return std::nullopt;
  std::string id = parts.back(), parent = parts.size() > 1 ? parts[parts.size() - 2] : "";

  auto in_file = [&](const std::string &given) {  // every file the name can mean
    std::vector<Candidate> found;
    for (const auto &f : find_files(given)) scan_lines(f, file_lines(f), id, parent, found);
    return found;
  };
  std::optional<Candidate> c;
  if (!k.file.empty()) {
    c = pick(in_file(k.file), parent);
  } else {
    // A qualified name is looked for inside its qualifier everywhere before anywhere else.
    // Minified code is skipped here as under the root: one long line that mentions
    // every name and defines none a reader could follow.
    std::vector<std::vector<Candidate>> local;
    for (const auto &m : mentioned)
      if (!skipped(m)) local.push_back(in_file(m));
    std::optional<std::vector<Candidate>> root;
    for (bool strict : parent.empty() ? std::vector<bool>{false} : std::vector<bool>{true, false}) {
      for (const auto &found : local)
        if ((c = pick(found, parent, strict))) break;
      if (c) break;
      if (!root) root = search_root(id, parent);
      if ((c = pick(*root, parent, strict))) break;
    }
  }
  if (!c) return std::nullopt;
  return Place{c->file, c->line, 0};
}

static std::optional<Place> place_of(const NameKey &k, const std::vector<std::string> &mentioned) {
  auto found = g_places.find(k.id());
  if (found != g_places.end()) return found->second;
  return g_places[k.id()] = locate(k, mentioned);
}

// The lines a hover shows: the range the plan gave, or from the definition down to where
// its first block closes, at most 15 lines, with their common indent taken off.
static std::string hover_markdown(const Place &p) {
  std::vector<std::string> lines = file_lines(p.file);
  if (lines.empty()) return "";
  size_t from = p.line > 0 ? static_cast<size_t>(p.line - 1) : 0, to;
  if (from >= lines.size()) return "";
  if (p.end >= p.line && p.line > 0) {
    to = std::min({lines.size(), static_cast<size_t>(p.end), from + 30});
  } else {
    to = std::min(lines.size(), from + 15);
    int depth = 0;
    bool opened = false;
    for (size_t n = from; n < to; n++) {
      for (char ch : lines[n]) {
        if (ch == '{') { depth++; opened = true; }
        else if (ch == '}') depth--;
      }
      if (opened && depth <= 0) { to = n + 1; break; }
    }
  }
  size_t indent = std::string::npos;
  for (size_t n = from; n < to; n++) {
    size_t at = lines[n].find_first_not_of(" \t");
    if (at != std::string::npos) indent = std::min(indent, at);
  }
  std::string body;
  for (size_t n = from; n < to; n++)
    body += (indent != std::string::npos && lines[n].size() > indent ? lines[n].substr(indent) : "") + "\n";
  std::string lang = p.file.substr(p.file.find_last_of('.') + 1);
  std::string fence = body.find("```") == std::string::npos ? "```" : "~~~~";
  return fence + lang + "\n" + body + fence + "\n";
}

// PLAN_REVIEW_EDITOR opens the code: a command taking `path:line`, like `zed`, `code -g` or
// `subl`. Unset, Zed when it is on the PATH, else VS Code, else the file's default app.
static void open_in_editor(const Place &p) {
  std::string path = g_root + "/" + p.file;
  std::string target = shell_quote(p.line > 0 ? path + ":" + std::to_string(p.line) : path);
  const char *editor = std::getenv("PLAN_REVIEW_EDITOR");
  std::string cmd;
  if (editor && *editor) cmd = std::string(editor) + " " + target;
  else if (on_path("zed")) cmd = "zed " + target;
  else if (on_path("code")) cmd = "code -g " + target;
#ifdef __APPLE__
  else cmd = "open " + shell_quote(path);
#else
  else cmd = "xdg-open " + shell_quote(path);
#endif
  std::system((cmd + " >/dev/null 2>&1 &").c_str());
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

static std::string g_origin;  // http://127.0.0.1:<port>, the one origin the page has
// The page may load and reach nothing but this broker, so a plan's remote image doesn't
// load and can't carry the plan's text off the machine in its URL. Its own scripts carry
// the round's nonce, so a script slipped into the page any other way doesn't run.
static std::string g_csp;

static void reply(int fd, const std::string &status, const std::string &ctype,
                  const std::string &body) {
  std::ostringstream head;
  head << "HTTP/1.1 " << status << "\r\n"
       << "Content-Type: " << ctype << "\r\n"
       << "Content-Length: " << body.size() << "\r\n"
       << "Cache-Control: no-store\r\n"
       << "X-Content-Type-Options: nosniff\r\n"
       << "Referrer-Policy: no-referrer\r\n";
  if (ctype.rfind("text/html", 0) == 0) head << "Content-Security-Policy: " << g_csp << "\r\n";
  head << "Connection: close\r\n\r\n";
  send_all(fd, head.str() + body);
}

// A request header's value, or nullopt when it isn't sent. `name` is in lower case.
static std::optional<std::string> header(const std::string &head, const std::string &name) {
  std::string lower = head;
  for (char &c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  size_t at = lower.find("\r\n" + name + ":");
  if (at == std::string::npos) return std::nullopt;
  at += name.size() + 3;
  size_t end = head.find("\r\n", at);
  return trim(head.substr(at, end == std::string::npos ? std::string::npos : end - at));
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
//   GET  <base>pele.min.js     for a viewer whose plan has diagrams
//   POST <base>feedback        {verdict: approve | changes | end, leaving?, general,
//                               comments: [{note, anchor: {kind, ...}}]}
//   POST <base>resolve         {keys: [{file?, name?, line?, end?}], files} → [found?]
//   POST <base>hover           a key → {markdown, where}
//   POST <base>open            a key → opens it in the editor
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

  // Only the page this broker served may use it, even with the path's token, in case that
  // leaked: a request for any other host is DNS rebinding (a site's own name pointed at
  // 127.0.0.1), and a POST from any other origin is another site's page.
  std::optional<std::string> origin = header(head, "origin");
  if (header(head, "host") != g_origin.substr(std::strlen("http://")) ||
      (method == "POST" && origin && *origin != g_origin)) {
    reply(fd, "403 Forbidden", "text/plain; charset=utf-8", "forbidden");
    ::close(fd);
    return;
  }

  if (method == "GET" && (path == base || path == base + "session")) {
    set_leaving(false);  // the viewer is back (a reload): cancel any pending end
    if (path == base)
      reply(fd, "200 OK", "text/html; charset=utf-8", page);
    else
      reply(fd, "200 OK", "application/json; charset=utf-8", session);
    ::close(fd);
    return;
  }

  // Only a plan with diagrams needs the renderer, so a viewer asks for it rather than
  // every page carrying it.
  if (method == "GET" && path == base + "pele.min.js") {
    std::string js;
    if (read_file(g_assets + "/pele.min.js", js))
      reply(fd, "200 OK", "text/javascript; charset=utf-8", js);
    else
      reply(fd, "404 Not Found", "text/plain; charset=utf-8", "pele.min.js not found");
    ::close(fd);
    return;
  }

  JVal parsed;
  if (method == "POST") {
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
    if (!parse_json(body, parsed)) {
      reply(fd, "400 Bad Request", "application/json", "{}");
      ::close(fd);
      return;
    }
  }

  // Names, for the browser page: which exist, a hover, and opening one in the editor.
  if (method == "POST" && path == base + "resolve") {
    std::vector<std::string> mentioned;
    if (const JVal *files = parsed.get("files"))
      for (const auto &f : files->arr) if (f.type == JVal::Str) mentioned.push_back(f.str);
    std::string out = "[";
    if (const JVal *keys = parsed.get("keys")) {
      std::lock_guard<std::mutex> lock(g_lookup_mutex);
      // Names without a file may need the whole root searched: all of them in one go.
      std::vector<std::string> ids;
      for (const auto &k : keys->arr) {
        NameKey key = key_of(k);
        std::vector<std::string> parts = name_parts(key.name);
        if (key.file.empty() && !parts.empty() && !g_places.count(key.id())) ids.push_back(parts.back());
      }
      search_root_for(ids);
      for (const auto &k : keys->arr) {
        if (out.size() > 1) out += ',';
        out += place_of(key_of(k), mentioned) ? "true" : "false";
      }
    }
    reply(fd, "200 OK", "application/json", out + "]");
    ::close(fd);
    return;
  }
  if (method == "POST" && (path == base + "hover" || path == base + "open")) {
    std::optional<Place> place;
    {
      std::lock_guard<std::mutex> lock(g_lookup_mutex);
      place = place_of(key_of(parsed), {});
    }
    if (!place) {
      reply(fd, "404 Not Found", "application/json", "null");
    } else if (path == base + "hover") {
      std::string where = place->file + (place->line > 0 ? ":" + std::to_string(place->line) : "");
      reply(fd, "200 OK", "application/json",
            "{\"markdown\":" + json_string(hover_markdown(*place)) + ",\"where\":" + json_string(where) + "}");
    } else {
      open_in_editor(*place);
      reply(fd, "200 OK", "application/json", "{}");
    }
    ::close(fd);
    return;
  }

  if (method == "POST" && path == base + "feedback") {
    std::string verdict;
    if (parsed.type != JVal::Obj ||
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
  g_root = code_root;

  g_assets = real_path(asset_dir());
  std::string page, marked;
  if (!read_file(g_assets + "/page.html", page) ||
      !read_file(g_assets + "/marked.min.js", marked)) {
    std::fprintf(stderr, "review: page.html or marked.min.js not found in %s\n", g_assets.c_str());
    return 1;
  }

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
  g_origin = "http://127.0.0.1:" + std::to_string(port);
  std::string url = g_origin + base;

  // Each script in the page carries this round's nonce, the only inline script the CSP runs.
  std::string nonce = token(22), tagged = "<script nonce=\"" + nonce + "\">";
  for (size_t at = 0; (at = page.find("<script>", at)) != std::string::npos; at += tagged.size())
    page.replace(at, std::strlen("<script>"), tagged);
  replace_first(page, "/*MARKED*/", marked);
  g_csp = "default-src 'self'; script-src 'self' 'nonce-" + nonce + "'; style-src 'self' 'unsafe-inline'; "
          "img-src 'self' data:; connect-src 'self'; object-src 'none'; base-uri 'none'; "
          "form-action 'none'; frame-ancestors 'none'";

  std::string dir = sessions_dir();
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  ::chmod(dir.c_str(), 0700);
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
      // A connection that stops sending or reading is dropped after 5 s rather than
      // holding its thread for the rest of the review. Everything is local: 5 s is plenty.
      timeval limit{5, 0};
      ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit));
      ::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit));
      std::thread(serve, client, base, page, session).detach();
    }
  }).detach();

  std::fprintf(stderr, "Review open at %s\n", url.c_str());
  std::fflush(stderr);
  // A viewer that is already running can also pick the session up from its file.
  if (!open_browser(url))
    std::fprintf(stderr, "review: open the URL above in your browser\n");

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
  if (g_session_path[0] && read_file(g_session_path, current) && current.find(url) != std::string::npos) {
    write_file(dir + "/" + key + ".round", std::to_string(round) + "\n" + result.text("verdict") + "\n");
    remove_session(0);
  }
  std::printf("%s\n", render(name, result).c_str());
  std::fflush(stdout);
  return 0;
}
