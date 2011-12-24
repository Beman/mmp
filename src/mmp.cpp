//  minimal macro processor  -----------------------------------------------------------//

//  © Copyright Beman Dawes, 2011

//  Licensed under the Boost Software License, Version 1.0.
//  See http://www.boost.org/LICENSE_1_0.txt

#define _CRT_SECURE_NO_WARNINGS

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
#include <cstdlib>   // for getenv()
#include <boost/lexical_cast.hpp>

using std::cout;
using std::endl;
using std::string;
using std::memcmp;
using boost::lexical_cast;

/*
   TODO List

    *  Probable bug if file starts with macro. Add test case of file started by macro.
    *  Throw on load_file() failure or each use check new_context() return.
    *  Optimization, better error messages: Don't invoke macro_() for $def, etc.
    *  Path, contents, of a file should be stored once and a shared_ptr should
       be kept in the context state.
    *  environmental variable reference not tested yet
    *  See how QB associates markers with file types, and provides overrides of same.
*/

//--------------------------------------------------------------------------------------//

namespace
{
  string          in_path;
  string          out_path;
  bool            verbose = false;
  bool            log_input = false;
  bool            log_output = false;

  int             error_count = 0;

  const string    default_command_start("$");
  string          in_file_command_start("$");
  const string    default_macro_start("$");
  const string    default_macro_end(";");
  const bool      no_macro_check = false;

  std::ofstream   out;
 
  enum  text_termination { text_end, elif_clause, else_clause, endif_clause };

  struct context
  {
    string                  path;
    int                     line_number;
    string                  content;
    string::const_iterator  cur;            // current position
    string::const_iterator  end;            // past-the-end
    string                  command_start;  // command start marker; !empty()
    string                  command_end;    // command end marker; may be empty()
    string                  macro_start_;   // !empty()
    string                  macro_end_;     // !empty()
    string                  snippet_id;     // may be empty()
  };

  typedef std::stack<context, std::list<context> > stack_type;
  stack_type state;  // context stack

  typedef std::map<string, string> macro_map;
  macro_map macro;
  
  text_termination text_(bool side_effects = true);
  bool expression_();
  string name_();
  void macro_call_();
  bool is_macro_start();
  bool is_macro_end();

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

  void advance(std::ptrdiff_t n=1, bool macro_check=true)
  {
    for(; n; --n)
    {
      if (*state.top().cur == '\n')
        ++state.top().line_number;
      ++state.top().cur;

      while (state.top().cur == state.top().end && state.size() > 1)
        state.pop();

      if (log_input)
      {
        cout << "  Input: ";
        if (state.top().cur == state.top().end)
          cout << "end\n";
        else
          cout << *state.top().cur << "\n";
      }

      if (macro_check && state.top().cur != state.top().end && is_macro_start())
      {
        macro_call_();
      }
    }
  }

//---------------------------------  skip_whitespace  ----------------------------------//

  inline void skip_whitespace()
  {
    for (; state.top().cur != state.top().end && std::isspace(*state.top().cur);
      advance()) {}
  }

//--------------------------------  is_command_start  ----------------------------------//

 inline bool is_command_start()
 {
   return std::memcmp(&*state.top().cur, state.top().command_start.c_str(),
     state.top().command_start.size()) == 0;
 }

//--------------------------------  is_macro_start  -----------------------------------//

 inline bool is_macro_start()
 {
   return std::memcmp(&*state.top().cur, state.top().macro_start_.c_str(),
     state.top().macro_start_.size()) == 0;
 }

//---------------------------------  is_macro_end  ------------------------------------//

 inline bool is_macro_end()
 {
   return std::memcmp(&*state.top().cur, state.top().macro_end_.c_str(),
     state.top().macro_end_.size()) == 0;
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
    const string& macro_start = default_macro_start,
    const string& macro_end = default_macro_end
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
    state.top().macro_start_ = macro_start;
    state.top().macro_end_ = macro_end;
    return true;
  }

//--------------------------------  push_content  --------------------------------------//

  void push_content(const string& name, const string& content)
  {
    if (verbose)
      cout << "pushing " << name << " with content \"" << content << '"' <<endl;

    context cx;

    cx.path = name;
    cx.line_number = 1;
    cx.content = content;
    cx.command_start = state.top().command_start; 
    cx.command_end = state.top().command_end; 
    cx.macro_start_ = state.top().macro_start_; 
    cx.macro_end_ = state.top().macro_end_;

    state.push(cx);
    state.top().cur = state.top().content.cbegin();
    state.top().end = state.top().content.cend();
  }

//-----------------------------------  set_id  -----------------------------------------//

  void set_id(const string& id)
  {
    BOOST_ASSERT(state.top().cur == state.top().content.cbegin()); // precondition check
    state.top().snippet_id = id;

    // find the start of the id command
    string command(state.top().command_start + "id " + id + "=");
    string::size_type pos = state.top().content.find(command);
    if (pos == string::npos)
    {
      error("Could not find snippet " + id + " in " + state.top().path);
      state.top().cur = state.top().end;
      return;
    }

    // set cur to start of snippet
    advance(pos + command.size(), no_macro_check);

    // find the end of the snippet
    pos = state.top().content.find(state.top().command_start+"endid", pos);
    if (pos == string::npos)
    {
      error("Could not find " + state.top().command_start + "endid for snippet "
        + id + " in " + state.top().path);
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
      else if ( std::strcmp( argv[1], "-log-input" ) == 0 ) log_input = true;
      else if ( std::strcmp( argv[1], "-log-output" ) == 0 ) log_output = true;
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
//                                   EBNF Grammars                                      //
//                                                                                      //
//  Notation: ::= for production rule, | for alternative, {...} for zero or more,       //
//  [...] for optional, "..." for instances of production rule "literal"                //
//                                                                                      //
//  Whitespace is permitted where grammar elements are separated by whitespace          // 
//                                                                                      //
//--------------------------------------------------------------------------------------//

/*
  //  text grammar

  $id text=

  text           ::= {character}
                 
  character      ::= {command-start command-element}buffer-character
  
  command-start  ::= "$"                        // replaceable; see docs

  command-element ::= command-end               // null command; push command-start
                    | command-body [command-end] // use command-end to avoid whitespace
                                                // after a command-body
                  
  command-end    ::= ";"                        // replaceable; see docs
                   
  command-body   ::= "def" name string          // define macro-name, macro-value
                   | "include" string           // include path
                   | "snippet" name string      // include snippet id, path
                   | "if" if_body
                   | "env" name                 // push value of environmental
                                                // variable name if found, otherwise warn
                   | name                       // macro-call; if name is a macro-name,
                                                // push macro-value, otherwise warn
               
  if_body        ::= expression text
                     {command-start "elif" expression text}
                     [command-start "else" text]
                     command-start "endif"
                 
  literal        ::= {character}                // characters have given value
                                                // alphabetic terminated by non-alphabetic 
                                                // others terminated by end of given value

  name           ::= character{character}       // terminated by !(isalnum || "_")

  string         ::= name
                   | """{string-char}"""
                 
  string-char    ::= "\""                       // escape for " character
                   | "\r"                       // escape for return
                   | "\n"                       // escape for newline
                   | character
                          
  expression     ::= and-expr {"||" and-expr}
                 
  and-expr       ::= primary_expr {"&&" primary_expr}
                 
  primary_expr   ::= string "==" string
                   | string "!=" string
                   | string "<"   string
                   | string "<=" string
                   | string ">" string
                   | string ">=" string
                   | "(" expression ")"
   $endid

  --------------------------------------------------------------------------------------

  //  snippet grammar

  $id snippet=
  snippet     ::= command_start "id " name "=" {character} command-start "endid"
  $endid

*/

//--------------------------------------------------------------------------------------//
//                                                                                      //
//                           Recursive Decent Parsers                                   //
//     functions with names ending in underscore correspond to grammar productions      //
//                                                                                      //
//--------------------------------------------------------------------------------------//

//--------------------------------------------------------------------------------------//
//                                 macro-call parser                                    //
//--------------------------------------------------------------------------------------//

string macro_name();

//-----------------------------------  macro_call  -------------------------------------//

void macro_call_()
{
  advance(state.top().macro_start_.size(), no_macro_check);

  // null macro
  if (is_macro_end())
  {
    advance(state.top().macro_end_.size(), no_macro_check);
    push_content("null macro", state.top().macro_start_);
  }

  // enviromental variable reference
  else if (state.top().cur != state.top().end && *state.top().cur == '(')
  {
    advance(1, no_macro_check);
    string name(macro_name());
    const char* p = std::getenv(name.c_str());
    if (state.top().cur != state.top().end && *state.top().cur == ')')
      advance(1, no_macro_check);
    else
      error("missing closing )");
    if (is_macro_end())
      advance(state.top().macro_end_.size(), no_macro_check);
    else
      error("missing " + state.top().macro_end_);

    if (p)
      push_content(state.top().macro_start_+"("+ name +")"+state.top().macro_end_, p);
    else
    {
      error("not found: " + state.top().macro_start_
        + "(" + name + ")" + state.top().macro_end_);
      push_content(state.top().macro_start_
        + "(" + name + ")" + state.top().macro_end_, state.top().macro_start_
        + "(" + name + ")" + state.top().macro_end_);
    }
  }

  // macro-name [macro-end]
  else
  {
    string name(macro_name());
    if (is_macro_end())
    {
      advance(state.top().macro_end_.size(), no_macro_check);
      macro_map::const_iterator it(macro.find(name));
      if (it != macro.cend())  // macro found
        push_content(state.top().macro_start_ + name + state.top().macro_end_,
          it->second);
      else  // macro not found so push advanced over characters
        push_content(state.top().macro_start_ + name + state.top().macro_end_,
          state.top().macro_start_ + name + state.top().macro_end_);
    }
    else  // no macro-end so push advanced over characters
      push_content(state.top().macro_start_ + name, state.top().macro_start_ + name);
  }
}

//------------------------------------  macro_name  ------------------------------------//

string macro_name()
{
  string name;

  while (state.top().cur != state.top().end
    && (std::isalnum(*state.top().cur) || *state.top().cur == '_'))
  {
    name += *state.top().cur;
    advance();
  }

  return name;
}

//--------------------------------------------------------------------------------------//
//                                    text parser                                       //
//--------------------------------------------------------------------------------------//

//-------------------------------------  name_  ----------------------------------------//

  string name_()
  {
    skip_whitespace(); 

    string s;

    // store string
    for (; state.top().cur != state.top().end &&
      (std::isalnum(*state.top().cur) || *state.top().cur == '_');
      advance())
    {
      s += *state.top().cur;
    }

    return s;
  }

//---------------------------------  simple_string_  -----------------------------------//

  inline string simple_string_()
  {
    return name_();
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

  void command_(const string& whitespace, const string& command, bool side_effects) 
  {
    // def[ine] macro command
    if (command == "def")
    {
      string name(name_());
      string value(string_());
      if (side_effects)
        macro[name] = value;
    }

    // include command
    else if (command == "include")
    {
      string path(string_());
      if (side_effects)
      {
        new_context(path);
        text_();
      }
    }

    // snippet command
    else if (command == "snippet")
    {
      string id(name_());
      string path(string_());
      if (side_effects)
      {
        new_context(path);
        set_id(id);
        text_();
      }
    }

    // if command
    else if (command == "if")
      if_body_(side_effects);

    // not a command
    else
    {
      if (side_effects)
      {
        out << state.top().command_start << whitespace << command;

        if (log_input)
          cout << "  Output: " << state.top().command_start << whitespace << command
               << endl;
      }
      return;
    }

    // bypass trailing whitespace; this has the effect of avoiding the output of
    // spurious whitespace such as a newline at the end of a command
    skip_whitespace(); 
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
        advance(state.top().command_start.size(), no_macro_check);

        string whitespace;  // in case this isn't really a command

        for (; state.top().cur != state.top().end && std::isspace(*state.top().cur);
            advance())
          whitespace += *state.top().cur;

        string command(name_());

        // text_ is terminated by an elif, else, or endif
        if (command == "elif")
          return elif_clause;
        else if (command == "else")
          return else_clause;
        else if (command == "endif")
          return endif_clause;
        else
         command_(whitespace, command, side_effects);
      }
      else  // character
      {
        if (side_effects)
        {
          out << *state.top().cur;;

          if (log_input)
            cout << "  Output: " << *state.top().cur << endl;
        }
        advance();
      }
    }

    //if (verbose)
    //  cout << "  " << state.top().path << " complete\n";

    BOOST_ASSERT(state.size() == 1);  // failure indicates program logic error
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
