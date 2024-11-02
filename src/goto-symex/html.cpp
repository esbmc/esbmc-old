#include "util/message.h"
#include <fstream>
#include <goto-symex/goto_trace.h>
#include <ostream>
#include <sstream>
#include <unordered_map>
#include <list>
#include <util/language.h>
#include <langapi/language_util.h>
#include <optional>
#include <filesystem>
#include <util/cmdline.h>

// Add these declarations at top after includes
#include <nlohmann/json.hpp>
using json = nlohmann::json;

static json tests_json;  // Static to maintain state across multiple report generations
static bool first_write = true;
static std::map<std::string, std::vector<std::string>> source_files;

// TODO: Multiple files
// TODO: Control Trace

/// Namespace for scripts taken from Clang, as generated by:
/// clang --analyze -Xclang -analyzer-output=html -o test test.c
namespace clang_bug_report
{
constexpr std::string_view html_style{
  R"(<style type="text/css">
body { color:#000000; background-color:#ffffff }
body { font-family:Helvetica, sans-serif; font-size:10pt }
h1 { font-size:14pt }
.FileName { margin-top: 5px; margin-bottom: 5px; display: inline; }
.FileNav { margin-left: 5px; margin-right: 5px; display: inline; }
.FileNav a { text-decoration:none; font-size: larger; }
.divider { margin-top: 30px; margin-bottom: 30px; height: 15px; }
.divider { background-color: gray; }
.code { border-collapse:collapse; width:100%; }
.code { font-family: "Monospace", monospace; font-size:10pt }
.code { line-height: 1.2em }
.comment { color: green; font-style: oblique }
.keyword { color: blue }
.string_literal { color: red }
.directive { color: darkmagenta }

/* Macros and variables could have pop-up notes hidden by default.
  - Macro pop-up:    expansion of the macro
  - Variable pop-up: value (table) of the variable */
.macro_popup, .variable_popup { display: none; }

/* Pop-up appears on mouse-hover event. */
.macro:hover .macro_popup, .variable:hover .variable_popup {
  display: block;
  padding: 2px;
  -webkit-border-radius:5px;
  -webkit-box-shadow:1px 1px 7px #000;
  border-radius:5px;
  box-shadow:1px 1px 7px #000;
  position: absolute;
  top: -1em;
  left:10em;
  z-index: 1
}

.macro_popup {
  border: 2px solid red;
  background-color:#FFF0F0;
  font-weight: normal;
}

.variable_popup {
  border: 2px solid blue;
  background-color:#F0F0FF;
  font-weight: bold;
  font-family: Helvetica, sans-serif;
  font-size: 9pt;
}

/* Pop-up notes needs a relative position as a base where they pops up. */
.macro, .variable {
  background-color: PaleGoldenRod;
  position: relative;
}
.macro { color: DarkMagenta; }

#tooltiphint {
  position: fixed;
  width: 50em;
  margin-left: -25em;
  left: 50%;
  padding: 10px;
  border: 1px solid #b0b0b0;
  border-radius: 2px;
  box-shadow: 1px 1px 7px black;
  background-color: #c0c0c0;
  z-index: 2;
}

.num { width:2.5em; padding-right:2ex; background-color:#eeeeee }
.num { text-align:right; font-size:8pt }
.num { color:#444444 }
.line { padding-left: 1ex; border-left: 3px solid #ccc }
.line { white-space: pre }
.msg { -webkit-box-shadow:1px 1px 7px #000 }
.msg { box-shadow:1px 1px 7px #000 }
.msg { -webkit-border-radius:5px }
.msg { border-radius:5px }
.msg { font-family:Helvetica, sans-serif; font-size:8pt }
.msg { float:left }
.msg { position:relative }
.msg { padding:0.25em 1ex 0.25em 1ex }
.msg { margin-top:10px; margin-bottom:10px }
.msg { font-weight:bold }
.msg { max-width:60em; word-wrap: break-word; white-space: pre-wrap }
.msgT { padding:0x; spacing:0x }
.msgEvent { background-color:#fff8b4; color:#000000 }
.msgControl { background-color:#bbbbbb; color:#000000 }
.msgNote { background-color:#ddeeff; color:#000000 }
.mrange { background-color:#dfddf3 }
.mrange { border-bottom:1px solid #6F9DBE }
.PathIndex { font-weight: bold; padding:0px 5px; margin-right:5px; }
.PathIndex { -webkit-border-radius:8px }
.PathIndex { border-radius:8px }
.PathIndexEvent { background-color:#bfba87 }
.PathIndexControl { background-color:#8c8c8c }
.PathIndexPopUp { background-color: #879abc; }
.PathNav a { text-decoration:none; font-size: larger }
.CodeInsertionHint { font-weight: bold; background-color: #10dd10 }
.CodeRemovalHint { background-color:#de1010 }
.CodeRemovalHint { border-bottom:1px solid #6F9DBE }
.msg.selected{ background-color:orange !important; }

table.simpletable {
  padding: 5px;
  font-size:12pt;
  margin:20px;
  border-collapse: collapse; border-spacing: 0px;
}
td.rowname {
  text-align: right;
  vertical-align: top;
  font-weight: bold;
  color:#444444;
  padding-right:2ex;
}

/* Hidden text. */
input.spoilerhider + label {
  cursor: pointer;
  text-decoration: underline;
  display: block;
}
input.spoilerhider {
 display: none;
}
input.spoilerhider ~ .spoiler {
  overflow: hidden;
  margin: 10px auto 0;
  height: 0;
  opacity: 0;
}
input.spoilerhider:checked + label + .spoiler{
  height: auto;
  opacity: 1;
}
</style>)"};

constexpr std::string_view annotated_source_header_fmt{
  R"(
<h3>Annotated Source Code</h3>
<p>Press <a href="#" onclick="toggleHelp(); return false;">'?'</a>
   to see keyboard shortcuts</p>
<input type="checkbox" class="spoilerhider" id="showinvocation" />
<label for="showinvocation" >Show analyzer invocation</label>
<div class="spoiler">{}
</div>
<div id='tooltiphint' hidden="true">
  <p>Keyboard shortcuts: </p>
  <ul>
    <li>Use 'j/k' keys for keyboard navigation</li>
    <li>Use 'Shift+S' to show/hide relevant lines</li>
    <li>Use '?' to toggle this window</li>
  </ul>
  <a href="#" onclick="toggleHelp(); return false;">Close</a>
</div>
)"};

// NOTE: Removed the "Show arrows"
constexpr std::string_view counterexample_checkbox{R"(<form>
    <input type="checkbox" name="showCounterexample" id="showCounterexample" />
    <label for="showCounterexample">
       Show only relevant lines
    </label>
</form>)"};

const std::string
counterexample_filter(const std::string_view relevant_lines_js)
{
  std::ostringstream oss;
  oss << "<script type='text/javascript'>\n";
  oss << "var relevant_lines = " << relevant_lines_js << ";\n";
  oss << R"(
var filterCounterexample = function (hide) {
  var tables = document.getElementsByClassName("code");
  for (var t=0; t<tables.length; t++) {
    var table = tables[t];
    var file_id = table.getAttribute("data-fileid");
    var lines_in_fid = relevant_lines[file_id];
    if (!lines_in_fid) {
      lines_in_fid = {};
    }
    var lines = table.getElementsByClassName("codeline");
    for (var i=0; i<lines.length; i++) {
        var el = lines[i];
        var lineNo = el.getAttribute("data-linenumber");
        if (!lines_in_fid[lineNo]) {
          if (hide) {
            el.setAttribute("hidden", "");
          } else {
            el.removeAttribute("hidden");
          }
        }
    }
  }
}

window.addEventListener("keydown", function (event) {
  if (event.defaultPrevented) {
    return;
  }
  // SHIFT + S
  if (event.shiftKey && event.keyCode == 83) {
    var checked = document.getElementsByName("showCounterexample")[0].checked;
    filterCounterexample(!checked);
    document.getElementsByName("showCounterexample")[0].click();
  } else {
    return;
  }
  event.preventDefault();
}, true);

document.addEventListener("DOMContentLoaded", function() {
    document.querySelector('input[name="showCounterexample"]').onchange=
        function (event) {
      filterCounterexample(this.checked);
    };
});
</script>)";

  return oss.str();
}

constexpr std::string_view keyboard_navigation_js{R"(  
<script type='text/javascript'>
var digitMatcher = new RegExp("[0-9]+");

var querySelectorAllArray = function(selector) {
  return Array.prototype.slice.call(
    document.querySelectorAll(selector));
}

document.addEventListener("DOMContentLoaded", function() {
    querySelectorAllArray(".PathNav > a").forEach(
        function(currentValue, currentIndex) {
            var hrefValue = currentValue.getAttribute("href");
            currentValue.onclick = function() {
                scrollTo(document.querySelector(hrefValue));
                return false;
            };
        });
});

var findNum = function() {
    var s = document.querySelector(".msg.selected");
    if (!s || s.id == "EndPath") {
        return 0;
    }
    var out = parseInt(digitMatcher.exec(s.id)[0]);
    return out;
};

var classListAdd = function(el, theClass) {
  if(!el.className.baseVal)
    el.className += " " + theClass;
  else
    el.className.baseVal += " " + theClass;
};

var classListRemove = function(el, theClass) {
  var className = (!el.className.baseVal) ?
      el.className : el.className.baseVal;
    className = className.replace(" " + theClass, "");
  if(!el.className.baseVal)
    el.className = className;
  else
    el.className.baseVal = className;
};

var scrollTo = function(el) {
    querySelectorAllArray(".selected").forEach(function(s) {
      classListRemove(s, "selected");
    });
    classListAdd(el, "selected");
    window.scrollBy(0, el.getBoundingClientRect().top -
        (window.innerHeight / 2));
    highlightArrowsForSelectedEvent();
};

var move = function(num, up, numItems) {
  if (num == 1 && up || num == numItems - 1 && !up) {
    return 0;
  } else if (num == 0 && up) {
    return numItems - 1;
  } else if (num == 0 && !up) {
    return 1 % numItems;
  }
  return up ? num - 1 : num + 1;
}

var numToId = function(num) {
  if (num == 0) {
    return document.getElementById("EndPath")
  }
  return document.getElementById("Path" + num);
};

var navigateTo = function(up) {
  var numItems = document.querySelectorAll(
      ".line > .msgEvent, .line > .msgControl").length;
  var currentSelected = findNum();
  var newSelected = move(currentSelected, up, numItems);
  var newEl = numToId(newSelected, numItems);

  // Scroll element into center.
  scrollTo(newEl);
};

window.addEventListener("keydown", function (event) {
  if (event.defaultPrevented) {
    return;
  }
  // key 'j'
  if (event.keyCode == 74) {
    navigateTo(/*up=*/false);
  // key 'k'
  } else if (event.keyCode == 75) {
    navigateTo(/*up=*/true);
  } else {
    return;
  }
  event.preventDefault();
}, true);
</script>
  
<script type='text/javascript'>

var toggleHelp = function() {
    var hint = document.querySelector("#tooltiphint");
    var attributeName = "hidden";
    if (hint.hasAttribute(attributeName)) {
      hint.removeAttribute(attributeName);
    } else {
      hint.setAttribute("hidden", "true");
    }
};
window.addEventListener("keydown", function (event) {
  if (event.defaultPrevented) {
    return;
  }
  if (event.key == "?") {
    toggleHelp();
  } else {
    return;
  }
  event.preventDefault();
});
</script>)"};

} // namespace clang_bug_report

class html_report
{
public:
  html_report(
    const goto_tracet &goto_trace,
    const namespacet &ns,
    const cmdlinet::options_mapt &options_map);
  void output(std::ostream &oss) const;

  bool show_partial_assertions = false;

protected:
  const std::string generate_head() const;
  const std::string generate_body() const;
  const goto_tracet &goto_trace;

private:
  const namespacet &ns;
  const cmdlinet::options_mapt &opt_map;
  std::optional<goto_trace_stept> violation_step;
  void print_file_table(
    std::ostream &os,
    std::pair<const std::string_view, size_t>) const;
  struct code_lines
  {
    explicit code_lines(const std::string &content) : content(content)
    {
    }
    const std::string content;
    std::string to_html() const;
  };

  struct code_steps
  {
    code_steps(const size_t step, const std::string &msg, bool is_jump)
      : step(step), msg(msg), is_jump(is_jump)
    {
    }
    size_t step;
    std::string msg;
    bool is_jump;
    size_t file_id = 1;
    std::string to_html(size_t last) const;
  };

  static inline const std::string
  tag_body_str(const std::string_view tag, const std::string_view body)
  {
    return fmt::format("<{0}>{1}</{0}>", tag, body);
  }
};

html_report::html_report(
  const goto_tracet &goto_trace,
  const namespacet &ns,
  const cmdlinet::options_mapt &options_map)
  : goto_trace(goto_trace), ns(ns), opt_map(options_map)
{
  // TODO: C++20 reverse view
  for (const goto_trace_stept &step : goto_trace.steps)
  {
    if (step.is_assert() && !step.guard)
    {
      violation_step = step;
      return;
    }
  }
  log_error("[HTML] Could not find any asserts in error trace!");
  abort();
}

const std::string html_report::generate_head() const
{
  std::ostringstream head;
  {
    head << tag_body_str("title", "ESBMC report");
    head << clang_bug_report::html_style;
  }
  return tag_body_str("head", head.str());
}

const std::string html_report::generate_body() const
{
  std::ostringstream body;
  const locationt &location = violation_step->pc->location;
  const std::string filename{
    std::filesystem::absolute(location.get_file().as_string()).string()};
  // Bug Summary
  {
    const std::string position{fmt::format(
      "function {}, line {}, column {}",
      location.get_function(),
      location.get_line(),
      location.get_column())};
    std::string violation_str{violation_step->comment};
    if (violation_str.empty())
      violation_str = "Assertion failure";
    violation_str[0] = toupper(violation_str[0]);
    body << "<h3>Bug Summary</h3>";
    body << R"(<table class="simpletable">)";
    body << fmt::format(
      R"(<tr><td class="rowname">File:</td><td>{}</td></tr>)", filename);
    body << fmt::format(
      R"(<tr><td class="rowname">Violation:</td><td><a href="#EndPath">{}</a><br />{}</td></tr></table>)",
      position,
      violation_str);
  }

  // Annoted Source Header
  {
    std::ostringstream oss;
    for (const auto &item : opt_map)
    {
      oss << item.first << " ";
      for (const std::string &value : item.second)
        oss << value << " ";
    }
    body << fmt::format(
      clang_bug_report::annotated_source_header_fmt, oss.str());
  }

  std::set<std::string> files;
  // Counter-Example filtering
  {
    std::unordered_set<size_t> relevant_lines;
    for (const auto &step : goto_trace.steps)
    {
      files.insert(step.pc->location.get_file().as_string());
      if (!(step.is_assert() && step.guard))
        relevant_lines.insert(atoi(step.pc->location.get_line().c_str()));
    }

    // TODO: Every file!
    std::ostringstream oss;
    oss << R"({"1": {)";
    for (const auto line : relevant_lines)
      oss << fmt::format("\"{}\": 1,", line);
    oss << "}}";
    body << clang_bug_report::counterexample_filter(oss.str());
    body << clang_bug_report::counterexample_checkbox;
    body << clang_bug_report::keyboard_navigation_js;
  }
  // Counter-Example and Arrows
  {
    for (const std::string &file : files)
    {
      print_file_table(
        body, {file, 1 + std::distance(files.begin(), files.find(file))});
    }
  }

  return tag_body_str("body", body.str());
}

void html_report::print_file_table(
  std::ostream &os,
  std::pair<const std::string_view, size_t> file) const
{
  std::vector<code_lines> lines;
  {
    std::ifstream input(std::string(file.first));
    std::string line;
    while (std::getline(input, line))
    {
      code_lines code_line(line);
      lines.push_back(code_line);
    }
  }
  os << fmt::format(
    "<div id=File{}><h4 class=FileName>{}</h4>",
    file.second,
    std::filesystem::absolute(file.first).string());

  std::unordered_map<size_t, std::list<code_steps>> steps;
  size_t counter = 0;
  for (const auto &step : goto_trace.steps)
  {
    if (step.pc->location.get_file().as_string() != file.first)

    {
      counter++;
      continue;
    }
    size_t line = atoi(step.pc->location.get_line().c_str());
    std::ostringstream oss;
    if (step.pc->is_assume())
      oss << "Assumption restriction";
    else if (step.pc->is_assert() || step.is_assert())
    {
      if (!show_partial_assertions && step.guard)
        continue;

      std::string comment =
        (step.comment.empty() ? "Asssertion check" : step.comment);

      comment[0] = toupper(comment[0]);
      oss << comment;
      //oss << "\n" << from_expr(ns, "", step.pc->guard);
    }
    else if (step.pc->is_assign())
    {
      oss << from_expr(ns, "", step.lhs);
      if (is_nil_expr(step.value))
        oss << " (assignment removed)";
      else
        oss << " = " << from_expr(ns, "", step.value);
    }
    else if (step.pc->is_function_call())
    {
      oss << "Function argument '";
      oss << from_expr(ns, "", step.lhs);
      oss << " = " << from_expr(ns, "", step.value) << "'";
    }

    auto &list = steps.insert({line, std::list<code_steps>()}).first->second;

    list.push_back(
      code_steps(++counter, oss.str(), step.is_assume() || step.is_assert()));

    // Is this step the violation?
    if (step.is_assert() && !step.guard)
      break;
  }

  // Table begin
  os << fmt::format(R"(<table class="code" data-fileid="{}">)", file.second);
  for (size_t i = 0; i < lines.size(); i++)
  {
    const auto &it = steps.find(i);
    if (it != steps.end())
    {
      for (const auto &step : it->second)
      {
        os << step.to_html(counter);
      }
    }
    constexpr std::string_view codeline_fmt{
      R"(<tr class="codeline" data-linenumber="{0}"><td class="num" id="LN{0}">{0}</td><td class="line">{1}</td></tr>)"};
    os << fmt::format(codeline_fmt, i + 1, lines[i].to_html());
  }

  os << "</table><hr class=divider>";
  // Table end
}

void html_report::output(std::ostream &oss) const
{
  std::ostringstream html;
  html << generate_head();
  html << generate_body();

  oss << "<!doctype html>";
  oss << tag_body_str("html", html.str());
}

void add_coverage_to_json(
  const goto_tracet &goto_trace,
  const namespacet &ns)
{
  json test_entry;
  test_entry["steps"] = json::array();
  test_entry["status"] = "unknown";
  test_entry["coverage"] = {{"files", json::object()}};
  
  // Track source code on first run
  if(first_write) {
    for(const auto &step : goto_trace.steps) {
      if(step.pc != goto_programt::const_targett() && !step.pc->location.is_nil()) {
        std::string file = id2string(step.pc->location.get_file());
        if(!file.empty() && source_files.find(file) == source_files.end()) {
          std::vector<std::string> lines;
          std::ifstream input(file);
          std::string line;
          while(std::getline(input, line)) {
            lines.push_back(line);
          }
          source_files[file] = lines;
        }
      }
    }
  }

  std::map<std::string, std::map<int, int>> line_hits;
  std::set<std::pair<std::string, int>> violations;
  size_t step_count = 0;
  bool found_violation = false;

  // Process steps with navigation
  for(const auto &step : goto_trace.steps)
  {
    if(step.pc != goto_programt::const_targett() && !step.pc->location.is_nil())
    {
      try {
        const locationt &loc = step.pc->location;
        std::string file = id2string(loc.get_file());
        std::string line_str = id2string(loc.get_line());
        std::string function = id2string(loc.get_function());
        int line = std::stoi(line_str);

        if(!file.empty() && line > 0) {
          // Track line hits
          line_hits[file][line]++;

          // Add step info with navigation
          json step_data;
          step_data["id"] = fmt::format("Path{}", step_count);
          step_data["file"] = file;
          step_data["line"] = line_str;
          step_data["function"] = function;
          step_data["step_number"] = step_count;

          // Add navigation info
          if(step_count > 0) {
            step_data["previous"] = {
              {"id", fmt::format("Path{}", step_count - 1)},
              {"step", step_count - 1}
            };
          }
          if(step_count < goto_trace.steps.size() - 1) {
            step_data["next"] = {
              {"id", fmt::format("Path{}", step_count + 1)},
              {"step", step_count + 1}
            };
          }

          // Add step type and message
          if(step.pc->is_assume()) {
            step_data["type"] = "assume";
            step_data["message"] = "Assumption restriction";
          }
          else if(step.pc->is_assert() || step.is_assert()) {
            step_data["type"] = "assert";
            if(!step.guard) {
              step_data["type"] = "violation";
              step_data["message"] = step.comment.empty() ? "Assertion check" : step.comment;
              found_violation = true;
              violations.insert({file, line});
              test_entry["status"] = "violation";
              step_data["assertion"] = {
                {"violated", true},
                {"comment", step.comment},
                {"guard", from_expr(ns, "", step.pc->guard)}
              };
            }
          }
          else if(step.pc->is_assign()) {
            step_data["type"] = "assignment";
            std::string msg = from_expr(ns, "", step.lhs);
            if(is_nil_expr(step.value)) {
              msg += " (assignment removed)";
            } else {
              msg += " = " + from_expr(ns, "", step.value);
            }
            step_data["message"] = msg;
          }
          else if(step.pc->is_function_call()) {
            step_data["type"] = "function_call";
            step_data["message"] = fmt::format(
              "Function argument '{}' = '{}'",
              from_expr(ns, "", step.lhs),
              from_expr(ns, "", step.value)
            );
          }

          test_entry["steps"].push_back(step_data);
          step_count++;
        }
      } catch(...) {
        continue;
      }
    }
  }

  // Add end path for final step
  if(step_count > 0) {
    json last_step = test_entry["steps"].back();
    last_step["id"] = "EndPath";
    if(last_step.contains("next")) {
      last_step.erase("next");
    }
    test_entry["steps"].back() = last_step;
  }

  // Build coverage data
  for(const auto& [file, lines] : line_hits) {
    json file_coverage;
    file_coverage["covered_lines"] = json::object();

    for(const auto& [line, hits] : lines) {
      std::string line_str = std::to_string(line);
      bool is_violation = violations.find({file, line}) != violations.end();

      file_coverage["covered_lines"][line_str] = {
        {"covered", true},
        {"hits", hits},
        {"type", is_violation ? "violation" : "execution"}
      };
    }

    file_coverage["coverage_stats"] = {
      {"covered_lines", lines.size()},
      {"total_hits", std::accumulate(lines.begin(), lines.end(), 0,
        [](int sum, const auto& p) { return sum + p.second; })}
    };

    test_entry["coverage"]["files"][file] = file_coverage;
  }

  // Add source code on first write
  if(first_write) {
    json source_data;
    for(const auto& [file, lines] : source_files) {
      source_data[file] = lines;
    }
    test_entry["source_files"] = source_data;
  }

  // Read existing JSON if not first write
  json all_tests;
  if(!first_write) {
    try {
      std::ifstream input("report.json");
      if(input.is_open()) {
        all_tests = json::array();
        input >> all_tests;
      } else {
        all_tests = json::array();
      }
    } catch(...) {
      all_tests = json::array();
    }
  } else {
    all_tests = json::array();
  }

  // Append new test and write
  all_tests.push_back(test_entry);
  std::ofstream json_out("report.json");
  if(json_out.is_open()) {
    json_out << std::setw(2) << all_tests << std::endl;
    first_write = false;
  }
}

void generate_html_report(
  const std::string_view uuid,
  const namespacet &ns,
  const goto_tracet &goto_trace,
  const cmdlinet::options_mapt &options_map)
{
  log_status("Generating HTML report for trace: {}", uuid);
  const html_report report(goto_trace, ns, options_map);

  std::ofstream html(fmt::format("report-{}.html", uuid));
  report.output(html);
  // Add this trace's coverage to the aggregated JSON
  // add_coverage_to_json(goto_trace, ns);
}

void generate_json_report(
  const std::string_view uuid,
  const namespacet &ns,
  const goto_tracet &goto_trace,
  const cmdlinet::options_mapt &options_map)
{
  log_status("Generating JSON report for trace: {}", uuid);
  // const html_report report(goto_trace, ns, options_map);

  // std::ofstream html(fmt::format("report-{}.html", uuid));
  // report.output(html);
  // Add this trace's coverage to the aggregated JSON
  add_coverage_to_json(goto_trace, ns);
}



#include <regex>
std::string html_report::code_lines::to_html() const
{
  // TODO: C++23 has constexpr for regex
  // TODO: Support for comments
  constexpr std::array keywords{
    "auto",     "break",  "case",    "char",   "const",    "continue",
    "default",  "do",     "double",  "else",   "enum",     "extern",
    "float",    "for",    "goto",    "if",     "int",      "long",
    "register", "return", "short",   "signed", "sizeof",   "static",
    "struct",   "switch", "typedef", "union",  "unsigned", "void",
    "volatile", "while"};

  std::string output(content);
  for (const auto &word : keywords)
  {
    std::regex e(
      fmt::format("(\\b({}))(\\b)(?=[^\"]*(\"[^\"]*\"[^\"]*)*$)", word));
    output = std::regex_replace(
      output, e, fmt::format("<span class='keyword'>{}</span>", word));
  }
  return output;
}
std::string html_report::code_steps::to_html(size_t last) const
{
  constexpr double margin = 1;

  const std::string next_step =
    step + 1 == last ? "EndPath" : fmt::format("Path{}", step + 1);
  const std::string previous_step =
    step != 0 ? fmt::format("Path{}", step - 1) : "";

  constexpr std::string_view next_step_format{
    R"(<td><div class="PathNav"><a href="#{}" title="Next event ({}) ">&#x2192;</a></div></td>)"};
  const std::string next_step_str =
    step < last ? fmt::format(next_step_format, next_step, step + 1) : "";

  constexpr std::string_view previous_step_format{
    R"(<td><div class="PathNav"><a href="#{}" title="Previous event ({}) ">&#x2190;</a></div></td>)"};

  const std::string previous_step_str =
    step != 0 ? fmt::format(previous_step_format, previous_step, step - 1) : "";

  constexpr std::string_view jump_format{
    R"(<tr><td class="num"></td><td class="line"><div id="Path{0}" class="msg msgControl" style="margin-left:{1}ex"><table class="msgT"><tr><td valign="top"><div class="PathIndex PathIndexControl">{0}</div></td>{2}<td>{3}</td>{4}</tr></table></div></td></tr>)"};

  constexpr std::string_view step_format{
    R"(<tr><td class="num"></td><td class="line"><div id="Path{0}" class="msg msgEvent" style="margin-left:{1}ex"><table class="msgT"><tr><td valign="top"><div class="PathIndex PathIndexEvent">{0}</div></td>{2}<td>{3}</td>{4}</tr></table></div></td></tr>)"};

  constexpr std::string_view end_format{
    R"(<tr><td class="num"></td><td class="line"><div id="EndPath" class="msg msgEvent" style="margin-left:{1}ex"><table class="msgT"><tr><td valign="top"><div class="PathIndex PathIndexEvent">{0}</div></td>{2}<td>{3}</td></table></div></td></tr>)"};

  std::string format(is_jump ? jump_format : step_format);
  return fmt::format(
    step == last ? end_format : format,
    step,
    margin * step + 1,
    previous_step_str,
    msg,
    next_step_str);
}
