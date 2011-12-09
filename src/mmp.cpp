//  minimal macro processor  -----------------------------------------------------------//

#include <boost/detail/lightweight_main.hpp>
#include <boost/assert.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <cctype>
#include <list>
#include <stack>
#include <map>

using std::cout;
using std::endl;
using std::string;

/*

    TODO List

    *  command() for def should check macro doesn't already exist.
    *  Count newlines so error message can give line number.

*/

//--------------------------------------------------------------------------------------//

namespace
{
  string          in_path;
  string          out_path;
  bool            verbose;

  const string    default_start_marker("$");
  string          in_file_start_marker("$");
  const string    default_macro_marker("$");
  const char      def_command[] = "def";
  const char      include_command[] = "include";

  std::ofstream        out;

  struct context
  {
    string                  path;
    string                  content;
    string::const_iterator  cur;           // current position
    string::const_iterator  end;           // past-the-end
    string                  start_marker;  // command start marker; never empty()
    string                  end_marker;    // command end marker; may be empty()
    string                  macro_marker;  // macro marker; never empty()
  };

  typedef std::stack<context, std::list<context> > stack_type;
  stack_type state;  // context stack

  typedef std::map<string, string> macro_map;
  macro_map macros;

  const bool lookahead = true;

  bool process_state();

//-----------------------------------  load_file  --------------------------------------//

  bool load_file(const string& path, string& target)  // true if succeeds
  {
    std::ifstream in(path, std::ios_base::in|std::ios_base::binary );
    if (!in)
    {
      cout << "Error: Could not open input file: " << path << '\n';
      return false;
    }
    std::getline(in, target, '\0'); // read the whole file
    return true;
  }

//----------------------------------  new_context  -------------------------------------//

  bool new_context(const string& path,
    const string& start_marker = default_start_marker,
    const string& end_marker = string(),
    const string& macro_marker = default_macro_marker
    )  // true if succeeds
  {
    state.push(context());
    if (!load_file(path, state.top().content))
    {
      state.pop();
      return false;
    }
    state.top().path = path;
    state.top().cur = state.top().content.cbegin();
    state.top().end = state.top().content.cend();
    state.top().start_marker = start_marker;
    state.top().end_marker = end_marker;
    state.top().macro_marker = macro_marker;
    return true;
  }

//------------------------------------  setup  -----------------------------------------//

  bool setup(int argc, char* argv[])  // true if succeeds
  {
    bool ok = true;
    while (argc > 1) 
    {
      if (argc > 2 && std::strcmp(argv[1], "--in") == 0 )
        { in_path = argv[2]; --argc; ++argv; }
      else if (argc > 2 && std::strcmp(argv[1], "--out") == 0 )
        { out_path = argv[2]; --argc; ++argv; }
      else if ( std::strcmp( argv[1], "--verbose" ) == 0 ) verbose = true;
      else
      { 
        cout << "Unknown switch: " << argv[1] << "\n"; ok = false;
      }
      --argc;
      ++argv;
    }

    if (in_path.empty())
      { cout << "Missing --in\n"; ok = false; }
    if (out_path.empty())
      { cout << "Missing --out\n"; ok = false; }

    if (!ok)
    {
      cout <<
        "Usage: mmp [switch...]\n"
        "  switch: --in path   Input file path. Required.\n"
        "          --out path  Output file path. Required.\n"
        "          --verbose   Report progress during processing\n"
        "Example: mmp --verbose --in ../doc/src/index.html --out ../doc/index.html\n"
        ;
    }
    return ok;
  }

//-------------------------------------  name  -----------------------------------------//

  string name(bool lookahead_=false)
  {
    string s;
    string::const_iterator it(state.top().cur);

    // bypass leading whitespace
    for (;it != state.top().end && std::isspace(*it);
      ++it) {} 

    // store string
    for (;it != state.top().end &&
      (std::isalnum(*it) || *it == '_');
      ++it)
    {
      s += *it;
    }

    if (!lookahead_)
      state.top().cur = it;

    return s;
  }

//---------------------------------  simple_string  ------------------------------------//

  string simple_string(bool lookahead_=false)
  {
    string s;
    string::const_iterator it(state.top().cur);

    // bypass leading whitespace
    for (;it != state.top().end && std::isspace(*it);
      ++it) {} 

    // store string
    for (;it != state.top().end && !std::isspace(*it);
      ++it)
    {
      s += *it;
    }

    if (!lookahead_)
      state.top().cur = it;

    return s;
  }

//---------------------------------  any_string  ---------------------------------------//

  string any_string()
  {
    // bypass leading whitespace
    for (;state.top().cur != state.top().end && std::isspace(*state.top().cur);
      ++state.top().cur) {} 

    if (*state.top().cur != '"')
      return simple_string();

    ++state.top().cur;  // bypass the '"'

    string s;

    // store string
    for (;state.top().cur != state.top().end && *state.top().cur != '"';
      ++state.top().cur)
    {
      s += *state.top().cur;
    }

    // maintain the state.top().cur invariant
    if (*state.top().cur == '"')
      ++state.top().cur;
    else
    {
      cout << "Error: No closing quote for string that begins \""
           << s.substr(0, 120) << "...\"\n";
    }

    return s;
  }

//------------------------------------  macro  -----------------------------------------//

  bool macro()  // true if succeeds
  {
    if (std::memcmp(&*state.top().cur, state.top().macro_marker.c_str(),
        state.top().macro_marker.size()) != 0)
      return false;
    std::advance(state.top().cur, state.top().macro_marker.size()); 
    string macro_name(name());
    macro_map::const_iterator it = macros.find(macro_name);
    if (it == macros.end())
    {
      cout << "Error: macro not found: " << macro_name << '\n';
    }
    out << it->second;
    return true;
  }

//-----------------------------------  command  ----------------------------------------//

  bool command()  // true if succeeds
  // postcondition: if true, state.top().cur updated to position past the command 
  {
    if (std::memcmp(&*state.top().cur, state.top().start_marker.c_str(),
        state.top().start_marker.size()) != 0)
      return false;

    // start_marker is present, so check for commands
    const char* p = &*state.top().cur + state.top().start_marker.size();

    // def[ine] macro command
    if (std::memcmp(p, def_command, sizeof(def_command)-1) == 0
      && std::isspace(*(p+sizeof(def_command)-1)))
    {
      std::advance(state.top().cur, state.top().start_marker.size()
        + (sizeof(def_command)-1));
      string macro_name(name());
      string macro_body(any_string());
      macros.insert(std::make_pair(macro_name, macro_body));
    }

    // include command
    else if (std::memcmp(p, include_command, sizeof(include_command)-1) == 0
      && std::isspace(*(p+sizeof(include_command)-1)))
    {
      std::advance(state.top().cur, state.top().start_marker.size()
        + (sizeof(include_command)-1));
      string path(any_string());
      new_context(path);
      process_state();
      state.pop();
    }

    // false alarm
    else
      return false;

    // bypass trailing whitespace; this has the effect of avoiding the output of
    // spurious whitespace such as a newline at the end of a command
    for (;state.top().cur != state.top().end && std::isspace(*state.top().cur);
      ++state.top().cur) {} 

    return true;
  }

//---------------------------------  process_state  ------------------------------------//

  bool process_state()  // true if succeeds
  {
    BOOST_ASSERT(!state.empty());  // failure indicates program logic error

    if (verbose)
      cout << "Processing " << state.top().path << "...\n";

    for(; state.top().cur != state.top().end;)
    {
      if (command())
        continue;
      if (macro())
        continue;
      out << *state.top().cur;
      ++state.top().cur;
    }

    if (verbose)
      cout << "  " << state.top().path << " complete\n";

    return true;
  }

}  // unnamed namespace

//--------------------------------------------------------------------------------------//
//                                                                                      //
//                                     cpp_main                                         //
//                                                                                      //
//--------------------------------------------------------------------------------------//

int cpp_main(int argc, char* argv[])
{
  if (!setup(argc, argv))
    return 1;

  out.open(out_path, std::ios_base::out|std::ios_base::binary);
  if (!out)
  {
    cout << "Error: Could not open output file: " << out_path << '\n';
    return 1;
  }

  if (!new_context(in_path, in_file_start_marker))
    return 1;

  if (!process_state())
    return 1;

  if (verbose)
  {
    cout << "Dump macro definitions:\n";
    for (macro_map::const_iterator it = macros.cbegin();
      it != macros.cend(); ++it)
    {
      cout << "  " << it->first << ": \"" << it->second << "\"\n";
    }
  }

  return 0;
}
