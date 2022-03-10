/**
 * Copyright: (2022) Bill Eggert b eggert at proton mail dot com
 * License: BSD-3
 *
 * This code is taken directly from Ben Strasser's excellent 'fast-cpp-csv-parser'
 * https://github.com/ben-strasser/fast-cpp-csv-parser, and modified such that
 * it does not throw. This makes it usable in situations where -fno-exceptions
 * is required.
 *
 * This library is intended to sacrifice some speed in order for safety.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef CSV_H
#define CSV_H

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <istream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace io
{

namespace error
{

static constexpr int max_file_name_length = 255;

class error
{
public:
    virtual void format_error_message() = 0;

    void set_file_name(const std::string &file_name_)           { file_name = file_name_; }
    void set_file_line(int file_line_)                          { file_line = file_line_;  }
    void set_errno(int errno__)                                 { errno_ = errno__;       }
    void set_column_name(const std::string &column_name_)       { column_name = column_name_; }
    void set_column_content(const std::string &column_content_) { column_content = column_content_; }

    const std::string &get_file_name() const      { return file_name; }
    int get_file_line() const                     { return file_line; }
    int get_errno() const                         { return errno_;    }
    const std::string &get_column_name() const    { return column_name; }
    const std::string &get_column_content() const { return column_content; }

    void set_error(const std::string &error_) { error = error_; }
    const std::string &get_error() const { return error; }

protected:
    std::string error;

private:
    std::string file_name;
    std::string column_name;
    std::string column_content;
    int file_line;
    int errno_;
};

class internal_error : public error
{
public:
    void format_error_message() override {}
};

class cannot_open_file : public error
{
public:
    void format_error_message() override
    {
        std::stringstream ss;
        ss << "Can not open file \"" << get_file_name() << "\"";
        if (get_errno() != 0)
        {
            ss << " because \"" << std::strerror(get_errno()) << "\".";
        }

        error = ss.str();
    }
};

class line_length_limit_exceeded : public error
{
public:
    void format_error_message() override
    {
        std::stringstream ss;
        ss << "Line number " << get_file_line() << " in file \""
           << get_file_name() << "\" exceeds the maximum length of 2^24-1.";

        error = ss.str();
    }
};

} // end namespace error

class ByteSourceBase
{
public:
    virtual int read(char *buffer, int size) = 0;
    virtual ~ByteSourceBase() = default;
};

namespace detail
{

class OwningStdIOByteSourceBase : public ByteSourceBase
{
public:
    explicit OwningStdIOByteSourceBase(FILE *file_):
        file(file_)
    {
        /*
         * Tell the std library that we want to do the buffering ourself.
         */
        std::setvbuf(file, 0, _IONBF, 0);
    }

    ~OwningStdIOByteSourceBase()
    {
        std::fclose(file);
    }

    int read(char *buffer, int size) override
    {
        return std::fread(buffer, 1, size, file);
    }

private:
    FILE *file;
};

class NonOwningIStreamByteSource : public ByteSourceBase
{
public:
    explicit NonOwningIStreamByteSource(std::istream &in_):
        in(in_)
    {}

    ~NonOwningIStreamByteSource() = default;

    int read(char *buffer, int size) override
    {
        in.read(buffer, size);
        return in.gcount();
    }

private:
       std::istream &in;
};

class NonOwningStringByteSource : public ByteSourceBase
{
public:
    NonOwningStringByteSource(
        const char *str_,
        long long size):
            str(str_),
            remaining_byte_count(size)
    {}

    ~NonOwningStringByteSource() = default;

    int read(char *buffer, int desired_byte_count) override
    {
        int to_copy_byte_count = desired_byte_count;
        if(remaining_byte_count < to_copy_byte_count)
        {
            to_copy_byte_count = remaining_byte_count;
        }

        std::memcpy(buffer, str, to_copy_byte_count);
        remaining_byte_count -= to_copy_byte_count;
        str += to_copy_byte_count;

        return to_copy_byte_count;
    }

private:
    const char *str;
    long long remaining_byte_count;
};

class SynchronousReader
{
public:
    void init(std::unique_ptr<ByteSourceBase> arg_byte_source)
    {
        byte_source = std::move(arg_byte_source);
    }

    bool is_valid() const
    {
        return byte_source != nullptr;
    }

    void start_read(
        char *arg_buffer,
        int arg_desired_byte_count)
    {
        buffer = arg_buffer;
        desired_byte_count = arg_desired_byte_count;
    }

    int finish_read()
    {
        return byte_source->read(buffer, desired_byte_count);
    }

private:
    std::unique_ptr<ByteSourceBase>byte_source;
    char *buffer;
    int desired_byte_count;
};

} // end namespace detail

////////////////////////////////////////////////////////////////////////////
//                               LineReader                               //
////////////////////////////////////////////////////////////////////////////

class LineReader
{
private:
    static const int block_len = 1<<20;
    std::unique_ptr<char[]>buffer;
    detail::SynchronousReader reader;
    int data_begin;
    int data_end;
    char file_name[error::max_file_name_length+1];
    unsigned file_line;

private:
    static std::unique_ptr<ByteSourceBase> open_file(
        const char *file_name,
        std::shared_ptr<error::error> &err)
    {
        if (err)
        {
            return nullptr;
        }

        /*
         * We open the file in binary mode as it makes no difference under *nix
         * and under Windows we handle \r\n newlines ourself.
         */
        FILE *file = std::fopen(file_name, "rb");
        if(file == 0)
        {
            /*
             * store errno as soon as possible, doing it after constructor
             * call can fail.
             */
            int x = errno;
            err = std::make_shared<error::cannot_open_file>();
            err->set_errno(x);
            err->set_file_name(file_name);

            return nullptr;
        }

        return std::unique_ptr<ByteSourceBase>(new detail::OwningStdIOByteSourceBase(file));
    }

    bool init(std::unique_ptr<ByteSourceBase> byte_source)
    {
        if (!byte_source)
        {
            return false;
        }

        file_line = 0;
        buffer = std::unique_ptr<char[]>(new char[3*block_len]);
        data_begin = 0;
        data_end = byte_source->read(buffer.get(), 2*block_len);

        /* Ignore UTF-8 BOM */
        if(data_end >= 3 && buffer[0] == '\xEF' && buffer[1] == '\xBB' && buffer[2] == '\xBF')
        {
            data_begin = 3;
        }

        if(data_end == 2*block_len)
        {
            reader.init(std::move(byte_source));
            reader.start_read(buffer.get() + 2*block_len, block_len);
        }

        return true;
    }

public:
    LineReader() = delete;
    LineReader(const LineReader&) = delete;
    LineReader&operator=(const LineReader&) = delete;

    explicit LineReader(
        std::shared_ptr<error::error> &err,
        const char *file_name_)
    {
        set_file_name(file_name_);
        init(open_file(file_name, err));
    }

    explicit LineReader(
        std::shared_ptr<error::error> &err,
        const std::string &file_name)
    {
        set_file_name(file_name.c_str());
        init(open_file(file_name.c_str(), err));
    }

    LineReader(
        std::shared_ptr<error::error> &,
        const char *file_name,
        std::unique_ptr<ByteSourceBase> byte_source)
    {
        set_file_name(file_name);
        init(std::move(byte_source));
    }

    LineReader(
        std::shared_ptr<error::error> &,
        const std::string &file_name,
        std::unique_ptr<ByteSourceBase> byte_source)
    {
        set_file_name(file_name.c_str());
        init(std::move(byte_source));
    }

    LineReader(
        std::shared_ptr<error::error> &,
        const char *file_name,
        const char *data_begin,
        const char *data_end)
    {
        set_file_name(file_name);
        init(std::unique_ptr<ByteSourceBase>(
            new detail::NonOwningStringByteSource(data_begin, data_end-data_begin)));
    }

    LineReader(
        std::shared_ptr<error::error> &,
        const std::string &file_name,
        const char *data_begin,
        const char *data_end)
    {
        set_file_name(file_name.c_str());
        init(std::unique_ptr<ByteSourceBase>(
            new detail::NonOwningStringByteSource(data_begin, data_end-data_begin)));
    }

    LineReader(
        std::shared_ptr<error::error> &,
        const char *file_name,
        FILE *file)
    {
        set_file_name(file_name);
        init(std::unique_ptr<ByteSourceBase>(
            new detail::OwningStdIOByteSourceBase(file)));
    }

    LineReader(
        std::shared_ptr<error::error> &,
        const std::string &file_name,
        FILE *file)
    {
        set_file_name(file_name.c_str());
        init(std::unique_ptr<ByteSourceBase>(
            new detail::OwningStdIOByteSourceBase(file)));
    }

    LineReader(
        std::shared_ptr<error::error> &,
        const char *file_name,
        std::istream &in)
    {
        set_file_name(file_name);
        init(std::unique_ptr<ByteSourceBase>(
            new detail::NonOwningIStreamByteSource(in)));
    }

    LineReader(
        std::shared_ptr<error::error> &,
        const std::string &file_name,
        std::istream &in)
    {
        set_file_name(file_name.c_str());
        init(std::unique_ptr<ByteSourceBase>(
            new detail::NonOwningIStreamByteSource(in)));
    }

    void set_file_name(const char *file_name_)
    {
        if(file_name != nullptr)
        {
            strncpy(file_name, file_name_, sizeof(file_name));
            file_name[sizeof(file_name)-1] = '\0';
        }
        else
        {
            file_name[0] = '\0';
        }
    }

    void set_file_name(const std::string &file_name_)
    {
        set_file_name(file_name_.c_str());
    }

    const char *get_truncated_file_name() const
    {
        return file_name;
    }

    void set_file_line(unsigned file_line_)
    {
        file_line = file_line_;
    }

    unsigned get_file_line() const
    {
        return file_line;
    }

    char *next_line(std::shared_ptr<error::error> &err)
    {
        if (err)
        {
            return nullptr;
        }

        if(data_begin == data_end)
        {
            return nullptr;
        }

        ++file_line;

        if (data_begin >= data_end)
        {
            err = std::make_shared<error::internal_error>();
            err->set_error("Internal error: data_begin >= data_end");
            return nullptr;
        }
        if (data_end > block_len*2)
        {
            err = std::make_shared<error::internal_error>();
            err->set_error("Internal error: data_end > block_len*2\n");
            return nullptr;
        }

        if (data_begin >= block_len)
        {
            std::memcpy(buffer.get(), buffer.get()+block_len, block_len);
            data_begin -= block_len;
            data_end -= block_len;
            if(reader.is_valid())
            {
                data_end += reader.finish_read();
                std::memcpy(buffer.get()+block_len, buffer.get()+2*block_len, block_len);
                reader.start_read(buffer.get() + 2*block_len, block_len);
            }
        }

        int line_end = data_begin;
        while(line_end != data_end && buffer[line_end] != '\n')
        {
            ++line_end;
        }

        if(line_end - data_begin + 1 > block_len)
        {
            err = std::make_shared<error::line_length_limit_exceeded>();
            err->set_file_name(file_name);
            err->set_file_line(file_line);
            return nullptr;
        }

        if(line_end != data_end && buffer[line_end] == '\n')
        {
            buffer[line_end] = '\0';
        }
        else
        {
            /*
             * Some files are missing the newline at the end of the
             * last line
             */
            ++data_end;
            buffer[line_end] = '\0';
        }

        /*
         * handle windows \r\n-line breaks
         */
        if(line_end != data_begin && buffer[line_end-1] == '\r')
        {
            buffer[line_end-1] = '\0';
        }

        char *ret = buffer.get() + data_begin;
        data_begin = line_end+1;

        return ret;
    }
};

////////////////////////////////////////////////////////////////////////////
//                           CsvReader Errors                             //
////////////////////////////////////////////////////////////////////////////

namespace error
{

constexpr int max_column_name_length = 63;
constexpr int max_column_content_length = 63;

class extra_column_in_header : public error
{
public:
    void format_error_message() override
    {
        std::stringstream ss;
        ss << "Extra column \"" << get_column_name() << "\" in header of file "
           << "\"" << get_file_name() << "\"";

        error = ss.str();
    }
};

class missing_column_in_header : public error
{
public:
    void format_error_message() override
    {
        std::stringstream ss;
        ss <<  "Missing column \"" << get_column_name() << "\" in header of file "
           << "\"" << get_file_name() << "\"";

        error = ss.str();
    }
};

class duplicated_column_in_header : public error
{
public:
    void format_error_message() override
    {
        std::stringstream ss;
        ss << "Duplicated column \"" << get_column_name() << "\" in header of file "
           << "\"" << get_file_name() << "\"";

        error = ss.str();
    }
};

class header_missing : public error
{
public:
    void format_error_message() override
    {
        std::stringstream ss;
        ss << "Header missing in file \"" << get_file_name() << "\"";

        error = ss.str();
    }
};

class too_few_columns : public error
{
public:
    void format_error_message() override
    {
        std::stringstream ss;
        ss <<  "Too few columns in line " << get_file_line() << " in file "
           << "\"" << get_file_name() << "\"";

        error = ss.str();
    }
};

class too_many_columns : public error
{
public:
    void format_error_message() override
    {
        std::stringstream ss;
        ss << "Too many columns in line " << get_file_line() << " in file "
           << "\"" << get_file_name() << "\"";

        error = ss.str();
    }
};

class escaped_string_not_closed : public error
{
public:
    void format_error_message() override
    {
        std::stringstream ss;
        ss << "Escaped string was not closed in line " << get_file_line()
           << " in file " << get_file_name();

        error = ss.str();
    }
};

class integer_must_be_positive : public error
{
public:
    void format_error_message() override
    {
        std::stringstream ss;
        ss << "The integer " << get_column_content() << " must be positive or 0 "
           << "in column " << get_column_name() << " in file " << get_file_name()
           << " in line " << get_file_line();

        error = ss.str();
    }
};

class no_digit : public error
{
public:
    void format_error_message() override
    {
        std::stringstream ss;
        ss << "The integer " << get_column_content() << " contains an invalid "
           << "digit in column " << get_column_name() << " in file "
           << get_file_name() << " in line " << get_file_line();

        error = ss.str();
    }
};

class integer_overflow : public error
{
public:
    void format_error_message() override
    {
        std::stringstream ss;
        ss << "The integer " << get_column_content() << " overflows in column "
           << get_column_name() << " in file " << get_file_name()
           <<" in line " << get_file_line();

        error = ss.str();
    }
};

class integer_underflow : public error
{
public:
    void format_error_message() override
    {
        std::stringstream ss;
        ss << "The integer " << get_column_content() << " underflows in column "
           << get_column_name() << " in file " << get_file_line()
           << " in line " << get_file_line();

        error = ss.str();
    }
};

class invalid_single_character : public error
{
public:
    void format_error_message() override
    {
        std::stringstream ss;
        ss << "The content " << get_column_content() << " of column "
           << get_column_name() << " in file " << get_file_name()
           << " in line " << get_file_line() << " is not a single character.)";

        error = ss.str();
    }
};

} // end namespace error

////////////////////////////////////////////////////////////////////////////
//                       Quotes and Separators                            //
////////////////////////////////////////////////////////////////////////////

using ignore_column = unsigned int;
static constexpr ignore_column ignore_no_column = 0;
static constexpr ignore_column ignore_extra_column = 1;
static constexpr ignore_column ignore_missing_column = 2;

template<char ... trim_char_list>
class trim_chars
{
private:
    constexpr static bool is_trim_char(char)
    {
        return false;
    }

    template<class ...OtherTrimChars>
    constexpr static bool is_trim_char(
        char c,
        char trim_char,
        OtherTrimChars... other_trim_chars)
    {
        return c == trim_char || is_trim_char(c, other_trim_chars...);
    }

public:
    static void trim(
        char *&str_begin,
        char *&str_end)
    {
        while(str_begin != str_end && is_trim_char(*str_begin, trim_char_list...))
        {
            ++str_begin;
        }

        while(str_begin != str_end && is_trim_char(*(str_end-1), trim_char_list...))
        {
            --str_end;
        }

        *str_end = '\0';
    }
};

class no_comment
{
public:
    static bool is_comment(const char *c)
    {
        (void)c;
        return false;
    }
};

template<char ... comment_start_char_list>
class single_line_comment
{
private:
    constexpr static bool is_comment_start_char(char c)
    {
        (void)c;
        return false;
    }

    template<class ...OtherCommentStartChars>
    constexpr static bool is_comment_start_char(
        char c,
        char comment_start_char,
        OtherCommentStartChars... other_comment_start_chars)
    {
        return c == comment_start_char ||
                    is_comment_start_char(c, other_comment_start_chars...);
    }

public:
    static bool is_comment(const char *line)
    {
        return is_comment_start_char(*line, comment_start_char_list...);
    }
};

class empty_line_comment
{
public:
    static bool is_comment(const char *line)
    {
        if (*line == '\0')
        {
            return true;
        }
        while(*line == ' ' || *line == '\t')
        {
            ++line;
            if(*line == 0)
            {
                return true;
            }
        }
        return false;
    }
};

template<char ... comment_start_char_list>
class single_and_empty_line_comment
{
public:
    static bool is_comment(const char *line)
    {
        return single_line_comment<comment_start_char_list...>::is_comment(line) ||
               empty_line_comment::is_comment(line);
    }
};

template<char sep>
class no_quote_escape
{
public:
    static const char *find_next_column_end(
        const char *col_begin,
        std::shared_ptr<error::error> &err)
    {
        (void)err;
        while(*col_begin != sep && *col_begin != '\0')
        {
            ++col_begin;
        }

        return col_begin;
    }

    static void unescape(char *&a, char *&b)
    {
        (void)a;
        (void)b;
    }
};

template<char sep, char quote>
class double_quote_escape
{
public:
    static const char *find_next_column_end(
        const char *col_begin,
        std::shared_ptr<error::error> &err)
    {
        while(*col_begin != sep && *col_begin != '\0')
        {
            if(*col_begin != quote)
            {
                ++col_begin;
            }
            else
            {
                do {
                    ++col_begin;
                    while(*col_begin != quote)
                    {
                        if(*col_begin == '\0')
                        {
                            err = std::make_shared<error::escaped_string_not_closed>();
                            return nullptr;
                        }
                        ++col_begin;
                    }
                    ++col_begin;
                } while(*col_begin == quote);
            }
        }
        return col_begin;
    }

    static void unescape(
        char *&col_begin,
        char *&col_end)
    {
        if(col_end - col_begin >= 2)
        {
            if(*col_begin == quote && *(col_end-1) == quote)
            {
                ++col_begin;
                --col_end;
                char *out = col_begin;
                for(char*in = col_begin; in!=col_end; ++in)
                {
                    if(*in == quote && (in+1) != col_end && *(in+1) == quote)
                    {
                        ++in;
                    }
                    *out = *in;
                    ++out;
                }
                col_end = out;
                *col_end = '\0';
            }
        }
    }
};

class ignore_overflow
{
public:
    template<class T>
    static void on_overflow(T&){}

    template<class T>
    static void on_underflow(T&){}
};

class set_to_max_on_overflow
{
public:
    template<class T>
    static void on_overflow(T &x)
    {
        /*
         * Using (std::numeric_limits<T>::max) instead of std::numeric_limits<T>::max
         * to make code including windows.h with its max macro happy
         */
        x = (std::numeric_limits<T>::max)();
    }

    template<class T>
    static void on_underflow(T &x)
    {
        x = (std::numeric_limits<T>::min)();
    }
};

namespace detail
{

template<class quote_policy>
bool chop_next_column(
    char *&line,
    char *&col_begin,
    char *&col_end,
    std::shared_ptr<error::error> &err)
{
    if (err)
    {
        return false;
    }

    if (line == nullptr)
    {
        err = std::make_shared<error::internal_error>();
        err->set_error("Internal error: line is null in chop_next_column");
        return false;
    }

    col_begin = line;

    /* The col_begin + (... - col_begin) removes the constness */
    col_end = col_begin + (quote_policy::find_next_column_end(col_begin, err) - col_begin);

    if (err)
    {
        return false;
    }

    if(*col_end == '\0')
    {
        line = nullptr;
    }
    else
    {
        *col_end = '\0';
        line = col_end + 1;
    }

    return true;
}

template<class trim_policy, class quote_policy>
bool parse_line(
   char *line,
   char **sorted_col,
   const std::vector<int> &col_order,
   std::shared_ptr<error::error> &err)
{
    if (err)
    {
        return false;
    }

    for (int i : col_order)
    {
        if(line == nullptr)
        {
            err = std::make_shared<::io::error::too_few_columns>();
            return false;
        }

        char *col_begin;
        char *col_end;

        if (!chop_next_column<quote_policy>(line, col_begin, col_end, err))
        {
            return false;
        }

        if (i != -1)
        {
            trim_policy::trim(col_begin, col_end);
            quote_policy::unescape(col_begin, col_end);
            sorted_col[i] = col_begin;
        }
    }

    if(line != nullptr)
    {
        err = std::make_shared<::io::error::too_many_columns>();
        return false;
    }

    return true;
}

template<unsigned column_count, class trim_policy, class quote_policy>
bool parse_header_line(
    char *line,
    std::vector<int> &col_order,
    const std::string *col_name,
    ignore_column ignore_policy,
    std::shared_ptr<error::error> &err)
{
    if (err)
    {
        return false;
    }

    col_order.clear();

    bool found[column_count];
    std::fill(found, found + column_count, false);
    while(line)
    {
        char *col_begin;
        char *col_end;
        if (!chop_next_column<quote_policy>(line, col_begin, col_end, err))
        {
            return false;
        }

        trim_policy::trim(col_begin, col_end);
        quote_policy::unescape(col_begin, col_end);

        for(unsigned i = 0; i < column_count; ++i)
        {
            if(col_begin == col_name[i])
            {
                if(found[i])
                {
                    err = std::make_shared<error::duplicated_column_in_header>();
                    err->set_column_name(col_begin);
                    return false;
                }

                found[i] = true;
                col_order.push_back(i);
                col_begin = 0;
                break;
            }
        }
        if(col_begin)
        {
            if(ignore_policy & ::io::ignore_extra_column)
            {
                col_order.push_back(-1);
            }
            else
            {
                err = std::make_shared<error::extra_column_in_header>();
                err->set_column_name(col_begin);
                return false;
            }
        }
    }
    if(!(ignore_policy & ::io::ignore_missing_column))
    {
        for(unsigned i = 0; i < column_count; ++i)
        {
            if(!found[i])
            {
                err = std::make_shared<error::missing_column_in_header>();
                err->set_column_name(col_name[i].c_str());
                return false;
            }
        }
    }

    return true;
}

template<class overflow_policy>
bool parse(
    char *col,
    char &x,
    std::shared_ptr<error::error> &err)
{
    if (err)
    {
        return false;
    }

    if(!*col)
    {
        err = std::make_shared<error::invalid_single_character>();
        return false;
    }

    x = *col;
    ++col;

    if(*col)
    {
        err = std::make_shared<error::invalid_single_character>();
        return false;
    }

    return true;
}

template<class overflow_policy>
bool parse(
    char *col,
    std::string &x,
    std::shared_ptr<error::error> &err)
{
    (void)err;
    x = col;
    return true;
}

template<class overflow_policy>
bool parse(
    char *col,
    const char *&x,
    std::shared_ptr<error::error> &err)
{
    (void)err;
    x = col;
    return true;
}

template<class overflow_policy>
bool parse(
    char *col,
    char *&x,
    std::shared_ptr<error::error> &err)
{
    (void)err;
    x = col;
    return true;
}

template<class overflow_policy, class T>
bool parse_unsigned_integer(
    const char *col,
    T &x,
    std::shared_ptr<error::error> &err)
{
    if (err)
    {
        return false;
    }

    if(*col == '-')
    {
        err = std::make_shared<error::integer_must_be_positive>();
        return false;
    }

    x = 0;
    while(*col != '\0')
    {
        if('0' <= *col && *col <= '9')
        {
            T y = *col - '0';
            if(x > ((std::numeric_limits<T>::max)()-y)/10)
            {
                overflow_policy::on_overflow(x);
                return true;
            }
            x = 10*x+y;
        }
        else
        {
            err = std::make_shared<error::no_digit>();
            return false;
        }
        ++col;
    }

    return true;
}

template<class overflow_policy> bool parse(char *col, unsigned char &x, std::shared_ptr<error::error> &err)
    {return parse_unsigned_integer<overflow_policy>(col, x, err);}
template<class overflow_policy> bool parse(char *col, unsigned short &x, std::shared_ptr<error::error> &err)
    {return parse_unsigned_integer<overflow_policy>(col, x, err);}
template<class overflow_policy> bool parse(char *col, unsigned int &x, std::shared_ptr<error::error> &err)
    {return parse_unsigned_integer<overflow_policy>(col, x, err);}
template<class overflow_policy> bool parse(char *col, unsigned long &x, std::shared_ptr<error::error> &err)
    {return parse_unsigned_integer<overflow_policy>(col, x, err);}
template<class overflow_policy> bool parse(char *col, unsigned long long &x, std::shared_ptr<error::error> &err)
    {return parse_unsigned_integer<overflow_policy>(col, x, err);}

template<class overflow_policy, class T>
bool parse_signed_integer(
    const char *col,
    T &x,
    std::shared_ptr<error::error> &err)
{
    if (err)
    {
        return false;
    }

    if(*col == '-')
    {
        ++col;
        x = 0;
        while(*col != '\0')
        {
            if('0' <= *col && *col <= '9')
            {
                T y = *col - '0';
                if(x < ((std::numeric_limits<T>::min)()+y)/10)
                {
                    overflow_policy::on_underflow(x);
                    return true;
                }
                x = 10*x-y;
            }
            else
            {
                err = std::make_shared<error::no_digit>();
                return false;
            }
            ++col;
        }
        return true;
    }
    else if(*col == '+')
    {
        ++col;
    }

    return parse_unsigned_integer<overflow_policy>(col, x, err);
}

template<class overflow_policy> bool parse(char *col, signed char &x, std::shared_ptr<error::error> &err)
    {return parse_signed_integer<overflow_policy>(col, x, err);}
template<class overflow_policy> bool parse(char *col, signed short &x, std::shared_ptr<error::error> &err)
    {return parse_signed_integer<overflow_policy>(col, x, err);}
template<class overflow_policy> bool parse(char *col, signed int &x, std::shared_ptr<error::error> &err)
    {return parse_signed_integer<overflow_policy>(col, x, err);}
template<class overflow_policy> bool parse(char *col, signed long &x, std::shared_ptr<error::error> &err)
    {return parse_signed_integer<overflow_policy>(col, x, err);}
template<class overflow_policy> bool parse(char *col, signed long long &x, std::shared_ptr<error::error> &err)
    {return parse_signed_integer<overflow_policy>(col, x, err);}

template<class T>
bool parse_float(
    const char *col,
    T &x,
    std::shared_ptr<error::error> &err)
{
    if (err)
    {
        return false;
    }

    bool is_neg = false;
    if(*col == '-')
    {
        is_neg = true;
        ++col;
    }
    else if(*col == '+')
    {
        ++col;
    }

    x = 0;
    while('0' <= *col && *col <= '9')
    {
        int y = *col - '0';
        x *= 10;
        x += y;
        ++col;
    }

    if(*col == '.'|| *col == ',')
    {
        ++col;
        T pos = 1;
        while('0' <= *col && *col <= '9')
        {
            pos /= 10;
            int y = *col - '0';
            ++col;
            x += y*pos;
        }
    }

    if(*col == 'e' || *col == 'E')
    {
        ++col;
        int e;

        if (!parse_signed_integer<set_to_max_on_overflow>(col, e, err))
        {
            return false;
        }

        if(e != 0)
        {
            T base;
            if(e < 0)
            {
                base = T(0.1);
                e = -e;
            }
            else
            {
                base = T(10);
            }

            while(e != 1)
            {
                if((e & 1) == 0)
                {
                    base = base*base;
                    e >>= 1;
                }
                else
                {
                    x *= base;
                    --e;
                }
            }
            x *= base;
        }
    }
    else
    {
        if(*col != '\0')
        {
            err = std::make_shared<error::no_digit>();
            return false;
        }
    }

    if(is_neg)
    {
        x = -x;
    }

    return true;
}

template<class overflow_policy> bool parse(char *col, float &x, std::shared_ptr<error::error> &err) { return parse_float(col, x, err); }
template<class overflow_policy> bool parse(char *col, double &x, std::shared_ptr<error::error> &err) { return parse_float(col, x, err); }
template<class overflow_policy> bool parse(char *col, long double &x, std::shared_ptr<error::error> &err) { return parse_float(col, x, err); }

template<class overflow_policy, class T>
void parse(
    char *col,
    T &x,
    std::shared_ptr<error::error> &err)
{
    /* Mute unused variable compiler warning */
    (void)col;
    (void)x;
    (void)err;

    /*
     * GCC evalutes "false" when reading the template and
     * "sizeof(T)!=sizeof(T)" only when instantiating it. This is why
     * this strange construct is used.
     */
    static_assert(
        sizeof(T)!=sizeof(T),
        "Can not parse this type. Only buildin integrals, floats, char, char*, "
        "const char* and std::string are supported");
}


} // end namespace detail

////////////////////////////////////////////////////////////////////////////
//                               CsvReader                                //
////////////////////////////////////////////////////////////////////////////

/*
 * Time to put it all together.
 */

template
<
    unsigned column_count,
    class trim_policy = trim_chars<' ', '\t'>,
    class quote_policy = no_quote_escape<','>,
    class overflow_policy = set_to_max_on_overflow,
    class comment_policy = no_comment
>
class CSVReader
{
private:
    LineReader in;

    char*row[column_count];
    std::string column_names[column_count];
    std::vector<int> col_order;
    bool valid;

    template<class ...ColNames>
    void set_column_names(
        std::string s,
        ColNames... cols)
    {
        column_names[column_count - sizeof...(ColNames)-1] = std::move(s);
        set_column_names(std::forward<ColNames>(cols)...);
    }

    void set_column_names(){}

public:
    CSVReader() = delete;
    CSVReader(const CSVReader&) = delete;
    CSVReader&operator=(const CSVReader&);

    template<class ...Args>
    explicit CSVReader(
        std::shared_ptr<error::error> &err,
        Args&&...args):
            in(err, std::forward<Args>(args)...)
    {
        if (err)
        {
            err->format_error_message();
            return;
        }

        std::fill(row, row+column_count, nullptr);
        col_order.resize(column_count);

        for(unsigned i=0; i<column_count; ++i)
        {
            col_order[i] = i;
        }

        for(unsigned i=1; i<=column_count; ++i)
        {
            column_names[i-1] = "col"+std::to_string(i);
        }
    }

    char *next_line(std::shared_ptr<error::error> &err)
    {
        if (err)
        {
            return nullptr;
        }

        return in.next_line(err);
    }

    template<class ...ColNames>
    bool read_header(
        ignore_column ignore_policy,
        std::shared_ptr<error::error> &err,
        ColNames...cols)
    {
        static_assert(sizeof...(ColNames)>=column_count, "not enough column names specified");
        static_assert(sizeof...(ColNames)<=column_count, "too many column names specified");

        if (err)
        {
            return false;
        }

        set_column_names(std::forward<ColNames>(cols)...);
        char *line;
        do
        {
            line = in.next_line(err);
            if (err)
            {
                return false;
            }
            if(!line)
            {
                err = std::make_shared<error::header_missing>();
                err->set_file_name(get_truncated_file_name());
                err->format_error_message();
                return false;
            }
        }while(comment_policy::is_comment(line));

        bool success = detail::parse_header_line<column_count, trim_policy, quote_policy>(
            line, col_order, column_names, ignore_policy, err);

        if (!success)
        {
            err->set_file_name(get_truncated_file_name());
            err->format_error_message();
        }

        return success;
    }

    template<class ...ColNames>
    bool set_header(ColNames...cols)
    {
        static_assert(
            sizeof...(ColNames)>=column_count,
            "not enough column names specified");

        static_assert(
            sizeof...(ColNames)<=column_count,
            "too many column names specified");

        set_column_names(std::forward<ColNames>(cols)...);
        std::fill(row, row+column_count, nullptr);
        col_order.resize(column_count);
        for(unsigned i=0; i<column_count; ++i)
        {
            col_order[i] = i;
        }

        return true;
    }

    bool has_column(const std::string &name) const
    {
        return col_order.end() != std::find(
                col_order.begin(), col_order.end(),
                        std::find(std::begin(column_names), std::end(column_names), name)
                - std::begin(column_names));
    }

    void set_file_name(const std::string &file_name)
    {
        in.set_file_name(file_name);
    }

    void set_file_name(const char *file_name)
    {
        in.set_file_name(file_name);
    }


    const char *get_truncated_file_name() const
    {
        return in.get_truncated_file_name();
    }

    void set_file_line(unsigned file_line)
    {
        in.set_file_line(file_line);
    }

    unsigned get_file_line() const
    {
        return in.get_file_line();
    }

private:
    bool parse_helper(
        std::size_t,
        std::shared_ptr<error::error> &err)
    {
        if (err)
        {
            err->format_error_message();
            return false;
        }

        return true;
    }

    template<class T, class ...ColType>
    bool parse_helper(
        std::size_t r,
        std::shared_ptr<error::error> &err,
        T &t,
        ColType&...cols)
    {
        if (err)
        {
            return false;
        }

        if(row[r])
        {
            if (!::io::detail::parse<overflow_policy>(row[r], t, err))
            {
                err->set_column_content(row[r]);
                err->set_column_name(column_names[r].c_str());
                return false;
            }
        }
        return parse_helper(r+1, err, cols...);
    }

public:
    template<class ...ColType>
    bool read_row(
        std::shared_ptr<error::error> &err,
        ColType& ...cols)
    {
        if (err)
        {
            return false;
        }

        static_assert(
            sizeof...(ColType)>=column_count,
            "not enough columns specified");

        static_assert(
            sizeof...(ColType)<=column_count,
            "too many columns specified");

        char *line;
        do{
            line = in.next_line(err);
            if(!line)
            {
                return false;
            }
            if (err)
            {
                err->set_file_name(in.get_truncated_file_name());
                err->set_file_line(in.get_file_line());
                err->format_error_message();
                return false;
            }
        }while(comment_policy::is_comment(line));

        if (!detail::parse_line<trim_policy, quote_policy>(line, row, col_order, err))
        {
            err->set_file_name(in.get_truncated_file_name());
            err->set_file_line(in.get_file_line());
            err->format_error_message();
            return false;
        }

        parse_helper(0, err, cols...);
        if (err)
        {
            err->set_file_name(in.get_truncated_file_name());
            err->set_file_line(in.get_file_line());
            err->format_error_message();
            return false;
        }

        return true;
    }
};

} // end namespace io

#endif // CSV_H