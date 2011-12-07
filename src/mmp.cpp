//  minimal macro processor  -----------------------------------------------------------//

#include <boost/detail/lightweight_main.hpp>
#include <iostream>
#include <fstream>
#include <string>

using std::cout;
using std::endl;
using std::string;

//--------------------------------------------------------------------------------------//

namespace
{
  string in_path;
  string out_path;
  bool verbose;
}

//------------------------------------  setup  -----------------------------------------//

bool setup(int argc, char* argv[])  // true if setup succeeds
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
    { cout << "Missing --in-file\n"; ok = false; }
  if (out_path.empty())
    { cout << "Missing --out-file\n"; ok = false; }

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

//--------------------------------------------------------------------------------------//
//                                                                                      //
//                                     cpp_main                                         //
//                                                                                      //
//--------------------------------------------------------------------------------------//

int cpp_main(int argc, char* argv[])
{
  if (!setup(argc, argv))
    return 1;
  return 0;
}
