// Copyright (c) the JPEG XL Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef TOOLS_CMDLINE_H_
#define TOOLS_CMDLINE_H_

#include <stdio.h>
#include <string.h>
#include <memory>
#include <string>
#include <vector>

#include "jxl/base/status.h"

namespace jpegxl {
namespace tools {

class CommandLineParser {
 public:
  typedef size_t OptionId;

  // An abstract class for defining command line options.
  class CmdOptionInterface {
   public:
    CmdOptionInterface() = default;
    virtual ~CmdOptionInterface() = default;

    // Return a string with the option name or available flags.
    virtual std::string help_flags() const = 0;

    // Return the help string if any, or nullptr if no help string.
    virtual const char* help_text() const = 0;

    // Return whether the option was passed.
    virtual bool matched() const = 0;

    // Returns whether this option matches the passed command line argument.
    virtual bool Match(const char* arg) const = 0;

    // Parses the option. The passed i points to the argument with the flag
    // that matches either the short or the long name.
    virtual bool Parse(int argc, const char* argv[], int* i) = 0;
  };

  // Add a positional argument. Returns the id of the added option or
  // kOptionError on error.
  OptionId AddPositionalOption(const char* name, const char* help_text,
                               const char** storage) {
    options_.emplace_back(new CmdOptionPositional(name, help_text, storage));
    return options_.size() - 1;
  }

  // Add an option with a value of type T. The option can be passed as
  // '-s <value>' or '--long value' or '--long=value'. The CommandLineParser
  // parser will call the function parser with the string pointing to '<value>'
  // in either case. Returns the id of the added option or kOptionError on
  // error.
  template <typename T>
  OptionId AddOptionValue(char short_name, const char* long_name,
                          const char* metavar, const char* help_text,
                          T* storage, bool(parser)(const char*, T*)) {
    options_.emplace_back(new CmdOptionFlag<T>(short_name, long_name, metavar,
                                               help_text, storage, parser));
    return options_.size() - 1;
  }

  // Add a flag without a value. Returns the id of the added option or
  // kOptionError on error.
  template <typename T>
  OptionId AddOptionFlag(char short_name, const char* long_name,
                         const char* help_text, T* storage, bool(parser)(T*)) {
    options_.emplace_back(new CmdOptionFlag<T>(short_name, long_name, help_text,
                                               storage, parser));
    return options_.size() - 1;
  }

  const CmdOptionInterface* GetOption(OptionId id) const {
    JXL_ASSERT(id < options_.size());
    return options_[id].get();
  }

  // Print the help message.
  void PrintHelp() const;

  // Parse the command line.
  bool Parse(int argc, const char* argv[]);

  // Return the remaining positional args
  std::vector<const char*> PositionalArgs() const;

 private:
  // A positional argument.
  class CmdOptionPositional : public CmdOptionInterface {
   public:
    CmdOptionPositional(const char* name, const char* help_text,
                        const char** storage)
        : name_(name), help_text_(help_text), storage_(storage) {}

    std::string help_flags() const override { return name_; }
    const char* help_text() const override { return help_text_; }
    bool matched() const override { return matched_; }

    // Only match non-flag values. This means that you can't pass '-foo' as a
    // positional argument, but it helps with detecting when passed a flag with
    // a typo.
    bool Match(const char* arg) const override {
      return !matched_ && arg[0] != '-';
    }

    bool Parse(const int argc, const char* argv[], int* i) override {
      *storage_ = argv[*i];
      (*i)++;
      matched_ = true;
      return true;
    }

   private:
    const char* name_;
    const char* help_text_;
    const char** storage_;

    bool matched_{false};
  };

  // A class for handling an option flag like '-v' or '--foo=bar'.
  template <typename T>
  class CmdOptionFlag : public CmdOptionInterface {
   public:
    // Construct a flag that doesn't take any value, for example '-v' or
    // '--long'. Passing a value to it raises an error.
    CmdOptionFlag(char short_name, const char* long_name, const char* help_text,
                  T* storage, bool(parser)(T*))
        : short_name_(short_name),
          long_name_(long_name),
          long_name_len_(long_name ? strlen(long_name) : 0),
          metavar_(nullptr),
          help_text_(help_text),
          storage_(storage) {
      parser_.parser_no_value_ = parser;
    }

    // Construct a flag that expects a value to be passed.
    CmdOptionFlag(char short_name, const char* long_name, const char* metavar,
                  const char* help_text, T* storage,
                  bool(parser)(const char* arg, T*))
        : short_name_(short_name),
          long_name_(long_name),
          long_name_len_(long_name ? strlen(long_name) : 0),
          metavar_(metavar ? metavar : ""),
          help_text_(help_text),
          storage_(storage) {
      parser_.parser_with_arg_ = parser;
    }

    std::string help_flags() const override {
      std::string ret;
      if (short_name_) {
        ret += std::string("-") + short_name_;
        if (metavar_) ret += std::string(" ") + metavar_;
        if (long_name_) ret += ", ";
      }
      if (long_name_) {
        ret += std::string("--") + long_name_;
        if (metavar_) ret += std::string("=") + metavar_;
      }
      return ret;
    }
    const char* help_text() const override { return help_text_; }
    bool matched() const override { return matched_; }

    bool Match(const char* arg) const override {
      return MatchShort(arg) || MatchLong(arg);
    }

    bool Parse(const int argc, const char* argv[], int* i) override {
      matched_ = true;
      if (MatchLong(argv[*i])) {
        const char* arg = argv[*i] + 2 + long_name_len_;
        if (arg[0] == '=') {
          if (metavar_) {
            // Passed '--long_name=...'.
            (*i)++;
            // Skip over the '=' on the LongMatch.
            arg += 1;
            return (*parser_.parser_with_arg_)(arg, storage_);
          } else {
            fprintf(stderr, "--%s didn't expect any argument passed to it.\n",
                    argv[*i]);
            return false;
          }
        }
      }
      // In any other case, it passed a -s or --long_name
      (*i)++;
      if (metavar_) {
        if (argc <= *i) {
          fprintf(stderr, "--%s expected an argument but none passed.\n",
                  argv[*i - 1]);
          return false;
        }
        return (*parser_.parser_with_arg_)(argv[(*i)++], storage_);
      } else {
        return (*parser_.parser_no_value_)(storage_);
      }
    }

   private:
    // Returns whether arg matches the short_name flag of this option.
    bool MatchShort(const char* arg) const {
      if (!short_name_ || arg[0] != '-') return false;
      return arg[1] == short_name_ && arg[2] == 0;
    }

    // Returns whether arg matches the long_name flag of this option,
    // potentially with an argument passed to it.
    bool MatchLong(const char* arg) const {
      if (!long_name_ || arg[0] != '-' || arg[1] != '-') return false;
      arg += 2;  // Skips the '--'
      if (strncmp(long_name_, arg, long_name_len_) != 0) return false;
      arg += long_name_len_;
      // Allow "--long_name=foo" and "--long_name" as long matches.
      return arg[0] == 0 || arg[0] == '=';
    }

    // A short option passed as '-X' where X is the char. A value of 0 means
    // no short option.
    const char short_name_;

    // A long option name passed as '--long' where 'long' is the name of the
    // option.
    const char* long_name_;
    size_t long_name_len_;

    // The text to display when referring to the value passed to this flag, for
    // example "N" in the flag '--value N'. If null, this flag accepts no value
    // and therefor no value must be passed.
    const char* metavar_;

    // The help string for this flag.
    const char* help_text_;

    // The pointer to the storage of this flag used when parsing.
    T* storage_;

    // The function to use to parse the value when matched. The function used is
    // parser_with_arg_ when metavar_ is not null (and the value string will be
    // used) or parser_no_value_ when metavar_ is null.
    union {
      bool (*parser_with_arg_)(const char*, T*);
      bool (*parser_no_value_)(T*);
    } parser_;

    // Whether this flag was matched.
    bool matched_{false};
  };

  const char* program_name_{nullptr};

  std::vector<std::unique_ptr<CmdOptionInterface>> options_;
};

}  // namespace tools
}  // namespace jpegxl

#endif  // TOOLS_CMDLINE_H_
