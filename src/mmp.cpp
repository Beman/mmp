//  minimal macro processor  -----------------------------------------------------------//

#include <boost/detail/lightweight_main.hpp>
#include <boost/assert.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <stack>

using std::cout;
using std::endl;
using std::string;

//--------------------------------------------------------------------------------------//

namespace
{
  string               in_path;
  string               out_path;
  bool                 verbose;
  const string         default_start_marker("$");
  const string         in_file_start_marker("$");

  std::ofstream        out;

  struct context
  {
    string                  path;
    string                  content;
    string::const_iterator  cur;           // current position
    string::const_iterator  end;           // past-the-end
    string                  start_marker;  // start marker; never empty()
    string                  end_marker;    // end marker; may be empty()
  };

  typedef std::stack<context, std::vector<context> > stack_type;

  stack_type state;  // context stack


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
    const string& end_marker = string())  // true if succeeds
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

//------------------------------------  macro  -----------------------------------------//

  bool macro()  // true if succeeds
  {
    return false;
  }

//-----------------------------------  command  ----------------------------------------//

  bool command()  // true if succeeds
  {
    return false;
  }

//---------------------------------  process_state  ------------------------------------//

  bool process_state()  // true if succeeds
  {
    BOOST_ASSERT(!state.empty());  // failure indicates program logic error

    for(; state.top().cur != state.top().end;)
    {
      if (macro()) continue;
      if (command()) continue;
      out << *state.top().cur;
      ++state.top().cur;
    }

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

  return 0;
}
