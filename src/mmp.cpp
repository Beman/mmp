//  minimal macro processor  -----------------------------------------------------------//

//  © Copyright Beman Dawes, 2011

//  The contents are licensed under the Boost Software License, Version 1.0.
//  See http://www.boost.org/LICENSE_1_0.txt

#include <boost/detail/lightweight_main.hpp>
#include <boost/assert.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <cctype>
#include <list>
#include <stack>
#include <map>
#include <boost/lexical_cast.hpp>

using std::cout;
using std::endl;
using std::string;
using std::memcmp;
using boost::lexical_cast;

/*
   TODO List

    *  Should simple_string be any non-whitespace character? Problem : $if a==b requires
       whitespace after a.
    *  Macro expansion should be pushed into state.
    *  Path, contents, of a file should be stored once and a shared_ptr should
       be kept in the context state.
    *  Can ~ be eliminated after command-start by calling string_(lookahead)
       or name_(lookahead), perhaps
       with a max length argument? Or just add a little function:
         bool is_next(const string& arg);  // true if found, advances if found
    *  environmental variable reference not implemented yet
    *  See how QB associates markers with file types, and provides overrides of same.
*/

//--------------------------------------------------------------------------------------//

namespace
{
  string          in_path;
  string          out_path;
  bool            verbose = false;

  int             error_count = 0;

  const string    default_command_start("$");
  string          in_file_command_start("$");
  const string    default_macro_marker("$");
  const char      def_command[] = "def";
  const char      include_command[] = "include";
  const char      snippet_command[] = "snippet";
  const char      if_command[] = "if";
  const char      elif_command[] = "elif";
  const char      else_command[] = "else";
  const char      endif_command[] = "endif";
  const bool      lookahead = true;

  std::ofstream   out;
 
  enum  text_termination { text_end, elif_clause, else_clause, endif_clause };

  struct context
  {
    string                  path;
    int                     line_number;
    string                  content;
    string::const_iterator  cur;            // current position
    string::const_iterator  end;            // past-the-end
    string                  command_start;  // command start marker; never empty()
    string                  command_end;    // command end marker; may be empty()
    string                  macro_marker;   // never empty()
    string                  snippet_id;     // may be empty()
  };

  typedef std::stack<context, std::list<context> > stack_type;
  stack_type state;  // context stack

  typedef std::map<string, string> macro_map;
  macro_map macro;

  
  text_termination text_(bool side_effects = true);
  bool expression_();
  string name_(bool lookahead_ = false);

//-------------------------------------  error  ----------------------------------------//

  void error(const string& msg)
  {
    ++error_count;
    if (state.empty() || !state.top().line_number)
      cout << in_path << ": error: " << msg << endl;
    else
      cout << state.top().path << '(' << state.top().line_number
           << "): error: " << msg << endl;
  }

//------------------------------------  advance  ---------------------------------------//

  void advance(std::ptrdiff_t n=1)
  {
    for(; n; --n)
    {
      if (*state.top().cur == '\n')
        ++state.top().line_number;
      ++state.top().cur;
    }
  }

//---------------------------------  skip_whitespace  ----------------------------------//

  inline void skip_whitespace()
  {
    for (; state.top().cur != state.top().end && std::isspace(*state.top().cur);
      ++state.top().cur)
    {}
  }

//-------------------------------  is_command_start  -----------------------------------//

 inline bool is_command_start()
 {
   return std::memcmp(&*state.top().cur, state.top().command_start.c_str(),
     state.top().command_start.size()) == 0;
 }

//-------------------------------  is_macro_marker  -----------------------------------//

 inline bool is_macro_marker()
 {
   return std::memcmp(&*state.top().cur, state.top().macro_marker.c_str(),
     state.top().macro_marker.size()) == 0;
 }

 //-----------------------------  advance_if_operator  ---------------------------------//
                                                         
 bool advance_if_operator(const string& op)
 {
   
   const char* begin = &*state.top().cur;
   const char* p(begin);
   while (isspace(*p))
     ++p;

   if (memcmp(p, op.c_str(), op.size()) != 0)
     return false;
   advance((p-begin) + op.size());
   return true;
 }



//-----------------------------------  load_file  --------------------------------------//

  bool load_file(const string& path, string& target)  // true if succeeds
  {
    std::ifstream in(path, std::ios_base::in|std::ios_base::binary );
    if (!in)
    {
      error("could not open input file \"" + path + '"');
      return false;
    }
    std::getline(in, target, '\0'); // read the whole file
    return true;
  }

//----------------------------------  new_context  -------------------------------------//

  bool new_context(const string& path,
    const string& command_start = default_command_start,
    const string& command_end = string(),
    const string& macro_marker = default_macro_marker
    )  // true if succeeds
  {
    state.push(context());
    state.top().path = path;
    state.top().line_number = 0;
    if (!load_file(path, state.top().content))
    {
      state.pop();
      return false;
    }
    ++state.top().line_number;
    state.top().cur = state.top().content.cbegin();
    state.top().end = state.top().content.cend();
    state.top().command_start = command_start;
    state.top().command_end = command_end;
    state.top().macro_marker = macro_marker;
    return true;
  }

//-----------------------------------  set_id  -----------------------------------------//

  void set_id(const string& id)
  {
    BOOST_ASSERT(state.top().cur == state.top().content.cbegin()); // precondition check
    state.top().snippet_id = id;
    string::size_type pos = state.top().content.find(
      state.top().command_start+"id "+id);
    if (pos == string::npos)
    {
      error("could not find snippet " + id + " in " + state.top().path);
      state.top().cur = state.top().end;
      return;
    }
    advance(pos);
    pos = state.top().content.find(state.top().command_start+"idend", pos);
    if (pos == string::npos)
    {
      error("could not find endid for snippet " + id + " in " + state.top().path);
      state.top().cur = state.top().end;
      return;
    }
    state.top().end = state.top().content.cbegin() + pos;
  }

//------------------------------------  setup  -----------------------------------------//

  bool setup(int argc, char* argv[])  // true if succeeds
  {
    bool ok = true;
    while (argc > 3) 
    {
      if (std::strchr(argv[1], '='))
      {
        string name(argv[1], std::strchr(argv[1], '='));
        string value(std::strchr(argv[1], '=')+1, argv[1]+std::strlen(argv[1]));
        macro[name] = value;
      }
      else if ( std::strcmp( argv[1], "-verbose" ) == 0 ) verbose = true;
      else
      { 
        cout << "Error: unknown option: " << argv[1] << "\n"; ok = false;
      }
      --argc;
      ++argv;
    }

    if (argc == 3)
    {
      in_path = argv[1];
      out_path = argv[2];
    }
    else
    {
      cout << "Error: missing path" << (argc < 2 ? "s" : "") << '\n';
      ok = false;
    }

    if (!ok)
    {
      cout <<
        "Usage: mmp [option...] input-path output-path\n"
        "  option: name=value   Define macro\n"
        "          -verbose     Report progress during processing\n"
        "Example: mmp -verbose VERSION=1.5 \"DESC=Beta 1\" index.html ..index.html\n"
        ;
    }
    return ok;
  }

//--------------------------------------------------------------------------------------//
//                                                                                      //
//                                   EBNF Grammar                                       //
//  uses ::= for production rules, {...} for zero or more, [...] for optional,          //
//  ~ for no whitespace allowed                                                         //
//                                                                                      //
//--------------------------------------------------------------------------------------//

/*
//$grammar

  text          ::= { macro-marker ~ macro-call
                    | command-start ~ command command-end
                    | character
                    }

  macro-call    ::= name ~ [macro-marker]               

  command-start ::= "$"                        // default
                  | string                     // string is replaceable

  command-end   ::= white-space {white-space}  // default
                  | string {white-space}       // string is replaceable

  string        ::= macro-marker ~ macro-call
                  | simple-string
                  | """{escaped-char}"""
                  | "$(" name ")"              // environmental variable reference

  simple-string ::= n-char{n-char}

  name          ::= n-char{n-char}

  n-char        ::= alnum-char | "_"

  command       ::= "def" name string
                  | "include" string           // string is filename
                  | "snippet" name string      // name is id, string is filename
                  | "if" if_body

  if_body       ::= expression text
                    {command-start "elif" command-end expression text}
                    [command-start "else" command-end text]
                    command-start "endif" command-end]


  primary_expr  ::= string "==" string
                  | string "!=" string
                  | string "<"   string
                  | string "<=" string
                  | string ">" string
                  | string ">=" string
                  | "(" expression ")"
  
  and-expr      ::= primary_expr {"&&" primary_expr}
                         
  expression    ::= and-expr {"||" and-expr}

//$
*/

//--------------------------------------------------------------------------------------//
//                                                                                      //
//                           Recursive Decent Parser                                    //
//     functions with names ending in underscore correspond to grammar productions      //
//                                                                                      //
//--------------------------------------------------------------------------------------//

//-------------------------------------  name_  ----------------------------------------//

  string name_(bool lookahead_)
  {
    skip_whitespace(); 

    string s;
    string::const_iterator it(state.top().cur);

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

//---------------------------------  simple_string_  -----------------------------------//

  inline string simple_string_(bool _lookahead=false)
  {
    return name_(_lookahead);
  }

//-----------------------------------  string_  ----------------------------------------//

  string string_()
  {
    skip_whitespace(); 

    if (*state.top().cur != '"')
      return simple_string_();

    int starting_line = state.top().line_number;

    advance();  // bypass the '"'

    string s;

    // store string
    for (; state.top().cur != state.top().end && *state.top().cur != '"'; advance())
    {
      s += *state.top().cur;
    }

    // maintain the state.top().cur invariant
    if (*state.top().cur == '"')
      advance();
    else
    {
      error("no closing quote for string that began on line "
        + lexical_cast<string>(starting_line));
    }

    return s;
  }

//----------------------------------  macro_call_  -------------------------------------//

  void macro_call_()
  {
    string macro_name(name_());
    macro_map::const_iterator it = macro.find(macro_name);
    if (it == macro.end())
      error(macro_name + " macro not found");
    out << it->second;
  }

//----------------------------------  primary_expr_  -----------------------------------//

  bool primary_expr_()  // true if evaluates to true
  {
    
    if (advance_if_operator("("))
    {
      bool expr = expression_();
      skip_whitespace();
      if (*state.top().cur == ')')
        advance();
      else
        error("syntax error: expected ')' to close expression");
      return expr;
    }

    string lhs(string_());
    skip_whitespace();
    string operation;
    if (std::strchr("=!<>", *state.top().cur))
    {
      operation += *state.top().cur;
      advance();
    }
    if (*state.top().cur == '=')
    {
      operation += '=';
      advance();
    }

    string rhs(string_());

    if (operation == "==")
      return lhs == rhs;
    else if (operation == "!=")
      return lhs != rhs;
    else if (operation == "<")
      return lhs < rhs;
    else if (operation == "<=")
      return lhs <= rhs;
    else if (operation == ">")
      return lhs > rhs;
    else if (operation == ">=")
      return lhs >= rhs;
    else
      error("expected a relational operator instead of \"" + operation + "\"");
    return false;
  }

//-----------------------------------  and_expr_  --------------------------------------//

  bool and_expr_()  // true if evaluates to true
  {
    bool result = primary_expr_();
  
    for (; advance_if_operator("&&");)
    {
      if (!primary_expr_())
        result = false;     
    }
    return result;
  }

//----------------------------------  expression_  -------------------------------------//

  bool expression_()  // true if evaluates to true
  {
    bool result = and_expr_();
  
    for (; advance_if_operator("||");)
    {
      if (and_expr_())
        result = true;     
    }
    return result;
  }

//-----------------------------------  if_body_  ---------------------------------------//

  void if_body_(bool side_effects)
  {
    int if_line_n = state.top().line_number;

    // expression text
    bool true_done = expression_();
    text_termination terminated_by = text_(true_done && side_effects);

    // {command-start "elif" command-end expression text}
    for (; terminated_by == elif_clause;)
    {
      terminated_by = text_((!true_done && (true_done = expression_())) && side_effects);
    }

    // [command-start "else" command-end text]
    if (terminated_by == else_clause)
      terminated_by = text_(!true_done && side_effects);

    // command-start "endif" command-end]
    if (terminated_by != endif_clause)
      error("expected \"endif\" to close \"if\" begun on line "
        + lexical_cast<string>(if_line_n));
  }

//-----------------------------------  command_  ---------------------------------------//

  bool command_(bool side_effects)  // return true if command found
  // postcondition: if true, state.top().cur updated to position past the command 
  {
    // command_start is present, so check for commands
    const char* p = &*state.top().cur + state.top().command_start.size();

    // def[ine] macro command
    if (memcmp(p, def_command, sizeof(def_command)-1) == 0
      && std::isspace(*(p+sizeof(def_command)-1)))
    {
      advance(state.top().command_start.size() + (sizeof(def_command)-1));
      string name(name_());
      string value(string_());
      if (side_effects)
        macro[name] = value;
    }

    // include command
    else if (memcmp(p, include_command, sizeof(include_command)-1) == 0
      && std::isspace(*(p+sizeof(include_command)-1)))
    {
      advance(state.top().command_start.size() + (sizeof(include_command)-1));
      string path(string_());
      if (side_effects)
      {
        new_context(path);
        text_();
        state.pop();
      }
    }

    // snippet command
    else if (memcmp(p, snippet_command, sizeof(snippet_command)-1) == 0
      && std::isspace(*(p+sizeof(snippet_command)-1)))
    {
      advance(state.top().command_start.size() + (sizeof(snippet_command)-1));
      string id(name_());
      string path(string_());
      if (side_effects)
      {
        new_context(path);
        set_id(id);
        text_();
        state.pop();
      }
    }

    // if command
    else if (memcmp(p, if_command, sizeof(if_command)-1) == 0
      && std::isspace(*(p+sizeof(if_command)-1)))
    {
      advance(state.top().command_start.size() + (sizeof(if_command)-1));
      if_body_(side_effects);
    }

    // false alarm
    else
      return false;

    // bypass trailing whitespace; this has the effect of avoiding the output of
    // spurious whitespace such as a newline at the end of a command
    for (;state.top().cur != state.top().end && std::isspace(*state.top().cur);
      advance()) {} 

    return true;
  }

//------------------------------------- text_  -----------------------------------------//

  text_termination text_(bool side_effects)
  {
    BOOST_ASSERT(!state.empty());  // failure indicates program logic error

    //if (verbose)
    //  cout << "Processing " << state.top().path << "...\n";

    for(; state.top().cur != state.top().end;)
    {
      if (is_command_start())
      {
        // text_ is terminated by an elif, else, or endif
        const char* p = &*state.top().cur + state.top().command_start.size();
        if (memcmp(p, elif_command, sizeof(elif_command)-1) == 0
          && std::isspace(*(p+sizeof(elif_command)-1)))
        {
          advance(state.top().command_start.size() + (sizeof(elif_command)-1));
          return elif_clause;
        }
        if (memcmp(p, else_command, sizeof(else_command)-1) == 0
          && std::isspace(*(p+sizeof(else_command)-1)))
        {
          advance(state.top().command_start.size() + (sizeof(else_command)-1));
          return else_clause;
        }
        if (memcmp(p, endif_command, sizeof(endif_command)-1) == 0
          && std::isspace(*(p+sizeof(endif_command)-1)))
        {
          advance(state.top().command_start.size() + (sizeof(endif_command)-1));
          return endif_clause;
        }

        if (command_(side_effects))
          continue;
      }

      if (is_macro_marker())
      {
        advance(state.top().macro_marker.size()); 

        if (side_effects)
          macro_call_();
        else
          name_();
        continue;
      }

      if (side_effects)
        out << *state.top().cur;
      advance();
    }

    //if (verbose)
    //  cout << "  " << state.top().path << " complete\n";

    return text_end;
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
    error("could not open output file " + out_path);
    goto done;
  }

  if (!new_context(in_path, in_file_command_start))
    goto done;

  text_();

  if (verbose)
  {
    cout << "Dump macro definitions:\n";
    for (macro_map::const_iterator it = macro.cbegin();
      it != macro.cend(); ++it)
    {
      cout << "  " << it->first << ": \"" << it->second << "\"\n";
    }
  }

done:

  cout << error_count << " error(s) detected\n";

  return error_count ? 1 :0;
}
