/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef CPROVER_MESSAGE_H

#define CPROVER_MESSAGE_H

#include <iostream>
#include <string>
#include <util/location.h>

class message_handlert
{
public:
  virtual void print(unsigned level, const std::string &message) = 0;

  virtual void
  print(unsigned level, const std::string &message, const locationt &location);

  virtual ~message_handlert() = default;
};

class messaget
{
public:
  virtual void print(const std::string &message)
  {
    print(1, message);
  }

  void status(const std::string &message)
  {
    print(6, message);
  }

  void result(const std::string &message)
  {
    print(4, message);
  }

  void warning(const std::string &message)
  {
    print(2, message);
  }

  void status(const std::string &message, const std::string &file)
  {
    locationt location;
    location.set_file(file);
    print(6, message, location);
  }

  void error(const std::string &message)
  {
    print(1, message);
  }

  void error(const std::string &message, const std::string &file)
  {
    locationt location;
    location.set_file(file);
    print(1, message, location);
  }

  virtual void print(unsigned level, const std::string &message);

  virtual void
  print(unsigned level, const std::string &message, const locationt &location);

  virtual void set_message_handler(message_handlert *_message_handler);
  virtual void set_verbosity(unsigned _verbosity)
  {
    verbosity = _verbosity;
  }

  virtual unsigned get_verbosity() const
  {
    return verbosity;
  }

  messaget()
  {
    message_handler = (message_handlert *)nullptr;
    verbosity = 10;
  }

  messaget(message_handlert &_message_handler)
  {
    message_handler = &_message_handler;
    verbosity = 10;
  }

  virtual ~messaget() = default;

  // Levels:
  //
  //  0 none
  //  1 only errors
  //  2 + warnings
  //  4 + results
  //  6 + phase information
  //  8 + statistical information
  //  9 + progress information
  // 10 + debug info

  message_handlert *get_message_handler()
  {
    return message_handler;
  }

protected:
  unsigned verbosity;
  message_handlert *message_handler;
};

namespace esbmc::global {
  extern messaget _msg; // use this if you know what you are doing
}
// Magic definitions to help the use of messages during the program
#define _TO_MSG(X) std::stringstream _convert_ss_to_str; _convert_ss_to_str << X;
#define _CALL_MSG(MODE,X) _TO_MSG(X); esbmc::global::_msg.##MODE(_convert_ss_to_str.str());
#define DEBUG(X) _CALL_MSG(debug, X)
#define WARNING(X) _CALL_MSG(warning, X)
#define ERROR(X) _CALL_MSG(error, X)
#define STATUS(X) _CALL_MSG(status, X)
#define PRINT(X) _CALL_MSG(print, X)

#endif
